// leds_stub.cpp — desktop stand-in for leds.cpp.
//
// No NeoPixel strip on the Mac. Calls succeed and staged colors are stored (so
// any code that reads back frame_ still works), but show() does nothing visible.
// An on-screen LED strip could be added later; for now footswitch/LED *logic*
// runs fully, just without lights.

#include "leds.h"

bool Leds::init() { return true; }
void Leds::close() {}

void Leds::set(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < 0 || i >= COUNT) return;
    frame_[i * 4 + 0] = r;
    frame_[i * 4 + 1] = g;
    frame_[i * 4 + 2] = b;
}

void Leds::clear() {
    for (auto& b : frame_) b = 0;
}

bool Leds::show() { return true; }
bool Leds::set_brightness(uint8_t) { return true; }
