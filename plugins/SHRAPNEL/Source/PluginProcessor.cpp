#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }
}

juce::AudioProcessorValueTreeState::ParameterLayout ShrapnelProcessor::createLayout()
{
    using Float = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;
    using Attr = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto percent = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<Float> (juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), def, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    };

    layout.add (std::make_unique<Choice> (juce::ParameterID { "mode", 1 }, "Mode",
        juce::StringArray { "Harmonics", "Clip", "Crush", "Smear", "Scramble", "Warp" }, 0));
    percent ("drive", "Drive", 50.0f);
    percent ("character", "Character", 50.0f);
    layout.add (std::make_unique<Float> (juce::ParameterID { "tone", 1 }, "Tone",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return std::abs (v) < 0.5f ? juce::String ("0") : (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)); })));
    percent ("tonal", "Split Tonal", 90.0f);
    percent ("noise", "Split Noise", 50.0f);
    percent ("transient", "Split Transient", 20.0f);
    percent ("dynamic", "Dynamic", 50.0f);
    layout.add (std::make_unique<Float> (juce::ParameterID { "gain", 1 }, "Output Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " dB"; })));
    percent ("mix", "Mix", 100.0f);
    layout.add (std::make_unique<Choice> (juce::ParameterID { "quality", 1 }, "Quality",
        juce::StringArray { "Low Latency", "Normal", "High" }, 1));
    layout.add (std::make_unique<Choice> (juce::ParameterID { "stereo", 1 }, "Stereo Mode",
        juce::StringArray { "Stereo", "Left/Right", "Mid/Side" }, 0));
    layout.add (std::make_unique<Choice> (juce::ParameterID { "clip", 1 }, "Clip",
        juce::StringArray { "Off", "Soft", "Hard" }, 1));
    return layout;
}

ShrapnelProcessor::ShrapnelProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SHRAPNEL", createLayout())
{
    apvts.state.addListener (this);
    curveChanged();
}

ShrapnelProcessor::~ShrapnelProcessor() { apvts.state.removeListener (this); }

bool ShrapnelProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return out == l.getMainInputChannelSet();
}

// ---- curve points: stored as <CURVE><POINT freq="" amount=""/>...</CURVE> in the state
std::vector<sh::CurvePoint> ShrapnelProcessor::getCurve() const
{
    std::vector<sh::CurvePoint> pts;
    const auto curve = apvts.state.getChildWithName ("CURVE");
    for (const auto& p : curve)
        if (pts.size() < (size_t) sh::kMaxCurvePoints)
            pts.push_back ({ (float) p.getProperty ("freq", 1000.0), (float) p.getProperty ("amount", 0.0) });
    std::sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.freq < b.freq; });
    return pts;
}

void ShrapnelProcessor::setCurve (const std::vector<sh::CurvePoint>& pts)
{
    auto curve = apvts.state.getOrCreateChildWithName ("CURVE", nullptr);
    curve.removeAllChildren (nullptr);
    for (size_t i = 0; i < pts.size() && i < (size_t) sh::kMaxCurvePoints; ++i)
    {
        juce::ValueTree p ("POINT");
        p.setProperty ("freq", juce::jlimit (20.0f, 20000.0f, pts[i].freq), nullptr);
        p.setProperty ("amount", juce::jlimit (-1.0f, 1.0f, pts[i].amount), nullptr);
        curve.appendChild (p, nullptr);
    }
}

void ShrapnelProcessor::curveChanged()
{
    const auto pts = getCurve();
    for (size_t i = 0; i < pts.size(); ++i)
    {
        curveAtomic[i * 2].store (pts[i].freq);
        curveAtomic[i * 2 + 1].store (pts[i].amount);
    }
    curveCount.store ((int) pts.size());
}

sh::Settings ShrapnelProcessor::currentSettings() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    sh::Settings s;
    s.mode = juce::roundToInt (v ("mode"));
    s.drive = v ("drive") * 0.01f;
    s.character = v ("character") * 0.01f;
    s.tone = v ("tone") * 0.01f;
    s.tonal = v ("tonal") * 0.01f;
    s.noise = v ("noise") * 0.01f;
    s.transient = v ("transient") * 0.01f;
    s.dynamic = v ("dynamic") * 0.01f;
    s.gainDb = v ("gain");
    s.mix = v ("mix") * 0.01f;
    s.quality = juce::roundToInt (v ("quality"));
    s.stereo = juce::roundToInt (v ("stereo"));
    s.clip = juce::roundToInt (v ("clip"));
    s.numPoints = juce::jlimit (0, sh::kMaxCurvePoints, curveCount.load());
    for (int i = 0; i < s.numPoints; ++i)
        s.points[(size_t) i] = { curveAtomic[(size_t) i * 2].load(), curveAtomic[(size_t) i * 2 + 1].load() };
    return s;
}

void ShrapnelProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate.store (sampleRate);
    engine.prepare (sampleRate);
    engine.setSettings (currentSettings());
    setLatencySamples (engine.getLatency());
    latencySamples.store (engine.getLatency());
}

void ShrapnelProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples();
    const int ch = main.getNumChannels();
    if (ch == 0 || n == 0) return;

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

    engine.process (l, r, n);

    float pout = 0.0f;
    for (int c = 0; c < ch; ++c) pout = juce::jmax (pout, main.getMagnitude (c, 0, n));
    if (pin > inPeak.load()) inPeak.store (pin);
    if (pout > outPeak.load()) outPeak.store (pout);

    for (int c = ch; c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
}

juce::AudioProcessorEditor* ShrapnelProcessor::createEditor() { return new ShrapnelEditor (*this); }

void ShrapnelProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void ShrapnelProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    curveChanged();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ShrapnelProcessor(); }
