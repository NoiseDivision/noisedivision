#pragma once
// TRANSPLANT: the sound engine, without any JUCE (so it can be tested on its own).
//
//  analysis : a track (the reference from a file, or your track while it plays) becomes 0.5 s "slices": the spectrum in
//             1/24-octave bins, loudness and pitch classes. From the slices: the sections (intro, build, drop, break,
//             outro), each section's typical spectrum (its loudest half), and the key.
//  match    : for each of your sections: the reference's spectrum, its low end moved into your key, minus yours = the
//             curve. SMOOTH blurs it and sets how many points describe it; you can move, add and remove points.
//  filter   : the curve becomes a long FIR (linear phase, or minimum phase = NATURAL), run as a partitioned convolution.
//             Each section has its own filter; switching between them crossfades with the input history intact.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace tp
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kBins = 240;                     // 1/24 octave, 20 Hz .. 20.5 kHz
constexpr int kPerOct = 24;
constexpr double kSlice = 0.5;                 // seconds per slice
inline double binHz (double b) { return 20.0 * std::pow (2.0, (b + 0.5) / kPerOct); }
inline double hzBin (double f) { return kPerOct * std::log2 (std::max (1.0, f) / 20.0) - 0.5; }
using Spec = std::array<float, kBins>;
inline double toDb (double p) { return 10.0 * std::log10 (p + 1e-20); }

// ---------------------------------------------------------------- FFT (radix 2, in place)
template <typename T>
struct FFT
{
    int n = 0;
    std::vector<std::complex<T>> tw;
    void init (int size)
    {
        n = size; tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar ((T) 1, (T) (-2.0 * kPi * i / n));
    }
    void run (std::complex<T>* x, bool inverse) const
    {
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
            if (i < j) std::swap (x[i], x[j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    auto w = tw[(size_t) (j * step)]; if (inverse) w = std::conj (w);
                    const auto v = x[i + j + half] * w;
                    x[i + j + half] = x[i + j] - v; x[i + j] += v;
                }
        }
        if (inverse) { const T s = (T) 1 / (T) n; for (int i = 0; i < n; ++i) x[i] *= s; }
    }
};

// ---------------------------------------------------------------- slices
struct Slice
{
    float t = 0;                       // song time of the slice's start, seconds
    float loud = -120;                 // loudness, dB (roughly K-weighted)
    Spec pow {};                       // power per bin (linear)
    std::array<float, 12> chroma {};   // pitch classes C..B (normalised)
};

class SliceMaker
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate; N = fs > 70000.0 ? 16384 : 8192; hop = N / 4;
        fft.init (N); win.resize ((size_t) N); buf.assign ((size_t) N, 0.0f); ring.assign ((size_t) N, 0.0f); rpos = 0; fill = 0; sinceHop = 0; acc.clear();
        double ws = 0;
        for (int i = 0; i < N; ++i) { win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / N)); ws += win[(size_t) i]; }
        norm = 4.0 / (ws * ws);
        // which FFT bins feed each 1/24-octave bin (narrow low bins: interpolate between two FFT bins)
        lo.assign (kBins, 0); hi.assign (kBins, 0); centre.assign (kBins, 0.0);
        for (int b = 0; b < kBins; ++b)
        {
            const double f0 = 20.0 * std::pow (2.0, b / (double) kPerOct), f1 = 20.0 * std::pow (2.0, (b + 1) / (double) kPerOct);
            lo[(size_t) b] = (int) std::ceil (f0 * N / fs); hi[(size_t) b] = (int) std::ceil (f1 * N / fs);
            centre[(size_t) b] = binHz (b) * N / fs;
        }
        kw.resize ((size_t) N / 2 + 1); pc.assign ((size_t) N / 2 + 1, -1);
        for (int k = 0; k <= N / 2; ++k)
        {
            const double f = k * fs / N;
            // K-weighting, roughly: high shelf +4 dB above ~1.5 kHz, high-pass around 40 Hz
            const double shelf = 1.0 + 0.585 * (f * f) / (f * f + 1500.0 * 1500.0);
            const double hp = (f * f * f * f) / (f * f * f * f + std::pow (40.0, 4));
            kw[(size_t) k] = shelf * shelf * hp;
            if (f >= 50.0 && f <= 2000.0) pc[(size_t) k] = (((int) std::lround (12.0 * std::log2 (f / 261.6256))) % 12 + 12) % 12;
        }
        X.resize ((size_t) N);
    }
    // mono samples; t0 = the song time of x[0]
    void push (const float* x, int n, double t0)
    {
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) rpos] = x[i]; rpos = (rpos + 1) % N;
            if (fill < N) ++fill;
            if (++sinceHop >= hop && fill == N)
            {
                sinceHop = 0;
                for (int k = 0; k < N; ++k) buf[(size_t) k] = ring[(size_t) ((rpos + k) % N)];
                frame (t0 + (i + 1) / fs - 0.5 * N / fs);
            }
        }
    }
    // a faster push for whole files: hop through the samples directly
    void analyseFile (const std::vector<float>& mono)
    {
        for (size_t start = 0; start + (size_t) N <= mono.size(); start += (size_t) hop)
        {
            std::copy (mono.begin() + (long) start, mono.begin() + (long) start + N, buf.begin());
            frame ((start + 0.5 * N) / fs);
        }
    }
    std::vector<Slice> slices() const
    {
        std::vector<Slice> out;
        for (const auto& [idx, a] : acc)
        {
            if (a.n == 0) continue;
            Slice s; s.t = (float) (idx * kSlice); s.loud = (float) toDb (a.loud / a.n);
            for (int b = 0; b < kBins; ++b) s.pow[(size_t) b] = (float) (a.pow[(size_t) b] / a.n);
            double cs = 0; for (double c : a.chroma) cs += c;
            for (int c = 0; c < 12; ++c) s.chroma[(size_t) c] = (float) (cs > 0 ? a.chroma[(size_t) c] / cs : 0.0);
            out.push_back (s);
        }
        return out;
    }
    void clear() { acc.clear(); fill = 0; sinceHop = 0; rpos = 0; std::fill (ring.begin(), ring.end(), 0.0f); }
    void resetStream() { fill = 0; sinceHop = 0; rpos = 0; }   // a jump in the song: don't mix audio from before it into the next frame
    double seconds() const { return acc.empty() ? 0.0 : (double) acc.size() * kSlice; }

