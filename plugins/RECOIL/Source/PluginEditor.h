#pragma once
#include <juce_dsp/juce_dsp.h>
#include <deque>
#include "PluginProcessor.h"
#include "RecoilLookAndFeel.h"
#include "ScalableEditor.h"

//==============================================================================
// WAVE view: what RECOIL does around each hit (AUDIO / MIDI) or over one cycle (SYNC).
//   top lane: PHASE, the kick's low end (red) against the bass after RECOIL (white), with how well they match
//   main lane: the bass before (hatched: what was taken out) and after (white), the shape on top (orange)
//   drag the DEPTH tab (on the dB scale) and the LENGTH tab; in EDIT mode, edit the shape's points directly.
class WaveView : public juce::Component, public juce::SettableTooltipClient
{
public:
    WaveView (RecoilProcessor&, RecoilLookAndFeel&);
    void update();
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverNode = -1; hoverTab = 0; repaint(); }

    enum class Tool { Point, Draw, Step };
    bool editing = false, snap = true;
    bool autoShape = false;                    // MAP + bass + AUTO: shows the last hit's computed duck, read-only
    Tool tool = Tool::Point;
    int gridDiv = 16;                                   // snap grid: divisions of the cycle
    std::function<void()> onEdited;

    void mirror();
    void clear();

private:
    // geometry (local)
    float px0() const { return 14.0f; }
    float px1() const { return (float) getWidth() - 46.0f; }
    // the phase lane is only there in AUDIO and MAP (SYNC and MIDI have no kick to line up with): the main lane takes its room
    bool showPhase = true;
    juce::Rectangle<float> laneR() const { return showPhase ? juce::Rectangle<float> (px0(), 52.0f, px1() - px0(), 56.0f) : juce::Rectangle<float> (px0(), 46.0f, px1() - px0(), 0.0f); }
    juce::Rectangle<float> mainR() const { return showPhase ? juce::Rectangle<float> (px0(), 122.0f, px1() - px0(), 212.0f) : juce::Rectangle<float> (px0(), 46.0f, px1() - px0(), 288.0f); }
    float tx (double t) const;                          // seconds from the hit -> x
    double xt (float x) const;
    float yLevel (double g) const { const auto m = mainR(); return m.getY() + 22.0f + (float) (1.0 - g) * (m.getHeight() - 32.0f); }
    double levelOf (float y) const { const auto m = mainR(); return 1.0 - (y - m.getY() - 22.0f) / (m.getHeight() - 32.0f); }
    double cycleSec() const;
    double shownGain (const std::vector<rc::Node>&, double x) const;   // shape -> gain (with curve, depth and mix unless editing)
    int nodeAt (juce::Point<float>) const;
    int segmentAt (float x) const;
    float param (const char* id) const { return processor.apvts.getRawParameterValue (id)->load(); }
    void setParam (const char* id, float v);
    void beginGesture (const char* id);
    void endGesture (const char* id);
    void commit (const juce::String& nameIfBuiltIn = {});
    void paintAt (juce::Point<float>);

    RecoilProcessor& processor;
    RecoilLookAndFeel& lnf;
    std::vector<rc::Node> nodes;                        // working copy of the shape
    std::vector<float> kick, before, after, gain;      // the captured window
    double capSpanSec = 0.3, capPreSec = 0.02, capWinSec = 0.324, fs = 48000.0, frozenSpan = -1.0;   // span: the duck; win: the view
    int64_t pendingHit = -1;
    std::deque<std::pair<int64_t, double>> pending;     // MAP: hits waiting for their window (start, duck length)
    int lastKind = -1;                                  // MAP: the kind of hit the view follows
    bool laneIsDrums = false;                           // MAP, not KICK: the top lane shows the sidechain (the drums)
    int lastHitCount = -1, hoverNode = -1, dragNode = -1, dragSeg = -1, hoverTab = 0, dragTab = 0;
    bool frozen = false, haveCapture = false;
    float dragY0 = 0.0f, bend0 = 0.0f, depth0 = 0.0f;
    double lastMatch = 0.0;
    // the note of the kick (its low end, after the hit) and of the bass under it, from each captured hit
    void measureKeys();
    std::array<float, 5> kickHz {}, bassHz {};
    int kickN = 0, bassN = 0;
    float keyOf (const std::array<float, 5>& v, int n) const;   // median of the last few, 0 if none
};

