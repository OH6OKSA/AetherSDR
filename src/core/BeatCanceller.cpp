#include "BeatCanceller.h"

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Detection tuning (see the header for the behaviour these give).
constexpr double kMinHz = 200.0;          // below: voice fundamentals, hum
constexpr double kMaxHz = 4000.0;
constexpr float kPeakOverMedian = 31.6f;  // 15 dB
constexpr float kStrongOverMedian = 1000.0f;  // 30 dB: confirm faster
constexpr float kNarrowFraction = 0.85f;  // of the ±6-bin power in ±2 bins
constexpr double kMatchHz = 8.0;          // same carrier from frame to frame
constexpr double kMaxDriftHz = 4.0;       // a carrier holds its frequency
constexpr int kConfirmHops = 6;           // ~130 ms at 24 kHz
constexpr int kConfirmHopsStrong = 3;     // ~65 ms
constexpr int kReleaseHops = 14;          // ~300 ms gone before release
constexpr double kNotchWidthHz = 40.0;
constexpr double kFadeSeconds = 0.015;

void fftInPlace(std::vector<std::complex<float>>& a)
{
    const int n = static_cast<int>(a.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / len;
        const std::complex<float> wl(static_cast<float>(std::cos(ang)), static_cast<float>(std::sin(ang)));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k];
                const std::complex<float> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

} // namespace

BeatCanceller::BeatCanceller()
{
    setSampleRate(24000);
}

void BeatCanceller::setSampleRate(int hz)
{
    m_rate = std::max(8000, hz);
    // ~43 ms frames, half-frame hop: 1024 at 24 kHz, 2048 at 48 kHz.
    int size = 256;
    while (size < m_rate * 0.043) {
        size <<= 1;
    }
    m_size = size;
    m_hop = size / 2;
    m_window.assign(m_size, 0.0f);
    for (int i = 0; i < m_size; ++i) {
        m_window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / m_size));
    }
    m_fifo.assign(m_size, 0.0f);
    m_fft.assign(m_size, {});
    m_power.assign(m_size / 2 + 1, 0.0f);
    m_sorted.reserve(m_size / 2 + 1);
    m_peaks.reserve(64);
    m_fadeStep = static_cast<float>(1.0 / (kFadeSeconds * m_rate));
    reset();
}

void BeatCanceller::reset()
{
    std::fill(m_fifo.begin(), m_fifo.end(), 0.0f);
    m_fifoPos = 0;
    m_sinceHop = 0;
    m_tracks = {};
    for (Notch& n : m_notches) {
        n = Notch{};
    }
}

int BeatCanceller::activeNotches() const
{
    return static_cast<int>(std::count_if(m_notches.begin(), m_notches.end(),
                                          [](const Notch& n) { return n.used; }));
}

double BeatCanceller::notchHz(int i) const
{
    return (i >= 0 && i < kMaxNotches && m_notches[i].used) ? m_notches[i].freq : 0.0;
}

void BeatCanceller::processStereo(float* lr, int frames)
{
    for (int i = 0; i < frames; ++i) {
        float& l = lr[2 * i];
        float& r = lr[2 * i + 1];
        pushAnalysis(0.5f * (l + r));
        l = applyNotches(l, 0);
        r = applyNotches(r, 1);
        advanceFades();
    }
}

void BeatCanceller::processMono(float* x, int frames)
{
    for (int i = 0; i < frames; ++i) {
        pushAnalysis(x[i]);
        x[i] = applyNotches(x[i], 0);
        advanceFades();
    }
}

void BeatCanceller::pushAnalysis(float mid)
{
    m_fifo[m_fifoPos] = mid;
    m_fifoPos = (m_fifoPos + 1) % m_size;
    if (++m_sinceHop >= m_hop) {
        m_sinceHop = 0;
        analyse();
    }
}

