#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    // forged steel with a molten seam (the logo): steel for the metal and the words, molten orange for what's live
    const juce::Colour kIron (0xff1a1817), kAsh (0xffb4aca6), kDim (0xff7f7873), kText (0xffeae6e2), kSteel (0xffd6d0cb), kMolten (0xffff8a3a);
    const juce::Colour kRed (0xffd4300a), kOrange (0xffff6a1a), kAmber (0xffffb347), kYellow (0xffffd98a);

    // a colour along the heat scale: 0 dark red .. 1 white-yellow
    juce::Colour heatColour (float v)
    {
        v = juce::jlimit (0.0f, 1.0f, v);
        const juce::Colour stops[] = { juce::Colour (0xff7a1404), kRed, kOrange, kAmber, juce::Colour (0xfffff1c8) };
        const float p = v * 4.0f; const int i = juce::jmin (3, (int) p);
        return stops[i].interpolatedWith (stops[i + 1], p - (float) i);
    }
    // a molten line along a path: glow, an orange core, a white-hot centre
    void moltenLine (juce::Graphics& g, const juce::Path& p, float w, float heat = 1.0f)
    {
        using namespace juce;
        g.setColour (kOrange.withAlpha (0.10f * heat)); g.strokePath (p, PathStrokeType (w + 12.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (kOrange.withAlpha (0.28f * heat)); g.strokePath (p, PathStrokeType (w + 4.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0xffe8501a).interpolatedWith (kOrange, heat)); g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0xffffe6b0).withAlpha (0.35f + 0.55f * heat)); g.strokePath (p, PathStrokeType (jmax (0.8f, w * 0.32f), PathStrokeType::curved, PathStrokeType::rounded));
    }
    // a soft blur (three box passes each way), for glows baked into the background
    void blurImage (juce::Image& img, int radius)
    {
        if (radius < 1) return;
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
        const int w = bd.width, h = bd.height;
        std::vector<float> buf ((size_t) juce::jmax (w, h) * 4), tmp ((size_t) juce::jmax (w, h) * 4);
        auto pass = [&] (int n, auto get, auto set)
        {
            for (int i = 0; i < n; ++i) { auto c = get (i); buf[(size_t) i * 4] = c.getAlpha(); buf[(size_t) i * 4 + 1] = c.getRed(); buf[(size_t) i * 4 + 2] = c.getGreen(); buf[(size_t) i * 4 + 3] = c.getBlue(); }
            for (int it = 0; it < 3; ++it)
            {
                for (int ch = 0; ch < 4; ++ch)
                {
                    float acc = 0; int cnt = 0;
                    for (int i = -radius; i < n + radius; ++i)
                    {
                        const int in = i + radius, out = i - radius;
                        if (in >= 0 && in < n) { acc += buf[(size_t) in * 4 + ch]; ++cnt; }
                        if (i >= 0 && i < n) tmp[(size_t) i * 4 + ch] = acc / (float) (2 * radius + 1);
                        if (out >= 0 && out < n) { acc -= buf[(size_t) out * 4 + ch]; --cnt; }
                    }
                }
                std::swap (buf, tmp);
            }
            for (int i = 0; i < n; ++i)
                set (i, juce::PixelARGB ((juce::uint8) juce::jlimit (0.0f, 255.0f, buf[(size_t) i * 4]), (juce::uint8) juce::jlimit (0.0f, 255.0f, buf[(size_t) i * 4 + 1]),
                                         (juce::uint8) juce::jlimit (0.0f, 255.0f, buf[(size_t) i * 4 + 2]), (juce::uint8) juce::jlimit (0.0f, 255.0f, buf[(size_t) i * 4 + 3])));
        };
        auto px = [&] (int x, int y) { return reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y)); };
        for (int y = 0; y < h; ++y) pass (w, [&] (int i) { return *px (i, y); }, [&] (int i, juce::PixelARGB c) { *px (i, y) = c; });
        for (int x = 0; x < w; ++x) pass (h, [&] (int i) { return *px (x, i); }, [&] (int i, juce::PixelARGB c) { *px (x, i) = c; });
    }
    juce::String minus() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")); }
    juce::String dbText (float v, bool plus)
    {
        const float a = std::abs (v) < 0.05f ? 0.0f : v;
        return (a > 0 && plus ? "+" : a < 0 ? minus() : juce::String()) + juce::String (std::abs (a), 1) + " dB";
    }
}

//==============================================================================
PyreLookAndFeel::PyreLookAndFeel()
{
    oswald = juce::Typeface::createSystemTypefaceFor (BinaryData::OswaldSemiBold_ttf, BinaryData::OswaldSemiBold_ttfSize);
    mont = juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratMedium_ttf, BinaryData::MontserratMedium_ttfSize);
    setColour (juce::TooltipWindow::textColourId, kText);
}

juce::Font PyreLookAndFeel::title (float size, float tracking) const
{
    juce::Font f (juce::FontOptions (oswald).withHeight (size));
    return f.withExtraKerningFactor (tracking / size);
}

