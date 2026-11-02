#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "BandMap.h"
#include "LoudnessMeter.h"

namespace meridian
{

/** Long-term average spectrum on the shared band layout (tonal balance). */
class TonalAnalyzer
{
public:
    static constexpr int numBands = BandMap::numBands;

    void prepare (double sampleRate);
    void push (const float* const* channels, int numChannels, int numSamples) noexcept;
    void requestReset() noexcept { resetRequested.store (true); }
    void resetNow() noexcept;

    std::array<std::atomic<float>, numBands> averageDb {};
    std::atomic<int> frames { 0 };
    const BandMap& getBandMap() const noexcept { return map; }

private:
    void analyseFrame() noexcept;

    int order = 12, size = 4096, hop = 2048;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, fifo, buf, power;
    int fifoPos = 0, hopCount = 0;
    BandMap map;
    std::array<double, numBands> accum {};
    std::array<float, numBands> energy {};
    int frameCount = 0;
    std::atomic<bool> resetRequested { false };
};

/** A "well-mixed" reference song loaded from disk: analysed for loudness and
    tonal balance in the background, and playable (level-matched) for A/B. */
class ReferenceTrack
{
public:
    struct Analysis
    {
        bool  valid = false;
        juce::String name, error;
        double durationSec = 0.0, fileSampleRate = 0.0;
        float integrated = LoudnessMeter::silence, range = 0.0f, truePeak = LoudnessMeter::silence;
        float shortTermMax = LoudnessMeter::silence, momentaryMax = LoudnessMeter::silence;
        std::array<float, BandMap::numBands> spectrumDb {};
    };

    ReferenceTrack();
    ~ReferenceTrack();

    void prepare (double hostSampleRate, int numChannels);
    void load (const juce::File& file);   // asynchronous
    void clear();

    bool isLoading() const noexcept { return loading.load(); }
    Analysis getAnalysis() const;
    juce::File getFile() const;
    int getVersion() const noexcept { return version.load(); }

    /** Audio thread: overwrite buffer with reference audio at the given gain. */
    void render (juce::AudioBuffer<float>& buffer, float gain) noexcept;
    void restart() noexcept { restartRequested.store (true); }
    double getPlaybackPositionSec() const noexcept;

    juce::AudioFormatManager& getFormatManager() noexcept { return formats; }

    /** Integrated loudness of the loaded reference (safe to read on the audio thread). */
    std::atomic<float> integratedLufs { LoudnessMeter::silence };

private:
    class Loader;
    void runLoad (const juce::File& file);
    void resampleForHost();

    juce::AudioFormatManager formats;
    std::unique_ptr<Loader> loader;

    mutable juce::CriticalSection analysisLock;
    Analysis analysis;
    juce::File currentFile;

    juce::AudioBuffer<float> original;       // at file rate (loader thread / message thread only)
    double originalRate = 0.0;

    juce::SpinLock playLock;
    juce::AudioBuffer<float> playback;       // at host rate
    juce::int64 playPos = 0;

    double hostRate = 48000.0;
    int hostChannels = 2;
    std::atomic<bool> loading { false }, restartRequested { false };
    std::atomic<int> version { 0 };
    std::atomic<double> positionSec { 0.0 };
};

} // namespace meridian
