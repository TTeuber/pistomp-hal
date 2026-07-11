// sim_input.h — desktop (macOS sim) control bus.
//
// On the Pi the encoders/footswitches/buttons are real hardware lines. In the
// Mac simulator there's no hardware, so the *_sim.cpp drivers read their state
// from this process-global bus instead. The bus has two writers — pump() reads
// the SDL keyboard, and the on-screen panel (lvgl_display_sdl.cpp) injects mouse
// clicks/drags — and the sim drivers read it from the input thread. All shared
// state is atomic, so there's no lock and no data race.
//
// Key map (see pump() in sim_input.cpp):
//   Nav encoder rotate : Left / Right       Nav select/hold-quit : Return
//   Enc1 turn / button : Q / W  +  E         Enc2 turn / button  : A / S  +  D
//   Enc3 turn (master) : Z / X               Footswitches FS0..3 : 1 2 3 4
//
// The same controls are also drawn on screen and driven by the mouse (drag an
// encoder vertically to turn it, click its centre for the push-switch, click a
// footswitch to engage it). Mouse and keyboard are OR'd together.

#pragma once

#include <cstdint>

namespace sim_input {

enum class Enc { Nav, E1, E2, E3, Count };
enum class Btn { Enc1, Enc2, NavSw, FS0, FS1, FS2, FS3, Count };

// Read the current SDL keyboard state and update the bus. Call on the thread
// that owns SDL (the UI thread) — wired as an LVGL timer by the SDL display.
void pump();

// Consume one detent of accumulated rotation: +1 (CW), -1 (CCW), or 0.
int encoder_step(Enc e);

// Current pressed state (no edge).
bool button_pressed(Btn b);

// ---- mouse injection (on-screen panel -> bus) ------------------------------
// Add `steps` detents of rotation (+CW / -CCW) to an encoder, as if turned.
void inject_encoder(Enc e, int steps);
// Hold/release a button from the mouse; OR'd with the keyboard in pump().
void set_mouse_button(Btn b, bool pressed);
// Monotonic running detent count (never consumed) — lets the panel draw the
// knob at its current angle. Reflects both keyboard turns and mouse drags.
long encoder_total(Enc e);

// ---- LED snapshot (leds_stub.cpp -> on-screen panel) -----------------------
// Publish the staged NeoPixel frame (rgbw[count*4], the W byte ignored) so the
// panel can light the on-screen LEDs. Called by Leds::show() in the stub.
void publish_leds(const uint8_t* rgbw, int count);
// Read back one pixel's colour (0,0,0 = off).
void led_rgb(int i, uint8_t& r, uint8_t& g, uint8_t& b);

// Map a sim driver's init() identifier to a logical control, mirroring the
// pi-Stomp v3 wiring in board_v3.h.
Enc encoder_for_dpin(int d_pin);    // 17->Nav 12->E1 24->E2 22->E3
Btn footswitch_for_channel(int ch); // 4->NavSw 0..3->FS0..3
Btn gpio_button_for_pin(int pin);   // 16->Enc1 26->Enc2

} // namespace sim_input
