#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;
    constexpr float kTopDb = 6.0f, kBottomDb = -96.0f;
    const float kStart = juce::degreesToRadians (225.0f), kEnd = juce::degreesToRadians (495.0f);

    juce::String hzText (float hz) { return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz"; }

    // what CHARACTER does in each mode: the panel's label and the knob's readout
    const char* characterRange (int mode)
    {
        static const char* r[] = { "EVEN \xe2\x86\x94 ODD", "TIGHT \xe2\x86\x94 FLAT", "STEPS \xe2\x86\x94 BLOCKS", "TIME \xe2\x86\x94 SPACE", "PHASE \xe2\x86\x94 GATE", "SQUEEZE \xe2\x86\x94 STRETCH" };
        return r[juce::jlimit (0, sh::kNumModes - 1, mode)];
    }
    juce::String characterText (int mode, float c)
    {
        const int p = juce::roundToInt (c * 100.0f);
        switch (mode)
        {
            case sh::Harmonics: return "EVEN " + juce::String (100 - p) + juce::String::fromUTF8 (" \xc2\xb7 ") + "ODD " + juce::String (p);
            case sh::Clip:      return "WIDTH " + juce::String (p) + "%";
            case sh::Crush:     return "BLOCKS " + juce::String (p) + "%";
            case sh::Smear:     return "BLUR " + juce::String (p) + "%";
            case sh::Scramble:  return "GATE " + juce::String (p) + "%";
            default:            return std::abs (p - 50) < 2 ? juce::String ("SHIFT ONLY") : (p < 50 ? "SQUEEZE " + juce::String ((50 - p) * 2) + "%" : "STRETCH " + juce::String ((p - 50) * 2) + "%");
        }
    }
    const char* modeTip (int m)
    {
        static const char* t[] = {
            "HARMONICS: adds 2nd to 8th harmonics to every tone, at its exact pitch. Thick and warm, but chords stay clean "
            "(no intermodulation) and nothing folds back as aliasing. CHARACTER: even (warm, tube) to odd (hard, buzzy).",
            "CLIP: flattens the spectrum: quiet partials and noise are pulled up to meet the loud ones. Aggressive and in your face. "
            "CHARACTER: tight (keeps the shape of the sound) to flat (a wall of grit).",
            "CRUSH: a spectral bitcrusher. Levels snap to coarse steps and neighbouring frequencies merge into blocks: steppy, robotic, lo-fi, "
            "without the usual aliasing whistle. CHARACTER: block size.",
            "SMEAR: frequencies are held and blurred over time: washed out, broken-tape and reverb-like textures. CHARACTER: how far it also "
            "blurs across frequency.",
            "SCRAMBLE: randomises the phases: breathy and whispery at first, then noise shaped like the sound. CHARACTER: also gates "
            "frequencies at random for a sparkly, glitchy texture.",
            "WARP: shifts the spectrum up and stretches or squeezes it, so harmonics stop lining up: metallic, bell-like, like ring "
            "modulation but smoother. CHARACTER: squeeze (left), shift only (middle), stretch (right)." };
        return t[juce::jlimit (0, 5, m)];
    }
}

//==============================================================================
ShrapnelDisplay::ShrapnelDisplay (ShrapnelProcessor& p, ShrapnelLookAndFeel& l) : processor (p), lnf (l)
{
    inCol.fill (-120.0f); outCol.fill (-120.0f); heatCol.fill (0.0f);
    points = processor.getCurve();
    shards.reserve (200);
    setOpaque (true);
}

juce::Rectangle<float> ShrapnelDisplay::plot() const { return getLocalBounds().toFloat().withTrimmedLeft (34.0f).withTrimmedRight (12.0f).withTrimmedTop (12.0f).withTrimmedBottom (24.0f); }
float ShrapnelDisplay::xOf (float hz) const { const auto r = plot(); return r.getX() + r.getWidth() * std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz); }
float ShrapnelDisplay::hzOf (float x) const { const auto r = plot(); return kMinHz * std::pow (kMaxHz / kMinHz, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth())); }
float ShrapnelDisplay::yOfDb (float db) const { const auto r = plot(); return r.getY() + r.getHeight() * (kTopDb - db) / (kTopDb - kBottomDb); }
float ShrapnelDisplay::yOfMult (float m) const { const auto r = plot(); return r.getBottom() - m / 3.0f * r.getHeight() * 0.92f; }
float ShrapnelDisplay::multOf (float y) const { const auto r = plot(); return juce::jlimit (0.0f, 3.0f, (r.getBottom() - y) / (r.getHeight() * 0.92f) * 3.0f); }

float ShrapnelDisplay::amountFor (int skip, float hz, float mult) const
{
    // the point's bell is set so the curve passes through the wanted multiplier at its frequency
    auto s = processor.currentSettings();
    s.numPoints = (int) points.size();
    for (size_t i = 0; i < points.size(); ++i) s.points[i] = points[i];
    return juce::jlimit (-1.0f, 1.0f, sh::curveFromMultiplier (mult) - sh::curveSum (s, hz, skip));
}

int ShrapnelDisplay::pointAt (juce::Point<float> m) const
{
    const auto s = processor.currentSettings();
    for (int i = 0; i < (int) points.size(); ++i)
    {
        const juce::Point<float> c (xOf (points[(size_t) i].freq), yOfMult (sh::curveMultiplier (s, points[(size_t) i].freq)));
        if (c.getDistanceFrom (m) < 11.0f) return i;
    }
    return -1;
}

