#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

// RECOIL look: anodised gunmetal. Fine-grained plates with crisp chamfers and hex screws, machined knobs
// (knurled skirt, turned top that catches the light), flat segmented switches, red-to-amber heat for the
// action, brass for the phase controls, tritium green for "locked".
namespace Pal
{
    inline const juce::Colour bgTop    { 0xff2a2e34 };
    inline const juce::Colour bgBot    { 0xff1a1c20 };
    inline const juce::Colour plateTop { 0xff343941 };
    inline const juce::Colour plateMid { 0xff2a2e35 };
    inline const juce::Colour plateBot { 0xff262a30 };
    inline const juce::Colour edge     { 0xff0a0b0d };
    inline const juce::Colour well     { 0xff0c0d10 };
    inline const juce::Colour segWell  { 0xff121417 };
    inline const juce::Colour red      { 0xffff3b42 };
    inline const juce::Colour orange   { 0xffff7a3d };
    inline const juce::Colour amber    { 0xffffad5a };
    inline const juce::Colour brass    { 0xffd2ad6a };
    inline const juce::Colour lock     { 0xff6ff59a };
    inline const juce::Colour trig     { 0xffe0646d };
    inline const juce::Colour text     { 0xffeef1f4 };
    inline const juce::Colour silver   { 0xffc9ced5 };
    inline const juce::Colour caps     { 0xff9aa1ab };
    inline const juce::Colour dim      { 0xff7b828c };
    inline const juce::Colour faint    { 0xff59606a };

    inline juce::Colour heat (float t) { t = juce::jlimit (0.0f, 1.0f, t); return red.interpolatedWith (amber, t); }
}

class RecoilLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Typeface::Ptr chakra, chakraBold, mont;

    RecoilLookAndFeel()
    {
        setColour (juce::Label::textColourId, Pal::silver);
        setColour (juce::TextEditor::backgroundColourId, Pal::well);
        setColour (juce::TextEditor::textColourId, Pal::text);
        setColour (juce::TextEditor::focusedOutlineColourId, Pal::orange);
        setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff3a3f47));
        setColour (juce::TextEditor::highlightColourId, Pal::orange.withAlpha (0.35f));
        setColour (juce::CaretComponent::caretColourId, Pal::text);
        setColour (juce::ComboBox::backgroundColourId, Pal::well);
        setColour (juce::ComboBox::textColourId, Pal::text);
        setColour (juce::ComboBox::outlineColourId, Pal::edge);
        setColour (juce::ComboBox::arrowColourId, Pal::orange);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff23272c));
        setColour (juce::PopupMenu::textColourId, Pal::silver);
        setColour (juce::PopupMenu::headerTextColourId, Pal::dim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Pal::orange);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
        setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff2a2e35));
        setColour (juce::AlertWindow::textColourId, Pal::silver);
        setColour (juce::AlertWindow::outlineColourId, Pal::orange);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf0121417));
        setColour (juce::TooltipWindow::textColourId, Pal::silver);
        setColour (juce::TooltipWindow::outlineColourId, Pal::orange.withAlpha (0.6f));
    }

    // Sizes are CSS-like pixel sizes. JUCE's font height is the line height: Chakra Petch 1.3x, Montserrat 1.219x.
    juce::Font chak (float size, bool bold = false, float track = 0.0f) const
    {
        const float h = size * 1.3f;
        auto tf = bold ? chakraBold : chakra;
        auto f = tf != nullptr ? juce::Font (juce::FontOptions (tf).withHeight (h)) : juce::Font (juce::FontOptions().withHeight (h));
        return f.withExtraKerningFactor (track / h);
    }
    juce::Font caps (float size, float track = 2.0f) const       // small uppercase captions
    {
        const float h = size * 1.219f;
        auto f = mont != nullptr ? juce::Font (juce::FontOptions (mont).withHeight (h)) : juce::Font (juce::FontOptions().withHeight (h));
        return f.withExtraKerningFactor (track / h);
    }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        if (chakra != nullptr && f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) return chakra;
        return LookAndFeel_V4::getTypefaceForFont (f);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return chak (13.0f); }
    juce::Font getPopupMenuFont() override { return chak (13.0f); }
    juce::Font getAlertWindowTitleFont() override { return chak (15.0f, true, 1.0f); }
    juce::Font getAlertWindowMessageFont() override { return caps (12.0f, 0.0f); }
    juce::Font getAlertWindowFont() override { return caps (12.0f, 0.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return chak (10.5f, false, 1.4f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }

    // ---------------------------------------------------------------- buttons
    // "seg" buttons are drawn as parts of a segmented switch (the well is drawn by the editor);
    // the rest are raised pills ("round" for the help button).
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        using namespace juce;
        auto r = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        if (b.getProperties().contains ("seg"))
        {
            if (on)
            {
                r = r.reduced (2.0f);
                g.setColour (Colours::black.withAlpha (0.5f));
                g.fillRoundedRectangle (r.translated (0.0f, 1.0f), 4.0f);
                g.setGradientFill (ColourGradient (Colour (0xff3d434b), 0, r.getY(), Colour (0xff30353c), 0, r.getBottom(), false));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (Colours::white.withAlpha (0.12f));
                g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 3.0f, r.getRight() - 3.0f);
            }
            else if (over)
            {
                g.setColour (Colours::white.withAlpha (0.04f));
                g.fillRoundedRectangle (r.reduced (2.0f), 4.0f);
            }
            return;
        }
        if (b.getProperties().contains ("round"))
            r = r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight()));
        const float rad = b.getProperties().contains ("round") ? r.getWidth() * 0.5f : 6.0f;
        r = r.reduced (1.0f);
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), rad);
        g.setGradientFill (ColourGradient (over ? Colour (0xff434952) : Colour (0xff3a3f47), 0, r.getY(), Colour (0xff2d3138), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (Pal::edge);
        g.drawRoundedRectangle (r, rad, 1.0f);
        g.setColour (Colours::white.withAlpha (0.10f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + rad * 0.6f, r.getRight() - rad * 0.6f);
        if (on && b.getProperties().contains ("round"))
        {
            g.setColour (Pal::orange.withAlpha (0.9f));
            g.drawRoundedRectangle (r, rad, 1.2f);
        }
        if (down) { g.setColour (Colours::black.withAlpha (0.2f)); g.fillRoundedRectangle (r, rad); }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool) override
    {
        using namespace juce;
        const bool on = b.getToggleState();
        Colour c = Colour (0xffaab1ba);
        if (b.getProperties().contains ("seg"))
        {
            c = on ? Colours::white : (over ? Pal::silver : Colour (0xff7d848e));
            if (on && b.getProperties().contains ("hot")) c = Pal::orange;
            if (on && b.getProperties().contains ("brass")) c = Pal::brass;
        }
        else if (b.getProperties().contains ("round") && on) c = Pal::orange;
        auto area = b.getLocalBounds();
        if (b.getProperties().contains ("pill") && on)                  // a lit toggle: glowing dot, coloured text
        {
            c = b.getProperties().contains ("brass") ? Pal::brass : Pal::orange;
            const float cy = (float) area.getCentreY(), cx = 11.0f;
            g.setColour (c.withAlpha (0.35f)); g.fillEllipse (cx - 5.0f, cy - 5.0f, 10.0f, 10.0f);
            g.setColour (c); g.fillEllipse (cx - 3.0f, cy - 3.0f, 6.0f, 6.0f);
            area.removeFromLeft (14);
        }
        if (! b.isEnabled()) c = c.withAlpha (0.35f);
        g.setColour (c);
        g.setFont (chak (b.getProperties().getWithDefault ("size", 10.5f), false, 1.4f));
        g.drawText (b.getButtonText(), area, Justification::centred, false);
    }

    // ---------------------------------------------------------------- preset box
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        using namespace juce;
        const auto r = Rectangle<float> (0, 0, (float) w, (float) h);
        g.setColour (Pal::well);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine (h - 1, 6.0f, (float) w - 6.0f);
        Path p;
        const float cx = (float) w - 16.0f, cy = (float) h * 0.5f;
        p.addTriangle (cx - 4.5f, cy - 2.5f, cx + 4.5f, cy - 2.5f, cx, cy + 3.0f);
        g.setColour (Pal::orange);
        g.fillPath (p);
    }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 34, box.getHeight() - 2);
        label.setFont (chak (13.0f, true, 0.3f));
    }

    // ---------------------------------------------------------------- knobs
    // A machined knob: lit value arc around it, shadow under it, knurled skirt, turned top, glowing pointer.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        using namespace juce;
        const float d = (float) s.getProperties().getWithDefault ("d", 44);
        const auto c = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
        const bool mini = s.getProperties().contains ("mini");
        const float R = d * 0.5f + (mini ? 4.0f : 9.0f);
        const bool brass = s.getProperties().contains ("brass");
        const float ang = a0 + pos * (a1 - a0);

        // ticks
        for (int i = 0; i <= 20 && ! mini; ++i)
        {
            const float a = a0 + (a1 - a0) * (float) i / 20.0f;
            const bool major = i % 5 == 0;
            g.setColour (major ? Colour (0xff717984) : Colour (0xff4c535c));
            g.drawLine (Line<float> (c.getPointOnCircumference (R + 5.0f, a), c.getPointOnCircumference (R + (major ? 9.0f : 7.0f), a)), 1.0f);
        }
        // arc track and value
        Path track; track.addCentredArc (c.x, c.y, R, R, 0.0f, a0, a1, true);
        g.setColour (Colour (0xff0d0f11));
        g.strokePath (track, PathStrokeType (mini ? 2.5f : 3.5f, PathStrokeType::curved, PathStrokeType::rounded));
        const bool bip = s.getProperties().contains ("bipolar");
        const float from = bip ? (a0 + a1) * 0.5f : a0;
        if (std::abs (ang - from) > 0.01f)
        {
            Path v; v.addCentredArc (c.x, c.y, R, R, 0.0f, jmin (from, ang), jmax (from, ang), true);
            if (brass) g.setColour (Pal::brass);
            else g.setGradientFill (ColourGradient (Pal::red, c.x - R, c.y, Pal::amber, c.x + R, c.y, false));
            g.strokePath (v, PathStrokeType (mini ? 2.0f : 2.5f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        if (! mini)
        {
            g.setColour (Colours::white.withAlpha (0.9f));
            g.fillEllipse (Rectangle<float> (5.2f, 5.2f).withCentre (c.getPointOnCircumference (R, ang)));
        }

        // body (cached)
        const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto& img = body (d, ps);
        g.setOpacity (1.0f);
        g.drawImage (img, Rectangle<float> (d + 30.0f, d + 30.0f).withCentre (c), RectanglePlacement::stretchToFit);

        // pointer
        const auto p0 = c.getPointOnCircumference (d * 0.06f, ang), p1 = c.getPointOnCircumference (d * 0.40f, ang);
        const Colour pc = brass ? Pal::brass : Colour (0xffff8a4c);
        g.setColour (pc.withAlpha (0.35f));
        g.drawLine (Line<float> (p0, p1), d > 80 ? 7.0f : 5.0f);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.drawLine (Line<float> (p0, p1), d > 80 ? 4.5f : 3.6f);
        g.setColour (pc);
        g.drawLine (Line<float> (p0, p1), d > 80 ? 3.2f : 2.6f);
    }

private:
    std::map<std::pair<int, int>, juce::Image> bodies;

    const juce::Image& body (float d, float ps)
    {
        using namespace juce;
        const auto key = std::make_pair (roundToInt (d * 10.0f), roundToInt (ps * 100.0f));
        auto it = bodies.find (key);
        if (it != bodies.end()) return it->second;

        const float S = d + 30.0f;
        Image img (Image::ARGB, roundToInt (S * ps), roundToInt (S * ps), true);
        Graphics g (img);
        g.addTransform (AffineTransform::scale (ps));
        const Point<float> c (S * 0.5f, S * 0.5f);
        const float r = d * 0.5f;

        // shadow
        Path disc; disc.addEllipse (Rectangle<float> (d, d).withCentre (c.translated (0.0f, 3.0f)));
        DropShadow (Colours::black.withAlpha (0.65f), roundToInt (d * 0.18f + 4.0f), { 0, roundToInt (d * 0.08f) }).drawForPath (g, disc);

        // knurled skirt
        g.setColour (Colour (0xff2b2f35));
        g.fillEllipse (Rectangle<float> (d, d).withCentre (c));
        const int teeth = roundToInt (d * 3.4f);
        for (int i = 0; i < teeth; ++i)
        {
            const float a = MathConstants<float>::twoPi * (float) i / (float) teeth;
            const float lit = 0.5f + 0.5f * std::cos (a - 0.6f);           // light from the top
            g.setColour (Colour (0xff565d67).interpolatedWith (Colour (0xff8a929d), lit * 0.5f).withAlpha (0.9f));
            g.drawLine (Line<float> (c.getPointOnCircumference (r * 0.80f, a), c.getPointOnCircumference (r - 0.6f, a)), jmax (0.6f, d * 0.012f));
        }
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), c.x, c.y - r, Colours::black.withAlpha (0.42f), c.x, c.y + r, false));
        g.fillEllipse (Rectangle<float> (d, d).withCentre (c));
        g.setColour (Colour (0xff0a0b0d));
        g.drawEllipse (Rectangle<float> (d, d).withCentre (c), 1.0f);

        // turned top: anisotropic reflections (a conic sweep) plus fine lathe rings
        const float rt = r * 0.82f;
        const Colour stops[] = { Colour (0xff3a4048), Colour (0xff6b737e), Colour (0xff3c424a), Colour (0xff2f343b), Colour (0xff5e6670), Colour (0xff3a4048), Colour (0xff2c3137), Colour (0xff3a4048) };
        const float pos[] = { 0.0f, 0.12f, 0.26f, 0.40f, 0.55f, 0.68f, 0.82f, 1.0f };
        const int slices = 240;
        for (int i = 0; i < slices; ++i)
        {
            const float t = (float) i / (float) slices;
            int s = 0; while (s < 6 && t > pos[s + 1]) ++s;
            const float u = (t - pos[s]) / (pos[s + 1] - pos[s]);
            g.setColour (stops[s].interpolatedWith (stops[s + 1], u));
            const float a = MathConstants<float>::twoPi * t + 3.49f, b = a + MathConstants<float>::twoPi / (float) slices + 0.004f;
            Path w; w.addPieSegment (Rectangle<float> (rt * 2.0f, rt * 2.0f).withCentre (c), a, b, 0.0f);
            g.fillPath (w);
        }
        for (float rr = 2.0f; rr < rt; rr += 1.6f)
        {
            g.setColour (((int) (rr * 10.0f) % 2) ? Colours::white.withAlpha (0.035f) : Colours::black.withAlpha (0.03f));
            g.drawEllipse (Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 0.8f);
        }
        g.setColour (Colours::black.withAlpha (0.6f));
        g.drawEllipse (Rectangle<float> (rt * 2.0f, rt * 2.0f).withCentre (c), 1.0f);
        Path hi; hi.addCentredArc (c.x, c.y, rt - 0.8f, rt - 0.8f, 0.0f, -1.2f, 1.2f, true);
        g.setColour (Colours::white.withAlpha (0.22f));
        g.strokePath (hi, PathStrokeType (1.0f));
        Path lo; lo.addCentredArc (c.x, c.y, rt - 1.5f, rt - 1.5f, 0.0f, 2.2f, 4.1f, true);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.strokePath (lo, PathStrokeType (2.0f));

        return bodies.emplace (key, img).first->second;
    }
};