private:
    struct Acc { std::array<double, kBins> pow {}; std::array<double, 12> chroma {}; double loud = 0; int n = 0; };
    void frame (double tCentre)
    {
        if (tCentre < 0) return;
        for (int i = 0; i < N; ++i) X[(size_t) i] = { (double) buf[(size_t) i] * win[(size_t) i], 0.0 };
        fft.run (X.data(), false);
        auto P = [&] (int k) { return std::norm (X[(size_t) k]) * norm; };
        auto& a = acc[(int) std::floor (tCentre / kSlice)];
        double loud = 0;
        for (int k = 1; k <= N / 2; ++k)
        {
            const double p = P (k);
            loud += p * kw[(size_t) k];
            if (pc[(size_t) k] >= 0) a.chroma[(size_t) pc[(size_t) k]] += std::sqrt (p);
        }
        for (int b = 0; b < kBins; ++b)
        {
            const int k0 = lo[(size_t) b], k1 = std::min (hi[(size_t) b], N / 2);
            double v;
            if (k1 > k0) { v = 0; for (int k = k0; k < k1; ++k) v += P (k); v /= (k1 - k0); }
            else { const double c = centre[(size_t) b]; const int k = (int) c; const double fr = c - k; v = P (k) * (1 - fr) + P (std::min (k + 1, N / 2)) * fr; }
            a.pow[(size_t) b] += v;
        }
        a.loud += loud; ++a.n;
    }
    double fs = 48000, norm = 1; int N = 8192, hop = 2048, fill = 0, sinceHop = 0, rpos = 0;
    FFT<double> fft; std::vector<std::complex<double>> X;
    std::vector<float> win, buf, ring; std::vector<int> lo, hi, pc; std::vector<double> centre, kw;
    std::map<int, Acc> acc;
};

// ---------------------------------------------------------------- key
struct Key { int tonic = -1; bool minor = true; float confidence = 0; };   // tonic: 0 = C
inline const char* noteName (int pc) { static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return pc >= 0 ? n[pc % 12] : "?"; }
inline std::string keyName (const Key& k) { return k.tonic < 0 ? std::string ("?") : std::string (noteName (k.tonic)) + (k.minor ? " MIN" : " MAJ"); }

inline Key findKey (const std::vector<Slice>& s)
{
    std::array<double, 12> c {};
    float maxL = -200; for (const auto& x : s) maxL = std::max (maxL, x.loud);
    for (const auto& x : s)
        if (x.loud > maxL - 25)
            for (int i = 0; i < 12; ++i) c[(size_t) i] += x.chroma[(size_t) i] * std::pow (10.0, (x.loud - maxL) / 20.0);
    static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto corr = [&] (const double* prof, int t)
    {
        double ma = 0, mb = 0; for (int i = 0; i < 12; ++i) { ma += c[(size_t) ((i + t) % 12)]; mb += prof[i]; } ma /= 12; mb /= 12;
        double sab = 0, saa = 0, sbb = 0;
        for (int i = 0; i < 12; ++i) { const double a = c[(size_t) ((i + t) % 12)] - ma, b = prof[i] - mb; sab += a * b; saa += a * a; sbb += b * b; }
        return sab / std::sqrt (saa * sbb + 1e-20);
    };
    Key k; double best = -2, second = -2;
    for (int t = 0; t < 12; ++t)
        for (int m = 0; m < 2; ++m)
        {
            const double r = corr (m ? mnr : maj, t);
            if (r > best) { second = best; best = r; k.tonic = t; k.minor = m == 1; }
            else if (r > second) second = r;
        }
    k.confidence = (float) std::clamp (0.5 + (best - second) * 4.0, 0.0, 1.0) * (float) std::clamp (best, 0.0, 1.0);
    return k;
}
// semitones to move the reference's low end into your key (the shorter way round); major keys count as their relative minor
inline int keyShift (const Key& mine, const Key& ref)
{
    if (mine.tonic < 0 || ref.tonic < 0) return 0;
    const int a = mine.minor ? mine.tonic : (mine.tonic + 9) % 12, b = ref.minor ? ref.tonic : (ref.tonic + 9) % 12;
    int d = ((a - b) % 12 + 12) % 12; if (d > 6) d -= 12;
    return d;
}

