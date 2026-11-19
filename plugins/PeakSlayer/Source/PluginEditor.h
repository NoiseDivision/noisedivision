#pragma once
#include "PluginProcessor.h"
#include "ScalableEditor.h"
#include "SlayerLookAndFeel.h"

// The UI at its design size. PeakSlayerEditor (below) wraps it to make it resizable.
class PeakSlayerUI : public juce::Component, private juce::Timer
{
public:
    explicit PeakSlayerUI (PeakSlayerProcessor&);
    ~PeakSlayerUI() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateHeat();
    void updateFilterLabels();

    void drawScope  (juce::Graphics&, juce::Colour edge, juce::Colour accent, float heat);
    void drawMeters (juce::Graphics&, juce::Colour edge, float heat);

    // presets
    void refreshPresetList();
    void stepPreset (int delta);
    void showSaveDialog();
    void showDeleteDialog();

    PeakSlayerProcessor& processor;
    SlayerLookAndFeel lnf;                      // declared before the widgets so it outlives them
    std::unique_ptr<juce::AlertWindow> dialog;  // save / delete prompt
    juce::TooltipWindow tooltipWindow { this, 500 }; // shows hover descriptions after 0.5 s

    juce::Image logo;
    juce::Rectangle<float> logoArea, scopeArea, meterArea, panelArea, filterArea;

    juce::Slider clipSlider, shapeSlider, inSlider, outSlider;
    juce::Label shapeLabel, inLabel, outLabel;

    juce::Slider mixSlider;      // dry/wet
    juce::Slider filterSlider;   // two handles: HPF (left) and LPF (right)
    juce::Label mixLabel, hpLabel, lpLabel;
    juce::ToggleButton hqButton { "HQ" }, safeButton { "SAFE" };

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, deleteButton { "DELETE" };

    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttach> clipAttach, shapeAttach, inAttach, outAttach, mixAttach;
    std::unique_ptr<juce::ParameterAttachment> hpfAttach, lpfAttach;
    std::unique_ptr<ButtonAttach> hqAttach, safeAttach;

    // meter display state (updated on the timer)
    struct PeakDisplay
    {
        float db = -100.0f, holdDb = -100.0f;
        int holdFrames = 0;
        void update (float newDb) noexcept;   // fast attack, 30 dB/s release, 1.5 s hold
    };
    PeakDisplay inPeak, outPeak;
    float rmsDb = -100.0f, lufs = -100.0f;
    int idleFrames = 0, frameCount = 0;
    std::uint32_t lastBlocks = 0;

    static constexpr int samplesShown = 4096; // ~85 ms at 48 kHz

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakSlayerUI)
};

//==============================================================================
// The plugin window: the UI above, scaled to whatever size the window is dragged to.
class PeakSlayerEditor : public ScalableEditor<PeakSlayerUI>
{
public:
    explicit PeakSlayerEditor (PeakSlayerProcessor& p) : ScalableEditor (p) {}
};
