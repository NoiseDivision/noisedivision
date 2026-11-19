#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

// SPECTRAKILL look: bone and blood. Ivory bone plates with grain and cracks, knobs that are
// bone domes inside a ring of teeth with a blood notch, blood-red switches, typewriter labels.
// Everything is drawn in code; textures come from a fixed random seed so they never flicker.
namespace Pal
{
    const juce::Colour bg0        { 0xffe8dcc0 };   // background, centre
    const juce::Colour bg1        { 0xffcdbb92 };   // background, edges
    const juce::Colour plateTop   { 0xfff4ecd8 };
    const juce::Colour plateBot   { 0xffe3d5b4 };
    const juce::Colour edge       { 0xffa8925f };   // plate outline
    const juce::Colour tabEdge    { 0xff9d8760 };
    const juce::Colour paper      { 0xfffbf6ea };   // preset box, tags
    const juce::Colour bone       { 0xffefe4cb };
    const juce::Colour ink        { 0xff2a1712 };   // main text
    const juce::Colour inkSoft    { 0xff6b5640 };   // labels
    const juce::Colour blood      { 0xffa3121a };
    const juce::Colour bloodDark  { 0xff5e0509 };
    const juce::Colour bloodBright{ 0xffd01c24 };
    const juce::Colour slot       { 0xff100707 };   // the KILL readout's dark slot
    const juce::Colour crack      { 0x8c462d19 };   // rgba(70,45,25,.55)
}

