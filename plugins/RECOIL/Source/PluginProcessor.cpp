#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }
    juce::String hz (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    }
    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi, [] (float s, float e, float t) { return s * std::pow (e / s, t); },
                 [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); } };
    }
    juce::String noteName (int n) { return n < 0 ? juce::String ("Any") : juce::MidiMessage::getMidiNoteName (n, true, true, 3); }

    const double kRateBeats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
}

juce::AudioProcessorValueTreeState::ParameterLayout RecoilProcessor::createLayout()
{
    using Float = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;
    using Bool = juce::AudioParameterBool;
    using Int = juce::AudioParameterInt;
    using Attr = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return juce::ParameterID { s, 2 }; };

    // trigger
    l.add (std::make_unique<Choice> (id ("trigger"), "Trigger", juce::StringArray { "Sync", "MIDI", "Audio", "Map", "RM" }, 2));
    l.add (std::make_unique<Choice> (id ("rate"), "Rate", juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" }, 2));
    l.add (std::make_unique<Choice> (id ("rateMode"), "Rate Mode", juce::StringArray { "Straight", "Dotted", "Triplet" }, 0));
    l.add (std::make_unique<Float> (id ("offset"), "Offset", juce::NormalisableRange<float> (-50.0f, 50.0f, 0.1f), 0.0f,
        Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + "%"; })));
    l.add (std::make_unique<Float> (id ("swing"), "Swing", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    l.add (std::make_unique<Bool> (id ("autoOffset"), "Auto Offset", false));
    l.add (std::make_unique<Int> (id ("midiNote"), "MIDI Note", -1, 127, -1,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return noteName (v); })));
    l.add (std::make_unique<Float> (id ("threshold"), "Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -40.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
    l.add (std::make_unique<Choice> (id ("listen"), "Listen", juce::StringArray { "Kick Lows", "Full" }, 0));
    l.add (std::make_unique<Float> (id ("dynamic"), "Dynamic", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    l.add (std::make_unique<Bool> (id ("fit"), "Fit Length", true));
    l.add (std::make_unique<Bool> (id ("groove"), "Groove Lock", true));

    // map: duck amount per kind of hit, ghost notes, nudge
    l.add (std::make_unique<Float> (id ("mapKick"), "Map Kick", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    l.add (std::make_unique<Float> (id ("mapSnare"), "Map Snare", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    l.add (std::make_unique<Float> (id ("mapRim"), "Map Rim", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    l.add (std::make_unique<Choice> (id ("mapGhost"), "Map Ghosts", juce::StringArray { "Ignore", "Scale", "Full" }, 0));
    l.add (std::make_unique<Float> (id ("mapGhostDb"), "Ghost Below", juce::NormalisableRange<float> (6.0f, 30.0f, 0.5f), 12.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + juce::String (juce::roundToInt (v)) + " dB"; })));
    l.add (std::make_unique<Float> (id ("mapOffset"), "Map Offset", juce::NormalisableRange<float> (-50.0f, 50.0f, 0.1f), 0.0f,
        Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " ms"; })));

    // shape
    l.add (std::make_unique<Choice> (id ("shape"), "Shape", shapes::choiceNames(), 0));
    l.add (std::make_unique<Choice> (id ("snareShape"), "Map Snare Shape", shapes::choiceNames(), 7));   // Quick
    l.add (std::make_unique<Choice> (id ("pluckShape"), "Map Pluck Shape", shapes::choiceNames(), 6));   // Snap
    l.add (std::make_unique<Choice> (id ("midShape"), "Map Mid Bass Shape", shapes::choiceNames(), 3));  // Smooth
    l.add (std::make_unique<Choice> (id ("noteShape"), "Map Note Shape", shapes::choiceNames(), 1));     // Hold
    l.add (std::make_unique<Choice> (id ("bassMode"), "Map Bass Mode", juce::StringArray { "Auto", "Shapes" }, 1));
    l.add (std::make_unique<Float> (id ("clearance"), "Clearance", juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
    l.add (std::make_unique<Float> (id ("recovery"), "Recovery", juce::NormalisableRange<float> (10.0f, 400.0f, 1.0f, 0.5f), 60.0f,
        Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })));
    // RM: per-sample ("ring mod") ducking against the kick on the sidechain
    {
        auto ms = [] (float v, int) { return v < 0.05f ? juce::String ("0 ms") : juce::String (v, v < 10.0f ? 1 : 0) + " ms"; };
        juce::NormalisableRange<float> hold (0.0f, 50.0f, 0.1f); hold.setSkewForCentre (5.0f);
        juce::NormalisableRange<float> rel (0.0f, 200.0f, 0.1f); rel.setSkewForCentre (10.0f);
        l.add (std::make_unique<Float> (id ("rmCeiling"), "RM Ceiling", juce::NormalisableRange<float> (-30.0f, 0.0f, 0.1f), -3.0f,
            Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return (v < -0.05f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String()) + juce::String (std::abs (v), 1) + " dB"; })));
        l.add (std::make_unique<Float> (id ("rmRange"), "RM Range", juce::NormalisableRange<float> (0.0f, 48.0f, 0.1f), 24.0f,
            Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " dB"; })));
        l.add (std::make_unique<Float> (id ("rmHold"), "RM Hold", hold, 0.0f, Attr().withLabel ("ms").withStringFromValueFunction (ms)));
        l.add (std::make_unique<Float> (id ("rmRelease"), "RM Smooth", rel, 3.0f, Attr().withLabel ("ms").withStringFromValueFunction (ms)));
    }
    l.add (std::make_unique<Float> (id ("depth"), "Depth", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) {
            if (v >= 99.95f) return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92\xe2\x88\x9e dB"));
            if (v <= 0.05f) return juce::String ("0 dB");
            return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + juce::String (-20.0f * std::log10 (1.0f - v * 0.01f), 1) + " dB"; })));
    l.add (std::make_unique<Float> (id ("length"), "Length", logRange (20.0f, 2000.0f), 250.0f,
        Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })));
    l.add (std::make_unique<Float> (id ("curve"), "Curve", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
        Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)) + "%"; })));
    l.add (std::make_unique<Float> (id ("attack"), "Attack", juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f, 0.5f), 4.0f,
        Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10 ? 1 : 0) + " ms"; })));

    // band
    l.add (std::make_unique<Choice> (id ("band"), "Band", juce::StringArray { "Full", "Split" }, 1));
    l.add (std::make_unique<Float> (id ("bandLo"), "Band Low", logRange (20.0f, 1000.0f), 20.0f, Attr().withLabel ("Hz").withStringFromValueFunction (hz)));
    l.add (std::make_unique<Float> (id ("bandHi"), "Band High", logRange (40.0f, 16000.0f), 180.0f, Attr().withLabel ("Hz").withStringFromValueFunction (hz)));
    l.add (std::make_unique<Choice> (id ("edge"), "Edge", juce::StringArray { "Soft", "Hard" }, 0));
    l.add (std::make_unique<Float> (id ("inside"), "Inside", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    l.add (std::make_unique<Float> (id ("above"), "Above", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));

    // phase
    l.add (std::make_unique<Choice> (id ("phase"), "Phase", juce::StringArray { "Auto", "Manual", "Off" }, 2));
    l.add (std::make_unique<Float> (id ("rotate"), "Rotate", juce::NormalisableRange<float> (-180.0f, 180.0f, 0.1f), 0.0f,
        Attr().withLabel (juce::CharPointer_UTF8 ("\xc2\xb0")).withStringFromValueFunction ([] (float v, int) {
            return (v > 0.05f ? "+" : v < -0.05f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String()) + juce::String (std::abs (juce::roundToInt (v))) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); })));
    l.add (std::make_unique<Bool> (id ("polarity"), "Polarity", false));

    // output
    l.add (std::make_unique<Float> (id ("mix"), "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, Attr().withLabel ("%").withStringFromValueFunction (pct)));
    // GAIN: -24..0 dB on the left half of the knob, 0..+12 dB on the right, so 0 dB sits at 12 o'clock
    juce::NormalisableRange<float> gainRange (-24.0f, 12.0f,
        [] (float, float, float v) { return v < 0.5f ? -24.0f + v * 48.0f : (v - 0.5f) * 24.0f; },
        [] (float, float, float db) { return db < 0.0f ? (db + 24.0f) / 48.0f : 0.5f + db / 24.0f; },
        [] (float, float, float db) { return juce::jlimit (-24.0f, 12.0f, std::round (db * 10.0f) / 10.0f); });
    l.add (std::make_unique<Float> (id ("gain"), "Output Gain", gainRange, 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " dB"; })));
    return l;
}

