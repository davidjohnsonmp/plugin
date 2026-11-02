#pragma once

#include "EqDisplay.h"

namespace meridian::ui
{

class Page : public juce::Component
{
public:
    Page (MeridianProcessor& p, juce::String pageName, juce::Colour pageAccent) : proc (p), name (std::move (pageName)), accent (pageAccent) {}
    virtual void tick() = 0;
    juce::Rectangle<float> plateBounds() const { return getLocalBounds().toFloat().reduced (22.0f, 16.0f); }
    void paint (juce::Graphics& g) override { paint::faceplate (g, plateBounds(), accent, name); }

    MeridianProcessor& proc;
    juce::String name;
    juce::Colour accent;
};

// -----------------------------------------------------------------------------
class UnmaskPage : public Page
{
public:
    explicit UnmaskPage (MeridianProcessor&);
    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RackKnob recover, tame, bias, brighten, boost, gain;
    PowerButton power;
    EqDisplay display;
    CentreBar brightness { 0.5f, colours::unmask };
    Segmented scSwitch, quality;
    float recDb = 0, tameDb = 0, biasV = 0, brightV = 0;
};

// -----------------------------------------------------------------------------
class CharacterPage : public Page
{
public:
    explicit CharacterPage (MeridianProcessor&);
    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    RackKnob amount, low, mid, high;
    PowerButton power;
    LevelMeter inputMeter { -36.0f, 0.0f, colours::good };
    std::array<std::unique_ptr<LevelMeter>, 3> grMeters;
    Segmented scSwitch, oversampling;
    float peakHold = -100.0f, rms = -100.0f;
    std::array<float, 3> gr {};
    juce::Rectangle<float> readoutArea;
};

// -----------------------------------------------------------------------------
class LimiterPage : public Page
{
public:
    explicit LimiterPage (MeridianProcessor&);
    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RackKnob drive, ceiling;
    PowerButton power;
    LevelMeter outMeter { -48.0f, 0.0f, colours::good };
    std::vector<float> grHistory, peakHistory;
    int histPos = 0;
    float grNow = 0.0f;
    juce::Rectangle<float> historyArea;
};

// -----------------------------------------------------------------------------
class LoudnessPage : public Page, public juce::FileDragAndDropTarget
{
public:
    explicit LoudnessPage (MeridianProcessor&);
    ~LoudnessPage() override;
    void tick() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

private:
    void chooseFile();
    void paintLoudness (juce::Graphics&, juce::Rectangle<float>);
    void paintReference (juce::Graphics&, juce::Rectangle<float>);
    void paintTonal (juce::Graphics&, juce::Rectangle<float>);

    juce::ComboBox target;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAttachment;
    juce::TextButton resetButton { "RESET" }, loadButton { "LOAD REFERENCE..." }, clearButton { "CLEAR" };
    ParamToggle matchBypass, matchRef;
    Segmented listen;
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<float> histST, histM;
    int histPos = 0, histTick = 0;

    float momentary = -100, shortTerm = -100, integrated = -100, range = 0, truePeak = -100, stMax = -100;
    float inIntegrated = -100;
    ReferenceTrack::Analysis ref;
    int refVersion = -1;
    std::array<float, BandMap::numBands> mixSpectrum {};
    int mixFrames = 0;
    bool dragOver = false;

    juce::Rectangle<float> leftPanel, rightPanel, historyArea, tableArea, tonalArea, fileArea;
};

} // namespace meridian::ui
