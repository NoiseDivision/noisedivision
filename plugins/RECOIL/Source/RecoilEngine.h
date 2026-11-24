#pragma once
// RECOIL 2 engine: shape-based volume ducking with zero latency.
//
//   trigger (SYNC / MIDI / AUDIO)  ->  playhead x in 0..1 over one cycle  ->  shape s(x)
//   gain  g = 1 - a * (1 - s)        a = DEPTH * dynamic scale * MIX        (s = 1 untouched, s = 0 fully ducked)
//   every jump (new hit, new cycle) is crossfaded over ATTACK with a half-cosine, so nothing clicks
//   FULL: y = g * x (bit-exact when g = 1).  SPLIT: Linkwitz-Riley bands, only the band gets g (INSIDE / ABOVE amounts)
//   PHASE: one rotation of the bass (or the band) by theta through a 90-degree all-pass pair. AUTO picks the theta
//          that best lines the bass up with the kick's low end while they overlap:
//              corr(theta) = cos(theta) * SA - sin(theta) * SB,   SA = sum k*g*A(x),  SB = sum k*g*B(x)
//          so theta* = -atan2(SB, SA) in closed form. It only moves while the bass is ducked, so it is never heard.
//
// Everything here is plain C++ (no JUCE) so it can be tested offline.

#include <array>
#include <vector>
#include <cmath>
#include <complex>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include "BassAnalyser.h"

namespace rc
{
constexpr double kPi = 3.14159265358979323846;

//==============================================================================
// Shapes: nodes from x = 0 to 1. bend > 0 starts fast, < 0 starts slow; s = true makes the segment an S-curve.
struct Node { float x = 0, y = 0, bend = 0; bool s = false; };

inline double bendT (double t, double b, bool s)
{
    if (s) { const double k = 1.0 + std::abs (b); const double p = std::pow (t, k), q = std::pow (1.0 - t, k); return p / (p + q); }
    if (std::abs (b) < 1e-4) return t;
    return b > 0 ? 1.0 - std::pow (1.0 - t, 1.0 + b) : std::pow (t, 1.0 - b);
}

inline double shapeAt (const std::vector<Node>& n, double x)
{
    if (n.empty()) return 1.0;
    if (x <= n.front().x) return n.front().y;
    if (x >= n.back().x) return n.back().y;
    for (size_t i = 1; i < n.size(); ++i)
        if (x <= n[i].x)
        {
            const auto& a = n[i - 1]; const auto& b = n[i];
            const double t = (x - a.x) / std::max (1e-9, (double) (b.x - a.x));
            return a.y + (b.y - a.y) * bendT (t, b.bend, b.s);
        }
    return n.back().y;
}

constexpr int kTable = 2048;
struct ShapeTable
{
    std::array<float, kTable + 1> v {};
    float releaseEnd = 1.0f;       // where the shape is back up after its dip (FIT LENGTH aims this at the kick's end)
    float lowest = 1.0f;           // lowest level of the shape
    ShapeTable() { v.fill (1.0f); } // until a shape arrives: no duck (never silence)
    // AUTO: a duck given in dB per frame (r[0..n)), spread over the whole table
    void buildDb (const float* r, int n)
    {
        n = std::clamp (n, 2, 128);
        float gn[128];
        for (int i = 0; i < n; ++i) gn[i] = std::pow (10.0f, -r[i] / 20.0f);   // a few pow per hit, not one per table point
        for (int i = 0; i <= kTable; ++i)
        {
            const float p = (float) i / kTable * (float) (n - 1);
            const int a = std::min ((int) p, n - 2); const float f = p - (float) a;
            v[(size_t) i] = gn[a] + (gn[a + 1] - gn[a]) * f;
        }
        v[(size_t) kTable] = 1.0f;
        lowest = *std::min_element (v.begin(), v.end());
        releaseEnd = 1.0f;
    }
    void build (const std::vector<Node>& nodes)
    {
        for (int i = 0; i <= kTable; ++i) v[(size_t) i] = (float) std::clamp (shapeAt (nodes, (double) i / kTable), 0.0, 1.25);
        int iMin = 0; for (int i = 1; i <= kTable; ++i) if (v[(size_t) i] < v[(size_t) iMin] - 1e-6f) iMin = i;
        lowest = v[(size_t) iMin];
        float top = lowest; for (int i = iMin; i <= kTable; ++i) top = std::max (top, v[(size_t) i]);
        releaseEnd = 1.0f;
        if (top - lowest > 0.1f)
            for (int i = iMin; i <= kTable; ++i)
                if (v[(size_t) i] >= lowest + 0.97f * (top - lowest)) { releaseEnd = std::max (0.02f, (float) i / kTable); break; }
    }
    inline float at (double x) const
    {
        x = std::clamp (x, 0.0, 1.0) * kTable;
        const int i = std::min ((int) x, kTable - 1); const float f = (float) (x - i);
        return v[(size_t) i] + (v[(size_t) i + 1] - v[(size_t) i]) * f;
    }
};

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    void lowpass (double fs, double fc, double q)
    {
        const double w = 2.0 * kPi * std::clamp (fc, 5.0, fs * 0.45) / fs, cw = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 - cw) * 0.5 / a0; b1 = (1.0 - cw) / a0; b2 = b0; a1 = -2.0 * cw / a0; a2 = (1.0 - al) / a0;
    }
    void highpass (double fs, double fc, double q)
    {
        const double w = 2.0 * kPi * std::clamp (fc, 5.0, fs * 0.45) / fs, cw = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 + cw) * 0.5 / a0; b1 = -(1.0 + cw) / a0; b2 = b0; a1 = -2.0 * cw / a0; a2 = (1.0 - al) / a0;
    }
    void lowpass1 (double fs, double fc)
    {
        const double k = std::tan (kPi * std::clamp (fc, 5.0, fs * 0.45) / fs);
        b0 = k / (1.0 + k); b1 = b0; b2 = 0; a1 = (k - 1.0) / (k + 1.0); a2 = 0;
    }
    void highpass1 (double fs, double fc)
    {
        const double k = std::tan (kPi * std::clamp (fc, 5.0, fs * 0.45) / fs);
        b0 = 1.0 / (1.0 + k); b1 = -b0; b2 = 0; a1 = (k - 1.0) / (k + 1.0); a2 = 0;
    }
    void allpass (double fs, double fc, double q)
    {
        const double w = 2.0 * kPi * std::clamp (fc, 5.0, fs * 0.45) / fs, cw = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 - al) / a0; b1 = -2.0 * cw / a0; b2 = 1.0; a1 = b1; a2 = b0;
    }
    void allpass1 (double fs, double fc)
    {
        const double k = std::tan (kPi * std::clamp (fc, 5.0, fs * 0.45) / fs);
        a1 = (k - 1.0) / (k + 1.0); b0 = a1; b1 = 1.0; b2 = 0; a2 = 0;
    }
    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Linkwitz-Riley crossover, order 2 (SOFT) or 4 (HARD). lo + hi = an all-pass.
