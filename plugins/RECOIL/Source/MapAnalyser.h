#pragma once
// MAP: analyses a bounced drum bus (the whole song, from its first bar) into a list of hits.
// Plain C++ (no JUCE) so it can be tested offline. Runs once, off the audio thread.
//
//   1. band envelopes (lows 30-150 Hz, body 150-400, mids 400-2k, highs 2k+) on a 64-sample hop
//   2. onset strength = summed dB rise per band over ~3 ms, so soft ghost notes count as much as loud hits
//   3. peaks of that, with a local adaptive threshold and an absolute floor; hits closer than 30 ms merge (flams)
//   4. each hit's exact start: where the full-band level reaches 15 % of the hit's peak (sub-ms)
//   5. its sound in the first 50 ms: share of energy per band, how long it rings, how loud it is
//   6. type: KICK (low-heavy), SNARE (body plus crack), RIM (short click in the mids), HAT (highs only)
//   7. level against the main hits of its own type, so ghost notes are known (e.g. -18 dB from the backbeat)

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <cstdio>

namespace rcmap
{
enum Type : uint8_t { Kick = 0, Snare = 1, Rim = 2, Hat = 3 };
constexpr int kTypes = 4;

struct Hit
{
    double t = 0.0;          // seconds from the start of the song
    uint8_t type = Kick;
    float rel = 0.0f;        // dB against the main hits of its type (0 = a full backbeat; -18 = a ghost)
    float decayMs = 200.0f;  // how long it rings (its main band falls 20 dB)
    bool on = true;          // the user can switch single hits off
};

struct Stats { int count[kTypes] {}; int ghosts = 0; double seconds = 0.0; };

namespace detail
{
    struct BQ
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        static BQ make (double fs, double fc, double q, bool high)
        {
            const double w = 2.0 * 3.14159265358979323846 * std::min (fc, fs * 0.45) / fs, cw = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
            BQ f;
            if (high) { f.b0 = (1.0 + cw) * 0.5 / a0; f.b1 = -(1.0 + cw) / a0; }
            else      { f.b0 = (1.0 - cw) * 0.5 / a0; f.b1 = (1.0 - cw) / a0; }
            f.b2 = f.b0; f.a1 = -2.0 * cw / a0; f.a2 = (1.0 - al) / a0;
            return f;
        }
        inline double run (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };

    // x filtered by a chain of biquads (lowpass/highpass pairs make a band)
    inline std::vector<float> band (const std::vector<float>& x, double fs, double lo, double hi)
    {
        std::vector<BQ> ch;
        if (lo > 0) { ch.push_back (BQ::make (fs, lo, 0.7071, true)); ch.push_back (BQ::make (fs, lo, 0.7071, true)); }
        if (hi > 0) { ch.push_back (BQ::make (fs, hi, 0.7071, false)); ch.push_back (BQ::make (fs, hi, 0.7071, false)); }
        std::vector<float> y (x.size());
        for (size_t i = 0; i < x.size(); ++i) { double v = x[i]; for (auto& f : ch) v = f.run (v); y[i] = (float) v; }
        return y;
    }

    inline double energy (const std::vector<float>& y, size_t a, size_t b)
    {
        double e = 0; b = std::min (b, y.size());
        for (size_t i = a; i < b; ++i) e += (double) y[i] * y[i];
        return e;
    }

