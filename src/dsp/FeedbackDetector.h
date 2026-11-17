#pragma once

#include "Fft.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

namespace lzdf
{

// Detects acoustic feedback (howling) in a mono analysis stream.
//
// A peak is only reported when it is
//   - narrow band   (peak-to-neighbour ratio, PNPR)
//   - prominent     (peak-to-average ratio, PAPR, plus an absolute floor)
//   - persistent    (tracked over consecutive frames)
//   - not decaying  (level slope over the track)
//   - stable        (frequency jitter; rejects vibrato)
//   - non-harmonic  (tones with a strong octave partner must grow for much longer)
// Runs on the analysis side only; the audio path never waits for it.
class FeedbackDetector
{
public:
    struct Event
    {
        double freq = 0.0;
        double levelDb = -200.0;
    };

    static constexpr int maxEvents = 8;
    static constexpr int maxTracks = 16;
    static constexpr int historyLen = 24;
    static constexpr int maxSpectrumBins = 16384; // largest FFT size (fs > 100 kHz)

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        // ~85 ms window at any rate (4096 at 44.1/48 kHz): enough resolution to place narrow notches
        n = 256;
        while (n < (int) (fs * 0.08) && n < maxSpectrumBins)
            n <<= 1;
        hop = n / 8;
        fft.prepare (n);
        ring.assign ((size_t) n, 0.0f);
        frame.assign ((size_t) n, {});
        powerDb.assign ((size_t) n / 2 + 1, -200.0f);
        window.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * lzdf::kPi * i / n));
        binHz = fs / n;
        kLow = (int) std::ceil (60.0 / binHz);
        kHigh = (int) std::floor (std::min (16000.0, 0.45 * fs) / binHz);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        writePos = 0;
        sinceHop = 0;
        for (auto& t : tracks)
            t = Track {};
        for (auto& s : spectrum)
            s.store (-120.0f, std::memory_order_relaxed);
    }

    // 0 = conservative, 1 = aggressive
    void setSensitivity (double s) noexcept { sensitivity = s < 0 ? 0 : (s > 1 ? 1 : s); }

    // Called by the suppressor so that frequencies already under a notch re-trigger faster.
    void setNotchQuery (bool (*fn) (void*, double), void* ctx) noexcept
    {
        nearNotch = fn;
        nearNotchCtx = ctx;
    }

    double getBinHz() const noexcept { return binHz; }
    int getFftSize() const noexcept { return n; }
    int getHop() const noexcept { return hop; }

    // Push analysis samples. Returns the number of events written to `out` (only on frame boundaries).
    int push (const float* x, int num, Event* out) noexcept
    {
        int count = 0;
        for (int i = 0; i < num; ++i)
        {
            ring[(size_t) writePos] = x[i];
            writePos = (writePos + 1) % n;
            if (++sinceHop >= hop)
            {
                sinceHop = 0;
                count += analyseFrame (out + count, maxEvents - count);
            }
        }
        return count;
    }

    // UI: smoothed spectrum in dBFS, bins 0..n/2
    const std::array<std::atomic<float>, maxSpectrumBins / 2 + 1>& getSpectrum() const noexcept { return spectrum; }
    int getNumSpectrumBins() const noexcept { return n / 2 + 1; }

