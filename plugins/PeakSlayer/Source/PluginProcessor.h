#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "LoudnessMeter.h"
#include "PresetManager.h"
#include <array>
#include <atomic>
#include <cmath>

class PeakSlayerProcessor : public juce::AudioProcessor
{
public:
    PeakSlayerProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PEAKSLAYER"; }
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

    // ---- constants measured from the reference clipper
    static constexpr float kCeilingDb   = -0.1f;        // fixed output ceiling
    static constexpr float kCeilingLin  = 0.98855309f;
    static constexpr float kKneeMatched = 0.413f;       // knee width / ceiling at SHAPE = centre
    static constexpr float kKneeSoftest = 1.5f;         // knee width / ceiling at SHAPE = full soft
    static constexpr float kDriveRange  = 15.0f;        // dB of drive across the CLIP knob

    static float driveDbFromKnob (float k) noexcept { return kCeilingDb + kDriveRange * k; }

    // SHAPE: -1 = softest, 0 = the measured curve, +1 = hard clip (no knee)
    static float kneeFromShape (float s) noexcept
    {
        return s <= 0.0f ? kKneeMatched + (kKneeSoftest - kKneeMatched) * (-s)
                         : kKneeMatched * (1.0f - s);
    }

    static constexpr float kClipDefault = 5.0f / 11.0f;  // CLIP starts at 5
    static constexpr float kFilterMin = 20.0f, kFilterMax = 20000.0f; // filter is off at these ends

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts };
    LoudnessMeter meter;                                 // measures the plugin's output
    std::atomic<float> inputPeak { 0.0f };               // linear peak of the incoming signal, max since last read

    // Scope ring buffers: written on the audio thread, read by the editor.
    static constexpr int scopeSize = 1 << 15;
    std::array<std::atomic<float>, scopeSize> scopeIn, scopeOut;
    std::atomic<int> scopeWrite { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static float clip (float x, float ceiling, float knee) noexcept;

    void setProcessingRate (double rate) noexcept;

    juce::dsp::Oversampling<float> oversampling { 2, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };

    juce::SmoothedValue<float> driveSmoothed, kneeSmoothed, mixSmoothed, outSmoothed;
    bool lastHq = true;

    // Clip band: the HPF / LPF pick out the band that gets clipped. Everything outside
    // it ("rest" = signal minus band) skips the clipper and is added straight back.
    // Two sets, because the filters run at the clipper's rate: [0] normal, [1] oversampled (HQ).
    std::array<juce::dsp::StateVariableTPTFilter<float>, 2> hpFilter, lpFilter;
    bool hpActive = false, lpActive = false;
    double baseSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakSlayerProcessor)
};
