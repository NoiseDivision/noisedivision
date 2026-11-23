#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Shapes.h"
#include <map>
#include <utility>
#include <vector>

// Factory presets (built in) plus user presets saved as small XML files in
//   Windows: %APPDATA%\RECOIL\Presets
//   macOS:   ~/Library/Application Support/RECOIL/Presets
// A preset holds every parameter and, when the shape is Custom, its points.
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
        return base.getChildFile ("RECOIL").getChildFile ("Presets");
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

    // MAP's OFFSET belongs to the song that was loaded, not to a sound: presets leave it alone
    static bool songSpecific (const juce::String& id) { return id == "mapOffset"; }
    static const char* tagOf (int k) { static const char* n[] = { "SHAPE", "SHAPE2", "SHAPE3", "SHAPE4", "SHAPE5" }; return n[k]; }
    static const char* keyOf (int k) { static const char* n[] = { "customName", "customName2", "customName3", "customName4", "customName5" }; return n[k]; }

    juce::String getCurrentName() const { return apvts.state.getProperty ("presetName", "Default").toString(); }

    bool load (const juce::String& name)
    {
        for (const auto& f : factory())
        {
            if (name == f.name)
            {
                for (auto* p : apvts.processor.getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                        if (! songSpecific (rp->paramID))
                            setNormalised (*rp, rp->getDefaultValue());
                if (name != "Default")                                  // the others build on the plain classic setup
                    for (const auto& [id, value] : classicBase())
                        setParam (id, value);
                for (const auto& [id, value] : f.values)
                    setParam (id, value);
                setCurrentName (name);
                return true;
            }
        }

        if (auto xml = juce::parseXML (fileFor (name)))
        {
            for (auto* p : apvts.processor.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                    if (xml->hasAttribute (rp->paramID) && ! songSpecific (rp->paramID))
                        setParam (rp->paramID, (float) xml->getDoubleAttribute (rp->paramID));
            for (int k = 0; k < 5; ++k)                                  // main shape, MAP's snare shape, the bass kinds
                if (auto* c = xml->getChildByName (tagOf (k)))
                {
                    auto shape = apvts.state.getOrCreateChildWithName (tagOf (k), nullptr);
                    shape.removeAllChildren (nullptr);
                    for (const auto& n : juce::ValueTree::fromXml (*c)) shape.appendChild (n.createCopy(), nullptr);
                    apvts.state.setProperty (keyOf (k), xml->getStringAttribute (keyOf (k), "Custom"), nullptr);
                }
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

        juce::XmlElement xml ("RecoilPreset");
        xml.setAttribute ("name", name);
        xml.setAttribute ("description", description.trim().substring (0, kMaxDescription));
        for (auto* p : apvts.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                xml.setAttribute (rp->paramID, (double) rp->convertFrom0to1 (rp->getValue()));
        for (int k = 0; k < 5; ++k)
        {
            if (auto shape = apvts.state.getChildWithName (tagOf (k)); shape.isValid() && shape.getNumChildren() > 0)
                if (auto e = shape.createXml()) { e->setTagName (tagOf (k)); xml.addChildElement (e.release()); }
            xml.setAttribute (keyOf (k), apvts.state.getProperty (keyOf (k), "Custom").toString());
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
    };

    // the plain setup the other factory presets are written against
    static const std::vector<std::pair<juce::String, float>>& classicBase()
    {
        static const std::vector<std::pair<juce::String, float>> v {
            { "dynamic", 0 }, { "fit", 0 }, { "length", 300 }, { "band", 0 }, { "bandHi", 150 }, { "above", 0 } };
        return v;
    }

    static float shape (const char* name) { return (float) shapes::choiceNames().indexOf (name); }

    // "Default" is the startup setup (the parameter defaults): AUDIO trigger, KICK LOWS at -40 dB (hits must also be within
    // 20 dB of the recent kicks, so any send level works), GROOVE LOCK, FIT LENGTH (250 ms until the first kick is measured),
    // DYNAMIC 50 %, Punch-Through at full DEPTH, ATTACK 4 ms, SPLIT below 180 Hz with ABOVE at 0 % (the rest untouched), PHASE off, MIX 100 %.
    // Every other preset starts from classicBase() and lists only what differs from it.
    // trigger: 0 SYNC, 1 MIDI, 2 AUDIO, 3 MAP. mapGhost: 0 IGNORE, 1 SCALE, 2 FULL. rate: 0 1/1 .. 5 1/32. band: 1 SPLIT. phase: 0 AUTO, 1 MANUAL, 2 OFF.
    static const std::vector<Factory>& factory()
    {
        static const std::vector<Factory> presets {
            { "Default", "Slap it on, send the kick: fits itself to most kicks", {} },
            { "Kick -> 808", "Low end cleared for every kick, length fitted to the kick",
              { { "band", 1 }, { "bandHi", 150 }, { "fit", 1 } } },
            { "Kick -> Bass", "A firm duck under the kick, the bass's top stays",
              { { "shape", shape ("Hold") }, { "depth", 90 }, { "length", 260 }, { "band", 1 }, { "bandHi", 220 } } },
            { "BLACKOUT", "Total silence behind every kick, then straight back",
              { { "shape", shape ("Blackout") }, { "fit", 1 } } },
            { "808 Tail", "Clears the hit, keeps the 808 under the kick's body, phase-aligned",
              { { "shape", shape ("808 Tail") }, { "fit", 1 }, { "band", 1 }, { "bandHi", 160 }, { "phase", 0 } } },
            { "Recoil Bounce", "The bass hits back right after each kick",
              { { "shape", shape ("Recoil") }, { "length", 360 } } },
            { "Dynamic Kick", "Ghost kicks and fills duck less than the main hits",
              { { "dynamic", 70 } } },
            { "Gentle Glue", "Just enough room for the kick, barely noticeable",
              { { "shape", shape ("Smooth") }, { "depth", 45 }, { "length", 220 }, { "attack", 3 } } },
            { "Pump 1/4", "The classic four-on-the-floor pump, no sidechain needed",
              { { "trigger", 0 }, { "shape", shape ("Hold Long") } } },
            { "Pump 1/8", "Faster pumping on the eighths",
              { { "trigger", 0 }, { "rate", 3 }, { "shape", shape ("Punch-Through") } } },
            { "Gate 1/16", "Chopped sixteenths for pads and chords",
              { { "trigger", 0 }, { "rate", 4 }, { "shape", shape ("Pulse") } } },
            { "Swell 1/2", "Pads that swell in between the hits",
              { { "trigger", 0 }, { "rate", 1 }, { "shape", shape ("Swell Gate") } } },
            { "Drum Bus Map", "Load your bounced drum bus (and the bass bus: a shape per kind of bass): kicks duck fully, snares half, ghost notes a little. BAND reaches 500 Hz for the snares",
              { { "trigger", 3 }, { "dynamic", 50 }, { "fit", 1 }, { "band", 1 }, { "bandHi", 500 }, { "bassMode", 1 },
                { "mapKick", 100 }, { "mapSnare", 50 }, { "mapRim", 0 }, { "mapGhost", 1 }, { "snareShape", 7 } } },
            { "MIDI Duck", "Triggered by MIDI notes; velocity sets how deep",
              { { "trigger", 1 }, { "dynamic", 100 }, { "length", 350 } } },
            { "Vocal Space", "Mids give way to the vocal's consonants, highs and lows untouched",
              { { "shape", shape ("Smooth") }, { "listen", 1 }, { "depth", 60 }, { "length", 180 }, { "attack", 2 },
                { "band", 1 }, { "bandLo", 800 }, { "bandHi", 5000 } } },
            { "Snare Click Gap", "A short gap in the mids for the snare's crack",
              { { "shape", shape ("Click Gap") }, { "listen", 1 }, { "length", 250 }, { "band", 1 }, { "bandLo", 300 }, { "bandHi", 6000 } } },
        };
        return presets;
    }

    static juce::String extension() { return ".rcpreset"; }
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
