#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    const float kStart = juce::degreesToRadians (225.0f);
    const float kEnd   = juce::degreesToRadians (495.0f);

    const juce::Colour kMoon  { 0xffcdd4e0 };     // cold moonlight
    const juce::Colour kEmber { 0xffc41820 };     // the red it turns into
    const juce::Colour kCandle { 0xffe28a4a };
    const juce::Colour kGrey  { 0xff5a5a64 };
    const juce::Colour kTickOff { 0xff1e1e26 };

    float smooth01 (float e0, float e1, float x) { const float t = juce::jlimit (0.0f, 1.0f, (x - e0) / (e1 - e0)); return t * t * (3.0f - 2.0f * t); }

    // moonlight at the start of the range, ember red towards the end
    juce::Colour accent (float t) { return kMoon.interpolatedWith (kEmber, juce::jlimit (0.0f, 1.0f, (t - 0.35f) / 0.6f)); }

    juce::Point<float> polar (juce::Point<float> c, float angle, float r) { return { c.x + r * std::sin (angle), c.y - r * std::cos (angle) }; }

    // fixed noise for the waves (the same every time, so they scroll instead of shimmering)
    struct Noise
    {
        float v[512];
        Noise() { juce::Random r (3); for (auto& x : v) x = r.nextFloat() - 0.5f; }
        float at (float x) const
        {
            const float p = x * 0.5f;
            const int i = (int) std::floor (p);
            const float f = p - (float) i;
            return v[i & 511] + (v[(i + 1) & 511] - v[i & 511]) * f;
        }
    };
    const Noise& noise() { static const Noise n; return n; }

    // a ring of ticks; lit ones glow in their accent colour
    void drawTicks (juce::Graphics& g, juce::Point<float> c, int count, float r1, float r2, float rMajor, float from, float to,
                    float a0, float a1, bool active, float width, float majorWidth)
    {
        using namespace juce;
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < count; ++i)
            {
                const float t = (float) i / (float) (count - 1), a = a0 + (a1 - a0) * t;
                const bool lit = to - from > 1.0e-3f && t >= from - 1.0e-4f && t <= to + 1.0e-4f;
                const bool major = majorWidth > 0.0f && i % 5 == 0;
                Path p;
                p.startNewSubPath (polar (c, a, r1));
                p.lineTo (polar (c, a, major ? rMajor : r2));
                const float w = major ? majorWidth : width;
                if (pass == 0)
                {
                    if (lit && active)       // soft glow under the lit ones
                    {
                        g.setColour (accent (t).withAlpha (0.18f));
                        g.strokePath (p, PathStrokeType (w * 3.2f, PathStrokeType::curved, PathStrokeType::rounded));
                    }
                    continue;
                }
                g.setColour (lit ? (active ? accent (t) : kGrey.withAlpha (0.8f)) : kTickOff);
                g.strokePath (p, PathStrokeType (w, PathStrokeType::mitered, PathStrokeType::butt));
            }
    }
}

//==============================================================================
DarkLookAndFeel::DarkLookAndFeel()
{
    setColour (juce::TooltipWindow::textColourId, kMoon);
}

// sizes are CSS-like pixel sizes; JUCE's font height is the line height (Oswald 1.482x, Montserrat 1.219x)
juce::Font DarkLookAndFeel::oswaldFont (float size, float track) const
{
    const float h = size * 1.482f;
    return juce::Font (juce::FontOptions (oswald).withHeight (h)).withExtraKerningFactor (track / h);
}
juce::Font DarkLookAndFeel::montFont (float size, float track) const
{
    const float h = size * 1.219f;
    return juce::Font (juce::FontOptions (montserrat).withHeight (h)).withExtraKerningFactor (track / h);
}

void DarkLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float a0, float a1, juce::Slider& slider)
{
    using namespace juce;
    const auto area = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto c = area.getCentre();
    const float angle = a0 + pos * (a1 - a0);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);

    // ---- small knobs in the advanced drawer (88 px square)
    if ((bool) slider.getProperties().getWithDefault ("small", false))
    {
        const float u = jmin (area.getWidth(), area.getHeight()) / 88.0f;
        const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);
        const float from = bipolar ? jmin (0.5f, pos) : 0.0f, to = bipolar ? jmax (0.5f, pos) : pos;
        drawTicks (g, c, 15, 36.0f * u, 42.0f * u, 0.0f, from, to, a0, a1, active, 2.0f * u, 0.0f);
        if (smallKnob.isValid())
            g.drawImage (smallKnob, Rectangle<float> (88.0f * u, 88.0f * u).withCentre (c), RectanglePlacement::stretchToFit);
        Path p; p.startNewSubPath (polar (c, angle, 12.0f * u)); p.lineTo (polar (c, angle, 26.0f * u));
        const auto col = active ? accent (bipolar ? std::abs (pos - 0.5f) * 2.0f : pos) : kGrey;
        g.setColour (col.withAlpha (0.25f));
        g.strokePath (p, PathStrokeType (7.0f * u, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (p, PathStrokeType (3.0f * u, PathStrokeType::curved, PathStrokeType::rounded));
        return;
    }

    // ---- the main knob (280 px square): ticks, the moon being eclipsed, the pointer
    const float u = jmin (area.getWidth(), area.getHeight()) / 280.0f;
    drawTicks (g, c, 31, 112.0f * u, 126.0f * u, 130.0f * u, 0.0f, pos, a0, a1, active, 2.2f * u, 3.0f * u);

    const float ec = active ? pos : 0.0f;                      // how far into the eclipse
    const float red = smooth01 (0.45f, 0.95f, ec);             // from about halfway it turns into a blood moon
    const float R = 78.0f * u;
    const auto moonBox = Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c);
    if (moonPale.isValid())  g.drawImage (moonPale, moonBox, RectanglePlacement::stretchToFit);
    if (red > 0.001f && moonBlood.isValid())
    {
        g.setOpacity (red);
        g.drawImage (moonBlood, moonBox, RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);
    }
    // the Earth's shadow sliding in from the right
    {
        Graphics::ScopedSaveState save (g);
        Path disc; disc.addEllipse (moonBox);
        g.reduceClipRegion (disc);
        const float sr = 82.0f * u, soft = 6.0f * u;
        const Point<float> sc (c.x + 2.1f * 84.0f * u * (1.0f - ec), c.y - 4.0f * u * (1.0f - ec));
        const auto shade = Colour (4, 2, 3).withAlpha (0.92f - 0.3f * ec);
        ColourGradient grad (shade, sc, shade.withAlpha (0.0f), sc.translated (sr + soft, 0.0f), true);
        grad.addColour ((sr - soft) / (sr + soft), shade);
        g.setGradientFill (grad);
        g.fillEllipse (Rectangle<float> ((sr + soft) * 2.0f, (sr + soft) * 2.0f).withCentre (sc));
    }
    g.setColour (accent (ec).withAlpha (0.35f + 0.3f * ec));
    g.drawEllipse (Rectangle<float> (158.0f * u, 158.0f * u).withCentre (c), 1.5f * u);

    // pointer: a glowing notch in the bezel
    Path p; p.startNewSubPath (polar (c, angle, 88.0f * u)); p.lineTo (polar (c, angle, 100.0f * u));
    const auto pc = active ? accent (pos) : kGrey;
    g.setColour (pc.withAlpha (0.25f));
    g.strokePath (p, PathStrokeType (10.0f * u, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (pc);
    g.strokePath (p, PathStrokeType (4.0f * u, PathStrokeType::curved, PathStrokeType::rounded));
}

void DarkLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    using namespace juce;
    const bool on = b.getToggleState();
    const auto area = b.getLocalBounds().toFloat();

    // ---- the ADVANCED drawer handle
    if ((bool) b.getProperties().getWithDefault ("drawer", false))
    {
        const float u = area.getWidth() / 140.0f;
        const Font f = montFont (9.0f * u, 3.0f * u);
        const float tw = GlyphArrangement::getStringWidth (f, "ADVANCED");
        const float x0 = area.getCentreX() - (tw + 12.0f * u) * 0.5f;
        g.setColour (highlighted ? kMoon.withAlpha (0.85f) : Colour (0x99aaafbe));
        g.setFont (f);
        g.drawText ("ADVANCED", Rectangle<float> (x0, area.getY(), tw + 4.0f * u, area.getHeight()), Justification::centredLeft, false);
        const float cx = x0 + tw + 7.0f * u, cy = area.getCentreY(), d = 3.0f * u;
        Path chevron;
        chevron.addTriangle (cx - d, cy + (on ? d * 0.6f : -d * 0.6f), cx + d, cy + (on ? d * 0.6f : -d * 0.6f), cx, cy + (on ? -d * 0.6f : d * 0.6f));
        g.fillPath (chevron);
        return;
    }

    // ---- on / bypass: a candle, lit when on, snuffed with a wisp of smoke when bypassed (120 x 86)
    const float u = area.getWidth() / 120.0f;
    const Point<float> base (60.0f * u, 62.0f * u);
    if (on)
    {
        const Point<float> gc (60.0f * u, 32.0f * u);
        ColourGradient glow (Colour (255, 170, 90).withAlpha (0.33f + 0.04f * flicker), gc, Colour (255, 120, 60).withAlpha (0.0f), gc.translated (46.0f * u, 0.0f), true);
        g.setGradientFill (glow);
        g.fillEllipse (Rectangle<float> (92.0f * u, 92.0f * u).withCentre (gc));
    }
    if (candle.isValid())
        g.drawImage (candle, Rectangle<float> (30.0f * u, 22.0f * u, 60.0f * u, 50.0f * u), RectanglePlacement::stretchToFit);
    if (on)
    {
        // the flame, flickering a little
        const float hgt = (19.0f + 1.6f * flicker) * u, sway = 0.9f * flicker * u;
        const float bottom = base.y - 28.0f * u, tipY = bottom - hgt;
        Path flame;
        flame.startNewSubPath (base.x + sway, tipY);
        flame.quadraticTo (base.x + 6.0f * u, bottom - hgt * 0.42f, base.x, bottom);
        flame.quadraticTo (base.x - 6.0f * u, bottom - hgt * 0.42f, base.x + sway, tipY);
        flame.closeSubPath();
        g.setColour (Colour (255, 160, 70).withAlpha (0.35f));
        g.strokePath (flame, PathStrokeType (5.0f * u, PathStrokeType::curved, PathStrokeType::rounded));
        ColourGradient fg (Colour (255, 240, 200).withAlpha (0.95f), 0.0f, tipY, Colour (0xffc4301a), 0.0f, bottom, false);
        fg.addColour (0.6, Colour (0xffffb347));
        g.setGradientFill (fg);
        g.fillPath (flame);
    }
    else
    {
        // smoke drifting up from the wick
        const float t = smokeTime, wob = std::sin (t * 1.3f) * 2.0f * u;
        Path smoke;
        smoke.startNewSubPath (base.x, base.y - 31.0f * u);
        smoke.cubicTo (base.x - 7.0f * u + wob, base.y - 37.0f * u, base.x + 7.0f * u - wob, base.y - 42.0f * u, base.x - 1.0f * u + wob, base.y - 49.0f * u);
        smoke.cubicTo (base.x - 6.0f * u - wob, base.y - 53.0f * u, base.x + 4.0f * u + wob, base.y - 56.0f * u, base.x + 1.0f * u + wob * 1.5f, base.y - 60.0f * u);
        g.setColour (Colour (170, 175, 190).withAlpha (0.35f));
        g.strokePath (smoke, PathStrokeType (1.5f * u, PathStrokeType::curved, PathStrokeType::rounded));
    }
    if (highlighted)
    {
        g.setColour (Colours::white.withAlpha (0.03f));
        g.fillEllipse (Rectangle<float> (70.0f * u, 70.0f * u).withCentre ({ 60.0f * u, 44.0f * u }));
    }
    g.setFont (oswaldFont (10.0f * u, 3.0f * u));
    g.setColour (on ? kCandle : Colour (0xb3969aa5));
    g.drawText (on ? "ON" : "BYPASSED", Rectangle<float> (1.5f * u, 71.0f * u, area.getWidth(), 14.0f * u), Justification::centred, false);
}