//==============================================================================
// SPECTRUM view: before / after / kick spectra, averaged, with the band (drag its edges when SPLIT).
class SpecView : public juce::Component, public juce::SettableTooltipClient
{
public:
    SpecView (RecoilProcessor&, RecoilLookAndFeel&);
    void update (float dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::TextEditor hzEditor;                                   // double-click a band edge's label: type the frequency
    int editingEdge = -1;
    void finishHzEdit (bool apply);
    juce::Rectangle<float> edgeTab (int edge) const;

private:
    static constexpr int kFftOrder = 12, kFft = 1 << kFftOrder, kPts = 360;
    juce::Rectangle<float> plot() const { return { 14.0f, 44.0f, (float) getWidth() - 60.0f, (float) getHeight() - 78.0f }; }
    float xOf (float hz) const;
    float hzOf (float x) const;
    float yDb (float db) const;
    using Pts = std::array<float, kPts>;
    void framePower (const ScopeRing&, int64_t start, Pts& out);           // this frame's power per point (linear)
    static void ema (Pts& dbAvg, const Pts& now, float k);
    bool mapMode() const;
    int edgeAt (float x) const;

    RecoilProcessor& processor;
    RecoilLookAndFeel& lnf;
    juce::dsp::FFT fft { kFftOrder };
    std::vector<float> window, buf;
    Pts ptHz {}, sBefore {}, sAfter {}, sKick {};
    Pts pB {}, pA {}, pS {};
    // MAP: what each kind of hit takes out (linear power, averaged) and each drum's own spectrum from the sidechain (dB)
    std::array<Pts, 3> removed {}, drum {};
    std::vector<float> typeBuf;
    int dragEdge = -1, hoverEdge = -1;
};

//==============================================================================
// MAP view: the analysed drum bus. Overview of the whole song on top (click to jump there), a zoomed timeline below with
// one row per kind of hit. Each hit's stem is its level; the bright part is how deep it ducks with the current settings.
// Click a hit to switch it off (or on again); drag to scroll, wheel to zoom. Follows the playhead while the song plays.
class MapView : public juce::Component, public juce::SettableTooltipClient
{
public:
    MapView (RecoilProcessor&, RecoilLookAndFeel&);
    void update();
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    double duckFor (const rcmap::Hit&, int index) const;   // 0..1 with the current settings (as the engine plays it)
    double autoDb (int index, int* frames) const;          // AUTO: the hit's deepest duck in dB
    bool dragOver = false;
    std::function<void()> onLoadRequest;

private:
    juce::Rectangle<float> over() const { return { 64.0f, 46.0f, (float) getWidth() - 110.0f, 24.0f }; }
    juce::Rectangle<float> zoom() const { return { 64.0f, 96.0f, (float) getWidth() - 110.0f, 236.0f }; }
    int rows() const { return processor.hasBass() ? 5 : 4; }   // KICK SNARE RIM HAT (+ BASS)
    float rowY (int type) const { const auto z = zoom(); return z.getY() + z.getHeight() * (float) (type + 1) / (float) rows() - 6.0f; }
    float xOfT (double t) const { const auto z = zoom(); return z.getX() + (float) ((t - viewStart) / viewLen) * z.getWidth(); }
    double tOfX (float x) const { const auto z = zoom(); return viewStart + (x - z.getX()) / z.getWidth() * viewLen; }
    double songLen() const;
    int hitAt (juce::Point<float>) const;
    float param (const char* id) const { return processor.apvts.getRawParameterValue (id)->load(); }

