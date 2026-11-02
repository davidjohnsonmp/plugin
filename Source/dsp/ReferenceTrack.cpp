#include "ReferenceTrack.h"

namespace meridian
{

// =============================================================================
// TonalAnalyzer
// =============================================================================
void TonalAnalyzer::prepare (double sampleRate)
{
    order = sampleRate > 66000.0 ? 13 : 12;
    size = 1 << order;
    hop = size / 2;
    fft = std::make_unique<juce::dsp::FFT> (order);
    window.resize ((size_t) size);
    for (int n = 0; n < size; ++n)
        window[(size_t) n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) size);
    fifo.assign ((size_t) size, 0.0f);
    buf.assign ((size_t) size * 2, 0.0f);
    power.assign ((size_t) size / 2 + 1, 0.0f);
    map.build (sampleRate, size);
    resetNow();
}

void TonalAnalyzer::resetNow() noexcept
{
    std::fill (fifo.begin(), fifo.end(), 0.0f);
    fifoPos = hopCount = 0;
    accum.fill (0.0);
    frameCount = 0;
    frames.store (0);
    for (auto& a : averageDb) a.store (-120.0f);
}

void TonalAnalyzer::push (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (resetRequested.exchange (false))
        resetNow();
    if (numChannels <= 0) return;

    const float scale = 1.0f / (float) numChannels;
    for (int i = 0; i < numSamples; ++i)
    {
        float m = 0.0f;
        for (int c = 0; c < numChannels; ++c) m += channels[c][i];
        fifo[(size_t) fifoPos] = m * scale;
        fifoPos = (fifoPos + 1) & (size - 1);
        if (++hopCount >= hop)
        {
            hopCount = 0;
            analyseFrame();
        }
    }
}

void TonalAnalyzer::analyseFrame() noexcept
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    double frameEnergy = 0.0;
    for (int n = 0; n < size; ++n)
    {
        const float v = fifo[(size_t) ((fifoPos + n) & (size - 1))];
        buf[(size_t) n] = v * window[(size_t) n];
        frameEnergy += (double) v * v;
    }
    // ignore silent frames so gaps between songs do not skew the average
    if (frameEnergy / size < 1.0e-8) return;

    fft->performFrequencyOnlyForwardTransform (buf.data(), true);
    const float norm = 2.0f / ((float) size * (0.375f * (float) size));
    for (int k = 0; k <= size / 2; ++k)
        power[(size_t) k] = buf[(size_t) k] * buf[(size_t) k] * norm;

    map.bandEnergies (power.data(), energy.data());
    ++frameCount;
    for (int b = 0; b < numBands; ++b)
    {
        accum[(size_t) b] += energy[(size_t) b];
        averageDb[(size_t) b].store ((float) (10.0 * std::log10 (accum[(size_t) b] / frameCount + 1.0e-12)),
                                     std::memory_order_relaxed);
    }
    frames.store (frameCount);
}

// =============================================================================
// ReferenceTrack
// =============================================================================
class ReferenceTrack::Loader : public juce::Thread
{
public:
    Loader (ReferenceTrack& o, juce::File f) : juce::Thread ("Meridian reference loader"), owner (o), file (std::move (f)) {}
    void run() override { owner.runLoad (file); }

private:
    ReferenceTrack& owner;
    juce::File file;
};

ReferenceTrack::ReferenceTrack()
{
    formats.registerBasicFormats();
}

ReferenceTrack::~ReferenceTrack()
{
    if (loader) loader->stopThread (10000);
}

void ReferenceTrack::prepare (double hostSampleRate, int numChannels)
{
    const bool changed = std::abs (hostSampleRate - hostRate) > 0.5 || numChannels != hostChannels;
    hostRate = hostSampleRate;
    hostChannels = std::max (1, numChannels);
    if (changed && ! loading.load() && original.getNumSamples() > 0)
        resampleForHost();
}

void ReferenceTrack::load (const juce::File& file)
{
    if (loader) loader->stopThread (10000);
    loading.store (true);
    loader = std::make_unique<Loader> (*this, file);
    loader->startThread (juce::Thread::Priority::low);
}

void ReferenceTrack::clear()
{
    if (loader) loader->stopThread (10000);
    juce::AudioBuffer<float> old;
    {
        const juce::SpinLock::ScopedLockType sl (playLock);
        std::swap (old, playback);
        playPos = 0;
    }
    {
        const juce::ScopedLock sl (analysisLock);
        analysis = {};
        currentFile = juce::File();
    }
    integratedLufs.store (LoudnessMeter::silence);
    original.setSize (0, 0);
    ++version;
}

