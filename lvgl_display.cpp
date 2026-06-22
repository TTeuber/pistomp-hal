// lvgl_display.cpp — see lvgl_display.h.
//
// The two jobs of a platform "port" for LVGL:
//   1. tell LVGL the time (a millis() callback) so its animations/timers tick,
//   2. give it somewhere to draw and a way to ship those pixels to the panel.
//
// We use PARTIAL render mode: LVGL renders one dirty region at a time into a
// modest buffer (here ~1/10 of the screen), calls our flush_cb for each, and we
// blit just that rectangle. This is why a label update costs a few KB of SPI,
// not a whole 150 KB frame.

#include "lvgl_display.h"
#include "ili9341.h"

#include "lvgl.h"

#include <ctime>
#include <vector>

namespace {
Ili9341* g_lcd = nullptr;
std::vector<uint8_t> g_buf1, g_buf2;

// Monotonic milliseconds for LVGL's tick.
uint32_t millis_cb() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull);
}

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const int w = area->x2 - area->x1 + 1;
    const int h = area->y2 - area->y1 + 1;
    // LVGL hands us little-endian RGB565; the ILI9341 expects big-endian on the
    // wire. Swap in place (this is THE classic "colors look wrong" bug).
    lv_draw_sw_rgb565_swap(px_map, (uint32_t)w * (uint32_t)h);
    g_lcd->flush(area->x1, area->y1, area->x2, area->y2, px_map);
    lv_display_flush_ready(disp);   // tell LVGL the buffer is free again
}
} // namespace

bool lvgl_display::init(Ili9341& lcd) {
    g_lcd = &lcd;

    lv_init();
    lv_tick_set_cb(millis_cb);

    lv_display_t* disp = lv_display_create(lcd.width(), lcd.height());

    // Partial buffers: ~1/10 screen each, double-buffered so LVGL can render the
    // next region while the previous one flushes. RGB565 = 2 bytes/pixel.
    const size_t buf_px = (size_t)lcd.width() * lcd.height() / 10;
    const size_t buf_bytes = buf_px * 2;
    g_buf1.resize(buf_bytes);
    g_buf2.resize(buf_bytes);
    lv_display_set_buffers(disp, g_buf1.data(), g_buf2.data(), buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    return true;
}
