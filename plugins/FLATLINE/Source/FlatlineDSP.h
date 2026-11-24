#pragma once
// FLATLINE: clean loudness. Plain C++ (no JUCE), so it can be tested offline.
//
//   input -> GAIN -> UPWARD (lifts the quiet parts) -> CLIPPER (4x oversampled, optional)
//         -> LIMITER (true-peak, lookahead, two-stage release) -> safety ceiling -> dither -> output
//
// The limiter: for every sample it estimates the true peak (4x interpolation), works out the gain that keeps it
// under CEILING, holds the lowest of those over the lookahead, lets it come back through a fast and a slow release
// (the slow one only builds up under sustained limiting, so dense material doesn't pump), and smooths the result
// over the lookahead with a moving average. The gain is fully down when the peak arrives and has no corners.
//
// Meters (ITU BS.1770 / EBU R128): momentary, short-term and integrated loudness, true peak, PLR, gain reduction,
// clip and lift amounts, and CLEAN (how little the clipper and limiter change the sound).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace fl
{
constexpr double kPi = 3.14159265358979323846;

enum class Style { Transparent = 0, Punch = 1, Dense = 2, Safe = 3 };

struct Settings
{
    double gainDb = 0.0;
    double ceilingDb = -1.0;         // dBTP
    Style style = Style::Transparent;
    bool autoRelease = true;
    double releaseMs = 100.0;        // when not AUTO
    double link = 1.0;               // 0..1
    double transient = 0.0;          // 0..1: TRANSIENTS emphasis (up to +8 dB on the attacks, before the clipper/limiter)
    bool upOn = false;
    double upAmountDb = 3.0;         // most the quiet parts are lifted
    double upBelowDb = 18.0;         // how far under the peaks counts as quiet
    bool clipOn = false;
    double clipDriveDb = 1.5;
    bool clipHard = false;
    bool truePeak = false;           // sample peak by default (louder); true peak in ADVANCED
    double lookaheadMs = -1.0;       // ADVANCED: < 0 = AUTO (from STYLE), else 0.5..5 ms
    int dither = 0;                  // 0 off, 1 24-bit, 2 16-bit
    bool delta = false;              // hear only what the clipper and limiter take away
    bool bypass = false;             // the untouched input, at the same loudness
};

struct StyleParams { double lookaheadMs, fastMs, slowMs, slowAttackMs; };
inline StyleParams styleParams (Style s)
{
    switch (s)
    {
        case Style::Punch: return { 2.0, 25.0, 350.0, 250.0 };    // short lookahead lets the transient's front through, fast recovery
        case Style::Dense: return { 1.5, 12.0, 200.0, 120.0 };    // quick in, quick out: dense and loud
        case Style::Safe:  return { 5.0, 120.0, 900.0, 400.0 };   // long and gentle, the least distortion
        default:           return { 4.0, 60.0, 600.0, 300.0 };    // Transparent
    }
}

// ---------------------------------------------------------------- small building blocks
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};

// windowed-sinc low-pass taps (Kaiser), for interpolation and the oversampler
inline std::vector<double> sincTaps (int n, double cutoff /* of the high rate's Nyquist */, double beta)
{
    auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
    std::vector<double> h ((size_t) n);
    const double m = (n - 1) * 0.5, i0b = bessel0 (beta);
    for (int i = 0; i < n; ++i)
    {
        const double t = i - m;
        const double sinc = t == 0 ? cutoff : std::sin (kPi * cutoff * t) / (kPi * t);
        const double r = t / m;
        h[(size_t) i] = sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / i0b;
    }
    return h;
}

