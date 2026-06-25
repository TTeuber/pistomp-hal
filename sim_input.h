// sim_input.h — desktop (macOS sim) control bus.
//
// On the Pi the encoders/footswitches/buttons are real hardware lines. In the
// Mac simulator there's no hardware, so the *_sim.cpp drivers read their state
// from this process-global bus instead. The bus is fed from the SDL keyboard by
// pump(), which runs on the UI thread (registered as an LVGL timer in
// lvgl_display_sdl.cpp). The sim drivers read it from the input thread; all
// shared state is atomic, so there's no lock and no data race.
//
// Key map (see pump() in sim_input.cpp):
//   Nav encoder rotate : Left / Right       Nav select/hold-quit : Return
//   Enc1 turn / button : Q / W  +  E         Enc2 turn / button  : A / S  +  D
//   Enc3 turn (master) : Z / X               Footswitches FS0..3 : 1 2 3 4

#pragma once

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

// Map a sim driver's init() identifier to a logical control, mirroring the
// pi-Stomp v3 wiring in board_v3.h.
Enc encoder_for_dpin(int d_pin);    // 17->Nav 12->E1 24->E2 22->E3
Btn footswitch_for_channel(int ch); // 4->NavSw 0..3->FS0..3
Btn gpio_button_for_pin(int pin);   // 16->Enc1 26->Enc2

} // namespace sim_input
