#pragma once
#include "PluginProcessor.h"
#include "KillLookAndFeel.h"
#include "ScalableEditor.h"

//==============================================================================
// The big display, an X-ray film: the output is the bone, the input a dashed ghost outline,
// what got killed is dark dried blood between them (fresh blood runs down from the top of the film), and the
// threshold is a red grease-pencil line with draggable points.
//   drag the curve          moves THRESHOLD
//   double-click            adds a point (up to 8); drag it to bend the threshold there
//   double-click a point    removes it (right-click works too)
class KillDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    KillDisplay (SpectraKillProcessor&, KillLookAndFeel&);
    void update();                      // called by the editor's timer
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    float maxCutDb = 0.0f;              // deepest cut right now (for the KILL readout)

private:
    juce::Rectangle<float> plot() const;
    float xOf (float hz) const;
    float hzOf (float x) const;
    float yOf (float db) const;
    float dbOf (float y) const;
    int pointAt (juce::Point<float>) const;
    bool nearCurve (juce::Point<float>) const;
    float curveWithout (int skip, float hz) const;     // threshold at hz leaving one point out
    void setThreshold (float db);

    SpectraKillProcessor& processor;
    KillLookAndFeel& lnf;
    sk::Display data;
    static constexpr int kCols = 256;
    std::array<float, kCols> inCol {}, outCol {}, gainCol {};
    std::vector<sk::CurvePoint> points;
    int hover = -1, dragging = -1;
    bool draggingCurve = false, hoverCurve = false;
    float dragStartThr = 0.0f, dragStartY = 0.0f;
    std::array<float, kCols> dripLen {};          // blood running down: grows fast, dries up slowly
    std::array<float, kCols> dripVar {};          // fixed per-column length variation
    static constexpr int kCrackSizes = 8;
    std::vector<std::array<juce::Path, kCrackSizes>> cracks;   // fracture shapes, already stroked, at 8 lengths
    juce::Image grid;                   // film, grain, grid and labels at the screen's pixel density
    float gridScale = 0.0f;
    int gridW = 0, gridH = 0;
    juce::Image curveImage;             // the threshold curve with its glow, redrawn only when it changes
    juce::Rectangle<float> curveArea;   // the band of the display the curve image covers
    juce::String curveKey;
    struct Stamp { juce::String key; juce::Image image; juce::Rectangle<float> area; };
    Stamp idStamp, modeStamp;           // the film's printed labels, redrawn only when their text changes
};

//==============================================================================
class SpectraKillUI : public juce::Component, private juce::Timer
{
public:
    explicit SpectraKillUI (SpectraKillProcessor&);
    ~SpectraKillUI() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPresetList();
    void stepPreset (int delta);
    void showSaveDialog();
    void showDeleteDialog();
    void setHelp (bool on);
    void drawPlate (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, int number, juce::Random&) const;
    void layoutKnob (juce::Slider&, juce::Label& name, juce::Label& value, juce::Rectangle<int> area, int size);

    SpectraKillProcessor& processor;
    KillLookAndFeel lnf;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::Image logo, staticLayer, grain;      // backdrop, plates, frames: drawn once at the screen's pixel density
    float staticScale = 0.0f;
    void renderStatic (float pixelScale);

    KillDisplay display;

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, deleteButton { "DELETE" }, helpButton { "?" };
    std::unique_ptr<juce::AlertWindow> dialog;

    struct Knob { juce::Slider slider; juce::Label name, value; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach; juce::String id; };
    Knob threshold, down, up, tilt, sharpness, attack, release, gain, mix;
    Knob* knobs[9] { &threshold, &down, &up, &tilt, &sharpness, &attack, &release, &gain, &mix };

    // switch rows tied to choice / bool parameters
    juce::TextButton stereoButtons[3], qualityButtons[sk::kNumQualities], clipButtons[3], sidechainButton { "SIDECHAIN" };
    std::unique_ptr<juce::ParameterAttachment> stereoAttach, qualityAttach, clipAttach, sidechainAttach;

    juce::Label latencyLabel;
    float inPeakDb = -100.0f, outPeakDb = -100.0f, killShown = 0.0f;
    int frame = 0;

    static constexpr int kW = 1040, kH = 700;
    juce::Rectangle<int> topBar, killBox, displayArea, strip, killPanel, shapePanel, timePanel, outPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraKillUI)
};

//==============================================================================
class SpectraKillEditor : public ScalableEditor<SpectraKillUI>
{
public:
    explicit SpectraKillEditor (SpectraKillProcessor& p) : ScalableEditor (p) {}
};
