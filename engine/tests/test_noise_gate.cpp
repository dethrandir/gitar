#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "doctest/doctest.h"
#include "gitar/noise_gate.hpp"

namespace {

constexpr float kSampleRate = 48000.0f;

std::vector<float> tone(float amplitude, float frequency, std::size_t frames) {
    std::vector<float> samples(frames, 0.0f);
    const float step = 2.0f * 3.14159265358979323846f * frequency / kSampleRate;
    for (std::size_t i = 0; i < frames; ++i) {
        samples[i] = amplitude * std::sin(step * static_cast<float>(i));
    }
    return samples;
}

float peak(const std::vector<float>& samples, std::size_t begin) {
    float result = 0.0f;
    for (std::size_t i = begin; i < samples.size(); ++i) {
        result = std::max(result, std::fabs(samples[i]));
    }
    return result;
}

}  // namespace

TEST_CASE("silence stays silent") {
    gitar::NoiseGate gate(kSampleRate);
    std::vector<float> samples(4096, 0.0f);
    gate.process(samples.data(), samples.size());
    for (float sample : samples) {
        CHECK(sample == doctest::Approx(0.0f));
    }
}

TEST_CASE("a loud tone passes through once the attack settles") {
    gitar::NoiseGate gate(kSampleRate);
    std::vector<float> samples = tone(0.5f, 1000.0f, 48000);
    const std::vector<float> original = samples;
    gate.process(samples.data(), samples.size());

    const std::size_t settle = 4800;
    for (std::size_t i = settle; i < samples.size(); ++i) {
        CHECK(samples[i] == doctest::Approx(original[i]).epsilon(1e-3));
    }
}

TEST_CASE("a quiet tone below the threshold is strongly attenuated") {
    gitar::NoiseGate gate(kSampleRate);
    std::vector<float> samples = tone(0.0005f, 1000.0f, 48000);
    gate.process(samples.data(), samples.size());
    CHECK(peak(samples, 0) < 1e-6f);
}

TEST_CASE("disabling the gate passes a quiet tone through unchanged") {
    gitar::NoiseGate gate(kSampleRate);
    gate.set_enabled(false);
    CHECK_FALSE(gate.enabled());

    std::vector<float> samples = tone(0.0005f, 1000.0f, 4096);
    const std::vector<float> original = samples;
    gate.process(samples.data(), samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        CHECK(samples[i] == original[i]);
    }
}

TEST_CASE("the threshold is clamped to the supported range") {
    gitar::NoiseGate gate(kSampleRate);
    gate.set_threshold_db(-200.0f);
    CHECK(gate.threshold_db() == doctest::Approx(-96.0f));
    gate.set_threshold_db(10.0f);
    CHECK(gate.threshold_db() == doctest::Approx(0.0f));
    gate.set_threshold_db(-40.0f);
    CHECK(gate.threshold_db() == doctest::Approx(-40.0f));
}

TEST_CASE("reset is safe before and after processing") {
    gitar::NoiseGate gate(kSampleRate);
    gate.reset();

    std::vector<float> samples = tone(0.5f, 1000.0f, 512);
    gate.process(samples.data(), samples.size());
    CHECK_NOTHROW(gate.reset());

    std::vector<float> silence(64, 0.0f);
    gate.process(silence.data(), silence.size());
    for (float sample : silence) {
        CHECK(sample == doctest::Approx(0.0f));
    }
}