juce::Font PyreLookAndFeel::body (float size, float tracking) const
{
    juce::Font f (juce::FontOptions (mont).withHeight (size));
    return f.withExtraKerningFactor (tracking / size);
}

void PyreLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& sl)
{
    using namespace juce;
    const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto c = b.getCentre();
    const float ringR = jmin (b.getWidth(), b.getHeight()) * 0.5f - 6.0f;
    const float r = ringR - (ringR > 40.0f ? 12.0f : 8.0f);
    const bool bipolar = sl.getProperties()["bipolar"];
    const float ang = a0 + pos * (a1 - a0);
    const float thick = ringR > 40.0f ? 5.0f : 3.5f;

    const float amount = bipolar ? std::abs (pos - 0.5f) * 2.0f : pos;

    // the ring: a groove cut in the steel, molten where the value is
    {
        Path track; track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, a0, a1, true);
        g.setColour (Colour (0xff070606)); g.strokePath (track, PathStrokeType (thick + 2.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0xff2b2826)); g.strokePath (track, PathStrokeType (thick - 1.0f, PathStrokeType::curved, PathStrokeType::rounded));
        const float from = bipolar ? (a0 + a1) * 0.5f : a0;
        if (std::abs (ang - from) > 0.01f)
        {
            Path arc; arc.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, jmin (from, ang), jmax (from, ang), true);
            moltenLine (g, arc, thick - 1.0f, 0.55f + 0.45f * amount);
        }
    }

    // the forged steel body
    {
        const auto body = Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
        g.setColour (Colours::black.withAlpha (0.65f)); g.fillEllipse (body.translated (0.0f, 4.0f).expanded (2.5f));
        g.setGradientFill (ColourGradient (Colour (0xffb9b0aa), c.x - r * 0.45f, c.y - r * 0.85f, Colour (0xff141211), c.x + r * 0.55f, c.y + r, true));
        g.fillEllipse (body);
        // brushed: fine rings
        for (int i = 0; i < 16; ++i)
        {
            const float rr = r * (0.3f + 0.68f * (float) i / 15.0f);
            g.setColour ((i % 2 ? Colours::white : Colours::black).withAlpha (0.045f));
            g.drawEllipse (Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 0.8f);
        }
        // knurled rim
        for (int i = 0; i < 60; ++i)
        {
            const float a = MathConstants<float>::twoPi * (float) i / 60.0f + ang;
            const auto p0 = c.getPointOnCircumference (r - 1.0f, a), p1 = c.getPointOnCircumference (r - (r > 30.0f ? 5.0f : 3.5f), a);
            g.setColour ((i % 2 ? Colours::white : Colours::black).withAlpha (0.16f));
            g.drawLine ({ p0, p1 }, 1.0f);
        }
        // bevel: light along the top, dark along the bottom
        { Path top; top.addCentredArc (c.x, c.y, r - 0.6f, r - 0.6f, 0.0f, -2.2f, 2.2f, true);
          g.setColour (Colours::white.withAlpha (0.28f)); g.strokePath (top, PathStrokeType (1.2f)); }
        g.setColour (Colours::black.withAlpha (0.5f)); g.drawEllipse (body.reduced (0.3f), 1.0f);
        // the face: a darker, finer steel plate
        const auto face = body.reduced (r * 0.24f);
        g.setGradientFill (ColourGradient (Colour (0xff6e6661), face.getX(), face.getY(), Colour (0xff1e1b19), face.getRight(), face.getBottom(), false));
        g.fillEllipse (face);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.10f), face.getX(), face.getCentreY(), Colours::transparentWhite, face.getCentreX(), face.getY(), false));
        g.fillEllipse (face);
        g.setColour (Colours::black.withAlpha (0.55f)); g.drawEllipse (face, 1.0f);
        { Path lip; lip.addCentredArc (c.x, c.y, face.getWidth() * 0.5f + 0.8f, face.getWidth() * 0.5f + 0.8f, 0.0f, 1.0f, 5.3f, true);
          g.setColour (Colours::white.withAlpha (0.12f)); g.strokePath (lip, PathStrokeType (1.0f)); }
    }

    // the pointer: a molten seam cut into the steel
    {
        const auto p0 = c.getPointOnCircumference (r * 0.28f, ang), p1 = c.getPointOnCircumference (r * 0.88f, ang);
        g.setColour (Colours::black.withAlpha (0.75f)); g.drawLine ({ p0, p1 }, r > 30.0f ? 5.0f : 4.0f);
        Path seam; seam.startNewSubPath (p0); seam.lineTo (p1);
        moltenLine (g, seam, r > 30.0f ? 2.4f : 1.8f, 0.6f + 0.4f * amount);
    }
}

void PyreLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    using namespace juce;
    const auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const auto c = r.getCentre(); const float rad = jmin (r.getWidth(), r.getHeight()) * 0.5f;
    const auto disc = Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.6f)); g.fillEllipse (disc.translated (0.0f, 2.0f));
    g.setGradientFill (ColourGradient (Colour (down ? 0xff3a3532 : 0xff6a625d), c.x, disc.getY(), Colour (0xff1a1716), c.x, disc.getBottom(), false));
    g.fillEllipse (disc);
    g.setColour (Colours::white.withAlpha (over ? 0.30f : 0.18f)); g.drawEllipse (disc.reduced (0.5f), 1.0f);
    if (b.getToggleState())
    {
        Path ring; ring.addEllipse (disc.expanded (1.5f));
        moltenLine (g, ring, 1.6f, 0.9f);
    }
}

void PyreLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (title (17.0f));
    g.setColour (b.getToggleState() ? kMolten : kSteel);
    g.drawText (b.getButtonText(), b.getLocalBounds().translated (0, 1), juce::Justification::centred, false);
}

juce::Rectangle<int> PyreLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::AttributedString s; s.append (text, body (13.0f), kText); s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl; tl.createLayout (s, 300.0f);
    const int w = (int) std::ceil (tl.getWidth()) + 28, h = (int) std::ceil (tl.getHeight()) + 20;
    return juce::Rectangle<int> (pos.x + 12, pos.y + 18, w, h).constrainedWithin (parent);
}

void PyreLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.setColour (juce::Colour (0xf0120d0b)); g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 5.0f);
    g.setColour (kOrange.withAlpha (0.5f)); g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 5.0f, 1.0f);
    juce::AttributedString s; s.append (text, body (13.0f), kText); s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl; tl.createLayout (s, (float) w - 24.0f);
    tl.draw (g, { 12.0f, 8.0f, (float) w - 24.0f, (float) h - 16.0f });
}

//==============================================================================
PyreContent::PyreContent (PyreProcessor& p, PyreLookAndFeel& l) : proc (p), lnf (l)
{
    setSize (kW, kH);
    struct Def { Knob* k; const char* id; const char* label; int x, y, r; bool bip; const char* tip; };
    const Def defs[] = {
        { &ignite, "ignite", "IGNITE", 116, 500, 46, false, "IGNITE: a drive in front, for sounds with few harmonics to begin with (a sine, a soft square, a filtered pad). It gives HEAT something to work with." },
        { &heat, "heat", "HEAT", 290, 494, 62, false, "HEAT: evens out the harmonics, toward a saw's even fall. 0% leaves them as they are; 100% goes all the way (up to MAX). Noise between the harmonics is left alone." },
        { &tilt, "tilt", "TILT", 464, 500, 46, true, "TILT: the slope HEAT aims for. Middle: a saw (-6 dB per octave). Left: darker, warmer. Right: brighter, more bite." },
        { &burn, "burn", "BURN", 640, 494, 62, false, "BURN: drives the result into a soft ceiling at -0.3 dBFS. This is where the level comes from: the denser the sound, the harder it can burn before it sounds broken." },
        { &maxRaise, "max", "MAX", 784, 450, 22, false, "MAX: the most any one harmonic may be raised" },
        { &mix, "mix", "MIX", 784, 512, 22, false, "MIX: blend with the untouched sound (kept in time with it)" },
        { &output, "output", "OUTPUT", 784, 574, 22, false, "OUTPUT: the level after everything" },
    };
    for (const auto& d : defs)
    {
        auto& k = *d.k;
        k.id = d.id; k.label = d.label; k.c = { d.x, d.y }; k.r = d.r;
        k.s.setLookAndFeel (&lnf);
        k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        k.s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        k.s.setMouseDragSensitivity (d.r > 30 ? 300 : 220);
        if (d.bip) k.s.getProperties().set ("bipolar", true);
        k.s.setTooltip (d.tip);
        k.a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, d.id, k.s);
        if (auto* prm = proc.apvts.getParameter (d.id)) k.s.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        addAndMakeVisible (k.s);
    }
    // HELP: pop-up explanations on hover, off until asked for (remembered for every project)
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "PYRE"; o.folderName = "PYRE"; o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (o);
    }
    helpButton.setLookAndFeel (&lnf);
    helpButton.setClickingTogglesState (true);
    helpButton.onClick = [this] { setHelp (helpButton.getToggleState()); };
    addAndMakeVisible (helpButton);
    setHelp (settings->getBoolValue ("showHelp", false));

    shownAfter.fill (-120.0f); shownBefore.fill (-120.0f);
    resized();
    startTimerHz (30);
}

PyreContent::~PyreContent()
{
    for (auto* k : knobs) k->s.setLookAndFeel (nullptr);
    helpButton.setLookAndFeel (nullptr);
}

void PyreContent::setHelp (bool on)
{
    helpButton.setToggleState (on, juce::dontSendNotification);
    tooltips = on ? std::make_unique<juce::TooltipWindow> (this, 500) : nullptr;
    if (tooltips) tooltips->setLookAndFeel (&lnf);
    helpButton.setTooltip ("Help is on: hover over any control to see what it does. Click to turn the pop-ups off.");
    settings->setValue ("showHelp", on);
    settings->saveIfNeeded();
}

void PyreContent::resized()
{
    helpButton.setBounds (858, 28, 34, 34);
    for (auto* k : knobs)
    {
        const int side = (k->r + (k->r > 30 ? 18 : 10)) * 2;
        k->s.setBounds (k->c.x - side / 2, k->c.y - side / 2, side, side);
    }
}

