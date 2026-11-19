#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    const float kStart = juce::degreesToRadians (225.0f);
    const float kEnd   = juce::degreesToRadians (495.0f);

    // Noise Division palette
    namespace Pal
    {
        const juce::Colour black    { 0xff111214 };
        const juce::Colour red      { 0xffff3038 };
        const juce::Colour orange   { 0xffff7138 };
        const juce::Colour silver   { 0xffc6c8cc };
        const juce::Colour steel    { 0xff777a82 };
        const juce::Colour green    { 0xff3ddc84 };
        const juce::Colour ledOff   { 0xff26282d };   // unlit LED segment
        const juce::Colour ledDim   { 0xff4a3033 };   // "lit" segment while bypassed
        const juce::Colour traceOff { 0xff3a3c42 };
    }

    // LED colour along the scale: KRUSH Red at the start, Hot Orange at the end
    juce::Colour ledColour (float t) { return Pal::red.interpolatedWith (Pal::orange, juce::jlimit (0.0f, 1.0f, t)); }

    // how "hot" the amount looks: KRUSH Red, then Hot Orange, then a hot amber near 100 %
    juce::Colour heatColour (float t)
    {
        const juce::Colour amber { 0xffffa648 };
        t = juce::jlimit (0.0f, 1.0f, t);
        return t < 0.6f ? Pal::red.interpolatedWith (Pal::orange, t / 0.6f)
                        : Pal::orange.interpolatedWith (amber, (t - 0.6f) / 0.4f);
    }

    // the two signal traces, in design pixels, each running in the direction the signal flows
    const juce::Point<float> kTraceIn[]  = { { 0, 300 }, { 40, 300 }, { 70, 270 }, { 110, 270 }, { 128, 288 } };
    const juce::Point<float> kTraceOut[] = { { 392, 288 }, { 410, 270 }, { 450, 270 }, { 480, 300 }, { 520, 300 } };
    constexpr int kTracePoints = 5;

    float traceLength (const juce::Point<float>* pts)
    {
        float len = 0.0f;
        for (int i = 1; i < kTracePoints; ++i) len += pts[i - 1].getDistanceFrom (pts[i]);
        return len;
    }

    // the part of a trace between distances d0 and d1 from its start, scaled by s
    juce::Path tracePiece (const juce::Point<float>* pts, float d0, float d1, float s)
    {
        juce::Path p;
        float walked = 0.0f;
        bool started = false;
        for (int i = 1; i < kTracePoints; ++i)
        {
            const float seg = pts[i - 1].getDistanceFrom (pts[i]);
            const float a = juce::jmax (d0, walked), b = juce::jmin (d1, walked + seg);
            if (b > a)
            {
                const auto pa = pts[i - 1] + (pts[i] - pts[i - 1]) * ((a - walked) / seg);
                const auto pb = pts[i - 1] + (pts[i] - pts[i - 1]) * ((b - walked) / seg);
                if (! started) { p.startNewSubPath (pa * s); started = true; }
                p.lineTo (pb * s);
            }
            walked += seg;
        }
        return p;
    }

    // how the current flows: speed (px/s), gap between pulses, pulse length and brightness
    struct Flow { float speed, spacing, length, brightness; };
    Flow inFlow()           { return { 55.0f, 64.0f, 8.0f, 0.35f }; }
    Flow outFlow (float a)  { return { 45.0f + 285.0f * a, 72.0f - 40.0f * a, 7.0f + 11.0f * a, 0.3f + 0.7f * a }; }

    juce::Point<float> polar (juce::Point<float> c, float angle, float r)
    {
        return { c.x + r * std::sin (angle), c.y - r * std::cos (angle) };
    }

    // a ring of LED segments from startAngle to endAngle, lit from `from` to `to` (0..1)
    void drawLedRing (juce::Graphics& g, juce::Point<float> c, float r1, float r2, int count, float from, float to,
                      float startAngle, float endAngle, bool active, float width, float majorWidth, float glowRadius)
    {
        using namespace juce;
        auto segment = [&] (int i)
        {
            const float t = (float) i / (float) (count - 1);
            const float a = startAngle + t * (endAngle - startAngle);
            Path seg;
            seg.startNewSubPath (polar (c, a, r1));
            seg.lineTo (polar (c, a, r2));
            Path stroked;
            PathStrokeType ((majorWidth > 0.0f && i % 5 == 0) ? majorWidth : width, PathStrokeType::mitered, PathStrokeType::butt)
                .createStrokedPath (stroked, seg);
            return stroked;
        };
        auto isLit = [&] (int i) { const float t = (float) i / (float) (count - 1); return t >= from - 1.0e-4f && t <= to + 1.0e-4f; };

        Path glow;
        for (int i = 0; i < count; ++i)
        {
            const auto seg = segment (i);
            if (isLit (i) && active) { glow.addPath (seg); continue; }
            g.setColour (isLit (i) ? Pal::ledDim : Pal::ledOff);
            g.fillPath (seg);
        }
        if (glow.isEmpty())
            return;

        // soft glow first, then the lit segments on top so they stay crisp
        DropShadow (Pal::red.withAlpha (0.75f), jmax (1, roundToInt (glowRadius)), {}).drawForPath (g, glow);
        for (int i = 0; i < count; ++i)
        {
            if (! isLit (i)) continue;
            g.setColour (ledColour ((float) i / (float) (count - 1)));
            g.fillPath (segment (i));
        }
    }

    // a glowing red pointer from radius r1 (inner) to r2 (outer)
    void drawGlowPointer (juce::Graphics& g, juce::Point<float> c, float angle, float r1, float r2, float width)
    {
        using namespace juce;
        Path p;
        p.addRoundedRectangle (-width * 0.5f, -r2, width, r2 - r1, width * 0.5f);
        p.applyTransform (AffineTransform::rotation (angle).translated (c));
        DropShadow (Pal::red.withAlpha (0.9f), jmax (1, roundToInt (width * 1.6f)), {}).drawForPath (g, p);
        g.setGradientFill (ColourGradient (Pal::orange, polar (c, angle, r2), Pal::red, polar (c, angle, r1), false));
        g.fillPath (p);
    }
}

