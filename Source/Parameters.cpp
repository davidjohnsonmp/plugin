#include "Parameters.h"

namespace meridian
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using Attr = juce::AudioParameterFloatAttributes;

static juce::String dbString (float v, int)   { return juce::String (v, 1) + " dB"; }
static juce::String pctString (float v, int)  { return juce::String (juce::roundToInt (v)) + " %"; }
static juce::String hzString (float v, int)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                        : juce::String (juce::roundToInt (v)) + " Hz";
}

static std::unique_ptr<APF> makeFloat (const char* id, const char* name, juce::NormalisableRange<float> range,
                                       float def, juce::String (*fmt) (float, int), const char* label = "")
{
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, range, def,
                                  Attr().withStringFromValueFunction (fmt).withLabel (label));
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---------------------------------------------------------------- Global
    auto global = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", "|");
    global->addChild (makeFloat (ID::inGain,  "Input",  { -24.0f, 24.0f, 0.01f }, 0.0f, dbString, "dB"));
    global->addChild (makeFloat (ID::outGain, "Output", { -24.0f, 24.0f, 0.01f }, 0.0f, dbString, "dB"));
    global->addChild (std::make_unique<APB> (juce::ParameterID { ID::bypass, 1 }, "Bypass", false));
    global->addChild (std::make_unique<APB> (juce::ParameterID { ID::matchBypass, 1 }, "Level-Matched Bypass", true));
    layout.add (std::move (global));

    // ---------------------------------------------------------------- Unmask EQ
    auto um = std::make_unique<juce::AudioProcessorParameterGroup> ("unmask", "Unmask EQ", "|");
    um->addChild (std::make_unique<APB> (juce::ParameterID { ID::umOn, 1 }, "Unmask On", true));
    um->addChild (makeFloat (ID::umRecover,  "Recover",  { 0.0f, 200.0f, 0.1f }, 0.0f, pctString, "%"));
    um->addChild (makeFloat (ID::umTame,     "Tame",     { 0.0f, 200.0f, 0.1f }, 0.0f, pctString, "%"));
    um->addChild (makeFloat (ID::umBias,     "Bias",     { -100.0f, 100.0f, 0.1f }, 0.0f, pctString, "%"));
    um->addChild (makeFloat (ID::umBrighten, "Brighten", { -100.0f, 100.0f, 0.1f }, 0.0f, pctString, "%"));
    um->addChild (makeFloat (ID::umBoost,    "Boost",    { -50.0f, 50.0f, 0.1f }, 0.0f, dbString, "dB"));

    juce::NormalisableRange<float> freqRange (20.0f, 20000.0f, 0.1f);
    freqRange.setSkewForCentre (630.0f);
    um->addChild (makeFloat (ID::umLowLimit,  "Range Low",  freqRange, 20.0f,    hzString, "Hz"));
    um->addChild (makeFloat (ID::umHighLimit, "Range High", freqRange, 20000.0f, hzString, "Hz"));
    um->addChild (makeFloat (ID::umGain, "Unmask Gain", { -12.0f, 12.0f, 0.01f }, 0.0f, dbString, "dB"));
    um->addChild (std::make_unique<APB> (juce::ParameterID { ID::umSidechain, 1 }, "Unmask Sidechain", false));
    um->addChild (std::make_unique<APC> (juce::ParameterID { ID::umQuality, 1 }, "Unmask Quality",
                                         juce::StringArray { "Live", "Normal", "Best" }, 1));
    layout.add (std::move (um));

    // ---------------------------------------------------------------- Character
    auto ch = std::make_unique<juce::AudioProcessorParameterGroup> ("character", "Character", "|");
    ch->addChild (std::make_unique<APB> (juce::ParameterID { ID::chOn, 1 }, "Character On", true));
    ch->addChild (makeFloat (ID::chAmount, "Amount", { 0.0f, 200.0f, 0.1f }, 100.0f, pctString, "%"));
    ch->addChild (makeFloat (ID::chLow,  "Low",  { -12.0f, 12.0f, 0.01f }, 0.0f, dbString, "dB"));
    ch->addChild (makeFloat (ID::chMid,  "Mid",  { -12.0f, 12.0f, 0.01f }, 0.0f, dbString, "dB"));
    ch->addChild (makeFloat (ID::chHigh, "High", { -12.0f, 12.0f, 0.01f }, 0.0f, dbString, "dB"));
    ch->addChild (std::make_unique<APB> (juce::ParameterID { ID::chSidechain, 1 }, "Character Sidechain (EXT)", false));
    ch->addChild (std::make_unique<APC> (juce::ParameterID { ID::chOversample, 1 }, "Oversampling",
                                         juce::StringArray { "Off", "2x", "4x" }, 1));
    layout.add (std::move (ch));

    // ---------------------------------------------------------------- Limiter
    auto lim = std::make_unique<juce::AudioProcessorParameterGroup> ("limiter", "Limiter", "|");
    lim->addChild (std::make_unique<APB> (juce::ParameterID { ID::limOn, 1 }, "Limiter On", true));
    lim->addChild (makeFloat (ID::limGain,    "Limiter Drive",   { 0.0f, 18.0f, 0.01f }, 3.0f, dbString, "dB"));
    lim->addChild (makeFloat (ID::limCeiling, "Limiter Ceiling", { -6.0f, 0.0f, 0.01f }, -1.0f, dbString, "dBTP"));
    layout.add (std::move (lim));

    // ---------------------------------------------------------------- Loudness
    auto ld = std::make_unique<juce::AudioProcessorParameterGroup> ("loudness", "Loudness", "|");
    juce::StringArray targetNames;
    for (auto& t : loudnessTargets) targetNames.add (t.name);
    ld->addChild (std::make_unique<APC> (juce::ParameterID { ID::lufsTarget, 1 }, "Loudness Target", targetNames, 0));
    ld->addChild (std::make_unique<APB> (juce::ParameterID { ID::refPlay, 1 }, "Listen to Reference", false));
    ld->addChild (std::make_unique<APB> (juce::ParameterID { ID::refMatch, 1 }, "Level-Match Reference", true));
    layout.add (std::move (ld));

    return layout;
}

} // namespace meridian