// ---------------------------------------------------------------- sections
enum SectionType { kIntro = 0, kBuild, kDrop, kBreak, kOutro, kFull };
inline const char* typeName (int t) { static const char* n[] = { "INTRO", "BUILD", "DROP", "BREAK", "OUTRO", "FULL" }; return n[std::clamp (t, 0, 5)]; }
struct Section
{
    int type = kFull, index = 1;      // e.g. DROP 2
    bool numbered = false;            // more than one of its kind: show the number
    double t0 = 0, t1 = 0;
    Spec pow {};                      // typical spectrum (its loudest half), linear power
    float loud = -120;                // loudness of its loudest half, dB (for level matching)
    std::string name() const { return type == kFull ? std::string ("WHOLE TRACK") : std::string (typeName (type)) + (numbered ? " " + std::to_string (index) : std::string()); }
};

// the loudest half of the slices in [t0, t1), averaged
// a section's loudness the way integrated LUFS counts it: everything more than 10 dB under its average is left out
inline float typicalLoudness (const std::vector<Slice>& s, double t0, double t1)
{
    std::vector<double> p;
    for (const auto& x : s) if (x.t >= t0 - 1e-6 && x.t < t1 - 1e-6) p.push_back (std::pow (10.0, x.loud / 10.0));
    if (p.empty()) return -120.0f;
    const double all = std::accumulate (p.begin(), p.end(), 0.0) / p.size(), gate = all * 0.1;
    double a = 0; int n = 0; for (double v : p) if (v >= gate) { a += v; ++n; }
    return (float) toDb (n ? a / n : all);
}
inline Spec typicalSpectrum (const std::vector<Slice>& s, double t0, double t1)
{
    std::vector<const Slice*> in;
    for (const auto& x : s) if (x.t >= t0 - 1e-6 && x.t < t1 - 1e-6) in.push_back (&x);
    Spec out {}; if (in.empty()) return out;
    std::sort (in.begin(), in.end(), [] (auto* a, auto* b) { return a->loud > b->loud; });
    const size_t n = std::max<size_t> (1, (in.size() + 1) / 2);
    for (size_t i = 0; i < n; ++i) for (int b = 0; b < kBins; ++b) out[(size_t) b] += in[i]->pow[(size_t) b] / (float) n;
    return out;
}

struct TrackInfo
{
    std::vector<Slice> slices;
    std::vector<Section> sections;
    Section whole;                     // the whole track
    Key key;
    double length = 0;
    bool valid() const { return ! slices.empty(); }
};

inline void analyseTrack (TrackInfo& tr)
{
    auto& s = tr.slices;
    std::sort (s.begin(), s.end(), [] (const Slice& a, const Slice& b) { return a.t < b.t; });
    tr.sections.clear();
    tr.length = s.empty() ? 0.0 : s.back().t + kSlice;
    tr.whole = Section(); tr.whole.type = kFull; tr.whole.t0 = 0; tr.whole.t1 = tr.length;
    tr.whole.pow = typicalSpectrum (s, 0, tr.length + 1);
    tr.whole.loud = typicalLoudness (s, 0, tr.length + 1);
    tr.key = findKey (s);
    if (s.size() < 8) { tr.sections.push_back (tr.whole); return; }
    // loudness on the slice grid (gaps: nearest heard slice), smoothed over 4 s
    const int G = (int) std::ceil (tr.length / kSlice);
    std::vector<double> L ((size_t) G, -200.0);
    for (const auto& x : s) { const int i = (int) std::lround (x.t / kSlice); if (i >= 0 && i < G) L[(size_t) i] = x.loud; }
    for (int i = 0, last = -1; i < G; ++i) { if (L[(size_t) i] > -199) last = i; else if (last >= 0) L[(size_t) i] = L[(size_t) last]; }
    for (int i = G - 1, next = -1; i >= 0; --i) { if (L[(size_t) i] > -199) next = i; else if (next >= 0) L[(size_t) i] = L[(size_t) next]; }
    std::vector<double> Ls ((size_t) G);
    for (int i = 0; i < G; ++i)
    {
        double a = 0; int n = 0;
        for (int j = std::max (0, i - 4); j <= std::min (G - 1, i + 3); ++j) { a += std::pow (10.0, L[(size_t) j] / 10.0); ++n; }
        Ls[(size_t) i] = toDb (a / n);
    }
    std::vector<double> sorted;
    const double mx = *std::max_element (Ls.begin(), Ls.end());
    for (double v : Ls) if (v > mx - 45) sorted.push_back (v);
    std::sort (sorted.begin(), sorted.end());
    auto pct = [&] (double p) { return sorted[(size_t) std::clamp ((int) (p * (sorted.size() - 1)), 0, (int) sorted.size() - 1)]; };
    const double hiL = pct (0.9), loL = pct (0.15);
    if (hiL - loL < 3.0) { tr.sections.push_back (tr.whole); tr.sections.back().type = kFull; return; }
    const double thr = loL + 0.62 * (hiL - loL);
    std::vector<int> high ((size_t) G);
    for (int i = 0; i < G; ++i) high[(size_t) i] = Ls[(size_t) i] > thr ? 1 : 0;
    // majority over 9 slices, then no run shorter than 8 s
    { auto h = high; for (int i = 0; i < G; ++i) { int c = 0, n = 0; for (int j = std::max (0, i - 4); j <= std::min (G - 1, i + 4); ++j) { c += h[(size_t) j]; ++n; } high[(size_t) i] = 2 * c > n ? 1 : 0; } }
    struct Run { int a, b, h; };
    auto runs = [&]
    {
        std::vector<Run> r;
        for (int i = 0; i < G; ++i) { if (r.empty() || r.back().h != high[(size_t) i]) r.push_back ({ i, i + 1, high[(size_t) i] }); else r.back().b = i + 1; }
        return r;
    };
    for (int pass = 0; pass < 8; ++pass)
    {
        auto r = runs(); bool changed = false;
        for (size_t k = 0; k < r.size(); ++k)
            if (r[k].b - r[k].a < 16 && r.size() > 1)
            {
                for (int i = r[k].a; i < r[k].b; ++i) high[(size_t) i] = 1 - r[k].h;
                changed = true; break;
            }
        if (! changed) break;
    }
    auto r = runs();
    std::vector<Section> out;
    for (size_t k = 0; k < r.size(); ++k)
    {
        Section sec; sec.t0 = r[k].a * kSlice; sec.t1 = r[k].b * kSlice;
        if (r[k].h) sec.type = kDrop;
        else sec.type = k == 0 && r.size() > 1 ? kIntro : (k == r.size() - 1 && r.size() > 1 ? kOutro : kBreak);
        // a quiet part right before a drop: its rising end is the build
        if (! r[k].h && k + 1 < r.size())
        {
            int i = r[k].b - 1, start = i;
            while (i > r[k].a && Ls[(size_t) i - 1] <= Ls[(size_t) i] + 0.3) { --i; start = i; }
            const double rise = Ls[(size_t) r[k].b - 1] - Ls[(size_t) start];
            if ((r[k].b - start) * kSlice >= 6.0 && rise >= 3.0)
            {
                if ((start - r[k].a) * kSlice >= 8.0)
                {
                    Section before = sec; before.t1 = start * kSlice; out.push_back (before);
                    sec.t0 = start * kSlice;
                }
                sec.type = kBuild;
            }
        }
        out.push_back (sec);
    }
    int counts[6] = {};
    for (auto& sec : out) { sec.index = ++counts[sec.type]; sec.pow = typicalSpectrum (s, sec.t0, sec.t1); sec.loud = typicalLoudness (s, sec.t0, sec.t1); }
    for (auto& sec : out) sec.numbered = counts[sec.type] > 1;
    tr.sections = out;
}

