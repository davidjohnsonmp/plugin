#pragma once

#include <array>
#include <atomic>
#include <vector>
#include "Biquad.h"

namespace meridian
{

/** ITU-R BS.1770-4 / EBU R128 loudness meter.

    Momentary (400 ms), short-term (3 s), gated integrated loudness, loudness
    range (EBU Tech 3342) and true peak (4x oversampled). Results are published
    through atomics so the editor can read them at any time. The same class is
    used offline to analyse the reference track. */
class LoudnessMeter
{
public:
    static constexpr float silence = -100.0f;

    void prepare (double sampleRate, int numChannels);
    void process (const float* const* channels, int numChannels, int numSamples) noexcept;

    void requestReset() noexcept { resetRequested.store (true); }
    void resetNow() noexcept;

    std::atomic<float> momentary { silence }, shortTerm { silence }, integrated { silence };
    std::atomic<float> loudnessRange { 0.0f };
    std::atomic<float> truePeakMax { silence }, truePeakNow { silence };
    std::atomic<float> momentaryMax { silence }, shortTermMax { silence };

private:
    void finishSubBlock() noexcept;
    float computeGated (const std::vector<unsigned int>& counts, const std::vector<double>& sums,
                        float relativeGateLu) const noexcept;
    float computeRange() const noexcept;

    static constexpr int maxCh = 8;
    static constexpr int tpTaps = 12;
    static constexpr int histBins = 1000;      // 0.1 LU bins from -70 to +30 LUFS
    static constexpr float histMin = -70.0f;

    double sr = 48000.0;
    int numChans = 2;
    std::array<Biquad, maxCh> pre {}, rlb {};
    int subBlockLen = 4800, subBlockFill = 0;
    double subSum = 0.0;

    std::array<double, 30> subBlocks {};   // last 3 s of 100 ms energies
    int subIndex = 0, subCount = 0;

    std::vector<unsigned int> gateCounts, stCounts;
    std::vector<double> gateSums, stSums;

    std::array<std::array<float, tpTaps>, maxCh> tpHist {};
    int tpPos = 0;
    float tpCoefs[4][tpTaps] {};
    float tpBlockMax = 0.0f;
    int tpBlockSamples = 0;

    float momMaxV = silence, stMaxV = silence, tpMaxV = 0.0f;
    std::atomic<bool> resetRequested { false };
};

} // namespace meridian
