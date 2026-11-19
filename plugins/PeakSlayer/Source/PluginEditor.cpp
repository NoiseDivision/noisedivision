#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    const float kStart = juce::degreesToRadians (225.0f);
    const float kEnd   = juce::degreesToRadians (495.0f);

    const float kMeterMinDb = -48.0f;

    juce::String dbText (float db)
    {
        return db <= -99.0f ? juce::String ("-inf") : juce::String (db, 1);
    }
}

PeakSlayerUI::PeakSlayerUI (PeakSlayerProcessor& p)
    : processor (p)
{
    setLookAndFeel (&lnf);

    logo = juce::ImageCache::getFromMemory (BinaryData::peakslayer_logo_png,
                                            BinaryData::peakslayer_logo_pngSize);

    // ---- knobs
    auto setupKnob = [this] (juce::Slider& s, int textH)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters (kStart, kEnd, true);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, textH);
        s.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxTextColourId,       Palette::white);
        addAndMakeVisible (s);
    };

    setupKnob (clipSlider, 24);
    clipSlider.getProperties().set ("big", true);

    setupKnob (shapeSlider, 22);
    shapeSlider.getProperties().set ("bipolar", true); // arc grows from the centre
    shapeSlider.getProperties().set ("ends", true);    // SOFT / HARD labels
    shapeSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 90, 22); // read-only readout

    setupKnob (inSlider, 22);
    setupKnob (outSlider, 22);
    inSlider.getProperties().set ("bipolar", true);
    outSlider.getProperties().set ("bipolar", true);
    inSlider.setTextValueSuffix (" dB");
    outSlider.setTextValueSuffix (" dB");

    auto setupLabel = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        l.setColour (juce::Label::textColourId, Palette::text);
        addAndMakeVisible (l);
    };
    setupLabel (shapeLabel, "SHAPE");
    setupLabel (inLabel,    "INPUT");
    setupLabel (outLabel,   "OUTPUT");

    hqButton.setTooltip ("HQ (high quality): runs the clipper at 4x the sample rate to cut the harsh, "
                         "fizzy aliasing that hard clipping adds to the highs. Uses more CPU and adds a few "
                         "samples of latency, which your DAW compensates for. Turn it off for zero latency.");
    addAndMakeVisible (hqButton);

    safeButton.setTooltip ("SAFE: a final hard limit at 0 dB on the plugin's output, after the OUTPUT knob. "
                           "Use it when part of the signal skips the clipper (clip band, or MIX below 100%) "
                           "and peaks could go over. If it works hard it adds its own distortion, so lower OUTPUT instead.");
    addAndMakeVisible (safeButton);

    clipAttach  = std::make_unique<SliderAttach> (p.apvts, "clip",  clipSlider);
    shapeAttach = std::make_unique<SliderAttach> (p.apvts, "shape", shapeSlider);
    inAttach    = std::make_unique<SliderAttach> (p.apvts, "in",    inSlider);
    outAttach   = std::make_unique<SliderAttach> (p.apvts, "out",   outSlider);
    hqAttach    = std::make_unique<ButtonAttach> (p.apvts, "hq",    hqButton);
    safeAttach  = std::make_unique<ButtonAttach> (p.apvts, "safe",  safeButton);

    // Double-click resets every knob to its starting value
    clipSlider.setDoubleClickReturnValue  (true, (double) PeakSlayerProcessor::kClipDefault);
    shapeSlider.setDoubleClickReturnValue (true, 0.0);
    inSlider.setDoubleClickReturnValue    (true, 0.0);
    outSlider.setDoubleClickReturnValue   (true, 0.0);

    // ---- mix (dry/wet)
    mixSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    mixSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
    mixSlider.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    mixSlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    mixSlider.setColour (juce::Slider::textBoxTextColourId,       Palette::white);
    mixSlider.setTextValueSuffix ("%");
    mixSlider.setTooltip ("MIX: blends the clipped signal with the untouched input. 100% is fully clipped, "
                          "0% is the dry input. Below 100% the output can go above the ceiling.");
    addAndMakeVisible (mixSlider);
    mixAttach = std::make_unique<SliderAttach> (p.apvts, "mix", mixSlider);
    mixSlider.setDoubleClickReturnValue (true, 100.0);
    setupLabel (mixLabel, "MIX");

    // ---- filter bar: one slider with two handles, HPF on the left and LPF on the right
    filterSlider.setSliderStyle (juce::Slider::TwoValueHorizontal);
    filterSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    filterSlider.setRange ((double) PeakSlayerProcessor::kFilterMin, (double) PeakSlayerProcessor::kFilterMax, 1.0);
    filterSlider.setSkewFactorFromMidPoint (1000.0);
    filterSlider.setMinAndMaxValues ((double) PeakSlayerProcessor::kFilterMin, (double) PeakSlayerProcessor::kFilterMax,
                                     juce::dontSendNotification);
    filterSlider.setTooltip ("CLIP BAND: only the frequencies between the two handles are clipped. Everything "
                             "outside passes through unclipped. Raise the left handle to keep the low end clean, "
                             "lower the right handle to keep the highs clean. Unclipped parts can go over the "
                             "ceiling, so lower OUTPUT or turn on SAFE.");
    addAndMakeVisible (filterSlider);

    hpfAttach = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter ("hpf"), [this] (float v)
    {
        filterSlider.setMinValue ((double) v, juce::dontSendNotification);
        updateFilterLabels();
    });
    lpfAttach = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter ("lpf"), [this] (float v)
    {
        filterSlider.setMaxValue ((double) v, juce::dontSendNotification);
        updateFilterLabels();
    });

    filterSlider.onDragStart = [this] { hpfAttach->beginGesture(); lpfAttach->beginGesture(); };
    filterSlider.onDragEnd   = [this] { hpfAttach->endGesture();   lpfAttach->endGesture(); };
    filterSlider.onValueChange = [this]
    {
        hpfAttach->setValueAsPartOfGesture ((float) filterSlider.getMinValue());
        lpfAttach->setValueAsPartOfGesture ((float) filterSlider.getMaxValue());
        updateFilterLabels();
    };

    setupLabel (hpLabel, "HPF");
    setupLabel (lpLabel, "LPF");
    lpfAttach->sendInitialUpdate();
    hpfAttach->sendInitialUpdate();

    clipSlider.onValueChange = [this] { updateHeat(); };
    updateHeat();

    // ---- preset bar
    presetBox.setTextWhenNothingSelected ("Default");
    presetBox.setJustificationType (juce::Justification::centredLeft);
    presetBox.onChange = [this]
    {
        const auto name = presetBox.getText();
        if (name.isNotEmpty() && name != processor.presets.getCurrentName())
            processor.presets.load (name);
        deleteButton.setEnabled (! PresetManager::isFactory (presetBox.getText()));
    };
    addAndMakeVisible (presetBox);

    prevButton.onClick   = [this] { stepPreset (-1); };
    nextButton.onClick   = [this] { stepPreset (+1); };
    saveButton.onClick   = [this] { showSaveDialog(); };
    deleteButton.onClick = [this] { showDeleteDialog(); };

    for (auto* b : { &prevButton, &nextButton, &saveButton, &deleteButton })
        addAndMakeVisible (*b);

    refreshPresetList();

    setSize (760, 742);
    startTimerHz (60);
}