ReferenceTrack::Analysis ReferenceTrack::getAnalysis() const
{
    const juce::ScopedLock sl (analysisLock);
    return analysis;
}

juce::File ReferenceTrack::getFile() const
{
    const juce::ScopedLock sl (analysisLock);
    return currentFile;
}

double ReferenceTrack::getPlaybackPositionSec() const noexcept { return positionSec.load(); }

void ReferenceTrack::runLoad (const juce::File& file)
{
    Analysis result;
    result.name = file.getFileName();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        result.error = "Could not open this file. Try WAV, AIFF, FLAC, MP3 or M4A.";
        const juce::ScopedLock sl (analysisLock);
        analysis = result;
        loading.store (false);
        ++version;
        return;
    }

    const double rate = reader->sampleRate;
    const int chans = (int) juce::jlimit (1u, 2u, reader->numChannels);
    const auto maxSamples = (juce::int64) (rate * 60.0 * 12.0); // up to 12 minutes
    const auto total = std::min (reader->lengthInSamples, maxSamples);

    juce::AudioBuffer<float> audio (chans, (int) total);
    reader->read (&audio, 0, (int) total, 0, true, chans > 1);

    // --- loudness analysis at the file's own sample rate
    LoudnessMeter meter;
    meter.prepare (rate, chans);
    TonalAnalyzer tonal;
    tonal.prepare (rate);

    const int chunk = 4096;
    for (juce::int64 pos = 0; pos < total; pos += chunk)
    {
        if (juce::Thread::currentThreadShouldExit()) { loading.store (false); return; }
        const int len = (int) std::min ((juce::int64) chunk, total - pos);
        const float* ptrs[2] = { audio.getReadPointer (0, (int) pos), audio.getReadPointer (chans - 1, (int) pos) };
        meter.process (ptrs, chans, len);
        tonal.push (ptrs, chans, len);
    }

    result.valid = true;
    result.durationSec = (double) total / rate;
    result.fileSampleRate = rate;
    result.integrated = meter.integrated.load();
    result.range = meter.loudnessRange.load();
    result.truePeak = meter.truePeakMax.load();
    result.shortTermMax = meter.shortTermMax.load();
    result.momentaryMax = meter.momentaryMax.load();
    for (int b = 0; b < BandMap::numBands; ++b)
        result.spectrumDb[(size_t) b] = tonal.averageDb[(size_t) b].load();

    original = std::move (audio);
    originalRate = rate;
    resampleForHost();

    {
        const juce::ScopedLock sl (analysisLock);
        analysis = result;
        currentFile = file;
    }
    integratedLufs.store (result.integrated);
    loading.store (false);
    ++version;
}

void ReferenceTrack::resampleForHost()
{
    if (original.getNumSamples() == 0 || originalRate <= 0.0) return;

    const double ratio = originalRate / hostRate;
    const int outLen = (int) std::floor ((double) original.getNumSamples() / ratio) - 4;
    if (outLen <= 0) return;

    juce::AudioBuffer<float> out (hostChannels, outLen);
    for (int c = 0; c < hostChannels; ++c)
    {
        const int src = std::min (c, original.getNumChannels() - 1);
        if (std::abs (ratio - 1.0) < 1.0e-9)
        {
            out.copyFrom (c, 0, original, src, 0, outLen);
        }
        else
        {
            juce::WindowedSincInterpolator interp;
            interp.process (ratio, original.getReadPointer (src), out.getWritePointer (c), outLen);
        }
    }

    {
        const juce::SpinLock::ScopedLockType sl (playLock);
        std::swap (out, playback);
        playPos = 0;
    }
    // previous buffer (now in `out`) is freed here, outside the lock
}

void ReferenceTrack::render (juce::AudioBuffer<float>& buffer, float gain) noexcept
{
    const int n = buffer.getNumSamples();
    const juce::SpinLock::ScopedTryLockType tl (playLock);
    if (! tl.isLocked() || playback.getNumSamples() == 0)
    {
        buffer.clear();
        return;
    }

    if (restartRequested.exchange (false)) playPos = 0;

    const int len = playback.getNumSamples();
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        const int src = std::min (c, playback.getNumChannels() - 1);
        const float* p = playback.getReadPointer (src);
        float* d = buffer.getWritePointer (c);
        juce::int64 pos = playPos;
        for (int i = 0; i < n; ++i)
        {
            d[i] = p[pos] * gain;
            if (++pos >= len) pos = 0;
        }
    }
    playPos = (playPos + n) % len;
    positionSec.store ((double) playPos / hostRate);
}

} // namespace meridian
