#include "PluginEditor.h"
#include "BinaryData.h"
#include <tuple>

namespace
{
    constexpr int kW = 1000, kH = 616;
    constexpr float kMinDb = -60.0f, kMaxDb = 6.0f;

    float meterPos (float db)      // 0 at the bottom of a meter, 1 at the top
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
        return std::pow (t, 1.6f);
    }

    juce::String hzText (double f)
    {
        return f < 1000.0 ? juce::String (juce::roundToInt (f)) + " Hz" : juce::String (f / 1000.0, f < 10000.0 ? 2 : 1) + " kHz";
    }
}

//==============================================================================
// Display: spectrum with bands, or scrolling waveform
//==============================================================================
DisplayView::DisplayView (KrushProcessor& p, KrushLookAndFeel& l) : processor (p), lnf (l) {}

int DisplayView::numBands() const { return (int) processor.apvts.getRawParameterValue ("bands")->load() + 1; }
float DisplayView::crossover (int k) const { return processor.apvts.getRawParameterValue ("x" + juce::String (k + 1))->load(); }

float DisplayView::xOfFreq (float f) const
{
    return (float) getWidth() * std::log (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f);
}

float DisplayView::freqOfX (float x) const
{
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, x / (float) juce::jmax (1, getWidth())));
}

int DisplayView::crossoverAt (float x) const
{
    if (waveMode) return -1;
    int best = -1; float bestDist = 7.0f;
    for (int k = 0; k < numBands() - 1; ++k)
    {
        const float d = std::abs (xOfFreq (crossover (k)) - x);
        if (d < bestDist) { bestDist = d; best = k; }
    }
    return best;
}

void DisplayView::update()
{
    if (waveMode) return;

    // copy the newest 4096 output samples, window them, take the spectrum
    const int w = processor.specWrite.load (std::memory_order_acquire);
    for (int i = 0; i < kFftSize; ++i)
        fftData[(size_t) i] = processor.specRing[(size_t) ((w + i) & (KrushProcessor::kSpecSize - 1))].load (std::memory_order_relaxed);
    std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) kFftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    for (int i = 0; i <= kFftSize / 2; ++i)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] * 4.0f / (float) kFftSize, -120.0f);
        auto& s = spectrumDb[(size_t) i];
        s = db > s ? db : juce::jmax (db, s - 1.6f);            // fast rise, gentle fall
    }
}

void DisplayView::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = getLocalBounds().toFloat();
    g.setColour (Pal::well);
    g.fillRoundedRectangle (r, 4.0f);

    Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (getLocalBounds().reduced (1));
    if (waveMode) drawWave (g, r); else drawSpectrum (g, r);

    g.setColour (Pal::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
}

void DisplayView::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace juce;
    const float top = r.getY() + 22.0f, bottom = r.getBottom() - 16.0f;
    const float dbTop = 0.0f, dbBottom = -90.0f;
    auto yOfDb = [&] (float db) { return jmap (jlimit (dbBottom, dbTop, db), dbBottom, dbTop, bottom, top); };

    // ---- bands: tinted regions with name labels
    const int nb = numBands();
    float edges[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    edges[0] = 0.0f;
    for (int k = 0; k < nb - 1; ++k) edges[k + 1] = xOfFreq (crossover (k));
    edges[nb] = r.getWidth();

    bool anySolo = false;
    for (int b = 0; b < nb; ++b) anySolo |= processor.apvts.getRawParameterValue (KrushProcessor::bandId (b, "solo"))->load() > 0.5f;

    for (int b = 0; b < nb; ++b)
    {
        const auto col = Pal::band (b);
        const auto region = Rectangle<float> (edges[b], r.getY(), edges[b + 1] - edges[b], r.getHeight());
        const bool sel = b == selectedBand;
        const bool solo = processor.apvts.getRawParameterValue (KrushProcessor::bandId (b, "solo"))->load() > 0.5f;
        const bool bypass = processor.apvts.getRawParameterValue (KrushProcessor::bandId (b, "bypass"))->load() > 0.5f;
        const bool muted = anySolo && ! solo;

        g.setColour (col.withAlpha (muted ? 0.02f : (sel ? 0.13f : 0.05f)));
        g.fillRect (region);
        if (sel)
        {
            g.setColour (col.withAlpha (0.9f));
            g.fillRect (region.withHeight (2.0f));
        }

        const int mode = (int) processor.apvts.getRawParameterValue (KrushProcessor::bandId (b, "mode"))->load();
        String text = String (b + 1);
        if (region.getWidth() > 70.0f) text << "  " << String (krush::modeName (mode)).toUpperCase();
        if (bypass && region.getWidth() > 150.0f) text << "  (BYPASS)";
        else if (solo && region.getWidth() > 130.0f) text << "  (SOLO)";
        g.setFont (lnf.font (10.5f));
        g.setColour (col.withAlpha (muted || bypass ? 0.4f : (sel ? 1.0f : 0.7f)));
        g.drawText (text, region.reduced (8.0f, 0.0f).withY (r.getY() + 5.0f).withHeight (14.0f), Justification::centredLeft, false);
    }

    // ---- grid
    g.setFont (lnf.font (9.0f, false));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xOfFreq (f);
        g.setColour (Colours::white.withAlpha (0.05f));
        g.drawVerticalLine ((int) x, top, bottom);
        g.setColour (Pal::steel.withAlpha (0.8f));
        g.drawText (f < 1000.0f ? String ((int) f) : String ((int) (f / 1000.0f)) + "k", Rectangle<float> (x - 20.0f, bottom + 2.0f, 40.0f, 12.0f), Justification::centred, false);
    }
    for (float db : { -18.0f, -36.0f, -54.0f, -72.0f })
    {
        g.setColour (Colours::white.withAlpha (0.04f));
        g.drawHorizontalLine ((int) yOfDb (db), r.getX(), r.getRight());
    }

    // ---- spectrum (tilted +3 dB per octave around 1 kHz, so music reads level)
    const float binHz = (float) processor.hostRate.load() / (float) kFftSize;
    Path line;
    const int w = (int) r.getWidth();
    for (int x = 0; x < w; ++x)
    {
        const float f0 = freqOfX ((float) x), f1 = freqOfX ((float) x + 1.0f);
        const float p0 = f0 / binHz, p1 = f1 / binHz;
        float db = -120.0f;
        if (p1 - p0 < 1.0f)
        {
            // narrower than one bin (the low end): interpolate between neighbouring bins
            const float pc = jlimit (1.0f, (float) (kFftSize / 2 - 1), 0.5f * (p0 + p1));
            const int b = (int) pc; const float fr = pc - (float) b;
            db = spectrumDb[(size_t) b] + fr * (spectrumDb[(size_t) b + 1] - spectrumDb[(size_t) b]);
        }
        else
        {
            const int b0 = jlimit (1, kFftSize / 2, (int) std::ceil (p0)), b1 = jlimit (1, kFftSize / 2, (int) p1);
            for (int b = b0; b <= jmax (b0, b1); ++b) db = jmax (db, spectrumDb[(size_t) b]);
        }
        db += 3.0f * std::log2 (jmax (20.0f, 0.5f * (f0 + f1)) / 1000.0f);
        const float y = yOfDb (db);
        if (x == 0) line.startNewSubPath ((float) x, y); else line.lineTo ((float) x, y);
    }
    Path fillPath (line);
    fillPath.lineTo (r.getWidth(), bottom); fillPath.lineTo (0.0f, bottom); fillPath.closeSubPath();
    g.setGradientFill (ColourGradient (Pal::silver.withAlpha (0.20f), 0.0f, top, Pal::silver.withAlpha (0.02f), 0.0f, bottom, false));
    g.fillPath (fillPath);
    g.setColour (Pal::silver.withAlpha (0.85f));
    g.strokePath (line, PathStrokeType (1.2f));

    // ---- crossover handles
    for (int k = 0; k < nb - 1; ++k)
    {
        const float x = xOfFreq (crossover (k));
        const bool active = k == hover || k == dragging;
        g.setColour (active ? Pal::red : Pal::silver.withAlpha (0.45f));
        g.drawLine (x, r.getY() + 22.0f, x, bottom, active ? 1.6f : 1.0f);
        const float hy = r.getY() + 30.0f;
        g.setColour (Pal::black);
        g.fillEllipse (x - 6.0f, hy - 6.0f, 12.0f, 12.0f);
        g.setColour (active ? Pal::red : Pal::silver);
        g.drawEllipse (x - 6.0f, hy - 6.0f, 12.0f, 12.0f, 1.5f);
        if (active)
        {
            g.setFont (lnf.font (10.5f));
            g.setColour (Colours::white);
            const bool flip = x > r.getWidth() - 80.0f;
            g.drawText (hzText (crossover (k)), Rectangle<float> (flip ? x - 78.0f : x + 10.0f, hy - 7.0f, 68.0f, 14.0f),
                        flip ? Justification::centredRight : Justification::centredLeft, false);
        }
    }
}

