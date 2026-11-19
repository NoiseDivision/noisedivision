#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// MASTERFUSE's audio processing. Plain C++ with no JUCE dependency, so it can be
// tested on its own.
//
// Signal path (stereo):
//   3-band split (Linkwitz-Riley, 24 dB/oct)
//   per band:  level detector on the mono sum -> soft-knee compressor -> makeup gain
//              -> stereo width (gain on the side signal) -> tanh saturation
//   sum of bands -> air EQ -> two-stage limiter (slow leveller, then a fast look-ahead stage)
//
// The high band's saturator runs at 4x the sample rate to avoid aliasing.
//
// Every amount-dependent value is a 5-point table measured at reference amounts of
// 0, 50, 100, 150 and 200 %, interpolated linearly in between.
namespace knobdsp
{
constexpr int kNumBands  = 3;   // 0 = low, 1 = mid, 2 = high
constexpr int kNumPoints = 5;   // amount = 0, 50, 100, 150, 200
constexpr float kMaxAmount = 200.0f;

struct Band
{
    float makeupDb[kNumPoints];  // gain at low level
    float slope[kNumPoints];     // compression above the knee, as 1 - 1/ratio
    float widthDb[kNumPoints];   // extra gain on the side (L minus R) signal
    float thresholdDb, kneeDb;   // static curve, in detector dB (RMS)
    float detectorMs;            // RMS window of the level detector
    float attackFastMs, attackSlowMs, attackFastShare;   // gain reduction builds up in two parts: a quick share and a slow rest
    float releaseDbPerSec;       // how fast the detected level is allowed to fall
};

struct Params
{
    Band band[kNumBands];
    float satMix[kNumPoints];    // how much of the saturated signal is blended in
    float satDrive;              // tanh drive
    float crossoverLowHz, crossoverHighHz;
};

// Values fitted by least squares to measurements of the reference at each amount
// (stepped tones, sweeps, pink noise and bursts), with the shared settings also
// fitted to a music recording at the default amount.
inline Params measuredParams()
{
    Params p {};
    p.band[0] = { { 0.00f, 1.44f, 2.80f, 3.46f, 4.08f }, { 0.000f, 0.066f, 0.197f, 0.162f, 0.197f }, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
                  -15.3f, 38.5f, 12.0f, 15.0f, 15.0f, 1.00f, 2.9f };
    p.band[1] = { { 0.00f, 2.04f, 3.59f, 5.00f, 6.23f }, { 0.000f, 0.114f, 0.200f, 0.264f, 0.318f }, { 0.0f, 0.0f, 0.1f, 0.3f, 1.2f },
                  -31.0f, 24.4f, 38.8f, 4.5f, 75.2f, 0.45f, 44.4f };
    p.band[2] = { { 0.00f, 0.68f, 0.99f, 2.06f, 2.95f }, { 0.000f, 0.338f, 0.674f, 0.849f, 0.950f }, { 0.0f, 1.7f, 3.0f, 3.6f, 3.8f },
                  -13.5f, 9.8f, 0.4f, 1.4f, 1.4f, 1.00f, 49.9f };
    const float mix[kNumPoints] = { 0.00f, 0.46f, 1.00f, 1.02f, 1.04f };
    std::copy (mix, mix + kNumPoints, p.satMix);
    p.satDrive = 1.169f;
    p.crossoverLowHz = 163.0f;
    p.crossoverHighHz = 2511.0f;
    return p;
}

inline float dbToGain (float db) noexcept { return std::exp (db * 0.11512925464970229f); }

inline float interpolate (const float* table, float amount) noexcept
{
    const float pos = std::clamp (amount, 0.0f, kMaxAmount) / (kMaxAmount / (float) (kNumPoints - 1));
    const int i = std::min ((int) pos, kNumPoints - 2);
    return table[i] + (table[i + 1] - table[i]) * (pos - (float) i);
}

//==============================================================================
class Biquad
{
public:
    void setLowpass (double fc, double fs) noexcept  { design (fc, fs, false); }
    void setHighpass (double fc, double fs) noexcept { design (fc, fs, true); }
    void reset() noexcept { z1 = z2 = 0.0; }

