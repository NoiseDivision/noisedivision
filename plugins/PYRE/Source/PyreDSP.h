#pragma once
// PYRE: makes a lead or synth as dense as a saw, so it sounds louder at the same peak level.
// Plain C++ (no JUCE), so it can be tested offline.
//
//   IGNITE  a drive in front, for sounds that have few harmonics to start with (a sine, a filtered square).
//           The plugin runs it oversampled; process() here takes it at whatever rate it is given.
//   HEAT    the flattener: an STFT (2048 points, hop 512). In every frame it finds the harmonics (spectral peaks
//           that stand out from their neighbourhood) and moves each one toward a line that falls TILT dB per octave
//           from the strongest one, as a saw's harmonics do (-6 dB/oct). HEAT is how far of the way (0..100%),
//           MAX caps how much any harmonic may be raised. Noise between the harmonics is left alone, and the phases
//           are untouched, so the notes and their character stay; only the balance of the harmonics changes.
//   BURN    then drives the result into a soft ceiling at -0.3 dBFS. This is where the level comes from: a dense,
//           even spectrum hides the clipping's own harmonics, so it can be pushed harder before it sounds broken.
//   Both channels share one set of gains (from the mid), so the stereo image holds.
//   Latency: exactly kN samples.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

namespace pyre
{
constexpr int kN = 2048, kHop = 512, kBins = kN / 2 + 1;
constexpr int kBands = 96;                       // for the display: log bands 30 Hz .. 16 kHz
constexpr double kPi = 3.14159265358979323846;

struct Settings
{
    double ignite = 0.0;        // 0..1
    double heat = 0.6;          // 0..1
    double tiltDbOct = -6.0;    // target slope
    double maxDb = 18.0;        // largest raise of any harmonic
    double burnDb = 0.0;        // drive into the ceiling, 0..18 dB
    double mix = 1.0;           // 0..1
    double outDb = 0.0;         // after everything, -24..0 dB
};

// ---- a small radix-2 complex FFT (in place)
class FFT
{
public:
    explicit FFT (int n) : n (n), rev ((size_t) n), tw ((size_t) n / 2)
    {
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t) i] = r; }
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0, -2.0 * kPi * i / n);
    }
    void run (std::complex<double>* x, bool inverse) const
    {
        for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap (x[i], x[rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < len / 2; ++j)
                {
                    auto w = tw[(size_t) (j * step)]; if (inverse) w = std::conj (w);
                    const auto u = x[i + j], v = x[i + j + len / 2] * w;
                    x[i + j] = u + v; x[i + j + len / 2] = u - v;
                }
        }
        if (inverse) for (int i = 0; i < n; ++i) x[i] /= (double) n;
    }
private:
    int n; std::vector<int> rev; std::vector<std::complex<double>> tw;
};

class Engine
{
public:
    // ---- for the display (relaxed: a torn frame costs nothing)
    std::array<std::atomic<float>, kBands> bandBefore {}, bandAfter {};   // dB
    std::atomic<float> anchorHz { 0.0f }, anchorDb { -120.0f }, louderDb { 0.0f }, heatNow { 0.0f };
    std::atomic<float> burnDbNow { 0.0f };                 // how far over the ceiling BURN pushed (most, since the editor last read it)
    std::atomic<int> frames { 0 };

