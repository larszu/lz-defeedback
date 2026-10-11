#pragma once

#include "FeedbackDetector.h"
#include "NotchBank.h"

#include <cmath>
#include <vector>

namespace lzdf
{

// Zero-latency feedback suppressor: notch bank in the signal path, detector on the (post-notch) output.
// Analysing the output closes the loop: a notch that is not deep enough keeps the peak alive and gets deepened.
class FeedbackSuppressor
{
public:
    enum class Mode
    {
        live,  // new notches float and are released after the hold time
        setup  // new notches are fixed (ring-out the system), never released until reset
    };

    struct Settings
    {
        double sensitivity = 0.5;     // 0..1
        double maxDepthDb = -24.0;    // deepest cut per notch
        double widthOct = 0.1;        // notch bandwidth
        int numNotches = 12;          // usable slots (1..NotchBank::maxNotches)
        Mode mode = Mode::live;
        double holdSeconds = 8.0;     // live notches stay untouched this long after the last hit
        double releaseSeconds = 20.0; // then recover 12 dB in this time
    };

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        fs = sampleRate;
        detector.prepare (fs);
        detector.setNotchQuery (&FeedbackSuppressor::nearNotchThunk, this);
        bank.prepare (fs, numChannels);
        mono.assign ((size_t) std::max (1, maxBlock), 0.0f);
        sinceDeepen.fill (1.0);
        applySettings();
    }

    void reset()
    {
        bank.reset();
        detector.reset();
        sinceDeepen.fill (1.0);
    }

    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        applySettings();
    }

    const Settings& getSettings() const noexcept { return settings; }

    // In-place processing of all channels; detection runs on the mono sum of the output.
    void process (float* const* data, int numChannels, int numSamples) noexcept
    {
        if (numSamples > (int) mono.size())
        {
            // host exceeded the announced block size: process in chunks
            for (int pos = 0; pos < numSamples; pos += (int) mono.size())
            {
                float* ptrs[NotchBank::maxChannels];
                const int chans = std::min (numChannels, NotchBank::maxChannels);
                for (int c = 0; c < chans; ++c)
                    ptrs[c] = data[c] + pos;
                process (ptrs, chans, std::min ((int) mono.size(), numSamples - pos));
            }
            return;
        }

        bank.process (data, numChannels, numSamples);

        const float g = numChannels > 0 ? 1.0f / (float) numChannels : 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                sum += data[c][i];
            mono[(size_t) i] = sum * g;
        }

        FeedbackDetector::Event events[FeedbackDetector::maxEvents];
        const int ne = detector.push (mono.data(), numSamples, events);
        for (int e = 0; e < ne; ++e)
            handleEvent (events[e]);

        updateRelease ((double) numSamples / fs);
        bank.publish();
    }

    // Lock in all current live notches as fixed (e.g. after ringing out).
    void fixAllCurrent() noexcept
    {
        for (auto& s : bank.getSlots())
            if (s.active)
                s.fixed = true;
    }

    NotchBank& getBank() noexcept { return bank; }
    const NotchBank& getBank() const noexcept { return bank; }
    FeedbackDetector& getDetector() noexcept { return detector; }
    const FeedbackDetector& getDetector() const noexcept { return detector; }

    int activeNotchCount() const noexcept
    {
        int n = 0;
        for (const auto& s : bank.getSlots())
            n += s.active ? 1 : 0;
        return n;
    }

private:
    static bool nearNotchThunk (void* ctx, double f)
    {
        return static_cast<FeedbackSuppressor*> (ctx)->findNotch (f) >= 0;
    }

    double matchOct() const noexcept { return std::max (settings.widthOct * 0.5, 1.0 / 24.0); }

    int findNotch (double f) const noexcept
    {
        const auto& slots = bank.getSlots();
        int best = -1;
        double bestDist = matchOct();
        for (int i = 0; i < NotchBank::maxNotches; ++i)
        {
            if (! slots[(size_t) i].active)
                continue;
            const double d = std::abs (std::log2 (f / slots[(size_t) i].freq));
            if (d < bestDist)
            {
                bestDist = d;
                best = i;
            }
        }
        return best;
    }

    void handleEvent (const FeedbackDetector::Event& ev) noexcept
    {
        auto& slots = bank.getSlots();
        const double step = 3.0;
        const double firstCut = 9.0;

        const int idx = findNotch (ev.freq);
        if (idx >= 0)
        {
            auto& s = slots[(size_t) idx];
            s.secondsSinceHit = 0.0;
            if (sinceDeepen[(size_t) idx] >= 0.04)
            {
                s.targetDb = std::max (s.targetDb - step, settings.maxDepthDb);
                sinceDeepen[(size_t) idx] = 0.0;
            }
            // follow slow drift of the feedback frequency
            s.freq = s.freq * std::pow (ev.freq / s.freq, 0.3);
            bank.updateCoefficients (s);
            return;
        }

        // new notch: free slot within the allowed count, else the shallowest floating one
        int target = -1;
        for (int i = 0; i < settings.numNotches; ++i)
            if (! slots[(size_t) i].active && slots[(size_t) i].currentDb == 0.0)
            {
                target = i;
                break;
            }
        if (target < 0)
        {
            double shallowest = -1e9;
            for (int i = 0; i < settings.numNotches; ++i)
            {
                const auto& s = slots[(size_t) i];
                if (s.active && ! s.fixed && s.targetDb > shallowest)
                {
                    shallowest = s.targetDb;
                    target = i;
                }
            }
        }
        if (target < 0)
            return; // all slots fixed

        auto& s = slots[(size_t) target];
        const bool reuse = s.currentDb < -0.01;
        s.active = true;
        s.fixed = settings.mode == Mode::setup;
        s.freq = ev.freq;
        s.targetDb = std::max (-firstCut, settings.maxDepthDb);
        s.secondsSinceHit = 0.0;
        if (! reuse)
        {
            s.currentDb = 0.0;
            s.z1.fill (0.0);
            s.z2.fill (0.0);
        }
        bank.updateCoefficients (s);
        sinceDeepen[(size_t) target] = 0.0;
    }

    void updateRelease (double dt) noexcept
    {
        const double rate = 12.0 / std::max (0.5, settings.releaseSeconds);
        auto& slots = bank.getSlots();
        for (int i = 0; i < NotchBank::maxNotches; ++i)
        {
            auto& s = slots[(size_t) i];
            sinceDeepen[(size_t) i] += dt;
            if (! s.active)
                continue;
            // slots beyond the allowed count are released regardless of mode
            const bool overLimit = i >= settings.numNotches;
            if (s.fixed && ! overLimit)
                continue;
            s.secondsSinceHit += dt;
            if (overLimit || s.secondsSinceHit > settings.holdSeconds)
            {
                s.targetDb = std::min (0.0, s.targetDb + rate * dt * (overLimit ? 10.0 : 1.0));
                if (s.targetDb > -0.5)
                {
                    s.targetDb = 0.0;
                    s.active = false;
                    s.fixed = false;
                }
            }
        }
    }

    void applySettings() noexcept
    {
        detector.setSensitivity (settings.sensitivity);
        bank.setWidthOctaves (settings.widthOct);
        for (auto& s : bank.getSlots())
            if (s.currentDb < -0.01)
                bank.updateCoefficients (s);
    }

    double fs = 48000.0;
    Settings settings;
    FeedbackDetector detector;
    NotchBank bank;
    std::vector<float> mono;
    std::array<double, NotchBank::maxNotches> sinceDeepen {};
};

} // namespace lzdf
