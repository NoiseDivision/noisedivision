#pragma once
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

// SPECTRAKILL's audio engine: a spectral compressor.
//
// The signal is cut into overlapping, windowed frames (square-root Hann, 75 % overlap) and
// turned into a spectrum with an FFT. Every frequency bin then gets its own compressor:
//   DOWN  pushes bins that rise above the threshold down towards it,
//   UP    lifts bins that sit below the threshold up towards it.
// The threshold is the THRESHOLD knob plus TILT (dB per octave around 1 kHz) plus the drawn
// curve. Each bin's gain follows its own ATTACK / RELEASE, and the gains are smoothed a little
// across neighbouring bins, which keeps the "watery" sound of spectral processing away.
// The frames are then turned back into sound and overlap-added; with nothing to compress the
// output is the input, delayed by the frame length (the latency reported to the host).
namespace sk
{
constexpr int kNumQualities = 5;
constexpr int kMaxCurvePoints = 8;
inline const char* qualityName (int q)
{
    static const char* n[] = { "LOW LATENCY", "NORMAL", "HIGH", "VERY HIGH", "ULTRA" };
    return n[std::clamp (q, 0, kNumQualities - 1)];
}

enum StereoMode { Stereo = 0, LeftRight, MidSide };
enum ClipMode { ClipOff = 0, ClipSoft, ClipHard };

struct CurvePoint { float freq = 1000.0f, db = 0.0f; };

struct Settings
{
    float thresholdDb = -24.0f;   // -60 .. 0 dBFS (per-bin level of a full-scale sine reads 0)
    float down = 0.5f;            // 0..1: 1 = bins are held at the threshold
    float up = 0.0f;              // 0..1: 1 = quiet bins are lifted all the way to the threshold
    float tiltDbPerOct = 0.0f;    // positive: the threshold falls towards the highs (they get hit harder)
    float attackMs = 10.0f, releaseMs = 120.0f;
    float sharpness = 0.5f;       // curve points: 0 = wide, gentle bells, 1 = narrow, focused ones
    float gainDb = 0.0f, mix = 1.0f;
    int quality = 2, stereo = Stereo, clip = ClipOff;
    bool sidechain = false;
    int numPoints = 0;
    std::array<CurvePoint, kMaxCurvePoints> points {};
};

// what the editor draws: levels per bin before and after, and the gain applied
struct Display
{
    int bins = 0;
    double binHz = 0.0;
    std::vector<float> inDb, outDb, gainDb;
};

class SpectralCompressor
{
public:
    static constexpr float kMaxBoostDb = 24.0f;    // UP never lifts a bin by more than this
    static constexpr float kMaxCutDb = 60.0f;
    static constexpr float kKneeDb = 6.0f;
    static constexpr float kFloorDb = -96.0f;      // bins this quiet are left alone by UP (it would just raise noise)

