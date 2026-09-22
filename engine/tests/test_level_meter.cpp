#include <cmath>
#include <cstddef>
#include <vector>

#include "doctest/doctest.h"
#include "gitar/level_meter.hpp"

TEST_CASE("amplitude_to_db maps amplitudes to decibels with a floor") {
    CHECK(gitar::LevelMeter::amplitude_to_db(1.0f) == doctest::Approx(0.0f));
    CHECK(gitar::LevelMeter::amplitude_to_db(0.5f) == doctest::Approx(-6.02f).epsilon(0.01));
    CHECK(gitar::LevelMeter::amplitude_to_db(0.0f) == doctest::Approx(-120.0f));
}

TEST_CASE("a full-scale block raises the peak to roughly zero dB") {
    gitar::LevelMeter meter(48000.0f, 0.3f, 0.3f);
    std::vector<float> block(256, 1.0f);
    meter.process(block.data(), block.size());
    CHECK(meter.peak_db() == doctest::Approx(0.0f).epsilon(0.001));
}

TEST_CASE("peak decays monotonically through silence") {
    gitar::LevelMeter meter(48000.0f, 0.1f, 0.1f);
    std::vector<float> loud(64, 1.0f);
    meter.process(loud.data(), loud.size());
    const float start = meter.peak_db();

    std::vector<float> silence(64, 0.0f);
    float previous = start;
    for (int i = 0; i < 20; ++i) {
        meter.process(silence.data(), silence.size());
        const float current = meter.peak_db();
        CHECK(current <= previous);
        previous = current;
    }
    CHECK(previous < start);
}

TEST_CASE("rms of a constant half-scale signal settles near minus six dB") {
    constexpr float kSampleRate = 1000.0f;
    gitar::LevelMeter meter(kSampleRate, 0.3f, 0.1f);
    std::vector<float> block(1000, 0.5f);
    for (int i = 0; i < 50; ++i) {
        meter.process(block.data(), block.size());
    }
    CHECK(meter.rms_db() == doctest::Approx(-6.02f).epsilon(0.02));
}

TEST_CASE("reset returns both readings to the floor") {
    gitar::LevelMeter meter(48000.0f);
    std::vector<float> block(128, 1.0f);
    meter.process(block.data(), block.size());
    CHECK(meter.peak_db() > -1.0f);

    meter.reset();
    CHECK(meter.peak_db() == doctest::Approx(-120.0f));
    CHECK(meter.rms_db() == doctest::Approx(-120.0f));
}

TEST_CASE("constructor clamps degenerate arguments") {
    gitar::LevelMeter meter(0.0f, -1.0f, 0.0f);
    std::vector<float> block(16, 1.0f);
    meter.process(block.data(), block.size());
    CHECK(std::isfinite(meter.peak_db()));
    CHECK(std::isfinite(meter.rms_db()));
}
