#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;
    constexpr float kTopDb = 6.0f, kBottomDb = -96.0f;      // display range for the spectra and the threshold
    const float kStart = juce::degreesToRadians (225.0f), kEnd = juce::degreesToRadians (495.0f);

    juce::String hzText (float hz) { return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz"; }

    const juce::Colour filmInk  { 0xff10181a };            // dark print on the film's labels
    const juce::Colour filmText { 0x8cbedcde };            // rgba(190,220,222,.55)

    juce::Path filmShape (juce::Rectangle<float> b) { juce::Path p; p.addRoundedRectangle (b, 7.0f); return p; }

    // a small paper tag with typewriter text, centred on x, bottom at y
    void drawTag (juce::Graphics& g, const juce::Font& f, const juce::String& t, juce::Rectangle<float> limits, float x, float bottom)
    {
        const float w = juce::GlyphArrangement::getStringWidth (f, t) + 12.0f;
        const auto box = juce::Rectangle<float> (x - w * 0.5f, bottom - 18.0f, w, 18.0f).constrainedWithin (limits);
        g.setColour (Pal::bone.withAlpha (0.92f));
        g.fillRect (box);
        g.setColour (Pal::ink);
        g.setFont (f);
        g.drawText (t, box.translated (0.0f, 1.0f), juce::Justification::centred, false);
    }
}

//==============================================================================
KillDisplay::KillDisplay (SpectraKillProcessor& p, KillLookAndFeel& l) : processor (p), lnf (l)
{
    inCol.fill (-120.0f); outCol.fill (-120.0f); gainCol.fill (0.0f);
    points = processor.getCurve();
    setOpaque (true);                   // the film covers the whole component: nothing behind it needs repainting

    // fixed randomness: drip lengths and the fracture shapes (unit length, pointing down)
    juce::Random rng (2025);
    for (auto& v : dripVar) v = 0.55f + rng.nextFloat() * 0.6f;
    juce::Path unused;
    for (int c = 0; c < kCols; c += 5)
    {
        std::vector<std::pair<juce::Line<float>, float>> segs;
        Bone::crackPath (unused, rng, 0.0f, 0.0f, 1.0f, juce::MathConstants<float>::halfPi + (rng.nextFloat() - 0.5f) * 0.5f, 1, segs, 1.1f);
        std::array<juce::Path, kCrackSizes> shapes;
        for (int q = 0; q < kCrackSizes; ++q)
        {
            const float len = 10.0f + 40.0f * (float) q / (float) (kCrackSizes - 1);
            for (const auto& [line, width] : segs)
            {
                juce::Path seg; seg.startNewSubPath (line.getStart() * len); seg.lineTo (line.getEnd() * len);
                juce::Path stroked; juce::PathStrokeType (juce::jmax (0.7f, width * 0.9f)).createStrokedPath (stroked, seg);
                shapes[(size_t) q].addPath (stroked);
            }
        }
        cracks.push_back (std::move (shapes));
    }
}

juce::Rectangle<float> KillDisplay::plot() const { return getLocalBounds().toFloat().withTrimmedLeft (40.0f).withTrimmedRight (10.0f).withTrimmedTop (18.0f).withTrimmedBottom (24.0f); }
float KillDisplay::xOf (float hz) const { const auto r = plot(); return r.getX() + r.getWidth() * std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz); }
float KillDisplay::hzOf (float x) const { const auto r = plot(); return kMinHz * std::pow (kMaxHz / kMinHz, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth())); }
float KillDisplay::yOf (float db) const { const auto r = plot(); return r.getY() + r.getHeight() * (kTopDb - db) / (kTopDb - kBottomDb); }
float KillDisplay::dbOf (float y) const { const auto r = plot(); return kTopDb - (y - r.getY()) / r.getHeight() * (kTopDb - kBottomDb); }

float KillDisplay::curveWithout (int skip, float hz) const
{
    return sk::SpectralCompressor::thresholdFor (processor.currentSettings(), hz, skip);
}

int KillDisplay::pointAt (juce::Point<float> m) const
{
    const auto s = processor.currentSettings();
    for (int i = 0; i < (int) points.size(); ++i)
    {
        const juce::Point<float> c (xOf (points[(size_t) i].freq), yOf (sk::SpectralCompressor::thresholdFor (s, points[(size_t) i].freq)));
        if (c.getDistanceFrom (m) < 11.0f) return i;
    }
    return -1;
}

bool KillDisplay::nearCurve (juce::Point<float> m) const
{
    if (! plot().expanded (0, 4).contains (m)) return false;
    return std::abs (yOf (sk::SpectralCompressor::thresholdFor (processor.currentSettings(), hzOf (m.x))) - m.y) < 7.0f;
}

void KillDisplay::setThreshold (float db)
{
    if (auto* p = processor.apvts.getParameter ("threshold"))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-60.0f, 0.0f, db)));
}

void KillDisplay::resized() { gridScale = 0.0f; }   // the film is redrawn on the next paint