//==============================================================================
FuseLookAndFeel::FuseLookAndFeel()
{
    setColour (juce::TooltipWindow::textColourId, Pal::silver);
}

juce::Font FuseLookAndFeel::chakraFont (float height, float kerning) const
{
    // sizes are given like the design's CSS font sizes; JUCE's font height is the whole line
    // (ascent + descent), which for Chakra Petch is 1.3 times that
    return juce::Font (juce::FontOptions (chakra).withHeight (height * 1.3f)).withExtraKerningFactor (kerning);
}

void FuseLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace juce;
    const auto area = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto c = area.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);

    // ---- small knobs in the advanced drawer (designed in an 88 px square)
    if ((bool) slider.getProperties().getWithDefault ("small", false))
    {
        const float u = jmin (area.getWidth(), area.getHeight()) / 88.0f;
        const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);
        const float from = bipolar ? jmin (0.5f, pos) : 0.0f, to = bipolar ? jmax (0.5f, pos) : pos;
        drawLedRing (g, c, 34.0f * u, 40.0f * u, 15, from, to, startAngle, endAngle, active, 2.4f * u, 0.0f, 4.0f * u);
        if (smallKnobImage.isValid())
            g.drawImage (smallKnobImage, Rectangle<float> (88.0f * u, 88.0f * u).withCentre (c), RectanglePlacement::stretchToFit);
        drawGlowPointer (g, c, angle, 6.0f * u, 20.0f * u, 4.0f * u);
        return;
    }

    // ---- the main knob (designed in a 280 px square: LED ring, knob body, pointer)
    const float u = jmin (area.getWidth(), area.getHeight()) / 280.0f;
    drawLedRing (g, c, 115.0f * u, 134.0f * u, 31, 0.0f, pos, startAngle, endAngle, active, 3.0f * u, 4.0f * u, 6.0f * u);
    if (knobImage.isValid())
        g.drawImage (knobImage, Rectangle<float> (300.0f * u, 300.0f * u).withCentre (c), RectanglePlacement::stretchToFit);
    drawGlowPointer (g, c, angle, 32.0f * u, 74.0f * u, 6.0f * u);
}

void FuseLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    using namespace juce;
    const bool on = b.getToggleState();
    const auto area = b.getLocalBounds().toFloat();

    // ---- level match: chamfered steel outline, green when on (designed 128 x 24)
    if ((bool) b.getProperties().getWithDefault ("pill", false))
    {
        const float u = area.getWidth() / 128.0f;
        Path shape;
        shape.startNewSubPath (7.5f * u, 0.5f * u);
        shape.lineTo (127.5f * u, 0.5f * u);
        shape.lineTo (127.5f * u, 16.5f * u);
        shape.lineTo (120.5f * u, 23.5f * u);
        shape.lineTo (0.5f * u, 23.5f * u);
        shape.lineTo (0.5f * u, 7.5f * u);
        shape.closeSubPath();

        g.setColour (Colour (0x990a0a0c));
        g.fillPath (shape);
        if (on)
        {
            g.setColour (Pal::green.withAlpha (0.07f));
            g.fillPath (shape);
        }
        g.setColour (on ? Pal::green : (highlighted ? Pal::steel : Colour (0xff4a4d55)));
        g.strokePath (shape, PathStrokeType (1.0f * u));

        const Font f = chakraFont (10.0f * u, 0.3f);
        const auto textArea = area.translated (1.5f * u, 0.0f);        // the letter spacing pads the end, so nudge right
        if (on)
        {
            Path text;
            GlyphArrangement ga;
            ga.addFittedText (f, "LEVEL MATCH", textArea.getX(), textArea.getY(), textArea.getWidth(), textArea.getHeight(), Justification::centred, 1);
            ga.createPath (text);
            DropShadow (Pal::green.withAlpha (0.5f), jmax (1, roundToInt (6.0f * u)), {}).drawForPath (g, text);
        }
        g.setFont (f);
        g.setColour (on ? Pal::green : (highlighted ? Pal::silver : Pal::steel));
        g.drawText ("LEVEL MATCH", textArea, Justification::centred, false);
        return;
    }

    // ---- the ADVANCED drawer handle with its chevron
    if ((bool) b.getProperties().getWithDefault ("drawer", false))
    {
        const float u = area.getWidth() / 140.0f;
        const Font f = chakraFont (9.0f * u, 0.35f);
        const float tw = GlyphArrangement::getStringWidth (f, "ADVANCED");
        const float x0 = area.getCentreX() - (tw + 15.0f * u) * 0.5f;
        g.setColour (highlighted ? Pal::silver : Pal::steel);
        g.setFont (f);
        g.drawText ("ADVANCED", Rectangle<float> (x0, area.getY(), tw + 4.0f * u, area.getHeight()), Justification::centredLeft, false);

        const float cx = x0 + tw + 10.5f * u, cy = area.getCentreY(), d = 3.5f * u;
        Path chevron;
        chevron.startNewSubPath (cx - d, cy + (on ? d * 0.5f : -d * 0.5f));
        chevron.lineTo (cx, cy + (on ? -d * 0.5f : d * 0.5f));
        chevron.lineTo (cx + d, cy + (on ? d * 0.5f : -d * 0.5f));
        g.strokePath (chevron, PathStrokeType (1.4f * u, PathStrokeType::mitered, PathStrokeType::rounded));
        return;
    }

    // ---- the on/off switch: a glass fuse that glows when on and is blown when bypassed (designed 140 x 58)
    const float u = area.getWidth() / 140.0f;
    const auto& img = on ? fuseOn : fuseOff;
    if (img.isValid())
    {
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (img, Rectangle<float> (8.0f * u, 4.0f * u, 124.0f * u, 36.0f * u), RectanglePlacement::stretchToFit);
    }
    if (highlighted)
    {
        g.setColour (Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (Rectangle<float> (9.0f * u, 10.0f * u, 122.0f * u, 24.0f * u), 4.0f * u);
    }
    g.setFont (chakraFont (10.0f * u, 0.3f));
    g.setColour (on ? Pal::orange : Pal::steel);
    g.drawText (on ? "ON" : "BYPASS", Rectangle<float> (1.5f * u, 42.0f * u, area.getWidth(), 14.0f * u), Justification::centred, false);
}

