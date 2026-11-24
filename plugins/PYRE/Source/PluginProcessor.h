#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PyreDSP.h"

// PYRE: any lead or synth as dense as a saw, then driven into a soft ceiling.
//
//   input -> IGNITE (drive, 4x oversampled) -> HEAT / TILT / MAX (the harmonic flattener, PyreDSP.h)
//         -> BURN (into the ceiling at -0.3 dBFS) -> MIX with the untouched input -> OUTPUT
//
// Latency: 2048 samples (the flattener) plus the oversampler's few samples, reported to the host.
class PyreProcessor : public juce::AudioProcessor
{
public:
    PyreProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PYRE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    pyre::Engine& engine() { return dsp; }
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };     // swapped to 0 by the editor

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    pyre::Settings settings() const;

    pyre::Engine dsp;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> dry, wet;
    std::vector<float> dryDelay[2]; int dryPos = 0, osLatency = 0;
    double dc[2] {};
    int chunk = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PyreProcessor)
};
