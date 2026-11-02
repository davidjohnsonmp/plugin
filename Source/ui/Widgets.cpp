#include "Widgets.h"

namespace meridian::ui
{

juce::String formatLufs (float v, int decimals)
{
    if (v <= -99.0f) return "-inf";
    return juce::String (v, decimals);
}

// =============================================================================
KnobSlider::KnobSlider()
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
    setMouseDragSensitivity (220);
    setScrollWheelEnabled (true);
}

void KnobSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isAltDown() && e.mods.isLeftButtonDown())
    {
        const double v = getValue();
        if (std::abs (v - defaultValue) > 1.0e-6 || ! hasPrevious)
        {
            previousValue = v;
            hasPrevious = true;
            setValue (defaultValue, juce::sendNotificationSync);
        }
        else
        {
            setValue (previousValue, juce::sendNotificationSync);
        }
        return;
    }
    setMouseDragSensitivity (e.mods.isShiftDown() ? 1100 : 220);
    juce::Slider::mouseDown (e);
}

void KnobSlider::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isAltDown()) return;
    juce::Slider::mouseUp (e);
}

// =============================================================================
RackKnob::RackKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                    const juce::String& t, juce::Colour a, Size s)
    : title (t), accent (a), size (s)
{
    setAccent (slider, accent);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

    if (auto* p = state.getParameter (paramId))
    {
        slider.defaultValue = p->convertFrom0to1 (p->getDefaultValue());
        // Primary unmask controls toggle against zero rather than the default.
        slider.setDoubleClickReturnValue (true, slider.defaultValue);
        slider.setTooltip (p->getName (64) + "  -  drag, Shift+drag for fine, Alt+click to A/B, double-click to reset");
    }
    slider.onValueChange = [this] { repaint(); };
}

void RackKnob::resized()
{
    auto r = getLocalBounds();
    const int titleH = size == Size::huge ? 26 : (size == Size::small ? 15 : 18);
    const int valueH = showValue ? (size == Size::huge ? 26 : (size == Size::small ? 16 : 20)) : 0;
    r.removeFromTop (titleH);
    r.removeFromBottom (valueH);
    const int side = std::min (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (side, side));
}

void RackKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float titleH = size == Size::huge ? 26.0f : (size == Size::small ? 15.0f : 18.0f);
    const float tSize  = size == Size::huge ? 20.0f : (size == Size::small ? 12.0f : 14.0f);
    paint::engraved (g, title.toUpperCase(), r.removeFromTop (titleH), juce::Justification::centred,
                     fonts::label (tSize), colours::text.withAlpha (0.9f));

    if (showValue)
    {
        const float valueH = size == Size::huge ? 26.0f : (size == Size::small ? 16.0f : 20.0f);
        auto vr = r.removeFromBottom (valueH);
        const auto text = slider.getTextFromValue (slider.getValue());
        const auto f = fonts::numeric (size == Size::huge ? 15.0f : (size == Size::small ? 10.5f : 12.0f));
        const float w = std::min (vr.getWidth(), juce::GlyphArrangement::getStringWidth (f, text) + 16.0f);
        auto pill = vr.withSizeKeepingCentre (w, vr.getHeight() - 3.0f);
        g.setColour (juce::Colour (0xff0b0c0f));
        g.fillRoundedRectangle (pill, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (pill.translated (0, 0.5f), 3.0f, 1.0f);
        g.setFont (f);
        g.setColour (slider.isMouseOverOrDragging() ? accent.brighter (0.3f) : colours::text.withAlpha (0.85f));
        g.drawText (text, pill, juce::Justification::centred, false);
    }
}

// =============================================================================
PowerButton::PowerButton (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, juce::Colour a, bool inverted)
    : juce::Button (paramId), ledMeansOff (inverted), accent (a)
{
    setClickingTogglesState (true);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, *this);
}

PowerButton::PowerButton (juce::Colour a) : juce::Button ("power"), accent (a)
{
    setClickingTogglesState (true);
}

void PowerButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float d = std::min (r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre (d, d);
    const bool lit = getToggleState();
    const auto ledCol = ledMeansOff ? colours::bad : accent;

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.translated (0, 1.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (down ? 0xff1a1c20 : 0xff3a3e45), c.getX(), c.getY(),
                                             juce::Colour (0xff131417), c.getX(), c.getBottom(), false));
    g.fillEllipse (c);
    g.setColour (over ? juce::Colour (0xff5a606a) : juce::Colour (0xff2a2d33));
    g.drawEllipse (c, 1.0f);

    // power glyph
    auto icon = c.reduced (d * 0.27f);
    const auto col = lit ? ledCol : colours::textFaint;
    if (lit)
    {
        g.setColour (ledCol.withAlpha (0.25f));
        g.fillEllipse (icon.expanded (d * 0.12f));
    }
    juce::Path p;
    p.addCentredArc (icon.getCentreX(), icon.getCentreY(), icon.getWidth() * 0.5f, icon.getHeight() * 0.5f,
                     0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.6f, true);
    g.setColour (col);
    g.strokePath (p, juce::PathStrokeType (std::max (1.5f, d * 0.07f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (icon.getCentreX(), icon.getY() - d * 0.04f, icon.getCentreX(), icon.getCentreY(), std::max (1.5f, d * 0.07f));
}

// =============================================================================
ParamToggle::ParamToggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                          const juce::String& text, juce::Colour accent)
    : juce::TextButton (text)
{
    setAccent (*this, accent);
    setClickingTogglesState (true);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, *this);
}