void ShrapnelDisplay::update (float dt)
{
    if (processor.copyDisplay (data) && data.bins > 2)
    {
        std::array<float, kCols> inNow, outNow, heatNow;
        inNow.fill (-140.0f); outNow.fill (-140.0f); heatNow.fill (0.0f);
        const float logSpan = std::log (kMaxHz / kMinHz);
        for (int k = 1; k < data.bins; ++k)
        {
            const float hz = (float) (k * data.binHz);
            if (hz < kMinHz || hz > kMaxHz) continue;
            const int c = juce::jlimit (0, kCols - 1, (int) (std::log (hz / kMinHz) / logSpan * kCols));
            inNow[(size_t) c] = juce::jmax (inNow[(size_t) c], data.inDb[(size_t) k]);
            outNow[(size_t) c] = juce::jmax (outNow[(size_t) c], data.outDb[(size_t) k]);
            heatNow[(size_t) c] = juce::jmax (heatNow[(size_t) c], data.heat[(size_t) k]);
        }
        // columns no bin landed in (low octaves): read between the two nearest bins
        for (int c = 0; c < kCols; ++c)
        {
            if (inNow[(size_t) c] > -139.0f) continue;
            const float hz = kMinHz * std::exp (logSpan * ((float) c + 0.5f) / (float) kCols);
            const float pos = (float) (hz / data.binHz);
            const int k0 = juce::jlimit (1, data.bins - 2, (int) pos);
            const float f = juce::jlimit (0.0f, 1.0f, pos - (float) k0);
            auto lerp = [f, k0] (const std::vector<float>& v) { return v[(size_t) k0] + (v[(size_t) k0 + 1] - v[(size_t) k0]) * f; };
            inNow[(size_t) c] = lerp (data.inDb); outNow[(size_t) c] = lerp (data.outDb); heatNow[(size_t) c] = lerp (data.heat);
        }
        for (int c = 0; c < kCols; ++c)
        {
            auto fall = [] (float& shown, float now, float rate) { shown = now >= shown ? now : juce::jmax (now, shown - rate); };
            fall (inCol[(size_t) c], inNow[(size_t) c], 1.6f);
            fall (outCol[(size_t) c], outNow[(size_t) c], 1.6f);
            heatCol[(size_t) c] = heatNow[(size_t) c];
        }
    }

    // shrapnel: shards thrown off the hardest-hit peaks, then falling
    const auto r = plot();
    for (auto& s : shards)
    {
        s.vy += 260.0f * dt;
        s.x += s.vx * dt; s.y += s.vy * dt;
        s.rot += s.spin * dt;
        s.life -= dt;
    }
    shards.erase (std::remove_if (shards.begin(), shards.end(), [&] (const Shard& s) { return s.life <= 0.0f || s.y > r.getBottom(); }), shards.end());
    for (int c = 0; c < kCols && shards.size() < 180; ++c)
    {
        const float h = heatCol[(size_t) c], lvl = outCol[(size_t) c];
        if (h < 0.08f || lvl < -66.0f) continue;
        const float chance = h * (lvl + 66.0f) / 66.0f * 0.05f * dt * 30.0f;
        if (rng.nextFloat() > chance) continue;
        Shard s;
        s.x = r.getX() + r.getWidth() * ((float) c + 0.5f) / (float) kCols;
        s.y = juce::jlimit (r.getY(), r.getBottom(), yOfDb (lvl));
        s.vx = (rng.nextFloat() - 0.5f) * 70.0f;
        s.vy = -(50.0f + rng.nextFloat() * 120.0f) * (0.6f + juce::jmin (1.0f, h / 2.0f));
        s.maxLife = s.life = 0.35f + rng.nextFloat() * 0.6f;
        s.size = 1.5f + rng.nextFloat() * 3.2f;
        s.rot = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        s.spin = (rng.nextFloat() - 0.5f) * 14.0f;
        s.colour = Pal::heat (rng.nextFloat());
        shards.push_back (s);
    }

    if (dragging < 0) points = processor.getCurve();
    repaint();
}