// ---------------------------------------------------------------- the match curve
struct Node { float bin = 0, db = 0; bool user = false; };

// the reference's low end moved by 'semis' (1 semitone = 2 bins), fading back to unshifted over half an octave above zoneHz
inline Spec shiftLowEnd (const Spec& refDb, int semis, double zoneHz)
{
    if (semis == 0) return refDb;
    Spec out = refDb; const double zb = hzBin (zoneHz);
    for (int b = 0; b < kBins; ++b)
    {
        const double w = std::clamp ((zb + 12.0 - b) / 12.0, 0.0, 1.0);
        if (w <= 0) continue;
        const double src = std::clamp (b - 2.0 * semis, 0.0, kBins - 1.0);
        const int i = (int) src; const double fr = src - i;
        const double v = refDb[(size_t) i] * (1 - fr) + refDb[(size_t) std::min (i + 1, kBins - 1)] * fr;
        out[(size_t) b] = (float) (w * v + (1 - w) * refDb[(size_t) b]);
    }
    return out;
}
inline Spec toDbSpec (const Spec& p) { Spec o; for (int b = 0; b < kBins; ++b) o[(size_t) b] = (float) toDb (p[(size_t) b]); return o; }

// above 'fromBin': single narrow peaks and notches (one song's notes and resonances, not its tonal balance) are pulled
// toward the local average: peaks to at most +3 dB over it, notches to at most -6 dB under it. Broad shapes stay exact.
inline Spec tame (const Spec& db, double fromBin)
{
    Spec env {}, out = db; const double sg = 4.0; const int R = 12;
    for (int b = 0; b < kBins; ++b)
    {
        double a = 0, w = 0;
        for (int k = -R; k <= R; ++k) { const int j = std::clamp (b + k, 0, kBins - 1); const double g = std::exp (-0.5 * k * k / (sg * sg)); a += g * db[(size_t) j]; w += g; }
        env[(size_t) b] = (float) (a / w);
    }
    for (int b = 0; b < kBins; ++b)
    {
        const double w = std::clamp ((b - fromBin) / 12.0, 0.0, 1.0);
        const double t = std::clamp ((double) db[(size_t) b], env[(size_t) b] - 6.0, env[(size_t) b] + 3.0);
        out[(size_t) b] = (float) (w * t + (1 - w) * db[(size_t) b]);
    }
    return out;
}

inline double tiltDb (int b) { return (b - hzBin (1000.0)) * 4.5 / kPerOct; }
// reference minus yours, level-matched over the middle of the spectrum; bins where either is (near) silent count less
inline Spec difference (const Spec& yoursDb, const Spec& refDb)
{
    Spec d {}; const float ym = *std::max_element (yoursDb.begin(), yoursDb.end()), rm = *std::max_element (refDb.begin(), refDb.end());
    double ytMax = -300; for (int b = 0; b < kBins; ++b) ytMax = std::max (ytMax, yoursDb[(size_t) b] + tiltDb (b));
    double m = 0; int n = 0;
    for (int b = 0; b < kBins; ++b) { const double f = binHz (b); if (f > 100 && f < 6000) { m += refDb[(size_t) b] - yoursDb[(size_t) b]; ++n; } }
    m /= std::max (1, n);
    for (int b = 0; b < kBins; ++b)
    {
        const double q = std::min (yoursDb[(size_t) b] - (ym - 75.0), refDb[(size_t) b] - (rm - 75.0));   // how far above "silent"
        const double w = std::clamp (q / 15.0, 0.0, 1.0);
        double v = w * (refDb[(size_t) b] - yoursDb[(size_t) b] - m);
        // no big boosts where your track has (almost) nothing: there's nothing there to bring up.
        // ("nothing" on a +4.5 dB/octave tilt, so a normal mix reads level from lows to highs)
        if (v > 0) v *= std::clamp ((yoursDb[(size_t) b] + tiltDb (b) - (ytMax - 42.0)) / 12.0, 0.0, 1.0);
        d[(size_t) b] = (float) v;
    }
    return d;
}