PeakSlayerUI::~PeakSlayerUI()
{
    stopTimer();
    dialog.reset();
    setLookAndFeel (nullptr);
}

// The whole UI heats up from steel to red as CLIP approaches 11.
void PeakSlayerUI::updateHeat()
{
    lnf.heat = std::pow ((float) clipSlider.getValue(), 1.6f);
    repaint();
}

// The filter labels show the cutoff, or just the filter's name when it is off.
void PeakSlayerUI::updateFilterLabels()
{
    auto hz = [] (double f)
    {
        return f < 1000.0 ? juce::String (juce::roundToInt (f)) + "Hz"
                          : juce::String (f / 1000.0, 1) + "kHz";
    };

    const double hp = filterSlider.getMinValue(), lp = filterSlider.getMaxValue();
    const bool hpOn = hp > (double) PeakSlayerProcessor::kFilterMin + 0.5;
    const bool lpOn = lp < (double) PeakSlayerProcessor::kFilterMax - 0.5;

    hpLabel.setText (hpOn ? hz (hp) : juce::String ("HPF"), juce::dontSendNotification);
    lpLabel.setText (lpOn ? hz (lp) : juce::String ("LPF"), juce::dontSendNotification);
    hpLabel.setColour (juce::Label::textColourId, hpOn ? Palette::white : Palette::text);
    lpLabel.setColour (juce::Label::textColourId, lpOn ? Palette::white : Palette::text);
}