void ShrapnelDisplay::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    const auto r = plot();
    const auto s = processor.currentSettings();
    const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();

    // ---- the well: scanlines, grid, labels (cached)
    if (! grid.isValid() || ps != gridScale || gridW != getWidth() || gridH != getHeight())
    {
        gridScale = ps; gridW = getWidth(); gridH = getHeight();
        grid = Image (Image::RGB, jmax (1, roundToInt (b.getWidth() * ps)), jmax (1, roundToInt (b.getHeight() * ps)), false);
        Graphics gg (grid);
        gg.addTransform (AffineTransform::scale (ps));
        gg.fillAll (Pal::well);
        gg.setColour (Colours::white.withAlpha (0.025f));
        for (float y = 0.0f; y < b.getHeight(); y += 4.0f) gg.fillRect (0.0f, y, b.getWidth(), 1.0f);
        gg.setFont (lnf.font (10.0f, false));
        for (float hz : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
        {
            gg.setColour (Colour (0xff2a2c31));
            gg.drawVerticalLine (roundToInt (xOf (hz)), r.getY(), r.getBottom());
            gg.setColour (Colour (0xff5f626a));
            gg.drawText (hz >= 1000.f ? String ((int) (hz / 1000)) + "k" : String ((int) hz), Rectangle<float> (xOf (hz) - 20.0f, r.getBottom() + 5.0f, 40.0f, 14.0f), Justification::centred, false);
        }
        for (int db : { 0, -24, -48, -72 })
        {
            gg.setColour (Colour (0xff25272b));
            gg.drawHorizontalLine (roundToInt (yOfDb ((float) db)), r.getX(), r.getRight());
            gg.setColour (Colour (0xff5f626a));
            gg.drawText (String (db), Rectangle<float> (0.0f, yOfDb ((float) db) - 7.0f, r.getX() - 6.0f, 14.0f), Justification::centredRight, false);
        }
        gg.setColour (Colours::black);
        gg.drawRect (b, 1.0f);
    }
    g.drawImage (grid, b, RectanglePlacement::stretchToFit);

    // ---- spectra
    auto colX = [&] (int c) { return r.getX() + r.getWidth() * ((float) c + 0.5f) / (float) kCols; };
    auto clampY = [&] (float db) { return jlimit (r.getY(), r.getBottom(), yOfDb (db)); };
    auto line = [&] (const std::array<float, kCols>& v)
    {
        Path p;
        for (int c = 0; c < kCols; ++c) { const float y = clampY (v[(size_t) c]); c == 0 ? p.startNewSubPath (colX (c), y) : p.lineTo (colX (c), y); }
        return p;
    };
    auto filled = [&] (Path p) { p.lineTo (colX (kCols - 1), r.getBottom()); p.lineTo (colX (0), r.getBottom()); p.closeSubPath(); return p; };

    const Path in = line (inCol), out = line (outCol);
    g.setColour (Colour (160, 165, 175).withAlpha (0.10f));
    g.fillPath (filled (in));
    g.setColour (Colour (0xff7d8189).withAlpha (0.8f));
    g.strokePath (in, PathStrokeType (1.0f));

    ColourGradient fill (Colour (0xffff5a30).withAlpha (0.55f), 0.0f, r.getY(), Pal::red.withAlpha (0.0f), 0.0f, r.getBottom(), false);
    fill.addColour (0.6, Pal::red.withAlpha (0.12f));
    g.setGradientFill (fill);
    g.fillPath (filled (out));
    ColourGradient stroke (Pal::red, r.getX(), 0.0f, Pal::amber, r.getRight(), 0.0f, false);
    stroke.addColour (0.6, Pal::orange);
    g.setGradientFill (stroke);
    g.setOpacity (0.3f);
    g.strokePath (out, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setGradientFill (stroke);
    g.setOpacity (1.0f);
    g.strokePath (out, PathStrokeType (1.5f));

    // ---- shards
    for (const auto& sh : shards)
    {
        Path t;
        t.addTriangle (0.0f, -sh.size, sh.size * 1.3f, sh.size * 0.4f, -sh.size * 0.6f, sh.size * 0.9f);
        g.setColour (sh.colour.withAlpha (jlimit (0.0f, 1.0f, 0.25f + 0.75f * sh.life / sh.maxLife)));
        g.fillPath (t, AffineTransform::rotation (sh.rot).translated (sh.x, sh.y));
    }

    // ---- drive curve (cached until it changes)
    const bool hotCurve = hover >= 0 || dragging >= 0;
    String key = String (s.drive, 3) + "|" + String ((int) hotCurve) + "|" + String (ps) + "|" + String (getWidth());
    for (int i = 0; i < s.numPoints; ++i) key << "|" << s.points[(size_t) i].freq << "," << s.points[(size_t) i].amount;
    if (key != curveKey)
    {
        curveKey = key;
        curveImage = Image (Image::ARGB, jmax (1, roundToInt (b.getWidth() * ps)), jmax (1, roundToInt (b.getHeight() * ps)), true);
        Graphics cg (curveImage);
        cg.addTransform (AffineTransform::scale (ps));
        Path curve;
        for (int i = 0; i <= 220; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / 220.0f;
            const float y = yOfMult (sh::curveMultiplier (s, hzOf (x)));
            i == 0 ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
        }
        const float dash[] = { 7.0f, 4.0f };
        Path dashed; PathStrokeType (2.0f).createDashedStroke (dashed, curve, dash, 2);
        const float alpha = 0.45f + 0.55f * jmin (1.0f, s.drive * 1.6f);
        cg.setColour (Pal::hazard.withAlpha (alpha * 0.25f));
        cg.strokePath (curve, PathStrokeType (5.0f));
        cg.setColour (Pal::hazard.withAlpha (alpha));
        cg.fillPath (dashed);
    }
    g.setOpacity (1.0f);
    g.drawImage (curveImage, b, RectanglePlacement::stretchToFit);

    // ---- points
    for (int i = 0; i < (int) points.size(); ++i)
    {
        const auto& p = points[(size_t) i];
        const float m = sh::curveMultiplier (s, p.freq);
        const Point<float> c (xOf (p.freq), yOfMult (m));
        const bool active = i == hover || i == dragging;
        Path d; d.addRectangle (-7.0f, -7.0f, 14.0f, 14.0f);
        d.applyTransform (AffineTransform::rotation (MathConstants<float>::pi * 0.25f).translated (c));
        g.setColour (active ? Pal::hazard : Colour (0xff1c1d20));
        g.fillPath (d);
        g.setColour (Pal::hazard);
        g.strokePath (d, PathStrokeType (2.0f));
        if (active)
        {
            g.setFont (lnf.font (11.0f, true, 0.5f));
            const String t = hzText (p.freq) + String::fromUTF8 (" \xc2\xb7 DRIVE \xc3\x97") + String (m, 1);
            const float tx = c.x + 14.0f > r.getRight() - 150.0f ? c.x - 160.0f : c.x + 14.0f;
            g.drawText (t, Rectangle<float> (tx, c.y - 24.0f, 150.0f, 14.0f), tx < c.x ? Justification::centredRight : Justification::centredLeft, false);
        }
    }
}

void ShrapnelDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = pointAt (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
}

void ShrapnelDisplay::mouseExit (const juce::MouseEvent&) { hover = -1; repaint(); }

void ShrapnelDisplay::mouseDown (const juce::MouseEvent& e)
{
    const int h = pointAt (e.position);
    if (h >= 0 && e.mods.isPopupMenu())
    {
        points.erase (points.begin() + h);
        processor.setCurve (points);
        hover = -1;
        repaint();
        return;
    }
    dragging = h;
}

void ShrapnelDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    auto& p = points[(size_t) dragging];
    p.freq = juce::jlimit (kMinHz, kMaxHz, hzOf (e.position.x));
    p.amount = amountFor (dragging, p.freq, multOf (e.position.y));
    processor.setCurve (points);
    const auto sorted = processor.getCurve();     // setCurve sorts by frequency: keep holding the same point
    for (int i = 0; i < (int) sorted.size(); ++i)
        if (std::abs (sorted[(size_t) i].freq - p.freq) < 0.01f && std::abs (sorted[(size_t) i].amount - p.amount) < 1.0e-4f) { points = sorted; dragging = hover = i; break; }
    repaint();
}

