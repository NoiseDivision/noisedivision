#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    constexpr float kStart = juce::MathConstants<float>::pi * -0.75f, kEnd = juce::MathConstants<float>::pi * 0.75f;
    const juce::String& minusSign() { static const juce::String m = juce::String::fromUTF8 ("\xe2\x88\x92"); return m; }
    juce::String fixMinus (const juce::String& s) { return s.replace ("-", minusSign()); }
    juce::String hzText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + "k" : juce::String (juce::roundToInt (v)); }
    juce::String dbText (double g)
    {
        if (g <= 0.0005) return minusSign() + juce::String::fromUTF8 ("\xe2\x88\x9e");
        const double d = 20.0 * std::log10 (g);
        return (d < -0.05 ? minusSign() : juce::String()) + (std::abs (d) < 10 ? juce::String (std::abs (d), 1) : juce::String (juce::roundToInt (std::abs (d))));
    }
    double warp (double s, double curve) { return (curve != 0.0 && s > 0.0 && s < 1.0) ? std::pow (s, std::exp2 (-2.0 * curve)) : s; }
    double rateBeatsOf (const juce::AudioProcessorValueTreeState& a)
    {
        static const double beats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
        const int m = juce::roundToInt (a.getRawParameterValue ("rateMode")->load());
        return beats[juce::jlimit (0, 5, juce::roundToInt (a.getRawParameterValue ("rate")->load()))] * (m == 1 ? 1.5 : m == 2 ? 2.0 / 3.0 : 1.0);
    }
    juce::String rateText (const juce::AudioProcessorValueTreeState& a)
    {
        static const char* names[] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
        const int m = juce::roundToInt (a.getRawParameterValue ("rateMode")->load());
        return juce::String (names[juce::jlimit (0, 5, juce::roundToInt (a.getRawParameterValue ("rate")->load()))]) + (m == 1 ? "D" : m == 2 ? "T" : "");
    }

    // thumbnail of a shape: one cycle, with the jump back to the start drawn at the end
    juce::Path shapePath (const std::vector<rc::Node>& n, juce::Rectangle<float> r)
    {
        juce::Path p;
        auto Y = [&] (double v) { return r.getY() + (float) (1.0 - juce::jmin (1.12, v) / 1.12) * r.getHeight(); };
        for (int k = 0; k <= 96; ++k)
        {
            const double x = k / 96.0;
            const juce::Point<float> pt (r.getX() + (float) x * r.getWidth(), Y (rc::shapeAt (n, x)));
            if (k == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        if (! n.empty() && std::abs (n.front().y - n.back().y) > 0.01f) p.lineTo (r.getRight(), Y (n.front().y));
        return p;
    }
}

//==============================================================================
// WAVE view
WaveView::WaveView (RecoilProcessor& p, RecoilLookAndFeel& l) : processor (p), lnf (l)
{
    nodes = processor.currentNodes();
    styleButton.getProperties().set ("seg", true); styleButton.getProperties().set ("hot", true); styleButton.getProperties().set ("size", 8.5f);
    styleButton.setClickingTogglesState (false);
    styleButton.setTooltip ("HIT: one lane per hit. The kick filled in red, the bass after RECOIL over it (with the last few hits fading behind), and the duck as a solid fill from the top. Click again for the two lanes (PHASE on top).");
    styleButton.onClick = [this]
    {
        const bool on = ! (bool) processor.apvts.state.getProperty ("waveStyle", false);
        processor.apvts.state.setProperty ("waveStyle", on, nullptr);
        styleButton.setToggleState (on, juce::dontSendNotification);
        repaint();
    };
    styleButton.setToggleState ((bool) processor.apvts.state.getProperty ("waveStyle", false), juce::dontSendNotification);
    addAndMakeVisible (styleButton);
}

void WaveView::resized()
{
    styleButton.setBounds ((int) px1() + 4, getHeight() - 30, 38, 18);
}

double WaveView::cycleSec() const
{
    if (frozenSpan > 0.0) return frozenSpan;
    return capSpanSec;
}

float WaveView::tx (double t) const
{
    const double t0 = -capPreSec, t1 = capWinSec;
    return px0() + (float) ((t - t0) / (t1 - t0)) * (px1() - px0());
}

double WaveView::xt (float x) const
{
    const double t0 = -capPreSec, t1 = capWinSec;
    return t0 + (x - px0()) / (px1() - px0()) * (t1 - t0);
}

double WaveView::shownGain (const std::vector<rc::Node>& n, double x) const
{
    if (autoShape) return rc::shapeAt (n, x);                       // AUTO: the nodes already are the gain that was applied
    const double s = warp (rc::shapeAt (n, x), param ("curve") * 0.01);
    if (editing) return s;
    const double a = param ("depth") * 0.01 * param ("mix") * 0.01;
    return 1.0 - a * (1.0 - s);
}

void WaveView::setParam (const char* id, float v)
{
    if (auto* p = processor.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}
void WaveView::beginGesture (const char* id) { if (auto* p = processor.apvts.getParameter (id)) p->beginChangeGesture(); }
void WaveView::endGesture (const char* id) { if (auto* p = processor.apvts.getParameter (id)) p->endChangeGesture(); }

void WaveView::update()
{
    fs = processor.hostRate.load();
    styleButton.setToggleState ((bool) processor.apvts.state.getProperty ("waveStyle", false), juce::dontSendNotification);
    styleButton.setVisible (! editing);
    // AUTO, and MAP outside EDIT: show the duck the shown hit actually got (from the captured gain). In MAP each kind
    // of hit has its own shape, depth and length, so the slot picked in SHAPES isn't necessarily what the shown hit used.
    const bool mapLive = juce::roundToInt (param ("trigger")) == 3 && ! editing && haveCapture;
    const bool rmMode = juce::roundToInt (param ("trigger")) == 4;                // RM: no shape, only the duck it really did
    autoShape = processor.bassAutoActive() || mapLive || rmMode;
    if (autoShape)
    {
        nodes.clear();
        const int at = (int) (capPreSec * fs), n = (int) gain.size();
        if (haveCapture && n > at + 2)
            for (int i = 0; i <= 64; ++i)
            {
                const int k = juce::jmin (n - 1, at + (int) ((double) i / 64.0 * capSpanSec * fs));
                nodes.push_back ({ (float) i / 64.0f, gain[(size_t) k], 0.0f, false });
            }
        if (nodes.size() < 2) nodes = { { 0, 1 }, { 1, 1 } };
    }
    else if (dragNode < 0 && dragSeg < 0) nodes = processor.currentNodes();

    // what one cycle should be right now
    const int trig = juce::roundToInt (param ("trigger"));
    if (showPhase != (trig >= 2)) { showPhase = trig >= 2; repaint(); }
    double span = processor.cycleSamples.load() / fs;
    if (span <= 0.0)
        span = trig == 0 ? rateBeatsOf (processor.apvts) * 60.0 / juce::jmax (1.0, processor.hostBpm.load()) : param ("length") * 0.001;
    span = juce::jlimit (0.03, 2.2, span);

    if (frozen || dragTab != 0) { repaint(); return; }

    if (trig == 3)
    {
        // MAP: the view follows one kind of hit (picked above it: KICK, SNARE, RIM or GHOST), so at any tempo it shows
        // every one of those, each from its start and with its own duck.
        const int kind = juce::jlimit (0, 3, (int) processor.apvts.state.getProperty ("waveHit", 0));
        if (kind != lastKind) { lastKind = kind; pending.clear(); lastHitCount = processor.hitCountKind[(size_t) kind].load(); haveCapture = false; laneIsDrums = kind != 0; }
        const double lenK = processor.hitLenKind[(size_t) kind].load() / fs;
        const double duck = juce::jlimit (0.03, 2.2, lenK > 0.0 ? lenK : span);
        const double win = duck * 1.12;
        const double pre = juce::jmax (0.004, win * 0.05);
        const int n = (int) ((pre + win) * fs);
        const int hc = processor.hitCountKind[(size_t) kind].load();
        if (hc != lastHitCount)
        {
            lastHitCount = hc;
            pending.push_back ({ processor.hitAtKind[(size_t) kind].load(), duck });
            while (pending.size() > 4) pending.pop_front();
        }
        const int64_t written = processor.scopeAfter.total.load();
        int64_t start = -1; double dur = span;
        while (! pending.empty() && written >= pending.front().first + (int64_t) (win * fs))
        {
            start = pending.front().first; dur = pending.front().second;      // the newest complete one wins
            pending.pop_front();
        }
        if (start >= 0)
        {
            const int64_t s0 = start - (int64_t) (pre * fs);
            pushEcho();
            kick.resize ((size_t) n); before.resize ((size_t) n); after.resize ((size_t) n); gain.resize ((size_t) n);
            // the top lane: the kick's low end for kicks; for snares, rims and ghost notes the sidechain itself (the drums),
            // so the hit being shown starts at 0 ms there too
            (kind == 0 ? processor.scopeKick : processor.scopeSc).readFrom (s0, kick.data(), n);
            laneIsDrums = kind != 0;
            processor.scopeBefore.readFrom (s0, before.data(), n);
            processor.scopeAfter.readFrom (s0, after.data(), n);
            processor.scopeGain.readFrom (s0, gain.data(), n);
            capSpanSec = juce::jlimit (0.02, 2.2, dur); capPreSec = pre; capWinSec = dur * 1.12; haveCapture = true;
            if (kind == 0) measureKeys();                               // the kick's note only from kicks
        }
        else if (pending.empty() && written - processor.hitAtKind[(size_t) kind].load() > (int64_t) (fs * 2.5))
        {
            kick.resize ((size_t) n); before.resize ((size_t) n); after.resize ((size_t) n); gain.resize ((size_t) n);
            (kind == 0 ? processor.scopeKick : processor.scopeSc).readFrom (written - n, kick.data(), n);
            laneIsDrums = kind != 0;
            processor.scopeBefore.readFrom (written - n, before.data(), n);
            processor.scopeAfter.readFrom (written - n, after.data(), n);
            processor.scopeGain.readFrom (written - n, gain.data(), n);
            capPreSec = pre; capWinSec = win; haveCapture = false;
        }
        repaint();
        return;
    }
    pending.clear(); laneIsDrums = false;

    const int hc = processor.hitCount.load();
    if (hc != lastHitCount) { lastHitCount = hc; if (pendingHit < 0) pendingHit = processor.hitAt.load(); }   // keep the first until its window is in
    const int64_t written = processor.scopeAfter.total.load();
    const double pre = juce::jmax (0.004, span * 0.06);
    const int n = (int) ((pre + span * 1.08) * fs);

    auto capture = [&] (int64_t start, bool aligned)
    {
        kick.resize ((size_t) n); before.resize ((size_t) n); after.resize ((size_t) n); gain.resize ((size_t) n);
        processor.scopeKick.readFrom (start, kick.data(), n);
        processor.scopeBefore.readFrom (start, before.data(), n);
        processor.scopeAfter.readFrom (start, after.data(), n);
        processor.scopeGain.readFrom (start, gain.data(), n);
        capSpanSec = span; capPreSec = pre; capWinSec = span * 1.08; haveCapture = aligned;
    };
    if (pendingHit >= 0 && written >= pendingHit + (int64_t) (span * 1.08 * fs))
    {
        pushEcho();
        capture (pendingHit - (int64_t) (pre * fs), true);
        pendingHit = -1;
        measureKeys();
    }
    else if (pendingHit < 0 && written - processor.hitAt.load() > (int64_t) (fs * 2.5))
        capture (written - n, false);                          // no hits for a while: show what is playing now
    else if (std::abs (span - capSpanSec) > 0.002 && ! haveCapture)
        { capSpanSec = span; capWinSec = span * 1.08; }
    repaint();
}

//==============================================================================
// Keys: a YIN pitch estimate on a decimated copy (cheap enough for the message thread, once per hit)
namespace
{
    float yinPitch (const float* x, int n, double fs, float fMin, float fMax)
    {
        const int dec = juce::jmax (1, (int) (fs / 8000.0));
        std::vector<float> y; y.reserve ((size_t) (n / dec + 1));
        for (int i = 0; i + dec <= n; i += dec) { float a = 0; for (int k = 0; k < dec; ++k) a += x[i + k]; y.push_back (a / (float) dec); }
        const double fsd = fs / dec;
        // even out the level (a kick fades fast): divide by its RMS over 12 ms blocks
        {
            const int blk = juce::jmax (4, (int) (0.012 * fsd));
            std::vector<float> g ((y.size() + (size_t) blk - 1) / (size_t) blk, 0.0f);
            for (size_t i = 0; i < y.size(); ++i) g[i / (size_t) blk] += y[i] * y[i];
            for (size_t b = 0; b < g.size(); ++b) { const size_t len = juce::jmin ((size_t) blk, y.size() - b * (size_t) blk); g[b] = std::sqrt (g[b] / (float) len); }
            const float top = *std::max_element (g.begin(), g.end());
            if (top < 1e-4f) return 0.0f;                                            // too quiet to tell
            for (size_t i = 0; i < y.size(); ++i)
            {
                const float pos = ((float) i + 0.5f) / (float) blk - 0.5f;
                const size_t a = (size_t) juce::jlimit (0, (int) g.size() - 1, (int) std::floor (pos)), b = juce::jmin (g.size() - 1, a + 1);
                const float f = juce::jlimit (0.0f, 1.0f, pos - (float) a);
                const float lv = juce::jmax (top * 0.05f, g[a] + (g[b] - g[a]) * f);
                y[i] /= lv;
            }
        }
        const int tauMin = juce::jmax (2, (int) (fsd / fMax)), tauMax = (int) (fsd / fMin);
        const int w = (int) y.size() - tauMax - 1;
        if (w < tauMax) return 0.0f;
        double energy = 0; for (int i = 0; i < w; ++i) energy += (double) y[(size_t) i] * y[(size_t) i];
        if (energy / w < 1e-8) return 0.0f;
        std::vector<double> d ((size_t) tauMax + 2, 0.0), cm ((size_t) tauMax + 2, 1.0);
        double run = 0;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            double s = 0; for (int i = 0; i < w; ++i) { const double e = y[(size_t) i] - y[(size_t) (i + tau)]; s += e * e; }
            d[(size_t) tau] = s; run += s; cm[(size_t) tau] = run > 0 ? s * tau / run : 1.0;
        }
        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (cm[(size_t) tau] < 0.25) { while (tau + 1 <= tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau; best = tau; break; }
        if (best < 0)
        {
            best = tauMin;
            for (int tau = tauMin; tau <= tauMax; ++tau) if (cm[(size_t) tau] < cm[(size_t) best]) best = tau;
            if (cm[(size_t) best] > 0.45) return 0.0f;                          // no clear pitch (noise, a chord)
        }
        const double a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1];
        const double den = a - 2 * b + c;
        const double t = best + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0);
        return (float) (fsd / t);
    }

    juce::String noteName (float hz, int* cents = nullptr)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const int m = juce::roundToInt (midi);
        if (cents) *cents = juce::roundToInt ((midi - m) * 100.0);
        return juce::String (names[((m % 12) + 12) % 12]) + juce::String (m / 12 - 1);
    }
}

void WaveView::measureKeys()
{
    const int n = (int) kick.size();
    const int at = (int) (capPreSec * fs);                                       // the hit
    if (processor.sidechainConnected.load())
    {
        // the kick: the note it settles on (its tail), past the click and the pitch drop, until it has faded 24 dB
        const int blk = (int) (0.01 * fs);
        float peak = 0.0f; int end = at;
        std::vector<float> rms;
        for (int i = at; i + blk <= n && i < at + (int) (0.4 * fs); i += blk)
        {
            double e = 0; for (int k = 0; k < blk; ++k) e += (double) kick[(size_t) (i + k)] * kick[(size_t) (i + k)];
            rms.push_back ((float) std::sqrt (e / blk)); peak = juce::jmax (peak, rms.back());
        }
        for (size_t r = 0; r < rms.size(); ++r) if (rms[r] > peak * 0.063f) end = at + (int) (r + 1) * blk;
        const int b = juce::jmin (n, end, at + (int) (0.3 * fs));
        // the last 150 ms of it at most: a long 808 is still gliding down early on
        const int a = juce::jmax (at + (int) (0.035 * fs), juce::jmax (juce::jmin (at + (int) (0.06 * fs), b - (int) (0.08 * fs)), b - (int) (0.15 * fs)));
        if (b - a > (int) (0.05 * fs))
            if (const float f = yinPitch (kick.data() + a, b - a, fs, 28.0f, 160.0f); f > 0.0f)
            { kickHz[(size_t) (kickN % 5)] = f; ++kickN; }
    }
    // the bass under it (before RECOIL), over the whole window
    if (const float f = yinPitch (before.data(), n, fs, 28.0f, 300.0f); f > 0.0f)
    { bassHz[(size_t) (bassN % 5)] = f; ++bassN; }
}

float WaveView::keyOf (const std::array<float, 5>& v, int cnt) const
{
    const int k = juce::jmin (cnt, 5);
    if (k == 0) return 0.0f;
    std::array<float, 5> t = v;
    std::sort (t.begin(), t.begin() + k);
    return t[(size_t) (k / 2)];
}

int WaveView::nodeAt (juce::Point<float> p) const
{
    const double cyc = cycleSec();
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const juce::Point<float> c (tx (nodes[(size_t) i].x * cyc), yLevel (shownGain (nodes, nodes[(size_t) i].x)));
        if (c.getDistanceFrom (p) < 8.0f) return i;
    }
    return -1;
}

int WaveView::segmentAt (float x) const
{
    const double sx = xt (x) / cycleSec();
    for (int i = 1; i < (int) nodes.size(); ++i)
        if (sx >= nodes[(size_t) i - 1].x && sx <= nodes[(size_t) i].x) return i;
    return -1;
}

void WaveView::commit (const juce::String&)
{
    juce::String name = processor.currentShapeName();
    if (shapes::builtIn (processor.shapeIndex()) != nullptr) name << " (edited)";
    processor.setCustomNodes (nodes, name);
    if (onEdited) onEdited();
}

void WaveView::paintAt (juce::Point<float> p)
{
    const double x = juce::jlimit (0.0, 1.0, xt (p.x) / cycleSec());
    const double step = 1.0 / gridDiv;
    const int q = juce::jlimit (0, gridDiv - 1, (int) std::floor (x / step));
    const float xa = (float) (q * step), xb = (float) ((q + 1) * step);
    const float y = (float) juce::jlimit (0.0, 1.2, levelOf (p.y));
    std::vector<rc::Node> out;
    for (auto& n : nodes) if (n.x < xa - 1e-6f || n.x >= xb - 1e-6f || n.x == 0.0f) out.push_back (n);
    if (q == 0) { for (auto& n : out) if (n.x == 0.0f) n.y = y; }
    else out.push_back ({ xa, y, 0.0f, false });
    if (tool == Tool::Step) out.push_back ({ xb - 0.0005f, y, 0.0f, false });
    else if (q == gridDiv - 1) { for (auto& n : out) if (n.x >= 0.9999f) n.y = y; }
    nodes = shapes::sanitise (out);
    commit();
}

void WaveView::mirror()
{
    nodes = processor.currentNodes();
    const int N = (int) nodes.size();
    std::vector<rc::Node> r ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        const auto& src = nodes[(size_t) (N - 1 - i)];
        r[(size_t) i] = { 1.0f - src.x, src.y, 0.0f, false };
        if (i > 0) { const auto& seg = nodes[(size_t) (N - i)]; r[(size_t) i].bend = -seg.bend; r[(size_t) i].s = seg.s; }
    }
    nodes = shapes::sanitise (r);
    commit();
}

void WaveView::clear()
{
    nodes = { { 0, 0 }, { 0.1f, 0 }, { 0.5f, 1, 2, true }, { 1, 1 } };
    commit();
}

void WaveView::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    hoverNode = editing ? nodeAt (p) : -1;
    hoverTab = 0;
    if (! editing)
    {
        const double cyc = cycleSec();
        const float gMin = (float) yLevel (shownGain (nodes, 0.0));
        (void) gMin;
        if (p.x > px1() && p.x < px1() + 44.0f) hoverTab = 1;                        // depth (the dB scale column)
        else if (juce::roundToInt (param ("trigger")) != 0 && std::abs (p.x - tx (cyc)) < 7.0f && mainR().contains (p)) hoverTab = 2;   // length
    }
    setMouseCursor (hoverTab == 1 ? juce::MouseCursor::UpDownResizeCursor : hoverTab == 2 ? juce::MouseCursor::LeftRightResizeCursor
                    : hoverNode >= 0 ? juce::MouseCursor::DraggingHandCursor
                    : editing && tool != Tool::Point ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (! editing)
    {
        mouseMove (e);
        if (hoverTab == 1) { dragTab = 1; depth0 = param ("depth"); dragY0 = p.y; beginGesture ("depth"); return; }
        if (hoverTab == 2)
        {
            dragTab = 2; frozenSpan = cycleSec(); beginGesture ("length");
            if (param ("fit") > 0.5f) { beginGesture ("fit"); setParam ("fit", 0.0f); endGesture ("fit"); }
            return;
        }
        if (mainR().contains (p) || laneR().contains (p)) frozen = ! frozen;
        repaint();
        return;
    }
    if (tool != Tool::Point) { paintAt (p); return; }
    const int n = nodeAt (p);
    if (e.mods.isPopupMenu() && n >= 0)
    {
        juce::PopupMenu m;
        const bool hasSeg = n > 0;
        m.addItem (1, "S-curve into this point", hasSeg, hasSeg && nodes[(size_t) n].s);
        m.addItem (2, "Fast start", hasSeg, hasSeg && ! nodes[(size_t) n].s && nodes[(size_t) n].bend > 0.01f);
        m.addItem (3, "Slow start", hasSeg, hasSeg && ! nodes[(size_t) n].s && nodes[(size_t) n].bend < -0.01f);
        m.addItem (4, "Straight", hasSeg, hasSeg && ! nodes[(size_t) n].s && std::abs (nodes[(size_t) n].bend) <= 0.01f);
        m.addSeparator();
        m.addItem (5, "Delete point", n > 0 && n < (int) nodes.size() - 1);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, n] (int r)
        {
            if (r == 0 || n >= (int) nodes.size()) return;
            auto& nd = nodes[(size_t) n];
            if (r == 1) { nd.s = true; if (std::abs (nd.bend) < 0.5f) nd.bend = 1.5f; }
            if (r == 2) { nd.s = false; nd.bend = 2.0f; }
            if (r == 3) { nd.s = false; nd.bend = -2.0f; }
            if (r == 4) { nd.s = false; nd.bend = 0.0f; }
            if (r == 5) nodes.erase (nodes.begin() + n);
            commit();
        });
        return;
    }
    if (n >= 0) { dragNode = n; return; }
    if (e.mods.isAltDown() || e.mods.isCommandDown())
    {
        dragSeg = segmentAt (p.x);
        if (dragSeg > 0) { dragY0 = p.y; bend0 = nodes[(size_t) dragSeg].bend; }
    }
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (dragTab == 1)
    {
        // the tab sits at the shape's lowest gain: move it and DEPTH follows
        double sMin = 1.0; for (int k = 0; k <= 200; ++k) sMin = juce::jmin (sMin, warp (rc::shapeAt (nodes, k / 200.0), param ("curve") * 0.01));
        const double g = juce::jlimit (0.0, 1.0, levelOf (p.y));
        const double mixv = juce::jmax (0.01, param ("mix") * 0.01);
        setParam ("depth", (float) juce::jlimit (0.0, 100.0, (1.0 - g) / juce::jmax (0.05, 1.0 - sMin) / mixv * 100.0));
        return;
    }
    if (dragTab == 2) { setParam ("length", (float) juce::jlimit (20.0, 2000.0, xt (p.x) * 1000.0)); return; }
    if (! editing) return;
    if (tool != Tool::Point) { paintAt (p); return; }
    if (dragNode >= 0 && dragNode < (int) nodes.size())
    {
        double x = xt (p.x) / cycleSec();
        if (snap) x = std::round (x * gridDiv) / gridDiv;
        const int last = (int) nodes.size() - 1;
        const double lo = dragNode == 0 ? 0.0 : nodes[(size_t) dragNode - 1].x + 0.001;
        const double hi = dragNode == 0 ? 0.0 : dragNode == last ? 1.0 : nodes[(size_t) dragNode + 1].x - 0.001;
        double y = juce::jlimit (0.0, 1.2, levelOf (p.y));
        if (std::abs (y - 1.0) < 0.025) y = 1.0;
        if (y < 0.025) y = 0.0;
        nodes[(size_t) dragNode].x = (float) juce::jlimit (lo, hi, x);
        nodes[(size_t) dragNode].y = (float) y;
        commit();
    }
    else if (dragSeg > 0 && dragSeg < (int) nodes.size())
    {
        auto& nd = nodes[(size_t) dragSeg];
        const float dir = nodes[(size_t) dragSeg].y >= nodes[(size_t) dragSeg - 1].y ? 1.0f : -1.0f;
        nd.bend = juce::jlimit (-6.0f, 6.0f, bend0 + (dragY0 - p.y) * 0.04f * dir);
        if (nd.s) nd.bend = std::abs (nd.bend);
        commit();
    }
}

