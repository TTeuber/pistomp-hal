// lv_conf.h — minimal LVGL v9 configuration for the pi-Stomp synth demo.
//
// LVGL's lv_conf_internal.h provides a sensible #ifndef default for EVERY option,
// so this file only needs to override the handful that matter to us. Everything
// not set here (memory allocator, OS = none, log off, draw units, etc.) falls
// back to LVGL's defaults.
//
// Found by the build because demo/CMakeLists.txt adds this directory to LVGL's
// include path and defines LV_CONF_INCLUDE_SIMPLE.

#ifndef LV_CONF_H
#define LV_CONF_H

// 16-bit color to match the ILI9341's RGB565. (We byte-swap to big-endian in the
// flush callback; LVGL itself renders little-endian RGB565.)
#define LV_COLOR_DEPTH 16

// Fonts: the default 14px for labels, plus a big 28px for the MUTED/LIVE banner.
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif // LV_CONF_H
