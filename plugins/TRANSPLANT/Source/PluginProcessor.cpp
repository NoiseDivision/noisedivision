#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const char* const kParamIds[] = { "amount", "smooth", "range", "from", "to", "keyMode", "keyShift", "keyZone", "phase", "follow", "oneCurve", "out", "bypass", "target", "fullness" };
    juce::String fmtPoints (const std::vector<tp::Node>& v)
    {
        juce::StringArray s; for (auto& n : v) s.add (juce::String (n.bin, 2) + ":" + juce::String (n.db, 2));
        return s.joinIntoString (";");
    }
    std::vector<tp::Node> parsePoints (const juce::String& str)
    {
        std::vector<tp::Node> v;
        for (auto& t : juce::StringArray::fromTokens (str, ";", ""))
            if (t.containsChar (':')) v.push_back ({ t.upToFirstOccurrenceOf (":", false, false).getFloatValue(), t.fromFirstOccurrenceOf (":", false, false).getFloatValue(), true });
        return v;
    }
    std::vector<float> parseList (const juce::String& str)
    {
        std::vector<float> v; for (auto& t : juce::StringArray::fromTokens (str, ";", "")) if (t.isNotEmpty()) v.push_back (t.getFloatValue());
        return v;
    }
}

TransplantProcessor::TransplantProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      Thread ("TRANSPLANT analysis"),
      apvts (*this, &undo, "PARAMS", createLayout())
{
    for (auto* id : kParamIds) apvts.addParameterListener (id, this);
    apvts.state.addListener (this);
    cacheEdits();
    startThread (juce::Thread::Priority::low);
}

TransplantProcessor::~TransplantProcessor()
{
    stopThread (5000);
    apvts.state.removeListener (this);
    for (auto* id : kParamIds) apvts.removeParameterListener (id, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout TransplantProcessor::createLayout()
{
    using F = juce::AudioParameterFloat; using Attr = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return juce::ParameterID { s, 1 }; };
    auto minus = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    auto db = Attr().withLabel ("dB").withStringFromValueFunction ([minus] (float v, int) { return (v > 0.05f ? "+" : v < -0.05f ? minus : "") + juce::String (std::abs (v), 1) + " dB"; });
    auto pct = Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100)) + "%"; });
    auto hz = Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return v >= 1000 ? juce::String (v / 1000.0f, v >= 10000 ? 0 : 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; });
    auto oct = Attr().withStringFromValueFunction ([] (float v, int)
    {
        const double o = tp::smoothOctaves (v);
        return o >= 0.95 ? juce::String (o, 1) + " oct" : "1/" + juce::String (juce::roundToInt (1.0 / o)) + " oct";
    });
    auto rng = Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (juce::roundToInt (v)) + " dB"; });
    auto skew = [] (float a, float b, float c, float step) { juce::NormalisableRange<float> r (a, b, step); r.setSkewForCentre (c); return r; };
    l.add (std::make_unique<F> (id ("amount"), "Amount", juce::NormalisableRange<float> (0.0f, 1.5f, 0.01f), 1.0f, pct));
    l.add (std::make_unique<F> (id ("smooth"), "Smooth", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f, oct));
    l.add (std::make_unique<F> (id ("range"), "Range", juce::NormalisableRange<float> (3.0f, 24.0f, 1.0f), 12.0f, rng));
    l.add (std::make_unique<F> (id ("from"), "From", skew (20.0f, 2000.0f, 150.0f, 1.0f), 20.0f, hz));
    l.add (std::make_unique<F> (id ("to"), "To", skew (200.0f, 20000.0f, 2500.0f, 1.0f), 20000.0f, hz));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("keyMode"), "Key Mode", juce::StringArray { "Auto", "Manual", "Off" }, 0));
    l.add (std::make_unique<juce::AudioParameterInt> (id ("keyShift"), "Key Shift", -6, 6, 0,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + juce::String (v) + " st"; })));
    l.add (std::make_unique<F> (id ("keyZone"), "Key Zone", skew (80.0f, 1000.0f, 300.0f, 1.0f), 300.0f, hz));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("phase"), "Phase", juce::StringArray { "Linear", "Natural" }, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (id ("follow"), "Follow Playhead", true));
    l.add (std::make_unique<juce::AudioParameterBool> (id ("oneCurve"), "One Curve For All", false));
    l.add (std::make_unique<F> (id ("out"), "Section Gain", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<F> (id ("inGain"), "Input", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<F> (id ("outGain"), "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<juce::AudioParameterBool> (id ("bypass"), "Bypass", false));
    l.add (std::make_unique<juce::AudioParameterBool> (id ("listen"), "Listen To Reference", false));
    l.add (std::make_unique<juce::AudioParameterBool> (id ("levelMatch"), "Level Match", true));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("target"), "Match To", juce::StringArray { "Reference", "EDM Balance" }, 0));
    l.add (std::make_unique<F> (id ("fullness"), "Fullness", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f, pct));
    {   // HEAR ON: what you hear, never what you export
        juce::StringArray places; for (auto* n : tp::place::names) places.add (n);
        l.add (std::make_unique<juce::AudioParameterChoice> (id ("hearOn"), "Hear On", places, 0));
    }
    return l;
}

bool TransplantProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == l.getMainInputChannelSet();
}

//============================================================================== audio
std::unique_ptr<TransplantProcessor::Program> TransplantProcessor::flatProgram (bool linear) const
{
    auto p = std::make_unique<Program>();
    p->linear = linear;
    tp::Spec flat {};
    p->curves.push_back (flat);
    p->filters.push_back (conv.makeFilter (tp::designIR (flat, rate.load(), L, linear)));
    p->whole = 0;
    return p;
}

