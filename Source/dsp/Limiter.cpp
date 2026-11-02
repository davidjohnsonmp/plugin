#include "Limiter.h"
#include <cmath>

namespace meridian
{

void Limiter::prepare (double sampleRate, int /*maxBlockSize*/, int numChannels)
{
    sr = sampleRate;
    numChans = std::clamp (numChannels, 1, maxCh);
    lookahead = std::max (8, (int) std::lround (0.0015 * sr));
    latency = lookahead + interpTaps / 2;

    const int dlen = juce::nextPowerOfTwo (latency + 2);
    delayMask = dlen - 1;
    delay.assign ((size_t) numChans, std::vector<float> ((size_t) dlen, 0.0f));
    hist.assign ((size_t) numChans, std::vector<float> ((size_t) interpTaps, 0.0f));

    // 4x interpolation phases (windowed sinc), evaluated between taps 3 and 4.
    for (int p = 0; p < 3; ++p)
    {
        const double frac = (p + 1) / 4.0;
        double sum = 0.0;
        for (int k = 0; k < interpTaps; ++k)
        {
            // tap k holds x[t-k]; we interpolate at position t - 4 + frac
            const double x = (double) (interpTaps / 2 - k) - (1.0 - frac) + 0.0;
            const double pos = x;  // distance from the interpolation point in samples
            const double sinc = std::abs (pos) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * pos)
                                                               / (juce::MathConstants<double>::pi * pos);
            const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * pos / (interpTaps / 2 + 0.5));
            phaseCoefs[p][k] = (float) (sinc * w);
            sum += sinc * w;
        }
        for (int k = 0; k < interpTaps; ++k) phaseCoefs[p][k] = (float) (phaseCoefs[p][k] / sum);
    }

    const int qlen = juce::nextPowerOfTwo (lookahead + 4);
    qMask = qlen - 1;
    qVal.assign ((size_t) qlen, 1.0f);
    qIdx.assign ((size_t) qlen, 0);
    avgRing.assign ((size_t) lookahead, 1.0f);

    drive.reset (sr, 0.05);
    ceiling.reset (sr, 0.05);
    mix.reset (sr, 0.05);
    reset();
}

void Limiter::reset()
{
    for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
    for (auto& h : hist)  std::fill (h.begin(), h.end(), 0.0f);
    delayPos = histPos = 0;
    qHead = qTail = 0;
    sampleIndex = 0;
    std::fill (avgRing.begin(), avgRing.end(), 1.0f);
    avgPos = 0;
    avgSum = (double) lookahead;
    env = 1.0f;
    slowGrDb = 0.0f;
}

void Limiter::process (juce::AudioBuffer<float>& buffer, bool enabled, float driveDb, float ceilingDb) noexcept
{
    const int n = buffer.getNumSamples();
    const int chans = std::min (buffer.getNumChannels(), numChans);
    drive.setTargetValue (juce::Decibels::decibelsToGain (driveDb));
    ceiling.setTargetValue (juce::Decibels::decibelsToGain (ceilingDb));
    mix.setTargetValue (enabled ? 1.0f : 0.0f);

    const float slowCoef = std::exp (-1.0f / (0.8f * (float) sr));
    float maxGr = 0.0f, outPeak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float dg = drive.getNextValue();
        const float ceil = ceiling.getNextValue();
        const float mx = mix.getNextValue();

        // --- detection: true-peak estimate of the driven signal around x[t-4]
        float peak = 0.0f;
        for (int c = 0; c < chans; ++c)
        {
            auto& h = hist[(size_t) c];
            h[(size_t) histPos] = buffer.getSample (c, i) * dg;

            // gather taps x[t-k]
            float taps[interpTaps];
            for (int k = 0; k < interpTaps; ++k)
                taps[k] = h[(size_t) ((histPos - k + interpTaps) % interpTaps)];

            peak = std::max (peak, std::abs (taps[interpTaps / 2]));
            for (int p = 0; p < 3; ++p)
            {
                float y = 0.0f;
                for (int k = 0; k < interpTaps; ++k) y += phaseCoefs[p][k] * taps[k];
                peak = std::max (peak, std::abs (y));
            }
        }
        histPos = (histPos + 1) % interpTaps;

        const float req = peak > ceil ? ceil / peak : 1.0f;

        // --- sliding-window minimum over lookahead+1 samples
        while (qTail != qHead && qVal[(size_t) ((qTail - 1) & qMask)] >= req)
            qTail = (qTail - 1) & qMask;
        qVal[(size_t) qTail] = req;
        qIdx[(size_t) qTail] = sampleIndex;
        qTail = (qTail + 1) & qMask;
        while (qIdx[(size_t) qHead] <= sampleIndex - (long long) (lookahead + 1))
            qHead = (qHead + 1) & qMask;
        const float held = qVal[(size_t) qHead];
        ++sampleIndex;

        // --- program-dependent release (instant attack: the box-car does the ramp)
        const float relSec = 0.05f + 0.45f * std::clamp (slowGrDb / 4.0f, 0.0f, 1.0f);
        const float relCoef = std::exp (-1.0f / (relSec * (float) sr));
        env = held < env ? held : held + relCoef * (env - held);

        // --- box-car smoothing
        avgSum += (double) env - (double) avgRing[(size_t) avgPos];
        avgRing[(size_t) avgPos] = env;
        avgPos = (avgPos + 1) % lookahead;
        const float gain = std::min (1.0f, (float) (avgSum / (double) lookahead));

        const float grNow = -juce::Decibels::gainToDecibels (gain, -60.0f);
        slowGrDb = slowCoef * slowGrDb + (1.0f - slowCoef) * grNow;
        maxGr = std::max (maxGr, grNow * mx);

        // --- delayed audio out
        for (int c = 0; c < chans; ++c)
        {
            auto& d = delay[(size_t) c];
            const float in = buffer.getSample (c, i);
            d[(size_t) delayPos] = in;
            const float delayed = d[(size_t) ((delayPos - latency) & delayMask)];
            float limited = std::clamp (delayed * dg * gain, -ceil, ceil);
            const float y = delayed + mx * (limited - delayed);
            buffer.setSample (c, i, y);
            outPeak = std::max (outPeak, std::abs (y));
        }
        delayPos = (delayPos + 1) & delayMask;
    }

    grDb.store (maxGr, std::memory_order_relaxed);
    outPeakDb.store (juce::Decibels::gainToDecibels (outPeak, -100.0f), std::memory_order_relaxed);
}

} // namespace meridian
