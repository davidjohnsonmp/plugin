#include "PluginEditor.h"

namespace meridian
{
using namespace ui;

// =============================================================================
ModuleCard::ModuleCard (const juce::String& n, const juce::String& sub, juce::Colour a,
                        juce::AudioProcessorValueTreeState* state, const char* powerParam)
    : accent (a), name (n), subtitle (sub)
{
    if (state != nullptr && powerParam != nullptr)
    {
        power = std::make_unique<PowerButton> (*state, powerParam, accent);
        power->setTooltip (n + " on/off");
        addAndMakeVisible (*power);
    }
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ModuleCard::resized()
{
    if (power) power->setBounds (getWidth() - 40, 12, 28, 28);
}

void ModuleCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (0, 2), 7.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (selected ? 0xff2f333a : 0xff23262b), r.getX(), r.getY(),
                                             juce::Colour (selected ? 0xff1e2126 : 0xff17191c), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 7.0f);
    g.setTiledImageFill (paint::brushedTexture(), 0, 0, 0.6f);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (selected ? accent.withAlpha (0.9f) : juce::Colour (0xff33373e));
    g.drawRoundedRectangle (r, 7.0f, selected ? 1.5f : 1.0f);

    if (selected)
    {
        auto glow = r.removeFromBottom (4.0f).reduced (14.0f, 0);
        g.setColour (accent.withAlpha (0.25f));
        g.fillRoundedRectangle (glow.expanded (4, 3), 4.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (glow, 2.0f);
        r = getLocalBounds().toFloat().reduced (2.0f);
    }

    auto t = r.reduced (14.0f, 9.0f);
    paint::engraved (g, name, t.removeFromTop (20), juce::Justification::centredLeft, fonts::title (17.0f),
                     selected ? colours::text : colours::text.withAlpha (0.75f));
    g.setFont (fonts::label (11.5f));
    g.setColour (colours::textDim);
    g.drawText (subtitle, t.removeFromTop (16), juce::Justification::centredLeft, false);
    g.setFont (fonts::numeric (12.0f));
    g.setColour (accent.withAlpha (selected ? 1.0f : 0.7f));
    g.drawText (status, t.removeFromTop (18), juce::Justification::centredLeft, false);
}

void ModuleCard::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()) && onSelect) onSelect();
}

// =============================================================================
EditorContent::EditorContent (MeridianProcessor& p)
    : proc (p),
      inputKnob (p.apvts, ID::inGain, "Input", colours::text, RackKnob::Size::small),
      outputKnob (p.apvts, ID::outGain, "Output", colours::text, RackKnob::Size::small),
      bypass (p.apvts),
      unmaskPage (p), characterPage (p), limiterPage (p), loudnessPage (p)
{
    setLookAndFeel (&lnf);

    inputKnob.showValue = false;
    outputKnob.showValue = false;
    inMeter.showScale = false;
    outMeter.showScale = false;
    inMeter.setTooltip ("Input peak level (after the Input knob)");
    outMeter.setTooltip ("Output peak level");

    for (auto* c : std::initializer_list<juce::Component*> { &inputKnob, &outputKnob, &inMeter, &outMeter, &bypass,
                                                             &prevPreset, &nextPreset, &presetName, &savePreset })
        addAndMakeVisible (c);

    prevPreset.onClick = [this] { proc.presets.previous(); };
    nextPreset.onClick = [this] { proc.presets.next(); };
    presetName.onClick = [this] { showPresetMenu(); };
    savePreset.onClick = [this] { savePresetDialog(); };
    presetName.setTooltip ("Presets");
    savePreset.setTooltip ("Save the current settings as a user preset");

    pages = { &unmaskPage, &characterPage, &limiterPage, &loudnessPage };
    for (auto* pg : pages) addChildComponent (pg);

    cards[0] = std::make_unique<ModuleCard> ("UNMASK EQ", "Adaptive perceptual EQ", colours::unmask, &p.apvts, ID::umOn);
    cards[1] = std::make_unique<ModuleCard> ("CHARACTER", "Mix-bus chain", colours::character, &p.apvts, ID::chOn);
    cards[2] = std::make_unique<ModuleCard> ("LIMITER", "True-peak limiter", colours::limiter, &p.apvts, ID::limOn);
    cards[3] = std::make_unique<ModuleCard> ("LOUDNESS", "LUFS + reference", colours::loudness, nullptr, nullptr);
    for (int i = 0; i < 4; ++i)
    {
        cards[(size_t) i]->onSelect = [this, i] { showPage (i); };
        addAndMakeVisible (*cards[(size_t) i]);
    }

    setSize (baseWidth, baseHeight);
    showPage (juce::jlimit (0, 3, proc.uiPage));
    startTimerHz (30);
}

