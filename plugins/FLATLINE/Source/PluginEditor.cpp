#include "PluginEditor.h"
#include "BinaryData.h"

using namespace flui;

namespace
{
    juce::String minus() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")); }
    juce::String num (float v, int dec, bool plus = false)
    {
        const float a = std::abs (v) < (dec ? 0.05f : 0.5f) ? 0.0f : v;
        return (plus && a > 0 ? "+" : a < 0 ? minus() : juce::String()) + juce::String (std::abs (a), dec);
    }
    // the logo: steel letters with their tops shaved off at one glowing line
    void drawLogo (juce::Graphics& g, FlatLookAndFeel& lnf, float x, float baseline, float size)
    {
        using namespace juce;
        GlyphArrangement ga; ga.addLineOfText (lnf.title (size, size * 0.06f), "FLATLINE", x, baseline);
        Path tp; ga.createPath (tp);
        const auto tb = tp.getBounds();
        const float cut = tb.getY() + tb.getHeight() * 0.15f;
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (Rectangle<float> (tb.getX() - 4, cut, tb.getWidth() + 8, tb.getBottom() - cut + 4).toNearestInt());
            ColourGradient steel (Colour (0xfff4f7f7), 0, tb.getY(), Colour (0xff6d777a), 0, tb.getBottom(), false);
            steel.addColour (0.45, Colour (0xffc3cbcc));
            g.setGradientFill (steel); g.fillPath (tp);
        }
        const float x0 = tb.getX() - 10, x1 = tb.getRight() + 10;
        for (auto [h, a] : { std::pair<float, float> { 14.0f, 0.10f }, { 6.0f, 0.25f }, { 2.4f, 1.0f } })
        {
            ColourGradient cg (green.withAlpha (0.0f), x0, cut, green.withAlpha (0.0f), x1, cut, false);
            cg.addColour (0.08, green.withAlpha (a)); cg.addColour (0.5, (h < 3 ? greenHot : green).withAlpha (a)); cg.addColour (0.92, green.withAlpha (a));
            g.setGradientFill (cg); g.fillRect (x0, cut - h * 0.5f, x1 - x0, h);
        }
    }
}

//==============================================================================
FlatLookAndFeel::FlatLookAndFeel()
{
    oswald = juce::Typeface::createSystemTypefaceFor (BinaryData::OswaldSemiBold_ttf, BinaryData::OswaldSemiBold_ttfSize);
    mont = juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratMedium_ttf, BinaryData::MontserratMedium_ttfSize);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff0e1314));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff1d3a2a));
    setColour (juce::PopupMenu::highlightedTextColourId, green);
    setColour (juce::ComboBox::textColourId, text);
}

juce::Font FlatLookAndFeel::title (float size, float tracking) const { juce::Font f (juce::FontOptions (oswald).withHeight (size)); return f.withExtraKerningFactor (tracking / size); }
juce::Font FlatLookAndFeel::body (float size, float tracking) const { juce::Font f (juce::FontOptions (mont).withHeight (size)); return f.withExtraKerningFactor (tracking / size); }

void FlatLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& sl)
{
    using namespace juce;
    const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto c = b.getCentre();
    const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f - 14.0f, ring = r + 8.0f;
    const bool on = sl.isEnabled();
    const bool bip = sl.getProperties()["bipolar"];
    const float ang = a0 + pos * (a1 - a0);
    Path track; track.addCentredArc (c.x, c.y, ring, ring, 0, a0, a1, true);
    g.setColour (Colour (0xff0a0d0e)); g.strokePath (track, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colour (0xff222b2d)); g.strokePath (track, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
    const float from = bip ? (a0 + a1) * 0.5f : a0;
    const Colour col = on ? green : Colour (0xff3f5048);
    if (std::abs (ang - from) > 0.01f)
    {
        Path arc; arc.addCentredArc (c.x, c.y, ring, ring, 0, jmin (from, ang), jmax (from, ang), true);
        if (on) { g.setColour (col.withAlpha (0.25f)); g.strokePath (arc, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded)); }
        g.setColour (col); g.strokePath (arc, PathStrokeType (2.6f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    const auto body = Rectangle<float> (r * 2, r * 2).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.55f)); g.fillEllipse (body.translated (0, 3).expanded (1));
    g.setGradientFill (ColourGradient (Colour (0xff5c686b), c.x - r * 0.4f, c.y - r * 0.8f, Colour (0xff0d1112), c.x + r * 0.5f, c.y + r, true));
    g.fillEllipse (body);
    g.setColour (Colours::white.withAlpha (0.13f)); g.drawEllipse (body.reduced (0.5f), 1.0f);
    const auto face = body.reduced (r * 0.26f);
    g.setGradientFill (ColourGradient (Colour (0xff2c3537), face.getX(), face.getY(), Colour (0xff111617), face.getRight(), face.getBottom(), false));
    g.fillEllipse (face);
    g.setColour (Colours::black.withAlpha (0.5f)); g.drawEllipse (face, 1.0f);
    const auto p0 = c.getPointOnCircumference (r * 0.3f, ang), p1 = c.getPointOnCircumference (r * 0.85f, ang);
    if (on) { g.setColour (col.withAlpha (0.3f)); g.drawLine ({ p0, p1 }, 6.0f); }
    g.setColour (on ? col : Colour (0xff55625d)); g.drawLine ({ p0, p1 }, 2.4f);
}

void FlatLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    using namespace juce;
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const String kind = b.getProperties()["kind"].toString();
    if (kind == "led")
    {
        const auto c = r.getCentre(); const auto d = Rectangle<float> (11, 11).withCentre (c);
        if (on) { g.setColour (green.withAlpha (0.35f)); g.fillEllipse (d.expanded (4)); }
        g.setColour (on ? green : Colour (0xff1d2a24)); g.fillEllipse (d);
        g.setColour (on ? greenHot.withAlpha (0.6f) : Colour (0xff2c3b34)); g.drawEllipse (d, 1.0f);
        return;
    }
    if (kind == "round")
    {
        const auto d = Rectangle<float> (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight())).withCentre (r.getCentre());
        g.setGradientFill (ColourGradient (Colour (down ? 0xff1d2427 : 0xff2a3336), 0, d.getY(), Colour (0xff121718), 0, d.getBottom(), false));
        g.fillEllipse (d);
        g.setColour (on ? green : Colour (over ? 0xff52656a : 0xff39474a)); g.drawEllipse (d.reduced (0.5f), on ? 1.6f : 1.0f);
        if (on) { g.setColour (green.withAlpha (0.25f)); g.drawEllipse (d.expanded (1.5f), 3.0f); }
        return;
    }
    const bool seg = kind == "seg";
    const Colour accent = b.getProperties()["red"] ? red : green;
    if (seg && ! on) { if (over) { g.setColour (Colours::white.withAlpha (0.04f)); g.fillRoundedRectangle (r, 5); } return; }
    if (on)
    {
        g.setGradientFill (ColourGradient (accent.withMultipliedBrightness (0.35f).withMultipliedSaturation (0.7f), 0, r.getY(), accent.withMultipliedBrightness (0.18f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 5);
        g.setColour (accent.withAlpha (0.55f)); g.drawRoundedRectangle (r, 5, 1.0f);
        if (! seg) { g.setColour (accent.withAlpha (0.15f)); g.drawRoundedRectangle (r.expanded (2), 7, 3.0f); }
    }
    else
    {
        g.setColour (Colour (down ? 0xff0d1214 : over ? 0xff182023 : 0xff11171a)); g.fillRoundedRectangle (r, 6);
        g.setColour (line.brighter (over ? 0.3f : 0.0f)); g.drawRoundedRectangle (r, 6, 1.0f);
    }
}

void FlatLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const juce::String kind = b.getProperties()["kind"].toString();
    if (kind == "led") return;
    const bool on = b.getToggleState();
    const float size = b.getProperties().getWithDefault ("size", 11.0f);
    g.setFont (kind == "round" ? title (17.0f) : title (size, size * 0.18f));
    const juce::Colour accent = b.getProperties()["red"] ? red : green;
    g.setColour (! b.isEnabled() ? faint : on ? accent : b.getProperties()["ready"] ? green : kind == "round" ? text : dim.brighter (0.25f));
    g.drawText (b.getButtonText(), b.getLocalBounds().translated (0, kind == "round" ? 1 : 0), juce::Justification::centred, false);
}

void FlatLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (juce::Colour (0xff0a0d0e)); g.fillRoundedRectangle (r, 6);
    g.setColour (line); g.drawRoundedRectangle (r, 6, 1.0f);
    juce::Path p; const float cx = (float) w - 16, cy = (float) h * 0.5f;
    p.addTriangle (cx - 5, cy - 3, cx + 5, cy - 3, cx, cy + 4);
    g.setColour (green); g.fillPath (p);
}
juce::Font FlatLookAndFeel::getComboBoxFont (juce::ComboBox&) { return body (13.0f); }
void FlatLookAndFeel::positionComboBoxText (juce::ComboBox& b, juce::Label& l) { l.setBounds (8, 0, b.getWidth() - 30, b.getHeight()); l.setFont (body (13.0f)); }
juce::Font FlatLookAndFeel::getPopupMenuFont() { return body (13.5f); }
void FlatLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff0e1314));
    g.setColour (line); g.drawRect (0, 0, w, h);
}

juce::Rectangle<int> FlatLookAndFeel::getTooltipBounds (const juce::String& t, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::AttributedString s; s.append (t, body (13.0f), text); s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl; tl.createLayout (s, 300.0f);
    return juce::Rectangle<int> (pos.x + 12, pos.y + 18, (int) std::ceil (tl.getWidth()) + 28, (int) std::ceil (tl.getHeight()) + 20).constrainedWithin (parent);
}
void FlatLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& t, int w, int h)
{
    g.setColour (juce::Colour (0xf00a0e0f)); g.fillRoundedRectangle (0, 0, (float) w, (float) h, 5);
    g.setColour (green.withAlpha (0.5f)); g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 5, 1);
    juce::AttributedString s; s.append (t, body (13.0f), text); s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl; tl.createLayout (s, (float) w - 28);
    tl.draw (g, { 14, 10, (float) w - 28, (float) h - 20 });
}

//============================================================================== GAIN tank
GainTank::GainTank (juce::RangedAudioParameter& p, FlatLookAndFeel& l)
    : prm (p), lnf (l), att (p, [this] (float v) { value = v; repaint(); })
{
    att.sendInitialUpdate();
    setRepaintsOnMouseActivity (true);
    setTooltip ("GAIN: how hard the signal is pushed into FLATLINE. Drag up or down (Shift for fine), click the arrows for 0.1 dB steps, double-click for 0 dB. TARGET's LEARN sets it for you.");
}
int GainTank::zone (const juce::MouseEvent& e) const
{
    const auto c = getLocalBounds().toFloat().getCentre(); const float r = (float) getWidth() * 0.5f - 14;
    const auto p = e.position;
    if (std::abs (p.x - c.x) < 18 && std::abs (p.y - (c.y - r * 0.55f)) < 12) return 1;
    if (std::abs (p.x - c.x) < 18 && std::abs (p.y - (c.y + r * 0.55f)) < 12) return -1;
    return 0;
}
void GainTank::nudge (float db) { att.setValueAsCompleteGesture (juce::jlimit (0.0f, 20.0f, std::round ((value + db) * 10.0f) / 10.0f)); }
void GainTank::mouseDown (const juce::MouseEvent& e)
{
    const int z = zone (e);
    if (z != 0) { nudge (0.1f * (float) z); return; }
    dragging = true; dragStart = value; att.beginGesture();
}
void GainTank::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float perPx = e.mods.isShiftDown() ? 0.01f : 0.08f;
    att.setValueAsPartOfGesture (juce::jlimit (0.0f, 20.0f, std::round ((dragStart - (float) e.getDistanceFromDragStartY() * perPx) * 10.0f) / 10.0f));
}
void GainTank::mouseUp (const juce::MouseEvent&) { if (dragging) att.endGesture(); dragging = false; }
void GainTank::mouseDoubleClick (const juce::MouseEvent& e) { if (zone (e) == 0) att.setValueAsCompleteGesture (0.0f); }
void GainTank::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) { nudge (w.deltaY > 0 ? 0.5f : -0.5f); }

void GainTank::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto c = getLocalBounds().toFloat().getCentre();
    const float r = (float) getWidth() * 0.5f - 14;
    const float frac = jlimit (0.0f, 1.0f, value / 20.0f);
    const auto disc = Rectangle<float> (r * 2, r * 2).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.55f)); g.fillEllipse (disc.expanded (10).translated (0, 4));
    g.setGradientFill (ColourGradient (Colour (0xff5c686b), c.x - r * 0.4f, c.y - r, Colour (0xff0d1112), c.x + r * 0.5f, c.y + r, true));
    g.fillEllipse (disc.expanded (9));
    g.setColour (Colours::white.withAlpha (0.14f)); g.drawEllipse (disc.expanded (9), 1.0f);
    // the ring: glows with the level
    if (frac > 0.01f) { g.setColour (green.withAlpha (0.12f + 0.25f * frac)); g.drawEllipse (disc.expanded (2.5f), 6.0f); }
    g.setColour (frac > 0.01f ? green.withAlpha (0.4f + 0.6f * frac) : Colour (0xff2a3436)); g.drawEllipse (disc.expanded (1.5f), 2.5f);
    g.setColour (Colour (0xff050807)); g.fillEllipse (disc);
    {
        Graphics::ScopedSaveState ss (g);
        Path clip; clip.addEllipse (disc); g.reduceClipRegion (clip);
        const float top = disc.getBottom() - disc.getHeight() * frac;
        g.setGradientFill (ColourGradient (Colour (0xff1f6b44), 0, top, Colour (0xff0b2a1b), 0, disc.getBottom(), false));
        g.fillRect (disc.getX(), top, disc.getWidth(), disc.getBottom() - top);
        for (float y = disc.getY(); y < disc.getBottom(); y += 7.5f) { g.setColour (Colours::black.withAlpha (0.22f)); g.fillRect (disc.getX(), y, disc.getWidth(), 1.0f); }
        if (frac > 0.005f)
        {
            g.setColour (green.withAlpha (0.25f)); g.fillRect (disc.getX(), top - 3.5f, disc.getWidth(), 7.0f);
            g.setColour (green); g.fillRect (disc.getX(), top - 1.2f, disc.getWidth(), 2.4f);
        }
    }
    auto chevron = [&] (float y, bool up, bool hot)
    {
        Path p; p.startNewSubPath (c.x - 8, y + (up ? 4 : -4)); p.lineTo (c.x, y + (up ? -3 : 3)); p.lineTo (c.x + 8, y + (up ? 4 : -4));
        g.setColour ((hot ? greenHot : Colour (0xffc9d4d4)).withAlpha (hot ? 1.0f : 0.75f));
        g.strokePath (p, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    };
    chevron (c.y - r * 0.55f, true, hover == 1);
    chevron (c.y + r * 0.55f, false, hover == -1);
    // the value, with a dark edge so it reads over the surface line
    GlyphArrangement ga; ga.addJustifiedText (lnf.title (r > 50 ? 22.0f : 16.0f, 0.5f), num (value, 1, true) + " dB", c.x - r, c.y + 8, r * 2, Justification::horizontallyCentred);
    Path tp; ga.createPath (tp);
    g.setColour (Colour (0xff06120b)); g.strokePath (tp, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colour (0xfff2f6f6)); g.fillPath (tp);
}

