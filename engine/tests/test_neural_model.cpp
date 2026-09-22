#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "doctest/doctest.h"
#include "gitar/neural_model.hpp"

namespace {

std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(GITAR_TEST_FIXTURES_DIR) / name;
}

void check_passthrough(const std::vector<float>& input) {
    std::vector<float> output(input.size(), 0.0f);
    gitar::NeuralModel model;
    REQUIRE(model.load(fixture("identity.nam")));
    model.process(input.data(), output.data(), static_cast<int>(input.size()));
    for (std::size_t i = 0; i < input.size(); ++i) {
        CHECK(output[i] == doctest::Approx(input[i]));
    }
}

}  // namespace

TEST_CASE("loads a .nam model and reports its shape") {
    gitar::NeuralModel model;
    std::string error;
    REQUIRE(model.load(fixture("identity.nam"), &error));
    CHECK(error.empty());
    CHECK(model.loaded());
    CHECK(model.input_channels() == 1);
    CHECK(model.output_channels() == 1);
    CHECK(model.expected_sample_rate() == doctest::Approx(48000.0));
}

TEST_CASE("an identity model passes an impulse through unchanged") {
    check_passthrough({1.0f, 0.0f, 0.0f, 0.0f});
}

TEST_CASE("a WAV impulse response passes an impulse through unchanged") {
    gitar::NeuralModel model;
    std::string error;
    REQUIRE(model.load(fixture("impulse.wav"), &error));
    CHECK(error.empty());
    CHECK(model.loaded());

    const std::vector<float> input = {1.0f, 0.25f, -0.5f, 0.0f, 0.75f};
    std::vector<float> output(input.size(), 0.0f);
    model.process(input.data(), output.data(), static_cast<int>(input.size()));
    // The fixture is 16-bit PCM, so its unit impulse is 32767/32768, not exactly
    // 1.0; the output tracks the input to within that quantisation.
    for (std::size_t i = 0; i < input.size(); ++i) {
        CHECK(output[i] == doctest::Approx(input[i]).epsilon(1e-4));
    }
}

TEST_CASE("loading a missing model fails with an error") {
    gitar::NeuralModel model;
    std::string error;
    CHECK_FALSE(model.load(fixture("does_not_exist.nam"), &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(model.loaded());
}

TEST_CASE("a moved-into model still processes") {
    gitar::NeuralModel source;
    REQUIRE(source.load(fixture("identity.nam")));

    gitar::NeuralModel model(std::move(source));
    CHECK(model.loaded());

    const std::vector<float> input = {1.0f, 0.5f, -0.5f, 0.0f};
    std::vector<float> output(input.size(), 0.0f);
    model.process(input.data(), output.data(), static_cast<int>(input.size()));
    for (std::size_t i = 0; i < input.size(); ++i) {
        CHECK(output[i] == doctest::Approx(input[i]));
    }
}

TEST_CASE("reset and process are safe on an unloaded model") {
    gitar::NeuralModel model;
    CHECK_FALSE(model.loaded());

    model.reset();

    std::vector<float> output(4, 1.0f);
    model.process(nullptr, output.data(), static_cast<int>(output.size()));
    for (float sample : output) {
        CHECK(sample == doctest::Approx(0.0f));
    }

    const std::vector<float> input = {1.0f, 1.0f, 1.0f, 1.0f};
    std::vector<float> ignored(input.size(), 1.0f);
    model.process(input.data(), ignored.data(), static_cast<int>(ignored.size()));
    for (float sample : ignored) {
        CHECK(sample == doctest::Approx(0.0f));
    }
}