void TransplantProcessor::prepareToPlay (double sampleRate, int blockSize)
{
    const juce::ScopedLock sl (programLock);
    rate.store (sampleRate);
    L = tp::firLength (sampleRate); B = 256;
    conv.prepare (B, L / B);
    dryLen = L / 2 + B + 65536;   // the latency plus room for a whole host block written ahead
    dryL.assign ((size_t) dryLen, 0.0f); dryR.assign ((size_t) dryLen, 0.0f); dryPos = 0;
    outGain.reset (sampleRate, 0.05); outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("out")->load()));
    inTrim.reset (sampleRate, 0.03); inTrim.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("inGain")->load()));
    outTrim.reset (sampleRate, 0.03); outTrim.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("outGain")->load()));
    wet.reset (sampleRate, 0.03); wet.setCurrentAndTargetValue (apvts.getRawParameterValue ("bypass")->load() > 0.5f ? 0.0f : 1.0f);
    lastFilter = nullptr;
    refL.assign (16384, 0.0f); refR.assign (16384, 0.0f);
    xfade.reset (sampleRate, 0.03); xfade.setCurrentAndTargetValue (apvts.getRawParameterValue ("listen")->load() > 0.5f ? 1.0f : 0.0f);
    refGain.reset (sampleRate, 0.05); refGain.setCurrentAndTargetValue (1.0f);
    refActive = false;
    for (int c = 0; c < 2; ++c) { kwY[c].prepare (sampleRate); kwR[c].prepare (sampleRate); }
    for (int i = 0; i < kMaxViews; ++i) { yMeter[i].prepare (sampleRate); rMeter[i].prepare (sampleRate); }
    liveY.prepare (sampleRate); liveR.prepare (sampleRate); meterView = -2;
    hearConv.prepare ({ sampleRate, (juce::uint32) juce::jmax (1, blockSize), 2 }); hearConv.reset(); hearDry.setSize (2, juce::jmax (1, blockSize) * 2);
    hearLoaded = -1; hearMix.reset (sampleRate, 0.03); hearMix.setCurrentAndTargetValue (0.0f);
    hearJob = true; notify();
    const bool linear = apvts.getRawParameterValue ("phase")->load() < 0.5f;
    if (preparedRate.load() != sampleRate || live_.load() == nullptr)
    {
        // a flat filter right away (correct latency), the real ones follow from the thread
        auto p = flatProgram (linear);
        live_.store (p.get()); inUse.store (nullptr);
        programs.clear();
        programs.push_back (std::move (p));
        preparedRate.store (sampleRate);
        dirty = true; notify();
    }
    setLatencySamples (linear ? L / 2 + B : B);
}

void TransplantProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() > 8192)   // very large host blocks: in pieces (the reference buffers hold 16384)
    {
        for (int pos = 0; pos < buffer.getNumSamples(); pos += 8192)
        {
            const int len = juce::jmin (8192, buffer.getNumSamples() - pos);
            juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, len);
            juce::MidiBuffer m; processBlock (part, m);
        }
        return;
    }
    const int n = buffer.getNumSamples(), nCh = juce::jmin (2, buffer.getNumChannels());
    if (n == 0 || nCh == 0) return;
    for (int c = nCh; c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
    float* l = buffer.getWritePointer (0);
    float* r = nCh > 1 ? buffer.getWritePointer (1) : nullptr;
    // INPUT: before everything (what TRANSPLANT learns, filters and bypasses)
    inTrim.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("inGain")->load()));
    if (inTrim.isSmoothing() || std::abs (inTrim.getTargetValue() - 1.0f) > 1.0e-6f)
        for (int i = 0; i < n; ++i) { const float gi = inTrim.getNextValue(); l[i] *= gi; if (r) r[i] *= gi; }
    outTrim.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("outGain")->load()));

    // where we are in the song
    double t = playTime.load(); bool isPlaying = false, hasTime = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto ts = pos->getTimeInSeconds()) { t = *ts; hasTime = true; }
            isPlaying = pos->getIsPlaying();
        }
    playTime.store (t); playing.store (isPlaying);

    // LEARN: hand the (mono) input to the analysis thread, with its song time
    if (learning.load() && isPlaying && hasTime)
    {
        int s1, z1, s2, z2;
        if (fifo.getFreeSpace() >= n && stampFifo.getFreeSpace() >= 1)
        {
            fifo.prepareToWrite (n, s1, z1, s2, z2);
            for (int i = 0; i < z1; ++i) fifoBuf[(size_t) (s1 + i)] = r ? 0.5f * (l[i] + r[i]) : l[i];
            for (int i = 0; i < z2; ++i) fifoBuf[(size_t) (s2 + i)] = r ? 0.5f * (l[z1 + i] + r[z1 + i]) : l[z1 + i];
            fifo.finishedWrite (z1 + z2);
            stampFifo.prepareToWrite (1, s1, z1, s2, z2);
            if (z1 > 0) stamps[(size_t) s1] = { n, t }; else if (z2 > 0) stamps[(size_t) s2] = { n, t };
            stampFifo.finishedWrite (1);
        }
    }

    // which filter: the section playing now, the one picked, or the whole-track one
    Program* p = live_.load();
    inUse.store (p);
    const tp::Convolver::Filter* f = nullptr;
    bool linear = true; float outDb = apvts.getRawParameterValue ("out")->load();
    if (p != nullptr && ! p->filters.empty())
    {
        linear = p->linear;
        int idx = p->whole;
        const bool one = apvts.getRawParameterValue ("oneCurve")->load() > 0.5f;
        const bool follow = apvts.getRawParameterValue ("follow")->load() > 0.5f;
        const int nSec = (int) p->t0.size();
        if (! one && nSec > 0)
        {
            if (follow && hasTime)
            {
                idx = nSec - 1;
                for (int i = 0; i < nSec; ++i) if (t < p->t1[(size_t) i]) { idx = i; break; }
            }
            else
            {
                const int s = selectedView.load();
                idx = s >= 0 && s < nSec ? s : p->whole;
            }
        }
        activeView.store (idx == p->whole ? -1 : idx);
        f = &p->filters[(size_t) idx];
        if (idx < (int) p->outDb.size()) outDb = p->outDb[(size_t) idx];
    }
    // keep the input for the bypass path (the convolver works in place)
    for (int i = 0; i < n; ++i) { const int k = (dryPos + i) % dryLen; dryL[(size_t) k] = l[i]; dryR[(size_t) k] = r ? r[i] : l[i]; }
    conv.process (l, r, n, f);

    // bypass (crossfaded, latency kept) and output
    const int lat = linear ? L / 2 + B : B;
    wet.setTargetValue (apvts.getRawParameterValue ("bypass")->load() > 0.5f ? 0.0f : 1.0f);
    outGain.setTargetValue (juce::Decibels::decibelsToGain (outDb));
    // LISTEN: the reference (its matching part, level-matched), crossfaded with your track
    const bool listen = apvts.getRawParameterValue ("listen")->load() > 0.5f && ! edmActive.load();
    xfade.setTargetValue (listen ? 1.0f : 0.0f);
    const bool refOn = listen || xfade.getCurrentValue() > 0.0f;
    // the moment you hear now is the latency behind the playhead: the reference follows that moment and its section
    const double tHeard = t - lat / rate.load();
    int idxNow = -1;
    if (p != nullptr && ! p->filters.empty())
    {
        idxNow = activeView.load() < 0 ? p->whole : activeView.load();
        const bool follow = apvts.getRawParameterValue ("follow")->load() > 0.5f, one = apvts.getRawParameterValue ("oneCurve")->load() > 0.5f;
        const int nSec = (int) p->t0.size();
        if (follow && ! one && hasTime && nSec > 0)
        {
            idxNow = nSec - 1;
            for (int i = 0; i < nSec; ++i) if (tHeard < p->t1[(size_t) i]) { idxNow = i; break; }
        }
    }
    // the reference always plays along (silently unless you LISTEN), so both can be metered side by side
    renderRef (n, tHeard, isPlaying, p, idxNow);
    // a new curve changes your level, another reference its level: measure again
    if (p != measuredFor) { for (auto& m : yMeter) m.reset(); measuredFor = p; }
    if (refAudio.load() != measuredRef) { for (auto& m : rMeter) m.reset(); measuredRef = refAudio.load(); }
    {
        float md = 0;
        if (p != nullptr && idxNow >= 0 && idxNow < (int) p->mdb.size()) md = p->mdb[(size_t) idxNow] + (idxNow < (int) p->outDb.size() ? p->outDb[(size_t) idxNow] : 0.0f) + apvts.getRawParameterValue ("outGain")->load();
        // once both have been heard for 2 s in this section, the measured difference takes over from the estimate
        if (idxNow >= 0 && idxNow < kMaxViews && yMeter[idxNow].valid() && rMeter[idxNow].valid())
            md = (float) juce::jlimit (-30.0, 30.0, yMeter[idxNow].db() - rMeter[idxNow].db());
        const float target = apvts.getRawParameterValue ("levelMatch")->load() > 0.5f ? md : 0.0f;
        refGain.setTargetValue (juce::Decibels::decibelsToGain (target));
        refGainShown.store (target);
    }
    const bool measureY = isPlaying && wet.getCurrentValue() >= 1.0f && apvts.getRawParameterValue ("bypass")->load() < 0.5f, measureR = isPlaying && refActive;
    if (idxNow != meterView || peakReset.exchange (false)) { liveY.resetPeak(); liveR.resetPeak(); meterView = idxNow; }
    double yP = 0, rP = 0;
    refAudible.store (listen && refActive);
    int w = liveWrite.load();
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue() * outTrim.getNextValue(), m = wet.getNextValue();
        const int rd = (dryPos - lat + dryLen * 4) % dryLen;
        float yl = l[i], yr = r ? r[i] : 0.0f;
        if (m < 1.0f) { yl = m * yl + (1 - m) * dryL[(size_t) rd]; if (r) yr = m * yr + (1 - m) * dryR[(size_t) rd]; }
        yl *= g; yr *= g;
        if (measureY) { const double a = kwY[0].run (yl), b = kwY[1].run (r ? yr : yl); yP += a * a + b * b; liveY.add (yl, r ? yr : yl); }
        if (measureR) { const double a = kwR[0].run (refL[(size_t) i]), b = kwR[1].run (refR[(size_t) i]); rP += a * a + b * b; liveR.add (refL[(size_t) i], refR[(size_t) i]); }
        const float x = xfade.getNextValue(), rg = refGain.getNextValue();
        if (refOn)
        {
            const float a = std::sqrt (1.0f - x), b = std::sqrt (x);           // equal-power crossfade
            yl = a * yl + b * rg * refL[(size_t) i];
            yr = a * yr + b * rg * refR[(size_t) i];
        }
        l[i] = yl; if (r) r[i] = yr;
        live[w] = r ? 0.5f * (l[i] + r[i]) : l[i]; w = (w + 1) % kLive;
        dryPos = (dryPos + 1) % dryLen;
    }
    liveWrite.store (w);
    applyHearOn (buffer, n);
    if (idxNow >= 0 && idxNow < kMaxViews)
    {
        if (measureY) yMeter[idxNow].add (yP, n);
        if (measureR) rMeter[idxNow].add (rP, n);
    }
    // publish the meters
    auto publish = [&] (MeterOut& o, const tp::LiveMeter& m, const tp::GatedLoudness* sec)
    {
        o.valid.store (m.valid());
        o.st.store ((float) m.shortTermLufs()); o.tp.store ((float) m.truePeakDb()); o.width.store ((float) m.widthPct());
        o.low.store ((float) m.bandDb (0)); o.mid.store ((float) m.bandDb (1)); o.high.store ((float) m.bandDb (2));
        o.sec.store (sec != nullptr && sec->valid() ? (float) (sec->db() - 0.691) : -120.0f);
    };
    const bool inRange = idxNow >= 0 && idxNow < kMaxViews;
    if (measureY) publish (meterYours, liveY, inRange ? &yMeter[idxNow] : nullptr);
    if (measureR) publish (meterRef, liveR, inRange ? &rMeter[idxNow] : nullptr);
}

