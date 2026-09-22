#include <cstdlib>
#include <string>

#include "doctest/doctest.h"
#include "gitar/devices.hpp"
#include "gitar/engine.hpp"

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
