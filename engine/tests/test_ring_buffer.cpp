#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include "doctest/doctest.h"
#include "gitar/ring_buffer.hpp"

static_assert(!std::is_copy_constructible_v<gitar::SpscRingBuffer<int, 4>>);
static_assert(!std::is_copy_assignable_v<gitar::SpscRingBuffer<int, 4>>);

TEST_CASE("ring buffer starts empty with the requested capacity") {
    gitar::SpscRingBuffer<int, 4> buffer;
    CHECK(buffer.empty());
    CHECK_FALSE(buffer.full());
    CHECK(buffer.size() == 0);
    CHECK(buffer.capacity() == 4);
}

TEST_CASE("ring buffer tracks empty, full and size transitions") {
    gitar::SpscRingBuffer<int, 4> buffer;
    CHECK(buffer.push(1));
    CHECK_FALSE(buffer.empty());
    CHECK_FALSE(buffer.full());
    CHECK(buffer.size() == 1);

    CHECK(buffer.push(2));
    CHECK(buffer.push(3));
    CHECK(buffer.push(4));
    CHECK(buffer.full());
    CHECK(buffer.size() == 4);

    CHECK_FALSE(buffer.push(5));
    CHECK(buffer.size() == 4);
}

TEST_CASE("ring buffer preserves FIFO order") {
    gitar::SpscRingBuffer<int, 4> buffer;
    for (int i = 0; i < 4; ++i) {
        CHECK(buffer.push(i));
    }
    for (int i = 0; i < 4; ++i) {
        int value = -1;
        CHECK(buffer.pop(value));
        CHECK(value == i);
    }
    CHECK(buffer.empty());
}

TEST_CASE("ring buffer pop on empty returns false and leaves out untouched") {
    gitar::SpscRingBuffer<int, 4> buffer;
    int value = 42;
    CHECK_FALSE(buffer.pop(value));
    CHECK(value == 42);
}

TEST_CASE("ring buffer wraps around after more than capacity operations") {
    gitar::SpscRingBuffer<int, 4> buffer;
    for (int i = 0; i < 100; ++i) {
        CHECK(buffer.push(i));
        int value = -1;
        CHECK(buffer.pop(value));
        CHECK(value == i);
    }
    CHECK(buffer.empty());
    CHECK(buffer.size() == 0);
}

TEST_CASE("ring buffer supports move-only payloads through push(T&&)") {
    gitar::SpscRingBuffer<std::unique_ptr<int>, 2> buffer;
    CHECK(buffer.push(std::make_unique<int>(7)));
    std::unique_ptr<int> out;
    CHECK(buffer.pop(out));
    REQUIRE(out != nullptr);
    CHECK(*out == 7);
}