// the reference, from the part that matches what's playing in your track: entering a section (or switching to REF)
// jumps to the same moment in the reference's matching part; it loops within that part.
void TransplantProcessor::renderRef (int n, double t, bool isPlaying, const Program* p, int idx)
{
    RefAudio* a = refAudio.load();
    refAudioInUse.store (a);
    std::fill (refL.begin(), refL.begin() + n, 0.0f); std::fill (refR.begin(), refR.begin() + n, 0.0f);
    if (a == nullptr || a->frames < 4 || ! isPlaying) { refActive = false; return; }
    const double fsr = a->rate, ratio = fsr / rate.load(), len = a->frames / fsr;
    double vt0 = 0, rt0 = 0, rt1 = len;
    if (p != nullptr && idx >= 0 && idx < (int) p->rt0.size()) { vt0 = p->vt0[(size_t) idx]; rt0 = p->rt0[(size_t) idx]; rt1 = juce::jmin (len, p->rt1[(size_t) idx]); }
    if (rt1 - rt0 < 0.5) { rt0 = 0; rt1 = len; }
    const bool jump = ! refActive || idx != refView || a != refLast || std::abs (t - refExpectT) > 0.05;
    if (jump)
    {
        refPos = (rt0 + std::fmod (juce::jmax (0.0, t - vt0), rt1 - rt0)) * fsr;
        refFade = 0;
    }
    const double end = rt1 * fsr, span = (rt1 - rt0) * fsr;
    const int fadeLen = juce::jmax (1, (int) (0.006 * rate.load()));
    const auto* d = a->d.data(); const juce::int64 F = a->frames;
    auto at = [d, F] (juce::int64 i, int c) { i = juce::jlimit<juce::int64> (0, F - 1, i); return d[(size_t) (i * 2 + c)] * (1.0f / 32768.0f); };
    for (int i = 0; i < n; ++i)
    {
        if (refPos >= end) { refPos -= span; refFade = 0; }
        const juce::int64 k = (juce::int64) refPos; const float fr = (float) (refPos - (double) k);
        const float g = juce::jmin (1.0f, refFade++ / (float) fadeLen);
        for (int c = 0; c < 2; ++c)
        {
            const float y0 = at (k - 1, c), y1 = at (k, c), y2 = at (k + 1, c), y3 = at (k + 2, c);
            const float v = y1 + 0.5f * fr * (y2 - y0 + fr * (2 * y0 - 5 * y1 + 4 * y2 - y3 + fr * (3 * (y1 - y2) + y3 - y0)));
            (c == 0 ? refL : refR)[(size_t) i] = v * g;
        }
        refPos += ratio;
    }
    refActive = true; refView = idx; refLast = a; refExpectT = t + n / rate.load();
}

