#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "Biquad.h"

class FlyVozEQProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numBands = 8;
    static constexpr float bandFreqs[numBands] = { 100, 200, 400, 800, 1600, 3200, 6400, 12000 };
    static constexpr float bandQ = 1.2f;
    static constexpr int numModes = 5;
    static juce::StringArray modeNames();

    FlyVozEQProcessor();
    ~FlyVozEQProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FLY VozEQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Para la interfaz ---
    void startAnalysis();
    void cancelAnalysis()       { analyzing = false; }
    void clearAnalysis()        { hasAnalysis = false; }
    bool isAnalyzing() const    { return analyzing.load(); }
    bool hasAnalysisData() const{ return hasAnalysis.load(); }
    float getProgress() const   { return progress.load(); }
    float getInputLevelDb() const { return inputLevelDb.load(); }
    float getDisplayGain (int b) const { return displayGain[(size_t) b].load(); }
    double getCurrentSampleRate() const { return currentFs; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void computeTargetGains (float* out) const;
    void analyzeFrame();

    double currentFs = 48000.0;
    Biquad hpf;
    std::array<Biquad, numBands> peaks;
    std::array<float, numBands> currentGain {};
    bool hpfOn = true;
    juce::SmoothedValue<float> outGain;

    // Análisis
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize> fifo {};
    std::array<float, fftSize * 2> fftData {};
    int fifoIndex = 0;
    std::array<double, numBands> bandAccum {};
    int voicedFrames = 0, framesNeeded = 1;

    std::atomic<bool> analyzing { false }, hasAnalysis { false };
    std::atomic<float> progress { 0.0f }, inputLevelDb { -100.0f };
    std::array<std::atomic<float>, numBands> measuredDb;
    std::array<std::atomic<float>, numBands> displayGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlyVozEQProcessor)
};
