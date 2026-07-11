// footswitch_sim.cpp — desktop stand-in for footswitch.cpp.
//
// Same edge/hold semantics; the ADC channel given at init() maps to a logical
// control (footswitch 0..3 or the nav switch) on the keyboard-fed sim_input
// bus. channel_ is a real header field, so nothing in the header changes.

#include "pistomp/footswitch.h"
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
    bool now = is_pressed();
    bool edge = now && !was_pressed_;
    was_pressed_ = now;
    return edge;
}