// SMOOTH 0..1: 0 = every 1/24 octave, 1 = about an octave wide
inline double smoothSigmaBins (double smooth) { return 0.35 * std::pow (40.0, std::clamp (smooth, 0.0, 1.0)); }
inline double smoothOctaves (double smooth) { return std::max (1.0 / kPerOct, 2.355 * smoothSigmaBins (smooth) / kPerOct); }
inline Spec smoothCurve (const Spec& d, double smooth)
{
    const double sg = smoothSigmaBins (smooth);
    if (sg < 0.4) return d;
    Spec o {}; const int R = (int) std::ceil (sg * 3);
    for (int b = 0; b < kBins; ++b)
    {
        double a = 0, w = 0;
        for (int k = -R; k <= R; ++k) { const int j = std::clamp (b + k, 0, kBins - 1); const double g = std::exp (-0.5 * k * k / (sg * sg)); a += g * d[(size_t) j]; w += g; }
        o[(size_t) b] = (float) (a / w);
    }
    return o;
}

// the fewest points that describe the curve within a tolerance (smoother = coarser = fewer points)
inline std::vector<Node> extractNodes (const Spec& c, double smooth)
{
    const double tol = 0.25 + 1.6 * std::clamp (smooth, 0.0, 1.0);
    std::vector<int> keep { 0, kBins - 1 };
    std::vector<std::pair<int, int>> stack { { 0, kBins - 1 } };
    while (! stack.empty())
    {
        auto [a, b] = stack.back(); stack.pop_back();
        double worst = 0; int wi = -1;
        for (int i = a + 1; i < b; ++i)
        {
            const double y = c[(size_t) a] + (c[(size_t) b] - c[(size_t) a]) * (i - a) / (double) (b - a);
            const double e = std::abs (c[(size_t) i] - y);
            if (e > worst) { worst = e; wi = i; }
        }
        if (wi > 0 && worst > tol) { keep.push_back (wi); stack.push_back ({ a, wi }); stack.push_back ({ wi, b }); }
    }
    std::sort (keep.begin(), keep.end());
    std::vector<Node> out;
    for (int i : keep) out.push_back ({ (float) i, c[(size_t) i], false });
    return out;
}

// a smooth curve through the points (monotone cubic: no overshoot between points); flat beyond the ends
inline Spec curveThrough (std::vector<Node> n)
{
    Spec o {};
    std::sort (n.begin(), n.end(), [] (const Node& a, const Node& b) { return a.bin < b.bin; });
    if (n.empty()) return o;
    if (n.size() == 1) { o.fill (n[0].db); return o; }
    const size_t m = n.size();
    std::vector<double> dl (m - 1), sl (m);
    for (size_t i = 0; i + 1 < m; ++i) dl[i] = (n[i + 1].db - n[i].db) / std::max (1e-3, (double) (n[i + 1].bin - n[i].bin));
    sl[0] = dl[0]; sl[m - 1] = dl[m - 2];
    for (size_t i = 1; i + 1 < m; ++i) sl[i] = dl[i - 1] * dl[i] <= 0 ? 0.0 : 2.0 / (1.0 / dl[i - 1] + 1.0 / dl[i]);
    size_t j = 0;
    for (int b = 0; b < kBins; ++b)
    {
        const double x = b;
        if (x <= n[0].bin) { o[(size_t) b] = n[0].db; continue; }
        if (x >= n[m - 1].bin) { o[(size_t) b] = n[m - 1].db; continue; }
        while (j + 1 < m && n[j + 1].bin < x) ++j;
        const double h = std::max (1e-3, (double) (n[j + 1].bin - n[j].bin)), t = (x - n[j].bin) / h;
        const double h00 = 2 * t * t * t - 3 * t * t + 1, h10 = t * t * t - 2 * t * t + t, h01 = -2 * t * t * t + 3 * t * t, h11 = t * t * t - t * t;
        o[(size_t) b] = (float) (h00 * n[j].db + h10 * h * sl[j] + h01 * n[j + 1].db + h11 * h * sl[j + 1]);
    }
    return o;
}

