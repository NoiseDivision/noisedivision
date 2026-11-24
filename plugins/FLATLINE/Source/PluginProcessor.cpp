#include "PluginProcessor.h"
#include "PluginEditor.h"

FlatlineProcessor::FlatlineProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout FlatlineProcessor::createLayout()
{
    using F = juce::AudioParameterFloat; using B = juce::AudioParameterBool; using C = juce::AudioParameterChoice;
    using Attr = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return juce::ParameterID { s, 1 }; };
    auto minus = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    auto db = [minus] (bool plus, int dec) { return Attr().withLabel ("dB").withStringFromValueFunction ([=] (float v, int)
        { const float a = std::abs (v) < 0.05f ? 0.0f : v; return (plus && a > 0 ? "+" : a < 0 ? minus : juce::String()) + juce::String (std::abs (a), dec) + " dB"; }); };
    auto pct = Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; });

    l.add (std::make_unique<F> (id ("gain"), "Gain", juce::NormalisableRange<float> (0.0f, 20.0f, 0.1f), 0.0f, db (true, 1)));
    l.add (std::make_unique<F> (id ("ceiling"), "Ceiling", juce::NormalisableRange<float> (-3.0f, 0.0f, 0.1f), -1.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([minus] (float v, int) { return (v < -0.05f ? minus : juce::String()) + juce::String (std::abs (v), 1) + " dB"; })));
    l.add (std::make_unique<C> (id ("style"), "Style", juce::StringArray { "Transparent", "Punch", "Dense", "Safe" }, 0));
    l.add (std::make_unique<B> (id ("autoRelease"), "Auto Release", true));
    juce::NormalisableRange<float> rel (1.0f, 10000.0f, 0.1f); rel.setSkewForCentre (150.0f);
    l.add (std::make_unique<F> (id ("release"), "Release", rel, 100.0f,
        Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
        { return v >= 1000.0f ? juce::String (v / 1000.0f, v < 9950.0f ? 2 : 1) + " s" : juce::String (v, v < 10 ? 1 : 0) + " ms"; })));
    l.add (std::make_unique<F> (id ("link"), "Stereo Link", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    l.add (std::make_unique<F> (id ("transient"), "Transients", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pct));
    l.add (std::make_unique<B> (id ("upOn"), "Upward On", false));
    l.add (std::make_unique<F> (id ("upAmount"), "Upward Amount", juce::NormalisableRange<float> (0.0f, 12.0f, 0.1f), 3.0f, db (true, 1)));
    l.add (std::make_unique<F> (id ("upBelow"), "Upward Below", juce::NormalisableRange<float> (6.0f, 40.0f, 0.1f), 14.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([minus] (float v, int) { return minus + juce::String (juce::roundToInt (v)) + " dB"; })));
    l.add (std::make_unique<B> (id ("clipOn"), "Clipper On", false));
    l.add (std::make_unique<F> (id ("clipDrive"), "Clipper Drive", juce::NormalisableRange<float> (0.0f, 12.0f, 0.1f), 1.5f, db (true, 1)));
    l.add (std::make_unique<B> (id ("clipHard"), "Clipper Hard", false));
    l.add (std::make_unique<F> (id ("target"), "Target", juce::NormalisableRange<float> (-24.0f, -4.0f, 0.5f), -11.0f,
        Attr().withLabel ("LUFS").withStringFromValueFunction ([minus] (float v, int) { return minus + juce::String (std::abs (v), std::abs (v - std::round (v)) > 0.01f ? 1 : 0) + " LUFS"; })));
    l.add (std::make_unique<B> (id ("truePeak"), "True Peak", false));
    l.add (std::make_unique<C> (id ("lookahead"), "Lookahead", juce::StringArray { "Auto", "0.5 ms", "1 ms", "1.5 ms", "2 ms", "3 ms", "4 ms", "5 ms" }, 0));
    l.add (std::make_unique<C> (id ("dither"), "Dither", juce::StringArray { "Off", "24-bit", "16-bit" }, 0));
    l.add (std::make_unique<B> (id ("bypass"), "Bypass (level matched)", false));
    l.add (std::make_unique<B> (id ("delta"), "Delta", false));
    return l;
}

const std::vector<FlatlineProcessor::Preset>& FlatlineProcessor::presets()
{
    // GAIN is 0 in every preset: turn it up yourself (or use LEARN)
    static const std::vector<Preset> p = {
        { "Default", "Clean and transparent: turn GAIN up until the loudness feels right",
          { { "gain", 0 }, { "ceiling", -1 }, { "style", 0 }, { "autoRelease", 1 }, { "link", 100 }, { "transient", 0 }, { "upOn", 0 }, { "clipOn", 0 }, { "truePeak", 0 }, { "target", -11 } } },
        { "Streaming -14", "Aimed at Spotify and YouTube: nothing for them to turn down, true peak safe through their encoders",
          { { "gain", 0 }, { "ceiling", -1 }, { "style", 0 }, { "autoRelease", 1 }, { "link", 100 }, { "transient", 0 }, { "upOn", 0 }, { "clipOn", 0 }, { "truePeak", 1 }, { "target", -14 } } },
        { "Loud Pop -9", "Loud and polished: some upward lift, a soft clip on the very tips",
          { { "gain", 0 }, { "ceiling", -1 }, { "style", 1 }, { "autoRelease", 1 }, { "link", 100 }, { "transient", 15 }, { "upOn", 1 }, { "upAmount", 2.5f }, { "upBelow", 14 }, { "clipOn", 1 }, { "clipDrive", 1.5f }, { "clipHard", 0 }, { "truePeak", 0 }, { "target", -9 } } },
        { "Club -7", "As loud as it gets cleanly: dense limiting and soft clipping",
          { { "gain", 0 }, { "ceiling", -0.5f }, { "style", 2 }, { "autoRelease", 1 }, { "link", 100 }, { "transient", 0 }, { "upOn", 0 }, { "clipOn", 1 }, { "clipDrive", 3 }, { "clipHard", 0 }, { "truePeak", 0 }, { "target", -7 } } },
        { "Redline -4", "Everything up to the red line: the clipper does the heavy lifting, the limiter only cleans up what's left",
          { { "gain", 0 }, { "ceiling", -0.5f }, { "style", 0 }, { "autoRelease", 1 }, { "link", 100 }, { "transient", 25 }, { "upOn", 0 }, { "clipOn", 1 }, { "clipDrive", 7.5f }, { "clipHard", 0 }, { "truePeak", 0 }, { "target", -4 } } },
        { "Punch Keeper", "Emphasises the attacks and lets the front of every hit through",
          { { "gain", 0 }, { "ceiling", -1 }, { "style", 1 }, { "autoRelease", 1 }, { "link", 80 }, { "transient", 40 }, { "upOn", 0 }, { "clipOn", 0 }, { "truePeak", 0 }, { "target", -10 } } },
        { "Dense Wall", "Quiet parts up, peaks down: thick and even",
          { { "gain", 0 }, { "ceiling", -1 }, { "style", 2 }, { "autoRelease", 1 }, { "link", 100 }, { "transient", 0 }, { "upOn", 1 }, { "upAmount", 6 }, { "upBelow", 10 }, { "clipOn", 1 }, { "clipDrive", 2 }, { "clipHard", 0 }, { "truePeak", 0 }, { "target", -8 } } },
        { "Gentle Master", "Slow and safe, true peak: catches the peaks, barely touches the rest",
          { { "gain", 0 }, { "ceiling", -1 }, { "style", 3 }, { "autoRelease", 1 }, { "link", 100 }, { "transient", 0 }, { "upOn", 0 }, { "clipOn", 0 }, { "truePeak", 1 }, { "target", -12 } } },
        { "Hard Clip Edge", "Hard clipping into the limiter: crisp, aggressive transients",
          { { "gain", 0 }, { "ceiling", -1 }, { "style", 1 }, { "autoRelease", 1 }, { "link", 100 }, { "transient", 20 }, { "upOn", 0 }, { "clipOn", 1 }, { "clipDrive", 4 }, { "clipHard", 1 }, { "truePeak", 0 }, { "target", -8 } } },
    };
    return p;
}

void FlatlineProcessor::loadPreset (int index)
{
    const auto& all = presets();
    currentPreset = juce::jlimit (0, (int) all.size() - 1, index);
    for (const auto& [pid, v] : all[(size_t) currentPreset].values)
        if (auto* prm = apvts.getParameter (pid)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    apvts.state.setProperty ("preset", currentPreset, nullptr);
}

fl::Settings FlatlineProcessor::settings() const
{
    auto v = [this] (const char* p) { return (double) apvts.getRawParameterValue (p)->load(); };
    fl::Settings s;
    s.gainDb = v ("gain"); s.ceilingDb = v ("ceiling");
    s.style = (fl::Style) juce::jlimit (0, 3, (int) std::lround (v ("style")));
    s.autoRelease = v ("autoRelease") > 0.5; s.releaseMs = v ("release");
    s.link = v ("link") * 0.01; s.transient = v ("transient") * 0.01;
    s.upOn = v ("upOn") > 0.5; s.upAmountDb = v ("upAmount"); s.upBelowDb = v ("upBelow");
    s.clipOn = v ("clipOn") > 0.5; s.clipDriveDb = v ("clipDrive"); s.clipHard = v ("clipHard") > 0.5;
    s.truePeak = v ("truePeak") > 0.5;
    { static const double looks[8] = { -1, 0.5, 1, 1.5, 2, 3, 4, 5 }; s.lookaheadMs = looks[juce::jlimit (0, 7, (int) std::lround (v ("lookahead")))]; } s.dither = (int) std::lround (v ("dither"));
    s.bypass = v ("bypass") > 0.5; s.delta = v ("delta") > 0.5;
    return s;
}

bool FlatlineProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == l.getMainInputChannelSet();
}

void FlatlineProcessor::prepareToPlay (double sampleRate, int)
{
    rate.store (sampleRate);
    dsp.prepare (sampleRate, getTotalNumInputChannels());
    setLatencySamples (dsp.latency());
}

void FlatlineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    if (numCh == 0 || buffer.getNumSamples() == 0) return;
    float* io[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0) };
    dsp.process (io, numCh, buffer.getNumSamples(), settings());
}

juce::AudioProcessorEditor* FlatlineProcessor::createEditor() { return new FlatlineEditor (*this); }

void FlatlineProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("preset", currentPreset, nullptr);
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void FlatlineProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset = (int) apvts.state.getProperty ("preset", 0);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FlatlineProcessor(); }
