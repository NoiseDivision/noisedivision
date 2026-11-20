#pragma once
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

// SHRAPNEL's audio engine: spectral distortion.
//
// Same framework as SPECTRAKILL: the sound is cut into overlapping windowed frames (square-root
// Hann, 75 % overlap), turned into a spectrum, processed bin by bin, turned back and overlap-added.
// With DRIVE at zero the output is the input, delayed by the frame length (reported as latency).
//
// Every bin gets its own distortion amount:
//   DRIVE  x  the drive curve (drawn points, x0 .. x3)  x  SPLIT (how much tonal, noise and
//   transient parts get hit)  x  DYNAMIC (louder bins get hit harder)
// and the selected MODE does the damage:
//   HARMONICS  adds 2nd..8th harmonics to every tonal peak (CHARACTER: even <-> odd), with no
//              intermodulation and no aliasing
//   CLIP       flattens each bin against the spectral envelope (CHARACTER: tight <-> flat)
//   CRUSH      levels snap to coarse steps, neighbouring bins merge into blocks (CHARACTER: block size)
//   SMEAR      bins are held and blurred over time and frequency (CHARACTER: frequency blur)
//   SCRAMBLE   phases randomised, bins randomly gated (CHARACTER: gating)
//   WARP       the spectrum is shifted and stretched so partials stop lining up (CHARACTER: squeeze <-> stretch)
// The wet level is kept close to the input level automatically, so DRIVE changes the sound,
// not the volume. TONE tilts the wet sound darker or brighter.
namespace sh
{
constexpr int kNumQualities = 3;
constexpr int kNumModes = 6;
constexpr int kMaxCurvePoints = 8;
enum Mode { Harmonics = 0, Clip, Crush, Smear, Scramble, Warp };
enum StereoMode { Stereo = 0, LeftRight, MidSide };
enum ClipMode { ClipOff = 0, ClipSoft, ClipHard };

inline const char* qualityName (int q) { static const char* n[] = { "LOW LATENCY", "NORMAL", "HIGH" }; return n[std::clamp (q, 0, kNumQualities - 1)]; }
inline const char* modeName (int m) { static const char* n[] = { "HARMONICS", "CLIP", "CRUSH", "SMEAR", "SCRAMBLE", "WARP" }; return n[std::clamp (m, 0, kNumModes - 1)]; }

struct CurvePoint { float freq = 1000.0f, amount = 0.0f; };   // amount -1 .. +1 (x0 .. x3)

struct Settings
{
    int mode = Harmonics;
    float drive = 0.5f;           // 0..1
    float character = 0.5f;       // 0..1, meaning depends on the mode
    float tone = 0.0f;            // -1..1: wet tilt, darker .. brighter
    float tonal = 0.9f, noise = 0.5f, transient = 0.2f;   // SPLIT: how much each kind of content gets hit
    float dynamic = 0.5f;         // 0..1: louder bins get hit harder
    float gainDb = 0.0f, mix = 1.0f;
    int quality = 1, stereo = Stereo, clip = ClipSoft;
    int numPoints = 0;
    std::array<CurvePoint, kMaxCurvePoints> points {};
};

// the curve's raw value at a frequency: the sum of the points' bells (each 1.2 octaves wide)
inline float curveSum (const Settings& st, float hz, int skipPoint = -1) noexcept
{
    hz = std::max (hz, 10.0f);
    float c = 0.0f;
    for (int i = 0; i < st.numPoints; ++i)
    {
        if (i == skipPoint) continue;
        const float d = std::log2 (hz / std::max (10.0f, st.points[(size_t) i].freq)) / 1.2f;
        c += st.points[(size_t) i].amount * std::exp (-2.0f * d * d);
    }
    return c;
}
// curve value <-> drive multiplier: -1 = x0 (spared), 0 = x1, +1 = x3
inline float multiplierFromCurve (float c) noexcept { c = std::clamp (c, -1.0f, 1.0f); return c >= 0.0f ? 1.0f + 2.0f * c : 1.0f + c; }
inline float curveFromMultiplier (float m) noexcept { m = std::clamp (m, 0.0f, 3.0f); return m >= 1.0f ? (m - 1.0f) * 0.5f : m - 1.0f; }

// drive multiplier for a frequency from the curve: 1 with no points, 0 .. 3 with them
inline float curveMultiplier (const Settings& st, float hz, int skipPoint = -1) noexcept
{
    return multiplierFromCurve (curveSum (st, hz, skipPoint));
}

// how hard a bin is hit before SPLIT and DYNAMIC: 1 = "full" (DRIVE 50 % on a flat curve)
inline float driveAt (const Settings& st, float hz) noexcept { return 2.0f * st.drive * curveMultiplier (st, hz); }

struct Display
{
    int bins = 0;
    double binHz = 0.0;
    std::vector<float> inDb, outDb, heat;    // heat: how hard each bin was hit (0..3)
};

class Engine
{
public:
    static int fftSizeFor (int quality, double sampleRate)
    {
        const int base = 512 << std::clamp (quality, 0, kNumQualities - 1);
        int scale = 1;
        while (sampleRate / scale > 60000.0 && scale < 8) scale *= 2;
        return base * scale;
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const int maxN = fftSizeFor (kNumQualities - 1, fs);
        const size_t maxB = (size_t) maxN / 2 + 1;
        for (int qq = 0; qq < kNumQualities; ++qq)
            fft[(size_t) qq] = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 ((double) fftSizeFor (qq, fs))));
        for (auto& c : chans)
        {
            c.inRing.assign ((size_t) maxN, 0.0f); c.outAcc.assign ((size_t) maxN, 0.0f); c.dry.assign ((size_t) maxN, 0.0f);
            c.frame.assign ((size_t) maxN * 2, 0.0f); c.out.assign ((size_t) maxN * 2, 0.0f);
            for (auto* v : { &c.mag, &c.phase, &c.prevPhase, &c.prevMag, &c.freq, &c.amount, &c.smear, &c.outPhase, &c.outMag, &c.outFreq })
                v->assign (maxB, 0.0f);
        }
        window.assign ((size_t) maxN, 0.0f);
        env.assign (maxB, 0.0f); prefix.assign (maxB + 1, 0.0);
        randA.assign (maxB, 0.0f); randB.assign (maxB, 0.0f); driveCurve.assign (maxB, 1.0f);
        display.inDb.assign (maxB, -120.0f); display.outDb.assign (maxB, -120.0f); display.heat.assign (maxB, 0.0f);
        configure (settings.quality);
    }

    void setSettings (const Settings& s)
    {
        const bool qualityChanged = s.quality != settings.quality;
        settings = s;
        if (qualityChanged) configure (s.quality);
        targetGain = (float) juce::Decibels::decibelsToGain (s.gainDb);
        targetMix = std::clamp (s.mix, 0.0f, 1.0f);
    }

    int getLatency() const noexcept { return n; }
    int getFftSize() const noexcept { return n; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        const bool ms = settings.stereo == MidSide;
        for (int i = 0; i < numSamples; ++i)
        {
            float a = left[i], b = right != nullptr ? right[i] : left[i];
            const float dryA = a, dryB = b;
            if (ms) { const float m = 0.5f * (a + b), sd = 0.5f * (a - b); a = m; b = sd; }
            auto& A = chans[0]; auto& B = chans[1];
            A.inRing[(size_t) writePos] = a; B.inRing[(size_t) writePos] = b;
            float wa = A.outAcc[(size_t) writePos], wb = B.outAcc[(size_t) writePos];
            A.outAcc[(size_t) writePos] = 0.0f; B.outAcc[(size_t) writePos] = 0.0f;
            const float da = A.dry[(size_t) writePos], db = B.dry[(size_t) writePos];
            A.dry[(size_t) writePos] = dryA; B.dry[(size_t) writePos] = dryB;
            writePos = (writePos + 1) % n;
            if (++hopCount == hop) { hopCount = 0; processFrame(); }
            wetGainNow += wetGainStep;           // set once per hop in processFrame(), ramped across it
            wa *= wetGainNow; wb *= wetGainNow;
            if (ms) { const float l = wa + wb, r = wa - wb; wa = l; wb = r; }
            mixNow += (targetMix - mixNow) * 0.002f;
            gainNow += (targetGain - gainNow) * 0.002f;
            left[i] = clip ((da + mixNow * (wa - da)) * gainNow);
            if (right != nullptr) right[i] = clip ((db + mixNow * (wb - db)) * gainNow);
        }
    }

    bool copyDisplay (Display& out)
    {
        const juce::SpinLock::ScopedTryLockType lock (displayLock);
        if (! lock.isLocked() || ! displayFresh) return false;
        out.bins = display.bins; out.binHz = display.binHz;
        out.inDb.assign (display.inDb.begin(), display.inDb.begin() + display.bins);
        out.outDb.assign (display.outDb.begin(), display.outDb.begin() + display.bins);
        out.heat.assign (display.heat.begin(), display.heat.begin() + display.bins);
        displayFresh = false;
        return true;
    }

