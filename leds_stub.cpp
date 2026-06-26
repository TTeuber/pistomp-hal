// leds_stub.cpp — desktop stand-in for leds.cpp.
//
// No NeoPixel strip on the Mac. Calls succeed and staged colors are stored, and
// show() publishes the frame to the sim bus so the on-screen panel
// (lvgl_display_sdl.cpp) can light its simulated LEDs. The footswitch/LED logic
// thus runs fully, with the lights mirrored on screen instead of on hardware.

#include "leds.h"
#include "sim_input.h"

bool Leds::init() { return true; }
void Leds::close() { clear(); show(); }   // blank the on-screen strip on exit

void Leds::set(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < 0 || i >= COUNT) return;
    frame_[i * 4 + 0] = r;
    frame_[i * 4 + 1] = g;
    frame_[i * 4 + 2] = b;
}

void Leds::clear() {
    for (auto& b : frame_) b = 0;
}

bool Leds::show() {
    sim_input::publish_leds(frame_, COUNT);
    return true;
}

bool Leds::set_brightness(uint8_t) { return true; }
