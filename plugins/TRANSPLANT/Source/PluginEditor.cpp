#include "PluginEditor.h"
#include "BinaryData.h"

using namespace tpc;

namespace
{
    juce::String minus() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")); }
    juce::String signedDb (float v, int dec = 1)
    {
        const float a = std::abs (v) < (dec ? 0.05f : 0.5f) ? 0.0f : v;
        return (a > 0 ? "+" : a < 0 ? minus() : juce::String()) + juce::String (std::abs (a), dec) + " dB";
    }
    juce::String hzText (double f) { return f >= 1000 ? juce::String (f / 1000.0, f >= 10000 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }
    juce::String mmss (double s) { const int t = (int) s; return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2); }
    // what a section is matched to, for labels: "REF DROP 2", or the EDM balance
    juce::String refName (const TransplantProcessor::View& v, bool edm) { return edm ? juce::String ("EDM BALANCE") : "REF " + v.refLabel.fromFirstOccurrenceOf ("AUTO: ", false, false); }
    float tilt (int b) { return (float) ((b - tp::hzBin (1000.0)) * 4.5 / tp::kPerOct); }   // +4.5 dB/octave, so a typical mix reads flat
    bool isAudioFile (const juce::String& f)
    {
        for (auto* e : { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg" }) if (f.endsWithIgnoreCase (e)) return true;
        return false;
    }
    void styleSeg (juce::TextButton& b, bool coral = false) { b.getProperties().set ("kind", "seg"); if (coral) b.getProperties().set ("coral", true); }

    // the drawers' common look
    struct Drawer : public juce::Component
    {
        Drawer (TpLookAndFeel& l, juce::String t, juce::String s) : lnf (l), title (std::move (t)), sub (std::move (s)) {}
        void paintFrame (juce::Graphics& g)
        {
            auto r = getLocalBounds().toFloat().reduced (1);
            juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 24, { 0, 8 }).drawForRectangle (g, r.toNearestInt());
            g.setColour (juce::Colour (0xf20f181c)); g.fillRoundedRectangle (r, 12);
            g.setColour (cy.withAlpha (0.25f)); g.drawRoundedRectangle (r, 12, 1);
            g.setFont (lnf.title (13.0f, 0.2f)); g.setColour (cy); g.drawText (title, 18, 12, 200, 18, juce::Justification::centredLeft);
            g.setFont (lnf.title (10.0f, 0.14f)); g.setColour (dim); g.drawText (sub, 200, 12, getWidth() - 218, 18, juce::Justification::centredRight);
        }
        TpLookAndFeel& lnf; juce::String title, sub;
    };
}

//============================================================================== look and feel
TpLookAndFeel::TpLookAndFeel()
{
    oswald = juce::Typeface::createSystemTypefaceFor (BinaryData::OswaldSemiBold_ttf, BinaryData::OswaldSemiBold_ttfSize);
    mont = juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratMedium_ttf, BinaryData::MontserratMedium_ttfSize);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff0e1619));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, cy.withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId, hot);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::TooltipWindow::textColourId, text);
}

void TpLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    using namespace juce;
    const auto c = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
    const float r = (float) jmin (w, h) * 0.5f - 5, ang = a0 + pos * (a1 - a0);
    const bool bip = s.getMinimum() < 0;
    const float from = bip ? a0 + (a1 - a0) * (float) s.valueToProportionOfLength (0.0) : a0;
    Path tr; tr.addCentredArc (c.x, c.y, r, r, 0, a0, a1, true);
    g.setColour (Colour (0xff26343a)); g.strokePath (tr, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));
    if (std::abs (ang - from) > 0.01f)
    {
        Path v; v.addCentredArc (c.x, c.y, r, r, 0, jmin (from, ang), jmax (from, ang), true);
        g.setColour (cy.withAlpha (0.25f)); g.strokePath (v, PathStrokeType (8.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (cy); g.strokePath (v, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    const float k = r - 8;
    g.setGradientFill (ColourGradient (Colour (0xff39454a), c.x, c.y - k, Colour (0xff12191c), c.x, c.y + k, false));
    g.fillEllipse (Rectangle<float> (k * 2, k * 2).withCentre (c));
    g.setColour (Colour (0xff2a383e)); g.drawEllipse (Rectangle<float> (k * 2, k * 2).withCentre (c), 1);
    g.setColour (hot); g.drawLine ({ c.getPointOnCircumference (k * 0.3f, ang), c.getPointOnCircumference (k * 0.85f, ang) }, 2.2f);
}

void TpLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    using namespace juce;
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const String kind = b.getProperties()["kind"].toString();
    const Colour acc = b.getProperties()["coral"] ? co : cy;
    if (kind == "tabLeft")
    {
        Path p; p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 8, 8, true, false, true, false);
        g.setColour (on ? Colour (0xff15252a) : panel2.withAlpha (over ? 1.0f : 0.92f)); g.fillPath (p);
        g.setColour (on ? acc.withAlpha (0.45f) : line); g.strokePath (p, PathStrokeType (1));
        return;
    }
    if (kind == "tab" || kind == "tabUp")
    {
        Path p; const bool up = kind == "tabUp";
        p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 8, 8, ! up, ! up, up, up);
        g.setColour (on ? Colour (0xff15252a) : panel2.withAlpha (over ? 1.0f : 0.92f)); g.fillPath (p);
        g.setColour (on ? acc.withAlpha (0.45f) : line); g.strokePath (p, PathStrokeType (1));
        return;
    }
    if (kind == "fill")
    {
        g.setColour (on ? acc : acc.withAlpha (over ? 0.95f : 0.85f)); g.fillRoundedRectangle (r, 7);
        return;
    }
    if (kind == "seg" && ! on)
    {
        g.setColour (Colour (down ? 0xff0d1416 : over ? 0xff19252a : 0xff111a1e)); g.fillRoundedRectangle (r, 6);
        g.setColour (line); g.drawRoundedRectangle (r, 6, 1);
        return;
    }
    if (on)
    {
        g.setColour (acc.withAlpha (0.14f)); g.fillRoundedRectangle (r, 7);
        g.setColour (acc.withAlpha (0.7f)); g.drawRoundedRectangle (r, 7, 1);
        return;
    }
    g.setColour (Colour (down ? 0xff0d1416 : over ? 0xff19252a : 0xff121b1f)); g.fillRoundedRectangle (r, 7);
    g.setColour (over ? line.brighter (0.3f) : line); g.drawRoundedRectangle (r, 7, 1);
}

void TpLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const juce::String kind = b.getProperties()["kind"].toString();
    const bool on = b.getToggleState();
    const juce::Colour acc = b.getProperties()["coral"] ? co : cy;
    const float size = b.getProperties().getWithDefault ("size", 11.0f);
    g.setFont (title (size, 0.16f));
    juce::Colour c = ! b.isEnabled() ? faint : kind == "fill" ? juce::Colour (0xff06201d) : on ? acc : text;
    if (kind == "tab" || kind == "tabUp" || kind == "tabLeft") c = on ? acc : text;
    g.setColour (c);
    if (kind == "tabLeft")
    {
        juce::Graphics::ScopedSaveState ss (g);
        const auto cc = b.getLocalBounds().toFloat().getCentre();
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, cc.x, cc.y));
        g.drawText (b.getButtonText(), juce::Rectangle<float> ((float) b.getHeight(), (float) b.getWidth()).withCentre (cc), juce::Justification::centred, false);
        return;
    }
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

void TpLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (panel); g.fillRoundedRectangle (r, 7);
    g.setColour (line); g.drawRoundedRectangle (r, 7, 1);
    juce::Path p; const float cx = (float) w - 14, cy_ = (float) h * 0.5f;
    p.addTriangle (cx - 4, cy_ - 2.5f, cx + 4, cy_ - 2.5f, cx, cy_ + 3.5f);
    g.setColour (cy); g.fillPath (p);
}
void TpLookAndFeel::positionComboBoxText (juce::ComboBox& b, juce::Label& l) { l.setBounds (6, 0, b.getWidth() - 26, b.getHeight()); l.setFont (body (12.5f)); }
void TpLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff0e1619));
    g.setColour (line); g.drawRect (0, 0, w, h);
}
juce::Rectangle<int> TpLookAndFeel::getTooltipBounds (const juce::String& t, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::AttributedString s; s.append (t, body (12.5f), text); s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl; tl.createLayout (s, 300.0f);
    return juce::Rectangle<int> (pos.x + 12, pos.y + 18, (int) std::ceil (tl.getWidth()) + 26, (int) std::ceil (tl.getHeight()) + 18).constrainedWithin (parent);
}
void TpLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& t, int w, int h)
{
    g.setColour (juce::Colour (0xf20a1012)); g.fillRoundedRectangle (0, 0, (float) w, (float) h, 6);
    g.setColour (cy.withAlpha (0.45f)); g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 6, 1);
    juce::AttributedString s; s.append (t, body (12.5f), text); s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl; tl.createLayout (s, (float) w - 26);
    tl.draw (g, { 13, 9, (float) w - 26, (float) h - 18 });
}

//============================================================================== knob, drag box
Knob::Knob (TransplantProcessor& p, const juce::String& id, const juce::String& l, TpLookAndFeel& lf, bool attach) : lnf (lf), label (l)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    if (attach) att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, id, slider);
    else if (auto* prm = p.apvts.getParameter (id))
    {
        const auto r = prm->getNormalisableRange();
        slider.setNormalisableRange ({ (double) r.start, (double) r.end,
            [r] (double, double, double v) { return (double) r.convertFrom0to1 ((float) v); },
            [r] (double, double, double v) { return (double) r.convertTo0to1 ((float) v); },
            [r] (double, double, double v) { return (double) r.snapToLegalValue ((float) v); } });
        slider.textFromValueFunction = [prm] (double v) { return prm->getText (prm->convertTo0to1 ((float) v), 0); };
        slider.setValue (prm->convertFrom0to1 (prm->getValue()), juce::dontSendNotification);
    }
    if (auto* prm = p.apvts.getParameter (id)) slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}
void Knob::resized() { slider.setBounds (getWidth() / 2 - 29, 0, 58, 58); }
void Knob::paint (juce::Graphics& g)
{
    g.setFont (lnf.title (10.5f, 0.16f)); g.setColour (text);
    g.drawText (label, 0, 60, getWidth(), 14, juce::Justification::centred);
    g.setFont (lnf.body (11.0f)); g.setColour (hot);
    g.drawText (slider.getTextFromValue (slider.getValue()), 0, 75, getWidth(), 14, juce::Justification::centred);
}

DragBox::DragBox (juce::RangedAudioParameter& p, const juce::String& l, TpLookAndFeel& lf)
    : prm (p), label (l), lnf (lf), att (p, [this] (float v) { value = v; repaint(); })
{
    att.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}
void DragBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (panel); g.fillRoundedRectangle (r, 7);
    g.setColour (isMouseOverOrDragging() ? line.brighter (0.3f) : line); g.drawRoundedRectangle (r, 7, 1);
    // a small knob: the arc from 0 dB (top) to the value
    {
        const float d = (float) getHeight() - 6.0f, cx = 5.0f + d / 2, cy = getHeight() / 2.0f, rad = d / 2 - 1.5f;
        const auto rng = prm.getNormalisableRange();
        const float a0 = juce::MathConstants<float>::pi * 1.25f, span = juce::MathConstants<float>::pi * 2.5f;
        const float norm = rng.convertTo0to1 (value), zero = rng.convertTo0to1 (juce::jlimit (rng.start, rng.end, 0.0f));
        juce::Path bgArc; bgArc.addCentredArc (cx, cy, rad, rad, 0, -a0, -a0 + span, true);
        g.setColour (line.brighter (0.2f)); g.strokePath (bgArc, juce::PathStrokeType (2.0f));
        juce::Path arc; arc.addCentredArc (cx, cy, rad, rad, 0, -a0 + span * juce::jmin (zero, norm), -a0 + span * juce::jmax (zero, norm), true);
        g.setColour (tpc::cy); g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float ang = -a0 + span * norm;
        g.setColour (text); g.drawLine (cx, cy, cx + std::sin (ang) * (rad - 3), cy - std::cos (ang) * (rad - 3), 1.6f);
    }
    g.setFont (lnf.title (10.0f, 0.18f)); g.setColour (dim); g.drawText (label, getHeight() + 6, 0, 40, getHeight(), juce::Justification::centredLeft);
    g.setFont (lnf.body (12.0f)); g.setColour (text); g.drawText (prm.getCurrentValueAsText(), getHeight() + 24, 0, getWidth() - getHeight() - 32, getHeight(), juce::Justification::centredRight);
}
void DragBox::mouseDown (const juce::MouseEvent&) { start = value; att.beginGesture(); }
void DragBox::mouseDrag (const juce::MouseEvent& e)
{
    const float per = e.mods.isShiftDown() ? 0.02f : 0.1f;
    att.setValueAsPartOfGesture (juce::jlimit (prm.getNormalisableRange().start, prm.getNormalisableRange().end, std::round ((start - e.getDistanceFromDragStartY() * per) * 10.0f) / 10.0f));
}
void DragBox::mouseUp (const juce::MouseEvent&) { att.endGesture(); }
void DragBox::mouseDoubleClick (const juce::MouseEvent&) { att.setValueAsCompleteGesture (prm.convertFrom0to1 (prm.getDefaultValue())); }