float PyreContent::xOfHz (float hz) const
{
    const auto p = pit();
    return p.getX() + 8.0f + std::log (juce::jmax (30.0f, hz) / 30.0f) / std::log (16000.0f / 30.0f) * (p.getWidth() - 16.0f);
}

float PyreContent::yOfDb (float db) const
{
    const auto p = pit();
    const float top = p.getY() + 14.0f, bot = p.getBottom();
    return juce::jlimit (top - 10.0f, bot, top + (-db / 78.0f) * (bot - top));
}

void PyreContent::timerCallback()
{
    auto& e = proc.engine();
    const float dt = 1.0f / 30.0f;
    t += dt;
    for (int b = 0; b < pyre::kBands; ++b)
    {
        const float a = e.bandAfter[(size_t) b].load(), be = e.bandBefore[(size_t) b].load();
        shownAfter[(size_t) b] = a > shownAfter[(size_t) b] ? a : juce::jmax (a, shownAfter[(size_t) b] - 2.0f);
        shownBefore[(size_t) b] = be > shownBefore[(size_t) b] ? be : juce::jmax (be, shownBefore[(size_t) b] - 2.0f);
    }
    anchorHz = e.anchorHz.load(); anchorDb = e.anchorDb.load();
    louder += 0.15f * (e.louderDb.load() - louder);
    const float over = e.burnDbNow.exchange (0.0f);
    burnGlow = juce::jmax (over, burnGlow - 0.35f);
    auto toDb = [] (float v) { return juce::Decibels::gainToDecibels (v, -100.0f); };
    const float i = toDb (proc.inPeak.exchange (0.0f)), o = toDb (proc.outPeak.exchange (0.0f));
    inDb = i >= inDb ? i : juce::jmax (i, inDb - 1.5f);
    outDb = o >= outDb ? o : juce::jmax (o, outDb - 1.5f);

    // embers: more of them the louder and hotter the fire
    float energy = 0.0f;
    for (int b = 0; b < pyre::kBands; ++b) energy += juce::jlimit (0.0f, 1.0f, (shownAfter[(size_t) b] + 72.0f) / 72.0f);
    energy /= (float) pyre::kBands;
    const float heatAmt = (float) proc.apvts.getRawParameterValue ("heat")->load() * 0.01f;
    const float rate = energy * (3.0f + 12.0f * heatAmt) + burnGlow * 1.5f;
    const auto p = pit();
    for (float k = rnd.nextFloat(); k < rate * dt * 10.0f; k += 1.0f)
    {
        const int b = rnd.nextInt (pyre::kBands);
        const float hz = 30.0f * std::pow (16000.0f / 30.0f, ((float) b + 0.5f) / pyre::kBands);
        const float top = yOfDb (shownAfter[(size_t) b]);
        if (top >= p.getBottom() - 4.0f) continue;
        embers.push_back ({ xOfHz (hz) + rnd.nextFloat() * 6.0f - 3.0f, top + rnd.nextFloat() * 12.0f,
                            rnd.nextFloat() * 16.0f - 8.0f, -(30.0f + rnd.nextFloat() * 70.0f), 1.0f, 0.8f + rnd.nextFloat() * 1.6f });
    }
    for (auto& em : embers)
    {
        em.vx += (rnd.nextFloat() - 0.5f) * 40.0f * dt;
        em.x += em.vx * dt; em.y += em.vy * dt; em.life -= dt * (0.45f + 0.4f * em.size * 0.3f);
    }
    embers.erase (std::remove_if (embers.begin(), embers.end(), [&] (const Ember& em) { return em.life <= 0.0f || em.y < p.getY() + 2.0f; }), embers.end());
    if (embers.size() > 400) embers.erase (embers.begin(), embers.begin() + (long) (embers.size() - 400));
    repaint();
}