    // FFT length for a quality setting: 512 ... 8192 at 44.1 / 48 kHz, scaled up at higher rates
    static int fftSizeFor (int quality, double sampleRate)
    {
        const int base = 512 << std::clamp (quality, 0, kNumQualities - 1);
        int scale = 1;
        while (sampleRate / scale > 60000.0 && scale < 8) scale *= 2;
        return base * scale;
    }

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        juce::ignoreUnused (maxBlock);
        const int maxN = fftSizeFor (kNumQualities - 1, fs);
        for (int q = 0; q < kNumQualities; ++q)
        {
            const int n = fftSizeFor (q, fs);
            fft[q] = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 ((double) n)));
        }
        for (auto& c : chans)
        {
            c.inRing.assign ((size_t) maxN, 0.0f); c.scRing.assign ((size_t) maxN, 0.0f);
            c.outAcc.assign ((size_t) maxN, 0.0f); c.dry.assign ((size_t) maxN, 0.0f);
            c.frame.assign ((size_t) maxN * 2, 0.0f); c.scFrame.assign ((size_t) maxN * 2, 0.0f);
            c.gain.assign ((size_t) maxN / 2 + 1, 0.0f); c.applied.assign ((size_t) maxN / 2 + 1, 0.0f);
            c.mag.assign ((size_t) maxN / 2 + 1, 0.0f); c.det.assign ((size_t) maxN / 2 + 1, 0.0f);
        }
        window.assign ((size_t) maxN, 0.0f);
        thr.assign ((size_t) maxN / 2 + 1, 0.0f);
        scratch.assign ((size_t) maxN / 2 + 1, 0.0f);
        dispLevel.assign ((size_t) maxN / 2 + 1, 0.0f);
        dispOut.assign ((size_t) maxN / 2 + 1, 0.0f);
        display.inDb.assign ((size_t) maxN / 2 + 1, -120.0f);
        display.outDb.assign ((size_t) maxN / 2 + 1, -120.0f);
        display.gainDb.assign ((size_t) maxN / 2 + 1, 0.0f);
        calibrated.fill (false);
        for (int qq = 0; qq < kNumQualities; ++qq) configure (qq);
        configure (settings.quality);
    }

    void setSettings (const Settings& s)
    {
        const bool curveChanged = s.thresholdDb != settings.thresholdDb || s.tiltDbPerOct != settings.tiltDbPerOct
                               || s.sharpness != settings.sharpness || s.numPoints != settings.numPoints
                               || ! std::equal (s.points.begin(), s.points.begin() + s.numPoints, settings.points.begin(),
                                                [] (const CurvePoint& a, const CurvePoint& b) { return a.freq == b.freq && a.db == b.db; });
        const bool qualityChanged = s.quality != settings.quality;
        settings = s;
        if (qualityChanged) configure (s.quality);
        else if (curveChanged) buildThreshold();
        attackCoef = 1.0f - (float) std::exp (-(double) hop / (fs * std::max (0.1, (double) s.attackMs) * 0.001));
        releaseCoef = 1.0f - (float) std::exp (-(double) hop / (fs * std::max (1.0, (double) s.releaseMs) * 0.001));
        targetGain = (float) juce::Decibels::decibelsToGain (s.gainDb);
        targetMix = std::clamp (s.mix, 0.0f, 1.0f);
    }

    int getLatency() const noexcept { return n; }
    float gainAt (int channel, float hz) const noexcept   // current gain change (dB) of the bin nearest hz
    {
        const int k = std::clamp ((int) std::lround (hz / (fs / n)), 0, bins - 1);
        return chans[(size_t) std::clamp (channel, 0, 1)].applied[(size_t) k];
    }
    int getFftSize() const noexcept { return n; }

    // threshold (dBFS) at a frequency: THRESHOLD, plus TILT around 1 kHz, plus a bell for each
    // curve point (SHARPNESS sets the bells' width). The editor draws the same curve.
    static float thresholdFor (const Settings& st, float hz, int skipPoint = -1) noexcept
    {
        hz = std::max (hz, 10.0f);
        float t = st.thresholdDb - st.tiltDbPerOct * std::log2 (hz / 1000.0f);
        const float width = 2.0f - 1.8f * std::clamp (st.sharpness, 0.0f, 1.0f);   // bell width in octaves
        for (int i = 0; i < st.numPoints; ++i)
        {
            if (i == skipPoint) continue;
            const float d = std::log2 (hz / std::max (10.0f, st.points[(size_t) i].freq)) / width;
            t += st.points[(size_t) i].db * std::exp (-2.0f * d * d);
        }
        return t;
    }
    float thresholdAt (float hz) const noexcept { return thresholdFor (settings, hz); }

    // left/right in place; sidechain (may be null) is the detector input when SIDECHAIN is on
    void process (float* left, float* right, const float* scLeft, const float* scRight, int numSamples) noexcept
    {
        const bool useSc = settings.sidechain && scLeft != nullptr;
        const bool ms = settings.stereo == MidSide;
        for (int i = 0; i < numSamples; ++i)
        {
            float a = left[i], b = right != nullptr ? right[i] : left[i];
            float sa = useSc ? scLeft[i] : a, sb = useSc ? (scRight != nullptr ? scRight[i] : scLeft[i]) : b;
            const float dryA = a, dryB = b;
            if (ms) { const float m = 0.5f * (a + b), s = 0.5f * (a - b); a = m; b = s;
                      const float sm = 0.5f * (sa + sb), ss = 0.5f * (sa - sb); sa = sm; sb = ss; }

            auto& A = chans[0]; auto& B = chans[1];
            A.inRing[(size_t) writePos] = a; B.inRing[(size_t) writePos] = b;
            A.scRing[(size_t) writePos] = sa; B.scRing[(size_t) writePos] = sb;
            float wa = A.outAcc[(size_t) writePos], wb = B.outAcc[(size_t) writePos];
            A.outAcc[(size_t) writePos] = 0.0f; B.outAcc[(size_t) writePos] = 0.0f;
            // the dry signal waits exactly as long as the processed one
            const float da = A.dry[(size_t) writePos], db = B.dry[(size_t) writePos];
            A.dry[(size_t) writePos] = dryA; B.dry[(size_t) writePos] = dryB;
            writePos = (writePos + 1) % n;
            if (++hopCount == hop) { hopCount = 0; processFrame (useSc); }

            if (ms) { const float l = wa + wb, r = wa - wb; wa = l; wb = r; }

            mixNow += (targetMix - mixNow) * 0.002f;
            gainNow += (targetGain - gainNow) * 0.002f;
            float outA = (da + mixNow * (wa - da)) * gainNow;
            float outB = (db + mixNow * (wb - db)) * gainNow;
            outA = clip (outA); outB = clip (outB);
            left[i] = outA;
            if (right != nullptr) right[i] = outB;
        }
    }

    // the editor's copy of the latest frame (call from the message thread)
    bool copyDisplay (Display& out)
    {
        const juce::SpinLock::ScopedTryLockType lock (displayLock);
        if (! lock.isLocked() || ! displayFresh) return false;
        out.bins = display.bins; out.binHz = display.binHz;
        out.inDb.assign (display.inDb.begin(), display.inDb.begin() + display.bins);
        out.outDb.assign (display.outDb.begin(), display.outDb.begin() + display.bins);
        out.gainDb.assign (display.gainDb.begin(), display.gainDb.begin() + display.bins);
        displayFresh = false;
        return true;
    }

