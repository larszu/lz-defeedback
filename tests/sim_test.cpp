// Closed-loop simulation tests for the DSP core (no JUCE needed).
//   mic = source + room(speaker delayed)  ->  suppressor  ->  speaker = tanh(out)
#include "../src/dsp/FeedbackSuppressor.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace lzdf;

namespace
{
double fs = 48000.0;
constexpr int block = 64;
int failures = 0;

void check (bool ok, const std::string& what)
{
    std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (! ok)
        ++failures;
}

double db (double x) { return 20.0 * std::log10 (x + 1e-12); }

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad bandpass (double f, double q)
    {
        Biquad b;
        const double w = 2 * lzdf::kPi * f / fs, al = std::sin (w) / (2 * q), a0 = 1 + al;
        b.b0 = al / a0; b.b1 = 0; b.b2 = -al / a0; b.a1 = -2 * std::cos (w) / a0; b.a2 = (1 - al) / a0;
        return b;
    }
    double tick (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Speaker -> mic path: broadband reflection plus resonant room modes, ~7 ms delay
// (larger than the block size, so block-wise processing stays causal).
struct Room
{
    struct Mode { Biquad bp; double gain; };
    std::vector<Mode> modes;
    double broadband = 0.15;
    double delaySec = 0.0073;

    double tick (double delayedSpeaker)
    {
        double y = broadband * delayedSpeaker;
        for (auto& m : modes)
            y += m.gain * m.bp.tick (delayedSpeaker);
        return y;
    }
};

Room makeRoom (std::initializer_list<std::pair<double, double>> modes)
{
    Room r;
    for (auto [f, g] : modes)
        r.modes.push_back ({ Biquad::bandpass (f, 8.0), g });
    return r;
}

// Harmonic notes with attack, decay and late vibrato.
struct Music
{
    std::mt19937 rng { 3 };
    double level = 0.25, sustainTau = 0.35, minLen = 0.25, maxLen = 0.9, vibratoDepth = 0.006;
    double noteStart = 0, noteLen = 0.5, f0 = 440, phase[6] {};
    double next (double t)
    {
        static const double scale[] = { 220.0, 246.9, 261.6, 293.7, 329.6, 349.2, 392.0, 440.0, 493.9, 523.3, 587.3, 659.3 };
        if (t - noteStart > noteLen)
        {
            noteStart = t;
            noteLen = std::uniform_real_distribution<double> (minLen, maxLen) (rng);
            f0 = scale[std::uniform_int_distribution<int> (0, 11) (rng)];
        }
        const double nt = t - noteStart;
        const double env = std::min (1.0, nt / 0.02) * std::exp (-nt / sustainTau);
        const double vib = nt > 0.15 ? 1.0 + vibratoDepth * std::sin (2 * lzdf::kPi * 5.5 * nt) : 1.0;
        double s = 0;
        for (int h = 1; h <= 6; ++h)
        {
            phase[h - 1] += 2 * lzdf::kPi * f0 * h * vib / fs;
            s += std::sin (phase[h - 1]) / h;
        }
        return level * env * s;
    }
};

struct LoopResult
{
    double tailRmsDb = 0, tailPeak = 0, firstNotchSec = -1, deepest = 0;
    int notches = 0;
};

LoopResult runLoop (Room room, double seconds, bool withSuppressor, bool musicSource = false)
{
    FeedbackSuppressor sup;
    sup.prepare (fs, block, 1);
    std::mt19937 rng (7);
    std::normal_distribution<double> noise (0.0, 0.01); // ~ -40 dBFS
    Music music;
    music.level = 0.03;

    const int total = (int) (seconds * fs);
    const int delay = (int) (room.delaySec * fs);
    const int tailStart = total - (int) (5.0 * fs);
    double tailSum = 0;
    LoopResult r;
    float buf[block];
    std::vector<double> speaker ((size_t) total + block, 0.0);

    for (int n0 = 0; n0 < total; n0 += block)
    {
        for (int i = 0; i < block; ++i)
        {
            const int n = n0 + i;
            const double delayed = n >= delay ? speaker[(size_t) (n - delay)] : 0.0;
            const double src = musicSource ? music.next (n / fs) + 0.1 * noise (rng) : noise (rng);
            buf[i] = (float) (src + room.tick (delayed));
        }
        float* ptr[1] = { buf };
        if (withSuppressor)
            sup.process (ptr, 1, block);
        for (int i = 0; i < block; ++i)
        {
            speaker[(size_t) (n0 + i)] = std::tanh ((double) buf[i]);
            if (n0 + i >= tailStart)
            {
                tailSum += (double) buf[i] * buf[i];
                r.tailPeak = std::max (r.tailPeak, (double) std::abs (buf[i]));
            }
        }
        if (withSuppressor && r.firstNotchSec < 0 && sup.activeNotchCount() > 0)
            r.firstNotchSec = n0 / fs;
    }
    r.tailRmsDb = db (std::sqrt (tailSum / (total - tailStart)));
    if (withSuppressor)
    {
        r.notches = sup.activeNotchCount();
        for (const auto& s : sup.getBank().getSlots())
            if (s.active)
                r.deepest = std::min (r.deepest, s.currentDb);
    }
    return r;
}

void testTransparency()
{
    std::printf ("transparency (no feedback, broadband noise, stereo):\n");
    FeedbackSuppressor sup;
    sup.prepare (fs, block, 2);
    std::mt19937 rng (1);
    std::normal_distribution<float> noise (0.0f, 0.05f);
    float l[block], r[block], lr[block], rr[block];
    bool identical = true;
    int maxNotches = 0;
    for (int b = 0; b < (int) (10 * fs / block); ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            l[i] = lr[i] = noise (rng);
            r[i] = rr[i] = noise (rng);
        }
        float* p[2] = { l, r };
        sup.process (p, 2, block);
        maxNotches = std::max (maxNotches, sup.activeNotchCount());
        if (sup.activeNotchCount() == 0 && (std::memcmp (l, lr, sizeof l) != 0 || std::memcmp (r, rr, sizeof r) != 0))
            identical = false;
    }
    check (maxNotches == 0, "no notch on broadband noise (got " + std::to_string (maxNotches) + ")");
    check (identical, "bit-identical output while idle (zero latency, no colouring)");
}

void testLoop (const char* name, Room room, int expectMinNotches, bool musicSource = false, double reactLimit = 1.5,
               double seconds = 20.0)
{
    std::printf ("%s:\n", name);
    const auto open = runLoop (room, seconds, false, musicSource);
    const auto with = runLoop (room, seconds, true, musicSource);
    std::printf ("    without: tail %.1f dBFS peak %.2f | with: tail %.1f dBFS peak %.2f, notches %d, deepest %.1f dB, first after %.2f s\n",
                 open.tailRmsDb, open.tailPeak, with.tailRmsDb, with.tailPeak, with.notches, with.deepest, with.firstNotchSec);
    check (open.tailRmsDb > -15.0, "loop is really unstable without suppressor");
    check (with.tailRmsDb < -30.0, "suppressor keeps the loop near the source level");
    check (with.tailPeak < 0.25, "no residual howl peaks");
    check (with.notches >= expectMinNotches, "at least " + std::to_string (expectMinNotches) + " notch(es) set");
    check (with.firstNotchSec >= 0 && with.firstNotchSec < reactLimit, "reacts within " + std::to_string (reactLimit).substr (0, 3) + " s");
}

int countNotchesOn (Music music, double seconds, double* deepestOut)
{
    FeedbackSuppressor sup;
    sup.prepare (fs, block, 1);
    std::mt19937 rng (5);
    std::normal_distribution<double> breath (0.0, 0.002);
    float buf[block];
    int maxNotches = 0;
    double deepest = 0;
    for (int n0 = 0; n0 < (int) (seconds * fs); n0 += block)
    {
        for (int i = 0; i < block; ++i)
            buf[i] = (float) (music.next ((n0 + i) / fs) + breath (rng));
        float* p[1] = { buf };
        sup.process (p, 1, block);
        maxNotches = std::max (maxNotches, sup.activeNotchCount());
        for (const auto& s : sup.getBank().getSlots())
            deepest = std::min (deepest, s.currentDb);
    }
    *deepestOut = deepest;
    return maxNotches;
}

void testMusic()
{
    std::printf ("music (no feedback, harmonic notes with vibrato and decay):\n");
    double deepest = 0;
    const int n = countNotchesOn (Music {}, 60.0, &deepest);
    std::printf ("    max simultaneous notches %d, deepest %.1f dB\n", n, deepest);
    check (n <= 1, "music does not trigger notches (max 1 tolerated)");
    check (deepest > -9.5, "no deepened notch on music");
}

void testSungNotes (const char* name, double vibratoDepth)
{
    std::printf ("%s:\n", name);
    Music m;
    m.sustainTau = 4.0;
    m.minLen = 2.0;
    m.maxLen = 3.0;
    m.vibratoDepth = vibratoDepth;
    double deepest = 0;
    const int n = countNotchesOn (m, 60.0, &deepest);
    std::printf ("    max simultaneous notches %d, deepest %.1f dB\n", n, deepest);
    check (n <= 1, "sustained notes do not trigger notches (max 1 tolerated)");
}
} // namespace