void DisplayView::drawWave (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace juce;
    const float cy = r.getCentreY(), half = r.getHeight() * 0.5f - 10.0f;
    auto yOf = [&] (float v) { return cy - jlimit (-1.2f, 1.2f, v) / 1.1f * half; };

    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) cy, r.getX(), r.getRight());
    const float dashes[] = { 4.0f, 4.0f };
    for (float v : { 1.0f, -1.0f })
        g.drawDashedLine ({ r.getX(), yOf (v), r.getRight(), yOf (v) }, dashes, 2, 1.0f);

    const int w = (int) r.getWidth();
    const int p = processor.scopeWrite.load (std::memory_order_acquire);
    const int mask = KrushProcessor::kScopeSize - 1;
    for (int pass = 0; pass < 2; ++pass)
    {
        auto& mins = pass == 0 ? processor.scopeInMin : processor.scopeOutMin;
        auto& maxs = pass == 0 ? processor.scopeInMax : processor.scopeOutMax;
        g.setColour (pass == 0 ? Pal::steel.withAlpha (0.6f) : Pal::red.withAlpha (0.85f));
        for (int x = 0; x < w; ++x)
        {
            const int idx = (p - (w - 1 - x)) & mask;          // newest on the right
            const float lo = mins[(size_t) idx].load (std::memory_order_relaxed);
            const float hi = maxs[(size_t) idx].load (std::memory_order_relaxed);
            g.drawVerticalLine (x, yOf (hi) - 0.5f, yOf (lo) + 0.5f);
        }
    }

    g.setFont (lnf.font (10.0f));
    g.setColour (Pal::steel);
    g.drawText ("INPUT", Rectangle<float> (10.0f, 6.0f, 60.0f, 14.0f), Justification::centredLeft, false);
    g.setColour (Pal::red);
    g.drawText ("OUTPUT", Rectangle<float> (62.0f, 6.0f, 60.0f, 14.0f), Justification::centredLeft, false);
}

void DisplayView::mouseMove (const juce::MouseEvent& e)
{
    const int h = crossoverAt ((float) e.x);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void DisplayView::mouseExit (const juce::MouseEvent&) { if (hover >= 0) { hover = -1; repaint(); } }

void DisplayView::mouseDown (const juce::MouseEvent& e)
{
    if (waveMode) return;
    dragging = crossoverAt ((float) e.x);
    if (dragging >= 0)
    {
        processor.apvts.getParameter ("x" + juce::String (dragging + 1))->beginChangeGesture();
        return;
    }
    // a click in a band selects it
    const int nb = numBands();
    int b = 0;
    while (b < nb - 1 && (float) e.x > xOfFreq (crossover (b))) ++b;
    if (onBandClicked) onBandClicked (b);
}

void DisplayView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const int nb = numBands();
    const float lo = dragging > 0 ? crossover (dragging - 1) * 1.26f : 20.0f;
    const float hi = dragging < nb - 2 ? crossover (dragging + 1) / 1.26f : 20000.0f;
    const float f = juce::jlimit (lo, hi, freqOfX ((float) e.x));
    auto* p = processor.apvts.getParameter ("x" + juce::String (dragging + 1));
    p->setValueNotifyingHost (p->convertTo0to1 (f));
    repaint();
}

void DisplayView::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        processor.apvts.getParameter ("x" + juce::String (dragging + 1))->endChangeGesture();
    dragging = -1;
}

void DisplayView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int k = crossoverAt ((float) e.x);
    if (k < 0) return;
    auto* p = processor.apvts.getParameter ("x" + juce::String (k + 1));
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->getDefaultValue());
    p->endChangeGesture();
}

