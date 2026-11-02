#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace meridian
{

/** Factory presets plus user presets saved as XML files in
    ~/Music/Meridian Rack/Presets. */
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    static int getNumFactoryPresets();
    static juce::String getFactoryPresetName (int index);

    juce::StringArray getAllPresetNames() const;  // factory first, then user
    int  getNumPresets() const;
    int  getCurrentIndex() const noexcept { return currentIndex; }
    juce::String getCurrentName() const;
    bool isModified() const noexcept { return modified; }
    void markModified() noexcept { modified = true; }

    void loadPreset (int index);
    void next();
    void previous();
    bool saveUserPreset (const juce::String& name);
    void refreshUserPresets();

    static juce::File getUserPresetFolder();

private:
    void applyFactory (int index);
    void resetToDefaults();

    juce::AudioProcessorValueTreeState& apvts;
    juce::Array<juce::File> userPresets;
    int currentIndex = 0;
    bool modified = false;
};

} // namespace meridian
