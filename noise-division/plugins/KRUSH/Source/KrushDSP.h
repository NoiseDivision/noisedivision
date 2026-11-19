#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// KRUSH audio engine. Plain C++ with no JUCE dependency, so it can be tested on its own.
//
// Everything here runs at the oversampled rate (the plugin wraps it in 4x oversampling):
//
//   in -> 4-band crossover (phase-coherent Linkwitz-Riley, 24 dB/oct)
//      -> per band: drive -> bias -> distortion (9 modes) -> gain -> band mix -> solo/bypass
//      -> sum -> DC blocker -> tone filter (HPF/LPF, distorted signal only) -> global dry/wet mix
//
// Fewer than four bands are made by parking the unused crossovers far above the audio
// range, so changing the band count glides smoothly instead of switching structure.
namespace krush
{
constexpr int kMaxBands = 4;
constexpr int kNumModes = 11;

enum Mode { Tube, SoftClip, HardClip, Asymmetric, Diode, SineShaper, Downsample, Saturation, Overdrive, Krush, None };   // new modes go at the end, so saved settings keep their meaning

inline const char* modeName (int m)
{
    static const char* names[kNumModes] = { "Tube", "Soft Clip", "Hard Clip", "Asymmetric", "Diode",
                                            "Sine Shaper", "Downsample", "Saturation", "Overdrive", "Krush", "None" };
    return names[std::clamp (m, 0, kNumModes - 1)];
}

// what the SHAPE control does in each mode
inline const char* shapeName (int m)
{
    static const char* names[kNumModes] = { "WARMTH", "KNEE", "FOLDBACK", "ASYMMETRY", "THRESHOLD",
                                            "FOLDS", "BITS", "SOFTNESS", "GRIT", "GNARL", "SHAPE" };
    return names[std::clamp (m, 0, kNumModes - 1)];
}

inline const char* modeDescription (int m)
{
    static const char* text[kNumModes] = {
        "Tube: smooth, warm saturation. The negative half compresses earlier, adding even harmonics. SHAPE adds warmth (more asymmetry).",
        "Soft Clip: rounds peaks off smoothly. SHAPE sets the knee, from very soft to firm.",
        "Hard Clip: flat-tops everything above the ceiling. SHAPE folds the peaks back down for a harsher, buzzier edge.",
        "Asymmetric: hard clipping on the top half, soft compression on the bottom half. SHAPE lowers the bottom half's ceiling.",
        "Diode: a single-diode clipper. The top half clips sharply, the bottom stays open. SHAPE lowers the diode's threshold.",
        "Sine Shaper: wave folding through a sine curve. Loud signals fold over into bright, metallic overtones. SHAPE adds more folds.",
        "Downsample: sample-rate reduction and bit crushing. DRIVE lowers the sample rate, SHAPE lowers the bit depth.",
        "Saturation: tape-like, gentle and smooth. SHAPE makes it softer and more compressed.",
        "Overdrive: a pedal-style curve with a firm top. SHAPE adds grit, making the knee sharper.",
        "Krush: the signature mode. A hot, lopsided saturation that folds back on itself and is partly rectified, "
        "so it snarls with octave-up grit even on quiet parts. SHAPE (GNARL) goes from heavy to completely unhinged.",
        "None: no distortion. The band passes through clean; its GAIN and MIX still work." };
    return text[std::clamp (m, 0, kNumModes - 1)];
}

//==============================================================================
// Static distortion curves. x is the driven signal, s the SHAPE value (0..1).
// Every curve has a slope of about 1 at zero and stays within -1..1.
inline float shapeCurve (int mode, float x, float s) noexcept
{
    const float ax = std::abs (x), sg = x < 0.0f ? -1.0f : 1.0f;
    switch (mode)
    {
        case Tube:
        {
            const float a = 1.0f + 3.0f * s;                       // negative half saturates at -1/a
            return x >= 0.0f ? 1.0f - std::exp (-x) : -(1.0f - std::exp (a * x)) / a;
        }
        case SoftClip:
        {
            const float p = 1.5f + 8.5f * s;
            return x / std::pow (1.0f + std::pow (ax, p), 1.0f / p);
        }
        case HardClip:
        {
            if (ax <= 1.0f) return x;
            // above the ceiling: flat at SHAPE 0; with more SHAPE the peaks fold back and forth between +-1
            const float z = std::fmod (s * (ax - 1.0f), 4.0f);
            return sg * (1.0f - (2.0f - std::abs (z - 2.0f)));
        }
        case Asymmetric:
        {
            const float floorLevel = 1.0f - 0.7f * s;
            return x >= 0.0f ? std::min (x, 1.0f) : -floorLevel * std::tanh (-x / floorLevel);
        }
        case Diode:
        {
            const float v = 1.0f - 0.75f * s;
            return x >= 0.0f ? v * (1.0f - std::exp (-x / v)) : -std::tanh (-x);
        }
        case SineShaper:
            return std::sin (1.5707963f * (1.0f + 3.0f * s) * x);
        case Saturation:
        {
            const float t = std::tanh (x);
            return t + s * (x / (1.0f + ax) - t);
        }
        case Overdrive:
        {
            const float n = 3.0f + 6.0f * s, c = n / (n - 1.0f);
            const float u = ax / c;
            return u >= 1.0f ? sg : sg * c * (u - std::pow (u, n) / n);
        }
        case Krush:
        {
            // 1. hot, lopsided saturation (even harmonics), 2. folded once it saturates,
            // 3. partly rectified for an octave-up snarl, blended over the saturated body
            const float t = std::tanh (x);
            const float body = (std::tanh (1.6f * x) + 0.2f * t * t) / 1.2f;
            const float fold = std::sin (1.5707963f * body * (1.6f + 1.6f * s));
            const float snarl = 0.7f * fold + 0.3f * (2.0f * std::abs (fold) - 1.0f);
            const float m = 0.35f + 0.4f * s;
            return (1.0f - m) * body + m * snarl;
        }
        default:
            return x;                                               // Downsample is handled separately
    }
}

// sample-rate reduction factor for Downsample, from DRIVE (0..36 dB): 1x .. 64x
inline float downsampleFactor (float driveDb) noexcept { return std::exp2 (6.0f * std::clamp (driveDb / 36.0f, 0.0f, 1.0f)); }
// bit depth for Downsample, from SHAPE: 0 = no crushing, 1 = 4 bits
inline float crushBits (float s) noexcept { return 16.0f - 12.0f * s; }

//==============================================================================
struct Smooth
{
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int left = 0, length = 1;

