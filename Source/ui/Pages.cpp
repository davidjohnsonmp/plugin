#include "Pages.h"

namespace meridian::ui
{

static void drawReadout (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title,
                         const juce::String& value, const juce::String& unit, juce::Colour valueColour, float valueSize = 22.0f)
{
    g.setColour (juce::Colour (0xff0a0c0f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (r.translated (0, 0.5f), 4.0f, 1.0f);
    auto inner = r.reduced (8.0f, 4.0f);
    g.setFont (fonts::label (11.0f));
    g.setColour (colours::textDim);
    g.drawText (title, inner.removeFromTop (14.0f), juce::Justification::centredLeft, false);
    g.setFont (fonts::numeric (valueSize));
    g.setColour (valueColour);
    auto f = fonts::numeric (valueSize);
    const float vw = juce::GlyphArrangement::getStringWidth (f, value);
    g.drawText (value, inner, juce::Justification::centredLeft, false);
    g.setFont (fonts::label (11.0f));
    g.setColour (colours::textFaint);
    g.drawText (unit, inner.withTrimmedLeft (vw + 6.0f), juce::Justification::centredLeft, false);
}

static juce::String fmtHz (float hz)
{
    return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
}

// =============================================================================
// UNMASK
// =============================================================================
UnmaskPage::UnmaskPage (MeridianProcessor& p)
    : Page (p, "UNMASK EQ", colours::unmask),
      recover (p.apvts, ID::umRecover, "Recover", colours::unmask, RackKnob::Size::large),
      tame (p.apvts, ID::umTame, "Tame", colours::unmask, RackKnob::Size::large),
      bias (p.apvts, ID::umBias, "Bias", colours::unmask),
      brighten (p.apvts, ID::umBrighten, "Brighten", colours::unmask),
      boost (p.apvts, ID::umBoost, "Boost", colours::unmask),
      gain (p.apvts, ID::umGain, "Gain", colours::unmask),
      power (p.apvts, ID::umOn, colours::unmask),
      display (p),
      scSwitch (p.apvts, ID::umSidechain, { "MAIN", "SIDECHAIN" }, colours::unmask),
      quality (p.apvts, ID::umQuality, { "LIVE", "NORMAL", "BEST" }, colours::unmask)
{
    for (auto* c : std::initializer_list<juce::Component*> { &recover, &tame, &bias, &brighten, &boost, &gain, &power,
                                                             &display, &brightness, &scSwitch, &quality })
        addAndMakeVisible (c);
    power.setTooltip ("Unmask EQ on/off (latency stays the same, so you can compare freely)");
    scSwitch.setTooltip ("Analyse the main input, or the sidechain input (e.g. the full mix while processing a stem)");
    quality.setTooltip ("LIVE: lowest latency. NORMAL: balanced. BEST: finest frequency resolution, more latency. "
                        "Some hosts need playback restarted after changing this.");
    brightness.setTooltip ("Perceived brightness of the output compared with the input. Keep it near the centre for an unbiased comparison.");
}

void UnmaskPage::resized()
{
    const auto p = plateBounds().toNearestInt();
    power.setBounds (p.getRight() - 52, p.getY() + 10, 32, 32);

    const int ky = p.getY() + 64, kh = 136;
    recover.setBounds (p.getX() + 34, ky, 128, kh);
    tame.setBounds (p.getX() + 176, ky, 128, kh);
    bias.setBounds (p.getX() + 344, ky + 16, 104, kh - 16);
    brighten.setBounds (p.getX() + 458, ky + 16, 104, kh - 16);
    boost.setBounds (p.getX() + 600, ky + 16, 104, kh - 16);

    const int rx = p.getRight() - 24 - 180;
    gain.setBounds (rx + 34, ky + 16, 112, kh - 16);

    display.setBounds (p.getX() + 24, p.getY() + 212, rx - 16 - (p.getX() + 24), 272);
    brightness.setBounds (display.getX() + 96, display.getBottom() + 14, display.getWidth() - 96, 9);

    scSwitch.setBounds (rx, p.getY() + 238, 180, 28);
    quality.setBounds (rx, p.getY() + 302, 180, 28);
}

void UnmaskPage::paint (juce::Graphics& g)
{
    Page::paint (g);
    const auto p = plateBounds();
    const float ky = p.getY() + 46.0f;

    paint::sectionTitle (g, "PRIMARY", { p.getX() + 34, ky, 270, 16 }, accent);
    paint::sectionTitle (g, "SECONDARY", { p.getX() + 344, ky, 218, 16 }, accent);
    paint::sectionTitle (g, "BALANCE", { p.getX() + 600, ky, 104, 16 }, accent);
    const float rx = p.getRight() - 24.0f - 180.0f;
    paint::sectionTitle (g, "OUTPUT", { rx, ky, 180, 16 }, accent);

    // info screen between knobs and output column
    auto info = juce::Rectangle<float> (p.getX() + 728, p.getY() + 84, rx - 24 - (p.getX() + 728), 104);
    paint::screen (g, info, 5.0f);
    auto ir = info.reduced (10.0f, 8.0f);
    auto line = [&] (const juce::String& k, const juce::String& v, juce::Colour c)
    {
        auto row = ir.removeFromTop (22.0f);
        g.setFont (fonts::label (12.0f));
        g.setColour (colours::textDim);
        g.drawText (k, row, juce::Justification::centredLeft, false);
        g.setFont (fonts::numeric (12.5f));
        g.setColour (c);
        g.drawText (v, row, juce::Justification::centredRight, false);
    };
    const float lo = proc.apvts.getRawParameterValue (ID::umLowLimit)->load();
    const float hi = proc.apvts.getRawParameterValue (ID::umHighLimit)->load();
    line ("RECOVER", "+" + juce::String (recDb, 1) + " dB", accent);
    line ("TAME", "-" + juce::String (tameDb, 1) + " dB", colours::character);
    line ("BIAS", std::abs (biasV) < 0.05f ? juce::String ("Neutral")
                                           : juce::String (biasV > 0 ? "Recover " : "Tame ") + juce::String (juce::roundToInt (std::abs (biasV) * 100)) + "%",
          colours::text);
    line (lo <= hi ? "RANGE" : "EXCLUDED", fmtHz (std::min (lo, hi)) + " - " + fmtHz (std::max (lo, hi)), colours::bad.brighter (0.3f));

    paint::engraved (g, "ANALYZER INPUT", { rx, p.getY() + 220, 180, 16 }, juce::Justification::centredLeft, fonts::label (12.0f));
    paint::engraved (g, "QUALITY", { rx, p.getY() + 284, 180, 16 }, juce::Justification::centredLeft, fonts::label (12.0f));

    const double sr = proc.getCurrentSampleRate();
    const float latMs = sr > 0 ? (float) (proc.getLatencySamples() * 1000.0 / sr) : 0.0f;
    auto lat = juce::Rectangle<float> (rx, p.getY() + 344, 180, 44);
    drawReadout (g, lat, "PLUGIN LATENCY", juce::String (latMs, 1), "ms", colours::text, 16.0f);

    auto help = juce::Rectangle<float> (rx, p.getY() + 400, 180, 96);
    g.setFont (fonts::label (11.5f));
    g.setColour (colours::textDim);
    g.drawFittedText ("Recover lifts masked detail.\nTame calms dominant parts.\nBias and Brighten steer them.\n"
                      "Boost shifts bass/mid balance.\nLoudness is kept constant.", help.toNearestInt(), juce::Justification::topLeft, 6);

    const auto d = display.getBounds().toFloat();
    g.setFont (fonts::label (11.0f));
    g.setColour (colours::textDim);
    g.drawText ("BRIGHTNESS", juce::Rectangle<float> (d.getX(), d.getBottom() + 10.0f, 90.0f, 16.0f), juce::Justification::centredLeft, false);
    g.setFont (fonts::numeric (10.0f));
    g.setColour (colours::textFaint);
    g.drawText ("darker", juce::Rectangle<float> (d.getX() + 96, d.getBottom() + 24.0f, 80, 12), juce::Justification::centredLeft, false);
    g.drawText ("brighter", juce::Rectangle<float> (d.getRight() - 80, d.getBottom() + 24.0f, 80, 12), juce::Justification::centredRight, false);
}

void UnmaskPage::tick()
{
    display.tick();
    recDb = recDb * 0.8f + proc.unmasker.recoverMeterDb.load() * 0.2f;
    tameDb = tameDb * 0.8f + proc.unmasker.tameMeterDb.load() * 0.2f;
    biasV = biasV * 0.8f + proc.unmasker.biasMeter.load() * 0.2f;
    brightV = brightV * 0.8f + proc.unmasker.brightnessMeter.load() * 0.2f;
    brightness.setValue (brightV);
    brightness.repaint();
    if (isShowing())
        repaint (plateBounds().withHeight (210).toNearestInt());
}

// =============================================================================
// CHARACTER
// =============================================================================
CharacterPage::CharacterPage (MeridianProcessor& p)
    : Page (p, "CHARACTER", colours::character),
      amount (p.apvts, ID::chAmount, "Amount", colours::character, RackKnob::Size::huge),
      low (p.apvts, ID::chLow, "Low", colours::character),
      mid (p.apvts, ID::chMid, "Mid", colours::character),
      high (p.apvts, ID::chHigh, "High", colours::character),
      power (p.apvts, ID::chOn, colours::character),
      scSwitch (p.apvts, ID::chSidechain, { "INT", "EXT" }, colours::character),
      oversampling (p.apvts, ID::chOversample, { "OFF", "2x", "4x" }, colours::character)
{
    for (auto* c : std::initializer_list<juce::Component*> { &amount, &low, &mid, &high, &power, &inputMeter, &scSwitch, &oversampling })
        addAndMakeVisible (c);

    inputMeter.setTarget (CharacterChain::inputTargetMin, CharacterChain::inputTargetMax);
    inputMeter.setScale ({ 0, -6, -12, -18, -24, -30, -36 });
    inputMeter.setTooltip ("Input level (RMS). Gain-stage the mix so the bar sits inside the green bracket.");

    const char* names[3] = { "LOW", "MID", "HIGH" };
    for (int b = 0; b < 3; ++b)
    {
        grMeters[(size_t) b] = std::make_unique<LevelMeter> (0.0f, 12.0f, colours::character, true);
        grMeters[(size_t) b]->setTarget (CharacterChain::grTargetMin[b], CharacterChain::grTargetMax[b]);
        grMeters[(size_t) b]->setScale ({ 0, 3, 6, 9, 12 });
        grMeters[(size_t) b]->setTooltip (juce::String (names[b]) + " band gain reduction. Aim for the green target zone.");
        addAndMakeVisible (*grMeters[(size_t) b]);
    }
    scSwitch.setTooltip ("INT: the mix drives the compression. EXT: the sidechain input drives it (e.g. the full mix while exporting stems).");
    oversampling.setTooltip ("Oversampling for the saturation stage. Higher is cleaner and uses more CPU.");
    amount.setTooltip ("How hard the whole chain works: compression, tone, saturation and width. 100% is the reference setting.");
}

void CharacterPage::resized()
{
    const auto p = plateBounds().toNearestInt();
    power.setBounds (p.getRight() - 52, p.getY() + 10, 32, 32);

    inputMeter.setBounds (p.getX() + 46, p.getY() + 80, 40, 340);
    readoutArea = juce::Rectangle<float> ((float) p.getX() + 26, (float) p.getY() + 436, 116, 72);

    high.setBounds (p.getX() + 168, p.getY() + 70, 112, 134);
    mid.setBounds (p.getX() + 168, p.getY() + 210, 112, 134);
    low.setBounds (p.getX() + 168, p.getY() + 350, 112, 134);

    amount.setBounds (p.getX() + 316, p.getY() + 56, 420, 450);

    for (int b = 0; b < 3; ++b)
        grMeters[(size_t) b]->setBounds (p.getX() + 772 + b * 64, p.getY() + 96, 40, 324);

    const int rx = p.getRight() - 24 - 150;
    scSwitch.setBounds (rx, p.getY() + 96, 150, 28);
    oversampling.setBounds (rx, p.getY() + 168, 150, 28);
}

void CharacterPage::paint (juce::Graphics& g)
{
    Page::paint (g);
    const auto p = plateBounds();

    paint::sectionTitle (g, "INPUT", { p.getX() + 26, p.getY() + 50, 116, 16 }, accent);
    paint::sectionTitle (g, "PRE EQ", { p.getX() + 168, p.getY() + 50, 112, 16 }, accent);
    paint::sectionTitle (g, "GAIN REDUCTION", { p.getX() + 772, p.getY() + 50, 172, 16 }, accent);
    const float rx = p.getRight() - 24.0f - 150.0f;
    paint::engraved (g, "SIDECHAIN", { rx, p.getY() + 76, 150, 16 }, juce::Justification::centredLeft, fonts::label (12.0f));
    paint::engraved (g, "OVERSAMPLING", { rx, p.getY() + 148, 150, 16 }, juce::Justification::centredLeft, fonts::label (12.0f));

    // input readouts (click to reset the peak)
    auto ra = readoutArea;
    drawReadout (g, ra.removeFromTop (34), "PEAK", formatLufs (peakHold), "dB", peakHold > -0.1f ? colours::bad : colours::text, 15.0f);
    ra.removeFromTop (4);
    const bool inZone = rms >= CharacterChain::inputTargetMin && rms <= CharacterChain::inputTargetMax;
    drawReadout (g, ra, "RMS", formatLufs (rms), "dB", inZone ? colours::good : colours::text, 15.0f);

    // GR labels and values
    const char* names[3] = { "LOW", "MID", "HIGH" };
    for (int b = 0; b < 3; ++b)
    {
        const auto m = grMeters[(size_t) b]->getBounds().toFloat();
        paint::engraved (g, names[b], { m.getX() - 8, m.getY() - 22, 56, 16 }, juce::Justification::centred, fonts::label (12.5f), colours::text);
        const bool ok = gr[(size_t) b] >= CharacterChain::grTargetMin[b] && gr[(size_t) b] <= CharacterChain::grTargetMax[b];
        auto vr = juce::Rectangle<float> (m.getX() - 10, m.getBottom() + 8, 60, 24);
        g.setColour (juce::Colour (0xff0a0c0f));
        g.fillRoundedRectangle (vr, 3.0f);
        g.setFont (fonts::numeric (13.0f));
        g.setColour (ok ? colours::good : colours::text);
        g.drawText ("-" + juce::String (gr[(size_t) b], 1), vr, juce::Justification::centred, false);
    }

    // hint
    auto hint = juce::Rectangle<float> (rx, p.getY() + 230, 150, 250);
    g.setFont (fonts::label (11.5f));
    g.setColour (colours::textDim);
    g.drawFittedText ("1. Set the INPUT so the RMS bar sits in the green bracket.\n\n"
                      "2. Mix until each band's gain reduction lands in its target zone.\n\n"
                      "3. Use AMOUNT to taste (100% = reference).",
                      hint.toNearestInt(), juce::Justification::topLeft, 12);
}

void CharacterPage::mouseDown (const juce::MouseEvent& e)
{
    if (readoutArea.contains (e.position))
    {
        peakHold = -100.0f;
        repaint();
    }
}

void CharacterPage::tick()
{
    rms = proc.character.inputRmsDb.load();
    peakHold = std::max (peakHold, proc.character.inputPeakDb.load());
    inputMeter.setLevel (rms);
    inputMeter.repaint();
    for (int b = 0; b < 3; ++b)
    {
        gr[(size_t) b] = gr[(size_t) b] * 0.7f + proc.character.grDb[(size_t) b].load() * 0.3f;
        grMeters[(size_t) b]->setLevel (gr[(size_t) b]);
        grMeters[(size_t) b]->repaint();
    }
    if (isShowing())
    {
        repaint (readoutArea.expanded (4).toNearestInt());
        repaint (juce::Rectangle<int> (grMeters[0]->getX() - 12, grMeters[0]->getBottom(), 220, 36));
    }
}

// =============================================================================
// LIMITER
// =============================================================================
LimiterPage::LimiterPage (MeridianProcessor& p)
    : Page (p, "LIMITER", colours::limiter),
      drive (p.apvts, ID::limGain, "Drive", colours::limiter, RackKnob::Size::large),
      ceiling (p.apvts, ID::limCeiling, "Ceiling", colours::limiter),
      power (p.apvts, ID::limOn, colours::limiter),
      grHistory (240, 0.0f), peakHistory (240, -60.0f)
{
    for (auto* c : std::initializer_list<juce::Component*> { &drive, &ceiling, &power, &outMeter })
        addAndMakeVisible (c);
    outMeter.setScale ({ 0, -6, -12, -18, -24, -36, -48 });
    drive.setTooltip ("Gain pushed into the limiter. The limiter keeps true peaks under the ceiling.");
    ceiling.setTooltip ("Maximum true-peak output level. -1 dBTP is a safe choice for streaming.");
}

void LimiterPage::resized()
{
    const auto p = plateBounds().toNearestInt();
    power.setBounds (p.getRight() - 52, p.getY() + 10, 32, 32);
    drive.setBounds (p.getX() + 40, p.getY() + 60, 220, 250);
    ceiling.setBounds (p.getX() + 86, p.getY() + 330, 128, 150);
    historyArea = juce::Rectangle<float> ((float) p.getX() + 300, (float) p.getY() + 66, 620, 410);
    outMeter.setBounds (p.getX() + 954, p.getY() + 92, 44, 384);
}

void LimiterPage::paint (juce::Graphics& g)
{
    Page::paint (g);
    const auto p = plateBounds();
    paint::sectionTitle (g, "GAIN REDUCTION HISTORY", { historyArea.getX(), p.getY() + 42, historyArea.getWidth(), 16 }, accent);
    paint::sectionTitle (g, "OUTPUT", { p.getX() + 954, p.getY() + 42, 150, 16 }, accent);

    paint::screen (g, historyArea);
    auto h = historyArea.reduced (40.0f, 14.0f).withTrimmedLeft (-28.0f);
    auto plot = h.withTrimmedLeft (8.0f);
    const float maxGr = 12.0f;
    g.setFont (fonts::numeric (10.0f));
    for (int db = 0; db <= 12; db += 3)
    {
        const float y = plot.getY() + plot.getHeight() * (float) db / maxGr;
        g.setColour (colours::screenGrid);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (colours::textFaint);
        g.drawText (db == 0 ? "0" : "-" + juce::String (db), juce::Rectangle<float> (h.getX() - 32, y - 7, 30, 14), juce::Justification::centredRight, false);
    }

    const int n = (int) grHistory.size();
    juce::Path wave, grPath;
    for (int i = 0; i < n; ++i)
    {
        const int idx = (histPos + i) % n;
        const float x = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
        // output level as a faint waveform-like envelope (bottom-up)
        const float lv = juce::jlimit (0.0f, 1.0f, (peakHistory[(size_t) idx] + 48.0f) / 48.0f);
        const float yw = plot.getCentreY() - lv * plot.getHeight() * 0.38f;
        if (i == 0) wave.startNewSubPath (x, plot.getCentreY());
        wave.lineTo (x, yw);
        const float yg = plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, grHistory[(size_t) idx] / maxGr);
        if (i == 0) grPath.startNewSubPath (x, plot.getY());
        grPath.lineTo (x, yg);
    }
    wave.lineTo (plot.getRight(), plot.getCentreY());
    wave.closeSubPath();
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillPath (wave);
    g.fillPath (wave, juce::AffineTransform::verticalFlip (plot.getCentreY() * 2.0f));

    grPath.lineTo (plot.getRight(), plot.getY());
    grPath.closeSubPath();
    g.setColour (accent.withAlpha (0.35f));
    g.fillPath (grPath);
    g.setColour (accent);
    g.strokePath (grPath, juce::PathStrokeType (1.4f));

    // readouts
    const float rx = p.getX() + 1010.0f;
    const float tp = proc.outputLoudness.truePeakMax.load();
    const float ceil = proc.apvts.getRawParameterValue (ID::limCeiling)->load();
    drawReadout (g, { rx, p.getY() + 92, 100, 52 }, "GR", grNow > 0.05f ? "-" + juce::String (grNow, 1) : "0.0", "dB", accent, 20.0f);
    drawReadout (g, { rx, p.getY() + 152, 100, 52 }, "TRUE PEAK", formatLufs (tp), "dBTP",
                 tp > ceil + 0.2f ? colours::bad : colours::text, 18.0f);
    drawReadout (g, { rx, p.getY() + 212, 100, 52 }, "SHORT-TERM", formatLufs (proc.outputLoudness.shortTerm.load()), "LUFS",
                 colours::text, 18.0f);
}

void LimiterPage::tick()
{
    grNow = grNow * 0.6f + proc.limiter.grDb.load() * 0.4f;
    grHistory[(size_t) histPos] = proc.limiter.grDb.load();
    peakHistory[(size_t) histPos] = proc.outputPeakDb.load();
    histPos = (histPos + 1) % (int) grHistory.size();
    outMeter.setLevel (proc.outputPeakDb.load());
    outMeter.repaint();
    if (isShowing()) repaint();
}

// =============================================================================
// LOUDNESS + REFERENCE
// =============================================================================
LoudnessPage::LoudnessPage (MeridianProcessor& p)
    : Page (p, "LOUDNESS", colours::loudness),
      matchBypass (p.apvts, ID::matchBypass, "LEVEL-MATCHED BYPASS", colours::loudness),
      matchRef (p.apvts, ID::refMatch, "MATCH LEVEL", colours::loudness),
      listen (p.apvts, ID::refPlay, { "LISTEN: MIX", "LISTEN: REFERENCE" }, colours::loudness),
      histST (600, -100.0f), histM (600, -100.0f)
{
    for (int i = 0; i < numLoudnessTargets; ++i)
        target.addItem (loudnessTargets[i].name, i + 1);
    targetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ID::lufsTarget, target);

    for (auto* b : { &resetButton, &loadButton, &clearButton })
        setAccent (*b, colours::loudness);
    resetButton.onClick = [this]
    {
        proc.resetLoudness();
        std::fill (histST.begin(), histST.end(), -100.0f);
        std::fill (histM.begin(), histM.end(), -100.0f);
    };
    loadButton.onClick = [this] { chooseFile(); };
    clearButton.onClick = [this] { proc.reference.clear(); };

    resetButton.setTooltip ("Restart integrated loudness, range, true-peak max and the tonal-balance average.");
    matchBypass.setTooltip ("When bypassing, turn the unprocessed signal up or down to the processed loudness for a fair comparison.");
    matchRef.setTooltip ("Play the reference at the same loudness as your mix, so you compare tone and dynamics rather than volume.");
    loadButton.setTooltip ("Load a finished, well-mixed song (WAV, AIFF, FLAC, MP3, M4A). You can also drop a file onto this page.");

    for (auto* c : std::initializer_list<juce::Component*> { &target, &resetButton, &loadButton, &clearButton, &matchBypass, &matchRef, &listen })
        addAndMakeVisible (c);
}

LoudnessPage::~LoudnessPage() = default;

void LoudnessPage::resized()
{
    const auto p = plateBounds();
    leftPanel = { p.getX() + 24, p.getY() + 68, 520, 444 };
    rightPanel = { p.getX() + 568, p.getY() + 68, p.getRight() - 24 - (p.getX() + 568), 444 };

    auto l = leftPanel;
    l.removeFromTop (182);                         // big readouts
    auto targetRow = l.removeFromTop (30);
    target.setBounds (targetRow.removeFromLeft (230).toNearestInt());
    l.removeFromTop (10);
    auto buttons = l.removeFromBottom (30);
    resetButton.setBounds (buttons.removeFromLeft (90).toNearestInt());
    buttons.removeFromLeft (10);
    matchBypass.setBounds (buttons.removeFromLeft (200).toNearestInt());
    l.removeFromBottom (10);
    historyArea = l;

    auto r = rightPanel;
    auto top = r.removeFromTop (30);
    loadButton.setBounds (top.removeFromLeft (170).toNearestInt());
    top.removeFromLeft (8);
    clearButton.setBounds (top.removeFromLeft (70).toNearestInt());
    r.removeFromTop (8);
    fileArea = r.removeFromTop (34);
    r.removeFromTop (8);
    tableArea = r.removeFromTop (140);
    r.removeFromTop (10);
    auto lr = r.removeFromTop (30);
    listen.setBounds (lr.removeFromLeft (290).toNearestInt());
    lr.removeFromLeft (10);
    matchRef.setBounds (lr.removeFromLeft (120).toNearestInt());
    r.removeFromTop (10);
    tonalArea = r;
}

void LoudnessPage::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a well-mixed reference song",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   proc.reference.getFormatManager().getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc.reference.load (f);
                          });
}