//============================================================================== monitor
Monitor::Monitor (FlatlineProcessor& p, FlatLookAndFeel& l) : proc (p), lnf (l)
{
    for (auto* b : { &waveBtn, &specBtn })
    {
        b->getProperties().set ("kind", "seg"); b->getProperties().set ("size", 10.0f);
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    waveBtn.setTooltip ("WAVE: the output over the last 4 seconds. Red from the top: how far the limiter turned it down. Amber marks on the ceiling: clipped. Mint from the bottom: how far UPWARD lifted it.");
    specBtn.setTooltip ("SPECTRUM: the sound going into the clipper and limiter (grey) and coming out (green). Red bars: where they work.");
    waveBtn.onClick = [this] { proc.apvts.state.setProperty ("view", 0, nullptr); repaint(); };
    specBtn.onClick = [this] { proc.apvts.state.setProperty ("view", 1, nullptr); repaint(); };
    fftBuf.assign (8192, 0.0f); win.resize (4096);
    for (int i = 0; i < 4096; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / 4095.0f);
    specPre.assign (200, -120.0f); specPost.assign (200, -120.0f);
    setTooltip ("Drag the ceiling line up or down to set CEILING.");
}
bool Monitor::spectrum() const { return (int) proc.apvts.state.getProperty ("view", 0) == 1; }
void Monitor::resized()
{
    waveBtn.setBounds (getWidth() - 186, 8, 70, 22);
    specBtn.setBounds (getWidth() - 116, 8, 104, 22);
}

void Monitor::update()
{
    waveBtn.setToggleState (! spectrum(), juce::dontSendNotification);
    specBtn.setToggleState (spectrum(), juce::dontSendNotification);
    auto& e = proc.engine();
    const int w = e.colWrite.load (std::memory_order_acquire);
    const int n = juce::jmin (getWidth(), fl::Engine::kCols - 8, w);
    cols.resize ((size_t) n);
    for (int i = 0; i < n; ++i) cols[(size_t) i] = e.cols[(size_t) ((w - n + i) % fl::Engine::kCols)];
    if (spectrum())
    {
        const int sw = e.scopeWrite.load (std::memory_order_acquire);
        const double fs = proc.rate.load();
        auto analyse = [&] (const std::array<float, fl::Engine::kScope>& src, std::vector<float>& out)
        {
            std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
            for (int i = 0; i < 4096; ++i) fftBuf[(size_t) i] = src[(size_t) ((sw - 4096 + i + fl::Engine::kScope * 4) % fl::Engine::kScope)] * win[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (fftBuf.data());
            for (int b = 0; b < 200; ++b)
            {
                const double f0 = 20.0 * std::pow (1000.0, b / 200.0), f1 = 20.0 * std::pow (1000.0, (b + 1) / 200.0);
                const double b0 = f0 / fs * 4096, b1 = f1 / fs * 4096;
                float m = 0;
                if (b1 - b0 < 1.0)
                {
                    // narrower than one FFT bin: read between bins, so the low end is a curve, not steps
                    const double bc = 0.5 * (b0 + b1); const int k = juce::jlimit (1, 2046, (int) bc); const float t = (float) (bc - k);
                    m = fftBuf[(size_t) k] * (1 - t) + fftBuf[(size_t) k + 1] * t;
                }
                else for (int k = juce::jmax (1, (int) b0); k < juce::jmin ((int) b1 + 1, 2048); ++k) m = juce::jmax (m, fftBuf[(size_t) k]);
                const float db = juce::Decibels::gainToDecibels (m / 1024.0f, -120.0f) + 3.0f * (float) std::log2 (juce::jmax (1.0, f0 / 1000.0));   // a +3 dB/oct tilt, so a mix reads level
                out[(size_t) b] = db > out[(size_t) b] ? 0.5f * out[(size_t) b] + 0.5f * db : juce::jmax (db, out[(size_t) b] - 1.5f);
            }
        };
        analyse (e.scopePre, specPre);
        analyse (e.scopePost, specPost);
    }
    repaint();
}

void Monitor::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (! spectrum() && std::abs (e.position.y - ceilY) < 8 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}
void Monitor::mouseDown (const juce::MouseEvent& e)
{
    if (spectrum() || std::abs (e.position.y - ceilY) > 10) return;
    dragCeil = true; dragStartDb = proc.apvts.getRawParameterValue ("ceiling")->load();
    if (auto* p = proc.apvts.getParameter ("ceiling")) p->beginChangeGesture();
}
void Monitor::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragCeil) return;
    if (auto* p = proc.apvts.getParameter ("ceiling"))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-3.0f, 0.0f, std::round ((dragStartDb - (float) e.getDistanceFromDragStartY() * 0.02f) * 10.0f) / 10.0f)));
}
void Monitor::mouseUp (const juce::MouseEvent&) { if (dragCeil) if (auto* p = proc.apvts.getParameter ("ceiling")) p->endChangeGesture(); dragCeil = false; }

void Monitor::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = getLocalBounds().toFloat();
    g.setColour (screen); g.fillRoundedRectangle (r, 8);
    {
        Graphics::ScopedSaveState ss (g);
        Path clip; clip.addRoundedRectangle (r, 8); g.reduceClipRegion (clip);
        if (spectrum()) paintSpectrum (g); else paintWave (g);
        // scanlines
        for (float y = 0; y < r.getHeight(); y += 3) { g.setColour (Colours::black.withAlpha (0.22f)); g.fillRect (0.0f, y, r.getWidth(), 1.0f); }
        g.setGradientFill (ColourGradient (Colours::transparentBlack, r.getCentreX(), r.getCentreY(), Colours::black.withAlpha (0.35f), 0, 0, true));
        g.fillRect (r);
    }
    // a solid well behind WAVE / SPECTRUM, so no line runs under the buttons
    {
        const auto well = Rectangle<float> ((float) waveBtn.getX() - 4, (float) waveBtn.getY() - 3, (float) (specBtn.getRight() - waveBtn.getX()) + 8, (float) waveBtn.getHeight() + 6);
        g.setColour (screen); g.fillRoundedRectangle (well, 7);
        g.setColour (line); g.drawRoundedRectangle (well.reduced (0.5f), 7, 1.0f);
    }
    g.setColour (line); g.drawRoundedRectangle (r.reduced (0.5f), 8, 1.0f);
}

