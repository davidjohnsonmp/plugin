#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "RackLookAndFeel.h"

namespace meridian::ui
{

/** Rotary slider with the rack feel:
      drag            adjust
      Shift + drag    fine adjust
      Alt + click     jump between zero/default and the previous value (quick A/B)
      double-click    reset to default
      mouse wheel     adjust */
class KnobSlider : public juce::Slider
{
public:
    KnobSlider();
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    double defaultValue = 0.0;

private:
    double previousValue = 0.0;
    bool hasPrevious = false;
};

class RackKnob : public juce::Component
{
public:
    enum class Size { small, medium, large, huge };

    RackKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
              const juce::String& title, juce::Colour accent, Size size = Size::medium);

    void paint (juce::Graphics&) override;
    void resized() override;
    void setTooltip (const juce::String& t) { slider.setTooltip (t); }

    KnobSlider slider;
    juce::String title;
    bool showValue = true;

private:
    juce::Colour accent;
    Size size;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Round power switch with an LED, bound to a bool parameter. */
class PowerButton : public juce::Button
{
public:
    PowerButton (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, juce::Colour accent,
                 bool invertedLed = false);
    PowerButton (juce::Colour accent);   // unbound
    void paintButton (juce::Graphics&, bool over, bool down) override;

    bool ledMeansOff = false;  // for a bypass switch the LED lights when bypassed

private:
    juce::Colour accent;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** Text toggle bound to a bool parameter. */
class ParamToggle : public juce::TextButton
{
public:
    ParamToggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                 const juce::String& text, juce::Colour accent);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** Row of mutually exclusive buttons bound to a choice (or bool) parameter. */
class Segmented : public juce::Component
{
public:
    Segmented (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
               const juce::StringArray& labels, juce::Colour accent);
    ~Segmented() override;
    void resized() override;
    void setTooltip (const juce::String& t) { for (auto* b : buttons) b->setTooltip (t); }
    void paint (juce::Graphics&) override;

private:
    void setIndex (int index);
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::RangedAudioParameter* param = nullptr;
};

/** Vertical bar meter with optional target zone, peak hold and scale. */
class LevelMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    LevelMeter (float minDb, float maxDb, juce::Colour colour, bool topDown = false);

    void setLevel (float db) noexcept;
    void setTarget (float lowDb, float highDb) noexcept { targetLo = lowDb; targetHi = highDb; hasTarget = true; }
    void setScale (std::initializer_list<float> marks) { scaleMarks = marks; }
    void resetPeak() noexcept { peak = minDb; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { resetPeak(); repaint(); }

    bool showScale = true;

private:
    float proportion (float db) const noexcept;

    float minDb, maxDb;
    juce::Colour colour;
    bool topDown;
    float level, peak;
    int peakHoldFrames = 0;
    float targetLo = 0, targetHi = 0;
    bool hasTarget = false;
    std::vector<float> scaleMarks;
};

/** Horizontal centre-zero bar (e.g. brightness deviation). */
class CentreBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    CentreBar (float r, juce::Colour c) : range (r), colour (c) {}
    void setValue (float v) noexcept { value = v; }
    void paint (juce::Graphics&) override;

private:
    float range, value = 0.0f;
    juce::Colour colour;
};

inline void setAccent (juce::Component& c, juce::Colour accent)
{
    c.getProperties().set ("accent", (juce::int64) accent.getARGB());
}

juce::String formatLufs (float v, int decimals = 1);

} // namespace meridian::ui