private:
    struct Track
    {
        bool active = false;
        int frames = 0;
        int missing = 0;
        int count = 0; // valid entries in history
        double freq = 0.0;
        std::array<float, historyLen> level {}, freqBins {};
    };

    struct Peak
    {
        double bin = 0.0;
        float level = -200.0f, pnpr = 0.0f;
    };
    static constexpr int maxPeaks = 48;

    struct Candidate
    {
        double bin = 0.0;
        float level = -200.0f;
        bool harmonic = false;
    };

    static float toDb (float p) noexcept { return 10.0f * std::log10 (p + 1e-20f); }

    int analyseFrame (Event* out, int capacity) noexcept
    {
        // window + FFT, amplitude normalised so a full-scale sine reads 0 dB
        for (int i = 0; i < n; ++i)
            frame[(size_t) i] = { ring[(size_t) ((writePos + i) % n)] * window[(size_t) i], 0.0f };
        fft.forward (frame.data());

        const float scale = 4.0f / (float) n;
        double sumLin = 0.0;
        for (int k = 0; k <= n / 2; ++k)
        {
            const float p = std::norm (frame[(size_t) k]) * scale * scale;
            powerDb[(size_t) k] = toDb (p);
            if (k >= kLow && k <= kHigh)
                sumLin += p;
            auto& s = spectrum[(size_t) k];
            const float prev = s.load (std::memory_order_relaxed);
            s.store (prev + 0.35f * (powerDb[(size_t) k] - prev), std::memory_order_relaxed);
        }
        const float avgDb = toDb ((float) (sumLin / (kHigh - kLow + 1)));

        const double s = sensitivity;
        const float pnprMin = (float) (26.0 - 12.0 * s);
        const float paprMin = (float) (20.0 - 10.0 * s);
        const float floorDb = -72.0f;

        // all tonal peaks of this frame (used for the harmonic-series test)
        numPeaks = 0;
        for (int k = kLow + 9; k <= kHigh - 9 && numPeaks < maxPeaks; ++k)
        {
            const float p = powerDb[(size_t) k];
            if (p < floorDb || ! (p > powerDb[(size_t) k - 1] && p >= powerDb[(size_t) k + 1]
                                  && p >= powerDb[(size_t) k - 2] && p >= powerDb[(size_t) k + 2]))
                continue;
            const float pnpr = p - neighbourDb (k);
            if (pnpr < 10.0f)
                continue;
            peaks[(size_t) numPeaks++] = { k + parabolicOffset (k), p, pnpr };
        }

        // candidates: peaks that are narrow and prominent enough to be feedback
        std::array<Candidate, maxEvents> cand {};
        int numCand = 0;
        for (int i = 0; i < numPeaks; ++i)
        {
            const auto& pk = peaks[(size_t) i];
            if (pk.level - avgDb < paprMin || pk.pnpr < pnprMin)
                continue;

            Candidate c;
            c.bin = pk.bin;
            c.level = pk.level;
            c.harmonic = isPartOfHarmonicSeries (i);

            if (numCand < maxEvents)
                cand[(size_t) numCand++] = c;
            else
            {
                int weakest = 0;
                for (int j = 1; j < maxEvents; ++j)
                    if (cand[(size_t) j].level < cand[(size_t) weakest].level)
                        weakest = j;
                if (c.level > cand[(size_t) weakest].level)
                    cand[(size_t) weakest] = c;
            }
        }

        // associate candidates with tracks
        int written = 0;
        std::array<bool, maxTracks> hit {};
        for (int c = 0; c < numCand; ++c)
        {
            const auto& cd = cand[(size_t) c];
            int best = -1;
            double bestDist = 1.5;
            for (int t = 0; t < maxTracks; ++t)
            {
                if (! tracks[(size_t) t].active || hit[(size_t) t])
                    continue;
                const double d = std::abs (tracks[(size_t) t].freq / binHz - cd.bin);
                if (d < bestDist)
                {
                    bestDist = d;
                    best = t;
                }
            }
            if (best < 0)
                best = allocateTrack();
            auto& tr = tracks[(size_t) best];
            if (! tr.active)
            {
                tr = Track {};
                tr.active = true;
            }
            hit[(size_t) best] = true;
            // A jump of more than 6 dB within one frame is a note attack, not loop growth: start over.
            if (tr.count > 0 && cd.level - tr.level[0] > 6.0f)
            {
                tr.frames = 0;
                tr.count = 0;
            }
            tr.missing = 0;
            tr.frames++;
            tr.freq = cd.bin * binHz;
            for (int h = historyLen - 1; h > 0; --h)
            {
                tr.level[(size_t) h] = tr.level[(size_t) h - 1];
                tr.freqBins[(size_t) h] = tr.freqBins[(size_t) h - 1];
            }
            tr.level[0] = cd.level;
            tr.freqBins[0] = (float) cd.bin;
            tr.count = tr.count < historyLen ? tr.count + 1 : historyLen;
            // frames only count while the peak behaves like feedback (not decaying)
            if (tr.count > 8 && tr.level[0] - tr.level[8] < -1.5f)
                tr.frames = 0;

            // Feedback holds or grows; played notes decay. Harmonic tones need to grow and last longer.
            int required = (int) std::lround (30.0 - 24.0 * s); // ~320 ms .. ~64 ms at 48 kHz
            double minSlope = -0.12;                             // dB per frame (~ -11 dB/s)
            if (cd.harmonic)
            {
                required *= 4;
                minSlope = 0.1; // must clearly grow
            }
            if (nearNotch != nullptr && nearNotch (nearNotchCtx, tr.freq))
            {
                required = std::max (3, required / 3);
                minSlope = std::max (minSlope, -0.05);
            }

            if (tr.frames >= required && qualifies (tr, minSlope) && (! cd.harmonic || growsSteadily (tr)) && written < capacity)
            {
                out[written].freq = tr.freq;
                out[written].levelDb = cd.level;
                ++written;
                tr.frames = 0; // must re-qualify before the next action at this frequency
            }
        }

        for (int t = 0; t < maxTracks; ++t)
        {
            auto& tr = tracks[(size_t) t];
            if (tr.active && ! hit[(size_t) t] && ++tr.missing > 2)
                tr.active = false;
        }
        return written;
    }