    double tick (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void design (double fc, double fs, bool highpass) noexcept   // 2nd-order Butterworth
    {
        const double w = 2.0 * 3.14159265358979323846 * std::min (fc, fs * 0.45) / fs;
        const double c = std::cos (w), alpha = std::sin (w) / 1.4142135623730951, a0 = 1.0 + alpha;
        if (highpass) { b0 = (1.0 + c) * 0.5 / a0; b1 = -(1.0 + c) / a0; }
        else          { b0 = (1.0 - c) * 0.5 / a0; b1 =  (1.0 - c) / a0; }
        b2 = b0; a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
    }

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
};

// 4th-order Linkwitz-Riley = two Butterworth sections in series
struct LR4
{
    Biquad a, b;
    void setLowpass (double fc, double fs) noexcept  { a.setLowpass (fc, fs);  b.setLowpass (fc, fs); }
    void setHighpass (double fc, double fs) noexcept { a.setHighpass (fc, fs); b.setHighpass (fc, fs); }
    void reset() noexcept { a.reset(); b.reset(); }
    double tick (double x) noexcept { return b.tick (a.tick (x)); }
};

//==============================================================================
// Half-band FIR low-pass (Kaiser-windowed), used for 2x up- and down-sampling.
class HalfbandFir
{
public:
    void design (int numTaps, double attenuationDb)   // numTaps must be 4k + 3
    {
        n = numTaps;
        const int c = (n - 1) / 2;
        const double beta = 0.1102 * (attenuationDb - 8.7);
        h.assign ((size_t) n, 0.0f);
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const int k = i - c;
            if (k == 0 || (k % 2) == 0) continue;                       // half-band: every other tap is zero
            const double x = 3.14159265358979323846 * k * 0.5;
            const double r = (double) k / (double) c;
            const double v = 0.5 * std::sin (x) / x * besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            h[(size_t) i] = (float) v;
            sum += v;
        }
        for (auto& v : h) v = (float) (v * 0.5 / sum);                  // side taps sum to 0.5 ...
        h[(size_t) c] = 0.5f;                                           // ... centre tap is 0.5: unity gain at DC
        buf.assign ((size_t) (2 * n), 0.0f);
        pos = 0;
    }

    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    int centre() const noexcept { return (n - 1) / 2; }

    float tick (float x) noexcept
    {
        buf[(size_t) pos] = buf[(size_t) (pos + n)] = x;
        const float* b = buf.data() + pos + 1;                          // the last n samples, oldest first
        float acc = 0.0f;
        for (int i = 0; i < n; ++i) acc += h[(size_t) i] * b[i];
        if (++pos == n) pos = 0;
        return acc;
    }

private:
    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 60; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; if (term < 1e-14 * sum) break; }
        return sum;
    }

    std::vector<float> h, buf;
    int n = 0, pos = 0;
};

// Runs a nonlinearity at 4x the sample rate. Latency is a whole number of samples.
class Oversampler4x
{
public:
    void prepare()
    {
        up1.design (63, 90.0); down1.design (63, 90.0);     // 1x <-> 2x: steep, protects the audio band
        up2.design (23, 90.0); down2.design (23, 90.0);     // 2x <-> 4x: gentle
        reset();
    }

    void reset() noexcept { up1.reset(); down1.reset(); up2.reset(); down2.reset(); z = 0.0f; }

    // stage 1 delays by centre() samples; stage 2 by (centre() + 1) half-samples
    int latency() const noexcept { return up1.centre() + (up2.centre() + 1) / 2; }

    template <typename Fn>
    float process (float x, Fn&& nonlinearity) noexcept
    {
        const float a[2] = { 2.0f * up1.tick (x), 2.0f * up1.tick (0.0f) };
        float pair[2];
        for (int i = 0; i < 2; ++i)
        {
            const float b0 = nonlinearity (2.0f * up2.tick (a[i]));
            const float b1 = nonlinearity (2.0f * up2.tick (0.0f));
            const float d = down2.tick (b0);
            down2.tick (b1);
            pair[i] = z;        // one extra sample of delay at the 2x rate makes the total a whole sample
            z = d;
        }
        const float y = down1.tick (pair[0]);
        down1.tick (pair[1]);
        return y;
    }

private:
    HalfbandFir up1, down1, up2, down2;
    float z = 0.0f;
};

//==============================================================================
// Stereo-linked limiter in two stages. The output never exceeds the ceiling.
//   1. a slow leveller takes up sustained overshoot beyond a small margin
//   2. a fast look-ahead stage catches what is left. Its window is so short that
//      on bass it shapes individual peaks (like a soft clipper), while from the
//      mids upwards it holds a steady gain and stays clean.
class Limiter
{
public:
    float slowMarginDb = 2.54f, slowAttackMs = 8.0f, slowReleaseMs = 440.0f;

