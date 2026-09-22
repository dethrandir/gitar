#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace gitar {

// Real-time safe metronome. process() mixes a short decaying sine click into a
// mono buffer in place; the controls are atomics, so it never allocates or
// locks. The first beat after enabling is immediate.
class Metronome {
   public:
    explicit Metronome(float sample_rate, float bpm = 120.0f)
        : sample_rate_(sanitize_rate(sample_rate)),
          click_length_(static_cast<std::size_t>(kClickSeconds * sample_rate_)),
          click_decay_(std::exp(-1.0f / (kDecaySeconds * sample_rate_))),
          phase_step_(kTwoPi * static_cast<double>(kClickHz) / static_cast<double>(sample_rate_)) {
        if (click_length_ == 0) {
            click_length_ = 1;
        }
        set_bpm(bpm);
    }

    void set_enabled(bool enabled) {
        enabled_.store(enabled, std::memory_order_relaxed);
    }

    bool enabled() const {
        return enabled_.load(std::memory_order_relaxed);
    }

    void set_bpm(float bpm) {
        const float finite = std::isfinite(bpm) ? bpm : 120.0f;
        bpm_.store(std::clamp(finite, kMinBpm, kMaxBpm), std::memory_order_relaxed);
    }

    float bpm() const {
        return bpm_.load(std::memory_order_relaxed);
    }

    void process(float* data, std::size_t frames) {
        if (data == nullptr || frames == 0) {
            return;
        }
        if (!enabled_.load(std::memory_order_relaxed)) {
            active_ = false;
            return;
        }
        if (!active_) {
            start_click();
            samples_until_beat_ = interval_samples();
            active_ = true;
        }
        for (std::size_t i = 0; i < frames; ++i) {
            if (samples_until_beat_ == 0) {
                start_click();
                samples_until_beat_ = interval_samples();
            }
            if (click_remaining_ > 0) {
                data[i] += click_envelope_ * static_cast<float>(std::sin(phase_));
                click_envelope_ *= click_decay_;
                phase_ += phase_step_;
                if (phase_ >= kTwoPi) {
                    phase_ -= kTwoPi;
                }
                --click_remaining_;
            }
            --samples_until_beat_;
        }
    }

    void reset() {
        active_ = false;
        phase_ = 0.0;
        click_envelope_ = 0.0f;
        click_remaining_ = 0;
        samples_until_beat_ = 0;
    }

   private:
    static constexpr float kClickHz = 1000.0f;
    static constexpr float kClickSeconds = 0.03f;
    static constexpr float kDecaySeconds = 0.008f;
    static constexpr float kAmplitude = 0.5f;
    static constexpr float kMinBpm = 20.0f;
    static constexpr float kMaxBpm = 400.0f;
    static constexpr double kTwoPi = 6.283185307179586476925286766559;

    static float sanitize_rate(float sample_rate) {
        return (sample_rate > 0.0f && std::isfinite(sample_rate)) ? sample_rate : 48000.0f;
    }

    void start_click() {
        click_remaining_ = click_length_;
        click_envelope_ = kAmplitude;
        phase_ = 0.0;
    }

    std::size_t interval_samples() const {
        const float bpm = bpm_.load(std::memory_order_relaxed);
        const double seconds = 60.0 / static_cast<double>(bpm);
        const auto samples =
            static_cast<std::size_t>(std::lround(seconds * static_cast<double>(sample_rate_)));
        return samples > 0 ? samples : 1;
    }

    float sample_rate_;
    std::size_t click_length_;
    float click_decay_;
    double phase_step_;

    // Audio-thread-only state; controls are atomics.
    bool active_ = false;
    double phase_ = 0.0;
    float click_envelope_ = 0.0f;
    std::size_t click_remaining_ = 0;
    std::size_t samples_until_beat_ = 0;

    std::atomic<bool> enabled_{false};
    std::atomic<float> bpm_{120.0f};
};

}  // namespace gitar
