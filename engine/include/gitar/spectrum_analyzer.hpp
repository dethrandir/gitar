#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace gitar {

// Real-time safe log-band spectrum analyzer. The audio thread accumulates the
// most recent fft_size samples in a preallocated ring buffer and runs a
// self-contained radix-2 FFT at most once per fft_size/2 new samples; it never
// allocates, locks or throws. The control thread reads the smoothed band levels
// through a seqlock (a version counter around per-band atomics) so a read never
// observes a half-updated frame. A single writer is assumed: only process()
// publishes.
class SpectrumAnalyzer {
   public:
    static constexpr std::size_t kBandCount = 24;

    explicit SpectrumAnalyzer(float sample_rate, std::size_t fft_size = 1024)
        : sample_rate_(sanitize_rate(sample_rate)),
          fft_size_(sanitize_fft_size(fft_size)),
          ring_(fft_size_, 0.0f),
          window_(fft_size_, 0.0f),
          real_(fft_size_, 0.0f),
          imag_(fft_size_, 0.0f),
          band_edges_(compute_band_edges()) {
        float window_sum = 0.0f;
        for (std::size_t i = 0; i < fft_size_; ++i) {
            window_[i] = hann_window(i);
            window_sum += window_[i];
        }
        // 2/sum(window) undoes the Hann coherent gain, so a full-scale sine
        // reads near 0 dB in its peak band.
        amplitude_scale_ = window_sum > 0.0f ? 2.0f / window_sum : 0.0f;

        const float frame_seconds = static_cast<float>(fft_size_ / 2) / sample_rate_;
        attack_coeff_ = one_pole(frame_seconds, kAttackSeconds);
        release_coeff_ = one_pole(frame_seconds, kReleaseSeconds);
        smoothed_.fill(kFloorDb);
        publish(smoothed_);
    }

    void process(const float* samples, std::size_t count) {
        if (samples == nullptr) {
            return;
        }
        const std::size_t mask = fft_size_ - 1;
        const std::size_t stride = fft_size_ / 2;
        for (std::size_t i = 0; i < count; ++i) {
            ring_[write_index_] = samples[i];
            write_index_ = (write_index_ + 1) & mask;
            if (filled_ < fft_size_) {
                ++filled_;
            }
            ++since_analysis_;
            if (filled_ >= fft_size_ && since_analysis_ >= stride) {
                analyze();
                since_analysis_ = 0;
            }
        }
    }