juce::TextLayout DarkLookAndFeel::tooltipLayout (const juce::String& text) const
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::topLeft);
    s.append (text, montFont (12.0f), juce::Colour (0xffc9ced8));
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, 280.0f);
    return tl;
}

juce::Rectangle<int> DarkLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> area)
{
    const auto tl = tooltipLayout (text);
    const int w = (int) std::ceil (tl.getWidth()) + 26, h = (int) std::ceil (tl.getHeight()) + 16;
    const int x = pos.x > area.getCentreX() ? pos.x - w - 6 : pos.x + 14;
    const int y = pos.y + h + 24 > area.getBottom() ? pos.y - h - 6 : pos.y + 20;
    return juce::Rectangle<int> (x, y, w, h).constrainedWithin (area);
}

void DarkLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    g.setColour (juce::Colour (0xff0b0b0e));
    g.fillRect (r);
    g.setColour (juce::Colour (0xff26262e));
    g.drawRect (r, 1.0f);
    g.setColour (kEmber);
    g.fillRect (0.0f, 0.0f, 3.0f, (float) h);
    tooltipLayout (text).draw (g, r.withTrimmedLeft (15.0f).withTrimmedRight (10.0f).withTrimmedTop (8.0f).withTrimmedBottom (8.0f));
}

//==============================================================================
DarkEditor::DarkEditor (DarkProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    using juce::ImageCache;
    bgLight        = ImageCache::getFromMemory (BinaryData::bg_light_png, BinaryData::bg_light_pngSize);
    bgDark         = ImageCache::getFromMemory (BinaryData::bg_dark_png, BinaryData::bg_dark_pngSize);
    lnf.moonPale   = ImageCache::getFromMemory (BinaryData::moon_pale_png, BinaryData::moon_pale_pngSize);
    lnf.moonBlood  = ImageCache::getFromMemory (BinaryData::moon_blood_png, BinaryData::moon_blood_pngSize);
    lnf.candle     = ImageCache::getFromMemory (BinaryData::candle_png, BinaryData::candle_pngSize);
    lnf.smallKnob  = ImageCache::getFromMemory (BinaryData::knob_small_png, BinaryData::knob_small_pngSize);
    lnf.oswald     = juce::Typeface::createSystemTypefaceFor (BinaryData::OswaldSemiBold_ttf, BinaryData::OswaldSemiBold_ttfSize);
    lnf.montserrat = juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratMedium_ttf, BinaryData::MontserratMedium_ttfSize);
    lnf.setDefaultSansSerifTypeface (lnf.montserrat);
    setLookAndFeel (&lnf);
    tooltipWindow.setLookAndFeel (&lnf);

    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setRotaryParameters (kStart, kEnd, true);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    knob.setMouseDragSensitivity (220);
    knob.setTooltip ("DARKNESS: how dark the sound gets. Low settings tilt the highs down and add warmth; higher settings add weight and "
                     "saturation and close the top; near 100% it sounds like it is coming through a wall. Double-click to return to 50%.");
    addAndMakeVisible (knob);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, "amount", knob);
    knob.setDoubleClickReturnValue (true, (double) DarkProcessor::kAmountDefault);
    knob.onValueChange = [this] { repaint(); };

    enableSwitch.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    enableSwitch.setTooltip ("Light or snuff the candle: switch DARK on or off (bypass) to compare with the untouched sound");
    addAndMakeVisible (enableSwitch);
    enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "enabled", enableSwitch);
    enableSwitch.onStateChange = [this]
    {
        if (lnf.active != enableSwitch.getToggleState()) { lnf.active = enableSwitch.getToggleState(); repaint(); }
    };
    lnf.active = enableSwitch.getToggleState();


    advancedButton.getProperties().set ("drawer", true);
    advancedButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    advancedButton.setToggleState ((bool) p.apvts.state.getProperty ("advancedOpen", false), juce::dontSendNotification);
    advancedButton.onClick = [this]
    {
        processor.apvts.state.setProperty ("advancedOpen", advancedButton.getToggleState(), nullptr);
        startTimerHz (60);
    };
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
    setupSmall (punchKnob,  "PUNCH: lets the hits through. The top opens for a moment on every attack, so drums keep their snap "
                            "while the tails and cymbals go dark. 0% darkens everything evenly.", false);
    setupSmall (mixKnob,    "MIX: blends the untouched sound back in. 100% is fully dark.", false);
    setupSmall (outputKnob, "OUTPUT: final level. Darker can sound a little quieter; turn this up to make up for it.", true);
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    punchAttachment  = std::make_unique<SliderAttachment> (p.apvts, "punch",  punchKnob);
    mixAttachment    = std::make_unique<SliderAttachment> (p.apvts, "mix",    mixKnob);
    outputAttachment = std::make_unique<SliderAttachment> (p.apvts, "output", outputKnob);
    punchKnob.setDoubleClickReturnValue (true, (double) DarkProcessor::kPunchDefault);
    mixKnob.setDoubleClickReturnValue (true, 100.0);
    outputKnob.setDoubleClickReturnValue (true, 0.0);

    drawerOpen = advancedButton.getToggleState() ? 1.0f : 0.0f;
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) designHeight());
    setResizeLimits (kWidth * 3 / 5, 300, kWidth * 2, 2400);
    setSize (kWidth, juce::roundToInt (designHeight()));
}

