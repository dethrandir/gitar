#include <array>
#include <cstddef>

#include "doctest/doctest.h"
#include "gitar/mono_mix.hpp"

TEST_CASE("mix_to_mono passes a mono signal through unchanged") {
    const std::array<float, 3> input{0.1f, -0.2f, 0.3f};
    std::array<float, 3> mono{};

    gitar::mix_to_mono(input.data(), 1, input.size(), mono.data());

    CHECK(mono[0] == doctest::Approx(0.1f));
    CHECK(mono[1] == doctest::Approx(-0.2f));
    CHECK(mono[2] == doctest::Approx(0.3f));
}

TEST_CASE("mix_to_mono keeps a signal duplicated on both channels at level") {
    const std::array<float, 4> input{0.5f, 0.5f, -0.4f, -0.4f};
    std::array<float, 2> mono{};

    gitar::mix_to_mono(input.data(), 2, 2, mono.data());

    CHECK(mono[0] == doctest::Approx(0.5f));
    CHECK(mono[1] == doctest::Approx(-0.4f));
}

TEST_CASE("mix_to_mono preserves a signal that is only on the right channel") {
    const std::array<float, 4> input{0.0f, 0.8f, 0.0f, -0.6f};
    std::array<float, 2> mono{};

    gitar::mix_to_mono(input.data(), 2, 2, mono.data());

    CHECK(mono[0] == doctest::Approx(0.4f));
    CHECK(mono[1] == doctest::Approx(-0.3f));
}

TEST_CASE("mix_to_mono averages across more than two channels") {
    const std::array<float, 4> input{1.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 1> mono{};

    gitar::mix_to_mono(input.data(), 4, 1, mono.data());

    CHECK(mono[0] == doctest::Approx(0.25f));
}
