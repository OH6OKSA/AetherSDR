// Standalone test for BeatCanceller (beat cancel, Kenwood "BC").
// CMake target `beat_canceller_test`.  Exit 0 = pass.
//
//   - A steady carrier that starts in the middle of noise is notched: its
//     level 0.4 s after onset is down by more than 25 dB, and it was caught
//     within 200 ms.
//   - Noise alone, and a voice-like signal (harmonics of a gliding pitch),
//     are not notched.
//   - A notch is released after the carrier stops.

#include "core/BeatCanceller.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using AetherSDR::BeatCanceller;

namespace {

constexpr int kRate = 24000;
constexpr double kPi = 3.14159265358979323846;
int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

// Level of `freq` in x[from, from+len) by correlation, as amplitude.
double toneLevel(const std::vector<float>& x, int from, int len, double freq)
{
    double re = 0.0, im = 0.0;
    for (int i = 0; i < len; ++i) {
        const double ph = 2.0 * kPi * freq * (from + i) / kRate;
        re += x[from + i] * std::cos(ph);
        im += x[from + i] * std::sin(ph);
    }
    return 2.0 * std::sqrt(re * re + im * im) / len;
}

double rms(const std::vector<float>& x, int from, int len)
{
    double s = 0.0;
    for (int i = 0; i < len; ++i) {
        s += double(x[from + i]) * x[from + i];
    }
    return std::sqrt(s / len);
}

std::vector<float> noise(int n, float amp, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<float> d(0.0f, amp);
    std::vector<float> x(n);
    for (float& v : x) {
        v = d(rng);
    }
    return x;
}

// Runs mono through the canceller in 256-sample blocks; returns the output and
// the first sample index at which a notch existed (-1 if never).
std::vector<float> run(BeatCanceller& bc, std::vector<float> x, int* firstNotch = nullptr)
{
    if (firstNotch) {
        *firstNotch = -1;
    }
    for (size_t i = 0; i < x.size(); i += 256) {
        const int n = static_cast<int>(std::min<size_t>(256, x.size() - i));
        bc.processMono(x.data() + i, n);
        if (firstNotch && *firstNotch < 0 && bc.activeNotches() > 0) {
            *firstNotch = static_cast<int>(i);
        }
    }
    return x;
}

} // namespace