void PyreContent::renderBackground (float scale)
{
    using namespace juce;
    bgScale = scale;
    bg = Image (Image::RGB, roundToInt (kW * scale), roundToInt (kH * scale), false);
    Graphics g (bg);
    g.addTransform (AffineTransform::scale (scale));

    // dark forged steel, brushed sideways
    g.setGradientFill (ColourGradient (Colour (0xff232120), 0.0f, 0.0f, Colour (0xff0c0b0a), 0.0f, (float) kH, false));
    g.fillAll();
    {
        Random r (11);
        for (int i = 0; i < 2600; ++i)
        {
            const float y = r.nextFloat() * kH, x = r.nextFloat() * kW - 100.0f;
            g.setColour ((r.nextBool() ? Colours::white : Colours::black).withAlpha (0.018f + r.nextFloat() * 0.03f));
            g.fillRect (x, y, 80.0f + r.nextFloat() * 500.0f, 0.6f);
        }
        for (int i = 0; i < 12000; ++i)
        {
            g.setColour ((r.nextBool() ? Colours::white : Colours::black).withAlpha (0.02f + r.nextFloat() * 0.02f));
            g.fillRect (r.nextFloat() * kW, r.nextFloat() * kH, 1.0f, 1.0f);
        }
    }
    // a soft sheen across the top, the glow of the fire on the lower panel
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.05f), (float) kW * 0.3f, 0.0f, Colours::transparentWhite, (float) kW * 0.3f, 120.0f, false));
    g.fillRect (0, 0, kW, 120);
    g.setGradientFill (ColourGradient (Colour (0x3cff5a14), (float) kW * 0.5f, 400.0f, Colour (0x00ff5a14), (float) kW * 0.5f, 400.0f + 420.0f, true));
    g.fillRect (0, 0, kW, kH);
    // the seam under the header, like the one through the logo: molten in the middle, cooling toward the ends
    {
        const float y = 80.0f;
        g.setColour (Colours::black.withAlpha (0.7f)); g.fillRect (30.0f, y - 1.5f, (float) kW - 60.0f, 3.0f);
        for (auto [h, a] : { std::pair<float, float> { 12.0f, 0.06f }, { 5.0f, 0.18f }, { 1.6f, 0.9f } })
        {
            ColourGradient sym (kOrange.withAlpha (0.0f), 30.0f, y, kOrange.withAlpha (0.0f), (float) kW - 30.0f, y, false);
            sym.addColour (0.5, h < 2.0f ? Colour (0xffffd49a) : kOrange.withAlpha (a));
            g.setGradientFill (sym); g.fillRect (30.0f, y - h * 0.5f, (float) kW - 60.0f, h);
        }
    }

    // the pit: a sunken firebox with a riveted iron frame
    const auto p = pit();
    {
        Path frame; frame.addRoundedRectangle (p.expanded (10.0f), 10.0f);
        g.setColour (Colours::black.withAlpha (0.6f)); g.fillPath (frame, AffineTransform::translation (0.0f, 5.0f));
        g.setGradientFill (ColourGradient (Colour (0xff5a5450), 0.0f, p.getY() - 10.0f, Colour (0xff1b1918), 0.0f, p.getBottom() + 10.0f, false));
        g.fillPath (frame);
        g.setColour (Colours::white.withAlpha (0.16f)); g.drawLine (p.getX() + 2.0f, p.getY() - 9.5f, p.getRight() - 2.0f, p.getY() - 9.5f, 1.0f);
        g.setColour (Colours::black.withAlpha (0.6f)); g.strokePath (frame, PathStrokeType (1.0f));
        for (auto c : { Point<float> (p.getX() - 3.0f, p.getY() - 3.0f), Point<float> (p.getRight() + 3.0f, p.getY() - 3.0f),
                        Point<float> (p.getX() - 3.0f, p.getBottom() + 3.0f), Point<float> (p.getRight() + 3.0f, p.getBottom() + 3.0f) })
        {
            g.setGradientFill (ColourGradient (Colour (0xffc9c1bb), c.x - 2.0f, c.y - 2.0f, Colour (0xff1c1a19), c.x + 3.0f, c.y + 3.0f, false));
            g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre (c));
        }
        g.setColour (Colour (0xff050303)); g.fillRoundedRectangle (p, 6.0f);
    }

    // the knob plates
    auto well = [&] (Point<int> c, float rad)
    {
        g.setGradientFill (ColourGradient (Colour (0xff070505), (float) c.x, (float) c.y, Colour (0x00070505), (float) c.x, (float) c.y + rad * 1.5f, true));
        g.fillEllipse (Rectangle<float> (rad * 3.0f, rad * 3.0f).withCentre (c.toFloat()));
    };
    for (auto* k : knobs) if (k->r > 30) well (k->c, (float) k->r + 18.0f);
    // the side column
    {
        const Rectangle<float> col (742.0f, 420.0f, 150.0f, 184.0f);
        g.setColour (Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (col.translated (0.0f, 3.0f), 8.0f);
        g.setGradientFill (ColourGradient (Colour (0xff2e2b29), 0.0f, col.getY(), Colour (0xff141211), 0.0f, col.getBottom(), false));
        g.fillRoundedRectangle (col, 8.0f);
        g.setColour (Colours::white.withAlpha (0.12f)); g.drawLine (col.getX() + 8.0f, col.getY() + 0.5f, col.getRight() - 8.0f, col.getY() + 0.5f, 1.0f);
        g.setColour (Colours::black.withAlpha (0.7f)); g.drawRoundedRectangle (col, 8.0f, 1.0f);
    }

    // frequency labels under the pit
    g.setFont (lnf.body (10.0f, 0.5f)); g.setColour (kDim);
    for (auto [hz, txt] : { std::pair<float, const char*> { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
                            { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } })
        g.drawText (txt, Rectangle<float> (xOfHz (hz) - 20.0f, p.getBottom() + 12.0f, 40.0f, 12.0f), Justification::centred, false);

    // the logo: PYRE in forged steel with a molten seam through it (as in Artwork/PYRE-logo.png)
    {
        GlyphArrangement ga; ga.addLineOfText (lnf.title (60.0f, 60.0f * 0.07f), "PYRE", 34.0f, 68.0f);
        Path tp; ga.createPath (tp);
        const auto tb = tp.getBounds();
        const float sh = tb.getHeight() * 0.055f, sy = tb.getY() + tb.getHeight() * 0.45f - sh * 0.5f;
        // the steel letters
        ColourGradient steel (Colour (0xfff2ece8), 0.0f, tb.getY(), Colour (0xff6e625b), 0.0f, tb.getBottom(), false);
        steel.addColour (0.45, Colour (0xffbdb2ab));
        g.setGradientFill (steel); g.fillPath (tp);
        // the seam's glow: the seam itself, blurred, laid over the steel (a soft halo that follows the letters)
        {
            const auto area = tb.expanded (40.0f).getSmallestIntegerContainer();
            Image glow (Image::ARGB, roundToInt ((float) area.getWidth() * scale), roundToInt ((float) area.getHeight() * scale), true);
            {
                Graphics gg (glow);
                gg.addTransform (AffineTransform::translation ((float) -area.getX(), (float) -area.getY()).scaled (scale));
                gg.reduceClipRegion (tp);
                gg.setColour (kOrange); gg.fillRect (tb.getX(), sy, tb.getWidth(), sh);
            }
            auto wide = glow.createCopy();
            blurImage (glow, roundToInt (2.0f * scale));
            blurImage (wide, roundToInt (7.0f * scale));
            g.setOpacity (0.85f); g.drawImage (wide, area.toFloat());
            g.setOpacity (0.9f); g.drawImage (glow, area.toFloat());
            g.setOpacity (1.0f);
        }
        // the seam, sharp
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (tp);
            ColourGradient seam (Colour (0xffffe2a0), 0.0f, sy, Colour (0xff7a1404), 0.0f, sy + sh, false);
            seam.addColour (0.4, kOrange);
            g.setGradientFill (seam); g.fillRect (tb.getX(), sy, tb.getWidth(), sh);
        }
    }
    g.setFont (lnf.body (11.0f, 3.2f)); g.setColour (kSteel);
    g.drawText ("HARMONIC LOUDNESS", Rectangle<float> (196.0f, 39.0f, 280.0f, 14.0f), Justification::centredLeft, false);
}