struct XOver
{
    std::array<Biquad, 2> lp, hp;
    double hpSign = 1.0;
    void set (double fs, double fc, bool hard)
    {
        if (hard) { lp[0].lowpass (fs, fc, 0.70710678); hp[0].highpass (fs, fc, 0.70710678); hpSign = 1.0; }
        else      { lp[0].lowpass1 (fs, fc); hp[0].highpass1 (fs, fc); hpSign = -1.0; }
        lp[1] = lp[0]; hp[1] = hp[0];
    }
    void reset() { for (auto& b : lp) b.reset(); for (auto& b : hp) b.reset(); }
    inline void process (double x, double& lo, double& hi)
    {
        lo = lp[1].process (lp[0].process (x));
        hi = hpSign * hp[1].process (hp[0].process (x));
    }
    inline double lowpass (double x) { return lp[1].process (lp[0].process (x)); }
    inline double highpass (double x) { return hp[1].process (hp[0].process (x)); }      // true polarity
};

// The all-pass a crossover of the same frequency and order adds (to keep the other bands in step).
struct XAllpass
{
    Biquad s;
    void set (double fs, double fc, bool hard) { if (hard) s.allpass (fs, fc, 0.70710678); else s.allpass1 (fs, fc); }
    void reset() { s.reset(); }
    inline double process (double x) { return s.process (x); }
};

//==============================================================================
// Two chains of first-order all-passes whose outputs stay 90 degrees apart from 15 Hz to 3 kHz (error < 0.1 degree at the
// listed rates, < 0.7 degree in between). Pole frequencies were optimised per rate; other rates use the nearest table.
struct Quadrature
{
    static constexpr int kN = 6;
    std::array<double, kN> ca {}, cb {};
    std::array<double, kN> ax1 {}, ay1 {}, bx1 {}, by1 {};
    void set (double fs)
    {
        struct Row { double fs; double f[2 * kN]; };
        static const Row rows[] = {
            { 44100.0, { 14.571529, 53.096807, 162.167752, 489.050337, 1551.290226, 8940.953228, 4.395640, 29.298579, 93.261310, 281.308618, 858.328009, 3081.847569 } },
            { 48000.0, { 14.565460, 53.061171, 162.006486, 488.422477, 1549.685273, 9107.416704, 4.394087, 29.283096, 93.183899, 280.984227, 857.193926, 3084.077030 } },
            { 88200.0, { 14.542526, 52.926647, 161.398170, 486.055092, 1543.616687, 9864.160519, 4.388217, 29.224618, 92.891793, 279.760935, 852.915851, 3092.712418 } },
            { 96000.0, { 14.541029, 52.917875, 161.358556, 485.901073, 1543.221027, 9922.281510, 4.387833, 29.220802, 92.872758, 279.681320, 852.637469, 3093.286499 } },
            { 176400.0, { 14.535338, 52.884539, 161.208006, 485.315718, 1541.716198, 10155.901138, 4.386376, 29.206301, 92.800419, 279.378746, 851.579314, 3095.483170 } },
            { 192000.0, { 14.534964, 52.882357, 161.198159, 485.277378, 1541.617463, 10171.944405, 4.386280, 29.205350, 92.795689, 279.358942, 851.509964, 3095.627419 } } };
        const Row* best = &rows[0];
        for (const auto& r : rows) if (std::abs (std::log (r.fs / fs)) < std::abs (std::log (best->fs / fs))) best = &r;
        auto coef = [fs] (double fp) { const double c = std::tan (kPi * std::min (fp, fs * 0.45) / fs); return (1.0 - c) / (1.0 + c); };
        for (int i = 0; i < kN; ++i) { ca[(size_t) i] = coef (best->f[i]); cb[(size_t) i] = coef (best->f[kN + i]); }
        reset();
    }
    void reset() { ax1.fill (0); ay1.fill (0); bx1.fill (0); by1.fill (0); }
    // H(z) = (c - z^-1) / (1 - c z^-1)
    inline void process (double x, double& a, double& b)
    {
        a = x; b = x;
        for (int i = 0; i < kN; ++i)
        {
            const size_t k = (size_t) i;
            const double ya = ca[k] * a - ax1[k] + ca[k] * ay1[k]; ax1[k] = a; ay1[k] = ya; a = ya;
            const double yb = cb[k] * b - bx1[k] + cb[k] * by1[k]; bx1[k] = b; by1[k] = yb; b = yb;
        }
    }
    // phase of chain A at a frequency, relative to the input (radians)
    double phaseA (double fs, double hz) const
    {
        const std::complex<double> z1 = std::polar (1.0, -2.0 * kPi * hz / fs);
        std::complex<double> h (1.0, 0.0);
        for (double c : ca) h *= (c - z1) / (1.0 - c * z1);
        return std::arg (h);
    }
};

inline double wrapPi (double a) { while (a > kPi) a -= 2 * kPi; while (a < -kPi) a += 2 * kPi; return a; }

//==============================================================================
enum class Trigger { Sync = 0, Midi = 1, Audio = 2, Map = 3, Rm = 4 };
enum class GhostMode { Ignore = 0, Scale = 1, Full = 2 };
enum class PhaseMode { Auto = 0, Manual = 1, Off = 2 };

struct Settings
{
    Trigger trigger = Trigger::Audio;
    double rateBeats = 1.0;         // SYNC: beats per cycle (1/4 = 1 beat)
    double offset = 0.0;            // SYNC: cycle fraction, -0.5..0.5
    double swing = 0.0;             // SYNC: 0..1, delays every second cycle by up to a third of a cycle
    bool autoOffset = false;
    int midiNote = -1;              // -1: any note
    double threshDb = -24.0;
    bool listenLows = true;
    double dynamic = 0.0;           // 0..1
    bool fit = false, groove = true;
    double depth = 1.0;             // 0..1 of the way to silence (1 = -inf)
    double lengthMs = 300.0;        // AUDIO / MIDI cycle length
    double curve = 0.0;             // -1..1
    double attackMs = 4.0;
    bool split = false, hard = false;
    double lo = 20.0, hi = 150.0, inside = 1.0, above = 0.0;
    PhaseMode phase = PhaseMode::Off;
    double rotateDeg = 0.0;
    bool polarity = false;
    double mix = 1.0, gainDb = 0.0;
    // MAP: how deep each kind of hit ducks (0..1), what happens to ghost notes, and a nudge in ms
    std::array<double, rcmap::kTypes> mapAmount { 1.0, 0.5, 0.0, 0.0 };
    GhostMode ghost = GhostMode::Ignore;
    double ghostDb = 12.0, mapOffsetMs = 0.0;
    // MAP with a bass bounce: AUTO works out each duck from the clash (CLEARANCE, RECOVERY); SHAPES picks a shape per kind of bass
    int bassMode = 0;                       // 0 AUTO, 1 SHAPES
    double clearanceDb = 6.0, recoveryMs = 60.0;
    // RM ("ring mod" sidechain): every sample the bass gets only the room the kick leaves under CEILING
    double rmCeilDb = -3.0, rmRangeDb = 24.0, rmHoldMs = 0.0, rmReleaseMs = 3.0;
};

struct HostTime
{
    bool valid = false, playing = false;
    double ppq = 0.0, bpm = 120.0, barBeats = 4.0;
    bool hasTime = false; double timeSec = 0.0;      // song position in seconds (MAP)
};

struct MidiHit { int offset; float velocity; };

// Per-sample outputs for the display (any of them may be null).
struct ScopeOut { float* kickLow = nullptr; float* before = nullptr; float* after = nullptr; float* gain = nullptr;
                  float* duckType = nullptr; };   // per sample: 0 no duck, 1 kick, 2 snare, 3 rim (MAP; other triggers: 1)

