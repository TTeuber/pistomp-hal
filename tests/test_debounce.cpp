// test_debounce.cpp -- host tests for the switch logic extracted from the
// footswitch / gpio_button drivers into pistomp/detail/debounce.h:
//   * pressed_below() -- active-low ADC threshold.
//   * rising_edge() / RisingEdge -- released->pressed edge, reported once.

#include "pistomp/detail/debounce.h"

#include <catch2/catch_test_macros.hpp>

using pistomp::detail::pressed_below;
using pistomp::detail::rising_edge;
using pistomp::detail::RisingEdge;

TEST_CASE("pressed_below thresholds active-low: pressed pulls the line down",
          "[debounce][threshold]") {
    // Footswitch convention: threshold 800, pressed when reading <= 800.
    REQUIRE(pressed_below(0, 800));       // fully pressed
    REQUIRE(pressed_below(800, 800));     // exactly at threshold counts as pressed
    REQUIRE_FALSE(pressed_below(801, 800));
    REQUIRE_FALSE(pressed_below(1023, 800));  // idle high

    // Encoder push-switch uses a lower threshold (512).
    REQUIRE(pressed_below(400, 512));
    REQUIRE_FALSE(pressed_below(513, 512));
}

TEST_CASE("rising edge fires once on press, not while held", "[debounce][edge]") {
    RisingEdge e;
    REQUIRE_FALSE(e.update(false));   // idle
    REQUIRE(e.update(true));          // released -> pressed: edge
    REQUIRE_FALSE(e.update(true));    // still held: no repeat
    REQUIRE_FALSE(e.update(true));
}

TEST_CASE("release produces no edge; the next press does", "[debounce][edge]") {
    RisingEdge e;
    REQUIRE(e.update(true));          // press
    REQUIRE_FALSE(e.update(false));   // release: no edge on falling
    REQUIRE(e.update(true));          // second press: edge again
}

TEST_CASE("multiple distinct presses each fire exactly once", "[debounce][edge]") {
    RisingEdge e;
    int edges = 0;
    // 5 clean press/release cycles.
    for (int i = 0; i < 5; ++i) {
        if (e.update(true))  ++edges;
        for (int h = 0; h < 3; ++h) e.update(true);   // held for a few polls
        e.update(false);                               // release
    }
    REQUIRE(edges == 5);
}

TEST_CASE("first poll already pressed still counts as an edge", "[debounce][edge]") {
    // prev_ defaults to released, so a switch already down on the very first
    // poll registers one edge (matches the driver's was_pressed_ = false init).
    RisingEdge e;
    REQUIRE(e.update(true));
}

TEST_CASE("reset() seeds the previous state", "[debounce][edge]") {
    RisingEdge e;
    e.reset(true);                   // pretend already held
    REQUIRE_FALSE(e.update(true));   // no edge -- state was already pressed
    REQUIRE_FALSE(e.update(false));  // release
    REQUIRE(e.update(true));         // press -> edge
}

TEST_CASE("pressed() reflects the last observed level", "[debounce][edge]") {
    RisingEdge e;
    REQUIRE_FALSE(e.pressed());
    e.update(true);
    REQUIRE(e.pressed());
    e.update(false);
    REQUIRE_FALSE(e.pressed());
}

TEST_CASE("free function matches the wrapper class", "[debounce][edge]") {
    bool prev = false;
    RisingEdge e;
    const bool seq[] = {false, true, true, false, true, false, false, true};
    for (bool p : seq) {
        REQUIRE(rising_edge(prev, p) == e.update(p));
    }
}

TEST_CASE("threshold + edge compose the footswitch poll behaviour",
          "[debounce][integration]") {
    // Reproduce Footswitch::poll_pressed_edge over a stream of raw ADC values:
    // an edge only when the reading crosses from above to at/below threshold.
    RisingEdge e;
    const int threshold = 800;
    // idle, dip to pressed (held 3 polls), release, press again.
    const int raw[] = {1023, 1000, 200, 150, 180, 900, 1023, 50};
    int edges = 0;
    for (int v : raw) {
        if (e.update(pressed_below(v, threshold))) ++edges;
    }
    REQUIRE(edges == 2);   // one edge into the 200/150/180 dip, one into the 50
}