EditorContent::~EditorContent()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void EditorContent::showPage (int index)
{
    currentPage = index;
    proc.uiPage = index;
    for (int i = 0; i < 4; ++i)
    {
        pages[(size_t) i]->setVisible (i == index);
        cards[(size_t) i]->selected = (i == index);
        cards[(size_t) i]->repaint();
    }
}

void EditorContent::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight);
    auto bar = r.removeFromBottom (barHeight);
    for (auto* pg : pages) pg->setBounds (r);

    // header
    inputKnob.setBounds (236, 4, 56, 58);
    inMeter.setBounds (296, 18, 8, 38);
    outputKnob.setBounds (header.getRight() - 236, 4, 56, 58);
    outMeter.setBounds (header.getRight() - 176, 18, 8, 38);
    bypass.setBounds (header.getRight() - 58, 13, 38, 38);

    const int pw = 300;
    auto pr = juce::Rectangle<int> (header.getCentreX() - pw / 2 - 10, 16, pw, 32);
    prevPreset.setBounds (pr.removeFromLeft (30));
    pr.removeFromLeft (4);
    savePreset.setBounds (pr.removeFromRight (56));
    pr.removeFromRight (4);
    nextPreset.setBounds (pr.removeFromRight (30));
    pr.removeFromRight (4);
    presetName.setBounds (pr);

    // module bar: three processing cards in signal order, loudness card at the right
    auto b = bar.reduced (24, 12);
    const int cw = 236, gap = 34;
    for (int i = 0; i < 3; ++i)
    {
        cards[(size_t) i]->setBounds (b.getX() + i * (cw + gap), b.getY(), cw, b.getHeight());
    }
    cards[3]->setBounds (b.getRight() - cw, b.getY(), cw, b.getHeight());
}

void EditorContent::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto header = r.removeFromTop ((float) headerHeight);
    auto bar = r.removeFromBottom ((float) barHeight);

    paint::stage (g, r);

    // header bar
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1d21), 0, header.getY(), colours::header, 0, header.getBottom(), false));
    g.fillRect (header);
    g.setTiledImageFill (paint::brushedTexture(), 0, 0, 0.5f);
    g.fillRect (header);
    g.setColour (juce::Colours::black);
    g.drawHorizontalLine ((int) header.getBottom() - 1, 0, header.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (0, 0, header.getRight());

    // wordmark
    {
        auto logo = juce::Rectangle<float> (22, 8, 200, 48);
        auto mark = juce::Rectangle<float> (logo.getX(), logo.getY() + 8, 26, 26);
        g.setColour (colours::unmask);
        juce::Path m;
        m.addTriangle (mark.getX(), mark.getBottom(), mark.getCentreX(), mark.getY(), mark.getRight(), mark.getBottom());
        g.strokePath (m, juce::PathStrokeType (2.0f));
        g.setColour (colours::character);
        g.fillEllipse (mark.getCentreX() - 4, mark.getY() + 12, 8, 8);
        auto f = fonts::title (25.0f);
        f.setExtraKerningFactor (0.18f);
        paint::engraved (g, "MERIDIAN", logo.withTrimmedLeft (36).withHeight (30).translated (0, 4), juce::Justification::centredLeft, f, colours::text);
        auto f2 = fonts::label (11.0f);
        f2.setExtraKerningFactor (0.35f);
        g.setFont (f2);
        g.setColour (colours::textDim);
        g.drawText ("MIX RACK", logo.withTrimmedLeft (37).withTrimmedTop (32), juce::Justification::topLeft, false);
    }

    // knob value labels
    auto drawVal = [&] (float db, int x)
    {
        g.setFont (fonts::numeric (11.5f));
        g.setColour (colours::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db, 1) + " dB", juce::Rectangle<float> ((float) x, 26, 60, 16),
                    juce::Justification::centredLeft, false);
    };
    drawVal (proc.apvts.getRawParameterValue (ID::inGain)->load(), inMeter.getRight() + 8);
    drawVal (proc.apvts.getRawParameterValue (ID::outGain)->load(), outMeter.getRight() + 8);

    // bypass label
    const bool session = MeridianProcessor::sessionBypass.load();
    g.setFont (fonts::label (11.5f));
    g.setColour (session ? (blink < 8 ? colours::bad : colours::textFaint) : colours::textDim);
    g.drawText (session ? "SESSION BYPASSED" : "BYPASS", juce::Rectangle<float> (header.getRight() - 186, 22, 122, 20),
                juce::Justification::centredRight, false);

    // module bar
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff141518), 0, bar.getY(), juce::Colour (0xff0c0d0f), 0, bar.getBottom(), false));
    g.fillRect (bar);
    g.setColour (juce::Colours::black);
    g.drawHorizontalLine ((int) bar.getY(), 0, bar.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawHorizontalLine ((int) bar.getY() + 1, 0, bar.getRight());

    // signal-flow arrows between processing cards
    for (int i = 0; i < 2; ++i)
    {
        const auto a = cards[(size_t) i]->getBounds().toFloat();
        const float x0 = a.getRight() + 6, x1 = a.getRight() + 28, y = a.getCentreY();
        g.setColour (colours::textFaint);
        g.drawLine (x0, y, x1 - 4, y, 1.5f);
        juce::Path head; head.addTriangle (x1 - 6, y - 4, x1 - 6, y + 4, x1, y);
        g.fillPath (head);
    }
    const auto lim = cards[2]->getBounds().toFloat();
    const auto ld = cards[3]->getBounds().toFloat();
    g.setColour (colours::textFaint);
    for (float x = lim.getRight() + 10; x < ld.getX() - 10; x += 7)
        g.drawLine (x, lim.getCentreY(), x + 3, lim.getCentreY(), 1.0f);
    g.setFont (fonts::label (10.5f));
    g.drawText ("OUT", juce::Rectangle<float> (lim.getRight() + 10, lim.getCentreY() - 18, ld.getX() - lim.getRight() - 20, 14),
                juce::Justification::centred, false);
}