//==============================================================================
// Presets

void PeakSlayerUI::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);

    int id = 1;
    for (const auto& n : PresetManager::getFactoryNames())
        presetBox.addItem (n, id++);

    const auto user = PresetManager::getUserNames();
    if (! user.isEmpty())
    {
        presetBox.addSeparator();
        for (const auto& n : user)
            presetBox.addItem (n, id++);
    }

    const auto current = processor.presets.getCurrentName();
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (presetBox.getItemText (i) == current)
            presetBox.setSelectedItemIndex (i, juce::dontSendNotification);

    deleteButton.setEnabled (! PresetManager::isFactory (current));
}

void PeakSlayerUI::stepPreset (int delta)
{
    const int n = presetBox.getNumItems();
    if (n == 0)
        return;

    const int current = presetBox.getSelectedItemIndex();
    const int next = current < 0 ? 0 : (current + delta + n) % n;
    presetBox.setSelectedItemIndex (next, juce::sendNotificationSync);
}

void PeakSlayerUI::showSaveDialog()
{
    const auto current = processor.presets.getCurrentName();

    dialog = std::make_unique<juce::AlertWindow> ("Save preset", "Name for this preset:",
                                                  juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel (&lnf);
    dialog->addTextEditor ("name", PresetManager::isFactory (current) ? juce::String ("My Preset") : current);
    dialog->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (dialog == nullptr)
            return;

        const auto name = dialog->getTextEditorContents ("name");
        dialog->setVisible (false);

        if (result == 1 && processor.presets.save (name).isNotEmpty())
            refreshPresetList();
    }), false);

    // put the cursor in the name field, with the suggestion selected so typing replaces it
    if (auto* ed = dialog->getTextEditor ("name"))
    {
        ed->grabKeyboardFocus();
        ed->selectAll();
    }
}

void PeakSlayerUI::showDeleteDialog()
{
    const auto name = presetBox.getText();
    if (name.isEmpty() || PresetManager::isFactory (name))
        return;

    dialog = std::make_unique<juce::AlertWindow> ("Delete preset", "Delete \"" + name + "\"?",
                                                  juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel (&lnf);
    dialog->addButton ("Delete", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, name] (int result)
    {
        if (dialog == nullptr)
            return;

        dialog->setVisible (false);

        if (result == 1 && processor.presets.remove (name))
            refreshPresetList();
    }), false);
}

//==============================================================================

void PeakSlayerUI::PeakDisplay::update (float newDb) noexcept
{
    db = juce::jmax (newDb, db - 0.5f, -100.0f);

    if (newDb >= holdDb)         { holdDb = newDb; holdFrames = 90; }
    else if (--holdFrames <= 0)  { holdDb = juce::jmax (newDb, holdDb - 0.5f, -100.0f); }
}

void PeakSlayerUI::timerCallback()
{
    auto& m = processor.meter;

    inPeak.update  (juce::Decibels::gainToDecibels (processor.inputPeak.exchange (0.0f), -100.0f));
    outPeak.update (juce::Decibels::gainToDecibels (m.peak.exchange (0.0f), -100.0f));

    // RMS and LUFS come from the audio thread; let RMS fall when audio stops
    const auto blocks = m.blocks.load (std::memory_order_relaxed);
    idleFrames = (blocks == lastBlocks) ? idleFrames + 1 : 0;
    lastBlocks = blocks;

    rmsDb = idleFrames > 20 ? juce::jmax (-100.0f, rmsDb - 1.0f) : m.rmsDb.load();
    lufs  = m.lufsShort.load();

    // keep the preset box in step with the plugin state (e.g. after the host restores a project)
    if (++frameCount % 20 == 0 && ! presetBox.isPopupActive())
    {
        const auto current = processor.presets.getCurrentName();
        if (presetBox.getText() != current)
            refreshPresetList();
    }

    repaint (scopeArea.getUnion (meterArea).expanded (4.0f).toNearestInt());
}

