#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace Pal
{
    inline const juce::Colour black    { 0xff111214 };   // Pitch Black: main background
    inline const juce::Colour charcoal { 0xff242529 };   // Industrial Charcoal: panels and controls
    inline const juce::Colour well     { 0xff0b0c0e };   // displays and slots
    inline const juce::Colour edge     { 0xff34363c };
    inline const juce::Colour red      { 0xffff3038 };   // KRUSH Red: primary accent
    inline const juce::Colour orange   { 0xffff7138 };   // Hot Orange: secondary highlights
    inline const juce::Colour silver   { 0xffc6c8cc };   // Metallic Silver: text and borders
    inline const juce::Colour steel    { 0xff777a82 };   // Steel Grey: secondary text

    inline juce::Colour band (int b)
    {
        // a gradient in four steps from KRUSH Red (band 1) to Hot Orange (band 4)
        return red.interpolatedWith (orange, (float) juce::jlimit (0, 3, b) / 3.0f);
    }
}

class KrushLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Typeface::Ptr regular, semibold;

    KrushLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId,       Pal::silver);
        setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId,  Pal::red.withAlpha (0.4f));
        setColour (juce::Label::textColourId,               Pal::silver);
        setColour (juce::TextEditor::backgroundColourId,    Pal::well);
        setColour (juce::TextEditor::textColourId,          Pal::silver);
        setColour (juce::TextEditor::focusedOutlineColourId, Pal::red);
        setColour (juce::CaretComponent::caretColourId,     Pal::silver);
        setColour (juce::TooltipWindow::backgroundColourId, Pal::charcoal);
        setColour (juce::TooltipWindow::textColourId,       Pal::silver);
        setColour (juce::TooltipWindow::outlineColourId,    Pal::red.withAlpha (0.6f));

        setColour (juce::ComboBox::backgroundColourId, Pal::well);
        setColour (juce::ComboBox::textColourId,       Pal::silver);
        setColour (juce::ComboBox::outlineColourId,    Pal::edge);
        setColour (juce::ComboBox::arrowColourId,      Pal::steel);
        setColour (juce::PopupMenu::backgroundColourId,            Pal::charcoal);
        setColour (juce::PopupMenu::textColourId,                  Pal::silver);
        setColour (juce::PopupMenu::headerTextColourId,            Pal::steel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Pal::red);
        setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);
        setColour (juce::AlertWindow::backgroundColourId, Pal::charcoal);
        setColour (juce::AlertWindow::textColourId,       Pal::silver);
        setColour (juce::AlertWindow::outlineColourId,    Pal::red);
        setColour (juce::TextEditor::outlineColourId,     Pal::edge);
        setColour (juce::TextEditor::highlightColourId,   Pal::red.withAlpha (0.4f));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.0f); }
    juce::Font getPopupMenuFont() override { return font (12.0f); }
    juce::Font getAlertWindowTitleFont() override { return font (15.0f); }
    juce::Font getAlertWindowMessageFont() override { return font (12.0f, false); }

    // ---- help pop-ups: dark box, red accent line, left-aligned text wrapped at about 300 px
    juce::TextLayout tooltipLayout (const juce::String& text) const
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::topLeft);
        s.append (text, font (12.0f, false), Pal::silver);
        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 300.0f);
        return tl;
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> area) override
    {
        const auto tl = tooltipLayout (text);
        const int w = (int) std::ceil (tl.getWidth()) + 26, h = (int) std::ceil (tl.getHeight()) + 16;
        const int x = pos.x > area.getCentreX() ? pos.x - w - 6 : pos.x + 14;
        const int y = pos.y + h + 24 > area.getBottom() ? pos.y - h - 6 : pos.y + 20;
        return juce::Rectangle<int> (x, y, w, h).constrainedWithin (area);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
        g.setColour (Pal::black);
        g.fillRect (r);
        g.setColour (Pal::edge);
        g.drawRect (r, 1.0f);
        g.setColour (Pal::red);
        g.fillRect (0.0f, 0.0f, 3.0f, (float) h);
        tooltipLayout (text).draw (g, r.withTrimmedLeft (15.0f).withTrimmedRight (10.0f).withTrimmedTop (8.0f).withTrimmedBottom (8.0f));
    }

    juce::Font font (float height, bool bold = true) const
    {
        auto tf = bold ? semibold : regular;
        return tf != nullptr ? juce::Font (juce::FontOptions (tf).withHeight (height)) : juce::Font (juce::FontOptions().withHeight (height));
    }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        if (semibold != nullptr && f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            return semibold;
        return LookAndFeel_V4::getTypefaceForFont (f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override { return font (juce::jmin (12.0f, buttonHeight * 0.6f)); }
    juce::Font getLabelFont (juce::Label& l) override { return font (l.getFont().getHeight()); }

    //==========================================================================
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        if (b.getProperties().contains ("round"))                 // small round button (help)
        {
            const auto c = r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), std::min (r.getWidth(), r.getHeight())).reduced (0.5f);
            g.setColour (on ? Pal::red : Pal::well.interpolatedWith (Pal::charcoal, over ? 0.7f : 0.3f));
            g.fillEllipse (c);
            g.setColour (on ? Pal::red : (over ? Pal::silver : Pal::steel));
            g.drawEllipse (c, 1.2f);
            if (down) { g.setColour (juce::Colours::black.withAlpha (0.2f)); g.fillEllipse (c); }
            return;
        }
        const auto accent = b.getProperties().contains ("accent") ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["accent"]) : Pal::red;

        g.setColour (on ? accent.withAlpha (0.20f) : Pal::well.interpolatedWith (Pal::charcoal, over ? 0.7f : 0.45f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (on ? accent : (over ? Pal::steel : Pal::edge));
        g.drawRoundedRectangle (r, 3.0f, on ? 1.4f : 1.0f);
        if (down) { g.setColour (juce::Colours::black.withAlpha (0.2f)); g.fillRoundedRectangle (r, 3.0f); }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool on = b.getToggleState();
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour ((on ? juce::Colours::white : Pal::silver).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.3f));
        auto r = b.getLocalBounds();
        const auto just = b.getProperties().contains ("left") ? juce::Justification::centredLeft : juce::Justification::centred;
        g.drawText (b.getButtonText(), r.reduced (b.getProperties().contains ("left") ? 10 : 2, 0), just, false);
    }

    //==========================================================================
    int getSliderThumbRadius (juce::Slider& s) override
    {
        if (s.getSliderStyle() == juce::Slider::TwoValueHorizontal) return 12;
        return s.isVertical() ? 8 : 7;
    }

    juce::Slider::SliderLayout getSliderLayout (juce::Slider& s) override
    {
        auto layout = LookAndFeel_V4::getSliderLayout (s);
        return layout;
    }

    // controls that do nothing in the current mode are drawn faded
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (s.isEnabled()) { drawLinear (g, x, y, w, h, pos, minPos, maxPos, style, s); return; }
        g.beginTransparencyLayer (0.3f);
        drawLinear (g, x, y, w, h, pos, minPos, maxPos, style, s);
        g.endTransparencyLayer();
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        if (s.isEnabled()) { drawRotary (g, x, y, w, h, pos, a0, a1, s); return; }
        g.beginTransparencyLayer (0.3f);
        drawRotary (g, x, y, w, h, pos, a0, a1, s);
        g.endTransparencyLayer();
    }

