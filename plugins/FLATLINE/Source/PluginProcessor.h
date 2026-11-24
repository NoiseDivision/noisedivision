#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FlatlineDSP.h"

// FLATLINE: clean loudness. GAIN -> UPWARD -> CLIPPER -> true-peak LIMITER, with loudness meters (see FlatlineDSP.h).
class FlatlineProcessor : public juce::AudioProcessor
{
public:
    FlatlineProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FLATLINE"; }
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
    fl::Engine& engine() { return dsp; }
    std::atomic<double> rate { 48000.0 };

    // factory presets: name, description, parameter values
    struct Preset { const char* name; const char* desc; std::vector<std::pair<const char*, float>> values; };
    static const std::vector<Preset>& presets();
    void loadPreset (int index);
    int currentPreset = 0;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    fl::Settings settings() const;
    fl::Engine dsp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlatlineProcessor)
};
