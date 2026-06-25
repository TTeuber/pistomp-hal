// sim_input.cpp — see sim_input.h. Translates the SDL keyboard into the
// pi-Stomp's control surface for the Mac simulator.

#include "sim_input.h"

#include <SDL2/SDL.h>

#include <atomic>

namespace sim_input {
namespace {

// Accumulated, not-yet-consumed rotation per encoder, and current button state.
std::atomic<int> g_encDelta[(int)Enc::Count];
std::atomic<bool> g_btn[(int)Btn::Count];

// Previous keydown state for each encoder's CW/CCW key, so a key *press* yields
// exactly one detent (no auto-repeat while held).
bool g_prevCw[(int)Enc::Count];
bool g_prevCcw[(int)Enc::Count];

void edge(Enc e, bool cw, bool ccw) {
  int i = (int)e;
  if (cw && !g_prevCw[i])
    g_encDelta[i].fetch_add(1, std::memory_order_relaxed);
  if (ccw && !g_prevCcw[i])
    g_encDelta[i].fetch_add(-1, std::memory_order_relaxed);
  g_prevCw[i] = cw;
  g_prevCcw[i] = ccw;
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

  g_btn[(int)Btn::Enc1].store(k[SDL_SCANCODE_E], std::memory_order_relaxed);
  g_btn[(int)Btn::Enc2].store(k[SDL_SCANCODE_D], std::memory_order_relaxed);
  g_btn[(int)Btn::NavSw].store(k[SDL_SCANCODE_RETURN],
                               std::memory_order_relaxed);
  g_btn[(int)Btn::FS0].store(k[SDL_SCANCODE_1], std::memory_order_relaxed);
  g_btn[(int)Btn::FS1].store(k[SDL_SCANCODE_2], std::memory_order_relaxed);
  g_btn[(int)Btn::FS2].store(k[SDL_SCANCODE_3], std::memory_order_relaxed);
  g_btn[(int)Btn::FS3].store(k[SDL_SCANCODE_4], std::memory_order_relaxed);
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
  return g_btn[(int)b].load(std::memory_order_relaxed);
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