//==============================================================================
// Curve view
//==============================================================================
void CurveView::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = getLocalBounds().toFloat();
    g.setColour (Pal::well);
    g.fillRoundedRectangle (r, 4.0f);

    auto v = [this] (const char* id) { return processor.apvts.getRawParameterValue (KrushProcessor::bandId (band, id))->load(); };
    const int mode = (int) v ("mode");
    const float driveDb = v ("drive"), s = v ("shape") * 0.01f, bias = v ("bias") * 0.005f;

    const auto plot = r.reduced (10.0f);
    const float range = 1.25f;
    auto px = [&] (float x) { return jmap (x, -range, range, plot.getX(), plot.getRight()); };
    auto py = [&] (float y) { return jmap (jlimit (-range, range, y), -range, range, plot.getBottom(), plot.getY()); };

    g.setColour (Colours::white.withAlpha (0.05f));
    for (float t : { -1.0f, -0.5f, 0.5f, 1.0f }) { g.drawVerticalLine ((int) px (t), plot.getY(), plot.getBottom()); g.drawHorizontalLine ((int) py (t), plot.getX(), plot.getRight()); }
    g.setColour (Colours::white.withAlpha (0.12f));
    g.drawVerticalLine ((int) px (0.0f), plot.getY(), plot.getBottom());
    g.drawHorizontalLine ((int) py (0.0f), plot.getX(), plot.getRight());

    Path curve;
    if (mode == krush::Downsample)
    {
        // one cycle of a sine, held and crushed: what the band does to a waveform
        Path sine;
        const int n = 256;
        const float hold = krush::downsampleFactor (driveDb) * (float) n / 128.0f;
        const float q = std::exp2 (krush::crushBits (s) - 1.0f);
        float held = 0.0f, phase = 1.0e9f;
        for (int i = 0; i <= n; ++i)
        {
            const float t = (float) i / (float) n;
            const float x = std::sin (MathConstants<float>::twoPi * t) * 0.9f;
            phase += 1.0f;
            if (phase >= hold) { phase -= hold; if (phase > hold) phase = 0.0f; held = s > 0.001f ? std::round (x * q) / q : x; }
            const float X = jmap (t, plot.getX(), plot.getRight());
            if (i == 0) { sine.startNewSubPath (X, py (x)); curve.startNewSubPath (X, py (held)); }
            else { sine.lineTo (X, py (x)); curve.lineTo (X, py (held)); }
        }
        g.setColour (Pal::steel.withAlpha (0.5f));
        g.strokePath (sine, PathStrokeType (1.0f));
    }
    else
    {
        // identity line for reference, then the curve: input level (x) against output level (y)
        g.setColour (Pal::steel.withAlpha (0.35f));
        const float dashes[] = { 3.0f, 3.0f };
        g.drawDashedLine ({ px (-range), py (-range), px (range), py (range) }, dashes, 2, 1.0f);

        const bool clean = mode == krush::None;
        const float drive = clean ? 1.0f : std::pow (10.0f, driveDb / 20.0f);
        const float off = clean ? 0.0f : krush::shapeCurve (mode, bias, s);
        const int n = 400;
        for (int i = 0; i <= n; ++i)
        {
            const float x = -range + 2.0f * range * (float) i / (float) n;
            const float y = clean ? x : krush::shapeCurve (mode, x * drive + bias, s) - off;
            if (i == 0) curve.startNewSubPath (px (x), py (y)); else curve.lineTo (px (x), py (y));
        }
    }
    g.setColour (Pal::red.withAlpha (0.25f));
    g.strokePath (curve, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Pal::red);
    g.strokePath (curve, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));

    g.setColour (Pal::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
}