// textures shared by the editor and the look-and-feel
namespace Bone
{
    // speckle noise: light and dark dots. Written straight into the pixels (fast), at 'px' pixels per dot
    inline juce::Image noise (int w, int h, float perDot, float alpha, juce::Colour dark, juce::Colour light, int seed, int px = 2)
    {
        juce::Image img (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        juce::Random rng (seed);
        const int count = (int) ((float) w * (float) h / ((float) px * (float) px * perDot));
        for (int i = 0; i < count; ++i)
        {
            const int x = rng.nextInt (w), y = rng.nextInt (h);
            const auto c = (rng.nextBool() ? dark : light).withAlpha (alpha * rng.nextFloat());
            for (int dy = 0; dy < px && y + dy < h; ++dy)
                for (int dx = 0; dx < px && x + dx < w; ++dx)
                    bd.setPixelColour (x + dx, y + dy, c);
        }
        return img;
    }

    // a crack: a wandering line that thins out, with a branch or two
    inline void crackPath (juce::Path& p, juce::Random& rng, float x, float y, float len, float ang, int branches,
                           std::vector<std::pair<juce::Line<float>, float>>& out, float width)
    {
        float px = x, py = y;
        for (int i = 0; i < 9; ++i)
        {
            const float step = len / 9.0f;
            ang += (rng.nextFloat() - 0.5f) * 0.9f;
            const float nx = px + std::cos (ang) * step, ny = py + std::sin (ang) * step;
            out.push_back ({ { px, py, nx, ny }, juce::jmax (0.4f, width * (1.0f - (float) i / 10.0f)) });
            if (branches > 0 && rng.nextFloat() < 0.22f)
                crackPath (p, rng, nx, ny, len * 0.45f, ang + (rng.nextBool() ? -1.0f : 1.0f) * (0.6f + rng.nextFloat() * 0.5f), branches - 1, out, width * 0.6f);
            px = nx; py = ny;
        }
    }
    inline void crack (juce::Graphics& g, juce::Random& rng, float x, float y, float len, float ang, float width, juce::Colour col, int branches = 2)
    {
        juce::Path unused;
        std::vector<std::pair<juce::Line<float>, float>> segs;
        crackPath (unused, rng, x, y, len, ang, branches, segs, width);
        g.setColour (col);
        for (const auto& [l, w] : segs)
        {
            juce::Path s; s.startNewSubPath (l.getStart()); s.lineTo (l.getEnd());
            g.strokePath (s, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // long faint fibres along the bone
    inline void fibres (juce::Graphics& g, juce::Random& rng, juce::Rectangle<float> r, float alpha)
    {
        for (int i = 0; i < (int) (r.getWidth() / 6.0f); ++i)
        {
            const float y = r.getY() + rng.nextFloat() * r.getHeight(), x = r.getX() + rng.nextFloat() * r.getWidth(), l = 20.0f + rng.nextFloat() * 90.0f;
            juce::Path p;
            p.startNewSubPath (x, y);
            p.quadraticTo (x + l * 0.5f, y + (rng.nextFloat() - 0.5f) * 4.0f, x + l, y + (rng.nextFloat() - 0.5f) * 3.0f);
            g.setColour (juce::Colour (90, 65, 35).withAlpha (alpha * 0.6f * rng.nextFloat()));
            g.strokePath (p, juce::PathStrokeType (0.6f));
        }
    }

    inline void splatter (juce::Graphics& g, juce::Random& rng, float x, float y, float r, int n, juce::Colour col)
    {
        for (int i = 0; i < n; ++i)
        {
            const float a = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            const float d = std::pow (rng.nextFloat(), 1.8f) * r, s = (1.0f - d / r) * 4.5f * rng.nextFloat() + 0.4f;
            const float alpha = 0.55f + rng.nextFloat() * 0.45f;
            const float sy = s * (0.6f + rng.nextFloat() * 0.5f);
            const float ex = x + std::cos (a) * d, ey = y + std::sin (a) * d;
            juce::Path e;
            e.addEllipse (-s, -sy, s * 2.0f, sy * 2.0f);
            g.setColour (col.withAlpha (alpha));
            g.fillPath (e, juce::AffineTransform::rotation (a).translated (ex, ey));
            if (rng.nextFloat() < 0.15f)
            {
                const float d2 = d + 10.0f + rng.nextFloat() * 18.0f;
                g.drawLine (ex, ey, x + std::cos (a) * d2, y + std::sin (a) * d2, s * 0.6f);
            }
        }
    }

    // a drop of blood running down from (x, y)
    inline juce::Path drip (float x, float y, float w, float len)
    {
        juce::Path p;
        p.startNewSubPath (x - w * 0.5f, y);
        p.lineTo (x + w * 0.5f, y);
        p.quadraticTo (x + w * 0.35f, y + len * 0.7f, x + w * 0.55f, y + len);
        p.addCentredArc (x, y + len, w * 0.55f, w * 0.55f, 0.0f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi * 3.0f);
        p.quadraticTo (x - w * 0.35f, y + len * 0.7f, x - w * 0.5f, y);
        p.closeSubPath();
        return p;
    }

    inline juce::Path chamfer (juce::Rectangle<float> r, float cut)
    {
        juce::Path p;
        p.startNewSubPath (r.getX() + cut, r.getY());
        p.lineTo (r.getRight(), r.getY());
        p.lineTo (r.getRight(), r.getBottom() - cut);
        p.lineTo (r.getRight() - cut, r.getBottom());
        p.lineTo (r.getX(), r.getBottom());
        p.lineTo (r.getX(), r.getY() + cut);
        p.closeSubPath();
        return p;
    }
}

class KillLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Typeface::Ptr oswMedium, oswSemi, oswBold, elite, body;

    KillLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, Pal::plateTop);
        setColour (juce::PopupMenu::textColourId, Pal::ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Pal::blood);
        setColour (juce::PopupMenu::highlightedTextColourId, Pal::paper);
        setColour (juce::ComboBox::backgroundColourId, Pal::paper);
        setColour (juce::ComboBox::outlineColourId, Pal::tabEdge);
        setColour (juce::ComboBox::textColourId, Pal::ink);
        setColour (juce::ComboBox::arrowColourId, Pal::blood);
        setColour (juce::AlertWindow::backgroundColourId, Pal::plateTop);
        setColour (juce::AlertWindow::textColourId, Pal::ink);
        setColour (juce::AlertWindow::outlineColourId, Pal::blood);
        setColour (juce::TextEditor::backgroundColourId, Pal::paper);
        setColour (juce::TextEditor::textColourId, Pal::ink);
        setColour (juce::TextEditor::outlineColourId, Pal::tabEdge);
        setColour (juce::TextEditor::focusedOutlineColourId, Pal::blood);
        setColour (juce::TextEditor::highlightColourId, Pal::blood.withAlpha (0.3f));
        setColour (juce::CaretComponent::caretColourId, Pal::ink);
        setColour (juce::TextButton::buttonColourId, Pal::plateTop);
        setColour (juce::TextButton::textColourOffId, Pal::ink);
        setColour (juce::Label::textColourId, Pal::ink);
    }

    // Sizes are CSS-like pixel sizes. JUCE's font height is the line height: Oswald's is 1.482x the
    // CSS size, Special Elite's is 1.0x. 'track' is letter spacing in pixels.
    juce::Font osw (float size, int weight = 700, float track = 0.0f) const
    {
        const float h = size * 1.482f;
        auto tf = weight >= 700 ? oswBold : weight >= 600 ? oswSemi : oswMedium;
        return juce::Font (juce::FontOptions (tf).withHeight (h)).withExtraKerningFactor (track / h);
    }
    juce::Font type (float size, float track = 0.0f) const          // typewriter (Special Elite)
    {
        return juce::Font (juce::FontOptions (elite).withHeight (size)).withExtraKerningFactor (track / size);
    }
    juce::Font text (float size) const { return juce::Font (juce::FontOptions (body).withHeight (size * 1.219f)); }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override { return body != nullptr ? body : juce::LookAndFeel_V4::getTypefaceForFont (f); }

    // ---------------------------------------------------------------- knobs
    // A ring of teeth (lit ones are blood red), a bone dome, a blood notch for the pointer.
    // The dome (shadow, grain, crack) is drawn once per knob and size, then reused.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float a0, float a1, juce::Slider& s) override
    {
        using namespace juce;
        const auto area = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float r = jmin (area.getWidth(), area.getHeight()) * 0.5f / 1.1f;      // teeth reach 1.08 r
        const auto c = area.getCentre();
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const float angle = a0 + pos * (a1 - a0);

        // teeth
        const int n = r > 40.0f ? 23 : 17;
        const float from = bipolar ? jmin (0.5f, pos) : 0.0f, to = bipolar ? jmax (0.5f, pos) : pos;
        const float tw = r * 0.12f, th = r * 0.2f;
        Path tooth;
        tooth.startNewSubPath (-tw * 0.5f, -th * 0.5f);
        tooth.lineTo (tw * 0.5f, -th * 0.5f);
        tooth.lineTo (tw * 0.42f, th * 0.2f);
        tooth.quadraticTo (0.0f, th * 0.75f, -tw * 0.42f, th * 0.2f);
        tooth.closeSubPath();
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) (n - 1), a = a0 + (a1 - a0) * t;
            const bool lit = to - from > 1.0e-3f && t >= from - 1.0e-4f && t <= to + 1.0e-4f;
            const auto xf = AffineTransform::rotation (a).translated (c.x + std::sin (a) * r * 0.98f, c.y - std::cos (a) * r * 0.98f);
            Path p (tooth); p.applyTransform (xf);
            const auto outer = Point<float> (0.0f, -th * 0.5f).transformedBy (xf), inner = Point<float> (0.0f, th * 0.5f).transformedBy (xf);
            if (lit)
            {
                g.setColour (Pal::bloodBright.withAlpha (0.22f));
                g.strokePath (p, PathStrokeType (jmax (2.0f, r * 0.07f), PathStrokeType::curved, PathStrokeType::rounded));
            }
            g.setGradientFill (ColourGradient (lit ? Colour (0xffe0242c) : Colour (0xffd9cba8), outer, lit ? Colour (0xff7a070d) : Colour (0xffa99470), inner, false));
            g.fillPath (p);
            g.setColour (Colour (40, 20, 10).withAlpha (0.5f));
            g.strokePath (p, PathStrokeType (0.6f));
        }