void TransplantProcessor::setListenRef (int index)
{
    listenIdx.store (juce::jmax (0, index));
    listenJob = true; dirty = true; notify();
}

// the reference you listen to, decoded into memory (16-bit is plenty for listening)
void TransplantProcessor::decodeListen()
{
    juce::String path;
    {
        const juce::ScopedLock sl (modelLock);
        if (refs.empty()) { refAudio.store (nullptr); refAudioState = 0; return; }
        const int li = juce::jlimit (0, (int) refs.size() - 1, listenIdx.load());
        listenIdx.store (li);
        path = refs[(size_t) li].path;
    }
    const juce::File f (path);
    if (path.isEmpty() || ! f.existsAsFile()) { refAudio.store (nullptr); refAudioState = 3; return; }
    refAudioState = 1;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr) { refAudio.store (nullptr); refAudioState = 3; return; }
    auto a = std::make_unique<RefAudio>();
    a->rate = reader->sampleRate; a->frames = reader->lengthInSamples;
    a->d.resize ((size_t) a->frames * 2);
    const int chunk = 65536; juce::AudioBuffer<float> buf (2, chunk);
    for (juce::int64 pos = 0; pos < a->frames; pos += chunk)
    {
        if (threadShouldExit()) return;
        const int len = (int) juce::jmin ((juce::int64) chunk, a->frames - pos);
        reader->read (&buf, 0, len, pos, true, reader->numChannels > 1);
        for (int i = 0; i < len; ++i)
            for (int c = 0; c < 2; ++c)
                a->d[(size_t) ((pos + i) * 2 + c)] = (juce::int16) juce::jlimit (-32767, 32767, (int) std::lround (buf.getSample (reader->numChannels > 1 ? c : 0, i) * 32767.0f));
    }
    const juce::ScopedLock sl (programLock);
    if (auto* old = refAudio.load()) audioRetired[old] = juce::Time::getMillisecondCounter();
    refAudio.store (a.get());
    audios.push_back (std::move (a));
    refAudioState = 2;
}

//============================================================================== HEAR ON
// the system's curve as a minimum-phase filter (no latency), made on the thread
void TransplantProcessor::loadHearOn()
{
    const int want = (int) apvts.getRawParameterValue ("hearOn")->load();
    const double fs = rate.load();
    if (fs <= 0 || want == hearLoaded.load()) return;
    if (want > tp::place::off && want < tp::place::count)
    {
        auto spec = tp::place::curve (want);
        float top = -100; for (float x : spec) top = juce::jmax (top, x);
        hearBoost.store (top);
        const auto h = tp::designIR (spec, fs, 4096, false);
        juce::AudioBuffer<float> ir (1, (int) h.size());
        for (int i = 0; i < (int) h.size(); ++i) ir.setSample (0, i, h[(size_t) i]);
        hearConv.loadImpulseResponse (std::move (ir), fs, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    }
    hearLoaded.store (want);
}

void TransplantProcessor::applyHearOn (juce::AudioBuffer<float>& buffer, int n)
{
    const int want = (int) apvts.getRawParameterValue ("hearOn")->load();
    if (want != hearLoaded.load()) { hearJob = true; notify(); }
    // never in a bounce: offline rendering always hears the studio
    const int on = isNonRealtime() ? 0 : hearLoaded.load();
    hearMix.setTargetValue (on > tp::place::off ? 1.0f : 0.0f);
    if (on <= tp::place::off && ! hearMix.isSmoothing()) { if (hearMix.getCurrentValue() <= 0.0f) { hearActive = false; hearTrimDb.store (0.0f); return; } }
    if (buffer.getNumChannels() < 2) return;
    hearActive = true;
    hearDry.makeCopyOf (buffer, true);
    auto* l = buffer.getWritePointer (0); auto* r = buffer.getWritePointer (1);
    const float mono = on > 0 && on < tp::place::count ? (float) tp::place::mono[on] : 0.0f;
    for (int i = 0; i < n; ++i) { const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]) * (1.0f - mono); l[i] = m + s; r[i] = m - s; }
    juce::dsp::AudioBlock<float> blk (buffer.getArrayOfWritePointers(), 2, (size_t) n);
    hearConv.process (juce::dsp::ProcessContextReplacing<float> (blk));
    // a boosted low end needs room: an automatic trim that steps down at once to keep peaks under -1 dBFS,
    // holds for 3 s and comes back up at 1 dB a second. It never shapes the waveform, so nothing distorts.
    const float ceiling = 0.891f;
    const float up = (float) std::pow (10.0, 1.0 / 20.0 / rate.load());
    const int holdLen = (int) (3.0 * rate.load());
    if (on != hearTrimFor) { hearTrim = 1.0f; hearHold = 0; hearTrimFor = on; }
    for (int i = 0; i < n; ++i)
    {
        const float x = hearMix.getNextValue();
        const float a = x * l[i] + (1 - x) * hearDry.getSample (0, i), b = x * r[i] + (1 - x) * hearDry.getSample (1, i);
        const float pk = juce::jmax (std::abs (a), std::abs (b));
        if (hearBoost.load() <= 0.5f) hearTrim = 1.0f;          // nothing boosted (Mono): nothing to make room for, same level as the studio
        else if (pk * hearTrim > ceiling) { hearTrim = ceiling / pk; hearHold = holdLen; }
        else if (hearHold > 0) --hearHold;
        else hearTrim = juce::jmin (1.0f, hearTrim * up);
        l[i] = a * hearTrim; r[i] = b * hearTrim;
    }
    hearTrimDb.store (juce::Decibels::gainToDecibels (hearTrim));
}

