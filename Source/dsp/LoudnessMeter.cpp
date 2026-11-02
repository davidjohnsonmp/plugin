#include "LoudnessMeter.h"
#include <algorithm>
#include <cmath>

namespace meridian
{

static inline float energyToLufs (double e) noexcept
{
    return e > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (e)) : LoudnessMeter::silence;
}

void LoudnessMeter::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    numChans = std::clamp (numChannels, 1, maxCh);

    // K-weighting (BS.1770-4), valid for any sample rate.
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (kPi * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        const auto c = BiquadCoefs::fromRaw (Vh + Vb * K / Q + K * K, 2.0 * (K * K - Vh), Vh - Vb * K / Q + K * K,
                                             a0, 2.0 * (K * K - 1.0), 1.0 - K / Q + K * K);
        for (auto& b : pre) b.c = c;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (kPi * f0 / sr);
        const auto c = BiquadCoefs::fromRaw (1.0, -2.0, 1.0, 1.0 + K / Q + K * K,
                                             2.0 * (K * K - 1.0), 1.0 - K / Q + K * K);
        for (auto& b : rlb) b.c = c;
    }

    subBlockLen = std::max (1, (int) std::lround (sr * 0.1));

    // 4x true-peak interpolator (windowed sinc, 48 taps total).
    for (int p = 0; p < 4; ++p)
    {
        const double frac = p / 4.0;
        double sum = 0.0;
        for (int k = 0; k < tpTaps; ++k)
        {
            const double pos = (double) (tpTaps / 2 - 1 - k) + frac;
            const double sinc = std::abs (pos) < 1.0e-9 ? 1.0 : std::sin (kPi * pos) / (kPi * pos);
            const double w = 0.5 + 0.5 * std::cos (kPi * pos / (tpTaps / 2 + 0.5));
            tpCoefs[p][k] = (float) (sinc * w);
            sum += sinc * w;
        }
        for (int k = 0; k < tpTaps; ++k) tpCoefs[p][k] = (float) (tpCoefs[p][k] / sum);
    }

    gateCounts.assign (histBins, 0u);  gateSums.assign (histBins, 0.0);
    stCounts.assign (histBins, 0u);    stSums.assign (histBins, 0.0);
    resetNow();
}

void LoudnessMeter::resetNow() noexcept
{
    for (auto& b : pre) b.reset();
    for (auto& b : rlb) b.reset();
    subBlockFill = 0; subSum = 0.0;
    subBlocks.fill (0.0); subIndex = 0; subCount = 0;
    std::fill (gateCounts.begin(), gateCounts.end(), 0u);
    std::fill (gateSums.begin(), gateSums.end(), 0.0);
    std::fill (stCounts.begin(), stCounts.end(), 0u);
    std::fill (stSums.begin(), stSums.end(), 0.0);
    for (auto& h : tpHist) h.fill (0.0f);
    tpPos = 0; tpBlockMax = 0.0f; tpBlockSamples = 0;
    momMaxV = stMaxV = silence; tpMaxV = 0.0f;

    momentary = silence; shortTerm = silence; integrated = silence;
    loudnessRange = 0.0f; truePeakMax = silence; truePeakNow = silence;
    momentaryMax = silence; shortTermMax = silence;
}

void LoudnessMeter::process (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (resetRequested.exchange (false))
        resetNow();

    const int chans = std::min (numChannels, numChans);
    for (int i = 0; i < numSamples; ++i)
    {
        double e = 0.0;
        for (int c = 0; c < chans; ++c)
        {
            const float x = channels[c][i];
            const float y = rlb[(size_t) c].process (pre[(size_t) c].process (x));
            // channel weights: 1.0 for L/R/C, 1.41 for surrounds (channels 4+ in 5.1)
            const double w = (chans > 4 && c >= 4) ? 1.41 : 1.0;
            if (! (chans >= 6 && c == 3)) // skip LFE in 5.1
                e += w * (double) y * (double) y;

            // true peak
            auto& h = tpHist[(size_t) c];
            h[(size_t) tpPos] = x;
            for (int p = 0; p < 4; ++p)
            {
                float v = 0.0f;
                for (int k = 0; k < tpTaps; ++k)
                    v += tpCoefs[p][k] * h[(size_t) ((tpPos - k + tpTaps) % tpTaps)];
                tpBlockMax = std::max (tpBlockMax, std::abs (v));
            }
        }
        tpPos = (tpPos + 1) % tpTaps;
        ++tpBlockSamples;

        subSum += e;
        if (++subBlockFill >= subBlockLen)
            finishSubBlock();
    }
}

