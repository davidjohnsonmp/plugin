#include "Presets.h"
#include "Parameters.h"

namespace meridian
{

namespace
{
    struct Setting { const char* id; float value; };
    struct Factory { const char* name; std::vector<Setting> settings; };

    const std::vector<Factory>& factoryPresets()
    {
        static const std::vector<Factory> presets {
            { "Default", {} },
            { "Clear Mix Polish", {
                { ID::umRecover, 60 }, { ID::umTame, 50 },
                { ID::chAmount, 100 }, { ID::limGain, 3 } } },
            { "Bright Pop Master", {
                { ID::umRecover, 80 }, { ID::umTame, 60 }, { ID::umBrighten, 25 }, { ID::umBoost, -2 },
                { ID::chAmount, 120 }, { ID::chHigh, 1.0f }, { ID::limGain, 5 }, { ID::limCeiling, -1.0f } } },
            { "Warm Hip-Hop Bus", {
                { ID::umRecover, 50 }, { ID::umTame, 70 }, { ID::umBrighten, -20 }, { ID::umBoost, 6 },
                { ID::chAmount, 140 }, { ID::chLow, 1.5f }, { ID::limGain, 4 } } },
            { "Vocal Forward", {
                { ID::umRecover, 90 }, { ID::umTame, 40 }, { ID::umBias, 20 },
                { ID::umLowLimit, 150 }, { ID::umHighLimit, 8000 },
                { ID::chAmount, 80 }, { ID::chMid, 1.0f }, { ID::limGain, 3 } } },
            { "Gentle Master", {
                { ID::umRecover, 35 }, { ID::umTame, 35 },
                { ID::chAmount, 60 }, { ID::limGain, 2 }, { ID::limCeiling, -1.0f } } },
            { "Loud Club Master", {
                { ID::umRecover, 70 }, { ID::umTame, 80 }, { ID::umBias, -10 }, { ID::umBoost, 4 },
                { ID::chAmount, 160 }, { ID::limGain, 8 }, { ID::limCeiling, -0.3f }, { ID::lufsTarget, 4 } } },
            { "Unmask Only", {
                { ID::umRecover, 100 }, { ID::umTame, 100 },
                { ID::chOn, 0 }, { ID::limOn, 0 } } },
            { "Character Only", {
                { ID::umOn, 0 }, { ID::chAmount, 100 }, { ID::limGain, 3 } } },
        };
        return presets;
    }

    constexpr const char* presetExtension = ".mrpreset";
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    refreshUserPresets();
}

int PresetManager::getNumFactoryPresets()          { return (int) factoryPresets().size(); }
juce::String PresetManager::getFactoryPresetName (int i)
{
    return juce::isPositiveAndBelow (i, getNumFactoryPresets()) ? juce::String (factoryPresets()[(size_t) i].name) : juce::String();
}

juce::File PresetManager::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("Meridian Rack").getChildFile ("Presets");
}

void PresetManager::refreshUserPresets()
{
    userPresets.clear();
    auto folder = getUserPresetFolder();
    if (folder.isDirectory())
        userPresets = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetExtension);
    std::sort (userPresets.begin(), userPresets.end(),
               [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });
}

juce::StringArray PresetManager::getAllPresetNames() const
{
    juce::StringArray names;
    for (auto& f : factoryPresets()) names.add (f.name);
    for (auto& f : userPresets) names.add (f.getFileNameWithoutExtension());
    return names;
}

int PresetManager::getNumPresets() const { return getNumFactoryPresets() + userPresets.size(); }

juce::String PresetManager::getCurrentName() const
{
    auto names = getAllPresetNames();
    return names[currentIndex] + (modified ? " *" : "");
}

void PresetManager::resetToDefaults()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != ID::bypass)
                rp->setValueNotifyingHost (rp->getDefaultValue());
}

void PresetManager::applyFactory (int index)
{
    resetToDefaults();
    for (auto& s : factoryPresets()[(size_t) index].settings)
        if (auto* p = apvts.getParameter (s.id))
            p->setValueNotifyingHost (p->convertTo0to1 (s.value));
}

void PresetManager::loadPreset (int index)
{
    const int total = getNumPresets();
    if (total == 0) return;
    index = ((index % total) + total) % total;

    if (index < getNumFactoryPresets())
    {
        applyFactory (index);
    }
    else
    {
        const auto file = userPresets[index - getNumFactoryPresets()];
        if (auto xml = juce::XmlDocument::parse (file))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            if (tree.isValid())
            {
                resetToDefaults();
                for (auto child : tree)
                    if (child.hasType ("PARAM"))
                        if (auto* p = apvts.getParameter (child["id"].toString()))
                            if (child["id"].toString() != ID::bypass)
                                p->setValueNotifyingHost (p->convertTo0to1 ((float) child["value"]));
            }
        }
    }
    currentIndex = index;
    modified = false;
}

void PresetManager::next()     { loadPreset (currentIndex + 1); }
void PresetManager::previous() { loadPreset (currentIndex - 1); }

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return false;

    auto folder = getUserPresetFolder();
    if (! folder.createDirectory()) return false;

    juce::ValueTree tree ("MERIDIAN_PRESET");
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            juce::ValueTree child ("PARAM");
            child.setProperty ("id", rp->getParameterID(), nullptr);
            child.setProperty ("value", rp->convertFrom0to1 (rp->getValue()), nullptr);
            tree.appendChild (child, nullptr);
        }

    auto file = folder.getChildFile (name + presetExtension);
    if (auto xml = tree.createXml(); xml == nullptr || ! xml->writeTo (file))
        return false;

    refreshUserPresets();
    const int idx = userPresets.indexOf (file);
    currentIndex = idx >= 0 ? getNumFactoryPresets() + idx : currentIndex;
    modified = false;
    return true;
}

} // namespace meridian
