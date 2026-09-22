#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "doctest/doctest.h"
#include "gitar/eq.hpp"

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr float kPi = 3.14159265358979323846f;

std::vector<float> tone(float amplitude, float frequency, std::size_t frames) {
    std::vector<float> samples(frames, 0.0f);
    const float step = 2.0f * kPi * frequency / kSampleRate;
    for (std::size_t i = 0; i < frames; ++i) {
        samples[i] = amplitude * std::sin(step * static_cast<float>(i));
    }
    return samples;
}

float rms(const std::vector<float>& samples, std::size_t begin) {
    double sum = 0.0;
    for (std::size_t i = begin; i < samples.size(); ++i) {
        sum += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    }
    const std::size_t count = samples.size() > begin ? samples.size() - begin : 0;
    return count == 0 ? 0.0f : static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

// Level ratio of a processed tone measured after the filter transient settles.
float gain_ratio(gitar::ThreeBandEq& eq, float frequency) {
    std::vector<float> samples = tone(0.25f, frequency, 48000);
    const float before = rms(samples, 4800);
    eq.process(samples.data(), samples.size());
    return rms(samples, 4800) / before;
}

}  // namespace

TEST_CASE("all bands at zero dB are unity gain") {
    gitar::ThreeBandEq eq(kSampleRate);
    std::vector<float> samples = tone(0.5f, 1000.0f, 4096);
    const std::vector<float> original = samples;
    eq.process(samples.data(), samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        CHECK(samples[i] == doctest::Approx(original[i]).epsilon(1e-5));
    }
}

TEST_CASE("a low-shelf boost raises lows more than highs") {
    gitar::ThreeBandEq eq(kSampleRate);
    eq.set_low_gain_db(12.0f);

    const float low = gain_ratio(eq, 100.0f);
    gitar::ThreeBandEq reference(kSampleRate);
    const float high = gain_ratio(reference, 8000.0f);

    CHECK(low > 1.5f);
    CHECK(high == doctest::Approx(1.0f).epsilon(1e-2));
    CHECK(low > high);
}

TEST_CASE("a high-shelf boost raises highs more than lows") {
    gitar::ThreeBandEq eq(kSampleRate);
    eq.set_high_gain_db(12.0f);

    const float high = gain_ratio(eq, 10000.0f);
    gitar::ThreeBandEq reference(kSampleRate);
    const float low = gain_ratio(reference, 100.0f);

    CHECK(high > 1.5f);
    CHECK(low == doctest::Approx(1.0f).epsilon(1e-2));
    CHECK(high > low);
}

TEST_CASE("a mid boost raises a tone near the mid frequency") {
    gitar::ThreeBandEq eq(kSampleRate);
    eq.set_mid_gain_db(12.0f);
    const float mid = gain_ratio(eq, 800.0f);
    CHECK(mid > 1.5f);
}

TEST_CASE("a mid cut lowers a tone near the mid frequency") {
    gitar::ThreeBandEq eq(kSampleRate);
    eq.set_mid_gain_db(-12.0f);
    const float mid = gain_ratio(eq, 800.0f);
    CHECK(mid < 0.7f);
}

TEST_CASE("gains are clamped to plus or minus 24 dB") {
    gitar::ThreeBandEq eq(kSampleRate);
    eq.set_low_gain_db(100.0f);
    CHECK(eq.low_gain_db() == doctest::Approx(24.0f));
    eq.set_mid_gain_db(-100.0f);
    CHECK(eq.mid_gain_db() == doctest::Approx(-24.0f));
    eq.set_high_gain_db(24.0f);
    CHECK(eq.high_gain_db() == doctest::Approx(24.0f));
}

TEST_CASE("reset is safe before and after processing") {
    gitar::ThreeBandEq eq(kSampleRate);
    CHECK_NOTHROW(eq.reset());

    std::vector<float> samples = tone(0.5f, 1000.0f, 512);
    eq.process(samples.data(), samples.size());
    CHECK_NOTHROW(eq.reset());

    std::vector<float> silence(64, 0.0f);
    eq.process(silence.data(), silence.size());
    for (float sample : silence) {
        CHECK(sample == doctest::Approx(0.0f));
    }
}
