#pragma once

#include "dsp/FeedbackSuppressor.h"

#include <juce_audio_processors/juce_audio_processors.h>

class LZDefeedbackProcessor : public juce::AudioProcessor
{
public:
    LZDefeedbackProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // UI requests, applied on the audio thread
    void requestReset() noexcept { resetRequested.store (true); }
    void requestFixAll() noexcept { fixRequested.store (true); }

    const lzdf::FeedbackSuppressor& getSuppressor() const noexcept { return suppressor; }
    double getAnalysisSampleRate() const noexcept { return currentRate.load(); }

    juce::AudioProcessorValueTreeState params;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void updateSettings() noexcept;
    void applyPendingRestore() noexcept;

    lzdf::FeedbackSuppressor suppressor;
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<bool> resetRequested { false }, fixRequested { false };

    // fixed notches restored from the session, handed over to the audio thread
    struct StoredNotch { float freq = 0, depthDb = 0; };
    std::array<StoredNotch, lzdf::NotchBank::maxNotches> pending {};
    std::atomic<int> pendingCount { -1 };

    std::atomic<float>* pSensitivity = nullptr;
    std::atomic<float>* pDepth = nullptr;
    std::atomic<float>* pWidth = nullptr;
    std::atomic<float>* pNotches = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pHold = nullptr;
    std::atomic<float>* pRelease = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LZDefeedbackProcessor)
};
