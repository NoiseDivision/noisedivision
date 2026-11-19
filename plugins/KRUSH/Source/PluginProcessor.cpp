#include "PluginProcessor.h"
#include "PluginEditor.h"

KrushProcessor::KrushProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto* a : { &scopeInMin, &scopeInMax, &scopeOutMin, &scopeOutMax })
        for (auto& v : *a) v.store (0.0f);
    for (auto& v : specRing) v.store (0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout KrushProcessor::createLayout()
{
    using Float = juce::AudioParameterFloat;
    using Attr = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto db  = [] (float v, int) { return juce::String (v, 1); };
    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)); };
    auto hz  = [] (float v, int) { return v < 1000.0f ? juce::String (juce::roundToInt (v)) : juce::String (v / 1000.0f, 2) + "k"; };

    juce::NormalisableRange<float> freq (20.0f, 20000.0f, 1.0f);
    freq.setSkewForCentre (630.0f);

    // ---- global
    layout.add (std::make_unique<Float> (juce::ParameterID { "input", 1 }, "Input",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (db)));
    layout.add (std::make_unique<Float> (juce::ParameterID { "output", 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (db)));
    layout.add (std::make_unique<Float> (juce::ParameterID { "mix", 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "bands", 1 }, "Bands",
        juce::StringArray { "1", "2", "3", "4" }, 2));
    const float xoverDefaults[] = { 120.0f, 2000.0f, 6000.0f };   // 3 bands: 20-120 Hz, 120 Hz-2 kHz, 2-20 kHz
    for (int k = 0; k < 3; ++k)
        layout.add (std::make_unique<Float> (juce::ParameterID { "x" + juce::String (k + 1), 1 }, "Crossover " + juce::String (k + 1),
            freq, xoverDefaults[k], Attr().withLabel ("Hz").withStringFromValueFunction (hz)));
    layout.add (std::make_unique<Float> (juce::ParameterID { "hpf", 1 }, "Tone High Pass", freq, 20.0f,
        Attr().withLabel ("Hz").withStringFromValueFunction (hz)));
    layout.add (std::make_unique<Float> (juce::ParameterID { "lpf", 1 }, "Tone Low Pass", freq, 20000.0f,
        Attr().withLabel ("Hz").withStringFromValueFunction (hz)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "toneSlope", 1 }, "Tone Slope",
        juce::StringArray { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "48 dB/oct" }, 1));

    // ---- output safety stage (sample peak, not true peak)
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "clipOn", 1 }, "Output Clip", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "limitOn", 1 }, "Output Limit", false));

    // ---- per band
    juce::StringArray modes;
    for (int m = 0; m < krush::kNumModes; ++m) modes.add (krush::modeName (m));
    const int defaultMode[] = { krush::None, krush::None, krush::None, krush::None };

    for (int b = 0; b < krush::kMaxBands; ++b)
    {
        const juce::String n = "Band " + juce::String (b + 1) + " ";
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { bandId (b, "mode"), 1 }, n + "Mode", modes, defaultMode[b]));
        layout.add (std::make_unique<Float> (juce::ParameterID { bandId (b, "drive"), 1 }, n + "Drive",
            juce::NormalisableRange<float> (0.0f, 36.0f, 0.1f), 12.0f, Attr().withLabel ("dB").withStringFromValueFunction (db)));
        layout.add (std::make_unique<Float> (juce::ParameterID { bandId (b, "shape"), 1 }, n + "Shape",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
        layout.add (std::make_unique<Float> (juce::ParameterID { bandId (b, "bias"), 1 }, n + "Bias",
            juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
        layout.add (std::make_unique<Float> (juce::ParameterID { bandId (b, "dyn"), 1 }, n + "Dynamics",
            juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
        layout.add (std::make_unique<Float> (juce::ParameterID { bandId (b, "gain"), 1 }, n + "Gain",
            juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (db)));
        layout.add (std::make_unique<Float> (juce::ParameterID { bandId (b, "mix"), 1 }, n + "Mix",
            juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bandId (b, "solo"), 1 }, n + "Solo", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bandId (b, "bypass"), 1 }, n + "Bypass", false));
    }
    return layout;
}

bool KrushProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

krush::Settings KrushProcessor::readSettings() const
{
    auto v = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    krush::Settings s;
    s.numBands = (int) v ("bands") + 1;
    for (int k = 0; k < 3; ++k) s.crossover[k] = v ("x" + juce::String (k + 1));
    for (int b = 0; b < krush::kMaxBands; ++b)
    {
        auto& B = s.band[b];
        B.mode = (int) v (bandId (b, "mode"));
        B.driveDb = v (bandId (b, "drive"));
        B.shape = v (bandId (b, "shape")) * 0.01f;
        B.bias = v (bandId (b, "bias")) * 0.005f;          // +-100 % = +-0.5 at the curve's input
        B.gainDb = v (bandId (b, "gain"));
        B.dynamics = v (bandId (b, "dyn")) * 0.01f;
        B.mix = v (bandId (b, "mix")) * 0.01f;
        B.solo = v (bandId (b, "solo")) > 0.5f;
        B.bypass = v (bandId (b, "bypass")) > 0.5f;
    }
    s.hpfHz = v ("hpf");
    s.lpfHz = v ("lpf");
    static const int orders[] = { 1, 2, 3, 4, 8 };
    s.toneOrder = orders[juce::jlimit (0, 4, (int) v ("toneSlope"))];
    s.mix = v ("mix") * 0.01f;
    return s;
}

void KrushProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate.store (sampleRate);
    chunkSize = juce::jmax (32, samplesPerBlock);

    oversampling.initProcessing ((size_t) chunkSize);
    oversampling.reset();
    outStage.prepare (sampleRate);
    outStage.snap (apvts.getRawParameterValue ("clipOn")->load() > 0.5f, apvts.getRawParameterValue ("limitOn")->load() > 0.5f);
    setLatencySamples ((int) std::round (oversampling.getLatencyInSamples()) + outStage.latency());

    const int factor = (int) oversampling.getOversamplingFactor();
    engine.prepare (sampleRate * factor, factor);
    engine.setSettings (readSettings());

    monoScratch.setSize (1, chunkSize);
    scopeScratch.setSize (1, chunkSize);
    inputGain.reset (sampleRate, 0.02);
    outputGain.reset (sampleRate, 0.02);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("input")->load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));

    rmsCoef = 1.0 - std::exp (-1.0 / (sampleRate * 0.3));
    for (auto& m : inMs) m = 0.0;
    for (auto& m : outMs) m = 0.0;
    bucketLen = juce::jmax (8, (int) std::round (sampleRate / 750.0));
    bucketFill = 0;
}