// ---------------------------------------------------------------- loudness (BS.1770)
class Loudness
{
public:
    void prepare (double fs, int channels)
    {
        nCh = channels;
        auto shelf = [&] (Biquad& b)
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1 + K / Q + K * K;
            b = {}; b.b0 = (Vh + Vb * K / Q + K * K) / a0; b.b1 = 2 * (K * K - Vh) / a0; b.b2 = (Vh - Vb * K / Q + K * K) / a0;
            b.a1 = 2 * (K * K - 1) / a0; b.a2 = (1 - K / Q + K * K) / a0;
        };
        auto hp = [&] (Biquad& b)
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (kPi * f0 / fs);
            const double d = 1 + K / Q + K * K;
            b = {}; b.b0 = 1; b.b1 = -2; b.b2 = 1; b.a1 = 2 * (K * K - 1) / d; b.a2 = (1 - K / Q + K * K) / d;
        };
        for (auto& c : k) { shelf (c[0]); hp (c[1]); }
        blockLen = std::max (1, (int) std::lround (0.1 * fs));
        reset();
    }
    void reset()
    {
        for (auto& c : k) { c[0].reset(); c[1].reset(); }
        acc = 0; accN = 0; blocks.fill (0.0); nBlocks = 0; head = 0;
        hist.fill (0.0); histN.fill (0);
        momentary = shortTerm = integrated = -120.0;
    }
    void resetIntegrated() { hist.fill (0.0); histN.fill (0); integrated = -120.0; }

    // one stereo (or mono) sample
    void push (double l, double r)
    {
        double e = 0;
        { const double y = k[0][1].process (k[0][0].process (l)); e += y * y; }
        if (nCh > 1) { const double y = k[1][1].process (k[1][0].process (r)); e += y * y; }
        acc += e; ++accN;
        if (accN >= blockLen) endBlock();
    }

    double momentary = -120.0, shortTerm = -120.0, integrated = -120.0;

private:
    static double lufs (double meanSquare) { return -0.691 + 10.0 * std::log10 (meanSquare + 1e-20); }
    void endBlock()
    {
        blocks[(size_t) head] = acc / accN; head = (head + 1) % 30; nBlocks = std::min (nBlocks + 1, 30);
        acc = 0; accN = 0;
        auto mean = [&] (int n) { n = std::min (n, nBlocks); double s = 0; for (int i = 1; i <= n; ++i) s += blocks[(size_t) ((head - i + 30) % 30)]; return n ? s / n : 0.0; };
        const double m = mean (4);
        momentary = nBlocks >= 1 ? lufs (m) : -120.0;
        shortTerm = lufs (mean (30));
        // integrated: 400 ms blocks every 100 ms into a histogram (0.1 LU bins from -70 to +5), gated -70 then -10
        if (nBlocks >= 4)
        {
            const double l = lufs (m);
            if (l > -70.0)
            {
                const int bin = std::clamp ((int) ((l + 70.0) * 10.0), 0, kBins - 1);
                hist[(size_t) bin] += m; histN[(size_t) bin] += 1;
            }
            double s = 0; long n = 0;
            for (int b = 0; b < kBins; ++b) { s += hist[(size_t) b]; n += histN[(size_t) b]; }
            if (n > 0)
            {
                const double gate = lufs (s / n) - 10.0;
                double s2 = 0; long n2 = 0;
                for (int b = std::max (0, (int) ((gate + 70.0) * 10.0)); b < kBins; ++b) { s2 += hist[(size_t) b]; n2 += histN[(size_t) b]; }
                integrated = n2 > 0 ? lufs (s2 / n2) : -120.0;
            }
        }
    }
    static constexpr int kBins = 750;
    int nCh = 2, blockLen = 4800;
    std::array<std::array<Biquad, 2>, 2> k;
    double acc = 0; int accN = 0;
    std::array<double, 30> blocks {}; int nBlocks = 0, head = 0;
    std::array<double, kBins> hist {}; std::array<long, kBins> histN {};
};