    void prepare (double sampleRate, double lookaheadMs, double releaseMs, float ceilingGain)
    {
        n = std::max (1, (int) std::lround (sampleRate * lookaheadMs / 1000.0));
        ceiling = ceilingGain;
        release = (float) (1.0 - std::exp (-1.0 / (sampleRate * releaseMs / 1000.0)));
        slowAttack  = (float) (1.0 - std::exp (-1.0 / (sampleRate * slowAttackMs / 1000.0)));
        slowRelease = (float) (1.0 - std::exp (-1.0 / (sampleRate * slowReleaseMs / 1000.0)));
        delayL.assign ((size_t) n, 0.0f); delayR.assign ((size_t) n, 0.0f);
        avg.assign ((size_t) n, 1.0f);
        dqVal.assign ((size_t) (n + 2), 1.0f); dqIdx.assign ((size_t) (n + 2), 0);
        reset();
    }

    void reset() noexcept
    {
        std::fill (delayL.begin(), delayL.end(), 0.0f); std::fill (delayR.begin(), delayR.end(), 0.0f);
        std::fill (avg.begin(), avg.end(), 1.0f);
        sum = (double) n; env = 1.0f; pos = 0; head = 0; count = 0; index = 0; reduction = 1.0f; slowDb = 0.0f;
    }

    int latency() const noexcept { return n; }
    float currentGain() const noexcept { return reduction; }

    void process (float& l, float& r) noexcept
    {
        // ---- stage 1: slow leveller
        float pk = std::max (std::abs (l), std::abs (r));
        const float overDb = pk > ceiling ? 8.685889638f * std::log (pk / ceiling) : 0.0f;
        const float wanted = std::max (0.0f, overDb - slowMarginDb);
        slowDb += wanted > slowDb ? slowAttack * (wanted - slowDb) : -slowRelease * slowDb;
        float slowGain = 1.0f;
        if (slowDb > 1.0e-4f)
        {
            slowGain = dbToGain (-slowDb);
            l *= slowGain; r *= slowGain; pk *= slowGain;
        }

        // ---- stage 2: fast look-ahead stage. Gain this sample needs, instant attack, exponential release
        const float req = pk > ceiling ? ceiling / pk : 1.0f;
        env = std::min (req, env + (1.0f - env) * release);

        // minimum over the last n + 1 values (monotonic queue)
        const int cap = n + 2;
        while (count > 0 && dqVal[(size_t) ((head + count - 1) % cap)] >= env) --count;
        dqVal[(size_t) ((head + count) % cap)] = env; dqIdx[(size_t) ((head + count) % cap)] = index; ++count;
        while (dqIdx[(size_t) head] < index - n) { head = (head + 1) % cap; --count; }
        const float m = dqVal[(size_t) head];
        ++index;

        // averaging the minima over n samples turns each step into a smooth ramp that
        // lands exactly when the peak leaves the delay line
        sum += (double) m - (double) avg[(size_t) pos];
        avg[(size_t) pos] = m;
        const float g = (float) (sum / (double) n);
        reduction = g * slowGain;

        const float outL = delayL[(size_t) pos], outR = delayR[(size_t) pos];
        delayL[(size_t) pos] = l; delayR[(size_t) pos] = r;

        if (++pos == n)
        {
            pos = 0;
            sum = 0.0;                                   // re-sum once per cycle so rounding can't drift
            for (float v : avg) sum += (double) v;
        }

        l = std::clamp (outL * g, -ceiling, ceiling);
        r = std::clamp (outR * g, -ceiling, ceiling);
    }

private:
    std::vector<float> delayL, delayR, avg, dqVal;
    std::vector<long long> dqIdx;
    double sum = 1.0;
    float env = 1.0f, ceiling = 1.0f, release = 0.0f, reduction = 1.0f;
    float slowDb = 0.0f, slowAttack = 0.0f, slowRelease = 0.0f;
    int n = 1, pos = 0, head = 0, count = 0;
    long long index = 0;
};

