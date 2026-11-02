#include "EqDisplay.h"

namespace meridian::ui
{

static constexpr float fMin = 20.0f, fMax = 20000.0f;

EqDisplay::EqDisplay (MeridianProcessor& p) : proc (p)
{
    lowParam  = proc.apvts.getParameter (ID::umLowLimit);
    highParam = proc.apvts.getParameter (ID::umHighLimit);
    setTooltip ("Drag the two range limiters to choose which frequencies are unmasked. "
                "Click and drag on the graph to read frequency and gain. Scroll or click the dB scale to zoom.");
}

void EqDisplay::resized()
{
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    axisArea = r.removeFromLeft (58.0f);
    r.removeFromBottom (18.0f);
    plot = r.reduced (4.0f, 6.0f).withTrimmedTop (14.0f);
}

float EqDisplay::xForHz (float hz) const noexcept
{
    return plot.getX() + plot.getWidth() * std::log (hz / fMin) / std::log (fMax / fMin);
}

float EqDisplay::hzForX (float x) const noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
    return fMin * std::pow (fMax / fMin, t);
}

float EqDisplay::yForDb (float db) const noexcept
{
    const float range = scales[scaleIndex];
    return plot.getCentreY() - (db / range) * plot.getHeight() * 0.5f;
}

float EqDisplay::curveDbAt (float hz) const noexcept
{
    const auto& map = proc.unmasker.getBandMap();
    if (hz <= map.centreHz[0]) return curve[0];
    for (int b = 1; b < numBands; ++b)
        if (hz <= map.centreHz[(size_t) b])
        {
            const float f0 = map.centreHz[(size_t) b - 1], f1 = map.centreHz[(size_t) b];
            const float t = std::log (hz / f0) / std::log (f1 / f0);
            return curve[(size_t) b - 1] + t * (curve[(size_t) b] - curve[(size_t) b - 1]);
        }
    return curve[numBands - 1];
}

void EqDisplay::tick()
{
    for (int b = 0; b < numBands; ++b)
    {
        curve[(size_t) b] = proc.unmasker.displayCurveDb[(size_t) b].load (std::memory_order_relaxed);
        weight[(size_t) b] = proc.unmasker.displayWeight[(size_t) b].load (std::memory_order_relaxed);
        const float s = proc.unmasker.displayInputDb[(size_t) b].load (std::memory_order_relaxed);
        spectrum[(size_t) b] = spectrum[(size_t) b] * 0.6f + s * 0.4f;
    }
    recMeter  = recMeter * 0.7f + proc.unmasker.recoverMeterDb.load() * 0.3f;
    tameMeter = tameMeter * 0.7f + proc.unmasker.tameMeterDb.load() * 0.3f;
    biasMeter = biasMeter * 0.7f + proc.unmasker.biasMeter.load() * 0.3f;
    repaint();
}

