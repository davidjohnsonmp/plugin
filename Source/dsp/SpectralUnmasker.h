#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "BandMap.h"

namespace meridian
{

/** Adaptive, perception-driven equaliser.

    Every analysis frame the spectrum is split into ERB-spaced bands, the
    masking that each band receives from its neighbours is estimated with a
    psychoacoustic spreading function, and each band is classified as
    "dominated" (masked by others) or "dominant" (masking others).

      Recover  lifts dominated bands.
      Tame     pulls dominant bands down.
      Bias     moves the dividing line between the two classes.
      Brighten tilts how the unmasking is solved (prefer highs or lows).
      Boost    simulates the bass/mid balance change of a volume change.

    The resulting curve is loudness-compensated so the output keeps the same
    perceived loudness, and applied with an overlap-add STFT (zero-phase gains,
    so the processing adds no phase smear and the dynamics are preserved). */
class SpectralUnmasker
{
public:
    static constexpr int numBands = BandMap::numBands;

    enum class Quality { live = 0, normal = 1, best = 2 };

    struct Settings
    {
        bool  enabled = true;
        float recover = 0.0f;   // 0..2
        float tame = 0.0f;      // 0..2
        float bias = 0.0f;      // -1..1
        float brighten = 0.0f;  // -1..1
        float boostDb = 0.0f;   // -50..50
        float lowHz = 20.0f;
        float highHz = 20000.0f;
        float gainDb = 0.0f;
        bool  useSidechain = false;
    };

    void prepare (double sampleRate, int numChannels, Quality quality);
    void reset();
    int  getLatencySamples() const noexcept { return fftSize; }
    int  getFftSize() const noexcept { return fftSize; }

    /** main is processed in place. sidechain may be nullptr. */
    void process (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>* sidechain, const Settings& s) noexcept;

    const BandMap& getBandMap() const noexcept { return map; }

    // ---- data for the display / meters (written by the audio thread) ----
    std::array<std::atomic<float>, numBands> displayCurveDb {};   // applied curve without module gain
    std::array<std::atomic<float>, numBands> displayWeight {};    // 0..1 active-range weight
    std::array<std::atomic<float>, numBands> displayInputDb {};   // analysed band levels
    std::atomic<float> recoverMeterDb { 0.0f }, tameMeterDb { 0.0f };
    std::atomic<float> biasMeter { 0.0f };       // -1 (tame wider) .. +1 (recover wider)
    std::atomic<float> brightnessMeter { 0.0f }; // output minus input brightness, in octaves

private:
    void processFrame (const Settings& s) noexcept;
    void computeCurve (const Settings& s) noexcept;
    float boostShape (float hz) const noexcept;
    float rangeWeight (float hz, float lowHz, float highHz) const noexcept;

    double sr = 48000.0;
    int fftOrder = 11, fftSize = 2048, hopSize = 256, numChans = 2;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    float olaNorm = 1.0f;

    std::vector<std::vector<float>> inFifo, outFifo;
    std::vector<float> anaFifo;
    int writePos = 0, readPos = 0, hopCounter = 0;

    std::vector<float> fftBuf, power, binGain;
    BandMap map;

    std::array<float, numBands> bandEnergy {}, smoothLevelDb {}, curveDb {}, targetDb {};
    std::vector<float> spreadMatrix;        // numBands * numBands linear spreading factors
    std::array<float, numBands> kWeight {}; // K-weighting power response per band (loudness compensation)
    float compDb = 0.0f, moduleMix = 1.0f, gainDbSmoothed = 0.0f;
    float meterRec = 0.0f, meterTam = 0.0f, meterBias = 0.0f, meterBright = 0.0f;
    bool  firstFrame = true;
};

} // namespace meridian
