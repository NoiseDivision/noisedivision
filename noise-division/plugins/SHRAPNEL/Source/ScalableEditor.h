#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Makes a fixed-size plugin UI resizable: the UI keeps its design size and layout and
// is simply drawn scaled (60 % to 200 %, same proportions). Drag the bottom-right corner
// to resize; double-click the corner area to return to 100 %. The size is saved with
// the project (in the plugin state, property "uiScale").
template <class UI>
class ScalableEditor : public juce::AudioProcessorEditor
{
public:
    template <class Processor>
    explicit ScalableEditor (Processor& p)
        : AudioProcessorEditor (&p), state (p.apvts.state), ui (p)
    {
        designW = juce::jmax (1, ui.getWidth());
        designH = juce::jmax (1, ui.getHeight());
        addAndMakeVisible (ui);

        // read the saved size first: setting the limits below already resizes the window
        const double scale = juce::jlimit ((double) kMinScale, (double) kMaxScale, (double) state.getProperty ("uiScale", 1.0));

        setResizable (true, true);
        getConstrainer()->setFixedAspectRatio ((double) designW / (double) designH);
        setResizeLimits (juce::roundToInt (designW * kMinScale), juce::roundToInt (designH * kMinScale),
                         juce::roundToInt (designW * kMaxScale), juce::roundToInt (designH * kMaxScale));
        setSize (juce::roundToInt (designW * scale), juce::roundToInt (designH * scale));
        ready = true;
    }

    void resized() override
    {
        const float scale = (float) getWidth() / (float) designW;
        ui.setBounds (0, 0, designW, designH);
        ui.setTransform (juce::AffineTransform::scale (scale));
        if (ready)                       // don't save the sizes the window passes through while opening
            state.setProperty ("uiScale", scale, nullptr);
    }

    // double-click near the resize corner: back to 100 %
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.x > getWidth() - 24 && e.y > getHeight() - 24)
            setSize (designW, designH);
    }

private:
    static constexpr float kMinScale = 0.6f, kMaxScale = 2.0f;
    juce::ValueTree state;
    UI ui;
    int designW = 1, designH = 1;
    bool ready = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScalableEditor)
};
