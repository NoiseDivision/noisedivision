#pragma once
#include "PluginProcessor.h"
#include "ShrapnelLookAndFeel.h"
#include "ScalableEditor.h"

//==============================================================================
// The inspection window: input spectrum (grey), distorted output (red to amber), shards thrown
// off the hardest-hit peaks, and the drive curve (amber, dashed) with draggable points.
//   double-click            adds a point (up to 8): drag it up to hit that area harder, down to spare it
//   drag a point            moves it
//   double-click a point    removes it (right-click works too)
class ShrapnelDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    ShrapnelDisplay (ShrapnelProcessor&, ShrapnelLookAndFeel&);
    void update (float dt);              // called by the editor's timer
    void paint (juce::Graphics&) override;
    void resized() override { gridScale = 0.0f; }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plot() const;
    float xOf (float hz) const;
    float hzOf (float x) const;
    float yOfDb (float db) const;
    float yOfMult (float m) const;
    float multOf (float y) const;
    int pointAt (juce::Point<float>) const;
    float amountFor (int skip, float hz, float mult) const;

    ShrapnelProcessor& processor;
    ShrapnelLookAndFeel& lnf;
    sh::Display data;
    static constexpr int kCols = 300;
    std::array<float, kCols> inCol {}, outCol {}, heatCol {};
    std::vector<sh::CurvePoint> points;
    int hover = -1, dragging = -1;

    struct Shard { float x, y, vx, vy, life, maxLife, size, rot, spin; juce::Colour colour; };
    std::vector<Shard> shards;
    juce::Random rng { 99 };

    juce::Image grid;                   // well, scanlines, grid and labels at the screen's pixel density
    float gridScale = 0.0f;
    int gridW = 0, gridH = 0;
    juce::Image curveImage;             // the drive curve, redrawn only when it changes
    juce::String curveKey;
};

//==============================================================================
class ShrapnelUI : public juce::Component, private juce::Timer
{
public:
    explicit ShrapnelUI (ShrapnelProcessor&);
    ~ShrapnelUI() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPresetList();
    void stepPreset (int delta);
    void showSaveDialog();
    void showDeleteDialog();
    void setHelp (bool on);
    void renderStatic (float pixelScale);
    void drawPlate (juce::Graphics&, juce::Rectangle<float>, const juce::String& title) const;

    ShrapnelProcessor& processor;
    ShrapnelLookAndFeel lnf;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::Image logo, staticLayer;
    float staticScale = 0.0f;

    ShrapnelDisplay display;

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, deleteButton { "DELETE" }, helpButton { "?" };
    std::unique_ptr<juce::AlertWindow> dialog;

    struct Knob { juce::Slider slider; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach; juce::String id; juce::Point<int> centre; int r = 20; };
    Knob drive, character, tone, mix, gain;
    Knob* knobs[5] { &drive, &character, &tone, &mix, &gain };

    struct Fader { juce::Slider slider; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach; juce::String id; juce::Rectangle<int> area; };
    Fader tonal, noise, transient, dynamic;
    Fader* faders[4] { &tonal, &noise, &transient, &dynamic };

    juce::TextButton modeButtons[sh::kNumModes], stereoButtons[3], qualityButtons[sh::kNumQualities], clipButtons[3];
    std::unique_ptr<juce::ParameterAttachment> modeAttach, stereoAttach, qualityAttach, clipAttach;

    float inPeakDb = -100.0f, outPeakDb = -100.0f;
    juce::String lastText;
    int frame = 0;
    double lastTick = 0.0;

    static constexpr int kW = 1100, kH = 720;
    friend class ScalableEditor<ShrapnelUI>;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShrapnelUI)
};

//==============================================================================
class ShrapnelEditor : public ScalableEditor<ShrapnelUI>
{
public:
    explicit ShrapnelEditor (ShrapnelProcessor& p) : ScalableEditor (p) {}
};