    Engine() { for (int b = 0; b < kBands; ++b) { bandBefore[(size_t) b].store (-120.0f); bandAfter[(size_t) b].store (-120.0f); } }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        win.resize (kN);
        for (int i = 0; i < kN; ++i) win[(size_t) i] = std::sqrt (0.5 - 0.5 * std::cos (2.0 * kPi * i / kN));   // sqrt-Hann, periodic
        for (auto& c : ch) { c.frame.assign (kN, 0.0); c.ola.assign (kN, 0.0); c.outQ.assign (kHop, 0.0); c.dry.assign (kN, 0.0); c.dc = c.dcIn = 0; }
        gainDb.assign (kBins, 0.0); gainSm.assign (kBins, 0.0);
        pos = 0; dryPos = 0; rmsIn = rmsOut = 1e-9;
        // display bands
        for (int b = 0; b <= kBands; ++b) bandEdge[(size_t) b] = 30.0 * std::pow (16000.0 / 30.0, (double) b / kBands);
        for (int b = 0; b < kBands; ++b) { bandBefore[(size_t) b].store (-120.0f); bandAfter[(size_t) b].store (-120.0f); }
    }

    // IGNITE, one sample (the plugin calls it oversampled): an asymmetric soft drive, small signals come up
    static double ignite (double x, double amt, double& dcState)
    {
        if (amt <= 1e-4) return x;
        const double d = 1.0 + 11.0 * std::pow (amt, 1.5), b = 0.25 * amt;
        const double y = (std::tanh (d * x + b) - std::tanh (b)) / std::tanh (d);
        // the bias leaves a little DC: take it out
        dcState += 0.0005 * (y - dcState);
        return y - dcState;
    }

    // in place, after IGNITE. dryIn: the signal before IGNITE (for MIX), same layout.
    void process (float* const* io, const float* const* dryIn, int numCh, int n, const Settings& s)
    {
        const int nc = std::min (numCh, 2);
        const double outG = std::pow (10.0, s.outDb / 20.0), burnG = std::pow (10.0, s.burnDb / 20.0), ceil = std::pow (10.0, -0.3 / 20.0);
        const double rmsCoef = std::exp (-1.0 / (0.4 * fs));
        for (int i = 0; i < n; ++i)
        {
            double inPow = 0, outPow = 0;
            for (int c = 0; c < nc; ++c)
            {
                auto& st = ch[(size_t) c];
                st.frame[(size_t) (kN - kHop + pos)] = io[c][i];
                const double wet = st.outQ[(size_t) pos];
                // the dry signal, delayed by the same kN
                const double dry = st.dry[(size_t) dryPos];
                st.dry[(size_t) dryPos] = dryIn ? dryIn[c][i] : io[c][i];
                double w = wet * burnG;
                overMax = std::max (overMax, std::abs (w));
                {
                    const double k = 0.8 * ceil, a = std::abs (w);
                    if (a > k) w = std::copysign (k + (ceil - k) * std::tanh ((a - k) / (ceil - k)), w);
                }
                const double y = (s.mix * w + (1.0 - s.mix) * dry) * outG;
                inPow += dry * dry; outPow += y * y;
                io[c][i] = (float) y;
            }
            rmsIn = rmsCoef * rmsIn + (1 - rmsCoef) * inPow;
            rmsOut = rmsCoef * rmsOut + (1 - rmsCoef) * outPow;
            dryPos = (dryPos + 1) % kN;
            if (++pos == kHop) { pos = 0; frame (nc, s); }
        }
        // mono input: the second channel copies the first
        if (numCh > nc) for (int c = nc; c < numCh; ++c) std::copy (io[0], io[0] + n, io[c]);
        if (overMax > ceil) burnDbNow.store (std::max (burnDbNow.load (std::memory_order_relaxed), (float) (20.0 * std::log10 (overMax / ceil))), std::memory_order_relaxed);
        overMax = 0.0;
        louderDb.store ((float) (10.0 * std::log10 ((rmsOut + 1e-12) / (rmsIn + 1e-12))), std::memory_order_relaxed);
    }

