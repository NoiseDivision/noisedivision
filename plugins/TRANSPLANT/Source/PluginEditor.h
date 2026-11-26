#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

namespace tpc   // the Surgical palette
{
    const juce::Colour bg (0xff0b1114), hdr (0xff0d1417), panel (0xff121b1f), panel2 (0xff172227), line (0xff22313a),
                       cy (0xff2ee6d6), hot (0xffc8fffa), co (0xffff5a4e), text (0xffe8f4f2), dim (0xff7f8a90), faint (0xff4a5860), you (0xff9aa3a8);
}

class TpLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TpLookAndFeel();
    juce::Typeface::Ptr oswald, mont;
    juce::Font title (float size, float kerning = 0.12f) const { return juce::Font (juce::FontOptions (oswald).withHeight (size)).withExtraKerningFactor (kerning); }
    juce::Font body (float size) const { return juce::Font (juce::FontOptions (mont).withHeight (size)); }
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return body (12.5f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return body (13.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int, int) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int, int) override;
};

// a knob: the slider plus its name and value underneath
class Knob : public juce::Component
{
public:
    Knob (TransplantProcessor&, const juce::String& paramId, const juce::String& label, TpLookAndFeel&);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Slider slider;
private:
    TpLookAndFeel& lnf; juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// a value you drag up and down (OUT in the header)
class DragBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragBox (juce::RangedAudioParameter&, const juce::String& label, TpLookAndFeel&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    juce::RangedAudioParameter& prm; juce::String label; TpLookAndFeel& lnf;
    juce::ParameterAttachment att; float value = 0, start = 0;
};

class TransplantContent;

// the big display: spectra, the curve and its points, the sections of your track, and the drawers on top
class Display : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    Display (TransplantProcessor&, TpLookAndFeel&, TransplantContent&);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void update (const TransplantProcessor::Snapshot&, int viewIndex);
    void updateLive();
    int shownView() const { return view; }
    void openDrawer (int which);   // 0 none, 1 KEY, 2 SECTIONS, 3 REFERENCES (MATCH is separate)
    void toggleMatch();
    bool keyOpen() const { return top == 1; }

    juce::TextButton keyTab { "KEY" }, secTab { "SECTIONS" }, refTab { "REFERENCES" }, matchTab { "MATCH" }, meterTab { "METERS" };
    std::unique_ptr<juce::Component> keyDrawer, secDrawer, refDrawer, matchDrawer, meterDrawer;
    void toggleMeters();
    juce::TextButton pointX { "X" };
    juce::TextButton yoursBtn { "YOURS" }, refBtn { "REF" }, matchBtn { "LEVEL MATCH" };
    std::unique_ptr<juce::ParameterAttachment> listenAtt;
    std::unique_ptr<juce::ButtonParameterAttachment> matchAtt;

private:
    // coordinates
    float xOfBin (double b) const;
    double binOfX (float x) const;
    float yOfCurve (double db) const;
    double curveOfY (float y) const;
    float yOfSpec (double db) const;
    juce::Rectangle<float> plot() const;
    juce::Rectangle<float> timeline() const;
    juce::Rectangle<float> zoneDonor() const, zonePatient() const;
    bool emptyState() const;
    int nodeAt (juce::Point<float>) const;
    juce::Path specPath (const tp::Spec&, bool fill) const;
    void showPointMenu (int node);

    TransplantProcessor& proc; TpLookAndFeel& lnf; TransplantContent& owner;
    TransplantProcessor::Snapshot snap;
    TransplantProcessor::View v; bool hasView = false;
    int view = -1;            // index in snap.views (the last one = whole track)
    std::vector<tp::Node> nodes; tp::Spec curve {};
    float specTop = 0;
    int hover = -1, selected = -1, dragging = -1;
    float dragBin = -1; bool dragMoved = false;
    juce::String dragView;
    bool dragOver = false;
    int top = 0; bool matchOpen = false;
    // live analyser
    juce::dsp::FFT fft { 13 };
    std::vector<float> fftBuf, win; tp::Spec liveSpec {}; bool liveValid = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

class TransplantContent : public juce::Component, private juce::Timer
{
public:
    static constexpr int kW = 1200, kH = 780;
    TransplantContent (TransplantProcessor&, TpLookAndFeel&);
    ~TransplantContent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void chooseReference();
    void chooseMine();
    void mineMenu();
    void refreshAll();
    TransplantProcessor& proc;
    TpLookAndFeel& lnf;
private:
    void timerCallback() override;
    void drawLogo (juce::Graphics&, juce::Rectangle<float>);
    void refreshPresets();
    Display display;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, learnBtn { "LEARN" }, aBtn { "A" }, bBtn { "B" }, undoBtn, redoBtn, bypassBtn { "BYPASS" }, helpBtn { "?" };
    juce::ComboBox presetBox;
    juce::Rectangle<int> refBox, mineBox;
    DragBox outBox;
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAtt;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastVersion = -1;
    bool help = false;
    juce::String footerL, footerR;
};

class TransplantEditor : public juce::AudioProcessorEditor
{
public:
    explicit TransplantEditor (TransplantProcessor&);
    ~TransplantEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (tpc::bg); }
private:
    TpLookAndFeel lnf;
    TransplantContent content;
    bool ready = false;
};
