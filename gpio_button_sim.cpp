// gpio_button_sim.cpp — desktop stand-in for gpio_button.cpp.
//
// Same edge/hold semantics; reads the keyboard-fed sim_input bus instead of a
// gpiod line. The logical button is resolved from the pin and cached per
// instance via a small pointer registry, so the header stays unchanged.

#include "gpio_button.h"
#include "sim_input.h"

#include <unordered_map>

namespace {
std::unordered_map<const GpioButton*, sim_input::Btn> g_map;
}

bool GpioButton::init(int pin, const char* /*consumer*/) {
    g_map[this] = sim_input::gpio_button_for_pin(pin);
    was_pressed_ = false;
    return true;
}

void GpioButton::close() { g_map.erase(this); }

bool GpioButton::is_pressed() {
    auto it = g_map.find(this);
    return it != g_map.end() && sim_input::button_pressed(it->second);
}

bool GpioButton::poll_pressed_edge() {
    bool now = is_pressed();
    bool edge = now && !was_pressed_;
    was_pressed_ = now;
    return edge;
}
