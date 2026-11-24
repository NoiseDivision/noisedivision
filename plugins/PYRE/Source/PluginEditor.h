#pragma once
#include "PluginProcessor.h"

// PYRE look: a fire pit. A charred iron panel, the harmonics drawn as flames that rise and even out as HEAT goes up,
// embers drifting up from the bottom, and forged knobs with a heat ring that glows hotter the further they turn.
class PyreLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PyreLookAndFeel();
    juce::Typeface::Ptr oswald, mont;
    juce::Font title (float size, float tracking = 0.0f) const;
    juce::Font body (float size, float tracking = 0.0f) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};

class PyreContent : public juce::Component, private juce::Timer
{
public:
    static constexpr int kW = 920, kH = 620;
    PyreContent (PyreProcessor&, PyreLookAndFeel&);
    ~PyreContent() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void renderBackground (float scale);
    void setHelp (bool on);
    void drawFire (juce::Graphics&);

    struct Knob
    {
        juce::Slider s;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a;
        juce::String id, label;
        juce::Point<int> c; int r = 40;
    };
    PyreProcessor& proc;
    PyreLookAndFeel& lnf;
    Knob ignite, heat, tilt, burn, maxRaise, mix, output;
    Knob* knobs[7] { &ignite, &heat, &tilt, &burn, &maxRaise, &mix, &output };

    juce::Image bg; float bgScale = 0.0f;
    juce::TextButton helpButton { "?" };
    std::unique_ptr<juce::TooltipWindow> tooltips;
    std::unique_ptr<juce::PropertiesFile> settings;
    // the fire: each band's flame (dB), smoothed for the eye
    std::array<float, pyre::kBands> shownAfter {}, shownBefore {};
    struct Ember { float x, y, vx, vy, life, size; };
    std::vector<Ember> embers;
    juce::Random rnd;
    float t = 0.0f, louder = 0.0f, burnGlow = 0.0f, inDb = -100.0f, outDb = -100.0f;
    float anchorHz = 0.0f, anchorDb = -120.0f;
    juce::Rectangle<float> pit() const { return { 30.0f, 96.0f, (float) kW - 60.0f, 300.0f }; }
    float xOfHz (float hz) const;
    float yOfDb (float db) const;
};

class PyreEditor : public juce::AudioProcessorEditor
{
public:
    explicit PyreEditor (PyreProcessor&);
    ~PyreEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0b0807)); }

private:
    PyreLookAndFeel lnf;
    PyreContent content;
    bool ready = false;   // don't remember the size until the window has its real one
};
