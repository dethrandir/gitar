#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace gitar {

// Real-time safe monophonic pitch detector. The audio thread only writes into a
// preallocated ring buffer and never allocates, locks or throws; the published
// frequency and confidence are atomics so the control thread can read them.
class PitchDetector {
   public:
    explicit PitchDetector(float sample_rate, std::size_t window = 2048, float min_hz = 60.0f,
                           float max_hz = 1200.0f)
        : sample_rate_(sanitize_rate(sample_rate)),
          window_(sanitize_window(window)),
          min_hz_(sanitize_frequency(min_hz, 60.0f)),
          max_hz_(sanitize_frequency(max_hz, 1200.0f)),
          buffer_(window_, 0.0f),
          linear_(window_, 0.0f),
          correlation_(window_, 0.0f) {
        if (min_hz_ > max_hz_) {
            std::swap(min_hz_, max_hz_);
        }
        min_lag_ =
            std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(sample_rate_ / max_hz_)));
        max_lag_ = std::min<std::size_t>(
            window_ - 2, static_cast<std::size_t>(std::ceil(sample_rate_ / min_hz_)));
        if (min_lag_ > max_lag_) {
            min_lag_ = 1;
            max_lag_ = window_ - 2;
        }
    }

    void process(const float* samples, std::size_t count) {
        if (samples == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < count; ++i) {
            buffer_[write_index_] = samples[i];
            write_index_ = (write_index_ + 1) % window_;
            if (filled_ < window_) {
                ++filled_;
            }
            ++since_analysis_;
        }
        if (filled_ >= window_ && since_analysis_ >= window_ / 2) {
            analyze();
            since_analysis_ = 0;
        }
    }

    float pitch_hz() const {
        return pitch_hz_.load(std::memory_order_relaxed);
    }

    float confidence() const {
        return confidence_.load(std::memory_order_relaxed);
    }

    void reset() {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        std::fill(linear_.begin(), linear_.end(), 0.0f);
        std::fill(correlation_.begin(), correlation_.end(), 0.0f);
        write_index_ = 0;
        filled_ = 0;
        since_analysis_ = 0;
        publish(0.0f, 0.0f);
    }

   private:
    void analyze() {
        for (std::size_t i = 0; i < window_; ++i) {
            linear_[i] = buffer_[(write_index_ + i) % window_];
        }

        double energy = 0.0;
        for (std::size_t i = 0; i < window_; ++i) {
            energy += static_cast<double>(linear_[i]) * static_cast<double>(linear_[i]);
        }
        const double rms = std::sqrt(energy / static_cast<double>(window_));
        if (rms < kMinRms) {
            publish(0.0f, 0.0f);
            return;
        }

        const std::size_t low = min_lag_ - 1;
        const std::size_t high = std::min<std::size_t>(window_ - 1, max_lag_ + 1);
        for (std::size_t lag = low; lag <= high; ++lag) {
            correlation_[lag] = normalized_correlation(linear_.data(), lag);
        }

        float peak_value = -1.0f;
        for (std::size_t lag = min_lag_; lag <= max_lag_; ++lag) {
            peak_value = std::max(peak_value, correlation_[lag]);
        }
        if (peak_value < kMinCorrelation) {
            publish(0.0f, 0.0f);
            return;
        }

        // The autocorrelation of a periodic signal peaks once per period, so the
        // first local maximum above a fraction of the global peak avoids octave
        // errors. A tone below min_hz only decays across the lag range, so it has
        // no rise-then-fall peak and is rejected.
        const float threshold = kPeakRatio * peak_value;
        std::size_t peak = 0;
        bool found = false;
        for (std::size_t lag = min_lag_; lag <= max_lag_; ++lag) {
            const bool descending = lag >= max_lag_ || correlation_[lag] >= correlation_[lag + 1];
            if (correlation_[lag] > correlation_[lag - 1] && descending &&
                correlation_[lag] >= threshold) {
                peak = lag;
                found = true;
                break;
            }
        }
        if (!found) {
            publish(0.0f, 0.0f);
            return;
        }

        double refined = static_cast<double>(peak);
        const float left = correlation_[peak - 1];
        const float center = correlation_[peak];
        const float right = correlation_[peak + 1];
        const float curvature = left - 2.0f * center + right;
        if (std::fabs(curvature) > 1e-12f) {
            refined += 0.5 * static_cast<double>(left - right) / static_cast<double>(curvature);
        }
        if (!(refined > 0.0)) {
            publish(0.0f, 0.0f);
            return;
        }
        publish(static_cast<float>(static_cast<double>(sample_rate_) / refined),
                std::clamp(center, 0.0f, 1.0f));
    }

    float normalized_correlation(const float* samples, std::size_t lag) const {
        const std::size_t count = window_ - lag;
        double numerator = 0.0;
        double energy_left = 0.0;
        double energy_right = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            const double left = static_cast<double>(samples[i]);
            const double right = static_cast<double>(samples[i + lag]);
            numerator += left * right;
            energy_left += left * left;
            energy_right += right * right;
        }
        const double denominator = std::sqrt(energy_left * energy_right);
        if (denominator <= 1e-20) {
            return 0.0f;
        }
        return static_cast<float>(numerator / denominator);
    }

    void publish(float hz, float confidence) {
        pitch_hz_.store(hz, std::memory_order_relaxed);
        confidence_.store(confidence, std::memory_order_relaxed);
    }

    static float sanitize_rate(float sample_rate) {
        return (sample_rate > 0.0f && std::isfinite(sample_rate)) ? sample_rate : 48000.0f;
    }

    static std::size_t sanitize_window(std::size_t window) {
        return window >= kMinWindow ? window : kMinWindow;
    }

    static float sanitize_frequency(float hz, float fallback) {
        return (hz > 0.0f && std::isfinite(hz)) ? hz : fallback;
    }

    static constexpr float kMinRms = 1.0e-4f;
    static constexpr float kMinCorrelation = 0.5f;
    static constexpr float kPeakRatio = 0.85f;
    static constexpr std::size_t kMinWindow = 64;

    float sample_rate_;
    std::size_t window_;
    float min_hz_;
    float max_hz_;
    std::size_t min_lag_ = 1;
    std::size_t max_lag_ = 1;

    std::vector<float> buffer_;
    std::vector<float> linear_;
    std::vector<float> correlation_;
    std::size_t write_index_ = 0;
    std::size_t filled_ = 0;
    std::size_t since_analysis_ = 0;

    std::atomic<float> pitch_hz_{0.0f};
    std::atomic<float> confidence_{0.0f};
};

}  // namespace gitar