void LoudnessMeter::finishSubBlock() noexcept
{
    const double subEnergy = subSum / (double) subBlockLen;
    subSum = 0.0;
    subBlockFill = 0;

    subBlocks[(size_t) subIndex] = subEnergy;
    subIndex = (subIndex + 1) % (int) subBlocks.size();
    subCount = std::min (subCount + 1, (int) subBlocks.size());

    auto meanOfLast = [this] (int count)
    {
        double s = 0.0;
        for (int k = 1; k <= count; ++k)
            s += subBlocks[(size_t) ((subIndex - k + (int) subBlocks.size()) % (int) subBlocks.size())];
        return s / (double) count;
    };

    // Momentary (400 ms) — each value is also a gating block for integrated loudness.
    if (subCount >= 4)
    {
        const double eM = meanOfLast (4);
        const float lM = energyToLufs (eM);
        momentary.store (lM);
        momMaxV = std::max (momMaxV, lM);
        momentaryMax.store (momMaxV);

        if (lM > histMin)
        {
            const int bin = std::clamp ((int) ((lM - histMin) * 10.0f), 0, histBins - 1);
            ++gateCounts[(size_t) bin];
            gateSums[(size_t) bin] += eM;
        }
        integrated.store (computeGated (gateCounts, gateSums, -10.0f));
    }
    else
    {
        momentary.store (energyToLufs (meanOfLast (subCount)));
    }

    // Short-term (3 s)
    if (subCount >= 30)
    {
        const double eS = meanOfLast (30);
        const float lS = energyToLufs (eS);
        shortTerm.store (lS);
        stMaxV = std::max (stMaxV, lS);
        shortTermMax.store (stMaxV);
        if (lS > histMin)
        {
            const int bin = std::clamp ((int) ((lS - histMin) * 10.0f), 0, histBins - 1);
            ++stCounts[(size_t) bin];
            stSums[(size_t) bin] += eS;
        }
        loudnessRange.store (computeRange());
    }
    else
    {
        shortTerm.store (energyToLufs (meanOfLast (subCount)));
    }

    // True peak (dBTP)
    tpMaxV = std::max (tpMaxV, tpBlockMax);
    truePeakNow.store (tpBlockMax > 1.0e-6f ? 20.0f * std::log10 (tpBlockMax) : silence);
    truePeakMax.store (tpMaxV > 1.0e-6f ? 20.0f * std::log10 (tpMaxV) : silence);
    tpBlockMax = 0.0f;
    tpBlockSamples = 0;
}

float LoudnessMeter::computeGated (const std::vector<unsigned int>& counts, const std::vector<double>& sums,
                                   float relativeGateLu) const noexcept
{
    double s = 0.0; unsigned long long n = 0;
    for (int b = 0; b < histBins; ++b) { s += sums[(size_t) b]; n += counts[(size_t) b]; }
    if (n == 0) return silence;

    const float relGate = energyToLufs (s / (double) n) + relativeGateLu;
    const int startBin = std::clamp ((int) std::ceil ((relGate - histMin) * 10.0f), 0, histBins);

    double s2 = 0.0; unsigned long long n2 = 0;
    for (int b = startBin; b < histBins; ++b) { s2 += sums[(size_t) b]; n2 += counts[(size_t) b]; }
    return n2 > 0 ? energyToLufs (s2 / (double) n2) : silence;
}

float LoudnessMeter::computeRange() const noexcept
{
    double s = 0.0; unsigned long long n = 0;
    for (int b = 0; b < histBins; ++b) { s += stSums[(size_t) b]; n += stCounts[(size_t) b]; }
    if (n == 0) return 0.0f;

    const float relGate = energyToLufs (s / (double) n) - 20.0f;
    const int startBin = std::clamp ((int) std::ceil ((relGate - histMin) * 10.0f), 0, histBins);

    unsigned long long total = 0;
    for (int b = startBin; b < histBins; ++b) total += stCounts[(size_t) b];
    if (total < 2) return 0.0f;

    auto percentile = [&] (double q)
    {
        const auto target = (unsigned long long) std::ceil (q * (double) total);
        unsigned long long acc = 0;
        for (int b = startBin; b < histBins; ++b)
        {
            acc += stCounts[(size_t) b];
            if (acc >= std::max (1ull, target))
                return histMin + ((float) b + 0.5f) * 0.1f;
        }
        return histMin + (float) histBins * 0.1f;
    };
    return std::max (0.0f, percentile (0.95) - percentile (0.10));
}

} // namespace meridian
