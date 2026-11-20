#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ShrapnelEngine.h"
#include <map>
#include <utility>
#include <vector>

// Factory presets (built in) plus user presets saved as small XML files in
//   Windows: %APPDATA%\SHRAPNEL\Presets
//   macOS:   ~/Library/Application Support/SHRAPNEL/Presets
// A preset holds every parameter and the drive curve's points.
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
        return base.getChildFile ("SHRAPNEL").getChildFile ("Presets");
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
            std::vector<sh::CurvePoint> pts;
            if (auto* c = xml->getChildByName ("CURVE"))
                for (auto* p : c->getChildIterator())
                    pts.push_back ({ (float) p->getDoubleAttribute ("freq", 1000.0), (float) p->getDoubleAttribute ("amount", 0.0) });
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

        juce::XmlElement xml ("ShrapnelPreset");
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
            e->setAttribute ("amount", (double) p.getProperty ("amount"));
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
        std::vector<sh::CurvePoint> curve;
    };

    // Each preset lists only what differs from the defaults (HARMONICS, DRIVE 50 %, CHARACTER 50 %,
    // TONE 0, SPLIT tonal 90 / noise 50 / transient 20 %, DYNAMIC 50 %, NORMAL quality, soft clip).
    // Modes: 0 HARMONICS, 1 CLIP, 2 CRUSH, 3 SMEAR, 4 SCRAMBLE, 5 WARP. Curve points: amount
    // -1 .. +1 (x0 .. x3 drive) around a frequency.
    static const std::vector<Factory>& factory()
    {
        static const std::vector<Factory> presets {
            { "Default", "Warm harmonics at half drive: a starting point", {}, {} },
            { "Bass Thickener", "Adds upper harmonics so bass and 808s cut through on small speakers",
              { { "drive", 62 }, { "character", 30 }, { "noise", 20 }, { "transient", 10 }, { "dynamic", 40 } },
              { { 90.0f, 0.5f }, { 2500.0f, -0.6f } } },
            { "Odd Fuzz", "Hard, buzzy odd harmonics for synths and guitars",
              { { "drive", 85 }, { "character", 90 }, { "tone", -20 }, { "dynamic", 70 } }, {} },
            { "Vocal Grit", "Pulls the hidden rasp up in vocals, blended in",
              { { "mode", 1 }, { "drive", 45 }, { "character", 35 }, { "tonal", 80 }, { "noise", 60 }, { "transient", 30 }, { "mix", 70 } },
              { { 2500.0f, 0.5f } } },
            { "Synth Shred", "Flattens everything into a wall of aggressive grit",
              { { "mode", 1 }, { "drive", 75 }, { "character", 70 }, { "tone", 20 } }, {} },
            { "Lo-Fi Robot", "Spectral bitcrush: steppy, blocky and robotic",
              { { "mode", 2 }, { "drive", 70 }, { "character", 70 }, { "tone", -30 } }, {} },
            { "Total Shred", "Everything crushed to blocks, every part of the sound. Hard clip",
              { { "mode", 2 }, { "drive", 100 }, { "character", 100 }, { "tonal", 100 }, { "noise", 100 }, { "transient", 100 }, { "clip", 2 } }, {} },
            { "Tape Ghost", "Smears the sound into a washed, broken-tape blur",
              { { "mode", 3 }, { "drive", 55 }, { "character", 40 }, { "transient", 10 }, { "mix", 60 } }, {} },
            { "Whisper", "Turns tones breathy and whispery, like noise shaped like the sound",
              { { "mode", 4 }, { "drive", 80 }, { "character", 20 }, { "tonal", 100 }, { "noise", 100 } }, {} },
            { "Glitch Gate", "Random spectral gating: sparkly, stuttering texture",
              { { "mode", 4 }, { "drive", 60 }, { "character", 90 }, { "mix", 70 } }, {} },
            { "Metallic Drums", "Shifts and stretches drums into clanky, metallic hits",
              { { "mode", 5 }, { "drive", 55 }, { "character", 70 }, { "noise", 60 }, { "transient", 40 }, { "mix", 80 } }, {} },
            { "Bell Shift", "Moves partials off their harmonics: bell-like and inharmonic",
              { { "mode", 5 }, { "drive", 40 }, { "character", 50 }, { "noise", 10 }, { "transient", 0 } }, {} },
        };
        return presets;
    }

    void writeCurve (const std::vector<sh::CurvePoint>& pts)
    {
        auto curve = apvts.state.getOrCreateChildWithName ("CURVE", nullptr);
        curve.removeAllChildren (nullptr);
        for (size_t i = 0; i < pts.size() && i < (size_t) sh::kMaxCurvePoints; ++i)
        {
            juce::ValueTree p ("POINT");
            p.setProperty ("freq", pts[i].freq, nullptr);
            p.setProperty ("amount", pts[i].amount, nullptr);
            curve.appendChild (p, nullptr);
        }
    }

    static juce::String extension() { return ".shpreset"; }
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