private:
    void drawLinear (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                     juce::Slider::SliderStyle style, juce::Slider& s)
    {
        using namespace juce;
        const bool fill = (bool) s.getProperties().getWithDefault ("fill", true);

        if (style == Slider::TwoValueHorizontal)
        {
            const float cy = (float) y + (float) h * 0.5f;
            g.setColour (Pal::edge);
            g.fillRoundedRectangle (Rectangle<float> ((float) x, cy - 1.5f, (float) w, 3.0f), 1.5f);
            g.setColour (Pal::red.withAlpha (0.25f));
            g.fillRoundedRectangle (Rectangle<float> (minPos, cy - 3.0f, maxPos - minPos, 6.0f), 3.0f);
            g.setColour (Pal::red);
            g.fillRoundedRectangle (Rectangle<float> (minPos, cy - 1.5f, maxPos - minPos, 3.0f), 1.5f);

            auto handle = [&] (float px, bool highPass)
            {
                const float r = 11.0f;
                g.setColour (Colours::black.withAlpha (0.5f));
                g.fillEllipse (px - r, cy - r + 1.5f, 2 * r, 2 * r);
                g.setGradientFill (ColourGradient (Colour (0xff3a3b41), px, cy - r, Colour (0xff1a1b1f), px, cy + r, false));
                g.fillEllipse (px - r, cy - r, 2 * r, 2 * r);
                g.setColour (Pal::red.withAlpha (0.8f));
                g.drawEllipse (px - r, cy - r, 2 * r, 2 * r, 1.2f);
                Path p;
                if (highPass) { p.startNewSubPath (px - 4.5f, cy + 4.0f); p.quadraticTo (px - 4.0f, cy - 3.5f, px + 4.5f, cy - 3.5f); }
                else          { p.startNewSubPath (px - 4.5f, cy - 3.5f); p.quadraticTo (px + 4.0f, cy - 3.5f, px + 4.5f, cy + 4.0f); }
                g.setColour (Pal::silver);
                g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
            };
            handle (minPos, true);
            handle (maxPos, false);
            return;
        }

        if (s.isVertical())
        {
            // fader: a narrow slot with a lit section and a rectangular cap
            const float cx = (float) x + (float) w * 0.5f;
            const Rectangle<float> slot (cx - 2.5f, (float) y, 5.0f, (float) h);
            g.setColour (Pal::well);
            g.fillRoundedRectangle (slot.expanded (1.0f, 2.0f), 3.0f);
            g.setColour (Pal::edge);
            g.drawRoundedRectangle (slot.expanded (1.0f, 2.0f), 3.0f, 1.0f);
            if (fill)
            {
                g.setColour (Pal::red.withAlpha (0.85f));
                g.fillRoundedRectangle (slot.withTop (pos), 2.0f);
            }
            const Rectangle<float> cap (cx - 13.0f, pos - 6.0f, 26.0f, 12.0f);
            g.setColour (Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), 2.0f);
            g.setGradientFill (ColourGradient (Colour (0xffd9dbdf), 0.0f, cap.getY(), Colour (0xff8d9097), 0.0f, cap.getBottom(), false));
            g.fillRoundedRectangle (cap, 2.0f);
            g.setColour (Pal::black.withAlpha (0.8f));
            g.fillRect (cap.getX() + 3.0f, pos - 0.75f, cap.getWidth() - 6.0f, 1.5f);
            return;
        }

        // horizontal (the mix slider)
        const float cy = (float) y + (float) h * 0.5f;
        const Rectangle<float> slot ((float) x, cy - 2.5f, (float) w, 5.0f);
        g.setColour (Pal::well);
        g.fillRoundedRectangle (slot.expanded (2.0f, 1.0f), 3.0f);
        g.setColour (Pal::edge);
        g.drawRoundedRectangle (slot.expanded (2.0f, 1.0f), 3.0f, 1.0f);
        g.setColour (Pal::red.withAlpha (0.85f));
        g.fillRoundedRectangle (slot.withRight (pos), 2.0f);
        const Rectangle<float> cap (pos - 6.0f, cy - 12.0f, 12.0f, 24.0f);
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), 2.0f);
        g.setGradientFill (ColourGradient (Colour (0xffd9dbdf), cap.getX(), 0.0f, Colour (0xff8d9097), cap.getRight(), 0.0f, false));
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (Pal::black.withAlpha (0.8f));
        g.fillRect (pos - 0.75f, cap.getY() + 3.0f, 1.5f, cap.getHeight() - 6.0f);
    }

    void drawRotary (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
    {
        using namespace juce;
        const auto area = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float r = jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const auto c = area.getCentre();
        const float angle = a0 + pos * (a1 - a0);
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);

        Path track, value;
        track.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, a0, a1, true);
        g.setColour (Pal::well);
        g.strokePath (track, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
        const float from = bipolar ? 0.5f * (a0 + a1) : a0;
        value.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, jmin (from, angle), jmax (from, angle), true);
        g.setColour (Pal::red);
        g.strokePath (value, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

        const float kr = r * 0.72f;
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillEllipse (c.x - kr, c.y - kr + 2.0f, 2 * kr, 2 * kr);
        g.setGradientFill (ColourGradient (Colour (0xff3d3e44), c.x, c.y - kr, Colour (0xff17181b), c.x, c.y + kr, false));
        g.fillEllipse (c.x - kr, c.y - kr, 2 * kr, 2 * kr);
        g.setColour (Pal::edge);
        g.drawEllipse (c.x - kr, c.y - kr, 2 * kr, 2 * kr, 1.0f);

        Path pointer;
        pointer.addRoundedRectangle (-1.5f, -kr + 4.0f, 3.0f, kr * 0.45f, 1.5f);
        g.setColour (Pal::silver);
        g.fillPath (pointer, AffineTransform::rotation (angle).translated (c));
    }
};