void ShrapnelDisplay::mouseUp (const juce::MouseEvent&) { dragging = -1; }

void ShrapnelDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = pointAt (e.position);
    if (h >= 0)
        points.erase (points.begin() + h);
    else if (plot().contains (e.position) && (int) points.size() < sh::kMaxCurvePoints)
    {
        const float hz = hzOf (e.position.x);
        points.push_back ({ hz, amountFor (-1, hz, multOf (e.position.y)) });
    }
    processor.setCurve (points);
    points = processor.getCurve();
    hover = pointAt (e.position);
    repaint();
}

//==============================================================================
class PresetMenuItem : public juce::PopupMenu::CustomComponent
{
public:
    PresetMenuItem (const juce::String& n, PresetManager& pm, ShrapnelLookAndFeel& l)
        : CustomComponent (true), name (n), description (PresetManager::getDescription (n)), presets (pm), lnf (l) {}
    void getIdealSize (int& w, int& h) override { w = 360; h = 38; }
    void paint (juce::Graphics& g) override
    {
        const bool hot = isItemHighlighted(), current = presets.getCurrentName() == name;
        if (hot) { g.setColour (Pal::red); g.fillRect (getLocalBounds().reduced (2, 1)); }
        if (current) { g.setColour (hot ? juce::Colours::white : Pal::red); g.fillRect (5, 8, 3, getHeight() - 16); }
        g.setColour (hot ? juce::Colours::white : (current ? Pal::red : Pal::text));
        g.setFont (lnf.font (12.0f, true, 0.5f));
        g.drawText (name, 14, 3, getWidth() - 20, 18, juce::Justification::centredLeft, true);
        g.setColour (hot ? juce::Colours::white.withAlpha (0.85f) : Pal::steel);
        g.setFont (lnf.font (10.0f, false));
        g.drawText (description, 14, 21, getWidth() - 20, 13, juce::Justification::centredLeft, true);
    }
private:
    juce::String name, description;
    PresetManager& presets;
    ShrapnelLookAndFeel& lnf;
};

