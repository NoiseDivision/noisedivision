#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DarkDSP.h"
#include <atomic>

// DARK: one knob, 0-100 %, that makes the sound darker (see DarkDSP.h), an on/off switch,
// and three "advanced" controls: punch, mix and output.
//
//   darkening (the knob, punch) -> mix with the untouched sound -> output -> on/off
class DarkProcessor : public juce::AudioProcessor
{
public:
    DarkProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DARK"; }
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

    static constexpr float kAmountDefault = 50.0f, kPunchDefault = 60.0f;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    dark::Processor dsp;
    juce::SmoothedValue<float> wetMix, mixAmount, outputGain;
    juce::AudioBuffer<float> scratch;            // [0] second channel for mono, [1..2] untouched copy
    int chunkSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DarkProcessor)
};