// the analysis' points, with the user's points and removals applied, AMOUNT on the analysis' points only
inline std::vector<Node> mergeNodes (const std::vector<Node>& autoN, const std::vector<Node>& userN, const std::vector<float>& removed, double amount)
{
    std::vector<Node> out;
    for (auto a : autoN)
    {
        bool skip = false;
        for (auto& u : userN) if (std::abs (u.bin - a.bin) < 3.0f) skip = true;           // a user point owns 1/8 octave around it
        for (float r : removed) if (std::abs (r - a.bin) < 1.5f) skip = true;
        if (a.bin <= 0 || a.bin >= kBins - 1) skip = skip && false;                         // the two ends always stay
        if (! skip) { a.db = (float) (a.db * amount); out.push_back (a); }
    }
    for (auto u : userN) { u.user = true; out.push_back (u); }
    std::sort (out.begin(), out.end(), [] (const Node& a, const Node& b) { return a.bin < b.bin; });
    return out;
}
// how much an EQ curve changes a section's loudness (roughly K-weighted), dB
inline double eqLoudnessChange (const Spec& powLin, const Spec& curveDb)
{
    double a = 0, b = 0;
    for (int i = 0; i < kBins; ++i)
    {
        const double f = binHz (i), bw = f * (std::pow (2.0, 1.0 / (2 * kPerOct)) - std::pow (2.0, -1.0 / (2 * kPerOct)));
        const double shelf = 1.0 + 0.585 * (f * f) / (f * f + 1500.0 * 1500.0), hp = std::pow (f, 4) / (std::pow (f, 4) + std::pow (40.0, 4));
        const double w = powLin[(size_t) i] * bw * shelf * shelf * hp;
        a += w * std::pow (10.0, curveDb[(size_t) i] / 10.0); b += w;
    }
    return b > 0 ? toDb (a / b) : 0.0;
}
inline Spec limitRange (Spec c, double range) { for (auto& v : c) v = (float) std::clamp ((double) v, -range, range); return c; }
// matching only between FROM and TO: the curve fades to 0 dB over a third of an octave outside them
inline Spec limitBand (Spec c, double fromHz, double toHz)
{
    const double a = hzBin (fromHz), b = hzBin (toHz);
    for (int i = 0; i < kBins; ++i)
    {
        const double w = std::clamp (1.0 - (a - i) / 8.0, 0.0, 1.0) * std::clamp (1.0 - (i - b) / 8.0, 0.0, 1.0);
        c[(size_t) i] = (float) (c[(size_t) i] * w);
    }
    return c;
}

// ---------------------------------------------------------------- the filter
// curve (dB per bin) -> FIR of length L. Linear phase: symmetric, delay L/2. Minimum phase (NATURAL): no delay.
inline std::vector<float> designIR (const Spec& curveDb, double fs, int L, bool linear)
{
    const int M = linear ? L : L * 4;
    std::vector<double> mag ((size_t) M / 2 + 1);
    for (int k = 0; k <= M / 2; ++k)
    {
        const double b = std::clamp (hzBin (std::max (1.0, k * fs / M)), 0.0, kBins - 1.0);
        const int i = (int) b; const double fr = b - i;
        double db = curveDb[(size_t) i] * (1 - fr) + curveDb[(size_t) std::min (i + 1, kBins - 1)] * fr;
        const double f = k * fs / M;
        if (f < 20.0) db *= std::clamp (f / 20.0, 0.0, 1.0) * 0 + 1.0;            // hold the lowest value below 20 Hz
        mag[(size_t) k] = std::pow (10.0, db / 20.0);
    }
    FFT<double> fft; fft.init (M);
    std::vector<std::complex<double>> X ((size_t) M);
    std::vector<float> h ((size_t) L, 0.0f);
    if (linear)
    {
        for (int k = 0; k < M; ++k) X[(size_t) k] = mag[(size_t) (k <= M / 2 ? k : M - k)];
        fft.run (X.data(), true);
        for (int i = 0; i < L; ++i)
        {
            const int src = (i - L / 2 + M) % M;
            const double t = (i + 0.5) / L;
            const double w = 0.42 - 0.5 * std::cos (2 * kPi * t) + 0.08 * std::cos (4 * kPi * t);   // Blackman
            h[(size_t) i] = (float) (X[(size_t) src].real() * w);
        }
    }
    else
    {
        for (int k = 0; k < M; ++k) X[(size_t) k] = std::log (std::max (1e-6, mag[(size_t) (k <= M / 2 ? k : M - k)]));
        fft.run (X.data(), true);                                     // real cepstrum
        for (int i = 1; i < M / 2; ++i) X[(size_t) i] *= 2.0;
        for (int i = M / 2 + 1; i < M; ++i) X[(size_t) i] = 0.0;
        fft.run (X.data(), false);
        for (auto& v : X) v = std::exp (v);
        fft.run (X.data(), true);
        const int fadeStart = L * 3 / 4;
        for (int i = 0; i < L; ++i)
        {
            const double w = i < fadeStart ? 1.0 : 0.5 + 0.5 * std::cos (kPi * (i - fadeStart) / (double) (L - fadeStart));
            h[(size_t) i] = (float) (X[(size_t) i].real() * w);
        }
    }
    return h;
}

// uniformly partitioned convolution (overlap-save), both channels in one complex FFT (left = real, right = imaginary).
// Latency: one block (B samples). Filters can be swapped at any time: the input history is kept, so the new filter is
// right at once; the change is crossfaded over 'fadeBlocks' blocks.
class Convolver
{
public:
    using C = std::complex<float>;
    struct Filter { std::vector<C> H; int parts = 0; };                  // parts * 2B bins

