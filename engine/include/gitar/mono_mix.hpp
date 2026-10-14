#pragma once

#include <cstddef>
#include <cstdint>

namespace gitar {

// Downmix interleaved frames to mono by averaging the channels. USB interfaces
// frequently record the instrument on a single channel (often the right one), so
// averaging keeps a signal present on any channel instead of silently dropping
// it, while a mono signal duplicated on both channels keeps its level.
inline void mix_to_mono(const float* interleaved, std::uint32_t channels, std::size_t frames,
                        float* mono) {
    if (channels <= 1) {
        for (std::size_t i = 0; i < frames; ++i) {
            mono[i] = interleaved[i];
        }
        return;
    }

    const float scale = 1.0f / static_cast<float>(channels);
    for (std::size_t i = 0; i < frames; ++i) {
        const float* const frame = interleaved + i * channels;
        float sum = 0.0f;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            sum += frame[channel];
        }
        mono[i] = sum * scale;
    }
}

}  // namespace gitar
