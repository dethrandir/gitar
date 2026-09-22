#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace gitar {

// Real-time safe downward noise gate. A peak envelope follower with a fast
// attack and a slower release decides whether the signal is above the
// threshold; the gate gain is then smoothed separately so opening and closing
// never step the waveform and click. All state is plain floats and the controls
// are atomics, so process() neither allocates nor locks.
class NoiseGate {
   public:
    explicit NoiseGate(float sample_rate, float threshold_db = -60.0f)
        : envelope_attack_(one_pole(0.001f, sample_rate)),
          envelope_release_(one_pole(0.050f, sample_rate)),
          gain_attack_(one_pole(0.005f, sample_rate)),
          gain_release_(one_pole(0.020f, sample_rate)) {
        set_threshold_db(threshold_db);
    }

    void set_enabled(bool enabled) {
        enabled_.store(enabled, std::memory_order_relaxed);
    }

    bool enabled() const {
        return enabled_.load(std::memory_order_relaxed);
    }

    void set_threshold_db(float db) {
        threshold_db_.store(std::clamp(db, kMinThresholdDb, kMaxThresholdDb),
                            std::memory_order_relaxed);
    }

    float threshold_db() const {
        return threshold_db_.load(std::memory_order_relaxed);
    }

    void process(float* data, std::size_t frames) {
        if (data == nullptr || frames == 0 || !enabled_.load(std::memory_order_relaxed)) {
            return;
        }
        const float threshold = db_to_amplitude(threshold_db_.load(std::memory_order_relaxed));
        for (std::size_t i = 0; i < frames; ++i) {
            const float sample = data[i];
            const float magnitude = std::fabs(sample);

            const float env_coeff = magnitude > envelope_ ? envelope_attack_ : envelope_release_;
            envelope_ = env_coeff * envelope_ + (1.0f - env_coeff) * magnitude;

            const float target = envelope_ > threshold ? 1.0f : kFloorGain;
            const float gain_coeff = target > gain_ ? gain_attack_ : gain_release_;
            gain_ = gain_coeff * gain_ + (1.0f - gain_coeff) * target;

            data[i] = sample * gain_;
        }
    }

    void reset() {
        envelope_ = 0.0f;
        gain_ = 0.0f;
    }

   private:
    static constexpr float kFloorGain = 1e-4f;
    static constexpr float kMinThresholdDb = -96.0f;
    static constexpr float kMaxThresholdDb = 0.0f;

    static float one_pole(float seconds, float sample_rate) {
        const float rate =
            (sample_rate > 0.0f && std::isfinite(sample_rate)) ? sample_rate : 48000.0f;
        return std::exp(-1.0f / (seconds * rate));
    }

    static float db_to_amplitude(float db) {
        return std::pow(10.0f, db / 20.0f);
    }

    float envelope_attack_;
    float envelope_release_;
    float gain_attack_;
    float gain_release_;
    float envelope_ = 0.0f;
    float gain_ = 0.0f;
    std::atomic<bool> enabled_{true};
    std::atomic<float> threshold_db_{-60.0f};
};

}  // namespace gitar