// ---------------------------------------------------------------- 8x true-peak estimate between samples
// (4x, the broadcast minimum, can read up to 0.5 dB low on content near 20 kHz; 8x stays within about 0.1 dB)
class TruePeak
{
public:
    static constexpr int kOver = 8, kTaps = 48;     // taps per phase; delay kTaps/2 samples
    TruePeak()
    {
        const auto h = sincTaps (kTaps * kOver + 1, (1.0 / kOver) * 0.985, 8.0);
        for (int p = 1; p < kOver; ++p)
            for (int t = 0; t < kTaps; ++t) { const int i = t * kOver + p; ph[(size_t) p - 1][(size_t) t] = (i < (int) h.size() ? h[(size_t) i] : 0.0) * kOver; }
    }
    void reset() { hist.fill (0.0); pos = 0; }
    // push x[n]; returns the true peak around x[n - kTaps/2]
    double push (double x)
    {
        hist[(size_t) pos] = x; hist[(size_t) (pos + kTaps)] = x; pos = (pos + 1) % kTaps;
        const double* w = &hist[(size_t) pos];                 // oldest .. newest
        double pk = std::abs (w[kTaps / 2 - 1]);
        for (int p = 0; p < kOver - 1; ++p)
        {
            double s = 0; const auto& c = ph[(size_t) p];
            for (int t = 0; t < kTaps; ++t) s += c[(size_t) t] * w[kTaps - 1 - t];
            pk = std::max (pk, std::abs (s));
        }
        return pk;
    }
private:
    std::array<std::array<double, kTaps>, kOver - 1> ph {};
    std::array<double, kTaps * 2> hist {};
    int pos = 0;
};

// ---------------------------------------------------------------- 4x oversampled clipper (always in the path: constant latency)
class OsClipper
{
public:
    static constexpr int kN = 257, kPhaseTaps = 65;  // latency: (kN-1)/2 at 4x, twice = 64 base samples; flat to about 20 kHz
    static constexpr int kLatency = 64;
    OsClipper()
    {
        h = sincTaps (kN, 0.25 * 0.94, 9.0);
        double s = 0; for (double v : h) s += v; for (double& v : h) v /= s;
        for (int p = 0; p < 4; ++p)
            for (int t = 0; t < kPhaseTaps; ++t) { const int i = t * 4 + p; up[p][t] = i < kN ? h[(size_t) i] * 4.0 : 0.0; }
    }
    void reset() { xin.fill (0.0); xu.fill (0.0); raw.fill (0.0); pin = pu = praw = 0; }
    // one base-rate sample; mode 0 pass, 1 soft, 2 hard; drive = linear gain into the clip, c = clip level
    double process (double x, int mode, double drive, double c, double& overMax)
    {
        // off: a plain delay of the same length (untouched, nothing filtered); the oversampler keeps running so switching on is smooth
        const double dry = raw[(size_t) praw]; raw[(size_t) praw] = x; praw = (praw + 1) % kLatency;
        xin[(size_t) pin] = x; xin[(size_t) (pin + kPhaseTaps)] = x; pin = (pin + 1) % kPhaseTaps;
        const double* w = &xin[(size_t) pin];
        double out = 0;
        for (int p = 0; p < 4; ++p)
        {
            double s = 0; for (int t = 0; t < kPhaseTaps; ++t) s += up[p][t] * w[kPhaseTaps - 1 - t];
            if (mode)
            {
                s *= drive;
                const double a = std::abs (s);
                if (a > c)
                {
                    overMax = std::max (overMax, a / c);
                    s = std::copysign (c, s);
                }
                else if (mode == 1)
                {
                    const double k = 0.7 * c;
                    if (a > k) s = std::copysign (k + (c - k) * std::tanh ((a - k) / (c - k)), s);
                }
            }
            xu[(size_t) pu] = s; xu[(size_t) (pu + kN)] = s; pu = (pu + 1) % kN;
            if (p == 0)
            {
                // decimate on phase 0, so the latency is a whole number of base samples
                const double* u = &xu[(size_t) pu];
                for (int t = 0; t < kN; ++t) out += h[(size_t) t] * u[kN - 1 - t];
            }
        }
        return mode ? out : dry;
    }
private:
    std::array<double, kLatency> raw {}; int praw = 0;
    std::vector<double> h;
    std::array<std::array<double, kPhaseTaps>, 4> up {};
    std::array<double, kPhaseTaps * 2> xin {};
    std::array<double, kN * 2> xu {};
    int pin = 0, pu = 0;
};

