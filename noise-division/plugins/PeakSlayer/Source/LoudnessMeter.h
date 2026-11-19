#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// Output metering: sample peak, RMS (300 ms) and short-term loudness
// (LUFS, 3 s window, K-weighted as in ITU-R BS.1770 / EBU R128).
// process() runs on the audio thread; the atomics are read by the editor.
class LoudnessMeter
{
public:
    std::atomic<float> peak { 0.0f };         // linear, max since the editor last read it
    std::atomic<float> rmsDb { -100.0f };
    std::atomic<float> lufsShort { -100.0f };
    std::atomic<std::uint32_t> blocks { 0 };  // increments every processed block

    void prepare (double sampleRate) noexcept
    {
        const double pi = 3.14159265358979323846;

        // K-weighting stage 1: high shelf
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K  = std::tan (pi * f0 / sampleRate);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                      2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        // K-weighting stage 2: high pass
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K  = std::tan (pi * f0 / sampleRate);
            const double a0 = 1.0 + K / Q + K * K;
            highpass = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }

        rmsCoef  = 1.0 - std::exp (-1.0 / (0.3 * sampleRate));
        blockLen = (int) std::lround (sampleRate * 0.1); // 100 ms blocks
        reset();
    }

    void reset() noexcept
    {
        for (auto& ch : state) ch = {};
        ring.fill (0.0);
        ringPos = 0; ringFilled = 0; blockAcc = 0.0; blockCount = 0; rmsState = 0.0;
        peak.store (0.0f); rmsDb.store (-100.0f); lufsShort.store (-100.0f);
    }

    void process (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = numChannels > maxChannels ? maxChannels : numChannels;
        if (numChannels <= 0 || blockLen <= 0)
            return;

        float pk = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            double meanSq = 0.0, kSum = 0.0;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const double x = (double) channels[ch][i];
                const float ax = std::abs ((float) x);
                if (ax > pk) pk = ax;
                meanSq += x * x;

                auto& s = state[(size_t) ch];
                const double y1 = tick (shelf, s[0], s[1], x);
                const double y2 = tick (highpass, s[2], s[3], y1);
                kSum += y2 * y2;
            }

            rmsState += rmsCoef * (meanSq / (double) numChannels - rmsState);

            blockAcc += kSum;
            if (++blockCount >= blockLen)
            {
                ring[(size_t) ringPos] = blockAcc / (double) blockLen;
                ringPos = (ringPos + 1) % ringSize;
                if (ringFilled < ringSize) ++ringFilled;
                blockAcc = 0.0; blockCount = 0;

                double total = 0.0;
                for (int k = 0; k < ringFilled; ++k) total += ring[(size_t) k];
                const double mean = total / (double) ringFilled;
                lufsShort.store (mean > 1.0e-10 ? (float) (-0.691 + 10.0 * std::log10 (mean)) : -100.0f);
            }
        }

        if (pk > peak.load (std::memory_order_relaxed))
            peak.store (pk, std::memory_order_relaxed);

        rmsDb.store (rmsState > 1.0e-10 ? (float) (10.0 * std::log10 (rmsState)) : -100.0f);
        blocks.fetch_add (1, std::memory_order_relaxed);
    }

private:
    struct Coefs { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };

    // transposed direct form II biquad
    static double tick (const Coefs& c, double& z1, double& z2, double x) noexcept
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    static constexpr int maxChannels = 2;
    static constexpr int ringSize = 30; // 30 x 100 ms = 3 s

    Coefs shelf, highpass;
    std::array<std::array<double, 4>, maxChannels> state {};
    std::array<double, ringSize> ring {};
    int ringPos = 0, ringFilled = 0, blockLen = 0, blockCount = 0;
    double blockAcc = 0.0, rmsState = 0.0, rmsCoef = 0.0;
};