//==============================================================================
// Editor
//==============================================================================
KrushUI::KrushUI (KrushProcessor& p)
    : processor (p), display (p, lnf), curve (p)
{
    lnf.regular  = juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratRegular_ttf, BinaryData::MontserratRegular_ttfSize);
    lnf.semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratSemiBold_ttf, BinaryData::MontserratSemiBold_ttfSize);
    setLookAndFeel (&lnf);
    logo = juce::ImageCache::getFromMemory (BinaryData::krush_logo_png, BinaryData::krush_logo_pngSize);

    // ---- presets
    presetBox.setTextWhenNothingSelected ("Default");
    presetBox.setJustificationType (juce::Justification::centredLeft);
    presetBox.setTooltip ("PRESETS: click to pick a preset. Each one has a short description in the list. "
                          "SAVE stores your own, with an optional description.");
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
    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    saveButton.setTooltip ("Save the current settings as a preset");
    deleteButton.setTooltip ("Delete this preset (your own presets only)");
    for (auto* b : { &prevButton, &nextButton, &saveButton, &deleteButton })
        addAndMakeVisible (*b);
    refreshPresetList();

    // ---- display and its view switch
    addAndMakeVisible (display);
    addAndMakeVisible (curve);
    display.waveMode = (bool) p.apvts.state.getProperty ("waveView", false);
    display.onBandClicked = [this] (int b) { selectBand (b); };
    for (auto* b : { &spectrumButton, &waveButton })
    {
        addAndMakeVisible (*b);
        b->onClick = [this, b]
        {
            display.waveMode = b == &waveButton;
            processor.apvts.state.setProperty ("waveView", display.waveMode, nullptr);
            display.repaint();
        };
    }
    // ---- help: turns the description pop-ups on and off (off by default, remembered between sessions)
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "KRUSH"; o.folderName = "KRUSH"; o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (o);
    }
    helpButton.setClickingTogglesState (true);
    helpButton.getProperties().set ("round", true);
    helpButton.onClick = [this] { setHelp (helpButton.getToggleState()); };
    addAndMakeVisible (helpButton);
    display.setTooltip ("The bands: drag the round handles to move the split points (double-click resets one), "
                        "click a band to edit it. WAVEFORM switches to a scrolling waveform.");
    curve.setTooltip ("The distortion curve of the selected band. Quiet input is in the middle, loud input at the edges. "
                      "The dashed line is the clean signal: the further the red line bends away from it, the more distortion.");
    setHelp (settings->getBoolValue ("showHelp", false));

    spectrumButton.setTooltip ("Spectrum of the output, with the bands. Drag the round handles to move the crossovers, click a band to edit it.");
    waveButton.setTooltip ("Scrolling waveform: input in grey, output in red.");

    // ---- band count and band tabs
    for (int i = 0; i < 4; ++i)
    {
        countButtons[i].setButtonText (juce::String (i + 1));
        countButtons[i].setTooltip ("Split the sound into " + juce::String (i + 1) + (i == 0 ? " band (no split)" : " bands"));
        countButtons[i].onClick = [this, i]
        {
            setParam ("bands", (float) i);
            if (band > i) selectBand (i);
        };
        addAndMakeVisible (countButtons[i]);

        bandTabs[i].setButtonText ("BAND " + juce::String (i + 1));
        bandTabs[i].getProperties().set ("accent", (juce::int64) Pal::band (i).getARGB());
        bandTabs[i].onClick = [this, i] { selectBand (i); };
        bandTabs[i].setTooltip ("Edit band " + juce::String (i + 1) + ". The panels below show this band's settings. "
                                "You can also click the band in the display above.");
        addAndMakeVisible (bandTabs[i]);
    }
    for (auto* b : { &soloButton, &bypassButton })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    soloButton.getProperties().set ("accent", (juce::int64) Pal::orange.getARGB());
    soloButton.setTooltip ("Hear only this band (and any other soloed bands)");
    bypassButton.setTooltip ("Pass this band through clean, without distortion");

    // ---- distortion modes
    for (int m = 0; m < krush::kNumModes; ++m)
    {
        auto& b = modeButtons[m];
        b.setButtonText (juce::String (krush::modeName (m)).toUpperCase());
        b.getProperties().set ("left", true);
        b.setTooltip (krush::modeDescription (m));
        b.onClick = [this, m] { setParam (KrushProcessor::bandId (band, "mode"), (float) m); };
        addAndMakeVisible (b);
    }

    // ---- per band controls
    auto setupFader = [this] (juce::Slider& s, bool fill)
    {
        s.setSliderStyle (juce::Slider::LinearVertical);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
        s.getProperties().set ("fill", fill);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxBackgroundColourId, Pal::well);
        s.setColour (juce::Slider::textBoxTextColourId, Pal::silver);
        addAndMakeVisible (s);
    };
    setupFader (driveFader, true);
    setupFader (gainFader, false);
    setupFader (bandMixFader, true);
    driveFader.setTooltip ("DRIVE: how hard this band hits the distortion (in Downsample: how far the sample rate drops)");
    gainFader.setTooltip ("GAIN: this band's level after the distortion");
    bandMixFader.setTooltip ("MIX: blends this band's distortion with its clean signal");

    for (auto* k : { &shapeKnob, &biasKnob, &dynKnob })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        addAndMakeVisible (*k);
    }
    biasKnob.getProperties().set ("bipolar", true);
    dynKnob.getProperties().set ("bipolar", true);
    dynKnob.setTooltip ("DYNAMICS: how much of this band's transients (the hits and attacks) gets through. "
                        "Turn right to bring back the punch that distortion squashes, left to smooth hits out "
                        "for a fuller, more sustained sound. Held notes are not affected.");
    biasKnob.setTooltip ("BIAS: shifts the signal off-centre before the distortion, so the two halves of the wave clip differently. Adds even harmonics.");

    auto setupLabel = [this] (juce::Label& l, const juce::String& text, float size)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (lnf.font (size));
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, Pal::steel);
        l.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (l);
    };
    setupLabel (driveLabel, "DRIVE", 11.0f);
    setupLabel (gainLabel, "GAIN", 11.0f);
    setupLabel (bandMixLabel, "MIX", 11.0f);
    setupLabel (shapeLabel, "SHAPE", 10.0f);
    setupLabel (biasLabel, "BIAS", 10.0f);
    setupLabel (dynLabel, "DYNAMICS", 10.0f);

    // ---- global: input/output faders, mix, tone filter
    for (auto* f : { &inputFader, &outputFader })
    {
        f->setSliderStyle (juce::Slider::LinearVertical);
        f->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        f->getProperties().set ("fill", false);
        f->setDoubleClickReturnValue (true, 0.0);
        addAndMakeVisible (*f);
    }
    inputFader.setTooltip ("INPUT: level going into KRUSH");
    outputFader.setTooltip ("OUTPUT: final level");
    inputAttach  = std::make_unique<SliderAttach> (p.apvts, "input", inputFader);
    outputAttach = std::make_unique<SliderAttach> (p.apvts, "output", outputFader);

    // ---- output safety: soft clipper and limiter after the OUTPUT fader
    for (auto* b : { &clipButton, &limitButton })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    clipButton.setTooltip ("CLIP: soft-clips the output so the sample peaks never go above -0.1 dBFS. "
                           "Fast and loud, adds a little grit on the loudest peaks. (Sample peak, not true peak.)");
    limitButton.setTooltip ("LIMIT: a clean look-ahead limiter that keeps the sample peaks at or below -0.1 dBFS. "
                            "Shows the gain reduction while it works. With CLIP also on, the clipper shaves the top 3 dB "
                            "first and the limiter does the rest. (Sample peak, not true peak.)");
    clipAttach  = std::make_unique<ButtonAttach> (p.apvts, "clipOn", clipButton);
    limitAttach = std::make_unique<ButtonAttach> (p.apvts, "limitOn", limitButton);

    mixSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    mixSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    mixSlider.setTooltip ("MIX: blends the whole distorted signal with the clean input");
    addAndMakeVisible (mixSlider);
    mixAttach = std::make_unique<SliderAttach> (p.apvts, "mix", mixSlider);
    mixSlider.setDoubleClickReturnValue (true, 100.0);

    filterSlider.setSliderStyle (juce::Slider::TwoValueHorizontal);
    filterSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    filterSlider.setRange (20.0, 20000.0, 1.0);
    filterSlider.setSkewFactorFromMidPoint (630.0);
    filterSlider.setMinAndMaxValues (20.0, 20000.0, juce::dontSendNotification);
    filterSlider.setTooltip ("TONE: filters the distorted sound (the clean part of MIX is not filtered). "
                             "Drag the left handle up to cut lows, the right handle down to tame fizz. At the far ends each filter is off.");
    addAndMakeVisible (filterSlider);
    hpfAttach = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter ("hpf"),
        [this] (float v) { filterSlider.setMinValue ((double) v, juce::dontSendNotification); updateFilterLabels(); });
    lpfAttach = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter ("lpf"),
        [this] (float v) { filterSlider.setMaxValue ((double) v, juce::dontSendNotification); updateFilterLabels(); });
    filterSlider.onDragStart = [this] { hpfAttach->beginGesture(); lpfAttach->beginGesture(); };
    filterSlider.onDragEnd   = [this] { hpfAttach->endGesture();   lpfAttach->endGesture(); };
    filterSlider.onValueChange = [this]
    {
        hpfAttach->setValueAsPartOfGesture ((float) filterSlider.getMinValue());
        lpfAttach->setValueAsPartOfGesture ((float) filterSlider.getMaxValue());
        updateFilterLabels();
    };
    const char* slopes[] = { "6", "12", "18", "24", "48" };
    for (int i = 0; i < 5; ++i)
    {
        slopeButtons[i].setButtonText (slopes[i]);
        slopeButtons[i].setTooltip ("Tone filter steepness: " + juce::String (slopes[i]) + " dB per octave. "
                                    "Gentle slopes shape the tone, steep ones cut hard.");
        slopeButtons[i].onClick = [this, i] { setParam ("toneSlope", (float) i); };
        addAndMakeVisible (slopeButtons[i]);
    }
    setupLabel (hpLabel, "HPF", 11.0f);
    setupLabel (lpLabel, "LPF", 11.0f);
    lpfAttach->sendInitialUpdate();
    hpfAttach->sendInitialUpdate();

    selectBand (juce::jlimit (0, 3, (int) p.apvts.state.getProperty ("selectedBand", 0)));

    setSize (kW, kH);
    startTimerHz (30);
}

