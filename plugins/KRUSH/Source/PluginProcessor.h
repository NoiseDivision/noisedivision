#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "KrushDSP.h"
#include "PresetManager.h"
#include "OutputStage.h"
#include <array>
#include <atomic>

// KRUSH: multiband distortion.
//   input gain -> 4x oversampling -> [band split -> distortion per band -> tone filter -> dry/wet] -> output gain
class KrushProcessor : public juce::AudioProcessor
{
public:
    KrushProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "KRUSH"; }
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

    static juce::String bandId (int band, const char* name) { return "b" + juce::String (band + 1) + "_" + name; }

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts };

    // ---- data for the editor (written on the audio thread)
    struct Meter { std::atomic<float> peak { 0.0f }, rmsDb { -100.0f }; };
    std::atomic<float> limitReductionDb { 0.0f };   // CLIP/LIMIT stage gain reduction, for the LIMIT button
    Meter inMeter[2], outMeter[2];               // peak = linear, highest since the editor last read it

    static constexpr int kScopeSize = 2048;      // scrolling waveform: min/max per ~1.3 ms
    std::array<std::atomic<float>, kScopeSize> scopeInMin, scopeInMax, scopeOutMin, scopeOutMax;
    std::atomic<int> scopeWrite { 0 };

    static constexpr int kSpecSize = 4096;       // spectrum: the last 4096 output samples (mono)
    std::array<std::atomic<float>, kSpecSize> specRing;
    std::atomic<int> specWrite { 0 };
    std::atomic<double> hostRate { 48000.0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    krush::Settings readSettings() const;
    void meter (const float* l, const float* r, int n, Meter* m, double& msL, double& msR);

    juce::dsp::Oversampling<float> oversampling { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    krush::Engine engine;
    krush::OutputStage outStage;                 // optional CLIP / LIMIT after the output fader

    juce::SmoothedValue<float> inputGain, outputGain;
    juce::AudioBuffer<float> monoScratch, scopeScratch;
    int chunkSize = 0;

    double inMs[2] = {}, outMs[2] = {}, rmsCoef = 0.0;
    int bucketLen = 64, bucketFill = 0;
    float bInMin = 0, bInMax = 0, bOutMin = 0, bOutMax = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KrushProcessor)
};
