#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

namespace flui
{
    const juce::Colour bg (0xff0f1314), panel (0xff151a1c), screen (0xff060908), line (0xff252e30);
    const juce::Colour green (0xff46f08f), greenHot (0xffd4ffe6), mint (0xff9dffc6), amber (0xffffb340), red (0xffff4d5a);
    const juce::Colour text (0xffe8eeee), dim (0xff7a8689), faint (0xff3c4749);
}

class FlatLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FlatLookAndFeel();
    juce::Typeface::Ptr oswald, mont;
    juce::Font title (float size, float tracking = 0.0f) const;
    juce::Font body (float size, float tracking = 0.0f) const;
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int, int) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int, int) override;
};

// GAIN: a round tank that fills from the bottom; the surface is a flat glowing line (the logo's cut)
class GainTank : public juce::Component, public juce::SettableTooltipClient
{
public:
    GainTank (juce::RangedAudioParameter&, FlatLookAndFeel&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = zone (e); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = 0; repaint(); }
private:
    int zone (const juce::MouseEvent&) const;   // 1 up arrow, -1 down arrow, 0 body
    void nudge (float db);
    juce::RangedAudioParameter& prm;
    FlatLookAndFeel& lnf;
    juce::ParameterAttachment att;
    float value = 0.0f, dragStart = 0.0f; int hover = 0; bool dragging = false;
};

// the monitor: WAVE (output, gain reduction from the top, clip marks, lift from the bottom) or SPECTRUM
class Monitor : public juce::Component, public juce::SettableTooltipClient
{
public:
    Monitor (FlatlineProcessor&, FlatLookAndFeel&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void update();
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    int alarm = 0;   // 0 fine, 1 working hard, 2 over-limited (colours the loudness readouts)
private:
    void paintWave (juce::Graphics&);
    void paintSpectrum (juce::Graphics&);
    bool spectrum() const;
    FlatlineProcessor& proc; FlatLookAndFeel& lnf;
    juce::TextButton waveBtn { "WAVE" }, specBtn { "SPECTRUM" };
    std::vector<fl::Engine::Column> cols;
    juce::dsp::FFT fft { 12 };
    std::vector<float> fftBuf, win, specPre, specPost;
    bool dragCeil = false; float dragStartDb = 0;
    float ceilY = 70.0f;
};

class FlatContent : public juce::Component, private juce::Timer
{
public:
    static constexpr int kW = 1100, kH = 720;
    FlatContent (FlatlineProcessor&, FlatLookAndFeel&);
    ~FlatContent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    void setHelp (bool on);
    void renderBackground (float scale);
    void startLearn();
    void refreshPresetBox();

    struct Knob { juce::Slider s; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a; juce::String id, label; juce::Point<int> c; int r = 28; };
    FlatlineProcessor& proc; FlatLookAndFeel& lnf;
    Monitor monitor;
    GainTank tank;
    Knob ceiling, release, link, transient, upAmount, upBelow, clipDrive;
    Knob* knobs[7] { &ceiling, &release, &link, &transient, &upAmount, &upBelow, &clipDrive };
    juce::TextButton styleBtn[4], autoBtn { "AUTO" }, upLed, clipLed, softBtn { "SOFT" }, hardBtn { "HARD" },
                     bypassBtn { juce::String::fromUTF8 ("BYPASS \xc2\xb7 LEVEL MATCHED") }, deltaBtn { "DELTA" }, advBtn { "ADVANCED" },
                     learnBtn { "LEARN" }, tMinus { "-" }, tPlus { "+" }, prevBtn { "<" }, nextBtn { ">" }, helpBtn { "?" };
    juce::ComboBox presetBox;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
    std::unique_ptr<juce::ButtonParameterAttachment> autoAtt, upAtt, clipAtt, bypassAtt, deltaAtt;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::Image bg; float bgScale = 0;

    // shown values (smoothed for the eye)
    float lufsS = -120, lufsI = -120, tp = -120, gr = 0, grMax = 0, clip = 0, lift = 0, clean = 100, plr = 0;
    // LEARN
    bool learning = false; int learnTicks = 0, learnGood = 0;
    // the last 10 s of short-term loudness at the current GAIN (cleared when GAIN moves), so LEARN can act at once
    std::vector<float> hist; int settle = 0; float lastGain = -1.0e9f, calib = 0;   // lastGain: a signature of the settings
    juce::String learnMsg;
};

class FlatlineEditor : public juce::AudioProcessorEditor
{
public:
    explicit FlatlineEditor (FlatlineProcessor&);
    ~FlatlineEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0b0e0f)); }
private:
    FlatLookAndFeel lnf;
    FlatContent content;
    bool ready = false;   // don't remember the size until the window has its real one
};