//============================================================================== the thread
void TransplantProcessor::run()
{
    while (! threadShouldExit())
    {
        wait (40);
        drainLearn();
        juce::File job; bool asMine = false;
        {
            const juce::ScopedLock sl (jobLock);
            if (! refJobs.isEmpty()) job = refJobs.removeAndReturn (0);
            else if (mineJob != juce::File()) { job = mineJob; mineJob = juce::File(); asMine = true; }
        }
        if (job != juce::File()) analyseFile (job, asMine);
        if (learnFinish.exchange (false))
        {
            tp::TrackInfo t; t.slices = learner.slices();
            if (t.slices.size() >= 4)
            {
                tp::analyseTrack (t);
                const juce::ScopedLock sl (modelLock);
                mine = std::move (t); mineName = "learned from playback";
                dirty = true;
            }
            else { const juce::ScopedLock sl (modelLock); busy = "NOTHING LEARNED: PRESS LEARN, THEN PLAY YOUR TRACK"; }
        }
        if (listenJob.exchange (false)) decodeListen();
        if (hearJob.exchange (false)) loadHearOn();
        if (dirty.exchange (false)) rebuild();
        // free old filters once the audio thread has moved on
        {
            const juce::ScopedLock sl (programLock);
            const auto now = juce::Time::getMillisecondCounter();
            for (size_t i = 0; i + 2 < programs.size();)
            {
                auto* q = programs[i].get();
                if (q != live_.load() && q != inUse.load() && now - retiredAt[q] > 1500) { retiredAt.erase (q); programs.erase (programs.begin() + (long) i); }
                else ++i;
            }
            for (size_t i = 0; i < audios.size();)
            {
                auto* q = audios[i].get();
                if (q != refAudio.load() && q != refAudioInUse.load() && audioRetired.count (q) && now - audioRetired[q] > 1500) { audioRetired.erase (q); audios.erase (audios.begin() + (long) i); }
                else ++i;
            }
        }
    }
}

void TransplantProcessor::drainLearn()
{
    if (learnReset.exchange (false))
    {
        // throw away anything queued, start a fresh analysis at the current sample rate
        int s1, z1, s2, z2;
        fifo.prepareToRead (fifo.getNumReady(), s1, z1, s2, z2); fifo.finishedRead (z1 + z2);
        stampFifo.prepareToRead (stampFifo.getNumReady(), s1, z1, s2, z2); stampFifo.finishedRead (z1 + z2);
        learner.prepare (rate.load()); lastStampEnd = -1; learnedSecs.store (0.0);
        learning.store (true);
        return;
    }
    std::vector<float> tmp;
    while (stampFifo.getNumReady() > 0)
    {
        int s1, z1, s2, z2;
        stampFifo.prepareToRead (1, s1, z1, s2, z2);
        const Stamp st = z1 > 0 ? stamps[(size_t) s1] : stamps[(size_t) s2];
        stampFifo.finishedRead (1);
        if (fifo.getNumReady() < st.n) break;
        tmp.resize ((size_t) st.n);
        fifo.prepareToRead (st.n, s1, z1, s2, z2);
        std::copy (fifoBuf.begin() + s1, fifoBuf.begin() + s1 + z1, tmp.begin());
        std::copy (fifoBuf.begin() + s2, fifoBuf.begin() + s2 + z2, tmp.begin() + z1);
        fifo.finishedRead (z1 + z2);
        if (lastStampEnd < 0 || std::abs (st.t - lastStampEnd) > 0.05) learner.resetStream();   // a jump in the song
        learner.push (tmp.data(), st.n, st.t);
        lastStampEnd = st.t + st.n / rate.load();
    }
    learnedSecs.store (learner.seconds());
}

void TransplantProcessor::analyseFile (const juce::File& file, bool asMine)
{
    { const juce::ScopedLock sl (modelLock); busy = "ANALYSING " + file.getFileName().toUpperCase(); }
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < reader->sampleRate)
    {
        const juce::ScopedLock sl (modelLock); busy = "COULDN'T READ " + file.getFileName().toUpperCase();
        version++;
        return;
    }
    tp::SliceMaker m; m.prepare (reader->sampleRate);
    const int chunk = 65536, nc = (int) juce::jmin (2u, reader->numChannels);
    juce::AudioBuffer<float> buf (nc, chunk);
    std::vector<float> mono ((size_t) chunk);
    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += chunk)
    {
        if (threadShouldExit()) return;
        const int n = (int) juce::jmin ((juce::int64) chunk, reader->lengthInSamples - pos);
        reader->read (&buf, 0, n, pos, true, nc > 1);
        for (int i = 0; i < n; ++i) mono[(size_t) i] = nc > 1 ? 0.5f * (buf.getSample (0, i) + buf.getSample (1, i)) : buf.getSample (0, i);
        m.push (mono.data(), n, (double) pos / reader->sampleRate);
    }
    tp::TrackInfo t; t.slices = m.slices(); tp::analyseTrack (t);
    const juce::ScopedLock sl (modelLock);
    if (asMine) { mine = std::move (t); mineName = file.getFileName(); }
    else if (refs.size() < 3) { refs.push_back ({ file.getFileNameWithoutExtension(), file.getFullPathName(), std::move (t) }); if ((int) refs.size() - 1 == listenIdx.load() || refs.size() == 1) listenJob = true; }
    busy = {};
    dirty = true;
}