void KillDisplay::update()
{
    if (processor.copyDisplay (data) && data.bins > 2)
    {
        // collapse the bins into columns: the loudest bin per column, falling slowly
        std::array<float, kCols> inNow, outNow, gNow;
        inNow.fill (-140.0f); outNow.fill (-140.0f); gNow.fill (0.0f);
        const float logSpan = std::log (kMaxHz / kMinHz);
        for (int k = 1; k < data.bins; ++k)
        {
            const float hz = (float) (k * data.binHz);
            if (hz < kMinHz || hz > kMaxHz) continue;
            const int c = juce::jlimit (0, kCols - 1, (int) (std::log (hz / kMinHz) / logSpan * kCols));
            inNow[(size_t) c] = juce::jmax (inNow[(size_t) c], data.inDb[(size_t) k]);
            outNow[(size_t) c] = juce::jmax (outNow[(size_t) c], data.outDb[(size_t) k]);
            if (std::abs (data.gainDb[(size_t) k]) > std::abs (gNow[(size_t) c])) gNow[(size_t) c] = data.gainDb[(size_t) k];
        }
        // columns no bin landed in (the low octaves, where bins are far apart): read between the
        // two nearest bins instead, so the low end follows the harmonics rather than drawing steps
        for (int c = 0; c < kCols; ++c)
        {
            if (inNow[(size_t) c] > -139.0f) continue;
            const float hz = kMinHz * std::exp (logSpan * ((float) c + 0.5f) / (float) kCols);
            const float pos = (float) (hz / data.binHz);
            const int k0 = juce::jlimit (1, data.bins - 2, (int) pos);
            const float f = juce::jlimit (0.0f, 1.0f, pos - (float) k0);
            auto lerp = [f, k0] (const std::vector<float>& v) { return v[(size_t) k0] + (v[(size_t) k0 + 1] - v[(size_t) k0]) * f; };
            inNow[(size_t) c] = lerp (data.inDb);
            outNow[(size_t) c] = lerp (data.outDb);
            gNow[(size_t) c] = lerp (data.gainDb);
        }
        maxCutDb = 0.0f;
        for (int c = 0; c < kCols; ++c)
        {
            auto fall = [] (float& shown, float now, float rate) { shown = now >= shown ? now : juce::jmax (now, shown - rate); };
            fall (inCol[(size_t) c], inNow[(size_t) c], 1.6f);
            fall (outCol[(size_t) c], outNow[(size_t) c], 1.6f);
            gainCol[(size_t) c] = gNow[(size_t) c];
            maxCutDb = juce::jmin (maxCutDb, gNow[(size_t) c]);
        }
    }

    // blood runs down the film as deep as the cut: it runs fast and dries up slowly
    const float ph = plot().getHeight();
    for (int c = 0; c < kCols; ++c)
    {
        const float cut = juce::jmax (0.0f, inCol[(size_t) c] - outCol[(size_t) c]);
        const float target = cut < 2.0f ? 0.0f : juce::jmin (1.0f, cut / 24.0f) * ph * 0.36f * dripVar[(size_t) c];
        auto& len = dripLen[(size_t) c];
        len = target >= len ? len + (target - len) * 0.6f : juce::jmax (target, len - 1.6f);
    }

    if (dragging < 0) points = processor.getCurve();
    repaint();
}

