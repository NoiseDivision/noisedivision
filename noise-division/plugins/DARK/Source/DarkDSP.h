#pragma once
#include <algorithm>
#include <cmath>

// DARK: one knob that makes a sound darker, made for drums and bass.
//
// What the knob does as it goes up (0..1):
//   tilt       highs tip down around 1 kHz, the low end leans up a little
//   weight     a low shelf (about 80 Hz) adds body from the middle of the range on
//   saturation warm, slightly asymmetric (tape-like) saturation thickens the low-mids
//   low-pass   a 12 dB/oct filter glides from 20 kHz down to about 2.2 kHz, with a slight
//              resonant bump near the end ("through the wall")
//   punch      the filter opens for a moment on every hit, so attacks still cut through
//              while tails and cymbal wash go dark
//   bass mono  below about 120 Hz the sides are folded into the middle as it gets darker
//
// No latency. Saturation uses first-order antiderivative anti-aliasing instead of
// oversampling, so nothing is delayed.
namespace dark
{
    constexpr double kPi = 3.14159265358979323846;

    inline float smoothstep (float e0, float e1, float x)
    {
        const float t = std::clamp ((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // the settings the knob turns into (pure functions, so the editor and tests can use them too)
    struct Shape
    {
        float tiltDb, lowShelfDb, drive, satMix, cutoffHz, resonance;
    };
    inline Shape shapeFor (float a)
    {
        a = std::clamp (a, 0.0f, 1.0f);
        Shape s;
        s.tiltDb     = 7.0f * a;                                      // highs down by this much (high shelf)
        s.lowShelfDb = 1.0f * a + 3.0f * smoothstep (0.3f, 0.85f, a); // body at ~80 Hz
        s.drive      = 1.0f + 1.6f * smoothstep (0.25f, 0.95f, a);    // saturation drive
        s.satMix     = 0.65f * smoothstep (0.2f, 0.8f, a);            // how much of the saturated signal is blended in
        s.cutoffHz   = 20000.0f * std::pow (2200.0f / 20000.0f, std::pow (a, 1.6f));
        s.resonance  = 0.707f + 0.55f * smoothstep (0.7f, 1.0f, a);   // Q of the low-pass
        return s;
    }

    // RBJ biquad, transposed direct form II
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline float process (float x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
        void reset() { z1 = z2 = 0; }
        void shelf (bool high, double fs, double f0, double gainDb, double slope = 0.8)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * kPi * f0 / fs;
            const double cw = std::cos (w), sw = std::sin (w);
            const double alpha = sw / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
            const double sA = 2.0 * std::sqrt (A) * alpha;
            double B0, B1, B2, A0, A1, A2;
            if (high)
            {
                B0 = A * ((A + 1) + (A - 1) * cw + sA);
                B1 = -2 * A * ((A - 1) + (A + 1) * cw);
                B2 = A * ((A + 1) + (A - 1) * cw - sA);
                A0 = (A + 1) - (A - 1) * cw + sA;
                A1 = 2 * ((A - 1) - (A + 1) * cw);
                A2 = (A + 1) - (A - 1) * cw - sA;
            }
            else
            {
                B0 = A * ((A + 1) - (A - 1) * cw + sA);
                B1 = 2 * A * ((A - 1) - (A + 1) * cw);
                B2 = A * ((A + 1) - (A - 1) * cw - sA);
                A0 = (A + 1) + (A - 1) * cw + sA;
                A1 = -2 * ((A - 1) + (A + 1) * cw);
                A2 = (A + 1) + (A - 1) * cw - sA;
            }
            b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
        }
        void lowpass (double fs, double f0, double q)
        {
            const double w = 2.0 * kPi * f0 / fs, cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            const double A0 = 1 + alpha;
            b0 = (1 - cw) / 2 / A0; b1 = (1 - cw) / A0; b2 = b0; a1 = -2 * cw / A0; a2 = (1 - alpha) / A0;
        }
    };

    // 2-pole state-variable low-pass (Simper/Zavalishin TPT): stays smooth while its cutoff moves every sample
    struct SVF
    {
        float ic1 = 0, ic2 = 0;
        inline float process (float x, float g, float k) noexcept
        {
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v2;
        }
        void reset() { ic1 = ic2 = 0; }
    };

    // tape-like saturation with first-order antiderivative anti-aliasing (ADAA):
    //   f(x) = (tanh(d (x + b)) - tanh(d b)) / slope, a little bias for even (warm) harmonics,
    //   scaled so quiet signals pass at the same level and only the louder parts get rounded
    struct Saturator
    {
        double xPrev = 0, FPrev = 0;
        static double logCosh (double x) { const double ax = std::abs (x); return ax + std::log1p (std::exp (-2.0 * ax)) - 0.6931471805599453; }
        inline float process (float xin, float d, float bias) noexcept
        {
            const double x = xin;
            const double u = d * (x + bias);
            const double F = logCosh (u) / d;                 // antiderivative of tanh(d (x + b))
            double y;
            const double dx = x - xPrev;
            if (std::abs (dx) > 1.0e-5)
                y = (F - FPrev) / dx;
            else
                y = std::tanh (d * (0.5 * (x + xPrev) + bias));
            xPrev = x; FPrev = F;
            const double t = std::tanh (d * bias);
            return (float) ((y - t) / (d * (1.0 - t * t)));
        }
        void reset() { xPrev = FPrev = 0; }
    };

    class Processor
    {
    public:
        void prepare (double sampleRate)
        {
            fs = sampleRate;
            for (auto& c : ch) { c.tilt.reset(); c.lowTilt.reset(); c.weight.reset(); c.sat.reset(); c.lp.reset(); }
            for (auto& b : sideLow) b.reset();
            sideLow[0].lowpass (fs, 120.0, 0.7071);
            sideLow[1].lowpass (fs, 120.0, 0.7071);
            envFast = envSlow = 0.0f;
            fastAtt = coef (0.0008); fastRel = coef (0.030);
            slowAtt = coef (0.025);  slowRel = coef (0.250);
            amountNow = amountTarget;
            counter = 0;
            updateFilters (amountNow);
        }

        void setAmount (float a01) noexcept { amountTarget = std::clamp (a01, 0.0f, 1.0f); }
        void setPunch (float p01) noexcept  { punch = std::clamp (p01, 0.0f, 1.0f); }

        // left / right in place
        void process (float* L, float* R, int n) noexcept
        {
            const float amountStep = 1.0f - (float) std::exp (-1.0 / (0.03 * fs));   // ~30 ms smoothing
            for (int i = 0; i < n; ++i)
            {
                if (counter-- <= 0)
                {
                    counter = kUpdate - 1;
                    amountNow += (amountTarget - amountNow) * std::min (1.0f, amountStep * (float) kUpdate);
                    if (std::abs (amountTarget - amountNow) < 1.0e-5f) amountNow = amountTarget;
                    updateFilters (amountNow);
                }

                // punch detector: how far the fast envelope jumps above the slow one, in dB
                const float lvl = std::max (std::abs (L[i]), std::abs (R[i]));
                envFast += (lvl > envFast ? fastAtt : fastRel) * (lvl - envFast);
                envSlow += (lvl > envSlow ? slowAtt : slowRel) * (lvl - envSlow);
                const float jump = envFast > 1.0e-5f ? 20.0f * std::log10 ((envFast + 1.0e-6f) / (envSlow + 1.0e-6f)) : 0.0f;
                const float hit = std::clamp (jump / 9.0f, 0.0f, 1.0f);
                // up to 2.5 octaves more top end during a hit
                const float open = std::exp2 (2.5f * punch * hit);
                const float fc = std::min (cutoff * open, (float) (fs * 0.45));
                const float g = (float) std::tan (kPi * std::min (fc, 20000.0f) / fs);

                float* io[2] = { &L[i], &R[i] };
                for (int c = 0; c < 2; ++c)
                {
                    auto& st = ch[c];
                    float x = *io[c];
                    x = st.lowTilt.process (st.tilt.process (x));
                    x = st.weight.process (x);
                    if (satMix > 0.0005f)
                        x += satMix * (st.sat.process (x, drive, 0.15f) - x);
                    x = lpActive ? st.lp.process (x, g, k) : x;
                    *io[c] = x;
                }

                // fold the lowest bass to mono as it gets darker
                if (monoAmount > 0.0f)
                {
                    const float side = 0.5f * (L[i] - R[i]);
                    const float lowSide = sideLow[1].process (sideLow[0].process (side));
                    L[i] -= monoAmount * lowSide;
                    R[i] += monoAmount * lowSide;
                }
            }
        }

        float currentAmount() const noexcept { return amountNow; }

    private:
        static constexpr int kUpdate = 16;

        float coef (double seconds) const { return 1.0f - (float) std::exp (-1.0 / (seconds * fs)); }

        void updateFilters (float a)
        {
            const auto s = shapeFor (a);
            for (auto& c : ch)
            {
                c.tilt.shelf (true, fs, 1000.0, -s.tiltDb, 0.6);
                c.lowTilt.shelf (false, fs, 300.0, s.tiltDb * 0.25, 0.6);
                c.weight.shelf (false, fs, 80.0, s.lowShelfDb, 0.9);
            }
            drive = s.drive;
            satMix = s.satMix;
            cutoff = s.cutoffHz;
            k = 1.0f / s.resonance;
            lpActive = a > 0.0005f;
            monoAmount = smoothstep (0.0f, 0.5f, a);
        }

        struct Channel { Biquad tilt, lowTilt, weight; Saturator sat; SVF lp; };
        Channel ch[2];
        Biquad sideLow[2];
        double fs = 48000.0;
        float amountTarget = 0.5f, amountNow = 0.5f, punch = 0.6f;
        float drive = 1.0f, satMix = 0.0f, cutoff = 20000.0f, k = 1.414f, monoAmount = 0.0f;
        bool lpActive = false;
        float envFast = 0, envSlow = 0, fastAtt = 0, fastRel = 0, slowAtt = 0, slowRel = 0;
        int counter = 0;
    };
}
