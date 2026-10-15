#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace AetherSDR::mobile {

// The desktop's noise-floor baseline (SpectrumWidget::updateNoiseFloorBaseline
// on main), Qt-free: asymmetric smoothing with candidate-state transient
// rejection. A shift of more than 4 dB is held as a candidate first; upward it
// must persist 16 frames and 1.2 s (a lightning crash does not lift the
// floor), downward 2 frames or 70 ms. The time constant follows the size of
// the change. reset() after a retune, zoom or band change re-acquires from
// the next frame.
class NoiseFloorTracker {
public:
    // One frame's floor estimate (estimateNoiseFloorDbm) at nowMs. Returns
    // whether the baseline moved this frame.
    bool observe(float frameFloor, std::int64_t nowMs)
    {
        if (!std::isfinite(frameFloor) || frameFloor <= -500.0f) {
            return false;
        }
        if (!m_valid || m_lastSampleMs <= 0) {
            m_baseline = frameFloor;
            m_valid = true;
            m_lastSampleMs = nowMs;
            m_candidateValid = false;
            m_candidateFrames = 0;
            return true;
        }
        const float delta = frameFloor - m_baseline;
        const float deltaAbs = std::abs(delta);
        constexpr float kTransientShiftDb = 4.0f;
        if (deltaAbs > kTransientShiftDb) {
            const bool sameCandidate = m_candidateValid
                && ((frameFloor - m_candidate) * delta >= 0.0f
                    || std::abs(frameFloor - m_candidate) < kTransientShiftDb);
            if (!sameCandidate) {
                m_candidateValid = true;
                m_candidate = frameFloor;
                m_candidateStartMs = nowMs;
                m_candidateFrames = 1;
                m_lastSampleMs = nowMs;
                return false;
            }
            m_candidate = 0.65f * m_candidate + 0.35f * frameFloor;
            ++m_candidateFrames;
            const std::int64_t ageMs = nowMs - m_candidateStartMs;
            const bool upward = delta > 0.0f;
            const bool keepWaiting = upward ? (m_candidateFrames < 16 || ageMs < 1200)
                                            : (m_candidateFrames < 2 && ageMs < 70);
            if (keepWaiting) {
                m_lastSampleMs = nowMs;
                return false;
            }
        } else {
            m_candidateValid = false;
            m_candidateFrames = 0;
        }
        const float elapsedSec = std::clamp(static_cast<float>(nowMs - m_lastSampleMs) / 1000.0f, 0.001f, 1.0f);
        float tauSec = 2.5f;
        if (deltaAbs > 20.0f) {
            tauSec = 0.08f;
        } else if (deltaAbs > 10.0f) {
            tauSec = 0.15f;
        } else if (deltaAbs > 5.0f) {
            tauSec = 0.35f;
        } else if (deltaAbs < 0.60f) {
            tauSec = 8.0f;
        }
        if (delta < -1.0f) {
            tauSec = std::min(tauSec, 0.12f);
        } else if (delta > 1.0f) {
            tauSec = std::max(tauSec, 1.2f);
        }
        const float alpha = 1.0f - std::exp(-elapsedSec / tauSec);
        m_baseline = (1.0f - alpha) * m_baseline + alpha * frameFloor;
        m_lastSampleMs = nowMs;
        if (deltaAbs > kTransientShiftDb) {
            m_candidateValid = false;
            m_candidateFrames = 0;
        }
        return true;
    }

    void reset()
    {
        m_valid = false;
        m_lastSampleMs = 0;
        m_candidateValid = false;
        m_candidateFrames = 0;
    }

    bool valid() const { return m_valid; }
    float baseline() const { return m_baseline; }

private:
    float m_baseline{-120.0f};
    bool m_valid{false};
    std::int64_t m_lastSampleMs{0};
    bool m_candidateValid{false};
    float m_candidate{0.0f};
    std::int64_t m_candidateStartMs{0};
    int m_candidateFrames{0};
};

// The desktop's reference-level easing (SpectrumWidget::moveRefLevelToward):
// up slowly, down quickly, nothing under 0.45 dB.
inline float easeLevelToward(float current, float target, float elapsedSec)
{
    const float delta = target - current;
    const float deltaAbs = std::abs(delta);
    if (deltaAbs < 0.45f) {
        return target;
    }
    elapsedSec = std::clamp(elapsedSec, 0.001f, 0.1f);
    float tauSec = 0.24f;
    if (deltaAbs > 30.0f) {
        tauSec = 0.12f;
    } else if (deltaAbs > 15.0f) {
        tauSec = 0.16f;
    }
    if (delta < 0.0f) {
        tauSec = deltaAbs > 30.0f ? 0.06f : (deltaAbs > 15.0f ? 0.08f : 0.10f);
    }
    const float next = current + delta * (1.0f - std::exp(-elapsedSec / tauSec));
    return std::abs(target - next) < 0.12f ? target : next;
}

} // namespace AetherSDR::mobile
