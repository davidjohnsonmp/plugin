#include "RackLookAndFeel.h"

namespace meridian::ui
{

// =============================================================================
// Fonts: use a condensed system face where available (macOS ships several),
// falling back to whatever the platform provides.
// =============================================================================
namespace
{
    juce::String pickFace (std::initializer_list<const char*> candidates)
    {
        static const auto names = juce::Font::findAllTypefaceNames();
        for (auto* c : candidates)
            if (names.contains (c, true))
                return c;
        return {};
    }

    const juce::String& condensedFace()
    {
        static const juce::String face = pickFace ({ "Avenir Next Condensed", "DIN Alternate", "Roboto Condensed",
                                                     "DejaVu Sans Condensed", "Arial Narrow" });
        return face;
    }

    const juce::String& monoFace()
    {
        static const juce::String face = pickFace ({ "SF Mono", "Menlo", "DejaVu Sans Mono", "Consolas" });
        return face;
    }

    juce::Font make (const juce::String& face, float h, int styleFlags)
    {
        juce::FontOptions o;
        if (face.isNotEmpty()) o = o.withName (face);
        o = o.withHeight (h).withStyle (styleFlags & juce::Font::bold ? "Bold" : "Regular");
        juce::Font f (o);
        if (styleFlags & juce::Font::bold) f.setBold (true);
        return f;
    }
}

juce::Font fonts::title (float h)   { return make (condensedFace(), h, juce::Font::bold); }
juce::Font fonts::label (float h)   { return make (condensedFace(), h, juce::Font::bold); }
juce::Font fonts::numeric (float h) { return make (monoFace(), h, juce::Font::plain); }

// =============================================================================
// Paint helpers
// =============================================================================
const juce::Image& paint::brushedTexture()
{
    static const juce::Image img = []
    {
        juce::Image im (juce::Image::ARGB, 512, 256, true);
        juce::Graphics g (im);
        juce::Random r (42);
        for (int i = 0; i < 1800; ++i)
        {
            const float y = r.nextFloat() * 256.0f;
            const float x = r.nextFloat() * 512.0f;
            const float len = 40.0f + r.nextFloat() * 260.0f;
            const bool light = r.nextBool();
            g.setColour ((light ? juce::Colours::white : juce::Colours::black).withAlpha (0.010f + r.nextFloat() * 0.020f));
            g.drawHorizontalLine ((int) y, x, x + len);
            if (x + len > 512.0f) g.drawHorizontalLine ((int) y, 0.0f, x + len - 512.0f);
        }
        return im;
    }();
    return img;
}

void paint::stage (juce::Graphics& g, juce::Rectangle<float> a)
{
    g.setGradientFill (juce::ColourGradient (colours::stageLight, a.getCentreX(), a.getY() + a.getHeight() * 0.35f,
                                             colours::stageDark, a.getX(), a.getBottom(), true));
    g.fillRect (a);
    // floor reflection
    auto floor = a.withTop (a.getBottom() - a.getHeight() * 0.18f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, floor.getX(), floor.getY(),
                                             juce::Colours::black.withAlpha (0.45f), floor.getX(), floor.getBottom(), false));
    g.fillRect (floor);
}

void paint::screw (juce::Graphics& g, juce::Point<float> c, float r, float angle)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - r - 0.5f, c.y - r + 1.0f, 2 * r + 1, 2 * r + 1);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9a9ea6), c.x - r * 0.5f, c.y - r * 0.6f,
                                             juce::Colour (0xff3c3f45), c.x + r * 0.6f, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    juce::Path slot;
    slot.addRectangle (-r * 0.75f, -r * 0.13f, r * 1.5f, r * 0.26f);
    slot.addRectangle (-r * 0.13f, -r * 0.75f, r * 0.26f, r * 1.5f);
    g.setColour (juce::Colour (0xff1c1d20));
    g.fillPath (slot, juce::AffineTransform::rotation (angle).translated (c));
}

void paint::faceplate (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour accent, const juce::String& name)
{
    // drop shadow onto the stage
    for (int i = 0; i < 8; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.06f));
        g.fillRoundedRectangle (a.expanded ((float) i * 1.5f).translated (0, 6.0f + (float) i), 10.0f + (float) i);
    }

    g.setGradientFill (juce::ColourGradient (colours::plateTop, a.getX(), a.getY(), colours::plateBottom, a.getX(), a.getBottom(), false));
    g.fillRoundedRectangle (a, 9.0f);

    // brushed texture
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (a, 9.0f);
        g.reduceClipRegion (clip);
        g.setTiledImageFill (brushedTexture(), (int) a.getX(), (int) a.getY(), 1.0f);
        g.fillRect (a);
        // top sheen
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), a.getX(), a.getY(),
                                                 juce::Colours::transparentWhite, a.getX(), a.getY() + 90.0f, false));
        g.fillRect (a);
    }

    // bevels
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (a.reduced (1.0f), 8.5f, 1.0f);
    g.setColour (colours::plateEdge);
    g.drawRoundedRectangle (a, 9.0f, 1.5f);

    // accent stripe + engraved name, top-left
    auto strip = juce::Rectangle<float> (a.getX() + 28.0f, a.getY() + 16.0f, 4.0f, 18.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (strip, 1.5f);
    g.setColour (accent.withAlpha (0.25f));
    g.fillRoundedRectangle (strip.expanded (3.0f), 3.0f);
    engraved (g, name, { a.getX() + 40.0f, a.getY() + 12.0f, 400.0f, 26.0f }, juce::Justification::centredLeft,
              fonts::title (19.0f), colours::text);

    const float sr = 5.0f;
    screw (g, { a.getX() + 12.0f, a.getY() + 12.0f }, sr, 0.3f);
    screw (g, { a.getRight() - 12.0f, a.getY() + 12.0f }, sr, 1.1f);
    screw (g, { a.getX() + 12.0f, a.getBottom() - 12.0f }, sr, 0.8f);
    screw (g, { a.getRight() - 12.0f, a.getBottom() - 12.0f }, sr, 0.1f);
}