void WaveView::mouseUp (const juce::MouseEvent&)
{
    if (dragTab == 1) endGesture ("depth");
    if (dragTab == 2) endGesture ("length");
    dragTab = 0; frozenSpan = -1.0; dragNode = -1; dragSeg = -1;
}

void WaveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! editing || tool != Tool::Point) return;
    const int n = nodeAt (e.position);
    if (n > 0 && n < (int) nodes.size() - 1) { nodes.erase (nodes.begin() + n); commit(); return; }
    if (n >= 0 || (int) nodes.size() >= shapes::kMaxNodes) return;
    double x = juce::jlimit (0.0, 1.0, xt (e.position.x) / cycleSec());
    if (snap) x = std::round (x * gridDiv) / gridDiv;
    for (auto& nd : nodes) if (std::abs (nd.x - x) < 1e-4) return;
    nodes.push_back ({ (float) x, (float) juce::jlimit (0.0, 1.2, levelOf (e.position.y)), 0.0f, false });
    nodes = shapes::sanitise (nodes);
    commit();
}

void WaveView::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto lane = laneR(), main = mainR();
    const float x0 = px0(), x1 = px1();
    const int trig = roundToInt (param ("trigger"));
    const double cyc = cycleSec();

    // ---- header legend
    if (! editing)
    {
        g.setFont (lnf.caps (9.0f, 1.6f));
        float x = 16.0f;
        auto item = [&] (const String& s, Colour c, bool hatch)
        {
            if (hatch)
            {
                Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (Rectangle<int> ((int) x, 17, 12, 4));
                g.setColour (Pal::orange);
                for (int k = -4; k < 16; k += 3) g.drawLine ((float) (x + k), 21.0f, (float) (x + k + 4), 17.0f, 1.2f);
            }
            else { g.setColour (c); g.fillRoundedRectangle (x, 17.5f, 12.0f, 3.0f, 1.0f); }
            g.setColour (Colour (0xff8a919b));
            g.drawText (s, Rectangle<float> (x + 18.0f, 12.0f, 120.0f, 14.0f), Justification::centredLeft, false);
            x += 18.0f + (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), s) + 16.0f;
        };
        if (hitStyle())
        {
            if (showPhase) item (laneIsDrums ? "DRUMS" : "KICK", Pal::red, false);
            item ("BASS AFTER", Pal::text, false);
        }
        else
        {
            item ("AFTER", Pal::text, false);
            item ("REMOVED", Pal::orange, true);
            if (showPhase) item ("KICK", Pal::red, false);
        }
        const bool mapMode = juce::roundToInt (processor.apvts.getRawParameterValue ("trigger")->load()) == 3;
        const char* kindNames[5] = { "SUB SHAPE", "SNARE SHAPE", "PLUCK SHAPE", "MID SHAPE", "NOTE SHAPE" };
        const char* hitDucks[4] = { "KICK DUCK", "SNARE DUCK", "RIM DUCK", "GHOST DUCK" };
        const bool rmMode = juce::roundToInt (processor.apvts.getRawParameterValue ("trigger")->load()) == 4;
        item (rmMode ? "RM SQUEEZE" : hitStyle() && ! autoShape ? "DUCK" : processor.bassAutoActive() ? "AUTO DUCK" : mapMode && ! editing && autoShape ? hitDucks[juce::jlimit (0, 3, lastKind)]
              : processor.bassShapesActive() ? kindNames[processor.shapeSlot()]
              : mapMode ? (processor.shapeSlot() == 1 ? "SNARE SHAPE" : "KICK SHAPE") : "SHAPE", Pal::orange, false);
    }

    // ---- grid
    {
        const double t1 = trig == 0 ? cyc * 1.08 : capWinSec;   // the whole view
        if (trig == 0 && roundToInt (param ("rateMode")) == 0)
        {
            const double beats = rateBeatsOf (processor.apvts);
            for (int k = 0; k <= 16; ++k)
            {
                const double t = cyc * k / 16.0;
                const float x = tx (t);
                g.setColour (k % 4 == 0 ? Colour (0xff23262c) : Colour (0xff16181c));
                g.fillRect (x, lane.getY(), 1.0f, main.getBottom() - lane.getY());
                if (k % 4 == 0)
                {
                    const double b = beats * k / 16.0;
                    const int whole = (int) std::floor (b + 1e-6);
                    const int six = roundToInt ((b - whole) * 4.0);
                    g.setFont (lnf.chak (10.5f));
                    g.setColour (k == 0 ? Pal::orange : Colour (0xff6c737d));
                    g.drawText (six == 0 ? String (whole + 1) : String (whole + 1) + "." + String (six + 1), Rectangle<float> (x - 24.0f, main.getBottom() + 8.0f, 48.0f, 14.0f), Justification::centred, false);
                }
            }
        }
        else
        {
            const double steps[] = { 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5 };
            double st = 0.5; for (double s : steps) if (t1 / s <= 12.0) { st = s; break; }
            for (int k = 0; k * st <= t1 + 1e-9; ++k)
            {
                const float x = tx (k * st);
                g.setColour (Colour (0xff23262c)); g.fillRect (x, lane.getY(), 1.0f, main.getBottom() - lane.getY());
                const double sub = k * st + st * 0.5;
                if (sub < t1) { g.setColour (Colour (0xff16181c)); g.fillRect (tx (sub), lane.getY(), 1.0f, main.getBottom() - lane.getY()); }
                g.setFont (lnf.chak (10.5f));
                g.setColour (k == 0 ? Pal::orange : Colour (0xff6c737d));
                g.drawText (k == 0 ? String ("0 ms") : String (roundToInt (k * st * 1000.0)), Rectangle<float> (x - 24.0f, main.getBottom() + 8.0f, 48.0f, 14.0f), Justification::centred, false);
            }
        }
    }
    g.setColour (Colour (0xff1c1f24));
    if (twoLanes()) g.fillRect (x0, lane.getBottom() + 7.0f, x1 - x0, 1.0f);
    const bool hit = hitStyle();

    // ---- captured waves, one value per pixel column (the sample with the largest swing)
    const int n = (int) after.size();
    const int cols = (int) (x1 - x0);
    std::vector<float> cK ((size_t) cols + 1), cB ((size_t) cols + 1), cA ((size_t) cols + 1);
    float mK = 1e-6f, mB = 1e-6f;
    if (n > 0)
    {
        for (int c = 0; c <= cols; ++c)
        {
            const int i0 = (int) ((double) c / (cols + 1) * n), i1 = juce::jmax (i0 + 1, (int) ((double) (c + 1) / (cols + 1) * n));
            float k = 0, b = 0, a = 0;
            for (int i = i0; i < juce::jmin (i1, n); ++i)
            {
                if (std::abs (kick[(size_t) i]) > std::abs (k)) k = kick[(size_t) i];
                if (std::abs (before[(size_t) i]) > std::abs (b)) b = before[(size_t) i];
                if (std::abs (after[(size_t) i]) > std::abs (a)) a = after[(size_t) i];
            }
            cK[(size_t) c] = k; cB[(size_t) c] = b; cA[(size_t) c] = a;
            mK = jmax (mK, std::abs (k)); mB = jmax (mB, std::abs (b));
        }
    }

    if (showPhase && ! laneIsDrums)
    {
        // keys: the kick's note and the bass's note, and how far apart they are
        const float kHz = keyOf (kickHz, kickN), bHz = keyOf (bassHz, bassN);
        float x = x1;
        auto right = [&] (const String& t, Font f, Colour c)
        {
            g.setFont (f); g.setColour (c);
            const float w = (float) GlyphArrangement::getStringWidthInt (f, t) + 1.0f;
            x -= w;
            g.drawText (t, Rectangle<float> (x, 33.0f, w + 2.0f, 14.0f), Justification::centredLeft, false);
            x -= 10.0f;
        };
        auto key = [&] (float hz)
        {
            if (hz <= 0.0f) return String::fromUTF8 ("\xe2\x80\x94");
            int ct = 0; const String nm = noteName (hz, &ct);
            return nm + "  " + (ct >= 0 ? "+" : minusSign()) + String (std::abs (ct)) + String::fromUTF8 ("\xc2\xa2");
        };
        if (kHz > 0.0f && bHz > 0.0f)
        {
            const double st = 12.0 * std::log2 (bHz / kHz);
            const int cls = ((juce::roundToInt (st) % 12) + 12) % 12;
            const bool sameNote = std::abs (st - 12.0 * std::round (st / 12.0)) < 0.5;
            String tag; Colour tc = Pal::amber;
            if (sameNote) { tag = "SAME NOTE"; tc = Pal::lock; }
            else if (cls == 7 || cls == 5) { tag = cls == 7 ? "A FIFTH APART" : "A FOURTH APART"; tc = Pal::lock.withAlpha (0.8f); }
            else
            {
                // by note, not by octave (an 808 an octave and two semitones up is still two semitones off the key)
                const int d = cls > 6 ? cls - 12 : cls;
                tag = String (std::abs (d)) + (std::abs (d) == 1 ? " SEMITONE " : " SEMITONES ") + (d > 0 ? "BASS HIGHER" : "BASS LOWER");
            }
            right (tag, lnf.caps (8.5f, 1.2f), tc);
        }
        right (key (bHz), lnf.chak (11.0f), Pal::text);
        right ("BASS", lnf.caps (8.5f, 1.6f), Colour (0xff6c737d));
        right (key (kHz), lnf.chak (11.0f), processor.sidechainConnected.load() ? Pal::red : Pal::dim);
        right ("KICK", lnf.caps (8.5f, 1.6f), Colour (0xff6c737d));
    }
    if (hit && showPhase)
    {
        g.setFont (lnf.caps (8.5f, 1.6f)); g.setColour (Colour (0xff6c737d));
        g.drawText (laneIsDrums ? String ("HIT  ") + String::fromUTF8 ("\xc2\xb7") + "  THE DRUMS (SIDECHAIN), THE BASS AND ITS DUCK"
                                : String ("HIT  ") + String::fromUTF8 ("\xc2\xb7") + "  KICK LOWS, THE BASS AND ITS DUCK", Rectangle<float> (jmax (x0 + 14.0f, tx (0.0) + 12.0f), 34.0f, 400.0f, 12.0f), Justification::centredLeft, false);
    }
    if (twoLanes())
    {
    // ---- phase lane
    g.setFont (lnf.caps (8.5f, 1.6f));
    g.setColour (Colour (0xff6c737d));
    static const char* kindNames[4] = { "KICK", "SNARE", "RIM", "GHOST NOTE" };
    g.drawText (laneIsDrums ? String (kindNames[jlimit (0, 3, lastKind)]) + "  " + String::fromUTF8 ("\xc2\xb7") + "  THE DRUMS (SIDECHAIN) vs BASS"
                            : "PHASE  " + String::fromUTF8 ("\xc2\xb7") + "  KICK LOWS vs BASS", Rectangle<float> (x0 + 14.0f, lane.getY() - 18.0f, 400.0f, 12.0f), Justification::centredLeft, false);
    const float lc = lane.getCentreY();
    g.setColour (Colour (0xff1c1f24)); g.fillRect (x0, lc, x1 - x0, 1.0f);
    const bool sc = processor.sidechainConnected.load();
    if (n > 0)
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (lane.toNearestInt());
        auto linePath = [&] (const std::vector<float>& v, float scale)
        {
            Path p; for (int c = 0; c <= cols; ++c) { const Point<float> pt (x0 + (float) c, lc - v[(size_t) c] * scale); if (c == 0) p.startNewSubPath (pt); else p.lineTo (pt); }
            return p;
        };
        if (sc && mK > 1e-4f) { g.setColour (Pal::red.withAlpha (0.95f)); g.strokePath (linePath (cK, lane.getHeight() * 0.42f / mK), PathStrokeType (1.5f)); }
        g.setColour (Pal::text.withAlpha (0.85f)); g.strokePath (linePath (cA, lane.getHeight() * 0.42f / mB), PathStrokeType (1.3f));
    }
    // readout
    if (! laneIsDrums)
    {
        const auto& eng = processor.getEngine();
        const int ph = roundToInt (param ("phase"));
        const float m = eng.match.load();
        g.setFont (lnf.caps (7.5f, 1.0f)); g.setColour (Colour (0xff6c737d));
        g.drawText ("MATCH", Rectangle<float> (x1 + 2.0f, lane.getY() + 2.0f, 42.0f, 10.0f), Justification::centred, false);
        g.setFont (lnf.chak (12.5f));
        g.setColour (ph == 2 || ! sc ? Pal::dim : (m > 0.5f ? Pal::lock : Pal::amber));
        g.drawText (ph == 2 || ! sc ? String::fromUTF8 ("\xe2\x80\x94") : (m >= 0 ? "+" : minusSign()) + String (std::abs (m), 2), Rectangle<float> (x1 + 2.0f, lane.getY() + 13.0f, 42.0f, 16.0f), Justification::centred, false);
        const float th = ph == 1 ? param ("rotate") : eng.thetaDeg.load();
        g.setFont (lnf.chak (10.0f)); g.setColour (Pal::caps);
        g.drawText (ph == 2 ? String ("OFF") : (th >= 0 ? "+" : minusSign()) + String (std::abs (roundToInt (th))) + String::fromUTF8 ("\xc2\xb0"), Rectangle<float> (x1 + 2.0f, lane.getY() + 30.0f, 42.0f, 14.0f), Justification::centred, false);
    }
    if (! sc && trig != 1)
    {
        g.setFont (lnf.caps (9.5f, 1.2f)); g.setColour (Pal::dim);
        g.drawText (trig == 2 ? "SEND THE KICK TO RECOIL'S SIDECHAIN INPUT"
                    : laneIsDrums ? "SEND THE DRUM BUS TO RECOIL'S SIDECHAIN TO SEE THE HITS HERE" : "SIDECHAIN NOT CONNECTED: PHASE NEEDS THE KICK", lane, Justification::centred, false);
    }
    }

    // ---- main lane: before (hatched), after (white)
    const float mt = main.getY() + 10.0f, mb = main.getBottom() - 6.0f, mc = (mt + mb) * 0.5f, amp = (mb - mt) * 0.5f * 0.96f;
    g.setColour (Colour (0xff1c1f24)); g.fillRect (x0, mc, x1 - x0, 1.0f);
    if (hit)
    {
        // ---- HIT style: one lane. Back to front: the duck (a solid fill from the top down to the gain), the kick (filled),
        // the bass of the last few hits (fading), the bass before (faint) and after RECOIL (bright)
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (main.toNearestInt());
        // the gain per pixel column: what this hit really got (the lowest in the column), or the shape until a hit is caught
        std::vector<float> cG ((size_t) cols + 1, 1.0f);
        for (int c = 0; c <= cols; ++c)
        {
            if (haveCapture && n > 0)
            {
                const int i0 = (int) ((double) c / (cols + 1) * n), i1 = juce::jmax (i0 + 1, (int) ((double) (c + 1) / (cols + 1) * n));
                float m = 1.0f; for (int i = i0; i < juce::jmin (i1, n); ++i) m = juce::jmin (m, gain[(size_t) i]);
                cG[(size_t) c] = m;
            }
            else
            {
                const double t = xt (x0 + (float) c);
                cG[(size_t) c] = (float) (t < 0.0 ? 1.0 : trig == 0 ? shownGain (nodes, std::fmod (t / cyc, 1.0)) : shownGain (nodes, juce::jmin (1.0, t / cyc)));
            }
        }
        {
            const float top = yLevel (1.0);                                   // 0 dB: no duck, no fill
            Path duck; duck.startNewSubPath (x0, top);
            for (int c = 0; c <= cols; ++c) duck.lineTo (x0 + (float) c, juce::jmax (top, yLevel (cG[(size_t) c])));
            duck.lineTo (x0 + (float) cols, top); duck.closeSubPath();
            g.setGradientFill (ColourGradient (Pal::orange.withAlpha (0.78f), 0.0f, top, Pal::orange.withAlpha (0.38f), 0.0f, main.getBottom(), false));
            g.fillPath (duck);
        }
        if (n > 0)
        {
            auto lobe = [&] (const std::vector<float>& v, float scale)
            {
                Path p; p.startNewSubPath (x0, mc);
                for (int c = 0; c <= cols; ++c) p.lineTo (x0 + (float) c, mc - v[(size_t) c] * scale);
                p.lineTo (x0 + (float) cols, mc); p.closeSubPath();
                return p;
            };
            auto line = [&] (const std::vector<float>& v, float scale)
            {
                Path p; for (int c = 0; c <= cols; ++c) { const Point<float> pt (x0 + (float) c, mc - v[(size_t) c] * scale); if (c == 0) p.startNewSubPath (pt); else p.lineTo (pt); }
                return p;
            };
            const bool sc = processor.sidechainConnected.load();
            if (sc && showPhase && mK > 1e-4f)
            {
                g.setColour (Pal::red.withAlpha (0.55f)); g.fillPath (lobe (cK, amp * 0.9f / mK));
                g.setColour (Pal::red); g.strokePath (line (cK, amp * 0.9f / mK), PathStrokeType (1.0f));
            }
            if (mB > 1e-5f)
            {
                // the last few hits, fading (only those captured over the same window)
                float alpha = 0.16f;
                for (const auto& e : echoes)
                {
                    if ((int) e.size() != n) continue;
                    std::vector<float> cE ((size_t) cols + 1);
                    for (int c = 0; c <= cols; ++c)
                    {
                        const int i0 = (int) ((double) c / (cols + 1) * n), i1 = juce::jmax (i0 + 1, (int) ((double) (c + 1) / (cols + 1) * n));
                        float a = 0; for (int i = i0; i < juce::jmin (i1, n); ++i) if (std::abs (e[(size_t) i]) > std::abs (a)) a = e[(size_t) i];
                        cE[(size_t) c] = a;
                    }
                    g.setColour (Pal::text.withAlpha (alpha)); g.strokePath (line (cE, amp / mB), PathStrokeType (1.0f));
                    alpha *= 0.6f;
                }
                g.setColour (Colour (0xff8a919b).withAlpha (0.35f)); g.strokePath (line (cB, amp / mB), PathStrokeType (0.9f));
                g.setColour (Colour (0xffe9edf1)); g.strokePath (line (cA, amp / mB), PathStrokeType (1.4f));
            }
        }
        // the edge of the duck, on top
        {
            Path edge;
            for (int c = 0; c <= cols; ++c) { const Point<float> pt (x0 + (float) c, yLevel (cG[(size_t) c])); if (c == 0) edge.startNewSubPath (pt); else edge.lineTo (pt); }
            g.setColour (Pal::orange.withAlpha (0.3f)); g.strokePath (edge, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (Colour (0xffffb27a)); g.strokePath (edge, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        if (n == 0 || mB <= 1e-5f)
        {
            g.setFont (lnf.caps (9.5f, 1.2f)); g.setColour (Pal::faint);
            g.drawText ("PLAY SOMETHING THROUGH RECOIL", main.withTrimmedTop (main.getHeight() * 0.55f), Justification::centredTop, false);
        }
        else if (! processor.sidechainConnected.load() && showPhase && trig != 1)
        {
            g.setFont (lnf.caps (9.5f, 1.2f)); g.setColour (Pal::dim);
            g.drawText (laneIsDrums ? "SEND THE DRUM BUS TO RECOIL'S SIDECHAIN TO SEE THE HITS" : "SEND THE KICK TO RECOIL'S SIDECHAIN TO SEE IT HERE",
                        main.withTrimmedTop (main.getHeight() - 40.0f), Justification::centred, false);
        }
    }
    else if (n > 0 && mB > 1e-5f)
    {
        auto lobe = [&] (const std::vector<float>& v)
        {
            Path p; p.startNewSubPath (x0, mc);
            for (int c = 0; c <= cols; ++c) p.lineTo (x0 + (float) c, mc - v[(size_t) c] / mB * amp);
            p.lineTo (x0 + (float) cols, mc); p.closeSubPath();
            return p;
        };
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (main.toNearestInt());
        const auto pb = lobe (cB);
        {
            Graphics::ScopedSaveState s2 (g);
            g.reduceClipRegion (pb);
            g.setColour (Pal::orange.withAlpha (0.10f)); g.fillRect (main);
            g.setColour (Pal::orange.withAlpha (0.55f));
            for (float k = main.getX() - main.getHeight(); k < main.getRight(); k += 5.0f)
                g.drawLine (k, main.getBottom(), k + main.getHeight(), main.getY(), 1.2f);
        }
        g.setColour (Pal::orange.withAlpha (0.35f)); g.strokePath (pb, PathStrokeType (0.8f));
        g.setColour (Colour (0xffe9edf1).withAlpha (0.9f)); g.fillPath (lobe (cA));
    }
    else
    {
        g.setFont (lnf.caps (9.5f, 1.2f)); g.setColour (Pal::faint);
        g.drawText ("PLAY SOMETHING THROUGH RECOIL", main.withTrimmedTop (main.getHeight() * 0.55f), Justification::centredTop, false);
    }

    // ---- shape (HIT style: the edge of the fill is the duck)
    if (! hit)
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (main.toNearestInt());
        g.setColour (Colour (0xff2a2e35));
        for (float x = x0; x < x1; x += 6.0f) g.fillRect (x, yLevel (1.0), 2.0f, 1.0f);
        Path p;
        for (int c = 0; c <= cols; ++c)
        {
            const double t = xt (x0 + (float) c);
            double gv;
            if (t < 0.0) gv = trig == 0 ? shownGain (nodes, 1.0) : 1.0;
            else if (t > cyc && trig == 0) gv = shownGain (nodes, std::fmod (t / cyc, 1.0));
            else gv = shownGain (nodes, juce::jmin (1.0, t / cyc));
            const Point<float> pt (x0 + (float) c, yLevel (gv));
            if (c == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.setColour (Pal::orange.withAlpha (0.25f)); g.strokePath (p, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Pal::orange); g.strokePath (p, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
        // nodes (and in EDIT, the bend handles)
        for (int i = 0; i < (autoShape ? 0 : (int) nodes.size()); ++i)
        {
            const auto& nd = nodes[(size_t) i];
            const Point<float> c (tx (nd.x * cyc), yLevel (shownGain (nodes, nd.x)));
            if (editing && i > 0 && nd.x - nodes[(size_t) i - 1].x > 0.03f && std::abs (nd.y - nodes[(size_t) i - 1].y) > 0.02f)
            {
                const double xm = 0.5 * (nd.x + nodes[(size_t) i - 1].x);
                const Point<float> m (tx (xm * cyc), yLevel (shownGain (nodes, xm)));
                Path d; d.addRectangle (-3.5f, -3.5f, 7.0f, 7.0f);
                d.applyTransform (AffineTransform::rotation (MathConstants<float>::pi * 0.25f).translated (m));
                g.setColour (Pal::well); g.fillPath (d);
                g.setColour (Pal::brass); g.strokePath (d, PathStrokeType (1.4f));
            }
            const bool hot = i == hoverNode || i == dragNode;
            const float r = editing ? (hot ? 6.0f : 4.5f) : 3.5f;
            if (i == 0 && ! editing) continue;
            g.setColour (hot ? Colours::white : Pal::well);
            g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (c));
            g.setColour (Pal::orange);
            g.drawEllipse (Rectangle<float> (r * 2, r * 2).withCentre (c), 1.8f);
            if (hot && editing)
            {
                const String tip = dbText (nd.y) + " dB  " + String::fromUTF8 ("\xc2\xb7") + "  " + String (nd.x * (trig == 0 ? 1.0 : cyc * 1000.0), trig == 0 ? 3 : 0) + (trig == 0 ? "" : " ms");
                g.setFont (lnf.chak (11.0f));
                const float w = (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), tip) + 16.0f;
                auto box = Rectangle<float> (c.x + 10.0f, c.y + 10.0f, w, 20.0f);
                if (box.getRight() > x1) box.setX (c.x - 10.0f - w);
                if (box.getBottom() > main.getBottom()) box.setY (c.y - 30.0f);
                g.setColour (Colour (0xff15171b)); g.fillRoundedRectangle (box, 4.0f);
                g.setColour (Colour (0xff3a3f47)); g.drawRoundedRectangle (box, 4.0f, 1.0f);
                g.setColour (Pal::text); g.drawText (tip, box, Justification::centred, false);
            }
        }
    }

    // hit marker
    if (haveCapture)
    {
        const float hx = tx (0.0);
        Path tri; tri.addTriangle (hx - 5.0f, lane.getY() - 8.0f, hx + 5.0f, lane.getY() - 8.0f, hx, lane.getY() - 1.0f);
        g.setColour (Pal::red); g.fillPath (tri);
        g.setColour (Pal::red.withAlpha (0.55f)); g.fillRect (hx, lane.getY(), 1.0f, main.getBottom() - lane.getY());
    }

    // ---- scale and DEPTH tab
    g.setFont (lnf.chak (10.0f));
    g.setColour (Colour (0xff6c737d));
    const std::pair<const char*, double> marks[] = { { "0", 1.0 }, { "-6", 0.5 }, { "-12", 0.25 } };
    for (auto& [l, v] : marks) g.drawText (fixMinus (l), Rectangle<float> (x1 + 4.0f, yLevel (v) - 7.0f, 40.0f, 14.0f), Justification::centred, false);
    g.setFont (lnf.caps (7.0f, 1.0f)); g.setColour (Colour (0xff4f565f));
    g.drawText ("dB", Rectangle<float> (x1 + 4.0f, main.getY(), 40.0f, 10.0f), Justification::centred, false);
    {
        double gMin = 9.0; for (int k = 0; k <= 200; ++k) gMin = jmin (gMin, shownGain (nodes, k / 200.0));
        const float y = yLevel (jmax (0.0, gMin));
        if (! editing)
        {
            g.setColour (Pal::orange.withAlpha (0.35f));
            for (float x = x0; x < x1; x += 6.0f) g.fillRect (x, y, 2.0f, 1.0f);
            // DYNAMIC: how shallow a soft hit's duck can get
            const float dyn = param ("dynamic") * 0.01f;
            if (dyn > 0.001f && trig != 0)
            {
                double sMin = 1.0; for (int k = 0; k <= 200; ++k) sMin = jmin (sMin, warp (rc::shapeAt (nodes, k / 200.0), param ("curve") * 0.01));
                const double a = param ("depth") * 0.01 * param ("mix") * 0.01 * (1.0 - dyn);
                const float y2 = yLevel (1.0 - a * (1.0 - sMin));
                g.setColour (Pal::brass.withAlpha (0.3f)); g.fillRoundedRectangle (x1 + 38.0f, y2, 5.0f, y - y2, 2.0f);
                g.setColour (Pal::brass); g.fillRoundedRectangle (x1 + 38.0f, y2 - 1.0f, 5.0f, 3.0f, 1.0f);
            }
            const auto tab = Rectangle<float> (x1 + 4.0f, y - 9.0f, 38.0f, 18.0f);
            g.setColour (hoverTab == 1 || dragTab == 1 ? Pal::amber : Pal::orange); g.fillRoundedRectangle (tab, 3.0f);
            g.setColour (Colour (0xff1a0c05)); g.setFont (lnf.chak (10.5f));
            g.drawText (dbText (gMin), tab, Justification::centred, false);
        }
    }

    // ---- LENGTH tab (AUDIO / MIDI)
    if (! editing && trig != 0)
    {
        const float xl = tx (cyc);
        g.setColour (Pal::brass.withAlpha (hoverTab == 2 || dragTab == 2 ? 1.0f : 0.8f));
        for (float y = main.getY(); y < main.getBottom(); y += 7.0f) g.fillRect (xl, y, 1.2f, 4.0f);
        const bool fit = param ("fit") > 0.5f;
        const String t = String::fromUTF8 ("\xe2\x9f\xb7 ") + String (roundToInt (cyc * 1000.0)) + " ms" + (processor.bassAutoActive() ? String::fromUTF8 (" \xc2\xb7 AUTO") : fit ? String::fromUTF8 (" \xc2\xb7 FIT") : String());
        g.setFont (lnf.chak (10.5f));
        const float w = (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), t) + 14.0f;
        const auto tab = Rectangle<float> (xl - 4.0f - w, yLevel (1.0) + 8.0f, w, 18.0f);
        g.setColour (Colour (0xff2a2418)); g.fillRoundedRectangle (tab, 3.0f);
        g.setColour (Colour (0xff6b5a36)); g.drawRoundedRectangle (tab, 3.0f, 1.0f);
        g.setColour (Pal::brass); g.drawText (t, tab, Justification::centred, false);
    }

    if (frozen)
    {
        g.setFont (lnf.caps (9.0f, 1.6f)); g.setColour (Pal::amber);
        g.drawText ("FROZEN  " + String::fromUTF8 ("\xc2\xb7") + "  CLICK TO GO LIVE", Rectangle<float> (x0, main.getBottom() - 20.0f, x1 - x0 - 6.0f, 14.0f), Justification::centredRight, false);
    }
    else if (editing)
    {
        g.setFont (lnf.caps (8.5f, 1.2f)); g.setColour (Colour (0xff6c737d));
        g.drawText (tool == Tool::Point ? String::fromUTF8 ("DRAG POINTS  \xc2\xb7  DOUBLE-CLICK ADD / REMOVE  \xc2\xb7  ALT-DRAG BENDS  \xc2\xb7  RIGHT-CLICK FOR CURVES")
                                        : String ("DRAG ACROSS TO DRAW ON THE GRID"),
                    Rectangle<float> (x0, main.getBottom() - 20.0f, x1 - x0 - 6.0f, 14.0f), Justification::centredRight, false);
    }
}

//==============================================================================
// SPECTRUM view
SpecView::SpecView (RecoilProcessor& p, RecoilLookAndFeel& l) : processor (p), lnf (l)
{
    window.resize ((size_t) kFft);
    for (int i = 0; i < kFft; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kFft - 1));
    buf.resize ((size_t) kFft * 2);
    for (int i = 0; i < kPts; ++i) ptHz[(size_t) i] = 20.0f * std::pow (1000.0f, (float) i / (float) (kPts - 1));
    sBefore.fill (-120.0f); sAfter.fill (-120.0f); sKick.fill (-120.0f);
    for (auto& d : drum) d.fill (-120.0f);
    typeBuf.resize ((size_t) kFft);
}