// sliding minimum over a fixed window (monotonic queue)
class SlidingMin
{
public:
    void setSize (int n) { size = std::max (1, n); vals.assign ((size_t) size + 2, 1.0); idx.assign ((size_t) size + 2, 0); h = t = 0; count = 0; }
    double push (double v)
    {
        const int cap = (int) vals.size();
        while (h != t && vals[(size_t) ((t - 1 + cap) % cap)] >= v) t = (t - 1 + cap) % cap;
        vals[(size_t) t] = v; idx[(size_t) t] = count; t = (t + 1) % cap;
        while (idx[(size_t) h] <= count - size) h = (h + 1) % cap;
        ++count;
        return vals[(size_t) h];
    }
private:
    std::vector<double> vals; std::vector<int64_t> idx; int h = 0, t = 0, size = 1; int64_t count = 0;
};

class MovingAverage
{
public:
    void setSize (int n) { size = std::max (1, n); buf.assign ((size_t) size, 1.0); sum = size; pos = 0; k = 0; }
    double push (double v)
    {
        sum += v - buf[(size_t) pos]; buf[(size_t) pos] = v; pos = (pos + 1) % size;
        if (++k >= 4096) { k = 0; sum = 0; for (double b : buf) sum += b; }   // no drift
        return sum / size;
    }
private:
    std::vector<double> buf; double sum = 1; int size = 1, pos = 0, k = 0;
};

template <typename T>
class Delay
{
public:
    void setSize (int n, T fill = T()) { len = std::max (1, n); buf.assign ((size_t) len, fill); pos = 0; }
    T push (T v) { const T o = buf[(size_t) pos]; buf[(size_t) pos] = v; pos = (pos + 1) % len; return o; }
private:
    std::vector<T> buf; int len = 1, pos = 0;
};

// ---------------------------------------------------------------- the engine
class Engine
{
public:
    // ---- for the editor (relaxed)
    std::atomic<float> grNow { 0 }, grMax { 0 }, clipNow { 0 }, liftNow { 0 };
    std::atomic<float> lufsM { -120 }, lufsS { -120 }, lufsI { -120 }, inLufsS { -120 }, tpMax { -120 }, clean { 100 };
    std::atomic<int> overs { 0 };
    std::atomic<bool> resetMeters { false };

    // the WAVE view: one column per slice of time
    struct Column { float hi = 0, lo = 0, gr = 0, clip = 0, lift = 0; };
    static constexpr int kCols = 1024;
    std::array<Column, kCols> cols {};
    std::atomic<int> colWrite { 0 };
    // the SPECTRUM view: mono, before the clipper/limiter (after GAIN and UPWARD) and after
    static constexpr int kScope = 8192;
    std::array<float, kScope> scopePre {}, scopePost {};
    std::atomic<int> scopeWrite { 0 };

    int maxLookahead (double fs) const { return (int) std::ceil (5.0 * 0.001 * fs); }
    int latency() const { return OsClipper::kLatency + TruePeak::kTaps / 2 + lookMax; }

    void prepare (double sampleRate, int channels)
    {
        fs = sampleRate; nCh = std::clamp (channels, 1, 2);
        lookMax = maxLookahead (fs);
        for (auto& c : ch)
        {
            c.clip.reset(); c.tp.reset();
            c.tpPlain.setSize (TruePeak::kTaps / 2);
            c.audio.setSize (TruePeak::kTaps / 2 + lookMax);
            c.preOs.setSize (OsClipper::kLatency);
            c.pre.setSize (TruePeak::kTaps / 2 + lookMax);
            c.dry.setSize (latency());
            c.fast = c.slow = 0;
        }
        curLook = -1;
        loudOut.prepare (fs, 2); loudIn.prepare (fs, 2);
        upEnv = 1e-9; upRef = -120; upLiftDb = 0; trFast = trSlow = 1e-12;
        gainSm = -1; matchSm = 1;
        colSamples = std::max (1, (int) std::lround (fs * 4.0 / 780.0)); colN = 0; col = {};
        cleanErr = cleanRef = 0; cleanC = std::exp (-1.0 / (3.0 * fs));
        tpOut[0].reset(); tpOut[1].reset();
        tpMaxLin = 0; grMaxDb = 0;
    }