//==============================================================================
// "Air": a gentle, broad tone curve (warmth around 700 Hz, a rise above ~4 kHz
// reaching about +1.2 dB at 20 kHz). Its shape was measured from a reference EQ's
// 40 kHz air band and reproduced with four standard EQ sections. setAmount()
// takes the reference's boost setting: 0 = flat, 1 = the default, 1.6 = the most.
class AirEQ
{
public:
    void prepare (double newSampleRate) { fs = newSampleRate; current = -1.0f; setAmount (0.0f); reset(); }
    void reset() noexcept { for (auto& ch : z) for (auto& v : ch) v = 0.0; }

    void setAmount (float setting) noexcept
    {
        if (std::abs (setting - current) < 1.0e-4f) return;
        current = setting;
        // below 1 the whole curve fades in; above 1 only the top end keeps rising
        const float fade = std::min (setting, 1.0f), boost = std::max (setting, fade);
        design (0, false, 720.2, 0.9573, 0.4543 * fade);   // broad warmth around 700 Hz
        design (1, false, 1725.8, 2.5704, -0.0169 * fade);   // slight dip near 1.7 kHz
        design (2, true,  6503.9, 0.3321, 1.1463 * fade);   // the air shelf
        design (3, true,  1000.0, 0.1000, 0.0882 * boost);  // what the boost setting adds
        active = setting > 1.0e-4f;
    }

    float process (int ch, float in) noexcept
    {
        if (! active) return in;
        double x = (double) in;
        for (int i = 0; i < kSections; ++i)
        {
            const auto& c = coef[i];
            double* s = z[ch] + 2 * i;
            const double y = c[0] * x + s[0];
            s[0] = c[1] * x - c[3] * y + s[1];
            s[1] = c[2] * x - c[4] * y;
            x = y;
        }
        return (float) x;
    }

private:
    static constexpr int kSections = 4;

    // standard peaking / high-shelf EQ (RBJ cookbook)
    void design (int i, bool shelf, double f0, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979323846 * std::min (f0, fs * 0.49) / fs;
        const double c = std::cos (w), al = std::sin (w) / (2.0 * q);
        double b0, b1, b2, a0, a1, a2;
        if (shelf)
        {
            const double sq = 2.0 * std::sqrt (A) * al;
            b0 = A * ((A + 1) + (A - 1) * c + sq); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sq);
            a0 = (A + 1) - (A - 1) * c + sq;       a1 = 2 * ((A - 1) - (A + 1) * c);       a2 = (A + 1) - (A - 1) * c - sq;
        }
        else
        {
            b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A;
        }
        coef[i][0] = b0 / a0; coef[i][1] = b1 / a0; coef[i][2] = b2 / a0; coef[i][3] = a1 / a0; coef[i][4] = a2 / a0;
    }

    double fs = 48000.0, coef[kSections][5] = {}, z[2][2 * kSections] = {};
    float current = -1.0f;
    bool active = false;
};

//==============================================================================
class Processor
{
public:
    // set before prepare(); used by the test harness
    bool  limiterEnabled = true;
    float outputGainDb   = 0.0f;       // gain into the limiter (may change while running; it is ramped)
    float ceilingDb      = -0.1f;
    float limiterLookaheadMs = 0.39f, limiterReleaseMs = 0.3f;
    bool  airEnabled = true;           // the air curve, which rises with the knob
    bool  saturateMidSide = false;     // saturate mid and side separately instead of left and right