//==============================================================================
class Engine
{
public:
    // ---- read by the editor (relaxed atomics: a stale frame costs nothing)
    std::atomic<float> fittedMs { 0.0f }, autoOffsetFrac { 0.0f }, thetaDeg { 0.0f }, match { 0.0f }, f0 { 0.0f };
    std::atomic<float> grooveConf { 0.0f }, avgDuckDb { 0.0f }, dynScale { 1.0f }, scLevel { 0.0f };
    std::atomic<bool> phaseLocked { false }, hasSidechain { false };
    std::atomic<int> relearnRequest { 0 };
    std::atomic<float> mapNow { -1.0f }, mapDriftMs { 0.0f }, mapInStep { -1.0f };   // MAP: song time; kick check (-1: no data)
    std::atomic<float> rmGrDb { 0.0f };                // RM: the deepest the bass went in the last block (dB, <= 0)
    int lastHit = -1;                                  // offset in the last block where a hit / cycle started (-1: none)
    int lastHitKind = 0;                               // MAP: what it was: 0 kick, 1 snare, 2 rim, 3 ghost note
    double cycleSamples = 0.0;                         // current cycle length (for the display)

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        fs = sampleRate;
        for (auto& c : ch) { c.quad.set (fs); c.xHi.reset(); c.xLo.reset(); c.apLo.reset(); }
        scLp1.lowpass (fs, 150.0, 0.70710678); scLp2 = scLp1; scHp.highpass (fs, 30.0, 0.70710678);
        tgtLp.lowpass (fs, 200.0, 0.70710678);
        mLpA1 = scLp1; mLpA2 = scLp1; mHpA = scHp; mLpB1 = scLp1; mLpB2 = scLp1; mHpB = scHp;
        bandsFor = -1.0;
        reset();
    }

    void reset()
    {
        x = 1.0; running = false; gCur = gStart = 1.0; rampPos = rampLen = 0; cycleIndex = INT64_MIN;
        fastEnv = slowEnv = lowEnv = 0; sinceHit = 1 << 30; hitPeak = refPeak = 0; hitScale = 1.0; measuring = false;
        fitHist.fill (0); fitCount = 0; fitMs = 0;
        slotConf.fill (0); slotPos.fill (0); slotSeen.fill (false); slotFired.fill (false); lastBar = INT64_MIN; predictedAt = -1; predictedConfirm = false;
        offCos = offSin = 0; offCount = 0;
        sa = sb = skk = srr = skr = 0; avgA = avgB = 0; thetaAuto = 0; thetaCur = 0; lockRun = 0; hitsLearned = 0; lastThetaAuto = 1e9; accum = false;
        zcLast = 0; zcPrev = 0; zcCount = 0; f0Est = 50.0;
        duckPow = 1.0;
        rmG = 1.0; rmHoldLeft = 0;
        mapCursor = kickCursor = 0; mapLastT = -1e9; mapAmt = 1.0; mapLen = 0.25 * fs; nMapFire = nMapKick = mk2 = 0;
        clock = 0; expected.fill (0); expMatched.fill (false); expHead = 0; pendingEarly = 0; driftAvg = 0; inStepAvg = -1.0;
        mapDriftMs.store (0.0f); mapInStep.store (-1.0f);
        for (auto& c : ch) { c.quad.reset(); c.xHi.reset(); c.xLo.reset(); c.apLo.reset(); }
        scLp1.reset(); scLp2.reset(); scHp.reset(); tgtLp.reset();
        for (auto* b : { &mLpA1, &mLpA2, &mHpA, &mLpB1, &mLpB2, &mHpB }) b->reset();
    }

    // Shape tables are swapped from the message thread: write the pending table, then publish it.
    // Slot 0 is the main shape (every trigger, and the kicks in MAP); slot 1 is MAP's snare and rim shape.
    bool trySetShape (const std::vector<Node>& nodes, int slot = 0)
    {
        auto& sl = slots[(size_t) std::clamp (slot, 0, kShapeSlots - 1)];
        if (sl.ready.load (std::memory_order_acquire)) return false;
        sl.pending.build (nodes);
        sl.ready.store (true, std::memory_order_release);
        return true;
    }
    const ShapeTable& table (int slot = 0) const { return slots[(size_t) std::clamp (slot, 0, kShapeSlots - 1)].active; }
    // slots: 0 main (kicks; with a bass bounce in SHAPES: SUB), 1 snares and rims, 2 PLUCK, 3 MID, 4 NOTE
    static constexpr int kShapeSlots = 5;
    std::atomic<const ShapeTable*> shownTable { nullptr };      // the table of the last duck (for the WAVE view in AUTO)
    std::atomic<bool> autoActive { false };

    // MAP hits are handed over the same way (the swap on the audio thread does not allocate)
    struct BassMap { std::vector<uint8_t> kind; std::vector<int8_t> clash; };   // clash: hits x rcbass::kFrames, or empty
    bool trySetMap (const std::vector<rcmap::Hit>& hits, const BassMap& bass = {})
    {
        if (pendingMapReady.load (std::memory_order_acquire)) return false;
        pendingMap = hits;
        pendingBass = bass;
        if (pendingBass.clash.size() != hits.size() * (size_t) rcbass::kFrames || pendingBass.kind.size() != hits.size()) pendingBass = {};
        pendingMapReady.store (true, std::memory_order_release);
        return true;
    }

    void process (float* const* io, int numCh, const float* scL, const float* scR, int n,
                  const Settings& s, const HostTime& host, const MidiHit* midi, int numMidi, const ScopeOut& scope)
    {
        for (auto& sl : slots)
            if (sl.ready.load (std::memory_order_acquire)) { sl.active = sl.pending; sl.ready.store (false, std::memory_order_release); }
        if (pendingMapReady.load (std::memory_order_acquire)) { activeMap.swap (pendingMap); activeBass.kind.swap (pendingBass.kind); activeBass.clash.swap (pendingBass.clash); pendingMapReady.store (false, std::memory_order_release); mapCursor = 0; mapLastT = -1e9; }
        if (relearnRequest.exchange (0) != 0) relearn();
        updateBands (s);
        const bool haveSc = scL != nullptr;
        hasSidechain.store (haveSc, std::memory_order_relaxed);

        // ---- per-block constants
        const double attack = std::max (0.0, s.attackMs) * 0.001 * fs;
        const double curveK = std::exp2 (-2.0 * s.curve);
        const double thr = std::pow (10.0, s.threshDb / 20.0);
        const double rel5 = std::exp (-1.0 / (0.005 * fs)), relSlow = std::exp (-1.0 / (0.08 * fs)), attSlow = std::exp (-1.0 / (0.015 * fs));
        const double relLow = std::exp (-1.0 / (0.010 * fs));
        const int holdoff = (int) (0.05 * fs);
        const double refDecay = std::exp (-1.0 / (3.0 * fs));
        const double duckAvgCoef = std::exp (-1.0 / (1.5 * fs));
        const double lenAudio = std::max (0.02, (s.fit && fitMs > 0 ? fitMs : s.lengthMs) * 0.001) * fs;
        const bool sync = s.trigger == Trigger::Sync;
        const bool mapMode = s.trigger == Trigger::Map;
        const bool rm = s.trigger == Trigger::Rm;
        autoActive.store (mapMode && ! activeBass.kind.empty() && s.bassMode == 0, std::memory_order_relaxed);
        if (! mapMode) cur = &slots[0].active;
        const double spb = fs * 60.0 / std::max (1.0, host.bpm);                 // samples per beat
        const double barBeats = std::max (1.0, host.barBeats);
        cycleSamples = sync ? s.rateBeats * spb : mapMode ? mapLen : lenAudio;
        const bool rotate = s.phase != PhaseMode::Off;
        if (rotate) phaseA0 = ch[0].quad.phaseA (fs, f0Est);
        lastHit = -1;
        const double outGain = std::pow (10.0, s.gainDb / 20.0);
        // RM constants
        const double rmCeil = std::pow (10.0, s.rmCeilDb / 20.0), rmFloor = std::pow (10.0, -std::max (0.0, s.rmRangeDb) / 20.0);
        const int rmHoldN = (int) std::lround (std::max (0.0, s.rmHoldMs) * 0.001 * fs);
        const double rmRel = s.rmReleaseMs <= 0.0 ? 1.0 : 1.0 - std::exp (-1.0 / (s.rmReleaseMs * 0.001 * fs));
        double rmMin = 1.0;

        // groove lock: predicted hits inside this block (AUDIO trigger, host playing)
        int predictAt = -1;
        const bool grooveOn = s.groove && s.trigger == Trigger::Audio && host.valid && host.playing;
        if (grooveOn) predictAt = planPrediction (host, n, spb, barBeats, attack);

        // MAP: the hits that start in this block, fired early by the attack so the duck is down when they land
        if (mapMode) planMap (s, host, n, attack);
        else mapNow.store (-1.0f, std::memory_order_relaxed);

        int mi = 0, mk = 0;
        for (int i = 0; i < n; ++i)
        {
            // ---------------- sidechain detector
            double kl = 0.0;
            if (haveSc)
            {
                const double sc = 0.5 * (scL[i] + (scR ? scR[i] : scL[i]));
                kl = scLp2.process (scLp1.process (scHp.process (sc)));
                const double det = std::abs (s.listenLows ? kl : sc);
                fastEnv = std::max (det, fastEnv * rel5);
                slowEnv = det > slowEnv ? slowEnv * attSlow + det * (1 - attSlow) : slowEnv * relSlow + det * (1 - relSlow);
                lowEnv = std::max (std::abs (kl), lowEnv * relLow);
            }
            if (sinceHit < (1 << 30)) ++sinceHit;
            ++clock;
            refPeak *= refDecay;

            bool hit = false, predicted = false; double hitVel = -1.0;

            // ---------------- triggers
            if (s.trigger == Trigger::Midi)
            {
                while (mi < numMidi && midi[mi].offset <= i) { hit = true; hitVel = std::max (hitVel, (double) midi[mi].velocity); ++mi; }
            }
            else if (mapMode)
            {
                while (mk < nMapFire && mapFire[mk].offset <= i)
                {
                    const auto& f = mapFire[mk++];
                    // a softer hit never cuts short a deeper duck that is still on its way back up
                    const auto& nt = *f.tab;
                    const double now = running ? mapAmt * (1.0 - cur->at (x)) : 0.0;
                    if (now > f.amount * (1.0 - nt.lowest) + 1e-6) continue;
                    hit = true; mapAmt = f.amount; mapLen = f.lengthSamples; cycleSamples = mapLen; cur = &nt; curType = f.type; lastHitKind = f.kind;
                    shownTable.store (&nt, std::memory_order_relaxed);
                }
                // out-of-step check: the kicks the map expects against the kicks on the sidechain
                if (haveSc)
                {
                    const bool onset = fastEnv >= thr && fastEnv >= 0.1 * refPeak && fastEnv > 2.0 * slowEnv && sinceHit > holdoff;
                    if (onset) { sinceHit = 0; refPeak = std::max (refPeak, fastEnv); checkKick (clock); }
                    else if (sinceHit < (int) (0.02 * fs)) refPeak = std::max (refPeak, fastEnv);     // the kick's peak
                }
                if (haveSc)
                {
                    while (mk2 < nMapKick && mapKick[mk2] <= i) expectKick (clock, mk2++);
                    ageKicks (clock);
                }
            }
            else if (s.trigger == Trigger::Audio || rm)
            {
                // (RM: the hits only feed the display and PHASE; the duck itself is worked out sample by sample below)
                // a hit: above THRESHOLD, a sudden rise, and within 20 dB of the recent kicks (so snare thumps and
                // bleed on the same bus are ignored at any send level)
                const bool onset = haveSc && fastEnv >= thr && fastEnv >= 0.1 * refPeak && fastEnv > 2.0 * slowEnv && sinceHit > holdoff;
                if (i == predictAt) { hit = predicted = true; predictedAt = 0; predictedConfirm = false; }
                if (onset)
                {
                    if (predictedAt >= 0 && predictedAt < (int) (0.035 * fs)) { predictedConfirm = true; sinceHit = 0; hitPeak = fastEnv; }   // the kick we predicted
                    else hit = true;
                    learnHit (host, i, spb, barBeats);
                }
                if (predictedAt >= 0)
                {
                    ++predictedAt;
                    if (! predictedConfirm && predictedAt > (int) (0.035 * fs)) { cancelCycle (attack); predictedAt = -1; }  // no kick came: let go
                    else if (predictedConfirm && predictedAt > (int) (0.035 * fs)) predictedAt = -1;
                }
            }
            // auto offset (SYNC) learns from sidechain onsets
            if (sync && s.autoOffset && haveSc && host.valid && host.playing && fastEnv >= thr && fastEnv >= 0.1 * refPeak && fastEnv > 2.0 * slowEnv && sinceHit > holdoff)
            {
                sinceHit = 0;
                const double ppqNow = host.ppq + i / spb;
                const double ph = ppqNow / s.rateBeats - std::floor (ppqNow / s.rateBeats);
                offCos = 0.8 * offCos + 0.2 * std::cos (2 * kPi * ph); offSin = 0.8 * offSin + 0.2 * std::sin (2 * kPi * ph); ++offCount;
            }

            // ---------------- playhead
            if (sync)
            {
                if (host.valid && host.playing)
                {
                    double off = s.offset;
                    if (s.autoOffset && offCount >= 3 && std::hypot (offCos, offSin) > 0.5) off += std::atan2 (offSin, offCos) / (2 * kPi);
                    const double pos = (host.ppq + i / spb) / s.rateBeats - off;
                    const double d = s.swing / 3.0;
                    auto cycleAt = [d] (double p, double& xf)
                    {
                        const int64_t idx = (int64_t) std::floor (p);
                        const double f = p - (double) idx;
                        int64_t cyc = idx; xf = f;
                        if (d > 0 && (idx & 1)) { if (f < d) { cyc = idx - 1; xf = 1.0; } else xf = (f - d) / (1.0 - d); }   // odd cycles start late
                        return cyc;
                    };
                    double xf = 0.0, xl = 0.0;
                    int64_t cyc = cycleAt (pos, xf);
                    // the drop starts one ATTACK early, so the gain is already down on the beat (no click, no kick let through):
                    // in that lead-in the next cycle holds at its start
                    const double leadCycles = (attack + 0.0005 * fs) / std::max (1.0, s.rateBeats * spb);
                    const int64_t cycLead = cycleAt (pos + leadCycles, xl);
                    if (cycLead > cyc) { cyc = cycLead; xf = 0.0; }
                    // the host can step back a hair between blocks: never re-enter a cycle already left
                    if (cycleIndex != INT64_MIN && cyc == cycleIndex - 1 && (double) cycleIndex - (pos + leadCycles) < 0.02) { cyc = cycleIndex; xf = 0.0; }
                    if (cyc != cycleIndex) { if (cycleIndex != INT64_MIN) startRamp (attack); cycleIndex = cyc; lastHit = i; startPhaseAccum(); }
                    x = xf; running = true;
                }
                else
                {
                    if (running) { startRamp (attack); running = false; }
                    x = 1.0; cycleIndex = INT64_MIN;
                }
            }
            else
            {
                if (hit)
                {
                    startRamp (attack); x = 0.0; running = true;
                    lastHit = i;
                    if (! mapMode) lastHitKind = 0;
                    if (mapMode) startPhaseAccum();
                    else
                    {
                        if (! predicted) sinceHit = 0;                   // a predicted hit leaves the detector armed for the real kick
                        startHitMeasure (hitVel);
                    }
                }
                else if (running) { x += 1.0 / std::max (1.0, cycleSamples); if (x >= 1.0) { x = 1.0; } }
            }

            // ---------------- dynamic depth (AUDIO: level of this hit vs recent hits; MIDI: velocity)
            if (measuring)
            {
                if (s.trigger == Trigger::Audio)
                {
                    hitPeak = std::max (hitPeak, fastEnv);
                    if (sinceHit > (int) (0.02 * fs)) { refPeak = std::max (refPeak, hitPeak); lastHitPeak = hitPeak; }
                }
                trackFit (s, sinceHit);
            }
            double& scale = curScale; scale = 1.0;
            if (mapMode) scale = mapAmt;
            else if (s.dynamic > 0 && s.trigger != Trigger::Sync)
            {
                double r = 1.0;
                if (s.trigger == Trigger::Audio) r = refPeak > 1e-9 ? std::min (1.0, std::max (hitPeak, 1e-9) / std::max (refPeak, hitPeak)) : 1.0;
                else r = hitScale;
                scale = 1.0 - s.dynamic * (1.0 - r);
            }

            // ---------------- gain
            const bool live = sync ? (host.valid && host.playing) : running;
            double sv = live ? cur->at (x) : (sync || ! everTriggered ? 1.0 : cur->at (1.0));
            if (curveK != 1.0 && sv > 0 && sv < 1) sv = std::pow (sv, curveK);
            const double a = std::clamp (s.depth, 0.0, 1.0) * scale * std::clamp (s.mix, 0.0, 1.0);
            const double target = sv;                                  // shape level (0..1.25)
            double shapeNow = target;
            if (rampPos < rampLen) { const double u = (double) rampPos / rampLen; shapeNow = gStart + (target - gStart) * (0.5 - 0.5 * std::cos (kPi * u)); ++rampPos; }
            gCur = shapeNow;
            double gIn = 1.0 - a * (s.split ? s.inside : 1.0) * (1.0 - gCur);
            double gOut = s.split ? 1.0 - a * s.above * (1.0 - gCur) : gIn;
            if (rm) { gIn = rmG; gOut = s.split ? 1.0 - s.above * (1.0 - rmG) : rmG; }   // for PHASE's glide; set again below

            // ---------------- phase: theta (applied) moves freely only while the bass is ducked
            double thetaWant = 0.0;
            if (rotate)
            {
                const double rel = s.phase == PhaseMode::Auto ? thetaAuto : s.rotateDeg * kPi / 180.0;
                thetaWant = (s.phase == PhaseMode::Auto ? rel : rel - phaseA0) + (s.polarity ? kPi : 0.0);   // chain B lags A by 90 deg: out = x turned by (theta + phiA)
                // MANUAL follows the knob quickly; AUTO jumps in the duck's trough and otherwise glides at 180 deg/s
                const double step = s.phase == PhaseMode::Manual ? (720.0 * kPi / 180.0) / fs
                                  : gIn < 0.12 ? 0.02 : (180.0 * kPi / 180.0) / fs;
                const double d = wrapPi (thetaWant - thetaCur);
                thetaCur = thetaFresh ? thetaWant : wrapPi (thetaCur + std::clamp (d, -step, step));
                thetaFresh = false;
            }
            else thetaFresh = true;
            const double ct = std::cos (thetaCur), st = std::sin (thetaCur);

            // ---------------- audio
            // each channel in two parts: "band" gets gIn, "rest" (outside the band) gets gOut: y = band * gIn + rest * gOut
            double monoBefore = 0, monoAfter = 0, monoA = 0, monoB = 0, monoIn = 0;
            double bandC[2] {}, restC[2] {};
            const int nCh = std::min (numCh, 2);
            for (int c = 0; c < nCh; ++c)
            {
                auto& st2 = ch[(size_t) c];
                const double in = io[c][i];
                monoIn += in;
                double yBefore;
                if (! s.split)
                {
                    double v = in;
                    if (rotate) { double qa, qb; st2.quad.process (in, qa, qb); v = qa * ct - qb * st; monoA += qa; monoB += qb; }
                    yBefore = v; bandC[c] = v; restC[c] = 0.0;
                }
                else if (! rotate)
                {
                    // SPLIT without rebuilding the signal from crossover bands: the whole input is ducked to gIn, and the part
                    // outside the band (high-passed above it, low-passed below it) is given back up to gOut:
                    //   y = in * gIn + outside * (gOut - gIn) = (in - outside) * gIn + outside * gOut
                    // Between hits gIn = gOut = 1 and the bass comes out exactly as it went in (no phase shift, no delay,
                    // no level change). At the deepest point only the outside part is left, just as with crossover bands.
                    yBefore = in;
                    double outside = st2.xHi.highpass (in);
                    if (useLo) outside += st2.xLo.lowpass (in);
                    bandC[c] = in - outside; restC[c] = outside;
                }
                else
                {
                    // PHASE turns the band only, so it needs the band on its own: crossover bands (they add up to an all-pass)
                    double low, high; st2.xHi.process (in, low, high);
                    double below = 0.0, band = low;
                    if (useLo) { double b2; st2.xLo.process (low, below, b2); band = b2; high = st2.apLo.process (high); }
                    { double qa, qb; st2.quad.process (band, qa, qb); band = qa * ct - qb * st; monoA += qa; monoB += qb; }
                    yBefore = below + band + high;
                    bandC[c] = band; restC[c] = below + high;
                }
                monoBefore += yBefore;
            }

            // ---------------- RM: per sample, the bass may only fill what the kick leaves under CEILING:
            //   |kick| + (|band| + ABOVE * |rest|) * g <= CEILING   (output GAIN included)
            // In SPLIT only the band (and ABOVE's share of the rest) is held against the kick; the rest isn't ducked, so it isn't counted.
            // down at once, then HOLD, then back up at the SMOOTH (release) rate. The fast gain moves are the sound of it:
            // the bass is modulated by the kick's own waveform (hence "ring mod").
            if (rm)
            {
                double want = 1.0;
                if (haveSc)
                {
                    const double kAbs = std::max (std::abs ((double) scL[i]), scR ? std::abs ((double) scR[i]) : 0.0);
                    const double ab = s.split ? std::clamp (s.above, 0.0, 1.0) : 0.0;
                    for (int c = 0; c < nCh; ++c)
                    {
                        const double B = std::abs (bandC[c]) * outGain, Rr = std::abs (restC[c]) * outGain;
                        const double den = B + ab * Rr;
                        if (kAbs + den <= rmCeil) continue;
                        const double g = den > 1e-12 ? (rmCeil - kAbs) / den : 1.0;
                        want = std::min (want, g);
                    }
                }
                want = std::clamp (want, rmFloor, 1.0);
                if (want < rmG) { rmG = want; rmHoldLeft = rmHoldN; }
                else if (rmHoldLeft > 0) --rmHoldLeft;
                else rmG += (want - rmG) * rmRel;
                rmMin = std::min (rmMin, rmG);
                const double m = std::clamp (s.mix, 0.0, 1.0);
                gIn = 1.0 - m * (1.0 - rmG);
                gOut = s.split ? 1.0 - m * std::clamp (s.above, 0.0, 1.0) * (1.0 - rmG) : gIn;
            }
            duckPow = duckAvgCoef * duckPow + (1 - duckAvgCoef) * gIn * gIn;

            for (int c = 0; c < nCh; ++c)
            {
                const double y = bandC[c] * gIn + restC[c] * gOut;
                monoAfter += y;
                io[c][i] = (float) (y * outGain);
            }
            const double nC = std::max (1, std::min (numCh, 2));
            monoBefore /= nC; monoAfter /= nC; monoA /= nC; monoB /= nC; monoIn /= nC;

            // ---------------- phase measurement: kick lows against the (rotatable) bass while both sound
            if (rotate) estimateF0 (monoIn);
            if (rotate && accum && haveSc)
            {
                // the kick's low end went through the detector's filters, so the bass is measured through the same ones
                const double fa = mLpA2.process (mLpA1.process (mHpA.process (monoA)));
                const double fb = mLpB2.process (mLpB1.process (mHpB.process (monoB)));
                const double w = gIn;
                sa += kl * w * fa; sb += kl * w * fb;
                const double r = fa * ct - fb * st;
                skk += kl * kl * w; srr += r * r * w; skr += kl * r * w;
                if (++sinceAccum > (int) (0.4 * fs) || (! sync && running && x >= 1.0)) finishPhaseHit();
            }

            if (scope.kickLow) scope.kickLow[i] = (float) kl;
            if (scope.before) scope.before[i] = (float) monoBefore;
            if (scope.after) scope.after[i] = (float) monoAfter;
            if (scope.gain) scope.gain[i] = (float) gIn;
            if (scope.duckType) scope.duckType[i] = gIn < 0.98 ? (float) (mapMode ? curType + 1 : 1) : 0.0f;
        }

        // ---- display values
        fittedMs.store ((float) fitMs, std::memory_order_relaxed);
        autoOffsetFrac.store (offCount >= 3 ? (float) (std::atan2 (offSin, offCos) / (2 * kPi)) : 0.0f, std::memory_order_relaxed);
        thetaDeg.store ((float) ((s.phase == PhaseMode::Auto ? wrapPi (thetaAuto + phaseA0) : s.rotateDeg * kPi / 180.0) * 180.0 / kPi), std::memory_order_relaxed);
        avgDuckDb.store ((float) (10.0 * std::log10 (std::max (1e-9, duckPow))), std::memory_order_relaxed);
        f0.store ((float) f0Est, std::memory_order_relaxed);
        float gc = 0.0f; for (int k = 0; k < kSlots; ++k) gc = std::max (gc, (float) slotConf[(size_t) k]);
        grooveConf.store (gc, std::memory_order_relaxed);
        scLevel.store ((float) fastEnv, std::memory_order_relaxed);
        dynScale.store ((float) curScale, std::memory_order_relaxed);
        // the deepest since the editor last read it (it swaps in 0)
        if (rm) rmGrDb.store (std::min (rmGrDb.load (std::memory_order_relaxed), (float) (20.0 * std::log10 (std::max (1e-6, rmMin)))), std::memory_order_relaxed);
    }