bool LoudnessPage::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (proc.reference.getFormatManager().findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void LoudnessPage::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
    {
        juce::File file (f);
        if (proc.reference.getFormatManager().findFormatForFileExtension (file.getFileExtension()) != nullptr)
        {
            proc.reference.load (file);
            break;
        }
    }
    repaint();
}

void LoudnessPage::tick()
{
    auto& m = proc.outputLoudness;
    momentary = m.momentary.load(); shortTerm = m.shortTerm.load(); integrated = m.integrated.load();
    range = m.loudnessRange.load(); truePeak = m.truePeakMax.load(); stMax = m.shortTermMax.load();
    inIntegrated = proc.inputLoudness.integrated.load();

    if (++histTick >= 3)
    {
        histTick = 0;
        histST[(size_t) histPos] = shortTerm;
        histM[(size_t) histPos] = momentary;
        histPos = (histPos + 1) % (int) histST.size();
    }

    if (proc.reference.getVersion() != refVersion || proc.reference.isLoading())
    {
        refVersion = proc.reference.getVersion();
        ref = proc.reference.getAnalysis();
    }
    mixFrames = proc.outputTonal.frames.load();
    for (int b = 0; b < BandMap::numBands; ++b)
        mixSpectrum[(size_t) b] = proc.outputTonal.averageDb[(size_t) b].load();

    clearButton.setEnabled (ref.valid || ref.error.isNotEmpty());
    listen.setEnabled (ref.valid);
    if (isShowing()) repaint();
}

