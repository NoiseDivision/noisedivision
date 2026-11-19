#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Optional safety stage after the OUTPUT fader: a soft clipper (CLIP) and a look-ahead
// peak limiter (LIMIT). Either one keeps the sample peaks at or below -0.1 dBFS.
// With both on, the clipper only shaves the sharpest 3 dB of the peaks and the limiter
// handles the rest, so the limiter works less hard.
// The look-ahead delay is always there, so the plugin's latency doesn't change when
// the switches are flipped.
namespace krush
{
class OutputStage
{
public:
    static constexpr float kCeilingDb = -0.1f;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        n = std::max (1, (int) std::lround (sampleRate * 0.0015));          // 1.5 ms look-ahead
        release     = (float) (1.0 - std::exp (-1.0 / (sampleRate * 0.060)));
        slowAttack  = (float) (1.0 - std::exp (-1.0 / (sampleRate * 0.008)));
        slowRelease = (float) (1.0 - std::exp (-1.0 / (sampleRate * 0.400)));
        switchStep  = (float) (1.0 / (sampleRate * 0.01));                 // 10 ms fades when switching
        delayL.assign ((size_t) n, 0.0f); delayR.assign ((size_t) n, 0.0f);
        rawDelayL.assign ((size_t) n, 0.0f); rawDelayR.assign ((size_t) n, 0.0f);
        avg.assign ((size_t) n, 1.0f);
        dqVal.assign ((size_t) (n + 2), 1.0f); dqIdx.assign ((size_t) (n + 2), 0);
        reset();
    }

    void reset() noexcept
    {
        for (auto* d : { &delayL, &delayR, &rawDelayL, &rawDelayR }) std::fill (d->begin(), d->end(), 0.0f);
        std::fill (avg.begin(), avg.end(), 1.0f);
        sum = (double) n; env = 1.0f; slowDb = 0.0f; pos = 0; head = 0; count = 0; index = 0; reductionDb = 0.0f;
    }

    // start in the given switch positions, without fading in
    void snap (bool clipOn, bool limitOn) noexcept { clipMix = clipOn ? 1.0f : 0.0f; limitMix = limitOn ? 1.0f : 0.0f; }

    int latency() const noexcept { return n; }
    float getReductionDb() const noexcept { return reductionDb; }    // limiter gain reduction, for display

    void process (float* left, float* right, int numSamples, bool clipOn, bool limitOn) noexcept
    {
        const float ceiling = std::pow (10.0f, kCeilingDb / 20.0f);
        float maxRed = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            clipMix  = moveTowards (clipMix,  clipOn ? 1.0f : 0.0f);
            limitMix = moveTowards (limitMix, limitOn ? 1.0f : 0.0f);

            // ---- soft clipper: at the ceiling on its own, 3 dB above it when the limiter follows
            float l = left[i], r = right[i];
            if (clipMix > 0.0f)
            {
                const float c = ceiling * (1.0f + 0.4125f * limitMix);      // 0.4125 = +3 dB
                l += clipMix * (softClip (l, c) - l);
                r += clipMix * (softClip (r, c) - r);
            }

            // ---- limiter (always runs, so the delay stays the same)
            float pk = std::max (std::abs (l), std::abs (r));

            // stage 1: slow leveller for sustained overshoot beyond 3 dB
            const float overDb = pk > ceiling ? 8.685889638f * std::log (pk / ceiling) : 0.0f;
            const float wanted = std::max (0.0f, overDb - 3.0f);
            slowDb += wanted > slowDb ? slowAttack * (wanted - slowDb) : -slowRelease * slowDb;
            const float slowGain = std::pow (10.0f, -slowDb / 20.0f);
            pk *= slowGain;

            // stage 2: look-ahead peak stage: instant attack, smooth release
            const float req = pk > ceiling ? ceiling / pk : 1.0f;
            env = std::min (req, env + (1.0f - env) * release);
            const int cap = n + 2;                                         // minimum over the last n + 1 values
            while (count > 0 && dqVal[(size_t) ((head + count - 1) % cap)] >= env) --count;
            dqVal[(size_t) ((head + count) % cap)] = env; dqIdx[(size_t) ((head + count) % cap)] = index; ++count;
            while (dqIdx[(size_t) head] < index - n) { head = (head + 1) % cap; --count; }
            const float m = dqVal[(size_t) head];
            ++index;
            sum += (double) m - (double) avg[(size_t) pos];                // averaging turns each step into a ramp
            avg[(size_t) pos] = m;
            const float fast = (float) (sum / (double) n);

            // two delay lines: the signal as it is (used while LIMIT is off) and after the slow stage
            const float dl = delayL[(size_t) pos], dr = delayR[(size_t) pos];
            const float rawL = rawDelayL[(size_t) pos], rawR = rawDelayR[(size_t) pos];
            delayL[(size_t) pos] = l * slowGain; delayR[(size_t) pos] = r * slowGain;
            rawDelayL[(size_t) pos] = l; rawDelayR[(size_t) pos] = r;
            if (++pos == n)
            {
                pos = 0; sum = 0.0;                                         // re-sum so rounding can't drift
                for (float v : avg) sum += (double) v;
            }

            const float limL = std::clamp (dl * fast, -ceiling, ceiling);
            const float limR = std::clamp (dr * fast, -ceiling, ceiling);
            left[i]  = rawL + limitMix * (limL - rawL);
            right[i] = rawR + limitMix * (limR - rawR);

            maxRed = std::max (maxRed, -20.0f * std::log10 (std::max (1.0e-6f, fast * slowGain)));
        }
        reductionDb = limitOn ? maxRed : 0.0f;
    }

private:
    // linear below the knee, a quadratic knee centred on the ceiling, then flat (PeakSlayer's curve)
    static float softClip (float x, float c) noexcept
    {
        const float u = std::abs (x), w = 0.413f * c, lo = c - 0.5f * w;
        float y;
        if (u <= lo) y = u;
        else if (u >= c + 0.5f * w) y = c;
        else { const float d = u - lo; y = u - d * d / (2.0f * w); }
        return std::copysign (y, x);
    }

    float moveTowards (float v, float target) const noexcept
    {
        return v < target ? std::min (target, v + switchStep) : std::max (target, v - switchStep);
    }

    std::vector<float> delayL, delayR, rawDelayL, rawDelayR, avg, dqVal;
    std::vector<long long> dqIdx;
    double fs = 48000.0, sum = 1.0;
    float env = 1.0f, release = 0.0f, slowDb = 0.0f, slowAttack = 0.0f, slowRelease = 0.0f, switchStep = 0.001f;
    float clipMix = 0.0f, limitMix = 0.0f, reductionDb = 0.0f;
    int n = 1, pos = 0, head = 0, count = 0;
    long long index = 0;
};
} // namespace krush
