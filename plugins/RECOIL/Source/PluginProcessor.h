#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "RecoilEngine.h"
#include "Shapes.h"
#include "PresetManager.h"
#include <array>
#include <atomic>

// Single-producer ring of mono samples for the displays (the editor reads it; a torn read only costs a frame).
struct ScopeRing
{
    static constexpr int kSize = 1 << 18;                       // 2.7 s at 96 kHz: enough for a 1/1 cycle at slow tempos
    std::vector<float> data = std::vector<float> ((size_t) kSize, 0.0f);
    std::atomic<int64_t> total { 0 };
    void push (const float* x, int n)
    {
        int64_t t = total.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) data[(size_t) ((t + i) & (kSize - 1))] = x[i];
        total.store (t + n, std::memory_order_release);
    }
    void readFrom (int64_t start, float* dest, int n) const     // absolute sample index; outside what is kept reads 0
    {
        const int64_t t = total.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            const int64_t idx = start + i;
            dest[i] = (idx < 0 || idx >= t || t - idx > kSize) ? 0.0f : data[(size_t) (idx & (kSize - 1))];
        }
    }
    void readLatest (float* dest, int n) const { readFrom (total.load (std::memory_order_acquire) - n, dest, n); }
};

// RECOIL 2: shape-based volume ducking (SYNC / MIDI / AUDIO trigger), zero latency.
class RecoilProcessor : public juce::AudioProcessor, private juce::Timer, private juce::ValueTree::Listener,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    RecoilProcessor();
    ~RecoilProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "RECOIL"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }   // a nameless program fails the VST3 validator
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioFormatManager formats;
    PresetManager presets { apvts };

    // ---- shape (message thread)
    // Two shape slots: 0 the main shape (and MAP's kicks), 1 MAP's snares and rims. The browser and the editor work on
    // the slot picked in the MAP strip (always slot 0 outside MAP). slot -1 = that one.
    int shapeSlot() const;
    void setShapeSlot (int slot);
    int shapeIndex (int slot = -1) const;
    std::vector<rc::Node> currentNodes (int slot = -1) const;
    juce::String currentShapeName (int slot = -1) const;
    juce::String currentShapeDescription (int slot = -1) const;
    void setCustomNodes (const std::vector<rc::Node>& nodes, const juce::String& name, int slot = -1);   // switches to Custom
    void selectBuiltIn (int index, int slot = -1);
    rc::Settings currentSettings() const;
    void relearnPhase() { engine.relearnRequest.store (1); }
    const rc::Engine& getEngine() const { return engine; }

    // ---- MAP (message thread). The hits live here and in the state's MAP child; the engine gets a copy.
    struct MapInfo { juce::String name, path; double seconds = 0; rcmap::Stats stats; };
    struct BassInfo { juce::String name, path; double seconds = 0; int counts[rcbass::kKinds] {}; };
    void loadMapFile (const juce::File& f);                  // analyses on a background thread
    void loadBassFile (const juce::File& f);                 // the bass bus, against the loaded drum bus
    void loadMapAndBass (const juce::File& drums, const juce::File& bass) { startMapJob (drums, bass, true); }
    void clearMap();
    void clearBass();
    bool hasBass() const { return ! bassKind.empty() && bassKind.size() == mapHits.size(); }
    const BassInfo& getBassInfo() const { return bassInfo; }
    const std::vector<uint8_t>& getBassKind() const { return bassKind; }
    const std::vector<int8_t>& getBassClash() const { return bassClash; }
    const std::vector<uint8_t>& getBassEnv() const { return bassEnv; }
    void setMapHitOn (int index, bool on);
    const std::vector<rcmap::Hit>& getMapHits() const { return mapHits; }
    const MapInfo& getMapInfo() const { return mapInfo; }
    std::atomic<float> mapProgress { -1.0f };               // 0..1 while analysing, -1 otherwise
    juce::String mapError;                                  // the last load's problem (message thread)
    std::atomic<int> mapVersion { 0 };                      // bumps whenever the hits change
    static bool isAudioFile (const juce::String& path);

    // ---- for the editor
    ScopeRing scopeKick, scopeSc, scopeBefore, scopeAfter, scopeGain, scopeType;   // scopeType: which hit is ducking (MAP)
    std::atomic<int64_t> hitAt { -1 };                     // ring index of the latest hit / cycle start
    std::atomic<int> hitCount { 0 };
    // MAP: the same, per kind of hit (0 kick, 1 snare, 2 rim, 3 ghost note), with that hit's duck length in samples
    std::array<std::atomic<int64_t>, 4> hitAtKind {};
    std::array<std::atomic<int>, 4> hitCountKind {};
    std::array<std::atomic<double>, 4> hitLenKind {};
    std::atomic<double> cycleSamples { 0.0 };
    std::atomic<double> hostRate { 48000.0 }, hostBpm { 120.0 };
    std::atomic<bool> hostPlaying { false }, sidechainConnected { false };
    std::atomic<float> scPeak { 0.0f }, outPeak { 0.0f };    // highest since the editor last read them

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void parameterChanged (const juce::String& id, float) override { if (id.endsWith ("hape")) shapeDirty.store (true); }
    static bool isShapeTree (const juce::ValueTree& t) { return t.getType().toString().startsWith ("SHAPE") || t.hasType ("N"); }
    int resolve (int slot) const { return slot < 0 ? shapeSlot() : juce::jlimit (0, kSlots - 1, slot); }