void paint::screen (juce::Graphics& g, juce::Rectangle<float> a, float corner)
{
    // recessed bezel
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (a.expanded (3.0f), corner + 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (a.expanded (3.0f).translated (0, 1.0f), corner + 2.0f, 1.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0a0e13), a.getCentreX(), a.getY(),
                                             colours::screen, a.getCentreX(), a.getBottom(), false));
    g.fillRoundedRectangle (a, corner);

    // inner shadow
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip; clip.addRoundedRectangle (a, corner);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), a.getX(), a.getY(),
                                             juce::Colours::transparentBlack, a.getX(), a.getY() + 14.0f, false));
    g.fillRect (a);
}

void paint::engraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> a,
                      juce::Justification just, juce::Font font, juce::Colour colour)
{
    g.setFont (font);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawText (text, a.translated (0, 1.0f), just, false);
    g.setColour (colour);
    g.drawText (text, a, just, false);
}

void paint::sectionTitle (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> a, juce::Colour accent)
{
    auto f = fonts::label (12.5f);
    g.setFont (f);
    const float tw = juce::GlyphArrangement::getStringWidth (f, text) + 4.0f;
    const float cy = a.getCentreY();
    g.setColour (colours::textDim);
    g.drawText (text, a, juce::Justification::centredLeft, false);
    g.setColour (accent.withAlpha (0.35f));
    g.drawHorizontalLine ((int) cy, a.getX() + tw + 6.0f, a.getRight());
}

// =============================================================================
// LookAndFeel
// =============================================================================
RackLookAndFeel::RackLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff17191d));
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2b3038));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, colours::textDim);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0d0f12));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff30343b));
    setColour (juce::ComboBox::arrowColourId, colours::textDim);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::TextButton::textColourOffId, colours::textDim);
    setColour (juce::TextButton::textColourOnId, colours::text);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d0f12));
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff30343b));
    setColour (juce::TextEditor::focusedOutlineColourId, colours::unmask);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff17191d));
    setColour (juce::AlertWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff17191d));
    setColour (juce::Slider::textBoxTextColourId, colours::text);
}

void RackLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    const auto accentVar = s.getProperties()["accent"];
    const juce::Colour accent = accentVar.isVoid() ? colours::unmask : juce::Colour ((juce::uint32) (juce::int64) accentVar);
    const bool enabled = s.isEnabled();

    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float size = std::min (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float rRing = size * 0.5f - 1.0f;
    const float rKnob = rRing * 0.74f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    // --- ticks
    const int numTicks = size > 120 ? 21 : 11;
    for (int i = 0; i < numTicks; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / (float) (numTicks - 1);
        const float r0 = rRing * 0.90f, r1 = rRing * (i % 5 == 0 ? 0.80f : 0.84f);
        g.setColour (colours::textFaint.withAlpha (0.7f));
        g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), 1.0f);
    }

    // --- LED ring track
    const float ringW = juce::jlimit (2.5f, 6.0f, size * 0.035f);
    juce::Path track;
    track.addCentredArc (c.x, c.y, rRing - ringW, rRing - ringW, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff0a0b0d));
    g.strokePath (track, juce::PathStrokeType (ringW + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc (bipolar controls grow from the centre)
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
    const float a0 = startAngle + zeroPos * (endAngle - startAngle);
    if (std::abs (angle - a0) > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, rRing - ringW, rRing - ringW, 0.0f, std::min (a0, angle), std::max (a0, angle), true);
        const auto col = enabled ? accent : colours::textFaint;
        g.setColour (col.withAlpha (0.22f));
        g.strokePath (arc, juce::PathStrokeType (ringW * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (arc, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // --- knob shadow
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - rKnob * 1.02f, c.y - rKnob * 0.94f, rKnob * 2.04f, rKnob * 2.12f);

    // --- knurled skirt
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4e56), c.x, c.y - rKnob,
                                             juce::Colour (0xff15171a), c.x, c.y + rKnob, false));
    g.fillEllipse (c.x - rKnob, c.y - rKnob, 2 * rKnob, 2 * rKnob);
    const int knurls = juce::jlimit (24, 72, (int) (rKnob * 1.4f));
    for (int i = 0; i < knurls; ++i)
    {
        const float a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) knurls;
        const float shade = 0.5f + 0.5f * std::cos (a + 0.8f);
        g.setColour (juce::Colours::black.withAlpha (0.25f + 0.25f * (1.0f - shade)));
        const float r0 = rKnob * 0.86f, r1 = rKnob * 0.995f;
        g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), 1.0f);
    }

    // --- spun-metal cap: wedge slices shaded by direction to fake a conic gradient
    const float rCap = rKnob * 0.80f;
    const int slices = 48;
    for (int i = 0; i < slices; ++i)
    {
        const float a1 = juce::MathConstants<float>::twoPi * (float) i / (float) slices;
        const float a2 = juce::MathConstants<float>::twoPi * (float) (i + 1) / (float) slices;
        const float m = 0.5f * (a1 + a2);
        const float v = 0.5f + 0.5f * std::cos (2.0f * (m - 0.7f));
        juce::Path wedge;
        wedge.addPieSegment (c.x - rCap, c.y - rCap, 2 * rCap, 2 * rCap, a1, a2 + 0.01f, 0.0f);
        g.setColour (juce::Colour (0xff2c2f35).interpolatedWith (juce::Colour (0xff8d929b), v * 0.75f));
        g.fillPath (wedge);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.18f), c.x - rCap * 0.4f, c.y - rCap * 0.7f,
                                             juce::Colours::transparentWhite, c.x, c.y + rCap * 0.2f, true));
    g.fillEllipse (c.x - rCap, c.y - rCap, 2 * rCap, 2 * rCap);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (c.x - rCap, c.y - rCap, 2 * rCap, 2 * rCap, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (c.x - rKnob + 0.5f, c.y - rKnob + 0.5f, 2 * rKnob - 1, 2 * rKnob - 1, 1.0f);

    // --- pointer
    const float p0 = rCap * 0.35f, p1 = rKnob * 0.93f;
    const juce::Line<float> pointer (c.x + p0 * std::sin (angle), c.y - p0 * std::cos (angle),
                                     c.x + p1 * std::sin (angle), c.y - p1 * std::cos (angle));
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine (pointer, juce::jlimit (3.0f, 7.0f, size * 0.045f));
    g.setColour (enabled ? juce::Colour (0xfff4f1ea) : colours::textFaint);
    g.drawLine (pointer, juce::jlimit (1.6f, 3.5f, size * 0.022f));
}

void RackLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto accentVar = b.getProperties()["accent"];
    const juce::Colour accent = accentVar.isVoid() ? colours::unmask : juce::Colour ((juce::uint32) (juce::int64) accentVar);
    const bool on = b.getToggleState();

    g.setGradientFill (juce::ColourGradient (juce::Colour (down ? 0xff141619 : 0xff2a2d33), r.getX(), r.getY(),
                                             juce::Colour (down ? 0xff1d2024 : 0xff17191c), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 4.0f);
    if (on)
    {
        g.setColour (accent.withAlpha (0.18f));
        g.fillRoundedRectangle (r, 4.0f);
    }
    g.setColour (on ? accent.withAlpha (0.8f) : juce::Colour (over ? 0xff4a4f58 : 0xff34383f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void RackLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const auto accentVar = b.getProperties()["accent"];
    const juce::Colour accent = accentVar.isVoid() ? colours::unmask : juce::Colour ((juce::uint32) (juce::int64) accentVar);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? accent.brighter (0.4f) : (b.isEnabled() ? colours::text : colours::textFaint));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

juce::Font RackLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return fonts::label (juce::jlimit (11.0f, 15.0f, (float) h * 0.5f));
}

void RackLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (juce::Colour (0xff0b0d10));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.isMouseOver (true) ? juce::Colour (0xff4a4f58) : juce::Colour (0xff30343b));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (colours::textDim);
    g.fillPath (arrow);
}

juce::Font RackLookAndFeel::getComboBoxFont (juce::ComboBox& b) { return fonts::label (juce::jlimit (12.0f, 16.0f, (float) b.getHeight() * 0.52f)); }

void RackLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font RackLookAndFeel::getPopupMenuFont() { return fonts::label (15.0f); }

void RackLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (juce::Colour (0xff30343b));
    g.drawRect (0, 0, w, h);
}

juce::Font RackLookAndFeel::getLabelFont (juce::Label& l) { return fonts::label (std::max (11.0f, (float) l.getHeight() * 0.6f)); }

void RackLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (juce::Colour (0xff17191d));
    g.setColour (juce::Colour (0xff3a3f47));
    g.drawRect (0, 0, w, h);
    g.setColour (colours::text);
    g.setFont (fonts::label (13.0f));
    g.drawFittedText (text, 6, 2, w - 12, h - 4, juce::Justification::centredLeft, 4);
}

} // namespace meridian::ui