//==============================================================================
ShrapnelUI::ShrapnelUI (ShrapnelProcessor& p) : processor (p), display (p, lnf)
{
    lnf.regular = juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratRegular_ttf, BinaryData::MontserratRegular_ttfSize);
    lnf.semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratSemiBold_ttf, BinaryData::MontserratSemiBold_ttfSize);
    lnf.oswald = juce::Typeface::createSystemTypefaceFor (BinaryData::OswaldSemiBold_ttf, BinaryData::OswaldSemiBold_ttfSize);
    setLookAndFeel (&lnf);
    logo = juce::ImageCache::getFromMemory (BinaryData::shrapnel_logo_png, BinaryData::shrapnel_logo_pngSize);

    addAndMakeVisible (display);
    display.setTooltip ("The spectrum: grey is what comes in, red to amber is what comes out, and the shards fly off where it is hit hardest. "
                        "The amber dashed line is the drive curve: how hard each frequency gets hit.\n"
                        "Double-click to add a point (up to 8) and drag it up to hit that area harder (up to x3) or down to spare it (x0). "
                        "Double-click or right-click a point to remove it.");

    // ---- presets
    presetBox.setTextWhenNothingSelected ("Default");
    presetBox.setTooltip ("PRESETS: click to pick one. Each has a short description in the list. SAVE stores your own, curve points included.");
    presetBox.onChange = [this]
    {
        const auto name = presetBox.getText();
        if (name.isNotEmpty() && name != processor.presets.getCurrentName())
            processor.presets.load (name);
        deleteButton.setEnabled (! PresetManager::isFactory (presetBox.getText()));
    };
    addAndMakeVisible (presetBox);
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (+1); };
    saveButton.onClick = [this] { showSaveDialog(); };
    deleteButton.onClick = [this] { showDeleteDialog(); };
    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    saveButton.setTooltip ("Save the current settings as a preset");
    deleteButton.setTooltip ("Delete this preset (your own presets only)");
    for (auto* b : { &prevButton, &nextButton, &saveButton, &deleteButton }) addAndMakeVisible (*b);
    refreshPresetList();

    // ---- help
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "SHRAPNEL"; o.folderName = "SHRAPNEL"; o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (o);
    }
    helpButton.setClickingTogglesState (true);
    helpButton.getProperties().set ("round", true);
    helpButton.onClick = [this] { setHelp (helpButton.getToggleState()); };
    addAndMakeVisible (helpButton);
    setHelp (settings->getBoolValue ("showHelp", false));

    // ---- knobs
    struct KDef { Knob* k; const char* id; juce::Point<int> c; int r; bool bipolar, hot; double reset; const char* tip; };
    const KDef kdefs[] = {
        { &drive, "drive", { 921, 206 }, 66, false, true, 50.0, "DRIVE: how hard the sound is hit. The level stays about the same, so you hear the character change, not a volume jump." },
        { &character, "character", { 840, 466 }, 30, false, false, 50.0, "CHARACTER: shapes the distortion. What it does depends on the mode (shown above the knob)." },
        { &tone, "tone", { 1000, 466 }, 30, true, false, 0.0, "TONE: tilts the distorted sound darker (left) or brighter (right)." },
        { &mix, "mix", { 806, 632 }, 18, false, false, 100.0, "MIX: blends the untouched sound back in. 100% is fully distorted." },
        { &gain, "gain", { 876, 632 }, 18, true, false, 0.0, "GAIN: output level." },
    };
    for (const auto& d : kdefs)
    {
        auto& k = *d.k;
        k.id = d.id; k.centre = d.c; k.r = d.r;
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setRotaryParameters (kStart, kEnd, true);
        k.slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        k.slider.setMouseDragSensitivity (d.r > 40 ? 300 : 220);
        k.slider.getProperties().set ("bipolar", d.bipolar);
        k.slider.getProperties().set ("hot", d.hot);
        k.slider.setTooltip (d.tip);
        k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, d.id, k.slider);
        k.slider.setDoubleClickReturnValue (true, d.reset);
        addAndMakeVisible (k.slider);
    }

    // ---- SPLIT faders
    struct FDef { Fader* f; const char* id; juce::Rectangle<int> area; double reset; const char* tip; };
    const FDef fdefs[] = {
        { &tonal, "tonal", { 170, 630, 110, 60 }, 90.0, "TONAL: how much the tones (notes, harmonics, resonances) get distorted." },
        { &noise, "noise", { 310, 630, 110, 60 }, 50.0, "NOISE: how much the noisy parts (breath, air, hiss, cymbal wash) get distorted." },
        { &transient, "transient", { 450, 630, 110, 60 }, 20.0, "TRANSIENT: how much the attacks get distorted. Keep it low to leave hits clean and punchy." },
        { &dynamic, "dynamic", { 600, 630, 120, 60 }, 50.0, "DYNAMIC: louder parts get hit harder, quieter parts less, like real analogue gear. 0% hits everything the same." },
    };
    for (const auto& d : fdefs)
    {
        auto& f = *d.f;
        f.id = d.id; f.area = d.area;
        f.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        f.slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        f.slider.setTooltip (d.tip);
        f.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, d.id, f.slider);
        f.slider.setDoubleClickReturnValue (true, d.reset);
        addAndMakeVisible (f.slider);
    }

    // ---- button rows tied to choice parameters
    auto choiceRow = [this, &p] (juce::TextButton* buttons, int count, const char* id, std::unique_ptr<juce::ParameterAttachment>& attach,
                                 std::initializer_list<const char*> labels, std::initializer_list<const char*> tips)
    {
        attach = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (id), [this, buttons, count] (float v)
        {
            for (int i = 0; i < count; ++i) buttons[i].setToggleState (i == juce::roundToInt (v), juce::dontSendNotification);
            repaint();
        });
        int i = 0;
        for (auto* l : labels) buttons[i++].setButtonText (l);
        i = 0;
        for (auto* t : tips) buttons[i++].setTooltip (t);
        for (int b = 0; b < count; ++b)
        {
            auto* a = attach.get();
            buttons[b].onClick = [a, b] { a->setValueAsCompleteGesture ((float) b); };
            addAndMakeVisible (buttons[b]);
        }
        attach->sendInitialUpdate();
    };
    for (int i = 0; i < sh::kNumModes; ++i) modeButtons[i].getProperties().set ("icon", i);
    choiceRow (modeButtons, sh::kNumModes, "mode", modeAttach, { "HARMONICS", "CLIP", "CRUSH", "SMEAR", "SCRAMBLE", "WARP" },
               { modeTip (0), modeTip (1), modeTip (2), modeTip (3), modeTip (4), modeTip (5) });
    choiceRow (stereoButtons, 3, "stereo", stereoAttach, { "ST", "L/R", "M/S" },
               { "STEREO: both sides are processed together, so the stereo image stays put and it stays mono-compatible",
                 "L / R: the left and right channels are processed separately",
                 "M / S: the middle (what both sides share) and the sides are processed separately" });
    choiceRow (qualityButtons, sh::kNumQualities, "quality", qualityAttach, { "LOW LAT", "NORMAL", "HIGH" },
               { "LOW LATENCY: about 11 ms delay. Tightest on drums, coarser on bass.",
                 "NORMAL: about 21 ms delay. The all-rounder.",
                 "HIGH: about 43 ms delay. Finest frequency detail: best for bass and HARMONICS on low notes. Your DAW compensates the delay." });
    choiceRow (clipButtons, 3, "clip", clipAttach, { "OFF", "SOFT", "HARD" },
               { "CLIP OFF: no clipping after the output", "SOFT CLIP: rounds off peaks above -0.1 dBFS", "HARD CLIP: cuts peaks flat at -0.1 dBFS" });

    setSize (kW, kH);
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