void EqDisplay::paint (juce::Graphics& g)
{
    const auto accent = colours::unmask;
    const auto& map = proc.unmasker.getBandMap();
    paint::screen (g, getLocalBounds().toFloat());

    // ---- grid
    const float freqs[] = { 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                            1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000 };
    for (float f : freqs)
    {
        const bool major = (f == 20 || f == 100 || f == 1000 || f == 10000 || f == 20000);
        g.setColour (major ? colours::screenGrid.brighter (0.35f) : colours::screenGrid);
        g.drawVerticalLine ((int) xForHz (f), plot.getY(), plot.getBottom());
    }
    g.setFont (fonts::numeric (10.0f));
    g.setColour (colours::textFaint);
    for (auto [f, t] : { std::pair<float, const char*> { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
                         { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } })
        g.drawText (t, juce::Rectangle<float> (xForHz (f) - 20, plot.getBottom() + 4, 40, 14), juce::Justification::centred, false);

    const float range = scales[scaleIndex];
    for (int i = -4; i <= 4; ++i)
    {
        const float db = range * (float) i / 4.0f;
        const float y = yForDb (db);
        g.setColour (i == 0 ? colours::screenGrid.brighter (0.6f) : colours::screenGrid);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (colours::textFaint);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db, range < 6 ? 1 : 0),
                    juce::Rectangle<float> (axisArea.getRight() - 34, y - 7, 32, 14), juce::Justification::centredRight, false);
    }

    // ---- excluded range (outside the limiters) darkened, active range tinted
    for (int b = 0; b < numBands; ++b)
    {
        const float fLo = b == 0 ? fMin : std::sqrt (map.centreHz[(size_t) b - 1] * map.centreHz[(size_t) b]);
        const float fHi = b == numBands - 1 ? fMax : std::sqrt (map.centreHz[(size_t) b] * map.centreHz[(size_t) b + 1]);
        const float x0 = xForHz (fLo), x1 = xForHz (fHi);
        g.setColour (juce::Colours::black.withAlpha (0.45f * (1.0f - weight[(size_t) b])));
        g.fillRect (x0, plot.getY(), x1 - x0 + 0.5f, plot.getHeight());
        g.setColour (colours::bad.withAlpha (0.022f * weight[(size_t) b]));
        g.fillRect (x0, plot.getY(), x1 - x0 + 0.5f, plot.getHeight());
    }

    // ---- analysed spectrum (faint)
    {
        juce::Path spec;
        for (int b = 0; b < numBands; ++b)
        {
            const float x = xForHz (map.centreHz[(size_t) b]);
            const float t = juce::jlimit (0.0f, 1.0f, (spectrum[(size_t) b] + 90.0f) / 80.0f);
            const float y = plot.getBottom() - t * plot.getHeight() * 0.85f;
            if (b == 0) spec.startNewSubPath (plot.getX(), y);
            spec.lineTo (x, y);
        }
        spec.lineTo (plot.getRight(), plot.getBottom());
        spec.lineTo (plot.getX(), plot.getBottom());
        spec.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        g.fillPath (spec);
    }

    // ---- EQ curve
    {
        juce::Path line;
        const int steps = (int) plot.getWidth() / 3;
        for (int i = 0; i <= steps; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) steps;
            const float y = juce::jlimit (plot.getY() - 2, plot.getBottom() + 2, yForDb (curveDbAt (hzForX (x))));
            if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        juce::Path fill (line);
        fill.lineTo (plot.getRight(), yForDb (0));
        fill.lineTo (plot.getX(), yForDb (0));
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.28f), 0, plot.getY(), accent.withAlpha (0.02f), 0, yForDb (0), false));
        g.fillPath (fill);
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (line, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent.brighter (0.3f));
        g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- range limiters
    const float lo = lowParam->convertFrom0to1 (lowParam->getValue());
    const float hi = highParam->convertFrom0to1 (highParam->getValue());
    for (int h = 0; h < 2; ++h)
    {
        const float hz = h == 0 ? lo : hi;
        const float x = xForHz (hz);
        const bool active = hoverHandle == h || dragging == h;
        g.setColour (colours::bad.withAlpha (active ? 0.95f : 0.65f));
        g.fillRect (x - 0.75f, plot.getY(), 1.5f, plot.getHeight());
        auto grip = juce::Rectangle<float> (x - 9, plot.getY() - 2, 18, 22);
        g.setColour (juce::Colour (0xff1a1d22));
        g.fillRoundedRectangle (grip, 3.0f);
        g.setColour (colours::bad.withAlpha (active ? 1.0f : 0.75f));
        g.drawRoundedRectangle (grip, 3.0f, 1.2f);
        juce::Path arrow;
        const float cy = grip.getCentreY();
        if (h == 0) arrow.addTriangle (x - 3, cy - 4, x - 3, cy + 4, x + 4, cy);
        else        arrow.addTriangle (x + 3, cy - 4, x + 3, cy + 4, x - 4, cy);
        g.fillPath (arrow);
    }

    // ---- left meters: Bias | Recover/Tame
    {
        auto m = axisArea.withTrimmedRight (36.0f).reduced (2.0f, 0.0f).withY (plot.getY()).withHeight (plot.getHeight());
        auto biasBar = m.removeFromLeft (7.0f);
        m.removeFromLeft (4.0f);
        auto rtBar = m.removeFromLeft (8.0f);

        g.setColour (juce::Colour (0xff030405));
        g.fillRoundedRectangle (biasBar, 2.0f);
        g.fillRoundedRectangle (rtBar, 2.0f);

        const float cy = biasBar.getCentreY();
        const float bh = biasBar.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, biasMeter);
        g.setColour (colours::textDim);
        g.fillRect (biasBar.getX() + 1, bh >= 0 ? cy - bh : cy, biasBar.getWidth() - 2, std::abs (bh));

        const float yRec = yForDb (juce::jmin (recMeter, range));
        const float yTam = yForDb (-juce::jmin (tameMeter, range));
        g.setColour (accent);
        g.fillRect (rtBar.getX() + 1, yRec, rtBar.getWidth() - 2, yForDb (0) - yRec);
        g.setColour (colours::character);
        g.fillRect (rtBar.getX() + 1, yForDb (0), rtBar.getWidth() - 2, yTam - yForDb (0));

        g.setFont (fonts::label (9.5f));
        g.setColour (colours::textFaint);
        g.drawText ("B", biasBar.withY (plot.getBottom() + 3).withHeight (12), juce::Justification::centred, false);
        g.drawText ("R/T", rtBar.expanded (6, 0).withY (plot.getBottom() + 3).withHeight (12), juce::Justification::centred, false);
    }

    // ---- readout popup
    if (showReadout)
    {
        const float hz = hzForX (pointer.x);
        const float db = curveDbAt (hz);
        const juce::String text = (hz >= 1000 ? juce::String (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz")
                                  + "   " + (db >= 0 ? "+" : "") + juce::String (db, 1) + " dB";
        auto f = fonts::numeric (11.5f);
        const float w = juce::GlyphArrangement::getStringWidth (f, text) + 16.0f;
        auto box = juce::Rectangle<float> (pointer.x + 12, pointer.y - 28, w, 20).constrainedWithin (plot);
        g.setColour (juce::Colour (0xee111317));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (accent.withAlpha (0.7f));
        g.drawRoundedRectangle (box, 3.0f, 1.0f);
        g.setColour (colours::text);
        g.setFont (f);
        g.drawText (text, box, juce::Justification::centred, false);
        g.setColour (accent);
        g.fillEllipse (pointer.x - 3, yForDb (db) - 3, 6, 6);
        g.setColour (accent.withAlpha (0.4f));
        g.drawVerticalLine ((int) pointer.x, plot.getY(), plot.getBottom());
    }

    // ---- scale label (click the axis to change)
    g.setFont (fonts::label (10.5f));
    g.setColour (colours::textDim);
    g.drawText ("+/-" + juce::String ((int) range) + " dB", axisArea.withHeight (14).withY (axisArea.getY() + 2.0f),
                juce::Justification::centredRight, false);
}

int EqDisplay::handleAt (juce::Point<float> p) const noexcept
{
    const float lo = xForHz (lowParam->convertFrom0to1 (lowParam->getValue()));
    const float hi = xForHz (highParam->convertFrom0to1 (highParam->getValue()));
    if (! plot.expanded (10, 10).contains (p)) return -1;
    const float dl = std::abs (p.x - lo), dh = std::abs (p.x - hi);
    if (std::min (dl, dh) > 8.0f) return -1;
    return dl <= dh ? 0 : 1;
}

void EqDisplay::setLimit (int which, float hz)
{
    auto* prm = which == 0 ? lowParam : highParam;
    prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (fMin, fMax, hz)));
}

