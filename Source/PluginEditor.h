#pragma once

#include "PluginProcessor.h"
#include "ui/Pages.h"

namespace meridian
{

/** Bottom-bar module card: selects a page, shows status, hosts the module's power switch. */
class ModuleCard : public juce::Component
{
public:
    ModuleCard (const juce::String& name, const juce::String& subtitle, juce::Colour accent,
                juce::AudioProcessorValueTreeState* state, const char* powerParam);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onSelect;
    bool selected = false;
    juce::String status;
    juce::Colour accent;

private:
    juce::String name, subtitle;
    std::unique_ptr<ui::PowerButton> power;
};

/** Bypass switch: click toggles this instance, Shift+click bypasses every instance in the session. */
class BypassButton : public ui::PowerButton
{
public:
    BypassButton (juce::AudioProcessorValueTreeState& s) : ui::PowerButton (s, ID::bypass, ui::colours::bad, true)
    {
        setClickingTogglesState (false);
        setTooltip ("Latency-matched bypass. Shift+click bypasses every Meridian Rack in the session.");
    }
    void clicked (const juce::ModifierKeys& mods) override
    {
        if (mods.isShiftDown()) MeridianProcessor::sessionBypass.store (! MeridianProcessor::sessionBypass.load());
        else setToggleState (! getToggleState(), juce::sendNotificationSync);
    }
};

class EditorContent : public juce::Component, private juce::Timer
{
public:
    explicit EditorContent (MeridianProcessor&);
    ~EditorContent() override;

    static constexpr int baseWidth = 1180, baseHeight = 720;
    static constexpr int headerHeight = 64, barHeight = 96;

    void paint (juce::Graphics&) override;
    void resized() override;
    void showPage (int index);
    void refreshNow() { timerCallback(); }   // used by the offline snapshot test

private:
    void timerCallback() override;
    void showPresetMenu();
    void savePresetDialog();

    MeridianProcessor& proc;
    ui::RackLookAndFeel lnf;

    ui::RackKnob inputKnob, outputKnob;
    ui::LevelMeter inMeter { -60.0f, 0.0f, ui::colours::good }, outMeter { -60.0f, 0.0f, ui::colours::good };
    BypassButton bypass;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, presetName, savePreset { "SAVE" };

    ui::UnmaskPage unmaskPage;
    ui::CharacterPage characterPage;
    ui::LimiterPage limiterPage;
    ui::LoudnessPage loudnessPage;
    std::array<ui::Page*, 4> pages;
    std::array<std::unique_ptr<ModuleCard>, 4> cards;
    int currentPage = 0;
    int blink = 0;
};

class MeridianEditor : public juce::AudioProcessorEditor
{
public:
    explicit MeridianEditor (MeridianProcessor&);
    ~MeridianEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (ui::colours::stageDark); }

    EditorContent& getContent() noexcept { return content; }

private:
    EditorContent content;
    juce::TooltipWindow tooltips { this, 600 };
};

} // namespace meridian
