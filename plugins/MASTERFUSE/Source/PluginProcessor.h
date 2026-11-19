#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "KnobDSP.h"
#include "LevelMatch.h"
#include <atomic>
#include <vector>

// MASTERFUSE: one control, 0-100 %, plus an on/off switch for comparing with the
// untouched sound, and three "advanced" gains: input, limiter gain and output.
// The processing itself lives in KnobDSP.h.
//
//   input gain -> band processing (the knob) -> limiter gain -> limiter -> output gain
class MasterFuseProcessor : public juce::AudioProcessor
{
public:
    MasterFuseProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MASTERFUSE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }   // a nameless program fails the VST3 validator
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static constexpr float kAmountDefault = 50.0f;
    static constexpr float kLimiterGainDefault = 5.0f;   // dB pushed into the limiter, as in the reference's default

    // The knob's 0-100 % covers the whole measured range of the reference (0-200 %),
    // so 50 % on the knob is the reference's own default of 100 %.
    static constexpr float kReferenceScale = 2.0f;

    juce::AudioProcessorValueTreeState apvts;

    // gain the MATCH switch applies (dB), for display
    std::atomic<float> matchGainDb { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    knobdsp::Processor dsp;

    // untouched signal, delayed by the processing latency, for the on/off switch
    std::vector<float> dryDelay[2];
    int dryPos = 0, latency = 0;
    juce::SmoothedValue<float> wetMix;          // 1 = processed, 0 = untouched
    juce::SmoothedValue<float> inputGain, outputGain;
    LevelMatcher matcher;                       // works out the MATCH gain
    juce::SmoothedValue<float> matchGain;       // applied to the processed signal while MATCH is on
    juce::AudioBuffer<float> scratch;           // [0] second channel for mono, [1..2] delayed dry
    int chunkSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterFuseProcessor)
};
