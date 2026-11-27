#pragma once
#include "PluginProcessor.h"

class FlyMixChatEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FlyMixChatEditor (FlyMixChatProcessor&);
    ~FlyMixChatEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void send (const juce::String& text);
    void refreshLog();

    FlyMixChatProcessor& proc;
    juce::LookAndFeel_V4 lnf;

    juce::TextEditor log, input;
    juce::TextButton sendButton { "MEZCLAR" };
    juce::OwnedArray<juce::TextButton> chips;

    struct Knob { juce::Slider slider; juce::Label label; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };
    juce::OwnedArray<Knob> knobs;
    juce::ComboBox delayNote;
    juce::Label delayNoteLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> delayNoteAtt;

    juce::Rectangle<int> knobArea, meterArea;
    int lastLogSize = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlyMixChatEditor)
};
