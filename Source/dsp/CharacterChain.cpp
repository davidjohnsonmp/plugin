#include "CharacterChain.h"

namespace meridian
{

static inline float dbToAmpC (float db) noexcept { return std::exp (db * 0.1151292546f); }

// Base thresholds (dBFS, detector level) and ratios for the three bands.
static constexpr float baseThresh[CharacterChain::numBands] = { -13.0f, -20.0f, -26.0f };
static constexpr float baseRatio[CharacterChain::numBands]  = { 2.5f, 2.0f, 2.0f };
static constexpr float attackMs[CharacterChain::numBands]   = { 25.0f, 8.0f, 3.0f };
static constexpr float releaseMs[CharacterChain::numBands]  = { 180.0f, 120.0f, 80.0f };

void CharacterChain::prepare (double sampleRate, int maxBlockSize, int numChannels, int oversamplingIndex)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    numChans = std::clamp (numChannels, 1, maxCh);

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, (juce::uint32) numChans };
    for (auto* f : { &splitLow, &splitHigh, &lowAllpass })
        f->prepare (spec);
    splitLow.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    splitLow.setCutoffFrequency (crossoverLowHz);
    splitHigh.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    splitHigh.setCutoffFrequency (crossoverHighHz);
    lowAllpass.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
    lowAllpass.setCutoffFrequency (crossoverHighHz);

    juce::dsp::ProcessSpec monoSpec { sr, (juce::uint32) maxBlock, 1 };
    scSplitLow.prepare (monoSpec);
    scSplitLow.setCutoffFrequency (crossoverLowHz);
    scSplitHigh.prepare (monoSpec);
    scSplitHigh.setCutoffFrequency (crossoverHighHz);

    oversampler.reset();
    osRate = sr;
    latency = 0;
    if (oversamplingIndex > 0)
    {
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) numChans, (size_t) oversamplingIndex,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversampler->initProcessing ((size_t) maxBlock);
        osRate = sr * (double) (1 << oversamplingIndex);
        latency = (int) std::lround (oversampler->getLatencyInSamples());
    }

    for (int c = 0; c < maxCh; ++c)
    {
        excHp1[(size_t) c].c = BiquadCoefs::highPass (osRate, 3000.0, 0.707);
        excHp2[(size_t) c].c = BiquadCoefs::highPass (osRate, 5000.0, 0.707);
        dcBlock[(size_t) c].c = BiquadCoefs::highPass (sr, 8.0, 0.707);
    }
    sideLp.setCutoff (sr, 120.0);

    dryDelay.setSize (numChans, juce::nextPowerOfTwo (latency + maxBlock + 1));
    mix.reset (sr, 0.05);
    mix.setCurrentAndTargetValue (1.0f);

    lastLow = lastMid = lastHigh = 999.0f;
    lastAmount = -1.0f;
    reset();
}

void CharacterChain::reset()
{
    splitLow.reset(); splitHigh.reset(); lowAllpass.reset();
    scSplitLow.reset(); scSplitHigh.reset();
    if (oversampler) oversampler->reset();
    for (auto* arr : { &preLow, &preMid, &preHigh, &polLow, &polMid, &polHigh, &dcBlock, &excHp1, &excHp2 })
        for (auto& b : *arr) b.reset();
    sideLp.reset();
    env.fill (1.0e-6f);
    slowGr.fill (0.0f);
    grHold.fill (0.0f);
    rmsPower = 0.0f;
    dryDelay.clear();
    dryWrite = 0;
}

float CharacterChain::gainComputer (float x, float t, float r, float w) noexcept
{
    const float over = x - t;
    if (2.0f * over < -w) return 0.0f;
    if (2.0f * std::abs (over) <= w)
    {
        const float k = over + 0.5f * w;
        return (1.0f - 1.0f / r) * k * k / (2.0f * w);
    }
    return over * (1.0f - 1.0f / r);
}