    void setParams (const Params& newParams) noexcept { params = newParams; if (sampleRate > 0.0) updateFixed(); }
    Limiter& getLimiter() noexcept { return limiter; }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        for (auto& os : oversampler) os.prepare();
        osLatency = oversampler[0].latency();
        for (auto& d : bandDelay) d.assign ((size_t) osLatency, 0.0f);
        limiter.prepare (sampleRate, limiterLookaheadMs, limiterReleaseMs, dbToGain (ceilingDb));
        air.prepare (sampleRate);
        updateFixed();
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = filters[ch];
            f.lowLp.reset(); f.lowHp.reset(); f.lowApLp.reset(); f.lowApHp.reset(); f.highLp.reset(); f.highHp.reset();
            oversampler[ch].reset();
            std::fill (bandDelay[ch].begin(), bandDelay[ch].end(), 0.0f);
        }
        delayPos = 0;
        for (int b = 0; b < kNumBands; ++b) { meanSquare[b] = 0.0; level[b] = -120.0f; redFast[b] = redSlow[b] = 0.0f; }
        limiter.reset();
        air.reset();
        amount = targetAmount;
        updateTargets();
        for (int b = 0; b < kNumBands; ++b) { makeup[b] = makeupTarget[b]; width[b] = widthTarget[b]; }
        satMix = satMixTarget;
        outGain = dbToGain (outputGainDb);
    }

    // amount is on the reference scale: 0 .. 200
    void setAmount (float newAmount) noexcept { targetAmount = std::clamp (newAmount, 0.0f, kMaxAmount); }

    int getLatency() const noexcept { return osLatency + (limiterEnabled ? limiter.latency() : 0); }

    // gain reduction of each band's compressor (dB, positive) and of the limiter, for metering
    float bandReductionDb[kNumBands] = { 0.0f, 0.0f, 0.0f };
    float limiterReductionDb = 0.0f;

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0) return;

        // glide the amount (about 30 ms), then ramp the gains across the block
        amount += (targetAmount - amount) * (float) (1.0 - std::exp (-(double) numSamples / (sampleRate * 0.03)));
        if (std::abs (targetAmount - amount) < 0.01f) amount = targetAmount;
        updateTargets();

        const float inv = 1.0f / (float) numSamples;
        float makeupInc[kNumBands], widthInc[kNumBands];
        for (int b = 0; b < kNumBands; ++b)
        {
            makeupInc[b] = (makeupTarget[b] - makeup[b]) * inv;
            widthInc[b]  = (widthTarget[b] - width[b]) * inv;
        }
        const float satInc = (satMixTarget - satMix) * inv;
        const float drive = params.satDrive, invDrive = 1.0f / params.satDrive;
        const float outInc = (dbToGain (outputGainDb) - outGain) * inv;

        for (int i = 0; i < numSamples; ++i)
        {
            const float mix = (satMix += satInc);
            auto saturate = [mix, drive, invDrive] (float x) noexcept
            {
                return x + mix * (std::tanh (drive * x) * invDrive - x);
            };

            // ---- split into bands
            float band[kNumBands][2];
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& f = filters[ch];
                const double x = (double) (ch == 0 ? left[i] : right[i]);
                const double low = f.lowLp.tick (x), rest = f.lowHp.tick (x);
                band[0][ch] = (float) (f.lowApLp.tick (low) + f.lowApHp.tick (low));   // all-pass, keeps the bands in phase
                band[1][ch] = (float) f.highLp.tick (rest);
                band[2][ch] = (float) f.highHp.tick (rest);
            }

            // ---- per band: detect on the mono sum, compress, makeup, width
            for (int b = 0; b < kNumBands; ++b)
            {
                const double mono = 0.5 * ((double) band[b][0] + (double) band[b][1]);
                const double power = mono * mono;
                double& ms = meanSquare[b];
                ms += detectorCoef[b] * (power - ms);

                // detected level: rises at once, falls at a steady rate (dB per second)
                const float now = 4.342944819f * (float) std::log (ms + 1.0e-12);      // 10 log10
                float& lev = level[b];
                lev = now > lev ? now : std::max (now, lev - releaseStep[b]);

                // static curve
                const float over = lev - kneeStart[b];
                float wanted = 0.0f;
                if (over > 0.0f)
                    wanted = over < kneeWidth[b] ? slope[b] * over * over / (2.0f * kneeWidth[b])
                                                 : slope[b] * (over - 0.5f * kneeWidth[b]);

                // gain reduction builds up gradually (a quick share plus a slow rest) and lets go with the level
                float& fast = redFast[b];
                float& slow = redSlow[b];
                fast = wanted > fast ? fast + attackFastCoef[b] * (wanted - fast) : wanted;
                slow = wanted > slow ? slow + attackSlowCoef[b] * (wanted - slow) : wanted;
                const float red = slow + fastShare[b] * (fast - slow);
                bandReductionDb[b] = red;

                const float g = (makeup[b] += makeupInc[b]) * dbToGain (-red);
                const float w = (width[b] += widthInc[b]);
                const float mid = 0.5f * (band[b][0] + band[b][1]) * g;
                const float side = 0.5f * (band[b][0] - band[b][1]) * g * w;
                // saturate either left/right or mid/side
                band[b][0] = saturateMidSide ? mid : mid + side;
                band[b][1] = saturateMidSide ? side : mid - side;
            }

            // ---- saturation: low and mid directly, high at 4x; low + mid are delayed to line up
            outGain += outInc;
            float out[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const float lowMid = saturate (band[0][ch]) + saturate (band[1][ch]);
                float delayed = lowMid;
                if (osLatency > 0)
                {
                    delayed = bandDelay[ch][(size_t) delayPos];
                    bandDelay[ch][(size_t) delayPos] = lowMid;
                }
                out[ch] = air.process (ch, delayed + oversampler[ch].process (band[2][ch], saturate)) * outGain;
            }
            if (osLatency > 0 && ++delayPos == osLatency) delayPos = 0;

            if (saturateMidSide)
            {
                const float m = out[0], sd = out[1];
                out[0] = m + sd;
                out[1] = m - sd;
            }

            if (limiterEnabled) limiter.process (out[0], out[1]);
            left[i] = out[0];
            right[i] = out[1];
        }

        limiterReductionDb = limiterEnabled ? -8.685889638f * std::log (std::max (limiter.currentGain(), 1.0e-6f)) : 0.0f;
    }