void BeatCanceller::analyse()
{
    for (int i = 0; i < m_size; ++i) {
        m_fft[i] = {m_fifo[(m_fifoPos + i) % m_size] * m_window[i], 0.0f};
    }
    fftInPlace(m_fft);
    const int half = m_size / 2;
    for (int k = 0; k <= half; ++k) {
        m_power[k] = std::norm(m_fft[k]);
    }

    const double binHz = static_cast<double>(m_rate) / m_size;
    const int kLo = std::max(7, static_cast<int>(kMinHz / binHz));
    const int kHi = std::min(half - 7, static_cast<int>(kMaxHz / binHz));

    // Median power of the band: the reference a carrier stands out from.
    m_sorted.assign(m_power.begin() + kLo, m_power.begin() + kHi + 1);
    auto mid = m_sorted.begin() + m_sorted.size() / 2;
    std::nth_element(m_sorted.begin(), mid, m_sorted.end());
    const float median = std::max(*mid, 1e-12f);

    m_peaks.clear();
    for (int k = kLo; k <= kHi; ++k) {
        const float p = m_power[k];
        if (p < median * kPeakOverMedian || p <= m_power[k - 1] || p < m_power[k + 1]
            || p < m_power[k - 2] || p < m_power[k + 2]) {
            continue;
        }
        // Narrow: a pure tone's Hann main lobe is ±2 bins.
        float core = 0.0f;
        float wide = 0.0f;
        for (int j = -6; j <= 6; ++j) {
            wide += m_power[k + j];
            if (std::abs(j) <= 2) {
                core += m_power[k + j];
            }
        }
        if (core < kNarrowFraction * wide) {
            continue;
        }
        // Parabolic interpolation on log power for the exact frequency.
        const double a = std::log(m_power[k - 1] + 1e-20);
        const double b = std::log(p + 1e-20);
        const double c = std::log(m_power[k + 1] + 1e-20);
        const double den = a - 2.0 * b + c;
        const double delta = den != 0.0 ? std::clamp(0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
        m_peaks.push_back({(k + delta) * binHz, p >= median * kStrongOverMedian});
        k += 2;
    }

    // Match peaks to tracks.
    std::array<bool, 8> seen{};
    for (const Peak& peak : m_peaks) {
        int best = -1;
        double bestDiff = kMatchHz;
        for (int t = 0; t < static_cast<int>(m_tracks.size()); ++t) {
            const Track& tr = m_tracks[t];
            const double d = std::abs(tr.freq - peak.freq);
            if (tr.used && !seen[t] && d <= bestDiff) {
                best = t;
                bestDiff = d;
            }
        }
        if (best < 0) {
            for (int t = 0; t < static_cast<int>(m_tracks.size()); ++t) {
                if (!m_tracks[t].used) {
                    m_tracks[t] = Track{};
                    m_tracks[t].used = true;
                    m_tracks[t].freq = m_tracks[t].minFreq = m_tracks[t].maxFreq = peak.freq;
                    best = t;
                    break;
                }
            }
            if (best < 0) {
                continue;   // all tracks busy
            }
        }
        Track& tr = m_tracks[best];
        seen[best] = true;
        tr.freq = tr.hits == 0 ? peak.freq : 0.7 * tr.freq + 0.3 * peak.freq;
        if (tr.slot < 0) {
            tr.minFreq = tr.hits == 0 ? peak.freq : std::min(tr.minFreq, peak.freq);
            tr.maxFreq = tr.hits == 0 ? peak.freq : std::max(tr.maxFreq, peak.freq);
        }
        tr.hits++;
        tr.misses = 0;
        tr.strong = tr.strong || peak.strong;
    }

    for (int t = 0; t < static_cast<int>(m_tracks.size()); ++t) {
        Track& tr = m_tracks[t];
        if (!tr.used) {
            continue;
        }
        if (!seen[t]) {
            tr.misses++;
            if (tr.slot >= 0) {
                if (tr.misses >= kReleaseHops) {
                    m_notches[tr.slot].active = false;   // fades out, then frees
                    tr = Track{};
                }
            } else {
                tr = Track{};   // a candidate must be seen every frame
            }
            continue;
        }
        if (tr.slot >= 0) {
            setNotch(m_notches[tr.slot], tr.freq);
            continue;
        }
        // A drifting peak is voice, not a carrier: start its count again.
        if (tr.maxFreq - tr.minFreq > kMaxDriftHz) {
            tr.hits = 1;
            tr.minFreq = tr.maxFreq = tr.freq;
            tr.strong = false;
            continue;
        }
        if (tr.hits >= (tr.strong ? kConfirmHopsStrong : kConfirmHops)) {
            for (int s = 0; s < kMaxNotches; ++s) {
                if (!m_notches[s].used) {
                    Notch& n = m_notches[s];
                    n = Notch{};
                    n.used = true;
                    n.active = true;
                    setNotch(n, tr.freq);
                    tr.slot = s;
                    break;
                }
            }
        }
    }
}

void BeatCanceller::setNotch(Notch& n, double freq)
{
    if (n.freq != 0.0 && std::abs(n.freq - freq) < 0.5) {
        return;
    }
    n.freq = freq;
    const double q = std::clamp(freq / kNotchWidthHz, 3.0, 80.0);
    for (auto& channel : n.stages) {
        for (Biquad& stage : channel) {
            stage.setCoefficients(Biquad::Type::Notch, m_rate, freq, q);
        }
    }
}

float BeatCanceller::applyNotches(float x, int channel)
{
    for (Notch& n : m_notches) {
        if (!n.used) {
            continue;
        }
        const float wet = n.stages[channel][1].process(n.stages[channel][0].process(x));
        x += n.gain * (wet - x);
    }
    return x;
}

void BeatCanceller::advanceFades()
{
    for (Notch& n : m_notches) {
        if (!n.used) {
            continue;
        }
        if (n.active) {
            n.gain = std::min(1.0f, n.gain + m_fadeStep);
        } else {
            n.gain -= m_fadeStep;
            if (n.gain <= 0.0f) {
                n = Notch{};
            }
        }
    }
}

} // namespace AetherSDR