    std::array<float, kBandCount> bands_db() const {
        std::array<float, kBandCount> out{};
        for (;;) {
            const unsigned version = version_.load(std::memory_order_acquire);
            if ((version & 1u) != 0u) {
                continue;  // a writer is mid-update; take a consistent snapshot
            }
            for (std::size_t i = 0; i < kBandCount; ++i) {
                out[i] = bands_[i].load(std::memory_order_relaxed);
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            if (version_.load(std::memory_order_relaxed) == version) {
                return out;
            }
        }
    }

    void reset() {
        std::fill(ring_.begin(), ring_.end(), 0.0f);
        std::fill(real_.begin(), real_.end(), 0.0f);
        std::fill(imag_.begin(), imag_.end(), 0.0f);
        smoothed_.fill(kFloorDb);
        write_index_ = 0;
        filled_ = 0;
        since_analysis_ = 0;
        publish(smoothed_);
    }

   private:
    void analyze() {
        const std::size_t mask = fft_size_ - 1;
        for (std::size_t i = 0; i < fft_size_; ++i) {
            real_[i] = ring_[(write_index_ + i) & mask] * window_[i];
            imag_[i] = 0.0f;
        }
        fft();

        const std::size_t max_bin = fft_size_ / 2;
        std::array<float, kBandCount> frame{};
        for (std::size_t band = 0; band < kBandCount; ++band) {
            const std::size_t begin = band_edges_[band];
            // Natural log edges can be narrower than one bin at the low end, so
            // force at least one bin per band rather than leaving it empty.
            const std::size_t end =
                std::min(std::max(band_edges_[band + 1], begin + 1), max_bin + 1);
            float sum = 0.0f;
            std::size_t bins = 0;
            for (std::size_t bin = begin; bin < end; ++bin) {
                const float re = real_[bin];
                const float im = imag_[bin];
                const float amplitude = amplitude_scale_ * std::sqrt(re * re + im * im);
                sum += amplitude * amplitude;
                ++bins;
            }
            const float power = bins > 0 ? sum / static_cast<float>(bins) : 0.0f;
            const float db =
                std::clamp(10.0f * std::log10(power + kPowerFloor), kFloorDb, kCeilingDb);
            const float previous = smoothed_[band];
            // Fast attack keeps transients visible; slower release makes the
            // display readable instead of flickering.
            const float coeff = db > previous ? attack_coeff_ : release_coeff_;
            frame[band] = previous + coeff * (db - previous);
        }
        smoothed_ = frame;
        publish(frame);
    }

    // In-place iterative radix-2 Cooley-Tukey FFT. The input is real, so only
    // bins [0, fft_size/2] are meaningful to the caller.
    void fft() {
        const std::size_t n = fft_size_;
        for (std::size_t i = 1, j = 0; i < n; ++i) {
            std::size_t bit = n >> 1;
            for (; (j & bit) != 0u; bit >>= 1) {
                j ^= bit;
            }
            j ^= bit;
            if (i < j) {
                std::swap(real_[i], real_[j]);
                std::swap(imag_[i], imag_[j]);
            }
        }
        for (std::size_t len = 2; len <= n; len <<= 1) {
            const double angle = -2.0 * kPi / static_cast<double>(len);
            const double w_re = std::cos(angle);
            const double w_im = std::sin(angle);
            const std::size_t half = len >> 1;
            for (std::size_t i = 0; i < n; i += len) {
                double rot_re = 1.0;
                double rot_im = 0.0;
                for (std::size_t j = 0; j < half; ++j) {
                    const std::size_t even = i + j;
                    const std::size_t odd = even + half;
                    const double o_re = real_[odd];
                    const double o_im = imag_[odd];
                    const double t_re = o_re * rot_re - o_im * rot_im;
                    const double t_im = o_re * rot_im + o_im * rot_re;
                    const double e_re = real_[even];
                    const double e_im = imag_[even];
                    real_[even] = static_cast<float>(e_re + t_re);
                    imag_[even] = static_cast<float>(e_im + t_im);
                    real_[odd] = static_cast<float>(e_re - t_re);
                    imag_[odd] = static_cast<float>(e_im - t_im);
                    const double next_re = rot_re * w_re - rot_im * w_im;
                    rot_im = rot_re * w_im + rot_im * w_re;
                    rot_re = next_re;
                }
            }
        }
    }

    void publish(const std::array<float, kBandCount>& values) {
        const unsigned version = version_.load(std::memory_order_relaxed);
        version_.store(version + 1u, std::memory_order_release);  // odd: update in progress
        for (std::size_t i = 0; i < kBandCount; ++i) {
            bands_[i].store(values[i], std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_release);
        version_.store(version + 2u, std::memory_order_release);  // even: stable
    }

    float hann_window(std::size_t index) const {
        return static_cast<float>(0.5 * (1.0 - std::cos(kTwoPi * static_cast<double>(index) /
                                                        static_cast<double>(fft_size_))));
    }

    std::array<std::size_t, kBandCount + 1> compute_band_edges() const {
        std::array<std::size_t, kBandCount + 1> edges{};
        const double sample_rate = static_cast<double>(sample_rate_);
        const double high = std::min(kMaxFrequencyFraction * sample_rate, 0.5 * sample_rate);
        const double ratio =
            std::pow(high / kMinFrequencyHz, 1.0 / static_cast<double>(kBandCount));
        const std::size_t max_bin = fft_size_ / 2;
        const long max_bin_long = static_cast<long>(max_bin);
        for (std::size_t i = 0; i <= kBandCount; ++i) {
            const double hz = kMinFrequencyHz * std::pow(ratio, static_cast<double>(i));
            const long bin = std::lround(hz * static_cast<double>(fft_size_) / sample_rate);
            edges[i] = static_cast<std::size_t>(std::clamp(bin, 1L, max_bin_long));
        }
        // Log spacing is monotonic, so rounding yields non-decreasing edges. The
        // lowest bands can land on the same bin; analyze() widens those to one
        // bin so no band is ever empty.
        return edges;
    }

    static float sanitize_rate(float sample_rate) {
        return (sample_rate > 0.0f && std::isfinite(sample_rate)) ? sample_rate : 48000.0f;
    }

    static std::size_t sanitize_fft_size(std::size_t fft_size) {
        std::size_t power = kMinFftSize;
        while (power < fft_size && power < kMaxFftSize) {
            power <<= 1;
        }
        return power;
    }

    static float one_pole(float frame_seconds, float tau_seconds) {
        return 1.0f - std::exp(-frame_seconds / tau_seconds);
    }

    static constexpr float kFloorDb = -120.0f;
    static constexpr float kCeilingDb = 0.0f;
    static constexpr float kPowerFloor = 1.0e-20f;
    static constexpr float kMinFrequencyHz = 40.0f;
    static constexpr float kMaxFrequencyFraction = 0.45f;
    static constexpr float kAttackSeconds = 0.05f;
    static constexpr float kReleaseSeconds = 0.3f;
    static constexpr std::size_t kMinFftSize = 64;
    static constexpr std::size_t kMaxFftSize = 16384;
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kTwoPi = 6.28318530717958647692;

    float sample_rate_;
    std::size_t fft_size_;
    std::size_t write_index_ = 0;
    std::size_t filled_ = 0;
    std::size_t since_analysis_ = 0;
    float amplitude_scale_ = 0.0f;
    float attack_coeff_ = 1.0f;
    float release_coeff_ = 1.0f;

    std::vector<float> ring_;
    std::vector<float> window_;
    std::vector<float> real_;
    std::vector<float> imag_;
    std::array<std::size_t, kBandCount + 1> band_edges_{};
    std::array<float, kBandCount> smoothed_{};

    std::atomic<unsigned> version_{0};
    std::array<std::atomic<float>, kBandCount> bands_{};
};

}  // namespace gitar