void LoudnessPage::paint (juce::Graphics& g)
{
    Page::paint (g);
    const auto p = plateBounds();
    paint::sectionTitle (g, "OUTPUT LOUDNESS  (BS.1770 / EBU R128)", { leftPanel.getX(), p.getY() + 46, leftPanel.getWidth(), 16 }, accent);
    paint::sectionTitle (g, "REFERENCE TRACK", { rightPanel.getX(), p.getY() + 46, rightPanel.getWidth(), 16 }, accent);
    paintLoudness (g, leftPanel);
    paintReference (g, rightPanel);

    if (dragOver)
    {
        g.setColour (accent.withAlpha (0.12f));
        g.fillRoundedRectangle (rightPanel.expanded (6), 8.0f);
        g.setColour (accent);
        g.drawRoundedRectangle (rightPanel.expanded (6), 8.0f, 2.0f);
    }
}

void LoudnessPage::paintLoudness (juce::Graphics& g, juce::Rectangle<float> area)
{
    const int ti = juce::jlimit (0, numLoudnessTargets - 1, (int) proc.apvts.getRawParameterValue (ID::lufsTarget)->load());
    const auto tgt = loudnessTargets[ti];

    // big integrated value
    auto big = area.removeFromTop (110);
    paint::screen (g, big, 6.0f);
    auto bi = big.reduced (16, 10);
    g.setFont (fonts::label (12.5f));
    g.setColour (colours::textDim);
    g.drawText ("INTEGRATED", bi.removeFromTop (16), juce::Justification::centredLeft, false);
    const float delta = integrated - tgt.lufs;
    const juce::Colour ic = integrated <= -99 ? colours::textFaint
                          : (std::abs (delta) <= 1.0f ? colours::good : (std::abs (delta) <= 3.0f ? colours::warn : colours::text));
    auto bigFont = fonts::numeric (54.0f);
    g.setFont (bigFont);
    g.setColour (ic);
    const auto iv = formatLufs (integrated);
    g.drawText (iv, bi, juce::Justification::centredLeft, false);
    const float w = juce::GlyphArrangement::getStringWidth (bigFont, iv);
    g.setFont (fonts::label (18.0f));
    g.setColour (colours::textDim);
    g.drawText ("LUFS", bi.withTrimmedLeft (w + 10).withTrimmedTop (22), juce::Justification::centredLeft, false);

    auto side = big.reduced (16, 12).removeFromRight (190);
    g.setFont (fonts::label (12.0f));
    g.setColour (colours::textDim);
    g.drawText ("VS TARGET " + juce::String (tgt.lufs, 0) + " LUFS", side.removeFromTop (16), juce::Justification::centredRight, false);
    g.setFont (fonts::numeric (24.0f));
    g.setColour (integrated <= -99 ? colours::textFaint : ic);
    g.drawText (integrated <= -99 ? "--" : (delta >= 0 ? "+" : "") + juce::String (delta, 1) + " LU", side.removeFromTop (34), juce::Justification::centredRight, false);
    g.setFont (fonts::label (11.5f));
    g.setColour (colours::textFaint);
    g.drawText ("input " + formatLufs (inIntegrated) + " LUFS", side.removeFromTop (16), juce::Justification::centredRight, false);

    area.removeFromTop (10);
    auto row = area.removeFromTop (58);
    const float cw = (row.getWidth() - 30) / 4.0f;
    drawReadout (g, row.removeFromLeft (cw), "SHORT-TERM", formatLufs (shortTerm), "LUFS", colours::text, 18.0f); row.removeFromLeft (10);
    drawReadout (g, row.removeFromLeft (cw), "MOMENTARY", formatLufs (momentary), "LUFS", colours::text, 18.0f); row.removeFromLeft (10);
    drawReadout (g, row.removeFromLeft (cw), "RANGE", juce::String (range, 1), "LU", colours::text, 18.0f); row.removeFromLeft (10);
    drawReadout (g, row, "TRUE PEAK", formatLufs (truePeak), "dBTP", truePeak > tgt.truePeak ? colours::bad : colours::text, 18.0f);

    // target row text (combo box sits on the left)
    auto tr = area.removeFromTop (30).withTrimmedLeft (240);
    g.setFont (fonts::label (12.0f));
    g.setColour (colours::textFaint);
    g.drawText ("peak limit " + juce::String (tgt.truePeak, 1) + " dBTP", tr, juce::Justification::centredLeft, false);

    // history
    paint::screen (g, historyArea, 5.0f);
    auto h = historyArea.reduced (34, 10).withTrimmedLeft (-22).withTrimmedBottom (4);
    auto plot = h.withTrimmedLeft (6);
    const float top = -6.0f, bottom = -42.0f;
    auto yFor = [&] (float l) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, (top - l) / (top - bottom)); };
    g.setFont (fonts::numeric (9.5f));
    for (float l = -6; l >= -42; l -= 6)
    {
        g.setColour (colours::screenGrid);
        g.drawHorizontalLine ((int) yFor (l), plot.getX(), plot.getRight());
        g.setColour (colours::textFaint);
        g.drawText (juce::String ((int) l), juce::Rectangle<float> (h.getX() - 26, yFor (l) - 6, 24, 12), juce::Justification::centredRight, false);
    }
    g.setColour (colours::good.withAlpha (0.7f));
    const float yt = yFor (tgt.lufs);
    for (float x = plot.getX(); x < plot.getRight(); x += 8)
        g.drawLine (x, yt, std::min (x + 4, plot.getRight()), yt, 1.0f);

    auto drawHist = [&] (const std::vector<float>& hist, juce::Colour c, float thick)
    {
        juce::Path path;
        bool started = false;
        const int n = (int) hist.size();
        for (int i = 0; i < n; ++i)
        {
            const float v = hist[(size_t) ((histPos + i) % n)];
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
            if (v <= -70.0f) { started = false; continue; }
            if (! started) { path.startNewSubPath (x, yFor (v)); started = true; }
            else path.lineTo (x, yFor (v));
        }
        g.setColour (c);
        g.strokePath (path, juce::PathStrokeType (thick));
    };
    drawHist (histM, accent.withAlpha (0.35f), 1.0f);
    drawHist (histST, accent, 1.8f);
    g.setFont (fonts::label (10.5f));
    g.setColour (colours::textFaint);
    g.drawText ("LAST 60 S    short-term (bright)  momentary (faint)  target (dashed)", historyArea.reduced (10, 4).removeFromBottom (14),
                juce::Justification::centredRight, false);
}

