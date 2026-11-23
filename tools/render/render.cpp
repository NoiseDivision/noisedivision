// Offline renderer: runs a WAV file through one Noise Division plugin's own processor.
//
//   <plugin>_render in.wav out.wav [--preset "Name"] [--set id=value ...] [--point freq=value ...] [--sidechain sc.wav] [--bpm 120]
//
// --point (SPECTRAKILL and SHRAPNEL) replaces the curve points: SPECTRAKILL takes dB, SHRAPNEL the
// drive amount from -1 (spare it) to +1 (hit it harder).
//
// --bpm makes the plugin see a song playing from bar 1 at that tempo in 4/4 (for tempo sync and groove learning).
//
// Values are in the parameter's own units (dB, %, Hz, or the index of a choice). The plugin's
// latency is removed, so the output lines up with the input sample for sample.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include PROC_HEADER

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

// a host transport that is playing from the start of the file
struct Transport : juce::AudioPlayHead
{
    double bpm = 120.0, sr = 48000.0; juce::int64 sample = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setBpm (bpm);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        p.setTimeInSamples (sample);
        p.setTimeInSeconds ((double) sample / sr);
        p.setPpqPosition ((double) sample / sr * bpm / 60.0);
        return p;
    }
};

static juce::AudioBuffer<float> readWav (const juce::File& f, double& sr)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { std::cerr << "can't read " << f.getFullPathName() << "\n"; std::exit (1); }
    juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    if (r->numChannels == 1) b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
    sr = r->sampleRate;
    return b;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::cerr << "usage: render in.wav out.wav [--preset name] [--set id=value] [--sidechain sc.wav]\n"; return 1; }
    double sr = 48000.0, scRate = 0.0;
    auto in = readWav (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]), sr);
    juce::AudioBuffer<float> sc;

    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    auto* typed = dynamic_cast<PROC_CLASS*> (proc.get());

    juce::StringArray sets, points; juce::String preset; double bpm = 0.0;
    for (int i = 3; i < argc; ++i)
    {
        juce::String a (argv[i]);
        if (a == "--preset" && i + 1 < argc) preset = argv[++i];
        else if (a == "--set" && i + 1 < argc) sets.add (argv[++i]);
        else if (a == "--point" && i + 1 < argc) points.add (argv[++i]);
        else if (a == "--bpm" && i + 1 < argc) bpm = juce::String (argv[++i]).getDoubleValue();
        else if (a == "--sidechain" && i + 1 < argc) sc = readWav (juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]), scRate);
    }

    // stereo in and out; a sidechain bus (if the plugin has one) gets the sidechain file or silence
    auto layout = proc->getBusesLayout();
    for (auto& s : layout.inputBuses)  s = juce::AudioChannelSet::stereo();
    for (auto& s : layout.outputBuses) s = juce::AudioChannelSet::stereo();
    proc->setBusesLayout (layout);
    const int block = 512;
    proc->setRateAndBufferSizeDetails (sr, block);

#if HAS_PRESETS
    if (preset.isNotEmpty() && ! typed->presets.load (preset)) { std::cerr << "no preset " << preset << "\n"; return 1; }
#else
    juce::ignoreUnused (typed);
    if (preset.isNotEmpty()) { std::cerr << "this plugin has no presets\n"; return 1; }
#endif
    for (auto& s : sets)
    {
        auto id = s.upToFirstOccurrenceOf ("=", false, false), v = s.fromFirstOccurrenceOf ("=", false, false);
        bool found = false;
        for (auto* p : proc->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->getParameterID() == id)
                {
                    rp->setValueNotifyingHost (rp->convertTo0to1 (v.getFloatValue()));
                    found = true;
                }
        if (! found) { std::cerr << "no parameter " << id << "\n"; return 1; }
    }

#ifdef CURVE_PROP
    if (! points.isEmpty())
    {
        auto curve = typed->apvts.state.getOrCreateChildWithName ("CURVE", nullptr);
        curve.removeAllChildren (nullptr);
        for (auto& pt : points)
        {
            juce::ValueTree t ("POINT");
            t.setProperty ("freq", pt.upToFirstOccurrenceOf ("=", false, false).getFloatValue(), nullptr);
            t.setProperty (CURVE_PROP, pt.fromFirstOccurrenceOf ("=", false, false).getFloatValue(), nullptr);
            curve.appendChild (t, nullptr);
        }
    }
#else
    if (! points.isEmpty()) { std::cerr << "this plugin has no curve points\n"; return 1; }
#endif

    Transport transport; transport.bpm = bpm; transport.sr = sr;
    if (bpm > 0.0) proc->setPlayHead (&transport);
    proc->prepareToPlay (sr, block);
    const int latency = proc->getLatencySamples();
    const int total = in.getNumSamples() + latency;
    const int chans = juce::jmax (proc->getTotalNumInputChannels(), proc->getTotalNumOutputChannels());
    juce::AudioBuffer<float> out (2, total), io (chans, block);
    juce::MidiBuffer midi;

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        io.setSize (chans, n, false, false, true);
        io.clear();
        for (int c = 0; c < 2; ++c)
            if (pos < in.getNumSamples())
                io.copyFrom (c, 0, in, c, pos, juce::jmin (n, in.getNumSamples() - pos));
        if (chans >= 4 && sc.getNumSamples() > 0)
            for (int c = 0; c < 2; ++c)
                if (pos < sc.getNumSamples())
                    io.copyFrom (2 + c, 0, sc, c, pos, juce::jmin (n, sc.getNumSamples() - pos));
        transport.sample = pos;
        proc->processBlock (io, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, io, c, 0, n);
    }
    proc->releaseResources();

    juce::AudioBuffer<float> trimmed (2, in.getNumSamples());
    for (int c = 0; c < 2; ++c) trimmed.copyFrom (c, 0, out, c, latency, in.getNumSamples());

    juce::File of = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    of.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (of), sr, 2, 32, {}, 0));
    w->writeFromAudioSampleBuffer (trimmed, 0, trimmed.getNumSamples());
    std::cout << "latency " << latency << " samples\n";
    return 0;
}