void KillDisplay::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    const auto r = plot();
    const auto s = processor.currentSettings();
    const auto film = filmShape (b);

    // the film: tinted base, grain, scratches, edge band, grid and labels. Drawn once at the
    // screen's real pixel density, then copied
    const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! grid.isValid() || ps != gridScale || gridW != getWidth() || gridH != getHeight())
    {
        gridScale = ps; gridW = getWidth(); gridH = getHeight();
        grid = Image (Image::RGB, jmax (1, roundToInt (getWidth() * ps)), jmax (1, roundToInt (getHeight() * ps)), false);
        Graphics gg (grid);
        gg.addTransform (AffineTransform::scale (ps));
        gg.fillAll (Colour (0xffeef3f2));                     // the lightbox glowing past the film's rounded corners
        ColourGradient base (Colour (0xff1d2e33), b.getWidth() * 0.5f, b.getHeight() * 0.45f,
                             Colour (0xff070c0e), b.getWidth() * 0.5f + b.getWidth() * 0.62f, b.getHeight() * 0.45f, true);
        base.addColour (0.6, Colour (0xff111c20));
        gg.setGradientFill (base);
        gg.fillPath (film);
        gg.reduceClipRegion (film);
        const auto light = Colour (190, 225, 230);
        gg.drawImage (Bone::noise (grid.getWidth(), grid.getHeight(), 37.0f, 0.05f, light, light, 41, jmax (1, roundToInt (ps))), b);
        Random rng (17);
        for (int i = 0; i < 26; ++i)                          // scratches
        {
            const float x = rng.nextFloat() * b.getWidth(), y = rng.nextFloat() * b.getHeight(), l = 20.0f + rng.nextFloat() * 160.0f, a = (rng.nextFloat() - 0.5f) * 0.5f;
            gg.setColour (Colour (200, 230, 235).withAlpha (0.03f + 0.05f * rng.nextFloat()));
            gg.drawLine (x, y, x + std::cos (a) * l, y + std::sin (a) * l, 0.6f);
        }
        gg.setColour (Colours::black.withAlpha (0.55f));      // the darker band round the edge of an X-ray sheet
        gg.drawRect (b, 5.0f);
        gg.setColour (Colour (0xff7e0910));                   // dried blood along the top edge
        gg.fillRect (b.withHeight (5.0f));

        auto dashed = [&gg] (Line<float> l, float alpha)
        {
            Path p; p.startNewSubPath (l.getStart()); p.lineTo (l.getEnd());
            const float dash[] = { 2.0f, 4.0f };
            Path d; PathStrokeType (1.0f).createDashedStroke (d, p, dash, 2);
            gg.setColour (Colour (190, 225, 230).withAlpha (alpha));
            gg.fillPath (d);
        };
        gg.setFont (lnf.type (11.0f));
        for (float hz : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        {
            dashed ({ xOf (hz), r.getY(), xOf (hz), r.getBottom() }, 0.07f);
            gg.setColour (filmText);
            const auto txt = hz >= 1000.f ? String ((int) (hz / 1000)) + "k" : String ((int) hz);
            const float lx = jlimit (r.getX() + 10.0f, r.getRight() - 12.0f, xOf (hz));     // keep the end labels inside
            gg.drawText (txt, Rectangle<float> (lx - 20.0f, r.getBottom() + 5.0f, 40.0f, 13.0f), Justification::centred, false);
        }
        for (int db = 0; db >= -90; db -= 15)
        {
            dashed ({ r.getX(), yOf ((float) db), r.getRight(), yOf ((float) db) }, db == 0 ? 0.12f : 0.06f);
            gg.setColour (filmText);
            gg.drawText (String (db), Rectangle<float> (0, yOf ((float) db) - 6.0f, r.getX() - 7.0f, 13.0f), Justification::centredRight, false);
        }
    }
    g.drawImage (grid, b, RectanglePlacement::stretchToFit);

    // ---- the spectra
    auto colX = [&] (int c) { return r.getX() + r.getWidth() * ((float) c + 0.5f) / (float) kCols; };
    auto clampY = [&] (float db) { return jlimit (r.getY(), r.getBottom(), yOf (db)); };
    auto outline = [&] (const std::array<float, kCols>& v)
    {
        Path p;
        for (int c = 0; c < kCols; ++c) { const float y = clampY (v[(size_t) c]); c == 0 ? p.startNewSubPath (colX (c), y) : p.lineTo (colX (c), y); }
        return p;
    };

    // the output is the bone: dense at its top edge, fading into the film below
    const auto outLine = outline (outCol);
    {
        Path fill (outLine);
        fill.lineTo (r.getRight(), r.getBottom());
        fill.lineTo (r.getX(), r.getBottom());
        fill.closeSubPath();
        ColourGradient bg (Colour (235, 240, 232).withAlpha (0.62f), 0, r.getY(), Colour (160, 190, 195).withAlpha (0.04f), 0, r.getBottom(), false);
        bg.addColour (0.45, Colour (200, 215, 212).withAlpha (0.26f));
        g.setGradientFill (bg);
        g.fillPath (fill);
    }
    // a glowing cortex along the top of the bone
    g.setColour (Colour (230, 245, 240).withAlpha (0.2f)); g.strokePath (outLine, PathStrokeType (4.5f));
    g.setColour (Colour (0xfff6f3ea)); g.strokePath (outLine, PathStrokeType (1.6f));

    // the input: a dashed ghost outline where the bone used to be
    {
        const float dash[] = { 3.0f, 2.0f };
        Path d; PathStrokeType (1.0f).createDashedStroke (d, outline (inCol), dash, 2);
        g.setColour (Colour (200, 225, 228).withAlpha (0.35f));
        g.fillPath (d);
    }

    // what got killed: dried blood between the ghost and the bone. The same dark tone as the
    // KILL readout's frame and the splatter around it, so it never blends into the bright fresh
    // blood running down over it
    {
        Path kill, edge;
        for (int c = 0; c < kCols; ++c) { const float y = clampY (inCol[(size_t) c]); c == 0 ? kill.startNewSubPath (colX (c), y) : kill.lineTo (colX (c), y); }
        edge = kill;
        for (int c = kCols - 1; c >= 0; --c) kill.lineTo (colX (c), clampY (jmin (inCol[(size_t) c], outCol[(size_t) c])));
        kill.closeSubPath();
        g.setGradientFill (ColourGradient (Colour (0xff6a0a10).withAlpha (0.94f), 0, r.getY(), Colour (0xff2a0306).withAlpha (0.92f), 0, r.getY() + r.getHeight() * 0.6f, false));
        g.fillPath (kill);
        g.setColour (Colour (0xffb04048).withAlpha (0.6f));       // a lighter rim along the top, where the sound used to reach
        g.strokePath (edge, PathStrokeType (1.0f));
    }

    // fractures where the cut is deep: short dark cracks running down into the bone
    {
        Path fr;
        for (int i = 0, c = 3; c < kCols && i < (int) cracks.size(); c += 5, ++i)
        {
            const float cut = inCol[(size_t) c] - outCol[(size_t) c];
            if (cut < 6.0f) continue;
            const int q = roundToInt (jmin (1.0f, (cut - 6.0f) * 1.6f / 40.0f) * (float) (kCrackSizes - 1));
            fr.addPath (cracks[(size_t) i][(size_t) q], AffineTransform::translation (colX (c), clampY (outCol[(size_t) c]) + 1.0f));
        }
        g.setColour (Colour (10, 20, 22).withAlpha (0.75f));
        g.fillPath (fr);
    }

    // blood running down from the top of the film
    {
        RectangleList<float> runs;                             // straight runs are plain rectangles (cheap), the drops round
        Path drops, shine;
        const float w = r.getWidth() / (float) kCols * 2.0f, top = 4.0f;
        for (int c = 1; c < kCols; c += 3)                    // every third column: gaps keep the film readable
        {
            const float len = dripLen[(size_t) c];
            if (len < 0.5f) continue;
            const float x = colX (c), total = len + (r.getY() - top);
            runs.addWithoutMerging ({ x - w * 0.4f, top, w * 0.8f, total });
            drops.addEllipse (x - w * 0.55f, top + total - w * 0.55f, w * 1.1f, w * 1.1f);
            shine.addEllipse (x - w * 0.18f - w * 0.16f, top + total - w * 0.15f - w * 0.16f, w * 0.32f, w * 0.32f);
        }
        g.setColour (Colour (176, 14, 22).withAlpha (0.92f));
        g.fillRectList (runs);
        g.fillPath (drops);
        g.setColour (Colours::white.withAlpha (0.25f));
        g.fillPath (shine);
    }

    // ---- the threshold: a red grease-pencil line (cached until the curve or the hover changes)
    const bool hot = hoverCurve || draggingCurve;
    String key = String (s.thresholdDb) + "|" + String (s.tiltDbPerOct) + "|" + String (s.sharpness) + "|" + String ((int) hot) + "|" + String (ps) + "|" + String (getWidth());
    for (int i = 0; i < s.numPoints; ++i) key << "|" << s.points[(size_t) i].freq << "," << s.points[(size_t) i].db;
    if (key != curveKey)
    {
        curveKey = key;
        Path curve;
        for (int i = 0; i <= 260; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / 260.0f;
            const float y = clampY (sk::SpectralCompressor::thresholdFor (s, hzOf (x))) + std::sin ((float) i * 1.7f) * 0.45f;   // a slightly shaky hand
            i == 0 ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
        }
        // only the band the curve sits in: a flat line makes a small image that is quick to copy
        const auto pb = curve.getBounds();
        curveArea = b.getIntersection ({ b.getX(), std::floor (pb.getY() - 8.0f), b.getWidth(), std::ceil (pb.getHeight() + 16.0f) });
        curveImage = Image (Image::ARGB, jmax (1, roundToInt (curveArea.getWidth() * ps)), jmax (1, roundToInt (curveArea.getHeight() * ps)), true);
        Graphics cg (curveImage);
        cg.addTransform (AffineTransform::translation (-curveArea.getX(), -curveArea.getY()).scaled (ps));
        const auto red = Colour (0xffff3a40);
        cg.setColour (red.withAlpha (hot ? 0.16f : 0.10f)); cg.strokePath (curve, PathStrokeType (10.0f, PathStrokeType::curved, PathStrokeType::rounded));
        cg.setColour (red.withAlpha (hot ? 0.38f : 0.28f)); cg.strokePath (curve, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
        cg.setColour (hot ? Colour (0xffff6a6e) : red);     cg.strokePath (curve, PathStrokeType (hot ? 2.9f : 2.4f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.setOpacity (1.0f);                // drawImage uses the current colour's alpha
    g.drawImage (curveImage, curveArea, RectanglePlacement::stretchToFit);

    // points: a bone in cross-section (ivory ring, marrow inside)
    const auto tagFont = lnf.type (13.0f);
    for (int i = 0; i < (int) points.size(); ++i)
    {
        const auto& p = points[(size_t) i];
        const Point<float> c (xOf (p.freq), clampY (sk::SpectralCompressor::thresholdFor (s, p.freq)));
        const bool active = i == hover || i == dragging;
        const float k = active ? 1.15f : 1.0f;
        Path ring;
        for (int j = 0; j < 14; ++j)
        {
            const float a = (float) j / 14.0f * MathConstants<float>::twoPi, rr = (10.0f + (j % 2 ? -1.3f : 0.6f)) * k;
            const Point<float> v (c.x + std::cos (a) * rr, c.y + std::sin (a) * rr);
            j == 0 ? ring.startNewSubPath (v) : ring.lineTo (v);
        }
        ring.closeSubPath();
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillPath (ring, AffineTransform::translation (0.0f, 2.0f));
        g.setColour (Pal::bone);
        g.fillPath (ring);
        if (active) { g.setColour (Pal::bloodBright); g.strokePath (ring, PathStrokeType (1.2f)); }
        g.setColour (Colour (0xff8e1016));
        g.fillEllipse (Rectangle<float> (10.0f * k, 10.0f * k).withCentre (c));
        g.setColour (Colour (0xff3a0306));
        g.fillEllipse (Rectangle<float> (4.8f * k, 4.8f * k).withCentre (c.translated (1.0f, 1.0f)));
        if (active)
            drawTag (g, tagFont, hzText (p.freq) + "  " + (p.db > 0.05f ? "+" : "") + String (p.db, 1) + " dB", r, c.x, c.y - 15.0f);
    }
    if (hot && hover < 0)
        drawTag (g, tagFont, "THRESHOLD  " + String (s.thresholdDb, 1) + " dB", r.reduced (8.0f, 0.0f), r.getX() + 90.0f, r.getBottom() - 46.0f);

    // ---- the film's printed ID block (bottom right) and the lead marker for the stereo mode (bottom left).
    // Text is slow to draw, so each is kept as a small image until what it says changes
    {
        const int lat = processor.latencySamples.load();
        const double rate = processor.hostRate.load();
        const auto preset = processor.presets.getCurrentName();
        StringArray lines;
        lines.add ("SPECIMEN No. " + String (std::abs (preset.hashCode()) % 10000).paddedLeft ('0', 4) + "   " + preset.toUpperCase());
        lines.add (String (sk::qualityName (s.quality)) + "  FFT " + String (sk::SpectralCompressor::fftSizeFor (s.quality, rate)) + "  " + String (lat * 1000.0 / rate, 1) + " MS");
        const bool scOk = processor.sidechainConnected.load();
        if (s.sidechain) lines.add (scOk ? "SIDECHAIN: ACTIVE" : "SIDECHAIN: NOTHING CONNECTED");
        const String mode = s.stereo == sk::LeftRight ? "LR" : s.stereo == sk::MidSide ? "MS" : "ST";

        auto stamp = [&] (Stamp& st, const String& key, Rectangle<float> area, std::function<void (Graphics&, Rectangle<float>)> draw)
        {
            const String k = key + "|" + String (ps) + "|" + area.toString();
            if (k != st.key)
            {
                st.key = k; st.area = area;
                st.image = Image (Image::ARGB, jmax (1, roundToInt (area.getWidth() * ps)), jmax (1, roundToInt (area.getHeight() * ps)), true);
                Graphics sg (st.image);
                sg.addTransform (AffineTransform::scale (ps));
                draw (sg, area.withZeroOrigin());
            }
            g.setOpacity (1.0f);
            g.drawImage (st.image, st.area, RectanglePlacement::stretchToFit);
        };

        const auto f = lnf.type (11.0f);
        float w = 0.0f;
        for (auto& l : lines) w = jmax (w, GlyphArrangement::getStringWidth (f, l));
        w = std::ceil (w + 16.0f);
        const float h = 8.0f + 14.0f * (float) lines.size();
        stamp (idStamp, lines.joinIntoString ("\n"), Rectangle<float> (std::floor (r.getRight() - w - 4.0f), std::floor (r.getBottom() - h - 6.0f), w, h),
               [&] (Graphics& sg, Rectangle<float> box)
        {
            sg.setColour (Colour (225, 235, 232).withAlpha (0.85f));
            sg.fillRect (box);
            sg.setFont (f);
            for (int i = 0; i < lines.size(); ++i)
            {
                sg.setColour (i == 2 && ! scOk ? Pal::blood : filmInk);
                sg.drawText (lines[i], Rectangle<float> (8.0f, 4.0f + 14.0f * (float) i, w - 10.0f, 14.0f), Justification::centredLeft, false);
            }
        });

        const auto mf = lnf.osw (17.0f, 700, 0.5f);
        const float mw = std::ceil (GlyphArrangement::getStringWidth (mf, mode) + 10.0f);
        stamp (modeStamp, mode, Rectangle<float> (r.getX() + 8.0f, r.getBottom() - 32.0f, mw, 24.0f), [&] (Graphics& sg, Rectangle<float> box)
        {
            sg.setColour (Colour (225, 235, 232).withAlpha (0.85f));
            sg.fillRect (box);
            sg.setColour (filmInk);
            sg.setFont (mf);
            sg.drawText (mode, box.translated (0.5f, 0.0f), Justification::centred, false);
        });
    }
}


void KillDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = pointAt (e.position);
    const bool c = h < 0 && nearCurve (e.position);
    if (h != hover || c != hoverCurve) { hover = h; hoverCurve = c; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : c ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor);
}

void KillDisplay::mouseExit (const juce::MouseEvent&) { hover = -1; hoverCurve = false; repaint(); }

void KillDisplay::mouseDown (const juce::MouseEvent& e)
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
    if (h >= 0) { dragging = h; return; }
    if (nearCurve (e.position))
    {
        draggingCurve = true;
        dragStartThr = processor.currentSettings().thresholdDb;
        dragStartY = e.position.y;
        if (auto* p = processor.apvts.getParameter ("threshold")) p->beginChangeGesture();
    }
}

void KillDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging >= 0)
    {
        auto& p = points[(size_t) dragging];
        p.freq = juce::jlimit (kMinHz, kMaxHz, hzOf (e.position.x));
        // the point's bell is set so the curve passes under the mouse
        p.db = juce::jlimit (-36.0f, 36.0f, dbOf (juce::jlimit (plot().getY(), plot().getBottom(), e.position.y)) - curveWithout (dragging, p.freq));
        processor.setCurve (points);
        // setCurve sorts by frequency: keep holding the same point
        const auto sorted = processor.getCurve();
        for (int i = 0; i < (int) sorted.size(); ++i)
            if (std::abs (sorted[(size_t) i].freq - p.freq) < 0.01f && std::abs (sorted[(size_t) i].db - p.db) < 0.01f) { points = sorted; dragging = hover = i; break; }
        repaint();
    }
    else if (draggingCurve)
    {
        const float perPixel = (kTopDb - kBottomDb) / plot().getHeight();
        setThreshold (dragStartThr - (e.position.y - dragStartY) * perPixel);
        repaint();
    }
}

