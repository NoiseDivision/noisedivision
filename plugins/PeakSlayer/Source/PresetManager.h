#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

// Factory presets (built in) plus user presets saved as small XML files in
//   Windows: %APPDATA%\PeakSlayer\Presets
//   macOS:   ~/Library/Application Support/PeakSlayer/Presets
// The current preset name is kept in the plugin state, so it is restored with the project.
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

    static juce::File getUserDir()
    {
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        base = base.getChildFile ("Application Support");     // ~/Library/Application Support
       #endif
        return base.getChildFile ("PeakSlayer").getChildFile ("Presets");
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

    juce::String getCurrentName() const
    {
        return apvts.state.getProperty ("presetName", "Default").toString();
    }

    bool load (const juce::String& name)
    {
        for (const auto& f : factory())
        {
            if (name == f.name)
            {
                setParam ("clip",  f.clip11 / 11.0f);
                setParam ("shape", f.shape);
                setParam ("in",    f.inDb);
                setParam ("out",   f.outDb);
                setParam ("hpf",   20.0f);     // filters off
                setParam ("lpf",   20000.0f);
                setParam ("mix",   100.0f);    // fully wet
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
    juce::String save (const juce::String& requestedName)
    {
        auto name = juce::File::createLegalFileName (requestedName.trim());
        if (name.isEmpty())
            return {};
        if (isFactory (name))
            name << " (User)"; // factory presets can't be overwritten

        juce::XmlElement xml ("PeakSlayerPreset");
        xml.setAttribute ("name", name);

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
    struct Factory { const char* name; float clip11, shape, inDb, outDb; };

    static const std::array<Factory, 5>& factory()
    {
        static const std::array<Factory, 5> presets {{
            { "Default",     5.0f,  0.0f, 0.0f,  0.0f },
            { "Balanced",    6.5f,  0.0f, 0.0f,  0.0f },
            { "Soft Glue",   3.5f, -0.6f, 0.0f,  0.0f },
            { "Hard Slam",   8.0f,  0.7f, 0.0f,  0.0f },
            { "To Eleven",  11.0f,  1.0f, 0.0f, -3.0f },
        }};
        return presets;
    }

    static juce::String extension() { return ".pspreset"; }
    static juce::File fileFor (const juce::String& name) { return getUserDir().getChildFile (name + extension()); }

    void setCurrentName (const juce::String& name) { apvts.state.setProperty ("presetName", name, nullptr); }

    void setParam (const juce::String& id, float realValue)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (realValue));
            p->endChangeGesture();
        }
    }

    juce::AudioProcessorValueTreeState& apvts;
};