private:
    struct Chan { std::vector<double> frame, ola, outQ, dry; double dc = 0, dcIn = 0; std::vector<std::complex<double>> spec; };
    std::array<Chan, 2> ch;
    double fs = 48000.0;
    std::vector<double> win, gainDb, gainSm;
    int pos = 0, dryPos = 0;
    double rmsIn = 1e-9, rmsOut = 1e-9, overMax = 0.0;
    FFT fft { kN };
    std::array<double, kBands + 1> bandEdge {};

    void frame (int nc, const Settings& s)
    {
        // analysis
        std::vector<double> pw (kBins, 0.0);
        for (int c = 0; c < nc; ++c)
        {
            auto& st = ch[(size_t) c];
            st.spec.resize (kN);
            for (int i = 0; i < kN; ++i) st.spec[(size_t) i] = { st.frame[(size_t) i] * win[(size_t) i], 0.0 };
            fft.run (st.spec.data(), false);
            for (int k = 0; k < kBins; ++k) pw[(size_t) k] += std::norm (st.spec[(size_t) k]) / nc;
        }

        // the harmonics: peaks that stand 6 dB over their neighbourhood, 40 Hz .. 16 kHz
        const double binHz = fs / kN;
        const int kLo = std::max (3, (int) (40.0 / binHz)), kHi = std::min (kBins - 14, (int) (16000.0 / binHz));
        double pMax = 0.0; int kMax = -1;
        for (int k = kLo; k <= kHi; ++k) if (pw[(size_t) k] > pMax) { pMax = pw[(size_t) k]; kMax = k; }
        std::fill (gainDb.begin(), gainDb.end(), 0.0);
        const double heat = std::clamp (s.heat, 0.0, 1.0);
        if (kMax > 0 && pMax > 1e-10 && heat > 0.0)
        {
            const double lMax = 10.0 * std::log10 (pMax), fMax = kMax * binHz;
            for (int k = kLo; k <= kHi; ++k)
            {
                const double p = pw[(size_t) k];
                if (! (p > pw[(size_t) k - 1] && p >= pw[(size_t) k + 1] && p >= pw[(size_t) k - 2] && p >= pw[(size_t) k + 2])) continue;
                // prominence: over the higher of the two valleys beside it (the next harmonic may be only a few bins away)
                double vl = p, vr = p;
                for (int j = 3; j <= 12; ++j) { vl = std::min (vl, pw[(size_t) (k - j)]); vr = std::min (vr, pw[(size_t) (k + j)]); }
                if (p < 4.0 * std::max (vl, vr)) continue;                          // not a harmonic: noise, a skirt
                const double l = 10.0 * std::log10 (p);
                if (l < lMax - 72.0) continue;                                      // too far down to be part of the sound
                const double f = k * binHz;
                const double target = std::min (lMax, lMax + s.tiltDbOct * std::log2 (f / fMax));
                const double g = std::clamp (heat * (target - l), -s.maxDb, s.maxDb);
                // the peak's main lobe (a Hann window: +-2 bins)
                for (int j = -2; j <= 2; ++j)
                {
                    auto& gd = gainDb[(size_t) (k + j)];
                    if (std::abs (g) > std::abs (gd)) gd = g;
                }
            }
            anchorHz.store ((float) fMax, std::memory_order_relaxed);
            anchorDb.store ((float) (lMax - 20.0 * std::log10 (kN / 4.0)), std::memory_order_relaxed);
        }
        // in time: half way per frame (about 10 ms), so a gain doesn't flutter from frame to frame
        for (int k = 0; k < kBins; ++k) gainSm[(size_t) k] += 0.5 * (gainDb[(size_t) k] - gainSm[(size_t) k]);

        // synthesis
        for (int c = 0; c < nc; ++c)
        {
            auto& st = ch[(size_t) c];
            for (int k = 0; k < kBins; ++k)
            {
                const double g = std::pow (10.0, gainSm[(size_t) k] / 20.0);
                st.spec[(size_t) k] *= g;
                if (k > 0 && k < kN / 2) st.spec[(size_t) (kN - k)] = std::conj (st.spec[(size_t) k]);
            }
            fft.run (st.spec.data(), true);
            for (int i = 0; i < kN; ++i) st.ola[(size_t) i] += st.spec[(size_t) i].real() * win[(size_t) i] * 0.5;   // sqrt-Hann twice at hop N/4 sums to 2
            for (int i = 0; i < kHop; ++i) st.outQ[(size_t) i] = st.ola[(size_t) i];
            std::move (st.ola.begin() + kHop, st.ola.end(), st.ola.begin());
            std::fill (st.ola.end() - kHop, st.ola.end(), 0.0);
            std::move (st.frame.begin() + kHop, st.frame.end(), st.frame.begin());
        }

        // display: per band, the loudest bin before and after (dB, scaled to about dBFS for a sine)
        const double ref = 20.0 * std::log10 (kN / 4.0);
        for (int b = 0; b < kBands; ++b)
        {
            const int k0 = std::max (1, (int) (bandEdge[(size_t) b] / binHz)), k1 = std::max (k0 + 1, (int) (bandEdge[(size_t) b + 1] / binHz));
            double pb = 0, pa = 0;
            for (int k = k0; k < std::min (k1, kBins); ++k)
            {
                pb = std::max (pb, pw[(size_t) k]);
                pa = std::max (pa, pw[(size_t) k] * std::pow (10.0, gainSm[(size_t) k] / 10.0));
            }
            bandBefore[(size_t) b].store ((float) (10.0 * std::log10 (pb + 1e-14) - ref), std::memory_order_relaxed);
            bandAfter[(size_t) b].store ((float) (10.0 * std::log10 (pa + 1e-14) - ref), std::memory_order_relaxed);
        }
        heatNow.store ((float) heat, std::memory_order_relaxed);
        frames.fetch_add (1, std::memory_order_relaxed);
    }
};
} // namespace pyre