bool SpecView::mapMode() const { return juce::roundToInt (processor.apvts.getRawParameterValue ("trigger")->load()) == 3; }

float SpecView::xOf (float hz) const { const auto r = plot(); return r.getX() + std::log (hz / 20.0f) / std::log (1000.0f) * r.getWidth(); }
float SpecView::hzOf (float x) const { const auto r = plot(); return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth())); }
float SpecView::yDb (float db) const { const auto r = plot(); return r.getY() + (-db) / 84.0f * r.getHeight(); }

void SpecView::framePower (const ScopeRing& ring, int64_t start, Pts& out)
{
    ring.readFrom (start, buf.data(), kFft);
    for (int i = 0; i < kFft; ++i) buf[(size_t) i] *= window[(size_t) i];
    std::fill (buf.begin() + kFft, buf.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (buf.data(), true);
    const float fs = (float) processor.hostRate.load();
    const float binHz = fs / (float) kFft, norm = 4.0f / (float) kFft;
    for (int i = 0; i < kPts; ++i)
    {
        const float f = ptHz[(size_t) i], lo = f * 0.944f, hi = f * 1.059f;
        const int b0 = juce::jmax (1, (int) std::floor (lo / binHz)), b1 = juce::jmin (kFft / 2 - 1, (int) std::ceil (hi / binHz));
        float pw = 0.0f; int n = 0;
        if (b1 <= b0 + 1)
        {
            const float pos = f / binHz; const int a = juce::jlimit (1, kFft / 2 - 2, (int) pos); const float t = pos - (float) a;
            const float m = buf[(size_t) a] * (1.0f - t) + buf[(size_t) a + 1] * t;
            pw = m * m; n = 1;
        }
        else for (int b = b0; b <= b1; ++b) { pw += buf[(size_t) b] * buf[(size_t) b]; ++n; }
        const float tilt = std::pow (10.0f, 0.45f * std::log2 (f / 1000.0f));
        out[(size_t) i] = pw / (float) n * norm * norm * tilt;
    }
}

void SpecView::ema (Pts& o, const Pts& now, float k)
{
    // average the power (not the dB), so a kick that only sounds now and then still shows its real level
    for (int i = 0; i < kPts; ++i)
    {
        const float prev = std::pow (10.0f, o[(size_t) i] * 0.1f);
        o[(size_t) i] = 10.0f * std::log10 (prev + (now[(size_t) i] - prev) * k + 1e-14f);
    }
}

void SpecView::update (float dt)
{
    const float k = juce::jlimit (0.0f, 1.0f, dt * 2.5f);                   // ~0.4 s average: the duck and the gaps between hits together
    const int64_t start = processor.scopeAfter.total.load() - kFft;        // the same stretch of time from every ring
    framePower (processor.scopeBefore, start, pB);
    framePower (processor.scopeAfter, start, pA);
    framePower (processor.scopeSc, start, pS);
    ema (sBefore, pB, k); ema (sAfter, pA, k); ema (sKick, pS, k);

    if (mapMode())
    {
        // which hit was ducking during this frame: the removed power is shared out between them
        processor.scopeType.readFrom (start, typeBuf.data(), kFft);
        int cnt[4] = {};
        for (float t : typeBuf) cnt[juce::jlimit (0, 3, juce::roundToInt (t))]++;
        // learnt only from frames one kind of hit has (nearly) to itself: what that duck takes out of the bass while it
        // happens, and (with a sidechain) what the drum itself sounds like
        const int ducking = cnt[1] + cnt[2] + cnt[3];
        const int d = cnt[1] >= cnt[2] && cnt[1] >= cnt[3] ? 0 : cnt[2] >= cnt[3] ? 1 : 2;
        if (cnt[d + 1] > kFft / 4 && cnt[d + 1] > 3 * (ducking - cnt[d + 1]))
        {
            const float kk = juce::jmin (1.0f, k * 1.5f);
            Pts rem;
            for (int i = 0; i < kPts; ++i) rem[(size_t) i] = juce::jmax (1e-14f, pB[(size_t) i] - pA[(size_t) i]);
            if (removed[(size_t) d][0] == 0.0f) removed[(size_t) d].fill (-120.0f);
            ema (removed[(size_t) d], rem, kk);
            if (processor.sidechainConnected.load()) ema (drum[(size_t) d], pS, kk);
        }
    }
    repaint();
}

int SpecView::edgeAt (float x) const
{
    const float lo = processor.apvts.getRawParameterValue ("bandLo")->load(), hi = processor.apvts.getRawParameterValue ("bandHi")->load();
    if (std::abs (x - xOf (hi)) < 8.0f) return 1;
    if (std::abs (x - xOf (lo)) < 8.0f) return 0;
    return -1;
}

void SpecView::mouseMove (const juce::MouseEvent& e)
{
    hoverEdge = edgeAt (e.position.x);
    setMouseCursor (hoverEdge >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

juce::Rectangle<float> SpecView::edgeTab (int edge) const
{
    const auto r = plot();
    const float hz = processor.apvts.getRawParameterValue (edge == 0 ? "bandLo" : "bandHi")->load();
    return { xOf (hz) - 34.0f, r.getBottom() - 25.0f, 68.0f, 20.0f };
}

void SpecView::mouseDoubleClick (const juce::MouseEvent& e)
{
    // on a band edge (its line or its label): type the frequency
    int edge = edgeAt (e.position.x);
    for (int k = 1; k >= 0 && edge < 0; --k) if (edgeTab (k).contains (e.position)) edge = k;
    if (edge < 0) return;
    if (dragEdge >= 0) { if (auto* p = processor.apvts.getParameter (dragEdge == 0 ? "bandLo" : "bandHi")) p->endChangeGesture(); dragEdge = -1; }
    editingEdge = edge;
    const float hz = processor.apvts.getRawParameterValue (edge == 0 ? "bandLo" : "bandHi")->load();
    hzEditor.setFont (lnf.chak (11.5f));
    hzEditor.setJustification (juce::Justification::centred);
    hzEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d0e10));
    hzEditor.setColour (juce::TextEditor::textColourId, Pal::text);
    hzEditor.setColour (juce::TextEditor::outlineColourId, Pal::orange);
    hzEditor.setColour (juce::TextEditor::focusedOutlineColourId, Pal::orange);
    hzEditor.setColour (juce::TextEditor::highlightColourId, Pal::orange.withAlpha (0.4f));
    hzEditor.setInputRestrictions (10, "0123456789.,kKhHzZ ");
    hzEditor.setText (juce::String (juce::roundToInt (hz)), false);
    hzEditor.setBounds (edgeTab (edge).expanded (6.0f, 2.0f).toNearestInt());
    hzEditor.onReturnKey = [this] { finishHzEdit (true); };
    hzEditor.onEscapeKey = [this] { finishHzEdit (false); };
    hzEditor.onFocusLost = [this] { finishHzEdit (true); };
    if (hzEditor.getParentComponent() == nullptr) addChildComponent (hzEditor);
    hzEditor.setVisible (true);
    hzEditor.grabKeyboardFocus();
    hzEditor.selectAll();
}

void SpecView::finishHzEdit (bool apply)
{
    if (editingEdge < 0) return;
    const int edge = editingEdge; editingEdge = -1;
    hzEditor.setVisible (false);
    if (! apply) return;
    // "120", "120 Hz", "1.2k", "1,2 kHz"
    auto t = hzEditor.getText().trim().toLowerCase().replaceCharacter (',', '.');
    const bool k = t.containsChar ('k');
    float hz = t.retainCharacters ("0123456789.").getFloatValue() * (k ? 1000.0f : 1.0f);
    if (hz <= 0.0f) return;
    const float lo = processor.apvts.getRawParameterValue ("bandLo")->load(), hi = processor.apvts.getRawParameterValue ("bandHi")->load();
    hz = edge == 0 ? juce::jlimit (20.0f, juce::jmin (1000.0f, hi / 1.25f), hz) : juce::jlimit (juce::jmax (40.0f, lo * 1.25f), 16000.0f, hz);
    if (auto* b = processor.apvts.getParameter ("band"); b != nullptr && b->getValue() < 0.5f) { b->beginChangeGesture(); b->setValueNotifyingHost (1.0f); b->endChangeGesture(); }
    if (auto* p = processor.apvts.getParameter (edge == 0 ? "bandLo" : "bandHi"))
    { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (hz)); p->endChangeGesture(); }
    repaint();
}

void SpecView::mouseDown (const juce::MouseEvent& e)
{
    dragEdge = edgeAt (e.position.x);
    if (dragEdge < 0) return;
    if (auto* b = processor.apvts.getParameter ("band"); b != nullptr && b->getValue() < 0.5f) { b->beginChangeGesture(); b->setValueNotifyingHost (1.0f); b->endChangeGesture(); }
    if (auto* p = processor.apvts.getParameter (dragEdge == 0 ? "bandLo" : "bandHi")) p->beginChangeGesture();
}

void SpecView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragEdge < 0) return;
    const char* id = dragEdge == 0 ? "bandLo" : "bandHi";
    float hz = hzOf (e.position.x);
    const float lo = processor.apvts.getRawParameterValue ("bandLo")->load(), hi = processor.apvts.getRawParameterValue ("bandHi")->load();
    hz = dragEdge == 0 ? juce::jlimit (20.0f, juce::jmin (1000.0f, hi / 1.25f), hz) : juce::jlimit (juce::jmax (40.0f, lo * 1.25f), 16000.0f, hz);
    if (auto* p = processor.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (hz));
}

void SpecView::mouseUp (const juce::MouseEvent&)
{
    if (dragEdge >= 0) if (auto* p = processor.apvts.getParameter (dragEdge == 0 ? "bandLo" : "bandHi")) p->endChangeGesture();
    dragEdge = -1;
}

void SpecView::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = plot();
    // legend
    g.setFont (lnf.caps (9.0f, 1.6f));
    float x = 16.0f;
    auto item = [&] (const String& s, Colour c)
    {
        g.setColour (c); g.fillRoundedRectangle (x, 17.5f, 12.0f, 3.0f, 1.0f);
        g.setColour (Colour (0xff8a919b));
        g.drawText (s, Rectangle<float> (x + 18.0f, 12.0f, 120.0f, 14.0f), Justification::centredLeft, false);
        x += 18.0f + (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), s) + 16.0f;
    };
    const bool map = mapMode();
    const Colour typeCol[3] = { Pal::red, Pal::orange, Pal::brass };
    item ("AFTER", Pal::text); item ("BEFORE", Colour (0xff4b525c));
    if (map) { item ("KICK", typeCol[0]); item ("SNARE", typeCol[1]); item ("RIM", typeCol[2]); }
    else item ("KICK", Pal::red);
    item ("BAND", map ? Pal::orange.withAlpha (0.35f) : Pal::orange);

    for (float f : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        g.setColour (Colour (0xff1a1d22)); g.fillRect (xOf (f), r.getY(), 1.0f, r.getHeight());
        g.setFont (lnf.chak (10.5f)); g.setColour (Colour (0xff6c737d));
        g.drawText (f >= 1000.0f ? String (roundToInt (f / 1000.0f)) + "k" : String (roundToInt (f)), Rectangle<float> (xOf (f) - 20.0f, r.getBottom() + 6.0f, 40.0f, 14.0f), Justification::centred, false);
    }
    for (int d = 0; d >= -84; d -= 12)
    {
        g.setColour (Colour (0xff16181c)); g.fillRect (r.getX(), yDb ((float) d), r.getWidth(), 1.0f);
        g.setFont (lnf.chak (10.0f)); g.setColour (Colour (0xff6c737d));
        g.drawText (d == 0 ? String ("0") : minusSign() + String (-d), Rectangle<float> (r.getRight() + 6.0f, yDb ((float) d) - 7.0f, 40.0f, 14.0f), Justification::centred, false);
    }

    const bool split = processor.apvts.getRawParameterValue ("band")->load() > 0.5f;
    const float lo = processor.apvts.getRawParameterValue ("bandLo")->load(), hi = processor.apvts.getRawParameterValue ("bandHi")->load();
    const float bx0 = split ? xOf (lo) : r.getX(), bx1 = split ? xOf (hi) : r.getRight();
    g.setColour (Pal::orange.withAlpha (0.06f)); g.fillRect (Rectangle<float> (bx0, r.getY(), bx1 - bx0, r.getHeight()));

    auto path = [&] (const std::array<float, kPts>& v, bool close)
    {
        Path p;
        for (int i = 0; i < kPts; ++i)
        {
            const Point<float> pt (xOf (ptHz[(size_t) i]), jmin (r.getBottom(), yDb (v[(size_t) i])));
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        if (close) { p.lineTo (r.getRight(), r.getBottom()); p.lineTo (r.getX(), r.getBottom()); p.closeSubPath(); }
        return p;
    };
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (r.toNearestInt());
        g.setColour (Colour (0xff3a3f47).withAlpha (0.5f)); g.fillPath (path (sBefore, true));
        g.setColour (Colour (0xff5d646e)); g.strokePath (path (sBefore, false), PathStrokeType (1.0f));
        g.setColour (Colour (0xffe9edf1).withAlpha (0.16f)); g.fillPath (path (sAfter, true));
        if (map)
        {
            // MAP: what each kind of hit takes out of the bass while it ducks (red kicks, orange snares, brass rims)
            for (int t = 0; t < 3; ++t)
                if (removed[(size_t) t][0] != 0.0f && *std::max_element (removed[(size_t) t].begin(), removed[(size_t) t].end()) > -84.0f)
                {
                    g.setColour (typeCol[t].withAlpha (0.22f)); g.fillPath (path (removed[(size_t) t], true));
                    g.setColour (typeCol[t].withAlpha (0.9f)); g.strokePath (path (removed[(size_t) t], false), PathStrokeType (1.3f));
                }
        }
        g.setColour (Pal::text); g.strokePath (path (sAfter, false), PathStrokeType (1.6f));
        if (map)
        {
            if (processor.sidechainConnected.load())
                for (int t = 0; t < 3; ++t)
                    if (drum[(size_t) t][kPts / 2] > -119.0f)
                    {
                        Path dp; PathStrokeType (1.6f).createDashedStroke (dp, path (drum[(size_t) t], false), std::array<float, 2> { 6.0f, 3.0f }.data(), 2);
                        g.setColour (typeCol[t]); g.fillPath (dp);
                        // name it at its loudest point
                        const auto& v = drum[(size_t) t];
                        const int pk = (int) (std::max_element (v.begin(), v.end()) - v.begin());
                        const float lx = jlimit (r.getX() + 4.0f, r.getRight() - 50.0f, xOf (ptHz[(size_t) pk]) - 25.0f);
                        g.setFont (lnf.caps (8.5f, 1.4f));
                        g.drawText (t == 0 ? "KICK" : t == 1 ? "SNARE" : "RIM", Rectangle<float> (lx, jmax (r.getY() + 2.0f, yDb (v[(size_t) pk]) - 16.0f), 50.0f, 12.0f), Justification::centred, false);
                    }
        }
        else if (processor.sidechainConnected.load()) { g.setColour (Pal::red.withAlpha (0.9f)); g.strokePath (path (sKick, false), PathStrokeType (1.5f)); }
    }

    g.setFont (lnf.caps (9.5f, 2.0f));
    if (split)
    {
        for (int e = 0; e < 2; ++e)
        {
            const float hz = e == 0 ? lo : hi, ex = xOf (hz);
            if (e == 0 && hz <= 20.5f) continue;
            g.setColour (Pal::orange.withAlpha (e == hoverEdge || e == dragEdge ? 1.0f : 0.8f)); g.fillRect (ex - 0.75f, r.getY(), 1.5f, r.getHeight());
            const String t = (hz >= 1000.0f ? String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (hz)) + " Hz");
            g.setFont (lnf.chak (10.5f));
            const float w = (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), t) + 14.0f;
            const auto tab = Rectangle<float> (ex - w * 0.5f, r.getBottom() - 24.0f, w, 18.0f);
            g.setColour (Pal::orange); g.fillRoundedRectangle (tab, 3.0f);
            g.setColour (Colour (0xff160b06)); g.drawText (t, tab, Justification::centred, false);
        }
        g.setFont (lnf.caps (9.5f, 2.0f));
        g.setColour (Pal::orange); g.drawText ("DUCKED", Rectangle<float> (bx0 + 10.0f, r.getY() + 8.0f, 100.0f, 12.0f), Justification::centredLeft, false);
        g.setColour (Colour (0xff6c737d));
        const int abovePct = roundToInt (processor.apvts.getRawParameterValue ("above")->load());
        const String outside = abovePct == 0 ? "UNTOUCHED OUTSIDE THE BAND" : "OUTSIDE THE BAND: " + String (abovePct) + "% OF THE DUCK";
        if (bx1 + 240.0f < r.getRight()) g.drawText (outside, Rectangle<float> (bx1 + 10.0f, r.getY() + 8.0f, 300.0f, 12.0f), Justification::centredLeft, false);
    }
    else
    {
        g.setColour (Pal::orange); g.drawText ("FULL BAND  " + String::fromUTF8 ("\xc2\xb7") + "  DRAG AN EDGE OR PICK SPLIT TO DUCK ONLY PART OF IT", Rectangle<float> (r.getX() + 10.0f, r.getY() + 8.0f, 500.0f, 12.0f), Justification::centredLeft, false);
    }
    if (map)
    {
        g.setFont (lnf.caps (8.5f, 1.2f)); g.setColour (Colour (0xff6c737d));
        g.drawText (processor.sidechainConnected.load() ? String::fromUTF8 ("FILLS: WHAT EACH HIT TAKES OUT  \xc2\xb7  DASHED: THE DRUMS (SIDECHAIN)")
                                                        : String ("FILLS: WHAT EACH HIT TAKES OUT OF THE BASS"),
                    Rectangle<float> (r.getX(), r.getBottom() + 22.0f, 330.0f, 12.0f), Justification::centredLeft, false);
    }
    g.setFont (lnf.caps (10.0f, 0.0f)); g.setColour (Pal::dim);
    const float avg = processor.getEngine().avgDuckDb.load();
    g.drawText ((map ? String() : "Averaged over the last moment  " + String::fromUTF8 ("\xc2\xb7") + "  ") + "the band is " + (avg > -0.05f ? String ("untouched") : String ("down ") + String (-avg, 1) + " dB on average"),
                Rectangle<float> (r.getRight() - 420.0f, r.getBottom() + 22.0f, 420.0f, 12.0f), Justification::centredRight, false);
}

