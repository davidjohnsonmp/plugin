// Offline test runner for Meridian Rack.
// Checks loudness accuracy against EBU test signals, latency alignment,
// unity reconstruction of the adaptive EQ, limiter ceiling, reference-track
// analysis, and renders every editor page to PNG.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

using namespace meridian;

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("[%s] %s\n", ok ? " OK " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

struct Pink
{
    juce::Random rng { 1234 };
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    float next()
    {
        const float w = rng.nextFloat() * 2.0f - 1.0f;
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        return out * 0.11f;
    }
};

// A crude "mix": pink noise + kick-like thumps + a bright tone, scaled to an RMS target.
static juce::AudioBuffer<float> makeMix (double sr, double seconds, float rmsDb)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (2, n);
    Pink pl, pr; pr.rng.setSeed (99);
    // music-like spectral tilt: highs fall off faster than pink noise
    Biquad tl, tr;
    tl.c = tr.c = BiquadCoefs::highShelf (sr, 2500.0, 0.7, -7.0);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double beat = std::fmod (t, 0.5);
        const float kick = (float) (std::exp (-beat * 18.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 + 80.0 * std::exp (-beat * 30.0)) * beat)) * 0.9f;
        const float tone = 0.04f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 3200.0 * t);
        b.setSample (0, i, tl.process (pl.next()) + kick + tone);
        b.setSample (1, i, tr.process (pr.next()) + kick + tone * 0.8f);
    }
    double e = 0.0;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) e += b.getSample (c, i) * b.getSample (c, i);
    const float rms = (float) std::sqrt (e / (2.0 * n));
    b.applyGain (juce::Decibels::decibelsToGain (rmsDb) / rms);
    return b;
}

static void runProcessor (MeridianProcessor& proc, juce::AudioBuffer<float>& audio, int block)
{
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> io (4, block);
    for (int pos = 0; pos < audio.getNumSamples(); pos += block)
    {
        const int len = std::min (block, audio.getNumSamples() - pos);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 4, 0, len);
        view.clear();
        for (int c = 0; c < 2; ++c) view.copyFrom (c, 0, audio, c, pos, len);
        proc.processBlock (view, midi);
        for (int c = 0; c < 2; ++c) audio.copyFrom (c, pos, view, c, 0, len);
    }
}

static void setParam (MeridianProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

// ---------------------------------------------------------------------------
static void testLoudness()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        for (float level : { -23.0f, -33.0f })
        {
            LoudnessMeter m;
            m.prepare (sr, 2);
            const int n = (int) (sr * 20.0);
            std::vector<float> l ((size_t) n), r ((size_t) n);
            const float a = juce::Decibels::decibelsToGain (level);
            for (int i = 0; i < n; ++i)
                l[(size_t) i] = r[(size_t) i] = a * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * i / sr);
            const float* ptr[2] = { l.data(), r.data() };
            for (int pos = 0; pos < n; pos += 512)
            {
                const float* pp[2] = { ptr[0] + pos, ptr[1] + pos };
                m.process (pp, 2, std::min (512, n - pos));
            }
            const float I = m.integrated.load(), M = m.momentary.load(), S = m.shortTerm.load();
            check (std::abs (I - level) < 0.1f && std::abs (M - level) < 0.1f && std::abs (S - level) < 0.1f,
                   juce::String::formatted ("Loudness %.0f Hz sine %.0f dBFS: I=%.2f M=%.2f S=%.2f LUFS", sr, level, I, M, S));
        }
    }

    // EBU Tech 3341 case 2-ish: alternating -36 / -23 dBFS (10 s each) -> integrated -23 (gating removes quiet part? no: relative gate)
    {
        const double sr = 48000.0;
        LoudnessMeter m; m.prepare (sr, 2);
        auto feed = [&] (float db, double secs)
        {
            const int n = (int) (sr * secs);
            std::vector<float> x ((size_t) n);
            for (int i = 0; i < n; ++i)
                x[(size_t) i] = juce::Decibels::decibelsToGain (db) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr);
            for (int pos = 0; pos < n; pos += 480)
            {
                const float* pp[2] = { x.data() + pos, x.data() + pos };
                m.process (pp, 2, std::min (480, n - pos));
            }
        };
        feed (-36.0f, 10.0); feed (-23.0f, 60.0); feed (-36.0f, 10.0);
        check (std::abs (m.integrated.load() + 23.0f) < 0.15f,
               juce::String::formatted ("Gated integrated (-36/-23/-36 dBFS): %.2f LUFS (expect -23.0)", m.integrated.load()));
        // EBU 3342 case 1: -20 then -30 LUFS, 20 s each -> LRA 10
        LoudnessMeter r; r.prepare (sr, 2);
        auto feedR = [&] (float db, double secs)
        {
            const int n = (int) (sr * secs);
            std::vector<float> x ((size_t) n);
            for (int i = 0; i < n; ++i)
                x[(size_t) i] = juce::Decibels::decibelsToGain (db) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr);
            for (int pos = 0; pos < n; pos += 480)
            {
                const float* pp[2] = { x.data() + pos, x.data() + pos };
                r.process (pp, 2, std::min (480, n - pos));
            }
        };
        feedR (-20.0f, 20.0); feedR (-30.0f, 20.0);
        check (std::abs (r.loudnessRange.load() - 10.0f) < 1.0f,
               juce::String::formatted ("Loudness range (-20 then -30 LUFS): %.1f LU (expect 10 +/- 1)", r.loudnessRange.load()));
    }

    // True peak: fs/4 sine at 45 degrees -> samples at -3 dB below the real peak
    {
        const double sr = 48000.0;
        LoudnessMeter m; m.prepare (sr, 1);
        const int n = 48000;
        std::vector<float> x ((size_t) n);
        const float a = juce::Decibels::decibelsToGain (-6.0f);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = a * (float) std::sin (juce::MathConstants<double>::halfPi * i + juce::MathConstants<double>::pi / 4.0);
        const float* pp[1] = { x.data() };
        m.process (pp, 1, n);
        check (std::abs (m.truePeakMax.load() + 6.0f) < 0.6f,
               juce::String::formatted ("True peak of -6 dBFS sine sampled off-peak: %.2f dBTP (sample peak -9.0)", m.truePeakMax.load()));
    }
}

