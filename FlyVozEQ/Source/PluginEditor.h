#pragma once
#include "PluginProcessor.h"

class FlyVozEQEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FlyVozEQEditor (FlyVozEQProcessor&);
    ~FlyVozEQEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawCurve (juce::Graphics&, juce::Rectangle<float> area);

    FlyVozEQProcessor& proc;
    juce::LookAndFeel_V4 lnf;

    juce::TextButton analyzeButton, clearButton;
    juce::OwnedArray<juce::TextButton> modeButtons;
    juce::Slider intensity, output;
    juce::Label intensityLabel, outputLabel, statusLabel;
    juce::ToggleButton hpfButton;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> intensityAtt, outputAtt;
    std::unique_ptr<BA> hpfAtt;

    juce::Rectangle<float> curveArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlyVozEQEditor)
};
