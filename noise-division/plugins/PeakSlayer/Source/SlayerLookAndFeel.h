#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace Palette
{
    inline const juce::Colour bg      { 0xff070708 };
    inline const juce::Colour panel   { 0xff101013 };
    inline const juce::Colour edge    { 0xff2b2b32 };
    inline const juce::Colour steel   { 0xffc9c9d2 };   // logo silver: the "cold" accent
    inline const juce::Colour red     { 0xffff1420 };   // logo red: the "hot" accent
    inline const juce::Colour darkRed { 0xff6b1218 };
    inline const juce::Colour text    { 0xff9c9ca8 };
    inline const juce::Colour white   { 0xfff4f4f7 };
    inline const juce::Colour unlit   { 0xff3a3a42 };
}

// Guitar-amp style knobs and an LED toggle. `heat` (0..1) follows the CLIP knob:
// every accent fades from steel to red as it rises.
class SlayerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    float heat = 0.0f;

    SlayerLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId,       Palette::white);
        setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId,               Palette::text);

        const juce::Colour box { 0xff141418 };
        setColour (juce::ComboBox::backgroundColourId, box);
        setColour (juce::ComboBox::textColourId,       Palette::white);
        setColour (juce::ComboBox::outlineColourId,    Palette::edge);
        setColour (juce::ComboBox::arrowColourId,      Palette::steel);

        setColour (juce::PopupMenu::backgroundColourId,            box);
        setColour (juce::PopupMenu::textColourId,                  Palette::white);
        setColour (juce::PopupMenu::headerTextColourId,            Palette::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::red);
        setColour (juce::PopupMenu::highlightedTextColourId,       Palette::white);

        setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff1a1a1f));
        setColour (juce::TextButton::buttonOnColourId, Palette::red);
        setColour (juce::TextButton::textColourOffId,  Palette::white);
        setColour (juce::TextButton::textColourOnId,   Palette::white);

        setColour (juce::AlertWindow::backgroundColourId, box);
        setColour (juce::AlertWindow::textColourId,       Palette::white);
        setColour (juce::AlertWindow::outlineColourId,    Palette::red);

        setColour (juce::TextEditor::backgroundColourId,     juce::Colour (0xff0a0a0c));
        setColour (juce::TextEditor::textColourId,           Palette::white);
        setColour (juce::TextEditor::outlineColourId,        Palette::edge);
        setColour (juce::TextEditor::focusedOutlineColourId, Palette::red);
        setColour (juce::TextEditor::highlightColourId,      Palette::red.withAlpha (0.4f));
        setColour (juce::CaretComponent::caretColourId,      Palette::white);

        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1c1c21));
        setColour (juce::TooltipWindow::textColourId,       Palette::white);
        setColour (juce::TooltipWindow::outlineColourId,    Palette::red);
    }

    juce::Colour accent() const { return Palette::steel.interpolatedWith (Palette::red, heat); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        using namespace juce;
        const bool big     = (bool) s.getProperties().getWithDefault ("big", false);
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const bool ends    = (bool) s.getProperties().getWithDefault ("ends", false);

        auto area = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float size  = jmin (area.getWidth(), area.getHeight());
        const auto  c     = area.getCentre();
        const float outer = size * 0.5f - 2.0f;
        const float knobR = big ? outer * 0.60f : (ends ? outer * 0.62f : outer * 0.76f);
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float mid   = 0.5f * (startAngle + endAngle);
        const Colour acc  = accent();

        auto pt = [&] (float a, float r) { return Point<float> (c.x + r * std::sin (a), c.y - r * std::cos (a)); };

        // ---- scale ticks (0..11 numbers on the big knob)
        const int nTicks = big ? 12 : 11;
        for (int i = 0; i < nTicks; ++i)
        {
            const float t = (float) i / (float) (nTicks - 1);
            const float a = startAngle + t * (endAngle - startAngle);
            const bool lit = bipolar ? (t >= jmin (0.5f, pos) - 0.001f && t <= jmax (0.5f, pos) + 0.001f)
                                     : (t <= pos + 0.001f);
            const bool danger = big && i >= 9;

            const float r1 = knobR + (big ? 10.0f : 6.0f);
            const float r2 = r1 + (big ? 9.0f : 5.0f);

            g.setColour (lit ? acc : (danger ? Palette::darkRed : Palette::unlit));
            g.drawLine (Line<float> (pt (a, r1), pt (a, r2)), big ? 2.2f : 1.5f);

            if (big)
            {
                const bool eleven = (i == 11);
                const auto numArea = Rectangle<float> (30.0f, 20.0f).withCentre (pt (a, r2 + 14.0f));

                if (eleven && pos > 0.985f)
                {
                    g.setColour (Palette::red.withAlpha (0.35f));
                    g.fillEllipse (numArea.withSizeKeepingCentre (30.0f, 30.0f));
                }

                g.setFont (Font (FontOptions (eleven ? 19.0f : 13.0f, Font::bold)));
                g.setColour (eleven ? (pos > 0.985f ? Palette::white : Palette::red)
                                    : (lit ? Palette::white : Palette::text));
                g.drawText (String (i), numArea, Justification::centred, false);
            }
        }

        // ---- SOFT / HARD end labels on the shape knob
        if (ends)
        {
            g.setFont (Font (FontOptions (10.5f, Font::bold)));
            const auto l = pt (startAngle, knobR + 20.0f);
            const auto r = pt (endAngle,   knobR + 20.0f);
            g.setColour (pos < 0.495f ? Palette::white : Palette::text);
            g.drawText ("SOFT", Rectangle<float> (44.0f, 14.0f).withCentre ({ l.x - 4.0f, l.y + 6.0f }),
                        Justification::centred, false);
            g.setColour (pos > 0.505f ? Palette::white : Palette::text);
            g.drawText ("HARD", Rectangle<float> (44.0f, 14.0f).withCentre ({ r.x + 4.0f, r.y + 6.0f }),
                        Justification::centred, false);
        }

        // ---- glowing value arc
        {
            Path arc;
            const float rad = knobR + (big ? 4.0f : 2.5f);
            const float a0 = bipolar ? jmin (mid, angle) : startAngle;
            const float a1 = bipolar ? jmax (mid, angle) : angle;
            arc.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
            g.setColour (acc.withAlpha (0.22f + 0.15f * heat));
            g.strokePath (arc, PathStrokeType (big ? 9.0f : 5.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (acc);
            g.strokePath (arc, PathStrokeType (big ? 2.8f : 2.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // ---- knob body
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillEllipse (c.x - knobR - 2.0f, c.y - knobR + 3.0f, 2.0f * knobR + 4.0f, 2.0f * knobR + 4.0f);

        g.setGradientFill (ColourGradient (Colour (0xff4a4a52), c.x - knobR, c.y - knobR,
                                           Colour (0xff0b0b0d), c.x + knobR, c.y + knobR, false));
        g.fillEllipse (c.x - knobR, c.y - knobR, 2.0f * knobR, 2.0f * knobR);

        // knurled edge (rotates with the knob, so you can see it turn)
        const int ridges = big ? 56 : 36;
        for (int k = 0; k < ridges; ++k)
        {
            const float a = angle + (float) k * MathConstants<float>::twoPi / (float) ridges;
            g.setColour (Colours::black.withAlpha (0.6f));
            g.drawLine (Line<float> (pt (a, knobR * 0.88f), pt (a, knobR * 0.99f)), big ? 1.8f : 1.4f);
            g.setColour (Colours::white.withAlpha (0.09f));
            g.drawLine (Line<float> (pt (a + 0.035f, knobR * 0.88f), pt (a + 0.035f, knobR * 0.99f)), 1.0f);
        }

        // top cap
        const float capR = knobR * 0.80f;
        g.setGradientFill (ColourGradient (Colour (0xff5a5a63), c.x - capR * 0.4f, c.y - capR * 0.6f,
                                           Colour (0xff101012), c.x + capR * 0.6f, c.y + capR * 0.8f, true));
        g.fillEllipse (c.x - capR, c.y - capR, 2.0f * capR, 2.0f * capR);
        g.setColour (Colours::black.withAlpha (0.7f));
        g.drawEllipse (c.x - capR, c.y - capR, 2.0f * capR, 2.0f * capR, 1.5f);

        // pointer
        const auto p1 = pt (angle, capR * 0.30f);
        const auto p2 = pt (angle, capR * 0.96f);
        g.setColour (acc.withAlpha (0.35f));
        g.drawLine (Line<float> (p1, p2), big ? 9.0f : 6.0f);
        g.setColour (Palette::white);
        g.drawLine (Line<float> (p1, p2), big ? 4.0f : 2.8f);

        if (big)
        {
            g.setFont (Font (FontOptions (14.0f, Font::bold)));
            g.setColour (Palette::text);
            g.drawText ("CLIP", Rectangle<float> (70.0f, 16.0f).withCentre ({ c.x, c.y + outer * 0.82f }),
                        Justification::centred, false);
        }
    }

    int getSliderThumbRadius (juce::Slider& s) override
    {
        return s.getSliderStyle() == juce::Slider::TwoValueHorizontal ? 12 : 7;
    }

    // Thin track with the active part lit. The two-handle (filter) version gets
    // round handles carrying a high-pass / low-pass glyph.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                           float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider&) override
    {
        using namespace juce;
        const bool two = (style == Slider::TwoValueHorizontal);
        const float cy = (float) y + (float) h * 0.5f;
        const Colour acc = accent();

        g.setColour (Colour (0xff2c2c33));
        g.fillRoundedRectangle (Rectangle<float> ((float) x, cy - 1.5f, (float) w, 3.0f), 1.5f);

        const float a = two ? minPos : (float) x;
        const float b = two ? maxPos : sliderPos;
        if (b > a)
        {
            g.setColour (acc.withAlpha (0.25f));
            g.fillRoundedRectangle (Rectangle<float> (a, cy - 3.0f, b - a, 6.0f), 3.0f);
            g.setColour (acc);
            g.fillRoundedRectangle (Rectangle<float> (a, cy - 1.5f, b - a, 3.0f), 1.5f);
        }

        if (! two)
        {
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillEllipse (sliderPos - 7.0f, cy - 6.0f, 14.0f, 14.0f);
            g.setColour (Palette::white);
            g.fillEllipse (sliderPos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
            return;
        }

        auto handle = [&] (float px, bool highPass)
        {
            const float r = 11.0f;
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillEllipse (px - r, cy - r + 1.5f, 2.0f * r, 2.0f * r);
            g.setGradientFill (ColourGradient (Colour (0xff3c3c44), px, cy - r, Colour (0xff1b1b20), px, cy + r, false));
            g.fillEllipse (px - r, cy - r, 2.0f * r, 2.0f * r);
            g.setColour (acc.withAlpha (0.7f));
            g.drawEllipse (px - r, cy - r, 2.0f * r, 2.0f * r, 1.2f);

            Path p; // the filter's response shape
            if (highPass) { p.startNewSubPath (px - 4.5f, cy + 4.0f); p.quadraticTo (px - 4.0f, cy - 3.5f, px + 4.5f, cy - 3.5f); }
            else          { p.startNewSubPath (px - 4.5f, cy - 3.5f); p.quadraticTo (px + 4.0f, cy - 3.5f, px + 4.5f, cy + 4.0f); }
            g.setColour (Palette::white);
            g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        };

        handle (minPos, true);
        handle (maxPos, false);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        using namespace juce;
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        const Colour acc = accent();

        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (on ? acc.withAlpha (0.85f) : Palette::edge);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.5f);

        const float cx = r.getX() + r.getHeight() * 0.5f;
        const float cy = r.getCentreY();
        if (on)
        {
            g.setColour (acc.withAlpha (0.3f));
            g.fillEllipse (cx - 9.0f, cy - 9.0f, 18.0f, 18.0f);
        }
        g.setColour (on ? acc : Palette::unlit);
        g.fillEllipse (cx - 5.0f, cy - 5.0f, 10.0f, 10.0f);

        g.setFont (Font (FontOptions (14.0f, Font::bold)));
        g.setColour (on ? Palette::white : Palette::text);
        g.drawText (b.getButtonText(),
                    Rectangle<float> (r.getX() + r.getHeight(), r.getY(), r.getWidth() - r.getHeight() - 6.0f, r.getHeight()),
                    Justification::centred, false);
    }
};
