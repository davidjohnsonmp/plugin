#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

namespace meridian::ui
{

/** The Unmask EQ screen: live EQ curve, analysed spectrum, active-range
    shading, two draggable frequency-range limiters, Recover/Tame/Bias meters
    and a frequency/gain readout while clicking and dragging. */
class EqDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EqDisplay (MeridianProcessor& p);

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    float xForHz (float hz) const noexcept;
    float hzForX (float x) const noexcept;
    float yForDb (float db) const noexcept;
    float curveDbAt (float hz) const noexcept;
    int   handleAt (juce::Point<float> p) const noexcept;
    void  setLimit (int which, float hz);

    MeridianProcessor& proc;
    juce::RangedAudioParameter* lowParam = nullptr;
    juce::RangedAudioParameter* highParam = nullptr;

    static constexpr int numBands = SpectralUnmasker::numBands;
    std::array<float, numBands> curve {}, weight {}, spectrum {};
    float recMeter = 0, tameMeter = 0, biasMeter = 0;

    juce::Rectangle<float> plot, axisArea;
    int scaleIndex = 2;
    static constexpr float scales[4] = { 3.0f, 6.0f, 12.0f, 24.0f };

    int dragging = -1;          // 0 low limiter, 1 high limiter, 2 readout
    int hoverHandle = -1;
    juce::Point<float> pointer;
    bool showReadout = false;
};

} // namespace meridian::ui
