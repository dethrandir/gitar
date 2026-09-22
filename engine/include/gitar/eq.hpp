#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace gitar {

// Three-band EQ (low shelf ~120 Hz, mid peaking ~800 Hz, high shelf ~3.2 kHz)
// built from RBJ Audio EQ Cookbook biquads in direct form I.
//
// set_* runs on the control thread while process runs on the audio thread.
// Sharing a plain coefficient struct would be a data race, and neither a mutex
// nor a heap hand-off belongs on the audio thread. Each band therefore keeps
// its five coefficients as atomics behind a version counter (a seqlock): the
// writer marks the version odd, writes the coefficients, then marks it even;
// the reader snapshots the version, copies the coefficients and retries if the
// version moved. Knob changes are rare, so the reader almost always succeeds
// on the first attempt. process() neither allocates nor locks. A single writer
// is assumed; the engine serializes control calls under its mutex.
class ThreeBandEq {
   public:
    explicit ThreeBandEq(float sample_rate) : sample_rate_(valid_rate(sample_rate)) {
        set_low_gain_db(0.0f);
        set_mid_gain_db(0.0f);
        set_high_gain_db(0.0f);
    }

    void set_low_gain_db(float db) {
        const float clamped = clamp_gain(db);
        low_db_.store(clamped, std::memory_order_relaxed);
        // 0 dB is an exact identity, so emit identity coefficients: this skips
        // the band entirely and avoids the tiny round-off that near-cancelling
        // shelf poles would otherwise feed back through the direct-form-I state.
        low_.set(clamped == 0.0f ? Coefficients{} : low_shelf(clamped));
    }

    void set_mid_gain_db(float db) {
        const float clamped = clamp_gain(db);
        mid_db_.store(clamped, std::memory_order_relaxed);
        mid_.set(clamped == 0.0f ? Coefficients{} : peaking(clamped));
    }

    void set_high_gain_db(float db) {
        const float clamped = clamp_gain(db);
        high_db_.store(clamped, std::memory_order_relaxed);
        high_.set(clamped == 0.0f ? Coefficients{} : high_shelf(clamped));
    }

    float low_gain_db() const {
        return low_db_.load(std::memory_order_relaxed);
    }

    float mid_gain_db() const {
        return mid_db_.load(std::memory_order_relaxed);
    }

    float high_gain_db() const {
        return high_db_.load(std::memory_order_relaxed);
    }

    void process(float* data, std::size_t frames) {
        if (data == nullptr || frames == 0) {
            return;
        }
        const Coefficients low = low_.get();
        const Coefficients mid = mid_.get();
        const Coefficients high = high_.get();
        for (std::size_t i = 0; i < frames; ++i) {
            float sample = data[i];
            sample = low_.step(sample, low);
            sample = mid_.step(sample, mid);
            sample = high_.step(sample, high);
            data[i] = sample;
        }
    }

    void reset() {
        low_.reset();
        mid_.reset();
        high_.reset();
    }

   private:
    static constexpr float kLowFrequencyHz = 120.0f;
    static constexpr float kMidFrequencyHz = 800.0f;
    static constexpr float kMidQ = 0.9f;
    static constexpr float kHighFrequencyHz = 3200.0f;
    static constexpr float kMinGainDb = -24.0f;
    static constexpr float kMaxGainDb = 24.0f;
    static constexpr float kPi = 3.14159265358979323846f;

    struct Coefficients {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
    };