void PeakSlayerUI::resized()
{
    const float w = (float) getWidth();

    logoArea  = juce::Rectangle<float> (w * 0.5f - 174.0f, 6.0f, 348.0f, 112.0f);
    scopeArea = juce::Rectangle<float> (20.0f, 166.0f, 544.0f, 236.0f);
    meterArea = juce::Rectangle<float> (574.0f, 166.0f, w - 594.0f, 236.0f);
    panelArea = juce::Rectangle<float> (20.0f, 416.0f, w - 40.0f, 262.0f);

    // preset bar
    const int by = 124, bh = 30;
    prevButton.setBounds   (20,  by, 28,  bh);
    presetBox.setBounds    (52,  by, 158, bh);
    nextButton.setBounds   (214, by, 28,  bh);
    saveButton.setBounds   (248, by, 58,  bh);
    deleteButton.setBounds (310, by, 70,  bh);
    mixLabel.setBounds     (388, by, 32,  bh);
    mixSlider.setBounds    (420, by, 138, bh);
    safeButton.setBounds   (getWidth() - 194, by, 88, bh);
    hqButton.setBounds     (getWidth() - 100, by, 80, bh);

    // filter bar along the bottom
    filterArea = juce::Rectangle<float> (20.0f, 688.0f, w - 40.0f, 40.0f);
    const int fy = (int) filterArea.getY();
    hpLabel.setBounds      (26, fy + 11, 64, 18);
    lpLabel.setBounds      (getWidth() - 90, fy + 11, 64, 18);
    filterSlider.setBounds (92, fy + 4, getWidth() - 184, 32);

    const int py = (int) panelArea.getY();

    // signal flow, left to right: INPUT -> CLIP -> SHAPE -> OUTPUT
    inLabel.setBounds     (45,  py + 62, 110, 18);
    inSlider.setBounds    (45,  py + 82, 110, 124);

    clipSlider.setBounds  (178, py + 10, 230, 246);

    shapeLabel.setBounds  (428, py + 40, 150, 18);
    shapeSlider.setBounds (428, py + 60, 150, 158);

    outLabel.setBounds    (605, py + 62, 110, 18);
    outSlider.setBounds   (605, py + 82, 110, 124);
}