void KillDisplay::mouseUp (const juce::MouseEvent&)
{
    if (draggingCurve)
        if (auto* p = processor.apvts.getParameter ("threshold")) p->endChangeGesture();
    dragging = -1;
    draggingCurve = false;
}

void KillDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = pointAt (e.position);
    if (h >= 0)
    {
        points.erase (points.begin() + h);
    }
    else if (plot().contains (e.position) && (int) points.size() < sk::kMaxCurvePoints)
    {
        const float hz = hzOf (e.position.x);
        points.push_back ({ hz, juce::jlimit (-36.0f, 36.0f, dbOf (e.position.y) - curveWithout (-1, hz)) });
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
    PresetMenuItem (const juce::String& n, PresetManager& pm, KillLookAndFeel& l)
        : CustomComponent (true), name (n), description (PresetManager::getDescription (n)), presets (pm), lnf (l) {}
    void getIdealSize (int& w, int& h) override { w = 340; h = 38; }
    void paint (juce::Graphics& g) override
    {
        const bool hot = isItemHighlighted(), current = presets.getCurrentName() == name;
        if (hot) { g.setColour (Pal::blood); g.fillRect (getLocalBounds().reduced (2, 1)); }
        if (current) { g.setColour (hot ? Pal::paper : Pal::blood); g.fillRect (5, 8, 3, getHeight() - 16); }
        g.setColour (hot ? Pal::paper : (current ? Pal::blood : Pal::ink));
        g.setFont (lnf.osw (13.0f, 600, 1.0f));
        g.drawText (name.toUpperCase(), 14, 2, getWidth() - 20, 20, juce::Justification::centredLeft, true);
        g.setColour (hot ? Pal::paper.withAlpha (0.85f) : Pal::inkSoft);
        g.setFont (lnf.text (10.5f));
        g.drawText (description, 14, 21, getWidth() - 20, 13, juce::Justification::centredLeft, true);
    }
private:
    juce::String name, description;
    PresetManager& presets;
    KillLookAndFeel& lnf;
};