// help pop-ups: dark box, red accent line, wrapped text
juce::TextLayout FuseLookAndFeel::tooltipLayout (const juce::String& text) const
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::topLeft);
    s.append (text, juce::Font (juce::FontOptions (montserrat).withHeight (12.0f)), Pal::silver);
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, 280.0f);
    return tl;
}

juce::Rectangle<int> FuseLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> area)
{
    const auto tl = tooltipLayout (text);
    const int w = (int) std::ceil (tl.getWidth()) + 26, h = (int) std::ceil (tl.getHeight()) + 16;
    const int x = pos.x > area.getCentreX() ? pos.x - w - 6 : pos.x + 14;
    const int y = pos.y + h + 24 > area.getBottom() ? pos.y - h - 6 : pos.y + 20;
    return juce::Rectangle<int> (x, y, w, h).constrainedWithin (area);
}

void FuseLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    g.setColour (Pal::black);
    g.fillRect (r);
    g.setColour (juce::Colour (0xff34363c));
    g.drawRect (r, 1.0f);
    g.setColour (Pal::red);
    g.fillRect (0.0f, 0.0f, 3.0f, (float) h);
    tooltipLayout (text).draw (g, r.withTrimmedLeft (15.0f).withTrimmedRight (10.0f).withTrimmedTop (8.0f).withTrimmedBottom (8.0f));
}

//==============================================================================
MasterFuseEditor::MasterFuseEditor (MasterFuseProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    using juce::ImageCache;
    background         = ImageCache::getFromMemory (BinaryData::background_png, BinaryData::background_pngSize);
    lnf.knobImage      = ImageCache::getFromMemory (BinaryData::knob_png, BinaryData::knob_pngSize);
    lnf.smallKnobImage = ImageCache::getFromMemory (BinaryData::knob_small_png, BinaryData::knob_small_pngSize);
    lnf.fuseOn         = ImageCache::getFromMemory (BinaryData::fuse_on_png, BinaryData::fuse_on_pngSize);
    lnf.fuseOff        = ImageCache::getFromMemory (BinaryData::fuse_off_png, BinaryData::fuse_off_pngSize);
    lnf.chakra         = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchSemiBold_ttf, BinaryData::ChakraPetchSemiBold_ttfSize);
    lnf.montserrat     = juce::Typeface::createSystemTypefaceFor (BinaryData::Montserrat_ttf, BinaryData::Montserrat_ttfSize);
    lnf.setDefaultSansSerifTypeface (lnf.montserrat);
    setLookAndFeel (&lnf);
    tooltipWindow.setLookAndFeel (&lnf);

    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setRotaryParameters (kStart, kEnd, true);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    knob.setMouseDragSensitivity (220);
    knob.setTooltip ("AMOUNT: how much MASTERFUSE does, from untouched (0%) to full (100%). Double-click to return to 50%.");
    addAndMakeVisible (knob);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, "amount", knob);
    knob.setDoubleClickReturnValue (true, (double) MasterFuseProcessor::kAmountDefault);
    knob.onValueChange = [this] { repaint(); };   // the readout is painted by the editor

    // on/off: the fuse
    enableSwitch.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    enableSwitch.setTooltip ("Switch the processing on or off (bypass) to compare with the untouched sound");
    addAndMakeVisible (enableSwitch);
    enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "enabled", enableSwitch);
    enableSwitch.onStateChange = [this]
    {
        if (lnf.active != enableSwitch.getToggleState())
        {
            lnf.active = enableSwitch.getToggleState();
            repaint();
        }
    };
    lnf.active = enableSwitch.getToggleState();

    // level match, above the fuse
    matchSwitch.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    matchSwitch.getProperties().set ("pill", true);
    addAndMakeVisible (matchSwitch);
    matchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "match", matchSwitch);
    matchSwitch.onClick = [this] { startTimerHz (30); };

    // ---- advanced drawer: input, limiter gain, output
    advancedButton.getProperties().set ("drawer", true);
    advancedButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    advancedButton.setToggleState ((bool) p.apvts.state.getProperty ("advancedOpen", false), juce::dontSendNotification);
    advancedButton.onClick = [this]
    {
        processor.apvts.state.setProperty ("advancedOpen", advancedButton.getToggleState(), nullptr);
        startTimerHz (60);
    };
    startTimerHz (30);   // sets the hover text; keeps running only while level match is on
    addAndMakeVisible (advancedButton);

    auto setupSmall = [this] (juce::Slider& s, const juce::String& tip, bool bipolar)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters (kStart, kEnd, true);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.getProperties().set ("small", true);
        s.getProperties().set ("bipolar", bipolar);
        s.setTooltip (tip);
        s.onValueChange = [this] { repaint(); };
        addAndMakeVisible (s);
    };
    setupSmall (inputKnob,   "INPUT: level going into the processing. Higher drives the compression and saturation harder.", true);
    setupSmall (limiterKnob, "LIMITER GAIN: how hard the result is pushed into the limiter. This is the loudness push; 5 dB is the default.", false);
    setupSmall (outputKnob,  "OUTPUT: final level after the limiter. Turn down to compare at equal loudness.", false);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    inputAttachment   = std::make_unique<SliderAttachment> (p.apvts, "input",   inputKnob);
    limiterAttachment = std::make_unique<SliderAttachment> (p.apvts, "limiter", limiterKnob);
    outputAttachment  = std::make_unique<SliderAttachment> (p.apvts, "output",  outputKnob);
    inputKnob.setDoubleClickReturnValue (true, 0.0);
    limiterKnob.setDoubleClickReturnValue (true, (double) MasterFuseProcessor::kLimiterGainDefault);
    outputKnob.setDoubleClickReturnValue (true, 0.0);

    // resizable by width; the height follows the artwork (plus the drawer when open)
    drawerOpen = advancedButton.getToggleState() ? 1.0f : 0.0f;
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) designHeight());
    setResizeLimits (kWidth * 3 / 5, 300, kWidth * 2, 2400);
    setSize (kWidth, juce::roundToInt (designHeight()));
}