    void process (float* const* io, int numCh, int n, const Settings& s)
    {
        if (resetMeters.exchange (false)) { loudOut.resetIntegrated(); tpMaxLin = 0; grMaxDb = 0; overs.store (0); }
        const auto sp = styleParams (s.style);
        // the lookahead: the whole delay is always lookMax (constant latency); a shorter STYLE lookahead just starts the gain later
        const int look = std::clamp ((int) std::lround ((s.lookaheadMs > 0 ? s.lookaheadMs : sp.lookaheadMs) * 0.001 * fs), 1, lookMax);
        if (look != curLook)
        {
            curLook = look;
            for (auto& c : ch) { c.minHold.setSize (look + 1); c.avg.setSize (look); c.gainDelay.setSize (std::max (1, lookMax - look), 1.0); }
        }
        const double ceil = std::pow (10.0, std::clamp (s.ceilingDb, -12.0, 0.0) / 20.0);
        const double rel = s.autoRelease ? sp.fastMs : std::max (1.0, s.releaseMs);
        const double fastC = std::exp (-1.0 / (rel * 0.001 * fs));
        const double slowRelC = std::exp (-1.0 / (sp.slowMs * 0.001 * fs)), slowAttC = std::exp (-1.0 / (sp.slowAttackMs * 0.001 * fs));
        const bool useSlow = s.autoRelease;
        const double link = std::clamp (s.link, 0.0, 1.0);
        const double gainTarget = std::pow (10.0, s.gainDb / 20.0);
        if (gainSm < 0) gainSm = gainTarget;
        const double gainC = 1.0 - std::exp (-1.0 / (0.02 * fs));
        const int clipMode = s.clipOn ? (s.clipHard ? 2 : 1) : 0;
        const double drive = std::pow (10.0, (s.clipOn ? s.clipDriveDb : 0.0) / 20.0);
        // upward
        const double envC = std::exp (-1.0 / (0.03 * fs)), refDecay = std::pow (10.0, -1.5 / 20.0 / fs);   // the peak reference falls 1.5 dB/s
        const double liftUpC = std::exp (-1.0 / (0.15 * fs)), liftDownC = std::exp (-1.0 / (0.01 * fs));
        // dither
        const double lsb = s.dither == 2 ? 1.0 / 32768.0 : s.dither == 1 ? 1.0 / 8388608.0 : 0.0;
        const double matchC = 1.0 - std::exp (-1.0 / (0.3 * fs));
        const double trFastA = std::exp (-1.0 / (0.0005 * fs)), trFastR = std::exp (-1.0 / (0.03 * fs));
        const double trSlowA = std::exp (-1.0 / (0.02 * fs)), trSlowR = std::exp (-1.0 / (0.15 * fs));

        float grBlock = 0, clipBlock = 0;
        for (int i = 0; i < n; ++i)
        {
            gainSm += (gainTarget - gainSm) * gainC;
            double x[2] = { io[0][i], numCh > 1 ? io[1][i] : io[0][i] };
            loudIn.push (x[0], x[1]);
            double dryIn[2] = { x[0], x[1] };
            x[0] *= gainSm; x[1] *= gainSm;

            // ---- UPWARD: lift what is BELOW dB under the recent peaks, by up to AMOUNT
            {
                const double e = std::max (x[0] * x[0], x[1] * x[1]);
                upEnv = envC * upEnv + (1 - envC) * e;
                const double lvl = 10.0 * std::log10 (upEnv + 1e-20);
                upRef = std::max (lvl, upRef + 20.0 * std::log10 (refDecay));
                double want = 0;
                if (s.upOn && lvl > -70.0 && lvl > upRef - 55.0)
                {
                    const double thr = upRef - s.upBelowDb;
                    want = std::clamp ((thr - lvl) * 0.6, 0.0, s.upAmountDb);
                    // fade out toward silence, so noise floors don't come up
                    const double floorDb = upRef - 55.0;
                    want *= std::clamp ((lvl - floorDb) / 15.0, 0.0, 1.0);
                }
                upLiftDb = want > upLiftDb ? liftUpC * upLiftDb + (1 - liftUpC) * want : liftDownC * upLiftDb + (1 - liftDownC) * want;
                const double g = std::pow (10.0, upLiftDb / 20.0);
                x[0] *= g; x[1] *= g;
            }

            // ---- TRANSIENTS: a fast and a slow envelope; where the fast one jumps ahead (an attack), turn it up
            if (s.transient > 0.0)
            {
                const double e = std::max (x[0] * x[0], x[1] * x[1]);
                trFast = e > trFast ? trFastA * trFast + (1 - trFastA) * e : trFastR * trFast + (1 - trFastR) * e;
                trSlow = e > trSlow ? trSlowA * trSlow + (1 - trSlowA) * e : trSlowR * trSlow + (1 - trSlowR) * e;
                const double diff = std::clamp (10.0 * std::log10 ((trFast + 1e-12) / (trSlow + 1e-12)), 0.0, 16.0);
                const double g = std::pow (10.0, std::min (8.0, diff * 0.75) * s.transient / 20.0);
                x[0] *= g; x[1] *= g;
            }

            // ---- CLIPPER (oversampled, always in the path)
            double overMax = 1.0, pre[2], r[2], dly[2];
            for (int c = 0; c < 2; ++c)
            {
                pre[c] = x[c] * drive;   // what the clipper and limiter start from (for CLEAN and DELTA)
                x[c] = ch[(size_t) c].clip.process (x[c], clipMode, drive, ceil, overMax);
            }
            clipBlock = std::max (clipBlock, (float) (20.0 * std::log10 (overMax)));
            // pre, delayed by the clipper's latency, to line up with the limiter's input
            for (int c = 0; c < 2; ++c) pre[c] = ch[(size_t) c].preOs.push (pre[c]);

            // ---- LIMITER: required gain from the true peak
            for (int c = 0; c < 2; ++c)
            {
                auto& st = ch[(size_t) c];
                const double pk = s.truePeak ? st.tp.push (x[c]) : std::abs (st.tpPlain.push (x[c]));
                r[c] = pk > ceil ? ceil / pk : 1.0;
                dly[c] = st.audio.push (x[c]);
            }
            const double rl = std::min (r[0], r[1]);
            double g[2];
            for (int c = 0; c < 2; ++c)
            {
                auto& st = ch[(size_t) c];
                const double dOwn = -20.0 * std::log10 (r[c]), dLink = -20.0 * std::log10 (rl);
                const double req = std::pow (10.0, -(link * dLink + (1 - link) * dOwn) / 20.0);
                const double held = st.minHold.push (req);
                const double d = -20.0 * std::log10 (held);
                st.fast = std::max (d, fastC * st.fast + (1 - fastC) * d);
                if (useSlow) st.slow = d > st.slow ? slowAttC * st.slow + (1 - slowAttC) * d : slowRelC * st.slow + (1 - slowRelC) * d;
                else st.slow = 0;
                const double env = std::max (st.fast, std::min (st.slow, st.fast + 6.0));
                const double gl = st.avg.push (std::pow (10.0, -env / 20.0));
                g[c] = lookMax > curLook ? st.gainDelay.push (gl) : gl;
            }

            double y[2], p[2], out[2];
            for (int c = 0; c < 2; ++c)
            {
                auto& st = ch[(size_t) c];
                y[c] = dly[c] * g[c];
                if (std::abs (y[c]) > ceil) { if (std::abs (y[c]) > ceil * 1.0001) overs.fetch_add (1, std::memory_order_relaxed); y[c] = std::copysign (ceil, y[c]); }   // safety (rounding)
                p[c] = st.pre.push (pre[c]);
                const double err = y[c] - p[c];
                cleanErr = cleanErr * cleanC + err * err; cleanRef = cleanRef * cleanC + p[c] * p[c];
                double o = s.delta ? p[c] - y[c] : y[c];
                const double dIn = st.dry.push (dryIn[c]);
                if (s.bypass) o = dIn * matchSm;
                if (lsb > 0)
                {
                    const double tpdf = (rng() + rng() - 1.0) * lsb;
                    o = std::round ((o + tpdf) / lsb) * lsb;
                }
                out[c] = o;
                grBlock = std::max (grBlock, (float) (-20.0 * std::log10 (std::max (1e-6, g[c]))));
                tpMaxLin = std::max (tpMaxLin, tpOut[(size_t) c].push (y[c]));
            }
            loudOut.push (y[0], y[1]);
            {
                const double want = (loudIn.shortTerm > -70 && loudOut.shortTerm > -70) ? std::pow (10.0, std::clamp (loudOut.shortTerm - loudIn.shortTerm, -24.0, 24.0) / 20.0) : 1.0;
                matchSm += (want - matchSm) * matchC;
            }
            io[0][i] = (float) out[0];
            if (numCh > 1) io[1][i] = (float) out[1];

            // ---- the WAVE view and the SPECTRUM view
            {
                col.hi = std::max (col.hi, (float) std::max (y[0], y[1]));
                col.lo = std::min (col.lo, (float) std::min (y[0], y[1]));
                col.gr = std::max (col.gr, (float) (-20.0 * std::log10 (std::max (1e-6, std::min (g[0], g[1])))));
                col.clip = std::max (col.clip, (float) (20.0 * std::log10 (overMax)));
                col.lift = std::max (col.lift, (float) upLiftDb);
                if (++colN >= colSamples)
                {
                    const int w = colWrite.load (std::memory_order_relaxed);
                    cols[(size_t) (w % kCols)] = col; colWrite.store (w + 1, std::memory_order_release);
                    col = {}; colN = 0;
                }
                const int sw = scopeWrite.load (std::memory_order_relaxed);
                scopePost[(size_t) (sw % kScope)] = (float) (0.5 * (y[0] + y[1]));
                scopePre[(size_t) (sw % kScope)] = (float) (0.5 * (p[0] + p[1]));
                scopeWrite.store (sw + 1, std::memory_order_release);
            }
        }

        // ---- meters
        grNow.store (grBlock, std::memory_order_relaxed);
        grMaxDb = std::max (grMaxDb, (double) grBlock); grMax.store ((float) grMaxDb, std::memory_order_relaxed);
        clipNow.store (clipBlock, std::memory_order_relaxed);
        liftNow.store ((float) upLiftDb, std::memory_order_relaxed);
        lufsM.store ((float) loudOut.momentary, std::memory_order_relaxed);
        lufsS.store ((float) loudOut.shortTerm, std::memory_order_relaxed);
        lufsI.store ((float) loudOut.integrated, std::memory_order_relaxed);
        inLufsS.store ((float) loudIn.shortTerm, std::memory_order_relaxed);
        tpMax.store ((float) (20.0 * std::log10 (tpMaxLin + 1e-9)), std::memory_order_relaxed);
        clean.store ((float) std::clamp (100.0 * (1.0 - 2.0 * std::sqrt (cleanErr / (cleanRef + 1e-12))), 0.0, 100.0), std::memory_order_relaxed);
    }

private:
    struct Chan
    {
        OsClipper clip; TruePeak tp; Delay<double> tpPlain, audio, dry, pre, preOs; SlidingMin minHold; MovingAverage avg; Delay<double> gainDelay;
        double fast = 0, slow = 0;
    };
    std::array<Chan, 2> ch;
    std::array<TruePeak, 2> tpOut;
    Loudness loudOut, loudIn;
    double fs = 48000; int nCh = 2, lookMax = 240, curLook = -1;
    double upEnv = 1e-9, upRef = -120, upLiftDb = 0, gainSm = -1, matchSm = 1;
    double trFast = 1e-12, trSlow = 1e-12;
    double cleanErr = 0, cleanRef = 0, cleanC = 0.9999, tpMaxLin = 0, grMaxDb = 0;
    Column col; int colN = 0, colSamples = 245;
    std::minstd_rand gen { 1234 };
    double rng() { return std::uniform_real_distribution<double> (0.0, 1.0) (gen); }

};
} // namespace fl