private:
    struct Channel { Quadrature quad; XOver xHi, xLo; XAllpass apLo; };
    std::array<Channel, 2> ch;
    double fs = 48000.0;
    struct Slot { ShapeTable active, pending; std::atomic<bool> ready { false }; };
    std::array<Slot, kShapeSlots> slots;
    std::array<ShapeTable, 4> autoPool; int autoNext = 0;
    const ShapeTable* cur = &slots[0].active;                         // the shape running now (MAP: kick or snare)

    // playhead
    double x = 1.0, gCur = 1.0, gStart = 1.0;
    bool running = false, everTriggered = false;
    int rampPos = 0, rampLen = 0;
    int64_t cycleIndex = INT64_MIN;

    // detector / dynamics / fit
    Biquad scLp1, scLp2, scHp, tgtLp, mLpA1, mLpA2, mHpA, mLpB1, mLpB2, mHpB;
    double fastEnv = 0, slowEnv = 0, lowEnv = 0, hitPeak = 0, refPeak = 0, hitScale = 1.0, lastHitPeak = 0;
    int sinceHit = 1 << 30;
    bool measuring = false;
    std::array<double, 4> fitHist {}; int fitCount = 0; double fitMs = 0, fitLowPeak = 0; bool fitDone = true;

    // groove lock: 64 slots per bar
    static constexpr int kSlots = 64;
    std::array<double, kSlots> slotConf {}, slotPos {};
    std::array<bool, kSlots> slotSeen {}, slotFired {};
    int64_t lastBar = INT64_MIN;
    int predictedAt = -1; bool predictedConfirm = false;

    // auto offset (SYNC)
    double offCos = 0, offSin = 0; int offCount = 0;

    // RM
    double rmG = 1.0; int rmHoldLeft = 0;

    // phase
    double sa = 0, sb = 0, skk = 0, srr = 0, skr = 0, avgA = 0, avgB = 0, thetaAuto = 0, thetaCur = 0, lastThetaAuto = 1e9, phaseA0 = 0;
    int lockRun = 0, hitsLearned = 0; bool accum = false, thetaFresh = true;
    double zcLast = 0, zcPrev = 0; int zcCount = 0; double f0Est = 50.0;

    double duckPow = 1.0, curScale = 1.0;

    // MAP playback
    std::vector<rcmap::Hit> activeMap, pendingMap;
    BassMap activeBass, pendingBass;
    std::atomic<bool> pendingMapReady { false };
    size_t mapCursor = 0; double mapLastT = -1e9;
    double mapAmt = 1.0, mapLen = 0.25 * 48000.0;
    struct MapFire { int offset; double amount, lengthSamples; const ShapeTable* tab; int type; int kind; };
    int curType = 0;
    static constexpr int kMaxFire = 64;
    std::array<MapFire, kMaxFire> mapFire {}; int nMapFire = 0;
    std::array<int, kMaxFire> mapKick {}; int nMapKick = 0, mk2 = 0;
    int64_t clock = 0;
    std::array<int64_t, 8> expected {}; std::array<bool, 8> expMatched {}; int expHead = 0;
    double driftAvg = 0.0, inStepAvg = -1.0;

    void planMap (const Settings& s, const HostTime& h, int n, double attack)
    {
        nMapFire = nMapKick = mk2 = 0;
        if (! (h.playing && h.hasTime) || activeMap.empty())
        {
            if (running) { startRamp (attack); running = false; x = 1.0; }
            mapNow.store (h.hasTime ? (float) h.timeSec : -1.0f, std::memory_order_relaxed);
            mapLastT = -1e9;
            return;
        }
        const double lead = (attack + 0.001 * fs) / fs;                   // seconds early
        const double t0 = h.timeSec - s.mapOffsetMs * 0.001;              // the map's clock at sample 0 (OFFSET + = later)
        const double t1 = t0 + n / fs;
        mapNow.store ((float) h.timeSec, std::memory_order_relaxed);
        // a jump (loop, locate) re-seeks; normal playback walks on
        if (std::abs (t0 - mapLastT) > 0.05 || mapCursor > activeMap.size())
        {
            mapCursor = (size_t) (std::lower_bound (activeMap.begin(), activeMap.end(), t0 + lead,
                                                   [] (const rcmap::Hit& a, double v) { return a.t < v; }) - activeMap.begin());
            kickCursor = (size_t) (std::lower_bound (activeMap.begin(), activeMap.end(), t0,
                                                   [] (const rcmap::Hit& a, double v) { return a.t < v; }) - activeMap.begin());
        }
        mapLastT = t1;
        const double ghostLin = std::pow (10.0, -std::abs (s.ghostDb) / 20.0);
        while (mapCursor < activeMap.size() && activeMap[mapCursor].t - lead < t1 && nMapFire < kMaxFire)
        {
            const auto& e = activeMap[mapCursor++];
            if (! e.on) continue;
            double amt = std::clamp (s.mapAmount[e.type], 0.0, 1.0);
            const double lvl = std::pow (10.0, std::min (0.0f, e.rel) / 20.0);
            const bool ghostNote = lvl < ghostLin;
            if (ghostNote)                                                  // a ghost note
            {
                if (s.ghost == GhostMode::Ignore) continue;
                if (s.ghost == GhostMode::Scale) amt *= lvl / ghostLin * 0.5;   // half the depth at the ghost line, less below it
            }
            else if (s.dynamic > 0) amt *= 1.0 - s.dynamic * (1.0 - lvl);
            if (amt <= 0.001) continue;
            const size_t idx = mapCursor - 1;
            const bool bass = ! activeBass.kind.empty();
            const ShapeTable* tb = nullptr;
            double lenMs = 0.0;
            if (bass && s.bassMode == 0 && (e.type == rcmap::Kick || e.type == rcmap::Snare))
            {
                // AUTO: this hit's own duck, from how loud the bass is against it
                float r[rcbass::kFrames];
                const int nf = rcbass::autoCurve (&activeBass.clash[idx * (size_t) rcbass::kFrames], s.clearanceDb, s.recoveryMs, r);
                if (nf == 0) continue;                                      // the bass doesn't get in the way: no duck
                auto* t = &autoPool[(size_t) autoNext];
                if (t == cur) { autoNext = (autoNext + 1) % (int) autoPool.size(); t = &autoPool[(size_t) autoNext]; }
                autoNext = (autoNext + 1) % (int) autoPool.size();
                t->buildDb (r, nf);
                tb = t; lenMs = nf * rcbass::kFrameSec * 1000.0;
            }
            else
            {
                int slot = e.type == rcmap::Kick ? 0 : 1;                  // snares and rims have their own shape
                if (bass && e.type == rcmap::Kick)
                {
                    // SHAPES: one shape per kind of bass under the kick
                    const auto k = activeBass.kind[idx];
                    if (k == rcbass::None) continue;                        // no bass: nothing to duck
                    slot = k == rcbass::Pluck ? 2 : k == rcbass::Mid ? 3 : k == rcbass::Note ? 4 : 0;
                }
                else if (bass && e.type == rcmap::Snare)
                {
                    // no bass anywhere near the snare's band: nothing to duck
                    const int8_t* c = &activeBass.clash[idx * (size_t) rcbass::kFrames];
                    bool any = false;
                    for (int i = 0; i < rcbass::kFrames && ! any; ++i) any = c[i] != rcbass::kGone && c[i] > -40;
                    if (! any) continue;
                }
                tb = &slots[(size_t) slot].active;
                lenMs = s.fit ? std::clamp ((double) e.decayMs / std::max (0.05, (double) tb->releaseEnd), 40.0, 2000.0) : s.lengthMs;
            }
            mapFire[nMapFire++] = { std::clamp ((int) std::floor ((e.t - lead - t0) * fs), 0, n - 1), amt, std::max (0.02, lenMs * 0.001) * fs, tb, (int) e.type,
                                     ghostNote ? 3 : std::min ((int) e.type, 2) };
        }
        // kicks landing in this block (no lead) for the out-of-step check
        while (kickCursor < activeMap.size() && activeMap[kickCursor].t < t1 && nMapKick < kMaxFire)
        {
            const auto& e = activeMap[kickCursor++];
            if (e.type == rcmap::Kick && e.on && e.rel > -12.0f) mapKick[nMapKick++] = std::clamp ((int) std::floor ((e.t - t0) * fs), 0, n - 1);
        }
    }
    size_t kickCursor = 0;

    void expectKick (int64_t at, int)
    {
        expHead = (expHead + 1) % (int) expected.size();
        if (expected[(size_t) expHead] != 0 && ! expMatched[(size_t) expHead]) scoreKick (false, 0.0);   // dropped out unmatched
        expected[(size_t) expHead] = at; expMatched[(size_t) expHead] = false;
        if (pendingEarly != 0 && at - pendingEarly <= (int64_t) (0.06 * fs))  // the sidechain kick came first
        {
            expMatched[(size_t) expHead] = true; scoreKick (true, (double) (pendingEarly - at) / fs * 1000.0);
        }
        pendingEarly = 0;
    }
    void ageKicks (int64_t now)
    {
        const int64_t win = (int64_t) (0.06 * fs);
        for (size_t k = 0; k < expected.size(); ++k)
            if (expected[k] != 0 && ! expMatched[k] && now - expected[k] > win) { expMatched[k] = true; scoreKick (false, 0.0); }
    }
    void checkKick (int64_t at)
    {
        // the sidechain kick can come up to 60 ms before or after the one the map expects
        const int64_t win = (int64_t) (0.06 * fs);
        int best = -1; int64_t bestD = win + 1;
        for (size_t k = 0; k < expected.size(); ++k)
        {
            if (expected[k] == 0 || expMatched[k]) continue;
            const int64_t d = at - expected[k];
            if (std::llabs (d) < std::llabs (bestD)) { bestD = d; best = (int) k; }
        }
        if (best >= 0 && std::llabs (bestD) <= win) { expMatched[(size_t) best] = true; scoreKick (true, (double) bestD / fs * 1000.0); }
        else pendingEarly = at;                                            // maybe early: matched when the map's kick arrives
    }
    int64_t pendingEarly = 0;
    void scoreKick (bool matched, double errMs)
    {
        errMs -= 2.5;                                                      // the detector's own filters lag by about 2.5 ms
        if (matched) driftAvg = inStepAvg < 0 ? errMs : 0.7 * driftAvg + 0.3 * errMs;
        inStepAvg = inStepAvg < 0 ? (matched ? 1.0 : 0.0) : 0.8 * inStepAvg + 0.2 * (matched ? 1.0 : 0.0);
        mapDriftMs.store ((float) driftAvg, std::memory_order_relaxed);
        mapInStep.store ((float) inStepAvg, std::memory_order_relaxed);
    }
    double bandsFor = -1.0; bool useLo = false;

    void updateBands (const Settings& s)
    {
        const double key = s.lo * 7.0 + s.hi * 13.0 + (s.hard ? 1.0 : 0.0) + fs * 1e-3;
        if (key == bandsFor) return;
        bandsFor = key;
        const double hi = std::clamp (s.hi, 30.0, fs * 0.4), lo = std::clamp (s.lo, 10.0, hi * 0.8);
        useLo = lo > 21.0;
        for (auto& c : ch) { c.xHi.set (fs, hi, s.hard); c.xLo.set (fs, lo, s.hard); c.apLo.set (fs, lo, s.hard); }
    }

    void startRamp (double attackSamples)
    {
        gStart = gCur; rampLen = (int) std::round (attackSamples); rampPos = 0;
        if (rampLen < 1) rampPos = rampLen = 0;
        everTriggered = true;
    }
    void cancelCycle (double attackSamples) { startRamp (attackSamples); x = 1.0; running = false; }

    void startHitMeasure (double vel)
    {
        measuring = true; fitDone = false; fitLowPeak = 0;
        if (vel >= 0) hitScale = std::clamp (vel, 0.0, 1.0);
        if (vel < 0) hitPeak = predictedAt == 0 ? lastHitPeak : fastEnv;       // a predicted hit starts at the last kick's level
        startPhaseAccum();
    }
    void startPhaseAccum() { if (accum) finishPhaseHit(); accum = true; sinceAccum = 0; sa = sb = skk = srr = skr = 0; }
    int sinceAccum = 0;

    // FIT LENGTH: time for the kick's low end to fall 20 dB below its peak, divided by where the shape is back up
    void trackFit (const Settings& s, int since)
    {
        if (fitDone) return;
        if (since < (int) (0.03 * fs)) { fitLowPeak = std::max (fitLowPeak, lowEnv); return; }
        fitLowPeak = std::max (fitLowPeak, lowEnv);
        if (fitLowPeak > 1e-5 && lowEnv < fitLowPeak * 0.1)
        {
            const double t = since / fs * 1000.0;
            const double len = std::clamp (t / std::max (0.05, (double) slots[0].active.releaseEnd), 40.0, 2000.0);
            fitHist[(size_t) (fitCount++ % 4)] = len;
            auto h = fitHist; const int m = std::min (fitCount, 4);
            for (int p = 1; p < m; ++p) for (int q = p; q > 0 && h[(size_t) q] < h[(size_t) q - 1]; --q) std::swap (h[(size_t) q], h[(size_t) q - 1]);
            fitMs = m % 2 ? h[(size_t) (m / 2)] : 0.5 * (h[(size_t) (m / 2 - 1)] + h[(size_t) (m / 2)]);
            fitDone = true;
        }
        else if (since > (int) (2.0 * fs)) fitDone = true;
        (void) s;
    }

    // ---- groove lock
    void learnHit (const HostTime& h, int i, double spb, double barBeats)
    {
        if (! (h.valid && h.playing)) return;
        const double ppq = h.ppq + i / spb;
        const double inBar = ppq - std::floor (ppq / barBeats) * barBeats;
        const int k = std::clamp ((int) (inBar / barBeats * kSlots), 0, kSlots - 1);
        auto& c = slotConf[(size_t) k];
        slotPos[(size_t) k] = c < 0.05 ? inBar : slotPos[(size_t) k] + 0.3 * (inBar - slotPos[(size_t) k]);
        c += 0.35 * (1.0 - c);
        slotSeen[(size_t) k] = true;
    }
    int planPrediction (const HostTime& h, int n, double spb, double barBeats, double attack)
    {
        const int64_t bar = (int64_t) std::floor (h.ppq / barBeats);
        if (bar != lastBar)
        {
            if (lastBar != INT64_MIN && bar == lastBar + 1)
                for (int k = 0; k < kSlots; ++k) if (! slotSeen[(size_t) k]) slotConf[(size_t) k] *= 0.6;
            if (lastBar != INT64_MIN && bar != lastBar + 1) { /* jump: keep what was learned */ }
            slotSeen.fill (false); slotFired.fill (false); lastBar = bar;
        }
        const double lead = (attack + 0.003 * fs) / spb;                    // beats before the kick
        const double start = h.ppq, end = h.ppq + n / spb;
        for (int k = 0; k < kSlots; ++k)
        {
            if (slotConf[(size_t) k] < 0.75 || slotFired[(size_t) k]) continue;
            double t = (double) bar * barBeats + slotPos[(size_t) k] - lead;
            for (int w = 0; w < 2; ++w, t += barBeats)                         // this bar or (near its end) the next
                if (t >= start && t < end) { slotFired[(size_t) k] = true; return std::clamp ((int) ((t - start) * spb), 0, n - 1); }
        }
        return -1;
    }

    // ---- phase
    void relearn() { avgA = avgB = 0; thetaAuto = 0; lockRun = 0; hitsLearned = 0; lastThetaAuto = 1e9; phaseLocked.store (false); }
    void finishPhaseHit()
    {
        accum = false;
        const double norm = std::sqrt (sa * sa + sb * sb);
        if (norm < 1e-9 || skk < 1e-9) return;
        if (srr > 1e-12) match.store ((float) (skr / std::sqrt (skk * srr)), std::memory_order_relaxed);
        if (phaseLocked.load (std::memory_order_relaxed)) return;
        avgA = 0.7 * avgA + 0.3 * sa / norm; avgB = 0.7 * avgB + 0.3 * sb / norm;
        const double th = -std::atan2 (avgB, avgA);
        ++hitsLearned;
        if (std::abs (wrapPi (th - lastThetaAuto)) < 8.0 * kPi / 180.0) ++lockRun; else lockRun = 0;
        lastThetaAuto = th; thetaAuto = th;
        if ((lockRun >= 4 && hitsLearned >= 6) || hitsLearned >= 12) phaseLocked.store (true, std::memory_order_relaxed);   // a moving bassline settles on its average
    }
    // fundamental of the bass from zero crossings of its low end (for MANUAL: 0 degrees = the original phase there)
    void estimateF0 (double in)
    {
        const double v = tgtLp.process (in);
        ++zcCount;
        if (zcPrev < 0 && v >= 0 && std::abs (v) + std::abs (zcPrev) > 1e-6)
        {
            const double frac = zcPrev / (zcPrev - v);
            const double pos = zcCount - 1 + frac;
            const double period = pos - zcLast;
            if (period > fs / 400.0 && period < fs / 18.0) f0Est += 0.05 * (fs / period - f0Est);
            zcLast = pos;
        }
        if (zcCount > (1 << 30)) { zcCount -= 1 << 29; zcLast -= 1 << 29; }
        zcPrev = v;
    }
};
} // namespace rc