private:
    void updateFixed() noexcept
    {
        for (auto& f : filters)
        {
            f.lowLp.setLowpass (params.crossoverLowHz, sampleRate);    f.lowHp.setHighpass (params.crossoverLowHz, sampleRate);
            f.lowApLp.setLowpass (params.crossoverHighHz, sampleRate); f.lowApHp.setHighpass (params.crossoverHighHz, sampleRate);
            f.highLp.setLowpass (params.crossoverHighHz, sampleRate);  f.highHp.setHighpass (params.crossoverHighHz, sampleRate);
        }
        for (int b = 0; b < kNumBands; ++b)
        {
            detectorCoef[b] = 1.0 - std::exp (-1.0 / (sampleRate * params.band[b].detectorMs / 1000.0));
            attackFastCoef[b] = (float) (1.0 - std::exp (-1.0 / (sampleRate * params.band[b].attackFastMs / 1000.0)));
            attackSlowCoef[b] = (float) (1.0 - std::exp (-1.0 / (sampleRate * params.band[b].attackSlowMs / 1000.0)));
            fastShare[b]      = std::clamp (params.band[b].attackFastShare, 0.0f, 1.0f);
            releaseStep[b]  = (float) (params.band[b].releaseDbPerSec / sampleRate);
            kneeWidth[b] = std::max (0.01f, params.band[b].kneeDb);
            kneeStart[b] = params.band[b].thresholdDb - 0.5f * kneeWidth[b];
        }
    }

    void updateTargets() noexcept
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            makeupTarget[b] = dbToGain (interpolate (params.band[b].makeupDb, amount));
            widthTarget[b]  = dbToGain (interpolate (params.band[b].widthDb, amount));
            slope[b]        = interpolate (params.band[b].slope, amount);
        }
        satMixTarget = interpolate (params.satMix, amount);
        air.setAmount (airEnabled ? (amount <= 100.0f ? amount / 100.0f : 1.0f + 0.6f * (amount - 100.0f) / 100.0f) : 0.0f);
    }

    struct ChannelFilters { LR4 lowLp, lowHp, lowApLp, lowApHp, highLp, highHp; };

    Params params = measuredParams();
    double sampleRate = 0.0;
    float amount = 0.0f, targetAmount = 0.0f;

    ChannelFilters filters[2];
    AirEQ air;
    Oversampler4x oversampler[2];
    std::vector<float> bandDelay[2];
    int osLatency = 0, delayPos = 0;
    Limiter limiter;

    double meanSquare[kNumBands] = { 0.0, 0.0, 0.0 }, detectorCoef[kNumBands] = {};
    float level[kNumBands] = { -120.0f, -120.0f, -120.0f }, redFast[kNumBands] = {}, redSlow[kNumBands] = {};
    float attackFastCoef[kNumBands] = {}, attackSlowCoef[kNumBands] = {}, fastShare[kNumBands] = {}, releaseStep[kNumBands] = {};
    float kneeStart[kNumBands] = {}, kneeWidth[kNumBands] = {}, slope[kNumBands] = {};
    float makeup[kNumBands] = { 1.0f, 1.0f, 1.0f }, makeupTarget[kNumBands] = { 1.0f, 1.0f, 1.0f };
    float width[kNumBands] = { 1.0f, 1.0f, 1.0f }, widthTarget[kNumBands] = { 1.0f, 1.0f, 1.0f };
    float satMix = 0.0f, satMixTarget = 0.0f, outGain = 1.0f;
};
} // namespace knobdsp
