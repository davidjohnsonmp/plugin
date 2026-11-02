#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace meridian
{

std::atomic<bool> MeridianProcessor::sessionBypass { false };

MeridianProcessor::MeridianProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MERIDIAN", createParameterLayout()),
      presets (apvts)
{
    pIn = p (ID::inGain); pOut = p (ID::outGain); pBypass = p (ID::bypass); pMatch = p (ID::matchBypass);
    pUmOn = p (ID::umOn); pRec = p (ID::umRecover); pTame = p (ID::umTame); pBias = p (ID::umBias);
    pBright = p (ID::umBrighten); pBoost = p (ID::umBoost); pLow = p (ID::umLowLimit); pHigh = p (ID::umHighLimit);
    pUmGain = p (ID::umGain); pUmSc = p (ID::umSidechain); pQuality = p (ID::umQuality);
    pChOn = p (ID::chOn); pAmount = p (ID::chAmount); pChLow = p (ID::chLow); pChMid = p (ID::chMid);
    pChHigh = p (ID::chHigh); pChSc = p (ID::chSidechain); pOs = p (ID::chOversample);
    pLimOn = p (ID::limOn); pLimGain = p (ID::limGain); pLimCeil = p (ID::limCeiling);
    pRefPlay = p (ID::refPlay); pRefMatch = p (ID::refMatch);

    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            apvts.addParameterListener (rp->getParameterID(), this);

    startTimerHz (10);
}

MeridianProcessor::~MeridianProcessor()
{
    stopTimer();
    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            apvts.removeParameterListener (rp->getParameterID(), this);
}

bool MeridianProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainIn != mainOut) return false;
    if (mainOut != juce::AudioChannelSet::stereo() && mainOut != juce::AudioChannelSet::mono()) return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void MeridianProcessor::configure (double sampleRate, int blockSize)
{
    currentRate = sampleRate;
    maxBlock = std::max (32, blockSize);
    numChannels = std::max (1, getMainBusNumOutputChannels());

    appliedQuality = juce::jlimit (0, 2, (int) pQuality->load());
    appliedOversampling = juce::jlimit (0, 2, (int) pOs->load());

    unmasker.prepare (sampleRate, numChannels, (SpectralUnmasker::Quality) appliedQuality);
    character.prepare (sampleRate, maxBlock, numChannels, appliedOversampling);
    limiter.prepare (sampleRate, maxBlock, numChannels);
    inputLoudness.prepare (sampleRate, numChannels);
    outputLoudness.prepare (sampleRate, numChannels);
    outputTonal.prepare (sampleRate);
    reference.prepare (sampleRate, numChannels);

    totalLatency = unmasker.getLatencySamples() + character.getLatencySamples() + limiter.getLatencySamples();

    bypassDelay.setSize (numChannels, juce::nextPowerOfTwo (totalLatency + maxBlock + 1));
    bypassDelay.clear();
    bypassWrite = 0;
    refBuffer.setSize (numChannels, maxBlock);
    scScratch.setSize (2, maxBlock);

    for (auto* s : { &inGainS, &outGainS, &matchGainS, &refGainS })
        s->reset (sampleRate, 0.05);
    bypassMix.reset (sampleRate, 0.03);
    refMix.reset (sampleRate, 0.03);
    inGainS.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pIn->load()));
    outGainS.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    bypassMix.setCurrentAndTargetValue (isEffectivelyBypassed() ? 1.0f : 0.0f);
    matchGainS.setCurrentAndTargetValue (1.0f);
    refMix.setCurrentAndTargetValue (pRefPlay->load() > 0.5f ? 1.0f : 0.0f);
    refGainS.setCurrentAndTargetValue (1.0f);

    setLatencySamples (totalLatency);
    prepared = true;
}

void MeridianProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    configure (sampleRate, samplesPerBlock);
    pendingReconfigure.store (false);
}

void MeridianProcessor::reset()
{
    unmasker.reset();
    character.reset();
    limiter.reset();
    bypassDelay.clear();
}

void MeridianProcessor::timerCallback()
{
    if (! prepared) return;

    const int q = juce::jlimit (0, 2, (int) pQuality->load());
    const int os = juce::jlimit (0, 2, (int) pOs->load());
    if (q != appliedQuality || os != appliedOversampling)
    {
        // Re-allocate the affected engines safely: the wrapper will not call
        // processBlock while processing is suspended.
        suspendProcessing (true);
        configure (currentRate, maxBlock);
        suspendProcessing (false);
        updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withLatencyChanged (true));
    }
}

