#include <cmath>
#include <cstddef>
#include <vector>

#include "doctest/doctest.h"
#include "gitar/pitch_detector.hpp"

namespace {

constexpr float kSampleRate = 48000.0f;

std::vector<float> sine(float hz, std::size_t samples, float amplitude = 0.5f) {
    std::vector<float> out(samples);
    const double step =
        2.0 * 3.14159265358979323846 * static_cast<double>(hz) / static_cast<double>(kSampleRate);
    for (std::size_t i = 0; i < samples; ++i) {
        out[i] = amplitude * static_cast<float>(std::sin(step * static_cast<double>(i)));
    }
    return out;
}

float detect(float hz, std::size_t samples = 16384) {
    gitar::PitchDetector detector(kSampleRate);
    const std::vector<float> signal = sine(hz, samples);
    detector.process(signal.data(), signal.size());
    return detector.pitch_hz();
}

}  // namespace

TEST_CASE("a 440 Hz sine is detected within one hertz") {
    gitar::PitchDetector detector(kSampleRate);
    const std::vector<float> signal = sine(440.0f, 16384);
    detector.process(signal.data(), signal.size());
    CHECK(detector.pitch_hz() == doctest::Approx(440.0f).epsilon(1.0 / 440.0));
    CHECK(detector.confidence() > 0.5f);
}

TEST_CASE("guitar string frequencies are detected") {
    CHECK(detect(82.41f) == doctest::Approx(82.41f).epsilon(1.0 / 82.41));
    CHECK(detect(329.63f) == doctest::Approx(329.63f).epsilon(1.0 / 329.63));
}

TEST_CASE("silence publishes no pitch") {
    gitar::PitchDetector detector(kSampleRate);
    const std::vector<float> silence(16384, 0.0f);
    detector.process(silence.data(), silence.size());
    CHECK(detector.pitch_hz() == doctest::Approx(0.0f));
    CHECK(detector.confidence() == doctest::Approx(0.0f));
}

TEST_CASE("a tone below the minimum frequency is rejected") {
    CHECK(detect(10.0f) == doctest::Approx(0.0f));
}

TEST_CASE("reset clears the published pitch") {
    gitar::PitchDetector detector(kSampleRate);
    const std::vector<float> signal = sine(440.0f, 16384);
    detector.process(signal.data(), signal.size());
    REQUIRE(detector.pitch_hz() == doctest::Approx(440.0f).epsilon(1.0 / 440.0));

    detector.reset();
    CHECK(detector.pitch_hz() == doctest::Approx(0.0f));
    CHECK(detector.confidence() == doctest::Approx(0.0f));
}

TEST_CASE("constructor clamps degenerate arguments") {
    gitar::PitchDetector detector(0.0f, 0, -1.0f, -2.0f);
    const std::vector<float> silence(4096, 0.0f);
    detector.process(silence.data(), silence.size());
    CHECK(std::isfinite(detector.pitch_hz()));
    CHECK(std::isfinite(detector.confidence()));
}
