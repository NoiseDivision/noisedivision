#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }
    juce::String db1 (float v, int) { return juce::String (v, 1) + " dB"; }
    juce::String ms (float v, int)  { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
}

juce::AudioProcessorValueTreeState::ParameterLayout SpectraKillProcessor::createLayout()
{
    using Float = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;
    using Attr = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto skewed = [] (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi, 0.01f);
        r.setSkewForCentre (centre);
        return r;
    };

    layout.add (std::make_unique<Float> (juce::ParameterID { "threshold", 1 }, "Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f, Attr().withLabel ("dB").withStringFromValueFunction (db1)));
    layout.add (std::make_unique<Float> (juce::ParameterID { "down", 1 }, "Down",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    layout.add (std::make_unique<Float> (juce::ParameterID { "up", 1 }, "Up",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    layout.add (std::make_unique<Float> (juce::ParameterID { "tilt", 1 }, "Tilt",
        juce::NormalisableRange<float> (-6.0f, 6.0f, 0.01f), 0.0f,
        Attr().withLabel ("dB/oct").withStringFromValueFunction ([] (float v, int) { return (v > 0.005f ? "+" : "") + juce::String (v, 1) + " dB/oct"; })));
    layout.add (std::make_unique<Float> (juce::ParameterID { "sharpness", 1 }, "Sharpness",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    layout.add (std::make_unique<Float> (juce::ParameterID { "attack", 1 }, "Attack",
        skewed (0.5f, 200.0f, 15.0f), 10.0f, Attr().withLabel ("ms").withStringFromValueFunction (ms)));
    layout.add (std::make_unique<Float> (juce::ParameterID { "release", 1 }, "Release",
        skewed (10.0f, 1000.0f, 150.0f), 120.0f, Attr().withLabel ("ms").withStringFromValueFunction (ms)));
    layout.add (std::make_unique<Float> (juce::ParameterID { "gain", 1 }, "Output Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; })));
    layout.add (std::make_unique<Float> (juce::ParameterID { "mix", 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));

    layout.add (std::make_unique<Choice> (juce::ParameterID { "quality", 1 }, "Quality",
        juce::StringArray { "Low Latency", "Normal", "High", "Very High", "Ultra" }, 2));
    layout.add (std::make_unique<Choice> (juce::ParameterID { "stereo", 1 }, "Stereo Mode",
        juce::StringArray { "Stereo", "Left/Right", "Mid/Side" }, 0));
    layout.add (std::make_unique<Choice> (juce::ParameterID { "clip", 1 }, "Clip",
        juce::StringArray { "Off", "Soft", "Hard" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sidechain", 1 }, "Sidechain", false));
    return layout;
}

SpectraKillProcessor::SpectraKillProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "SPECTRAKILL", createLayout())
{
    apvts.state.addListener (this);
    curveChanged();
}

SpectraKillProcessor::~SpectraKillProcessor() { apvts.state.removeListener (this); }

bool SpectraKillProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (out != l.getMainInputChannelSet()) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

// ---- curve points: stored as <CURVE><POINT freq="" db=""/>...</CURVE> in the state
std::vector<sk::CurvePoint> SpectraKillProcessor::getCurve() const
{
    std::vector<sk::CurvePoint> pts;
    const auto curve = apvts.state.getChildWithName ("CURVE");
    for (const auto& p : curve)
        if (pts.size() < (size_t) sk::kMaxCurvePoints)
            pts.push_back ({ (float) p.getProperty ("freq", 1000.0), (float) p.getProperty ("db", 0.0) });
    std::sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.freq < b.freq; });
    return pts;
}

void SpectraKillProcessor::setCurve (const std::vector<sk::CurvePoint>& pts)
{
    auto curve = apvts.state.getOrCreateChildWithName ("CURVE", nullptr);
    curve.removeAllChildren (nullptr);
    for (size_t i = 0; i < pts.size() && i < (size_t) sk::kMaxCurvePoints; ++i)
    {
        juce::ValueTree p ("POINT");
        p.setProperty ("freq", juce::jlimit (20.0f, 20000.0f, pts[i].freq), nullptr);
        p.setProperty ("db", juce::jlimit (-36.0f, 36.0f, pts[i].db), nullptr);
        curve.appendChild (p, nullptr);
    }
}

void SpectraKillProcessor::curveChanged()
{
    const auto pts = getCurve();
    for (size_t i = 0; i < pts.size(); ++i)
    {
        curveAtomic[i * 2].store (pts[i].freq);
        curveAtomic[i * 2 + 1].store (pts[i].db);
    }
    curveCount.store ((int) pts.size());
}

sk::Settings SpectraKillProcessor::currentSettings() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    sk::Settings s;
    s.thresholdDb = v ("threshold");
    s.down = v ("down") * 0.01f;
    s.up = v ("up") * 0.01f;
    s.tiltDbPerOct = v ("tilt");
    s.sharpness = v ("sharpness") * 0.01f;
    s.attackMs = v ("attack");
    s.releaseMs = v ("release");
    s.gainDb = v ("gain");
    s.mix = v ("mix") * 0.01f;
    s.quality = juce::roundToInt (v ("quality"));
    s.stereo = juce::roundToInt (v ("stereo"));
    s.clip = juce::roundToInt (v ("clip"));
    s.sidechain = v ("sidechain") > 0.5f;
    s.numPoints = juce::jlimit (0, sk::kMaxCurvePoints, curveCount.load());
    for (int i = 0; i < s.numPoints; ++i)
        s.points[(size_t) i] = { curveAtomic[(size_t) i * 2].load(), curveAtomic[(size_t) i * 2 + 1].load() };
    return s;
}

void SpectraKillProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate.store (sampleRate);
    engine.prepare (sampleRate, samplesPerBlock);
    engine.setSettings (currentSettings());
    setLatencySamples (engine.getLatency());
    latencySamples.store (engine.getLatency());
    monoScratch.setSize (1, juce::jmax (1, samplesPerBlock));
}

void SpectraKillProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples();
    const int ch = main.getNumChannels();
    if (ch == 0 || n == 0) return;

    // sidechain (only when the host has connected something to it)
    const float* scL = nullptr; const float* scR = nullptr;
    if (getBusCount (true) > 1)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (getBus (true, 1)->isEnabled() && sc.getNumChannels() > 0)
        {
            scL = sc.getReadPointer (0);
            scR = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : scL;
        }
    }
    sidechainConnected.store (scL != nullptr);

    engine.setSettings (currentSettings());
    if (engine.getLatency() != getLatencySamples())        // quality changed
    {
        setLatencySamples (engine.getLatency());
        latencySamples.store (engine.getLatency());
    }

    float* l = main.getWritePointer (0);
    float* r = ch > 1 ? main.getWritePointer (1) : nullptr;

    float pin = 0.0f;
    for (int c = 0; c < ch; ++c) pin = juce::jmax (pin, main.getMagnitude (c, 0, n));

    engine.process (l, r, scL, scR, n);

    float pout = 0.0f;
    for (int c = 0; c < ch; ++c) pout = juce::jmax (pout, main.getMagnitude (c, 0, n));
    if (pin > inPeak.load()) inPeak.store (pin);
    if (pout > outPeak.load()) outPeak.store (pout);

    for (int c = ch; c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
}

juce::AudioProcessorEditor* SpectraKillProcessor::createEditor() { return new SpectraKillEditor (*this); }

void SpectraKillProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SpectraKillProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    curveChanged();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpectraKillProcessor(); }