void PeakSlayerUI::paint (juce::Graphics& g)
{
    using namespace juce;

    const float heat = lnf.heat;
    const Colour accent = lnf.accent();
    const Colour edge = Palette::edge.interpolatedWith (Colour (0xff9a1119), heat);

    // ---- background: black steel that turns blood red with heat
    g.setGradientFill (ColourGradient (Colour (0xff18181c).interpolatedWith (Colour (0xff43060b), heat), 0.0f, 0.0f,
                                       Palette::bg.interpolatedWith (Colour (0xff170203), heat), 0.0f, (float) getHeight(), false));
    g.fillAll();

    // glow radiating from the CLIP knob
    {
        const auto kc = clipSlider.getBounds().toFloat().getCentre();
        g.setGradientFill (ColourGradient (Palette::red.withAlpha (0.50f * heat), kc.x, kc.y,
                                           Palette::red.withAlpha (0.0f), kc.x, kc.y + 330.0f, true));
        g.fillRect (getLocalBounds());
    }

    // ---- logo
    {
        const auto lc = logoArea.getCentre();
        g.setGradientFill (ColourGradient (Palette::red.withAlpha (0.10f + 0.30f * heat), lc.x, lc.y,
                                           Palette::red.withAlpha (0.0f), lc.x + 230.0f, lc.y, true));
        g.fillRect (logoArea.expanded (70.0f, 6.0f));

        if (logo.isValid())
        {
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (logo, logoArea, RectanglePlacement::centred);
        }
    }

    // ---- control panel
    g.setColour (Colour (0xff0c0c0f).withAlpha (0.78f));
    g.fillRoundedRectangle (panelArea, 10.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (panelArea, 10.0f, 1.2f);

    // ---- filter bar
    g.setColour (Colour (0xff0c0c0f).withAlpha (0.78f));
    g.fillRoundedRectangle (filterArea, 10.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (filterArea, 10.0f, 1.2f);

    drawMeters (g, edge, heat);
    drawScope (g, edge, accent, heat);
}

//==============================================================================
// Meters: IN and OUT (peak dBFS, with hold lines), then the output's RMS (300 ms)
// and LUFS (short-term, 3 s)

void PeakSlayerUI::drawMeters (juce::Graphics& g, juce::Colour edge, float heat)
{
    using namespace juce;

    g.setColour (Palette::panel.interpolatedWith (Colour (0xff1a0507), heat));
    g.fillRoundedRectangle (meterArea, 8.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (meterArea, 8.0f, 1.2f);

    const float top    = meterArea.getY() + 26.0f;
    const float bottom = meterArea.getBottom() - 26.0f;
    auto yOfDb = [&] (float db)
    {
        return jmap (jlimit (kMeterMinDb, 0.0f, db), kMeterMinDb, 0.0f, bottom, top);
    };

    // scale
    g.setFont (Font (FontOptions (9.5f)));
    for (int db : { 0, -6, -12, -24, -36, -48 })
    {
        const float y = yOfDb ((float) db);
        g.setColour (Palette::text);
        g.drawText (String (db), Rectangle<float> (meterArea.getX() + 2.0f, y - 6.0f, 22.0f, 12.0f),
                    Justification::centredRight, false);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine ((int) y, meterArea.getX() + 28.0f, meterArea.getRight() - 6.0f);
    }

    struct Bar { const char* name; float value; const PeakDisplay* peak; };
    const Bar bars[] = { { "IN",   inPeak.db,  &inPeak },
                         { "OUT",  outPeak.db, &outPeak },
                         { "RMS",  rmsDb,      nullptr },
                         { "LUFS", lufs,       nullptr } };

    const float barW = 26.0f, gap = 5.0f;
    float x = meterArea.getX() + 31.0f;

    for (const auto& b : bars)
    {
        const Rectangle<float> track (x, top, barW, bottom - top);

        g.setFont (Font (FontOptions (10.0f, Font::bold)));
        g.setColour (Palette::text);
        g.drawText (b.name, Rectangle<float> (x - 4.0f, meterArea.getY() + 7.0f, barW + 8.0f, 12.0f),
                    Justification::centred, false);

        g.setColour (Colour (0xff09090b));
        g.fillRoundedRectangle (track, 3.0f);

        if (b.value > kMeterMinDb)
        {
            // steel below about -14, turning red towards 0
            g.setGradientFill (ColourGradient (Palette::red, 0.0f, yOfDb (-1.0f),
                                               Palette::steel, 0.0f, yOfDb (-14.0f), false));
            g.fillRoundedRectangle (track.withTop (yOfDb (b.value)), 3.0f);
        }

        if (b.peak != nullptr && b.peak->holdDb > kMeterMinDb)
        {
            g.setColour (b.peak->holdDb >= PeakSlayerProcessor::kCeilingDb - 0.05f ? Palette::red : Palette::white);
            g.fillRect (Rectangle<float> (x, yOfDb (b.peak->holdDb) - 1.0f, barW, 2.0f));
        }

        g.setFont (Font (FontOptions (10.5f, Font::bold)));
        g.setColour (Palette::white);
        g.drawText (dbText (b.peak != nullptr ? b.peak->holdDb : b.value),
                    Rectangle<float> (x - 5.0f, bottom + 6.0f, barW + 10.0f, 14.0f), Justification::centred, false);

        x += barW + gap + (b.peak == &outPeak ? 6.0f : 0.0f);
    }
}

//==============================================================================
// Scope: output in white, the slain peaks (what the clipper removed) in red

void PeakSlayerUI::drawScope (juce::Graphics& g, juce::Colour edge, juce::Colour accent, float heat)
{
    using namespace juce;

    g.setColour (Palette::panel.interpolatedWith (Colour (0xff1a0507), heat));
    g.fillRoundedRectangle (scopeArea, 8.0f);
    g.setColour (accent.withAlpha (0.10f + 0.25f * heat));
    g.drawRoundedRectangle (scopeArea.expanded (1.5f), 9.5f, 3.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (scopeArea, 8.0f, 1.2f);

    Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (scopeArea.reduced (1.0f).toNearestInt());

    const float displayRange = 1.6f; // headroom above the ceiling, so removed peaks are visible
    const float cy = scopeArea.getCentreY();
    const float halfH = scopeArea.getHeight() * 0.5f;
    auto yOf = [&] (float v) { return cy - (v / displayRange) * halfH; };

    // grid
    g.setColour (Colours::white.withAlpha (0.045f));
    for (float lv : { -0.5f, 0.0f, 0.5f })
        g.drawHorizontalLine ((int) yOf (lv), scopeArea.getX(), scopeArea.getRight());
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine ((int) (scopeArea.getX() + scopeArea.getWidth() * (float) i / 8.0f),
                            scopeArea.getY(), scopeArea.getBottom());

    // ceiling lines
    const float ceilLin = PeakSlayerProcessor::kCeilingLin;
    g.setColour (Palette::red.withAlpha (0.60f));
    const float dashes[] = { 6.0f, 4.0f };
    for (float sign : { 1.0f, -1.0f })
    {
        const float y = yOf (sign * ceilLin);
        g.drawDashedLine ({ scopeArea.getX(), y, scopeArea.getRight(), y }, dashes, 2, 1.0f);
    }

    // waveform
    const int mask = PeakSlayerProcessor::scopeSize - 1;
    const int end = processor.scopeWrite.load (std::memory_order_acquire) - 128; // stay clear of the write head
    const int start = end - samplesShown;
    const int width = jmax (1, (int) scopeArea.getWidth());
    const float eps = 0.012f;
    bool anyClip = false;

    for (int px = 0; px < width; ++px)
    {
        const int i0 = start + (px * samplesShown) / width;
        const int i1 = jmax (i0 + 1, start + ((px + 1) * samplesShown) / width);

        float ilo = 1e9f, ihi = -1e9f, olo = 1e9f, ohi = -1e9f;
        for (int i = i0; i < i1; ++i)
        {
            const float vi = processor.scopeIn [(size_t) (i & mask)].load (std::memory_order_relaxed);
            const float vo = processor.scopeOut[(size_t) (i & mask)].load (std::memory_order_relaxed);
            ilo = jmin (ilo, vi); ihi = jmax (ihi, vi);
            olo = jmin (olo, vo); ohi = jmax (ohi, vo);
        }

        const float x = scopeArea.getX() + (float) px + 0.5f;

        g.setColour (Palette::white.withAlpha (0.92f));
        g.drawLine (x, yOf (ohi) - 0.75f, x, yOf (olo) + 0.75f, 1.0f); // min 1.5 px, so flat tops stay visible

        auto drawRemoved = [&] (float a, float b)
        {
            g.setColour (Palette::red.withAlpha (0.28f));
            g.drawLine (x, a, x, b, 3.5f);
            g.setColour (Palette::red);
            g.drawLine (x, a, x, b, 1.3f);
        };

        if (ihi > ohi + eps) { drawRemoved (yOf (ihi), yOf (ohi)); anyClip = true; }
        if (ilo < olo - eps) { drawRemoved (yOf (olo), yOf (ilo)); anyClip = true; }
    }

    // labels + slaying LED
    g.setFont (Font (FontOptions (11.0f, Font::bold)));
    g.setColour (Palette::text);
    g.drawText ("CEILING -0.1 dB", Rectangle<float> (scopeArea.getX() + 12.0f, scopeArea.getY() + 6.0f, 140.0f, 14.0f),
                Justification::centredLeft, false);

    const float ledX = scopeArea.getRight() - 88.0f;
    const float ledY = scopeArea.getY() + 8.0f;
    if (anyClip)
    {
        g.setColour (Palette::red.withAlpha (0.3f));
        g.fillEllipse (ledX - 4.0f, ledY - 4.0f, 17.0f, 17.0f);
    }
    g.setColour (anyClip ? Palette::red : Palette::unlit);
    g.fillEllipse (ledX, ledY, 9.0f, 9.0f);
    g.setColour (anyClip ? Palette::white : Palette::text);
    g.drawText ("SLAYING", Rectangle<float> (ledX + 14.0f, ledY - 3.0f, 64.0f, 14.0f), Justification::centredLeft, false);
}
