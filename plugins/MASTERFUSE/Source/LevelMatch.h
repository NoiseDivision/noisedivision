#pragma once
#include <algorithm>
#include <cmath>

// Measures the loudness of the untouched and the processed signal and works out the
// gain that makes the processed one equally loud.
//
// Loudness is K-weighted as in ITU-R BS.1770 (the weighting behind LUFS). It starts as
// an exact running average from the moment playback starts, then becomes a rolling
// average over the last ~10 seconds, so the gain settles fast and then stays steady.
// Near-silent passages are skipped, so the gain holds through breaks instead of drifting.
class LevelMatcher
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        const double pi = 3.14159265358979323846;

        // K-weighting stage 1: high shelf
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K  = std::tan (pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                      2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        // K-weighting stage 2: high pass
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K  = std::tan (pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            highpass = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : state) s = {};
        msDry = msWet = 0.0;
        seen = 0.0;
        gainDb = 0.0f;
    }

    // dry = untouched signal, wet = processed signal, both stereo and time-aligned
    void analyse (const float* dryL, const float* dryR, const float* wetL, const float* wetR, int n) noexcept
    {
        if (n <= 0) return;

        double sumDry = 0.0, sumWet = 0.0;
        for (int i = 0; i < n; ++i)
        {
            sumDry += weigh (0, dryL[i]) + weigh (1, dryR[i]);
            sumWet += weigh (2, wetL[i]) + weigh (3, wetR[i]);
        }
        const double blockDry = sumDry / n, blockWet = sumWet / n;

        // skip near-silence (below about -70 LUFS), so the gain holds through breaks
        if (blockDry < 1.0e-7 || blockWet < 1.0e-9)
            return;

        // exact average of everything so far, until the rolling window takes over
        seen += (double) n;
        const double a = std::max ((double) n / seen, 1.0 - std::exp (-(double) n / (fs * kAverageSeconds)));
        msDry += a * (blockDry - msDry);
        msWet += a * (blockWet - msWet);

        gainDb = std::clamp ((float) (10.0 * std::log10 (msDry / msWet)), -24.0f, 12.0f);
    }

    // gain (dB) that brings the processed signal to the untouched signal's loudness
    float getGainDb() const noexcept { return gainDb; }

private:
    static constexpr double kAverageSeconds = 10.0;

    struct Coefs { double b0, b1, b2, a1, a2; };
    struct State { double s1 = 0, s2 = 0, h1 = 0, h2 = 0; };

    double weigh (int i, float in) noexcept
    {
        auto& st = state[i];
        const double x = (double) in;
        const double y1 = shelf.b0 * x + st.s1;
        st.s1 = shelf.b1 * x - shelf.a1 * y1 + st.s2;
        st.s2 = shelf.b2 * x - shelf.a2 * y1;
        const double y2 = highpass.b0 * y1 + st.h1;
        st.h1 = highpass.b1 * y1 - highpass.a1 * y2 + st.h2;
        st.h2 = highpass.b2 * y1 - highpass.a2 * y2;
        return y2 * y2;
    }

    Coefs shelf {}, highpass {};
    State state[4];                    // dry L, dry R, processed L, processed R
    double fs = 48000.0, msDry = 0.0, msWet = 0.0, seen = 0.0;
    float gainDb = 0.0f;
};