private:
    bool qualifies (const Track& tr, double minSlope) const noexcept
    {
        const int w = std::min (tr.count, 16);
        if (w < 3)
            return false;
        // least-squares slope of level (dB/frame), index 0 = newest
        double sx = 0, sy = 0, sxx = 0, sxy = 0, mf = 0;
        for (int i = 0; i < w; ++i)
        {
            const double x = -i;
            const double y = tr.level[(size_t) i];
            sx += x; sy += y; sxx += x * x; sxy += x * y;
            mf += tr.freqBins[(size_t) i];
        }
        const double slope = (w * sxy - sx * sy) / (w * sxx - sx * sx);
        mf /= w;
        double var = 0;
        for (int i = 0; i < w; ++i)
            var += (tr.freqBins[(size_t) i] - mf) * (tr.freqBins[(size_t) i] - mf);
        const double freqStd = std::sqrt (var / w);

        return slope > minSlope && freqStd < 0.35;
    }

    static double slopeOf (const Track& tr, int from, int len) noexcept
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int i = from; i < from + len; ++i)
        {
            const double x = -i, y = tr.level[(size_t) i];
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        return (len * sxy - sx * sy) / (len * sxx - sx * sx);
    }

    // Exponential loop growth is a straight line in dB over ~260 ms; a (re-)attack smeared by the window dips, rises and flattens.
    static bool growsSteadily (const Track& tr) noexcept
    {
        return tr.count >= 24 && slopeOf (tr, 0, 8) > 0.05 && slopeOf (tr, 8, 8) > 0.05 && slopeOf (tr, 16, 8) > 0.05;
    }

    int allocateTrack() noexcept
    {
        int victim = 0;
        for (int t = 0; t < maxTracks; ++t)
        {
            if (! tracks[(size_t) t].active)
                return t;
            if (tracks[(size_t) t].frames < tracks[(size_t) victim].frames)
                victim = t;
        }
        tracks[(size_t) victim].active = false;
        return victim;
    }

    float neighbourDb (int k) const noexcept
    {
        double sum = 0.0;
        int cnt = 0;
        for (int d = 4; d <= 9; ++d)
        {
            sum += std::pow (10.0, powerDb[(size_t) (k - d)] / 10.0);
            sum += std::pow (10.0, powerDb[(size_t) (k + d)] / 10.0);
            cnt += 2;
        }
        return toDb ((float) (sum / cnt));
    }

    double parabolicOffset (int k) const noexcept
    {
        const double a = powerDb[(size_t) k - 1], b = powerDb[(size_t) k], c = powerDb[(size_t) k + 1];
        const double den = a - 2.0 * b + c;
        if (std::abs (den) < 1e-9)
            return 0.0;
        const double off = 0.5 * (a - c) / den;
        return off > 0.5 ? 0.5 : (off < -0.5 ? -0.5 : off);
    }

    // True if this peak is an integer multiple (2..8) of another tonal peak, or has one at twice its frequency.
    // Voices and instruments are harmonic series; a saturating amplifier adds only odd harmonics to feedback,
    // so the octave above is the strongest hint for a played note.
    bool isPartOfHarmonicSeries (int idx) const noexcept
    {
        const auto& me = peaks[(size_t) idx];
        for (int j = 0; j < numPeaks; ++j)
        {
            if (j == idx)
                continue;
            const auto& other = peaks[(size_t) j];
            if (other.level < me.level - 30.0f)
                continue;
            if (other.bin < me.bin)
            {
                const double ratio = me.bin / other.bin;
                const double m = std::round (ratio);
                if (m >= 2.0 && m <= 8.0 && std::abs (me.bin - m * other.bin) <= std::max (1.5, 0.01 * me.bin))
                    return true;
            }
            else if (std::abs (other.bin - 2.0 * me.bin) <= std::max (1.5, 0.01 * other.bin) && other.level > me.level - 24.0f)
                return true;
        }
        return false;
    }

    double fs = 48000.0, binHz = 11.72;
    int n = 4096, hop = 512, kLow = 6, kHigh = 1365;
    int writePos = 0, sinceHop = 0;
    double sensitivity = 0.5;
    Fft fft;
    std::vector<float> ring, window, powerDb;
    std::vector<std::complex<float>> frame;
    std::array<Track, maxTracks> tracks {};
    std::array<Peak, maxPeaks> peaks {};
    int numPeaks = 0;
    std::array<std::atomic<float>, maxSpectrumBins / 2 + 1> spectrum {};
    bool (*nearNotch) (void*, double) = nullptr;
    void* nearNotchCtx = nullptr;
};

} // namespace lzdf