void Monitor::paintWave (juce::Graphics& g)
{
    using namespace juce;
    const float W = (float) getWidth(), H = (float) getHeight();
    const float grTop = 22.0f, grH = 34.0f;          // the gain reduction lane (0..12 dB)
    ceilY = grTop + grH + 12.0f;
    const float liftH = 30.0f, bottom = H - liftH, cy = (ceilY + bottom) * 0.5f, amp = cy - ceilY;
    const float ceilLin = Decibels::decibelsToGain (proc.apvts.getRawParameterValue ("ceiling")->load());
    // grid
    g.setColour (Colour (0xff12201a));
    for (float y = ceilY; y < bottom; y += 38) g.fillRect (0.0f, y, W, 1.0f);
    for (float x = 0; x < W; x += W / 8) g.fillRect (x, grTop, 1.0f, bottom - grTop);
    g.setColour (Colour (0xff18302a)); g.fillRect (0.0f, cy, W, 1.0f);
    g.setColour (green.withAlpha (0.3f));
    for (float x = 0; x < W; x += 9) g.fillRect (x, 2 * cy - ceilY, 4.0f, 1.0f);
    const int n = (int) cols.size();
    const float x0 = W - (float) n;
    // the output trace
    if (n > 1)
    {
        Path hi, lo, band;
        for (int i = 0; i < n; ++i)
        {
            const auto& c = cols[(size_t) i];
            const float x = x0 + (float) i;
            const float yh = cy - jlimit (-1.2f, 1.2f, c.hi / ceilLin) * amp, yl = cy - jlimit (-1.2f, 1.2f, c.lo / ceilLin) * amp;
            if (i == 0) { hi.startNewSubPath (x, yh); lo.startNewSubPath (x, yl); } else { hi.lineTo (x, yh); lo.lineTo (x, yl); }
        }
        band = hi; for (int i = n - 1; i >= 0; --i) { const auto& c = cols[(size_t) i]; band.lineTo (x0 + (float) i, cy - jlimit (-1.2f, 1.2f, c.lo / ceilLin) * amp); } band.closeSubPath();
        g.setColour (green.withAlpha (0.16f)); g.fillPath (band);
        for (auto* p : { &hi, &lo })
        {
            g.setColour (green.withAlpha (0.25f)); g.strokePath (*p, PathStrokeType (3.5f));
            g.setColour (green.withAlpha (0.9f)); g.strokePath (*p, PathStrokeType (1.2f));
        }
    }
    // gain reduction lane, hanging from the top
    g.setColour (screen); g.fillRect (0.0f, 0.0f, W, grTop);
    g.setColour (Colour (0xff2a1416)); g.fillRect (0.0f, grTop, W, 1.0f);
    if (n > 1)
    {
        Path grP; grP.startNewSubPath (x0, grTop);
        for (int i = 0; i < n; ++i) grP.lineTo (x0 + (float) i, grTop + jmin (1.0f, cols[(size_t) i].gr / 12.0f) * grH);
        grP.lineTo (W, grTop); grP.closeSubPath();
        g.setColour (red.withAlpha (0.22f)); g.fillPath (grP);
        Path grL; for (int i = 0; i < n; ++i) { const float y = grTop + jmin (1.0f, cols[(size_t) i].gr / 12.0f) * grH; if (i == 0) grL.startNewSubPath (x0, y); else grL.lineTo (x0 + (float) i, y); }
        g.setColour (red); g.strokePath (grL, PathStrokeType (1.5f));
        // clip marks on the ceiling line
        for (int i = 0; i < n; ++i) if (cols[(size_t) i].clip > 0.05f) { g.setColour (amber.withAlpha (jmin (1.0f, 0.4f + cols[(size_t) i].clip))); g.fillRect (x0 + (float) i - 1, ceilY - 2.5f, 3.0f, 5.0f); }
        // lift lane at the bottom
        g.setColour (screen); g.fillRect (0.0f, bottom, W, liftH);
        g.setColour (Colour (0xff14301f)); g.fillRect (0.0f, bottom, W, 1.0f);
        Path lf; lf.startNewSubPath (x0, H);
        for (int i = 0; i < n; ++i) lf.lineTo (x0 + (float) i, H - 2 - jmin (1.0f, cols[(size_t) i].lift / 8.0f) * (liftH - 6));
        lf.lineTo (W, H); lf.closeSubPath();
        g.setColour (mint.withAlpha (0.16f)); g.fillPath (lf);
        Path ll; for (int i = 0; i < n; ++i) { const float y = H - 2 - jmin (1.0f, cols[(size_t) i].lift / 8.0f) * (liftH - 6); if (i == 0) ll.startNewSubPath (x0, y); else ll.lineTo (x0 + (float) i, y); }
        g.setColour (mint); g.strokePath (ll, PathStrokeType (1.3f));
    }
    // the ceiling
    g.setColour (green.withAlpha (0.12f)); g.fillRect (0.0f, ceilY - 6, W, 12.0f);
    g.setColour (green.withAlpha (0.35f)); g.fillRect (0.0f, ceilY - 2.5f, W, 5.0f);
    g.setColour (green); g.fillRect (0.0f, ceilY - 1.2f, W, 2.4f);
    const auto tab = Rectangle<float> (W - 128, ceilY + 7, 116, 22);
    g.setColour (Colour (0xff0f2a1c)); g.fillRoundedRectangle (tab, 4);
    g.setColour (dragCeil ? greenHot : green); g.drawRoundedRectangle (tab, 4, 1.0f);
    g.setFont (lnf.title (12.5f, 1.0f)); g.setColour (greenHot);
    g.drawText ("CEILING " + num (proc.apvts.getRawParameterValue ("ceiling")->load(), 1) + (proc.apvts.getRawParameterValue ("truePeak")->load() > 0.5f ? " dBTP" : " dB"), tab, Justification::centred, false);
    // labels
    g.setFont (lnf.body (9.0f, 2.0f));
    g.setColour (red); g.drawText ("LIMITER " + String::fromUTF8 ("\xc2\xb7") + " GAIN REDUCTION", Rectangle<float> (12, 4, 260, 14), Justification::centredLeft, false);
    g.setColour (amber); g.drawText (String::fromUTF8 ("\xe2\x96\xac") + " CLIPPED", Rectangle<float> (250, 4, 120, 14), Justification::centredLeft, false);
    g.setColour (dim); g.drawText ("0 / " + minus() + "6 / " + minus() + "12 dB", Rectangle<float> (W - 400, 4, 190, 14), Justification::centredRight, false);
    g.setColour (mint); g.drawText ("UPWARD " + String::fromUTF8 ("\xc2\xb7") + " LIFT", Rectangle<float> (12, bottom + 3, 200, 14), Justification::centredLeft, false);
    if (n < 4) { g.setColour (dim); g.setFont (lnf.body (11.0f, 3.0f)); g.drawText ("PLAY SOMETHING THROUGH FLATLINE", getLocalBounds(), Justification::centred, false); }
}