private:
    struct Channel
    {
        std::vector<float> inRing, scRing, outAcc, dry, frame, scFrame, gain, applied, mag, det;   // gain: per-bin state; applied: smoothed copy
        double lastEnergy = 0.0;
        int holdFrames = 0;
        bool onset = false;
    };

    void configure (int quality)
    {
        q = std::clamp (quality, 0, kNumQualities - 1);
        n = fftSizeFor (q, fs);
        hop = n / 4;
        bins = n / 2 + 1;
        // periodic square-root Hann: analysis x synthesis = Hann, which adds up to 2 at 75 % overlap
        for (int i = 0; i < n; ++i)
            window[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n));
        olaScale = 0.5f;
        // a full-scale sine centred on a bin reads 0 dB
        double wsum = 0.0; for (int i = 0; i < n; ++i) wsum += window[(size_t) i];
        levelNorm = (float) (2.0 / wsum);
        // calibrate the 3-bin energy reading with a full-scale test tone, on a bin and halfway between two
        // (done for every quality in prepare(), so switching quality while playing allocates nothing)
        if (calibrated[(size_t) q]) energyNorm = energyNormFor[(size_t) q];
        else
        {
            auto& f = *fft[(size_t) q];
            std::vector<float> buf ((size_t) n * 2);
            double readings = 0.0;
            for (double offset : { 0.0, 0.25, 0.5 })
            {
                const double bin = n / 8 + offset;
                for (int i = 0; i < n; ++i) buf[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * bin * i / n) * window[(size_t) i];
                std::fill (buf.begin() + n, buf.end(), 0.0f);
                f.performRealOnlyForwardTransform (buf.data(), true);
                const int k = (int) std::lround (bin);
                double best = 0.0;
                for (int c = k - 1; c <= k + 1; ++c)
                {
                    double e = 0.0;
                    for (int j = c - 1; j <= c + 1; ++j) { const double m = std::hypot (buf[(size_t) (2 * j)], buf[(size_t) (2 * j + 1)]) * levelNorm; e += m * m; }
                    best = std::max (best, std::sqrt (e));
                }
                readings += best;
            }
            energyNorm = energyNormFor[(size_t) q] = (float) (3.0 / readings);
            calibrated[(size_t) q] = true;
        }
        for (auto& c : chans)
        {
            std::fill (c.inRing.begin(), c.inRing.end(), 0.0f); std::fill (c.scRing.begin(), c.scRing.end(), 0.0f);
            std::fill (c.outAcc.begin(), c.outAcc.end(), 0.0f); std::fill (c.dry.begin(), c.dry.end(), 0.0f);
            std::fill (c.gain.begin(), c.gain.end(), 0.0f); std::fill (c.applied.begin(), c.applied.end(), 0.0f);
            c.lastEnergy = 0.0; c.onset = false; c.holdFrames = 0;
        }
        writePos = 0; hopCount = 0;
        buildThreshold();
        attackCoef = 1.0f - (float) std::exp (-(double) hop / (fs * std::max (0.1, (double) settings.attackMs) * 0.001));
        releaseCoef = 1.0f - (float) std::exp (-(double) hop / (fs * std::max (1.0, (double) settings.releaseMs) * 0.001));
    }

    void buildThreshold()
    {
        const double binHz = fs / n;
        for (int k = 0; k < bins; ++k)
            thr[(size_t) k] = thresholdAt ((float) std::max (binHz * k, 10.0));
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

    // per-bin gain change (dB) wanted for a level, before attack / release
    float targetFor (float levelDb, float t) const noexcept
    {
        float g = 0.0f;
        const float h = 0.5f * kKneeDb;
        // DOWN: soft knee around the threshold
        const float over = levelDb - t;
        if (settings.down > 0.0f)
        {
            if (over >= h) g -= settings.down * over;
            else if (over > -h) g -= settings.down * (over + h) * (over + h) / (4.0f * h);
        }
        // UP: quiet bins lifted towards the threshold, but never noise near the floor
        if (settings.up > 0.0f && levelDb > kFloorDb)
        {
            const float under = t - levelDb;
            float boost = 0.0f;
            if (under >= h) boost = settings.up * under;
            else if (under > -h) boost = settings.up * (under + h) * (under + h) / (4.0f * h);
            const float fade = std::clamp ((levelDb - kFloorDb) / 24.0f, 0.0f, 1.0f);
            g += std::min (boost, kMaxBoostDb) * fade;
        }
        return std::clamp (g, -kMaxCutDb, kMaxBoostDb);
    }

    // level of each bin from the energy of itself and its two neighbours, then the loudest reading nearby
    // (spread = false: the energy reading only, which the display uses to keep harmonics apart)
    void neighbourhoodLevel (const float* src, float* dst, bool spread = true) noexcept
    {
        for (int k = 0; k < bins; ++k)
        {
            const float a = src[std::max (0, k - 1)], b = src[k], d = src[std::min (bins - 1, k + 1)];
            scratch[(size_t) k] = std::sqrt (a * a + b * b + d * d) * energyNorm;
        }
        if (! spread) { std::copy (scratch.begin(), scratch.begin() + bins, dst); return; }
        for (int k = 0; k < bins; ++k)
            dst[k] = std::max ({ scratch[(size_t) std::max (0, k - 1)], scratch[(size_t) k], scratch[(size_t) std::min (bins - 1, k + 1)] });
    }

    void processFrame (bool useSc) noexcept
    {
        auto& fftNow = *fft[(size_t) q];
        const bool linked = settings.stereo == Stereo;

        // ---- spectra of the signal (and of the sidechain when it drives the detector)
        for (auto& c : chans)
        {
            for (int i = 0; i < n; ++i)
            {
                const int idx = (writePos + i) % n;               // oldest sample first
                c.frame[(size_t) i] = c.inRing[(size_t) idx] * window[(size_t) i];
                if (useSc) c.scFrame[(size_t) i] = c.scRing[(size_t) idx] * window[(size_t) i];
            }
            std::fill (c.frame.begin() + n, c.frame.begin() + 2 * n, 0.0f);
            fftNow.performRealOnlyForwardTransform (c.frame.data(), true);
            for (int k = 0; k < bins; ++k)
                c.mag[(size_t) k] = std::hypot (c.frame[(size_t) (2 * k)], c.frame[(size_t) (2 * k + 1)]) * levelNorm;
            if (useSc)
            {
                std::fill (c.scFrame.begin() + n, c.scFrame.begin() + 2 * n, 0.0f);
                fftNow.performRealOnlyForwardTransform (c.scFrame.data(), true);
                for (int k = 0; k < bins; ++k)
                    c.det[(size_t) k] = std::hypot (c.scFrame[(size_t) (2 * k)], c.scFrame[(size_t) (2 * k + 1)]) * levelNorm;
            }
            else
            {
                std::copy (c.mag.begin(), c.mag.begin() + bins, c.det.begin());
            }
        }
        // A tone smears over a few neighbouring bins. Each bin's level is read from the energy of
        // itself and its two neighbours (calibrated so a tone reads its true level wherever it falls
        // between bins), and then the loudest reading nearby is used, so all bins of a tone agree.
        for (auto& c : chans)
            neighbourhoodLevel (c.det.data(), c.det.data());
        if (linked)
            for (int k = 0; k < bins; ++k)
                chans[0].det[(size_t) k] = chans[1].det[(size_t) k] = std::max (chans[0].det[(size_t) k], chans[1].det[(size_t) k]);

        // ---- transient guard: when a frame is suddenly much louder than the last one, a hit is
        // arriving at its far end. Compressing this frame would smear the hit's gain change back
        // into the quiet part before it ("pre-echo"), so the compressors wait for the next frame,
        // which is centred on the hit.
        for (auto& c : chans)
        {
            double e = 0.0;
            for (int k = 0; k < bins; ++k) e += (double) c.det[(size_t) k] * c.det[(size_t) k];
            if (e > 4.0 * c.lastEnergy + 1.0e-12)          // more than 6 dB louder
                c.holdFrames = 2;                          // the hit is in the last quarter, then the last half
            c.onset = c.holdFrames > 0;
            if (c.holdFrames > 0) --c.holdFrames;
            c.lastEnergy = e;
        }

        // ---- one compressor per bin
        for (int ci = 0; ci < 2; ++ci)
        {
            auto& c = chans[(size_t) ci];
            const bool hold = linked ? (chans[0].onset || chans[1].onset) : c.onset;
            if (linked && ci == 1) { std::copy (chans[0].applied.begin(), chans[0].applied.begin() + bins, c.applied.begin()); }
            else
            {
                for (int k = 0; k < bins; ++k)
                {
                    const float lvl = 20.0f * std::log10 (c.det[(size_t) k] + 1.0e-9f);
                    const float target = targetFor (lvl, thr[(size_t) k]);
                    float& g = c.gain[(size_t) k];
                    if (target < g)
                    {
                        // during a hit the guard holds back new cuts (they would smear), but a lift
                        // from UP is still taken away at once, so the hit doesn't come through boosted
                        const float t = hold ? std::max (target, 0.0f) : target;
                        if (t < g) g += (t - g) * attackCoef;
                    }
                    else g += (target - g) * releaseCoef;
                }
                // the gains that get applied are smoothed a little across neighbouring bins (1-2-1, twice);
                // the per-bin state itself stays unsmoothed, so the attack and release times stay exact
                std::copy (c.gain.begin(), c.gain.begin() + bins, c.applied.begin());
                for (int pass = 0; pass < 2; ++pass)
                {
                    scratch[0] = c.applied[0];
                    for (int k = 1; k < bins - 1; ++k)
                        scratch[(size_t) k] = 0.25f * c.applied[(size_t) (k - 1)] + 0.5f * c.applied[(size_t) k] + 0.25f * c.applied[(size_t) (k + 1)];
                    scratch[(size_t) (bins - 1)] = c.applied[(size_t) (bins - 1)];
                    std::copy (scratch.begin(), scratch.begin() + bins, c.applied.begin());
                }
            }
            for (int k = 0; k < bins; ++k)
            {
                const float lin = std::pow (10.0f, c.applied[(size_t) k] * 0.05f);
                c.frame[(size_t) (2 * k)] *= lin;
                c.frame[(size_t) (2 * k + 1)] *= lin;
            }
        }

        // ---- display: channel A (left or mid), before / after / gain
        {
            const juce::SpinLock::ScopedTryLockType lock (displayLock);
            if (lock.isLocked())
            {
                display.bins = bins; display.binHz = fs / n;
                const auto& c = chans[0];
                const auto& c2 = chans[1];
                // before: the input's level; after: the level of what actually comes out
                for (int k = 0; k < bins; ++k) dispLevel[(size_t) k] = std::max (c.mag[(size_t) k], c2.mag[(size_t) k]);
                neighbourhoodLevel (dispLevel.data(), dispLevel.data(), false);
                for (int k = 0; k < bins; ++k)
                    dispOut[(size_t) k] = std::max (c.mag[(size_t) k] * std::pow (10.0f, c.applied[(size_t) k] * 0.05f),
                                                    c2.mag[(size_t) k] * std::pow (10.0f, c2.applied[(size_t) k] * 0.05f));
                neighbourhoodLevel (dispOut.data(), dispOut.data(), false);
                for (int k = 0; k < bins; ++k)
                {
                    const float ga = c.applied[(size_t) k], gb = c2.applied[(size_t) k];
                    display.inDb[(size_t) k] = 20.0f * std::log10 (dispLevel[(size_t) k] + 1.0e-9f);
                    display.outDb[(size_t) k] = 20.0f * std::log10 (dispOut[(size_t) k] + 1.0e-9f);
                    display.gainDb[(size_t) k] = std::abs (ga) > std::abs (gb) ? ga : gb;
                }
                displayFresh = true;
            }
        }

        // ---- back to sound, overlap-add
        for (auto& c : chans)
        {
            fftNow.performRealOnlyInverseTransform (c.frame.data());
            for (int i = 0; i < n; ++i)
            {
                const int idx = (writePos + i) % n;
                c.outAcc[(size_t) idx] += c.frame[(size_t) i] * window[(size_t) i] * olaScale;
            }
        }
    }

    double fs = 48000.0;
    std::array<std::unique_ptr<juce::dsp::FFT>, kNumQualities> fft;
    std::array<Channel, 2> chans;
    std::vector<float> window, thr, scratch, dispLevel, dispOut;
    Settings settings;
    int q = 2, n = 2048, hop = 512, bins = 1025, writePos = 0, hopCount = 0;
    std::array<float, kNumQualities> energyNormFor {};
    std::array<bool, kNumQualities> calibrated {};
    float olaScale = 0.5f, levelNorm = 1.0f, energyNorm = 1.0f, attackCoef = 0.1f, releaseCoef = 0.01f;
    float targetGain = 1.0f, gainNow = 1.0f, targetMix = 1.0f, mixNow = 1.0f;

    juce::SpinLock displayLock;
    Display display;
    bool displayFresh = false;
};
} // namespace sk