//==============================================================================
// Shape browser
ShapeBrowser::ShapeBrowser (RecoilProcessor& p, RecoilLookAndFeel& l) : processor (p), lnf (l)
{
    const char* names[3] = { "FACTORY", "RECOIL", "USER" };
    const char* tips[3] = { "FACTORY: 16 familiar ducking and gate shapes", "RECOIL: shapes made for sidechaining, only in RECOIL", "USER: shapes you saved from the editor. + saves the current one." };
    for (int i = 0; i < 3; ++i)
    {
        tabs[i].setButtonText (names[i]);
        tabs[i].setTooltip (tips[i]);
        tabs[i].getProperties().set ("seg", true);
        tabs[i].getProperties().set (i == 1 ? "brass" : "hot", true);
        tabs[i].getProperties().set ("size", 9.5f);
        tabs[i].onClick = [this, i] { setBank (i); };
        addAndMakeVisible (tabs[i]);
    }
    const char* slotNames[2] = { "KICK", "SNARE" };
    const char* slotTips[2] = { "KICK: the shape the kicks in the map use. Pick or edit it here.",
                                "SNARE: the shape the snares and rims in the map use. Usually shorter than the kick's." };
    for (int i = 0; i < 2; ++i)
    {
        slots[i].setButtonText (slotNames[i]);
        slots[i].setTooltip (slotTips[i]);
        slots[i].getProperties().set ("seg", true);
        slots[i].getProperties().set ("hot", true);
        slots[i].getProperties().set ("size", 9.5f);
        slots[i].onClick = [this, i] { processor.setShapeSlot (i); refreshSlots(); syncBankToShape(); if (onSlotChange) onSlotChange(); };
        addChildComponent (slots[i]);
    }
    const char* kindNames[5] = { "SUB", "SNARE", "PLUCK", "MID", "NOTE" };
    const char* kindTips[5] = { "SUB: the shape for kicks over a sustained sub or 808",
                                "SNARE: the shape for snares and rims",
                                "PLUCK: the shape for kicks over a short, plucked bass",
                                "MID: the shape for kicks over a mid bass (reese, growl)",
                                "NOTE: the shape for kicks where a new bass note starts with the kick" };
    for (int i = 0; i < 5; ++i)
    {
        kindSlots[i].setButtonText (kindNames[i]);
        kindSlots[i].setTooltip (kindTips[i]);
        kindSlots[i].getProperties().set ("seg", true);
        kindSlots[i].getProperties().set ("hot", true);
        kindSlots[i].getProperties().set ("size", 8.5f);
        kindSlots[i].onClick = [this, i] { processor.setShapeSlot (i); refreshSlots(); syncBankToShape(); if (onSlotChange) onSlotChange(); };
        addChildComponent (kindSlots[i]);
    }
    refreshSlots();
    syncBankToShape();
}

void ShapeBrowser::refreshSlots()
{
    const bool map = juce::roundToInt (processor.apvts.getRawParameterValue ("trigger")->load()) == 3;
    const bool bass = map && processor.hasBass(), perBass = processor.bassShapesActive();
    autoPanel = processor.bassAutoActive();
    const int s = processor.shapeSlot();
    for (int i = 0; i < 2; ++i) { slots[i].setVisible (map && ! bass); slots[i].setToggleState (i == s, juce::dontSendNotification); }
    for (int i = 0; i < 5; ++i) { kindSlots[i].setVisible (perBass); kindSlots[i].setToggleState (i == s, juce::dontSendNotification); }
    for (auto& t : tabs) t.setVisible (! autoPanel);
    repaint();
}

void ShapeBrowser::resized()
{
    slots[0].setBounds (110, 9, 52, 22);
    slots[1].setBounds (162, 9, 64, 22);
    const int kw[5] = { 38, 50, 46, 36, 44 };
    int kx = 18;
    for (int i = 0; i < 5; ++i) { kindSlots[i].setBounds (kx, 340, kw[i], 20); kx += kw[i]; }
    tabs[0].setBounds (18, 38, 70, 22);
    tabs[1].setBounds (88, 38, 62, 22);
    tabs[2].setBounds (150, 38, 50, 22);
}

void ShapeBrowser::setBank (int b)
{
    bank = juce::jlimit (0, 2, b);
    for (int i = 0; i < 3; ++i) tabs[i].setToggleState (i == bank, juce::dontSendNotification);
    if (bank == 2)
    {
        user = shapes::userNames();
        userNodes.clear();
        for (const auto& n : user) userNodes.push_back (shapes::loadUser (n));
    }
    repaint();
}

void ShapeBrowser::syncBankToShape()
{
    const int idx = processor.shapeIndex();
    setBank (idx < (int) shapes::factory().size() ? 0 : idx < shapes::numBuiltIn() ? 1
             : (shapes::userNames().contains (processor.currentShapeName()) ? 2 : bank));
}

juce::Rectangle<float> ShapeBrowser::tile (int i) const
{
    const float tw = 51.0f, th = 46.0f, gap = 6.0f;
    return { 16.0f + (float) (i % 4) * (tw + gap), 70.0f + (float) (i / 4) * (th + gap), tw, th };
}

int ShapeBrowser::tileAt (juce::Point<float> p) const
{
    for (int i = 0; i < 16; ++i) if (tile (i).contains (p)) return i;
    return -1;
}

void ShapeBrowser::mouseMove (const juce::MouseEvent& e) { const int h = autoPanel ? -1 : tileAt (e.position); if (h != hover) { hover = h; repaint(); } }

void ShapeBrowser::mouseDown (const juce::MouseEvent& e)
{
    if (autoPanel) return;
    const int t = tileAt (e.position);
    if (t < 0) return;
    if (bank == 0 && t < (int) shapes::factory().size()) processor.selectBuiltIn (t);
    else if (bank == 1 && t < (int) shapes::recoil().size()) processor.selectBuiltIn ((int) shapes::factory().size() + t);
    else if (bank == 2)
    {
        if (t < user.size())
        {
            const auto name = user[t];
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m;
                m.addItem (1, "Delete \"" + name + "\"");
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, name] (int r)
                {
                    if (r == 1 && shapes::removeUser (name)) setBank (2);
                });
                return;
            }
            const auto n = shapes::loadUser (name);
            if (! n.empty()) processor.setCustomNodes (n, name);
        }
        else if (t == user.size() && onSaveRequest) onSaveRequest();
    }
    repaint();
}

void ShapeBrowser::paint (juce::Graphics& g)
{
    using namespace juce;
    const bool mapSlots = slots[0].isVisible(), perBass = kindSlots[0].isVisible();
    if (autoPanel)
    {
        // AUTO: no shapes to pick. The two knobs sit on top (CLEARANCE, RECOVERY); here: their names, and the bass under the kicks
        auto knobText = [&] (float cx, float cy, float d, const char* label, const char* id)
        {
            g.setFont (lnf.caps (9.5f, 2.0f)); g.setColour (Colour (0xff8a919b));
            g.drawText (label, Rectangle<float> (cx - 60.0f, cy + d * 0.5f + 14.0f, 120.0f, 12.0f), Justification::centred, false);
            g.setFont (lnf.chak (13.5f)); g.setColour (Pal::text);
            g.drawText (processor.apvts.getParameter (id)->getCurrentValueAsText(), Rectangle<float> (cx - 60.0f, cy + d * 0.5f + 28.0f, 120.0f, 16.0f), Justification::centred, false);
        };
        g.setFont (lnf.caps (9.0f, 1.2f)); g.setColour (Pal::dim);
        g.drawText ("EVERY HIT GETS ITS OWN DUCK", Rectangle<float> (16.0f, 42.0f, 220.0f, 12.0f), Justification::centredLeft, false);
        knobText (66.0f, 122.0f, 56.0f, "CLEARANCE", "clearance");
        knobText (180.0f, 122.0f, 40.0f, "RECOVERY", "recovery");

        // the bass under the kicks
        const auto& bi = processor.getBassInfo();
        int total = 0; for (int k = 0; k < rcbass::kKinds; ++k) total += bi.counts[k];
        g.setFont (lnf.caps (9.0f, 1.6f)); g.setColour (Colour (0xff6c737d));
        g.drawText ("BASS UNDER THE KICKS", Rectangle<float> (16.0f, 206.0f, 220.0f, 12.0f), Justification::centredLeft, false);
        const int order[] = { rcbass::Sub, rcbass::Note, rcbass::Pluck, rcbass::Mid, rcbass::None };
        for (int r = 0; r < 5; ++r)
        {
            const int k = order[r];
            const float y = 224.0f + 17.0f * (float) r;
            g.setFont (lnf.caps (9.0f, 1.0f)); g.setColour (k == rcbass::None ? Pal::faint : Pal::silver);
            g.drawText (rcbass::kindName (k), Rectangle<float> (16.0f, y, 70.0f, 12.0f), Justification::centredLeft, false);
            const auto bar = Rectangle<float> (88.0f, y + 3.0f, 112.0f, 6.0f);
            g.setColour (Colour (0xff15171a)); g.fillRoundedRectangle (bar, 3.0f);
            if (total > 0) { g.setColour ((k == rcbass::None ? Pal::faint : Pal::brass).withAlpha (0.9f)); g.fillRoundedRectangle (bar.withWidth (jmax (3.0f, bar.getWidth() * (float) bi.counts[k] / (float) total)), 3.0f); }
            g.setFont (lnf.chak (10.5f)); g.setColour (Pal::dim);
            g.drawText (String (bi.counts[k]), Rectangle<float> (204.0f, y - 1.0f, 30.0f, 14.0f), Justification::centredRight, false);
        }
        g.setFont (lnf.caps (10.0f, 0.0f)); g.setColour (Pal::dim);
        g.drawFittedText ("Each kick and snare ducks just enough to stay CLEARANCE above the bass that is really playing. No bass: no duck.",
                          Rectangle<int> (16, 312, 220, 48), Justification::topLeft, 4, 1.0f);
        return;
    }
    if (mapSlots)
    {
        // well behind KICK / SNARE
        g.setColour (Colour (0xff0d0e10)); g.fillRoundedRectangle (Rectangle<float> (108.0f, 7.0f, 120.0f, 26.0f), 6.0f);
        g.setColour (Colour (0xff2c3036)); g.drawRoundedRectangle (Rectangle<float> (108.5f, 7.5f, 119.0f, 25.0f), 6.0f, 1.0f);
    }
    else if (! (processor.hasBass() && juce::roundToInt (processor.apvts.getRawParameterValue ("trigger")->load()) == 3))
    {
        // RECOIL legend dot
        g.setColour (Pal::brass); g.fillEllipse (138.0f, 20.0f, 5.0f, 5.0f);
        g.setFont (lnf.caps (9.0f, 1.0f)); g.setColour (Pal::dim);
        g.drawText ("RECOIL", Rectangle<float> (148.0f, 16.0f, 80.0f, 12.0f), Justification::centredLeft, false);
    }

    const int idx = processor.shapeIndex();
    const auto customName = processor.currentShapeName();
    for (int i = 0; i < 16; ++i)
    {
        const auto r = tile (i);
        const shapes::Entry* e = nullptr;
        std::vector<rc::Node> nodes;
        bool on = false, plus = false;
        if (bank == 0 && i < (int) shapes::factory().size()) { e = &shapes::factory()[(size_t) i]; nodes = e->nodes; on = idx == i; }
        else if (bank == 1 && i < (int) shapes::recoil().size()) { e = &shapes::recoil()[(size_t) i]; nodes = e->nodes; on = idx == (int) shapes::factory().size() + i; }
        else if (bank == 2 && i < user.size()) { nodes = userNodes[(size_t) i]; on = idx == shapes::customIndex() && customName == user[i]; }
        else if (bank == 2 && i == user.size()) plus = true;

        if (nodes.empty() && ! plus)
        {
            g.setColour (Colour (0xff2a2e35)); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            continue;
        }
        if (plus)
        {
            g.setColour (i == hover ? Pal::orange.withAlpha (0.8f) : Colour (0xff3a3f47)); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            g.setFont (lnf.chak (16.0f)); g.drawText ("+", r, Justification::centred, false);
            continue;
        }
        if (on)
        {
            g.setColour (Pal::orange.withAlpha (0.35f)); g.fillRoundedRectangle (r.expanded (2.0f), 6.0f);
            g.setGradientFill (ColourGradient (Colour (0xffff8a4c), 0, r.getY(), Colour (0xffff6a2e), 0, r.getBottom(), false));
            g.fillRoundedRectangle (r, 5.0f);
        }
        else
        {
            g.setColour (i == hover ? Colour (0xff2b2f35) : Colour (0xff22252a)); g.fillRoundedRectangle (r, 5.0f);
            g.setColour (Colour (0xff0a0b0d)); g.drawRoundedRectangle (r, 5.0f, 1.0f);
        }
        const auto pr = r.reduced (8.0f, 8.0f);
        g.setColour (on ? Colour (0xff1a0c05) : Colour (0xffa3a9b1));
        g.strokePath (shapePath (nodes, pr), PathStrokeType (on ? 2.4f : 1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // the selected shape's name and what it does
    g.setFont (lnf.chak (13.0f, true));
    g.setColour (idx >= (int) shapes::factory().size() && idx < shapes::numBuiltIn() ? Pal::brass : Colours::white);
    const char* kindNames[5] = { "SUB: ", "SNARE: ", "PLUCK: ", "MID: ", "NOTE: " };
    g.drawText ((perBass ? String (kindNames[processor.shapeSlot()]) : mapSlots ? String (processor.shapeSlot() == 1 ? "SNARE: " : "KICK: ") : String())
                + processor.currentShapeName().toUpperCase(), Rectangle<float> (16.0f, 282.0f, 220.0f, 18.0f), Justification::centredLeft, true);
    g.setFont (lnf.caps (10.0f, 0.0f)); g.setColour (Pal::dim);
    g.drawFittedText (processor.currentShapeDescription(), Rectangle<int> (16, 302, 220, perBass ? 32 : 56), Justification::topLeft, perBass ? 2 : 4, 1.0f);
    if (perBass)
    {
        g.setColour (Colour (0xff0d0e10)); g.fillRoundedRectangle (Rectangle<float> (16.0f, 338.0f, 218.0f, 24.0f), 6.0f);
        g.setColour (Colour (0xff2c3036)); g.drawRoundedRectangle (Rectangle<float> (16.5f, 338.5f, 217.0f, 23.0f), 6.0f, 1.0f);
    }
    if (bank == 2 && user.isEmpty())
    {
        g.setFont (lnf.caps (9.5f, 0.0f)); g.setColour (Pal::faint);
        g.drawText ("No saved shapes yet: edit one, then +", Rectangle<float> (16.0f, 126.0f, 220.0f, 14.0f), Justification::centredLeft, false);
    }
}

//==============================================================================
void ThresholdMeter::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), 6.0f);
    g.setColour (Colour (0xff0d0f11)); g.fillRoundedRectangle (r, 3.0f);
    const float lx = xOf (levelDb);
    if (lx > 1.0f)
    {
        g.setGradientFill (ColourGradient (Colour (0xff5a2a22), r.getX(), 0, Pal::orange, r.getRight(), 0, false));
        g.fillRoundedRectangle (r.withWidth (lx), 3.0f);
    }
    const float tx = xOf (processor.apvts.getRawParameterValue ("threshold")->load());
    g.setColour (Colours::white.withAlpha (0.3f)); g.fillRect (tx - 2.0f, 0.0f, 5.0f, (float) getHeight());
    g.setColour (Colours::white); g.fillRect (tx - 0.75f, 0.0f, 1.5f, (float) getHeight());
}

void ThresholdMeter::mouseDown (const juce::MouseEvent& e)
{
    if (auto* p = processor.apvts.getParameter ("threshold")) p->beginChangeGesture();
    mouseDrag (e);
}
void ThresholdMeter::mouseDrag (const juce::MouseEvent& e)
{
    const float db = juce::jlimit (-60.0f, 0.0f, e.position.x / (float) getWidth() * 60.0f - 60.0f);
    if (auto* p = processor.apvts.getParameter ("threshold")) p->setValueNotifyingHost (p->convertTo0to1 (db));
    repaint();
}
void ThresholdMeter::mouseUp (const juce::MouseEvent&) { if (auto* p = processor.apvts.getParameter ("threshold")) p->endChangeGesture(); }

//==============================================================================
float BandSlider::xOf (float hz) const { return 7.0f + std::log (hz / 20.0f) / std::log (800.0f) * ((float) getWidth() - 14.0f); }
float BandSlider::hzOf (float x) const { return 20.0f * std::pow (800.0f, juce::jlimit (0.0f, 1.0f, (x - 7.0f) / ((float) getWidth() - 14.0f))); }

void BandSlider::paint (juce::Graphics& g)
{
    using namespace juce;
    const bool split = processor.apvts.getRawParameterValue ("band")->load() > 0.5f;
    const float lo = processor.apvts.getRawParameterValue ("bandLo")->load(), hi = processor.apvts.getRawParameterValue ("bandHi")->load();
    const Rectangle<float> track (7.0f, 8.0f, (float) getWidth() - 14.0f, 6.0f);
    g.setColour (Colour (0xff0d0f11)); g.fillRoundedRectangle (track, 3.0f);
    const auto fill = Rectangle<float> (xOf (lo), track.getY(), xOf (hi) - xOf (lo), track.getHeight());
    if (split) g.setGradientFill (ColourGradient (Pal::red, fill.getX(), 0, Pal::orange, fill.getRight(), 0, false));
    else g.setColour (Colour (0xff3a3f47));
    g.fillRoundedRectangle (fill, 3.0f);
    for (float hz : { lo, hi })
    {
        const auto t = Rectangle<float> (14.0f, 22.0f).withCentre ({ xOf (hz), track.getCentreY() });
        g.setColour (Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (t.translated (0, 2), 3.0f);
        g.setGradientFill (ColourGradient (Colour (0xff5b626c), 0, t.getY(), Colour (0xff353a41), 0, t.getBottom(), false));
        g.fillRoundedRectangle (t, 3.0f);
        g.setColour (Colour (0xff0a0b0d)); g.drawRoundedRectangle (t, 3.0f, 1.0f);
        g.setColour (split ? Pal::orange : Pal::dim); g.fillRect (t.getCentreX() - 1.0f, t.getY() + 5.0f, 2.0f, t.getHeight() - 10.0f);
    }
    g.setFont (lnf.chak (9.5f)); g.setColour (Pal::dim);
    for (float f : { 20.0f, 100.0f, 1000.0f, 10000.0f })
        g.drawText (f >= 1000.0f ? String (roundToInt (f / 1000.0f)) + "k" : String (roundToInt (f)), Rectangle<float> (xOf (f) - 20.0f, 26.0f, 40.0f, 12.0f), Justification::centred, false);
}

void BandSlider::mouseDown (const juce::MouseEvent& e)
{
    const float lo = processor.apvts.getRawParameterValue ("bandLo")->load(), hi = processor.apvts.getRawParameterValue ("bandHi")->load();
    drag = std::abs (e.position.x - xOf (lo)) < std::abs (e.position.x - xOf (hi)) ? 0 : 1;
    if (std::abs (xOf (lo) - xOf (hi)) < 4.0f) drag = e.position.x > xOf (hi) ? 1 : 0;
    if (auto* b = processor.apvts.getParameter ("band"); b != nullptr && b->getValue() < 0.5f) { b->beginChangeGesture(); b->setValueNotifyingHost (1.0f); b->endChangeGesture(); }
    if (auto* p = processor.apvts.getParameter (drag == 0 ? "bandLo" : "bandHi")) p->beginChangeGesture();
    mouseDrag (e);
}

void BandSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    const float lo = processor.apvts.getRawParameterValue ("bandLo")->load(), hi = processor.apvts.getRawParameterValue ("bandHi")->load();
    float hz = hzOf (e.position.x);
    hz = drag == 0 ? juce::jlimit (20.0f, juce::jmin (1000.0f, hi / 1.25f), hz) : juce::jlimit (juce::jmax (40.0f, lo * 1.25f), 16000.0f, hz);
    if (auto* p = processor.apvts.getParameter (drag == 0 ? "bandLo" : "bandHi")) p->setValueNotifyingHost (p->convertTo0to1 (hz));
    repaint();
}