void Monitor::paintSpectrum (juce::Graphics& g)
{
    using namespace juce;
    const float W = (float) getWidth(), H = (float) getHeight();
    const float l = 20, rgt = W - 30, top = 40, bot = H - 40;
    auto fx = [&] (double f) { return l + (float) (std::log (f / 20.0) / std::log (1000.0)) * (rgt - l); };
    auto dy = [&] (float d) { return top + (-d / 72.0f) * (bot - top); };
    g.setFont (lnf.body (9.0f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (Colour (0xff12201a)); g.fillRect (fx (f), top - 10, 1.0f, bot - top + 10);
        g.setColour (Colour (0xff5d6a6c)); g.drawText (f >= 1000 ? String ((int) (f / 1000)) + "k" : String ((int) f), Rectangle<float> (fx (f) - 20, bot + 8, 40, 12), Justification::centred, false);
    }
    for (float d = 0; d >= -60; d -= 12)
    {
        g.setColour (Colour (0xff12201a)); g.fillRect (l, dy (d), rgt - l, 1.0f);
        g.setColour (Colour (0xff4b5759)); g.drawText (d == 0 ? "0" : minus() + String ((int) -d), Rectangle<float> (rgt + 2, dy (d) - 6, 26, 12), Justification::centredLeft, false);
    }
    auto curve = [&] (const std::vector<float>& v)
    {
        Path p;
        for (int b = 0; b < 200; ++b) { const float x = l + (rgt - l) * ((float) b + 0.5f) / 200.0f, y = jlimit (top - 8, bot, dy (v[(size_t) b])); if (b == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
        return p;
    };
    const auto pre = curve (specPre), post = curve (specPost);
    Path fill = post; fill.lineTo (rgt, bot); fill.lineTo (l, bot); fill.closeSubPath();
    g.setGradientFill (ColourGradient (green.withAlpha (0.28f), 0, top, green.withAlpha (0.02f), 0, bot, false)); g.fillPath (fill);
    g.setColour (Colour (0xff8b9698).withAlpha (0.75f)); g.strokePath (pre, PathStrokeType (1.3f));
    g.setColour (green.withAlpha (0.3f)); g.strokePath (post, PathStrokeType (4.0f));
    g.setColour (green); g.strokePath (post, PathStrokeType (1.6f));
    // where the clipper and limiter work: the drop from before to after, per band
    for (int b = 0; b < 200; b += 4)
    {
        float d = 0; for (int k = b; k < b + 4; ++k) d = jmax (d, specPre[(size_t) k] - specPost[(size_t) k]);
        if (d < 0.3f || specPre[(size_t) b] < -80) continue;
        const float x = l + (rgt - l) * ((float) b + 2.0f) / 200.0f, h = jmin (40.0f, d * 6.0f);
        g.setColour (red.withAlpha (0.8f)); g.fillRect (x - 1.6f, bot - h, 3.2f, h);
    }
    g.setFont (lnf.body (9.0f, 2.0f));
    g.setColour (Colour (0xff8b9698)); g.fillRect (16.0f, 14.0f, 14.0f, 2.5f);
    g.setColour (dim); g.drawText ("BEFORE", Rectangle<float> (36, 9, 80, 12), Justification::centredLeft, false);
    g.setColour (green); g.fillRect (106.0f, 14.0f, 14.0f, 2.5f);
    g.setColour (dim); g.drawText ("AFTER", Rectangle<float> (126, 9, 80, 12), Justification::centredLeft, false);
    g.setColour (red); g.fillRect (186.0f, 10.0f, 4.0f, 8.0f);
    g.setColour (dim); g.drawText ("WHERE IT WORKS", Rectangle<float> (196, 9, 160, 12), Justification::centredLeft, false);
}

//============================================================================== content
FlatContent::FlatContent (FlatlineProcessor& p, FlatLookAndFeel& l)
    : proc (p), lnf (l), monitor (p, l), tank (*p.apvts.getParameter ("gain"), l)
{
    setSize (kW, kH);
    addAndMakeVisible (monitor);
    addAndMakeVisible (tank);

    struct Def { Knob* k; const char* id; const char* label; int x, y; const char* tip; bool bip; };
    const Def defs[] = {
        { &ceiling, "ceiling", "CEILING", 266, 584, "CEILING: the highest the output may go. By default FLATLINE limits sample peaks (louder); switch to true peak in ADVANCED, with -1.0, when the track goes through MP3 or streaming encoders. You can also drag the line on the display.", false },
        { &release, "release", "RELEASE", 360, 584, "RELEASE: how fast the limiter lets go, from 1 ms to 10 s. With AUTO on (the default) STYLE picks a fast and a slow release and blends them with the music; turn AUTO off to set it yourself.", false },
        { &link, "link", "LINK", 454, 584, "LINK: how much the two sides are limited together. 100% keeps the stereo image still; lower is a little louder but can shift the image on hard peaks.", false },
        { &transient, "transient", "TRANSIENTS", 552, 584, "TRANSIENTS: turns up the attack of every hit before the limiter, so drums keep their snap even when you push hard. Costs a little loudness: the limiter has more peak to catch.", false },
        { &upAmount, "upAmount", "AMOUNT", 656, 584, "UPWARD AMOUNT: the most the quiet parts are lifted. The peaks aren't touched, so the song gets denser and louder without losing punch.", false },
        { &upBelow, "upBelow", "BELOW", 746, 584, "BELOW: how far under the recent peaks a sound has to be to count as quiet. Lower lifts more of the music.", false },
        { &clipDrive, "clipDrive", "DRIVE", 856, 584, "CLIPPER DRIVE: how hard the signal hits the clipper (4x oversampled) before the limiter. It shaves the very tips of the transients, so the limiter has less to do: louder, with a crisper edge.", false },
    };
    for (const auto& d : defs)
    {
        auto& k = *d.k; k.id = d.id; k.label = d.label; k.c = { d.x, d.y };
        k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        k.s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        k.s.setTooltip (d.tip);
        k.a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, d.id, k.s);
        if (auto* prm = proc.apvts.getParameter (d.id)) k.s.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        k.s.onValueChange = [this] { repaint (200, 600, 720, 70); };
        addAndMakeVisible (k.s);
    }

    // STYLE
    const char* names[4] = { "TRANSPARENT", "PUNCH", "DENSE", "SAFE" };
    const char* tips[4] = { "TRANSPARENT: 4 ms lookahead and a release that follows the music. The cleanest; the default.",
                            "PUNCH: a short lookahead lets the front of each hit through, and a fast release brings it back. Livelier, a little louder.",
                            "DENSE: quick in, quick out. The loudest and thickest; listen for distortion on bass.",
                            "SAFE: long lookahead and a slow release. The least distortion; gentle on acoustic and dynamic music." };
    auto* styleP = proc.apvts.getParameter ("style");
    for (int i = 0; i < 4; ++i)
    {
        auto& b = styleBtn[i]; b.setButtonText (names[i]); b.getProperties().set ("kind", "seg"); b.getProperties().set ("size", 11.0f);
        b.setTooltip (tips[i]);
        b.onClick = [styleP, i] { styleP->setValueNotifyingHost (styleP->convertTo0to1 ((float) i)); };
        addAndMakeVisible (b);
    }
    atts.push_back (std::make_unique<juce::ParameterAttachment> (*styleP, [this] (float v) { for (int i = 0; i < 4; ++i) styleBtn[i].setToggleState (juce::roundToInt (v) == i, juce::dontSendNotification); }));
    atts.back()->sendInitialUpdate();

    auto toggle = [&] (juce::TextButton& b, const char* kind, float size, const char* tip)
    {
        b.getProperties().set ("kind", kind); b.getProperties().set ("size", size);
        b.setClickingTogglesState (true); b.setTooltip (tip); addAndMakeVisible (b);
    };
    toggle (autoBtn, "", 9.0f, "AUTO RELEASE: STYLE picks the release and blends a fast and a slow one with the music. Off: RELEASE sets it.");
    toggle (upLed, "led", 9.0f, "UPWARD on or off: lifts the quiet parts before the clipper and limiter.");
    toggle (clipLed, "led", 9.0f, "CLIPPER on or off: a 4x oversampled clipper before the limiter.");
    toggle (bypassBtn, "", 10.5f, "BYPASS: the untouched sound at the same loudness, so you hear what FLATLINE does, not just that it is louder.");
    toggle (deltaBtn, "", 12.0f, "DELTA: hear only what the clipper and limiter take away. If it sounds like the music, they are working too hard.");
    deltaBtn.getProperties().set ("red", true);
    autoAtt = std::make_unique<juce::ButtonParameterAttachment> (*proc.apvts.getParameter ("autoRelease"), autoBtn);
    upAtt = std::make_unique<juce::ButtonParameterAttachment> (*proc.apvts.getParameter ("upOn"), upLed);
    clipAtt = std::make_unique<juce::ButtonParameterAttachment> (*proc.apvts.getParameter ("clipOn"), clipLed);
    bypassAtt = std::make_unique<juce::ButtonParameterAttachment> (*proc.apvts.getParameter ("bypass"), bypassBtn);
    deltaAtt = std::make_unique<juce::ButtonParameterAttachment> (*proc.apvts.getParameter ("delta"), deltaBtn);

    // SOFT / HARD
    auto* hardP = proc.apvts.getParameter ("clipHard");
    for (auto* b : { &softBtn, &hardBtn }) { b->getProperties().set ("kind", "seg"); b->getProperties().set ("size", 9.0f); addAndMakeVisible (*b); }
    softBtn.setTooltip ("SOFT: rounds the tips off: smooth, the cleaner choice."); hardBtn.setTooltip ("HARD: cuts them flat: crisper and more aggressive.");
    softBtn.onClick = [hardP] { hardP->setValueNotifyingHost (0.0f); };
    hardBtn.onClick = [hardP] { hardP->setValueNotifyingHost (1.0f); };
    atts.push_back (std::make_unique<juce::ParameterAttachment> (*hardP, [this] (float v) { softBtn.setToggleState (v < 0.5f, juce::dontSendNotification); hardBtn.setToggleState (v >= 0.5f, juce::dontSendNotification); }));
    atts.back()->sendInitialUpdate();
    for (const char* id : { "upOn", "clipOn", "autoRelease", "ceiling", "target", "gain" })
        atts.push_back (std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (id), [this] (float) { repaint(); }));

    // ADVANCED: a menu
    advBtn.getProperties().set ("size", 12.0f);
    advBtn.setTooltip ("ADVANCED: true-peak detection, lookahead and dither.");
    advBtn.onClick = [this]
    {
        juce::PopupMenu m;
        m.setLookAndFeel (&lnf);
        const bool tpOn = proc.apvts.getRawParameterValue ("truePeak")->load() > 0.5f;
        const int dith = juce::roundToInt (proc.apvts.getRawParameterValue ("dither")->load());
        m.addSectionHeader ("TRUE PEAK");
        m.addItem (2, "Sample peak (louder; the default)", true, ! tpOn);
        m.addItem (1, "True peak (8x: safest for MP3 / streaming encoders)", true, tpOn);
        const int look = juce::roundToInt (proc.apvts.getRawParameterValue ("lookahead")->load());
        const char* lookNames[8] = { "Auto (set by STYLE)", "0.5 ms", "1 ms", "1.5 ms", "2 ms", "3 ms", "4 ms", "5 ms" };
        m.addSectionHeader ("LOOKAHEAD (shorter: punchier, longer: cleaner)");
        for (int i = 0; i < 8; ++i) m.addItem (20 + i, lookNames[i], true, look == i);
        m.addSectionHeader ("DITHER (last plugin before a fixed-point export)");
        m.addItem (10, "Off", true, dith == 0);
        m.addItem (11, "24-bit", true, dith == 1);
        m.addItem (12, "16-bit", true, dith == 2);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (advBtn), [this] (int r)
        {
            auto set = [this] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
            if (r == 1) set ("truePeak", 1); else if (r == 2) set ("truePeak", 0);
            else if (r >= 20) set ("lookahead", (float) (r - 20));
            else if (r >= 10) set ("dither", (float) (r - 10));
            repaint();
        });
    };
    addAndMakeVisible (advBtn);

    // TARGET + LEARN
    learnBtn.getProperties().set ("size", 10.0f);
    learnBtn.setTooltip ("LEARN: 1) set CLIPPER (and the rest) until you hear no distortion. 2) Play the loudest part of the song: CALIBRATING fills up over about 10 seconds. "
                         "3) When it reads LEARN, click it: FLATLINE sets GAIN so the loudest moment hits TARGET, then checks once more. Changing a setting starts the calibration again.");
    learnBtn.onClick = [this] { if (learning) { learning = false; learnMsg = {}; } else startLearn(); };
    addAndMakeVisible (learnBtn);
    auto* targetP = proc.apvts.getParameter ("target");
    for (auto* b : { &tMinus, &tPlus }) { b->getProperties().set ("size", 13.0f); addAndMakeVisible (*b); }
    tMinus.setButtonText (minus()); tMinus.setTooltip ("TARGET: the loudness to aim for (short-term LUFS)."); tPlus.setTooltip (tMinus.getTooltip());
    tMinus.onClick = [targetP] { targetP->setValueNotifyingHost (targetP->convertTo0to1 (targetP->convertFrom0to1 (targetP->getValue()) - 0.5f)); };
    tPlus.onClick = [targetP] { targetP->setValueNotifyingHost (targetP->convertTo0to1 (targetP->convertFrom0to1 (targetP->getValue()) + 0.5f)); };

    // presets
    for (auto* b : { &prevBtn, &nextBtn }) { b->getProperties().set ("size", 13.0f); addAndMakeVisible (*b); }
    prevBtn.setButtonText (juce::String::fromUTF8 ("\xe2\x80\xb9")); nextBtn.setButtonText (juce::String::fromUTF8 ("\xe2\x80\xba"));
    refreshPresetBox();
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.currentPreset) { proc.loadPreset (i); repaint(); } };
    prevBtn.onClick = [this] { const int n = (int) FlatlineProcessor::presets().size(); proc.loadPreset ((proc.currentPreset + n - 1) % n); refreshPresetBox(); repaint(); };
    nextBtn.onClick = [this] { const int n = (int) FlatlineProcessor::presets().size(); proc.loadPreset ((proc.currentPreset + 1) % n); refreshPresetBox(); repaint(); };
    addAndMakeVisible (presetBox);

    // HELP
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "FLATLINE"; o.folderName = "FLATLINE"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (o);
    }
    helpBtn.getProperties().set ("kind", "round"); helpBtn.setClickingTogglesState (true);
    helpBtn.onClick = [this] { setHelp (helpBtn.getToggleState()); };
    addAndMakeVisible (helpBtn);
    setHelp (settings->getBoolValue ("showHelp", false));

    resized();
    startTimerHz (30);
}