int main()
{
    // Carrier at 1234 Hz from 1.0 s, over noise.
    {
        BeatCanceller bc;
        bc.setSampleRate(kRate);
        const int n = 3 * kRate;
        std::vector<float> x = noise(n, 0.02f, 1);
        const int onset = kRate;
        for (int i = onset; i < n; ++i) {
            x[i] += 0.3f * static_cast<float>(std::sin(2.0 * kPi * 1234.0 * i / kRate));
        }
        int first = -1;
        const std::vector<float> y = run(bc, x, &first);
        const int at = onset + kRate * 4 / 10;
        const double before = toneLevel(x, at, kRate / 2, 1234.0);
        const double after = toneLevel(y, at, kRate / 2, 1234.0);
        const double dropDb = 20.0 * std::log10(before / std::max(after, 1e-9));
        std::printf("  carrier drop %.1f dB, caught %.0f ms after onset, notch at %.1f Hz\n",
                    dropDb, (first - onset) * 1000.0 / kRate, bc.notchHz(0));
        check(dropDb > 25.0, "steady carrier notched by > 25 dB");
        check(first >= onset && first - onset < kRate / 5, "carrier caught within 200 ms");
        check(std::abs(bc.notchHz(0) - 1234.0) < 3.0, "notch on the carrier frequency");
        check(first < 0 || first >= onset, "no notch before the carrier");
    }

    // Noise only.
    {
        BeatCanceller bc;
        bc.setSampleRate(kRate);
        int first = -1;
        run(bc, noise(3 * kRate, 0.05f, 2), &first);
        check(first < 0, "noise alone is not notched");
    }

    // Voice-like: harmonics of a pitch gliding 110..160 Hz at 4 Hz, with a
    // syllable envelope. Must pass essentially unchanged.
    {
        BeatCanceller bc;
        bc.setSampleRate(kRate);
        const int n = 3 * kRate;
        std::vector<float> x = noise(n, 0.005f, 3);
        double phase = 0.0;
        for (int i = 0; i < n; ++i) {
            const double t = double(i) / kRate;
            const double f0 = 135.0 + 25.0 * std::sin(2.0 * kPi * 4.0 * t)
                            + 10.0 * std::sin(2.0 * kPi * 1.3 * t);
            phase += 2.0 * kPi * f0 / kRate;
            const double env = 0.5 + 0.5 * std::sin(2.0 * kPi * 3.0 * t);
            double v = 0.0;
            for (int h = 1; h <= 20; ++h) {
                v += std::sin(h * phase) / h;
            }
            x[i] += static_cast<float>(0.2 * env * v);
        }
        int first = -1;
        const std::vector<float> y = run(bc, x, &first);
        const double lossDb = 20.0 * std::log10(rms(x, kRate, 2 * kRate) / rms(y, kRate, 2 * kRate));
        std::printf("  voice-like loss %.2f dB, first notch %d\n", lossDb, first);
        check(lossDb < 0.5, "voice-like signal passes (< 0.5 dB loss)");
    }

    // Release: carrier for 1 s, then silence/noise for 1 s.
    {
        BeatCanceller bc;
        bc.setSampleRate(kRate);
        const int n = 2 * kRate;
        std::vector<float> x = noise(n, 0.02f, 4);
        for (int i = 0; i < kRate; ++i) {
            x[i] += 0.3f * static_cast<float>(std::sin(2.0 * kPi * 800.0 * i / kRate));
        }
        run(bc, x);
        check(bc.activeNotches() == 0, "notch released after the carrier stops");
    }

    // Two carriers at once.
    {
        BeatCanceller bc;
        bc.setSampleRate(kRate);
        const int n = 2 * kRate;
        std::vector<float> x = noise(n, 0.02f, 5);
        for (int i = 0; i < n; ++i) {
            x[i] += 0.2f * static_cast<float>(std::sin(2.0 * kPi * 700.0 * i / kRate))
                  + 0.2f * static_cast<float>(std::sin(2.0 * kPi * 1900.0 * i / kRate));
        }
        const std::vector<float> y = run(bc, x);
        const int at = kRate;
        const double d1 = 20.0 * std::log10(toneLevel(x, at, kRate / 2, 700.0) / toneLevel(y, at, kRate / 2, 700.0));
        const double d2 = 20.0 * std::log10(toneLevel(x, at, kRate / 2, 1900.0) / toneLevel(y, at, kRate / 2, 1900.0));
        std::printf("  two carriers: %.1f dB, %.1f dB\n", d1, d2);
        check(d1 > 25.0 && d2 > 25.0, "two carriers both notched");
    }

    // Stereo path: same as mono on identical channels.
    {
        BeatCanceller bc;
        bc.setSampleRate(kRate);
        const int n = 2 * kRate;
        std::vector<float> lr(2 * n);
        std::vector<float> nz = noise(n, 0.02f, 6);
        for (int i = 0; i < n; ++i) {
            const float v = nz[i] + 0.3f * static_cast<float>(std::sin(2.0 * kPi * 1000.0 * i / kRate));
            lr[2 * i] = lr[2 * i + 1] = v;
        }
        bc.processStereo(lr.data(), n);
        std::vector<float> left(n);
        for (int i = 0; i < n; ++i) {
            left[i] = lr[2 * i];
        }
        const double after = toneLevel(left, kRate, kRate / 2, 1000.0);
        check(20.0 * std::log10(0.3 / after) > 25.0, "stereo carrier notched");
    }

    std::printf(g_failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