void CharacterChain::updateEq (const Settings& s) noexcept
{
    if (s.lowDb != lastLow || s.midDb != lastMid || s.highDb != lastHigh)
    {
        const auto lo = BiquadCoefs::lowShelf (sr, 110.0, 0.7, s.lowDb);
        const auto mi = BiquadCoefs::peak (sr, 1000.0, 0.6, s.midDb);
        const auto hi = BiquadCoefs::highShelf (sr, 7500.0, 0.7, s.highDb);
        for (int c = 0; c < maxCh; ++c) { preLow[(size_t) c].c = lo; preMid[(size_t) c].c = mi; preHigh[(size_t) c].c = hi; }
        lastLow = s.lowDb; lastMid = s.midDb; lastHigh = s.highDb;
    }
    if (std::abs (s.amount - lastAmount) > 0.002f)
    {
        const float a = s.amount;
        const auto lo = BiquadCoefs::lowShelf (sr, 70.0, 0.7, 1.2 * a);
        const auto mi = BiquadCoefs::peak (sr, 350.0, 0.9, -0.8 * a);
        const auto hi = BiquadCoefs::highShelf (sr, 11000.0, 0.7, 1.5 * a);
        for (int c = 0; c < maxCh; ++c) { polLow[(size_t) c].c = lo; polMid[(size_t) c].c = mi; polHigh[(size_t) c].c = hi; }
        lastAmount = a;
    }
}

