#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ShrapnelEngine.h"
#include "PresetManager.h"
#include <array>
#include <atomic>

// SHRAPNEL: spectral distortion. See ShrapnelEngine.h for how the sound is processed.
// The drive curve's points live in the plugin state (child "CURVE"), not as parameters,
// because there can be 0 to 8 of them; the audio thread reads a lock-free copy.
class ShrapnelProcessor : public juce::AudioProcessor, private juce::ValueTree::Listener
{
public:
    ShrapnelProcessor();
    ~ShrapnelProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SHRAPNEL"; }
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

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts };

    // ---- drive curve points (message thread)
    std::vector<sh::CurvePoint> getCurve() const;
    void setCurve (const std::vector<sh::CurvePoint>&);
    sh::Settings currentSettings() const;           // what the engine is set to: also used to draw the curve

    // ---- for the editor
    bool copyDisplay (sh::Display& d) { return engine.copyDisplay (d); }
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };       // linear, highest since the editor last read them
    std::atomic<int> latencySamples { 0 };
    std::atomic<double> hostRate { 48000.0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { curveChanged(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { curveChanged(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { curveChanged(); }
    void valueTreeRedirected (juce::ValueTree&) override { curveChanged(); }
    void curveChanged();

    sh::Engine engine;
    std::array<std::atomic<float>, sh::kMaxCurvePoints * 2> curveAtomic {};
    std::atomic<int> curveCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShrapnelProcessor)
};
