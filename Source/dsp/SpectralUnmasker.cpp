#include "SpectralUnmasker.h"
#include "Biquad.h"

namespace meridian
{

static inline float smoothstep01 (float x) noexcept
{
    x = std::clamp (x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

static inline float dbToPower (float db) noexcept { return std::exp (db * 0.2302585093f); } // 10^(db/10)
static inline float dbToAmp   (float db) noexcept { return std::exp (db * 0.1151292546f); } // 10^(db/20)
static inline float powerToDb (float p)  noexcept { return 10.0f * std::log10 (p + 1.0e-12f); }

void SpectralUnmasker::prepare (double sampleRate, int numChannels, Quality quality)
{
    sr = sampleRate;
    numChans = std::max (1, numChannels);

    int baseOrder = 11, overlap = 8;
    switch (quality)
    {
        case Quality::live:   baseOrder = 9;  overlap = 4;  break;
        case Quality::normal: baseOrder = 11; overlap = 8;  break;
        case Quality::best:   baseOrder = 12; overlap = 16; break;
    }
    const int extra = sr > 132000.0 ? 2 : (sr > 66000.0 ? 1 : 0);
    fftOrder = baseOrder + extra;
    fftSize  = 1 << fftOrder;
    hopSize  = fftSize / overlap;

    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    window.resize ((size_t) fftSize);
    for (int n = 0; n < fftSize; ++n)
        window[(size_t) n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) fftSize);

    // Overlap-add normalisation for analysis * synthesis windowing.
    double acc = 0.0;
    for (int n = 0; n < hopSize; ++n)
        for (int m = n; m < fftSize; m += hopSize)
            acc += (double) window[(size_t) m] * window[(size_t) m];
    olaNorm = (float) (acc / hopSize);

    inFifo.assign ((size_t) numChans, std::vector<float> ((size_t) fftSize, 0.0f));
    outFifo.assign ((size_t) numChans, std::vector<float> ((size_t) fftSize, 0.0f));
    anaFifo.assign ((size_t) fftSize, 0.0f);
    fftBuf.assign ((size_t) fftSize * 2, 0.0f);
    power.assign ((size_t) fftSize / 2 + 1, 0.0f);
    binGain.assign ((size_t) fftSize / 2 + 1, 1.0f);

    map.build (sr, fftSize);

    spreadMatrix.assign ((size_t) numBands * numBands, 0.0f);
    for (int i = 0; i < numBands; ++i)
        for (int j = 0; j < numBands; ++j)
        {
            if (i == j) continue;
            const float dz = map.bark[(size_t) i] - map.bark[(size_t) j] + 0.474f;
            const float sfDb = 15.81f + 7.5f * dz - 17.5f * std::sqrt (1.0f + dz * dz);
            spreadMatrix[(size_t) (i * numBands + j)] = dbToPower (sfDb);
        }

    // K-weighting (BS.1770) power response at each band centre, so the loudness
    // compensation keeps the integrated LUFS reading unchanged.
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (kPi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const auto pre = BiquadCoefs::fromRaw (Vh + Vb * K / Q + K * K, 2.0 * (K * K - Vh), Vh - Vb * K / Q + K * K,
                                               1.0 + K / Q + K * K, 2.0 * (K * K - 1.0), 1.0 - K / Q + K * K);
        const double f1 = 38.13547087602444, Q1 = 0.5003270373238773, K1 = std::tan (kPi * f1 / sr);
        const auto rlb = BiquadCoefs::fromRaw (1.0, -2.0, 1.0, 1.0 + K1 / Q1 + K1 * K1, 2.0 * (K1 * K1 - 1.0), 1.0 - K1 / Q1 + K1 * K1);
        for (int b = 0; b < numBands; ++b)
            kWeight[(size_t) b] = (float) (magnitudeSquared (pre, map.centreHz[(size_t) b], sr)
                                         * magnitudeSquared (rlb, map.centreHz[(size_t) b], sr));
    }

    reset();
}

void SpectralUnmasker::reset()
{
    for (auto& f : inFifo)  std::fill (f.begin(), f.end(), 0.0f);
    for (auto& f : outFifo) std::fill (f.begin(), f.end(), 0.0f);
    std::fill (anaFifo.begin(), anaFifo.end(), 0.0f);
    writePos = readPos = hopCounter = 0;
    smoothLevelDb.fill (-120.0f);
    curveDb.fill (0.0f);
    targetDb.fill (0.0f);
    compDb = 0.0f;
    firstFrame = true;
    meterRec = meterTam = meterBias = meterBright = 0.0f;
}

float SpectralUnmasker::boostShape (float hz) const noexcept
{
    const float low  = 1.0f / std::sqrt (1.0f + (hz / 120.0f) * (hz / 120.0f));
    const float oct  = std::log2 (hz / 2500.0f) / 1.3f;
    const float mid  = std::exp (-0.5f * oct * oct);
    const float high = 1.0f / (1.0f + (9000.0f / hz) * (9000.0f / hz));
    return 0.32f * low - 0.10f * mid + 0.06f * high;
}

float SpectralUnmasker::rangeWeight (float hz, float lowHz, float highHz) const noexcept
{
    constexpr float width = 1.0f / 3.0f; // transition width in octaves
    auto above = [&] (float edge) { return smoothstep01 ((std::log2 (hz / edge) + width) / width); }; // 1 at/above edge
    auto below = [&] (float edge) { return 1.0f - smoothstep01 (std::log2 (hz / edge) / width); };    // 1 at/below edge

    if (lowHz <= highHz)
    {
        const float a = lowHz  <= 20.5f    ? 1.0f : above (lowHz);
        const float b = highHz >= 19990.0f ? 1.0f : below (highHz);
        return a * b;
    }
    // Limiters reversed: exclude the range between them.
    const float inside = smoothstep01 (std::log2 (hz / highHz) / width)
                       * (1.0f - smoothstep01 ((std::log2 (hz / lowHz) + width) / width));
    return 1.0f - inside;
}

void SpectralUnmasker::process (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>* sidechain,
                                const Settings& s) noexcept
{
    const int n = main.getNumSamples();
    const int chans = std::min (main.getNumChannels(), numChans);
    const int mask = fftSize - 1;

    const bool useSc = s.useSidechain && sidechain != nullptr
                    && sidechain->getNumChannels() > 0 && sidechain->getNumSamples() >= n;
    const int scChans = useSc ? sidechain->getNumChannels() : 0;

    for (int i = 0; i < n; ++i)
    {
        float a = 0.0f;
        if (useSc)
        {
            for (int c = 0; c < scChans; ++c) a += sidechain->getSample (c, i);
            a /= (float) scChans;
        }
        else
        {
            for (int c = 0; c < chans; ++c) a += main.getSample (c, i);
            a /= (float) std::max (1, chans);
        }
        anaFifo[(size_t) writePos] = a;

        for (int c = 0; c < chans; ++c)
        {
            auto* d = main.getWritePointer (c);
            inFifo[(size_t) c][(size_t) writePos] = d[i];
            d[i] = outFifo[(size_t) c][(size_t) readPos];
            outFifo[(size_t) c][(size_t) readPos] = 0.0f;
        }

        writePos = (writePos + 1) & mask;
        readPos  = (readPos + 1) & mask;

        if (++hopCounter >= hopSize)
        {
            hopCounter = 0;
            processFrame (s);
        }
    }
}

void SpectralUnmasker::processFrame (const Settings& s) noexcept
{
    const int N = fftSize, mask = N - 1, numBins = N / 2 + 1;

    // ---- analysis ------------------------------------------------------
    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
    for (int n = 0; n < N; ++n)
        fftBuf[(size_t) n] = anaFifo[(size_t) ((writePos + n) & mask)] * window[(size_t) n];

    fft->performFrequencyOnlyForwardTransform (fftBuf.data(), true);

    const float norm = 2.0f / ((float) N * (0.375f * (float) N)); // 2 / (N * sum w^2)
    for (int k = 0; k < numBins; ++k)
        power[(size_t) k] = fftBuf[(size_t) k] * fftBuf[(size_t) k] * norm;

    map.bandEnergies (power.data(), bandEnergy.data());
    computeCurve (s);

    // ---- per-bin gains ---------------------------------------------------
    const float frameT = (float) hopSize / (float) sr;
    const float mixCoef = std::exp (-frameT / 0.060f);
    moduleMix = mixCoef * moduleMix + (1.0f - mixCoef) * (s.enabled ? 1.0f : 0.0f);
    gainDbSmoothed = mixCoef * gainDbSmoothed + (1.0f - mixCoef) * s.gainDb;

    for (int k = 0; k < numBins; ++k)
    {
        const float pos = map.binBandPos[(size_t) k];
        const int b0 = (int) pos;
        const int b1 = std::min (b0 + 1, numBands - 1);
        const float fr = pos - (float) b0;
        const float db = curveDb[(size_t) b0] + fr * (curveDb[(size_t) b1] - curveDb[(size_t) b0]);
        binGain[(size_t) k] = dbToAmp (moduleMix * (db + gainDbSmoothed));
    }

    // ---- resynthesis -----------------------------------------------------
    const float outScale = 1.0f / olaNorm;
    for (int c = 0; c < numChans; ++c)
    {
        auto& in = inFifo[(size_t) c];
        auto& out = outFifo[(size_t) c];

        for (int n = 0; n < N; ++n)
            fftBuf[(size_t) n] = in[(size_t) ((writePos + n) & mask)] * window[(size_t) n];
        std::fill (fftBuf.begin() + N, fftBuf.end(), 0.0f);

        fft->performRealOnlyForwardTransform (fftBuf.data(), true);
        for (int k = 0; k < numBins; ++k)
        {
            fftBuf[(size_t) (2 * k)]     *= binGain[(size_t) k];
            fftBuf[(size_t) (2 * k + 1)] *= binGain[(size_t) k];
        }
        fft->performRealOnlyInverseTransform (fftBuf.data());

        for (int n = 0; n < N; ++n)
            out[(size_t) ((readPos + n) & mask)] += fftBuf[(size_t) n] * window[(size_t) n] * outScale;
    }

    // ---- publish display data -------------------------------------------
    for (int b = 0; b < numBands; ++b)
    {
        displayCurveDb[(size_t) b].store (moduleMix * curveDb[(size_t) b], std::memory_order_relaxed);
        displayInputDb[(size_t) b].store (smoothLevelDb[(size_t) b], std::memory_order_relaxed);
    }
    recoverMeterDb.store (moduleMix * meterRec, std::memory_order_relaxed);
    tameMeterDb.store (moduleMix * meterTam, std::memory_order_relaxed);
    biasMeter.store (meterBias, std::memory_order_relaxed);
    brightnessMeter.store (moduleMix * meterBright, std::memory_order_relaxed);
}

void SpectralUnmasker::computeCurve (const Settings& s) noexcept
{
    const float frameT = (float) hopSize / (float) sr;
    const float atk = std::exp (-frameT / 0.012f);
    const float rel = std::exp (-frameT / 0.150f);

    std::array<float, numBands> elin {}, weight {}, audible {}, ratio {}, recPart {}, tamPart {};

    // 1. smoothed band levels -------------------------------------------------
    for (int b = 0; b < numBands; ++b)
    {
        const float L = powerToDb (bandEnergy[(size_t) b]);
        float& sm = smoothLevelDb[(size_t) b];
        if (firstFrame) sm = L;
        else            sm = L > sm ? atk * sm + (1.0f - atk) * L : rel * sm + (1.0f - rel) * L;
        elin[(size_t) b] = dbToPower (sm);
        weight[(size_t) b] = rangeWeight (map.centreHz[(size_t) b], s.lowHz, s.highHz);
        audible[(size_t) b] = std::clamp ((sm - map.athDb[(size_t) b] - 3.0f) / 15.0f, 0.0f, 1.0f);
        displayWeight[(size_t) b].store (weight[(size_t) b], std::memory_order_relaxed);
    }
    firstFrame = false;

    // 2. masking received by each band from the (active, audible) others --------
    for (int i = 0; i < numBands; ++i)
    {
        float m = 0.0f;
        const float* row = spreadMatrix.data() + (size_t) i * numBands;
        for (int j = 0; j < numBands; ++j)
            m += row[j] * elin[(size_t) j] * weight[(size_t) j] * audible[(size_t) j];
        ratio[(size_t) i] = smoothLevelDb[(size_t) i] - (powerToDb (m) - 6.0f);
    }

    // 3. reference point: median masking ratio of the active bands --------------
    std::array<float, numBands> tmp {};
    int count = 0;
    for (int b = 0; b < numBands; ++b)
        if (weight[(size_t) b] * audible[(size_t) b] > 0.3f)
            tmp[(size_t) count++] = ratio[(size_t) b];
    float median = 0.0f;
    if (count >= 4)
    {
        std::nth_element (tmp.begin(), tmp.begin() + count / 2, tmp.begin() + count);
        median = tmp[(size_t) (count / 2)];
    }
    else
    {
        for (int b = 0; b < numBands; ++b) median += ratio[(size_t) b];
        median /= (float) numBands;
    }

    // 4. classification and unmasking gains --------------------------------------
    auto soft = [] (float x) { return x / (1.0f + x / 18.0f); };
    float nRec = 0.0f, nTam = 0.0f, sumRec = 0.0f, sumTam = 0.0f, active = 0.0f;

    for (int b = 0; b < numBands; ++b)
    {
        const float oct = map.octavePos[(size_t) b];
        const float threshold = s.bias * 6.0f + s.brighten * oct * 2.0f;
        const float x = (ratio[(size_t) b] - median) - threshold;
        const float wa = weight[(size_t) b] * audible[(size_t) b];

        const float tiltR = std::clamp (1.0f + s.brighten * 0.6f * oct, 0.1f, 2.5f);
        const float tiltT = std::clamp (1.0f - s.brighten * 0.6f * oct, 0.1f, 2.5f);

        const float r = x < 0.0f ? s.recover * 1.25f * soft (-x) * tiltR * wa : 0.0f;
        const float t = x > 0.0f ? s.tame    * 1.00f * soft ( x) * tiltT * wa : 0.0f;
        recPart[(size_t) b] = r;
        tamPart[(size_t) b] = t;
        targetDb[(size_t) b] = std::clamp (r - t, -18.0f, 18.0f);

        if (wa > 0.3f && (s.recover > 0.0f || s.tame > 0.0f))
        {
            (x < 0.0f ? nRec : nTam) += wa;
            active += 1.0f;
            sumRec += r;
            sumTam += t;
        }
    }

    // 5. smooth across frequency (one pass of a 1-2-1 kernel) -------------------
    for (int pass = 0; pass < 1; ++pass)
    {
        auto src = targetDb;
        for (int b = 0; b < numBands; ++b)
        {
            const float l = src[(size_t) std::max (b - 1, 0)];
            const float r = src[(size_t) std::min (b + 1, numBands - 1)];
            targetDb[(size_t) b] = 0.25f * l + 0.5f * src[(size_t) b] + 0.25f * r;
        }
    }

    // 6. add Boost, smooth in time ------------------------------------------------
    const float tc = std::exp (-frameT / 0.045f);
    for (int b = 0; b < numBands; ++b)
    {
        const float target = targetDb[(size_t) b] + s.boostDb * boostShape (map.centreHz[(size_t) b]);
        targetDb[(size_t) b] = target;
    }

    // 7. loudness compensation -------------------------------------------------
    //    Gain is matched on K-weighted energy (the BS.1770 / LUFS loudness model),
    //    so the processed signal reads the same LUFS as the input. Brightness is
    //    tracked as the centroid of Zwicker-style specific loudness.
    double e0 = 0.0, e1 = 0.0;
    float s0 = 0.0f, s1 = 0.0f, bIn = 0.0f, bOut = 0.0f;
    std::array<float, numBands> smoothedCurve {};
    for (int b = 0; b < numBands; ++b)
    {
        smoothedCurve[(size_t) b] = tc * (curveDb[(size_t) b] - compDb) + (1.0f - tc) * targetDb[(size_t) b];
        const float g = dbToPower (smoothedCurve[(size_t) b]);
        e0 += (double) kWeight[(size_t) b] * elin[(size_t) b];
        e1 += (double) kWeight[(size_t) b] * elin[(size_t) b] * g;

        const float ath = dbToPower (map.athDb[(size_t) b]);
        const float a = audible[(size_t) b];
        const float nIn  = a * std::pow (elin[(size_t) b] / ath, 0.23f);
        const float nOut = a * std::pow (elin[(size_t) b] * g / ath, 0.23f);
        s0 += nIn;  s1 += nOut;
        bIn += nIn * map.octavePos[(size_t) b];
        bOut += nOut * map.octavePos[(size_t) b];
    }

    const float cc = std::exp (-frameT / 0.250f);
    if (e0 > 1.0e-10 && e1 > 1.0e-10)
    {
        const float target = std::clamp ((float) (10.0 * std::log10 (e0 / e1)), -24.0f, 24.0f);
        compDb = cc * compDb + (1.0f - cc) * target;
    }
    if (s0 > 1.0e-3f && s1 > 1.0e-3f)
        meterBright = cc * meterBright + (1.0f - cc) * (bOut / s1 - bIn / s0);

    for (int b = 0; b < numBands; ++b)
        curveDb[(size_t) b] = smoothedCurve[(size_t) b] + compDb;

    // 8. meters ------------------------------------------------------------------
    const float mc = std::exp (-frameT / 0.120f);
    const float avgRec = active > 0.0f ? sumRec / active : 0.0f;
    const float avgTam = active > 0.0f ? sumTam / active : 0.0f;
    const float biasNow = (nRec + nTam) > 0.0f ? (nRec - nTam) / (nRec + nTam) : 0.0f;
    meterRec  = mc * meterRec  + (1.0f - mc) * avgRec;
    meterTam  = mc * meterTam  + (1.0f - mc) * avgTam;
    meterBias = mc * meterBias + (1.0f - mc) * biasNow;
}

} // namespace meridian