void MeridianProcessor::parameterChanged (const juce::String& id, float)
{
    if (id != ID::bypass && id != ID::refPlay && id != ID::refMatch && id != ID::lufsTarget)
        presets.markModified();
}

bool MeridianProcessor::isEffectivelyBypassed() const
{
    return pBypass->load() > 0.5f || sessionBypass.load();
}

void MeridianProcessor::resetLoudness()
{
    inputLoudness.requestReset();
    outputLoudness.requestReset();
    outputTonal.requestReset();
}

void MeridianProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (! prepared) { buffer.clear(); return; }

    auto main = getBusBuffer (buffer, false, 0);
    const bool hasSc = getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled();
    auto scBus = hasSc ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const juce::AudioBuffer<float>* scPtr = scBus.getNumChannels() > 0 ? &scBus : nullptr;

    // Clear any output channels beyond the inputs.
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    const int total = main.getNumSamples();
    for (int offset = 0; offset < total; offset += maxBlock)
    {
        const int len = std::min (maxBlock, total - offset);
        juce::AudioBuffer<float> view (main.getArrayOfWritePointers(), main.getNumChannels(), offset, len);
        if (scPtr != nullptr)
        {
            // Copy the sidechain chunk so the engines see a stable, correctly sized buffer.
            const int scCh = std::min (scPtr->getNumChannels(), scScratch.getNumChannels());
            juce::AudioBuffer<float> scView (scScratch.getArrayOfWritePointers(), scCh, 0, len);
            for (int c = 0; c < scCh; ++c) scView.copyFrom (c, 0, *scPtr, c, offset, len);
            processChunk (view, &scView);
        }
        else
        {
            processChunk (view, nullptr);
        }
    }
}