RecoilProcessor::RecoilProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "RECOIL", createLayout())
{
    formats.registerBasicFormats();
    apvts.state.addListener (this);
    apvts.addParameterListener ("shape", this);
    for (int k = 1; k < kSlots; ++k) apvts.addParameterListener (shapeParam (k), this);
    pushShape();
    startTimerHz (30);
}

RecoilProcessor::~RecoilProcessor()
{
    alive->store (false);
    if (mapThread != nullptr) mapThread->stopThread (10000);
    stopTimer();
    apvts.removeParameterListener ("shape", this);
    for (int k = 1; k < kSlots; ++k) apvts.removeParameterListener (shapeParam (k), this);
    apvts.state.removeListener (this);
}

bool RecoilProcessor::isBusesLayoutSupported (const BusesLayout& l) const
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

// ---- shape
bool RecoilProcessor::bassShapesActive() const
{
    return juce::roundToInt (apvts.getRawParameterValue ("trigger")->load()) == 3 && hasBass()
        && juce::roundToInt (apvts.getRawParameterValue ("bassMode")->load()) == 1;
}

bool RecoilProcessor::bassAutoActive() const
{
    return juce::roundToInt (apvts.getRawParameterValue ("trigger")->load()) == 3 && hasBass()
        && juce::roundToInt (apvts.getRawParameterValue ("bassMode")->load()) == 0;
}

