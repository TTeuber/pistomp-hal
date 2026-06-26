// sim_input.cpp — see sim_input.h. Translates the SDL keyboard into the
// pi-Stomp's control surface for the Mac simulator.

#include "sim_input.h"

#include <SDL2/SDL.h>

#include <atomic>

namespace sim_input {
namespace {

// Accumulated, not-yet-consumed rotation per encoder; a monotonic running total
// for drawing the knob; current keyboard + mouse button state; and the published
// LED frame. All atomic — written on the UI thread, read on the input thread.
std::atomic<int> g_encDelta[(int)Enc::Count];
std::atomic<long> g_encTotal[(int)Enc::Count];
std::atomic<bool> g_kbBtn[(int)Btn::Count];     // keyboard, refreshed every pump()
std::atomic<bool> g_mouseBtn[(int)Btn::Count];  // mouse, held across pumps
std::atomic<uint32_t> g_led[6];                 // 0x00RRGGBB per pixel

// Previous keydown state for each encoder's CW/CCW key, so a key *press* yields
// exactly one detent (no auto-repeat while held).
bool g_prevCw[(int)Enc::Count];
bool g_prevCcw[(int)Enc::Count];

void turn(Enc e, int steps) {
  int i = (int)e;
  g_encDelta[i].fetch_add(steps, std::memory_order_relaxed);
  g_encTotal[i].fetch_add(steps, std::memory_order_relaxed);
}

void edge(Enc e, bool cw, bool ccw) {
  int i = (int)e;
  if (cw && !g_prevCw[i])
    turn(e, +1);
  if (ccw && !g_prevCcw[i])
    turn(e, -1);
  g_prevCw[i] = cw;
  g_prevCcw[i] = ccw;
}

// Store keyboard state OR'd with whatever the mouse is currently holding.
void store_btn(Btn b, bool kb) {
  int i = (int)b;
  g_kbBtn[i].store(kb, std::memory_order_relaxed);
}

} // namespace

void pump() {
  const Uint8 *k = SDL_GetKeyboardState(nullptr);
  if (!k)
    return;

  edge(Enc::Nav, k[SDL_SCANCODE_RIGHT], k[SDL_SCANCODE_LEFT]);
  edge(Enc::E1, k[SDL_SCANCODE_W], k[SDL_SCANCODE_Q]);
  edge(Enc::E2, k[SDL_SCANCODE_S], k[SDL_SCANCODE_A]);
  edge(Enc::E3, k[SDL_SCANCODE_X], k[SDL_SCANCODE_Z]);

  store_btn(Btn::Enc1, k[SDL_SCANCODE_E]);
  store_btn(Btn::Enc2, k[SDL_SCANCODE_D]);
  store_btn(Btn::NavSw, k[SDL_SCANCODE_RETURN]);
  store_btn(Btn::FS0, k[SDL_SCANCODE_1]);
  store_btn(Btn::FS1, k[SDL_SCANCODE_2]);
  store_btn(Btn::FS2, k[SDL_SCANCODE_3]);
  store_btn(Btn::FS3, k[SDL_SCANCODE_4]);
}

int encoder_step(Enc e) {
  int i = (int)e;
  int v = g_encDelta[i].load(std::memory_order_relaxed);
  while (v != 0) {
    int nv = v > 0 ? v - 1 : v + 1;
    if (g_encDelta[i].compare_exchange_weak(v, nv, std::memory_order_relaxed))
      return v > 0 ? +1 : -1;
  }
  return 0;
}

bool button_pressed(Btn b) {
  int i = (int)b;
  return g_kbBtn[i].load(std::memory_order_relaxed) ||
         g_mouseBtn[i].load(std::memory_order_relaxed);
}

void inject_encoder(Enc e, int steps) {
  if (steps) turn(e, steps);
}

void set_mouse_button(Btn b, bool pressed) {
  g_mouseBtn[(int)b].store(pressed, std::memory_order_relaxed);
}

long encoder_total(Enc e) {
  return g_encTotal[(int)e].load(std::memory_order_relaxed);
}

void publish_leds(const uint8_t* rgbw, int count) {
  if (count > 6) count = 6;
  for (int i = 0; i < count; i++) {
    uint32_t c = ((uint32_t)rgbw[i * 4 + 0] << 16) |
                 ((uint32_t)rgbw[i * 4 + 1] << 8) |
                 ((uint32_t)rgbw[i * 4 + 2]);
    g_led[i].store(c, std::memory_order_relaxed);
  }
}

void led_rgb(int i, uint8_t& r, uint8_t& g, uint8_t& b) {
  uint32_t c = (i >= 0 && i < 6) ? g_led[i].load(std::memory_order_relaxed) : 0;
  r = (c >> 16) & 0xFF;
  g = (c >> 8) & 0xFF;
  b = c & 0xFF;
}

Enc encoder_for_dpin(int d_pin) {
  switch (d_pin) {
  case 12:
    return Enc::E1;
  case 24:
    return Enc::E2;
  case 22:
    return Enc::E3;
  default:
    return Enc::Nav; // 17 (nav) and anything else
  }
}

Btn footswitch_for_channel(int ch) {
  switch (ch) {
  case 0:
    return Btn::FS0;
  case 1:
    return Btn::FS1;
  case 2:
    return Btn::FS2;
  case 3:
    return Btn::FS3;
  default:
    return Btn::NavSw; // channel 4 = nav switch
  }
}

Btn gpio_button_for_pin(int pin) {
  return pin == 26 ? Btn::Enc2 : Btn::Enc1; // 16 = enc1, 26 = enc2
}

} // namespace sim_input