//==============================================================================
SpectraKillUI::SpectraKillUI (SpectraKillProcessor& p) : processor (p), display (p, lnf)
{
    lnf.oswMedium = juce::Typeface::createSystemTypefaceFor (BinaryData::OswaldMedium_ttf, BinaryData::OswaldMedium_ttfSize);
    lnf.oswSemi = juce::Typeface::createSystemTypefaceFor (BinaryData::OswaldSemiBold_ttf, BinaryData::OswaldSemiBold_ttfSize);
    lnf.oswBold = juce::Typeface::createSystemTypefaceFor (BinaryData::OswaldBold_ttf, BinaryData::OswaldBold_ttfSize);
    lnf.elite = juce::Typeface::createSystemTypefaceFor (BinaryData::SpecialEliteRegular_ttf, BinaryData::SpecialEliteRegular_ttfSize);
    lnf.body = juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratRegular_ttf, BinaryData::MontserratRegular_ttfSize);
    setLookAndFeel (&lnf);
    logo = juce::ImageCache::getFromMemory (BinaryData::spectrakill_logo_png, BinaryData::spectrakill_logo_pngSize);

    addAndMakeVisible (display);
    display.setTooltip ("The spectrum as an X-ray: the bright bone is what goes out, the dashed ghost line is what came in, and the dark dried blood "
                        "between them is what SPECTRAKILL takes away (fresh blood runs down from the top as deep as the cut). The red line is the threshold.\n"
                        "Drag the line up or down to set THRESHOLD. Double-click to add a point (up to 8) and drag it to bend the "
                        "threshold there; double-click or right-click a point to remove it.");

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
        o.applicationName = "SPECTRAKILL"; o.folderName = "SPECTRAKILL"; o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (o);
    }
    helpButton.setClickingTogglesState (true);
    helpButton.getProperties().set ("round", true);
    helpButton.onClick = [this] { setHelp (helpButton.getToggleState()); };
    addAndMakeVisible (helpButton);
    setHelp (settings->getBoolValue ("showHelp", false));

    // ---- knobs
    struct Def { Knob* k; const char* id; const char* name; const char* tip; bool bipolar; double reset; };
    const Def defs[] = {
        { &threshold, "threshold", "THRESHOLD", "THRESHOLD: the level where SPECTRAKILL starts working, for every frequency. "
                                                "Anything above the red line gets pushed down (DOWN), anything below can be lifted (UP).", false, -24.0 },
        { &down, "down", "DOWN", "DOWN: how hard frequencies above the threshold are pushed down. 100% holds them at the threshold.", false, 50.0 },
        { &up, "up", "UP", "UP: lifts quiet frequencies below the threshold towards it (upward compression), bringing out detail. "
                           "Never lifts more than 24 dB, and leaves silence alone.", false, 0.0 },
        { &tilt, "tilt", "TILT", "TILT: tips the threshold around 1 kHz. Turn right to hit the highs harder, left to hit the lows harder.", true, 0.0 },
        { &sharpness, "sharpness", "SHARPNESS", "SHARPNESS: how focused the curve points are. Low makes wide, gentle bends; high makes narrow, targeted ones.", false, 50.0 },
        { &attack, "attack", "ATTACK", "ATTACK: how fast a frequency gets pushed down once it crosses the threshold.", false, 10.0 },
        { &release, "release", "RELEASE", "RELEASE: how fast a frequency comes back once it drops below the threshold.", false, 120.0 },
        { &gain, "gain", "GAIN", "GAIN: output level after the processing.", true, 0.0 },
        { &mix, "mix", "MIX", "MIX: blends the untouched sound back in, for parallel compression. 100% is fully processed.", false, 100.0 },
    };
    for (const auto& d : defs)
    {
        auto& k = *d.k;
        k.id = d.id;
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setRotaryParameters (kStart, kEnd, true);
        k.slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        k.slider.setMouseDragSensitivity (240);
        k.slider.setTooltip (d.tip);
        k.slider.getProperties().set ("bipolar", d.bipolar);
        k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, d.id, k.slider);
        k.slider.setDoubleClickReturnValue (true, d.reset);
        k.name.setText (d.name, juce::dontSendNotification);
        k.slider.setName (d.id);
        k.name.setFont (lnf.osw (12.0f, 500, 3.0f));
        k.name.setColour (juce::Label::textColourId, Pal::inkSoft);
        k.name.setJustificationType (juce::Justification::centred);
        k.value.setFont (lnf.osw (16.0f, 700, 0.5f));
        k.value.setColour (juce::Label::textColourId, Pal::ink);
        k.value.setJustificationType (juce::Justification::centred);
        k.name.setInterceptsMouseClicks (false, false);
        k.value.setInterceptsMouseClicks (false, false);
        for (auto* c : { (juce::Component*) &k.slider, (juce::Component*) &k.name, (juce::Component*) &k.value }) addAndMakeVisible (c);
    }

    // ---- switch rows
    auto choiceRow = [this, &p] (juce::TextButton* buttons, int count, const char* id, std::unique_ptr<juce::ParameterAttachment>& attach,
                                 std::initializer_list<const char*> labels, std::initializer_list<const char*> tips)
    {
        auto* param = p.apvts.getParameter (id);
        attach = std::make_unique<juce::ParameterAttachment> (*param, [buttons, count] (float v)
        {
            for (int i = 0; i < count; ++i) buttons[i].setToggleState (i == juce::roundToInt (v), juce::dontSendNotification);
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
    choiceRow (stereoButtons, 3, "stereo", stereoAttach, { "STEREO", "L / R", "M / S" },
               { "STEREO: both channels are processed together, so the stereo image stays put",
                 "L / R: the left and right channels are processed separately",
                 "M / S: the middle (what both sides share) and the sides are processed separately" });
    choiceRow (qualityButtons, sk::kNumQualities, "quality", qualityAttach, { "LOW LAT", "NORMAL", "HIGH", "V.HIGH", "ULTRA" },
               { "LOW LATENCY: about 11 ms delay. Fastest response, coarse frequency detail. Best for drums and live use.",
                 "NORMAL: about 21 ms delay. Good for drums, hats and percussion.",
                 "HIGH: about 43 ms delay. The all-rounder: vocals, guitars, synths.",
                 "VERY HIGH: about 85 ms delay. Finer detail in the lows: bass, piano, pads.",
                 "ULTRA: about 171 ms delay. Finest frequency detail, slowest response: full mixes and mastering. Your DAW compensates the delay." });
    choiceRow (clipButtons, 3, "clip", clipAttach, { "OFF", "SOFT", "HARD" },
               { "CLIP OFF: no clipping after the output", "SOFT CLIP: rounds off peaks above -0.1 dBFS", "HARD CLIP: cuts peaks flat at -0.1 dBFS" });
    {
        auto* param = p.apvts.getParameter ("sidechain");
        sidechainAttach = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { sidechainButton.setToggleState (v > 0.5f, juce::dontSendNotification); });
        sidechainButton.onClick = [this] { sidechainAttach->setValueAsCompleteGesture (sidechainButton.getToggleState() ? 0.0f : 1.0f); };
        sidechainButton.setTooltip ("SIDECHAIN: the compression follows another track's spectrum instead (route it to SPECTRAKILL's sidechain input "
                                    "in your DAW). Where that track is loud, the same frequencies get pushed down here.");
        addAndMakeVisible (sidechainButton);
        sidechainAttach->sendInitialUpdate();
    }

    setSize (kW, kH);
    startTimerHz (30);
}

SpectraKillUI::~SpectraKillUI()
{
    stopTimer();
    dialog.reset();
    tooltips.reset();
    setLookAndFeel (nullptr);
}

void SpectraKillUI::setHelp (bool on)
{
    helpButton.setToggleState (on, juce::dontSendNotification);
    tooltips = on ? std::make_unique<juce::TooltipWindow> (this, 500) : nullptr;
    helpButton.setTooltip ("Help is on: hover over any control to see what it does. Click to turn the pop-ups off.");
    settings->setValue ("showHelp", on);
    settings->saveIfNeeded();
}

void SpectraKillUI::refreshPresetList()
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

void SpectraKillUI::stepPreset (int delta)
{
    const int n = presetBox.getNumItems();
    if (n == 0) return;
    const int current = presetBox.getSelectedItemIndex();
    presetBox.setSelectedItemIndex (current < 0 ? 0 : (current + delta + n) % n, juce::sendNotificationSync);
}

void SpectraKillUI::showSaveDialog()
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

void SpectraKillUI::showDeleteDialog()
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


void SpectraKillUI::timerCallback()
{
    display.update();

    // knob value readouts
    for (auto* k : knobs)
        if (auto* prm = processor.apvts.getParameter (k->id))
        {
            const auto t = prm->getCurrentValueAsText();
            if (k->value.getText() != t) k->value.setText (t, juce::dontSendNotification);
        }

    // meters and the KILL readout
    auto toDb = [] (float v) { return juce::Decibels::gainToDecibels (v, -100.0f); };
    const float in = toDb (processor.inPeak.exchange (0.0f)), out = toDb (processor.outPeak.exchange (0.0f));
    inPeakDb = in >= inPeakDb ? in : juce::jmax (in, inPeakDb - 1.2f);
    outPeakDb = out >= outPeakDb ? out : juce::jmax (out, outPeakDb - 1.2f);
    const float kill = -display.maxCutDb;
    killShown = kill >= killShown ? kill : juce::jmax (kill, killShown - 0.6f);

    if (++frame % 15 == 0 && presetBox.getText() != processor.presets.getCurrentName()) refreshPresetList();
    repaint (killBox.expanded (4));                                                       // the KILL readout
    repaint (outPanel.getX() + 40, outPanel.getY() + 140, outPanel.getWidth() - 50, 24);   // the meters
}

// a bone plate: chipped outline, grain, a crack or two, a stamped blood title and a specimen number
void SpectraKillUI::drawPlate (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, int number, juce::Random& rng) const
{
    using namespace juce;
    const auto r = area.toFloat();
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    auto j = [&rng] (float a) { return (rng.nextFloat() - 0.5f) * a; };
    Path plate;
    auto add = [&plate] (float px, float py) { plate.isEmpty() ? plate.startNewSubPath (px, py) : plate.lineTo (px, py); };
    for (float t = 0.0f; t <= 1.0001f; t += 0.05f)  add (x + w * t, y + j (1.5f));
    for (float t = 0.0f; t <= 1.0001f; t += 0.08f)  add (x + w + j (1.5f), y + h * t);
    for (float t = 1.0f; t >= -0.0001f; t -= 0.035f) add (x + w * t, y + h + (rng.nextFloat() < 0.18f ? -rng.nextFloat() * 6.0f : j (2.0f)));   // chipped bottom edge
    for (float t = 1.0f; t >= -0.0001f; t -= 0.08f) add (x + j (1.5f), y + h * t);
    plate.closeSubPath();

    DropShadow (Colours::black.withAlpha (0.35f), 10, { 0, 4 }).drawForPath (g, plate);
    g.setGradientFill (ColourGradient (Pal::plateTop, 0, y, Pal::plateBot, 0, y + h, false));
    g.fillPath (plate);
    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plate);
        g.setOpacity (0.8f);
        g.drawImage (grain, Rectangle<float> (0, 0, (float) kW, (float) kH));
        g.setOpacity (1.0f);
        Bone::fibres (g, rng, r, 0.10f);
        Bone::crack (g, rng, x + w * (0.2f + rng.nextFloat() * 0.6f), y + h, 40.0f + rng.nextFloat() * 40.0f, -1.4f - rng.nextFloat() * 0.4f, 1.4f, Pal::crack, 2);
        if (rng.nextFloat() < 0.7f)
            Bone::crack (g, rng, x + w, y + h * (0.3f + rng.nextFloat() * 0.5f), 30.0f + rng.nextFloat() * 30.0f, 3.0f, 1.2f, Pal::crack, 1);
    }
    g.setColour (Pal::edge);
    g.strokePath (plate, PathStrokeType (1.0f));

    // stamped title, specimen number, dashed rule
    const auto tf = lnf.osw (15.0f, 700);
    const float tw = GlyphArrangement::getStringWidth (tf, title);
    g.setFont (tf);
    g.setColour (Pal::blood);
    g.drawText (title, Rectangle<float> (x + 14.0f, y + 4.0f, tw + 4.0f, 26.0f), Justification::centredLeft, false);
    g.setFont (lnf.type (11.0f));
    g.setColour (Pal::inkSoft);
    g.drawText ("No. " + String (number), Rectangle<float> (x + 22.0f + tw, y + 11.0f, 80.0f, 14.0f), Justification::centredLeft, false);
    Path rule; rule.startNewSubPath (x + 10.0f, y + 31.0f); rule.lineTo (x + w - 10.0f, y + 31.0f);
    const float dash[] = { 3.0f, 3.0f };
    Path d; PathStrokeType (1.0f).createDashedStroke (d, rule, dash, 2);
    g.setColour (Colour (90, 60, 30).withAlpha (0.35f));
    g.fillPath (d);
}

