// mini_pedal — a tiny but complete pedal: everything wired together.
//
//   * mono guitar in -> stereo out
//   * encoder 1 sets output level (gain), in dB
//   * footswitch 0 is a true-bypass toggle, with its NeoPixel as the LED
//   * the level, bypass state and a live output meter show on the LVGL screen
//
// REALTIME DISCIPLINE (non-negotiable). The audio callback runs on the realtime
// thread: no locks, no allocation, no printf, and no LVGL calls in it. It shares
// state with the UI thread ONLY through std::atomic — it reads gain/bypass and
// writes a peak level that the UI polls. Everything slow (LVGL, LED I/O, control
// polling) stays on the main/UI thread.

#include "pistomp/audio_io.h"
#include "pistomp/board.h"
#include "pistomp/encoder.h"
#include "pistomp/footswitch.h"
#include "pistomp/ili9341.h"
#include "pistomp/leds.h"
#include "pistomp/lvgl_display.h"
#include "pistomp/realtime.h"

#include "lvgl.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <string>
#include <thread>
#include <vector>

namespace {
volatile std::sig_atomic_t g_stop = 0;
void on_sigint(int) { g_stop = 1; }

// The audio<->UI channel. Relaxed atomics: no ordering dependency between them.
std::atomic<float> g_gain{1.0f};    // UI writes (linear), callback reads
std::atomic<bool>  g_bypass{false}; // UI writes, callback reads
std::atomic<float> g_peak{0.0f};    // callback writes (block peak), UI reads

std::string default_device(const std::vector<pistomp::AudioDeviceInfo>& ds) {
    for (const auto& d : ds) if (d.isDefault) return d.name;
    return ds.empty() ? std::string{} : ds.front().name;
}
} // namespace

int main() {
    // --- codec: open, then (macOS) bind the host default devices; no-op on the
    //     Pi's fixed codec, where open() already bound it. ---
    pistomp::AudioIO audio;
    pistomp::AudioConfig cfg;
    if (!audio.open(cfg)) return 1;
    auto caps = audio.captureDevices();
    auto plays = audio.playbackDevices();
    if (!caps.empty() || !plays.empty()) {
        cfg.captureName = default_device(caps);
        cfg.playbackName = default_device(plays);
        if (!audio.reopen(cfg)) return 1;
    }

    // --- control surface + screen (UI thread = main; build widgets first) ---
    pistomp::Board board;
    Encoder gainEnc;
    Footswitch bypassSw;
    Leds leds;
    board.openEnc1(gainEnc, "mini_pedal");
    board.openFootswitch(bypassSw, 0);
    leds.init();

    Ili9341 lcd;
    if (!board.openLcd(lcd, /*rotation=*/1)) return 1;
    lvgl_display::init(lcd);

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101014), 0);
    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "mini pedal");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t* status = lv_label_create(screen);   // gain + bypass text
    lv_obj_set_style_text_color(status, lv_color_hex(0xC0C0D0), 0);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t* meter = lv_bar_create(screen);       // output level
    lv_obj_set_size(meter, 260, 20);
    lv_obj_align(meter, LV_ALIGN_CENTER, 0, 30);
    lv_bar_set_range(meter, 0, 100);

    // --- go realtime. Lock memory, block SIGINT before the audio thread, then
    //     start the callback (the DSP). ---
    pistomp::realtime::lock_all_memory();   // best-effort; warns-and-continues
    pistomp::realtime::block_signal(SIGINT);

    bool ok = audio.start([](const float* const* in, float* const* out, int n) {
        const float gain = g_gain.load(std::memory_order_relaxed);
        const bool bypass = g_bypass.load(std::memory_order_relaxed);
        float peak = 0.0f;
        for (int f = 0; f < n; f++) {
            float s = in[0][f] * (bypass ? 1.0f : gain);   // guitar on channel 0
            out[0][f] = out[1][f] = s;                     // fan to stereo
            float a = std::fabs(s);
            if (a > peak) peak = a;
        }
        g_peak.store(peak, std::memory_order_relaxed);
    });
    if (!ok) return 1;

    pistomp::realtime::unblock_signal(SIGINT);
    std::signal(SIGINT, on_sigint);

    leds.set(0, 0, 120, 0); leds.show();   // start engaged: LED green

    // --- UI loop (~50 Hz): poll controls, mirror state to the LEDs + screen,
    //     and service LVGL. No realtime constraints here. ---
    int gainDb = 0;         // -30..+12 dB, stepped by the encoder
    float shown = 0.0f;     // smoothed meter value
    while (!g_stop && audio.running()) {
        // Encoder -> gain. Recompute the linear gain the callback reads.
        if (int d = gainEnc.poll()) {
            gainDb += d;
            if (gainDb > 12) gainDb = 12; else if (gainDb < -30) gainDb = -30;
            g_gain.store(std::pow(10.0f, gainDb / 20.0f), std::memory_order_relaxed);
        }
        // Footswitch -> true-bypass toggle, with the NeoPixel as its indicator.
        if (bypassSw.poll_pressed_edge()) {
            bool bypass = !g_bypass.load(std::memory_order_relaxed);
            g_bypass.store(bypass, std::memory_order_relaxed);
            if (bypass) leds.set(0, 0, 0, 0);          // bypassed: dark
            else        leds.set(0, 0, 120, 0);         // engaged: green
            leds.show();
        }

        // Meter: read the callback's peak, fall slowly so it's readable.
        float peak = g_peak.load(std::memory_order_relaxed);
        shown = peak > shown ? peak : shown * 0.85f;
        lv_bar_set_value(meter, (int)(shown * 100.0f), LV_ANIM_OFF);
        lv_label_set_text_fmt(status, "%+d dB   %s", gainDb,
                              g_bypass.load(std::memory_order_relaxed) ? "BYPASS"
                                                                       : "ENGAGED");

        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    audio.stop();
    leds.close();
    gainEnc.close();
    bypassSw.close();
    lcd.fill(0x0000);
    lcd.close();
    return 0;
}