//============================================================================== drawers
namespace
{
struct KeyDrawer : public Drawer, private juce::Timer
{
    KeyDrawer (TransplantProcessor& p, TpLookAndFeel& l)
        : Drawer (l, "KEY", "THE REFERENCE'S BASS NOTES, MOVED INTO YOUR KEY BEFORE COMPARING"), proc (p), zone (p, "keyZone", "KEY ZONE", l)
    {
        addAndMakeVisible (zone);
        zone.slider.setTooltip ("KEY ZONE: up to where the reference's low end is moved into your key. Above it (plus half an octave to fade) the reference is compared as it is.");
        const char* names[3] = { "AUTO", "MANUAL", "OFF" };
        auto* mode = p.apvts.getParameter ("keyMode");
        for (int i = 0; i < 3; ++i)
        {
            auto& b = modeBtn[i]; b.setButtonText (names[i]); styleSeg (b); addAndMakeVisible (b);
            b.onClick = [mode, i] { mode->setValueNotifyingHost (mode->convertTo0to1 ((float) i)); };
        }
        modeBtn[0].setTooltip ("AUTO: TRANSPLANT reads both keys and moves the reference's low end the shorter way round.");
        modeBtn[1].setTooltip ("MANUAL: you set the shift (if the key reading is wrong, or for a track without a clear key).");
        modeBtn[2].setTooltip ("OFF: compare the reference's low end as it is, like an ordinary match EQ.");
        modeAtt = std::make_unique<juce::ParameterAttachment> (*mode, [this] (float v) { for (int i = 0; i < 3; ++i) modeBtn[i].setToggleState ((int) std::lround (v) == i, juce::dontSendNotification); repaint(); });
        modeAtt->sendInitialUpdate();
        auto* sh = p.apvts.getParameter ("keyShift");
        for (auto* b : { &dn, &up }) { styleSeg (*b); addAndMakeVisible (*b); }
        dn.onClick = [sh] { sh->setValueNotifyingHost (sh->convertTo0to1 (juce::jlimit (-6.0f, 6.0f, sh->convertFrom0to1 (sh->getValue()) - 1))); };
        up.onClick = [sh] { sh->setValueNotifyingHost (sh->convertTo0to1 (juce::jlimit (-6.0f, 6.0f, sh->convertFrom0to1 (sh->getValue()) + 1))); };
        startTimerHz (10);
    }
    void timerCallback() override
    {
        const bool manual = (int) proc.apvts.getRawParameterValue ("keyMode")->load() == 1;
        dn.setVisible (manual); up.setVisible (manual);
        repaint();
    }
    void resized() override
    {
        zone.setBounds (400, 44, 90, 92);
        for (int i = 0; i < 3; ++i) modeBtn[i].setBounds (500, 44 + i * 30, 72, 26);
        dn.setBounds (300, 108, 26, 22); up.setBounds (330, 108, 26, 22);
    }
    void paint (juce::Graphics& g) override
    {
        paintFrame (g);
        const auto s = proc.snapshot();
        auto col = [&] (int x, const juce::String& l, const juce::String& big, const juce::String& small, juce::Colour c)
        {
            g.setFont (lnf.title (10.0f, 0.18f)); g.setColour (dim); g.drawText (l, x, 48, 120, 14, juce::Justification::centredLeft);
            g.setFont (lnf.title (28.0f, 0.04f)); g.setColour (c); g.drawText (big, x, 64, 140, 36, juce::Justification::centredLeft);
            g.setFont (lnf.body (10.5f)); g.setColour (dim); g.drawText (small, x, 100, 140, 14, juce::Justification::centredLeft);
        };
        col (18, "YOUR TRACK", s.mineKey.isNotEmpty() ? s.mineKey : juce::String ("--"), s.mineKey.isNotEmpty() ? "detected " + juce::String (juce::roundToInt (s.mineConf * 100)) + "%" : "not learned yet", text);
        juce::String rk = "--", rsmall = "no reference yet";
        if (! s.refs.empty()) { rk = s.refs[0].key; rsmall = s.refs.size() > 1 ? juce::String ((int) s.refs.size()) + " references, each shifted" : "detected " + juce::String (juce::roundToInt (s.refs[0].conf * 100)) + "%"; }
        if (! s.refs.empty() && s.refs[0].conf < 0.45f && s.refs.size() == 1) rsmall = "unsure (" + juce::String (juce::roundToInt (s.refs[0].conf * 100)) + "%): check MANUAL";
        col (150, "REFERENCE", rk, rsmall, co);
        const int mode = (int) proc.apvts.getRawParameterValue ("keyMode")->load();
        const int sh = s.refs.empty() ? 0 : s.refs[0].shift;
        col (290, "SHIFT", mode == 2 ? juce::String ("OFF") : (sh > 0 ? "+" : sh < 0 ? minus() : juce::String()) + juce::String (std::abs (sh)) + " ST",
             mode == 0 ? "the shorter way round" : mode == 1 ? "set by hand" : "low end compared as it is", cy);
        g.setFont (lnf.body (10.5f)); g.setColour (dim);
        g.drawFittedText ("Different keys put the bass notes at different frequencies. Without this, a match EQ boosts the reference's notes and cuts yours.",
                          18, 130, 370, 30, juce::Justification::topLeft, 2);
    }
    TransplantProcessor& proc; Knob zone;
    juce::TextButton modeBtn[3], dn { "-" }, up { "+" };
    std::unique_ptr<juce::ParameterAttachment> modeAtt;
};

// COPY TO: pick the sections that get this section's MATCH settings
struct CopyPicker : public juce::Component
{
    CopyPicker (TransplantProcessor& p, TpLookAndFeel& l, juce::String from, juce::StringArray names) : proc (p), lnf (l), source (std::move (from))
    {
        for (auto& n : names)
        {
            auto* b = boxes.add (new juce::TextButton (n)); styleSeg (*b); b->setClickingTogglesState (true);
            b->onClick = [this] { refresh(); }; addAndMakeVisible (b);
        }
        styleSeg (all); all.onClick = [this] { const bool on = ! allOn(); for (auto* b : boxes) b->setToggleState (on, juce::dontSendNotification); refresh(); };
        addAndMakeVisible (all);
        go.getProperties().set ("kind", "fill"); addAndMakeVisible (go);
        go.onClick = [this]
        {
            juce::StringArray to; for (auto* b : boxes) if (b->getToggleState()) to.add (b->getButtonText());
            if (to.isEmpty()) return;
            proc.undo.beginNewTransaction(); proc.copyMatchTo (source, to);
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
        };
        setSize (220, 74 + boxes.size() * 30 + 44);
        refresh();
    }
    bool allOn() const { for (auto* b : boxes) if (! b->getToggleState()) return false; return boxes.size() > 0; }
    void refresh()
    {
        int n = 0; for (auto* b : boxes) n += b->getToggleState();
        all.setButtonText (allOn() ? "NONE" : "ALL"); go.setEnabled (n > 0);
        go.setButtonText (n == 0 ? juce::String ("COPY") : "COPY TO " + juce::String (n));
    }
    void resized() override
    {
        int y = 48; for (auto* b : boxes) { b->setBounds (12, y, getWidth() - 24, 26); y += 30; }
        all.setBounds (12, y + 4, 70, 28); go.setBounds (88, y + 4, getWidth() - 100, 28);
    }
    void paint (juce::Graphics& g) override
    {
        g.setFont (lnf.title (11.0f, 0.16f)); g.setColour (tpc::cy); g.drawText ("COPY " + source + " TO", 12, 8, getWidth() - 24, 16, juce::Justification::centredLeft);
        g.setFont (lnf.body (10.5f)); g.setColour (tpc::dim); g.drawText ("its MATCH settings (points stay)", 12, 25, getWidth() - 24, 14, juce::Justification::centredLeft);
    }
    TransplantProcessor& proc; TpLookAndFeel& lnf; juce::String source;
    juce::OwnedArray<juce::TextButton> boxes; juce::TextButton all { "ALL" }, go { "COPY" };
};

struct MatchDrawer : public Drawer
{
    MatchDrawer (TransplantProcessor& p, TpLookAndFeel& l, std::function<void()> resetFn)
        : Drawer (l, "MATCH", "HOW THE CURVE IS MADE"), proc (p),
          amount (p, "amount", "AMOUNT", l, false), smooth (p, "smooth", "SMOOTH", l, false), range (p, "range", "RANGE", l, false),
          from (p, "from", "FROM", l, false), to (p, "to", "TO", l, false), out (p, "out", "GAIN", l, false), full (p, "fullness", "FULLNESS", l, false)
    {
        // the knobs show and change the section on the display; each section keeps its own settings
        Knob* ks[7] = { &amount, &smooth, &range, &from, &to, &out, &full };
        full.slider.setTooltip ("FULLNESS: gives back the bass and treble your ear stops hearing when music is quieter (the equal-loudness curves). The quieter this section is than your loudest one, the more it gets: breakdowns feel as full as the drop. Each section keeps its own.");
        for (int k = 0; k < 7; ++k)
        {
            addAndMakeVisible (*ks[k]);
            auto& s = ks[k]->slider;
            s.onDragStart = [this] { proc.undo.beginNewTransaction(); };
            s.onValueChange = [this, k, &s, kn = ks[k]] { kn->repaint(); if (! setting && view.isNotEmpty()) proc.setMatchValue (view, k, (float) s.getValue()); };
        }
        copyAll.getProperties().set ("kind", "plain"); addAndMakeVisible (copyAll);
        copyAll.setTooltip ("Copies the settings of this section (AMOUNT, SMOOTH, RANGE, FROM, TO, FULLNESS, GAIN) to the sections you pick. Your points stay.");
        copyAll.onClick = [this]
        {
            if (view.isEmpty()) return;
            juce::StringArray names; for (auto& w : proc.snapshot().views) if (w.name != view) names.add (w.name);
            auto* top = getTopLevelComponent();
            auto picker = std::make_unique<CopyPicker> (proc, lnf, view, names);
            juce::CallOutBox::launchAsynchronously (std::move (picker), top->getLocalArea (this, copyAll.getBounds()), top);
        };
        amount.slider.setTooltip ("AMOUNT: how much of the analysed curve is applied. Points you moved or added keep their exact values.");
        smooth.slider.setTooltip ("SMOOTH: left = surgical (every 1/24 octave, many points). Right = broad strokes: fewer, wider points. Points you moved stay.");
        range.slider.setTooltip ("RANGE: the most the curve may boost or cut anywhere.");
        from.slider.setTooltip ("FROM: no matching below this frequency (fades in over a third of an octave).");
        to.slider.setTooltip ("TO: no matching above this frequency. Set it low (around 250 Hz) to match only the low end.");
        out.slider.setTooltip ("GAIN: this section's level after the EQ. The curve is level-matched in the mids, so this is for fine matching. The whole song's INPUT and OUTPUT are at the bottom.");
        auto* ph = p.apvts.getParameter ("phase");
        const char* names[2] = { "LINEAR", "NATURAL" };
        for (int i = 0; i < 2; ++i)
        {
            auto& b = phase[i]; b.setButtonText (names[i]); styleSeg (b); addAndMakeVisible (b);
            b.onClick = [ph, i] { ph->setValueNotifyingHost ((float) i); };
        }
        phase[0].setTooltip ("LINEAR phase: no phase shift at all, the low end stays tight. About 0.18 s of latency (your DAW compensates).");
        phase[1].setTooltip ("NATURAL phase: like an analogue or normal EQ. Almost no latency, for tracking or live use.");
        phAtt = std::make_unique<juce::ParameterAttachment> (*ph, [this] (float v) { phase[0].setToggleState (v < 0.5f, juce::dontSendNotification); phase[1].setToggleState (v >= 0.5f, juce::dontSendNotification); });
        phAtt->sendInitialUpdate();
        reset.getProperties().set ("kind", "plain"); addAndMakeVisible (reset);
        reset.setTooltip ("Puts the points of the section you're looking at back to the analysis (your moves and additions are removed).");
        reset.onClick = std::move (resetFn);
    }
    void resized() override
    {
        int x = 14;
        for (auto* k : { &amount, &smooth, &range, &from, &to, &full }) { k->setBounds (x, 40, 76, 92); x += 78; }
        out.setBounds (x + 10, 40, 76, 92);
        phase[0].setBounds (getWidth() - 214, 50, 90, 26); phase[1].setBounds (getWidth() - 214, 80, 90, 26);
        reset.setBounds (getWidth() - 116, 50, 100, 26);
        copyAll.setBounds (getWidth() - 116, 80, 100, 26);
    }
    // the section the knobs belong to
    void setView (const juce::String& name)
    {
        view = name;
        sub = name.isEmpty() ? juce::String ("HOW THE CURVE IS MADE") : "SETTINGS FOR " + name;
        Knob* ks[7] = { &amount, &smooth, &range, &from, &to, &out, &full };
        setting = true;
        for (int k = 0; k < 7; ++k)
            if (! ks[k]->slider.isMouseButtonDown())
                ks[k]->slider.setValue (proc.matchValue (name, k), juce::dontSendNotification);
        setting = false;
        for (auto* k : ks) k->repaint();
        for (auto* k : ks) k->setEnabled (name.isNotEmpty());
        copyAll.setEnabled (name.isNotEmpty());
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        paintFrame (g);
        g.setColour (line); g.fillRect (out.getX() - 7, 46, 1, 80);
        g.setFont (lnf.title (10.0f, 0.18f)); g.setColour (dim);
        g.drawText ("PHASE", getWidth() - 214, 34, 90, 14, juce::Justification::centredLeft);
        g.setFont (lnf.body (10.5f));
        g.drawFittedText (juce::String (points) + " points. Drag, double-click, right-click.",
                          getWidth() - 116, 110, 104, 30, juce::Justification::topLeft, 2);
    }
    TransplantProcessor& proc; Knob amount, smooth, range, from, to, out, full;
    juce::TextButton phase[2], reset { "RESET POINTS" }, copyAll { "COPY TO..." };
    std::unique_ptr<juce::ParameterAttachment> phAtt;
    int points = 0; juce::String view; bool setting = false;
};

struct SectionsDrawer : public Drawer
{
    SectionsDrawer (TransplantProcessor& p, TpLookAndFeel& l)
        : Drawer (l, "SECTIONS", "EACH PART OF YOUR TRACK MATCHES ITS OWN PART OF THE REFERENCE"), proc (p)
    {
        for (auto* b : { &follow, &one }) { addAndMakeVisible (*b); b->setClickingTogglesState (true); }
        followAtt = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter ("follow"), follow);
        oneAtt = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter ("oneCurve"), one);
        follow.setTooltip ("FOLLOW PLAYHEAD: the curve changes with the section that's playing (by song position), crossfaded. Off: the section you click below is used for the whole song.");
        one.setTooltip ("ONE CURVE FOR ALL: forget the sections, one curve made from the whole track (its loudest half) against the whole reference.");
    }
    void update (const TransplantProcessor::Snapshot& s)
    {
        juce::StringArray names; for (size_t i = 0; i + 1 < s.views.size(); ++i) names.add (s.views[i].name);
        juce::StringArray refSecs; if (! s.refs.empty()) refSecs = s.refs[0].sections;
        if (names != rowNames || refSecs != refNames) { rowNames = names; refNames = refSecs; build(); }
        for (int i = 0; i < rows.size() && i < (int) s.views.size(); ++i)
        {
            auto* r = rows[i];
            const auto tgt = proc.sectionTarget (r->name);
            const int id = tgt == "AUTO" ? 1 : tgt == "WHOLE TRACK" ? 2 : refNames.indexOf (tgt) + 3;
            r->box.setSelectedId (juce::jmax (1, id), juce::dontSendNotification);
            r->byp.setToggleState (proc.sectionBypassed (r->name), juce::dontSendNotification);
            r->note = s.views[(size_t) i].refLabel;
        }
        repaint();
    }
    void build()
    {
        rows.clear();
        for (auto& n : rowNames)
        {
            auto* r = rows.add (new Row());
            r->name = n;
            r->box.addItem ("AUTO", 1); r->box.addItem ("WHOLE TRACK", 2);
            for (int i = 0; i < refNames.size(); ++i) r->box.addItem ("REF " + refNames[i], i + 3);
            r->box.onChange = [this, r]
            {
                proc.undo.beginNewTransaction();
                const int id = r->box.getSelectedId();
                proc.setSectionTarget (r->name, id == 1 ? juce::String ("AUTO") : id == 2 ? juce::String ("WHOLE TRACK") : refNames[id - 3]);
            };
            r->box.setTooltip ("Which part of the reference this section is matched to. AUTO: the same kind (a drop to a drop), the nearest stand-in if the reference has none.");
            r->byp.setButtonText ("BYPASS"); styleSeg (r->byp, true); r->byp.setClickingTogglesState (true);
            r->byp.onClick = [this, r] { proc.undo.beginNewTransaction(); proc.setSectionBypass (r->name, r->byp.getToggleState()); };
            r->byp.setTooltip ("No EQ in this section (flat).");
            addAndMakeVisible (r->box); addAndMakeVisible (r->byp);
        }
        const int h = 96 + juce::jmax (1, rows.size()) * 30;
        setSize (getWidth(), h);
        resized();
    }
    void resized() override
    {
        for (int i = 0; i < rows.size(); ++i)
        {
            rows[i]->box.setBounds (150, 42 + i * 30, 200, 24);
            rows[i]->byp.setBounds (getWidth() - 96, 42 + i * 30, 78, 24);
        }
        const int y = getHeight() - 42;
        follow.setBounds (18, y, 150, 26); one.setBounds (getWidth() - 178, y, 160, 26);
    }
    void paint (juce::Graphics& g) override
    {
        paintFrame (g);
        if (rows.isEmpty())
        {
            g.setFont (lnf.body (12.0f)); g.setColour (dim);
            g.drawText ("Learn your track first: its sections show up here.", 18, 44, getWidth() - 36, 24, juce::Justification::centredLeft);
        }
        for (int i = 0; i < rows.size(); ++i)
        {
            const int y = 42 + i * 30;
            const bool drop = rows[i]->name.startsWith ("DROP");
            g.setFont (lnf.title (12.5f, 0.12f)); g.setColour (drop ? cy : text);
            g.drawText (rows[i]->name, 18, y, 110, 24, juce::Justification::centredLeft);
            g.setColour (dim); g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")), 124, y, 20, 24, juce::Justification::centred);
            g.setFont (lnf.body (10.5f)); g.setColour (dim);
            g.drawText (rows[i]->note.startsWith ("AUTO") ? "matched to REF " + rows[i]->note.fromFirstOccurrenceOf (": ", false, false) : juce::String(), 362, y, getWidth() - 470, 24, juce::Justification::centredLeft);
        }
        g.setFont (lnf.body (10.5f)); g.setColour (dim);
        g.drawText ("changes by song position, crossfaded", 176, getHeight() - 42, 260, 26, juce::Justification::centredLeft);
    }
    struct Row { juce::String name, note; juce::ComboBox box; juce::TextButton byp; };
    TransplantProcessor& proc;
    juce::OwnedArray<Row> rows; juce::StringArray rowNames, refNames;
    juce::TextButton follow { "FOLLOW PLAYHEAD" }, one { "ONE CURVE FOR ALL" };
    std::unique_ptr<juce::ButtonParameterAttachment> followAtt, oneAtt;
};