static void testUnmaskerUnity()
{
    for (int q = 0; q < 3; ++q)
    {
        SpectralUnmasker u;
        const double sr = 48000.0;
        u.prepare (sr, 2, (SpectralUnmasker::Quality) q);
        const int N = u.getLatencySamples();
        auto in = makeMix (sr, 2.0, -18.0f);
        auto out = in;
        SpectralUnmasker::Settings s;  // recover = tame = boost = 0
        for (int pos = 0; pos < out.getNumSamples(); pos += 256)
        {
            juce::AudioBuffer<float> v (out.getArrayOfWritePointers(), 2, pos, std::min (256, out.getNumSamples() - pos));
            u.process (v, nullptr, s);
        }
        double err = 0.0, ref = 0.0;
        for (int i = N + 4096; i < in.getNumSamples(); ++i)
        {
            const float d = out.getSample (0, i) - in.getSample (0, i - N);
            err += d * d; ref += in.getSample (0, i - N) * in.getSample (0, i - N);
        }
        const double snr = 10.0 * std::log10 (ref / (err + 1e-20));
        check (snr > 60.0, juce::String::formatted ("Unmask EQ at zero settings reconstructs input (quality %d, latency %d): %.1f dB SNR", q, N, snr));
    }
}

static void testUnmaskerAction()
{
    const double sr = 48000.0;
    SpectralUnmasker u;
    u.prepare (sr, 2, SpectralUnmasker::Quality::normal);
    auto in = makeMix (sr, 8.0, -16.0f);
    auto out = in;
    SpectralUnmasker::Settings s;
    s.recover = 1.0f; s.tame = 1.0f;
    for (int pos = 0; pos < out.getNumSamples(); pos += 512)
    {
        juce::AudioBuffer<float> v (out.getArrayOfWritePointers(), 2, pos, std::min (512, out.getNumSamples() - pos));
        u.process (v, nullptr, s);
    }
    bool finite = true;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < out.getNumSamples(); ++i) finite &= std::isfinite (out.getSample (c, i));

    LoudnessMeter mi, mo; mi.prepare (sr, 2); mo.prepare (sr, 2);
    mi.process (in.getArrayOfReadPointers(), 2, in.getNumSamples());
    mo.process (out.getArrayOfReadPointers(), 2, out.getNumSamples());

    float minG = 100, maxG = -100;
    for (auto& g : u.displayCurveDb) { minG = std::min (minG, g.load()); maxG = std::max (maxG, g.load()); }
    check (finite, "Unmask EQ output is finite at Recover/Tame 100%");
    check (maxG - minG > 1.0f, juce::String::formatted ("Unmask EQ curve is active: %.1f .. %.1f dB (recover meter %.2f, tame meter %.2f, bias %.2f)",
                                                       minG, maxG, u.recoverMeterDb.load(), u.tameMeterDb.load(), u.biasMeter.load()));
    check (std::abs (mo.integrated.load() - mi.integrated.load()) < 1.5f,
           juce::String::formatted ("Unmask EQ keeps loudness: in %.2f LUFS, out %.2f LUFS", mi.integrated.load(), mo.integrated.load()));

    // Boost changes balance but keeps loudness
    SpectralUnmasker u2; u2.prepare (sr, 2, SpectralUnmasker::Quality::normal);
    auto out2 = in;
    SpectralUnmasker::Settings s2; s2.boostDb = 20.0f;
    for (int pos = 0; pos < out2.getNumSamples(); pos += 512)
    {
        juce::AudioBuffer<float> v (out2.getArrayOfWritePointers(), 2, pos, std::min (512, out2.getNumSamples() - pos));
        u2.process (v, nullptr, s2);
    }
    LoudnessMeter m2; m2.prepare (sr, 2);
    m2.process (out2.getArrayOfReadPointers(), 2, out2.getNumSamples());
    const float low = u2.displayCurveDb[2].load(), mid = u2.displayCurveDb[40].load();
    check (low - mid > 3.0f && std::abs (m2.integrated.load() - mi.integrated.load()) < 2.0f,
           juce::String::formatted ("Boost +20 dB lifts bass vs mids (%.1f vs %.1f dB), loudness %.2f vs %.2f LUFS",
                                    low, mid, m2.integrated.load(), mi.integrated.load()));
}