void BandSlider::mouseUp (const juce::MouseEvent&)
{
    if (drag >= 0) if (auto* p = processor.apvts.getParameter (drag == 0 ? "bandLo" : "bandHi")) p->endChangeGesture();
    drag = -1;
}

//==============================================================================
class PresetMenuItem : public juce::PopupMenu::CustomComponent
{
public:
    PresetMenuItem (const juce::String& n, PresetManager& pm, RecoilLookAndFeel& l)
        : CustomComponent (true), name (n), description (PresetManager::getDescription (n)), presets (pm), lnf (l) {}
    void getIdealSize (int& w, int& h) override { w = 380; h = 40; }
    void paint (juce::Graphics& g) override
    {
        const bool hot = isItemHighlighted(), current = presets.getCurrentName() == name;
        if (hot) { g.setColour (Pal::orange); g.fillRoundedRectangle (getLocalBounds().reduced (3, 1).toFloat(), 4.0f); }
        if (current) { g.setColour (hot ? juce::Colours::black : Pal::orange); g.fillRect (7, 9, 3, getHeight() - 18); }
        g.setColour (hot ? juce::Colours::black : (current ? Pal::orange : Pal::text));
        g.setFont (lnf.chak (13.0f, true, 0.3f));
        g.drawText (name, 16, 3, getWidth() - 22, 18, juce::Justification::centredLeft, true);
        g.setColour (hot ? juce::Colours::black.withAlpha (0.75f) : Pal::dim);
        g.setFont (lnf.caps (10.0f, 0.0f));
        g.drawText (description, 16, 21, getWidth() - 22, 14, juce::Justification::centredLeft, true);
    }
private:
    juce::String name, description;
    PresetManager& presets;
    RecoilLookAndFeel& lnf;
};

//==============================================================================
//==============================================================================
// MAP view
namespace
{
    const juce::Colour kTypeCol[4] = { Pal::red, Pal::orange, Pal::brass, Pal::silver };
    const char* kTypeName[4] = { "KICK", "SNARE", "RIM", "HAT" };
    juce::String mmss (double t, bool ms)
    {
        if (! ms) { const int s = juce::roundToInt (t); return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2); }
        const int m = (int) (t / 60.0); const double sec = t - m * 60.0;
        return juce::String (m) + ":" + (sec < 10 ? "0" : "") + juce::String (sec, 3);
    }
}

MapView::MapView (RecoilProcessor& p, RecoilLookAndFeel& l) : processor (p), lnf (l) {}

double MapView::songLen() const
{
    const auto& h = processor.getMapHits();
    return juce::jmax (processor.getMapInfo().seconds, h.empty() ? 1.0 : h.back().t + 1.0);
}

double MapView::autoDb (int index, int* frames) const
{
    // AUTO: the deepest point of this hit's duck in dB (0: no duck), and how many 5 ms frames it lasts
    const auto& clash = processor.getBassClash();
    if (frames) *frames = 0;
    if (index < 0 || (size_t) (index + 1) * rcbass::kFrames > clash.size()) return 0.0;
    float r[rcbass::kFrames];
    const int n = rcbass::autoCurve (&clash[(size_t) index * rcbass::kFrames], param ("clearance"), param ("recovery"), r);
    if (frames) *frames = n;
    float mx = 0.0f; for (int i = 0; i < n; ++i) mx = juce::jmax (mx, r[i]);
    return mx;
}

double MapView::duckFor (const rcmap::Hit& e, int index) const
{
    // the same rules as the engine's planMap
    if (! e.on) return 0.0;
    double bassScale = 1.0;
    if (processor.hasBass() && index >= 0 && (e.type == rcmap::Kick || e.type == rcmap::Snare))
    {
        if (processor.bassAutoActive())
        {
            const double db = autoDb (index, nullptr);
            if (db <= 0.0) return 0.0;
            bassScale = 1.0 - std::pow (10.0, -db / 20.0);
        }
        else if (processor.bassShapesActive())
        {
            if (e.type == rcmap::Kick && processor.getBassKind()[(size_t) index] == rcbass::None) return 0.0;
            if (e.type == rcmap::Snare)
            {
                const int8_t* c = &processor.getBassClash()[(size_t) index * rcbass::kFrames];
                bool any = false;
                for (int i = 0; i < rcbass::kFrames && ! any; ++i) any = c[i] != rcbass::kGone && c[i] > -40;
                if (! any) return 0.0;
            }
        }
    }
    const char* ids[] = { "mapKick", "mapSnare", "mapRim" };
    double amt = e.type < 3 ? param (ids[e.type]) * 0.01 : 0.0;
    const double ghostLin = std::pow (10.0, -param ("mapGhostDb") / 20.0);
    const double lvl = std::pow (10.0, juce::jmin (0.0f, e.rel) / 20.0);
    const int ghost = juce::roundToInt (param ("mapGhost"));
    if (lvl < ghostLin)
    {
        if (ghost == 0) return 0.0;
        if (ghost == 1) amt *= lvl / ghostLin * 0.5;
    }
    else amt *= 1.0 - param ("dynamic") * 0.01 * (1.0 - lvl);
    return juce::jlimit (0.0, 1.0, amt * param ("depth") * 0.01 * bassScale);
}

void MapView::update()
{
    const float now = processor.getEngine().mapNow.load();
    const bool playing = processor.hostPlaying.load() && now >= 0.0f;
    if (playing && juce::Time::getMillisecondCounterHiRes() - lastNav > 4000.0)
    {
        // follow: the playhead sits a quarter into the window
        viewStart = (double) now - viewLen * 0.25;
    }
    const int v = processor.mapVersion.load();
    if (playing || v != lastVersion || processor.mapProgress.load() >= 0.0f || now != lastNow) repaint();
    lastVersion = v; lastNow = now;
}

int MapView::hitAt (juce::Point<float> p) const
{
    const auto z = zoom();
    if (! z.expanded (0, 8).contains (p)) return -1;
    const auto& hs = processor.getMapHits();
    const double t0 = tOfX (p.x - 7.0f), t1 = tOfX (p.x + 7.0f);
    auto it = std::lower_bound (hs.begin(), hs.end(), t0, [] (const rcmap::Hit& a, double v) { return a.t < v; });
    int best = -1; float bestD = 1e9f;
    for (; it != hs.end() && it->t <= t1; ++it)
    {
        const float dy = std::abs (p.y - (rowY (it->type) - 26.0f));
        if (dy > 40.0f) continue;
        const float d = std::abs (xOfT (it->t) - p.x) + dy * 0.2f;
        if (d < bestD) { bestD = d; best = (int) (it - hs.begin()); }
    }
    return best;
}

void MapView::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto& hs = processor.getMapHits();
    const auto& info = processor.getMapInfo();
    const auto& eng = processor.getEngine();
    const float prog = processor.mapProgress.load();
    const auto ov = over(), z = zoom();

    // ---- top row: GHOSTS BELOW and DYNAMIC (their knobs sit on top), the kick check
    auto small = [&] (const String& label, const String& value, float x)
    {
        g.setFont (lnf.caps (10.0f, 0.3f)); g.setColour (Colour (0xff8a919b));
        g.drawText (label, Rectangle<float> (x, 12.0f, 200.0f, 14.0f), Justification::centredLeft, false);
        const float w = (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label);
        g.setFont (lnf.chak (11.5f)); g.setColour (Pal::text);
        g.drawText (value, Rectangle<float> (x + w + 6.0f, 11.0f, 100.0f, 16.0f), Justification::centredLeft, false);
    };
    small ("GHOSTS BELOW", minusSign() + String (roundToInt (param ("mapGhostDb"))) + " dB", 46.0f);
    small ("DYNAMIC", fixMinus (processor.apvts.getParameter ("dynamic")->getCurrentValueAsText()), 212.0f);
    {
        const float inStep = eng.mapInStep.load(), drift = eng.mapDriftMs.load();
        Colour c = Pal::faint; String st;
        if (! processor.sidechainConnected.load()) st = "NO KICK CHECK";
        else if (inStep < 0.0f) { st = processor.hostPlaying.load() ? "CHECKING" : "KICK CHECK"; c = Pal::dim; }
        else if (inStep < 0.6f) { st = "OUT OF STEP"; c = Pal::red; }
        else if (std::abs (drift) > 4.0f) { st = "SC " + String (std::abs (roundToInt (drift))) + (drift > 0 ? " MS LATE" : " MS EARLY"); c = Pal::amber; }
        else { st = "IN STEP"; c = Pal::lock; }
        g.setColour (c.withAlpha (0.35f)); g.fillEllipse (404.0f, 14.0f, 10.0f, 10.0f);
        g.setColour (c); g.fillEllipse (406.0f, 16.0f, 6.0f, 6.0f);
        g.setFont (lnf.caps (10.0f, 0.6f)); g.setColour (c == Pal::faint ? Pal::dim : c);
        g.drawText (st, Rectangle<float> (420.0f, 12.0f, 110.0f, 14.0f), Justification::centredLeft, false);
    }

    // ---- empty, analysing, error
    if (hs.empty() || prog >= 0.0f)
    {
        const auto box = Rectangle<float> (14.0f, 46.0f, (float) getWidth() - 28.0f, (float) getHeight() - 60.0f);
        g.setColour (dragOver ? Pal::orange.withAlpha (0.10f) : Colours::white.withAlpha (0.02f));
        g.fillRoundedRectangle (box, 8.0f);
        Path dash; PathStrokeType (1.2f).createDashedStroke (dash, [&] { Path q; q.addRoundedRectangle (box.reduced (1.0f), 8.0f); return q; }(), std::array<float, 2> { 6.0f, 5.0f }.data(), 2);
        g.setColour (dragOver ? Pal::orange : Colour (0xff3a3f47)); g.fillPath (dash);
        if (prog >= 0.0f)
        {
            g.setFont (lnf.caps (13.0f, 3.0f)); g.setColour (Pal::text);
            g.drawText ("ANALYSING THE DRUM BUS", box.withTrimmedBottom (box.getHeight() * 0.5f - 4.0f).withTrimmedTop (box.getHeight() * 0.5f - 40.0f), Justification::centred, false);
            const auto bar = Rectangle<float> (box.getCentreX() - 160.0f, box.getCentreY() + 4.0f, 320.0f, 6.0f);
            g.setColour (Colour (0xff15171a)); g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (Pal::orange); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * jlimit (0.02f, 1.0f, prog)), 3.0f);
            g.setFont (lnf.chak (12.0f)); g.setColour (Pal::dim);
            g.drawText (String (roundToInt (prog * 100.0f)) + "%", bar.translated (0, 12.0f).withHeight (16.0f), Justification::centred, false);
            return;
        }
        g.setFont (lnf.caps (15.0f, 3.5f)); g.setColour (dragOver ? Pal::orange : Pal::text);
        g.drawText (dragOver ? "DROP TO MAP IT" : "DROP YOUR DRUM BUS HERE", box.withHeight (30.0f).withCentre ({ box.getCentreX(), box.getCentreY() - 46.0f }), Justification::centred, false);
        g.setFont (lnf.chak (12.5f)); g.setColour (Pal::silver);
        const String lines[] = { "Bounce the whole song's drum bus from the very start (bar 1), then drop the file here or click LOAD.",
                                 "RECOIL finds every kick, snare, rim and hat, how loud each one is and how long it rings.",
                                 "While the song plays, the bass ducks exactly on each hit: no sidechain, no latency, no misses." };
        for (int i = 0; i < 3; ++i) g.drawText (lines[i], Rectangle<float> (box.getX(), box.getCentreY() - 6.0f + 20.0f * (float) i, box.getWidth(), 18.0f), Justification::centred, false);
        if (processor.mapError.isNotEmpty())
        {
            g.setColour (Pal::red); g.setFont (lnf.chak (12.5f, true));
            g.drawText (processor.mapError, Rectangle<float> (box.getX(), box.getCentreY() + 64.0f, box.getWidth(), 18.0f), Justification::centred, false);
        }
        g.setFont (lnf.caps (9.5f, 1.6f)); g.setColour (Pal::dim);
        g.drawText ("WAV  AIFF  FLAC  OGG  MP3", Rectangle<float> (box.getX(), box.getBottom() - 30.0f, box.getWidth(), 14.0f), Justification::centred, false);
        return;
    }

    const double len = songLen();
    const float now = eng.mapNow.load();
    viewLen = jlimit (1.0, jmax (1.0, len), viewLen);
    viewStart = jlimit (-viewLen * 0.25, jmax (0.0, len - viewLen * 0.75), viewStart);

    // ---- overview: the whole song
    g.setFont (lnf.caps (9.0f, 1.6f)); g.setColour (Colour (0xff6c737d));
    g.drawText ("SONG", Rectangle<float> (14.0f, ov.getY(), 46.0f, ov.getHeight()), Justification::centredLeft, false);
    g.setColour (Colour (0xff0f1114)); g.fillRoundedRectangle (ov, 3.0f);
    for (size_t hi = 0; hi < hs.size(); ++hi)
    {
        const auto& e = hs[hi];
        if (e.type == rcmap::Hat) continue;
        const float x = ov.getX() + (float) (e.t / len) * ov.getWidth();
        const float d = (float) duckFor (e, (int) hi);
        const float y0 = e.type == rcmap::Kick ? ov.getY() + 2.0f : ov.getCentreY() + 1.0f, h = ov.getHeight() * 0.5f - 3.0f;
        g.setColour (kTypeCol[e.type].withAlpha (! e.on ? 0.12f : 0.25f + 0.6f * d));
        g.fillRect (x, y0, 1.0f, h);
    }
    {
        const float x0 = ov.getX() + (float) (jmax (0.0, viewStart) / len) * ov.getWidth(), x1 = ov.getX() + (float) (jmin (len, viewStart + viewLen) / len) * ov.getWidth();
        g.setColour (Pal::brass.withAlpha (0.10f)); g.fillRect (Rectangle<float> (x0, ov.getY(), jmax (2.0f, x1 - x0), ov.getHeight()));
        g.setColour (Pal::brass.withAlpha (0.8f)); g.drawRect (Rectangle<float> (x0, ov.getY(), jmax (2.0f, x1 - x0), ov.getHeight()), 1.0f);
    }
    if (now >= 0.0f && now <= len) { g.setColour (Pal::text); g.fillRect (ov.getX() + (float) (now / len) * ov.getWidth() - 0.5f, ov.getY() - 2.0f, 1.5f, ov.getHeight() + 4.0f); }
    g.setFont (lnf.chak (10.0f)); g.setColour (Colour (0xff6c737d));
    g.drawText (mmss (len, false), Rectangle<float> (ov.getRight() + 4.0f, ov.getY(), 44.0f, ov.getHeight()), Justification::centredLeft, false);

    // ---- zoomed timeline: bars (from the host tempo) and rows
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (z.expanded (0, 14).toNearestInt());
        const double bpm = processor.hostBpm.load(), barSec = 4.0 * 60.0 / jmax (20.0, bpm), beatSec = barSec / 4.0;
        const bool beats = viewLen / beatSec < 64.0;
        const double step = beats ? beatSec : barSec * std::ceil (viewLen / barSec / 16.0);
        for (double t = std::floor (viewStart / step) * step; t < viewStart + viewLen; t += step)
        {
            if (t < 0) continue;
            const float x = xOfT (t);
            const double barF = t / barSec;
            const bool isBar = std::abs (barF - std::round (barF)) < 1e-6;
            g.setColour (isBar ? Colour (0xff23272d) : Colour (0xff15181c)); g.fillRect (x, z.getY(), 1.0f, z.getHeight());
            if (isBar)
            {
                g.setFont (lnf.chak (10.0f)); g.setColour (Colour (0xff6c737d));
                g.drawText (String (roundToInt (barF) + 1), Rectangle<float> (x + 3.0f, z.getY() - 13.0f, 40.0f, 12.0f), Justification::centredLeft, false);
            }
        }
    }
    const int nRows = rows();
    for (int r = 0; r < nRows; ++r)
    {
        const float y = rowY (r);
        g.setColour (Colour (0xff1c1f24)); g.fillRect (z.getX(), y, z.getWidth(), 1.0f);
        g.setFont (lnf.caps (9.0f, 1.6f)); g.setColour (r == 4 ? Pal::silver : r == 3 ? Pal::faint : kTypeCol[r].withAlpha (0.85f));
        g.drawText (r == 4 ? "BASS" : kTypeName[r], Rectangle<float> (14.0f, y - 14.0f, 48.0f, 12.0f), Justification::centredLeft, false);
    }
    if (nRows == 5)
    {
        // the bass: its level, and what kind of bass each kick lands on
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (z.toNearestInt());
        const auto& env = processor.getBassEnv();
        const float y = rowY (4), h = z.getHeight() / (float) nRows - 10.0f;
        Path p; p.startNewSubPath (z.getX(), y);
        const double hop = rcbass::kEnvHop;
        const int i0 = jmax (0, (int) std::floor (viewStart / hop)), i1 = jmin ((int) env.size() - 1, (int) std::ceil ((viewStart + viewLen) / hop));
        const int stride = jmax (1, (int) ((i1 - i0) / jmax (1.0f, z.getWidth())));
        for (int i = i0; i <= i1; i += stride)
        {
            const float lv = jlimit (0.0f, 1.0f, ((float) env[(size_t) i] - 100.0f + 60.0f) / 60.0f);   // -60..0 dB
            p.lineTo (xOfT (i * hop), y - h * lv);
        }
        p.lineTo (xOfT (i1 * hop), y); p.closeSubPath();
        g.setColour (Pal::silver.withAlpha (0.22f)); g.fillPath (p);
        g.setColour (Pal::silver.withAlpha (0.55f)); g.strokePath (p, PathStrokeType (1.0f));
        const auto& kind = processor.getBassKind();
        int kicks = 0;
        for (auto it = std::lower_bound (hs.begin(), hs.end(), viewStart, [] (const rcmap::Hit& a, double v) { return a.t < v; }); it != hs.end() && it->t <= viewStart + viewLen; ++it)
            kicks += it->type == rcmap::Kick ? 1 : 0;
        if (kicks > 0 && z.getWidth() / (float) kicks > 34.0f)
        {
            g.setFont (lnf.caps (8.0f, 1.0f));
            for (auto it = std::lower_bound (hs.begin(), hs.end(), viewStart - 0.2, [] (const rcmap::Hit& a, double v) { return a.t < v; }); it != hs.end() && it->t <= viewStart + viewLen; ++it)
            {
                if (it->type != rcmap::Kick) continue;
                const auto k = kind[(size_t) (it - hs.begin())];
                const float x = xOfT (it->t);
                g.setColour (k == rcbass::None ? Pal::faint : Pal::brass);
                g.fillRect (x - 0.5f, y - h - 2.0f, 1.0f, 4.0f);
                g.drawText (k == rcbass::None ? String ("-") : String (rcbass::kindName (k)), Rectangle<float> (x + 3.0f, y - h - 6.0f, 50.0f, 10.0f), Justification::centredLeft, false);
            }
        }
    }
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (z.toNearestInt());
        const float stemMax = z.getHeight() / (float) nRows - 12.0f;
        auto it = std::lower_bound (hs.begin(), hs.end(), viewStart - 0.05, [] (const rcmap::Hit& a, double v) { return a.t < v; });
        for (; it != hs.end() && it->t <= viewStart + viewLen + 0.05; ++it)
        {
            const auto& e = *it;
            const int idx = (int) (it - hs.begin());
            const float x = xOfT (e.t), y = rowY (e.type);
            const float lvl = std::pow (10.0f, jmin (0.0f, e.rel) / 20.0f);
            const float top = y - jmax (3.0f, stemMax * std::sqrt (lvl));           // sqrt: ghost notes stay visible
            const float d = (float) duckFor (e, idx);
            const Colour c = kTypeCol[e.type];
            const bool hov = idx == hover;
            if (! e.on)
            {
                g.setColour (Pal::faint.withAlpha (0.7f)); g.fillRect (x - 0.5f, top, 1.0f, y - top);
                g.drawLine (x - 3.5f, top - 3.5f, x + 3.5f, top + 3.5f, 1.4f); g.drawLine (x - 3.5f, top + 3.5f, x + 3.5f, top - 3.5f, 1.4f);
            }
            else
            {
                g.setColour (c.withAlpha (0.28f)); g.fillRect (x - 1.0f, top, 2.0f, y - top);
                if (d > 0.0f)
                {
                    const float h = (y - top) * d;
                    g.setColour (c); g.fillRect (x - 1.5f, y - h, 3.0f, h);
                }
                g.setColour (d > 0.0f ? c : c.withAlpha (0.5f));
                if (d > 0.0f) g.fillEllipse (x - 3.0f, top - 3.0f, 6.0f, 6.0f);
                else g.drawEllipse (x - 3.0f, top - 3.0f, 6.0f, 6.0f, 1.2f);       // heard but not ducked (a ghost note set to IGNORE, or 0 %)
            }
            if (hov) { g.setColour (Pal::text); g.drawEllipse (x - 6.0f, top - 6.0f, 12.0f, 12.0f, 1.3f); }
        }
        if (now >= 0.0f)
        {
            const float x = xOfT (now);
            g.setColour (Pal::text.withAlpha (0.15f)); g.fillRect (x - 3.0f, z.getY(), 6.0f, z.getHeight());
            g.setColour (Pal::text); g.fillRect (x - 0.5f, z.getY(), 1.5f, z.getHeight());
        }
    }

    // ---- bottom line: the hovered hit, or what the map holds
    g.setFont (lnf.chak (11.5f));
    const auto line = Rectangle<float> (14.0f, (float) getHeight() - 26.0f, (float) getWidth() - 28.0f, 16.0f);
    const String dot = String::fromUTF8 ("  \xc2\xb7  ");
    if (isPositiveAndBelow (hover, (int) hs.size()))
    {
        const auto& e = hs[(size_t) hover];
        const double d = duckFor (e, hover);
        const bool ghost = e.rel < -param ("mapGhostDb");
        String bassTxt;
        if (processor.hasBass() && e.type == rcmap::Kick) bassTxt = dot + "over " + String (rcbass::kindName (processor.getBassKind()[(size_t) hover])).toLowerCase();
        if (processor.bassAutoActive() && (e.type == rcmap::Kick || e.type == rcmap::Snare) && e.on)
        {
            int nf = 0; const double db = autoDb (hover, &nf);
            if (db > 0.0) bassTxt << dot << "auto " << fixMinus (String (-juce::roundToInt (db))) << " dB for " << String (nf * 5) << " ms";
        }
        String s = String (kTypeName[e.type]) + dot + mmss (e.t, true) + dot + fixMinus (String (e.rel, 1)) + " dB" + (ghost ? " (ghost)" : "")
                 + bassTxt + dot
                 + (! e.on ? String ("OFF: click to switch it back on") : d > 0.0 ? "ducks " + String (roundToInt (d * 100.0)) + "%" + dot + "click to switch it off" : String ("not ducked"));
        g.setColour (Pal::text); g.drawText (s, line, Justification::centredLeft, true);
    }
    else
    {
        int off = 0; for (auto& e : hs) off += e.on ? 0 : 1;
        String s = info.name.isNotEmpty() ? info.name + dot : String();
        s << mmss (len, false) << dot << info.stats.count[0] << " kicks" << dot << info.stats.count[1] << " snares (" << info.stats.ghosts << " ghosts)" << dot
          << info.stats.count[2] << " rims" << dot << info.stats.count[3] << " hats";
        if (off > 0) s << dot << off << " off";
        g.setColour (Pal::dim); g.drawText (s, line, Justification::centredLeft, true);
        g.setFont (lnf.caps (9.0f, 1.0f)); g.setColour (Pal::faint);
        g.drawText ("DRAG TO SCROLL  WHEEL TO ZOOM", line, Justification::centredRight, false);
    }
}