DarkEditor::~DarkEditor()
{
    stopTimer();
    tooltipWindow.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void DarkEditor::timerCallback()
{
    const float target = advancedButton.getToggleState() ? 1.0f : 0.0f;
    if (drawerOpen != target)
    {
        drawerOpen += (target - drawerOpen) * 0.3f;
        if (std::abs (target - drawerOpen) < 0.01f) drawerOpen = target;
        applyHeight();
    }
    else
        stopTimer();
}

void DarkEditor::applyHeight()
{
    const float s = (float) getWidth() / (float) kWidth;
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) designHeight());
    setSize (getWidth(), juce::roundToInt (designHeight() * s));
    repaint();
}

// waves scroll, the flame flickers, the smoke drifts: about 30 frames a second, small areas only
void DarkEditor::animate()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastFrameMs > 0.0 ? (float) juce::jmin (0.1, (now - lastFrameMs) * 0.001) : 0.0f;
    lastFrameMs = now;
    if (! isShowing())
        return;
    wavePhase = std::fmod (wavePhase + dt * (lnf.active ? 38.0f : 14.0f), 1024.0f);
    const float t = (float) (now * 0.001);
    lnf.flicker = 0.6f * std::sin (t * 11.0f) + 0.4f * std::sin (t * 23.7f + 1.3f) * std::sin (t * 3.1f);
    lnf.smokeTime += dt;

    if (now - lastRepaintMs < 1000.0 / 31.0)
        return;
    lastRepaintMs = now;
    const float s = (float) getWidth() / (float) kWidth;
    repaint (juce::Rectangle<float> (0.0f, 258.0f * s, 132.0f * s, 50.0f * s).getSmallestIntegerContainer());
    repaint (juce::Rectangle<float> (388.0f * s, 258.0f * s, 132.0f * s, 50.0f * s).getSmallestIntegerContainer());
    enableSwitch.repaint (0, 0, enableSwitch.getWidth(), juce::roundToInt (enableSwitch.getHeight() * 0.62f));
}