void SpectraKillUI::layoutKnob (juce::Slider& s, juce::Label& name, juce::Label& value, juce::Rectangle<int> area, int size)
{
    // knob centred in the top of the area (its teeth reach a little past 'size'), name and value underneath
    const int cx = area.getCentreX(), cy = area.getY() + size / 2;
    const int outer = juce::roundToInt ((float) size * 1.1f);
    s.setBounds (cx - outer / 2, cy - outer / 2, outer, outer);
    name.setBounds (cx - 64, area.getY() + size + 4, 130, 20);       // +2: letter spacing trails the last letter
    value.setBounds (cx - 65, area.getY() + size + 19, 130, 24);
}

void SpectraKillUI::resized()
{
    topBar      = { 0, 0, kW, 78 };
    killBox     = { 744, 12, 230, 46 };
    displayArea = { 16, 86, kW - 32, 330 };
    strip       = { 16, 424, kW - 32, 36 };
    killPanel   = { 16, 470, 404, 216 };
    shapePanel  = { 428, 470, 190, 216 };
    timePanel   = { 626, 470, 190, 216 };
    outPanel    = { 824, 470, 200, 216 };

    prevButton.setBounds (250, 22, 28, 30);
    presetBox.setBounds (284, 22, 250, 30);
    nextButton.setBounds (540, 22, 28, 30);
    saveButton.setBounds (576, 22, 62, 30);
    deleteButton.setBounds (644, 22, 74, 30);
    helpButton.setBounds (995, 21, 30, 30);

    display.setBounds (displayArea);

    for (int i = 0; i < 3; ++i) stereoButtons[i].setBounds (strip.getX() + 58 + i * 76, strip.getY() + 5, 72, 26);
    sidechainButton.setBounds (strip.getX() + 58 + 3 * 76 + 14, strip.getY() + 5, 104, 26);
    for (int i = 0; i < sk::kNumQualities; ++i) qualityButtons[i].setBounds (strip.getRight() - 5 * 76 + i * 76, strip.getY() + 5, 72, 26);

    // KILL: threshold big, then DOWN and UP
    layoutKnob (threshold.slider, threshold.name, threshold.value, { killPanel.getX() + 20, killPanel.getY() + 34, 120, 180 }, 120);
    layoutKnob (down.slider, down.name, down.value, { killPanel.getX() + 150, killPanel.getY() + 46, 120, 170 }, 96);
    layoutKnob (up.slider, up.name, up.value, { killPanel.getX() + 274, killPanel.getY() + 46, 120, 170 }, 96);
    // SHAPE and TIME: two each
    layoutKnob (tilt.slider, tilt.name, tilt.value, { shapePanel.getX() + 4, shapePanel.getY() + 62, 90, 150 }, 74);
    layoutKnob (sharpness.slider, sharpness.name, sharpness.value, { shapePanel.getX() + 96, shapePanel.getY() + 62, 90, 150 }, 74);
    layoutKnob (attack.slider, attack.name, attack.value, { timePanel.getX() + 4, timePanel.getY() + 62, 90, 150 }, 74);
    layoutKnob (release.slider, release.name, release.value, { timePanel.getX() + 96, timePanel.getY() + 62, 90, 150 }, 74);
    // OUTPUT: gain and mix, meters, clip
    layoutKnob (gain.slider, gain.name, gain.value, { outPanel.getX() + 6, outPanel.getY() + 32, 94, 110 }, 60);
    layoutKnob (mix.slider, mix.name, mix.value, { outPanel.getX() + 100, outPanel.getY() + 32, 94, 110 }, 60);
    for (int i = 0; i < 3; ++i) clipButtons[i].setBounds (outPanel.getX() + 46 + i * 50, outPanel.getY() + 174, 47, 24);

    staticScale = 0.0f;              // redraw the static layer on the next paint
}