ShrapnelUI::~ShrapnelUI()
{
    stopTimer();
    dialog.reset();
    tooltips.reset();
    setLookAndFeel (nullptr);
}

void ShrapnelUI::setHelp (bool on)
{
    helpButton.setToggleState (on, juce::dontSendNotification);
    tooltips = on ? std::make_unique<juce::TooltipWindow> (this, 500) : nullptr;
    helpButton.setTooltip ("Help is on: hover over any control to see what it does. Click to turn the pop-ups off.");
    settings->setValue ("showHelp", on);
    settings->saveIfNeeded();
}

void ShrapnelUI::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& n : PresetManager::getFactoryNames()) presetBox.addItem (n, id++);
    const auto user = PresetManager::getUserNames();
    if (! user.isEmpty())
    {
        presetBox.addSeparator();
        for (const auto& n : user) presetBox.addItem (n, id++);
    }
    for (juce::PopupMenu::MenuItemIterator it (*presetBox.getRootMenu()); it.next();)
    {
        auto& item = it.getItem();
        if (item.itemID > 0) item.customComponent = new PresetMenuItem (item.text, processor.presets, lnf);
    }
    const auto current = processor.presets.getCurrentName();
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (presetBox.getItemText (i) == current) presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
    deleteButton.setEnabled (! PresetManager::isFactory (current));
}

void ShrapnelUI::stepPreset (int delta)
{
    const int n = presetBox.getNumItems();
    if (n == 0) return;
    const int current = presetBox.getSelectedItemIndex();
    presetBox.setSelectedItemIndex (current < 0 ? 0 : (current + delta + n) % n, juce::sendNotificationSync);
}

void ShrapnelUI::showSaveDialog()
{
    const auto current = processor.presets.getCurrentName();
    const bool own = ! PresetManager::isFactory (current);
    dialog = std::make_unique<juce::AlertWindow> ("Save preset", juce::String(), juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel (&lnf);
    dialog->addTextEditor ("name", own ? current : juce::String ("My Preset"), "Name");
    dialog->addTextEditor ("description", own ? PresetManager::getUserDescription (current) : juce::String(), "Description (optional, shown in the preset list)");
    if (auto* d = dialog->getTextEditor ("description")) d->setInputRestrictions (PresetManager::kMaxDescription);
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (dialog == nullptr) return;
        const auto name = dialog->getTextEditorContents ("name");
        const auto description = dialog->getTextEditorContents ("description");
        dialog->setVisible (false);
        if (result == 1 && processor.presets.save (name, description).isNotEmpty()) refreshPresetList();
    }), false);
    if (auto* ed = dialog->getTextEditor ("name")) { ed->grabKeyboardFocus(); ed->selectAll(); }
}

void ShrapnelUI::showDeleteDialog()
{
    const auto name = presetBox.getText();
    if (name.isEmpty() || PresetManager::isFactory (name)) return;
    dialog = std::make_unique<juce::AlertWindow> ("Delete preset", "Delete \"" + name + "\"?", juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel (&lnf);
    dialog->addButton ("Delete", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, name] (int result)
    {
        if (dialog == nullptr) return;
        dialog->setVisible (false);
        if (result == 1 && processor.presets.remove (name)) refreshPresetList();
    }), false);
}

void ShrapnelUI::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.1, (now - lastTick) * 0.001);
    lastTick = now;
    display.update (dt);

    // the readouts: repaint only when one of them changed
    juce::String text;
    for (auto* k : knobs) text << k->slider.getValue() << ";";
    for (auto* f : faders) text << juce::roundToInt (f->slider.getValue()) << ";";
    text << processor.apvts.getRawParameterValue ("mode")->load();
    if (text != lastText) { lastText = text; repaint(); }

    auto toDb = [] (float v) { return juce::Decibels::gainToDecibels (v, -100.0f); };
    const float in = toDb (processor.inPeak.exchange (0.0f)), out = toDb (processor.outPeak.exchange (0.0f));
    inPeakDb = in >= inPeakDb ? in : juce::jmax (in, inPeakDb - 1.2f);
    outPeakDb = out >= outPeakDb ? out : juce::jmax (out, outPeakDb - 1.2f);
    repaint (932, 650, 144, 18);

    if (++frame % 15 == 0 && presetBox.getText() != processor.presets.getCurrentName()) refreshPresetList();
}