// views (what the display shows) and the filters
void TransplantProcessor::rebuild()
{
    auto P = [this] (const char* id) { return (double) apvts.getRawParameterValue (id)->load(); };
    const double zone = P ("keyZone");
    float glob[kMatchN]; for (int k = 0; k < kMatchN; ++k) glob[k] = (float) P (matchId (k));
    const int keyMode = (int) P ("keyMode"), manual = (int) std::lround (P ("keyShift"));
    std::map<juce::String, EditData> edits;
    { const juce::ScopedLock sl (editLock); edits = editCache; }
    tp::TrackInfo me; std::vector<RefTrack> rs; juce::String mname;
    { const juce::ScopedLock sl (modelLock); me = mine; rs = refs; mname = mineName; }

    Snapshot s;
    const bool edm = P ("target") > 0.5; s.edm = edm; edmActive.store (edm);
    s.mineName = mname; s.mineLength = me.length;
    s.mineKey = me.valid() ? juce::String (tp::keyName (me.key)) : juce::String(); s.mineConf = me.key.confidence;
    std::vector<int> shifts;
    for (auto& r : rs)
    {
        const int sh = keyMode == 0 ? tp::keyShift (me.key, r.info.key) : keyMode == 1 ? manual : 0;
        shifts.push_back (sh);
        Snapshot::Ref sr; sr.name = r.name; sr.key = tp::keyName (r.info.key); sr.conf = r.info.key.confidence; sr.shift = sh; sr.length = r.info.length;
        sr.path = r.path; sr.fileOk = r.path.isNotEmpty() && juce::File (r.path).existsAsFile();
        for (auto& sec : r.info.sections) sr.sections.add (sec.name());
        s.refs.push_back (sr);
    }
    // which part of a reference matches a section: the same kind and number, or the nearest stand-in
    auto resolve = [] (const tp::TrackInfo& r, const juce::String& want, int type, int index) -> const tp::Section&
    {
        if (want.isNotEmpty() && want != "AUTO")
        {
            for (auto& sec : r.sections) if (juce::String (sec.name()) == want) return sec;
            return r.whole;
        }
        static const int fallback[6][3] = { { tp::kIntro, tp::kBreak, tp::kOutro }, { tp::kBuild, tp::kBreak, tp::kIntro }, { tp::kDrop, tp::kDrop, tp::kDrop },
                                            { tp::kBreak, tp::kIntro, tp::kOutro }, { tp::kOutro, tp::kBreak, tp::kIntro }, { tp::kFull, tp::kFull, tp::kFull } };
        if (type == tp::kFull) return r.whole;
        for (int k : fallback[type])
        {
            const tp::Section* best = nullptr;
            for (auto& sec : r.sections) if (sec.type == k && (best == nullptr || (sec.index <= index && sec.index > best->index))) best = &sec;
            if (best) return *best;
        }
        return r.whole;
    };
    std::vector<const tp::Section*> mineSecs;
    if (me.valid()) { for (auto& sec : me.sections) mineSecs.push_back (&sec); mineSecs.push_back (&me.whole); }
    float loudest = -120; for (auto* sec : mineSecs) loudest = juce::jmax (loudest, sec->loud);
    for (auto* sec : mineSecs)
    {
        View v; v.name = sec == &me.whole ? juce::String ("WHOLE TRACK") : juce::String (sec->name());
        v.t0 = sec->t0; v.t1 = sec->t1;
        v.yours = tp::toDbSpec (sec->pow);
        const auto it = edits.find (v.name);
        const EditData ed = it != edits.end() ? it->second : EditData();
        for (int k = 0; k < kMatchN; ++k) v.match[k] = ed.has[k] ? ed.m[k] : glob[k];
        const double amount = v.match[0], smooth = v.match[1], range = v.match[2], from = v.match[3], to = v.match[4];
        if (edm)
        {
            // no reference: the middle of the EDM tonal balance range, for every section
            v.ref = tp::edm::target();
            {   // at your level in the mids, like a reference, so the two can be compared on the display
                double m = 0; int n = 0;
                for (int b = 0; b < tp::kBins; ++b) { const double f = tp::binHz (b); if (f > 100 && f < 6000) { m += v.yours[(size_t) b] - v.ref[(size_t) b]; ++n; } }
                for (auto& x : v.ref) x += (float) (m / juce::jmax (1, n));
            }
            v.refRaw = v.ref; v.hasRef = true;
            v.refLabel = "EDM BALANCE"; v.refT0 = v.refT1 = 0; v.matchDb = 0;
            const auto target = tp::smoothCurve (tp::difference (tp::tame (v.yours, tp::hzBin (150.0) + 12.0), v.ref), smooth);
            v.nodes = tp::mergeNodes (tp::extractNodes (target, smooth), ed.user, ed.removed, amount);
            for (auto& n : v.nodes) n.db = (float) juce::jlimit (-range, range, (double) n.db);
            v.curve = ed.bypass ? tp::Spec {} : tp::limitBand (tp::limitRange (tp::curveThrough (v.nodes), range), from, to);
        }
        else if (! rs.empty())
        {
            tp::Spec ref {}, raw {}; juce::String lbl;
            for (size_t i = 0; i < rs.size(); ++i)
            {
                const auto& rsec = resolve (rs[i].info, ed.target, sec->type, sec->index);
                if (i == 0) lbl = rsec.name();
                const auto db = tp::toDbSpec (rsec.pow), sh = tp::shiftLowEnd (db, shifts[i], zone);
                for (int b = 0; b < tp::kBins; ++b) { ref[(size_t) b] += sh[(size_t) b] / (float) rs.size(); raw[(size_t) b] += db[(size_t) b] / (float) rs.size(); }
            }
            v.ref = ref; v.refRaw = raw; v.hasRef = true;
            {
                const int li = juce::jlimit (0, (int) rs.size() - 1, listenIdx.load());
                const auto& lsec = resolve (rs[(size_t) li].info, ed.target, sec->type, sec->index);
                v.refT0 = lsec.t0; v.refT1 = lsec.t1 > lsec.t0 ? lsec.t1 : rs[(size_t) li].info.length;
                v.matchDb = sec->loud - lsec.loud;   // the EQ's own change in loudness is added below
            }
            v.refLabel = ed.target.isEmpty() || ed.target == "AUTO" ? "AUTO: " + lbl : lbl;
            const double tameFrom = tp::hzBin (keyMode == 2 ? 150.0 : zone) + 12.0;   // the key-matched low end keeps its notes
            const auto target = tp::smoothCurve (tp::difference (tp::tame (v.yours, tameFrom), tp::tame (ref, tameFrom)), smooth);
            v.nodes = tp::mergeNodes (tp::extractNodes (target, smooth), ed.user, ed.removed, amount);
            for (auto& n : v.nodes) n.db = (float) juce::jlimit (-range, range, (double) n.db);
            v.curve = ed.bypass ? tp::Spec {} : tp::limitBand (tp::limitRange (tp::curveThrough (v.nodes), range), from, to);
            v.matchDb = juce::jlimit (-30.0f, 30.0f, v.matchDb + (float) tp::eqLoudnessChange (sec->pow, v.curve));
        }
        // FULLNESS: give back what the ear loses at this section's level, against the loudest section (plus 6 dB, so the drop gets some too)
        if (! ed.bypass && v.match[6] > 0.001f)
        {
            const auto fc = tp::fullnessCurve (juce::jmax (0.0, (double) (loudest - sec->loud)) + 6.0);
            for (int b = 0; b < tp::kBins; ++b) v.full[(size_t) b] = fc[(size_t) b] * v.match[6];
            if (v.hasRef && ! edm)
            {
                tp::Spec both = v.curve; for (int b = 0; b < tp::kBins; ++b) both[(size_t) b] += v.full[(size_t) b];
                v.matchDb = juce::jlimit (-30.0f, 30.0f, v.matchDb + (float) (tp::eqLoudnessChange (sec->pow, both) - tp::eqLoudnessChange (sec->pow, v.curve)));
            }
        }
        s.views.push_back (v);
    }
    s.version = ++version;
    { const juce::ScopedLock sl (modelLock); snap = s; }
    buildProgram (false);
}

