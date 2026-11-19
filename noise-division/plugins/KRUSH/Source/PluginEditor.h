#pragma once
#include "PluginProcessor.h"
#include "ScalableEditor.h"
#include "KrushLookAndFeel.h"

//==============================================================================
// The big display at the top: either the spectrum with the bands and draggable
// crossovers, or a scrolling waveform (input in grey, output in red).
class DisplayView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DisplayView (KrushProcessor&, KrushLookAndFeel&);

    void update();                         // called by the editor's timer before repainting
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    bool waveMode = false;
    int selectedBand = 0;
    std::function<void (int)> onBandClicked;

private:
    float xOfFreq (float f) const;
    float freqOfX (float x) const;
    int numBands() const;
    float crossover (int k) const;
    int crossoverAt (float x) const;
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void drawWave (juce::Graphics&, juce::Rectangle<float>);

    KrushProcessor& processor;
    KrushLookAndFeel& lnf;

    static constexpr int kFftOrder = 12, kFftSize = 1 << kFftOrder;
    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> fftData = std::vector<float> (2 * kFftSize, 0.0f);
    std::vector<float> spectrumDb = std::vector<float> (kFftSize / 2 + 1, -120.0f);

    int hover = -1, dragging = -1;
};

//==============================================================================
// The transfer curve of the selected band, as set by DRIVE, SHAPE and BIAS.
class CurveView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CurveView (KrushProcessor& p) : processor (p) {}
    void paint (juce::Graphics&) override;
    int band = 0;
private:
    KrushProcessor& processor;
};

//==============================================================================
// The UI at its design size. KrushEditor (below) wraps it to make it resizable.
class KrushUI : public juce::Component, private juce::Timer
{
public:
    explicit KrushUI (KrushProcessor&);
    ~KrushUI() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectBand (int b);
    void setParam (const juce::String& id, float realValue);
    void drawMeters (juce::Graphics&);
    void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title) const;
    void updateFilterLabels();

    // presets
    void refreshPresetList();
    void stepPreset (int delta);
    void showSaveDialog();
    void showDeleteDialog();

    KrushProcessor& processor;
    KrushLookAndFeel lnf;
    std::unique_ptr<juce::TooltipWindow> tooltips;     // only exists while help is on
    std::unique_ptr<juce::PropertiesFile> settings;     // remembers the help switch for all KRUSH instances
    void setHelp (bool on);
    juce::Image logo;

    DisplayView display;
    CurveView curve;

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, deleteButton { "DELETE" };
    std::unique_ptr<juce::AlertWindow> dialog;
    int frameCount = 0;

    juce::TextButton spectrumButton { "SPECTRUM" }, waveButton { "WAVEFORM" };
    juce::TextButton helpButton { "?" };
    juce::TextButton countButtons[4], bandTabs[4];
    juce::TextButton soloButton { "SOLO" }, bypassButton { "BYPASS" };
    juce::TextButton modeButtons[krush::kNumModes];

    juce::Slider driveFader, gainFader, bandMixFader, shapeKnob, biasKnob, dynKnob;
    juce::Label driveLabel, gainLabel, bandMixLabel, shapeLabel, biasLabel, dynLabel;

    juce::Slider inputFader, outputFader, mixSlider, filterSlider;
    juce::Label hpLabel, lpLabel;
    juce::TextButton clipButton { "CLIP" }, limitButton { "LIMIT" };   // output safety stage
    juce::TextButton slopeButtons[5];          // tone filter slope: 6 / 12 / 18 / 24 / 48 dB per octave

    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttach> driveAttach, gainAttach, bandMixAttach, shapeAttach, biasAttach, dynAttach;
    std::unique_ptr<ButtonAttach> soloAttach, bypassAttach, clipAttach, limitAttach;
    float shownReduction = 0.0f;
    std::unique_ptr<SliderAttach> inputAttach, outputAttach, mixAttach;
    std::unique_ptr<juce::ParameterAttachment> hpfAttach, lpfAttach;

    int band = 0;

    // meter display state
    struct Bar { float peakDb = -100.0f, holdDb = -100.0f, rmsDb = -100.0f; int holdFrames = 0; };
    Bar bars[4];                               // in L, in R, out L, out R

    juce::Rectangle<int> displayArea, gainPanel, shapePanel, modePanel, meterPanel, filterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KrushUI)
};

//==============================================================================
// The plugin window: the UI above, scaled to whatever size the window is dragged to.
class KrushEditor : public ScalableEditor<KrushUI>
{
public:
    explicit KrushEditor (KrushProcessor& p) : ScalableEditor (p) {}
};