private:
    struct Channel
    {
        std::vector<float> inRing, outAcc, dry, frame, out;
        std::vector<float> mag, phase, prevPhase, prevMag, freq, amount, smear, outPhase, outMag, outFreq;
        float autoGain = 1.0f;
    };

    static float princarg (float p) noexcept
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        return p - twoPi * std::floor ((p + juce::MathConstants<float>::pi) / twoPi);
    }

    void configure (int quality)
    {
        q = std::clamp (quality, 0, kNumQualities - 1);
        n = fftSizeFor (q, fs);
        hop = n / 4;
        bins = n / 2 + 1;
        for (int i = 0; i < n; ++i)
            window[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n));
        double wsum = 0.0; for (int i = 0; i < n; ++i) wsum += window[(size_t) i];
        levelNorm = (float) (2.0 / wsum);
        for (auto& c : chans)
        {
            for (auto* v : { &c.inRing, &c.outAcc, &c.dry }) std::fill (v->begin(), v->end(), 0.0f);
            for (auto* v : { &c.prevPhase, &c.prevMag, &c.smear, &c.outPhase }) std::fill (v->begin(), v->end(), 0.0f);
            c.autoGain = 1.0f;
        }
        writePos = 0; hopCount = 0;
        wetGain = wetGainNow = 1.0f; wetGainStep = 0.0f; dryAvg = wetAvg = 0.0f;
        matchCoef = 1.0f - std::exp (-(float) hop / (float) (fs * 0.06));
    }

    float clip (float x) const noexcept
    {
        constexpr float c = 0.98855f;   // -0.1 dBFS
        if (settings.clip == ClipHard) return std::clamp (x, -c, c);
        if (settings.clip == ClipSoft)
        {
            const float u = std::abs (x), w = 0.413f * c, lo = c - 0.5f * w;
            float y;
            if (u <= lo) y = u;
            else if (u >= c + 0.5f * w) y = c;
            else { const float d = u - lo; y = u - d * d / (2.0f * w); }
            return std::copysign (y, x);
        }
        return x;
    }

    // box-average of a channel's magnitudes over +-radius bins (used as the spectral envelope)
    void envelope (const std::vector<float>& m, int radius) noexcept
    {
        prefix[0] = 0.0;
        for (int k = 0; k < bins; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + m[(size_t) k];
        for (int k = 0; k < bins; ++k)
        {
            const int a = std::max (0, k - radius), b = std::min (bins - 1, k + radius);
            env[(size_t) k] = (float) ((prefix[(size_t) b + 1] - prefix[(size_t) a]) / (b - a + 1));
        }
    }

    // how hard each bin of a channel gets hit: drive curve x split x dynamic
    void amounts (Channel& c) noexcept
    {
        const double binHz = fs / n;
        envelope (c.mag, 8);
        const float invNorm = levelNorm;
        for (int k = 0; k < bins; ++k)
        {
            const float m = c.mag[(size_t) k];
            // tonal: a peak standing well above its surroundings (a tone also lights its two neighbours)
            float peak = 0.0f;
            for (int j = std::max (1, k - 1); j <= std::min (bins - 2, k + 1); ++j)
            {
                const float mj = c.mag[(size_t) j];
                if (mj >= c.mag[(size_t) j - 1] && mj >= c.mag[(size_t) j + 1])
                    peak = std::max (peak, std::clamp ((20.0f * std::log10 ((mj + 1.0e-9f) / (env[(size_t) j] + 1.0e-9f)) - 3.0f) / 6.0f, 0.0f, 1.0f));
            }
            // transient: the bin jumped up since the last frame
            const float jump = 20.0f * std::log10 ((m + 1.0e-9f) / (c.prevMag[(size_t) k] + 1.0e-9f));
            const float tr = std::clamp ((jump - 4.0f) / 8.0f, 0.0f, 1.0f);
            const float split = tr * settings.transient + (1.0f - tr) * (peak * settings.tonal + (1.0f - peak) * settings.noise);
            const float levelDb = 20.0f * std::log10 (m * invNorm + 1.0e-9f);
            const float lf = std::clamp ((levelDb + 54.0f) / 36.0f, 0.0f, 1.6f);
            const float dyn = 1.0f + settings.dynamic * (lf - 1.0f);
            c.amount[(size_t) k] = std::clamp (driveCurve[(size_t) k] * split * dyn, 0.0f, 3.0f);
            juce::ignoreUnused (binHz);
        }
    }

    void processFrame() noexcept
    {
        auto& f = *fft[(size_t) q];
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float hopF = (float) hop;
        const bool linked = settings.stereo == Stereo;

        // drive curve per bin
        for (int k = 0; k < bins; ++k) driveCurve[(size_t) k] = driveAt (settings, (float) std::max (10.0, fs / n * k));

        // shared randomness (linked stereo uses the same for both sides so the image stays put)
        for (int k = 0; k < bins; ++k) { randA[(size_t) k] = rng.nextFloat() * 2.0f - 1.0f; randB[(size_t) k] = rng.nextFloat(); }

        float inEnergy[2] = {}, outEnergy[2] = {};
        for (int ci = 0; ci < 2; ++ci)
        {
            auto& c = chans[(size_t) ci];
            for (int i = 0; i < n; ++i)
                c.frame[(size_t) i] = c.inRing[(size_t) ((writePos + i) % n)] * window[(size_t) i];
            std::fill (c.frame.begin() + n, c.frame.begin() + 2 * n, 0.0f);
            f.performRealOnlyForwardTransform (c.frame.data(), true);
            for (int k = 0; k < bins; ++k)
            {
                const float re = c.frame[(size_t) (2 * k)], im = c.frame[(size_t) (2 * k + 1)];
                c.mag[(size_t) k] = std::sqrt (re * re + im * im);
                c.phase[(size_t) k] = std::atan2 (im, re);
                // true frequency of the bin (radians per sample), from how far its phase moved since the last frame
                const float expected = twoPi * (float) k / (float) n;
                c.freq[(size_t) k] = expected + princarg (c.phase[(size_t) k] - c.prevPhase[(size_t) k] - expected * hopF) / hopF;
                inEnergy[ci] += c.mag[(size_t) k] * c.mag[(size_t) k];
            }
            amounts (c);
        }
        if (linked)
            for (int k = 0; k < bins; ++k)
                chans[0].amount[(size_t) k] = chans[1].amount[(size_t) k] = std::max (chans[0].amount[(size_t) k], chans[1].amount[(size_t) k]);

        for (int ci = 0; ci < 2; ++ci)
        {
            auto& c = chans[(size_t) ci];
            if (! linked && ci == 1)
                for (int k = 0; k < bins; ++k) { randA[(size_t) k] = rng.nextFloat() * 2.0f - 1.0f; randB[(size_t) k] = rng.nextFloat(); }
            std::copy (c.mag.begin(), c.mag.begin() + bins, c.outMag.begin());
            std::copy (c.phase.begin(), c.phase.begin() + bins, c.outFreq.begin());   // outFreq doubles as the output phase here
            switch (settings.mode)
            {
                case Harmonics: harmonics (c); break;
                case Clip:      clipMode (c); break;
                case Crush:     crush (c); break;
                case Smear:     smear (c); break;
                case Scramble:  scramble (c); break;
                case Warp:      warp (c); break;
                default: break;
            }
            // TONE: tilt the wet sound around 1 kHz (+-4 dB per octave at the ends)
            const double binHz = fs / n;
            for (int k = 0; k < bins; ++k)
            {
                float m = c.outMag[(size_t) k];
                if (settings.tone != 0.0f)
                {
                    const float oct = std::log2 ((float) std::max (20.0, binHz * k) / 1000.0f);
                    m *= std::pow (10.0f, std::clamp (settings.tone * 4.0f * oct, -18.0f, 12.0f) / 20.0f);
                }
                c.outMag[(size_t) k] = m;
                outEnergy[ci] += m * m;
            }
        }

        // every frame comes out with the energy it went in with, so the distortion changes the sound,
        // not its level, and hits keep their dynamics (a slower match on the sound itself in process()
        // then evens out what overlap-adding changes, e.g. with random phases)
        for (int ci = 0; ci < 2; ++ci)
        {
            auto& c = chans[(size_t) ci];
            const float ein = linked ? inEnergy[0] + inEnergy[1] : inEnergy[ci];
            const float eout = linked ? outEnergy[0] + outEnergy[1] : outEnergy[ci];
            c.autoGain = eout > 1.0e-12f && ein > 1.0e-12f ? std::clamp (std::sqrt (ein / eout), 0.125f, 8.0f) : 1.0f;
        }

        // display (before the inverse transform)
        {
            const juce::SpinLock::ScopedTryLockType lock (displayLock);
            if (lock.isLocked())
            {
                display.bins = bins; display.binHz = fs / n;
                for (int k = 0; k < bins; ++k)
                {
                    const float mi = std::max (chans[0].mag[(size_t) k], chans[1].mag[(size_t) k]) * levelNorm;
                    const float mo = std::max (chans[0].outMag[(size_t) k] * chans[0].autoGain, chans[1].outMag[(size_t) k] * chans[1].autoGain) * levelNorm * wetGain;
                    display.inDb[(size_t) k] = 20.0f * std::log10 (mi + 1.0e-9f);
                    display.outDb[(size_t) k] = 20.0f * std::log10 (mo + 1.0e-9f);
                    display.heat[(size_t) k] = std::max (chans[0].amount[(size_t) k], chans[1].amount[(size_t) k]);
                }
                displayFresh = true;
            }
        }

        for (auto& c : chans)
        {
            for (int k = 0; k < bins; ++k)
            {
                const float m = c.outMag[(size_t) k] * c.autoGain, p = c.outFreq[(size_t) k];
                c.frame[(size_t) (2 * k)] = m * std::cos (p);
                c.frame[(size_t) (2 * k + 1)] = m * std::sin (p);
            }
            c.frame[1] = 0.0f; c.frame[(size_t) (2 * (bins - 1) + 1)] = 0.0f;
            f.performRealOnlyInverseTransform (c.frame.data());
            for (int i = 0; i < n; ++i)
            {
                const int idx = (writePos + i) % n;
                c.outAcc[(size_t) idx] += c.frame[(size_t) i] * window[(size_t) i] * 0.5f;
            }
            std::copy (c.phase.begin(), c.phase.begin() + bins, c.prevPhase.begin());
            std::copy (c.mag.begin(), c.mag.begin() + bins, c.prevMag.begin());
        }

        // Loudness match on the actual sound. The next hop of output is now complete (in outAcc), and so
        // is the dry sound it lines up with, so its gain is set before it plays: no lag, no overshoot.
        // A slow average evens out what overlap-adding changes (random phases come out quieter), and
        // no hop may come out more than 3 dB louder than the dry sound.
        float dryNext = 0.0f, wetNext = 0.0f;
        for (int i = 0; i < hop; ++i)
        {
            const size_t idx = (size_t) ((writePos + i) % n);
            for (auto& c : chans) { dryNext += c.dry[idx] * c.dry[idx]; wetNext += c.outAcc[idx] * c.outAcc[idx]; }
        }
        if (settings.stereo == MidSide) wetNext *= 2.0f;
        if (dryNext > 4.0f * dryAvg) { dryAvg = dryNext; wetAvg = wetNext; }      // a hit: measure it at once
        else { dryAvg += (dryNext - dryAvg) * matchCoef; wetAvg += (wetNext - wetAvg) * matchCoef; }
        float target = wetGain;
        if (dryAvg > 1.0e-7f * (float) hop && wetAvg > 1.0e-12f)
            target = std::clamp (std::sqrt (dryAvg / wetAvg), 0.5f, 2.0f);
        if (dryNext > 1.0e-9f && wetNext > 1.0e-12f)
            target = std::min (target, std::sqrt (2.0f * dryNext / wetNext));
        wetGain = target;
        wetGainStep = (target - wetGainNow) / (float) hop;
    }

    // ---------------------------------------------------------------- the modes
    // (outMag / outFreq-as-phase start as a copy of the input; each mode changes them)

    // HARMONICS: every tonal peak gets harmonics 2..8 at its true frequency. A harmonic's phase is
    // h times the fundamental's, so it stays locked to it from frame to frame. Nothing is created
    // above Nyquist (no aliasing) and no sum/difference tones appear (no intermodulation).
    void harmonics (Channel& c) noexcept
    {
        const float pi = juce::MathConstants<float>::pi;
        const float even = 1.0f - settings.character, odd = settings.character;
        for (int k = 2; k < bins - 2; ++k)
        {
            const float m = c.mag[(size_t) k];
            if (m < c.mag[(size_t) k - 1] || m < c.mag[(size_t) k + 1] || m < c.mag[(size_t) k - 2] || m < c.mag[(size_t) k + 2]) continue;
            const float a = c.amount[(size_t) k];
            if (a < 0.01f || m * levelNorm < 1.0e-4f) continue;          // quieter than -80 dB: leave it
            const float w = c.freq[(size_t) k];
            if (w <= 0.0f) continue;
            const float base = 0.6f * a * 1.23f;
            for (int h = 2; h <= 8; ++h)
            {
                const float hw = w * (float) h;
                if (hw >= pi * 0.98f) break;
                const int kk = (int) std::lround (hw * (float) n / juce::MathConstants<float>::twoPi);
                if (kk <= 0 || kk >= bins - 1) break;
                const float amp = m * base * ((h % 2 == 0) ? even : odd) / std::pow ((float) h, 0.9f);
                if (amp <= 0.0f) continue;
                const float ph = (float) h * c.phase[(size_t) k] + pi * (float) (kk - h * k);
                // add as a complex value onto what is already there
                const float re = c.outMag[(size_t) kk] * std::cos (c.outFreq[(size_t) kk]) + amp * std::cos (ph);
                const float im = c.outMag[(size_t) kk] * std::sin (c.outFreq[(size_t) kk]) + amp * std::sin (ph);
                c.outMag[(size_t) kk] = std::sqrt (re * re + im * im);
                c.outFreq[(size_t) kk] = std::atan2 (im, re);
            }
        }
    }

    // CLIP: each bin is pulled towards the spectral envelope, so quiet partials and noise come up to
    // meet the loud ones: aggressive and flat. CHARACTER sets the envelope width (tight .. flat).
    void clipMode (Channel& c) noexcept
    {
        envelope (c.mag, 2 + (int) std::lround (settings.character * 40.0f * (float) n / 1024.0f));
        for (int k = 0; k < bins; ++k)
        {
            const float a = c.amount[(size_t) k];
            if (a < 0.001f) continue;
            const float alpha = 0.92f * (1.0f - std::exp (-1.2f * a));
            const float e = env[(size_t) k] + 1.0e-9f, x = c.mag[(size_t) k] / e;
            c.outMag[(size_t) k] = e * std::pow (x + 1.0e-9f, 1.0f - alpha);
        }
    }

    // CRUSH: a spectral bitcrusher. Levels snap to steps (6..24 dB), and bins merge into blocks
    // that share one level (CHARACTER: block size).
    void crush (Channel& c) noexcept
    {
        const int maxBlock = 1 + (int) std::lround (settings.character * 24.0f * (float) n / 1024.0f);
        for (int k = 0; k < bins;)
        {
            const float a = c.amount[(size_t) k];
            const float b = std::min (1.0f, a);
            const int block = std::max (1, (int) std::lround (1.0f + (float) (maxBlock - 1) * b));
            float peak = 0.0f;
            for (int j = k; j < std::min (bins, k + block); ++j) peak = std::max (peak, c.mag[(size_t) j]);
            const float step = 6.0f + 18.0f * std::min (1.0f, a / 2.0f);
            const float lv = 20.0f * std::log10 (peak * levelNorm + 1.0e-9f);
            const float qv = std::pow (10.0f, std::round (lv / step) * step / 20.0f) / levelNorm;
            for (int j = k; j < std::min (bins, k + block); ++j)
                c.outMag[(size_t) j] = c.mag[(size_t) j] + b * (qv - c.mag[(size_t) j]);
            k += block;
        }
    }

    // SMEAR: every bin is held and released slowly (time smear), then blurred across neighbouring
    // bins (CHARACTER), with a little phase drift: washed, broken-tape textures.
    void smear (Channel& c) noexcept
    {
        for (int k = 0; k < bins; ++k)
        {
            const float a = c.amount[(size_t) k];
            const float hold = 0.55f + 0.42f * std::min (1.0f, a / 1.5f);
            float& s = c.smear[(size_t) k];
            s = std::max (c.mag[(size_t) k] * (1.0f - hold) + s * hold, c.mag[(size_t) k] * (1.0f - std::min (1.0f, a)));
        }
        envelope (c.smear, (int) std::lround (settings.character * 10.0f * (float) n / 1024.0f));
        for (int k = 0; k < bins; ++k)
        {
            const float b = std::min (1.0f, c.amount[(size_t) k]);
            c.outMag[(size_t) k] = c.mag[(size_t) k] + b * (env[(size_t) k] - c.mag[(size_t) k]);
            c.outFreq[(size_t) k] = c.phase[(size_t) k] + b * settings.character * 0.6f * juce::MathConstants<float>::pi * randA[(size_t) k];
        }
    }

    // SCRAMBLE: phases are randomised (whispery, breathy, then noise shaped like the sound) and
    // bins are randomly gated out (CHARACTER), keeping the energy the same.
    void scramble (Channel& c) noexcept
    {
        const float pi = juce::MathConstants<float>::pi;
        for (int k = 0; k < bins; ++k)
        {
            const float b = std::min (1.0f, c.amount[(size_t) k]);
            if (b < 0.001f) continue;
            c.outFreq[(size_t) k] = c.phase[(size_t) k] + b * pi * randA[(size_t) k];
            const float keep = 1.0f - 0.8f * settings.character * b;
            c.outMag[(size_t) k] = randB[(size_t) k] < keep ? c.mag[(size_t) k] / std::sqrt (keep) : 0.0f;
        }
    }

    // WARP: each bin's true frequency is moved: shifted up (ring-mod-like) and stretched or squeezed
    // around 500 Hz (CHARACTER), so harmonics stop lining up: metallic, bell-like. Output phases are
    // accumulated at the new frequencies so the moved partials stay clean.
    void warp (Channel& c) noexcept
    {
        const float twoPi = juce::MathConstants<float>::twoPi, pi = juce::MathConstants<float>::pi;
        std::vector<float>& acc = c.out;            // re/im accumulators for the moved bins
        std::fill (acc.begin(), acc.begin() + 2 * bins, 0.0f);
        std::fill (c.outMag.begin(), c.outMag.begin() + bins, 0.0f);
        std::vector<float>& newW = env;             // frequency of the strongest bin landing on each output bin
        std::fill (newW.begin(), newW.begin() + bins, -1.0f);
        std::vector<float>& best = randB;           // (randB is not needed in this mode)
        std::fill (best.begin(), best.begin() + bins, 0.0f);
        const float beta = (settings.character - 0.5f) * 0.6f;
        for (int k = 0; k < bins; ++k)
        {
            const float m = c.mag[(size_t) k];
            const float b = std::min (1.0f, c.amount[(size_t) k]);
            const float stay = m * (1.0f - b), move = m * b;
            if (stay > 0.0f)
            {
                acc[(size_t) (2 * k)] += stay * std::cos (c.phase[(size_t) k]);
                acc[(size_t) (2 * k + 1)] += stay * std::sin (c.phase[(size_t) k]);
            }
            if (move <= 0.0f) continue;
            const float w = std::max (0.0f, c.freq[(size_t) k]);
            const float a = std::min (3.0f, c.amount[(size_t) k]);
            const float hz = w * (float) fs / twoPi;
            float nhz = hz * std::pow (std::max (hz, 20.0f) / 500.0f, beta * std::min (1.0f, a)) + 70.0f * a;
            const float nyq = (float) fs * 0.5f;
            if (nhz > nyq) nhz = 2.0f * nyq - nhz;                    // fold back down instead of aliasing
            if (nhz <= 0.0f) continue;
            const float nw = nhz * twoPi / (float) fs;
            const int kk = std::clamp ((int) std::lround (nw * (float) n / twoPi), 0, bins - 1);
            if (move > best[(size_t) kk]) { best[(size_t) kk] = move; newW[(size_t) kk] = nw; }
            c.outMag[(size_t) kk] += move;                            // collected now, given a phase below
        }
        for (int k = 0; k < bins; ++k)
        {
            float& ph = c.outPhase[(size_t) k];
            if (newW[(size_t) k] >= 0.0f) ph = princarg (ph + newW[(size_t) k] * (float) hop);
            const float mv = c.outMag[(size_t) k];
            const float re = acc[(size_t) (2 * k)] + mv * std::cos (ph), im = acc[(size_t) (2 * k + 1)] + mv * std::sin (ph);
            c.outMag[(size_t) k] = std::sqrt (re * re + im * im);
            c.outFreq[(size_t) k] = std::atan2 (im, re);
        }
        juce::ignoreUnused (pi);
    }

    double fs = 48000.0;
    std::array<std::unique_ptr<juce::dsp::FFT>, kNumQualities> fft;
    std::array<Channel, 2> chans;
    std::vector<float> window, env, randA, randB, driveCurve;
    std::vector<double> prefix;
    Settings settings;
    juce::Random rng { 1234 };
    int q = 1, n = 1024, hop = 256, bins = 513, writePos = 0, hopCount = 0;
    float levelNorm = 1.0f;
    float wetGain = 1.0f, wetGainNow = 1.0f, wetGainStep = 0.0f, dryAvg = 0.0f, wetAvg = 0.0f, matchCoef = 0.01f;
    float targetGain = 1.0f, gainNow = 1.0f, targetMix = 1.0f, mixNow = 1.0f;

    juce::SpinLock displayLock;
    Display display;
    bool displayFresh = false;
};
} // namespace sh
