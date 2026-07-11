// display — LVGL on the ILI9341 panel (an SDL window in the macOS sim).
//
// Board::openLcd brings the panel up (and shares the SPI-bus lock); then
// lvgl_display::init() runs lv_init() and binds LVGL to it. After that this is
// ordinary LVGL: build a couple of widgets, install a timer to animate one, and
// pump lv_timer_handler() on this (the UI) thread. Nothing here is platform
// specific — the same code drives the 320x240 SPI panel on the Pi.

#include "pistomp/board.h"
#include "pistomp/ili9341.h"
#include "pistomp/lvgl_display.h"

#include "lvgl.h"

#include <chrono>
#include <csignal>
#include <thread>

namespace {
volatile std::sig_atomic_t g_stop = 0;
void on_sigint(int) { g_stop = 1; }

// Animate the bar: bounce a value between 0 and 100. The bar comes in as the
// timer's user data, so no globals. LVGL timers run inside lv_timer_handler().
void animate_cb(lv_timer_t* t) {
    auto* bar = static_cast<lv_obj_t*>(lv_timer_get_user_data(t));
    static int v = 0, dir = 2;
    v += dir;
    if (v >= 100 || v <= 0) dir = -dir;
    lv_bar_set_value(bar, v, LV_ANIM_ON);
}
} // namespace

int main() {
    pistomp::Board board;

    Ili9341 lcd;
    if (!board.openLcd(lcd, /*rotation=*/1)) {   // 320x240 landscape
        return 1;
    }
    lvgl_display::init(lcd);   // runs lv_init() and wires LVGL to the panel

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101014), 0);

    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "pistomp-hal display");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t* bar = lv_bar_create(screen);
    lv_obj_set_size(bar, 240, 24);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
    lv_bar_set_range(bar, 0, 100);

    lv_timer_create(animate_cb, 20, bar);   // ~50 Hz sweep

    std::signal(SIGINT, on_sigint);          // the SDL window's close = SIGINT

    // The LVGL loop: service timers (animation, and in the sim the keyboard pump
    // + panel repaint) until Ctrl-C / window close.
    while (!g_stop) {
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    lcd.fill(0x0000);   // blank the panel on exit (no-op in the sim)
    lcd.close();
    return 0;
}
