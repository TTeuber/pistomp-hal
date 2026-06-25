// lvgl_display_sdl.cpp — desktop stand-in for lvgl_display.cpp.
//
// On the Pi this binds LVGL to the ILI9341 over SPI. On the Mac it instead
// creates an SDL window (LVGL's built-in SDL driver supplies the framebuffer,
// flush, tick and RGB565 handling), plus mouse + keyboard input devices. The
// `lcd` argument is accepted only to keep the app's call site identical; its
// width()/height() set the window size.
//
// SDL events (and our simulated controls) are pumped by the SDL driver's own
// LVGL timer and the pump timer registered below — both serviced by the app's
// existing lv_timer_handler() loop, so no application code changes.

#include "lvgl_display.h"
#include "ili9341.h"
#include "sim_input.h"

#include "lvgl.h"
#include <SDL2/SDL.h>

#include <csignal>

namespace {
// LVGL timer trampoline -> read the SDL keyboard into the sim_input bus.
void pump_cb(lv_timer_t* /*t*/) { sim_input::pump(); }

// Closing the window (or Cmd-Q) must NOT let LVGL's own SDL driver tear itself
// down: on SDL_QUIT it calls SDL_Quit()+lv_deinit(), and on window-close it
// deletes the display -- all from inside lv_timer_handler(), while the app's run
// loop is still iterating, which then dereferences freed LVGL state and crashes.
//
// So we install an SDL event filter that DROPS the quit/close events before LVGL
// ever sees them (return 0), and instead raises SIGINT -- the exact graceful
// shutdown path Ctrl-C uses (the app's handler clears its `running` flag, the
// main loop exits, and everything is torn down in order). The filter runs during
// SDL_PumpEvents, so the events never reach LVGL's poll loop.
int quit_filter(void* /*user*/, SDL_Event* e) {
    const bool quit = e->type == SDL_QUIT ||
                      (e->type == SDL_WINDOWEVENT &&
                       e->window.event == SDL_WINDOWEVENT_CLOSE);
    if (quit) {
        std::raise(SIGINT);
        return 0;   // drop -> LVGL never tears itself down mid-loop
    }
    return 1;       // keep everything else (mouse/keyboard/etc.)
}
} // namespace

bool lvgl_display::init(Ili9341& lcd) {
    lv_init();

    lv_display_t* disp = lv_sdl_window_create(lcd.width(), lcd.height());
    lv_sdl_window_set_title(disp, "pi-Stomp (sim)");

    lv_sdl_mouse_create();
    lv_sdl_keyboard_create();

    // Turn window-close / Cmd-Q into a graceful SIGINT shutdown (see above).
    // lv_sdl_window_create() has already initialized SDL by this point.
    SDL_SetEventFilter(quit_filter, nullptr);

    // Drive the simulated encoders/footswitches from the keyboard, on the UI
    // thread (same thread that pumps SDL), every 5 ms.
    lv_timer_create(pump_cb, 5, nullptr);
    return true;
}
