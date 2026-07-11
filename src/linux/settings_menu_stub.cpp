// settings_menu_stub.cpp — the Pi build's no-op for sim_menu::install().
//
// The native Settings… window is a macOS-simulator affordance (settings_menu_
// mac.mm). The real pi-Stomp has no menu bar — its control surface is the codec
// hardware UI and the web app — so on the Pi install() does nothing. Same split
// as ili9341_stub.cpp / leds_stub.cpp: the header is the contract, the platform
// picks the .cpp.

#include "pistomp/sim_menu.h"

namespace sim_menu {

void install(const Model&) {}

} // namespace sim_menu
