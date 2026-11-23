#pragma once
// MAP + BASS: with a bounce of the bass bus as well as the drum bus, RECOIL knows which bass plays under every hit.
// Plain C++ (no JUCE), runs once off the audio thread.
//
// For every kick and snare in the map it measures, in that hit's own band (kick 30-150 Hz, snare 150-400 Hz),
// how loud the bass is against the drum over the next 600 ms, in 5 ms frames ("clash": bass dB minus drum dB).
// At play time AUTO turns that into a duck: just deep enough to keep the drum CLEARANCE dB above the bass, held while
// the drum is still there, coming back no faster than RECOVERY allows. No bass under a hit: no duck.
//
// It also sorts the bass under each kick into a kind, for SHAPES mode (one shape per kind):
//   NONE   nothing (or next to nothing) playing          -> no duck
//   NOTE   a bass note starts with the kick              -> its own shape (often a longer hold)
//   PLUCK  short: falls 12 dB within 250 ms
//   MID    most of its energy above 90 Hz (reese, mid bass)
//   SUB    a sustained sub or 808 (everything else)

#include "MapAnalyser.h"

namespace rcbass
{
enum Kind : uint8_t { None = 0, Sub = 1, Pluck = 2, Mid = 3, Note = 4 };
constexpr int kKinds = 5;
inline const char* kindName (int k) { static const char* n[] = { "NO BASS", "SUB", "PLUCK", "MID", "NOTE" }; return n[std::clamp (k, 0, 4)]; }

constexpr int kFrames = 120;                 // 120 x 5 ms = 600 ms after each hit
constexpr double kFrameSec = 0.005;
constexpr int8_t kGone = -128;               // the hit is over (or the next one started): nothing to protect
constexpr double kEnvHop = 0.02;             // the bass level kept for the MAP view: one value per 20 ms

struct Result
{
    std::vector<uint8_t> kind;               // per hit (hats and rims: None)
    std::vector<int8_t> clash;               // per hit, kFrames each: bass minus drum in dB, or kGone
    std::vector<uint8_t> env;                // bass level for the MAP view, dB + 100 (0 = -100 dB)
    int counts[kKinds] {};
};

namespace detail
{
    // RMS level in dB of y over [a, b)
    inline float levelDb (const std::vector<float>& y, size_t a, size_t b)
    {
        b = std::min (b, y.size());
        if (b <= a) return -120.0f;
        const double e = rcmap::detail::energy (y, a, b) / (double) (b - a);
        return (float) (10.0 * std::log10 (e + 1e-14));
    }
}

// drums and bass: mono at the same rate, both from the start of the song. hits: the drum map (sorted).
inline Result analyse (const std::vector<float>& drums, const std::vector<float>& bass, double fs,
                       const std::vector<rcmap::Hit>& hits, const std::function<void (float)>& progress = {})
{
    using rcmap::detail::band;
    using detail::levelDb;
    Result res;
    res.kind.assign (hits.size(), None);
    res.clash.assign (hits.size() * (size_t) kFrames, kGone);

    const auto dLow = band (drums, fs, 30.0, 150.0), dBody = band (drums, fs, 150.0, 400.0);
    if (progress) progress (0.25f);
    const auto bLow = band (bass, fs, 30.0, 150.0), bBody = band (bass, fs, 150.0, 400.0), bAll = band (bass, fs, 30.0, 500.0);
    const auto bSub = band (bass, fs, 25.0, 90.0), bUp = band (bass, fs, 90.0, 500.0);
    if (progress) progress (0.5f);

    // the bass's own loud level (90th percentile of 100 ms blocks that have something in them): "no bass" is relative to it
    float bassRef = -60.0f;
    {
        std::vector<float> lv;
        const size_t blk = (size_t) (0.1 * fs);
        for (size_t a = 0; a + blk <= bAll.size(); a += blk) { const float l = levelDb (bAll, a, a + blk); if (l > -80.0f) lv.push_back (l); }
        if (! lv.empty()) { std::sort (lv.begin(), lv.end()); bassRef = lv[(size_t) (0.9 * (double) (lv.size() - 1))]; }
    }

    // bass level for the view
    {
        const size_t hop = (size_t) (kEnvHop * fs);
        for (size_t a = 0; a + hop <= bAll.size(); a += hop)
            res.env.push_back ((uint8_t) std::clamp ((int) std::lround (levelDb (bAll, a, a + hop) + 100.0f), 0, 100));
    }

    const size_t fr = (size_t) std::lround (kFrameSec * fs);
    auto at = [&] (double t) { return (size_t) std::max (0.0, std::round (t * fs)); };
    for (size_t h = 0; h < hits.size(); ++h)
    {
        const auto& e = hits[h];
        if (progress && h % 64 == 0) progress (0.5f + 0.5f * (float) h / (float) std::max<size_t> (1, hits.size()));
        if (e.type != rcmap::Kick && e.type != rcmap::Snare) continue;
        const bool kick = e.type == rcmap::Kick;
        const auto& dB = kick ? dLow : dBody;
        const auto& bB = kick ? bLow : bBody;
        const size_t s0 = at (e.t);

        // the next kick or snare ends this one's frames (it brings its own duck)
        double tNext = e.t + kFrames * kFrameSec;
        for (size_t j = h + 1; j < hits.size() && hits[j].t < tNext; ++j)
            if (hits[j].type == rcmap::Kick || hits[j].type == rcmap::Snare) { tNext = hits[j].t; break; }

        // clash frames
        float drumPeak = -120.0f;
        float dLv[kFrames], bLv[kFrames];
        for (int i = 0; i < kFrames; ++i)
        {
            const size_t a = s0 + (size_t) i * fr;
            // 20 ms windows: longer than one cycle of a 50 Hz kick or bass, so a steady tone reads steady
            const size_t w = (size_t) std::lround (0.02 * fs);
            dLv[i] = levelDb (dB, a, a + w); bLv[i] = levelDb (bB, a, a + w);
            if (i < 8) drumPeak = std::max (drumPeak, dLv[i]);
        }
        int8_t* c = &res.clash[h * (size_t) kFrames];
        for (int i = 0; i < kFrames; ++i)
        {
            const double t = e.t + i * kFrameSec;
            if (t >= tNext || (i >= 2 && dLv[i] < drumPeak - 20.0f)) c[i] = kGone;          // the drum is over
            else if (bLv[i] < -90.0f) c[i] = -100;                                          // no bass here
            else c[i] = (int8_t) std::clamp ((int) std::lround (bLv[i] - dLv[i]), -100, 100);
        }

        // kind of bass under a kick
        if (kick)
        {
            const float lvl = levelDb (bAll, s0, s0 + (size_t) (0.1 * fs));
            if (lvl < -60.0f || lvl < bassRef - 30.0f) { res.kind[h] = None; res.counts[None]++; continue; }
            // a new note: the bass jumps up near the kick
            float rise = 0.0f;
            for (int i = -2; i <= 12; ++i)
            {
                const double t = e.t + i * kFrameSec;
                if (t < 0.06) continue;
                // windows of 25 ms: longer than one cycle of the lowest bass note, so a steady note reads steady
                const float now = levelDb (bAll, at (t), at (t + 0.025)), before = levelDb (bAll, at (t - 0.055), at (t - 0.03));
                rise = std::max (rise, now - before);
            }
            const float early = levelDb (bAll, at (e.t + 0.02), at (e.t + 0.05)), late = levelDb (bAll, at (e.t + 0.25), at (e.t + 0.28));
            const float lo = levelDb (bSub, s0, s0 + (size_t) (0.15 * fs)), mid = levelDb (bUp, s0, s0 + (size_t) (0.15 * fs));
            uint8_t k = Sub;
            if (rise > 6.0f) k = Note;
            else if (late < early - 12.0f) k = Pluck;
            else if (mid > lo + 3.0f) k = Mid;
            res.kind[h] = k; res.counts[k]++;
        }
    }
    if (progress) progress (1.0f);
    return res;
}

// AUTO at play time: the duck in dB per 5 ms frame. Returns how many frames it lasts (0: no duck needed).
// clearance: how far (dB) the drum should stand above the bass. recoveryMs: the fastest it comes back 20 dB.
inline int autoCurve (const int8_t* c, double clearance, double recoveryMs, float* r)
{
    float raw[kFrames + 1];
    for (int i = 0; i < kFrames; ++i)
        raw[i] = c[i] == kGone ? 0.0f : (float) std::clamp ((double) c[i] + clearance, 0.0, 48.0);
    raw[kFrames] = 0.0f;
    // one depth per hit: enough for the drum to stay CLEARANCE above the bass all the way to the end of its tail
    // (until it has faded 20 dB), held for that long. Following it frame by frame would dig deeper as the tail fades.
    int last = -1; float depth = 0.0f;
    for (int i = 0; i < kFrames; ++i)
    {
        if (c[i] == kGone) break;
        last = i;
        depth = std::max (depth, raw[i]);
    }
    for (int i = 0; i < kFrames; ++i) raw[i] = i <= last ? depth : 0.0f;
    const float step = (float) (20.0 * kFrameSec * 1000.0 / std::max (5.0, recoveryMs));   // dB per frame on the way back
    float s = 0.0f, mx = 0.0f; int end = 0;
    for (int i = 0; i < kFrames; ++i)
    {
        const float want = std::max (raw[i], raw[i + 1]);                // one frame early: down before the drum peaks
        s = std::max (want, s - step);
        r[i] = s; mx = std::max (mx, s);
        if (s > 0.25f) end = i + 1;
    }
    if (mx < 1.0f) return 0;                                              // under 1 dB: not worth a duck
    // the return to 0 dB after the last frame
    while (end < kFrames && r[end - 1] > 0.25f) { r[end] = std::max (0.0f, r[end - 1] - step); ++end; }
    return std::max (2, end);
}

// ---- compact text for the plugin state
inline std::string toHex (const std::vector<uint8_t>& v)
{
    static const char* d = "0123456789abcdef";
    std::string s; s.resize (v.size() * 2);
    for (size_t i = 0; i < v.size(); ++i) { s[2 * i] = d[v[i] >> 4]; s[2 * i + 1] = d[v[i] & 15]; }
    return s;
}
inline std::vector<uint8_t> fromHex (const std::string& s)
{
    auto h = [] (char c) { return c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'; };
    std::vector<uint8_t> v (s.size() / 2);
    for (size_t i = 0; i < v.size(); ++i) v[i] = (uint8_t) (h (s[2 * i]) * 16 + h (s[2 * i + 1]));
    return v;
}
} // namespace rcbass