    class Band {
       public:
        void set(const Coefficients& c) {
            const unsigned version = version_.load(std::memory_order_relaxed);
            version_.store(version + 1u, std::memory_order_release);  // odd: update in progress
            b0_.store(c.b0, std::memory_order_relaxed);
            b1_.store(c.b1, std::memory_order_relaxed);
            b2_.store(c.b2, std::memory_order_relaxed);
            a1_.store(c.a1, std::memory_order_relaxed);
            a2_.store(c.a2, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            version_.store(version + 2u, std::memory_order_release);  // even: stable
        }

        Coefficients get() const {
            Coefficients c;
            for (;;) {
                const unsigned version = version_.load(std::memory_order_acquire);
                if ((version & 1u) != 0u) {
                    continue;  // a writer is mid-update; take a consistent snapshot
                }
                c.b0 = b0_.load(std::memory_order_relaxed);
                c.b1 = b1_.load(std::memory_order_relaxed);
                c.b2 = b2_.load(std::memory_order_relaxed);
                c.a1 = a1_.load(std::memory_order_relaxed);
                c.a2 = a2_.load(std::memory_order_relaxed);
                std::atomic_thread_fence(std::memory_order_acquire);
                if (version_.load(std::memory_order_relaxed) == version) {
                    return c;
                }
            }
        }

        float step(float x, const Coefficients& c) {
            const float y = c.b0 * x + c.b1 * x1_ + c.b2 * x2_ - c.a1 * y1_ - c.a2 * y2_;
            x2_ = x1_;
            x1_ = x;
            y2_ = y1_;
            y1_ = y;
            return y;
        }

        void reset() {
            x1_ = 0.0f;
            x2_ = 0.0f;
            y1_ = 0.0f;
            y2_ = 0.0f;
        }

       private:
        std::atomic<unsigned> version_{0};
        std::atomic<float> b0_{1.0f};
        std::atomic<float> b1_{0.0f};
        std::atomic<float> b2_{0.0f};
        std::atomic<float> a1_{0.0f};
        std::atomic<float> a2_{0.0f};
        float x1_ = 0.0f;
        float x2_ = 0.0f;
        float y1_ = 0.0f;
        float y2_ = 0.0f;
    };

    static float valid_rate(float sample_rate) {
        return (sample_rate > 0.0f && std::isfinite(sample_rate)) ? sample_rate : 48000.0f;
    }

    static float clamp_gain(float db) {
        const float finite = std::isfinite(db) ? db : 0.0f;
        return std::clamp(finite, kMinGainDb, kMaxGainDb);
    }

    static float amplitude(float gain_db) {
        return std::pow(10.0f, gain_db / 40.0f);
    }

    Coefficients low_shelf(float gain_db) const {
        const float a = amplitude(gain_db);
        const float w0 = 2.0f * kPi * kLowFrequencyHz / sample_rate_;
        const float cos_w0 = std::cos(w0);
        // S == 1, so the shelf slope term collapses to sqrt(2).
        const float alpha = std::sin(w0) * 0.5f * std::sqrt(2.0f);
        const float shelf = 2.0f * std::sqrt(a) * alpha;
        const float a0 = (a + 1.0f) + (a - 1.0f) * cos_w0 + shelf;

        Coefficients c;
        c.b0 = a * ((a + 1.0f) - (a - 1.0f) * cos_w0 + shelf) / a0;
        c.b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cos_w0) / a0;
        c.b2 = a * ((a + 1.0f) - (a - 1.0f) * cos_w0 - shelf) / a0;
        c.a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cos_w0) / a0;
        c.a2 = ((a + 1.0f) + (a - 1.0f) * cos_w0 - shelf) / a0;
        return c;
    }

    Coefficients high_shelf(float gain_db) const {
        const float a = amplitude(gain_db);
        const float w0 = 2.0f * kPi * kHighFrequencyHz / sample_rate_;
        const float cos_w0 = std::cos(w0);
        const float alpha = std::sin(w0) * 0.5f * std::sqrt(2.0f);
        const float shelf = 2.0f * std::sqrt(a) * alpha;
        const float a0 = (a + 1.0f) - (a - 1.0f) * cos_w0 + shelf;

        Coefficients c;
        c.b0 = a * ((a + 1.0f) + (a - 1.0f) * cos_w0 + shelf) / a0;
        c.b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cos_w0) / a0;
        c.b2 = a * ((a + 1.0f) + (a - 1.0f) * cos_w0 - shelf) / a0;
        c.a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cos_w0) / a0;
        c.a2 = ((a + 1.0f) - (a - 1.0f) * cos_w0 - shelf) / a0;
        return c;
    }

    Coefficients peaking(float gain_db) const {
        const float a = amplitude(gain_db);
        const float w0 = 2.0f * kPi * kMidFrequencyHz / sample_rate_;
        const float cos_w0 = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * kMidQ);
        const float a0 = 1.0f + alpha / a;

        Coefficients c;
        c.b0 = (1.0f + alpha * a) / a0;
        c.b1 = (-2.0f * cos_w0) / a0;
        c.b2 = (1.0f - alpha * a) / a0;
        c.a1 = (-2.0f * cos_w0) / a0;
        c.a2 = (1.0f - alpha / a) / a0;
        return c;
    }

    float sample_rate_;
    std::atomic<float> low_db_{0.0f};
    std::atomic<float> mid_db_{0.0f};
    std::atomic<float> high_db_{0.0f};
    Band low_;
    Band mid_;
    Band high_;
};

}  // namespace gitar