int RecoilProcessor::shapeSlot() const
{
    const bool map = juce::roundToInt (apvts.getRawParameterValue ("trigger")->load()) == 3;
    const int s = juce::jlimit (0, kSlots - 1, (int) apvts.state.getProperty ("shapeSlot", 0));
    if (! map) return 0;
    if (bassShapesActive()) return s;                          // SUB, SNARE, PLUCK, MID, NOTE
    return s == 1 ? 1 : 0;                                     // KICK, SNARE
}

void RecoilProcessor::setShapeSlot (int slot)
{
    apvts.state.setProperty ("shapeSlot", juce::jlimit (0, kSlots - 1, slot), nullptr);
}

int RecoilProcessor::shapeIndex (int slot) const
{
    return juce::roundToInt (apvts.getRawParameterValue (shapeParam (resolve (slot)))->load());
}

std::vector<rc::Node> RecoilProcessor::currentNodes (int slot) const
{
    slot = resolve (slot);
    if (auto* e = shapes::builtIn (shapeIndex (slot))) return e->nodes;
    auto v = shapes::sanitise (shapes::fromTree (apvts.state.getChildWithName (shapeTree (slot))));
    if (v.size() < 2) return shapes::builtIn (0)->nodes;
    return v;
}

juce::String RecoilProcessor::currentShapeName (int slot) const
{
    slot = resolve (slot);
    if (auto* e = shapes::builtIn (shapeIndex (slot))) return e->name;
    const auto n = apvts.state.getProperty (nameKey (slot), "Custom").toString();
    return n.isEmpty() ? juce::String ("Custom") : n;
}

juce::String RecoilProcessor::currentShapeDescription (int slot) const
{
    if (auto* e = shapes::builtIn (shapeIndex (slot))) return e->desc;
    return "Your shape. Drag points, Alt-drag a segment to bend it, double-click to add or remove a point.";
}

void RecoilProcessor::setCustomNodes (const std::vector<rc::Node>& nodes, const juce::String& name, int slot)
{
    slot = resolve (slot);
    auto t = apvts.state.getOrCreateChildWithName (shapeTree (slot), nullptr);
    t.removeAllChildren (nullptr);
    for (const auto& c : shapes::toTree (shapes::sanitise (nodes))) t.appendChild (c.createCopy(), nullptr);
    apvts.state.setProperty (nameKey (slot), name, nullptr);
    if (shapeIndex (slot) != shapes::customIndex())
        if (auto* p = apvts.getParameter (shapeParam (slot)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) shapes::customIndex()));
            p->endChangeGesture();
        }
    shapeDirty.store (true);
}

void RecoilProcessor::selectBuiltIn (int index, int slot)
{
    if (auto* p = apvts.getParameter (shapeParam (resolve (slot))))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, shapes::numBuiltIn() - 1, index)));
        p->endChangeGesture();
    }
}