// everything that never moves: the bone backdrop, the lightbox, plates and labels
void SpectraKillUI::renderStatic (float ps)
{
    using namespace juce;
    staticScale = ps;
    staticLayer = Image (Image::RGB, roundToInt (kW * ps), roundToInt (kH * ps), false);
    Graphics g (staticLayer);
    g.addTransform (AffineTransform::scale (ps));
    Random rng (11);

    // grain is made once at 2x and reused (it is the slow part)
    if (! grain.isValid())
        grain = Bone::noise (kW * 2, kH * 2, 9.0f, 0.13f, Colour (60, 40, 20), Colour (255, 250, 235), 5);
    const auto all = Rectangle<float> (0, 0, (float) kW, (float) kH);

    // backdrop: old bone, cracked and splattered
    g.setGradientFill (ColourGradient (Pal::bg0, 520.0f, 300.0f, Pal::bg1, 520.0f + 820.0f, 300.0f, true));
    g.fillAll();
    g.drawImage (grain, all);
    Bone::fibres (g, rng, all, 0.13f);
    for (int i = 0; i < 9; ++i)
        Bone::crack (g, rng, rng.nextFloat() * kW, rng.nextBool() ? 0.0f : (float) kH, 80.0f + rng.nextFloat() * 140.0f,
                     rng.nextBool() ? 1.2f : -1.6f, 1.3f, Pal::crack, 2);
    Bone::splatter (g, rng, 976.0f, 64.0f, 58.0f, 90, Pal::bloodDark);
    Bone::splatter (g, rng, 30.0f, 650.0f, 46.0f, 60, Pal::bloodDark);
    Bone::splatter (g, rng, 560.0f, 455.0f, 22.0f, 25, Pal::blood);

    if (logo.isValid())
    {
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        const float h = 74.0f, w = h * (float) logo.getWidth() / (float) logo.getHeight();
        g.drawImage (logo, Rectangle<float> (6.0f, 2.0f, w, h), RectanglePlacement::stretchToFit);
    }

    // the lightbox behind the film: a bone housing, glowing white from behind
    {
        const auto d = displayArea.toFloat();
        const auto housing = d.expanded (9.0f);
        Path hp; hp.addRectangle (housing);
        DropShadow (Colours::black.withAlpha (0.5f), 16, { 0, 6 }).drawForPath (g, hp);
        g.setColour (Colour (0xffd9cba8));
        g.fillRect (housing);
        {
            Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (housing.toNearestInt());
            g.setOpacity (0.8f);
            g.drawImage (grain, all);
        }
        g.setColour (Colour (0xffeef3f2));
        g.fillRect (d.expanded (3.0f));
    }

    // switch strip labels
    g.setFont (lnf.type (12.0f));
    g.setColour (Pal::inkSoft);
    g.drawText ("MODE", Rectangle<int> (22, strip.getY(), 50, strip.getHeight()), Justification::centredLeft, false);
    g.drawText ("QUALITY", Rectangle<int> (strip.getRight() - 5 * 76 - 90, strip.getY(), 80, strip.getHeight()), Justification::centredRight, false);

    drawPlate (g, killPanel, "KILL", 489, rng);
    drawPlate (g, shapePanel, "SHAPE", 457, rng);
    drawPlate (g, timePanel, "TIME", 312, rng);
    drawPlate (g, outPanel, "OUTPUT", 298, rng);

    const auto m = outPanel.toFloat();
    g.setFont (lnf.type (11.0f));
    g.setColour (Pal::inkSoft);
    g.drawText ("IN", Rectangle<float> (m.getX() + 12, m.getY() + 141, 30, 13), Justification::centredLeft, false);
    g.drawText ("OUT", Rectangle<float> (m.getX() + 12, m.getY() + 153, 30, 13), Justification::centredLeft, false);
    g.drawText ("CLIP", Rectangle<float> (m.getX() + 12, m.getY() + 174, 32, 24), Justification::centredLeft, false);
}