    void prepare (int blockSize, int partitions)
    {
        B = blockSize; P = partitions; fft.init (2 * B);
        fdl.assign ((size_t) (P * 2 * B), C {}); slot = 0;
        tin.assign ((size_t) 2 * B, C {}); Y.assign ((size_t) 2 * B, C {}); Y2.assign ((size_t) 2 * B, C {});
        inL.assign ((size_t) B, 0.0f); inR.assign ((size_t) B, 0.0f); outL.assign ((size_t) B, 0.0f); outR.assign ((size_t) B, 0.0f);
        pos = 0; cur = old = nullptr; fade = 0;
    }
    Filter makeFilter (const std::vector<float>& ir) const
    {
        Filter f; f.parts = P; f.H.assign ((size_t) (P * 2 * B), C {});
        std::vector<C> t ((size_t) 2 * B);
        for (int p = 0; p < P; ++p)
        {
            std::fill (t.begin(), t.end(), C {});
            for (int i = 0; i < B; ++i) { const size_t s = (size_t) (p * B + i); if (s < ir.size()) t[(size_t) i] = ir[s]; }
            fft.run (t.data(), false);
            std::copy (t.begin(), t.end(), f.H.begin() + (long) (p * 2 * B));
        }
        return f;
    }
    int latency() const { return B; }
    // in place, any block size. f: the filter to use now (nullptr = pass through, delayed by the latency)
    void process (float* l, float* r, int n, const Filter* f)
    {
        if (f != cur)
        {
            old = cur; cur = f; fade = fadeBlocks;
        }
        for (int i = 0; i < n; ++i)
        {
            const float xl = l[i], xr = r ? r[i] : xl;
            l[i] = outL[(size_t) pos]; if (r) r[i] = outR[(size_t) pos];
            inL[(size_t) pos] = xl; inR[(size_t) pos] = xr;
            if (++pos == B) { pos = 0; block(); }
        }
    }
    int fadeBlocks = 16;

private:
    void apply (const Filter* f, std::vector<C>& out)
    {
        std::fill (out.begin(), out.end(), C {});
        const int N2 = 2 * B;
        for (int p = 0; p < P; ++p)
        {
            const C* x = fdl.data() + (size_t) (((slot - p + P) % P) * N2);
            const C* h = f->H.data() + (size_t) (p * N2);
            for (int k = 0; k < N2; ++k) out[(size_t) k] += x[k] * h[k];
        }
        fft.run (out.data(), true);
    }
    void block()
    {
        // overlap-save: [previous B | these B]
        for (int i = 0; i < B; ++i) { tin[(size_t) i] = tin[(size_t) (i + B)]; tin[(size_t) (i + B)] = C (inL[(size_t) i], inR[(size_t) i]); }
        slot = (slot + 1) % P;
        C* x = fdl.data() + (size_t) (slot * 2 * B);
        std::copy (tin.begin(), tin.end(), x);
        fft.run (x, false);
        auto emit = [&] (const std::vector<C>* a, const std::vector<C>* b, float g0, float g1)
        {
            for (int i = 0; i < B; ++i)
            {
                const float w = g0 + (g1 - g0) * (i + 0.5f) / B;
                C v = a ? (*a)[(size_t) (i + B)] : tin[(size_t) (i + B)];
                if (b) v = v * w + (*b)[(size_t) (i + B)] * (1 - w);
                outL[(size_t) i] = v.real(); outR[(size_t) i] = v.imag();
            }
        };
        const std::vector<C>* a = nullptr;
        if (cur) { apply (cur, Y); a = &Y; }
        if (fade > 0)
        {
            const std::vector<C>* b = nullptr;
            if (old) { apply (old, Y2); b = &Y2; } else { for (int i = 0; i < B; ++i) Y2[(size_t) (i + B)] = tin[(size_t) (i + B)]; b = &Y2; }
            if (! a) { for (int i = 0; i < B; ++i) Y[(size_t) (i + B)] = tin[(size_t) (i + B)]; a = &Y; }
            const float g0 = 1.0f - fade / (float) fadeBlocks, g1 = 1.0f - (fade - 1) / (float) fadeBlocks;
            emit (a, b, g0, g1);
            if (--fade == 0) old = nullptr;
        }
        else emit (a, nullptr, 1, 1);
    }
    int B = 256, P = 32, pos = 0, slot = 0, fade = 0;
    FFT<float> fft;
    std::vector<C> fdl, tin, Y, Y2;
    std::vector<float> inL, inR, outL, outR;
    const Filter* cur = nullptr; const Filter* old = nullptr;
};