MasterFuseEditor::~MasterFuseEditor()
{
    stopTimer();
    tooltipWindow.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MasterFuseEditor::timerCallback()
{
    // level match: the hover text shows the gain currently applied
    const bool matching = matchSwitch.getToggleState();
    const float db = processor.matchGainDb.load();
    const juce::String tip = juce::String ("Level match ") + (matching ? "ON" : "OFF") + ": turns the processed sound to the same loudness as the "
        "untouched sound, so switching the plugin on and off compares the sound, not the volume.\n"
        + (matching ? "Currently applying " + juce::String (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB. " : juce::String())
        + "Turn it off before exporting, for the full loudness.";
    if (tip != matchSwitch.getTooltip())
        matchSwitch.setTooltip (tip);

    // advanced drawer animation
    const float target = advancedButton.getToggleState() ? 1.0f : 0.0f;
    if (drawerOpen != target)
    {
        drawerOpen += (target - drawerOpen) * 0.3f;
        if (std::abs (target - drawerOpen) < 0.01f)
            drawerOpen = target;
        applyHeight();
    }
    else if (! matching)
    {
        stopTimer();
    }
}

// keeps the current width (and so the zoom) and sets the height for the drawer position
void MasterFuseEditor::applyHeight()
{
    const float s = (float) getWidth() / (float) kWidth;
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) designHeight());
    setSize (getWidth(), juce::roundToInt (designHeight() * s));
    repaint();
}

void MasterFuseEditor::animateFlow()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastFrameMs > 0.0 ? (float) juce::jmin (0.1, (now - lastFrameMs) * 0.001) : 0.0f;
    lastFrameMs = now;
    if (! lnf.active || ! isShowing())
        return;

    flowIn  = std::fmod (flowIn  + inFlow().speed * dt, 10000.0f);
    flowOut = std::fmod (flowOut + outFlow (amount01()).speed * dt, 10000.0f);

    if (now - lastRepaintMs < 1000.0 / 31.0)      // about 30 frames a second is plenty
        return;
    lastRepaintMs = now;

    // only the strips around the traces need redrawing
    const float s = (float) getWidth() / (float) kWidth;
    repaint (juce::Rectangle<float> (0.0f, 255.0f * s, 140.0f * s, 60.0f * s).getSmallestIntegerContainer());
    repaint (juce::Rectangle<float> (380.0f * s, 255.0f * s, 140.0f * s, 60.0f * s).getSmallestIntegerContainer());
}

