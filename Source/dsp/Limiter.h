#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace meridian
{

/** Look-ahead, stereo-linked true-peak limiter with program-dependent release.

    Inter-sample peaks are estimated with a 4x polyphase interpolator. The gain
    curve is a sliding-window minimum followed by a box-car average of the same
    length as the look-ahead, which guarantees the ceiling is never exceeded
    while keeping the gain changes smooth (no clicks, minimal distortion).
    Release time adapts between ~50 ms (short peaks) and ~500 ms (sustained
    limiting) to avoid pumping. */
class Limiter
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    int  getLatencySamples() const noexcept { return latency; }

    void process (juce::AudioBuffer<float>& buffer, bool enabled, float driveDb, float ceilingDb) noexcept;

    std::atomic<float> grDb { 0.0f };
    std::atomic<float> outPeakDb { -100.0f };

private:
    static constexpr int maxCh = 8;
    static constexpr int interpTaps = 8;

    double sr = 48000.0;
    int numChans = 2, lookahead = 72, latency = 76;

    std::vector<std::vector<float>> delay;   // audio delay lines
    std::vector<std::vector<float>> hist;    // interpolator history
    int delayMask = 0, delayPos = 0, histPos = 0;
    float phaseCoefs[3][interpTaps] {};

    // sliding minimum (monotonic queue)
    std::vector<float> qVal; std::vector<long long> qIdx;
    int qHead = 0, qTail = 0, qMask = 0;
    long long sampleIndex = 0;

    // box-car average
    std::vector<float> avgRing; int avgPos = 0; double avgSum = 0.0;

    float env = 1.0f, slowGrDb = 0.0f;
    juce::SmoothedValue<float> drive, ceiling, mix;
};

} // namespace meridian
