#pragma once

#include <array>
#include <atomic>
#include "Constants.h"

#include <cmath>
#include <complex>
#include <vector>

namespace lzdf
{

// Bank of narrow peaking-cut biquads (RBJ cookbook) in the direct audio path.
// Adds no latency. A notch at 0 dB is skipped entirely, so an idle bank is bit-transparent.
class NotchBank
{
public:
    static constexpr int maxNotches = 24;
    static constexpr int maxChannels = 8;

    struct Slot
    {
        bool active = false;
        bool fixed = false;       // locked in setup mode, never released
        double freq = 1000.0;
        double targetDb = 0.0;    // negative = cut
        double currentDb = 0.0;   // smoothed towards targetDb
        double secondsSinceHit = 0.0;
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<double, maxChannels> z1 {}, z2 {};
    };

    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        channels = numChannels < maxChannels ? numChannels : maxChannels;
        reset();
    }

    void reset()
    {
        for (auto& s : slots)
            s = Slot {};
        publish();
    }

    void setWidthOctaves (double w) noexcept { widthOct = w; }

    // Process one block in place. Coefficients are refreshed every sub-block for click-free ramps.
    void process (float* const* data, int numChannels, int numSamples) noexcept
    {
        numChannels = numChannels < channels ? numChannels : channels;
        constexpr int sub = 32;
        // dB change allowed per sub-block: fast cut (~ 60 dB/s at 48 k), slow recovery handled by caller via targetDb.
        const double maxStep = 120.0 * sub / fs;

        for (int start = 0; start < numSamples; start += sub)
        {
            const int len = (numSamples - start) < sub ? (numSamples - start) : sub;

            for (auto& s : slots)
            {
                if (! s.active && s.currentDb == 0.0)
                    continue;

                const double diff = s.targetDb - s.currentDb;
                if (diff != 0.0)
                {
                    s.currentDb += diff > maxStep ? maxStep : (diff < -maxStep ? -maxStep : diff);
                    updateCoefficients (s);
                }

                if (s.currentDb > -0.01)
                {
                    if (! s.active)
                    {
                        s.currentDb = 0.0;
                        s.z1.fill (0.0);
                        s.z2.fill (0.0);
                    }
                    continue;
                }

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float* x = data[ch] + start;
                    double z1 = s.z1[(size_t) ch], z2 = s.z2[(size_t) ch];
                    for (int i = 0; i < len; ++i)
                    {
                        const double in = x[i];
                        const double out = s.b0 * in + z1;
                        z1 = s.b1 * in - s.a1 * out + z2;
                        z2 = s.b2 * in - s.a2 * out;
                        x[i] = (float) out;
                    }
                    s.z1[(size_t) ch] = z1;
                    s.z2[(size_t) ch] = z2;
                }
            }
        }
    }

    // Combined magnitude response in dB at frequency f (current state).
    double responseDb (double f) const noexcept
    {
        double db = 0.0;
        for (const auto& s : slots)
            if (s.currentDb < -0.01)
                db += magnitudeDb (s, f);
        return db;
    }

    std::array<Slot, maxNotches>& getSlots() noexcept { return slots; }
    const std::array<Slot, maxNotches>& getSlots() const noexcept { return slots; }

    // Lock-free snapshot for the UI thread.
    struct Published
    {
        std::atomic<float> freq { 0 }, depthDb { 0 };
        std::atomic<bool> active { false }, fixed { false };
    };

    void publish() noexcept
    {
        for (int i = 0; i < maxNotches; ++i)
        {
            pub[(size_t) i].freq.store ((float) slots[(size_t) i].freq, std::memory_order_relaxed);
            pub[(size_t) i].depthDb.store ((float) slots[(size_t) i].currentDb, std::memory_order_relaxed);
            pub[(size_t) i].active.store (slots[(size_t) i].active, std::memory_order_relaxed);
            pub[(size_t) i].fixed.store (slots[(size_t) i].fixed, std::memory_order_relaxed);
        }
    }

    const std::array<Published, maxNotches>& published() const noexcept { return pub; }

    void updateCoefficients (Slot& s) const noexcept
    {
        const double A = std::pow (10.0, s.currentDb / 40.0);
        const double w0 = 2.0 * lzdf::kPi * s.freq / fs;
        const double sw = std::sin (w0), cw = std::cos (w0);
        // bandwidth in octaves -> alpha (RBJ)
        const double alpha = sw * std::sinh (std::log (2.0) / 2.0 * widthOct * w0 / sw);
        const double a0 = 1.0 + alpha / A;
        s.b0 = (1.0 + alpha * A) / a0;
        s.b1 = (-2.0 * cw) / a0;
        s.b2 = (1.0 - alpha * A) / a0;
        s.a1 = (-2.0 * cw) / a0;
        s.a2 = (1.0 - alpha / A) / a0;
    }

private:
    double magnitudeDb (const Slot& s, double f) const noexcept
    {
        const double w = 2.0 * lzdf::kPi * f / fs;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
        const auto num = s.b0 + s.b1 * z1 + s.b2 * z2;
        const auto den = 1.0 + s.a1 * z1 + s.a2 * z2;
        return 20.0 * std::log10 (std::abs (num / den) + 1e-12);
    }

    double fs = 48000.0;
    int channels = 2;
    double widthOct = 0.1;
    std::array<Slot, maxNotches> slots {};
    std::array<Published, maxNotches> pub {};
};

} // namespace lzdf