void RecoilProcessor::pushShape()
{
    bool all = true;
    for (int k = 0; k < kSlots; ++k) all = engine.trySetShape (currentNodes (k), k) && all;
    if (all) shapeDirty.store (false);
}

void RecoilProcessor::timerCallback()
{
    if (shapeDirty.load()) pushShape();
    if (mapDirty.load() && engine.trySetMap (mapHits, bassMapForEngine())) mapDirty.store (false);
}

//==============================================================================
// MAP
bool RecoilProcessor::isAudioFile (const juce::String& path)
{
    return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

namespace
{
    // a file as mono at about 22 kHz (box-averaged down; resampled to exactly 'targetFs' when one is given)
    std::vector<float> readMono (juce::AudioFormatManager& fm, const juce::File& file, double& fs, double& seconds, juce::String& error,
                                 juce::Thread* th, std::atomic<float>& progress, float p0, float p1, double targetFs = 0.0)
    {
        std::vector<float> mono;
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
        if (r == nullptr) { error = "Can't read " + file.getFileName() + " (WAV, AIFF, FLAC, OGG or MP3)"; return mono; }
        if (r->sampleRate <= 0 || r->lengthInSamples < (juce::int64) (r->sampleRate * 0.5)) { error = file.getFileName() + " is too short"; return mono; }
        if (r->lengthInSamples > (juce::int64) (r->sampleRate * 60 * 30)) { error = file.getFileName() + " is longer than 30 minutes: bounce just the song"; return mono; }
        const int k = juce::jmax (1, (int) std::floor (r->sampleRate / 22050.0));
        fs = r->sampleRate / k;
        seconds = (double) r->lengthInSamples / r->sampleRate;
        mono.reserve ((size_t) (r->lengthInSamples / k + 1));
        const int chunk = 1 << 16, nCh = (int) juce::jmin (2u, r->numChannels);
        juce::AudioBuffer<float> buf (juce::jmax (1, nCh), chunk);
        double acc = 0; int cnt = 0;
        for (juce::int64 pos = 0; pos < r->lengthInSamples; pos += chunk)
        {
            if (th != nullptr && th->threadShouldExit()) { mono.clear(); return mono; }
            const int m = (int) juce::jmin ((juce::int64) chunk, r->lengthInSamples - pos);
            r->read (&buf, 0, m, pos, true, nCh > 1);
            for (int i = 0; i < m; ++i)
            {
                acc += nCh > 1 ? 0.5 * (buf.getSample (0, i) + buf.getSample (1, i)) : buf.getSample (0, i);
                if (++cnt == k) { mono.push_back ((float) (acc / k)); acc = 0; cnt = 0; }
            }
            progress.store (p0 + (p1 - p0) * (float) pos / (float) r->lengthInSamples);
        }
        // the averaging delays by half a step: take it back out
        const double shift = (k - 1) * 0.5 / r->sampleRate * fs;
        if (targetFs > 0.0 && (std::abs (targetFs - fs) > 1e-6 || shift > 1e-6))
        {
            const double step = fs / targetFs;
            std::vector<float> out ((size_t) std::floor ((double) mono.size() / step));
            for (size_t i = 0; i < out.size(); ++i)
            {
                const double p = juce::jmax (0.0, (double) i * step + shift);
                const size_t a = juce::jmin (mono.size() - 2, (size_t) p); const double f = p - (double) a;
                out[i] = (float) (mono[a] + (mono[a + 1] - mono[a]) * f);
            }
            mono.swap (out); fs = targetFs;
        }
        return mono;
    }

    // bass clash: only kicks' and snares' rows, each trimmed after its last frame in use
    juce::String clashToString (const std::vector<rcmap::Hit>& hits, const std::vector<int8_t>& clash)
    {
        std::vector<uint8_t> out;
        for (size_t h = 0; h < hits.size(); ++h)
        {
            if (hits[h].type != rcmap::Kick && hits[h].type != rcmap::Snare) continue;
            const int8_t* c = &clash[h * (size_t) rcbass::kFrames];
            int n = rcbass::kFrames; while (n > 0 && c[n - 1] == rcbass::kGone) --n;
            out.push_back ((uint8_t) n);
            for (int i = 0; i < n; ++i) out.push_back ((uint8_t) c[i]);
        }
        return rcbass::toHex (out);
    }
    std::vector<int8_t> clashFromString (const std::vector<rcmap::Hit>& hits, const juce::String& s)
    {
        const auto in = rcbass::fromHex (s.toStdString());
        std::vector<int8_t> clash (hits.size() * (size_t) rcbass::kFrames, rcbass::kGone);
        size_t p = 0;
        for (size_t h = 0; h < hits.size(); ++h)
        {
            if (hits[h].type != rcmap::Kick && hits[h].type != rcmap::Snare) continue;
            if (p >= in.size()) return {};
            const int n = juce::jmin ((int) in[p++], rcbass::kFrames);
            for (int i = 0; i < n && p < in.size(); ++i) clash[h * (size_t) rcbass::kFrames + (size_t) i] = (int8_t) in[p++];
        }
        return clash;
    }
}

struct RecoilProcessor::MapJob : public juce::Thread
{
    // drums: analyse this drum bus (empty: keep 'hits'); bass: analyse this bass bus against the drums (empty: none)
    MapJob (RecoilProcessor& p, juce::File d, juce::File b, bool newDrums)
        : juce::Thread ("RECOIL map"), owner (p), drums (std::move (d)), bassFile (std::move (b)), analyseDrums (newDrums), alive (p.alive)
    {
        result.hits = p.mapHits; result.info = p.mapInfo;
    }

    void run() override
    {
        auto& res = result;
        res.newDrums = analyseDrums;
        double fs = 0, secs = 0;
        const bool withBass = bassFile != juce::File();
        auto mono = readMono (owner.formats, drums, fs, secs, res.error, this, owner.mapProgress, 0.0f, withBass ? 0.2f : 0.3f);
        if (threadShouldExit()) return;
        if (res.error.isEmpty() && analyseDrums)
        {
            res.info = {};
            res.info.name = drums.getFileNameWithoutExtension();
            res.info.path = drums.getFullPathName();
            res.info.seconds = secs;
            res.hits = rcmap::analyse (mono, fs, &res.info.stats, [this, withBass] (float p) { owner.mapProgress.store ((withBass ? 0.2f : 0.3f) + (withBass ? 0.3f : 0.7f) * p); });
            if (res.hits.empty()) res.error = "No hits found in " + drums.getFileName();
        }
        if (res.error.isEmpty() && withBass)
        {
            double bfs = 0, bsecs = 0;
            juce::String err;
            auto bass = readMono (owner.formats, bassFile, bfs, bsecs, err, this, owner.mapProgress, 0.5f, 0.7f, fs);
            if (threadShouldExit()) return;
            if (err.isNotEmpty()) res.bassError = err;
            else
            {
                res.bass = rcbass::analyse (mono, bass, fs, res.hits, [this] (float p) { owner.mapProgress.store (0.7f + 0.3f * p); });
                res.hasBass = true;
                res.bassInfo.name = bassFile.getFileNameWithoutExtension();
                res.bassInfo.path = bassFile.getFullPathName();
                res.bassInfo.seconds = bsecs;
                if (std::abs (bsecs - secs) > 2.0) res.bassError = "The drum and bass bounces differ in length by " + juce::String (std::abs (bsecs - secs), 1) + " s: do they both start at bar 1?";
            }
        }
        auto a = alive; auto* p = &owner;
        juce::MessageManager::callAsync ([a, p, r = std::move (result)]() mutable { if (a->load()) p->mapAnalysed (std::move (r)); });
    }

    RecoilProcessor& owner;
    juce::File drums, bassFile;
    bool analyseDrums;
    JobResult result;
    std::shared_ptr<std::atomic<bool>> alive;
};

void RecoilProcessor::startMapJob (const juce::File& drums, const juce::File& bass, bool newDrums)
{
    if (mapThread != nullptr) mapThread->stopThread (10000);
    mapError.clear();
    mapProgress.store (0.0f);
    mapThread = std::make_unique<MapJob> (*this, drums, bass, newDrums);
    mapThread->startThread();
    mapVersion.fetch_add (1);
}

void RecoilProcessor::loadMapFile (const juce::File& f)
{
    // a new drum bus: the bass (if one is loaded and still there) is worked out again against it
    const juce::File bass (bassInfo.path);
    startMapJob (f, bassInfo.path.isNotEmpty() && bass.existsAsFile() ? bass : juce::File(), true);
}

void RecoilProcessor::loadBassFile (const juce::File& f)
{
    const juce::File drums (mapInfo.path);
    if (mapHits.empty()) { mapError = "Load the drum bus first, then the bass"; mapVersion.fetch_add (1); return; }
    if (mapInfo.path.isEmpty() || ! drums.existsAsFile()) { mapError = "The drum bus file has moved: load it again, then the bass"; mapVersion.fetch_add (1); return; }
    startMapJob (drums, f, false);
}

void RecoilProcessor::mapAnalysed (JobResult r)
{
    mapProgress.store (-1.0f);
    mapError = r.error.isNotEmpty() ? r.error : r.bassError;
    if (r.error.isEmpty())
    {
        if (r.newDrums) { mapHits = std::move (r.hits); mapInfo = r.info; }
        if (r.hasBass)
        {
            bassKind = std::move (r.bass.kind); bassClash = std::move (r.bass.clash); bassEnv = std::move (r.bass.env);
            bassInfo = r.bassInfo;
            for (int k = 0; k < rcbass::kKinds; ++k) bassInfo.counts[k] = r.bass.counts[k];
        }
        else if (r.newDrums) clearBassData();                           // the old bass no longer matches the new hits
        storeMap();
        mapDirty.store (true);
    }
    mapVersion.fetch_add (1);
}

void RecoilProcessor::clearBassData()
{
    bassKind.clear(); bassClash.clear(); bassEnv.clear(); bassInfo = {};
}

void RecoilProcessor::clearBass()
{
    clearBassData(); mapError.clear();
    storeMap();
    mapDirty.store (true);
    mapVersion.fetch_add (1);
}

void RecoilProcessor::clearMap()
{
    mapHits.clear(); mapInfo = {}; mapError.clear();
    clearBassData();
    storeMap();
    mapDirty.store (true);
    mapVersion.fetch_add (1);
}

void RecoilProcessor::setMapHitOn (int index, bool on)
{
    if (! juce::isPositiveAndBelow (index, (int) mapHits.size())) return;
    mapHits[(size_t) index].on = on;
    storeMap();
    mapDirty.store (true);
    mapVersion.fetch_add (1);
}

rc::Engine::BassMap RecoilProcessor::bassMapForEngine() const
{
    rc::Engine::BassMap b;
    if (hasBass()) { b.kind = bassKind; b.clash = bassClash; }
    return b;
}

void RecoilProcessor::storeMap()
{
    auto t = apvts.state.getOrCreateChildWithName ("MAP", nullptr);
    t.setProperty ("name", mapInfo.name, nullptr);
    t.setProperty ("path", mapInfo.path, nullptr);
    t.setProperty ("seconds", mapInfo.seconds, nullptr);
    t.setProperty ("hits", juce::String (rcmap::toString (mapHits)), nullptr);
    const bool b = hasBass();
    t.setProperty ("bassName", b ? bassInfo.name : juce::String(), nullptr);
    t.setProperty ("bassPath", b ? bassInfo.path : juce::String(), nullptr);
    t.setProperty ("bassSeconds", b ? bassInfo.seconds : 0.0, nullptr);
    t.setProperty ("bassKind", b ? juce::String (rcbass::toHex (bassKind)) : juce::String(), nullptr);
    t.setProperty ("bassClash", b ? clashToString (mapHits, bassClash) : juce::String(), nullptr);
    t.setProperty ("bassEnv", b ? juce::String (rcbass::toHex (bassEnv)) : juce::String(), nullptr);
}

void RecoilProcessor::restoreMap()
{
    const auto t = apvts.state.getChildWithName ("MAP");
    mapHits = t.isValid() ? rcmap::fromString (t.getProperty ("hits").toString().toStdString()) : std::vector<rcmap::Hit> {};
    mapInfo = {};
    clearBassData();
    if (t.isValid())
    {
        mapInfo.name = t.getProperty ("name").toString();
        mapInfo.path = t.getProperty ("path").toString();
        mapInfo.seconds = (double) t.getProperty ("seconds", 0.0);
        for (auto& h : mapHits) { mapInfo.stats.count[h.type]++; if (h.type == rcmap::Snare && h.rel < -12.0f) mapInfo.stats.ghosts++; }
        mapInfo.stats.seconds = mapInfo.seconds;
        const auto kind = rcbass::fromHex (t.getProperty ("bassKind").toString().toStdString());
        if (! mapHits.empty() && kind.size() == mapHits.size())
        {
            auto clash = clashFromString (mapHits, t.getProperty ("bassClash").toString());
            if (! clash.empty())
            {
                bassKind = kind; bassClash = std::move (clash);
                bassEnv = rcbass::fromHex (t.getProperty ("bassEnv").toString().toStdString());
                bassInfo.name = t.getProperty ("bassName").toString();
                bassInfo.path = t.getProperty ("bassPath").toString();
                bassInfo.seconds = (double) t.getProperty ("bassSeconds", 0.0);
                for (size_t h = 0; h < mapHits.size(); ++h) if (mapHits[h].type == rcmap::Kick) bassInfo.counts[bassKind[h]]++;
            }
        }
    }
    mapDirty.store (true);
    mapVersion.fetch_add (1);
}

rc::Settings RecoilProcessor::currentSettings() const
{
    auto v = [this] (const char* pid) { return apvts.getRawParameterValue (pid)->load(); };
    rc::Settings s;
    s.trigger = (rc::Trigger) juce::jlimit (0, 4, juce::roundToInt (v ("trigger")));
    s.mapAmount = { v ("mapKick") * 0.01, v ("mapSnare") * 0.01, v ("mapRim") * 0.01, 0.0 };
    s.ghost = (rc::GhostMode) juce::jlimit (0, 2, juce::roundToInt (v ("mapGhost")));
    s.ghostDb = v ("mapGhostDb");
    s.mapOffsetMs = v ("mapOffset");
    s.bassMode = juce::roundToInt (v ("bassMode"));
    s.clearanceDb = v ("clearance");
    s.recoveryMs = v ("recovery");
    s.rmCeilDb = v ("rmCeiling"); s.rmRangeDb = v ("rmRange"); s.rmHoldMs = v ("rmHold"); s.rmReleaseMs = v ("rmRelease");
    const int mode = juce::roundToInt (v ("rateMode"));
    s.rateBeats = kRateBeats[juce::jlimit (0, 5, juce::roundToInt (v ("rate")))] * (mode == 1 ? 1.5 : mode == 2 ? 2.0 / 3.0 : 1.0);
    s.offset = v ("offset") * 0.01;
    s.swing = v ("swing") * 0.01;
    s.autoOffset = v ("autoOffset") > 0.5f;
    s.midiNote = juce::roundToInt (v ("midiNote"));
    s.threshDb = v ("threshold");
    s.listenLows = juce::roundToInt (v ("listen")) == 0;
    s.dynamic = v ("dynamic") * 0.01;
    s.fit = v ("fit") > 0.5f;
    s.groove = v ("groove") > 0.5f;
    s.depth = v ("depth") * 0.01;
    s.lengthMs = v ("length");
    s.curve = v ("curve") * 0.01;
    s.attackMs = v ("attack");
    s.split = juce::roundToInt (v ("band")) == 1;
    s.lo = v ("bandLo");
    s.hi = juce::jmax ((double) v ("bandHi"), s.lo * 1.25);
    s.hard = juce::roundToInt (v ("edge")) == 1;
    s.inside = v ("inside") * 0.01;
    s.above = v ("above") * 0.01;
    s.phase = (rc::PhaseMode) juce::jlimit (0, 2, juce::roundToInt (v ("phase")));
    if (s.trigger == rc::Trigger::Sync || s.trigger == rc::Trigger::Midi) s.phase = (rc::PhaseMode) 2;   // no kick to line up with: phase alignment off
    s.rotateDeg = v ("rotate");
    s.polarity = v ("polarity") > 0.5f;
    s.mix = v ("mix") * 0.01;
    s.gainDb = v ("gain");
    return s;
}

void RecoilProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate.store (sampleRate);
    engine.prepare (sampleRate, samplesPerBlock);
    for (int k = 0; k < kSlots; ++k) engine.trySetShape (currentNodes (k), k);
    shapeDirty.store (true);                                  // in case a slot was still busy
    const size_t n = (size_t) juce::jmax (1, samplesPerBlock);
    for (auto* v : { &kTmp, &bTmp, &aTmp, &gTmp, &sTmp, &tTmp }) v->assign (n, 0.0f);
    midiTmp.reserve (256);
    setLatencySamples (0);
}

void RecoilProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples();
    const int ch = main.getNumChannels();
    if (ch == 0 || n == 0) return;
    for (auto* v : { &kTmp, &bTmp, &aTmp, &gTmp, &sTmp, &tTmp }) if ((int) v->size() < n) v->resize ((size_t) n);

    const auto s = currentSettings();

    rc::HostTime host;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            host.playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { host.ppq = *p; host.valid = true; }
            if (auto b = pos->getBpm()) host.bpm = *b;
            if (auto ts = pos->getTimeSignature()) host.barBeats = ts->numerator * 4.0 / juce::jmax (1, ts->denominator);
            if (auto t = pos->getTimeInSeconds()) { host.timeSec = *t; host.hasTime = true; }
            else if (auto smp = pos->getTimeInSamples()) { host.timeSec = (double) *smp / getSampleRate(); host.hasTime = true; }
            else if (host.valid && host.bpm > 0) { host.timeSec = host.ppq * 60.0 / host.bpm; host.hasTime = true; }
        }
    hostPlaying.store (host.valid && host.playing);
    hostBpm.store (host.bpm);

    // sidechain
    const float* sl = nullptr; const float* sr = nullptr;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0) { sl = sc.getReadPointer (0); sr = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : nullptr; }
    }
    sidechainConnected.store (sl != nullptr);

    // MIDI note-ons
    midiTmp.clear();
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (msg.isNoteOn() && (s.midiNote < 0 || msg.getNoteNumber() == s.midiNote) && midiTmp.size() < 256)
            midiTmp.push_back ({ m.samplePosition, msg.getFloatVelocity() });
    }

    float* io[2] = { main.getWritePointer (0), ch > 1 ? main.getWritePointer (1) : nullptr };
    float pkSc = 0;
    if (sl != nullptr)
        for (int i = 0; i < n; ++i) { sTmp[(size_t) i] = sr != nullptr ? 0.5f * (sl[i] + sr[i]) : sl[i]; pkSc = juce::jmax (pkSc, std::abs (sTmp[(size_t) i])); }
    else std::fill (sTmp.begin(), sTmp.begin() + n, 0.0f);

    rc::ScopeOut so; so.kickLow = kTmp.data(); so.before = bTmp.data(); so.after = aTmp.data(); so.gain = gTmp.data(); so.duckType = tTmp.data();
    const int64_t blockStart = scopeAfter.total.load (std::memory_order_relaxed);
    engine.process (io, juce::jmin (ch, 2), sl, sr, n, s, host, midiTmp.data(), (int) midiTmp.size(), so);
    if (sl == nullptr) std::fill (kTmp.begin(), kTmp.begin() + n, 0.0f);

    scopeKick.push (kTmp.data(), n); scopeSc.push (sTmp.data(), n);
    scopeBefore.push (bTmp.data(), n); scopeAfter.push (aTmp.data(), n); scopeGain.push (gTmp.data(), n); scopeType.push (tTmp.data(), n);
    cycleSamples.store (engine.cycleSamples);
    if (engine.lastHit >= 0)
    {
        hitAt.store (blockStart + engine.lastHit); hitCount.fetch_add (1);
        const auto k = (size_t) juce::jlimit (0, 3, engine.lastHitKind);
        hitAtKind[k].store (blockStart + engine.lastHit); hitLenKind[k].store (engine.cycleSamples); hitCountKind[k].fetch_add (1);
    }

    float pk = 0;
    for (int c = 0; c < juce::jmin (ch, 2); ++c) for (int i = 0; i < n; ++i) pk = juce::jmax (pk, std::abs (io[c][i]));
    if (pk > outPeak.load()) outPeak.store (pk);
    if (pkSc > scPeak.load()) scPeak.store (pkSc);

    for (int c = ch; c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
}

juce::AudioProcessorEditor* RecoilProcessor::createEditor() { return new RecoilEditor (*this); }

void RecoilProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void RecoilProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    shapeDirty.store (true);
    restoreMap();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RecoilProcessor(); }
