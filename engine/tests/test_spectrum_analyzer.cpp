#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <vector>

#include "doctest/doctest.h"
#include "gitar/spectrum_analyzer.hpp"

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine(float hz, std::size_t samples, float amplitude = 0.5f) {
    std::vector<float> out(samples);
    const double step = 2.0 * kPi * static_cast<double>(hz) / static_cast<double>(kSampleRate);
    for (std::size_t i = 0; i < samples; ++i) {
        out[i] = amplitude * static_cast<float>(std::sin(step * static_cast<double>(i)));
    }
    return out;
}

using Bands = std::array<float, gitar::SpectrumAnalyzer::kBandCount>;

std::size_t argmax(const Bands& bands) {
    return static_cast<std::size_t>(
        std::distance(bands.begin(), std::max_element(bands.begin(), bands.end())));
}

// Mirrors the analyzer's log spacing so a test can assert which band a
// frequency belongs to without reaching into private state.
std::size_t band_for(float hz) {
    const double low = 40.0;
    const double high = 0.45 * static_cast<double>(kSampleRate);
    const double ratio = std::pow(high / low, 1.0 / gitar::SpectrumAnalyzer::kBandCount);
    const double index = std::log(static_cast<double>(hz) / low) / std::log(ratio);
    const int last = static_cast<int>(gitar::SpectrumAnalyzer::kBandCount) - 1;
    return static_cast<std::size_t>(std::clamp(static_cast<int>(std::floor(index)), 0, last));
}

Bands analyze(float hz, std::size_t samples) {
    gitar::SpectrumAnalyzer analyzer(kSampleRate);
    const std::vector<float> signal = sine(hz, samples);
    analyzer.process(signal.data(), signal.size());
    return analyzer.bands_db();
}

}  // namespace

TEST_CASE("a 440 Hz sine peaks in the band that contains 440 Hz") {
    const Bands bands = analyze(440.0f, 16384);
    CHECK(argmax(bands) == band_for(440.0f));
}

TEST_CASE("a 4 kHz sine peaks in a higher band than 440 Hz") {
    const Bands low = analyze(440.0f, 16384);
    const Bands high = analyze(4000.0f, 16384);
    CHECK(argmax(high) > argmax(low));
}

TEST_CASE("silence yields very low levels everywhere") {
    gitar::SpectrumAnalyzer analyzer(kSampleRate);
    const std::vector<float> silence(16384, 0.0f);
    analyzer.process(silence.data(), silence.size());
    const Bands bands = analyzer.bands_db();
    for (float db : bands) {
        CHECK(db <= -100.0f);
    }
}

TEST_CASE("published bands are finite and within a sane range") {
    const Bands bands = analyze(440.0f, 16384);
    for (float db : bands) {
        CHECK(std::isfinite(db));
        CHECK(db >= -120.0f);
        CHECK(db <= 0.0f);
    }
}

TEST_CASE("reset is safe and clears the published bands") {
    gitar::SpectrumAnalyzer analyzer(kSampleRate);
    const std::vector<float> signal = sine(440.0f, 16384);
    analyzer.process(signal.data(), signal.size());
    REQUIRE(argmax(analyzer.bands_db()) == band_for(440.0f));

    CHECK_NOTHROW(analyzer.reset());
    for (float db : analyzer.bands_db()) {
        CHECK(db == doctest::Approx(-120.0f));
    }
    CHECK_NOTHROW(analyzer.reset());
}

TEST_CASE("constructor clamps degenerate arguments") {
    gitar::SpectrumAnalyzer analyzer(0.0f, 0);
    const std::vector<float> silence(4096, 0.0f);
    analyzer.process(silence.data(), silence.size());
    for (float db : analyzer.bands_db()) {
        CHECK(std::isfinite(db));
        CHECK(db >= -120.0f);
        CHECK(db <= 0.0f);
    }
}