    RecoilProcessor& processor;
    RecoilLookAndFeel& lnf;
    double viewStart = 0.0, viewLen = 8.0, lastNav = -1e9, dragT0 = 0.0;
    int hover = -1, lastVersion = -1;
    bool draggingOverview = false, dragged = false;
    float dragX0 = 0.0f;
    float lastNow = -2.0f;
};

//==============================================================================
// The shape browser: FACTORY / RECOIL / USER banks, 4 x 4 tiles, the selected shape's name and what it does.
class ShapeBrowser : public juce::Component, public juce::SettableTooltipClient
{
public:
    ShapeBrowser (RecoilProcessor&, RecoilLookAndFeel&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void resized() override;
    void setBank (int b);
    void syncBankToShape();
    void refreshSlots();                                     // KICK / SNARE (MAP only): which shape the browser edits
    std::function<void()> onSaveRequest, onSlotChange;

private:
    juce::Rectangle<float> tile (int i) const;
    int tileAt (juce::Point<float>) const;
    RecoilProcessor& processor;
    RecoilLookAndFeel& lnf;
    juce::TextButton tabs[3], slots[2], kindSlots[5];          // kindSlots: SUB SNARE PLUCK MID NOTE (MAP with a bass bounce, PER BASS)
    bool autoPanel = false;                                     // MAP with a bass bounce, AUTO: no tiles, the AUTO panel
    int bank = 0, hover = -1;
    juce::StringArray user;
    std::vector<std::vector<rc::Node>> userNodes;
};

//==============================================================================
// AUDIO trigger input meter: the sidechain level, with the THRESHOLD as a line you drag.
class ThresholdMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    ThresholdMeter (RecoilProcessor& p) : processor (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override;
    float levelDb = -100.0f, peakDb = -100.0f;
private:
    float xOf (float db) const { return (float) getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); }
    RecoilProcessor& processor;
};

// BAND: two-thumb range slider on a log frequency scale (20 Hz - 16 kHz).
class BandSlider : public juce::Component, public juce::SettableTooltipClient
{
public:
    BandSlider (RecoilProcessor& p, RecoilLookAndFeel& l) : processor (p), lnf (l) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    float xOf (float hz) const;
    float hzOf (float x) const;
    RecoilProcessor& processor;
    RecoilLookAndFeel& lnf;
    int drag = -1;
};

//==============================================================================
class RecoilUI : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit RecoilUI (RecoilProcessor&);
    ~RecoilUI() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { mapView.dragOver = true; mapView.repaint(); }
    void fileDragExit (const juce::StringArray&) override { mapView.dragOver = false; mapView.repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void loadMap (const juce::File&);
    void loadBass (const juce::File&);
    void finishLoad();
    bool switchToModeDefault (int mode);                        // true if it loaded the mode's default preset
    void chooseMapFile (bool bass);
    void showMapMenu (bool bass);
    void timerCallback() override;
    void refreshPresetList();
    void stepPreset (int delta);
    void showSaveDialog();
    void showDeleteDialog();
    void showSaveShapeDialog();
    void setHelp (bool on);
    void setView (int v);
    void setEditing (bool on);
    void layoutTrigger();
    void renderStatic (float pixelScale);
    void drawPlate (juce::Graphics&, juce::Rectangle<float>) const;
    void drawHead (juce::Graphics&, float x, float y, const juce::String& title, juce::Colour dot) const;
    void drawWell (juce::Graphics&, juce::Rectangle<float>) const;
    juce::String text (const char* id) const;
    int trig() const { return juce::roundToInt (processor.apvts.getRawParameterValue ("trigger")->load()); }

    RecoilProcessor& processor;
    RecoilLookAndFeel lnf;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::Image logo, staticLayer;
    float staticScale = 0.0f;

    WaveView wave;
    SpecView spec;
    MapView mapView;
    ShapeBrowser browser;
    ThresholdMeter thresh;
    BandSlider bandSlider;

    juce::ComboBox presetBox, noteBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, deleteButton { "DEL" }, helpButton { "?" };
    juce::TextButton editButton { "EDIT" }, viewButtons[3], toolButtons[3], snapButton { "SNAP" }, gridButton { "GRID 1/16" },
                     mirrorButton { "MIRROR" }, clearButton { "CLEAR" }, saveShapeButton { "SAVE SHAPE" };
    std::unique_ptr<juce::AlertWindow> dialog;
    std::unique_ptr<juce::FileChooser> chooser;

    struct Knob { juce::Slider slider; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach; juce::String id, label; juce::Point<int> centre; int d = 44; int labelMode = 0; };
    Knob depth, length, curve, attack, inside, above, rotate, mix, gain, dynamic, offset, swing, mapKick, mapSnare, mapRim, mapOffset, mapGhostDb,
         clearance, recovery;
    Knob* knobs[19] { &depth, &length, &curve, &attack, &inside, &above, &rotate, &mix, &gain, &dynamic, &offset, &swing,
                      &mapKick, &mapSnare, &mapRim, &mapOffset, &mapGhostDb, &clearance, &recovery };

    juce::TextButton waveHitButtons[4];                         // MAP + WAVE: which kind of hit the view follows
    juce::TextButton triggerButtons[4], ghostButtons[3], bassModeButtons[2], loadButton { "DRUMS" }, bassButton { "BASS" }, fitMapButton { "FIT LENGTH" }, alignButton { "ALIGN" }, rateButtons[6], rateModeButtons[3], listenButtons[2], bandButtons[2], edgeButtons[2], phaseButtons[3];
    juce::TextButton fitButton { "FIT LENGTH" }, grooveButton { "GROOVE LOCK" }, autoOffsetButton { "AUTO OFFSET" }, polarityButton { juce::String::fromUTF8 ("\xc3\x98") }, relearnButton { "RELEARN" };
    std::vector<std::unique_ptr<juce::ParameterAttachment>> attachments;
    std::unique_ptr<juce::ParameterAttachment> noteAttach;

    float scDb = -100.0f, outDb = -100.0f;
    juce::String lastText;
    int frame = 0, lastTrig = -1;
    double lastTick = 0.0;

    static constexpr int kW = 1100, kH = 720;
    friend class ScalableEditor<RecoilUI>;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecoilUI)
};

//==============================================================================
class RecoilEditor : public ScalableEditor<RecoilUI>
{
public:
    explicit RecoilEditor (RecoilProcessor& p) : ScalableEditor (p) {}
};