void MapView::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitAt (e.position);
    if (h != hover) { hover = h; repaint(); }
}

void MapView::mouseDown (const juce::MouseEvent& e)
{
    dragged = false; dragX0 = e.position.x; dragT0 = viewStart;
    if (processor.getMapHits().empty())
    {
        if (onLoadRequest && processor.mapProgress.load() < 0.0f) onLoadRequest();
        return;
    }
    draggingOverview = over().expanded (0, 6).contains (e.position);
    if (draggingOverview) mouseDrag (e);
}

void MapView::mouseDrag (const juce::MouseEvent& e)
{
    if (processor.getMapHits().empty()) return;
    lastNav = juce::Time::getMillisecondCounterHiRes();
    if (draggingOverview)
    {
        const auto ov = over();
        viewStart = (e.position.x - ov.getX()) / ov.getWidth() * songLen() - viewLen * 0.5;
        dragged = true;
    }
    else if (std::abs (e.position.x - dragX0) > 3.0f || dragged)
    {
        dragged = true;
        viewStart = dragT0 - (e.position.x - dragX0) / zoom().getWidth() * viewLen;
    }
    repaint();
}

void MapView::mouseUp (const juce::MouseEvent& e)
{
    if (! dragged && ! draggingOverview)
    {
        const int h = hitAt (e.position);
        if (h >= 0) processor.setMapHitOn (h, ! processor.getMapHits()[(size_t) h].on);
    }
    draggingOverview = false;
    repaint();
}

void MapView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (processor.getMapHits().empty()) return;
    lastNav = juce::Time::getMillisecondCounterHiRes();
    const double at = tOfX (e.position.x);
    const double f = std::exp (-(w.deltaY != 0.0f ? w.deltaY : w.deltaX) * 1.6);
    const double newLen = juce::jlimit (1.0, juce::jmax (1.0, songLen()), viewLen * f);
    viewStart = at - (at - viewStart) * newLen / viewLen;
    viewLen = newLen;
    repaint();
}

//==============================================================================
RecoilUI::RecoilUI (RecoilProcessor& p) : processor (p), wave (p, lnf), spec (p, lnf), mapView (p, lnf), browser (p, lnf), thresh (p), bandSlider (p, lnf)
{
    lnf.chakra = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchSemiBold_ttf, BinaryData::ChakraPetchSemiBold_ttfSize);
    lnf.chakraBold = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchBold_ttf, BinaryData::ChakraPetchBold_ttfSize);
    lnf.mont = juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratSemiBold_ttf, BinaryData::MontserratSemiBold_ttfSize);
    setLookAndFeel (&lnf);
    logo = juce::ImageCache::getFromMemory (BinaryData::recoil_logo_png, BinaryData::recoil_logo_pngSize);

    addAndMakeVisible (wave);
    addChildComponent (spec);
    addChildComponent (mapView);
    mapView.onLoadRequest = [this] { chooseMapFile (false); };
    mapView.setTooltip ("MAP: every hit RECOIL found in your drum bus. Top: the whole song (click to jump). Below: one row per kind of hit; "
                        "the stem is how loud the hit is, the bright part how deep it ducks. Click a hit to switch it off or on. Drag to scroll, wheel to zoom.");
    addAndMakeVisible (browser);
    addAndMakeVisible (thresh);
    addAndMakeVisible (bandSlider);
    wave.setTooltip ("Top lane: the kick's low end (red) against the bass after RECOIL (white). MATCH shows how well they line up while both sound. "
                     "Above it: the note of the kick (where it settles) and of the bass, and how far apart they are.\n"
                     "Main lane: the bass before (hatched: what RECOIL took out) and after (white), with the shape on top. "
                     "Drag the orange dB tab to set DEPTH and the brass tab to set LENGTH. Click to freeze the picture. EDIT lets you change the shape itself.");
    spec.setTooltip ("Spectrum of the bass before (grey) and after (white) RECOIL, and the kick (red), averaged over a moment. Drag the orange edges to set the band.");
    thresh.setTooltip ("INPUT: the sidechain level. Drag the white line to set the THRESHOLD a hit has to reach.");
    bandSlider.setTooltip ("BAND: drag the handles to pick the part of the bass that is ducked. Below and above it, ABOVE decides how much of the shape it gets.");
    browser.setTooltip ("SHAPES: click one to use it. Right-click one of your own shapes to delete it.");
    browser.onSaveRequest = [this] { showSaveShapeDialog(); };
    browser.onSlotChange = [this] { lastText.clear(); repaint(); };
    wave.onEdited = [this] { browser.repaint(); };

    // ---- view / edit
    const char* viewNames[3] = { "WAVE", "SPECTRUM", "MAP" };
    for (int i = 0; i < 3; ++i)
    {
        viewButtons[i].setButtonText (viewNames[i]);
        viewButtons[i].getProperties().set ("seg", true);
        viewButtons[i].getProperties().set ("size", 9.5f);
        viewButtons[i].onClick = [this, i] { setView (i); };
        addAndMakeVisible (viewButtons[i]);
    }
    viewButtons[0].setTooltip ("WAVE: what RECOIL does to the bass around each hit, with the shape on top");
    viewButtons[1].setTooltip ("SPECTRUM: which frequencies are ducked, with the band");
    viewButtons[2].setTooltip ("MAP: the drum bus you loaded, hit by hit. Click a hit to switch it off or on.");
    editButton.getProperties().set ("seg", true); editButton.getProperties().set ("brass", true); editButton.getProperties().set ("size", 9.5f);
    editButton.setTooltip ("EDIT: change the shape's points right on the display. Editing a factory shape makes it your own Custom shape.");
    editButton.onClick = [this] { setEditing (! wave.editing); };
    addAndMakeVisible (editButton);
    const char* toolNames[3] = { "POINT", "DRAW", "STEP" };
    const char* toolTips[3] = { "POINT: drag points, double-click to add or remove one, Alt-drag a segment to bend it, right-click a point for curve types",
                                "DRAW: drag across the display to draw the shape on the grid", "STEP: drag across to draw flat steps on the grid (gates)" };
    for (int i = 0; i < 3; ++i)
    {
        toolButtons[i].setButtonText (toolNames[i]);
        toolButtons[i].setTooltip (toolTips[i]);
        toolButtons[i].getProperties().set ("seg", true); toolButtons[i].getProperties().set ("brass", true); toolButtons[i].getProperties().set ("size", 9.5f);
        toolButtons[i].onClick = [this, i] { wave.tool = (WaveView::Tool) i; for (int k = 0; k < 3; ++k) toolButtons[k].setToggleState (k == i, juce::dontSendNotification); };
        addChildComponent (toolButtons[i]);
    }
    toolButtons[0].setToggleState (true, juce::dontSendNotification);
    snapButton.getProperties().set ("pill", true); snapButton.getProperties().set ("brass", true); snapButton.getProperties().set ("size", 9.5f);
    snapButton.setToggleState (true, juce::dontSendNotification);
    snapButton.setTooltip ("SNAP: points land on the grid");
    snapButton.onClick = [this] { wave.snap = ! wave.snap; snapButton.setToggleState (wave.snap, juce::dontSendNotification); };
    gridButton.getProperties().set ("size", 9.5f);
    gridButton.setTooltip ("GRID: how finely the cycle is divided for SNAP, DRAW and STEP. Click to change.");
    gridButton.onClick = [this]
    {
        const int divs[] = { 4, 8, 16, 32 };
        int k = 0; while (k < 3 && divs[k] != wave.gridDiv) ++k;
        wave.gridDiv = divs[(k + 1) % 4];
        gridButton.setButtonText ("GRID 1/" + juce::String (wave.gridDiv));
    };
    mirrorButton.setTooltip ("MIRROR: play the shape backwards");
    clearButton.setTooltip ("CLEAR: start again from a simple duck");
    saveShapeButton.setTooltip ("SAVE SHAPE: keep this shape in your USER bank");
    mirrorButton.onClick = [this] { wave.mirror(); };
    clearButton.onClick = [this] { wave.clear(); };
    saveShapeButton.onClick = [this] { showSaveShapeDialog(); };
    for (auto* b : { &snapButton, &gridButton, &mirrorButton, &clearButton, &saveShapeButton }) { b->getProperties().set ("size", 9.5f); addChildComponent (*b); }

    // ---- presets
    presetBox.setTextWhenNothingSelected ("Default");
    presetBox.setTooltip ("PRESETS: click to pick one; each has a short description. SAVE stores your own (a Custom shape included).");
    presetBox.onChange = [this]
    {
        const auto name = presetBox.getText();
        if (name.isNotEmpty() && name != processor.presets.getCurrentName()) { processor.presets.load (name); browser.syncBankToShape(); }
        deleteButton.setEnabled (! PresetManager::isFactory (presetBox.getText()));
    };
    addAndMakeVisible (presetBox);
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (+1); };
    saveButton.onClick = [this] { showSaveDialog(); };
    deleteButton.onClick = [this] { showDeleteDialog(); };
    prevButton.setTooltip ("Previous preset"); nextButton.setTooltip ("Next preset");
    saveButton.setTooltip ("Save the current settings as a preset"); deleteButton.setTooltip ("Delete this preset (your own presets only)");
    for (auto* b : { &prevButton, &nextButton, &saveButton, &deleteButton }) addAndMakeVisible (*b);
    refreshPresetList();

    // ---- help
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "RECOIL"; o.folderName = "RECOIL"; o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (o);
    }
    helpButton.setClickingTogglesState (true);
    helpButton.getProperties().set ("round", true);
    helpButton.onClick = [this] { setHelp (helpButton.getToggleState()); };
    addAndMakeVisible (helpButton);
    setHelp (settings->getBoolValue ("showHelp", false));

    // ---- knobs
    struct KDef { Knob* k; const char* id; const char* label; juce::Point<int> c; int d; int mode; const char* flags; double reset; const char* tip; };
    const KDef kdefs[] = {
        { &depth, "depth", "DEPTH", { 89, 595 }, 86, 0, "", 100.0, "DEPTH: how far the bass goes down at the bottom of the shape. All the way right is silence." },
        { &length, "length", "LENGTH", { 204, 590 }, 56, 0, "brass", 250.0, "LENGTH: how long one cycle of the shape lasts after each hit (AUDIO and MIDI). In SYNC the RATE sets it. FIT LENGTH sets it from the kick." },
        { &curve, "curve", "CURVE", { 294, 590 }, 56, 0, "bipolar", 0.0, "CURVE: bends the whole shape. Right comes back sooner, left stays down longer." },
        { &attack, "attack", "ATTACK", { 372, 584 }, 44, 0, "", 4.0, "ATTACK: how long the bass takes to go down. In SYNC and MAP the drop starts this much early, so it is down on the hit. 4 ms is click-free on any bass; under 2 ms can click." },
        { &inside, "inside", "INSIDE", { 482, 642 }, 40, 1, "", 100.0, "INSIDE: how much of the shape the band gets." },
        { &above, "above", "ABOVE", { 604, 642 }, 40, 1, "brass", 0.0, "ABOVE: how much of the shape the rest gets (outside the band). 0% leaves it untouched." },
        { &rotate, "rotate", "ROTATE", { 760, 612 }, 56, 0, "bipolar brass", 0.0, "ROTATE: turns the bass's phase (MANUAL). 0 is the original; in AUTO it shows the turn RECOIL picked." },
        { &mix, "mix", "MIX", { 947, 583 }, 34, 0, "", 100.0, "MIX: how much of the ducking you hear. 50% ducks half as deep." },
        { &gain, "gain", "GAIN", { 1015, 583 }, 34, 0, "bipolar", 0.0, "GAIN: output level. AVG DUCK shows how much level the ducking takes on average, if you want to make it up." },
        { &dynamic, "dynamic", "", { 662, 81 }, 18, 2, "mini brass", 50.0, "DYNAMIC: softer hits duck less. AUDIO compares each hit with the recent loudest; MIDI uses velocity. 0% ducks every hit fully." },
        { &offset, "offset", "", { 608, 81 }, 18, 2, "mini bipolar", 0.0, "OFFSET: moves the cycle earlier or later, as a share of the cycle." },
        { &swing, "swing", "", { 716, 81 }, 18, 2, "mini", 0.0, "SWING: starts every second cycle later." },
        { &mapKick, "mapKick", "", { 438, 81 }, 18, 2, "mini", 100.0, "KICK: how deep the kicks in the map duck" },
        { &mapSnare, "mapSnare", "", { 540, 81 }, 18, 2, "mini", 50.0, "SNARE: how deep the snares and claps in the map duck" },
        { &mapRim, "mapRim", "", { 646, 81 }, 18, 2, "mini", 0.0, "RIM: how deep rimshots and sticks duck (0%: not at all)" },
        { &mapOffset, "mapOffset", "", { 934, 81 }, 18, 2, "mini bipolar brass", 0.0, "OFFSET: moves every hit of the map earlier or later, in ms. ALIGN in the MAP view sets it from the kicks on the sidechain." },
        { &mapGhostDb, "mapGhostDb", "", { 46, 127 }, 18, 2, "mini brass", 12.0, "GHOSTS BELOW: a hit this much quieter than the main hits of its kind counts as a ghost note" },
        { &clearance, "clearance", "", { 900, 230 }, 56, 3, "", 6.0, "CLEARANCE: how far (dB) each kick and snare should stand above the bass. More clearance: deeper, longer ducks." },
        { &recovery, "recovery", "", { 1014, 230 }, 40, 3, "", 60.0, "RECOVERY: the fastest the bass may come back up (time for 20 dB). Longer: smoother, less pumping." },
        { &rmCeiling, "rmCeiling", "", { 212, 81 }, 18, 2, "mini", -3.0, "CEILING: kick + bass may not go above this (dBFS). On every sample the bass gets only the room the kick leaves under it. Lower: more squeeze." },
        { &rmRange, "rmRange", "", { 342, 81 }, 18, 2, "mini", 24.0, "RANGE: the furthest the bass may go down (dB)" },
        { &rmHold, "rmHold", "", { 456, 81 }, 18, 2, "mini brass", 0.0, "HOLD: how long the bass stays down before it comes back. HOLD and SMOOTH at 0: the bass follows the kick's waveform sample by sample, the grittiest" },
        { &rmRelease, "rmRelease", "", { 568, 81 }, 18, 2, "mini brass", 3.0, "SMOOTH: how fast the bass comes back after each squeeze. Longer: smoother, less grit, a little less level" },
    };
    for (const auto& d : kdefs)
    {
        auto& k = *d.k;
        k.id = d.id; k.label = d.label; k.centre = d.c; k.d = d.d; k.labelMode = d.mode;
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setRotaryParameters (kStart, kEnd, true);
        k.slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        k.slider.setMouseDragSensitivity (d.d > 80 ? 320 : 220);
        k.slider.getProperties().set ("d", d.d);
        for (auto& f : juce::StringArray::fromTokens (d.flags, " ", "")) if (f.isNotEmpty()) k.slider.getProperties().set (f == "bipolar" ? juce::Identifier ("bipolar") : juce::Identifier (f), true);
        k.slider.setTooltip (d.tip);
        k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, d.id, k.slider);
        k.slider.setDoubleClickReturnValue (true, d.reset);
        addAndMakeVisible (k.slider);
    }

    // ---- segmented switches and toggles tied to parameters
    auto segRow = [this, &p] (juce::TextButton* buttons, int count, const char* id, std::initializer_list<const char*> labels,
                              std::initializer_list<const char*> tips, const char* accent, float size = 10.5f)
    {
        auto a = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (id), [this, buttons, count] (float v)
        {
            for (int i = 0; i < count; ++i) buttons[i].setToggleState (i == juce::roundToInt (v), juce::dontSendNotification);
            layoutTrigger();
            repaint();
        });
        int i = 0; for (auto* l : labels) buttons[i++].setButtonText (juce::String::fromUTF8 (l));
        i = 0; for (auto* t : tips) buttons[i++].setTooltip (juce::String::fromUTF8 (t));
        auto* raw = a.get();
        for (int b = 0; b < count; ++b)
        {
            buttons[b].getProperties().set ("seg", true);
            buttons[b].getProperties().set ("size", size);
            if (accent != nullptr) buttons[b].getProperties().set (accent, true);
            buttons[b].onClick = [raw, b] { raw->setValueAsCompleteGesture ((float) b); };
            addAndMakeVisible (buttons[b]);
        }
        attachments.push_back (std::move (a));
    };
    segRow (triggerButtons, 5, "trigger", { "SYNC", "MIDI", "AUDIO", "MAP", "RM" },
            { "SYNC: the shape runs in time with the song, no sidechain needed",
              "MIDI: every MIDI note into RECOIL starts the shape. Route the kick's MIDI pattern to RECOIL.",
              "AUDIO: every kick on RECOIL's sidechain input starts the shape",
              "MAP: load the song's drum bus (bounced from bar 1); every kick and snare in it starts the shape exactly on time, with its own depth",
              "RM: \"ring mod\" sidechain. No shape: on every sample the bass gets only the room the kick on the sidechain leaves under CEILING. Louder, no pumping, a little grit" }, "hot");
    segRow (ghostButtons, 3, "mapGhost", { "IGNORE", "SCALE", "FULL" },
            { "Ghost notes don't duck the bass", "Ghost notes duck a little, softer ones less", "Ghost notes duck like the main hits" }, "hot", 9.0f);
    for (int i = 0; i < 5; ++i)
    {
        auto plain = triggerButtons[i].onClick;
        triggerButtons[i].onClick = [this, i, plain] { if (! switchToModeDefault (i)) plain(); };
    }
    {
        const char* names[4] = { "KICK", "SNARE", "RIM", "GHOST" };
        const char* tips[4] = { "The display shows each kick", "The display shows each snare", "The display shows each rim",
                                "The display shows each ghost note (when GHOSTS isn't IGNORE)" };
        for (int i = 0; i < 4; ++i)
        {
            auto& b = waveHitButtons[i];
            b.setButtonText (names[i]); b.setTooltip (juce::String ("MAP: ") + tips[i] + ", from its start, with the duck it got.");
            b.getProperties().set ("seg", true); b.getProperties().set ("hot", true); b.getProperties().set ("size", 9.0f);
            b.onClick = [this, i]
            {
                processor.apvts.state.setProperty ("waveHit", i, nullptr);
                for (int k = 0; k < 4; ++k) waveHitButtons[k].setToggleState (k == i, juce::dontSendNotification);
            };
            b.setToggleState (i == (int) processor.apvts.state.getProperty ("waveHit", 0), juce::dontSendNotification);
            addChildComponent (b);
        }
    }
    segRow (bassModeButtons, 2, "bassMode", { "AUTO", "PER BASS" },
            { "AUTO: every kick and snare gets its own duck, worked out from how loud the bass is against it (CLEARANCE)",
              "PER BASS: pick a shape for each kind of bass under the kicks: SUB, PLUCK, MID, NOTE (and SNARE)" }, "hot", 9.0f);
    loadButton.getProperties().set ("size", 8.5f);
    loadButton.setTooltip ("DRUMS: pick the drum bus bounce (WAV, AIFF, FLAC, OGG or MP3). You can also drop the file anywhere on RECOIL.");
    loadButton.onClick = [this] { if (processor.getMapHits().empty()) chooseMapFile (false); else showMapMenu (false); };
    addAndMakeVisible (loadButton);
    bassButton.getProperties().set ("size", 8.5f);
    bassButton.setTooltip ("BASS: the bass bus bounced from bar 1, with RECOIL bypassed (optional). Then every duck can follow the bass that is really playing.");
    bassButton.onClick = [this] { if (! processor.hasBass()) chooseMapFile (true); else showMapMenu (true); };
    addAndMakeVisible (bassButton);
    alignButton.getProperties().set ("size", 9.5f);
    alignButton.setTooltip ("ALIGN: set OFFSET so the map lines up with the kicks on the sidechain");
    alignButton.onClick = [this]
    {
        if (auto* prm = processor.apvts.getParameter ("mapOffset"))
        {
            const float v = juce::jlimit (-50.0f, 50.0f, processor.apvts.getRawParameterValue ("mapOffset")->load() + processor.getEngine().mapDriftMs.load());
            prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture();
        }
    };
    addChildComponent (alignButton);
    segRow (rateButtons, 6, "rate", { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" },
            { "One cycle per bar", "One cycle per half bar", "One cycle per beat", "Two cycles per beat", "Four cycles per beat", "Eight cycles per beat" }, "hot", 9.5f);
    segRow (rateModeButtons, 3, "rateMode", { "STRAIGHT", "DOT", "TRIP" }, { "Straight note values", "Dotted: 1.5 times as long", "Triplets: two thirds as long" }, nullptr, 9.5f);
    segRow (listenButtons, 2, "listen", { "KICK LOWS", "FULL" },
            { "KICK LOWS: listens to the trigger below 150 Hz, so hats and snares on the same bus don't fire it",
              "FULL: listens to the whole trigger (for snares, vocals or anything else)" }, "hot", 9.5f);
    segRow (bandButtons, 2, "band", { "FULL", "SPLIT" },
            { "FULL: the whole bass is ducked; when nothing is ducked, the sound is untouched bit for bit",
              "SPLIT: only the band is ducked (INSIDE); ABOVE sets how much the rest gets" }, "hot", 9.5f);
    segRow (edgeButtons, 2, "edge", { "SOFT", "HARD" }, { "SOFT: the band's edges fade over about an octave, with the least phase shift", "HARD: steeper edges (24 dB/oct)" }, nullptr, 9.0f);
    segRow (phaseButtons, 3, "phase", { "AUTO", "MANUAL", "OFF" },
            { "AUTO: RECOIL turns the bass's phase so its low end lines up with the kick where they overlap. It learns over a few hits, then locks. It only moves while the bass is ducked, so you never hear it move.",
              "MANUAL: turn the bass's phase yourself with ROTATE",
              "OFF: the bass's phase is left exactly as it is" }, "hot", 9.5f);

    auto toggle = [this, &p] (juce::TextButton& b, const char* id, const char* tip, bool brassy)
    {
        b.getProperties().set ("pill", true);
        b.getProperties().set ("size", 9.5f);
        if (brassy) b.getProperties().set ("brass", true);
        b.setTooltip (juce::String::fromUTF8 (tip));
        auto a = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (id), [this, &b] (float v) { b.setToggleState (v > 0.5f, juce::dontSendNotification); repaint(); });
        auto* raw = a.get();
        b.onClick = [raw, &b] { raw->setValueAsCompleteGesture (b.getToggleState() ? 0.0f : 1.0f); };
        addAndMakeVisible (b);
        attachments.push_back (std::move (a));
    };
    toggle (fitMapButton, "fit", "FIT LENGTH: each hit gets its own LENGTH, from how long it rings in the drum bus", false);
    toggle (fitButton, "fit", "FIT LENGTH: measures how long each kick's low end rings and sets LENGTH so the shape comes back up as the kick ends", false);
    toggle (grooveButton, "groove", "GROOVE LOCK: while the song plays, RECOIL learns where the kicks fall and starts each duck a few ms early, so the bass is already down when the kick lands. Still 0 ms latency.", false);
    toggle (autoOffsetButton, "autoOffset", "AUTO OFFSET: listens to the sidechain and moves the cycle onto your actual kicks", true);
    toggle (polarityButton, "polarity", "POLARITY: flips the bass upside down (the same as 180 degrees)", true);
    polarityButton.getProperties().remove ("pill");
    polarityButton.getProperties().set ("round", true);
    relearnButton.getProperties().set ("size", 9.5f);
    relearnButton.setTooltip ("RELEARN: forget the phase AUTO picked and listen again (after you change the bass or the kick)");
    relearnButton.onClick = [this] { processor.relearnPhase(); };
    addAndMakeVisible (relearnButton);

    // MIDI note filter
    noteBox.addItem ("Any note", 1);
    for (int n = 0; n < 128; ++n) noteBox.addItem (juce::MidiMessage::getMidiNoteName (n, true, true, 3), n + 2);
    noteBox.setTooltip ("NOTE: which MIDI note starts the shape. Any note, or just one (so one MIDI track can drive several instances).");
    noteAttach = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter ("midiNote"), [this] (float v) { noteBox.setSelectedId (juce::roundToInt (v) + 2, juce::dontSendNotification); });
    noteBox.onChange = [this] { noteAttach->setValueAsCompleteGesture ((float) (noteBox.getSelectedId() - 2)); };
    addAndMakeVisible (noteBox);
    noteAttach->sendInitialUpdate();
    for (auto& a : attachments) a->sendInitialUpdate();

    setView ((int) processor.apvts.state.getProperty ("view", 0));
    setSize (kW, kH);
    layoutTrigger();
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