void CharacterChain::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain,
                              const Settings& s) noexcept
{
    const int n = std::min (buffer.getNumSamples(), maxBlock);
    const int chans = std::min (buffer.getNumChannels(), numChans);
    if (n <= 0 || chans <= 0) return;

    updateEq (s);
    mix.setTargetValue (s.enabled ? 1.0f : 0.0f);

    // ---- keep a latency-aligned copy of the input for the module on/off fade
    const int dlen = dryDelay.getNumSamples();
    const int dmask = dlen - 1;
    const int startWrite = dryWrite;
    for (int c = 0; c < chans; ++c)
    {
        const auto* src = buffer.getReadPointer (c);
        auto* d = dryDelay.getWritePointer (c);
        for (int i = 0; i < n; ++i) d[(startWrite + i) & dmask] = src[i];
    }
    dryWrite = (startWrite + n) & dmask;

    // ---- input metering
    float peak = 0.0f;
    const float rmsCoef = std::exp (-1.0f / (0.3f * (float) sr));
    for (int i = 0; i < n; ++i)
    {
        float p = 0.0f;
        for (int c = 0; c < chans; ++c)
        {
            const float x = buffer.getSample (c, i);
            peak = std::max (peak, std::abs (x));
            p += x * x;
        }
        rmsPower = rmsCoef * rmsPower + (1.0f - rmsCoef) * (p / (float) chans);
    }
    inputPeakDb.store (juce::Decibels::gainToDecibels (peak, -100.0f), std::memory_order_relaxed);
    inputRmsDb.store (juce::Decibels::gainToDecibels (std::sqrt (rmsPower), -100.0f), std::memory_order_relaxed);

    const float a = s.amount;
    const float wet = std::min (a, 1.0f);
    const float push = std::max (a - 1.0f, 0.0f);

    std::array<float, numBands> atk {}, rel {}, th {}, ra {};
    for (int b = 0; b < numBands; ++b)
    {
        atk[(size_t) b] = std::exp (-1.0f / (attackMs[b] * 0.001f * (float) sr));
        rel[(size_t) b] = std::exp (-1.0f / (releaseMs[b] * 0.001f * (float) sr));
        th[(size_t) b] = baseThresh[b] - push * 6.0f;
        ra[(size_t) b] = baseRatio[b] + push * 1.0f;
    }
    const float slowCoef = std::exp (-1.0f / (1.5f * (float) sr));

    const bool ext = s.externalSidechain && sidechain != nullptr && sidechain->getNumChannels() > 0
                  && sidechain->getNumSamples() >= n;
    std::array<float, numBands> blockMaxGr {};

    // ---- stage A: pre-EQ, multiband compression, polish EQ (base rate)
    for (int i = 0; i < n; ++i)
    {
        float bandSig[numBands][maxCh];
        std::array<float, numBands> pw {};

        for (int c = 0; c < chans; ++c)
        {
            float x = buffer.getSample (c, i);
            x = preHigh[(size_t) c].process (preMid[(size_t) c].process (preLow[(size_t) c].process (x)));
            float low = 0.0f, rest = 0.0f, mid = 0.0f, high = 0.0f;
            splitLow.processSample (c, x, low, rest);
            splitHigh.processSample (c, rest, mid, high);
            low = lowAllpass.processSample (c, low);
            bandSig[0][c] = low; bandSig[1][c] = mid; bandSig[2][c] = high;
            pw[0] += low * low; pw[1] += mid * mid; pw[2] += high * high;
        }

        if (ext)
        {
            float scx = 0.0f;
            for (int c = 0; c < sidechain->getNumChannels(); ++c) scx += sidechain->getSample (c, i);
            scx /= (float) sidechain->getNumChannels();
            float low = 0.0f, rest = 0.0f, mid = 0.0f, high = 0.0f;
            scSplitLow.processSample (0, scx, low, rest);
            scSplitHigh.processSample (0, rest, mid, high);
            pw = { low * low, mid * mid, high * high };
        }
        else
        {
            for (auto& p : pw) p /= (float) chans;
        }

        std::array<float, numBands> g {};
        for (int b = 0; b < numBands; ++b)
        {
            float& e = env[(size_t) b];
            const float p = pw[(size_t) b];
            e = p > e ? atk[(size_t) b] * e + (1.0f - atk[(size_t) b]) * p
                      : rel[(size_t) b] * e + (1.0f - rel[(size_t) b]) * p;
            const float levelDb = 10.0f * std::log10 (e + 1.0e-12f) + 3.0f;
            const float gr = gainComputer (levelDb, th[(size_t) b], ra[(size_t) b], 6.0f);
            blockMaxGr[(size_t) b] = std::max (blockMaxGr[(size_t) b], gr);
            slowGr[(size_t) b] = slowCoef * slowGr[(size_t) b] + (1.0f - slowCoef) * gr;
            const float makeup = 0.5f * slowGr[(size_t) b] * wet;
            g[(size_t) b] = ((1.0f - wet) + wet * dbToAmpC (-gr)) * dbToAmpC (makeup);
        }

        for (int c = 0; c < chans; ++c)
        {
            float y = bandSig[0][c] * g[0] + bandSig[1][c] * g[1] + bandSig[2][c] * g[2];
            y = polHigh[(size_t) c].process (polMid[(size_t) c].process (polLow[(size_t) c].process (y)));
            buffer.setSample (c, i, y);
        }
    }

    for (int b = 0; b < numBands; ++b)
    {
        // gentle meter ballistics: fast up, slower down
        auto& h = grHold[(size_t) b];
        h = blockMaxGr[(size_t) b] > h ? blockMaxGr[(size_t) b] : h * 0.92f + blockMaxGr[(size_t) b] * 0.08f;
        grDb[(size_t) b].store (h, std::memory_order_relaxed);
    }

    // ---- stage B: oversampled saturation + exciter
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) chans, (size_t) n);
        juce::dsp::AudioBlock<float> osBlock = oversampler ? oversampler->processSamplesUp (block) : block;

        const float k = 1.0f + 1.2f * a;
        const float bias = 0.08f * a;
        const float tb = std::tanh (bias);
        const float norm = 1.0f / (k * (1.0f - tb * tb));
        const float satMix = std::min (1.0f, a * 0.6f);
        const float excAmt = 0.06f * a;

        for (size_t c = 0; c < osBlock.getNumChannels(); ++c)
        {
            auto* d = osBlock.getChannelPointer (c);
            for (size_t i = 0; i < osBlock.getNumSamples(); ++i)
            {
                const float x = d[i];
                const float sat = (std::tanh (k * x + bias) - tb) * norm;
                float y = x + satMix * (sat - x);
                const float hp = excHp1[c].process (x);
                y += excAmt * excHp2[c].process (std::tanh (3.0f * hp));
                d[i] = y;
            }
        }

        if (oversampler) oversampler->processSamplesDown (block);
    }

    // ---- stage C: width, DC block, module fade
    const float sideLowGain = 1.0f - 0.35f * wet;
    const float sideHighGain = 1.0f + 0.10f * a;
    for (int i = 0; i < n; ++i)
    {
        if (chans == 2)
        {
            const float L = buffer.getSample (0, i), R = buffer.getSample (1, i);
            const float m = 0.5f * (L + R), sd = 0.5f * (L - R);
            const float sl = sideLp.lp (sd);
            const float s2 = sl * sideLowGain + (sd - sl) * sideHighGain;
            buffer.setSample (0, i, m + s2);
            buffer.setSample (1, i, m - s2);
        }

        const float mx = mix.getNextValue();
        for (int c = 0; c < chans; ++c)
        {
            const float wetS = dcBlock[(size_t) c].process (buffer.getSample (c, i));
            const float dry = dryDelay.getSample (c, (startWrite + i - latency) & dmask);
            buffer.setSample (c, i, dry + mx * (wetS - dry));
        }
    }
}

} // namespace meridian
