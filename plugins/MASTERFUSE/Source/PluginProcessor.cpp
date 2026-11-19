#include "PluginProcessor.h"
#include "PluginEditor.h"

MasterFuseProcessor::MasterFuseProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout MasterFuseProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "amount", 1 }, "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), kAmountDefault,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "enabled", 1 }, "Enabled", true));

    // MATCH: brings the processed signal to the untouched signal's loudness, for fair on/off comparisons
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "match", 1 }, "Level Match", false));

    // ---- advanced section
    auto db = [] (float v, int) { return juce::String (v, 1); };

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "input", 1 }, "Input",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "limiter", 1 }, "Limiter Gain",
        juce::NormalisableRange<float> (0.0f, 12.0f, 0.1f), kLimiterGainDefault,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 0.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));

    return layout;
}

bool MasterFuseProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void MasterFuseProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.setAmount (apvts.getRawParameterValue ("amount")->load() * kReferenceScale);
    // the limiter works at 0 dB and its output is then trimmed to the ceiling,
    // so the gain into it is the limiter gain plus the (negative) ceiling
    dsp.outputGainDb = apvts.getRawParameterValue ("limiter")->load() + dsp.ceilingDb;
    dsp.prepare (sampleRate);

    inputGain.reset (sampleRate, 0.03);
    outputGain.reset (sampleRate, 0.03);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("input")->load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));

    latency = dsp.getLatency();
    setLatencySamples (latency);

    for (auto& d : dryDelay)
        d.assign ((size_t) juce::jmax (1, latency), 0.0f);
    dryPos = 0;

    chunkSize = juce::jmax (16, samplesPerBlock);
    scratch.setSize (3, chunkSize);

    matcher.prepare (sampleRate);
    matchGain.reset (sampleRate, 0.25);
    matchGain.setCurrentAndTargetValue (1.0f);
    matchGainDb.store (0.0f);

    wetMix.reset (sampleRate, 0.015);
    wetMix.setCurrentAndTargetValue (apvts.getRawParameterValue ("enabled")->load() > 0.5f ? 1.0f : 0.0f);
}

void MasterFuseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int total = buffer.getNumSamples();
    if (numCh == 0 || total == 0 || chunkSize == 0)
        return;

    dsp.setAmount (apvts.getRawParameterValue ("amount")->load() * kReferenceScale);
    dsp.outputGainDb = apvts.getRawParameterValue ("limiter")->load() + dsp.ceilingDb;
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("input")->load()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));
    wetMix.setTargetValue (apvts.getRawParameterValue ("enabled")->load() > 0.5f ? 1.0f : 0.0f);
    const bool match = apvts.getRawParameterValue ("match")->load() > 0.5f;

    const int delayLen = (int) dryDelay[0].size();

    // hosts may send more samples than they announced, so work in chunks
    for (int start = 0; start < total; start += chunkSize)
    {
        const int n = juce::jmin (chunkSize, total - start);

        float* left = buffer.getWritePointer (0, start);
        float* right = numCh > 1 ? buffer.getWritePointer (1, start) : scratch.getWritePointer (0);
        if (numCh == 1)
            juce::FloatVectorOperations::copy (right, left, n);

        // keep the untouched signal, delayed to line up with the processed one
        float* dry[2] = { scratch.getWritePointer (1), scratch.getWritePointer (2) };
        {
            const float* in[2] = { left, right };
            int pos = dryPos;
            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    float& slot = dryDelay[ch][(size_t) pos];
                    dry[ch][i] = latency > 0 ? slot : in[ch][i];
                    slot = in[ch][i];
                }
                if (++pos >= delayLen) pos = 0;
            }
            dryPos = pos;
        }

        // input gain -> processing and limiter -> output gain
        if (inputGain.isSmoothing())
            for (int i = 0; i < n; ++i) { const float g = inputGain.getNextValue(); left[i] *= g; right[i] *= g; }
        else if (const float g = inputGain.getTargetValue(); g != 1.0f)
            for (int i = 0; i < n; ++i) { left[i] *= g; right[i] *= g; }

        dsp.process (left, right, n);

        if (outputGain.isSmoothing())
            for (int i = 0; i < n; ++i) { const float g = outputGain.getNextValue(); left[i] *= g; right[i] *= g; }
        else if (const float g = outputGain.getTargetValue(); g != 1.0f)
            for (int i = 0; i < n; ++i) { left[i] *= g; right[i] *= g; }

        // MATCH: keep measuring both signals, so the gain is ready the moment it's switched on
        matcher.analyse (dry[0], dry[1], left, right, n);
        matchGainDb.store (matcher.getGainDb());
        matchGain.setTargetValue (match ? juce::Decibels::decibelsToGain (matcher.getGainDb()) : 1.0f);
        if (matchGain.isSmoothing())
            for (int i = 0; i < n; ++i) { const float g = matchGain.getNextValue(); left[i] *= g; right[i] *= g; }
        else if (const float g = matchGain.getTargetValue(); g != 1.0f)
            for (int i = 0; i < n; ++i) { left[i] *= g; right[i] *= g; }

        // on/off switch: a short crossfade between processed and untouched
        if (wetMix.isSmoothing() || wetMix.getTargetValue() < 0.5f)
        {
            for (int i = 0; i < n; ++i)
            {
                const float m = wetMix.getNextValue();
                left[i]  = dry[0][i] + m * (left[i]  - dry[0][i]);
                right[i] = dry[1][i] + m * (right[i] - dry[1][i]);
            }
        }
    }
}

juce::AudioProcessorEditor* MasterFuseProcessor::createEditor()
{
    return new MasterFuseEditor (*this);
}

void MasterFuseProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void MasterFuseProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasterFuseProcessor();
}
