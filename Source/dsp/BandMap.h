#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <utility>

namespace meridian
{

/** Maps FFT bins onto a set of bands spaced evenly on the ERB-rate scale
    (roughly how the inner ear groups frequencies). Shared by the adaptive EQ,
    the output tonal-balance analyser and the reference-track analyser so that
    all three describe the spectrum in exactly the same way. */
struct BandMap
{
    static constexpr int numBands = 64;

    std::array<float, numBands> centreHz {};
    std::array<float, numBands> bark {};
    std::array<float, numBands> octavePos {};      // log2 (f / 1 kHz)
    std::array<float, numBands> bandwidthBins {};  // band width expressed in FFT bins
    std::array<float, numBands> athDb {};          // threshold of hearing (dBFS domain)
    std::vector<std::vector<std::pair<int, float>>> weights; // per band: (bin, weight)
    std::vector<float> binBandPos;                 // fractional band index of every bin
    double sampleRate = 48000.0;
    int fftSize = 2048;
    float binHz = 23.4f;

    static float hzToErb (float f) noexcept { return 21.4f * std::log10 (1.0f + 0.00437f * f); }
    static float erbToHz (float e) noexcept { return (std::pow (10.0f, e / 21.4f) - 1.0f) / 0.00437f; }
    static float hzToBark (float f) noexcept
    {
        return 13.0f * std::atan (0.00076f * f) + 3.5f * std::atan ((f / 7500.0f) * (f / 7500.0f));
    }
    /** Terhardt's approximation of the absolute threshold of hearing in dB SPL,
        shifted so that 0 dBFS corresponds to roughly 100 dB SPL. */
    static float athDbfs (float f) noexcept
    {
        const float k = std::max (f, 20.0f) / 1000.0f;
        const float spl = 3.64f * std::pow (k, -0.8f) - 6.5f * std::exp (-0.6f * (k - 3.3f) * (k - 3.3f))
                        + 0.001f * k * k * k * k;
        return std::min (spl, 80.0f) - 100.0f;
    }

    void build (double sr, int newFftSize)
    {
        sampleRate = sr;
        fftSize = newFftSize;
        const int numBins = fftSize / 2 + 1;
        binHz = (float) (sr / fftSize);

        const float fMin = 25.0f;
        const float fMax = (float) std::min (19500.0, 0.46 * sr);
        const float e0 = hzToErb (fMin), e1 = hzToErb (fMax);
        const float de = (e1 - e0) / (float) (numBands - 1);

        for (int b = 0; b < numBands; ++b)
        {
            const float e = e0 + de * (float) b;
            centreHz[(size_t) b]  = erbToHz (e);
            bark[(size_t) b]      = hzToBark (centreHz[(size_t) b]);
            octavePos[(size_t) b] = std::log2 (centreHz[(size_t) b] / 1000.0f);
            const float bw = erbToHz (e + 0.5f * de) - erbToHz (e - 0.5f * de);
            bandwidthBins[(size_t) b] = bw / binHz;
            athDb[(size_t) b] = athDbfs (centreHz[(size_t) b]) + 10.0f * std::log10 (std::max (bw, 1.0f) / 100.0f);
        }

        weights.assign (numBands, {});
        binBandPos.assign ((size_t) numBins, 0.0f);

        for (int k = 0; k < numBins; ++k)
        {
            const float f = (float) k * binHz;
            const float pos = (hzToErb (f) - e0) / de;
            binBandPos[(size_t) k] = std::clamp (pos, 0.0f, (float) (numBands - 1));

            if (k == 0) continue; // skip DC
            const int lo = (int) std::floor (pos);
            for (int b = lo; b <= lo + 1; ++b)
            {
                if (b < 0 || b >= numBands) continue;
                const float w = 1.0f - std::abs (pos - (float) b);
                if (w > 0.0f) weights[(size_t) b].push_back ({ k, w });
            }
        }

        // Bands narrower than one FFT bin: interpolate from the two nearest bins.
        for (int b = 0; b < numBands; ++b)
        {
            if (! weights[(size_t) b].empty()) continue;
            const float exact = centreHz[(size_t) b] / binHz;
            const int k0 = std::clamp ((int) std::floor (exact), 1, numBins - 2);
            const float frac = std::clamp (exact - (float) k0, 0.0f, 1.0f);
            weights[(size_t) b].push_back ({ k0, 1.0f - frac });
            weights[(size_t) b].push_back ({ k0 + 1, frac });
        }
    }

    /** power: one-sided normalised power per bin. Writes band energies (linear). */
    void bandEnergies (const float* power, float* outEnergy) const noexcept
    {
        for (int b = 0; b < numBands; ++b)
        {
            float sum = 0.0f, wsum = 0.0f;
            for (auto& [k, w] : weights[(size_t) b]) { sum += w * power[k]; wsum += w; }
            const float density = wsum > 0.0f ? sum / wsum : 0.0f;
            outEnergy[b] = density * std::max (bandwidthBins[(size_t) b], 1.0e-3f);
        }
    }
};

} // namespace meridian