void MeridianProcessor::processChunk (juce::AudioBuffer<float>& buf, const juce::AudioBuffer<float>* sc)
{
    const int n = buf.getNumSamples();
    const int chans = std::min (buf.getNumChannels(), numChannels);

    // ---- keep the raw input (for latency-aligned bypass)
    const int dlen = bypassDelay.getNumSamples(), dmask = dlen - 1;
    const int start = bypassWrite;
    for (int c = 0; c < chans; ++c)
    {
        auto* d = bypassDelay.getWritePointer (c);
        const auto* s = buf.getReadPointer (c);
        for (int i = 0; i < n; ++i) d[(start + i) & dmask] = s[i];
    }
    bypassWrite = (start + n) & dmask;

    // ---- raw input loudness (used for level-matched bypass)
    inputLoudness.process (buf.getArrayOfReadPointers(), chans, n);

    // ---- input gain
    inGainS.setTargetValue (juce::Decibels::decibelsToGain (pIn->load()));
    float inPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = inGainS.getNextValue();
        for (int c = 0; c < chans; ++c)
        {
            const float v = buf.getSample (c, i) * g;
            buf.setSample (c, i, v);
            inPeak = std::max (inPeak, std::abs (v));
        }
    }
    inputPeakDb.store (juce::Decibels::gainToDecibels (inPeak, -100.0f), std::memory_order_relaxed);

    // ---- 1. Unmask EQ
    SpectralUnmasker::Settings us;
    us.enabled = pUmOn->load() > 0.5f;
    us.recover = pRec->load() * 0.01f;
    us.tame = pTame->load() * 0.01f;
    us.bias = pBias->load() * 0.01f;
    us.brighten = pBright->load() * 0.01f;
    us.boostDb = pBoost->load();
    us.lowHz = pLow->load();
    us.highHz = pHigh->load();
    us.gainDb = pUmGain->load();
    us.useSidechain = pUmSc->load() > 0.5f;
    unmasker.process (buf, sc, us);

    // ---- 2. Character
    CharacterChain::Settings cs;
    cs.enabled = pChOn->load() > 0.5f;
    cs.amount = pAmount->load() * 0.01f;
    cs.lowDb = pChLow->load();
    cs.midDb = pChMid->load();
    cs.highDb = pChHigh->load();
    cs.externalSidechain = pChSc->load() > 0.5f;
    character.process (buf, sc, cs);

    // ---- 3. Limiter
    limiter.process (buf, pLimOn->load() > 0.5f, pLimGain->load(), pLimCeil->load());

    // ---- output gain
    outGainS.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = outGainS.getNextValue();
        for (int c = 0; c < chans; ++c) buf.setSample (c, i, buf.getSample (c, i) * g);
    }

    // ---- output metering (always the processed signal)
    outputLoudness.process (buf.getArrayOfReadPointers(), chans, n);
    outputTonal.push (buf.getArrayOfReadPointers(), chans, n);

    // ---- latency-aligned, optionally level-matched bypass
    const bool bypassed = isEffectivelyBypassed();
    bypassMix.setTargetValue (bypassed ? 1.0f : 0.0f);
    float matchDb = 0.0f;
    if (pMatch->load() > 0.5f)
    {
        const float inL = inputLoudness.shortTerm.load(), outL = outputLoudness.shortTerm.load();
        if (inL > -70.0f && outL > -70.0f) matchDb = juce::jlimit (-24.0f, 24.0f, outL - inL);
    }
    bypassMatchGainDb.store (matchDb, std::memory_order_relaxed);
    matchGainS.setTargetValue (juce::Decibels::decibelsToGain (matchDb));

    if (bypassed || bypassMix.isSmoothing())
    {
        for (int i = 0; i < n; ++i)
        {
            const float mx = bypassMix.getNextValue();
            const float mg = matchGainS.getNextValue();
            for (int c = 0; c < chans; ++c)
            {
                const float dry = bypassDelay.getSample (c, (start + i - totalLatency) & dmask) * mg;
                const float wet = buf.getSample (c, i);
                buf.setSample (c, i, wet + mx * (dry - wet));
            }
        }
    }
    else
    {
        matchGainS.skip (n);
    }

    // ---- reference playback (replaces the output while engaged)
    const bool refOn = pRefPlay->load() > 0.5f;
    refMix.setTargetValue (refOn ? 1.0f : 0.0f);
    if (refOn || refMix.isSmoothing())
    {
        const float refI = reference.integratedLufs.load();
        float refDb = 0.0f;
        if (pRefMatch->load() > 0.5f && refI > -70.0f)
        {
            const float mixI = outputLoudness.integrated.load();
            const float mixS = outputLoudness.shortTerm.load();
            const float mixL = mixI > -70.0f ? mixI : mixS;
            if (mixL > -70.0f) refDb = juce::jlimit (-30.0f, 12.0f, mixL - refI);
        }
        referenceGainDb.store (refDb, std::memory_order_relaxed);
        refGainS.setTargetValue (juce::Decibels::decibelsToGain (refDb));

        juce::AudioBuffer<float> rv (refBuffer.getArrayOfWritePointers(), chans, 0, n);
        reference.render (rv, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const float mx = refMix.getNextValue();
            const float rg = refGainS.getNextValue();
            for (int c = 0; c < chans; ++c)
            {
                const float out = buf.getSample (c, i);
                buf.setSample (c, i, out + mx * (rv.getSample (c, i) * rg - out));
            }
        }
    }

    float outPeak = 0.0f;
    for (int c = 0; c < chans; ++c)
        outPeak = std::max (outPeak, buf.getMagnitude (c, 0, n));
    outputPeakDb.store (juce::Decibels::gainToDecibels (outPeak, -100.0f), std::memory_order_relaxed);
}

// =============================================================================
void MeridianProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    auto xml = state.createXml();
    auto* extra = xml->createNewChildElement ("MERIDIAN_EXTRA");
    extra->setAttribute ("referenceFile", reference.getFile().getFullPathName());
    extra->setAttribute ("uiPage", uiPage);
    extra->setAttribute ("presetIndex", presets.getCurrentIndex());
    copyXmlToBinary (*xml, destData);
}

void MeridianProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;

    if (auto* extra = xml->getChildByName ("MERIDIAN_EXTRA"))
    {
        uiPage = extra->getIntAttribute ("uiPage", 0);
        const juce::File f (extra->getStringAttribute ("referenceFile"));
        if (f.existsAsFile() && f != reference.getFile())
            reference.load (f);
        xml->removeChildElement (extra, true);
    }

    if (xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MeridianProcessor::createEditor()
{
    return new MeridianEditor (*this);
}

} // namespace meridian

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new meridian::MeridianProcessor();
}