FlatContent::~FlatContent() { stopTimer(); }

void FlatContent::refreshPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    int id = 1; for (const auto& pr : FlatlineProcessor::presets()) presetBox.addItem (pr.name, id++);
    presetBox.setSelectedId (proc.currentPreset + 1, juce::dontSendNotification);
    presetBox.setTooltip (FlatlineProcessor::presets()[(size_t) juce::jlimit (0, (int) FlatlineProcessor::presets().size() - 1, proc.currentPreset)].desc);
}

void FlatContent::setHelp (bool on)
{
    helpBtn.setToggleState (on, juce::dontSendNotification);
    tooltips = on ? std::make_unique<juce::TooltipWindow> (this, 500) : nullptr;
    if (tooltips) tooltips->setLookAndFeel (&lnf);
    helpBtn.setTooltip ("Help is on: hover over any control to see what it does. Click to turn the pop-ups off.");
    settings->setValue ("showHelp", on); settings->saveIfNeeded();
}

void FlatContent::resized()
{
    monitor.setBounds (20, 86, 780, 330);
    tank.setBounds (115 - 76, 594 - 76, 152, 152);
    for (auto* k : knobs) k->s.setBounds (k->c.x - 42, k->c.y - 42, 84, 84);
    int x = 716; const int w[4] = { 100, 64, 62, 52 };
    for (int i = 0; i < 4; ++i) { styleBtn[i].setBounds (x, 20, w[i], 28); x += w[i]; }
    prevBtn.setBounds (394, 20, 28, 28); presetBox.setBounds (430, 18, 230, 32); nextBtn.setBounds (668, 20, 28, 28);
    helpBtn.setBounds (1046, 19, 32, 32);
    autoBtn.setBounds (335, 664, 50, 20);
    upLed.setBounds (618, 486, 20, 20); clipLed.setBounds (808, 486, 20, 20);
    softBtn.setBounds (817, 664, 39, 20); hardBtn.setBounds (856, 664, 39, 20);
    bypassBtn.setBounds (932, 494, 136, 40); deltaBtn.setBounds (932, 542, 136, 40); advBtn.setBounds (932, 590, 136, 40);
    learnBtn.setBounds (828, 190, 116, 26); tMinus.setBounds (952, 190, 26, 26); tPlus.setBounds (1042, 190, 26, 26);
}

void FlatContent::mouseDown (const juce::MouseEvent& e)
{
    // click the INTEGRATED box to start measuring again
    if (juce::Rectangle<int> (816, 236, 264, 70).contains (e.getPosition())) { proc.engine().resetMeters.store (true); learnMsg = {}; }
}

void FlatContent::startLearn()
{
    learning = true; learnTicks = 0; learnGood = 0; learnMsg = "LISTENING";
}