void EditorContent::timerCallback()
{
    for (auto* pg : pages) pg->tick();

    inMeter.setLevel (proc.inputPeakDb.load());
    outMeter.setLevel (proc.outputPeakDb.load());
    inMeter.repaint();
    outMeter.repaint();

    presetName.setButtonText (proc.presets.getCurrentName());

    const auto& a = proc.apvts;
    auto v = [&] (const char* id) { return a.getRawParameterValue (id)->load(); };
    cards[0]->status = v (ID::umOn) > 0.5f ? "R " + juce::String (juce::roundToInt (v (ID::umRecover))) + "%   T "
                                             + juce::String (juce::roundToInt (v (ID::umTame))) + "%" : "OFF";
    cards[1]->status = v (ID::chOn) > 0.5f ? "AMOUNT " + juce::String (juce::roundToInt (v (ID::chAmount))) + "%" : "OFF";
    cards[2]->status = v (ID::limOn) > 0.5f ? "GR " + juce::String (proc.limiter.grDb.load(), 1) + " dB" : "OFF";
    cards[3]->status = formatLufs (proc.outputLoudness.shortTerm.load()) + " LUFS S";
    for (auto& c : cards) c->repaint();

    blink = (blink + 1) % 16;
    repaint (0, 0, getWidth(), headerHeight);
}

void EditorContent::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lnf);
    menu.addSectionHeader ("FACTORY");
    const int nf = PresetManager::getNumFactoryPresets();
    const auto names = proc.presets.getAllPresetNames();
    for (int i = 0; i < nf; ++i)
        menu.addItem (i + 1, names[i], true, proc.presets.getCurrentIndex() == i);
    if (names.size() > nf)
    {
        menu.addSectionHeader ("USER");
        for (int i = nf; i < names.size(); ++i)
            menu.addItem (i + 1, names[i], true, proc.presets.getCurrentIndex() == i);
    }
    menu.addSeparator();
    menu.addItem (10000, "Save current as...");
    menu.addItem (10001, "Show preset folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetName),
                        [this] (int result)
                        {
                            if (result == 10000) savePresetDialog();
                            else if (result == 10001)
                            {
                                auto f = PresetManager::getUserPresetFolder();
                                f.createDirectory();
                                f.revealToUser();
                            }
                            else if (result > 0) proc.presets.loadPreset (result - 1);
                        });
}

void EditorContent::savePresetDialog()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name for the new preset:", juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", "My Preset");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result == 1)
            proc.presets.saveUserPreset (w->getTextEditorContents ("name"));
    }), true);
}

// =============================================================================
MeridianEditor::MeridianEditor (MeridianProcessor& p)
    : juce::AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setResizable (true, true);
    setResizeLimits (EditorContent::baseWidth * 6 / 10, EditorContent::baseHeight * 6 / 10,
                     EditorContent::baseWidth * 3 / 2, EditorContent::baseHeight * 3 / 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) EditorContent::baseWidth / EditorContent::baseHeight);
    setSize (EditorContent::baseWidth, EditorContent::baseHeight);
}

MeridianEditor::~MeridianEditor() = default;

void MeridianEditor::resized()
{
    const float scale = (float) getWidth() / (float) EditorContent::baseWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, EditorContent::baseWidth, EditorContent::baseHeight);
}

} // namespace meridian
