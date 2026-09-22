#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "gitar/level_meter.hpp"

namespace gitar {

// Real-time safe in-place gain with post-gain metering. The gain is atomic so
// the control thread can adjust it while the audio thread is running.
class GainProcessor {
   public:
    GainProcessor(std::uint32_t sample_rate, std::uint32_t channels)
        : channels_(channels == 0 ? 1 : channels), meter_(static_cast<float>(sample_rate)) {}

    void set_gain(float gain) {
        gain_.store(std::clamp(gain, 0.0f, 4.0f), std::memory_order_relaxed);
    }

    float gain() const {
        return gain_.load(std::memory_order_relaxed);
    }

    void process(float* interleaved, std::size_t frames) {
        const float gain = gain_.load(std::memory_order_relaxed);
        const std::size_t samples = frames * channels_;
        for (std::size_t i = 0; i < samples; ++i) {
            interleaved[i] *= gain;
        }
        meter_.process(interleaved, samples);
    }

    const LevelMeter& meter() const {
        return meter_;
    }

    void reset() {
        meter_.reset();
    }

   private:
    std::uint32_t channels_;
    std::atomic<float> gain_{1.0f};
    LevelMeter meter_;
};

}  // namespace gitar