void FlatContent::timerCallback()
{
    auto& e = proc.engine();
    auto ease = [] (float& v, float t, float k) { v += (t - v) * k; };
    lufsS = e.lufsS.load(); lufsI = e.lufsI.load(); tp = e.tpMax.load();
    const float g = e.grNow.load(); gr = g > gr ? g : gr + (g - gr) * 0.25f;
    grMax = e.grMax.load();
    const float c = e.clipNow.load(); clip = c > clip ? c : clip + (c - clip) * 0.2f;
    ease (lift, e.liftNow.load(), 0.3f);
    ease (clean, e.clean.load(), 0.2f);
    plr = (tp > -100 && lufsI > -70) ? tp - lufsI : 0.0f;
    // alarm states
    monitor.alarm = (gr > 9.0f || (clean < 70.0f && lufsS > -60)) ? 2 : (gr > 4.5f || (clean < 88.0f && lufsS > -60)) ? 1 : 0;
    monitor.update();

    // CALIBRATING: the last 10 s of short-term loudness at the current settings. Any change to the sound (GAIN, CLIPPER,
    // STYLE...) starts it again: short-term needs 3 s to show the change, so it waits that long, then remembers up to 10 s.
    {
        float sig = 0;
        for (const char* id : { "gain", "ceiling", "style", "autoRelease", "release", "link", "transient", "upOn", "upAmount", "upBelow",
                                "clipOn", "clipDrive", "clipHard", "truePeak", "lookahead", "bypass", "delta" })
            sig = sig * 1.37f + proc.apvts.getRawParameterValue (id)->load();
        if (std::abs (sig - lastGain) > 1e-4f * (1.0f + std::abs (sig))) { lastGain = sig; hist.clear(); settle = 95; if (! learning && learnMsg == "TARGET HIT") learnMsg = {}; }
        if (settle > 0) --settle;
        else if (lufsS > -60.0f) { hist.push_back (lufsS); if (hist.size() > 300) hist.erase (hist.begin()); }
    }
    // LEARN: aims the loudest moment of what it has heard at TARGET. Press it after the loud part has played a moment
    // and it sets GAIN at once; then it listens 1 s more (after the 3 s settle) to fine-tune.
    if (learning && hist.size() >= 30)
    {
        const float target = proc.apvts.getRawParameterValue ("target")->load();
        const float loudest = *std::max_element (hist.begin(), hist.end());
        const float diff = target - loudest;
        auto* gp = proc.apvts.getParameter ("gain");
        const float gain = gp->convertFrom0to1 (gp->getValue());
        if (std::abs (diff) < 0.5f) { learning = false; learnMsg = "TARGET HIT"; }
        else
        {
            // the limiter gives back less than the GAIN added, so each step aims a touch past and then checks
            const float ng = juce::jlimit (0.0f, 20.0f, std::round ((gain + juce::jlimit (-8.0f, 8.0f, diff * (diff > 0 ? 1.15f : 1.0f))) * 10.0f) / 10.0f);
            if (std::abs (ng - gain) < 0.05f) { learning = false; learnMsg = diff > 0 ? "GAIN AT MAX" : "GAIN AT 0"; }
            else { gp->setValueNotifyingHost (gp->convertTo0to1 (ng)); learnMsg = "ADJUSTING"; }
            if (++learnTicks > 8) { learning = false; learnMsg = {}; }   // safety: never loops forever on music that won't settle
        }
    }
    const bool ready = hist.size() >= 300;
    learnBtn.setButtonText (learning ? (learnMsg == "ADJUSTING" ? "ADJUSTING..." : "LISTENING...")
                            : learnMsg == "TARGET HIT" ? "TARGET HIT" : ready ? "LEARN" : "CALIBRATING");
    learnBtn.setToggleState (learning || learnMsg == "TARGET HIT", juce::dontSendNotification);
    learnBtn.getProperties().set ("ready", ready);
    calib = (float) hist.size() / 300.0f;

    const bool autoRel = proc.apvts.getRawParameterValue ("autoRelease")->load() > 0.5f;
    release.s.setEnabled (! autoRel);
    upAmount.s.setEnabled (upLed.getToggleState()); upBelow.s.setEnabled (upLed.getToggleState());
    clipDrive.s.setEnabled (clipLed.getToggleState());
    softBtn.setEnabled (clipLed.getToggleState()); hardBtn.setEnabled (clipLed.getToggleState());
    repaint (0, 70, kW, 400);
}

void FlatContent::renderBackground (float scale)
{
    using namespace juce;
    bgScale = scale;
    bg = Image (Image::RGB, roundToInt (kW * scale), roundToInt (kH * scale), false);
    Graphics g (bg);
    g.addTransform (AffineTransform::scale (scale));
    g.setGradientFill (ColourGradient (Colour (0xff141a1b), 0, 0, Colour (0xff0b0e0f), 0, (float) kH, false)); g.fillAll();
    for (int x = 0; x < kW; x += 3) { g.setColour (Colours::white.withAlpha (0.012f)); g.fillRect ((float) x, 0.0f, 1.0f, (float) kH); }
    // header
    g.setGradientFill (ColourGradient (Colour (0xff1a2022), 0, 0, Colour (0xff121718), 0, 70, false)); g.fillRect (0, 0, kW, 70);
    g.setColour (line); g.fillRect (0, 70, kW, 1);
    drawLogo (g, lnf, 24.0f, 58.0f, 46.0f);
    g.setFont (lnf.body (10.0f, 3.0f)); g.setColour (dim);
    g.drawText ("CLEAN LOUDNESS", Rectangle<float> (232, 28, 150, 14), Justification::centredLeft, false);
    // STYLE well
    g.setColour (Colour (0xff0a0d0e)); g.fillRoundedRectangle (712, 16, 286, 36, 7);
    g.setColour (line); g.drawRoundedRectangle (712.5f, 16.5f, 285, 35, 7, 1);
    // panels
    auto box = [&] (Rectangle<float> r)
    {
        g.setColour (Colours::black.withAlpha (0.4f)); g.fillRoundedRectangle (r.translated (0, 2), 9);
        g.setColour (panel); g.fillRoundedRectangle (r, 9);
        g.setColour (line); g.drawRoundedRectangle (r.reduced (0.5f), 9, 1);
    };
    box ({ 20, 426, 780, 36 });
    box ({ 20, 476, 190, 228 }); box ({ 220, 476, 380, 228 }); box ({ 610, 476, 180, 228 }); box ({ 800, 476, 110, 228 }); box ({ 920, 476, 160, 228 });
    auto screenBox = [&] (Rectangle<float> r)
    {
        g.setColour (screen); g.fillRoundedRectangle (r, 7);
        g.setColour (line); g.drawRoundedRectangle (r.reduced (0.5f), 7, 1);
    };
    screenBox ({ 816, 86, 264, 142 }); screenBox ({ 816, 236, 264, 70 }); screenBox ({ 816, 314, 264, 70 }); screenBox ({ 816, 392, 264, 70 });
    // section heads
    g.setFont (lnf.title (12.5f, 3.0f)); g.setColour (text);
    g.drawText ("GAIN", Rectangle<float> (34, 486, 100, 20), Justification::centredLeft, false);
    g.drawText ("LIMITER", Rectangle<float> (256, 486, 120, 20), Justification::centredLeft, false);
    g.drawText ("UPWARD", Rectangle<float> (644, 486, 120, 20), Justification::centredLeft, false);
    g.drawText ("CLIPPER", Rectangle<float> (834, 486, 76, 20), Justification::centredLeft, false);
    // the limiter's light is always on
    { const auto d = Rectangle<float> (11, 11).withCentre ({ 242, 496 }); g.setColour (green.withAlpha (0.35f)); g.fillEllipse (d.expanded (4)); g.setColour (green); g.fillEllipse (d); }
}

