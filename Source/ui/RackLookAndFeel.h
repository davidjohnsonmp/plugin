#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace meridian::ui
{

namespace colours
{
    inline const juce::Colour stageDark   { 0xff0b0c0e };
    inline const juce::Colour stageLight  { 0xff1c1e23 };
    inline const juce::Colour header      { 0xff121316 };
    inline const juce::Colour plateTop    { 0xff33363d };
    inline const juce::Colour plateBottom { 0xff23252a };
    inline const juce::Colour plateEdge   { 0xff0d0e10 };
    inline const juce::Colour screen      { 0xff06080b };
    inline const juce::Colour screenGrid  { 0xff18202a };
    inline const juce::Colour text        { 0xffe9e6df };
    inline const juce::Colour textDim     { 0xff8b9099 };
    inline const juce::Colour textFaint   { 0xff5a5f68 };
    inline const juce::Colour good        { 0xff5ad17a };
    inline const juce::Colour warn        { 0xfff0b43c };
    inline const juce::Colour bad         { 0xffef4a4a };

    // module accents
    inline const juce::Colour unmask      { 0xff42d4cb };
    inline const juce::Colour character   { 0xffeaa743 };
    inline const juce::Colour limiter     { 0xffef5b5b };
    inline const juce::Colour loudness    { 0xff8ac6ff };
}

namespace fonts
{
    juce::Font title (float height);     // condensed, bold
    juce::Font label (float height);     // condensed, medium
    juce::Font numeric (float height);   // tabular figures for readouts
}

/** Drawing helpers shared by every page. */
namespace paint
{
    void stage (juce::Graphics&, juce::Rectangle<float> area);
    void faceplate (juce::Graphics&, juce::Rectangle<float> area, juce::Colour accent, const juce::String& name);
    void screw (juce::Graphics&, juce::Point<float> centre, float radius, float angle);
    void screen (juce::Graphics&, juce::Rectangle<float> area, float corner = 6.0f);
    void engraved (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                   juce::Justification just, juce::Font font, juce::Colour colour = colours::textDim);
    void sectionTitle (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, juce::Colour accent);
    const juce::Image& brushedTexture();
}

class RackLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RackLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
};

} // namespace meridian::ui
