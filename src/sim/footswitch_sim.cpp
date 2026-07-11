// footswitch_sim.cpp — desktop stand-in for footswitch.cpp.
//
// Same edge/hold semantics; the ADC channel given at init() maps to a logical
// control (footswitch 0..3 or the nav switch) on the keyboard-fed sim_input
// bus. channel_ is a real header field, so nothing in the header changes.

#include "pistomp/footswitch.h"
#include "pistomp/detail/debounce.h"   // shared edge logic (same as the Pi driver)
#include "pistomp/sim_input.h"

bool Footswitch::init(int channel, int threshold) {
    channel_ = channel;
    threshold_ = threshold;
    was_pressed_ = false;
    return true;
}

void Footswitch::close() {}

bool Footswitch::is_pressed() {
    return sim_input::button_pressed(sim_input::footswitch_for_channel(channel_));
}

bool Footswitch::poll_pressed_edge() {
    return pistomp::detail::rising_edge(was_pressed_, is_pressed());
}
