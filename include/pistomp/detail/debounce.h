// debounce.h — switch edge/level logic extracted from the footswitch and
// gpio_button drivers (src/linux/ + src/sim/) so it can be unit-tested on any
// host (std only, no platform includes). Behaviour is identical to the drivers:
// they read a line/ADC, reduce it to a pressed bool, and report the
// released->pressed edge exactly once (so a held switch toggles state once, not
// once per poll).

#pragma once

namespace pistomp {
namespace detail {

// Active-low ADC threshold: pressed when the 0..1023 reading is <= threshold
// (the switch pulls the line down when closed). Mirrors Footswitch::is_pressed.
inline bool pressed_below(int raw, int threshold) { return raw <= threshold; }

// Rising-edge detector over a pressed bool. update() returns true exactly on
// the false->true (released->pressed) transition. prev is the caller-owned
// state; this is the same computation the drivers inline as
//   edge = pressed && !was_pressed_; was_pressed_ = pressed;
inline bool rising_edge(bool& prev, bool pressed) {
    bool edge = pressed && !prev;   // released -> pressed transition
    prev = pressed;
    return edge;
}

// Convenience wrapper owning the previous-state bit, for callers (and tests)
// that want an object. update() is the same code path the drivers run via
// rising_edge().
class RisingEdge {
public:
    // True exactly on the false->true transition. See rising_edge().
    bool update(bool pressed) { return rising_edge(prev_, pressed); }
    void reset(bool pressed = false) { prev_ = pressed; }
    bool pressed() const { return prev_; }

private:
    bool prev_ = false;
};

} // namespace detail
} // namespace pistomp