RecoilUI::~RecoilUI()
{
    stopTimer();
    dialog.reset();
    tooltips.reset();
    setLookAndFeel (nullptr);
}

juce::String RecoilUI::text (const char* id) const { return fixMinus (processor.apvts.getParameter (id)->getCurrentValueAsText()); }

void RecoilUI::setView (int v)
{
    v = juce::jlimit (0, 2, v);
    processor.apvts.state.setProperty ("view", v, nullptr);
    for (int i = 0; i < 3; ++i) viewButtons[i].setToggleState (i == v, juce::dontSendNotification);
    wave.setVisible (v == 0);
    spec.setVisible (v == 1);
    mapView.setVisible (v == 2);
    editButton.setVisible (v == 0);
    if (v != 0) setEditing (false);
    layoutTrigger();
    repaint();
}

//==============================================================================
// MAP: load / drop / menu
bool RecoilUI::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (RecoilProcessor::isAudioFile (f)) return true;
    return false;
}

void RecoilUI::filesDropped (const juce::StringArray& files, int, int)
{
    mapView.dragOver = false;
    juce::Array<juce::File> audio;
    for (auto& f : files) if (RecoilProcessor::isAudioFile (f)) audio.add (juce::File (f));
    if (audio.isEmpty()) return;
    auto isBass = [] (const juce::File& f) { return f.getFileNameWithoutExtension().toLowerCase().contains ("bass"); };
    if (audio.size() >= 2)
    {
        // two files: the one with "bass" in its name is the bass bus
        int b = isBass (audio[0]) ? 0 : isBass (audio[1]) ? 1 : 1;
        processor.loadMapAndBass (audio[1 - b], audio[b]);
        return finishLoad();
    }
    const auto f = audio[0];
    if (processor.getMapHits().empty()) { loadMap (f); return; }
    // a map is loaded: is this the drums or the bass?
    juce::PopupMenu m;
    m.setLookAndFeel (&lnf);
    m.addSectionHeader (f.getFileName());
    m.addItem (2, "Use as the bass bus", true, isBass (f));
    m.addItem (1, "Use as the drum bus (a new map)");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&mapView), [this, f] (int r)
    {
        if (r == 1) loadMap (f);
        else if (r == 2) loadBass (f);
    });
}

bool RecoilUI::switchToModeDefault (int mode)
{
    // on one mode's default preset, picking another mode loads that mode's default instead of only switching
    static const char* defaults[5] = { "Pump 1/4", "MIDI Duck", "Default", "Drum Bus Map", "RM Squeeze" };
    const auto cur = processor.presets.getCurrentName();
    bool onDefault = false;
    for (auto* d : defaults) onDefault = onDefault || cur == d;
    if (! onDefault || cur == defaults[juce::jlimit (0, 4, mode)]) return false;
    processor.presets.load (defaults[juce::jlimit (0, 4, mode)]);
    refreshPresetList();
    browser.syncBankToShape();
    return true;
}

void RecoilUI::finishLoad()
{
    if (switchToModeDefault (3)) { setView (2); return; }
    if (auto* prm = processor.apvts.getParameter ("trigger"))
    {
        // switching to MAP: open the band up to 500 Hz (if it is still at the kick-only default) so snares duck too
        const bool wasMap = juce::roundToInt (processor.apvts.getRawParameterValue ("trigger")->load()) == 3;
        if (! wasMap)
            if (auto* hi = processor.apvts.getParameter ("bandHi"))
                if (std::abs (processor.apvts.getRawParameterValue ("bandHi")->load() - 180.0f) < 0.5f)
                { hi->beginChangeGesture(); hi->setValueNotifyingHost (hi->convertTo0to1 (500.0f)); hi->endChangeGesture(); }
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (3.0f)); prm->endChangeGesture();
    }
    setView (2);
}

void RecoilUI::loadBass (const juce::File& f)
{
    processor.loadBassFile (f);
    finishLoad();
}

void RecoilUI::loadMap (const juce::File& f)
{
    processor.loadMapFile (f);
    finishLoad();
}

void RecoilUI::chooseMapFile (bool bass)
{
    chooser = std::make_unique<juce::FileChooser> (bass ? "Load the bass bus (bounced from bar 1, with RECOIL bypassed)" : "Load the drum bus (bounced from the start of the song)",
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, bass] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        if (bass) loadBass (f); else loadMap (f);
    });
}

void RecoilUI::showMapMenu (bool bass)
{
    juce::PopupMenu m;
    m.setLookAndFeel (&lnf);
    if (bass)
    {
        m.addItem (1, "Load a new bass bounce...");
        m.addSeparator();
        m.addItem (3, "Remove the bass");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bassButton), [this] (int r)
        {
            if (r == 1) chooseMapFile (true);
            else if (r == 3) processor.clearBass();
        });
        return;
    }
    int off = 0; for (auto& e : processor.getMapHits()) off += e.on ? 0 : 1;
    m.addItem (1, "Load a new drum bounce...");
    m.addItem (2, "Switch every hit back on", off > 0);
    m.addSeparator();
    m.addItem (3, "Clear the map");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadButton), [this] (int r)
    {
        if (r == 1) chooseMapFile (false);
        else if (r == 2) { const auto& h = processor.getMapHits(); for (int i = 0; i < (int) h.size(); ++i) if (! h[(size_t) i].on) processor.setMapHitOn (i, true); }
        else if (r == 3) processor.clearMap();
    });
}

void RecoilUI::setEditing (bool on)
{
    wave.editing = on;
    editButton.setToggleState (on, juce::dontSendNotification);
    for (auto& b : toolButtons) b.setVisible (on);
    for (auto* b : { &snapButton, &gridButton, &mirrorButton, &clearButton, &saveShapeButton }) b->setVisible (on);
    wave.repaint();
    repaint();
}

void RecoilUI::setHelp (bool on)
{
    helpButton.setToggleState (on, juce::dontSendNotification);
    tooltips = on ? std::make_unique<juce::TooltipWindow> (this, 500) : nullptr;
    helpButton.setTooltip ("Help is on: hover over any control to see what it does. Click to turn the pop-ups off.");
    settings->setValue ("showHelp", on);
    settings->saveIfNeeded();
}

void RecoilUI::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& n : PresetManager::getFactoryNames()) presetBox.addItem (n, id++);
    const auto user = PresetManager::getUserNames();
    if (! user.isEmpty())
    {
        presetBox.addSeparator();
        for (const auto& n : user) presetBox.addItem (n, id++);
    }
    for (juce::PopupMenu::MenuItemIterator it (*presetBox.getRootMenu()); it.next();)
    {
        auto& item = it.getItem();
        if (item.itemID > 0) item.customComponent = new PresetMenuItem (item.text, processor.presets, lnf);
    }
    const auto current = processor.presets.getCurrentName();
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (presetBox.getItemText (i) == current) presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
    deleteButton.setEnabled (! PresetManager::isFactory (current));
}

void RecoilUI::stepPreset (int delta)
{
    const int n = presetBox.getNumItems();
    if (n == 0) return;
    const int current = presetBox.getSelectedItemIndex();
    presetBox.setSelectedItemIndex (current < 0 ? 0 : (current + delta + n) % n, juce::sendNotificationSync);
}

void RecoilUI::showSaveDialog()
{
    const auto current = processor.presets.getCurrentName();
    const bool own = ! PresetManager::isFactory (current);
    dialog = std::make_unique<juce::AlertWindow> ("Save preset", juce::String(), juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel (&lnf);
    dialog->addTextEditor ("name", own ? current : juce::String ("My Preset"), "Name");
    dialog->addTextEditor ("description", own ? PresetManager::getUserDescription (current) : juce::String(), "Description (optional, shown in the preset list)");
    if (auto* d = dialog->getTextEditor ("description")) d->setInputRestrictions (PresetManager::kMaxDescription);
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (dialog == nullptr) return;
        const auto name = dialog->getTextEditorContents ("name");
        const auto description = dialog->getTextEditorContents ("description");
        dialog->setVisible (false);
        if (result == 1 && processor.presets.save (name, description).isNotEmpty()) refreshPresetList();
    }), false);
    if (auto* ed = dialog->getTextEditor ("name")) { ed->grabKeyboardFocus(); ed->selectAll(); }
}

void RecoilUI::showDeleteDialog()
{
    const auto name = presetBox.getText();
    if (name.isEmpty() || PresetManager::isFactory (name)) return;
    dialog = std::make_unique<juce::AlertWindow> ("Delete preset", "Delete \"" + name + "\"?", juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel (&lnf);
    dialog->addButton ("Delete", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, name] (int result)
    {
        if (dialog == nullptr) return;
        dialog->setVisible (false);
        if (result == 1 && processor.presets.remove (name)) refreshPresetList();
    }), false);
}

void RecoilUI::showSaveShapeDialog()
{
    dialog = std::make_unique<juce::AlertWindow> ("Save shape", "It will be in your USER bank.", juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel (&lnf);
    const auto cur = processor.currentShapeName();
    dialog->addTextEditor ("name", cur.replace (" (edited)", "") == cur ? cur : cur.replace (" (edited)", " 2"), "Name");
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (dialog == nullptr) return;
        const auto name = juce::File::createLegalFileName (dialog->getTextEditorContents ("name").trim());
        dialog->setVisible (false);
        if (result != 1 || name.isEmpty()) return;
        const auto nodes = processor.currentNodes();
        if (shapes::saveUser (name, nodes)) { processor.setCustomNodes (nodes, name); browser.setBank (2); }
    }), false);
    if (auto* ed = dialog->getTextEditor ("name")) { ed->grabKeyboardFocus(); ed->selectAll(); }
}

// The trigger strip only shows what the current trigger mode uses.
void RecoilUI::layoutTrigger()
{
    const int t = trig();
    const bool audio = t == 2, sync = t == 0, midi = t == 1, map = t == 3, rm = t == 4;
    const bool mapShown = mapView.isVisible();
    { const bool was = browser.getProperties()["map"]; if (was != map) { browser.getProperties().set ("map", map); browser.refreshSlots(); browser.syncBankToShape(); } }
    {
        const bool phaseOk = t >= 2;                                   // SYNC and MIDI have no kick to line up with
        for (auto& b : phaseButtons) { b.setEnabled (phaseOk); b.setAlpha (phaseOk ? 1.0f : 0.35f); }
        polarityButton.setAlpha (phaseOk ? 1.0f : 0.35f);
        relearnButton.setAlpha (phaseOk ? 1.0f : 0.35f);
    }
    for (int i = 0; i < 4; ++i)
    {
        waveHitButtons[i].setVisible (map && wave.isVisible());
        waveHitButtons[i].setToggleState (i == (int) processor.apvts.state.getProperty ("waveHit", 0), juce::dontSendNotification);
    }
    loadButton.setVisible (map);
    bassButton.setVisible (map);
    {
        const bool bass = map && processor.hasBass(), autoMode = processor.bassAutoActive();
        for (auto& b : bassModeButtons) b.setVisible (bass);
        clearance.slider.setVisible (autoMode);
        recovery.slider.setVisible (autoMode);
        if (autoMode && wave.editing) setEditing (false);
        editButton.setVisible (wave.isVisible() && ! autoMode);
        browser.refreshSlots();
    }
    for (auto& b : ghostButtons) b.setVisible (map);
    for (auto* k : { &mapKick, &mapSnare, &mapRim, &mapOffset }) k->slider.setVisible (map);
    mapGhostDb.slider.setVisible (mapShown);
    fitMapButton.setVisible (mapShown);
    alignButton.setVisible (mapShown);
    {
        const float inStep = processor.getEngine().mapInStep.load(), drift = processor.getEngine().mapDriftMs.load();
        alignButton.setEnabled (map && processor.sidechainConnected.load() && inStep >= 0.6f && std::abs (drift) > 1.0f);
    }
    thresh.setVisible (audio);
    for (auto& b : listenButtons) b.setVisible (audio);
    fitButton.setVisible (audio);
    grooveButton.setVisible (audio);
    for (auto& b : rateButtons) b.setVisible (sync);
    for (auto& b : rateModeButtons) b.setVisible (sync);
    offset.slider.setVisible (sync);
    swing.slider.setVisible (sync);
    autoOffsetButton.setVisible (sync);
    for (auto* k : { &rmCeiling, &rmRange, &rmHold, &rmRelease }) k->slider.setVisible (rm);
    // RM has no shape: the shape controls rest
    for (auto* k : { &depth, &length, &curve, &attack }) { k->slider.setEnabled (! rm); k->slider.setAlpha (rm ? 0.3f : 1.0f); }
    browser.setEnabled (! rm); browser.setAlpha (rm ? 0.35f : 1.0f);
    if (rm && wave.editing) setEditing (false);
    if (rm) editButton.setVisible (false);
    noteBox.setVisible (midi);
    dynamic.slider.setVisible (map ? mapShown : ! sync && ! rm);
    dynamic.centre = map ? juce::Point<int> (212, 127) : midi ? juce::Point<int> (346, 81) : juce::Point<int> (662, 81);
    dynamic.slider.setBounds (dynamic.centre.x - 14, dynamic.centre.y - 14, 28, 28);

    if (! rm) { length.slider.setEnabled (! sync); length.slider.setAlpha (sync ? 0.45f : 1.0f); }
    const bool split = processor.apvts.getRawParameterValue ("band")->load() > 0.5f;
    for (auto* k : { &inside, &above }) { k->slider.setEnabled (split); k->slider.setAlpha (split ? 1.0f : 0.4f); }
    for (auto& b : edgeButtons) b.setEnabled (split);
    const int ph = juce::roundToInt (processor.apvts.getRawParameterValue ("phase")->load());
    const bool phaseOk = t >= 2;                                       // SYNC and MIDI: phase alignment is off
    rotate.slider.setEnabled (phaseOk && ph == 1);
    rotate.slider.setAlpha (phaseOk && ph == 1 ? 1.0f : phaseOk ? 0.45f : 0.3f);
    polarityButton.setEnabled (phaseOk && ph != 2);
    relearnButton.setEnabled (phaseOk && ph == 0);
    lastTrig = t;
}

void RecoilUI::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.1, (now - lastTick) * 0.001);
    lastTick = now;

    auto toDb = [] (float v) { return juce::Decibels::gainToDecibels (v, -100.0f); };
    const float sc = toDb (processor.scPeak.exchange (0.0f)), out = toDb (processor.outPeak.exchange (0.0f));
    scDb = sc >= scDb ? sc : juce::jmax (sc, scDb - 1.2f);
    outDb = out >= outDb ? out : juce::jmax (out, outDb - 1.2f);
    if (trig() == 4)
    {
        const float r = const_cast<rc::Engine&> (processor.getEngine()).rmGrDb.exchange (0.0f);
        rmDb = r <= rmDb ? r : juce::jmin (r, rmDb + 0.8f);
        repaint (720, 64, 360, 30);
    }
    thresh.levelDb = toDb (processor.getEngine().scLevel.load());
    if (thresh.isVisible()) thresh.repaint();

    if (wave.isVisible()) wave.update();
    if (spec.isVisible()) spec.update (dt);
    if (mapView.isVisible()) mapView.update();
    if (trig() == 3 && frame % 10 == 0) layoutTrigger();
    if (trig() != lastTrig) { layoutTrigger(); repaint(); }

    // live text and meters (the plates are cached, so this is cheap)
    juce::String t;
    for (auto* k : knobs) t << k->slider.getValue() << ";";
    t << processor.currentShapeName() << processor.shapeIndex() << processor.currentShapeName (1) << processor.shapeSlot() << juce::roundToInt (processor.getEngine().fittedMs.load())
      << processor.mapVersion.load() << juce::roundToInt (processor.mapProgress.load() * 100.0f)
      << (int) processor.hasBass() << (int) processor.bassAutoActive() << (int) processor.bassShapesActive()
      << text ("bandLo") << text ("bandHi") << text ("band") << trig();
    if (t != lastText) { lastText = t; browser.refreshSlots(); layoutTrigger(); repaint(); }
    else { repaint (16, 62, 1068, 38); repaint (706, 492, 378, 212); }

    if (++frame % 15 == 0 && presetBox.getText() != processor.presets.getCurrentName()) { refreshPresetList(); browser.syncBankToShape(); }
}

void RecoilUI::resized()
{
    wave.setBounds (16, 108, 806, 372);
    spec.setBounds (16, 108, 806, 372);
    mapView.setBounds (16, 108, 806, 372);
    browser.setBounds (834, 108, 250, 372);
    editButton.setBounds (574, 116, 46, 22);
    viewButtons[0].setBounds (628, 116, 52, 22);
    viewButtons[1].setBounds (680, 116, 78, 22);
    viewButtons[2].setBounds (758, 116, 40, 22);
    fitMapButton.setBounds (322, 116, 90, 22);
    alignButton.setBounds (546, 116, 54, 22);
    toolButtons[0].setBounds (32, 116, 50, 22);
    toolButtons[1].setBounds (82, 116, 50, 22);
    toolButtons[2].setBounds (132, 116, 48, 22);
    gridButton.setBounds (190, 116, 78, 22);
    snapButton.setBounds (274, 116, 62, 22);
    mirrorButton.setBounds (342, 116, 64, 22);
    clearButton.setBounds (412, 116, 54, 22);
    saveShapeButton.setBounds (472, 116, 94, 22);

    prevButton.setBounds (280, 22, 28, 26);
    presetBox.setBounds (314, 20, 260, 30);
    nextButton.setBounds (580, 22, 28, 26);
    saveButton.setBounds (616, 22, 52, 26);
    deleteButton.setBounds (674, 22, 40, 26);
    helpButton.setBounds (1050, 20, 30, 30);
    triggerButtons[0].setBounds (794, 22, 46, 26);
    triggerButtons[1].setBounds (840, 22, 44, 26);
    triggerButtons[2].setBounds (884, 22, 52, 26);
    triggerButtons[3].setBounds (936, 22, 42, 26);
    triggerButtons[4].setBounds (978, 22, 36, 26);

    // trigger strip
    thresh.setBounds (228, 74, 100, 14);
    listenButtons[0].setBounds (512, 70, 76, 22);
    listenButtons[1].setBounds (588, 70, 42, 22);
    fitButton.setBounds (780, 70, 100, 22);
    grooveButton.setBounds (886, 70, 110, 22);
    for (int i = 0; i < 6; ++i) rateButtons[i].setBounds (226 + i * 34, 70, 34, 22);
    rateModeButtons[0].setBounds (442, 70, 66, 22);
    rateModeButtons[1].setBounds (508, 70, 40, 22);
    rateModeButtons[2].setBounds (548, 70, 40, 22);
    autoOffsetButton.setBounds (812, 70, 108, 22);
    noteBox.setBounds (222, 69, 100, 24);
    loadButton.setBounds (148, 64, 52, 17);
    bassButton.setBounds (148, 82, 52, 17);
    bassModeButtons[0].setBounds (944, 117, 44, 22);
    {
        const int w[4] = { 42, 52, 36, 52 };
        int x = 386;
        for (int i = 0; i < 4; ++i) { waveHitButtons[i].setBounds (x, 117, w[i], 21); x += w[i]; }
    }
    bassModeButtons[1].setBounds (988, 117, 70, 22);
    ghostButtons[0].setBounds (780, 70, 50, 22);
    ghostButtons[1].setBounds (830, 70, 44, 22);
    ghostButtons[2].setBounds (874, 70, 38, 22);

    // panels
    bandButtons[0].setBounds (584, 504, 46, 22);
    bandButtons[1].setBounds (630, 504, 50, 22);
    edgeButtons[0].setBounds (612, 534, 36, 20);
    edgeButtons[1].setBounds (648, 534, 36, 20);
    bandSlider.setBounds (450, 558, 234, 40);
    phaseButtons[0].setBounds (722, 534, 46, 22);
    phaseButtons[1].setBounds (768, 534, 60, 22);
    phaseButtons[2].setBounds (828, 534, 42, 22);
    polarityButton.setBounds (832, 592, 34, 34);
    relearnButton.setBounds (812, 652, 70, 22);

    for (auto* k : knobs)
    {
        const int side = k->labelMode == 2 ? 28 : k->d + 40;
        k->slider.setBounds (k->centre.x - side / 2, k->centre.y - side / 2, side, side);
    }
    layoutTrigger();
    staticScale = 0.0f;
}