struct MetersDrawer : public Drawer, public juce::SettableTooltipClient, private juce::Timer
{
    MetersDrawer (TransplantProcessor& p, TpLookAndFeel& l) : Drawer (l, "METERS", ""), proc (p)
    {
        setTooltip ("Your track after TRANSPLANT and the reference's matching part, measured side by side while your DAW plays (the reference plays along silently when you're not listening to it), at its own level. TONAL BALANCE: both in 1/3 octaves, inside a broad range typical of loud electronic masters (a guide, not a rule). Click to restart the true peak maxima.");
        startTimerHz (12);
    }
    void timerCallback() override { if (isVisible()) repaint(); }
    void mouseDown (const juce::MouseEvent&) override { proc.peakReset = true; }
    void paint (juce::Graphics& g) override
    {
        using namespace juce;
        paintFrame (g);
        const auto& y = proc.meterYours; const auto& r = proc.meterRef;
        const bool yv = y.valid.load(), rv = r.valid.load() && ! edm;
        const int W = getWidth(), cY = W - 216, cR = W - 144, cD = W - 72, cw = 64;
        g.setFont (lnf.title (12.5f, 0.14f));
        g.setColour (dim); g.drawText (section, 100, 12, W - 118, 18, Justification::centredRight);
        g.setColour (text); g.drawText ("YOURS", cY, 42, cw, 16, Justification::centredRight);
        g.setColour (edm ? faint : co); g.drawText ("REF", cR, 42, cw, 16, Justification::centredRight);
        g.setColour (dim); g.drawText ("DIFF", cD, 42, cw, 16, Justification::centredRight);
        int yy = 66;
        auto row = [&] (const String& name, const String& unit, float a, float b, bool okA, bool okB, int dec, bool showDiff = true)
        {
            g.setColour (line); g.fillRect (18, yy + 34, W - 36, 1);
            g.setFont (lnf.title (13.5f, 0.12f)); g.setColour (text); g.drawText (name, 18, yy + 1, 180, 16, Justification::centredLeft);
            g.setFont (lnf.body (11.0f)); g.setColour (dim); g.drawText (unit, 18, yy + 18, 180, 14, Justification::centredLeft);
            auto fmt = [dec] (float v)
            {
                const float q = dec == 0 ? (float) roundToInt (v) : std::round (v * 10.0f) / 10.0f;
                return (q < 0 ? String (CharPointer_UTF8 ("\xe2\x88\x92")) : String()) + (dec == 0 ? String (std::abs (roundToInt (q))) : String (std::abs (q), 1));
            };
            g.setFont (lnf.title (19.0f, 0.02f));
            g.setColour (okA ? text : faint); g.drawText (okA ? fmt (a) : String ("--"), cY, yy, cw, 34, Justification::centredRight);
            g.setColour (okB ? co : faint); g.drawText (okB ? fmt (b) : String ("--"), cR, yy, cw, 34, Justification::centredRight);
            if (showDiff && okA && okB)
            {
                const float d = a - b;
                g.setColour (std::abs (d) < (dec ? 0.5f : 3.0f) ? dim : hot);
                g.drawText ((d > 0 ? "+" : "") + fmt (d), cD, yy, cw, 34, Justification::centredRight);
            }
            yy += 40;
        };
        const float ySec = y.sec.load(), rSec = r.sec.load(), yTp = y.tp.load(), rTp = r.tp.load();
        row ("SHORT-TERM", "LUFS, last 3 s", y.st.load(), r.st.load(), yv, rv, 1);
        row ("THIS SECTION", "LUFS, integrated", ySec, rSec, ySec > -100, rv && rSec > -100, 1);
        row ("TRUE PEAK MAX", "dBTP, this section", yTp, rTp, yv && yTp > -100, rv && rTp > -100, 1);
        row ("PLR", "true peak minus loudness", yTp - ySec, rTp - rSec, yv && ySec > -100, rv && rSec > -100, 1);
        row ("WIDTH", "% side, last 3 s", y.width.load(), r.width.load(), yv, rv, 0);
        // ---- TONAL BALANCE: both tracks (1/3 octave, tilted +4.5 dB/oct, levelled over 100 Hz - 5 kHz) inside a guide range
        g.setFont (lnf.title (13.5f, 0.16f)); g.setColour (cy); g.drawText ("TONAL BALANCE", 18, yy + 6, 200, 18, Justification::centredLeft);
        g.setFont (lnf.body (11.0f)); g.setColour (dim); g.drawText ("this section, 1/3 octave", 150, yy + 6, 220, 18, Justification::centredLeft);
        {
            g.setFont (lnf.body (11.0f));
            float lx = (float) W - 18;
            for (auto [col, t] : { std::pair<Colour, String> { co, edm ? "TARGET" : "REF" }, { text, "YOURS" }, { you.withAlpha (0.55f), "BEFORE" }, { cy.withAlpha (0.5f), "EDM RANGE" } })
            {
                const float tw = GlyphArrangement::getStringWidth (lnf.body (11.0f), t);
                lx -= tw; g.setColour (dim); g.drawText (t, (int) lx, yy + 27, (int) tw + 2, 16, Justification::centredLeft);
                lx -= 18; g.setColour (col); g.fillRect (lx, (float) yy + 34, 13.0f, 2.5f); lx -= 14;
            }
        }
        yy += 52;
        const auto gr = Rectangle<float> (18, (float) yy, (float) W - 36, 172);
        balance (g, gr);
        yy += 172 + 18;
        g.setFont (lnf.body (11.0f)); g.setColour (dim);
        g.drawFittedText (edm ? String ("No reference: your track against the middle of the EDM range. True peaks restart with each section, or click here.")
                              : String ("The reference: its matching part, at its own level, playing along in sync. True peaks restart with each section, or click here."),
                          18, yy + 6, W - 36, 34, Justification::topLeft, 2, 1.0f);
    }
    // the graph: 30 Hz - 20 kHz (log); levels as they are (falling with frequency), both tracks levelled the same way
    void balance (juce::Graphics& g, juce::Rectangle<float> r)
    {
        using namespace juce;
        const float* lo = tp::edm::lo; const float* hi = tp::edm::hi;
        const double f0 = 30, f1 = 20000, yTop = 40, yBot = -30;
        auto untilt = [] (double f) { return -4.5 * std::log2 (f / 1000.0); };
        auto X = [&] (double f) { return r.getX() + r.getWidth() * (float) (std::log (f / f0) / std::log (f1 / f0)); };
        auto Y = [&] (double v) { return r.getY() + r.getHeight() * (float) ((yTop - jlimit (yBot, yTop, v)) / (yTop - yBot)); };
        auto bandAt = [&] (const float* t, double f) { return tp::edm::at (t, f) + untilt (f); };
        g.setColour (Colour (0xff0c1316)); g.fillRoundedRectangle (r.expanded (2), 4);
        // regions
        g.setFont (lnf.title (11.5f, 0.12f));
        const double edges[5] = { f0, 250, 2000, 6000, f1 };
        const char* names[4] = { "LOW", "LOW-MID", "HIGH-MID", "HIGH" };
        for (int k = 0; k < 4; ++k)
        {
            if (k > 0) { g.setColour (Colour (0xff22323a)); g.fillRect (X (edges[k]), r.getY(), 1.0f, r.getHeight()); }
            g.setColour (dim); g.drawText (names[k], Rectangle<float> (X (edges[k]), r.getY() + 4, X (edges[k + 1]) - X (edges[k]), 14).toNearestInt(), Justification::centred);
        }
        g.setFont (lnf.body (10.5f)); g.setColour (dim);
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            g.drawText (f >= 1000 ? String ((int) (f / 1000)) + "k" : String ((int) f), (int) X (f) - 16, (int) r.getBottom() + 4, 32, 13, Justification::centred);
        // the range: layers that narrow towards its middle, so it glows where a track usually sits
        const int N = 120;
        std::vector<float> bl (N + 1), bh (N + 1), bx (N + 1);
        for (int i = 0; i <= N; ++i)
        {
            const double f = f0 * std::pow (f1 / f0, i / (double) N);
            bx[(size_t) i] = X (f); bl[(size_t) i] = (float) bandAt (lo, f); bh[(size_t) i] = (float) bandAt (hi, f);
        }
        for (int layer = 0; layer < 6; ++layer)
        {
            const float s = 1.0f - layer * 0.17f;   // width of this layer, from the full band in
            Path band;
            for (int i = 0; i <= N; ++i) { const float m = 0.5f * (bl[(size_t) i] + bh[(size_t) i]), h = 0.5f * (bh[(size_t) i] - bl[(size_t) i]) * s;
                                           const auto pt = Point<float> (bx[(size_t) i], Y (m + h)); if (i == 0) band.startNewSubPath (pt); else band.lineTo (pt); }
            for (int i = N; i >= 0; --i) { const float m = 0.5f * (bl[(size_t) i] + bh[(size_t) i]), h = 0.5f * (bh[(size_t) i] - bl[(size_t) i]) * s;
                                           band.lineTo (bx[(size_t) i], Y (m - h)); }
            band.closeSubPath();
            g.setColour (cy.withAlpha (layer == 0 ? 0.07f : 0.055f)); g.fillPath (band);
            if (layer == 0) { g.setColour (cy.withAlpha (0.28f)); g.strokePath (band, PathStrokeType (1.0f)); }
        }
        if (! hasCurves) { g.setColour (faint); g.drawText ("learn your track and add a reference", r.toNearestInt(), Justification::centred); return; }
        // both tracks: power smoothed over about 1/3 octave, levelled on the tilted 100 Hz - 5 kHz mean
        auto smooth = [&] (const tp::Spec& s, std::vector<float>& out)
        {
            std::array<double, tp::kBins> pw {}; for (int b = 0; b < tp::kBins; ++b) pw[(size_t) b] = std::pow (10.0, s[(size_t) b] / 10.0);
            const double sg = tp::kPerOct / 6.0;
            out.assign ((size_t) tp::kBins, 0.0f); double m = 0; int n = 0;
            for (int b = 0; b < tp::kBins; ++b)
            {
                double acc = 0, w = 0;
                for (int k = std::max (0, b - (int) (3 * sg)); k <= std::min (tp::kBins - 1, b + (int) (3 * sg)); ++k) { const double ww = std::exp (-0.5 * ((k - b) / sg) * ((k - b) / sg)); acc += ww * pw[(size_t) k]; w += ww; }
                out[(size_t) b] = (float) tp::toDb (acc / w);
                if (b >= (int) tp::hzBin (100) && b <= (int) tp::hzBin (5000)) { m += out[(size_t) b] + tp::tiltDb (b); ++n; }
            }
            for (auto& o : out) o -= (float) (m / std::max (1, n));
        };
        auto line = [&] (const tp::Spec& s, Colour col, float w)
        {
            std::vector<float> v; smooth (s, v);
            Path p; bool started = false;
            for (int b = 0; b < tp::kBins; ++b)
            {
                const double f = 20.0 * std::pow (2.0, (b + 0.5) / tp::kPerOct);
                if (f < f0 || f > f1) continue;
                const auto pt = Point<float> (X (f), Y (v[(size_t) b]));
                if (! started) { p.startNewSubPath (pt); started = true; } else p.lineTo (pt);
            }
            g.setColour (col); g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
        };
        line (yoursBefore, you.withAlpha (0.5f), 1.2f); line (refSpec, co, 1.6f); line (yoursAfter, text, 2.0f);
    }
    TransplantProcessor& proc; juce::String section;
    tp::Spec yoursAfter {}, yoursBefore {}, refSpec {}; bool hasCurves = false, edm = false;
};