void FlatContent::paint (juce::Graphics& g)
{
    using namespace juce;
    const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! bg.isValid() || std::abs (ps - bgScale) > 0.01f) renderBackground (ps);
    g.drawImage (bg, Rectangle<float> (0, 0, (float) kW, (float) kH), RectanglePlacement::stretchToFit);
    const String dot = String::fromUTF8 ("\xc2\xb7");
    auto lab = [&] (const String& s, float x, float y, float w, Colour c = dim, Justification j = Justification::centredLeft)
    { g.setFont (lnf.body (9.0f, 2.0f)); g.setColour (c); g.drawText (s, Rectangle<float> (x, y, w, 12), j, false); };
    auto big = [&] (const String& v, const String& unit, float x, float y, float size, Colour c, bool glow = false)
    {
        GlyphArrangement ga; ga.addLineOfText (lnf.title (size, 0.3f), v, x, y);
        Path p; ga.createPath (p);
        if (glow) { g.setColour (c.withAlpha (0.25f)); g.strokePath (p, PathStrokeType (4.0f)); }
        g.setColour (c); g.fillPath (p);
        g.setFont (lnf.title (size * 0.3f, 0.5f)); g.setColour (dim);
        g.drawText (unit, Rectangle<float> (p.getBounds().getRight() + 5, y - size * 0.32f, 60, size * 0.32f), Justification::bottomLeft, false);
    };
    const bool live = lufsS > -70.0f;
    const float target = proc.apvts.getRawParameterValue ("target")->load();
    const Colour state = monitor.alarm == 2 ? red : monitor.alarm == 1 ? amber : green;

    // ---- loudness box
    lab ("SHORT-TERM", 828, 96, 120);
    lab ("TARGET", 960, 96, 108, mint, Justification::centredRight);
    big (live ? num (lufsS, 1) : String::fromUTF8 ("\xe2\x80\x94"), "LUFS", 828, 160, 50, live ? state : dim, live);
    {
        const Rectangle<float> bar (828, 172, 240, 4);
        g.setColour (Colour (0xff0c1312)); g.fillRoundedRectangle (bar, 2);
        auto pos = [&] (float l) { return bar.getX() + bar.getWidth() * jlimit (0.0f, 1.0f, (l + 30.0f) / 26.0f); };
        if (live) { g.setGradientFill (ColourGradient (Colour (0xff1c5a38), bar.getX(), 0, state, pos (lufsS), 0, false)); g.fillRoundedRectangle (bar.withRight (pos (lufsS)), 2); }
        g.setColour (greenHot); g.fillRect (pos (target) - 1, bar.getY() - 4, 2.0f, 12.0f);
    }
    {
        const Rectangle<float> pb (830, 217, 112, 2);
        g.setColour (Colour (0xff0c1312)); g.fillRect (pb);
        g.setColour (calib >= 1.0f ? green : green.withAlpha (0.55f)); g.fillRect (pb.withWidth (pb.getWidth() * jlimit (0.0f, 1.0f, calib)));
    }
    g.setFont (lnf.title (14.0f, 0.5f)); g.setColour (text);
    g.drawText (num (target, std::abs (target - std::round (target)) > 0.01f ? 1 : 0), Rectangle<float> (978, 190, 64, 26), Justification::centred, false);

    // ---- integrated / true peak
    lab ("INTEGRATED " + dot + " TP MAX", 828, 244, 240);
    lab ("click to reset", 980, 244, 88, faint, Justification::centredRight);
    big (lufsI > -70 ? num (lufsI, 1) : String::fromUTF8 ("\xe2\x80\x94"), "LUFS", 828, 292, 26, text);
    big (tp > -100 ? num (tp, 1) : String::fromUTF8 ("\xe2\x80\x94"), "dBTP", 980, 292, 26, tp <= proc.apvts.getRawParameterValue ("ceiling")->load() + 0.05f ? text
                                                                                         : proc.apvts.getRawParameterValue ("truePeak")->load() > 0.5f ? red : amber);   // sample peak mode: inter-sample peaks over the ceiling are expected
    // ---- limit / clip / lift
    lab ("LIMIT " + dot + " CLIP " + dot + " LIFT", 828, 322, 240);
    big (num (-gr, 1), "dB", 828, 370, 24, red);
    big (num (-clip, 1), "dB", 920, 370, 24, proc.apvts.getRawParameterValue ("clipOn")->load() > 0.5f ? amber : faint);
    big (num (lift, 1, true), "dB", 1002, 370, 24, proc.apvts.getRawParameterValue ("upOn")->load() > 0.5f ? mint : faint);
    // ---- PLR / clean
    lab ("PUNCH LEFT (PLR) " + dot + " CLEAN", 828, 400, 240);
    big (plr > 0 ? num (plr, 1) : String::fromUTF8 ("\xe2\x80\x94"), "dB", 828, 448, 24, text);
    big (live ? String (roundToInt (clean)) : String::fromUTF8 ("\xe2\x80\x94"), "%", 1000, 448, 24, live ? state : dim);

    // ---- streaming
    lab ("STREAMING TURNS IT DOWN", 34, 438, 200);
    struct P { const char* n; float ref; };
    const P plats[] = { { "SPOTIFY", -14 }, { "APPLE", -16 }, { "YOUTUBE", -14 }, { "TIDAL", -14 }, { "DEEZER", -15 } };
    float x = 232;
    const float ref = lufsI > -70 ? lufsI : lufsS;
    for (const auto& pl : plats)
    {
        lab (pl.n, x, 438, 80);
        const float d = (ref > -70) ? jmin (0.0f, pl.ref - ref) : 0.0f;
        g.setFont (lnf.title (14.0f, 0.5f)); g.setColour (ref > -70 ? (d < -6 ? amber : text) : faint);
        g.drawText (ref > -70 ? num (d, 1) : String::fromUTF8 ("\xe2\x80\x94"), Rectangle<float> (x + (float) GlyphArrangement::getStringWidthInt (lnf.body (9.0f, 2.0f), pl.n) + 8, 434, 50, 20), Justification::centredLeft, false);
        x += 112;
    }

    // ---- knob names and values
    for (auto* k : knobs)
    {
        const bool on = k->s.isEnabled();
        g.setFont (lnf.title (11.5f, 2.6f)); g.setColour (on ? text : dim);
        g.drawText (k->label, Rectangle<float> ((float) k->c.x - 50, 626, 100, 14), Justification::centred, false);
        g.setFont (lnf.body (11.5f)); g.setColour (on ? green : dim);
        const String v = (k == &release && ! on) ? String ("AUTO") : k->s.getTextFromValue (k->s.getValue());
        g.drawText (v, Rectangle<float> ((float) k->c.x - 50, 642, 100, 14), Justification::centred, false);
    }
    lab ("LIFTS THE QUIET PARTS", 626, 668, 160, dim);
    // right panel info
    g.setFont (lnf.body (8.5f, 1.5f)); g.setColour (dim);
    const double fs = proc.rate.load();
    const int dith = roundToInt (proc.apvts.getRawParameterValue ("dither")->load());
    auto lookText = [this]
    {
        static const float styleLook[4] = { 4.0f, 2.0f, 1.5f, 5.0f }, looks[8] = { 0, 0.5f, 1, 1.5f, 2, 3, 4, 5 };
        const int l = roundToInt (proc.apvts.getRawParameterValue ("lookahead")->load());
        const int st = jlimit (0, 3, roundToInt (proc.apvts.getRawParameterValue ("style")->load()));
        const float ms = l == 0 ? styleLook[st] : looks[l];
        return "LOOKAHEAD " + String (ms, ms == std::floor (ms) ? 0 : 1) + " MS" + (l == 0 ? " (AUTO)" : "");
    };
    const String info[4] = { String (proc.engine().latency() * 1000.0 / fs, 1) + " MS LATENCY",
                             String ("TRUE PEAK ") + (proc.apvts.getRawParameterValue ("truePeak")->load() > 0.5f ? "8X" : "OFF") + " " + dot + " CLIP 4X",
                             String ("DITHER ") + (dith == 0 ? "OFF" : dith == 1 ? "24-BIT" : "16-BIT"),
                             lookText() };
    for (int i = 0; i < 4; ++i) g.drawText (info[i], Rectangle<float> (934, 640.0f + 14.0f * (float) i, 140, 12), Justification::centredLeft, false);
}

//============================================================================== editor
FlatlineEditor::FlatlineEditor (FlatlineProcessor& p) : AudioProcessorEditor (p), content (p, lnf)
{
    // read the remembered width first: setting the limits below resizes the window, and resized() would overwrite it
    const int w = juce::jlimit (FlatContent::kW * 6 / 10, FlatContent::kW * 2, (int) p.apvts.state.getProperty ("uiSize", FlatContent::kW));
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) FlatContent::kW / FlatContent::kH);
    setResizeLimits (FlatContent::kW * 6 / 10, FlatContent::kH * 6 / 10, FlatContent::kW * 2, FlatContent::kH * 2);
    setSize (w, juce::roundToInt ((double) w * FlatContent::kH / FlatContent::kW));
    ready = true;
}
FlatlineEditor::~FlatlineEditor() { setLookAndFeel (nullptr); }
void FlatlineEditor::resized()
{
    if (ready) if (auto* pp = dynamic_cast<FlatlineProcessor*> (&processor)) pp->apvts.state.setProperty ("uiSize", getWidth(), nullptr);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) FlatContent::kW));
    content.setBounds (0, 0, FlatContent::kW, FlatContent::kH);
}
