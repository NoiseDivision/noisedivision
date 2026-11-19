#pragma once
#include "PluginProcessor.h"

// MASTERFUSE look: a dark circuit board. The static parts (board, title, bezel, knob
// bodies, fuse) are images rendered from Artwork/mock.html; everything that moves or
// lights up (LED ring, pointers, readout, signal trace, labels) is drawn live.
class FuseLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FuseLookAndFeel();

    juce::Image knobImage, smallKnobImage, fuseOn, fuseOff;
    juce::Typeface::Ptr chakra, montserrat;
    bool active = true;          // false while bypassed: the LEDs and the signal trace go dark

    juce::Font chakraFont (float height, float kerning = 0.0f) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    // help pop-ups in the same style
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;

private:
    juce::TextLayout tooltipLayout (const juce::String&) const;
};

class MasterFuseEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MasterFuseEditor (MasterFuseProcessor&);
    ~MasterFuseEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;      // slides the advanced drawer, updates the level match hover text
    void applyHeight();
    float designHeight() const { return (float) kHeight + (float) kDrawerHeight * drawerOpen; }
    void drawSignalTrace (juce::Graphics&, float s) const;     // the traces and their glow (cached)
    void drawPulses (juce::Graphics&, float s) const;          // the moving pulses (every frame)
    void drawReadout (juce::Graphics&, float s) const;
    void drawDrawer (juce::Graphics&, float s) const;
    void drawHeatGlow (juce::Graphics&, float s) const;
    float amount01() const { return (float) juce::jlimit (0.0, 1.0, knob.getValue() / 100.0); }
    void animateFlow();                 // moves the "current" pulses along the traces, once per screen refresh

    MasterFuseProcessor& processor;
    FuseLookAndFeel lnf;

    juce::Image background;
    juce::TooltipWindow tooltipWindow { this, 700 };

    juce::Slider knob;
    juce::ToggleButton enableSwitch, matchSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment, matchAttachment;

    // advanced drawer: slides out below the main panel
    juce::ToggleButton advancedButton;
    juce::Slider inputKnob, limiterKnob, outputKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAttachment, limiterAttachment, outputAttachment;
    float drawerOpen = 0.0f;            // 0 = shut, 1 = fully open

    // the pulses of "current" flowing along the IN and OUT traces (positions in design pixels)
    juce::VBlankAttachment vblank { this, [this] { animateFlow(); } };
    double lastFrameMs = 0.0, lastRepaintMs = 0.0;

    // the board, its warm glow and the glowing traces only change when the amount, the on/off
    // switch or the size changes, so they are drawn once into this image and reused every frame
    juce::Image boardCache;
    float cacheAmount = -1.0f, cacheScale = 0.0f;
    bool cacheActive = false;
    int cacheWidth = 0;
    float flowIn = 0.0f, flowOut = 0.0f;

    // the design is drawn on a 520 x 680 canvas, plus the drawer when open
    static constexpr int kWidth = 520, kHeight = 680, kDrawerHeight = 160;
    static constexpr int kKnobCentreY = 340;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterFuseEditor)
};
