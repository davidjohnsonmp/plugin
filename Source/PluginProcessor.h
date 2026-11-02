#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Presets.h"
#include "dsp/SpectralUnmasker.h"
#include "dsp/CharacterChain.h"
#include "dsp/Limiter.h"
#include "dsp/LoudnessMeter.h"
#include "dsp/ReferenceTrack.h"

namespace meridian
{

class MeridianProcessor : public juce::AudioProcessor,
                          private juce::Timer,
                          private juce::AudioProcessorValueTreeState::Listener
{
public:
    MeridianProcessor();
    ~MeridianProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Meridian Rack"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return PresetManager::getNumFactoryPresets(); }
    int getCurrentProgram() override { return std::min (presets.getCurrentIndex(), getNumPrograms() - 1); }
    void setCurrentProgram (int index) override { presets.loadPreset (index); }
    const juce::String getProgramName (int index) override { return PresetManager::getFactoryPresetName (index); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- shared with the editor
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;

    SpectralUnmasker unmasker;
    CharacterChain character;
    Limiter limiter;
    LoudnessMeter inputLoudness, outputLoudness;
    TonalAnalyzer outputTonal;
    ReferenceTrack reference;

    std::atomic<float> inputPeakDb { -100.0f }, outputPeakDb { -100.0f };
    std::atomic<float> bypassMatchGainDb { 0.0f }, referenceGainDb { 0.0f };
    std::atomic<bool>  pendingReconfigure { false };

    static std::atomic<bool> sessionBypass;   // shift-click bypass shared by all instances
    bool isEffectivelyBypassed() const;

    int  getActiveQuality() const noexcept { return appliedQuality; }
    int  getActiveOversampling() const noexcept { return appliedOversampling; }
    double getCurrentSampleRate() const noexcept { return currentRate; }

    int uiPage = 0;   // remembered editor page

    void resetLoudness();

private:
    void timerCallback() override;
    void parameterChanged (const juce::String& id, float) override;
    void configure (double sampleRate, int blockSize);
    void processChunk (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>* sidechain);

    std::atomic<float>* p (const char* id) { return apvts.getRawParameterValue (id); }

    double currentRate = 0.0;
    int maxBlock = 512, numChannels = 2;
    int appliedQuality = 1, appliedOversampling = 1;
    int totalLatency = 0;
    bool prepared = false;

    juce::SmoothedValue<float> inGainS, outGainS, bypassMix, matchGainS, refMix, refGainS;

    juce::AudioBuffer<float> bypassDelay, refBuffer, scScratch;
    int bypassWrite = 0;

    // cached parameter pointers
    std::atomic<float> *pIn{}, *pOut{}, *pBypass{}, *pMatch{};
    std::atomic<float> *pUmOn{}, *pRec{}, *pTame{}, *pBias{}, *pBright{}, *pBoost{}, *pLow{}, *pHigh{}, *pUmGain{}, *pUmSc{}, *pQuality{};
    std::atomic<float> *pChOn{}, *pAmount{}, *pChLow{}, *pChMid{}, *pChHigh{}, *pChSc{}, *pOs{};
    std::atomic<float> *pLimOn{}, *pLimGain{}, *pLimCeil{};
    std::atomic<float> *pRefPlay{}, *pRefMatch{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeridianProcessor)
};

} // namespace meridian