void TransplantProcessor::buildProgram (bool)
{
    const double fs = preparedRate.load();
    if (fs <= 0) return;
    Snapshot s = snapshot();
    const bool linear = apvts.getRawParameterValue ("phase")->load() < 0.5f;
    auto p = std::make_unique<Program>();
    p->linear = linear;
    Program* prev = live_.load();
    const juce::ScopedLock sl (programLock);
    if (s.views.empty()) { p = flatProgram (linear); }
    else
    {
        for (size_t i = 0; i < s.views.size(); ++i)
        {
            const auto& v = s.views[i];
            if (i + 1 < s.views.size()) { p->t0.push_back (v.t0); p->t1.push_back (v.t1); }
            p->vt0.push_back (i + 1 < s.views.size() ? v.t0 : 0.0); p->rt0.push_back (v.refT0); p->rt1.push_back (v.refT1); p->mdb.push_back (v.matchDb); p->outDb.push_back (v.match[5]);
            tp::Spec eq = v.curve;   // the match curve plus FULLNESS
            for (int b = 0; b < tp::kBins; ++b) eq[(size_t) b] = juce::jlimit (-30.0f, 30.0f, eq[(size_t) b] + v.full[(size_t) b]);
            p->curves.push_back (eq);
            bool reused = false;
            if (prev != nullptr && prev->linear == linear)
                for (size_t j = 0; j < prev->curves.size() && ! reused; ++j)
                    if (prev->curves[j] == eq) { p->filters.push_back (prev->filters[j]); reused = true; }
            if (! reused) p->filters.push_back (conv.makeFilter (tp::designIR (eq, fs, L, linear)));
            if (threadShouldExit()) return;
        }
        p->whole = (int) s.views.size() - 1;
    }
    const int lat = linear ? L / 2 + B : B;
    if (prev != nullptr) retiredAt[prev] = juce::Time::getMillisecondCounter();
    live_.store (p.get());
    programs.push_back (std::move (p));
    if (lat != getLatencySamples()) { wantLatency.store (lat); triggerAsyncUpdate(); }
}

void TransplantProcessor::handleAsyncUpdate() { setLatencySamples (wantLatency.load()); }

//============================================================================== actions
void TransplantProcessor::loadReference (const juce::File& f)
{
    if (auto* t = apvts.getParameter ("target")) if (t->getValue() > 0.5f) t->setValueNotifyingHost (0.0f);   // a new reference: match to it
    { const juce::ScopedLock sl (jobLock); refJobs.add (f); }
    { const juce::ScopedLock sl (modelLock); busy = "ANALYSING " + f.getFileName().toUpperCase(); }
    notify();
}
void TransplantProcessor::removeReference (int index)
{
    const juce::ScopedLock sl (modelLock);
    if (index >= 0 && index < (int) refs.size()) refs.erase (refs.begin() + index);
    if (index <= listenIdx.load()) listenIdx.store (juce::jmax (0, listenIdx.load() - (index < listenIdx.load() ? 1 : 0)));
    listenJob = true; dirty = true; notify();
}
void TransplantProcessor::loadMineFromFile (const juce::File& f)
{
    { const juce::ScopedLock sl (jobLock); mineJob = f; }
    { const juce::ScopedLock sl (modelLock); busy = "ANALYSING " + f.getFileName().toUpperCase(); }
    notify();
}
void TransplantProcessor::clearMine()
{
    const juce::ScopedLock sl (modelLock);
    mine = tp::TrackInfo(); mineName = {};
    dirty = true; notify();
}
void TransplantProcessor::startLearn() { { const juce::ScopedLock sl (modelLock); busy = {}; } learnReset = true; notify(); }
void TransplantProcessor::stopLearn() { learning = false; learnFinish = true; notify(); }

juce::ValueTree TransplantProcessor::editsFor (const juce::String& view, bool create)
{
    auto root = apvts.state.getChildWithName ("EDITS");
    if (! root.isValid()) { if (! create) return {}; root = juce::ValueTree ("EDITS"); apvts.state.appendChild (root, &undo); }
    for (auto c : root) if (c["name"].toString() == view) return c;
    if (! create) return {};
    juce::ValueTree c ("S"); c.setProperty ("name", view, nullptr);
    root.appendChild (c, &undo);
    return c;
}
void TransplantProcessor::setUserPoint (const juce::String& view, float oldBin, float bin, float db)
{
    auto t = editsFor (view, true);
    auto pts = parsePoints (t["user"].toString());
    bool found = false;
    if (oldBin >= 0)
        for (auto& p : pts) if (std::abs (p.bin - oldBin) < 0.02f) { p.bin = bin; p.db = db; found = true; break; }
    if (! found) pts.push_back ({ bin, db, true });
    t.setProperty ("user", fmtPoints (pts), &undo);
}
void TransplantProcessor::removePoint (const juce::String& view, float bin, bool user)
{
    auto t = editsFor (view, true);
    if (user)
    {
        auto pts = parsePoints (t["user"].toString());
        pts.erase (std::remove_if (pts.begin(), pts.end(), [bin] (const tp::Node& n) { return std::abs (n.bin - bin) < 0.02f; }), pts.end());
        t.setProperty ("user", fmtPoints (pts), &undo);
    }
    else
    {
        auto s = t["removed"].toString();
        t.setProperty ("removed", (s.isEmpty() ? juce::String() : s + ";") + juce::String (bin, 2), &undo);
    }
}
void TransplantProcessor::resetPoints (const juce::String& view)
{
    auto t = editsFor (view, false);
    if (t.isValid()) { t.removeProperty ("user", &undo); t.removeProperty ("removed", &undo); }
}
void TransplantProcessor::setSectionTarget (const juce::String& view, const juce::String& refSection) { editsFor (view, true).setProperty ("target", refSection, &undo); }
juce::String TransplantProcessor::sectionTarget (const juce::String& view) const
{
    const juce::ScopedLock sl (editLock);
    auto it = editCache.find (view); return it != editCache.end() && it->second.target.isNotEmpty() ? it->second.target : juce::String ("AUTO");
}
void TransplantProcessor::setSectionBypass (const juce::String& view, bool b) { editsFor (view, true).setProperty ("bypass", b, &undo); }
bool TransplantProcessor::sectionBypassed (const juce::String& view) const
{
    const juce::ScopedLock sl (editLock);
    auto it = editCache.find (view); return it != editCache.end() && it->second.bypass;
}

const char* TransplantProcessor::matchId (int k)
{
    static const char* const ids[kMatchN] = { "amount", "smooth", "range", "from", "to", "out", "fullness" };
    return ids[juce::jlimit (0, kMatchN - 1, k)];
}
float TransplantProcessor::matchValue (const juce::String& view, int k) const
{
    {
        const juce::ScopedLock sl (editLock);
        auto it = editCache.find (view);
        if (it != editCache.end() && it->second.has[k]) return it->second.m[k];
    }
    return apvts.getRawParameterValue (matchId (k))->load();
}
void TransplantProcessor::setMatchValue (const juce::String& view, int k, float value)
{
    if (view.isEmpty()) return;
    auto* prm = apvts.getParameter (matchId (k));
    editsFor (view, true).setProperty (juce::String ("m_") + matchId (k), prm->convertFrom0to1 (prm->convertTo0to1 (value)), &undo);
}
void TransplantProcessor::copyMatchTo (const juce::String& view, const juce::StringArray& to)
{
    float v[kMatchN]; for (int k = 0; k < kMatchN; ++k) v[k] = matchValue (view, k);
    for (auto& name : to)
        if (name != view)
            for (int k = 0; k < kMatchN; ++k) setMatchValue (name, k, v[k]);
}