int main()
{
    testTransparency();
    testLoop ("single room mode 1.2 kHz", makeRoom ({ { 1200.0, 1.25 } }), 1);
    testLoop ("two room modes 450 Hz + 2.7 kHz", makeRoom ({ { 450.0, 1.2 }, { 2700.0, 1.3 } }), 2);
    testLoop ("strong mode 3.4 kHz", makeRoom ({ { 3400.0, 1.6 } }), 1);
    testLoop ("slowly building feedback 800 Hz (loop gain just above 1)", makeRoom ({ { 800.0, 0.92 } }), 1, false, 4.0, 30.0);
    testLoop ("feedback while music plays, 1.6 kHz", makeRoom ({ { 1600.0, 1.25 } }), 1, true);
    testMusic();
    testSungNotes ("long sung notes with vibrato (2-3 s, +-2 %)", 0.02);
    testSungNotes ("long straight notes without vibrato (2-3 s)", 0.0);

    fs = 96000.0;
    std::printf ("\n--- 96 kHz ---\n");
    testTransparency();
    testLoop ("single room mode 1.2 kHz @96k", makeRoom ({ { 1200.0, 1.25 } }), 1);
    testMusic();

    for (double rate : { 192000.0, 11025.0 })
    {
        fs = rate;
        std::printf ("\n--- %.0f Hz ---\n", rate);
        testTransparency();
        testLoop ("single room mode 1.2 kHz", makeRoom ({ { 1200.0, 1.25 } }), 1);
    }

    std::printf (failures == 0 ? "\nALL PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