struct RefsDrawer : public Drawer
{
    RefsDrawer (TransplantProcessor& p, TpLookAndFeel& l, std::function<void()> addFn)
        : Drawer (l, "REFERENCES", ""), proc (p)
    {
        // match to: reference tracks, or the middle of the EDM tonal balance range
        auto* tp_ = p.apvts.getParameter ("target");
        styleSeg (useRef, true); styleSeg (useEdm);
        for (auto* b : { &useRef, &useEdm }) addAndMakeVisible (*b);
        useRef.onClick = [tp_] { tp_->setValueNotifyingHost (0.0f); };
        useEdm.onClick = [tp_] { tp_->setValueNotifyingHost (1.0f); };
        useRef.setTooltip ("Match to your reference tracks (up to three, averaged).");
        useEdm.setTooltip ("No reference: match your track to the middle of the EDM tonal balance range (the shaded range on METERS). Your references are kept for when you switch back.");
        tAtt = std::make_unique<juce::ParameterAttachment> (*tp_, [this] (float v)
        {
            edm = v > 0.5f; useRef.setToggleState (! edm, juce::dontSendNotification); useEdm.setToggleState (edm, juce::dontSendNotification);
            for (int i = 0; i < 3; ++i) { del[i].setVisible (! edm && i < (int) refs.size()); lis[i].setVisible (! edm && i < (int) refs.size()); }
            repaint();
        });
        tAtt->sendInitialUpdate();
        add.onClick = std::move (addFn); addAndMakeVisible (add);
        add.setTooltip ("Add a reference track (WAV, AIFF, FLAC, MP3, OGG). You can also drop files on the display.");
        for (int i = 0; i < 3; ++i)
        {
            auto& b = del[i]; b.setButtonText ("X"); addChildComponent (b);
            b.onClick = [this, i] { proc.removeReference (i); };
            b.setTooltip ("Remove this reference.");
            auto& l = lis[i]; l.setButtonText ("LISTEN"); styleSeg (l, true); addChildComponent (l);
            l.onClick = [this, i] { proc.setListenRef (i); };
            l.setTooltip ("The reference you hear with LISTEN: REF.");
        }
    }
    void update (const TransplantProcessor::Snapshot& s)
    {
        refs = s.refs;
        for (int i = 0; i < 3; ++i)
        {
            del[i].setVisible (! edm && i < (int) refs.size()); lis[i].setVisible (! edm && i < (int) refs.size());
            lis[i].setToggleState (i == proc.listenRef(), juce::dontSendNotification);
        }
        add.setEnabled (refs.size() < 3);
        repaint();
    }
    void resized() override
    {
        for (int i = 0; i < 3; ++i) { del[i].setBounds (getWidth() - 50, 44 + i * 34, 30, 26); lis[i].setBounds (getWidth() - 126, 44 + i * 34, 70, 26); }
        add.setBounds (18, getHeight() - 44, 170, 28);
        useEdm.setBounds (getWidth() - 128, 9, 110, 24); useRef.setBounds (getWidth() - 222, 9, 92, 24);
    }
    void paint (juce::Graphics& g) override
    {
        paintFrame (g);
        for (int i = 0; i < 3; ++i)
        {
            const int y = 44 + i * 34;
            g.setColour (panel); g.fillRoundedRectangle (18, (float) y, (float) getWidth() - 150, 26, 6);
            if (i < (int) refs.size())
            {
                g.setColour (co); g.fillEllipse (30, (float) y + 9, 8, 8);
                g.setFont (lnf.body (12.0f)); g.setColour (text); g.drawText (refs[(size_t) i].name, 46, y, 260, 26, juce::Justification::centredLeft, true);
                g.setFont (lnf.title (11.0f, 0.12f)); g.setColour (dim);
                g.drawText ((refs[(size_t) i].fileOk ? juce::String() : juce::String ("FILE MISSING   ")) + refs[(size_t) i].key + "   " + mmss (refs[(size_t) i].length) + "   " + juce::String (refs[(size_t) i].sections.size()) + " SECTIONS",
                            260, y, getWidth() - 420, 26, juce::Justification::centredRight);
            }
            else { g.setFont (lnf.body (11.0f)); g.setColour (faint); g.drawText ("empty", 46, y, 200, 26, juce::Justification::centredLeft); }
        }
        g.setFont (lnf.title (10.0f, 0.18f)); g.setColour (dim);
        g.drawText ("MATCH TO", getWidth() - 330, 9, 100, 24, juce::Justification::centredRight);
        g.setFont (lnf.body (10.5f)); g.setColour (dim);
        g.drawText ("Several references: each is moved into your key, then they're averaged.", 200, getHeight() - 44, getWidth() - 220, 28, juce::Justification::centredLeft);
        if (edm)
        {
            auto r = juce::Rectangle<float> (14, 40, (float) getWidth() - 28, 108);
            g.setColour (juce::Colour (0xf00f181c)); g.fillRoundedRectangle (r, 8);
            g.setColour (cy.withAlpha (0.5f)); g.drawRoundedRectangle (r, 8, 1);
            g.setFont (lnf.title (15.0f, 0.14f)); g.setColour (cy); g.drawText ("MATCHING TO THE EDM BALANCE", r.withTrimmedTop (18).withHeight (22).toNearestInt(), juce::Justification::centred);
            g.setFont (lnf.body (11.5f)); g.setColour (dim);
            g.drawFittedText ("No reference needed: each section of your track is matched to the middle of the EDM tonal balance range (the shaded range on METERS). Your references are kept for when you switch back to REFERENCE.",
                              r.withTrimmedTop (46).reduced (30, 0).toNearestInt(), juce::Justification::centredTop, 3, 1.0f);
        }
    }
    TransplantProcessor& proc;
    std::vector<TransplantProcessor::Snapshot::Ref> refs;
    juce::TextButton add { "+ ADD REFERENCE" }, del[3], lis[3], useRef { "REFERENCE" }, useEdm { "EDM BALANCE" };
    std::unique_ptr<juce::ParameterAttachment> tAtt; bool edm = false;
};
}

//============================================================================== display
std::unique_ptr<juce::Component> Display::createCopyPicker (const juce::String& from)
{
    juce::StringArray names; for (auto& w : proc.snapshot().views) if (w.name != from) names.add (w.name);
    return std::make_unique<CopyPicker> (proc, lnf, from, names);
}