// =============================================================================
Segmented::Segmented (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                      const juce::StringArray& labels, juce::Colour accent)
{
    param = state.getParameter (paramId);
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        setAccent (*b, accent);
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { if (attachment) attachment->setValueAsCompleteGesture ((float) i); };
        b->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < labels.size() - 1 ? juce::Button::ConnectedOnRight : 0));
        addAndMakeVisible (b);
    }
    if (param != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { setIndex (juce::roundToInt (v)); });
        attachment->sendInitialUpdate();
    }
}

Segmented::~Segmented() = default;

void Segmented::setIndex (int index)
{
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == index, juce::dontSendNotification);
}

void Segmented::resized()
{
    auto r = getLocalBounds();
    const int w = r.getWidth() / std::max (1, buttons.size());
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds (i == buttons.size() - 1 ? r : r.removeFromLeft (w));
}

void Segmented::paint (juce::Graphics&) {}

// =============================================================================
LevelMeter::LevelMeter (float minD, float maxD, juce::Colour c, bool td)
    : minDb (minD), maxDb (maxD), colour (c), topDown (td), level (td ? 0.0f : minD), peak (td ? 0.0f : minD)
{
}

void LevelMeter::setLevel (float db) noexcept
{
    db = juce::jlimit (std::min (minDb, maxDb), std::max (minDb, maxDb), db);
    // smooth fall
    if (topDown) level = db > level ? db : level * 0.85f + db * 0.15f;
    else         level = db > level ? db : std::max (db, level - 1.2f);
    if (db >= peak || --peakHoldFrames <= 0)
    {
        if (db >= peak) { peak = db; peakHoldFrames = 45; }
        else            peak = topDown ? peak * 0.97f : peak - 0.4f;
    }
}

float LevelMeter::proportion (float db) const noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto bar = showScale ? r.removeFromLeft (std::min (14.0f, r.getWidth() * 0.45f)) : r;
    auto scale = r;

    g.setColour (juce::Colour (0xff050608));
    g.fillRoundedRectangle (bar, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (bar.translated (0, 0.5f), 2.0f, 1.0f);

    auto inner = bar.reduced (2.0f);
    auto yFor = [&] (float db)
    {
        const float p = proportion (db);
        return topDown ? inner.getY() + p * inner.getHeight() : inner.getBottom() - p * inner.getHeight();
    };

    if (hasTarget)
    {
        const float y1 = yFor (targetLo), y2 = yFor (targetHi);
        auto zone = juce::Rectangle<float> (bar.getX() - 3.0f, std::min (y1, y2), bar.getWidth() + 6.0f, std::abs (y2 - y1));
        g.setColour (colours::good.withAlpha (0.10f));
        g.fillRect (zone);
        g.setColour (colours::good.withAlpha (0.9f));
        // bracket shape
        g.drawLine (zone.getX(), zone.getY(), zone.getX() + 4.0f, zone.getY(), 1.5f);
        g.drawLine (zone.getX(), zone.getBottom(), zone.getX() + 4.0f, zone.getBottom(), 1.5f);
        g.drawLine (zone.getX(), zone.getY(), zone.getX(), zone.getBottom(), 1.5f);
        g.drawLine (zone.getRight(), zone.getY(), zone.getRight() - 4.0f, zone.getY(), 1.5f);
        g.drawLine (zone.getRight(), zone.getBottom(), zone.getRight() - 4.0f, zone.getBottom(), 1.5f);
        g.drawLine (zone.getRight(), zone.getY(), zone.getRight(), zone.getBottom(), 1.5f);
    }

    const float yl = yFor (level);
    auto fill = topDown ? inner.withBottom (yl) : inner.withTop (yl);
    if (fill.getHeight() > 0.5f)
    {
        juce::ColourGradient grad (colour.darker (0.5f), inner.getX(), topDown ? inner.getY() : inner.getBottom(),
                                   colour.brighter (0.2f), inner.getX(), topDown ? inner.getBottom() : inner.getY(), false);
        if (! topDown)
        {
            grad.addColour (proportion (-6.0f), colour);
            grad.addColour (std::min (0.999f, proportion (-1.0f)), colours::warn);
            grad.addColour (1.0, colours::bad);
        }
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 1.5f);
    }

    const float yp = yFor (peak);
    if (peak > (topDown ? 0.05f : minDb + 0.5f))
    {
        g.setColour (colours::text.withAlpha (0.85f));
        g.fillRect (inner.getX(), yp - 1.0f, inner.getWidth(), 2.0f);
    }

    if (showScale)
    {
        g.setFont (fonts::numeric (9.5f));
        for (float m : scaleMarks)
        {
            const float y = yFor (m);
            g.setColour (colours::textDim);
            g.drawHorizontalLine ((int) y, scale.getX() + 1.0f, scale.getX() + 4.0f);
            g.drawText (juce::String ((int) m), juce::Rectangle<float> (scale.getX() + 5.0f, y - 6.0f, scale.getWidth() - 5.0f, 12.0f),
                        juce::Justification::centredLeft, false);
        }
    }
}

// =============================================================================
void CentreBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff050608));
    g.fillRoundedRectangle (r, 2.0f);
    const float cx = r.getCentreX();
    const float p = juce::jlimit (-1.0f, 1.0f, value / range);
    auto bar = p >= 0 ? juce::Rectangle<float> (cx, r.getY() + 2, p * r.getWidth() * 0.5f, r.getHeight() - 4)
                      : juce::Rectangle<float> (cx + p * r.getWidth() * 0.5f, r.getY() + 2, -p * r.getWidth() * 0.5f, r.getHeight() - 4);
    g.setColour (colour.withAlpha (0.85f));
    g.fillRect (bar);
    g.setColour (colours::textDim);
    g.fillRect (cx - 0.5f, r.getY(), 1.0f, r.getHeight());
}

} // namespace meridian::ui