void LoudnessPage::paintReference (juce::Graphics& g, juce::Rectangle<float>)
{
    // file line
    paint::screen (g, fileArea, 4.0f);
    auto fr = fileArea.reduced (10, 0);
    g.setFont (fonts::label (13.5f));
    if (proc.reference.isLoading())
    {
        g.setColour (accent);
        g.drawText ("Analysing...", fr, juce::Justification::centredLeft, false);
    }
    else if (ref.valid)
    {
        g.setColour (colours::text);
        const int secs = (int) ref.durationSec;
        g.drawText (ref.name, fr.withTrimmedRight (120), juce::Justification::centredLeft, true);
        g.setFont (fonts::numeric (11.5f));
        g.setColour (colours::textDim);
        g.drawText (juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2) + "  "
                    + juce::String (ref.fileSampleRate / 1000.0, 1) + " kHz", fr, juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour (ref.error.isNotEmpty() ? colours::bad : colours::textFaint);
        g.drawText (ref.error.isNotEmpty() ? ref.error : "No reference loaded - click LOAD or drop an audio file here",
                    fr, juce::Justification::centredLeft, true);
    }

    // comparison table
    auto t = tableArea;
    g.setColour (juce::Colour (0xff0a0c0f));
    g.fillRoundedRectangle (t, 4.0f);
    t = t.reduced (12, 6);
    const float c0 = t.getWidth() * 0.40f, cw = (t.getWidth() - c0) / 3.0f;
    auto header = t.removeFromTop (20);
    g.setFont (fonts::label (11.5f));
    g.setColour (colours::textDim);
    header.removeFromLeft (c0);
    g.drawText ("YOUR MIX", header.removeFromLeft (cw), juce::Justification::centredRight, false);
    g.drawText ("REFERENCE", header.removeFromLeft (cw), juce::Justification::centredRight, false);
    g.drawText ("DIFFERENCE", header, juce::Justification::centredRight, false);

    struct Row { const char* name; float mix, ref; const char* unit; };
    const Row rows[] = {
        { "Integrated loudness", integrated, ref.integrated, "LUFS" },
        { "Loudest 3 s (short-term max)", stMax, ref.shortTermMax, "LUFS" },
        { "Loudness range", range, ref.range, "LU" },
        { "True peak", truePeak, ref.truePeak, "dBTP" },
        { "Peak-to-loudness (PLR)", (truePeak > -99 && integrated > -99) ? truePeak - integrated : -100.0f,
                                    (ref.truePeak > -99 && ref.integrated > -99) ? ref.truePeak - ref.integrated : -100.0f, "dB" },
    };
    const float rh = t.getHeight() / 5.0f;
    for (auto& rw : rows)
    {
        auto line = t.removeFromTop (rh);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.drawHorizontalLine ((int) line.getY(), line.getX(), line.getRight());
        g.setFont (fonts::label (12.5f));
        g.setColour (colours::text);
        g.drawText (rw.name, line.removeFromLeft (c0), juce::Justification::centredLeft, false);
        const bool isRange = juce::String (rw.unit) == "LU";
        auto fmt = [&] (float v) { return isRange ? juce::String (v, 1) : formatLufs (v); };
        g.setFont (fonts::numeric (12.5f));
        g.setColour (colours::text);
        g.drawText (fmt (rw.mix), line.removeFromLeft (cw), juce::Justification::centredRight, false);
        g.setColour (ref.valid ? colours::text : colours::textFaint);
        g.drawText (ref.valid ? fmt (rw.ref) : "--", line.removeFromLeft (cw), juce::Justification::centredRight, false);
        const bool both = ref.valid && rw.mix > -99 && rw.ref > -99;
        const float d = rw.mix - rw.ref;
        g.setColour (! both ? colours::textFaint : (std::abs (d) <= 1.0f ? colours::good : (std::abs (d) <= 3.0f ? colours::warn : colours::bad)));
        g.drawText (both ? (d >= 0 ? "+" : "") + juce::String (d, 1) : "--", line, juce::Justification::centredRight, false);
    }

    paintTonal (g, tonalArea);
}