    // time (ms) from 'a' until |y| (smoothed) has fallen 20 dB below the hit's own peak (found in its first
    // 30 ms). Stops at 'end' (the next hit): a hit still ringing there counts as ringing that long.
    inline float decay20 (const std::vector<float>& y, size_t a, size_t end, double fs, double maxMs)
    {
        end = std::min ({ y.size(), end, a + (size_t) (maxMs * 0.001 * fs) });
        if (end <= a + 2) return 0.0f;
        const double rel = std::exp (-1.0 / (0.004 * fs));
        const size_t pkEnd = std::min (end, a + (size_t) (0.030 * fs));
        double env = 0, pk = 0;
        for (size_t i = a; i < end; ++i)
        {
            env = std::max ((double) std::abs (y[i]), env * rel);
            if (i < pkEnd) { pk = std::max (pk, env); continue; }
            if (pk > 0 && env < pk * 0.1) return (float) ((double) (i - a) * 1000.0 / fs);
        }
        return (float) ((double) (end - a) * 1000.0 / fs);
    }
}

// progress: 0..1 (may be null). Returns hits sorted by time.
inline std::vector<Hit> analyse (const std::vector<float>& mono, double fs, Stats* stats = nullptr,
                                 const std::function<void (float)>& progress = {})
{
    using namespace detail;
    std::vector<Hit> hits;
    if (mono.size() < (size_t) (fs * 0.2)) return hits;
    if (progress) progress (0.02f);

    // ---- bands at full rate (also used for the features)
    const auto low = band (mono, fs, 30.0, 150.0);   if (progress) progress (0.15f);
    const auto body = band (mono, fs, 150.0, 400.0); if (progress) progress (0.28f);
    const auto mid = band (mono, fs, 400.0, 2000.0); if (progress) progress (0.41f);
    const auto high = band (mono, fs, 2000.0, 0.0);  if (progress) progress (0.54f);
    const std::vector<float>* bands[4] = { &low, &body, &mid, &high };

    // ---- envelopes on a 64-sample hop (peak per hop, 10 ms release)
    const int hop = 64;
    const size_t frames = mono.size() / hop;
    std::vector<float> env[4];
    float gMax = 1e-9f;
    for (int b = 0; b < 4; ++b)
    {
        env[b].assign (frames, 0.0f);
        const double rel = std::exp (-(double) hop / (0.010 * fs));
        double e = 0;
        for (size_t f = 0; f < frames; ++f)
        {
            float pk = 0; for (int k = 0; k < hop; ++k) pk = std::max (pk, std::abs ((*bands[b])[f * hop + (size_t) k]));
            e = std::max ((double) pk, e * rel);
            env[b][f] = (float) e;
            gMax = std::max (gMax, (float) e);
        }
    }
    // full-band envelope on the same hop (for the level-jump test), slow enough to ignore the dips between low cycles
    std::vector<float> full (frames, 0.0f);
    {
        const double rel = std::exp (-(double) hop / (0.040 * fs));       // slow: rides over a kick's low cycles
        double e = 0;
        for (size_t f = 0; f < frames; ++f)
        {
            float pk = 0; for (int k = 0; k < hop; ++k) pk = std::max (pk, std::abs (mono[f * hop + (size_t) k]));
            e = std::max ((double) pk, e * rel); full[f] = (float) e;
        }
    }
    if (progress) progress (0.66f);

    // ---- onset strength: summed dB rise over ~3 ms, ignoring everything 60 dB below the loudest moment
    const int lag = std::max (1, (int) std::round (0.003 * fs / hop));
    const float floorLin = gMax * 0.001f;
    std::vector<float> odf (frames, 0.0f);
    for (size_t f = (size_t) lag; f < frames; ++f)
    {
        float s = 0;
        for (int b = 0; b < 4; ++b)
        {
            const float now = std::max (env[b][f], floorLin), then = std::max (env[b][f - (size_t) lag], floorLin);
            s += std::max (0.0f, 20.0f * std::log10 (now / then));
        }
        odf[f] = s;
    }

    // ---- peaks: local maximum within 20 ms, above an adaptive threshold (local median + 6) and 8 dB
    const int win = (int) std::round (0.020 * fs / hop), med = (int) std::round (0.25 * fs / hop);
    std::vector<size_t> peaks;
    std::vector<float> tmp;
    for (size_t f = (size_t) lag; f < frames; ++f)
    {
        if (odf[f] < 8.0f) continue;
        bool isMax = true;
        for (int k = -win; k <= win && isMax; ++k)
        {
            const long j = (long) f + k;
            if (j < 0 || j >= (long) frames || k == 0) continue;
            if (odf[(size_t) j] > odf[f] || (odf[(size_t) j] == odf[f] && k < 0)) isMax = false;
        }
        if (! isMax) continue;
        // a real hit lifts the level well above the 20 ms before it; noise in a decaying tail only wobbles
        {
            float before = 1e9f, after = 0.0f;
            for (long j = std::max (0L, (long) f - 2 * win); j < (long) f - lag; ++j) before = std::min (before, full[(size_t) j]);
            for (size_t j = f; j < std::min (frames, f + (size_t) win / 2 + 1); ++j) after = std::max (after, full[j]);
            if (before > 1e8f || after < before * 1.8f) continue;            // under ~5 dB: not a new hit
           #ifdef RCMAP_DEBUG
            std::fprintf (stderr, "P %.4f odf %.1f before %.1f after %.1f\n", (double) (f * hop) / fs, odf[f], 20 * std::log10 (before + 1e-9f), 20 * std::log10 (after + 1e-9f));
           #endif
        }
        tmp.clear();
        for (long j = std::max (0L, (long) f - med); j < std::min ((long) frames, (long) f + med); j += 2) tmp.push_back (odf[(size_t) j]);
        std::nth_element (tmp.begin(), tmp.begin() + (long) tmp.size() / 2, tmp.end());
        if (odf[f] < tmp[tmp.size() / 2] + 6.0f) continue;
        if (! peaks.empty() && (f - peaks.back()) * (size_t) hop < (size_t) (0.030 * fs)) continue;   // flam: one hit
        peaks.push_back (f);
    }
    if (progress) progress (0.75f);

    // ---- each hit: exact start, sound, level, ring time
    struct Feat { double t; float rLow, rBody, rMid, rHigh, peakDb, decayFull, decayLow, decayBody; };
    std::vector<Feat> fe;
    const double rel1 = std::exp (-1.0 / (0.001 * fs));
    for (size_t pi = 0; pi < peaks.size(); ++pi)
    {
        const size_t c = peaks[pi] * (size_t) hop;
        const size_t a = c > (size_t) (0.015 * fs) ? c - (size_t) (0.015 * fs) : 0;
        const size_t b = std::min (mono.size(), c + (size_t) (0.015 * fs));
        std::vector<double> e (b - a); double en = 0, pk = 0;
        for (size_t i = a; i < b; ++i) { en = std::max ((double) std::abs (mono[i]), en * rel1); e[i - a] = en; }
        for (size_t i = c - a; i < e.size(); ++i) pk = std::max (pk, e[i]);
        size_t on = c - a;
        while (on > 0 && e[on - 1] >= pk * 0.15) --on;
        while (on + 1 < e.size() && e[on] < pk * 0.15) ++on;
        const size_t s0 = a + on, s1 = s0 + (size_t) (0.050 * fs);
        // only count what is new in each band: a hat landing inside a ringing kick must not look like a kick.
        // The 10 ms before the hit, stretched over the 50 ms window (at half strength for the decay), is carried over.
        const size_t p0 = s0 > (size_t) (0.010 * fs) ? s0 - (size_t) (0.010 * fs) : 0;
        auto fresh = [&] (const std::vector<float>& y)
        {
            const double now = energy (y, s0, s1);
            const double carried = p0 < s0 ? energy (y, p0, s0) * 2.5 : 0.0;
            return std::max (now * 0.02, now - carried);
        };
        const double el = fresh (low), eb = fresh (body), em = fresh (mid), eh = fresh (high);
        const double tot = std::max (1e-12, el + eb + em + eh);
        float pkAbs = 0; for (size_t i = s0; i < std::min (mono.size(), s0 + (size_t) (0.030 * fs)); ++i) pkAbs = std::max (pkAbs, std::abs (mono[i]));
        Feat f;
        f.t = (double) s0 / fs;
        f.rLow = (float) (el / tot); f.rBody = (float) (eb / tot); f.rMid = (float) (em / tot); f.rHigh = (float) (eh / tot);
        f.peakDb = 20.0f * std::log10 (std::max (pkAbs, 1e-9f));
        const size_t next = pi + 1 < peaks.size() ? peaks[pi + 1] * (size_t) hop - (size_t) (0.004 * fs) : mono.size();
        f.decayFull = decay20 (mono, s0, next, fs, 400.0);
        f.decayLow = decay20 (low, s0, mono.size(), fs, 1500.0);      // a kick's low end keeps ringing under the hats
        f.decayBody = decay20 (body, s0, next, fs, 800.0);
        fe.push_back (f);
    }
    if (progress) progress (0.88f);

    // ---- type. Kicks: split the share of lows into two groups (2-means), the lower-pitched group is the kick
    double cLo = 0.1, cHi = 0.7;
    for (int it = 0; it < 20; ++it)
    {
        double sLo = 0, sHi = 0; int nLo = 0, nHi = 0;
        for (auto& f : fe) { if (std::abs (f.rLow - cLo) <= std::abs (f.rLow - cHi)) { sLo += f.rLow; ++nLo; } else { sHi += f.rLow; ++nHi; } }
        if (nLo) cLo = sLo / nLo;
        if (nHi) cHi = sHi / nHi;
    }
    const double kickSplit = (cHi - cLo > 0.25) ? 0.5 * (cLo + cHi) : 0.45;
    for (auto& f : fe)
    {
        Hit h; h.t = f.t;
        // a hat over the tail of a kick: treble with next to no mids (a kick's click and a snare both have mids)
        if (f.rMid < 0.03f && f.rHigh > 0.25f && f.rBody < 0.2f) { h.type = Hat; h.decayMs = f.decayFull; }
        else if (f.rLow >= kickSplit) { h.type = Kick; h.decayMs = f.decayLow; }
        // almost all highs, no body: hats and cymbals, open or closed, however long they ring
        else if (f.rHigh >= 0.8f && f.rBody < 0.05f && f.rLow < 0.1f) { h.type = Hat; h.decayMs = f.decayFull; }
        // short hits without body are hats (highs only) or rims (a click with real mids); everything else with
        // body or a long noisy tail is a snare
        else if (f.decayFull < 60.0f && f.rBody < 0.03f && f.rMid < 0.15f) { h.type = Hat; h.decayMs = f.decayFull; }
        else if (f.decayFull < 45.0f && f.rBody < 0.06f && f.rMid >= 0.15f) { h.type = Rim; h.decayMs = f.decayFull; }
        else { h.type = Snare; h.decayMs = std::max (f.decayBody, f.decayFull); }
        h.rel = f.peakDb;
        hits.push_back (h);
       #ifdef RCMAP_DEBUG
        std::fprintf (stderr, "F %.4f low %.2f body %.2f mid %.2f high %.2f pk %.1f dF %.0f dL %.0f dB %.0f\n", f.t, f.rLow, f.rBody, f.rMid, f.rHigh, f.peakDb, f.decayFull, f.decayLow, f.decayBody);
       #endif
    }

    // ---- level against the main hits of each type (their 90th percentile)
    for (int ty = 0; ty < kTypes; ++ty)
    {
        std::vector<float> lv;
        for (auto& h : hits) if (h.type == ty) lv.push_back (h.rel);
        if (lv.empty()) continue;
        std::sort (lv.begin(), lv.end());
        const float mainDb = lv[(size_t) std::min ((double) lv.size() - 1, std::floor (0.9 * (double) (lv.size() - 1) + 0.5))];
        for (auto& h : hits) if (h.type == ty) h.rel = std::min (0.0f, h.rel - mainDb);
    }

    if (stats != nullptr)
    {
        *stats = {};
        stats->seconds = (double) mono.size() / fs;
        for (auto& h : hits) { stats->count[h.type]++; if (h.type == Snare && h.rel < -12.0f) stats->ghosts++; }
    }
    if (progress) progress (1.0f);
    return hits;
}

// ---- compact text for the plugin state: one hit per line, "t(0.1 ms) type rel(0.1 dB) decay(ms) on"
inline std::string toString (const std::vector<Hit>& hits)
{
    std::string out; out.reserve (hits.size() * 24);
    char buf[64];
    for (auto& h : hits)
    {
        std::snprintf (buf, sizeof (buf), "%lld %d %d %d %d\n", (long long) std::llround (h.t * 10000.0), (int) h.type,
                       (int) std::lround (h.rel * 10.0f), (int) std::lround (h.decayMs), h.on ? 1 : 0);
        out += buf;
    }
    return out;
}

inline std::vector<Hit> fromString (const std::string& s)
{
    std::vector<Hit> hits;
    const char* p = s.c_str();
    while (*p)
    {
        long long t; int ty, rel, dec, on, used = 0;
        if (std::sscanf (p, "%lld %d %d %d %d%n", &t, &ty, &rel, &dec, &on, &used) != 5 || used <= 0) break;
        p += used;
        while (*p == '\n' || *p == '\r' || *p == ' ') ++p;
        if (ty < 0 || ty >= kTypes) continue;
        Hit h; h.t = (double) t / 10000.0; h.type = (uint8_t) ty; h.rel = (float) rel / 10.0f;
        h.decayMs = (float) std::max (5, dec); h.on = on != 0;
        hits.push_back (h);
    }
    std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.t < b.t; });
    return hits;
}
} // namespace rcmap