void PyreContent::drawFire (juce::Graphics& g)
{
    using namespace juce;
    const auto p = pit();
    Graphics::ScopedSaveState ss (g);
    Path clip; clip.addRoundedRectangle (p, 6.0f); g.reduceClipRegion (clip);

    // embers' glow on the floor of the pit
    float energy = 0.0f;
    for (float v : shownAfter) energy = jmax (energy, jlimit (0.0f, 1.0f, (v + 60.0f) / 60.0f));
    g.setGradientFill (ColourGradient (Colour (0xff3a0a02).withAlpha (0.25f + 0.55f * energy), 0.0f, p.getBottom(), Colour (0x00000000), 0.0f, p.getY() + p.getHeight() * 0.25f, false));
    g.fillRect (p);

    // grid: octaves and dB, faint
    g.setColour (Colour (0xff1e1411));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f }) g.fillRect (xOfHz (hz), p.getY(), 1.0f, p.getHeight());
    for (float db : { -12.0f, -24.0f, -36.0f, -48.0f, -60.0f }) g.fillRect (p.getX(), yOfDb (db), p.getWidth(), 1.0f);
    g.setFont (lnf.body (9.0f)); g.setColour (Colour (0xff4a3a33));
    for (float db : { -12.0f, -24.0f, -36.0f, -48.0f }) g.drawText (minus() + String ((int) -db), Rectangle<float> (p.getRight() - 34.0f, yOfDb (db) - 12.0f, 30.0f, 11.0f), Justification::centredRight, false);

    const float heatAmt = (float) proc.apvts.getRawParameterValue ("heat")->load() * 0.01f;
    // the flames: a point per band, each top flickering a little (more with HEAT)
    auto flamePath = [&] (const std::array<float, pyre::kBands>& v, float dbShift, float flick)
    {
        Path f; f.startNewSubPath (p.getX(), p.getBottom() + 2.0f);
        Point<float> prev (p.getX(), p.getBottom() + 2.0f);
        for (int b = 0; b < pyre::kBands; ++b)
        {
            const float hz = 30.0f * std::pow (16000.0f / 30.0f, ((float) b + 0.5f) / pyre::kBands);
            const float lvl = v[(size_t) b] + dbShift;
            float y = yOfDb (lvl);
            const float on = jlimit (0.0f, 1.0f, (lvl + 70.0f) / 30.0f);
            y -= on * flick * (0.6f + 0.4f * std::sin (t * 9.0f + (float) b * 1.9f)) * (0.5f + 0.5f * std::sin (t * 5.3f + (float) b * 0.7f));
            const Point<float> pt (xOfHz (hz), jmin (y, p.getBottom() + 2.0f));
            f.quadraticTo (prev.x, prev.y, (prev.x + pt.x) * 0.5f, (prev.y + pt.y) * 0.5f);
            prev = pt;
        }
        f.lineTo (p.getRight(), prev.y); f.lineTo (p.getRight(), p.getBottom() + 2.0f); f.closeSubPath();
        return f;
    };
    const auto outer = flamePath (shownAfter, 0.0f, 3.0f + 9.0f * heatAmt);
    ColourGradient fire (Colour (0xfffff4d6), 0.0f, p.getY() + 10.0f, Colour (0xff4a0b02), 0.0f, p.getBottom(), false);
    fire.addColour (0.25, kYellow); fire.addColour (0.45, kAmber); fire.addColour (0.62, kOrange); fire.addColour (0.82, kRed);
    g.setColour (kOrange.withAlpha (0.18f)); g.strokePath (outer, PathStrokeType (10.0f, PathStrokeType::curved));
    g.setGradientFill (fire); g.fillPath (outer);
    // the hot core
    {
        const auto core = flamePath (shownAfter, -14.0f, 2.0f + 5.0f * heatAmt);
        ColourGradient hot (Colour (0xccfffbe9), 0.0f, p.getY() + 30.0f, Colour (0x00ffd98a), 0.0f, p.getBottom(), false);
        g.setGradientFill (hot); g.fillPath (core);
    }
    g.setColour (Colour (0xffffe3a6).withAlpha (0.75f)); g.strokePath (outer, PathStrokeType (1.2f, PathStrokeType::curved));

    // the sound before PYRE: ash
    {
        Path a; bool started = false;
        for (int b = 0; b < pyre::kBands; ++b)
        {
            const float hz = 30.0f * std::pow (16000.0f / 30.0f, ((float) b + 0.5f) / pyre::kBands);
            const Point<float> pt (xOfHz (hz), yOfDb (shownBefore[(size_t) b]));
            if (! started) { a.startNewSubPath (pt); started = true; } else a.lineTo (pt);
        }
        g.setColour (Colours::black.withAlpha (0.5f)); g.strokePath (a, PathStrokeType (3.0f, PathStrokeType::curved));
        g.setColour (kAsh.withAlpha (0.65f)); g.strokePath (a, PathStrokeType (1.3f, PathStrokeType::curved));
    }

    // what HEAT aims for: TILT's line from the strongest harmonic
    if (anchorDb > -100.0f && anchorHz > 20.0f && heatAmt > 0.001f)
    {
        const float slope = -6.0f + 4.0f * (float) proc.apvts.getRawParameterValue ("tilt")->load() / 100.0f;
        Path tl;
        for (int i = 0; i <= 80; ++i)
        {
            const float hz = 40.0f * std::pow (16000.0f / 40.0f, (float) i / 80.0f);
            const float db = jmin (anchorDb, anchorDb + slope * std::log2 (hz / anchorHz));
            const Point<float> pt (xOfHz (hz), yOfDb (db));
            if (i == 0) tl.startNewSubPath (pt); else tl.lineTo (pt);
        }
        Path dashed; const float dl[] = { 6.0f, 5.0f };
        PathStrokeType (1.4f).createDashedStroke (dashed, tl, dl, 2);
        g.setColour (Colour (0xfffff1c8).withAlpha (0.55f)); g.fillPath (dashed);
    }

    // embers
    for (const auto& em : embers)
    {
        const float a = jlimit (0.0f, 1.0f, em.life);
        const Colour c = heatColour (0.45f + 0.55f * a);
        g.setColour (c.withAlpha (0.18f * a)); g.fillEllipse (Rectangle<float> (em.size * 5.0f, em.size * 5.0f).withCentre ({ em.x, em.y }));
        g.setColour (c.withAlpha (0.9f * a)); g.fillEllipse (Rectangle<float> (em.size * 1.6f, em.size * 1.6f).withCentre ({ em.x, em.y }));
    }

    // a word when there is nothing burning
    if (energy < 0.02f)
    {
        g.setFont (lnf.body (11.0f, 3.0f)); g.setColour (kDim);
        g.drawText ("PLAY A LEAD OR SYNTH THROUGH PYRE", p, Justification::centred, false);
    }
    // legend
    g.setFont (lnf.body (9.5f, 1.8f));
    float lx = p.getX() + 14.0f;
    auto item = [&] (const String& s, Colour c, bool dashed)
    {
        if (dashed) { for (float x = 0; x < 14.0f; x += 5.0f) { g.setColour (c); g.fillRect (lx + x, p.getY() + 15.0f, 3.0f, 1.5f); } }
        else { g.setColour (c); g.fillRoundedRectangle (lx, p.getY() + 14.0f, 14.0f, 3.0f, 1.0f); }
        g.setColour (kAsh.withAlpha (0.8f));
        g.drawText (s, Rectangle<float> (lx + 20.0f, p.getY() + 9.0f, 160.0f, 12.0f), Justification::centredLeft, false);
        lx += 20.0f + (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), s) + 18.0f;
    };
    item ("PYRE", kOrange, false);
    item ("BEFORE", kAsh, false);
    item ("HEAT AIMS FOR", Colour (0xfffff1c8), true);
}

