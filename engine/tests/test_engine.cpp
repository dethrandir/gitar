#include <cstdlib>
#include <filesystem>
#include <string>

#include "doctest/doctest.h"
#include "gitar/devices.hpp"
#include "gitar/engine.hpp"

namespace {

std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(GITAR_TEST_FIXTURES_DIR) / name;
}

}  // namespace

TEST_CASE("a fresh engine is stopped with zero counters") {
    gitar::Engine engine;
    CHECK_FALSE(engine.running());
    CHECK(engine.latency_ms() == 0.0);
    CHECK(engine.overrun_frames() == 0);
    CHECK(engine.underrun_frames() == 0);
}

TEST_CASE("device enumeration never throws") {
    CHECK_NOTHROW((void)gitar::list_devices());
}

TEST_CASE("stop is idempotent on a fresh engine") {
    gitar::Engine engine;
    CHECK_NOTHROW(engine.stop());
    CHECK_NOTHROW(engine.stop());
    CHECK_FALSE(engine.running());
}

TEST_CASE("start fails cleanly for an unknown device") {
    gitar::Engine engine;
    gitar::EngineConfig config;
    config.input_device = "gitar-no-such-input-device";
    std::string error;
    CHECK_FALSE(engine.start(config, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(engine.running());
    CHECK(engine.latency_ms() == 0.0);
}

TEST_CASE("engine starts and stops with the default devices when enabled") {
    if (std::getenv("GITAR_ENGINE_DEVICE_TEST") == nullptr) {
        return;
    }
    gitar::Engine engine;
    gitar::EngineConfig config;
    std::string error;
    REQUIRE(engine.start(config, &error));
    CHECK(error.empty());
    CHECK(engine.running());
    CHECK(engine.sample_rate() == config.sample_rate);
    CHECK(engine.period_frames() > 0);
    engine.stop();
    CHECK_FALSE(engine.running());
}

TEST_CASE("load_model loads a fixture and clears it with an empty path") {
    gitar::Engine engine;
    CHECK_FALSE(engine.model_loaded());
    CHECK(engine.model_path().empty());

    std::string error;
    REQUIRE(engine.load_model(fixture("identity.nam").string(), &error));
    CHECK(error.empty());
    CHECK(engine.model_loaded());
    CHECK(engine.model_path() == fixture("identity.nam").string());

    REQUIRE(engine.load_model(""));
    CHECK_FALSE(engine.model_loaded());
    CHECK(engine.model_path().empty());
}

TEST_CASE("load_model reports a missing model with an error") {
    gitar::Engine engine;
    std::string error;
    CHECK_FALSE(engine.load_model(fixture("does_not_exist.nam").string(), &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(engine.model_loaded());
}

TEST_CASE("load_cab_ir loads a fixture, clears and reports a missing file") {
    gitar::Engine engine;
    CHECK_FALSE(engine.cab_ir_loaded());
    CHECK(engine.cab_ir_path().empty());

    std::string error;
    REQUIRE(engine.load_cab_ir(fixture("impulse.wav").string(), &error));
    CHECK(error.empty());
    CHECK(engine.cab_ir_loaded());
    CHECK(engine.cab_ir_path() == fixture("impulse.wav").string());

    REQUIRE(engine.load_cab_ir(""));
    CHECK_FALSE(engine.cab_ir_loaded());
    CHECK(engine.cab_ir_path().empty());

    CHECK_FALSE(engine.load_cab_ir(fixture("does_not_exist.wav").string(), &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(engine.cab_ir_loaded());
}

TEST_CASE("set_eq round-trips and clamps") {
    gitar::Engine engine;
    CHECK(engine.eq_low_db() == doctest::Approx(0.0f));
    CHECK(engine.eq_mid_db() == doctest::Approx(0.0f));
    CHECK(engine.eq_high_db() == doctest::Approx(0.0f));

    engine.set_eq(3.0f, 0.0f, -3.0f);
    CHECK(engine.eq_low_db() == doctest::Approx(3.0f));
    CHECK(engine.eq_mid_db() == doctest::Approx(0.0f));
    CHECK(engine.eq_high_db() == doctest::Approx(-3.0f));

    engine.set_eq(-100.0f, 100.0f, 0.0f);
    CHECK(engine.eq_low_db() == doctest::Approx(-24.0f));
    CHECK(engine.eq_mid_db() == doctest::Approx(24.0f));
    CHECK(engine.eq_high_db() == doctest::Approx(0.0f));
}

TEST_CASE("the gate settings round-trip and clamp") {
    gitar::Engine engine;
    CHECK(engine.gate_enabled());

    engine.set_gate_enabled(false);
    CHECK_FALSE(engine.gate_enabled());

    engine.set_gate_threshold_db(-40.0f);
    CHECK(engine.gate_threshold_db() == doctest::Approx(-40.0f));

    engine.set_gate_threshold_db(-200.0f);
    CHECK(engine.gate_threshold_db() == doctest::Approx(-96.0f));

    engine.set_gate_threshold_db(20.0f);
    CHECK(engine.gate_threshold_db() == doctest::Approx(0.0f));
}
