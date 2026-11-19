#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SpectralEngine.h"
#include <map>
#include <utility>
#include <vector>

// Factory presets (built in) plus user presets saved as small XML files in
//   Windows: %APPDATA%\SPECTRAKILL\Presets
//   macOS:   ~/Library/Application Support/SPECTRAKILL/Presets
// A preset holds every parameter and the threshold curve's points.
// The current preset name is kept in the plugin state, so it is restored with the project.
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

    static constexpr int kMaxDescription = 70;     // fits on one line in the preset list

    static juce::File getUserDir()
    {
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        base = base.getChildFile ("Application Support");
       #endif
        return base.getChildFile ("SPECTRAKILL").getChildFile ("Presets");
    }

    static juce::StringArray getFactoryNames()
    {
        juce::StringArray names;
        for (const auto& f : factory()) names.add (f.name);
        return names;
    }

    static juce::StringArray getUserNames()
    {
        juce::StringArray names;
        for (const auto& f : getUserDir().findChildFiles (juce::File::findFiles, false, "*" + extension()))
            names.add (f.getFileNameWithoutExtension());
        names.sortNatural();
        return names;
    }

    static bool isFactory (const juce::String& name) { return getFactoryNames().contains (name); }

    static juce::String getDescription (const juce::String& name)
    {
        for (const auto& f : factory())
            if (f.name == name) return f.description;
        const auto own = getUserDescription (name);
        return own.isNotEmpty() ? own : juce::String ("Your preset");
    }

    static juce::String getUserDescription (const juce::String& name)
    {
        if (isFactory (name)) return {};
        if (auto xml = juce::parseXML (fileFor (name)))
            return xml->getStringAttribute ("description").trim();
        return {};
    }

    juce::String getCurrentName() const { return apvts.state.getProperty ("presetName", "Default").toString(); }

    bool load (const juce::String& name)
    {
        for (const auto& f : factory())
        {
            if (name == f.name)
            {
                for (auto* p : apvts.processor.getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                        setNormalised (*rp, rp->getDefaultValue());
                for (const auto& [id, value] : f.values)
                    setParam (id, value);
                writeCurve (f.curve);
                setCurrentName (name);
                return true;
            }
        }

        if (auto xml = juce::parseXML (fileFor (name)))
        {
            for (auto* p : apvts.processor.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                    if (xml->hasAttribute (rp->paramID))
                        setParam (rp->paramID, (float) xml->getDoubleAttribute (rp->paramID));
            std::vector<sk::CurvePoint> pts;
            if (auto* c = xml->getChildByName ("CURVE"))
                for (auto* p : c->getChildIterator())
                    pts.push_back ({ (float) p->getDoubleAttribute ("freq", 1000.0), (float) p->getDoubleAttribute ("db", 0.0) });
            writeCurve (pts);
            setCurrentName (name);
            return true;
        }
        return false;
    }

    // Saves the current settings. Returns the name actually used (empty on failure).
    juce::String save (const juce::String& requestedName, const juce::String& description = {})
    {
        auto name = juce::File::createLegalFileName (requestedName.trim());
        if (name.isEmpty())
            return {};
        if (isFactory (name))
            name << " (User)";

        juce::XmlElement xml ("SpectraKillPreset");
        xml.setAttribute ("name", name);
        xml.setAttribute ("description", description.trim().substring (0, kMaxDescription));
        for (auto* p : apvts.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                xml.setAttribute (rp->paramID, (double) rp->convertFrom0to1 (rp->getValue()));
        auto* c = xml.createNewChildElement ("CURVE");
        for (const auto& p : apvts.state.getChildWithName ("CURVE"))
        {
            auto* e = c->createNewChildElement ("POINT");
            e->setAttribute ("freq", (double) p.getProperty ("freq"));
            e->setAttribute ("db", (double) p.getProperty ("db"));
        }

        if (! getUserDir().createDirectory().wasOk() || ! xml.writeTo (fileFor (name)))
            return {};
        setCurrentName (name);
        return name;
    }

    bool remove (const juce::String& name)
    {
        if (isFactory (name))
            return false;
        const bool ok = fileFor (name).deleteFile();
        if (ok && getCurrentName() == name)
            setCurrentName ("Default");
        return ok;
    }

private:
    struct Factory
    {
        juce::String name, description;
        std::vector<std::pair<juce::String, float>> values;
        std::vector<sk::CurvePoint> curve;
    };

    // Each preset lists only what differs from the defaults (threshold -24 dB, DOWN 50 %, UP 0 %,
    // attack 10 ms, release 120 ms, quality HIGH). Curve points lower (negative) or raise the
    // threshold around a frequency: lower = that area gets hit harder.
    static const std::vector<Factory>& factory()
    {
        static const std::vector<Factory> presets {
            { "Default", "A starting point: half-strength cut above -24 dB", {}, {} },
            { "Harsh Killer", "Pulls the 2-5 kHz bite out of vocals and guitars",
              { { "threshold", -30 }, { "down", 70 }, { "attack", 4 }, { "release", 80 }, { "tilt", 1.0f }, { "sharpness", 40 } },
              { { 3500.0f, -9.0f } } },
            { "Drum Smash", "Squashes the room and ring out of drums, keeps the hits",
              { { "threshold", -38 }, { "down", 75 }, { "up", 20 }, { "attack", 15 }, { "release", 120 }, { "quality", 1 }, { "mix", 80 } },
              {} },
            // matched against a reference spectral compressor's drum demo (dry vs processed): quiet
            // room and tails come up 5-8 dB, the hits keep their peaks, the top gets brighter
            { "Drum Detail", "Lifts room and tails, keeps the hits, brighter top. Soft clip on",
              { { "threshold", -12 }, { "down", 80 }, { "up", 32 }, { "tilt", 2.6f }, { "attack", 0.5f }, { "release", 185 },
                { "gain", -1 }, { "quality", 1 }, { "clip", 1 } },
              {} },
            { "Hi-Hat Tamer", "Takes the sizzle and spikes off hats and cymbals",
              { { "threshold", -34 }, { "down", 80 }, { "attack", 2 }, { "release", 60 }, { "tilt", 2.0f }, { "quality", 1 } },
              { { 9000.0f, -6.0f } } },
            { "Bass Control", "Evens out boomy notes in bass and 808s",
              { { "threshold", -22 }, { "down", 65 }, { "attack", 20 }, { "release", 200 }, { "tilt", -2.0f }, { "quality", 3 } },
              { { 90.0f, -5.0f } } },
            { "Full Mix Glue", "Gentle spectral balance for a whole mix or master",
              { { "threshold", -20 }, { "down", 30 }, { "up", 15 }, { "attack", 30 }, { "release", 300 }, { "quality", 4 } },
              {} },
            { "Upward Detail", "Brings up quiet detail and room without touching peaks",
              { { "threshold", -40 }, { "down", 0 }, { "up", 50 }, { "attack", 20 }, { "release", 200 } },
              {} },
            { "Resonance Kill", "Finds ringing tones in synths and growls and flattens them",
              { { "threshold", -30 }, { "down", 90 }, { "attack", 3 }, { "release", 50 }, { "sharpness", 70 } },
              {} },
            { "Piano Smooth", "Softens hard notes and boxy resonances in piano",
              { { "threshold", -28 }, { "down", 40 }, { "up", 10 }, { "attack", 25 }, { "release", 250 }, { "quality", 3 } },
              { { 500.0f, -4.0f } } },
            // route the kick (or vocal) to the sidechain input: the bass dips only at the frequencies
            // and moments the kick is loud, so both keep their own space instead of the whole bass pumping
            { "Sidechain Duck", "Sidechain: bass dips only where the kick or vocal is loud",
              { { "sidechain", 1 }, { "threshold", -42 }, { "down", 85 }, { "attack", 2 }, { "release", 140 },
                { "tilt", -1.5f }, { "sharpness", 35 }, { "quality", 1 } },
              { { 70.0f, -6.0f } } },
            { "Total Kill", "Everything flattened and pushed loud. Soft clip on",
              { { "threshold", -45 }, { "down", 100 }, { "up", 40 }, { "attack", 5 }, { "release", 80 }, { "gain", 6 }, { "clip", 1 } },
              {} },
        };
        return presets;
    }

    void writeCurve (const std::vector<sk::CurvePoint>& pts)
    {
        auto curve = apvts.state.getOrCreateChildWithName ("CURVE", nullptr);
        curve.removeAllChildren (nullptr);
        for (size_t i = 0; i < pts.size() && i < (size_t) sk::kMaxCurvePoints; ++i)
        {
            juce::ValueTree p ("POINT");
            p.setProperty ("freq", pts[i].freq, nullptr);
            p.setProperty ("db", pts[i].db, nullptr);
            curve.appendChild (p, nullptr);
        }
    }

    static juce::String extension() { return ".skpreset"; }
    static juce::File fileFor (const juce::String& name) { return getUserDir().getChildFile (name + extension()); }

    void setCurrentName (const juce::String& name) { apvts.state.setProperty ("presetName", name, nullptr); }

    static void setNormalised (juce::RangedAudioParameter& p, float v)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (v);
        p.endChangeGesture();
    }

    void setParam (const juce::String& id, float realValue)
    {
        if (auto* p = apvts.getParameter (id))
            setNormalised (*p, p->convertTo0to1 (realValue));
    }

    juce::AudioProcessorValueTreeState& apvts;
};
