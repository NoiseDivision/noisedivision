#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "TransplantDSP.h"

// TRANSPLANT: a match EQ that moves the reference's low end into your key and matches section to section.
class TransplantProcessor : public juce::AudioProcessor,
                            private juce::Thread,
                            private juce::AudioProcessorValueTreeState::Listener,
                            private juce::ValueTree::Listener,
                            private juce::AsyncUpdater
{
public:
    TransplantProcessor();
    ~TransplantProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.4; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;

    // ---------------------------------------------------------------- what the display shows, per section of your track
    struct View
    {
        juce::String name;                 // "DROP 2", or "WHOLE TRACK"
        double t0 = 0, t1 = 0;
        tp::Spec yours {}, ref {}, refRaw {}, curve {};   // dB; ref = in your key; curve = the EQ applied (after AMOUNT / RANGE / FROM-TO)
        std::vector<tp::Node> nodes;       // the points on the curve (user = moved or added by you)
        juce::String refLabel;             // which part of the reference it matches ("DROP 1"), "AUTO: DROP 1" when automatic
        bool hasRef = false;
        // for listening: the matching part of the reference you listen to, and the gain that levels it with yours
        double refT0 = 0, refT1 = 0; float matchDb = 0;
        // this section's MATCH settings (its own, or the plugin-wide ones it hasn't changed): amount, smooth, range, from, to, out
        float match[7] { 1.0f, 0.4f, 12.0f, 20.0f, 20000.0f, 0.0f, 0.0f };   // ... out, fullness
        tp::Spec full {};                  // FULLNESS: the level compensation added to the curve (dB)
    };
    struct Snapshot
    {
        std::vector<View> views;           // your sections, then WHOLE TRACK last
        juce::String mineName, mineKey; double mineLength = 0; float mineConf = 0;
        struct Ref { juce::String name, key, path; float conf = 0; int shift = 0; juce::StringArray sections; double length = 0; bool fileOk = true; };
        std::vector<Ref> refs;
        bool edm = false;                  // matching to the middle of the EDM tonal balance range, not a reference
        int version = 0;
    };
    Snapshot snapshot() const { const juce::ScopedLock sl (modelLock); return snap; }
    int modelVersion() const { return version.load(); }
    bool hasMine() const { const juce::ScopedLock sl (modelLock); return mine.valid(); }
    int numRefs() const { const juce::ScopedLock sl (modelLock); return (int) refs.size(); }

    // ---------------------------------------------------------------- actions (message thread)
    void loadReference (const juce::File&);            // analysed in the background (up to 3)
    void removeReference (int index);
    void loadMineFromFile (const juce::File&);         // your own bounce instead of LEARN
    void startLearn();
    void stopLearn();
    bool isLearning() const { return learning.load(); }
    double learnedSeconds() const { return learnedSecs.load(); }
    juce::String busyMessage() const { const juce::ScopedLock sl (modelLock); return busy; }
    void clearMine();

    // points (view index = position in snapshot().views)
    void setUserPoint (const juce::String& view, float oldBin, float bin, float db);   // oldBin < 0: a new point
    void removePoint (const juce::String& view, float bin, bool user);
    void resetPoints (const juce::String& view);
    void setSectionTarget (const juce::String& view, const juce::String& refSection); // "AUTO", "WHOLE TRACK", "DROP 1"...
    juce::String sectionTarget (const juce::String& view) const;
    void setSectionBypass (const juce::String& view, bool bypassed);
    // MATCH settings per section: AMOUNT, SMOOTH, RANGE, FROM, TO, OUTPUT. A section that hasn't set one uses the parameter's value.
    static constexpr int kMatchN = 7;
    static const char* matchId (int k);
    float matchValue (const juce::String& view, int k) const;
    void setMatchValue (const juce::String& view, int k, float value);
    void copyMatchTo (const juce::String& view, const juce::StringArray& to);   // these sections get this section's settings
    std::atomic<bool> edmActive { false };
    bool sectionBypassed (const juce::String& view) const;

    // listening to the reference (LISTEN: YOURS / REF)
    void setListenRef (int index);
    // HEAR ON: a preview of other systems, for your ears only (never in a bounce)
    juce::dsp::Convolution hearConv; juce::AudioBuffer<float> hearDry; juce::LinearSmoothedValue<float> hearMix;
    std::atomic<int> hearLoaded { -1 }; std::atomic<bool> hearJob { false }; bool hearActive = false;
    void loadHearOn();
    void applyHearOn (juce::AudioBuffer<float>&, int n);
    int listenRef() const { return listenIdx.load(); }
    std::atomic<float> refGainShown { 0.0f };   // the gain on the reference now (level match), dB
    std::atomic<bool> refAudible { false };     // the reference is what's playing
    std::atomic<int> refAudioState { 0 };       // 0 none, 1 loading, 2 ready, 3 file missing

    // METERS: your track (after TRANSPLANT) and the reference (its matching part, at its own level), side by side
    struct MeterOut
    {
        std::atomic<float> st { -120 }, sec { -120 }, tp { -120 }, width { 0 }, low { -60 }, mid { -60 }, high { -60 };
        std::atomic<bool> valid { false };
    };
    MeterOut meterYours, meterRef;
    std::atomic<bool> peakReset { false };      // the true peak maxima start again (also when the section changes)

    // A/B
    void switchAB();
    void copyAtoB();
    int abSlot() const { return ab; }

    // presets
    static const juce::StringArray& presetNames();
    void loadPreset (int index);
    int currentPreset = 0;
    int uiWidth = 1200;                     // the window's width, remembered with the session

    // ---------------------------------------------------------------- live info for the display
    std::atomic<int> selectedView { -1 };   // the section shown/used when not following the playhead (-1 = whole track)
    std::atomic<int> activeView { -1 };     // the section playing now
    std::atomic<double> playTime { 0.0 };
    std::atomic<bool> playing { false };
    std::atomic<double> rate { 48000.0 };
    int latencyMs() const { return juce::roundToInt (getLatencySamples() * 1000.0 / rate.load()); }
    // the last 8192 output samples (mono), for the live analyser
    static constexpr int kLive = 8192;
    float live[kLive] {};
    std::atomic<int> liveWrite { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void run() override;                                   // the background thread
    void parameterChanged (const juce::String&, float) override { dirty = true; notify(); }
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&) override { if (! t.hasType ("PARAM")) treeChanged(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { treeChanged(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { treeChanged(); }
    void valueTreeRedirected (juce::ValueTree&) override { treeChanged(); }
    void treeChanged();
    void cacheEdits();
    struct EditData { std::vector<tp::Node> user; std::vector<float> removed; juce::String target; bool bypass = false;
                      float m[7] {}; bool has[7] {}; };
    std::map<juce::String, EditData> editCache;
    juce::CriticalSection editLock;
    void handleAsyncUpdate() override;                     // latency changes

    juce::ValueTree editsFor (const juce::String& view, bool create);
    void rebuild();                                        // views + filters (background thread)
    void drainLearn();
    void analyseFile (const juce::File&, bool asMine);
    static juce::String encodeTrack (const tp::TrackInfo&);
    static bool decodeTrack (const juce::String&, tp::TrackInfo&);

    // ---- model (guarded by modelLock)
    juce::CriticalSection modelLock;
    tp::TrackInfo mine;
    juce::String mineName;
    struct RefTrack { juce::String name, path; tp::TrackInfo info; };
    std::vector<RefTrack> refs;
    Snapshot snap;
    juce::String busy;
    std::atomic<int> version { 0 };
    std::atomic<bool> dirty { true };

    // ---- jobs for the thread
    juce::CriticalSection jobLock;
    juce::Array<juce::File> refJobs; juce::File mineJob;
    std::atomic<bool> learnFinish { false }, learnReset { false };

    // ---- learning: audio -> thread
    std::atomic<bool> learning { false };
    std::atomic<double> learnedSecs { 0.0 };
    juce::AbstractFifo fifo { 1 << 20 };
    std::vector<float> fifoBuf = std::vector<float> (1 << 20);
    struct Stamp { int n; double t; };
    juce::AbstractFifo stampFifo { 4096 };
    std::vector<Stamp> stamps = std::vector<Stamp> (4096);
    tp::SliceMaker learner;
    double lastStampEnd = -1;

    // ---- filters: built by the thread, used by the audio thread
    struct Program
    {
        bool linear = true;
        std::vector<tp::Convolver::Filter> filters;   // one per view (sections, then whole track); bypassed sections: flat
        std::vector<double> t0, t1;
        std::vector<double> vt0, rt0, rt1; std::vector<float> mdb, outDb;   // per view: its start, the reference's matching part, level match, OUTPUT
        std::vector<tp::Spec> curves;                 // what each filter was made from (to reuse unchanged ones)
        int whole = 0;
    };
    std::vector<std::unique_ptr<Program>> programs;   // newest last; old ones freed once the audio thread has moved on
    std::map<Program*, juce::uint32> retiredAt;
    juce::CriticalSection programLock;
    std::atomic<Program*> live_ { nullptr };
    std::atomic<Program*> inUse { nullptr };
    std::atomic<double> preparedRate { 0.0 };
    void buildProgram (bool force);
    std::unique_ptr<Program> flatProgram (bool linear) const;

    // ---- audio
    tp::Convolver conv;
    int B = 256, L = 16384;
    std::atomic<int> wantLatency { 0 };
    std::vector<float> dryL, dryR; int dryPos = 0, dryLen = 1;
    juce::SmoothedValue<float> outGain, wet;
    const tp::Convolver::Filter* lastFilter = nullptr;

    // ---- the reference's audio (only the one you listen to), decoded into memory by the thread
    struct RefAudio { std::vector<juce::int16> d; juce::int64 frames = 0; double rate = 44100; };
    std::vector<std::unique_ptr<RefAudio>> audios;
    std::map<RefAudio*, juce::uint32> audioRetired;
    std::atomic<RefAudio*> refAudio { nullptr }, refAudioInUse { nullptr };
    std::atomic<int> listenIdx { 0 };
    std::atomic<bool> listenJob { false };
    void decodeListen();
    void renderRef (int n, double t, bool isPlaying, const Program* p, int idx);
    std::vector<float> refL, refR;
    double refPos = 0, refExpectT = -1; int refView = -1, refFade = 0; bool refActive = false; const RefAudio* refLast = nullptr;
    juce::SmoothedValue<float> xfade, refGain;
    // level match, refined by listening: K-weighted power of what you hear, per section, for your track and the reference
    static constexpr int kMaxViews = 64;
    tp::KWeight kwY[2], kwR[2];
    tp::GatedLoudness yMeter[kMaxViews], rMeter[kMaxViews];
    const Program* measuredFor = nullptr; const RefAudio* measuredRef = nullptr;
    tp::LiveMeter liveY, liveR; int meterView = -2;

    // ---- A/B
    juce::ValueTree other;   // the slot not showing
    int ab = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransplantProcessor)
};