void KrushUI::setHelp (bool on)
{
    helpButton.setToggleState (on, juce::dontSendNotification);
    tooltips = on ? std::make_unique<juce::TooltipWindow> (this, 500) : nullptr;
    helpButton.setTooltip ("Help is on: hover over any control to see what it does. Click to turn the pop-ups off.");
    settings->setValue ("showHelp", on);
    settings->saveIfNeeded();
}

KrushUI::~KrushUI()
{
    stopTimer();
    dialog.reset();
    tooltips.reset();
    setLookAndFeel (nullptr);
}

void KrushUI::setParam (const juce::String& id, float realValue)
{
    if (auto* p = processor.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        p->endChangeGesture();
    }
}

void KrushUI::selectBand (int b)
{
    const int nb = (int) processor.apvts.getRawParameterValue ("bands")->load() + 1;
    band = juce::jlimit (0, nb - 1, b);
    processor.apvts.state.setProperty ("selectedBand", band, nullptr);
    display.selectedBand = band;
    curve.band = band;

    auto& vts = processor.apvts;

    // Disconnect the controls from the previous band first. Otherwise, while the new
    // attachments load the new band's values into the controls, the old attachments
    // would still be listening and copy those values into the previous band.
    driveAttach.reset(); gainAttach.reset(); bandMixAttach.reset(); shapeAttach.reset(); biasAttach.reset(); dynAttach.reset();
    soloAttach.reset(); bypassAttach.reset();

    driveAttach   = std::make_unique<SliderAttach> (vts, KrushProcessor::bandId (band, "drive"), driveFader);
    gainAttach    = std::make_unique<SliderAttach> (vts, KrushProcessor::bandId (band, "gain"), gainFader);
    bandMixAttach = std::make_unique<SliderAttach> (vts, KrushProcessor::bandId (band, "mix"), bandMixFader);
    shapeAttach   = std::make_unique<SliderAttach> (vts, KrushProcessor::bandId (band, "shape"), shapeKnob);
    biasAttach    = std::make_unique<SliderAttach> (vts, KrushProcessor::bandId (band, "bias"), biasKnob);
    dynAttach     = std::make_unique<SliderAttach> (vts, KrushProcessor::bandId (band, "dyn"), dynKnob);
    soloAttach    = std::make_unique<ButtonAttach> (vts, KrushProcessor::bandId (band, "solo"), soloButton);
    bypassAttach  = std::make_unique<ButtonAttach> (vts, KrushProcessor::bandId (band, "bypass"), bypassButton);

    // readouts (set after the attachments, which install their own)
    driveFader.textFromValueFunction = [this] (double v)
    {
        const int mode = (int) processor.apvts.getRawParameterValue (KrushProcessor::bandId (band, "mode"))->load();
        return mode == krush::Downsample ? juce::String::fromUTF8 ("\xc3\xb7") + juce::String (krush::downsampleFactor ((float) v), 1)
                                         : juce::String (v, 1) + " dB";
    };
    gainFader.textFromValueFunction = [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; };
    bandMixFader.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    for (auto* s : { &driveFader, &gainFader, &bandMixFader }) s->updateText();

    for (auto* s : { &driveFader, &gainFader, &bandMixFader, &shapeKnob, &biasKnob, &dynKnob })
    {
        auto* prm = dynamic_cast<juce::RangedAudioParameter*> (vts.getParameter (KrushProcessor::bandId (band,
                        s == &driveFader ? "drive" : s == &gainFader ? "gain" : s == &bandMixFader ? "mix"
                      : s == &shapeKnob ? "shape" : s == &biasKnob ? "bias" : "dyn")));
        if (prm != nullptr) s->setDoubleClickReturnValue (true, (double) prm->convertFrom0to1 (prm->getDefaultValue()));
    }
    timerCallback();
    repaint();
}

//==============================================================================
// Presets

// A preset in the list: the name, with a short description underneath.
class PresetMenuItem : public juce::PopupMenu::CustomComponent
{
public:
    PresetMenuItem (const juce::String& n, PresetManager& pm, KrushLookAndFeel& l)
        : CustomComponent (true), name (n), description (PresetManager::getDescription (n)), presets (pm), lnf (l) {}

    void getIdealSize (int& w, int& h) override { w = 330; h = 36; }

    void paint (juce::Graphics& g) override
    {
        const bool hot = isItemHighlighted(), current = presets.getCurrentName() == name;
        if (hot)
        {
            g.setColour (Pal::red);
            g.fillRect (getLocalBounds().reduced (2, 1));
        }
        if (current)                                        // the loaded preset: a bar on the left
        {
            g.setColour (hot ? juce::Colours::white : Pal::red);
            g.fillRect (5, 8, 3, getHeight() - 16);
        }
        g.setColour (hot ? juce::Colours::white : (current ? Pal::red : Pal::silver));
        g.setFont (lnf.font (12.5f));
        g.drawText (name, 14, 4, getWidth() - 20, 15, juce::Justification::centredLeft, true);
        g.setColour (hot ? juce::Colours::white.withAlpha (0.85f) : Pal::steel);
        g.setFont (lnf.font (10.5f, false));
        g.drawText (description, 14, 19, getWidth() - 20, 13, juce::Justification::centredLeft, true);
    }

private:
    juce::String name, description;
    PresetManager& presets;
    KrushLookAndFeel& lnf;
};

