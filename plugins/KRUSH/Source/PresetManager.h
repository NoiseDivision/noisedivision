#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "KrushDSP.h"
#include <map>
#include <utility>
#include <vector>

// Factory presets (built in) plus user presets saved as small XML files in
//   Windows: %APPDATA%\KRUSH\Presets
//   macOS:   ~/Library/Application Support/KRUSH/Presets
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
        base = base.getChildFile ("Application Support");     // ~/Library/Application Support
       #endif
        return base.getChildFile ("KRUSH").getChildFile ("Presets");
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

    // one line shown under the name in the preset list
    static juce::String getDescription (const juce::String& name)
    {
        static const std::map<juce::String, juce::String> text {
            { "Default",       "Clean start: three bands, no distortion" },
            { "Warm Glue",     "Gentle tube and saturation glue for mixes and buses" },
            { "Bass Crunch",   "Punchy overdriven lows below 200 Hz, everything above clean" },
            { "Top Grit",      "Hard-clipped highs for bite, lows and mids clean" },
            { "Lo-Fi Crush",   "Sample-rate and bit crush, dark and gritty" },
            { "Fold Madness",  "Sine and hard-clip folding for wild, metallic tones" },
            { "Full Krush",    "The signature Krush curve on every band, kick punch kept" },
            { "Diode Fuzz",    "Fuzzy, sustained diode drive on everything above 300 Hz" },
            { "Clean Tape",    "Subtle tape glue: low bump, softer top, rounded peaks" },
            { "Tube Amp",      "Driven tube mids, cabinet-style roll-off. Guitars and synths" },
            { "Hip Hop Punch", "Fatter, louder drums with extra snap. Output clipper on" },
        };
        const auto it = text.find (name);
        if (it != text.end())
            return it->second;
        const auto own = getUserDescription (name);                 // user presets: their own description
        return own.isNotEmpty() ? own : juce::String ("Your preset");
    }

    // the description saved with a user preset (empty if it has none)
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
                // every control back to its default, then the preset's own settings
                for (auto* p : apvts.processor.getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                        setNormalised (*rp, rp->getDefaultValue());
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
                    if (xml->hasAttribute (rp->paramID))
                        setParam (rp->paramID, (float) xml->getDoubleAttribute (rp->paramID));
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
            name << " (User)";            // factory presets can't be overwritten

        juce::XmlElement xml ("KrushPreset");
        xml.setAttribute ("name", name);
        xml.setAttribute ("description", description.trim().substring (0, kMaxDescription));
        for (auto* p : apvts.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                xml.setAttribute (rp->paramID, (double) rp->convertFrom0to1 (rp->getValue()));

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
    struct Factory { juce::String name; std::vector<std::pair<juce::String, float>> values; };

    // Each factory preset lists only what differs from the defaults.
    // Bands not mentioned keep the defaults: mode None, crossovers 120 Hz / 2 kHz / 6 kHz.
    static const std::vector<Factory>& factory()
    {
        static const std::vector<Factory> presets {
            { "Default", {} },
            { "Warm Glue", { { "bands", 2 }, { "b1_mode", krush::Tube }, { "b1_drive", 6 }, { "b1_shape", 40 }, { "b1_dyn", -10 },
                             { "b2_mode", krush::Saturation }, { "b2_drive", 6 }, { "b2_gain", -1.5f }, { "b2_dyn", -10 },
                             { "b3_mode", krush::SoftClip }, { "b3_drive", 4 }, { "b3_shape", 30 }, { "output", -2 } } },
            { "Bass Crunch", { { "bands", 1 }, { "x1", 200 }, { "b1_mode", krush::Overdrive }, { "b1_drive", 18 }, { "b1_shape", 60 }, { "b1_dyn", 30 },
                               { "b1_gain", -6 }, { "b2_mode", krush::None } } },
            { "Top Grit", { { "bands", 2 }, { "x1", 150 }, { "x2", 3000 }, { "b1_mode", krush::None }, { "b2_mode", krush::None },
                            { "b3_mode", krush::HardClip }, { "b3_drive", 18 }, { "b3_shape", 30 }, { "b3_gain", -8 }, { "b3_mix", 60 }, { "b3_dyn", -25 } } },
            { "Lo-Fi Crush", { { "bands", 0 }, { "b1_mode", krush::Downsample }, { "b1_drive", 18 }, { "b1_shape", 60 }, { "b1_dyn", -30 },
                               { "lpf", 8000 }, { "mix", 70 } } },
            { "Fold Madness", { { "bands", 2 }, { "b1_mode", krush::None }, { "b2_mode", krush::SineShaper }, { "b2_drive", 14 }, { "b2_shape", 60 },
                                { "b2_gain", -8 }, { "b3_mode", krush::HardClip }, { "b3_drive", 16 }, { "b3_shape", 70 }, { "b3_gain", -8 } } },
            { "Full Krush", { { "bands", 2 }, { "b1_mode", krush::Krush }, { "b1_drive", 10 }, { "b1_shape", 35 }, { "b1_gain", -8 }, { "b1_dyn", 40 },
                              { "b2_mode", krush::Krush }, { "b2_drive", 14 }, { "b2_shape", 55 }, { "b2_gain", -10 },
                              { "b3_mode", krush::Krush }, { "b3_drive", 12 }, { "b3_shape", 45 }, { "b3_gain", -12 },
                              { "lpf", 12000 }, { "toneSlope", 3 } } },
            { "Diode Fuzz", { { "bands", 1 }, { "x1", 300 }, { "b1_mode", krush::None }, { "b2_mode", krush::Diode }, { "b2_drive", 24 }, { "b2_dyn", -30 },
                              { "b2_shape", 70 }, { "b2_gain", -10 }, { "lpf", 9000 } } },
            // gentle tape-style glue: a little low-end bump, softened top, rounded transients, about level-matched
            { "Clean Tape", { { "bands", 2 }, { "b1_mode", krush::Saturation }, { "b1_drive", 5 }, { "b1_bias", 10 }, { "b1_gain", -2.5f },
                              { "b2_mode", krush::Saturation }, { "b2_drive", 3 }, { "b2_gain", -3 }, { "b2_dyn", -20 },
                              { "b3_mode", krush::SoftClip }, { "b3_drive", 3 }, { "b3_gain", -4.5f }, { "b3_dyn", -30 },
                              { "hpf", 25 }, { "lpf", 15000 }, { "toneSlope", 1 } } },
            // amp-style: tight lows, driven tube mids, tamed fizz, cabinet-like 24 dB/oct roll-off
            { "Tube Amp", { { "bands", 2 }, { "x1", 150 }, { "x2", 2500 },
                            { "b1_mode", krush::Tube }, { "b1_drive", 6 }, { "b1_gain", -4 },
                            { "b2_mode", krush::Tube }, { "b2_drive", 18 }, { "b2_shape", 60 }, { "b2_bias", 20 }, { "b2_gain", -12 }, { "b2_dyn", -15 },
                            { "b3_mode", krush::Overdrive }, { "b3_drive", 10 }, { "b3_gain", -14 },
                            { "hpf", 80 }, { "lpf", 6500 }, { "toneSlope", 3 } } },
            // drums: fat saturated low end, soft-clipped snap in the mids, crisp top, output clipper on for loudness
            { "Hip Hop Punch", { { "bands", 2 }, { "x1", 100 }, { "x2", 3000 },
                                 { "b1_mode", krush::Tube }, { "b1_drive", 8 }, { "b1_gain", -2 }, { "b1_dyn", 30 },
                                 { "b2_mode", krush::SoftClip }, { "b2_drive", 9 }, { "b2_shape", 40 }, { "b2_gain", -5 }, { "b2_mix", 80 }, { "b2_dyn", 40 },
                                 { "b3_mode", krush::Saturation }, { "b3_drive", 5 }, { "b3_gain", -2.5f },
                                 { "hpf", 30 }, { "toneSlope", 3 }, { "output", 3 }, { "clipOn", 1 } } },
        };
        return presets;
    }

    static juce::String extension() { return ".kpreset"; }
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