    void setLength (int samples) noexcept { length = std::max (1, samples); }
    void snap (float v) noexcept { current = target = v; left = 0; }
    void set (float v) noexcept
    {
        if (v == target) return;
        target = v; left = length; step = (target - current) / (float) length;
    }
    float next() noexcept
    {
        if (left > 0) { current += step; if (--left == 0) current = target; }
        return current;
    }
};

class Biquad
{
public:
    void lowpass (double fc, double fs) noexcept  { design (fc, fs, false); }
    void highpass (double fc, double fs) noexcept { design (fc, fs, true); }
    void reset() noexcept { z1 = z2 = 0.0; }
    double tick (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
private:
    void design (double fc, double fs, bool hp) noexcept   // 2nd-order Butterworth
    {
        const double w = 2.0 * 3.14159265358979323846 * std::min (fc, fs * 0.45) / fs;
        const double c = std::cos (w), al = std::sin (w) / 1.4142135623730951, a0 = 1.0 + al;
        if (hp) { b0 = (1.0 + c) * 0.5 / a0; b1 = -(1.0 + c) / a0; }
        else    { b0 = (1.0 - c) * 0.5 / a0; b1 =  (1.0 - c) / a0; }
        b2 = b0; a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

struct LR4      // 4th-order Linkwitz-Riley = two Butterworth sections
{
    Biquad a, b;
    void lowpass (double fc, double fs) noexcept  { a.lowpass (fc, fs);  b.lowpass (fc, fs); }
    void highpass (double fc, double fs) noexcept { a.highpass (fc, fs); b.highpass (fc, fs); }
    void reset() noexcept { a.reset(); b.reset(); }
    double tick (double x) noexcept { return b.tick (a.tick (x)); }
};

struct AllpassLR4   // the phase shift of one crossover, applied to bands below it so all bands stay aligned
{
    LR4 lp, hp;
    void set (double fc, double fs) noexcept { lp.lowpass (fc, fs); hp.highpass (fc, fs); }
    void reset() noexcept { lp.reset(); hp.reset(); }
    double tick (double x) noexcept { return lp.tick (x) + hp.tick (x); }
};

// 2nd-order state-variable filter section (topology-preserving transform)
class SVF
{
public:
    void set (double fc, double fs, double q, bool highpass) noexcept
    {
        hp = highpass;
        g = std::tan (3.14159265358979323846 * std::min (fc, fs * 0.45) / fs);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() noexcept { ic1 = ic2 = 0.0; }
    double tick (double x) noexcept
    {
        const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1; ic2 = 2.0 * v2 - ic2;
        return hp ? x - k * v1 - v2 : v2;
    }
private:
    double g = 0, k = 1.414, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    bool hp = false;
};

// 1st-order section (6 dB/oct)
class OnePole
{
public:
    void set (double fc, double fs, bool highpass) noexcept
    {
        hp = highpass;
        const double g = std::tan (3.14159265358979323846 * std::min (fc, fs * 0.45) / fs);
        G = g / (1.0 + g);
    }
    void reset() noexcept { s = 0.0; }
    double tick (double x) noexcept
    {
        const double v = (x - s) * G, lp = v + s;
        s = lp + v;
        return hp ? x - lp : lp;
    }
private:
    double G = 0.0, s = 0.0;
    bool hp = false;
};

// Butterworth high- or low-pass of order 1, 2, 3, 4 or 8 (6 to 48 dB/oct), built from the sections above
class ToneFilter
{
public:
    void set (double fc, double fs, int order, bool highpass) noexcept
    {
        if (order != currentOrder) { reset(); currentOrder = order; }
        // Butterworth pole Qs for each order; an odd order adds one 1st-order section
        static const double q2[] = { 0.70710678 }, q3[] = { 1.0 }, q4[] = { 0.54119610, 1.30656296 },
                            q8[] = { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
        const double* q = q2; numSvf = 1; useOnePole = false;
        switch (order)
        {
            case 1: numSvf = 0; useOnePole = true; break;
            case 3: q = q3; numSvf = 1; useOnePole = true; break;
            case 4: q = q4; numSvf = 2; break;
            case 8: q = q8; numSvf = 4; break;
            default: break;
        }
        for (int i = 0; i < numSvf; ++i) svf[i].set (fc, fs, q[i], highpass);
        if (useOnePole) pole.set (fc, fs, highpass);
    }
    void reset() noexcept { for (auto& f : svf) f.reset(); pole.reset(); }
    double tick (double x) noexcept
    {
        if (useOnePole) x = pole.tick (x);
        for (int i = 0; i < numSvf; ++i) x = svf[i].tick (x);
        return x;
    }
private:
    SVF svf[4];
    OnePole pole;
    int numSvf = 1, currentOrder = 2;
    bool useOnePole = false;
};

//==============================================================================
struct BandSettings
{
    int mode = Tube;
    float driveDb = 12.0f, shape = 0.5f, bias = 0.0f, gainDb = 0.0f, mix = 1.0f;
    float dynamics = 0.0f;          // -1 .. +1: less / more of the band's transients after the distortion
    bool solo = false, bypass = false;
};

struct Settings
{
    int numBands = 3;
    float crossover[kMaxBands - 1] = { 150.0f, 1500.0f, 6000.0f };
    BandSettings band[kMaxBands];   // modes default to Tube here; the plugin's own default is None
    float hpfHz = 20.0f, lpfHz = 20000.0f;      // tone filter: off at these ends
    int toneOrder = 2;                          // filter order: 1, 2, 3, 4 or 8 = 6, 12, 18, 24 or 48 dB/oct
    float mix = 1.0f;                           // global dry/wet
};

class Engine
{
public:
    static constexpr float kFilterMin = 20.0f, kFilterMax = 20000.0f;

    // fs = the oversampled rate, osFactor = how many times the host rate that is
    void prepare (double fs, int osFactor)
    {
        sampleRate = fs; oversampling = osFactor;
        const int ramp = (int) (fs * 0.02);
        for (auto& b : bands)
            for (auto* sm : { &b.drive, &b.shape, &b.bias, &b.gain, &b.mix, &b.audible, &b.clean, &b.dyn })
                sm->setLength (ramp);
        globalMix.setLength (ramp);
        modeFadeLength = std::max (1, (int) (fs * 0.012));
        // transient detector: a peak follower, then a fast and a slow copy of it that differ only in how
        // quickly they rise; while they disagree, a transient is happening
        envFastAtk = (float) (1.0 - std::exp (-1.0 / (fs * 0.0008)));
        envSlowAtk = (float) (1.0 - std::exp (-1.0 / (fs * 0.020)));
        envRel     = (float) (1.0 - std::exp (-1.0 / (fs * 0.200)));
        dcCoef = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * 4.0 / fs);   // 4 Hz
        first = true;
        reset();
    }

    void reset() noexcept
    {
        for (auto& ch : chan)
        {
            for (auto& f : ch.lp) f.reset();
            for (auto& f : ch.hp) f.reset();
            for (auto& f : ch.ap) f.reset();
            ch.toneHp.reset(); ch.toneLp.reset(); ch.dc = 0.0;
            for (auto& d : ch.hold) d = {};
        }
        for (auto& b : bands) { b.fadeLeft = 0; b.envPeak = b.envFast = b.envSlow = 0.0f; }
    }

    void setSettings (const Settings& s) noexcept { settings = s; }

    // per band output level for metering is not needed; the band split itself is exposed for the display
    float currentCrossover (int i) const noexcept { return (float) xoverHz[i]; }

    void process (float* left, float* right, int n) noexcept
    {
        updateBlock (n);
        float* io[2] = { left, right };

        for (int i = 0; i < n; ++i)
        {
            if ((i & 31) == 0) glideCrossovers (std::min (32, n - i));
            const float g = globalMix.next();
            float bandOut[kMaxBands][2];

            // per band smoothed controls, shared by both channels
            float drive[kMaxBands], shape[kMaxBands], bias[kMaxBands], gain[kMaxBands], mix[kMaxBands], audible[kMaxBands], clean[kMaxBands];
            for (int b = 0; b < kMaxBands; ++b)
            {
                auto& B = bands[b];
                drive[b] = B.drive.next(); shape[b] = B.shape.next(); bias[b] = B.bias.next();
                gain[b] = B.gain.next(); mix[b] = B.mix.next(); audible[b] = B.audible.next(); clean[b] = B.clean.next();
            }

            // ---- split both channels into bands
            double split[2][kMaxBands];
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& C = chan[ch];
                double rest = (double) io[ch][i];
                for (int k = 0; k < kMaxBands - 1; ++k)
                {
                    split[ch][k] = C.lp[k].tick (rest);
                    rest = C.hp[k].tick (rest);
                }
                split[ch][kMaxBands - 1] = rest;
                // phase alignment: band 1 gets the shifts of crossovers 2 and 3, band 2 that of crossover 3
                // (each needs its own filter, because a filter remembers the signal it has seen)
                split[ch][0] = C.ap[2].tick (C.ap[1].tick (split[ch][0]));
                split[ch][1] = C.ap[0].tick (split[ch][1]);
            }

            // ---- DYNAMICS: a gain per band (stereo linked) that lifts or ducks the band's transients,
            //      detected on the clean band and applied after the distortion
            float dynGain[kMaxBands];
            for (int b = 0; b < kMaxBands; ++b) dynGain[b] = transientGain (b, (float) std::max (std::abs (split[0][b]), std::abs (split[1][b])));

            for (int ch = 0; ch < 2; ++ch)
            {
                // ---- distort each band
                for (int b = 0; b < kMaxBands; ++b)
                {
                    auto& B = bands[b];
                    const float in = (float) split[ch][b];
                    float y = distort (b, ch, B.mode, in, drive[b], shape[b], bias[b]);
                    if (B.fadeLeft > 0)
                    {
                        const float t = (float) B.fadeLeft / (float) modeFadeLength;   // 1 -> 0 over the fade
                        y += t * (distort (b, ch, B.prevMode, in, drive[b], shape[b], bias[b]) - y);
                    }
                    y *= gain[b] * dynGain[b];
                    const float wetBand = in + mix[b] * (y - in);
                    bandOut[b][ch] = audible[b] * (in + clean[b] * (wetBand - in));
                }
            }
            for (auto& B : bands) if (B.fadeLeft > 0) --B.fadeLeft;

            for (int ch = 0; ch < 2; ++ch)
            {
                auto& C = chan[ch];
                double wet = (double) bandOut[0][ch] + bandOut[1][ch] + bandOut[2][ch] + bandOut[3][ch];
                C.dc += dcCoef * (wet - C.dc);                  // DC blocker (bias and asymmetric curves add DC)
                wet -= C.dc;
                if (toneHpOn) wet = C.toneHp.tick (wet);
                if (toneLpOn) wet = C.toneLp.tick (wet);
                const float dry = io[ch][i];
                io[ch][i] = dry + g * ((float) wet - dry);
            }
        }
    }

private:
    struct HoldState { double phase = 0.0; float held = 0.0f; };
    struct Channel
    {
        LR4 lp[kMaxBands - 1], hp[kMaxBands - 1];
        AllpassLR4 ap[3];               // [0] band 2 at crossover 3, [1] band 1 at crossover 2, [2] band 1 at crossover 3
        ToneFilter toneHp, toneLp;
        double dc = 0.0;
        HoldState hold[kMaxBands];
    };
    struct Band
    {
        Smooth drive, shape, bias, gain, mix, audible, clean, dyn;
        int mode = Tube, prevMode = Tube, fadeLeft = 0;
        float envPeak = 0.0f, envFast = 0.0f, envSlow = 0.0f;
    };

    // how far the fast envelope is above the slow one (in dB) is the size of the transient right now;
    // DYNAMICS scales that into a boost (more punch) or a cut (smoother, more sustained)
    float transientGain (int b, float level) noexcept
    {
        auto& B = bands[b];
        B.envPeak = level > B.envPeak ? level : B.envPeak + envRel * (level - B.envPeak);   // instant attack, slow release
        B.envFast = B.envPeak > B.envFast ? B.envFast + envFastAtk * (B.envPeak - B.envFast) : B.envPeak;
        B.envSlow = B.envPeak > B.envSlow ? B.envSlow + envSlowAtk * (B.envPeak - B.envSlow) : B.envPeak;
        const float amount = B.dyn.next();
        if (std::abs (amount) < 1.0e-4f)
            return 1.0f;
        constexpr float floor = 1.0e-5f;                                       // about -100 dB: ignore silence
        const float diffDb = 8.685889638f * std::log ((B.envFast + floor) / (B.envSlow + floor));
        const float db = std::clamp (amount * kDynDepth * std::max (0.0f, diffDb - 0.5f), -kDynMaxCut, kDynMaxBoost);   // 0.5 dB dead zone
        return std::exp (db * 0.1151292546f);
    }

    static constexpr float kDynDepth = 0.7f, kDynMaxBoost = 12.0f, kDynMaxCut = 18.0f;

    float distort (int b, int ch, int mode, float in, float drive, float shape, float bias) noexcept
    {
        if (mode == None)
            return in;
        if (mode == Downsample)
        {
            // sample-and-hold at a reduced rate, then optional bit crushing
            auto& h = chan[ch].hold[b];
            const double hold = (double) downsampleFactor (bands[b].driveDb) * oversampling;
            h.phase += 1.0;
            if (h.phase >= hold) { h.phase -= hold; h.held = in; }
            float y = h.held;
            if (shape > 0.001f)
            {
                const float q = std::exp2 (crushBits (shape) - 1.0f);
                y = std::round (y * q) / q;
            }
            return y;
        }
        const float x = in * drive + bias;
        return shapeCurve (mode, x, shape) - shapeCurve (mode, bias, shape);   // remove the bias's static offset
    }

    void updateBlock (int n) noexcept
    {
        const auto& S = settings;

        // ---- crossover targets: unused ones are parked far above the audio range
        const double park = sampleRate * 0.4;
        double prev = 0.0;
        for (int k = 0; k < kMaxBands - 1; ++k)
        {
            double f = std::clamp ((double) S.crossover[k], 20.0, 20000.0);
            f = std::max (f, prev * 1.26);                            // keep at least a third of an octave apart
            prev = f;
            xoverTarget[k] = k < S.numBands - 1 ? f : park;
        }
        if (first) glideCrossovers (0);
        (void) n;

        // ---- per band controls
        bool anySolo = false;
        for (int b = 0; b < S.numBands; ++b) anySolo |= S.band[b].solo;

        for (int b = 0; b < kMaxBands; ++b)
        {
            const auto& bs = S.band[b];
            auto& B = bands[b];
            const int mode = std::clamp (bs.mode, 0, kNumModes - 1);
            if (mode != B.mode)
            {
                B.prevMode = B.mode; B.mode = mode;
                B.fadeLeft = first ? 0 : modeFadeLength;
            }
            B.driveDb = bs.driveDb;
            const bool noDrive = mode == Downsample || mode == None;
            const float driveGain = noDrive ? 1.0f : std::pow (10.0f, bs.driveDb / 20.0f);
            const float vals[] = { driveGain, bs.shape, noDrive ? 0.0f : bs.bias, std::pow (10.0f, bs.gainDb / 20.0f), bs.mix,
                                   (b >= S.numBands || ! anySolo || bs.solo) ? 1.0f : 0.0f, bs.bypass ? 0.0f : 1.0f,
                                   std::clamp (bs.dynamics, -1.0f, 1.0f) };
            Smooth* sms[] = { &B.drive, &B.shape, &B.bias, &B.gain, &B.mix, &B.audible, &B.clean, &B.dyn };
            for (int j = 0; j < 8; ++j) { if (first) sms[j]->snap (vals[j]); else sms[j]->set (vals[j]); }
        }

        // ---- tone filter and global mix
        toneHpOn = S.hpfHz > kFilterMin + 0.5f;
        toneLpOn = S.lpfHz < kFilterMax - 0.5f;
        for (auto& C : chan)
        {
            if (toneHpOn) C.toneHp.set (S.hpfHz, sampleRate, S.toneOrder, true);
            if (toneLpOn) C.toneLp.set (S.lpfHz, sampleRate, S.toneOrder, false);
        }
        if (first) globalMix.snap (S.mix); else globalMix.set (S.mix);
        first = false;
    }

    // moves the crossovers towards their targets in small log steps (n = 0: jump straight there)
    void glideCrossovers (int n) noexcept
    {
        const double glide = n == 0 ? 1.0 : 1.0 - std::exp (-(double) n / (sampleRate * 0.03));
        bool changed = n == 0;
        for (int k = 0; k < kMaxBands - 1; ++k)
        {
            const double cur = xoverHz[k] > 0.0 ? xoverHz[k] : xoverTarget[k];
            const double next = std::exp (std::log (cur) + glide * (std::log (xoverTarget[k]) - std::log (cur)));
            if (std::abs (next - xoverHz[k]) > 1.0e-4 * next) { xoverHz[k] = next; changed = true; }
        }
        if (! changed) return;
        for (auto& C : chan)
        {
            for (int k = 0; k < kMaxBands - 1; ++k)
            {
                C.lp[k].lowpass (xoverHz[k], sampleRate);
                C.hp[k].highpass (xoverHz[k], sampleRate);
            }
            C.ap[0].set (xoverHz[2], sampleRate);
            C.ap[1].set (xoverHz[1], sampleRate);
            C.ap[2].set (xoverHz[2], sampleRate);
        }
    }

    Settings settings;
    double xoverTarget[kMaxBands - 1] = { 150.0, 1500.0, 6000.0 };
    double sampleRate = 192000.0, dcCoef = 0.0, xoverHz[kMaxBands - 1] = { 0.0, 0.0, 0.0 };
    int oversampling = 4, modeFadeLength = 1;
    float envFastAtk = 0.1f, envSlowAtk = 0.01f, envRel = 0.001f;
    bool first = true, toneHpOn = false, toneLpOn = false;
    struct BandState : Band { float driveDb = 12.0f; };
    std::array<BandState, kMaxBands> bands;
    std::array<Channel, 2> chan;
    Smooth globalMix;
};
} // namespace krush