public:
    // slots: 0 main / kicks (SUB with a bass bounce in SHAPES), 1 snares and rims, 2 PLUCK, 3 MID, 4 NOTE
    static constexpr int kSlots = 5;
    static const char* shapeParam (int slot) { static const char* n[] = { "shape", "snareShape", "pluckShape", "midShape", "noteShape" }; return n[juce::jlimit (0, kSlots - 1, slot)]; }
    static const char* shapeTree (int slot) { static const char* n[] = { "SHAPE", "SHAPE2", "SHAPE3", "SHAPE4", "SHAPE5" }; return n[juce::jlimit (0, kSlots - 1, slot)]; }
    static const char* nameKey (int slot) { static const char* n[] = { "customName", "customName2", "customName3", "customName4", "customName5" }; return n[juce::jlimit (0, kSlots - 1, slot)]; }
    bool bassShapesActive() const;                           // MAP, a bass bounce, and SHAPES
    bool bassAutoActive() const;                             // MAP, a bass bounce, and AUTO
private:
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree& c) override { if (isShapeTree (p) || isShapeTree (c)) shapeDirty.store (true); }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree& c, int) override { if (isShapeTree (p) || isShapeTree (c)) shapeDirty.store (true); }
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&) override { if (isShapeTree (t)) shapeDirty.store (true); }
    void valueTreeRedirected (juce::ValueTree&) override { shapeDirty.store (true); }
    void pushShape();
    void storeMap();                                        // hits -> state
    void restoreMap();                                      // state -> hits
    struct JobResult
    {
        bool newDrums = false, hasBass = false;
        std::vector<rcmap::Hit> hits; MapInfo info;
        rcbass::Result bass; BassInfo bassInfo;
        juce::String error, bassError;
    };
    void startMapJob (const juce::File& drums, const juce::File& bass, bool newDrums);
    void mapAnalysed (JobResult r);
    void clearBassData();
    rc::Engine::BassMap bassMapForEngine() const;

    std::vector<rcmap::Hit> mapHits;
    MapInfo mapInfo;
    BassInfo bassInfo;
    std::vector<uint8_t> bassKind, bassEnv;
    std::vector<int8_t> bassClash;
    std::atomic<bool> mapDirty { false };
    struct MapJob;
    std::unique_ptr<juce::Thread> mapThread;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    rc::Engine engine;
    std::atomic<bool> shapeDirty { true };
    std::vector<float> kTmp, bTmp, aTmp, gTmp, sTmp, tTmp;
    std::vector<rc::MidiHit> midiTmp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecoilProcessor)
};
