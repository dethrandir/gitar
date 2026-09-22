#include <cstddef>
#include <vector>

#include "doctest/doctest.h"
#include "gitar/processor.hpp"

TEST_CASE("gain of 0.5 halves the amplitude") {
    gitar::GainProcessor processor(48000, 2);
    processor.set_gain(0.5f);
    std::vector<float> block(8, 1.0f);
    processor.process(block.data(), 4);
    for (float sample : block) {
        CHECK(sample == doctest::Approx(0.5f));
    }
}

TEST_CASE("gain of 2.0 doubles the amplitude") {
    gitar::GainProcessor processor(48000, 1);
    processor.set_gain(2.0f);
    std::vector<float> block(4, 0.25f);
    processor.process(block.data(), block.size());
    for (float sample : block) {
        CHECK(sample == doctest::Approx(0.5f));
    }
}

TEST_CASE("gain is clamped to the supported range") {
    gitar::GainProcessor processor(48000, 2);
    processor.set_gain(10.0f);
    CHECK(processor.gain() == doctest::Approx(4.0f));
    processor.set_gain(-3.0f);
    CHECK(processor.gain() == doctest::Approx(0.0f));
}

TEST_CASE("the meter reflects the post-gain level") {
    gitar::GainProcessor processor(48000, 1);
    processor.set_gain(0.5f);
    std::vector<float> block(256, 1.0f);
    processor.process(block.data(), block.size());
    CHECK(processor.meter().peak_db() == doctest::Approx(-6.02f).epsilon(0.01));
}

TEST_CASE("reset returns the meter to the floor") {
    gitar::GainProcessor processor(48000, 1);
    std::vector<float> block(128, 1.0f);
    processor.process(block.data(), block.size());
    CHECK(processor.meter().peak_db() > -1.0f);

    processor.reset();
    CHECK(processor.meter().peak_db() == doctest::Approx(-120.0f));
}
