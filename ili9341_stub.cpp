// ili9341_stub.cpp — desktop stand-in for ili9341.cpp.
//
// There's no SPI panel on the Mac; the SDL window owns the real framebuffer
// (see lvgl_display_sdl.cpp). This stub exists only so the app's call sequence
// — g_board.openLcd(lcd), lvgl_display::init(lcd), and the lcd.fill(0)/close()
// on shutdown — compiles and runs unchanged. init() still reports the panel's
// dimensions so the SDL window is created at the right size.

#include "ili9341.h"

bool Ili9341::init(int rotation) {
    if (rotation == 0) { w_ = 240; h_ = 320; }   // portrait
    else               { w_ = 320; h_ = 240; }   // landscape (pi-Stomp default)
    return true;
}

void Ili9341::close() {}
void Ili9341::flush(int, int, int, int, const uint8_t*) {}
void Ili9341::fill(uint16_t) {}