void SpectraKillUI::paint (juce::Graphics& g)
{
    using namespace juce;
    const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! staticLayer.isValid() || ps != staticScale) renderStatic (ps);
    g.drawImage (staticLayer, Rectangle<float> (0, 0, (float) kW, (float) kH), RectanglePlacement::stretchToFit);

    // KILL readout: the deepest cut right now, in a dark slot
    {
        const auto box = killBox.toFloat();
        g.setColour (Pal::slot);
        g.fillRect (box);
        g.setColour (Pal::bloodDark);
        g.drawRect (box, 2.0f);
        g.setFont (lnf.type (13.0f));
        g.setColour (Colour (0xffb9a685));
        g.drawText ("KILL", box.withTrimmedLeft (14).withWidth (60), Justification::centredLeft, false);
        const String v = (killShown < 0.05f ? String ("0.0") : "-" + String (killShown, 1)) + " dB";
        const auto area = box.withTrimmedLeft (60).withTrimmedRight (12).expanded (0.0f, 8.0f).translated (0.0f, 1.0f);
        g.setFont (lnf.osw (32.0f, 700));
        if (killShown > 0.05f)
        {
            g.setColour (Pal::bloodBright.withAlpha (0.16f));                 // cheap glow: a few offset copies
            for (auto o : { Point<float> (-2, 0), Point<float> (2, 0), Point<float> (0, -2), Point<float> (0, 2) })
                g.drawText (v, area + o, Justification::centredRight, false);
        }
        g.setColour (killShown > 0.05f ? Pal::bloodBright : Colour (0xff4a2522));
        g.drawText (v, area, Justification::centredRight, false);
    }

    // output meters: thin blood bars
    const auto m = outPanel.toFloat();
    for (int i = 0; i < 2; ++i)
    {
        const float y = m.getY() + 146.0f + (float) i * 12.0f;
        const float db = i == 0 ? inPeakDb : outPeakDb;
        const auto bar = Rectangle<float> (m.getX() + 46, y, m.getWidth() - 60, 5);
        g.setColour (Pal::ink); g.fillRect (bar);
        const float t = jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setGradientFill (ColourGradient (Pal::bloodDark, bar.getX(), 0, db > -0.2f ? Colour (0xffff3a40) : Pal::bloodBright, bar.getRight(), 0, false));
        g.fillRect (bar.withWidth (bar.getWidth() * t));
    }
}