void ShrapnelUI::resized()
{
    display.setBounds (20, 110, 718, 380);
    prevButton.setBounds (330, 18, 28, 28);
    presetBox.setBounds (364, 18, 230, 28);
    nextButton.setBounds (600, 18, 28, 28);
    saveButton.setBounds (638, 18, 64, 28);
    deleteButton.setBounds (708, 18, 72, 28);
    helpButton.setBounds (1058, 20, 26, 26);

    for (int i = 0; i < 3; ++i) stereoButtons[i].setBounds (330 + i * 46, 84, 40, 22);
    for (int i = 0; i < sh::kNumQualities; ++i) qualityButtons[i].setBounds (484 + i * 82, 84, 76, 22);
    const float bw = (698.0f - 5.0f * 8.0f) / 6.0f;
    for (int i = 0; i < sh::kNumModes; ++i)
        modeButtons[i].setBounds (juce::roundToInt (30.0f + (float) i * (bw + 8.0f)), 534, juce::roundToInt (bw), 58);
    clipButtons[0].setBounds (930, 598, 44, 24);
    clipButtons[1].setBounds (978, 598, 50, 24);
    clipButtons[2].setBounds (1032, 598, 46, 24);

    for (auto* k : knobs)
    {
        const int side = k->r * 2 + 42;
        k->slider.setBounds (k->centre.x - side / 2, k->centre.y - side / 2, side, side);
    }
    for (auto* f : faders)
        f->slider.setBounds (f->area.getX() - 7, f->area.getY() + 13, f->area.getWidth() + 14, 24);
    staticScale = 0.0f;
}

