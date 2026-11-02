#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "Biquad.h"

namespace meridian
{

/** One-knob mix-bus chain:

      pre-EQ (low / mid / high)
        -> 3-band compressor (stereo-linked, internal or external sidechain)
        -> tonal "polish" EQ
        -> oversampled saturation + high-band harmonic exciter
        -> stereo width (wider highs, tighter mono bass)

    The Amount control (0-200 %) drives all stages together. The gain-reduction
    meters come with target zones: gain-stage the mix so each band sits inside
    its zone, then use Amount to taste. */
class CharacterChain
{
public:
    struct Settings
    {
        bool  enabled = true;
        float amount = 1.0f;    // 0..2
        float lowDb = 0.0f, midDb = 0.0f, highDb = 0.0f;
        bool  externalSidechain = false;
    };

    static constexpr int numBands = 3;
    static constexpr float crossoverLowHz = 200.0f;
    static constexpr float crossoverHighHz = 3500.0f;

    // Gain-reduction target zones (dB) and input RMS target zone (dBFS).
    static constexpr float grTargetMin[numBands] = { 2.0f, 1.5f, 1.0f };
    static constexpr float grTargetMax[numBands] = { 4.5f, 3.5f, 3.0f };
    static constexpr float inputTargetMin = -16.0f;
    static constexpr float inputTargetMax = -11.0f;

    void prepare (double sampleRate, int maxBlockSize, int numChannels, int oversamplingIndex);
    void reset();
    int  getLatencySamples() const noexcept { return latency; }

    void process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain,
                  const Settings& s) noexcept;

    std::array<std::atomic<float>, numBands> grDb {};
    std::atomic<float> inputPeakDb { -100.0f }, inputRmsDb { -100.0f };

private:
    void updateEq (const Settings& s) noexcept;
    static float gainComputer (float levelDb, float threshDb, float ratio, float kneeDb) noexcept;

    double sr = 48000.0;
    int numChans = 2, maxBlock = 512, latency = 0;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    double osRate = 48000.0;

    juce::dsp::LinkwitzRileyFilter<float> splitLow, splitHigh, lowAllpass;
    juce::dsp::LinkwitzRileyFilter<float> scSplitLow, scSplitHigh;

    static constexpr int maxCh = 8;
    std::array<Biquad, maxCh> preLow, preMid, preHigh, polLow, polMid, polHigh, dcBlock;
    std::array<Biquad, maxCh> excHp1, excHp2;
    OnePole sideLp;

    std::array<float, numBands> env {}, slowGr {}, grHold {};
    float rmsPower = 0.0f;
    float lastLow = 999.0f, lastMid = 999.0f, lastHigh = 999.0f, lastAmount = -1.0f;
    juce::SmoothedValue<float> mix;

    juce::AudioBuffer<float> dryDelay;
    int dryWrite = 0;
};

} // namespace meridian