// a red-orange warmth spreading out from behind the knob, stronger as the amount goes up
void MasterFuseEditor::drawHeatGlow (juce::Graphics& g, float s) const
{
    using namespace juce;
    if (! lnf.active)
        return;
    const float a = amount01();
    const auto c = heatColour (a);
    const Point<float> centre (260.0f * s, (float) kKnobCentreY * s);

    ColourGradient glow (c.withAlpha (0.04f + 0.22f * a * a + 0.06f * a), centre, Colours::transparentBlack, centre.translated (0.0f, (210.0f + 150.0f * a) * s), true);
    glow.addColour (0.45, c.withAlpha (0.02f + 0.09f * a * a));
    g.setGradientFill (glow);
    g.fillRect (Rectangle<float> (0.0f, 0.0f, (float) getWidth(), (float) kHeight * s));

    // and a little of it along the bottom edge, as if the board itself is warming up
    ColourGradient floor (Colours::transparentBlack, 0.0f, (float) kHeight * s - 220.0f * s, c.withAlpha (0.10f * a * a), 0.0f, (float) kHeight * s, false);
    g.setGradientFill (floor);
    g.fillRect (Rectangle<float> (0.0f, (float) kHeight * s - 220.0f * s, (float) getWidth(), 220.0f * s));
}

void MasterFuseEditor::resized()
{
    const float s = (float) getWidth() / (float) kWidth;
    auto box = [s] (float x, float y, float w, float h)
    {
        return juce::Rectangle<float> (x * s, y * s, w * s, h * s).toNearestInt();
    };

    knob.setBounds (box (120.0f, (float) kKnobCentreY - 140.0f, 280.0f, 280.0f));
    matchSwitch.setBounds (box (196.0f, 548.0f, 128.0f, 24.0f));
    enableSwitch.setBounds (box (190.0f, 580.0f, 140.0f, 58.0f));
    advancedButton.setBounds (box (190.0f, 646.0f, 140.0f, 20.0f));

    // drawer contents sit below the main panel; they are simply out of view while it is shut
    int i = 0;
    for (auto* k : { &inputKnob, &limiterKnob, &outputKnob })
        k->setBounds (box (110.0f + 150.0f * (float) i++ - 44.0f, (float) kHeight + 70.0f - 44.0f, 88.0f, 88.0f));
}

// the glowing trace from IN into the knob and out of it to OUT; dark while bypassed
void MasterFuseEditor::drawSignalTrace (juce::Graphics& g, float s) const
{
    using namespace juce;
    const float a = amount01();
    const auto hot = heatColour (a);

    for (const bool isOut : { false, true })
    {
        const auto* pts = isOut ? kTraceOut : kTraceIn;
        const float len = traceLength (pts);
        const Path line = tracePiece (pts, 0.0f, len, s);

        if (! lnf.active)
        {
            g.setColour (Pal::traceOff);
            g.strokePath (line, PathStrokeType (2.2f * s, PathStrokeType::mitered, PathStrokeType::butt));
            continue;
        }

        // the trace itself: the output side gets thicker, brighter and glows more with the amount
        const float width = (isOut ? 2.2f + 1.6f * a : 2.2f) * s;
        Path stroked;
        PathStrokeType (width, PathStrokeType::mitered, PathStrokeType::butt).createStrokedPath (stroked, line);
        const float glowAlpha = isOut ? 0.4f + 0.5f * a : 0.55f;
        const float glowRadius = isOut ? 6.0f + 12.0f * a : 7.0f;
        DropShadow ((isOut ? hot : Pal::red).withAlpha (glowAlpha), jmax (1, roundToInt (glowRadius * s)), {}).drawForPath (g, stroked);
        if (isOut && a > 0.5f)   // at high amounts a second, wider haze
            DropShadow (hot.withAlpha ((a - 0.5f) * 0.6f), jmax (1, roundToInt (22.0f * s)), {}).drawForPath (g, stroked);
        g.setGradientFill (ColourGradient (Pal::red, pts[0].x * s, 0.0f, isOut ? hot : Pal::orange, pts[kTracePoints - 1].x * s, 0.0f, false));
        g.fillPath (stroked);
    }

    g.setFont (lnf.chakraFont (8.0f * s, 0.25f));
    g.setColour ((lnf.active ? Pal::red : Colour (0xff55585f)).withAlpha (0.75f));
    g.drawText ("IN", Rectangle<float> (10.0f * s, 284.0f * s, 30.0f * s, 10.0f * s), Justification::centredLeft, false);
    g.setColour ((lnf.active ? hot : Colour (0xff55585f)).withAlpha (0.75f + 0.25f * a));
    g.drawText ("OUT", Rectangle<float> (486.0f * s, 284.0f * s, 30.0f * s, 10.0f * s), Justification::centredLeft, false);
}