void KrushUI::refreshPresetList()
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
    // draw every entry with its description
    for (juce::PopupMenu::MenuItemIterator it (*presetBox.getRootMenu()); it.next();)
    {
        auto& item = it.getItem();
        if (item.itemID > 0)
            item.customComponent = new PresetMenuItem (item.text, processor.presets, lnf);
    }
    const auto current = processor.presets.getCurrentName();
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (presetBox.getItemText (i) == current)
            presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
    deleteButton.setEnabled (! PresetManager::isFactory (current));
}

void KrushUI::stepPreset (int delta)
{
    const int n = presetBox.getNumItems();
    if (n == 0) return;
    const int current = presetBox.getSelectedItemIndex();
    presetBox.setSelectedItemIndex (current < 0 ? 0 : (current + delta + n) % n, juce::sendNotificationSync);
}

void KrushUI::showSaveDialog()
{
    const auto current = processor.presets.getCurrentName();
    const bool own = ! PresetManager::isFactory (current);
    dialog = std::make_unique<juce::AlertWindow> ("Save preset", juce::String(), juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor ("name", own ? current : juce::String ("My Preset"), "Name");
    dialog->addTextEditor ("description", own ? PresetManager::getUserDescription (current) : juce::String(),
                           "Description (optional, shown in the preset list)");
    if (auto* d = dialog->getTextEditor ("description"))
        d->setInputRestrictions (PresetManager::kMaxDescription);
    dialog->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (dialog == nullptr) return;
        const auto name = dialog->getTextEditorContents ("name");
        const auto description = dialog->getTextEditorContents ("description");
        dialog->setVisible (false);
        if (result == 1 && processor.presets.save (name, description).isNotEmpty())
            refreshPresetList();
    }), false);
    if (auto* ed = dialog->getTextEditor ("name")) { ed->grabKeyboardFocus(); ed->selectAll(); }
}

void KrushUI::showDeleteDialog()
{
    const auto name = presetBox.getText();
    if (name.isEmpty() || PresetManager::isFactory (name)) return;
    dialog = std::make_unique<juce::AlertWindow> ("Delete preset", "Delete \"" + name + "\"?", juce::MessageBoxIconType::NoIcon, this);
    dialog->addButton ("Delete", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, name] (int result)
    {
        if (dialog == nullptr) return;
        dialog->setVisible (false);
        if (result == 1 && processor.presets.remove (name))
            refreshPresetList();
    }), false);
}

void KrushUI::updateFilterLabels()
{
    const double hp = filterSlider.getMinValue(), lp = filterSlider.getMaxValue();
    const bool hpOn = hp > 20.5, lpOn = lp < 19999.5;
    hpLabel.setText (hpOn ? hzText (hp) : juce::String ("HPF"), juce::dontSendNotification);
    lpLabel.setText (lpOn ? hzText (lp) : juce::String ("LPF"), juce::dontSendNotification);
    hpLabel.setColour (juce::Label::textColourId, hpOn ? Pal::silver : Pal::steel);
    lpLabel.setColour (juce::Label::textColourId, lpOn ? Pal::silver : Pal::steel);
}

void KrushUI::timerCallback()
{
    auto& vts = processor.apvts;
    const int nb = (int) vts.getRawParameterValue ("bands")->load() + 1;
    if (band >= nb) { selectBand (nb - 1); return; }

    for (int i = 0; i < 4; ++i)
    {
        countButtons[i].setToggleState (i == nb - 1, juce::dontSendNotification);
        bandTabs[i].setToggleState (i == band, juce::dontSendNotification);
        bandTabs[i].setEnabled (i < nb);
    }
    const int slope = (int) vts.getRawParameterValue ("toneSlope")->load();
    for (int i = 0; i < 5; ++i) slopeButtons[i].setToggleState (i == slope, juce::dontSendNotification);
    spectrumButton.setToggleState (! display.waveMode, juce::dontSendNotification);
    waveButton.setToggleState (display.waveMode, juce::dontSendNotification);

    const int mode = (int) vts.getRawParameterValue (KrushProcessor::bandId (band, "mode"))->load();
    for (int m = 0; m < krush::kNumModes; ++m)
        modeButtons[m].setToggleState (m == mode, juce::dontSendNotification);

    driveLabel.setText (mode == krush::Downsample ? "RATE" : "DRIVE", juce::dontSendNotification);
    shapeLabel.setText (juce::String (krush::shapeName (mode)) + "  " + juce::String (juce::roundToInt (shapeKnob.getValue())) + "%", juce::dontSendNotification);
    const int bias = juce::roundToInt (biasKnob.getValue());
    biasLabel.setText ("BIAS  " + juce::String (bias > 0 ? "+" : "") + juce::String (bias) + "%", juce::dontSendNotification);
    const int dyn = juce::roundToInt (dynKnob.getValue());
    dynLabel.setText ("DYNAMICS  " + juce::String (dyn > 0 ? "+" : "") + juce::String (dyn) + "%", juce::dontSendNotification);
    biasKnob.setEnabled (mode != krush::Downsample && mode != krush::None);
    shapeKnob.setEnabled (mode != krush::None);
    driveFader.setEnabled (mode != krush::None);
    shapeKnob.setTooltip (juce::String (krush::shapeName (mode)) + ": " + krush::modeDescription (mode));
    driveFader.updateText();

    // meters: peak with a 1.5 s hold, falling 20 dB/s
    KrushProcessor::Meter* src[4] = { &processor.inMeter[0], &processor.inMeter[1], &processor.outMeter[0], &processor.outMeter[1] };
    for (int i = 0; i < 4; ++i)
    {
        auto& b = bars[i];
        const float pk = juce::Decibels::gainToDecibels (src[i]->peak.exchange (0.0f), -100.0f);
        b.peakDb = juce::jmax (pk, b.peakDb - 0.67f);
        if (pk >= b.holdDb) { b.holdDb = pk; b.holdFrames = 45; }
        else if (--b.holdFrames <= 0) b.holdDb = juce::jmax (pk, b.holdDb - 0.67f);
        b.rmsDb = src[i]->rmsDb.load();
    }

    // LIMIT button shows the gain reduction while it works (fast attack, slow fall so it can be read)
    const float red = processor.limitReductionDb.exchange (0.0f);
    shownReduction = red >= shownReduction ? red : juce::jmax (red, shownReduction - 0.1f);
    const bool limiting = limitButton.getToggleState() && shownReduction >= 0.05f;
    const juce::String limitText = limiting ? "LIMIT -" + juce::String (shownReduction, 1) : juce::String ("LIMIT");
    if (limitButton.getButtonText() != limitText) limitButton.setButtonText (limitText);

    if (++frameCount % 15 == 0 && ! presetBox.isPopupActive() && presetBox.getText() != processor.presets.getCurrentName())
        refreshPresetList();

    display.update();
    display.repaint();
    curve.repaint();
    repaint (meterPanel);
    for (auto r : { gainPanel, shapePanel, modePanel })
        repaint (r.withHeight (26));      // band and mode names in the panel titles
}