void DarkEditor::resized()
{
    const float s = (float) getWidth() / (float) kWidth;
    auto box = [s] (float x, float y, float w, float h) { return juce::Rectangle<float> (x * s, y * s, w * s, h * s).toNearestInt(); };
    knob.setBounds (box (120.0f, (float) kKnobCentreY - 140.0f, 280.0f, 280.0f));
    enableSwitch.setBounds (box (200.0f, 554.0f, 120.0f, 86.0f));
    advancedButton.setBounds (box (190.0f, 652.0f, 140.0f, 20.0f));
    int i = 0;
    for (auto* k : { &punchKnob, &mixKnob, &outputKnob })
        k->setBounds (box (110.0f + 150.0f * (float) i++ - 44.0f, (float) kHeight + 70.0f - 44.0f, 88.0f, 88.0f));
}

// the signal: bright and jagged going in, smooth and dark coming out
void DarkEditor::drawWaves (juce::Graphics& g, float s) const
{
    using namespace juce;
    const float yT = 282.0f, a = lnf.active ? amount01() : 0.0f;
    const auto& n = noise();
    auto wave = [&] (float x0, float x1, float sm)
    {
        Path p;
        for (float x = x0; x <= x1 + 0.01f; x += 2.0f)
        {
            const float ph = x + wavePhase;
            const float edge = jmin (1.0f, (x - x0) / 20.0f, (x1 - x) / 6.0f + 1.0f);
            const float y = yT + std::sin (ph * 0.18f) * (7.0f + 3.0f * sm) + n.at (ph) * 9.0f * (1.0f - sm) * jmax (0.0f, edge);
            x == x0 ? p.startNewSubPath (x * s, y * s) : p.lineTo (x * s, y * s);
        }
        return p;
    };
    const Path in = wave (0.0f, 128.0f, 0.0f);
    g.setColour (kMoon.withAlpha (0.14f));
    g.strokePath (in, PathStrokeType (5.0f * s, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (kMoon.withAlpha (0.75f));
    g.strokePath (in, PathStrokeType (1.4f * s, PathStrokeType::curved, PathStrokeType::rounded));

    const Path out = wave (392.0f, 520.0f, a);
    const auto oc = lnf.active ? accent (a) : kMoon;
    g.setColour (oc.withAlpha (0.12f + 0.12f * a));
    g.strokePath (out, PathStrokeType ((5.0f + 5.0f * a) * s, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (oc.withAlpha (0.5f + 0.4f * (1.0f - a * 0.4f)));
    g.strokePath (out, PathStrokeType (1.4f * s, PathStrokeType::curved, PathStrokeType::rounded));
}

// the small eclipse under the title follows the knob
void DarkEditor::drawEmblem (juce::Graphics& g, float s) const
{
    using namespace juce;
    const float a = lnf.active ? amount01() : 0.0f;
    const Point<float> c (260.0f * s, 120.0f * s);
    const float r = 8.0f * s;
    g.setColour (kMoon.withAlpha (0.8f));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    {
        Graphics::ScopedSaveState save (g);
        Path disc; disc.addEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        g.reduceClipRegion (disc);
        g.setColour (Colour (0xff07070a));
        g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c.translated (r * (1.6f - 1.6f * a), -1.0f * s)));
    }
    g.setColour (accent (a).withAlpha (0.7f));
    g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.0f * s);
}

void DarkEditor::drawReadout (juce::Graphics& g, float s) const
{
    using namespace juce;
    const Rectangle<float> box (206.0f * s, 496.0f * s, 108.0f * s, 36.0f * s);
    g.setColour (Colour (0xff09080b));
    g.fillRect (box);
    g.setColour (Colour (0xff25232b));
    g.drawRect (box.reduced (0.5f * s), 1.0f * s);

    const String text = String (roundToInt (knob.getValue())) + "%";
    const auto col = lnf.active ? accent (amount01()) : kGrey;
    const Font f = lnf.oswaldFont (26.0f * s, 2.0f * s);
    const auto textBox = box.translated (1.0f * s, 0.0f).expanded (0.0f, 6.0f * s);
    g.setFont (f);
    if (lnf.active)
    {
        g.setColour (col.withAlpha (0.16f));
        for (auto o : { Point<float> (-1.5f, 0), Point<float> (1.5f, 0), Point<float> (0, -1.5f), Point<float> (0, 1.5f) })
            g.drawText (text, textBox + o * s, Justification::centred, false);
    }
    g.setColour (col);
    g.drawText (text, textBox, Justification::centred, false);
}

void DarkEditor::drawDrawer (juce::Graphics& g, float s) const
{
    using namespace juce;
    const Rectangle<float> drawer (0.0f, (float) kHeight * s, (float) getWidth(), (float) kDrawerHeight * s);
    g.setColour (Colour (0xff060608));
    g.fillRect (drawer);
    const auto line = lnf.active ? accent (amount01()) : kGrey;
    ColourGradient sep (line.withAlpha (0.0f), 0.0f, 0.0f, line.withAlpha (0.0f), drawer.getWidth(), 0.0f, false);
    sep.addColour (0.5, line.withAlpha (0.7f));
    g.setGradientFill (sep);
    g.fillRect (drawer.withHeight (jmax (1.0f, 1.0f * s)));

    const Slider* knobs[] = { &punchKnob, &mixKnob, &outputKnob };
    const char* names[] = { "PUNCH", "MIX", "OUTPUT" };
    for (int i = 0; i < 3; ++i)
    {
        const float cx = (float) knobs[i]->getBounds().getCentreX();
        const Rectangle<float> col (cx - 75.0f * s, drawer.getY(), 150.0f * s, drawer.getHeight());
        g.setFont (lnf.montFont (10.0f * s, 3.0f * s));
        g.setColour (Colour (0xbfbec3d2));
        g.drawText (names[i], col.withY (drawer.getY() + 15.0f * s).withHeight (14.0f * s).translated (1.5f * s, 0.0f), Justification::centred, false);
        const double v = knobs[i]->getValue();
        const String value = i == 2 ? ((v > 0.049 ? "+" : "") + String (std::abs (v) < 0.05 ? 0.0 : v, 1) + " dB") : String (roundToInt (v)) + "%";
        g.setFont (lnf.oswaldFont (14.0f * s, 1.0f * s));
        g.setColour (Colour (0xe6d7dae4));
        g.drawText (value, col.withY (drawer.getY() + 120.0f * s).withHeight (22.0f * s), Justification::centred, false);
    }
}

void DarkEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    const float s = (float) getWidth() / (float) kWidth;
    const float pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const float d = darkness();
    const auto mainArea = Rectangle<float> (0.0f, 0.0f, (float) getWidth(), (float) kHeight * s);

    // the room: light and dark versions blended for the current darkness
    if (! roomCache.isValid() || cacheWidth != getWidth() || cacheScale != pixelScale || std::abs (cacheDark - d) > 0.002f)
    {
        roomCache = Image (Image::RGB, jmax (1, roundToInt ((float) getWidth() * pixelScale)), jmax (1, roundToInt ((float) kHeight * s * pixelScale)), false);
        Graphics cg (roomCache);
        cg.addTransform (AffineTransform::scale (pixelScale));
        cg.setImageResamplingQuality (Graphics::highResamplingQuality);
        cg.fillAll (Colour (0xff050508));
        if (bgLight.isValid()) cg.drawImage (bgLight, mainArea, RectanglePlacement::stretchToFit);
        if (bgDark.isValid() && d > 0.001f)
        {
            cg.setOpacity (d);
            cg.drawImage (bgDark, mainArea, RectanglePlacement::stretchToFit);
            cg.setOpacity (1.0f);
        }
        cacheWidth = getWidth(); cacheScale = pixelScale; cacheDark = d;
    }
    g.drawImage (roomCache, mainArea, RectanglePlacement::stretchToFit);

    drawEmblem (g, s);
    drawWaves (g, s);
    drawReadout (g, s);
    if (drawerOpen > 0.0f)
        drawDrawer (g, s);
}
