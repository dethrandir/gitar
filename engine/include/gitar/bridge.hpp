#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "gitar/ring_buffer.hpp"

namespace gitar {

// Bridges interleaved f32 audio between a producer (capture callback) and a
// consumer (playback callback). The devices may run on unrelated clocks, so
// whole frames are dropped or inserted at the edges instead of letting the
// buffer overflow and corrupt channel alignment.
class InterleavedBridge {
   public:
    explicit InterleavedBridge(std::uint32_t channels) : channels_(channels == 0 ? 1 : channels) {}

    std::size_t write(const float* interleaved, std::size_t frames) {
        if (frames == 0) {
            return 0;
        }
        const std::size_t free_frames = buffer_.capacity() / channels_ - buffer_.size() / channels_;
        const std::size_t accepted = frames < free_frames ? frames : free_frames;
        for (std::size_t i = 0; i < accepted * channels_; ++i) {
            buffer_.push(interleaved[i]);
        }
        if (accepted < frames) {
            overrun_frames_.fetch_add(frames - accepted, std::memory_order_relaxed);
        }
        return accepted;
    }

    std::size_t read(float* interleaved, std::size_t frames) {
        if (frames == 0) {
            return 0;
        }
        const std::size_t available_frames = buffer_.size() / channels_;
        const std::size_t read_frames = frames < available_frames ? frames : available_frames;
        for (std::size_t i = 0; i < read_frames * channels_; ++i) {
            float sample = 0.0f;
            buffer_.pop(sample);
            interleaved[i] = sample;
        }
        const std::size_t missing_frames = frames - read_frames;
        if (missing_frames > 0) {
            for (std::size_t i = read_frames * channels_; i < frames * channels_; ++i) {
                interleaved[i] = 0.0f;
            }
            underrun_frames_.fetch_add(missing_frames, std::memory_order_relaxed);
        }
        return read_frames;
    }

    std::uint64_t overrun_frames() const {
        return overrun_frames_.load(std::memory_order_relaxed);
    }

    std::uint64_t underrun_frames() const {
        return underrun_frames_.load(std::memory_order_relaxed);
    }

    void reset() {
        overrun_frames_.store(0, std::memory_order_relaxed);
        underrun_frames_.store(0, std::memory_order_relaxed);
    }

   private:
    std::uint32_t channels_;
    SpscRingBuffer<float, 65536> buffer_;
    std::atomic<std::uint64_t> overrun_frames_{0};
    std::atomic<std::uint64_t> underrun_frames_{0};
};

}  // namespace gitar
