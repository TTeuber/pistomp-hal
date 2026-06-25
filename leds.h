// leds.h — drive the pi-Stomp's 6 NeoPixels (WS2812 addressable RGB).
//
// Ported from sandbox/led_test.cpp. The kernel's ws2812-pio-rp1 driver handles
// all the hard WS2812 bit-timing and exposes the strip as /dev/leds0, so from
// C++ it's just open()/pwrite() of a 24-byte frame (6 pixels x R,G,B,W). No
// library, no timing code. NOTE: /dev/leds0 is root-only -> run the app w/ sudo.

#pragma once
#include <cstdint>

class Leds {
public:
    static constexpr int COUNT = 6;

    bool init();
    void close();                 // blanks the strip (NeoPixels latch their last frame)

    void set(int i, uint8_t r, uint8_t g, uint8_t b);   // stage one pixel
    void clear();                                       // stage all-off
    bool show();                                        // push staged frame to the strip

    // Set the driver's global brightness multiplier (0..255). The ws2812-pio
    // overlay scales every pixel by this; a 1-byte write to offset 0 sets it
    // (vs. the 24-byte pixel frame -- the driver dispatches on write size).
    bool set_brightness(uint8_t multiplier);

private:
    int fd_ = -1;
    uint8_t frame_[COUNT * 4] = {0};   // R,G,B,W per pixel; W unused (kept 0)
};
