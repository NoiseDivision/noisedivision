#include "PluginProcessor.h"
#include "PluginEditor.h"

PeakSlayerProcessor::PeakSlayerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto& s : scopeIn)  s.store (0.0f);
    for (auto& s : scopeOut) s.store (0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout PeakSlayerProcessor::createLayout()
{
    using Attr = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // CLIP: 0..1 internally, shown as 0..11. Starts at 5; 6.8 is the measured reference setting.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "clip", 1 }, "Clip",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), kClipDefault,
        Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v * 11.0f, 1); })
              .withValueFromStringFunction ([] (const juce::String& t)
                  { return juce::jlimit (0.0f, 1.0f, t.getFloatValue() / 11.0f); })));

    // SHAPE: -1 soft .. 0 matched .. +1 hard
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "shape", 1 }, "Shape",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
        Attr().withStringFromValueFunction ([] (float v, int)
                  {
                      const int pct = juce::roundToInt (std::abs (v) * 100.0f);
                      if (pct == 0) return juce::String ("MATCHED");
                      return juce::String (v < 0.0f ? "SOFT " : "HARD ") + juce::String (pct);
                  })
              .withValueFromStringFunction ([] (const juce::String& t)
                  {
                      const float mag = t.retainCharacters ("0123456789.").getFloatValue() / 100.0f;
                      const bool soft = t.containsIgnoreCase ("soft") || t.contains ("-");
                      return juce::jlimit (-1.0f, 1.0f, soft ? -mag : mag);
                  })));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "hq", 1 }, "HQ", true));

    // SAFE: hard limit at 0 dBFS on the final output
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "safe", 1 }, "Safe", false));

    auto dbText = [] (float v, int) { return juce::String (v, 1); };

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "in", 1 }, "Input",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "out", 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction (dbText)));

    // Clip band edges. HPF is off at 20 Hz, LPF is off at 20 kHz (= clip everything).
    juce::NormalisableRange<float> freqRange (kFilterMin, kFilterMax, 1.0f);
    freqRange.setSkewForCentre (1000.0f);
    auto hzText = [] (float v, int) { return juce::String (juce::roundToInt (v)); };

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hpf", 1 }, "High Pass", freqRange, kFilterMin,
        Attr().withLabel ("Hz").withStringFromValueFunction (hzText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lpf", 1 }, "Low Pass", freqRange, kFilterMax,
        Attr().withLabel ("Hz").withStringFromValueFunction (hzText)));

    // Dry/wet mix in percent (100 = fully clipped signal)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        Attr().withLabel ("%").withStringFromValueFunction (hzText)));

    return layout;
}

bool PeakSlayerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void PeakSlayerProcessor::setProcessingRate (double rate) noexcept
{
    const float drive = juce::Decibels::decibelsToGain (driveDbFromKnob (apvts.getRawParameterValue ("clip")->load())
                                                        + apvts.getRawParameterValue ("in")->load());
    driveSmoothed.reset (rate, 0.02);
    driveSmoothed.setCurrentAndTargetValue (drive);
    kneeSmoothed.reset (rate, 0.02);
    kneeSmoothed.setCurrentAndTargetValue (kneeFromShape (apvts.getRawParameterValue ("shape")->load()));
    mixSmoothed.reset (rate, 0.02);
    mixSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("mix")->load() * 0.01f);
}

void PeakSlayerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    baseSampleRate = sampleRate;

    oversampling.initProcessing ((size_t) samplesPerBlock);
    oversampling.reset();

    const auto factor = (double) oversampling.getOversamplingFactor();
    lastHq = apvts.getRawParameterValue ("hq")->load() > 0.5f;
    setLatencySamples (lastHq ? (int) std::round (oversampling.getLatencyInSamples()) : 0);
    setProcessingRate (lastHq ? sampleRate * factor : sampleRate);

    const auto channels = (juce::uint32) juce::jmax (1, getTotalNumOutputChannels());
    for (size_t r = 0; r < 2; ++r)
    {
        const double rate = (r == 0) ? sampleRate : sampleRate * factor;
        const juce::dsp::ProcessSpec spec { rate, (juce::uint32) (samplesPerBlock * (r == 0 ? 1 : (int) factor)), channels };
        hpFilter[r].prepare (spec);
        lpFilter[r].prepare (spec);
        hpFilter[r].setType (juce::dsp::StateVariableTPTFilterType::highpass);
        lpFilter[r].setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    }
    hpActive = lpActive = false;

    meter.prepare (sampleRate);

    outSmoothed.reset (sampleRate, 0.02);
    outSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue ("out")->load()));
}

// Linear below the knee, a quadratic soft knee centred on the ceiling, then flat.
// knee = knee width relative to the ceiling (0 = hard clip, 0.413 = measured curve).
float PeakSlayerProcessor::clip (float x, float ceiling, float knee) noexcept
{
    const float u  = std::abs (x);
    const float w  = knee * ceiling;
    const float lo = ceiling - 0.5f * w;

    float y;
    if (u <= lo)                                     y = u;
    else if (w < 1.0e-6f || u >= ceiling + 0.5f * w) y = ceiling;
    else { const float d = u - lo;                   y = u - (d * d) / (2.0f * w); }

    return std::copysign (y, x);
}

void PeakSlayerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numCh == 0 || numSamples == 0)
        return;

    const float knob  = apvts.getRawParameterValue ("clip")->load();
    const float shape = apvts.getRawParameterValue ("shape")->load();
    const float inDb  = apvts.getRawParameterValue ("in")->load();
    const float outDb = apvts.getRawParameterValue ("out")->load();
    const float hpHz  = apvts.getRawParameterValue ("hpf")->load();
    const float lpHz  = apvts.getRawParameterValue ("lpf")->load();
    const float mix   = apvts.getRawParameterValue ("mix")->load() * 0.01f;
    const bool  hq    = apvts.getRawParameterValue ("hq")->load() > 0.5f;
    const bool  safe  = apvts.getRawParameterValue ("safe")->load() > 0.5f;

    const int factor = hq ? (int) oversampling.getOversamplingFactor() : 1;
    const double rate = baseSampleRate * (double) factor;
    const size_t r = hq ? 1 : 0; // which filter set

    if (hq != lastHq)
    {
        lastHq = hq;
        oversampling.reset();
        hpActive = lpActive = false; // forces a filter reset below
        setProcessingRate (rate);
        setLatencySamples (hq ? (int) std::round (oversampling.getLatencyInSamples()) : 0);
    }

    driveSmoothed.setTargetValue (juce::Decibels::decibelsToGain (driveDbFromKnob (knob) + inDb));
    kneeSmoothed.setTargetValue (kneeFromShape (shape));
    mixSmoothed.setTargetValue (mix);
    outSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outDb));

    // Input meter: peak of the signal as it arrives, before any gain
    {
        float pk = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            pk = juce::jmax (pk, buffer.getMagnitude (ch, 0, numSamples));
        if (pk > inputPeak.load (std::memory_order_relaxed))
            inputPeak.store (pk, std::memory_order_relaxed);
    }

    // Clip band: which filters are in use this block
    const float maxHz = (float) (rate * 0.45);
    const bool hpOn = hpHz > kFilterMin + 0.5f;
    const bool lpOn = lpHz < kFilterMax - 0.5f;

    if (hpOn) { if (! hpActive) hpFilter[r].reset(); hpFilter[r].setCutoffFrequency (juce::jmin (hpHz, maxHz)); }
    if (lpOn) { if (! lpActive) lpFilter[r].reset(); lpFilter[r].setCutoffFrequency (juce::jmin (lpHz, maxHz)); }
    hpActive = hpOn; lpActive = lpOn;

    // Everything below happens at the clipper's rate (4x when HQ is on), so the clipped
    // band, the unclipped rest and the dry signal all stay perfectly lined up.
    juce::dsp::AudioBlock<float> fullBlock (buffer);
    auto block = hq ? oversampling.processSamplesUp (fullBlock) : fullBlock;
    const int n = (int) block.getNumSamples();

    const int startPos = scopeWrite.load (std::memory_order_relaxed);
    const int mask = scopeSize - 1;
    const float chScale = 1.0f / (float) numCh;

    for (int i = 0; i < n; ++i)
    {
        const float d = driveSmoothed.getNextValue();
        const float k = kneeSmoothed.getNextValue();
        const float m = mixSmoothed.getNextValue();
        float sumIn = 0.0f, sumOut = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* p = block.getChannelPointer ((size_t) ch);
            const float dry  = p[i];
            const float full = dry * d;             // drive on the whole signal keeps the tonal balance

            float band = full;                      // the part that gets clipped
            if (hpOn) band = hpFilter[r].processSample (ch, band);
            if (lpOn) band = lpFilter[r].processSample (ch, band);
            const float rest = full - band;         // the part that passes unclipped

            const float wet = clip (band, kCeilingLin, k) + rest;

            sumIn  += full;
            sumOut += wet;
            p[i] = dry + m * (wet - dry);           // dry/wet mix
        }

        // Scope: driven signal vs clipped result, one point per output sample
        if (i % factor == 0)
        {
            const auto pos = (size_t) ((startPos + i / factor) & mask);
            scopeIn [pos].store (sumIn  * chScale, std::memory_order_relaxed);
            scopeOut[pos].store (sumOut * chScale, std::memory_order_relaxed);
        }
    }

    if (hq)
    {
        oversampling.processSamplesDown (fullBlock);

        // The downsampling filter can ring slightly past the ceiling. When the whole
        // signal went through the clipper, pin it back so the ceiling is exact.
        if (! hpOn && ! lpOn && mix >= 0.999f && ! mixSmoothed.isSmoothing())
            for (int ch = 0; ch < numCh; ++ch)
                juce::FloatVectorOperations::clip (buffer.getWritePointer (ch), buffer.getReadPointer (ch),
                                                   -kCeilingLin, kCeilingLin, numSamples);
    }

    // Output gain
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outSmoothed.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    // SAFE: nothing leaves the plugin above 0 dBFS
    if (safe)
        for (int ch = 0; ch < numCh; ++ch)
            juce::FloatVectorOperations::clip (buffer.getWritePointer (ch), buffer.getReadPointer (ch),
                                               -1.0f, 1.0f, numSamples);

    meter.process (buffer.getArrayOfReadPointers(), numCh, numSamples);

    scopeWrite.store ((startPos + numSamples) & mask, std::memory_order_release);
}

juce::AudioProcessorEditor* PeakSlayerProcessor::createEditor()
{
    return new PeakSlayerEditor (*this);
}

void PeakSlayerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void PeakSlayerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakSlayerProcessor();
}
