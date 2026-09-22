#include <cstddef>
#include <vector>

#include "doctest/doctest.h"
#include "gitar/bridge.hpp"
#include "gitar/ring_buffer.hpp"

TEST_CASE("bridge reports frames written and reads them back unchanged") {
    gitar::InterleavedBridge bridge(2);
    std::vector<float> input(20);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<float>(i);
    }
    CHECK(bridge.write(input.data(), 10) == 10);

    std::vector<float> output(20, -1.0f);
    CHECK(bridge.read(output.data(), 10) == 10);
    for (std::size_t i = 0; i < input.size(); ++i) {
        CHECK(output[i] == doctest::Approx(input[i]));
    }
    CHECK(bridge.overrun_frames() == 0);
    CHECK(bridge.underrun_frames() == 0);
}

TEST_CASE("bridge zero-fills the remainder of a short read") {
    gitar::InterleavedBridge bridge(2);
    std::vector<float> output(8, 7.0f);
    CHECK(bridge.read(output.data(), 4) == 0);
    for (float sample : output) {
        CHECK(sample == 0.0f);
    }
    CHECK(bridge.underrun_frames() == 4);
}

TEST_CASE("bridge counts overruns once the buffer is full") {
    gitar::InterleavedBridge bridge(1);
    const std::size_t capacity = gitar::SpscRingBuffer<float, 65536>::capacity();
    std::vector<float> block(capacity, 1.0f);
    CHECK(bridge.write(block.data(), capacity) == capacity);
    CHECK(bridge.overrun_frames() == 0);

    const float extra = 2.0f;
    CHECK(bridge.write(&extra, 5) == 0);
    CHECK(bridge.overrun_frames() == 5);
}

TEST_CASE("bridge clamps writes to whole frames that fit") {
    gitar::InterleavedBridge bridge(3);
    const std::size_t capacity = gitar::SpscRingBuffer<float, 65536>::capacity();
    const std::size_t frame_capacity = capacity / 3;
    std::vector<float> block((frame_capacity + 1) * 3, 1.0f);
    CHECK(bridge.write(block.data(), frame_capacity + 1) == frame_capacity);
    CHECK(bridge.overrun_frames() == 1);

    std::vector<float> output((frame_capacity + 1) * 3, -1.0f);
    CHECK(bridge.read(output.data(), frame_capacity + 1) == frame_capacity);
    CHECK(bridge.underrun_frames() == 1);
}

TEST_CASE("bridge survives wraparound across many write/read cycles") {
    gitar::InterleavedBridge bridge(2);
    for (int cycle = 0; cycle < 50000; ++cycle) {
        const float sample = static_cast<float>(cycle);
        float in[4] = {sample, sample, sample, sample};
        REQUIRE(bridge.write(in, 2) == 2);
        float out[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        REQUIRE(bridge.read(out, 2) == 2);
        CHECK(out[0] == doctest::Approx(sample));
        CHECK(out[2] == doctest::Approx(sample));
    }
    CHECK(bridge.overrun_frames() == 0);
    CHECK(bridge.underrun_frames() == 0);
}

TEST_CASE("bridge treats zero channels as mono") {
    gitar::InterleavedBridge bridge(0);
    float in[3] = {1.0f, 2.0f, 3.0f};
    CHECK(bridge.write(in, 3) == 3);
    float out[3] = {0.0f, 0.0f, 0.0f};
    CHECK(bridge.read(out, 3) == 3);
    CHECK(out[0] == doctest::Approx(1.0f));
    CHECK(out[2] == doctest::Approx(3.0f));
}
