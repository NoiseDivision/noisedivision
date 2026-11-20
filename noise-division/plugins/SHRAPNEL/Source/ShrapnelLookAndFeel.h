#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

// SHRAPNEL look: industrial, from the KRUSH family. Riveted dark steel plates, red-to-orange
// accents with a glow, knurled steel knobs in a lit arc, silver fader caps, a hazard stripe.
namespace Pal
{
    inline const juce::Colour bg       { 0xff141517 };
    inline const juce::Colour plateTop { 0xff232428 };
    inline const juce::Colour plateBot { 0xff1b1c1f };
    inline const juce::Colour plateEdge{ 0xff2c2e33 };
    inline const juce::Colour well     { 0xff0b0c0e };
    inline const juce::Colour edge     { 0xff3a3c42 };
    inline const juce::Colour red      { 0xffff3038 };   // KRUSH Red
    inline const juce::Colour orange   { 0xffff7138 };   // Hot Orange
    inline const juce::Colour amber    { 0xffffa648 };
    inline const juce::Colour hazard   { 0xffe8a020 };   // the drive curve and the warning stripe
    inline const juce::Colour silver   { 0xffc6c8cc };
    inline const juce::Colour steel    { 0xff777a82 };
    inline const juce::Colour text     { 0xffe4e5e8 };
    inline const juce::Colour dim      { 0xff55585f };

    inline juce::Colour heat (float t) { t = juce::jlimit (0.0f, 1.0f, t); return t < 0.6f ? red.interpolatedWith (orange, t / 0.6f) : orange.interpolatedWith (amber, (t - 0.6f) / 0.4f); }
}

class ShrapnelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Typeface::Ptr regular, semibold, oswald;

    ShrapnelLookAndFeel()
    {
        setColour (juce::Label::textColourId, Pal::silver);
        setColour (juce::TextEditor::backgroundColourId, Pal::well);
        setColour (juce::TextEditor::textColourId, Pal::silver);
        setColour (juce::TextEditor::focusedOutlineColourId, Pal::red);
        setColour (juce::TextEditor::outlineColourId, Pal::edge);
        setColour (juce::TextEditor::highlightColourId, Pal::red.withAlpha (0.4f));
        setColour (juce::CaretComponent::caretColourId, Pal::silver);
        setColour (juce::ComboBox::backgroundColourId, Pal::well);
        setColour (juce::ComboBox::textColourId, Pal::text);
        setColour (juce::ComboBox::outlineColourId, Pal::edge);
        setColour (juce::ComboBox::arrowColourId, Pal::red);
        setColour (juce::PopupMenu::backgroundColourId, Pal::plateTop);
        setColour (juce::PopupMenu::textColourId, Pal::silver);
        setColour (juce::PopupMenu::headerTextColourId, Pal::steel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Pal::red);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::AlertWindow::backgroundColourId, Pal::plateTop);
        setColour (juce::AlertWindow::textColourId, Pal::silver);
        setColour (juce::AlertWindow::outlineColourId, Pal::red);
    }

    // sizes are CSS-like pixel sizes (JUCE's font height is the line height: Montserrat 1.219x, Oswald 1.482x)
    juce::Font font (float size, bool bold = true, float track = 0.0f) const
    {
        const float h = size * 1.219f;
        auto tf = bold ? semibold : regular;
        auto f = tf != nullptr ? juce::Font (juce::FontOptions (tf).withHeight (h)) : juce::Font (juce::FontOptions().withHeight (h));
        return f.withExtraKerningFactor (track / h);
    }
    juce::Font value (float size, float track = 1.0f) const
    {
        const float h = size * 1.482f;
        return juce::Font (juce::FontOptions (oswald).withHeight (h)).withExtraKerningFactor (track / h);
    }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        if (semibold != nullptr && f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) return semibold;
        return LookAndFeel_V4::getTypefaceForFont (f);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.0f, false, 0.5f); }
    juce::Font getPopupMenuFont() override { return font (12.0f); }
    juce::Font getAlertWindowTitleFont() override { return font (14.0f, true, 1.0f); }
    juce::Font getAlertWindowMessageFont() override { return font (12.0f, false); }
    juce::Font getAlertWindowFont() override { return font (12.0f, false); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (juce::jmin (11.0f, (float) h * 0.5f), true, 1.5f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }

    // ---------------------------------------------------------------- buttons
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        using namespace juce;
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        if (b.getProperties().contains ("round"))
        {
            const auto c = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight())).reduced (0.5f);
            g.setColour (on ? Pal::red : (over ? Pal::plateTop : Pal::plateBot));
            g.fillEllipse (c);
            g.setColour (on ? Pal::red : (over ? Pal::silver : Pal::edge));
            g.drawEllipse (c, 1.0f);
            return;
        }
        if (on)
        {
            g.setColour (Pal::red.withAlpha (0.22f));                      // glow
            g.drawRoundedRectangle (r.expanded (2.0f), 4.0f, 4.0f);
            g.setGradientFill (ColourGradient (Colour (0xff5a1014), 0, r.getY(), Colour (0xff38080b), 0, r.getBottom(), false));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (Pal::red.withAlpha (0.25f));
            g.drawRoundedRectangle (r.reduced (2.0f), 1.5f, 3.0f);
            g.setColour (Pal::red);
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
        }
        else
        {
            g.setGradientFill (ColourGradient (over ? Colour (0xff2c2d32) : Colour (0xff26272b), 0, r.getY(), Colour (0xff1d1e21), 0, r.getBottom(), false));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (over ? Pal::steel : Pal::edge);
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
        }
        if (down) { g.setColour (Colours::black.withAlpha (0.2f)); g.fillRoundedRectangle (r, 2.0f); }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        using namespace juce;
        const bool on = b.getToggleState();
        const auto r = b.getLocalBounds().toFloat();
        if (b.getProperties().contains ("icon"))       // mode buttons: icon over the name
        {
            const int i = (int) b.getProperties()["icon"];
            const auto col = on ? Colours::white : Colour (0xff8e9198);
            drawModeIcon (g, i, { r.getCentreX(), r.getY() + r.getHeight() * 0.5f - 8.0f }, col);
            g.setFont (font (10.0f, true, 1.5f));
            g.setColour (on ? Colours::white : Colour (0xff9a9da4));
            g.drawText (b.getButtonText(), r.withTrimmedTop (r.getHeight() - 22.0f).translated (0.75f, 0.0f), Justification::centred, false);
            return;
        }
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour ((on ? Colours::white : Pal::silver).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.35f));
        g.drawText (b.getButtonText(), r.translated (0.75f, 0.0f), Justification::centred, false);
    }

    static void drawModeIcon (juce::Graphics& g, int i, juce::Point<float> c, juce::Colour col)
    {
        using namespace juce;
        Path p;
        const float x = c.x, y = c.y;
        switch (i)
        {
            case 0:   // harmonics: falling spikes
                p.startNewSubPath (x - 14, y + 6); p.lineTo (x - 10, y - 8); p.lineTo (x - 6, y + 6);
                p.startNewSubPath (x - 2, y + 6); p.lineTo (x, y - 3); p.lineTo (x + 2, y + 6);
                p.startNewSubPath (x + 6, y + 6); p.lineTo (x + 7, y); p.lineTo (x + 8, y + 6);
                p.startNewSubPath (x + 11, y + 6); p.lineTo (x + 11.5f, y + 2); p.lineTo (x + 12, y + 6); break;
            case 1:   // clip: flattened top
                p.startNewSubPath (x - 14, y + 6); p.lineTo (x - 8, y - 6); p.lineTo (x + 8, y - 6); p.lineTo (x + 14, y + 6); break;
            case 2:   // crush: steps
                p.startNewSubPath (x - 14, y + 6); p.lineTo (x - 14, y + 1); p.lineTo (x - 7, y + 1); p.lineTo (x - 7, y - 6);
                p.lineTo (x, y - 6); p.lineTo (x, y - 2); p.lineTo (x + 7, y - 2); p.lineTo (x + 7, y + 3); p.lineTo (x + 14, y + 3); p.lineTo (x + 14, y + 6); break;
            case 3:   // smear: a soft hump
                p.startNewSubPath (x - 14, y + 6); p.cubicTo (x - 6, y + 6, x - 6, y - 7, x, y - 7); p.cubicTo (x + 6, y - 7, x + 6, y + 6, x + 14, y + 6); break;
            case 4:   // scramble: zig-zag
            {
                const float pts[][2] = { { -14, 0 }, { -10, -5 }, { -7, 5 }, { -3, -7 }, { 0, 6 }, { 4, -3 }, { 7, 4 }, { 10, -6 }, { 14, 2 } };
                p.startNewSubPath (x + pts[0][0], y + pts[0][1]);
                for (int k = 1; k < 9; ++k) p.lineTo (x + pts[k][0], y + pts[k][1]);
                break;
            }
            default:  // warp: slanted strokes
                p.startNewSubPath (x - 14, y + 6); p.lineTo (x - 8, y - 6);
                p.startNewSubPath (x - 4, y + 6); p.lineTo (x + 1, y - 6);
                p.startNewSubPath (x + 5, y + 6); p.lineTo (x + 9, y - 2);
                p.startNewSubPath (x + 12, y + 6); p.lineTo (x + 14, y + 2); break;
        }
        g.setColour (col);
        g.strokePath (p, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // ---------------------------------------------------------------- knobs
    // knurled steel cap inside a lit red-to-orange arc; the component is (2 r + 42) square
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        using namespace juce;
        const auto area = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const auto c = area.getCentre();
        const float r = (jmin (area.getWidth(), area.getHeight()) - 42.0f) * 0.5f;
        const float R = r + 9.0f;
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const bool hot = (bool) s.getProperties().getWithDefault ("hot", false);
        const float angle = a0 + pos * (a1 - a0);
        const float arcW = jmax (4.0f, r * 0.1f);

        // ticks
        g.setColour (Colour (0xff4a4d53));
        for (int i = 0; i <= 10; ++i)
        {
            const float t = a0 + (a1 - a0) * (float) i / 10.0f;
            g.drawLine (Line<float> (polar (c, t, R + 5.0f), polar (c, t, R + 9.0f)), 1.5f);
        }
        // track and lit arc
        Path track; track.addCentredArc (c.x, c.y, R, R, 0.0f, a0, a1, true);
        g.setColour (Colour (0xff0e0f11));
        g.strokePath (track, PathStrokeType (arcW + 2.0f));
        const float from = bipolar ? 0.5f * (a0 + a1) : a0;
        if (std::abs (angle - from) > 1.0e-3f)
        {
            Path arc; arc.addCentredArc (c.x, c.y, R, R, 0.0f, jmin (from, angle), jmax (from, angle), true);
            const auto p0 = polar (c, a0, R), p1 = polar (c, a1, R);
            ColourGradient grad (Pal::red, p0.x, p0.y, hot ? Pal::amber : Pal::orange, p1.x, p1.y, false);
            g.setGradientFill (grad);
            g.setOpacity (0.25f);
            g.strokePath (arc, PathStrokeType (arcW + 7.0f));
            g.setGradientFill (grad);
            g.setOpacity (1.0f);
            g.strokePath (arc, PathStrokeType (arcW));
        }
        // cap (cached per size)
        const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
        const int key = roundToInt (r * 10.0f) * 1000 + roundToInt (ps * 100.0f);
        auto& cap = caps[key];
        const float side = r * 2.0f + 16.0f;
        if (! cap.isValid())
        {
            cap = Image (Image::ARGB, jmax (1, roundToInt (side * ps)), jmax (1, roundToInt (side * ps)), true);
            Graphics cg (cap);
            cg.addTransform (AffineTransform::scale (ps));
            const Point<float> cc (side * 0.5f, side * 0.5f);
            Path disc; disc.addEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (cc));
            DropShadow (Colours::black.withAlpha (0.6f), roundToInt (r * 0.18f + 3.0f), { 0, 4 }).drawForPath (cg, disc);
            ColourGradient body (Colour (0xff55585e), cc.x - r * 0.25f, cc.y - r * 0.35f, Colour (0xff121315), cc.x + r * 0.6f, cc.y + r * 0.8f, true);
            body.addColour (0.55, Colour (0xff2a2c30));
            cg.setGradientFill (body);
            cg.fillPath (disc);
            cg.setColour (Colour (0xff0b0b0d));
            cg.strokePath (disc, PathStrokeType (1.5f));
            const int teeth = roundToInt (r * 1.4f);
            for (int i = 0; i < teeth; ++i)
            {
                const float t = (float) i / (float) teeth * MathConstants<float>::twoPi;
                cg.setColour (i % 2 ? Colour (0xff0d0e10) : Colour (0xff4a4d53));
                cg.drawLine (Line<float> (polar (cc, t, r - 3.0f), polar (cc, t, r)), 1.6f);
            }
            Path inner; inner.addEllipse (Rectangle<float> (r * 1.44f, r * 1.44f).withCentre (cc));
            cg.setGradientFill (body);
            cg.fillPath (inner);
            cg.setColour (Colour (0xff0b0b0d));
            cg.strokePath (inner, PathStrokeType (1.0f));
            cg.setColour (Colours::white.withAlpha (0.08f));
            cg.drawEllipse (Rectangle<float> (r * 1.44f - 2.0f, r * 1.44f - 2.0f).withCentre (cc), 1.0f);
        }
        g.drawImage (cap, Rectangle<float> (side, side).withCentre (c));
        // pointer
        g.setColour (Pal::red);
        g.drawLine (Line<float> (polar (c, angle, r * 0.2f), polar (c, angle, r * 0.66f)), jmax (2.5f, r / 12.0f));
    }

    // ---------------------------------------------------------------- horizontal sliders (SPLIT)
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override
    {
        using namespace juce;
        const float cy = (float) y + (float) h * 0.5f;
        const Rectangle<float> slot ((float) x, cy - 3.0f, (float) w, 6.0f);
        g.setColour (Pal::well);
        g.fillRect (slot);
        g.setColour (Pal::plateEdge);
        g.drawRect (slot, 1.0f);
        if (pos > (float) x + 0.5f)
        {
            const auto lit = slot.withRight (pos);
            g.setColour (Pal::red.withAlpha (0.25f));
            g.fillRect (lit.expanded (0.0f, 3.0f));
            g.setGradientFill (ColourGradient (Pal::red, (float) x, 0.0f, Pal::orange, (float) (x + w), 0.0f, false));
            g.fillRect (lit);
        }
        const Rectangle<float> cap (pos - 7.0f, cy - 9.0f, 14.0f, 18.0f);
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillRect (cap.translated (0.0f, 2.0f));
        g.setGradientFill (ColourGradient (Colour (0xffd9dbde), 0.0f, cap.getY(), Colour (0xff8e9198), 0.0f, cap.getBottom(), false));
        g.fillRect (cap);
        g.setColour (Colour (0xff111111));
        g.drawRect (cap, 1.0f);
        g.fillRect (pos - 0.5f, cap.getY() + 3.0f, 1.0f, cap.getHeight() - 6.0f);
    }
    int getSliderThumbRadius (juce::Slider&) override { return 7; }

    // ---------------------------------------------------------------- preset box, menus, help
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff26272b), 0, 0, juce::Colour (0xff1d1e21), 0, (float) h, false));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (box.isMouseOver (true) ? Pal::steel : Pal::edge);
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.5f, ax + 4.0f, ay - 2.5f, ax, ay + 3.0f);
        g.setColour (Pal::red);
        g.fillPath (arrow);
    }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& l) override
    {
        l.setBounds (6, 1, box.getWidth() - 28, box.getHeight() - 2);
        l.setFont (getComboBoxFont (box));
    }

    juce::TextLayout tooltipLayout (const juce::String& t) const
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::topLeft);
        s.append (t, font (12.0f, false), Pal::silver);
        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 300.0f);
        return tl;
    }
    juce::Rectangle<int> getTooltipBounds (const juce::String& t, juce::Point<int> pos, juce::Rectangle<int> area) override
    {
        const auto tl = tooltipLayout (t);
        const int w = (int) std::ceil (tl.getWidth()) + 26, h = (int) std::ceil (tl.getHeight()) + 16;
        const int x = pos.x > area.getCentreX() ? pos.x - w - 6 : pos.x + 14;
        const int y = pos.y + h + 24 > area.getBottom() ? pos.y - h - 6 : pos.y + 20;
        return juce::Rectangle<int> (x, y, w, h).constrainedWithin (area);
    }
    void drawTooltip (juce::Graphics& g, const juce::String& t, int w, int h) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.setColour (Pal::bg); g.fillRect (r);
        g.setColour (Pal::edge); g.drawRect (r, 1.0f);
        g.setColour (Pal::red); g.fillRect (0.0f, 0.0f, 3.0f, (float) h);
        tooltipLayout (t).draw (g, r.withTrimmedLeft (15).withTrimmedRight (10).withTrimmedTop (8).withTrimmedBottom (8));
    }

    static juce::Point<float> polar (juce::Point<float> c, float a, float r) { return { c.x + r * std::sin (a), c.y - r * std::cos (a) }; }

private:
    std::map<int, juce::Image> caps;
};