// a riveted steel plate with a red-marked title
void ShrapnelUI::drawPlate (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title) const
{
    using namespace juce;
    Path box; box.addRectangle (r);
    DropShadow (Colours::black.withAlpha (0.45f), 18, { 0, 6 }).drawForPath (g, box);
    g.setGradientFill (ColourGradient (Pal::plateTop, 0.0f, r.getY(), Pal::plateBot, 0.0f, r.getBottom(), false));
    g.fillRect (r);
    g.setColour (Pal::plateEdge);
    g.drawRect (r, 1.0f);
    g.setColour (Colours::white.withAlpha (0.05f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 1.0f, r.getRight() - 1.0f);
    for (auto c : { Point<float> (r.getX() + 8.0f, r.getY() + 8.0f), Point<float> (r.getRight() - 8.0f, r.getY() + 8.0f),
                    Point<float> (r.getX() + 8.0f, r.getBottom() - 8.0f), Point<float> (r.getRight() - 8.0f, r.getBottom() - 8.0f) })
    {
        g.setColour (Colour (0xff2a2b2f)); g.fillEllipse (Rectangle<float> (6.4f, 6.4f).withCentre (c));
        g.setColour (Colour (0xff5a5d63)); g.fillEllipse (Rectangle<float> (4.4f, 4.4f).withCentre (c));
    }
    if (title.isNotEmpty())
    {
        g.setColour (Pal::red);
        g.fillRect (r.getX() + 18.0f, r.getY() + 14.0f, 3.0f, 12.0f);
        g.setFont (lnf.font (11.0f, true, 2.0f));
        g.setColour (Pal::silver);
        g.drawText (title, Rectangle<float> (r.getX() + 30.0f, r.getY() + 12.0f, 200.0f, 16.0f), Justification::centredLeft, false);
    }
}

void ShrapnelUI::renderStatic (float ps)
{
    using namespace juce;
    staticScale = ps;
    staticLayer = Image (Image::RGB, roundToInt (kW * ps), roundToInt (kH * ps), false);
    Graphics g (staticLayer);
    g.addTransform (AffineTransform::scale (ps));

    // background: dark steel with a faint brushed grain
    g.setGradientFill (ColourGradient (Colour (0xff24262a), 550.0f, -70.0f, Colour (0xff131416), 550.0f + 900.0f, -70.0f, true));
    g.fillAll();
    g.setColour (Colours::white.withAlpha (0.012f));
    for (int x = 0; x < kW; x += 3) g.fillRect ((float) x, 0.0f, 1.0f, (float) kH);

    // top bar
    g.setGradientFill (ColourGradient (Pal::plateTop, 0.0f, 0.0f, Pal::plateBot, 0.0f, 64.0f, false));
    g.fillRect (0, 0, kW, 64);
    g.setColour (Pal::plateEdge);
    g.drawHorizontalLine (64, 0.0f, (float) kW);
    if (logo.isValid())
    {
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        const float h = 60.0f, w = h * (float) logo.getWidth() / (float) logo.getHeight();
        g.drawImage (logo, Rectangle<float> (12.0f, 3.0f, w, h), RectanglePlacement::stretchToFit);
    }

    drawPlate (g, { 14, 76, 730, 420 }, {});
    drawPlate (g, { 14, 506, 730, 98 }, "MODE");
    drawPlate (g, { 14, 614, 730, 92 }, "SPLIT");
    drawPlate (g, { 756, 76, 330, 306 }, "DRIVE");
    drawPlate (g, { 756, 392, 330, 168 }, "CHARACTER");
    drawPlate (g, { 756, 570, 330, 136 }, "OUTPUT");

    // display legend
    g.setFont (lnf.font (10.0f, true, 2.0f));
    g.setColour (Colour (0xff7d8189)); g.drawText ("IN", 34, 88, 30, 14, Justification::centredLeft, false);
    g.setColour (Pal::orange);         g.drawText ("OUT", 58, 88, 40, 14, Justification::centredLeft, false);
    g.setColour (Pal::hazard);         g.drawText ("DRIVE CURVE", 92, 88, 120, 14, Justification::centredLeft, false);

    // hazard stripe under the DRIVE knob
    {
        Graphics::ScopedSaveState save (g);
        const Rectangle<float> stripe (772.0f, 362.0f, 298.0f, 6.0f);
        g.reduceClipRegion (stripe.toNearestInt());
        g.setColour (Pal::hazard.withAlpha (0.85f));
        for (float x = stripe.getX() - 12.0f; x < stripe.getRight() + 12.0f; x += 18.0f)
        {
            Path p; p.addQuadrilateral (x, stripe.getBottom(), x + 9.0f, stripe.getBottom(), x + 15.0f, stripe.getY(), x + 6.0f, stripe.getY());
            g.fillPath (p);
        }
    }

    // labels
    g.setFont (lnf.font (10.0f, true, 2.0f));
    g.setColour (Pal::steel);
    g.drawText ("WHAT GETS HIT", 32, 648, 120, 14, Justification::centredLeft, false);
    const char* fnames[] = { "TONAL", "NOISE", "TRANSIENT", "DYNAMIC" };
    for (int i = 0; i < 4; ++i)
        g.drawText (fnames[i], faders[i]->area.withHeight (14), Justification::centredLeft, false);
    g.setColour (Pal::plateEdge);
    g.drawVerticalLine (584, 628.0f, 690.0f);
    const char* knames[] = { "DRIVE", "CHARACTER", "TONE", "MIX", "GAIN" };
    g.setColour (Pal::steel);
    for (int i = 0; i < 5; ++i)
    {
        const auto& k = *knobs[i];
        g.drawText (knames[i], Rectangle<float> ((float) k.centre.x - 70.0f, (float) (k.centre.y + k.r + 21), 140.0f, 14.0f).translated (1.0f, 0.0f), Justification::centred, false);
    }
    g.drawText ("OUTPUT CLIP", Rectangle<float> (934.0f, 627.0f, 140.0f, 14.0f), Justification::centred, false);
    g.drawText (String::fromUTF8 ("IN \xc2\xb7 OUT"), Rectangle<float> (934.0f, 672.0f, 140.0f, 14.0f), Justification::centred, false);
}

void ShrapnelUI::paint (juce::Graphics& g)
{
    using namespace juce;
    const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! staticLayer.isValid() || ps != staticScale) renderStatic (ps);
    g.drawImage (staticLayer, Rectangle<float> (0, 0, (float) kW, (float) kH), RectanglePlacement::stretchToFit);

    const int mode = roundToInt (processor.apvts.getRawParameterValue ("mode")->load());

    // panel tags: the current mode, and what CHARACTER does in it
    g.setFont (lnf.font (11.0f, true, 2.0f));
    g.setColour (Pal::orange);
    g.drawText (sh::modeName (mode), Rectangle<float> (98.0f, 518.0f, 200.0f, 16.0f), Justification::centredLeft, false);
    g.drawText (String::fromUTF8 (characterRange (mode)), Rectangle<float> (900.0f, 404.0f, 180.0f, 16.0f), Justification::centredLeft, false);

    // knob readouts
    for (int i = 0; i < 5; ++i)
    {
        const auto& k = *knobs[i];
        String v;
        if (&k == &character) v = characterText (mode, (float) k.slider.getValue() * 0.01f);
        else if (auto* prm = processor.apvts.getParameter (k.id)) v = prm->getCurrentValueAsText();
        g.setFont (lnf.value (&k == &drive ? 15.0f : 13.0f));
        g.setColour (&k == &drive ? Pal::orange : Pal::text);
        g.drawText (v, Rectangle<float> ((float) k.centre.x - 80.0f, (float) (k.centre.y + k.r + 34), 160.0f, 20.0f), Justification::centred, false);
    }
    // fader readouts
    g.setFont (lnf.value (13.0f));
    g.setColour (Pal::text);
    for (auto* f : faders)
        g.drawText (String (roundToInt (f->slider.getValue())) + "%", f->area.withY (f->area.getY() + 40).withHeight (20).toFloat(), Justification::centred, false);

    // meters
    for (int i = 0; i < 2; ++i)
    {
        const Rectangle<float> bar (934.0f, 652.0f + 9.0f * (float) i, 140.0f, 5.0f);
        g.setColour (Pal::well);
        g.fillRect (bar);
        const float db = i == 0 ? inPeakDb : outPeakDb;
        const float t = jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setGradientFill (ColourGradient (Colour (0xffc2402c), bar.getX(), 0.0f, i == 0 ? Pal::orange : Pal::red, bar.getRight(), 0.0f, false));
        g.fillRect (bar.withWidth (bar.getWidth() * t));
    }
}