//==============================================================================
void KrushUI::resized()
{
    displayArea = { 16, 72, 740, 196 };
    gainPanel   = { 16, 314, 220, 226 };
    shapePanel  = { 248, 314, 272, 226 };
    modePanel   = { 532, 314, 224, 226 };
    meterPanel  = { 772, 72, 212, 468 };
    filterArea  = { 16, 556, 968, 44 };

    // header
    prevButton.setBounds (196, 20, 26, 26);
    presetBox.setBounds (226, 20, 184, 26);
    nextButton.setBounds (414, 20, 26, 26);
    saveButton.setBounds (448, 20, 54, 26);
    deleteButton.setBounds (506, 20, 62, 26);
    spectrumButton.setBounds (584, 20, 84, 26);
    waveButton.setBounds (672, 20, 84, 26);
    helpButton.setBounds (764, 22, 22, 22);
    for (int i = 0; i < 4; ++i) countButtons[i].setBounds (862 + i * 31, 20, 29, 26);

    display.setBounds (displayArea);

    // band strip
    for (int i = 0; i < 4; ++i) bandTabs[i].setBounds (16 + i * 74, 280, 70, 26);
    soloButton.setBounds (600, 280, 74, 26);
    bypassButton.setBounds (682, 280, 74, 26);

    // gain staging: three faders
    const int fy = gainPanel.getY() + 30;
    int i = 0;
    for (auto* f : { &driveFader, &gainFader, &bandMixFader })
    {
        const int cx = gainPanel.getX() + 38 + 72 * i++;
        f->setBounds (cx - 28, fy, 56, 166);
    }
    i = 0;
    for (auto* l : { &driveLabel, &gainLabel, &bandMixLabel })
    {
        const int cx = gainPanel.getX() + 38 + 72 * i++;
        l->setBounds (cx - 36, fy + 170, 72, 16);
    }

    // shape: curve plus two knobs
    curve.setBounds (shapePanel.getX() + 12, shapePanel.getY() + 32, 180, 180);
    // three knobs stacked beside the curve
    for (auto [k, l, y] : { std::tuple { &shapeKnob, &shapeLabel, 28 }, { &biasKnob, &biasLabel, 94 }, { &dynKnob, &dynLabel, 160 } })
    {
        k->setBounds (shapePanel.getX() + 208, shapePanel.getY() + y, 48, 48);
        l->setBounds (shapePanel.getX() + 190, shapePanel.getY() + y + 47, 84, 14);
    }

    // distortion modes
    for (int m = 0; m < krush::kNumModes; ++m)
    {
        const int row = m == krush::None ? 0 : m + 1;            // NONE at the top of the list
        modeButtons[m].setBounds (modePanel.getX() + 10, modePanel.getY() + 27 + row * 18, modePanel.getWidth() - 20, 16);
    }

    // right column: faders beside the meters, mix below
    inputFader.setBounds (meterPanel.getX() + 6, 132, 30, 294);
    outputFader.setBounds (meterPanel.getRight() - 36, 132, 30, 294);
    clipButton.setBounds (meterPanel.getX() + 28, 462, 74, 22);
    limitButton.setBounds (meterPanel.getRight() - 102, 462, 74, 22);
    mixSlider.setBounds (meterPanel.getX() + 40, 506, meterPanel.getWidth() - 80, 28);

    // tone filter bar
    hpLabel.setBounds (filterArea.getX() + 62, filterArea.getY() + 13, 76, 18);
    filterSlider.setBounds (filterArea.getX() + 140, filterArea.getY() + 6, filterArea.getWidth() - 466, 32);
    lpLabel.setBounds (filterArea.getRight() - 322, filterArea.getY() + 13, 76, 18);
    for (int k = 0; k < 5; ++k)
        slopeButtons[k].setBounds (filterArea.getRight() - 176 + k * 33, filterArea.getY() + 10, 30, 24);
}

void KrushUI::drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title) const
{
    g.setColour (Pal::charcoal);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    g.setColour (Pal::edge);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (Pal::red);
        g.fillRect (r.getX() + 12, r.getY() + 12, 3, 10);
        g.setFont (lnf.font (11.0f));
        g.setColour (Pal::silver);
        g.drawText (title, r.getX() + 21, r.getY() + 9, r.getWidth() - 30, 16, juce::Justification::centredLeft, false);
    }
}

