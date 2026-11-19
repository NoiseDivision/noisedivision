#pragma once
#include "PluginProcessor.h"

// DARK look: a night-time room. The static parts (the room at full light and in full dark,
// the title, the knob housing, the moon faces, the candle) are images rendered from
// Artwork/art.html; everything that moves (the eclipse, ticks, pointer, waves, flame,
// readout, switches) is drawn live.
class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel();

    juce::Image moonPale, moonBlood, eclipseShadow, smallKnob, candle;
    juce::Typeface::Ptr oswald, montserrat;
    bool active = true;          // false while bypassed: the ticks go grey and the moon stays full

    juce::Font oswaldFont (float cssSize, float trackingPx = 0.0f) const;
    juce::Font montFont (float cssSize, float trackingPx = 0.0f) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;

    float flicker = 0.0f, smokeTime = 0.0f;     // candle animation, advanced by the editor

private:
    juce::TextLayout tooltipLayout (const juce::String&) const;
};

class DarkEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DarkEditor (DarkProcessor&);
    ~DarkEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;      // slides the advanced drawer
    void applyHeight();
    float designHeight() const { return (float) kHeight + (float) kDrawerHeight * drawerOpen; }
    float amount01() const { return (float) juce::jlimit (0.0, 1.0, knob.getValue() / 100.0); }
    float darkness() const { return lnf.active ? amount01() : 0.15f; }
    void animate();                     // waves, flame and smoke, once per screen refresh
    void drawWaves (juce::Graphics&, float s) const;
    void drawReadout (juce::Graphics&, float s) const;
    void drawEmblem (juce::Graphics&, float s) const;
    void drawDrawer (juce::Graphics&, float s) const;

    DarkProcessor& processor;
    DarkLookAndFeel lnf;
    juce::Image bgLight, bgDark;
    juce::TooltipWindow tooltipWindow { this, 700 };

    juce::Slider knob;
    juce::ToggleButton enableSwitch, advancedButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment;

    juce::Slider punchKnob, mixKnob, outputKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> punchAttachment, mixAttachment, outputAttachment;
    float drawerOpen = 0.0f;

    juce::VBlankAttachment vblank { this, [this] { animate(); } };
    double lastFrameMs = 0.0, lastRepaintMs = 0.0;
    float wavePhase = 0.0f;

    // the room (light and dark blended for the current amount) only changes with the amount,
    // the on/off switch or the size, so it is drawn once into this image and reused
    juce::Image roomCache;
    float cacheDark = -1.0f, cacheScale = 0.0f;
    int cacheWidth = 0;

    static constexpr int kWidth = 520, kHeight = 680, kDrawerHeight = 160;
    static constexpr int kKnobCentreY = 340;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DarkEditor)
};