void PyreContent::paint (juce::Graphics& g)
{
    using namespace juce;
    const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! bg.isValid() || std::abs (ps - bgScale) > 0.01f) renderBackground (ps);
    g.drawImage (bg, Rectangle<float> (0, 0, (float) kW, (float) kH), RectanglePlacement::stretchToFit);

    drawFire (g);

    // ---- readouts in the header: LEVEL (how much hotter than the input), BURN (how far into the ceiling), meters
    {
        g.setFont (lnf.body (9.5f, 2.4f)); g.setColour (kDim);
        g.drawText ("LEVEL", Rectangle<float> (500.0f, 26.0f, 120.0f, 12.0f), Justification::centredLeft, false);
        g.drawText (burnGlow > 0.05f ? "BURNING" : "CEILING", Rectangle<float> (622.0f, 26.0f, 140.0f, 12.0f), Justification::centredLeft, false);
        const bool live = outDb > -90.0f;
        g.setFont (lnf.title (26.0f, 0.5f));
        g.setColour (live ? kSteel : kDim);
        g.drawText (live ? dbText (louder, true) : String::fromUTF8 ("\xe2\x80\x94"), Rectangle<float> (500.0f, 38.0f, 120.0f, 34.0f), Justification::centredLeft, false);
        g.setColour (burnGlow > 0.05f ? kMolten : kAsh);
        g.drawText (burnGlow > 0.05f ? dbText (burnGlow, false) : minus() + "0.3 dB", Rectangle<float> (622.0f, 38.0f, 130.0f, 34.0f), Justification::centredLeft, false);
        // meters
        auto meter = [&] (float y, const String& name, float db)
        {
            g.setFont (lnf.body (9.0f, 1.6f)); g.setColour (kDim);
            g.drawText (name, Rectangle<float> (736.0f, y - 2.0f, 36.0f, 12.0f), Justification::centredLeft, false);
            const Rectangle<float> bar (770.0f, y, 74.0f, 7.0f);
            g.setColour (Colour (0xff120d0b)); g.fillRoundedRectangle (bar, 3.0f);
            const float v = jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
            if (v > 0.0f)
            {
                g.setGradientFill (ColourGradient (Colour (0xff6e625b), bar.getX(), 0.0f, kOrange, bar.getRight(), 0.0f, false));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * v), 3.0f);
            }
        };
        meter (36.0f, "IN", inDb);
        meter (54.0f, "OUT", outDb);
    }

    // ---- knob names and values
    for (auto* k : knobs)
    {
        const String v = k->s.getTextFromValue (k->s.getValue());
        if (k->r > 30)
        {
            const float y = (float) k->c.y + (float) k->r + 22.0f;
            g.setFont (lnf.title (15.0f, 3.0f)); g.setColour (kSteel);
            g.drawText (k->label, Rectangle<float> ((float) k->c.x - 80.0f, y, 160.0f, 18.0f), Justification::centred, false);
            g.setFont (lnf.body (11.5f, 0.5f)); g.setColour (kMolten);
            g.drawText (v, Rectangle<float> ((float) k->c.x - 80.0f, y + 18.0f, 160.0f, 14.0f), Justification::centred, false);
        }
        else
        {
            g.setFont (lnf.title (12.5f, 2.2f)); g.setColour (kSteel);
            g.drawText (k->label, Rectangle<float> ((float) k->c.x + 40.0f, (float) k->c.y - 15.0f, 70.0f, 14.0f), Justification::centredLeft, false);
            g.setFont (lnf.body (10.5f, 0.3f)); g.setColour (kMolten);
            g.drawText (v, Rectangle<float> ((float) k->c.x + 40.0f, (float) k->c.y + 1.0f, 70.0f, 14.0f), Justification::centredLeft, false);
        }
    }
}

