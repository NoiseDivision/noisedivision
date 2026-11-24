#include "PluginProcessor.h"
#include "PluginEditor.h"

PyreProcessor::PyreProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout PyreProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using Attr = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return juce::ParameterID { s, 1 }; };
    auto pct = Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; });
    auto db = [] (bool plus) { return Attr().withLabel ("dB").withStringFromValueFunction ([plus] (float v, int)
        { const float a = std::abs (v) < 0.05f ? 0.0f : v;
          return (plus && a > 0 ? "+" : a < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String()) + juce::String (std::abs (a), 1) + " dB"; }); };

    l.add (std::make_unique<F> (id ("ignite"), "Ignite", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pct));
    l.add (std::make_unique<F> (id ("heat"), "Heat", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, pct));
    l.add (std::make_unique<F> (id ("tilt"), "Tilt", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
        Attr().withStringFromValueFunction ([] (float v, int)
        {
            const double slope = -6.0 + 4.0 * v / 100.0;
            return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + juce::String (std::abs (slope), 1) + " dB/oct";
        })));
    l.add (std::make_unique<F> (id ("max"), "Max Raise", juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 18.0f, db (true)));
    l.add (std::make_unique<F> (id ("burn"), "Burn", juce::NormalisableRange<float> (0.0f, 18.0f, 0.1f), 3.0f, db (true)));
    l.add (std::make_unique<F> (id ("mix"), "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    l.add (std::make_unique<F> (id ("output"), "Output", juce::NormalisableRange<float> (-24.0f, 0.0f, 0.1f), 0.0f, db (false)));
    return l;
}

pyre::Settings PyreProcessor::settings() const
{
    auto v = [this] (const char* p) { return (double) apvts.getRawParameterValue (p)->load(); };
    pyre::Settings s;
    s.ignite = v ("ignite") * 0.01;
    s.heat = v ("heat") * 0.01;
    s.tiltDbOct = -6.0 + 4.0 * v ("tilt") / 100.0;
    s.maxDb = v ("max");
    s.burnDb = v ("burn");
    s.mix = v ("mix") * 0.01;
    s.outDb = v ("output");
    return s;
}

bool PyreProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void PyreProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chunk = juce::jmax (16, samplesPerBlock);
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    os->initProcessing ((size_t) chunk);
    osLatency = (int) std::lround (os->getLatencyInSamples());
    dsp.prepare (sampleRate);
    dry.setSize (2, chunk); wet.setSize (2, chunk);
    for (auto& d : dryDelay) d.assign ((size_t) juce::jmax (1, osLatency + 1), 0.0f);
    dryPos = 0; dc[0] = dc[1] = 0.0;
    setLatencySamples (pyre::kN + osLatency);
}

void PyreProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());
    const int numCh = juce::jmin (2, buffer.getNumChannels()), total = buffer.getNumSamples();
    if (numCh == 0 || total == 0 || chunk == 0 || os == nullptr) return;
    const auto s = settings();

    float ip = 0.0f;
    for (int start = 0; start < total; start += chunk)
    {
        const int n = juce::jmin (chunk, total - start);
        // the input on two channels (a mono track: the same on both)
        for (int c = 0; c < 2; ++c)
        {
            const float* src = buffer.getReadPointer (juce::jmin (c, numCh - 1), start);
            juce::FloatVectorOperations::copy (wet.getWritePointer (c), src, n);
            ip = juce::jmax (ip, juce::FloatVectorOperations::findMaximum (src, n), -juce::FloatVectorOperations::findMinimum (src, n));
        }
        // the untouched input, delayed like the oversampler (the engine adds its own kN to both)
        for (int c = 0; c < 2; ++c)
        {
            auto& d = dryDelay[c];
            float* out = dry.getWritePointer (c);
            const float* in = wet.getReadPointer (c);
            int p = dryPos;
            for (int i = 0; i < n; ++i) { out[i] = d[(size_t) p]; d[(size_t) p] = in[i]; p = (p + 1) % (int) d.size(); }
        }
        dryPos = (dryPos + n) % (int) dryDelay[0].size();

        // IGNITE, 4x oversampled
        {
            juce::dsp::AudioBlock<float> block (wet.getArrayOfWritePointers(), 2, (size_t) n);
            auto up = os->processSamplesUp (block);
            if (s.ignite > 1e-4)
                for (size_t c = 0; c < up.getNumChannels(); ++c)
                {
                    float* x = up.getChannelPointer (c);
                    for (size_t i = 0; i < up.getNumSamples(); ++i) x[i] = (float) pyre::Engine::ignite (x[i], s.ignite, dc[c]);
                }
            os->processSamplesDown (block);
        }

        float* io[2] = { wet.getWritePointer (0), wet.getWritePointer (1) };
        const float* dr[2] = { dry.getReadPointer (0), dry.getReadPointer (1) };
        dsp.process (io, dr, 2, n, s);

        for (int c = 0; c < numCh; ++c) juce::FloatVectorOperations::copy (buffer.getWritePointer (c, start), wet.getReadPointer (c), n);
    }
    float op = 0.0f;
    for (int c = 0; c < numCh; ++c) op = juce::jmax (op, buffer.getMagnitude (c, 0, total));
    inPeak.store (juce::jmax (inPeak.load(), ip)); outPeak.store (juce::jmax (outPeak.load(), op));
}

juce::AudioProcessorEditor* PyreProcessor::createEditor() { return new PyreEditor (*this); }

void PyreProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PyreProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PyreProcessor(); }