Display::Display (TransplantProcessor& p, TpLookAndFeel& l, TransplantContent& o) : proc (p), lnf (l), owner (o)
{
    for (auto* t : { &keyTab, &secTab, &refTab }) { t->getProperties().set ("kind", "tab"); addAndMakeVisible (*t); }
    matchTab.getProperties().set ("kind", "tabUp"); addAndMakeVisible (matchTab);
    meterTab.getProperties().set ("kind", "tabLeft"); addAndMakeVisible (meterTab);
    meterTab.onClick = [this] { toggleMeters(); };
    meterTab.setTooltip ("METERS: loudness, true peak, PLR, width and tonal balance of your track and the reference, side by side.");
    keyTab.onClick = [this] { openDrawer (top == 1 ? 0 : 1); };
    secTab.onClick = [this] { openDrawer (top == 2 ? 0 : 2); };
    refTab.onClick = [this] { openDrawer (top == 3 ? 0 : 3); };
    matchTab.onClick = [this] { toggleMatch(); };
    keyTab.setTooltip ("KEY: your key, the reference's key, and how its low end is moved into yours.");
    secTab.setTooltip ("SECTIONS: which part of the reference each part of your track is matched to.");
    refTab.setTooltip ("REFERENCES: add or remove reference tracks (up to three).");
    matchTab.setTooltip ("MATCH: amount, smoothing, range, from/to, output and phase. Each section keeps its own settings.");
    keyDrawer = std::make_unique<KeyDrawer> (p, l);
    secDrawer = std::make_unique<SectionsDrawer> (p, l);
    refDrawer = std::make_unique<RefsDrawer> (p, l, [this] { owner.chooseReference(); });
    matchDrawer = std::make_unique<MatchDrawer> (p, l, [this]
    {
        if (! hasView) return;
        proc.undo.beginNewTransaction(); proc.resetPoints (v.name); selected = -1;
    });
    meterDrawer = std::make_unique<MetersDrawer> (p, l);
    for (auto* d : { keyDrawer.get(), secDrawer.get(), refDrawer.get(), matchDrawer.get(), meterDrawer.get() }) addChildComponent (d);
    pointX.getProperties().set ("kind", "plain"); pointX.getProperties().set ("size", 10.0f);
    pointX.onClick = [this]
    {
        if (selected < 0 || selected >= (int) nodes.size()) return;
        proc.undo.beginNewTransaction(); proc.removePoint (v.name, nodes[(size_t) selected].bin, nodes[(size_t) selected].user); selected = -1; pointX.setVisible (false);
    };
    pointX.setTooltip ("Remove this point.");
    addChildComponent (pointX);
    for (int i = 0; i < tp::place::count; ++i) hearBox.addItem (tp::place::names[i], i + 1);
    hearAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "hearOn", hearBox);
    hearBox.setTooltip ("HEAR ON: roughly how the track (and the reference, with REF) would sound on a club system, in a car, on a laptop, a phone or earbuds, or quietly, each at its usual level. MONO folds everything to mono, as on a mono club system or a single speaker. For your ears only: never in a bounce. Typical systems, not measurements of any product.");
    hearBox.onChange = [this] { repaint(); };
    addAndMakeVisible (hearBox);
    edmBtn.getProperties().set ("kind", "plain"); addChildComponent (edmBtn);
    edmBtn.setTooltip ("No reference? Match your track to the middle of the EDM tonal balance range instead. You can switch in REFERENCES.");
    edmBtn.onClick = [this] { proc.apvts.getParameter ("target")->setValueNotifyingHost (1.0f); };
    // LISTEN: your track or the reference (its matching part), level-matched
    auto* lp = p.apvts.getParameter ("listen");
    styleSeg (yoursBtn); styleSeg (refBtn, true);
    for (auto* b : { &yoursBtn, &refBtn }) addAndMakeVisible (*b);
    yoursBtn.onClick = [lp] { lp->setValueNotifyingHost (0.0f); };
    refBtn.onClick = [lp] { lp->setValueNotifyingHost (1.0f); };
    listenAtt = std::make_unique<juce::ParameterAttachment> (*lp, [this] (float v)
    {
        yoursBtn.setToggleState (v < 0.5f, juce::dontSendNotification); refBtn.setToggleState (v >= 0.5f, juce::dontSendNotification); repaint();
    });
    listenAtt->sendInitialUpdate();
    matchBtn.setClickingTogglesState (true); addAndMakeVisible (matchBtn);
    matchAtt = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter ("levelMatch"), matchBtn);
    yoursBtn.setTooltip ("Hear your track through TRANSPLANT.");
    refBtn.setTooltip ("Hear the reference instead, from the part that matches what's playing: in your drop you hear its drop, at the same moment. It plays while your DAW plays.");
    matchBtn.setTooltip ("LEVEL MATCH: the reference is turned down (or up) to your track's loudness (LUFS) in each section, so you compare tone, not volume. It starts from the analysis; once you've heard a couple of seconds of both in a section, it uses what it measured.");
    fftBuf.assign (2 << 13, 0.0f); win.resize (1 << 13);
    for (int i = 0; i < (1 << 13); ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (1 << 13));
    setRepaintsOnMouseActivity (false);
}

void Display::resized()
{
    const int W = getWidth(), H = getHeight();
    keyTab.setBounds (W / 2 - 190, 0, 80, 28); secTab.setBounds (W / 2 - 100, 0, 110, 28); refTab.setBounds (W / 2 + 20, 0, 130, 28);
    matchTab.setBounds (W / 2 - 55, H - 28, 110, 28);
    edmBtn.setBounds (zoneDonor().withTrimmedTop (186).withHeight (30).reduced (24, 0).toNearestInt());
    hearBox.setBounds (74, H - 27, 116, 22);
    yoursBtn.setBounds (W - 404, H - 30, 64, 24); refBtn.setBounds (W - 338, H - 30, 52, 24);
    matchBtn.setBounds (W - 276, H - 30, 108, 24);
    keyDrawer->setBounds (W / 2 - 300, 34, 600, 172);
    secDrawer->setBounds (W / 2 - 350, 34, 700, secDrawer->getHeight() > 0 ? secDrawer->getHeight() : 126);
    refDrawer->setBounds (W / 2 - 300, 34, 600, 196);
    matchDrawer->setBounds (W / 2 - 400, H - 28 - 150, 800, 148);
    meterTab.setBounds (W - 28, H / 2 - 120, 28, 110);
    meterDrawer->setBounds (W - 28 - 430, 36, 430, 552);
}

void Display::openDrawer (int which)
{
    top = which;
    keyDrawer->setVisible (top == 1); secDrawer->setVisible (top == 2); refDrawer->setVisible (top == 3);
    keyTab.setToggleState (top == 1, juce::dontSendNotification); secTab.setToggleState (top == 2, juce::dontSendNotification); refTab.setToggleState (top == 3, juce::dontSendNotification);
    repaint();
}
void Display::toggleMeters()
{
    const bool on = ! meterDrawer->isVisible();
    meterDrawer->setVisible (on); meterTab.setToggleState (on, juce::dontSendNotification);
    repaint();
}
void Display::toggleMatch()
{
    matchOpen = ! matchOpen; matchDrawer->setVisible (matchOpen); matchTab.setToggleState (matchOpen, juce::dontSendNotification);
    repaint();
}

juce::Rectangle<float> Display::plot() const { return { 52.0f, 40.0f, (float) getWidth() - 52 - 50, (float) getHeight() - 40 - 112 }; }
juce::Rectangle<float> Display::timeline() const { const auto p = plot(); return { p.getX() + 96, p.getBottom() + 34, p.getWidth() - 96, 20 }; }
float Display::xOfBin (double b) const { const auto p = plot(); return p.getX() + p.getWidth() * (float) ((b + 0.5) / tp::kBins); }
double Display::binOfX (float x) const { const auto p = plot(); return (x - p.getX()) / p.getWidth() * tp::kBins - 0.5; }
float Display::yOfCurve (double db) const
{
    const auto p = plot(); const double sc = juce::jmax (12.0, (double) (double) v.match[2]) * 1.5;
    return p.getCentreY() - (float) (db / sc) * p.getHeight() * 0.5f;
}
double Display::curveOfY (float y) const
{
    const auto p = plot(); const double sc = juce::jmax (12.0, (double) (double) v.match[2]) * 1.5;
    return (p.getCentreY() - y) / (p.getHeight() * 0.5) * sc;
}
float Display::yOfSpec (double db) const { const auto p = plot(); return p.getY() + p.getHeight() * 0.32f + (float) (specTop - db) * p.getHeight() * 0.86f / 66.0f; }

juce::Path Display::specPath (const tp::Spec& raw, bool fill) const
{
    juce::Path path; const auto p = plot();
    const auto s = tp::smoothCurve (raw, 0.3);   // a touch of smoothing, for reading (the analysis uses the real thing)
    for (int b = 0; b < tp::kBins; ++b)
    {
        const float x = xOfBin (b), y = juce::jlimit (p.getY(), p.getBottom(), yOfSpec (s[(size_t) b] + tilt (b)));
        if (b == 0) path.startNewSubPath (p.getX(), y);
        path.lineTo (x, y);
        if (b == tp::kBins - 1) path.lineTo (p.getRight(), y);
    }
    if (fill) { path.lineTo (p.getRight(), p.getBottom()); path.lineTo (p.getX(), p.getBottom()); path.closeSubPath(); }
    return path;
}

bool Display::emptyState() const { return snap.views.empty() || (snap.refs.empty() && ! snap.edm); }
juce::Rectangle<float> Display::zoneDonor() const { return juce::Rectangle<float> (360, 240).withCentre ({ getWidth() * 0.5f - 200, getHeight() * 0.48f }); }
juce::Rectangle<float> Display::zonePatient() const { return juce::Rectangle<float> (360, 240).withCentre ({ getWidth() * 0.5f + 200, getHeight() * 0.48f }); }

void Display::update (const TransplantProcessor::Snapshot& s, int vi)
{
    snap = s;
    hasView = vi >= 0 && vi < (int) snap.views.size();
    if (dragging < 0)
    {
        if (vi != view) selected = -1;
        view = vi;
        if (hasView) { v = snap.views[(size_t) vi]; nodes = v.nodes; curve = v.curve; }
        else { v = {}; nodes.clear(); curve = {}; }
    }
    specTop = -200;
    if (hasView)
        for (int b = 0; b < tp::kBins; ++b) specTop = juce::jmax (specTop, v.yours[(size_t) b] + tilt (b), v.hasRef ? v.ref[(size_t) b] + tilt (b) : -200.0f);
    if (specTop < -150) specTop = -20;
    static_cast<SectionsDrawer*> (secDrawer.get())->update (s);
    static_cast<RefsDrawer*> (refDrawer.get())->update (s);
    static_cast<MatchDrawer*> (matchDrawer.get())->points = (int) nodes.size();
    for (auto* b : { &refBtn, &matchBtn }) b->setEnabled (! snap.edm);
    edmBtn.setVisible (snap.refs.empty() && ! snap.edm);
    static_cast<MatchDrawer*> (matchDrawer.get())->setView (hasView && v.hasRef ? v.name : juce::String());
    {
        auto* md = static_cast<MetersDrawer*> (meterDrawer.get());
        md->section = hasView ? (v.name + (v.hasRef ? "  VS  " + refName (v, snap.edm) : juce::String())) : juce::String();
        md->edm = snap.edm;
        md->hasCurves = hasView && v.hasRef;
        if (md->hasCurves) { for (int b = 0; b < tp::kBins; ++b) md->yoursAfter[(size_t) b] = v.yours[(size_t) b] + v.curve[(size_t) b] + v.full[(size_t) b]; md->refSpec = v.refRaw; md->yoursBefore = v.yours; }
    }
    if (top == 2) secDrawer->setBounds (secDrawer->getBounds().withHeight (secDrawer->getHeight()));
    pointX.setVisible (selected >= 0 && selected < (int) nodes.size());
    repaint();
}

void Display::updateLive()
{
    // the output's spectrum, measured the same way as the analysis (so it lands on "yours + curve")
    const int N = 1 << 13; const int w = proc.liveWrite.load();
    for (int i = 0; i < N; ++i) fftBuf[(size_t) i] = proc.live[(w + i) % TransplantProcessor::kLive] * win[(size_t) i];
    std::fill (fftBuf.begin() + N, fftBuf.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data(), true);
    double ws = 0; for (float x : win) ws += x;
    const double norm = 4.0 / (ws * ws), fs = proc.rate.load();
    float peak = 0; for (int k = 0; k < N / 2; ++k) peak = juce::jmax (peak, fftBuf[(size_t) k]);
    if (peak < 1e-6f || ! proc.playing.load()) { liveValid = false; return; }
    for (int b = 0; b < tp::kBins; ++b)
    {
        const double f0 = 20.0 * std::pow (2.0, b / 24.0), f1 = 20.0 * std::pow (2.0, (b + 1) / 24.0);
        const int k0 = (int) std::ceil (f0 * N / fs), k1 = juce::jmin ((int) std::ceil (f1 * N / fs), N / 2);
        double pw;
        if (k1 > k0) { pw = 0; for (int k = k0; k < k1; ++k) pw += (double) fftBuf[(size_t) k] * fftBuf[(size_t) k]; pw /= (k1 - k0); }
        else { const double c = tp::binHz (b) * N / fs; const int k = juce::jmin ((int) c, N / 2 - 1); pw = (double) fftBuf[(size_t) k] * fftBuf[(size_t) k]; }
        const float db = (float) tp::toDb (pw * norm);
        liveSpec[(size_t) b] = liveValid ? liveSpec[(size_t) b] * 0.75f + db * 0.25f : db;
    }
    liveValid = true;
}