//==============================================================================
PyreEditor::PyreEditor (PyreProcessor& p) : AudioProcessorEditor (p), content (p, lnf)
{
    // the size it had last time (saved with the project). Read it before setting the limits: they resize the window,
    // and resized() would overwrite it with the minimum. ("uiSize": 1.0.0 saved that minimum as "uiWidth", so it's ignored.)
    const int w = juce::jlimit (PyreContent::kW * 6 / 10, PyreContent::kW * 2, (int) p.apvts.state.getProperty ("uiSize", PyreContent::kW));
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) PyreContent::kW / PyreContent::kH);
    setResizeLimits (PyreContent::kW * 6 / 10, PyreContent::kH * 6 / 10, PyreContent::kW * 2, PyreContent::kH * 2);
    setSize (w, juce::roundToInt ((double) w * PyreContent::kH / PyreContent::kW));
    ready = true;
}

PyreEditor::~PyreEditor() { setLookAndFeel (nullptr); }

void PyreEditor::resized()
{
    if (ready) if (auto* pp = dynamic_cast<PyreProcessor*> (&processor)) pp->apvts.state.setProperty ("uiSize", getWidth(), nullptr);
    const float s = (float) getWidth() / (float) PyreContent::kW;
    content.setTransform (juce::AffineTransform::scale (s));
    content.setBounds (0, 0, PyreContent::kW, PyreContent::kH);
}
