#pragma once

#include "Biquad.h"

#include <array>
#include <complex>
#include <vector>

namespace AetherSDR {

// Beat cancel (Kenwood "BC"): finds steady carriers in receive audio — the
// whistle of someone tuning up on the frequency — and notches them out.
//
// A carrier is a narrow spectral peak, well above the spectrum's median, that
// holds its frequency within a few Hz for ~130 ms (~65 ms when very strong).
// Voice harmonics glide with the pitch and are left alone. Each confirmed
// carrier gets a two-stage biquad notch (~40 Hz wide) that fades in over
// 15 ms and fades out once the carrier has been gone for ~300 ms. Detection
// runs on the input, before the notches, so a notched carrier stays tracked.
//
// Audio thread only; no allocation after setSampleRate(). No latency (IIR).
class BeatCanceller {
public:
    static constexpr int kMaxNotches = 4;

    BeatCanceller();

    // Sizes the analysis to the rate (~43 ms frames) and resets.
    void setSampleRate(int hz);
    int sampleRate() const { return m_rate; }
    void reset();

    // Interleaved stereo float, in place. Both channels get the same notches,
    // detected on their mid signal.
    void processStereo(float* lr, int frames);
    // Mono float, in place.
    void processMono(float* x, int frames);

    // Carriers currently being notched (fading ones included).
    int activeNotches() const;
    // Frequency of notch slot i in Hz, 0 when the slot is free.
    double notchHz(int i) const;

private:
    struct Track {
        double freq{0.0};
        double minFreq{0.0};
        double maxFreq{0.0};
        int hits{0};
        int misses{0};
        bool strong{false};
        int slot{-1};   // notch slot while confirmed
        bool used{false};
    };
    struct Notch {
        Biquad stages[2][2];   // [channel][stage]
        double freq{0.0};
        float gain{0.0f};      // 0 = dry, 1 = fully notched
        bool active{false};    // target of the fade
        bool used{false};
    };
    struct Peak {
        double freq;
        bool strong;
    };

    void pushAnalysis(float mid);
    void analyse();
    void setNotch(Notch& n, double freq);
    float applyNotches(float x, int channel);
    void advanceFades();

    int m_rate{24000};
    int m_size{1024};        // FFT size
    int m_hop{512};
    std::vector<float> m_window;
    std::vector<float> m_fifo;      // last m_size mid samples (ring)
    int m_fifoPos{0};
    int m_sinceHop{0};
    std::vector<std::complex<float>> m_fft;
    std::vector<float> m_power;
    std::vector<float> m_sorted;
    std::vector<Peak> m_peaks;
    std::array<Track, 8> m_tracks{};
    std::array<Notch, kMaxNotches> m_notches{};
    float m_fadeStep{0.003f};
};

} // namespace AetherSDR