static void testProcessorLatency()
{
    MeridianProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    setParam (proc, ID::umOn, 0); setParam (proc, ID::chOn, 0); setParam (proc, ID::limOn, 0);

    // let module fades settle
    juce::AudioBuffer<float> warm (2, 48000); warm.clear();
    runProcessor (proc, warm, 512);

    const int lat = proc.getLatencySamples();
    juce::AudioBuffer<float> imp (2, lat + 4096); imp.clear();
    imp.setSample (0, 100, 1.0f); imp.setSample (1, 100, 1.0f);
    runProcessor (proc, imp, 512);
    int peakAt = -1; float peakV = 0;
    for (int i = 0; i < imp.getNumSamples(); ++i)
        if (std::abs (imp.getSample (0, i)) > peakV) { peakV = std::abs (imp.getSample (0, i)); peakAt = i; }
    check (peakAt == 100 + lat && std::abs (peakV - 1.0f) < 0.02f,
           juce::String::formatted ("Reported latency %d matches the signal path (impulse at %d, value %.3f)", lat, peakAt - 100, peakV));

    // bypass must line up exactly too
    setParam (proc, ID::umOn, 1); setParam (proc, ID::chOn, 1); setParam (proc, ID::limOn, 1);
    setParam (proc, ID::bypass, 1); setParam (proc, ID::matchBypass, 0);
    runProcessor (proc, warm, 512);
    juce::AudioBuffer<float> imp2 (2, lat + 4096); imp2.clear();
    imp2.setSample (0, 100, 0.5f);
    runProcessor (proc, imp2, 512);
    check (std::abs (imp2.getSample (0, 100 + lat) - 0.5f) < 1.0e-4f,
           juce::String::formatted ("Bypass is latency-aligned (sample at +latency = %.4f)", imp2.getSample (0, 100 + lat)));
}

static void testFullChain()
{
    const double sr = 48000.0;
    MeridianProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);
    proc.presets.loadPreset (2);  // Bright Pop Master
    auto mix = makeMix (sr, 12.0, -14.0f);
    LoudnessMeter mi; mi.prepare (sr, 2); mi.process (mix.getArrayOfReadPointers(), 2, mix.getNumSamples());
    runProcessor (proc, mix, 512);

    bool finite = true;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < mix.getNumSamples(); ++i) finite &= std::isfinite (mix.getSample (c, i));
    check (finite, "Full chain output is finite (Bright Pop Master preset)");
    const float tp = proc.outputLoudness.truePeakMax.load();
    const float ceil = proc.apvts.getRawParameterValue (ID::limCeiling)->load();
    check (tp <= ceil + 0.3f, juce::String::formatted ("Limiter holds the ceiling: true peak %.2f dBTP (ceiling %.1f)", tp, ceil));
    std::printf ("       input %.1f LUFS -> output %.1f LUFS, LRA %.1f LU\n", mi.integrated.load(),
                 proc.outputLoudness.integrated.load(), proc.outputLoudness.loudnessRange.load());
    std::printf ("       character GR low/mid/high: %.1f / %.1f / %.1f dB, input RMS %.1f dBFS\n",
                 proc.character.grDb[0].load(), proc.character.grDb[1].load(), proc.character.grDb[2].load(),
                 proc.character.inputRmsDb.load());
}

