#include "PluginProcessor.h"
#include "PluginEditor.h"

DarkProcessor::DarkProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DarkProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto percent = juce::AudioParameterFloatAttributes().withLabel ("%")
                       .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); });

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "amount", 1 }, "Darkness",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), kAmountDefault, percent));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "enabled", 1 }, "Enabled", true));

    // ---- advanced
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "punch", 1 }, "Punch",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), kPunchDefault, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "output", 1 }, "Output",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        {
            return (v > 0.049f ? "+" : "") + juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1);
        })));
    return layout;
}

bool DarkProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void DarkProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.setAmount (apvts.getRawParameterValue ("amount")->load() * 0.01f);
    dsp.setPunch (apvts.getRawParameterValue ("punch")->load() * 0.01f);
    dsp.prepare (sampleRate);

    chunkSize = juce::jmax (16, samplesPerBlock);
    scratch.setSize (3, chunkSize);

    auto init = [sampleRate] (juce::SmoothedValue<float>& v, double seconds, float value) { v.reset (sampleRate, seconds); v.setCurrentAndTargetValue (value); };
    init (wetMix, 0.015, apvts.getRawParameterValue ("enabled")->load() > 0.5f ? 1.0f : 0.0f);
    init (mixAmount, 0.03, apvts.getRawParameterValue ("mix")->load() * 0.01f);
    init (outputGain, 0.03, juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));
    setLatencySamples (0);
}

void DarkProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int total = buffer.getNumSamples();
    if (numCh == 0 || total == 0 || chunkSize == 0)
        return;

    dsp.setAmount (apvts.getRawParameterValue ("amount")->load() * 0.01f);
    dsp.setPunch (apvts.getRawParameterValue ("punch")->load() * 0.01f);
    wetMix.setTargetValue (apvts.getRawParameterValue ("enabled")->load() > 0.5f ? 1.0f : 0.0f);
    mixAmount.setTargetValue (apvts.getRawParameterValue ("mix")->load() * 0.01f);
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));

    // hosts may send more samples than they announced, so work in chunks
    for (int start = 0; start < total; start += chunkSize)
    {
        const int n = juce::jmin (chunkSize, total - start);
        float* left = buffer.getWritePointer (0, start);
        float* right = numCh > 1 ? buffer.getWritePointer (1, start) : scratch.getWritePointer (0);
        if (numCh == 1)
            juce::FloatVectorOperations::copy (right, left, n);

        float* dry[2] = { scratch.getWritePointer (1), scratch.getWritePointer (2) };
        juce::FloatVectorOperations::copy (dry[0], left, n);
        juce::FloatVectorOperations::copy (dry[1], right, n);

        dsp.process (left, right, n);

        // mix with the untouched sound, then the output level
        for (int i = 0; i < n; ++i)
        {
            const float m = mixAmount.getNextValue(), g = outputGain.getNextValue();
            left[i]  = (dry[0][i] + m * (left[i]  - dry[0][i])) * g;
            right[i] = (dry[1][i] + m * (right[i] - dry[1][i])) * g;
        }

        // on/off: a short crossfade between processed and untouched
        if (wetMix.isSmoothing() || wetMix.getTargetValue() < 0.5f)
            for (int i = 0; i < n; ++i)
            {
                const float m = wetMix.getNextValue();
                left[i]  = dry[0][i] + m * (left[i]  - dry[0][i]);
                right[i] = dry[1][i] + m * (right[i] - dry[1][i]);
            }
    }
}

juce::AudioProcessorEditor* DarkProcessor::createEditor() { return new DarkEditor (*this); }

void DarkProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void DarkProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DarkProcessor(); }