void KrushUI::paint (juce::Graphics& g)
{
    using namespace juce;
    g.fillAll (Pal::black);

    // ---- header: logo, tagline, view switch, band count
    if (logo.isValid())
    {
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (logo, Rectangle<float> (16.0f, 6.0f, 160.0f, 52.0f), RectanglePlacement::xLeft | RectanglePlacement::yMid);
    }
    g.setFont (lnf.font (11.0f));
    g.setColour (Pal::steel);
    g.drawText ("BANDS", 806, 25, 52, 16, Justification::centredRight, false);
    g.setColour (Pal::edge);
    g.fillRect (16, 63, kW - 32, 1);

    // ---- panels
    const int mode = (int) processor.apvts.getRawParameterValue (KrushProcessor::bandId (band, "mode"))->load();
    drawPanel (g, gainPanel, "GAIN STAGING");
    drawPanel (g, shapePanel, "SHAPE");
    drawPanel (g, modePanel, "DISTORTION");
    drawPanel (g, meterPanel, {});
    drawPanel (g, filterArea, {});

    // selected band in each panel's corner, in its colour
    g.setFont (lnf.font (10.5f));
    g.setColour (Pal::band (band));
    for (auto r : { gainPanel, shapePanel })
        g.drawText ("BAND " + String (band + 1), r.getRight() - 72, r.getY() + 9, 60, 16, Justification::centredRight, false);
    g.drawText (String (krush::modeName (mode)).toUpperCase(), modePanel.getRight() - 112, modePanel.getY() + 9, 100, 16, Justification::centredRight, false);

    // band strip label
    g.setColour (Pal::steel);
    g.setFont (lnf.font (10.5f));
    g.drawText ("SELECTED BAND", 320, 285, 140, 16, Justification::centredLeft, false);

    // filter bar label
    g.setColour (Pal::red);
    g.fillRect (filterArea.getX() + 12, filterArea.getY() + 17, 3, 10);
    g.setFont (lnf.font (11.0f));
    g.setColour (Pal::silver);
    g.drawText ("TONE", filterArea.getX() + 21, filterArea.getY() + 14, 40, 16, Justification::centredLeft, false);
    g.setColour (Pal::steel);
    g.setFont (lnf.font (10.5f));
    g.drawText ("dB/OCT", filterArea.getRight() - 238, filterArea.getY() + 14, 56, 16, Justification::centredRight, false);

    drawMeters (g);
}

void KrushUI::drawMeters (juce::Graphics& g)
{
    using namespace juce;
    const auto P = meterPanel;

    // ---- numbers: peak (held) and RMS for input and output
    const Rectangle<int> box (P.getX() + 10, P.getY() + 10, P.getWidth() - 20, 46);
    g.setColour (Pal::well);
    g.fillRoundedRectangle (box.toFloat(), 3.0f);
    g.setColour (Pal::edge);
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 3.0f, 1.0f);

    auto num = [] (float db) { return db <= -99.0f ? String ("-inf") : String (db, 1); };
    const int colX[4] = { box.getX() + 4, box.getX() + 40, box.getRight() - 76, box.getRight() - 40 };
    g.setFont (lnf.font (9.5f));
    g.setColour (Pal::steel);
    for (int i = 0; i < 4; ++i)
        g.drawText (i % 2 == 0 ? "L" : "R", colX[i], box.getY() + 2, 36, 12, Justification::centred, false);
    g.drawText ("PEAK", box.getCentreX() - 24, box.getY() + 15, 48, 13, Justification::centred, false);
    g.drawText ("RMS", box.getCentreX() - 24, box.getY() + 30, 48, 13, Justification::centred, false);
    g.setFont (lnf.font (10.5f));
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (bars[i].holdDb > -0.05f ? Pal::red : Pal::silver);
        g.drawText (num (bars[i].holdDb), colX[i], box.getY() + 15, 36, 13, Justification::centred, false);
        g.setColour (Pal::silver);
        g.drawText (num (bars[i].rmsDb), colX[i], box.getY() + 30, 36, 13, Justification::centred, false);
    }

    // ---- bars
    const float top = 142.0f, bottom = 416.0f;
    auto yOf = [&] (float db) { return bottom - (bottom - top) * meterPos (db); };
    const float barX[4] = { (float) P.getX() + 42.0f, (float) P.getX() + 56.0f, (float) P.getRight() - 68.0f, (float) P.getRight() - 54.0f };
    for (int i = 0; i < 4; ++i)
    {
        const Rectangle<float> slot (barX[i], top, 11.0f, bottom - top);
        g.setColour (Pal::well);
        g.fillRect (slot);
        const float yp = yOf (bars[i].peakDb), yr = yOf (bars[i].rmsDb);
        g.setGradientFill (ColourGradient (Pal::red, 0.0f, yOf (0.0f), Pal::steel, 0.0f, yOf (-24.0f), false));
        g.fillRect (slot.withTop (yp).reduced (2.0f, 0.0f));          // peak: thin, bright
        g.setGradientFill (ColourGradient (Pal::red, 0.0f, yOf (0.0f), Pal::orange.interpolatedWith (Pal::steel, 0.5f), 0.0f, yOf (-24.0f), false));
        g.fillRect (slot.withTop (yr));                                 // RMS: full width
        g.setColour (bars[i].holdDb > -0.05f ? Pal::red : Pal::silver);
        g.fillRect (slot.getX(), yOf (bars[i].holdDb) - 1.0f, slot.getWidth(), 2.0f);
    }

    // ---- scale between the two pairs
    g.setFont (lnf.font (9.0f, false));
    for (float db : { 6.0f, 0.0f, -3.0f, -6.0f, -10.0f, -15.0f, -20.0f, -30.0f, -40.0f, -60.0f })
    {
        const float y = yOf (db);
        g.setColour (db == 0.0f ? Pal::silver : Pal::steel);
        g.drawText (db <= -60.0f ? String ("-inf") : (db > 0.0f ? "+" : "") + String ((int) db), P.getCentreX() - 20, (int) y - 6, 40, 12, Justification::centred, false);
        g.setColour (Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) y, (float) P.getX() + 70.0f, (float) P.getCentreX() - 18.0f);
        g.drawHorizontalLine ((int) y, (float) P.getCentreX() + 18.0f, (float) P.getRight() - 72.0f);
    }

    // ---- fader values and names
    g.setFont (lnf.font (10.5f));
    g.setColour (Pal::silver);
    auto dbText = [] (double v) { return (v > 0.05 ? "+" : "") + String (v, 1); };
    g.drawText ("INPUT", P.getX() + 4, 428, 80, 14, Justification::centred, false);
    g.drawText ("OUTPUT", P.getRight() - 84, 428, 80, 14, Justification::centred, false);
    g.setColour (Pal::red);
    g.drawText (dbText (inputFader.getValue()) + " dB", P.getX() + 4, 442, 80, 14, Justification::centred, false);
    g.drawText (dbText (outputFader.getValue()) + " dB", P.getRight() - 84, 442, 80, 14, Justification::centred, false);

    // ---- mix
    g.setFont (lnf.font (10.5f));
    g.setColour (Pal::silver);
    g.drawText ("MIX  " + String (roundToInt (mixSlider.getValue())) + "%", P.getX(), 494, P.getWidth(), 14, Justification::centred, false);
    g.setColour (Pal::steel);
    g.drawText ("DRY", P.getX() + 6, 513, 34, 14, Justification::centredLeft, false);
    g.drawText ("WET", P.getRight() - 40, 513, 34, 14, Justification::centredRight, false);
}