        // bone dome (cached)
        const float rb = r * 0.72f;
        const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
        const float pad = rb * 0.45f;
        const String key = s.getName() + "|" + String (roundToInt (rb * 10.0f)) + "|" + String (roundToInt (ps * 100.0f));
        auto& dome = domes[key];
        if (! dome.isValid())
        {
            const float side = rb * 2.0f + pad * 2.0f;
            dome = Image (Image::ARGB, jmax (1, roundToInt (side * ps)), jmax (1, roundToInt (side * ps)), true);
            Graphics dg (dome);
            dg.addTransform (AffineTransform::scale (ps));
            const Point<float> dc (side * 0.5f, side * 0.5f);
            const auto circle = Rectangle<float> (rb * 2.0f, rb * 2.0f).withCentre (dc);
            Path disc; disc.addEllipse (circle);
            DropShadow (Colours::black.withAlpha (0.5f), roundToInt (rb * 0.3f), { 0, roundToInt (rb * 0.12f) }).drawForPath (dg, disc);
            ColourGradient grad (Colour (0xfffffaf0), dc.x - rb * 0.35f, dc.y - rb * 0.45f, Colour (0xffa8916a), dc.x + rb * 0.55f, dc.y + rb * 0.75f, true);
            grad.addColour (0.55, Colour (0xffe6d8b8));
            dg.setGradientFill (grad);
            dg.fillPath (disc);
            {
                Graphics::ScopedSaveState save (dg);
                dg.reduceClipRegion (disc);
                auto grain = Bone::noise (dome.getWidth(), dome.getHeight(), 9.0f, 0.16f, Colour (60, 40, 20), Colour (255, 250, 235), key.hashCode(), jmax (1, roundToInt (ps)));
                dg.drawImage (grain, Rectangle<float> (0, 0, side, side));
                Random rng (key.hashCode() + 3);
                Bone::crack (dg, rng, dc.x + rb * (0.1f + rng.nextFloat() * 0.4f), dc.y - rb, rb * 0.9f, 1.6f + rng.nextFloat() * 0.6f, 0.9f, Colour (70, 45, 25).withAlpha (0.5f), 1);
                dg.setColour (Colour (120, 85, 45).withAlpha (0.35f));
                dg.drawEllipse (Rectangle<float> (rb * 1.24f, rb * 1.24f).withCentre (dc), 1.0f);
            }
            dg.setColour (Colour (60, 35, 15).withAlpha (0.7f));
            dg.drawEllipse (circle, 1.2f);
        }
        const float side = rb * 2.0f + pad * 2.0f;
        g.drawImage (dome, Rectangle<float> (side, side).withCentre (c));