void RecoilUI::drawPlate (juce::Graphics& g, juce::Rectangle<float> r) const
{
    using namespace juce;
    Path box; box.addRoundedRectangle (r, 8.0f);
    DropShadow (Colours::black.withAlpha (0.35f), 18, { 0, 8 }).drawForPath (g, box);
    g.setGradientFill (ColourGradient (Pal::plateTop, 0.0f, r.getY(), Pal::plateBot, 0.0f, r.getBottom(), false));
    g.fillPath (box);
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (box);
        Random rnd ((int64) (r.getX() * 31 + r.getY()));
        for (int i = 0; i < (int) (r.getWidth() * r.getHeight() / 90.0f); ++i)
        {
            const float y = r.getY() + rnd.nextFloat() * r.getHeight(), x = r.getX() + rnd.nextFloat() * r.getWidth();
            g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.02f + rnd.nextFloat() * 0.03f));
            g.fillRect (x, y, 8.0f + rnd.nextFloat() * 60.0f, 0.6f);
        }
    }
    g.setColour (Pal::edge);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    g.setColour (Colours::white.withAlpha (0.10f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 6.0f, r.getRight() - 6.0f);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawHorizontalLine ((int) r.getBottom() - 2, r.getX() + 6.0f, r.getRight() - 6.0f);
    const float in = r.getHeight() < 50.0f ? 9.0f : 14.0f;
    for (auto c : { Point<float> (r.getX() + in, r.getY() + in), Point<float> (r.getRight() - in, r.getY() + in),
                    Point<float> (r.getX() + in, r.getBottom() - in), Point<float> (r.getRight() - in, r.getBottom() - in) })
    {
        if (r.getHeight() < 50.0f && c.y > r.getCentreY()) continue;               // a slim strip gets one screw per side
        const Point<float> cc = r.getHeight() < 50.0f ? Point<float> (c.x, r.getCentreY()) : c;
        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillEllipse (Rectangle<float> (11.0f, 11.0f).withCentre (cc.translated (0.0f, 0.6f)));
        g.setGradientFill (ColourGradient (Colour (0xff7f8792), cc.x - 2.0f, cc.y - 3.0f, Colour (0xff2a2e34), cc.x + 4.0f, cc.y + 5.0f, false));
        g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre (cc));
        Path hex;
        for (int i = 0; i < 6; ++i)
        {
            const auto p = cc.getPointOnCircumference (2.6f, MathConstants<float>::pi / 3.0f * (float) i + 0.3f);
            if (i == 0) hex.startNewSubPath (p); else hex.lineTo (p);
        }
        hex.closeSubPath();
        g.setColour (Colour (0xff16181b));
        g.fillPath (hex);
    }
}

void RecoilUI::drawHead (juce::Graphics& g, float x, float y, const juce::String& title, juce::Colour dot) const
{
    using namespace juce;
    g.setColour (dot.withAlpha (0.35f)); g.fillRoundedRectangle (x - 2.0f, y + 1.0f, 10.0f, 10.0f, 2.0f);
    g.setColour (dot); g.fillRoundedRectangle (x, y + 3.0f, 6.0f, 6.0f, 1.0f);
    g.setFont (lnf.caps (10.5f, 2.3f));
    g.setColour (Colour (0xffd9dde2));
    g.drawText (title, Rectangle<float> (x + 16.0f, y - 1.0f, 300.0f, 14.0f), Justification::centredLeft, false);
}

void RecoilUI::drawWell (juce::Graphics& g, juce::Rectangle<float> r) const
{
    using namespace juce;
    g.setColour (Pal::segWell); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::black.withAlpha (0.8f)); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (Colours::white.withAlpha (0.06f)); g.drawHorizontalLine ((int) r.getBottom(), r.getX() + 5.0f, r.getRight() - 5.0f);
}

void RecoilUI::renderStatic (float ps)
{
    using namespace juce;
    staticScale = ps;
    staticLayer = Image (Image::RGB, roundToInt (kW * ps), roundToInt (kH * ps), false);
    Graphics g (staticLayer);
    g.addTransform (AffineTransform::scale (ps));

    g.setGradientFill (ColourGradient (Pal::bgTop, 0.0f, 0.0f, Pal::bgBot, 0.0f, (float) kH, false));
    g.fillAll();
    {
        Random rnd (7);
        for (int i = 0; i < 26000; ++i)
        {
            g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.03f));
            g.fillRect (rnd.nextFloat() * kW, rnd.nextFloat() * kH, 1.0f, 1.0f);
        }
    }
    if (logo.isValid())
    {
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        const float h = 32.0f, w = h * (float) logo.getWidth() / (float) logo.getHeight();
        g.setOpacity (1.0f);
        g.drawImage (logo, Rectangle<float> (22.0f, 18.0f, w, h), RectanglePlacement::stretchToFit);
    }

    // display well
    {
        const Rectangle<float> r (16.0f, 108.0f, 806.0f, 372.0f);
        g.setColour (Colour (0xff050607)); g.fillRoundedRectangle (r.expanded (1.0f), 7.0f);
        g.setColour (Colour (0xff0b0c0f)); g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Colours::white.withAlpha (0.06f)); g.drawHorizontalLine ((int) r.getBottom() + 1, r.getX() + 6.0f, r.getRight() - 6.0f);
    }
    drawPlate (g, { 16, 62, 1068, 38 });
    drawPlate (g, { 834, 108, 250, 372 });
    drawPlate (g, { 16, 492, 404, 212 });
    drawPlate (g, { 432, 492, 262, 212 });
    drawPlate (g, { 706, 492, 186, 212 });
    drawPlate (g, { 904, 492, 180, 212 });

    drawWell (g, { 792, 20, 224, 30 });
    drawWell (g, { 626, 114, 174, 26 });
    drawWell (g, { 582, 502, 100, 26 });
    drawWell (g, { 610, 532, 76, 24 });
    drawWell (g, { 720, 532, 152, 26 });

    g.setFont (lnf.caps (9.5f, 2.0f));
    g.setColour (Pal::caps);
    g.drawText ("TRIGGER", Rectangle<float> (716.0f, 29.0f, 68.0f, 14.0f), Justification::centredRight, false);

    drawHead (g, 852.0f, 126.0f, "SHAPES", Pal::orange);
    drawHead (g, 450.0f, 510.0f, "BAND", Pal::orange);
    drawHead (g, 922.0f, 510.0f, "OUTPUT", Pal::orange);

    g.setFont (lnf.caps (9.5f, 2.0f));
    g.setColour (Pal::caps);
    for (auto* k : knobs)
    {
        if (k->labelMode == 0 && k->label.isNotEmpty())
            g.drawText (k->label, Rectangle<float> ((float) k->centre.x - 60.0f, (float) (k->centre.y + k->d / 2 + 15), 120.0f, 12.0f), Justification::centred, false);
        if (k->labelMode == 1)
            g.drawText (k->label, Rectangle<float> ((float) k->centre.x + 32.0f, 628.0f, 80.0f, 12.0f), Justification::centredLeft, false);
    }
    g.drawText ("EDGE", Rectangle<float> (560.0f, 538.0f, 46.0f, 12.0f), Justification::centredRight, false);
    g.drawText ("POLARITY", Rectangle<float> (806.0f, 630.0f, 86.0f, 12.0f), Justification::centred, false);
    g.setFont (lnf.caps (9.5f, 0.0f)); g.setColour (Pal::dim);
    g.drawText ("How much of the shape each side gets", Rectangle<float> (452.0f, 680.0f, 230.0f, 12.0f), Justification::centredLeft, false);
    g.setFont (lnf.chak (8.0f)); g.setColour (Colour (0xff6a717b));
    g.drawText ("SC", Rectangle<float> (1038.0f, 527.0f, 20.0f, 10.0f), Justification::centredRight, false);
    g.drawText ("OUT", Rectangle<float> (1064.0f, 527.0f, 22.0f, 10.0f), Justification::centredLeft, false);
}

void RecoilUI::paint (juce::Graphics& g)
{
    using namespace juce;
    const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! staticLayer.isValid() || ps != staticScale) renderStatic (ps);
    g.drawImage (staticLayer, Rectangle<float> (0, 0, (float) kW, (float) kH), RectanglePlacement::stretchToFit);
    const auto& eng = processor.getEngine();
    const int t = trig();
    const String dot = String::fromUTF8 ("\xc2\xb7");

    // ---- edit tool wells (they sit inside the display)
    if (wave.isVisible() && editButton.isVisible()) drawWell (g, { 572, 114, 50, 26 });
    if (waveHitButtons[0].isVisible()) drawWell (g, { 384, 114, 186, 26 });
    if (wave.editing && wave.isVisible()) drawWell (g, { 30, 114, 152, 26 });

    // ---- trigger strip
    auto cap = [&] (const String& s, float x) { g.setFont (lnf.caps (9.5f, 2.0f)); g.setColour (Pal::caps); g.drawText (s, Rectangle<float> (x, 75.0f, 120.0f, 12.0f), Justification::centredLeft, false); };
    auto small = [&] (const String& label, const String& value, float x, Colour vc = Pal::text)
    {
        g.setFont (lnf.caps (10.0f, 0.3f)); g.setColour (Colour (0xff8a919b));
        g.drawText (label, Rectangle<float> (x, 74.0f, 200.0f, 14.0f), Justification::centredLeft, false);
        const float w = (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label);
        g.setFont (lnf.chak (11.5f)); g.setColour (vc);
        g.drawText (value, Rectangle<float> (x + w + 6.0f, 73.0f, 160.0f, 16.0f), Justification::centredLeft, false);
    };
    drawHead (g, 34.0f, 75.0f, t == 0 ? "TEMPO SYNC" : t == 1 ? "MIDI TRIGGER" : t == 3 ? "SONG MAP" : t == 4 ? "RING MOD" : "AUDIO TRIGGER", Pal::orange);
    if (t == 4)
    {
        small ("CEILING", text ("rmCeiling"), 228.0f);
        small ("RANGE", text ("rmRange"), 358.0f);
        small ("HOLD", text ("rmHold"), 472.0f);
        small ("SMOOTH", text ("rmRelease"), 584.0f);
        // the squeeze right now: a bar from 0 dB down to -24 dB
        const bool sc = processor.sidechainConnected.load();
        cap ("SQUEEZE", 728.0f);
        const Rectangle<float> bar (806.0f, 76.0f, 160.0f, 10.0f);
        g.setColour (Pal::segWell); g.fillRoundedRectangle (bar, 3.0f);
        const float db = jlimit (0.0f, 24.0f, -rmDb);
        if (db > 0.05f) { g.setColour (Pal::orange); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * db / 24.0f), 3.0f); }
        g.setFont (lnf.chak (11.5f)); g.setColour (sc ? Pal::text : Pal::dim);
        g.drawText (! sc ? String ("no kick") : db < 0.05f ? String ("0 dB") : fixMinus ("-" + String (db, 1) + " dB"),
                    Rectangle<float> (974.0f, 73.0f, 90.0f, 16.0f), Justification::centredLeft, false);
    }
    else if (t == 3)
    {
        // two rows: the drum bus, the bass bus
        const auto& info = processor.getMapInfo();
        const auto& binfo = processor.getBassInfo();
        const float prog = processor.mapProgress.load();
        const bool hasHits = ! processor.getMapHits().empty(), hasBass = processor.hasBass();
        auto row = [&] (float y, const String& main, Colour c, const String& sub)
        {
            g.setFont (lnf.chak (11.0f)); g.setColour (c);
            const float w = jmin (130.0f, (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), main) + 2.0f);
            g.drawText (main, Rectangle<float> (208.0f, y, w, 14.0f), Justification::centredLeft, true);
            if (sub.isNotEmpty())
            {
                g.setFont (lnf.chak (9.5f)); g.setColour (Pal::dim);
                g.drawText (sub, Rectangle<float> (214.0f + w, y + 1.0f, 418.0f - 214.0f - w, 13.0f), Justification::centredLeft, true);
            }
        };
        const int secs = roundToInt (info.seconds);
        const String len = String (secs / 60) + ":" + String (secs % 60).paddedLeft ('0', 2);
        if (prog >= 0.0f) row (65.0f, "Analysing... " + String (roundToInt (prog * 100.0f)) + "%", Pal::amber, {});
        else if (processor.mapError.isNotEmpty() && ! hasHits) row (65.0f, processor.mapError, Pal::red, {});
        else if (! hasHits) row (65.0f, "Drop the drum bus bounce here", Pal::dim, {});
        else row (65.0f, info.name, Pal::text, len + " " + dot + " " + String (info.stats.count[0]) + " K " + dot + " " + String (info.stats.count[1]) + " S " + dot + " " + String (info.stats.ghosts) + " ghosts");
        if (prog < 0.0f)
        {
            if (processor.mapError.isNotEmpty() && hasHits) row (82.0f, processor.mapError, Pal::red, {});
            else if (hasBass)
            {
                const auto& c = binfo.counts;
                const int secsB = roundToInt (binfo.seconds);
                row (82.0f, binfo.name, Pal::text, String (secsB / 60) + ":" + String (secsB % 60).paddedLeft ('0', 2) + " " + dot + " "
                                                  + String (processor.bassAutoActive() ? "AUTO" : processor.bassShapesActive() ? "PER BASS" : "loaded"));
            }
            else row (82.0f, hasHits ? "Optional: the bass bus, for PER BASS" : "Then the bass bus (optional)", Pal::faint, {});
        }
        small ("KICK", text ("mapKick"), 454.0f);
        small ("SNARE", text ("mapSnare"), 556.0f);
        small ("RIM", text ("mapRim"), 662.0f);
        cap ("GHOSTS", 722.0f);
        drawWell (g, { 778, 68, 136, 26 });
        small ("OFFSET", text ("mapOffset"), 950.0f);
    }
    else if (t == 2)
    {
        cap ("INPUT", 184.0f);
        small ("THRESHOLD", fixMinus (String (roundToInt (processor.apvts.getRawParameterValue ("threshold")->load())) + " dB"), 336.0f);
        cap ("LISTEN", 462.0f);
        drawWell (g, { 510, 68, 122, 26 });
        small ("DYNAMIC", text ("dynamic"), 680.0f);
        const bool playing = processor.hostPlaying.load(), groove = processor.apvts.getRawParameterValue ("groove")->load() > 0.5f;
        const float conf = eng.grooveConf.load();
        const Colour led = ! processor.sidechainConnected.load() ? Pal::faint : groove && playing && conf >= 0.75f ? Pal::lock : groove && playing ? Pal::amber : Pal::lock.withAlpha (0.6f);
        g.setColour (led.withAlpha (0.35f)); g.fillEllipse (1001.0f, 76.0f, 10.0f, 10.0f);
        g.setColour (led); g.fillEllipse (1003.0f, 78.0f, 6.0f, 6.0f);
        g.setFont (lnf.caps (10.0f, 0.0f)); g.setColour (Pal::dim);
        g.drawText (! processor.sidechainConnected.load() ? "no input" : groove && playing && conf >= 0.75f ? "locked" : groove && playing ? "learning" : "0 ms",
                    Rectangle<float> (1015.0f, 74.0f, 60.0f, 14.0f), Justification::centredLeft, false);
    }
    else if (t == 0)
    {
        cap ("RATE", 184.0f);
        drawWell (g, { 224, 68, 208, 26 });
        drawWell (g, { 440, 68, 150, 26 });
        small ("OFFSET", text ("offset"), 624.0f);
        small ("SWING", text ("swing"), 732.0f);
        g.setFont (lnf.caps (10.0f, 0.0f)); g.setColour (Pal::dim);
        g.drawText (String (processor.hostBpm.load(), 1) + " BPM  " + dot + "  " + (processor.hostPlaying.load() ? "playing" : "stopped"),
                    Rectangle<float> (928.0f, 74.0f, 140.0f, 14.0f), Justification::centredLeft, false);
    }
    else
    {
        cap ("NOTE", 184.0f);
        small ("DYNAMIC", text ("dynamic"), 364.0f);
        g.setFont (lnf.caps (10.0f, 0.0f)); g.setColour (Pal::dim);
        g.drawText ("Every note-on restarts the shape. With DYNAMIC up, velocity sets how deep.", Rectangle<float> (480.0f, 74.0f, 520.0f, 14.0f), Justification::centredLeft, false);
    }

    // ---- knob values
    g.setFont (lnf.chak (14.0f));
    for (auto* k : knobs)
    {
        if (k->labelMode >= 2) continue;
        String v = text (k->id.toRawUTF8());
        if (k == &length && t == 0) v = rateText (processor.apvts);
        else if (k == &length && t == 3 && processor.apvts.getRawParameterValue ("fit")->load() > 0.5f) v = "PER HIT";
        else if (k == &length && t != 3 && processor.apvts.getRawParameterValue ("fit")->load() > 0.5f && eng.fittedMs.load() > 0) v = String (roundToInt (eng.fittedMs.load())) + " ms FIT";
        else if (k == &rotate)
        {
            const int ph = roundToInt (processor.apvts.getRawParameterValue ("phase")->load());
            if (ph == 2) v = "OFF";
            else if (ph == 0) { const float th = eng.thetaDeg.load(); v = (th >= 0 ? "+" : minusSign()) + String (std::abs (roundToInt (th))) + String::fromUTF8 ("\xc2\xb0"); }
        }
        g.setColour (k->slider.isEnabled() ? Pal::text : Pal::dim);
        if (k->labelMode == 1)
        {
            g.setFont (lnf.chak (12.5f));
            g.drawText (v, Rectangle<float> ((float) k->centre.x + 32.0f, 642.0f, 80.0f, 16.0f), Justification::centredLeft, false);
            g.setFont (lnf.chak (14.0f));
        }
        else g.drawText (v, Rectangle<float> ((float) k->centre.x - 60.0f, (float) (k->centre.y + k->d / 2 + 29), 120.0f, 18.0f), Justification::centred, false);
    }

    // ---- SHAPE head
    drawHead (g, 34.0f, 510.0f, "SHAPE", Pal::orange);
    g.setFont (lnf.caps (10.5f, 2.3f)); g.setColour (Pal::orange);
    if (processor.bassAutoActive())
    {
        g.setColour (Pal::orange);
        g.drawText ("AUTO  " + dot + "  CLEARANCE " + text ("clearance"), Rectangle<float> (112.0f, 509.0f, 290.0f, 14.0f), Justification::centredLeft, true);
    }
    else if (processor.bassShapesActive())
    {
        const char* kinds[] = { "SUB", "SNARE", "PLUCK", "MID", "NOTE" };
        const int sl = processor.shapeSlot();
        g.setColour (Pal::orange);
        g.drawText (String ("PER BASS  ") + dot + "  " + kinds[sl] + " " + processor.currentShapeName (sl).toUpperCase(), Rectangle<float> (112.0f, 509.0f, 290.0f, 14.0f), Justification::centredLeft, true);
    }
    else if (trig() == 4)
    {
        g.setColour (Pal::dim);
        g.drawText ("RM  " + dot + "  NO SHAPE: SAMPLE BY SAMPLE", Rectangle<float> (112.0f, 509.0f, 290.0f, 14.0f), Justification::centredLeft, true);
    }
    else if (trig() == 3)
    {
        // MAP: both shapes, the one being edited brighter
        const int sl = processor.shapeSlot();
        g.setFont (lnf.caps (10.5f, 1.2f));
        String k = "KICK " + processor.currentShapeName (0).toUpperCase(), s = "SNARE " + processor.currentShapeName (1).toUpperCase();
        const float wk = jmin (180.0f, (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), k) + 4.0f);
        g.setColour (sl == 0 ? Pal::orange : Pal::dim);
        g.drawText (k, Rectangle<float> (112.0f, 509.0f, wk, 14.0f), Justification::centredLeft, true);
        g.setColour (sl == 1 ? Pal::orange : Pal::dim);
        g.drawText (s, Rectangle<float> (112.0f + wk + 12.0f, 509.0f, 290.0f - wk - 12.0f, 14.0f), Justification::centredLeft, true);
    }
    else g.drawText (processor.currentShapeName().toUpperCase(), Rectangle<float> (112.0f, 509.0f, 290.0f, 14.0f), Justification::centredLeft, true);

    // ---- BAND value
    {
        const bool split = processor.apvts.getRawParameterValue ("band")->load() > 0.5f;
        g.setFont (lnf.chak (12.5f));
        g.setColour (split ? Pal::text : Pal::dim);
        const float lo = processor.apvts.getRawParameterValue ("bandLo")->load(), hi = processor.apvts.getRawParameterValue ("bandHi")->load();
        g.drawText (split ? hzText (lo) + String::fromUTF8 (" \xe2\x80\x93 ") + hzText (hi) + " Hz" : String ("Whole signal"), Rectangle<float> (452.0f, 534.0f, 120.0f, 16.0f), Justification::centredLeft, false);
    }

    // ---- PHASE head
    {
        const int ph = roundToInt (processor.apvts.getRawParameterValue ("phase")->load());
        drawHead (g, 724.0f, 510.0f, "PHASE", Pal::orange);
        String st; Colour c = Pal::dim;
        if (trig() == 0 || trig() == 1) st = "OFF";
        else if (ph == 2) st = "OFF";
        else if (ph == 1) { st = "MANUAL"; c = Pal::brass; }
        else if (! processor.sidechainConnected.load()) { st = "NO KICK"; c = Pal::dim; }
        else if (eng.phaseLocked.load()) { st = "LOCKED"; c = Pal::lock; }
        else { st = "LEARNING"; c = Pal::amber; }
        g.setFont (lnf.caps (10.5f, 2.3f)); g.setColour (c);
        g.drawText (st, Rectangle<float> (800.0f, 509.0f, 90.0f, 14.0f), Justification::centredLeft, false);
    }

    // ---- OUTPUT: average duck and meters
    {
        g.setFont (lnf.caps (9.5f, 0.5f)); g.setColour (Colour (0xff8a919b));
        g.drawText ("AVG DUCK", Rectangle<float> (922.0f, 664.0f, 70.0f, 14.0f), Justification::centredLeft, false);
        const float avg = eng.avgDuckDb.load();
        g.setFont (lnf.chak (12.0f)); g.setColour (Pal::orange);
        g.drawText ((avg < -0.05f ? minusSign() : String()) + String (std::abs (avg), 1) + " dB", Rectangle<float> (982.0f, 663.0f, 70.0f, 16.0f), Justification::centredLeft, false);
        auto meter = [&] (float x, float frac, Colour top, Colour bot)
        {
            const Rectangle<float> r (x, 540.0f, 8.0f, 118.0f);
            g.setColour (Colour (0xff0d0f11)); g.fillRoundedRectangle (r, 2.0f);
            frac = jlimit (0.0f, 1.0f, frac);
            if (frac <= 0.0f) return;
            const auto f = r.withTrimmedTop (r.getHeight() * (1.0f - frac));
            g.setGradientFill (ColourGradient (top, 0, f.getY(), bot, 0, f.getBottom(), false));
            g.fillRoundedRectangle (f, 2.0f);
        };
        meter (1050.0f, (scDb + 60.0f) / 60.0f, Colour (0xffff5a61), Colour (0xff7d1d22));
        meter (1066.0f, (outDb + 60.0f) / 60.0f, Pal::text, Colour (0xff5f6670));
    }
}