// peak and 300 ms RMS per channel
void KrushProcessor::meter (const float* l, const float* r, int n, Meter* m, double& msL, double& msR)
{
    const float* ch[2] = { l, r };
    double* ms[2] = { &msL, &msR };
    for (int c = 0; c < 2; ++c)
    {
        float pk = 0.0f;
        double acc = *ms[c];
        for (int i = 0; i < n; ++i)
        {
            const float x = ch[c][i];
            pk = juce::jmax (pk, std::abs (x));
            acc += rmsCoef * ((double) x * x - acc);
        }
        *ms[c] = acc;
        if (pk > m[c].peak.load (std::memory_order_relaxed)) m[c].peak.store (pk, std::memory_order_relaxed);
        m[c].rmsDb.store (acc > 1.0e-10 ? (float) (10.0 * std::log10 (acc)) : -100.0f, std::memory_order_relaxed);
    }
}

void KrushProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int total = buffer.getNumSamples();
    if (numCh == 0 || total == 0 || chunkSize == 0)
        return;

    engine.setSettings (readSettings());
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("input")->load()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));
    const bool clipOn = apvts.getRawParameterValue ("clipOn")->load() > 0.5f;
    const bool limitOn = apvts.getRawParameterValue ("limitOn")->load() > 0.5f;
    float reduction = 0.0f;

    // hosts may send more samples than they announced, so work in chunks
    for (int start = 0; start < total; start += chunkSize)
    {
        const int n = juce::jmin (chunkSize, total - start);
        float* left = buffer.getWritePointer (0, start);
        float* right = numCh > 1 ? buffer.getWritePointer (1, start) : monoScratch.getWritePointer (0);
        if (numCh == 1)
            juce::FloatVectorOperations::copy (right, left, n);

        // ---- input gain and input meter
        for (int i = 0; i < n; ++i) { const float g = inputGain.getNextValue(); left[i] *= g; right[i] *= g; }
        meter (left, right, n, inMeter, inMs[0], inMs[1]);

        // keep the input (mono) for the waveform view
        float* scopeIn = scopeScratch.getWritePointer (0);
        for (int i = 0; i < n; ++i) scopeIn[i] = 0.5f * (left[i] + right[i]);

        // ---- the distortion, at 4x the sample rate
        float* chans[2] = { left, right };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        auto os = oversampling.processSamplesUp (block);
        engine.process (os.getChannelPointer (0), os.getChannelPointer (1), (int) os.getNumSamples());
        oversampling.processSamplesDown (block);

        // ---- output gain, output meter, display feeds
        for (int i = 0; i < n; ++i) { const float g = outputGain.getNextValue(); left[i] *= g; right[i] *= g; }
        outStage.process (left, right, n, clipOn, limitOn);       // always runs, so the latency is constant
        reduction = juce::jmax (reduction, outStage.getReductionDb());
        meter (left, right, n, outMeter, outMs[0], outMs[1]);

        // waveform view: min/max of input and output per bucket
        {
            int p = scopeWrite.load (std::memory_order_relaxed);
            int f = bucketFill;
            for (int i = 0; i < n; ++i)
            {
                const float in = scopeIn[i], o = 0.5f * (left[i] + right[i]);
                if (f == 0) { bInMin = bInMax = in; bOutMin = bOutMax = o; }
                else
                {
                    bInMin = juce::jmin (bInMin, in);  bInMax = juce::jmax (bInMax, in);
                    bOutMin = juce::jmin (bOutMin, o); bOutMax = juce::jmax (bOutMax, o);
                }
                if (++f == bucketLen)
                {
                    f = 0;
                    p = (p + 1) & (kScopeSize - 1);
                    scopeInMin[(size_t) p].store (bInMin, std::memory_order_relaxed);
                    scopeInMax[(size_t) p].store (bInMax, std::memory_order_relaxed);
                    scopeOutMin[(size_t) p].store (bOutMin, std::memory_order_relaxed);
                    scopeOutMax[(size_t) p].store (bOutMax, std::memory_order_relaxed);
                }
            }
            bucketFill = f;
            scopeWrite.store (p, std::memory_order_release);
        }

        // spectrum view: the output, mono
        {
            int w = specWrite.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
            {
                specRing[(size_t) w].store (0.5f * (left[i] + right[i]), std::memory_order_relaxed);
                w = (w + 1) & (kSpecSize - 1);
            }
            specWrite.store (w, std::memory_order_release);
        }
    }

    // highest gain reduction since the editor last read it
    float prev = limitReductionDb.load (std::memory_order_relaxed);
    while (reduction > prev && ! limitReductionDb.compare_exchange_weak (prev, reduction)) {}
}

juce::AudioProcessorEditor* KrushProcessor::createEditor()
{
    return new KrushEditor (*this);
}

void KrushProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void KrushProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KrushProcessor();
}
