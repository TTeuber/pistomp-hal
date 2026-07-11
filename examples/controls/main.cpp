// controls — the control surface without a screen.
//
// Board vends the ready-to-poll controls (it knows the pin/ADC map and wires the
// shared SPI-bus lock for you). Here: the nav encoder cycles a colour, each
// footswitch toggles its own NeoPixel on/off in that colour. A plain poll loop
// prints every event; run until Ctrl-C.
//
// No realtime thread and no display — just the input/LED side of the HAL. On the
// Pi these are real GPIO/ADC lines and NeoPixels. (In the macOS sim the keyboard
// is read through the SDL window, so a screenless program has no input source
// there — this one is meant to be driven on hardware; see the display and
// mini_pedal examples for the sim-driven controls.)

#include "pistomp/board.h"
#include "pistomp/encoder.h"
#include "pistomp/footswitch.h"
#include "pistomp/leds.h"

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace {
volatile std::sig_atomic_t g_stop = 0;
void on_sigint(int) { g_stop = 1; }

// Minimal full-saturation HSV->RGB (h in degrees) so one knob sweeps the rainbow.
void hsv_rgb(int h, uint8_t& r, uint8_t& g, uint8_t& b) {
    float hp = h / 60.0f;                                     // 0..6
    float x = 1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f);
    float rf = 0, gf = 0, bf = 0;
    switch ((int)hp % 6) {
        case 0: rf = 1; gf = x; break;
        case 1: rf = x; gf = 1; break;
        case 2: gf = 1; bf = x; break;
        case 3: gf = x; bf = 1; break;
        case 4: rf = x; bf = 1; break;
        default: rf = 1; bf = x; break;
    }
    // NeoPixels are bright — hold levels to ~40% so the colour reads cleanly.
    r = (uint8_t)(rf * 100); g = (uint8_t)(gf * 100); b = (uint8_t)(bf * 100);
}
} // namespace

int main() {
    pistomp::Board board;

    Encoder nav;
    Footswitch fs[pistomp::board::kFootswitchCount];
    Leds leds;

    if (!board.openNavEncoder(nav, "controls_demo")) {
        std::fprintf(stderr, "nav encoder open failed\n");
        return 1;
    }
    for (int i = 0; i < pistomp::board::kFootswitchCount; i++)
        board.openFootswitch(fs[i], i);
    if (!leds.init())
        std::fprintf(stderr, "LEDs unavailable (Pi: run with sudo for /dev/leds0)\n");

    int hue = 0;
    bool on[pistomp::board::kFootswitchCount] = {false};

    std::signal(SIGINT, on_sigint);
    std::printf("controls: nav encoder cycles colour, footswitches toggle LEDs. "
                "Ctrl-C to stop.\n");

    while (!g_stop) {
        bool dirty = false;

        // Nav encoder: each detent turns the colour wheel; recolour lit LEDs.
        if (int d = nav.poll()) {
            hue = (hue + d * 20 + 360) % 360;
            std::printf("nav -> hue %d\n", hue);
            uint8_t r, g, b; hsv_rgb(hue, r, g, b);
            for (int i = 0; i < pistomp::board::kFootswitchCount; i++)
                if (on[i]) leds.set(i, r, g, b);
            dirty = true;
        }

        // Footswitches: toggle this switch's LED on the press edge.
        for (int i = 0; i < pistomp::board::kFootswitchCount; i++) {
            if (fs[i].poll_pressed_edge()) {
                on[i] = !on[i];
                std::printf("FS%d -> %s\n", i, on[i] ? "on" : "off");
                if (on[i]) { uint8_t r, g, b; hsv_rgb(hue, r, g, b); leds.set(i, r, g, b); }
                else leds.set(i, 0, 0, 0);
                dirty = true;
            }
        }

        if (dirty) leds.show();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));  // ~1 kHz poll
    }

    leds.close();   // blank the strip on exit
    nav.close();
    for (auto& s : fs) s.close();
    std::printf("\nstopped.\n");
    return 0;
}