void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (h != hoverHandle) { hoverHandle = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (axisArea.contains (e.position))
    {
        scaleIndex = (scaleIndex + 1) % 4;
        repaint();
        return;
    }
    const int h = handleAt (e.position);
    if (h >= 0)
    {
        dragging = h;
        (h == 0 ? lowParam : highParam)->beginChangeGesture();
        if (e.getNumberOfClicks() > 1)
            setLimit (h, h == 0 ? fMin : fMax);
        return;
    }
    if (plot.contains (e.position))
    {
        dragging = 2;
        showReadout = true;
        pointer = e.position;
        repaint();
    }
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == 0 || dragging == 1)
        setLimit (dragging, hzForX (e.position.x));
    else if (dragging == 2)
        pointer = plot.getConstrainedPoint (e.position);
    repaint();
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging == 0 || dragging == 1)
        (dragging == 0 ? lowParam : highParam)->endChangeGesture();
    dragging = -1;
    showReadout = false;
    repaint();
}

void EqDisplay::mouseExit (const juce::MouseEvent&)
{
    hoverHandle = -1;
    repaint();
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (w.deltaY > 0 && scaleIndex > 0) --scaleIndex;
    else if (w.deltaY < 0 && scaleIndex < 3) ++scaleIndex;
    repaint();
}

} // namespace meridian::ui
