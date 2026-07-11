// test_quadrature.cpp -- host tests for the rotary decoder extracted from
// src/linux/encoder.cpp into pistomp/detail/quadrature.h.
//
// A "reading" is the (A,B) pair on the two encoder lines. Idle is 11 (both
// pulled high). One physical detent walks a 4-step Gray-code cycle:
//   CW : 01 -> 00 -> 10 -> 11   CCW: 10 -> 00 -> 01 -> 11
// The decoder reports the step only on the closing reading; contact bounce
// (illegal transitions) is filtered by the transition table.

#include "pistomp/detail/quadrature.h"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <utility>
#include <vector>

using pistomp::detail::QuadratureDecoder;

namespace {

using Reading = std::pair<bool, bool>;   // {A, B}

// A / B levels for each Gray-code position.
constexpr Reading R11{true, true};    // idle (both high)
constexpr Reading R01{false, true};
constexpr Reading R00{false, false};
constexpr Reading R10{true, false};

// One full detent per direction (closing reading last).
const std::vector<Reading> kCw  = {R01, R00, R10, R11};
const std::vector<Reading> kCcw = {R10, R00, R01, R11};

// Feed a sequence; return the sum of reported detents.
int feed(QuadratureDecoder& d, const std::vector<Reading>& seq) {
    int sum = 0;
    for (auto r : seq) sum += d.step(r.first, r.second);
    return sum;
}

// Feed a sequence and require every step except the last to report 0.
int feed_last(QuadratureDecoder& d, const std::vector<Reading>& seq) {
    int last = 0;
    for (size_t i = 0; i < seq.size(); ++i) {
        int r = d.step(seq[i].first, seq[i].second);
        if (i + 1 < seq.size()) REQUIRE(r == 0);
        else last = r;
    }
    return last;
}

} // namespace

TEST_CASE("full CW detent reports +1 exactly once, on the closing reading",
          "[quadrature]") {
    QuadratureDecoder d;
    REQUIRE(feed_last(d, kCw) == 1);
}

TEST_CASE("full CCW detent reports -1 exactly once, on the closing reading",
          "[quadrature]") {
    QuadratureDecoder d;
    REQUIRE(feed_last(d, kCcw) == -1);
}

TEST_CASE("detents accumulate across many continuous turns", "[quadrature]") {
    QuadratureDecoder d;
    int net = 0;
    for (int i = 0; i < 10; ++i) net += feed(d, kCw);
    REQUIRE(net == 10);
    for (int i = 0; i < 4; ++i) net += feed(d, kCcw);
    REQUIRE(net == 6);
}

TEST_CASE("direction reversal is clean: CW then CCW nets to zero",
          "[quadrature]") {
    QuadratureDecoder d;
    REQUIRE(feed(d, kCw) == 1);
    REQUIRE(feed(d, kCcw) == -1);
}

TEST_CASE("a partial cycle (no closing reading) yields no detent",
          "[quadrature]") {
    QuadratureDecoder d;
    REQUIRE(feed(d, {R01, R00, R10}) == 0);   // CW minus its closing 11
}

TEST_CASE("contact bounce: illegal jumps never report a detent",
          "[quadrature]") {
    QuadratureDecoder d;
    // 11 <-> 00 are diagonal (both bits flip at once): impossible for a real
    // encoder, so every transition is illegal and must be ignored.
    int sum = 0;
    for (int i = 0; i < 8; ++i) {
        sum += d.step(R11.first, R11.second);
        sum += d.step(R00.first, R00.second);
    }
    REQUIRE(sum == 0);
}

TEST_CASE("no motion: a held reading never reports a detent", "[quadrature]") {
    QuadratureDecoder d;
    int sum = 0;
    for (int i = 0; i < 16; ++i) sum += d.step(R11.first, R11.second);
    REQUIRE(sum == 0);
}

TEST_CASE("bounce mid-turn is filtered, then the real detent still lands",
          "[quadrature]") {
    QuadratureDecoder d;
    // Start a CW turn, glitch back and forth on the middle reading, then finish.
    REQUIRE(d.step(R01.first, R01.second) == 0);
    REQUIRE(d.step(R00.first, R00.second) == 0);
    // Spurious bounce: 00 -> 01 -> 00 (a wobble that isn't forward progress).
    d.step(R01.first, R01.second);
    d.step(R00.first, R00.second);
    // Complete the CW cycle; the detent must still be reported.
    d.step(R10.first, R10.second);
    REQUIRE(d.step(R11.first, R11.second) == 1);
}

TEST_CASE("reset() clears decoder state", "[quadrature]") {
    QuadratureDecoder d;
    feed(d, {R01, R00, R10});   // partial turn leaves state mid-cycle
    d.reset();
    // After reset a fresh full CW cycle behaves like the very first one.
    REQUIRE(feed_last(d, kCw) == 1);
}

TEST_CASE("free function matches the wrapper class", "[quadrature]") {
    // The driver calls quadrature_step() on loose state; the class wraps it.
    // Same inputs must give the same outputs.
    unsigned prev = 0, store = 0;
    QuadratureDecoder d;
    for (int rep = 0; rep < 3; ++rep) {
        for (auto r : kCw) {
            int a = pistomp::detail::quadrature_step(prev, store, r.first, r.second);
            int b = d.step(r.first, r.second);
            REQUIRE(a == b);
        }
    }
}