// pulses of current running along the traces: faster, closer and brighter on the output side as the amount goes up
void MasterFuseEditor::drawPulses (juce::Graphics& g, float s) const
{
    using namespace juce;
    if (! lnf.active)
        return;
    const float a = amount01();
    const auto hot = heatColour (a);

    for (const bool isOut : { false, true })
    {
        const auto* pts = isOut ? kTraceOut : kTraceIn;
        const float len = traceLength (pts);
        const float width = (isOut ? 2.2f + 1.6f * a : 2.2f) * s;
        const auto flow = isOut ? outFlow (a) : inFlow();
        const float phase = std::fmod (isOut ? flowOut : flowIn, flow.spacing);
        for (float d = phase - flow.spacing; d < len; d += flow.spacing)
        {
            const Path pulse = tracePiece (pts, jmax (0.0f, d), jmin (len, d + flow.length), s);
            if (pulse.isEmpty()) continue;
            const auto core = Colour (0xffffe2c8).interpolatedWith (hot, 0.35f);
            g.setColour (hot.withAlpha (0.10f * flow.brightness));
            g.strokePath (pulse, PathStrokeType (width + 7.0f * s, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (hot.withAlpha (0.35f * flow.brightness));
            g.strokePath (pulse, PathStrokeType (width + 3.0f * s, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (core.withAlpha (flow.brightness));
            g.strokePath (pulse, PathStrokeType (width * 0.7f, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }
}

// the amount in a small dark window, in digits that heat up with it
void MasterFuseEditor::drawReadout (juce::Graphics& g, float s) const
{
    using namespace juce;
    const Rectangle<float> box (206.0f * s, 500.0f * s, 108.0f * s, 36.0f * s);
    g.setColour (Colour (0xff070708));
    g.fillRoundedRectangle (box, 3.0f * s);
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.9f), 0.0f, box.getY(), Colours::transparentBlack, 0.0f, box.getY() + 8.0f * s, false));
    g.fillRoundedRectangle (box.withHeight (8.0f * s), 3.0f * s);
    g.setColour (Colour (0xff2d2f34));
    g.drawRoundedRectangle (box.reduced (0.5f * s), 3.0f * s, 1.0f * s);

    const String text = String (roundToInt (knob.getValue())) + "%";
    const String ghost = String::repeatedString ("8", text.length() - 1) + "%";
    const Font f = lnf.chakraFont (26.0f * s, 0.08f);
    const auto textBox = box.translated (1.0f * s, -1.0f * s);

    // the digits heat up with the amount: red, orange, then amber near 100 %
    const float a = amount01();
    const auto col = lnf.active ? heatColour (a) : Pal::red.interpolatedWith (Pal::steel, 0.45f);

    g.setFont (f);
    g.setColour (col.withAlpha (0.07f));
    g.drawText (ghost, textBox, Justification::centred, false);

    Path digits;
    GlyphArrangement ga;
    ga.addFittedText (f, text, textBox.getX(), textBox.getY(), textBox.getWidth(), textBox.getHeight(), Justification::centred, 1);
    ga.createPath (digits);
    if (lnf.active)
        DropShadow (col.withAlpha (0.45f + 0.4f * a), jmax (1, roundToInt ((7.0f + 7.0f * a) * s)), {}).drawForPath (g, digits);
    g.setColour (col);
    g.fillPath (digits);
}

void MasterFuseEditor::drawDrawer (juce::Graphics& g, float s) const
{
    using namespace juce;
    const Rectangle<float> drawer (0.0f, (float) kHeight * s, (float) getWidth(), (float) kDrawerHeight * s);
    g.setGradientFill (ColourGradient (Colour (0xff0b0c0e), 0.0f, drawer.getY(), Colour (0xff141518), 0.0f, drawer.getBottom(), false));
    g.fillRect (drawer);
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.6f), 0.0f, drawer.getY(), Colours::transparentBlack, 0.0f, drawer.getY() + 14.0f * s, false));
    g.fillRect (drawer.withHeight (14.0f * s));
    g.setColour (Pal::red.withAlpha (0.35f));
    g.fillRect (drawer.withHeight (jmax (1.0f, 1.0f * s)));

    struct Item { const juce::Slider* knob; const char* name; bool sign; };
    const Item items[] = { { &inputKnob, "INPUT", true }, { &limiterKnob, "LIMITER GAIN", false }, { &outputKnob, "OUTPUT", true } };
    for (const auto& it : items)
    {
        const float cx = (float) it.knob->getBounds().getCentreX();
        const Rectangle<float> col (cx - 75.0f * s, drawer.getY(), 150.0f * s, drawer.getHeight());

        g.setFont (lnf.chakraFont (10.0f * s, 0.3f));
        g.setColour (Pal::steel);
        g.drawText (it.name, col.withY (drawer.getY() + 16.0f * s).withHeight (14.0f * s).translated (1.5f * s, 0.0f), Justification::centred, false);

        const double v = it.knob->getValue();
        const String value = ((it.sign && v > 0.049) ? "+" : "") + String (std::abs (v) < 0.05 ? 0.0 : v, 1) + " dB";
        g.setFont (lnf.chakraFont (14.0f * s, 0.07f));
        g.setColour (Pal::silver);
        g.drawText (value, col.withY (drawer.getY() + 116.0f * s).withHeight (20.0f * s), Justification::centred, false);
    }
}

void MasterFuseEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    const float s = (float) getWidth() / (float) kWidth;

    // the board layer, rebuilt only when something it shows has changed (at the screen's real pixel density)
    const float pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const float a = amount01();
    if (! boardCache.isValid() || cacheWidth != getWidth() || cacheScale != pixelScale
        || cacheActive != lnf.active || std::abs (cacheAmount - a) > 0.0005f)
    {
        boardCache = Image (Image::RGB, jmax (1, roundToInt ((float) getWidth() * pixelScale)),
                            jmax (1, roundToInt ((float) kHeight * s * pixelScale)), false);
        Graphics cg (boardCache);
        cg.addTransform (AffineTransform::scale (pixelScale));
        cg.fillAll (Pal::black);
        if (background.isValid())
        {
            cg.setImageResamplingQuality (Graphics::highResamplingQuality);
            cg.drawImage (background, Rectangle<float> (0.0f, 0.0f, (float) getWidth(), (float) kHeight * s), RectanglePlacement::stretchToFit);
        }
        drawHeatGlow (cg, s);
        drawSignalTrace (cg, s);
        cacheWidth = getWidth(); cacheScale = pixelScale; cacheActive = lnf.active; cacheAmount = a;
    }
    g.fillAll (Pal::black);
    g.drawImage (boardCache, Rectangle<float> (0.0f, 0.0f, (float) getWidth(), (float) kHeight * s), RectanglePlacement::stretchToFit);

    drawPulses (g, s);
    drawReadout (g, s);
    if (drawerOpen > 0.0f)
        drawDrawer (g, s);
}