void Display::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto p = plot();
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (Colour (0xff15232a), r.getCentreX(), 0, panel, r.getCentreX(), r.getHeight() * 0.7f, true));
    g.fillRoundedRectangle (r, 12);
    g.setColour (line); g.drawRoundedRectangle (r.reduced (0.5f), 12, 1);
    // grid
    g.setFont (lnf.body (10.0f));
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = xOfBin (tp::hzBin (f));
        g.setColour (Colour (0xff1c2a30)); g.fillRect (x, p.getY(), 1.0f, p.getHeight());
        g.setColour (Colour (0xff5f6b71)); g.drawText (f >= 1000 ? String ((int) (f / 1000)) + "k" : String ((int) f), (int) x - 20, (int) p.getBottom() + 6, 40, 14, Justification::centred);
    }
    const double sc = jmax (12.0, (double) (double) v.match[2]) * 1.5;
    for (double d = -std::floor (sc / 6) * 6; d <= sc; d += 6)
    {
        const float y = yOfCurve (d);
        g.setColour (Colour (d == 0 ? 0xff2a3a42 : 0xff1c2a30)); g.fillRect (p.getX(), y, p.getWidth(), 1.0f);
        if (meterDrawer->isVisible() && y < meterDrawer->getBottom()) continue;
        g.setColour (Colour (0xff5f6b71)); g.drawText (d > 0 ? "+" + String ((int) d) : d < 0 ? minus() + String ((int) -d) : String ("0"), (int) p.getRight() + 8, (int) y - 7, 36, 14, Justification::centredLeft);
    }
    if (hasView)
    {
        // your track (filled), the reference in your key, its unshifted low end (KEY open), the output live
        auto yp = specPath (v.yours, true);
        g.setGradientFill (ColourGradient (you.withAlpha (0.20f), 0, p.getY() + p.getHeight() * 0.3f, you.withAlpha (0.02f), 0, p.getBottom(), false));
        g.fillPath (yp);
        g.setColour (you.withAlpha (0.85f)); g.strokePath (specPath (v.yours, false), PathStrokeType (1.3f));
        if (v.hasRef)
        {
            if (top == 1)
            {
                Path dashed; const float dl[] = { 3, 3 };
                PathStrokeType (1.2f).createDashedStroke (dashed, specPath (v.refRaw, false), dl, 2);
                g.setColour (co.withAlpha (0.4f)); g.fillPath (dashed);
            }
            g.setColour (co.withAlpha (0.92f)); g.strokePath (specPath (v.ref, false), PathStrokeType (1.5f));
        }
        if (liveValid) { g.setColour (Colours::white.withAlpha (0.22f)); g.strokePath (specPath (liveSpec, false), PathStrokeType (1.0f)); }
        // the curve: during a drag, drawn from the points right here (the real one follows from the analysis)
        tp::Spec c = curve;
        if (dragging >= 0)
        {
            const double range = (double) v.match[2];
            c = tp::limitBand (tp::limitRange (tp::curveThrough (nodes), range), v.match[3], v.match[4]);
        }
        if (v.hasRef)
        {
            Path cp;
            for (int b = 0; b < tp::kBins; ++b)
            {
                const float x = xOfBin (b), y = yOfCurve (c[(size_t) b]);
                if (b == 0) cp.startNewSubPath (p.getX(), y);
                cp.lineTo (x, y);
                if (b == tp::kBins - 1) cp.lineTo (p.getRight(), y);
            }
            const bool byp = proc.sectionBypassed (v.name);
            g.setColour (cy.withAlpha (byp ? 0.08f : 0.22f)); g.strokePath (cp, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour ((byp ? dim : hot).withAlpha (byp ? 0.5f : 1.0f)); g.strokePath (cp, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
            // with FULLNESS: what the EQ does in all
            float fmax = 0; for (float x : v.full) fmax = jmax (fmax, std::abs (x));
            if (fmax > 0.05f && ! byp)
            {
                Path tp_;
                for (int b = 0; b < tp::kBins; ++b)
                {
                    const float x = xOfBin (b), y = yOfCurve (c[(size_t) b] + v.full[(size_t) b]);
                    if (b == 0) tp_.startNewSubPath (p.getX(), y);
                    tp_.lineTo (x, y);
                    if (b == tp::kBins - 1) tp_.lineTo (p.getRight(), y);
                }
                Path dashed; const float dl[] = { 6, 4 };
                PathStrokeType (1.6f).createDashedStroke (dashed, tp_, dl, 2);
                g.setColour (hot.withAlpha (0.7f)); g.fillPath (dashed);
                g.setFont (lnf.title (9.5f, 0.16f)); g.setColour (hot.withAlpha (0.8f));
                g.drawText ("WITH FULLNESS", (int) xOfBin (tp::hzBin (35.0)), (int) yOfCurve (c[(size_t) tp::hzBin (35.0)] + v.full[(size_t) tp::hzBin (35.0)]) - 18, 110, 12, Justification::centredLeft);
            }
            // the points
            const float rr = nodes.size() > 60 ? 3.2f : nodes.size() > 30 ? 4.0f : 5.0f;
            for (int i = 0; i < (int) nodes.size(); ++i)
            {
                const auto& n = nodes[(size_t) i];
                const auto c0 = Point<float> (xOfBin (n.bin), jlimit (p.getY(), p.getBottom(), yOfCurve (n.db)));
                const bool on = i == selected || i == dragging, ov = i == hover;
                const float rad = on ? rr + 3 : ov ? rr + 1.5f : rr;
                if (on) { g.setColour (cy.withAlpha (0.3f)); g.drawEllipse (Rectangle<float> (rad * 2 + 12, rad * 2 + 12).withCentre (c0), 1.2f); }
                g.setColour (n.user ? Colours::white : on ? cy : Colour (0xff0f181c)); g.fillEllipse (Rectangle<float> (rad * 2, rad * 2).withCentre (c0));
                g.setColour (n.user ? Colours::white : cy); g.drawEllipse (Rectangle<float> (rad * 2, rad * 2).withCentre (c0), 1.6f);
            }
            // the selected point's values
            if (selected >= 0 && selected < (int) nodes.size())
            {
                const auto& n = nodes[(size_t) selected];
                const float x = xOfBin (n.bin), y = jlimit (p.getY(), p.getBottom(), yOfCurve (n.db));
                auto box = Rectangle<float> (208, 46).withPosition (x - 16, y + 18);
                if (box.getRight() > p.getRight()) box.setX (x - box.getWidth() + 16);
                if (box.getBottom() > p.getBottom()) box.setY (y - 18 - box.getHeight());
                g.setColour (Colour (0xf20f181c)); g.fillRoundedRectangle (box, 9);
                g.setColour (cy.withAlpha (0.35f)); g.drawRoundedRectangle (box, 9, 1);
                g.setFont (lnf.title (9.5f, 0.18f)); g.setColour (dim);
                g.drawText ("FREQ", (int) box.getX() + 12, (int) box.getY() + 6, 60, 12, Justification::centredLeft);
                g.drawText ("GAIN", (int) box.getX() + 88, (int) box.getY() + 6, 60, 12, Justification::centredLeft);
                g.setFont (lnf.title (16.0f, 0.04f)); g.setColour (text);
                g.drawText (hzText (tp::binHz (n.bin)), (int) box.getX() + 12, (int) box.getY() + 19, 80, 20, Justification::centredLeft);
                g.setColour (hot); g.drawText (signedDb (n.db), (int) box.getX() + 88, (int) box.getY() + 19, 80, 20, Justification::centredLeft);
                pointX.setBounds ((int) box.getRight() - 36, (int) box.getY() + 11, 26, 24);
            }
        }
        // legend
        g.setFont (lnf.body (10.0f));
        float lx = p.getX() + 8;
        for (auto [c, t] : { std::pair<Colour, String> { you, "YOUR TRACK" }, { co, snap.edm ? "TARGET: MIDDLE OF THE EDM RANGE" : "REFERENCE, IN YOUR KEY" }, { cy, "MATCH CURVE" }, { Colours::white.withAlpha (0.5f), "OUTPUT" } })
        {
            if (! v.hasRef && c == co) continue;
            g.setColour (c); g.fillRect (lx, p.getY() + 1, 14.0f, 2.0f);
            g.setColour (dim); g.drawText (t, (int) lx + 20, (int) p.getY() - 6, 200, 14, Justification::centredLeft);
            lx += 34 + juce::GlyphArrangement::getStringWidth (lnf.body (10.0f), t);
        }
        // the section shown, top right
        const bool listening = proc.apvts.getRawParameterValue ("listen")->load() > 0.5f;
        g.setFont (lnf.title (12.0f, 0.18f)); g.setColour (listening ? co : cy);
        if (meterDrawer->isVisible()) {}   // the drawer names the section itself
        else if (listening && v.hasRef)
            g.drawText ("LISTENING: REF " + v.refLabel.fromFirstOccurrenceOf ("AUTO: ", false, false), (int) p.getRight() - 360, (int) p.getY() - 7, 360, 16, Justification::centredRight);
        else
        g.drawText (v.name + (v.hasRef ? String (juce::CharPointer_UTF8 ("  \xe2\x86\x92  ")) + refName (v, snap.edm) : String()),
                    (int) p.getRight() - 360, (int) p.getY() - 7, 360, 16, Justification::centredRight);
    }
    // HEAR ON
    {
        const int h = (int) proc.apvts.getRawParameterValue ("hearOn")->load();
        g.setFont (lnf.title (10.0f, 0.18f)); g.setColour (h > 0 ? co : dim);
        g.drawText ("HEAR ON", 14, getHeight() - 30, 58, 24, Justification::centredLeft);
        if (h > 0)
        {
            g.setFont (lnf.body (10.5f)); g.setColour (co);
            const float trim = proc.hearTrimDb.load();
            g.drawText ("PREVIEW: NOT IN YOUR BOUNCE" + (trim < -0.1f ? "   TRIM " + String (trim, 1) + " dB" : String()), 198, getHeight() - 30, 320, 24, Justification::centredLeft);
        }
    }
    // LISTEN
    {
        const bool listen = proc.apvts.getRawParameterValue ("listen")->load() > 0.5f && ! snap.edm;
        g.setFont (lnf.title (10.0f, 0.18f)); g.setColour (listen ? co : dim);
        g.drawText ("LISTEN", getWidth() - 470, getHeight() - 30, 60, 24, Justification::centredRight);
        const float gdb = proc.refGainShown.load();
        if (! snap.edm) {
        g.setFont (lnf.body (11.0f)); g.setColour (proc.apvts.getRawParameterValue ("levelMatch")->load() > 0.5f ? hot : faint);
        g.drawText ("REF " + signedDb (gdb), getWidth() - 162, getHeight() - 30, 150, 24, Justification::centredLeft);
        }
        if (listen)
        {
            g.setColour (co.withAlpha (0.55f)); g.drawRoundedRectangle (r.reduced (1.5f), 12, 2.0f);
        }
    }
    // your track's sections
    const auto tl = timeline();
    g.setFont (lnf.title (10.5f, 0.18f));
    const bool wholeSel = view == (int) snap.views.size() - 1;
    g.setColour (wholeSel ? cy : dim);
    g.drawText ("YOUR TRACK", (int) p.getX(), (int) tl.getY(), 90, (int) tl.getHeight(), Justification::centredLeft);
    if (snap.views.size() > 1 && snap.mineLength > 0)
    {
        const bool one = proc.apvts.getRawParameterValue ("oneCurve")->load() > 0.5f;
        for (size_t i = 0; i + 1 < snap.views.size(); ++i)
        {
            const auto& s = snap.views[i];
            auto rr = Rectangle<float> (tl.getX() + tl.getWidth() * (float) (s.t0 / snap.mineLength), tl.getY(), tl.getWidth() * (float) ((s.t1 - s.t0) / snap.mineLength), tl.getHeight()).reduced (1, 0);
            const bool sel = (int) i == view, drop = s.name.startsWith ("DROP"), byp = proc.sectionBypassed (s.name);
            g.setColour (one ? panel2 : sel ? cy : drop ? cy.withAlpha (0.16f) : Colour (0xff1a262b)); g.fillRoundedRectangle (rr, 4);
            g.setColour (one ? line : drop || sel ? cy.withAlpha (0.7f) : line); g.drawRoundedRectangle (rr, 4, 1);
            if (byp) { g.setColour (co.withAlpha (0.6f)); g.drawLine (rr.getX() + 4, rr.getBottom() - 3, rr.getRight() - 4, rr.getBottom() - 3, 1.5f); }
            g.setColour (one ? faint : sel ? Colour (0xff06201d) : drop ? hot : you);
            g.drawText (s.name, rr.toNearestInt(), Justification::centred, true);
        }
        if (proc.playing.load() || proc.playTime.load() > 0)
        {
            const float x = tl.getX() + tl.getWidth() * (float) jlimit (0.0, 1.0, proc.playTime.load() / snap.mineLength);
            g.setColour (proc.refAudible.load() ? co : Colours::white); g.fillRect (x - 0.75f, tl.getY() - 4, 1.5f, tl.getHeight() + 8);
        }
    }
    else
    {
        g.setColour (faint); g.setFont (lnf.body (10.5f));
        g.drawText (snap.views.empty() ? "not learned yet" : "one section", tl.toNearestInt(), Justification::centredLeft);
    }
}

void Display::paintOverChildren (juce::Graphics& g)
{
    using namespace juce;
    auto msg = proc.busyMessage();
    if (msg.isEmpty() && proc.apvts.getRawParameterValue ("listen")->load() > 0.5f && ! snap.refs.empty())
    {
        const int st = proc.refAudioState.load();
        msg = st == 3 ? "THE REFERENCE FILE WASN'T FOUND: ADD IT AGAIN TO LISTEN" : st == 1 ? "LOADING THE REFERENCE..." : ! proc.playing.load() ? "PRESS PLAY TO HEAR THE REFERENCE" : String();
    }
    if (proc.isLearning())
    {
        auto b = Rectangle<float> (460, 54).withCentre ({ getWidth() * 0.5f, getHeight() * 0.22f });
        g.setColour (Colour (0xf20f181c)); g.fillRoundedRectangle (b, 10); g.setColour (cy.withAlpha (0.6f)); g.drawRoundedRectangle (b, 10, 1);
        g.setFont (lnf.title (15.0f, 0.16f)); g.setColour (cy);
        g.drawText ("LEARNING  " + mmss (proc.learnedSeconds()) + (proc.playing.load() ? "" : "   (PRESS PLAY)"), b.withTrimmedBottom (20).toNearestInt(), Justification::centred);
        g.setFont (lnf.body (11.0f)); g.setColour (dim);
        g.drawText ("play your track through once, then press STOP", b.withTrimmedTop (28).toNearestInt(), Justification::centred);
    }
    if (emptyState() && ! proc.isLearning())
    {
        g.setColour (bg.withAlpha (0.82f)); g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1), 12);
        auto zone = [&] (Rectangle<float> z, Colour c, const String& step, const String& big, const String& small, bool done)
        {
            g.setColour (c.withAlpha (done ? 0.10f : 0.05f)); g.fillRoundedRectangle (z, 16);
            Path pth; pth.addRoundedRectangle (z, 16); Path dashed; const float dl[] = { 7, 5 };
            PathStrokeType (2.0f).createDashedStroke (dashed, pth, dl, 2); g.setColour (c.withAlpha (dragOver ? 0.9f : 0.55f)); g.fillPath (dashed);
            g.setFont (lnf.title (13.0f, 0.2f)); g.setColour (c); g.drawText (step, z.withTrimmedTop (64).withHeight (18).toNearestInt(), Justification::centred);
            g.setFont (lnf.title (26.0f, 0.04f)); g.setColour (text); g.drawText (done ? String ("DONE") : big, z.withTrimmedTop (88).withHeight (34).toNearestInt(), Justification::centred);
            g.setFont (lnf.body (11.0f)); g.setColour (dim); g.drawFittedText (small, z.withTrimmedTop (130).withHeight (44).reduced (20, 0).toNearestInt(), Justification::centredTop, 3);
        };
        zone (zoneDonor(), co, "1  THE DONOR", snap.edm ? String ("EDM BALANCE") : String ("DROP A REFERENCE"),
              snap.edm ? String ("no reference: your track is matched to the middle of the EDM tonal balance range")
                       : String ("click to browse, or drop a file here\nWAV  AIFF  FLAC  MP3  OGG"), ! snap.refs.empty());
        zone (zonePatient(), cy, "2  THE PATIENT", "PRESS LEARN", "then play your track through once from the start.\nOr drop your own bounce here.", ! snap.views.empty());
    }
    if (msg.isNotEmpty())
    {
        g.setFont (lnf.title (12.0f, 0.18f));
        const float w = juce::GlyphArrangement::getStringWidth (lnf.title (12.0f, 0.18f), msg) + 40;
        auto b = Rectangle<float> (w, 28).withCentre ({ getWidth() * 0.5f, 62.0f });
        g.setColour (Colour (0xf20f181c)); g.fillRoundedRectangle (b, 8); g.setColour (co.withAlpha (0.5f)); g.drawRoundedRectangle (b, 8, 1);
        g.setColour (text); g.drawText (msg, b.toNearestInt(), Justification::centred);
    }
}

