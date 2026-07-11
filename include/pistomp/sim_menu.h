// sim_menu.h — the macOS simulator's native Settings… menu + window.
//
// Part of the desktop simulator (alongside lvgl_display_sdl / audio_io_mac):
// the real pi-Stomp has no menu bar, but on the Mac we want audio device
// selection the normal Mac way — an app-menu "Settings…" item (⌘,) that opens
// a window of device/format pickers.
//
// This header stays Cocoa-free so the app (and the Pi build) can include it
// without dragging in AppKit. The app hands install() a Model of plain
// std::function callbacks; the Objective-C++ backend (settings_menu_mac.mm)
// turns them into NSPopUpButtons. On the Pi, settings_menu_stub.cpp makes
// install() a no-op — same "a file move, not a rewrite" split as the rest of
// the HAL.
//
// Threading: install() and every Model callback run on the main thread (the
// SDL/Cocoa event loop), so the app can touch its UI/audio state from apply()
// without locks.

#pragma once
#include <functional>
#include <string>
#include <vector>

namespace sim_menu {

// What the settings window shows and does. The app supplies live getters (so the
// popups always reflect current state when opened) and one apply() that performs
// the switch. Device lists are names matching pistomp::AudioDeviceInfo::name.
struct Model {
    std::function<std::vector<std::string>()> inputs;   // capture device names
    std::function<std::vector<std::string>()> outputs;  // playback device names
    std::function<std::vector<int>()>         rates;    // e.g. {44100, 48000, 96000}
    std::function<std::vector<int>()>         buffers;  // frames, e.g. {32, 64, 128, 256, 512}

    std::function<std::string()> currentInput;
    std::function<std::string()> currentOutput;
    std::function<int()>         currentRate;
    std::function<int()>         currentBuffer;

    // Apply a selection live. Returns true on success; false surfaces an alert.
    std::function<bool(const std::string& in, const std::string& out,
                       int rate, int buffer)> apply;
};

// Install the native Settings… menu item. Call once, after SDL has created the
// window (so the menu bar exists). No-op on non-macOS builds.
void install(const Model& model);

} // namespace sim_menu
