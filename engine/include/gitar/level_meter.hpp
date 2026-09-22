#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace gitar {

// Real-time safe peak/RMS meter. No allocation, no locks, no exceptions.
class LevelMeter {
   public:
    explicit LevelMeter(float sample_rate, float release_seconds = 0.3f,
                        float rms_window_seconds = 0.3f)
        : sample_rate_(sanitize(sample_rate, 48000.0f)),
          release_coeff_(release_coefficient(sanitize(release_seconds, 0.3f), sample_rate_)),
          rms_coeff_(one_pole_coefficient(sanitize(rms_window_seconds, 0.3f), sample_rate_)) {}

    void process(const float* samples, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            const float sample = samples[i];
            peak_ = std::max(std::fabs(sample), peak_ * release_coeff_);
            mean_square_ += rms_coeff_ * (sample * sample - mean_square_);
        }
    }

    void reset() {
        peak_ = 0.0f;
        mean_square_ = 0.0f;
    }

    float peak_db() const {
        return amplitude_to_db(peak_);
    }

    float rms_db() const {
        return amplitude_to_db(std::sqrt(mean_square_));
    }

    static float amplitude_to_db(float amplitude) {
        constexpr float kFloorDb = -120.0f;
        constexpr float kMinAmplitude = 1e-12f;
        return std::max(20.0f * std::log10(std::max(amplitude, kMinAmplitude)), kFloorDb);
    }

   private:
    static float sanitize(float value, float fallback) {
        return (value > 0.0f && std::isfinite(value)) ? value : fallback;
    }

    static float release_coefficient(float seconds, float sample_rate) {
        return std::exp(-1.0f / (seconds * sample_rate));
    }

    static float one_pole_coefficient(float seconds, float sample_rate) {
        return 1.0f - std::exp(-1.0f / (seconds * sample_rate));
    }

    float sample_rate_;
    float release_coeff_;
    float rms_coeff_;
    float peak_ = 0.0f;
    float mean_square_ = 0.0f;
};

}  // namespace gitar