int Display::nodeAt (juce::Point<float> pt) const
{
    int best = -1; float bd = 9.0f;
    const auto p = plot();
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const auto c = juce::Point<float> (xOfBin (nodes[(size_t) i].bin), juce::jlimit (p.getY(), p.getBottom(), yOfCurve (nodes[(size_t) i].db)));
        const float d = c.getDistanceFrom (pt);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void Display::mouseMove (const juce::MouseEvent& e)
{
    const int h = hasView && v.hasRef ? nodeAt (e.position) : -1;
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void Display::mouseDown (const juce::MouseEvent& e)
{
    if (emptyState() && ! proc.isLearning())
    {
        if (zoneDonor().contains (e.position)) { owner.chooseReference(); return; }
        if (zonePatient().contains (e.position)) { if (snap.views.empty()) owner.mineMenu(); return; }
        return;
    }
    // the section strip: pick what to look at (and what plays when not following the playhead)
    const auto tl = timeline();
    if (e.position.y >= tl.getY() - 4 && e.position.y <= tl.getBottom() + 4)
    {
        if (e.position.x < tl.getX()) { proc.selectedView = -1; }
        else if (snap.mineLength > 0)
        {
            const double t = (e.position.x - tl.getX()) / tl.getWidth() * snap.mineLength;
            for (size_t i = 0; i + 1 < snap.views.size(); ++i) if (t >= snap.views[i].t0 && t < snap.views[i].t1) proc.selectedView = (int) i;
        }
        owner.refreshAll();
        return;
    }
    if (! hasView || ! v.hasRef) return;
    const int n = nodeAt (e.position);
    if (e.mods.isPopupMenu()) { showPointMenu (n); return; }
    selected = n;
    if (n >= 0)
    {
        proc.undo.beginNewTransaction();
        dragging = n; dragMoved = false; dragView = v.name;
        dragBin = nodes[(size_t) n].user ? nodes[(size_t) n].bin : -1.0f;
    }
    pointX.setVisible (selected >= 0);
    repaint();
}

void Display::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0 || dragging >= (int) nodes.size()) return;
    if (! dragMoved && e.getDistanceFromDragStart() < 3) return;
    dragMoved = true;
    auto& n = nodes[(size_t) dragging];
    const double range = (double) v.match[2];
    float bin = (float) juce::jlimit (0.0, tp::kBins - 1.0, binOfX (e.position.x));
    float db = (float) juce::jlimit (-range, range, curveOfY (e.position.y));
    if (e.mods.isShiftDown()) { bin = n.bin; }                          // Shift: gain only
    if (e.mods.isAltDown()) db = n.db;                                  // Alt: frequency only
    // don't pass the neighbours
    if (dragging > 0) bin = juce::jmax (bin, nodes[(size_t) dragging - 1].bin + 0.25f);
    if (dragging + 1 < (int) nodes.size()) bin = juce::jmin (bin, nodes[(size_t) dragging + 1].bin - 0.25f);
    n.bin = bin; n.db = db; n.user = true;
    proc.setUserPoint (dragView, dragBin, bin, db);
    dragBin = bin;
    repaint();
}

void Display::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0 && ! dragMoved) { /* just a click: keep it selected */ }
    dragging = -1;
}

void Display::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! hasView || ! v.hasRef || ! plot().contains (e.position)) return;
    const int n = nodeAt (e.position);
    proc.undo.beginNewTransaction();
    if (n >= 0) { proc.removePoint (v.name, nodes[(size_t) n].bin, nodes[(size_t) n].user); selected = -1; }
    else
    {
        const double range = (double) v.match[2];
        proc.setUserPoint (v.name, -1, (float) juce::jlimit (0.0, tp::kBins - 1.0, binOfX (e.position.x)), (float) juce::jlimit (-range, range, curveOfY (e.position.y)));
    }
}

void Display::showPointMenu (int n)
{
    juce::PopupMenu m;
    if (n >= 0)
    {
        m.addItem (1, nodes[(size_t) n].user ? "Remove this point" : "Remove this point (the analysis' point)");
        if (nodes[(size_t) n].user) m.addItem (2, "Back to the analysis here");
        m.addSeparator();
    }
    m.addItem (3, "Reset all points in " + v.name);
    m.addItem (4, "Reset all points in every section");
    const auto name = v.name; const float bin = n >= 0 ? nodes[(size_t) n].bin : 0; const bool user = n >= 0 && nodes[(size_t) n].user;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, name, bin, user] (int r)
    {
        proc.undo.beginNewTransaction();
        if (r == 1) proc.removePoint (name, bin, user);
        if (r == 2) proc.removePoint (name, bin, true);
        if (r == 3) proc.resetPoints (name);
        if (r == 4) for (auto& vv : snap.views) proc.resetPoints (vv.name);
        selected = -1;
    });
}

bool Display::isInterestedInFileDrag (const juce::StringArray& files) { for (auto& f : files) if (isAudioFile (f)) return true; return false; }
void Display::filesDropped (const juce::StringArray& files, int x, int y)
{
    dragOver = false;
    const bool asMine = emptyState() && zonePatient().contains ((float) x, (float) y);
    for (auto& f : files)
        if (isAudioFile (f))
        {
            if (asMine) { proc.loadMineFromFile (juce::File (f)); break; }
            proc.loadReference (juce::File (f));
        }
    repaint();
}

//============================================================================== content
TransplantContent::TransplantContent (TransplantProcessor& p, TpLookAndFeel& l)
    : proc (p), lnf (l), display (p, l, *this), inBox (*p.apvts.getParameter ("inGain"), "IN", l), outBox (*p.apvts.getParameter ("outGain"), "OUT", l)
{
    addAndMakeVisible (display);
    for (auto* b : { &prevBtn, &nextBtn, &learnBtn, &undoBtn, &redoBtn, &bypassBtn, &helpBtn }) addAndMakeVisible (*b);
    for (auto* b : { &aBtn, &bBtn }) { styleSeg (*b); addAndMakeVisible (*b); }
    undoBtn.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xb6"))); redoBtn.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xb7")));
    undoBtn.getProperties().set ("size", 15.0f); redoBtn.getProperties().set ("size", 15.0f);
    addAndMakeVisible (presetBox);
    refreshPresets();
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0) proc.loadPreset (i); };
    prevBtn.onClick = [this] { const int n = TransplantProcessor::presetNames().size(); proc.loadPreset ((proc.currentPreset + n - 1) % n); refreshPresets(); };
    nextBtn.onClick = [this] { const int n = TransplantProcessor::presetNames().size(); proc.loadPreset ((proc.currentPreset + 1) % n); refreshPresets(); };
    learnBtn.onClick = [this] { if (proc.isLearning()) proc.stopLearn(); else proc.startLearn(); };
    aBtn.onClick = [this] { if (proc.abSlot() != 0) proc.switchAB(); refreshAll(); };
    bBtn.onClick = [this] { if (proc.abSlot() != 1) proc.switchAB(); refreshAll(); };
    undoBtn.onClick = [this] { proc.undo.undo(); };
    redoBtn.onClick = [this] { proc.undo.redo(); };
    bypassBtn.setClickingTogglesState (true);
    bypassBtn.getProperties().set ("coral", true);
    bypassAtt = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter ("bypass"), bypassBtn);
    helpBtn.setClickingTogglesState (true);
    helpBtn.onClick = [this] { help = helpBtn.getToggleState(); repaint(); };
    // tooltips
    presetBox.setTooltip ("Presets set how the curve is made (smoothing, range, from/to, phase), for every section. They don't touch your references, your track or your points.");
    learnBtn.setTooltip ("LEARN: press it, then play your track through once from the start. TRANSPLANT hears its spectrum, sections and key. Press again (STOP) when done.");
    aBtn.setTooltip ("A/B: two versions of the settings and points. Right-click to copy one to the other.");
    bBtn.setTooltip (aBtn.getTooltip());
    undoBtn.setTooltip ("Undo"); redoBtn.setTooltip ("Redo");
    bypassBtn.setTooltip ("BYPASS: the original sound (the latency stays the same, so A/B is fair).");
    helpBtn.setTooltip ("How TRANSPLANT works.");
    inBox.setTooltip ("INPUT: the level going into TRANSPLANT, for the whole song. Drag up or down (Shift: fine), double-click for 0 dB.");
    outBox.setTooltip ("OUTPUT: the level coming out, for the whole song (each section's own GAIN is in MATCH). Drag up or down (Shift: fine), double-click for 0 dB.");
    addAndMakeVisible (inBox); addAndMakeVisible (outBox);
    tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
    addMouseListener (this, true);
    startTimerHz (30);
}
TransplantContent::~TransplantContent() { removeMouseListener (this); }

void TransplantContent::refreshPresets()
{
    presetBox.clear (juce::dontSendNotification);
    presetBox.addItemList (TransplantProcessor::presetNames(), 1);
    presetBox.setSelectedId (proc.currentPreset + 1, juce::dontSendNotification);
}

void TransplantContent::resized()
{
    prevBtn.setBounds (182, 13, 26, 32); presetBox.setBounds (212, 13, 142, 32); nextBtn.setBounds (358, 13, 26, 32);
    refBox = { 394, 13, 262, 32 }; mineBox = { 664, 13, 206, 32 };
    learnBtn.setBounds (878, 13, 86, 32);
    aBtn.setBounds (980, 13, 26, 32); bBtn.setBounds (1006, 13, 26, 32);
    undoBtn.setBounds (1040, 13, 28, 32); redoBtn.setBounds (1070, 13, 28, 32);
    bypassBtn.setBounds (1106, 13, 62, 32); helpBtn.setBounds (1172, 13, 22, 32);
    inBox.setBounds (kW / 2 - 112, kH - 31, 108, 28); outBox.setBounds (kW / 2 + 4, kH - 31, 108, 28);
    display.setBounds (12, 70, kW - 24, kH - 70 - 34);
}

void TransplantContent::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == this)
    {
        if (refBox.contains (e.getPosition())) { if (proc.numRefs() >= 3 || proc.edmActive.load()) display.openDrawer (3); else chooseReference(); }
        else if (mineBox.contains (e.getPosition())) mineMenu();
        else if (help) { help = false; helpBtn.setToggleState (false, juce::dontSendNotification); repaint(); }
    }
    if (e.mods.isPopupMenu() && (e.eventComponent == &aBtn || e.eventComponent == &bBtn))
    {
        juce::PopupMenu m; m.addItem (1, proc.abSlot() == 0 ? "Copy A to B" : "Copy B to A");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (e.eventComponent), [this] (int r) { if (r == 1) proc.copyAtoB(); });
    }
}

