#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include "Biquad.h"
#include "StyleEngine.h"

class FlyMixChatProcessor : public juce::AudioProcessor
{
public:
    FlyMixChatProcessor();
    ~FlyMixChatProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "FLY MixChat"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Chat (hilo de la interfaz) ---
    juce::String sendChat (const juce::String& message);
    juce::StringArray chatLog;
    MixSettings readSettings() const;
    float getGainReductionDb() const { return grDisplay.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    double fs = 48000.0;
    struct ChannelFilters { Biquad hpf, low, mud, pres, air, deessHp; };
    std::array<ChannelFilters, 2> filt;
    std::array<float, MixSettings::count> lastFilterValues {};

    float compEnv = 0.0f, deessEnv = 0.0f;
    std::atomic<float> grDisplay { 0.0f };

    // Ancho (Haas) y delay
    std::vector<float> widthBuf;
    int widthPos = 0;
    std::array<std::vector<float>, 2> delayBuf;
    int delayPos = 0;
    std::array<float, 2> delayLp {};
    juce::SmoothedValue<float> delayTimeSamples;

    juce::Reverb reverb;
    juce::AudioBuffer<float> wetBuf;
    juce::SmoothedValue<float> outGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlyMixChatProcessor)
};