        // blood notch
        Path notch;
        notch.startNewSubPath (-rb * 0.07f, -rb * 0.3f);
        notch.lineTo (-rb * 0.11f, -rb * 0.95f);
        notch.lineTo (rb * 0.11f, -rb * 0.95f);
        notch.lineTo (rb * 0.07f, -rb * 0.3f);
        notch.closeSubPath();
        notch.applyTransform (AffineTransform::rotation (angle).translated (c));
        g.setColour (Colour (160, 10, 18).withAlpha (0.35f));
        g.strokePath (notch, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Pal::blood);
        g.fillPath (notch);
    }

    // ---------------------------------------------------------------- buttons: bone tabs, blood when on
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        using namespace juce;
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        if (b.getProperties().contains ("round"))
        {
            const float d = jmin (r.getWidth(), r.getHeight()) - 1.0f;
            const auto e = r.withSizeKeepingCentre (d, d);
            g.setColour (on ? Pal::blood : (over ? Pal::paper : Colour (0xfff3ead4)));
            g.fillEllipse (e);
            g.setColour (on ? Pal::bloodDark : Pal::inkSoft);
            g.drawEllipse (e, 1.2f);
            return;
        }
        const auto shape = Bone::chamfer (r, jmin (5.0f, r.getHeight() * 0.25f));
        if (on)
        {
            g.setColour (Pal::bloodBright.withAlpha (0.25f));
            g.strokePath (shape, PathStrokeType (4.0f));
            g.setGradientFill (ColourGradient (Colour (0xffc51820), 0, r.getY(), Colour (0xff6e0509), 0, r.getBottom(), false));
        }
        else
        {
            g.setGradientFill (ColourGradient (over ? Colour (0xfffaf3e2) : Colour (0xfff3ead4), 0, r.getY(), Colour (0xffd8c8a3), 0, r.getBottom(), false));
        }
        g.fillPath (shape);
        g.setColour (on ? Colour (0xffe65058) : (over ? Pal::inkSoft : Pal::tabEdge));
        g.strokePath (shape, PathStrokeType (1.0f));
        if (down) { g.setColour (Colours::black.withAlpha (0.12f)); g.fillPath (shape); }
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        if (b.getProperties().contains ("round")) return osw (15.0f, 700);
        return osw (juce::jmin (12.0f, (float) h * 0.46f), 600, 2.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        const bool on = b.getToggleState();
        g.setColour (on ? juce::Colour (0xfffff3e0) : (b.isEnabled() ? Pal::ink : Pal::inkSoft.withAlpha (0.45f)));
        // letter spacing adds space after the last letter too: nudge right to keep it centred
        const int nudge = b.getProperties().contains ("round") ? 0 : 1;
        g.drawText (b.getButtonText(), b.getLocalBounds().translated (nudge, 0), juce::Justification::centred, false);
    }

    // ---------------------------------------------------------------- combo (preset box)
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (Pal::paper); g.fillRect (r);
        g.setColour (box.isMouseOver (true) ? Pal::inkSoft : Pal::tabEdge); g.drawRect (r, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 6, ay - 4, ax + 6, ay - 4, ax, ay + 4);
        g.setColour (Pal::blood); g.fillPath (arrow);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return osw (15.0f, 500, 1.0f); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 34, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
    void drawLabel (juce::Graphics& g, juce::Label& l) override
    {
        if (auto* box = dynamic_cast<juce::ComboBox*> (l.getParentComponent()); box != nullptr && ! l.isBeingEdited())
        {
            g.setColour (Pal::ink);
            g.setFont (getComboBoxFont (*box));
            g.drawText (l.getText().toUpperCase(), l.getLocalBounds().withTrimmedLeft (4), juce::Justification::centredLeft, true);
            return;
        }
        juce::LookAndFeel_V4::drawLabel (g, l);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (Pal::plateTop);
        g.setColour (Pal::edge);
        g.drawRect (0, 0, w, h, 1);
    }
    juce::Font getPopupMenuFont() override { return osw (13.0f, 500, 0.5f); }

    // ---------------------------------------------------------------- help pop-ups
    juce::TextLayout tooltipLayout (const juce::String& t) const
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::topLeft);
        s.append (t, text (12.0f), Pal::ink);
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
        g.setColour (Pal::paper); g.fillRect (r);
        g.setColour (Pal::edge); g.drawRect (r, 1.0f);
        g.setColour (Pal::blood); g.fillRect (0.0f, 0.0f, 3.0f, (float) h);
        tooltipLayout (t).draw (g, r.withTrimmedLeft (15).withTrimmedRight (10).withTrimmedTop (8).withTrimmedBottom (8));
    }

    juce::Font getAlertWindowTitleFont() override { return osw (16.0f, 700, 1.0f); }
    juce::Font getAlertWindowMessageFont() override { return text (13.0f); }
    juce::Font getAlertWindowFont() override { return text (12.0f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }

private:
    std::map<juce::String, juce::Image> domes;
};