void TransplantContent::chooseReference()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a reference track", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [this] (const juce::FileChooser& fc) { for (auto& f : fc.getResults()) proc.loadReference (f); });
}
void TransplantContent::chooseMine()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose your bounce (the whole track, from the start)", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.loadMineFromFile (fc.getResult()); });
}
void TransplantContent::mineMenu()
{
    juce::PopupMenu m;
    m.addItem (1, proc.isLearning() ? "Stop learning" : "Learn from playback (press play after this)");
    m.addItem (2, "Load your own bounce...");
    m.addSeparator();
    m.addItem (3, "Forget your track", proc.hasMine());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (mineBox)), [this] (int r)
    {
        if (r == 1) { if (proc.isLearning()) proc.stopLearn(); else proc.startLearn(); }
        if (r == 2) chooseMine();
        if (r == 3) proc.clearMine();
    });
}

void TransplantContent::refreshAll() { lastVersion = -1; timerCallback(); }

void TransplantContent::timerCallback()
{
    const auto snap = proc.snapshot();
    // which section to show: the one playing (following the playhead), else the one picked
    const bool follow = proc.apvts.getRawParameterValue ("follow")->load() > 0.5f;
    const bool one = proc.apvts.getRawParameterValue ("oneCurve")->load() > 0.5f;
    const int nv = (int) snap.views.size();
    int vi = proc.selectedView.load();
    if (follow && proc.playing.load()) vi = proc.activeView.load();
    if (one || vi < 0 || vi >= nv - 1) vi = nv - 1;
    display.update (snap, vi);
    display.updateLive();
    // header state
    const bool learning = proc.isLearning();
    learnBtn.setButtonText (learning ? "STOP  " + mmss (proc.learnedSeconds()) : juce::String ("LEARN"));
    learnBtn.getProperties().set ("kind", learning || (snap.views.empty() && ! snap.refs.empty()) ? "fill" : "");
    aBtn.setToggleState (proc.abSlot() == 0, juce::dontSendNotification); bBtn.setToggleState (proc.abSlot() == 1, juce::dontSendNotification);
    undoBtn.setEnabled (proc.undo.canUndo()); redoBtn.setEnabled (proc.undo.canRedo());
    if (presetBox.getSelectedId() != proc.currentPreset + 1) presetBox.setSelectedId (proc.currentPreset + 1, juce::dontSendNotification);
    // footer
    footerL = {};
    if (vi >= 0 && vi < nv)
    {
        const auto& v = snap.views[(size_t) vi];
        footerL = (follow && proc.playing.load() ? "PLAYING " : "SHOWING ") + v.name;
        if (proc.apvts.getRawParameterValue ("listen")->load() > 0.5f && v.hasRef)
            footerL = "LISTENING TO REF " + v.refLabel.fromFirstOccurrenceOf ("AUTO: ", false, false) + " (FOR YOUR " + v.name + ")"
                      + (proc.apvts.getRawParameterValue ("levelMatch")->load() > 0.5f ? "     LEVEL MATCHED " + signedDb (proc.refGainShown.load()).toUpperCase() : juce::String ("     NOT LEVEL MATCHED"));
        else
        if (v.hasRef) footerL += juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + refName (v, snap.edm);
        if (snap.edm) footerL += "     NO REFERENCE: MATCHING TO THE MIDDLE OF THE EDM RANGE";
        else if (! snap.refs.empty())
        {
            const int sh = snap.refs[0].shift; const int mode = (int) proc.apvts.getRawParameterValue ("keyMode")->load();
            footerL += mode == 2 ? juce::String ("     KEY SHIFT OFF")
                                 : "     KEY SHIFT " + juce::String (sh > 0 ? "+" : sh < 0 ? minus() : "") + juce::String (std::abs (sh)) + " ST BELOW " + hzText (proc.apvts.getRawParameterValue ("keyZone")->load()).toUpperCase();
        }
    }
    footerR = juce::String (proc.apvts.getRawParameterValue ("phase")->load() < 0.5f ? "LINEAR PHASE" : "NATURAL PHASE") + "     " + juce::String (proc.latencyMs()) + " MS LATENCY     "
              + juce::String (proc.rate.load() / 1000.0, proc.rate.load() == 44100.0 ? 1 : 0) + " KHZ";
    repaint (0, 0, kW, 64);
    repaint (0, kH - 34, kW, 34);
    if (help) repaint();
}

void TransplantContent::drawLogo (juce::Graphics& g, juce::Rectangle<float> area)
{
    // INCISION: the word cut through the middle; the top half is the donor's (coral, shifted), stitched on
    using namespace juce;
    GlyphArrangement ga; ga.addLineOfText (lnf.title (area.getHeight(), 0.06f), "TRANSPLANT", area.getX(), area.getBottom() - area.getHeight() * 0.12f);
    Path tp; ga.createPath (tp);
    const auto tb = tp.getBounds();
    const float cut = tb.getCentreY() - 0.5f, sh = 1.5f;
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (Rectangle<float> (tb.getX() - 4, cut + 0.5f, tb.getWidth() + 12, tb.getHeight()).toNearestInt());
        g.setGradientFill (ColourGradient (Colour (0xfff4f7f7), 0, tb.getY(), Colour (0xff6d777a), 0, tb.getBottom(), false));
        g.fillPath (tp);
    }
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (Rectangle<float> (tb.getX() - 4, tb.getY() - 4, tb.getWidth() + 12, cut - 0.5f - (tb.getY() - 4)).toNearestInt());
        g.setGradientFill (ColourGradient (Colour (0xffffd6ce), 0, tb.getY(), Colour (0xffe88c80), 0, cut, false));
        g.fillPath (tp, AffineTransform::translation (sh, 0));
    }
    for (auto [h, a] : { std::pair<float, float> { 6.0f, 0.10f }, { 2.4f, 0.22f }, { 0.9f, 0.85f } })
    {
        ColourGradient cg (cy.withAlpha (0.0f), tb.getX() - 8, cut, cy.withAlpha (0.0f), tb.getRight() + 8, cut, false);
        cg.addColour (0.06, cy.withAlpha (a)); cg.addColour (0.94, cy.withAlpha (a));
        g.setGradientFill (cg); g.fillRect (tb.getX() - 8, cut - 0.5f - h * 0.5f, tb.getWidth() + 16, h);
    }
    g.setColour (hot.withAlpha (0.85f));
    for (float x = tb.getX() + 8; x < tb.getRight() - 4; x += 19)
        g.drawLine (x - 1.0f, cut - 2.6f, x + 1.0f, cut + 2.2f, 1.0f);
}

void TransplantContent::paint (juce::Graphics& g)
{
    using namespace juce;
    g.fillAll (bg);
    g.setColour (hdr); g.fillRect (0, 0, kW, 58);
    g.setColour (line); g.fillRect (0, 58, kW, 1);
    drawLogo (g, { 16, 13, 156, 31 });
    const auto snap = proc.snapshot();
    auto box = [&] (Rectangle<int> r, Colour dot, const String& label, const String& value, const String& extra)
    {
        auto rf = r.toFloat().reduced (0.5f);
        const bool over = r.contains (getMouseXYRelative());
        g.setColour (over ? Colour (0xff17232a) : panel); g.fillRoundedRectangle (rf, 7);
        g.setColour (over ? line.brighter (0.3f) : line); g.drawRoundedRectangle (rf, 7, 1);
        g.setColour (dot); g.fillEllipse ((float) r.getX() + 10, (float) r.getCentreY() - 4, 8, 8);
        g.setFont (lnf.title (10.0f, 0.18f)); g.setColour (dim);
        const float lw = juce::GlyphArrangement::getStringWidth (lnf.title (10.0f, 0.18f), label);
        g.drawText (label, r.getX() + 24, r.getY(), (int) lw + 4, r.getHeight(), Justification::centredLeft);
        const int vx = r.getX() + 32 + (int) lw;
        g.setFont (lnf.body (11.5f)); g.setColour (value.startsWith ("drop") || value.startsWith ("not") ? dim : text);
        const float ew = extra.isEmpty() ? 0 : juce::GlyphArrangement::getStringWidth (lnf.body (11.0f), extra) + 8;
        g.drawText (value, vx, r.getY(), r.getRight() - vx - (int) ew - 8, r.getHeight(), Justification::centredLeft, true);
        if (extra.isNotEmpty()) { g.setFont (lnf.body (11.0f)); g.setColour (dim); g.drawText (extra, r.getRight() - (int) ew - 8, r.getY(), (int) ew, r.getHeight(), Justification::centredRight); }
    };
    String rv = "drop a file or click", rk;
    if (! snap.refs.empty()) { rv = snap.refs[0].name; rk = snap.refs[0].key; if (snap.refs.size() > 1) rv += "  +" + String ((int) snap.refs.size() - 1); }
    if (snap.edm) box (refBox, cy, "TARGET", "EDM tonal balance", "middle");
    else box (refBox, co, "REFERENCE", rv, rk);
    String mv = proc.isLearning() ? String ("learning...") : snap.views.empty() ? String ("not learned yet") : mmss (snap.mineLength), mk = snap.views.empty() ? String() : snap.mineKey;
    box (mineBox, you, "YOUR TRACK", mv, mk);
    g.setColour (line); g.fillRect (972, 17, 1, 24);
    // footer
    g.setFont (lnf.title (10.0f, 0.16f)); g.setColour (dim);
    g.drawText (footerL, 18, kH - 30, kW / 2 - 136, 24, Justification::centredLeft);
    g.drawText (footerR, kW / 2 + 120, kH - 30, kW / 2 - 138, 24, Justification::centredRight);
}

void TransplantContent::paintOverChildren (juce::Graphics& g)
{
    if (! help) return;
    using namespace juce;
    auto r = display.getBounds().toFloat().reduced (60, 40);
    g.setColour (Colour (0xf40c1417)); g.fillRoundedRectangle (r, 14);
    g.setColour (cy.withAlpha (0.4f)); g.drawRoundedRectangle (r, 14, 1);
    g.setFont (lnf.title (18.0f, 0.16f)); g.setColour (cy); g.drawText ("HOW TRANSPLANT WORKS", r.reduced (30, 22).removeFromTop (24).toNearestInt(), Justification::centredLeft);
    const char* steps[][2] = {
        { "1  THE DONOR", "Drop a reference track on the display (or click REFERENCE). Up to three; they're averaged." },
        { "2  THE PATIENT", "Press LEARN and play your track through once from the start, then STOP. Or load your own bounce from YOUR TRACK." },
        { "3  THE MATCH", "TRANSPLANT finds the sections of both tracks (intro, build, drop, break) and their keys. Each section of yours is matched to the same kind in the reference: your drop to its drop." },
        { "4  THE KEY", "Below KEY ZONE, the reference's bass notes are moved into your key before comparing, so the curve doesn't boost its notes and cut yours." },
        { "5  YOUR HANDS", "The curve is points: drag them, double-click to add or remove, right-click for more. Shift drags gain only. SMOOTH sets how surgical it is: fewer, wider points to the right. Points you've touched are white and stay put." },
        { "6  LISTEN", "YOURS / REF (bottom right): hear the reference's matching part at the same moment, level-matched to your track. Flip back and forth while it plays." },
        { "7  PLAYBACK", "With FOLLOW PLAYHEAD on (SECTIONS), the curve changes with the section that's playing, by song position, so keep the song where you learned it. Bounces work the same." },
    };
    float y = r.getY() + 64;
    for (auto& s : steps)
    {
        g.setFont (lnf.title (12.5f, 0.16f)); g.setColour (text); g.drawText (s[0], (int) r.getX() + 30, (int) y, 150, 20, Justification::centredLeft);
        g.setFont (lnf.body (12.0f)); g.setColour (dim);
        g.drawFittedText (s[1], (int) r.getX() + 180, (int) y + 1, (int) r.getWidth() - 210, 40, Justification::topLeft, 3);
        y += 54;
    }
    g.setFont (lnf.body (11.0f)); g.setColour (faint);
    g.drawText ("click anywhere to close", r.reduced (30, 16).removeFromBottom (18).toNearestInt(), Justification::centredRight);
}

//============================================================================== editor
TransplantEditor::TransplantEditor (TransplantProcessor& p) : AudioProcessorEditor (p), content (p, lnf)
{
    const int w = juce::jlimit (TransplantContent::kW * 6 / 10, TransplantContent::kW * 2, p.uiWidth);
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) TransplantContent::kW / TransplantContent::kH);
    setResizeLimits (TransplantContent::kW * 6 / 10, TransplantContent::kH * 6 / 10, TransplantContent::kW * 2, TransplantContent::kH * 2);
    setSize (w, juce::roundToInt ((double) w * TransplantContent::kH / TransplantContent::kW));
    ready = true;
}
TransplantEditor::~TransplantEditor() { setLookAndFeel (nullptr); }
void TransplantEditor::resized()
{
    if (ready) if (auto* pp = dynamic_cast<TransplantProcessor*> (&processor)) pp->uiWidth = getWidth();
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) TransplantContent::kW));
    content.setBounds (0, 0, TransplantContent::kW, TransplantContent::kH);
}