// BS.1770 K-weighting (the filter LUFS uses), for measuring what you actually hear
struct KWeight
{
    struct BQ { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0; double run (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    BQ shelf {}, hp {};
    void prepare (double fs)
    {
        { const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
          const double K = std::tan (kPi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
          const double a0 = 1.0 + K / Q + K * K;
          shelf = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 }; }
        { const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (kPi * f0 / fs);
          const double a0 = 1.0 + K / Q + K * K;
          hp = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 }; }
    }
    double run (double x) { return hp.run (shelf.run (x)); }
    void reset() { shelf.z1 = shelf.z2 = hp.z1 = hp.z2 = 0; }
};

// integrated loudness the LUFS way: 400 ms blocks, quiet ones gated out (-70 absolute, -10 relative). Keeps the last 4 minutes.
struct GatedLoudness
{
    static constexpr int kMax = 600;
    double blk[kMax] {}; int count = 0, pos = 0; double cur = 0; int curN = 0, blockLen = 19200;
    void prepare (double fs) { blockLen = (int) (0.4 * fs); reset(); }
    void reset() { count = pos = 0; cur = 0; curN = 0; }
    void add (double powerSum, int n)       // the sum over n samples of the K-weighted power (both channels)
    {
        cur += powerSum; curN += n;
        if (curN >= blockLen) { blk[pos] = cur / curN; pos = (pos + 1) % kMax; count = std::min (count + 1, kMax); cur = 0; curN = 0; }
    }
    bool valid() const { return count >= 5; }     // 2 seconds
    double db() const
    {
        double s = 0; int n = 0;
        for (int i = 0; i < count; ++i) if (blk[i] > 1e-7) { s += blk[i]; ++n; }
        if (n == 0) return -120;
        const double gate = s / n * 0.1; s = 0; n = 0;
        for (int i = 0; i < count; ++i) if (blk[i] > 1e-7 && blk[i] >= gate) { s += blk[i]; ++n; }
        return n ? toDb (s / n) : -120;
    }
};

// live meters for one stereo source: short-term loudness, true peak, width, and low / mid / high balance
class LiveMeter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate; blockLen = std::max (1, (int) (0.1 * fs));
        for (auto& k : kw) k.prepare (fs);
        auto lp = [this] (double f) { const double K = std::tan (kPi * f / fs), q = 0.70710678, a0 = 1 + K / q + K * K;
                                      return KWeight::BQ { K * K / a0, 2 * K * K / a0, K * K / a0, 2 * (K * K - 1) / a0, (1 - K / q + K * K) / a0 }; };
        auto hp = [this] (double f) { const double K = std::tan (kPi * f / fs), q = 0.70710678, a0 = 1 + K / q + K * K;
                                      return KWeight::BQ { 1 / a0, -2 / a0, 1 / a0, 2 * (K * K - 1) / a0, (1 - K / q + K * K) / a0 }; };
        for (int c = 0; c < 2; ++c) { lo1[c] = lo2[c] = lp (150.0); hi1[c] = hi2[c] = hp (5000.0); }
        // true peak: 4x, a 48-tap windowed sinc (12 taps per phase)
        for (int ph = 0; ph < 4; ++ph)
            for (int k = 0; k < 12; ++k)
            {
                const double x = (k - 6 + ph / 4.0) , w = 0.5 + 0.5 * std::cos (kPi * x / 6.5);
                tpc[ph][k] = (float) ((std::abs (x) < 1e-9 ? 1.0 : std::sin (kPi * x) / (kPi * x)) * w);
            }
        reset();
    }
    void reset()
    {
        for (auto& b : ring) b = Block(); pos = filled = 0; cur = Block(); curN = 0; tpMax = 0;
        for (auto& h : hist) for (auto& v : h) v = 0; hp_ = 0;
    }
    void resetPeak() { tpMax = 0; }
    void add (float l, float r)
    {
        const float x[2] = { l, r };
        double kp = 0;
        for (int c = 0; c < 2; ++c)
        {
            const double k = kw[c].run (x[c]); kp += k * k;
            const double lo = lo2[c].run (lo1[c].run (x[c])), hi = hi2[c].run (hi1[c].run (x[c]));
            cur.low += lo * lo; cur.high += hi * hi; cur.all += (double) x[c] * x[c];
            hist[c][(size_t) hp_] = x[c];
        }
        hp_ = (hp_ + 1) % 12;
        for (int c = 0; c < 2; ++c)
            for (int ph = 0; ph < 4; ++ph)
            {
                double s = 0; for (int k = 0; k < 12; ++k) s += tpc[ph][k] * hist[c][(size_t) ((hp_ + k) % 12)];
                tpMax = std::max (tpMax, std::abs (s));
            }
        const double m = 0.5 * (l + r), sd = 0.5 * (l - r);
        cur.mid += m * m; cur.side += sd * sd; cur.k += kp;
        if (++curN >= blockLen) { cur.n = curN; ring[(size_t) pos] = cur; pos = (pos + 1) % 30; filled = std::min (filled + 1, 30); cur = Block(); curN = 0; }
    }
    // read on the audio thread after a block, publish as plain numbers
    bool valid() const { return filled >= 5; }
    double shortTermLufs() const { Block s = sum (30); return s.n ? -0.691 + toDb (s.k / s.n) : -120; }
    double truePeakDb() const { return toDb (tpMax * tpMax); }
    double widthPct() const { Block s = sum (30); return s.mid + s.side > 1e-12 ? 100.0 * s.side / (s.mid + s.side) : 0.0; }
    double bandDb (int band) const     // 0 low (<150 Hz), 1 mid, 2 high (>5 kHz), relative to the whole
    {
        Block s = sum (30); if (s.all <= 1e-12) return -60;
        const double v = band == 0 ? s.low : band == 2 ? s.high : std::max (1e-12, s.all - s.low - s.high);
        return toDb (v / s.all);
    }
private:
    struct Block { double k = 0, low = 0, high = 0, all = 0, mid = 0, side = 0; int n = 0; };
    Block sum (int blocks) const
    {
        Block s; const int nb = std::min (blocks, filled);
        for (int i = 0; i < nb; ++i) { const auto& b = ring[(size_t) ((pos - 1 - i + 30) % 30)]; s.k += b.k; s.low += b.low; s.high += b.high; s.all += b.all; s.mid += b.mid; s.side += b.side; s.n += b.n; }
        return s;
    }
    double fs = 48000; int blockLen = 4800, pos = 0, filled = 0, curN = 0, hp_ = 0;
    KWeight kw[2]; KWeight::BQ lo1[2] {}, lo2[2] {}, hi1[2] {}, hi2[2] {};
    std::array<Block, 30> ring {}; Block cur;
    float tpc[4][12] {}; std::array<std::array<float, 12>, 2> hist {}; double tpMax = 0;
};

// FIR length for a sample rate (about 0.34 s: enough for 1/24-octave detail down to ~60 Hz)
inline int firLength (double fs) { return fs > 140000 ? 65536 : fs > 70000 ? 32768 : 16384; }
}
