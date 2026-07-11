// lvgl_display.h — bind LVGL to our ILI9341 panel.
//
// Sets up an LVGL display: a tick source (so LVGL knows wall-clock time), two
// partial draw buffers, and a flush callback that pushes rendered pixels to the
// LCD over SPI. After this, the rest of the app just builds LVGL widgets and
// calls lv_timer_handler() periodically.

#pragma once

class Ili9341;

namespace lvgl_display {
// Call once after lcd.init(). Runs lv_init() and wires the display to `lcd`.
// `lcd` must outlive all LVGL use (the flush callback holds a pointer to it).
bool init(Ili9341& lcd);
}