void LoudnessPage::paintTonal (juce::Graphics& g, juce::Rectangle<float> area)
{
    paint::screen (g, area, 5.0f);
    auto inner = area.reduced (34, 10).withTrimmedLeft (-20).withTrimmedBottom (14).withTrimmedTop (14);
    auto plot = inner.withTrimmedLeft (4);
    const auto& map = proc.outputTonal.getBandMap();

    auto xFor = [&] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    const float rangeDb = 40.0f;
    auto yFor = [&] (float db) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, (rangeDb * 0.5f - db) / rangeDb); };

    g.setFont (fonts::numeric (9.5f));
    for (auto [f, t] : { std::pair<float, const char*> { 50, "50" }, { 100, "100" }, { 500, "500" }, { 1000, "1k" }, { 5000, "5k" }, { 10000, "10k" } })
    {
        g.setColour (colours::screenGrid);
        g.drawVerticalLine ((int) xFor (f), plot.getY(), plot.getBottom());
        g.setColour (colours::textFaint);
        g.drawText (t, juce::Rectangle<float> (xFor (f) - 16, plot.getBottom() + 2, 32, 12), juce::Justification::centred, false);
    }
    for (float db = -15; db <= 15; db += 15)
    {
        g.setColour (db == 0 ? colours::screenGrid.brighter (0.5f) : colours::screenGrid);
        g.drawHorizontalLine ((int) yFor (db), plot.getX(), plot.getRight());
        g.setColour (colours::textFaint);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (inner.getX() - 24, yFor (db) - 6, 22, 12), juce::Justification::centredRight, false);
    }

    // normalise each spectrum to its own average over 100 Hz - 8 kHz so the two
    // curves compare tonal shape, independent of loudness
    auto normalise = [&] (const std::array<float, BandMap::numBands>& s)
    {
        std::array<float, BandMap::numBands> out {};
        double sum = 0; int n = 0;
        for (int b = 0; b < BandMap::numBands; ++b)
            if (map.centreHz[(size_t) b] > 100 && map.centreHz[(size_t) b] < 8000) { sum += s[(size_t) b]; ++n; }
        const float mean = n > 0 ? (float) (sum / n) : 0.0f;
        for (int b = 0; b < BandMap::numBands; ++b)
            out[(size_t) b] = s[(size_t) b] - mean;
        return out;
    };

    const bool haveMix = mixFrames > 4;
    const bool haveRef = ref.valid;
    std::array<float, BandMap::numBands> mixN {}, refN {};
    if (haveMix) mixN = normalise (mixSpectrum);
    if (haveRef) refN = normalise (ref.spectrumDb);

    auto pathFor = [&] (const std::array<float, BandMap::numBands>& s)
    {
        juce::Path pth;
        for (int b = 0; b < BandMap::numBands; ++b)
        {
            const float x = xFor (map.centreHz[(size_t) b]);
            const float y = yFor (s[(size_t) b]);
            if (b == 0) pth.startNewSubPath (x, y); else pth.lineTo (x, y);
        }
        return pth;
    };

    if (haveMix && haveRef)
    {
        // shade the difference
        juce::Path diff;
        for (int b = 0; b < BandMap::numBands; ++b)
        {
            const float x = xFor (map.centreHz[(size_t) b]);
            if (b == 0) diff.startNewSubPath (x, yFor (mixN[(size_t) b])); else diff.lineTo (x, yFor (mixN[(size_t) b]));
        }
        for (int b = BandMap::numBands - 1; b >= 0; --b)
            diff.lineTo (xFor (map.centreHz[(size_t) b]), yFor (refN[(size_t) b]));
        diff.closeSubPath();
        g.setColour (colours::warn.withAlpha (0.12f));
        g.fillPath (diff);
    }
    if (haveRef)
    {
        auto rp = pathFor (refN);
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.6f).createDashedStroke (dashed, rp, dashes, 2);
        g.setColour (colours::text.withAlpha (0.85f));
        g.fillPath (dashed);
    }
    if (haveMix)
    {
        g.setColour (accent);
        g.strokePath (pathFor (mixN), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
    }

    g.setFont (fonts::label (11.0f));
    g.setColour (colours::textDim);
    auto legend = area.reduced (12, 6).removeFromTop (14);
    g.drawText ("TONAL BALANCE", legend, juce::Justification::centredLeft, false);
    g.setColour (accent);
    g.drawText ("your mix", legend.withTrimmedRight (90), juce::Justification::centredRight, false);
    g.setColour (colours::text);
    g.drawText ("- - reference", legend, juce::Justification::centredRight, false);

    if (! haveMix)
    {
        g.setColour (colours::textFaint);
        g.setFont (fonts::label (12.0f));
        g.drawText ("Play your session to measure the mix", plot, juce::Justification::centred, false);
    }
}

} // namespace meridian::ui