static void testCharacterCalibration()
{
    const double sr = 48000.0;
    for (float rms : { -18.0f, -16.0f, -13.5f, -11.0f })
    {
        CharacterChain ch;
        ch.prepare (sr, 512, 2, 1);
        auto mix = makeMix (sr, 6.0, rms);
        CharacterChain::Settings s;
        float sum[3] = { 0, 0, 0 }; int cnt = 0;
        for (int pos = 0; pos < mix.getNumSamples(); pos += 512)
        {
            juce::AudioBuffer<float> v (mix.getArrayOfWritePointers(), 2, pos, std::min (512, mix.getNumSamples() - pos));
            ch.process (v, nullptr, s);
            if (pos > (int) sr * 2) { for (int b = 0; b < 3; ++b) sum[b] += ch.grDb[(size_t) b].load(); ++cnt; }
        }
        std::printf ("       calibration: mix RMS %.0f dBFS (meter %.1f) -> GR low %.1f, mid %.1f, high %.1f dB\n",
                     rms, ch.inputRmsDb.load(), sum[0] / cnt, sum[1] / cnt, sum[2] / cnt);
    }
}

static void testReference()
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meridian_ref_test.wav");
    {
        const double sr = 44100.0;
        juce::AudioBuffer<float> b (2, (int) (sr * 10));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = juce::Decibels::decibelsToGain (-18.0f) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * i / sr);
            b.setSample (0, i, v); b.setSample (1, i, v);
        }
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
        auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
        if (writer != nullptr) writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    ReferenceTrack ref;
    ref.prepare (48000.0, 2);
    ref.load (file);
    for (int i = 0; i < 200 && ref.isLoading(); ++i) juce::Thread::sleep (50);
    const auto a = ref.getAnalysis();
    check (a.valid && std::abs (a.integrated + 18.0f) < 0.15f,
           juce::String::formatted ("Reference file analysed: %s, %.1f s, I=%.2f LUFS (expect -18), TP %.2f dBTP",
                                    a.name.toRawUTF8(), a.durationSec, a.integrated, a.truePeak));
    juce::AudioBuffer<float> out (2, 4800);
    ref.render (out, 1.0f);
    check (out.getMagnitude (0, 0, 4800) > 0.1f, "Reference playback renders audio at host rate");
}

static void renderEditorSnapshots (const juce::File& outDir)
{
    const double sr = 48000.0;
    MeridianProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);
    proc.presets.loadPreset (1);   // Clear Mix Polish

    const auto refFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Reference Song - Final Master.wav");
    {
        auto refAudio = makeMix (44100.0, 20.0, -10.0f);
        refFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (refFile.createOutputStream());
        auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (44100.0).withNumChannels (2).withBitsPerSample (24));
        if (writer) writer->writeFromAudioSampleBuffer (refAudio, 0, refAudio.getNumSamples());
    }
    proc.reference.load (refFile);
    for (int i = 0; i < 200 && proc.reference.isLoading(); ++i) juce::Thread::sleep (50);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<MeridianEditor*> (ed.get());
    auto& content = editor->getContent();

    auto mix = makeMix (sr, 30.0, -14.0f);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> io (4, 512);
    int ticks = 0;
    for (int pos = 0; pos + 512 <= mix.getNumSamples(); pos += 512)
    {
        io.clear();
        for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, mix, c, pos, 512);
        proc.processBlock (io, midi);
        if ((pos / 512) % 3 == 0) { content.refreshNow(); ++ticks; }
    }

    const char* names[] = { "1_unmask", "2_character", "3_limiter", "4_loudness" };
    for (int i = 0; i < 4; ++i)
    {
        content.showPage (i);
        for (int t = 0; t < 30; ++t)
        {
            for (int k = 0; k < 3; ++k)
            {
                io.clear();
                const int pos = (int) ((t * 3 + k) * 512) % (mix.getNumSamples() - 512);
                for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, mix, c, pos, 512);
                proc.processBlock (io, midi);
            }
            content.refreshNow();
        }
        auto img = content.createComponentSnapshot (content.getLocalBounds(), true, 1.0f);
        auto file = outDir.getChildFile (juce::String (names[i]) + ".png");
        file.deleteFile();
        juce::FileOutputStream fos (file);
        juce::PNGImageFormat png;
        png.writeImageToStream (img, fos);
        std::printf ("       wrote %s (%dx%d)\n", file.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    testLoudness();
    testUnmaskerUnity();
    testUnmaskerAction();
    testCharacterCalibration();
    testProcessorLatency();
    testFullChain();
    testReference();

    if (argc > 1)
        renderEditorSnapshots (juce::File (juce::String (argv[1])));

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