// a copy of the edits the thread can read safely (the ValueTree belongs to the message thread)
void TransplantProcessor::cacheEdits()
{
    std::map<juce::String, EditData> m;
    for (auto c : apvts.state.getChildWithName ("EDITS"))
    {
        EditData e; e.user = parsePoints (c["user"].toString()); e.removed = parseList (c["removed"].toString());
        e.target = c["target"].toString(); e.bypass = (bool) c["bypass"];
        for (int k = 0; k < kMatchN; ++k)
        {
            const juce::Identifier id (juce::String ("m_") + matchId (k));
            if (c.hasProperty (id)) { e.has[k] = true; e.m[k] = (float) c[id]; }
        }
        m[c["name"].toString()] = e;
    }
    const juce::ScopedLock sl (editLock); editCache = m;
}
void TransplantProcessor::treeChanged() { cacheEdits(); dirty = true; notify(); }

//============================================================================== A/B, presets
void TransplantProcessor::switchAB()
{
    auto cur = apvts.copyState();
    if (! other.isValid()) other = cur.createCopy();
    apvts.replaceState (other);
    other = cur;
    ab ^= 1;
    treeChanged();
}
void TransplantProcessor::copyAtoB() { other = apvts.copyState(); }

const juce::StringArray& TransplantProcessor::presetNames()
{
    static const juce::StringArray n { "Default", "Surgical", "Broad Strokes", "Gentle Polish", "Low End Only", "Top End Only", "Natural Phase" };
    return n;
}
void TransplantProcessor::loadPreset (int index)
{
    struct V { const char* id; float v; };
    static const std::vector<std::vector<V>> presets {
        { },
        { { "smooth", 0.12f }, { "range", 18.0f } },
        { { "smooth", 0.75f }, { "range", 9.0f } },
        { { "amount", 0.5f }, { "smooth", 0.6f }, { "range", 6.0f } },
        { { "to", 300.0f } },
        { { "from", 2000.0f } },
        { { "phase", 1.0f } },
    };
    if (index < 0 || index >= (int) presets.size()) return;
    undo.beginNewTransaction();
    for (auto* id : { "amount", "smooth", "range", "from", "to", "keyZone", "phase", "out", "fullness" })
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->getDefaultValue());
    for (auto& v : presets[(size_t) index])
        if (auto* p = apvts.getParameter (v.id)) p->setValueNotifyingHost (p->convertTo0to1 (v.v));
    // a preset sets the MATCH settings of every section
    for (auto c : apvts.state.getChildWithName ("EDITS"))
        for (int k = 0; k < kMatchN; ++k) c.removeProperty (juce::Identifier (juce::String ("m_") + matchId (k)), &undo);
    currentPreset = index;
}

//============================================================================== state
juce::String TransplantProcessor::encodeTrack (const tp::TrackInfo& t)
{
    juce::MemoryOutputStream o;
    o.writeInt ((int) t.slices.size());
    for (auto& s : t.slices)
    {
        o.writeFloat (s.t); o.writeFloat (s.loud);
        for (float c : s.chroma) o.writeShort ((short) juce::jlimit (0, 32767, (int) (c * 32767.0f)));
        for (float p : s.pow) o.writeShort ((short) juce::jlimit (-32000, 32000, (int) std::lround (tp::toDb (p) * 100.0)));
    }
    return o.getMemoryBlock().toBase64Encoding();
}
bool TransplantProcessor::decodeTrack (const juce::String& str, tp::TrackInfo& t)
{
    juce::MemoryBlock mb; if (! mb.fromBase64Encoding (str)) return false;
    juce::MemoryInputStream in (mb, false);
    const int n = in.readInt(); if (n <= 0 || n > 100000) return false;
    t.slices.resize ((size_t) n);
    for (auto& s : t.slices)
    {
        s.t = in.readFloat(); s.loud = in.readFloat();
        for (auto& c : s.chroma) c = in.readShort() / 32767.0f;
        for (auto& p : s.pow) p = (float) std::pow (10.0, in.readShort() / 1000.0);
    }
    tp::analyseTrack (t);
    return true;
}

void TransplantProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("TRANSPLANT");
    root.appendChild (apvts.copyState(), nullptr);
    juce::ValueTree abt ("AB"); abt.setProperty ("slot", ab, nullptr);
    if (other.isValid()) abt.appendChild (other.createCopy(), nullptr);
    root.appendChild (abt, nullptr);
    juce::ValueTree data ("DATA");
    {
        const juce::ScopedLock sl (modelLock);
        if (mine.valid()) { juce::ValueTree m ("MINE"); m.setProperty ("name", mineName, nullptr); m.setProperty ("d", encodeTrack (mine), nullptr); data.appendChild (m, nullptr); }
        for (auto& r : refs) { juce::ValueTree m ("REF"); m.setProperty ("name", r.name, nullptr); m.setProperty ("path", r.path, nullptr); m.setProperty ("d", encodeTrack (r.info), nullptr); data.appendChild (m, nullptr); }
        data.setProperty ("listen", listenIdx.load(), nullptr);
    }
    root.appendChild (data, nullptr);
    root.setProperty ("preset", currentPreset, nullptr);
    root.setProperty ("uiWidth", uiWidth, nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, dest);
}

void TransplantProcessor::setStateInformation (const void* d, int size)
{
    struct StudioAgain { juce::AudioProcessorValueTreeState& s; ~StudioAgain() { if (auto* h = s.getParameter ("hearOn")) h->setValueNotifyingHost (0.0f); } } studioAgain { apvts };
    auto xml = getXmlFromBinary (d, size);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("TRANSPLANT")) return;
    auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid()) apvts.replaceState (params);
    auto abt = root.getChildWithName ("AB");
    ab = (int) abt.getProperty ("slot", 0);
    other = abt.getNumChildren() > 0 ? abt.getChild (0).createCopy() : juce::ValueTree();
    currentPreset = (int) root.getProperty ("preset", 0);
    uiWidth = (int) root.getProperty ("uiWidth", 1200);
    tp::TrackInfo m; juce::String mn; std::vector<RefTrack> rs;
    for (auto c : root.getChildWithName ("DATA"))
    {
        tp::TrackInfo t;
        if (! decodeTrack (c["d"].toString(), t)) continue;
        if (c.hasType ("MINE")) { m = std::move (t); mn = c["name"].toString(); }
        else rs.push_back ({ c["name"].toString(), c["path"].toString(), std::move (t) });
    }
    listenIdx.store ((int) root.getChildWithName ("DATA").getProperty ("listen", 0));
    listenJob = true;
    { const juce::ScopedLock sl (modelLock); mine = std::move (m); mineName = mn; refs = std::move (rs); }
    treeChanged();
}

juce::AudioProcessorEditor* TransplantProcessor::createEditor() { return new TransplantEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TransplantProcessor(); }
