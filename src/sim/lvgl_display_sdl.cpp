// lvgl_display_sdl.cpp — desktop stand-in for lvgl_display.cpp, with a simulated
// pi-Stomp front panel.
//
// On the Pi this binds LVGL to the ILI9341 over SPI. On the Mac it instead opens
// one SDL window sized like the whole pedal face and draws the control surface
// into it with plain shapes: the LCD, the three tweak encoders + nav encoder,
// the four footswitches, and the six NeoPixels.
//
// LVGL still thinks it is driving a 320x240 panel: we render it to an offscreen
// DIRECT-mode framebuffer, upload that to an SDL texture, and blit the texture
// into the LCD rectangle of the panel — so NOTHING in the app's UI code changes.
// The `lcd` argument is accepted only to size the LCD region; its pixels come
// from LVGL, not the stubbed Ili9341.
//
// Inputs: the keyboard still works (sim_input::pump, below), and the mouse drives
// the on-screen controls — drag an encoder vertically to turn it, click its
// centre for the push-switch, click/hold a footswitch to engage it. Both feed the
// same sim_input bus the *_sim.cpp drivers read.
//
// All of this runs on the UI thread via two LVGL timers (pump + render), serviced
// by the app's existing lv_timer_handler() loop — so no application code changes.

#include "pistomp/lvgl_display.h"
#include "pistomp/ili9341.h"
#include "pistomp/sim_input.h"

#include "lvgl.h"
#include <SDL2/SDL.h>

#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

using sim_input::Btn;
using sim_input::Enc;

// ---- panel geometry --------------------------------------------------------
// One window the size of the whole face; every control is a circle at a fixed
// centre. Mirrors the physical layout (LCD top-left; E1/E2/E3 + nav to its right;
// four footswitches across the bottom, each with a status LED above it; two spare
// LEDs top-right). The LCD rect is filled by LVGL; everything else we draw.
constexpr int kWinW = 900;
constexpr int kWinH = 560;

constexpr SDL_Rect kLcdRect{30, 30, 320, 240};  // matches Ili9341 landscape

struct Knob {
    Enc enc;
    Btn btn;        // push-switch; Btn::Count if the encoder has none (E3)
    bool hasBtn;
    int cx, cy, r;
};
const Knob kKnobs[] = {
    {Enc::E1,  Btn::Enc1,  true,  440, 120, 36},
    {Enc::E2,  Btn::Enc2,  true,  570, 120, 42},
    {Enc::E3,  Btn::Count, false, 700, 120, 42},
    {Enc::Nav, Btn::NavSw, true,  440, 225, 30},  // "EN" in the sketch
};

struct Switch {
    Btn btn;
    int cx, cy, r;  // footswitch
    int ledIdx;     // status LED index, drawn above it
    int ledCx, ledCy, ledR;
};
const Switch kSwitches[] = {
    {Btn::FS0, 120, 470, 46, 0, 120, 385, 16},
    {Btn::FS1, 320, 470, 46, 1, 320, 385, 16},
    {Btn::FS2, 540, 470, 46, 2, 540, 385, 16},
    {Btn::FS3, 760, 470, 46, 3, 760, 385, 16},
};
// The two top-right LEDs are input-level meters: index 5 = input 1 (L),
// index 4 = input 2 (R). Green = signal, orange = near clip, red = clipping.
struct Lamp { int idx, cx, cy, r; };
const Lamp kLamps[] = {
    {4, 790, 70, 18},
    {5, 850, 70, 22},
};

constexpr double kDegPerDetent = 18.0;  // knob notch turn per detent
constexpr int kPxPerDetent = 12;        // vertical drag pixels per detent
constexpr int kClickSlop = 5;           // drag under this many px counts as a click
constexpr Uint32 kClickHoldMs = 90;     // how long a click "presses" the switch

// ---- LVGL <-> SDL state ----------------------------------------------------
SDL_Window* g_win = nullptr;
SDL_Renderer* g_ren = nullptr;
SDL_Texture* g_lcdTex = nullptr;
std::vector<uint8_t> g_fb;        // LVGL DIRECT framebuffer (RGB565)
uint8_t* g_lcdPx = nullptr;       // last flushed frame (points into g_fb)
uint32_t g_lcdStride = 0;

// ---- mouse interaction state ----------------------------------------------
int g_dragKnob = -1;              // index into kKnobs, or -1
int g_dragStartY = 0;
int g_dragInjected = 0;           // detents injected so far this drag
bool g_dragMoved = false;
int g_pressSwitch = -1;           // kSwitches index held by the mouse, or -1
Uint32 g_btnReleaseAt[(int)Btn::Count] = {0};  // timed release for knob clicks

// ---- small drawing helpers -------------------------------------------------
void setColor(uint8_t r, uint8_t g, uint8_t b) {
    SDL_SetRenderDrawColor(g_ren, r, g, b, 0xFF);
}

void fillCircle(int cx, int cy, int r) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)std::lround(std::sqrt((double)r * r - (double)dy * dy));
        SDL_RenderDrawLine(g_ren, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

void strokeCircle(int cx, int cy, int r) {
    // Cheap outline: a thin annulus drawn as two filled circles in two colors is
    // overkill; just plot the rim with the midpoint algorithm.
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        const int pts[8][2] = {{x, y}, {y, x}, {-y, x}, {-x, y},
                               {-x, -y}, {-y, -x}, {y, -x}, {x, -y}};
        for (auto& p : pts) SDL_RenderDrawPoint(g_ren, cx + p[0], cy + p[1]);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

bool inCircle(int mx, int my, int cx, int cy, int r) {
    int dx = mx - cx, dy = my - cy;
    return dx * dx + dy * dy <= r * r;
}

void drawLed(int cx, int cy, int r, uint8_t cr, uint8_t cg, uint8_t cb);

// ---- the panel paint -------------------------------------------------------
void drawPanel() {
    // Background + a subtle inset border around the whole face.
    setColor(24, 24, 27);
    SDL_RenderClear(g_ren);
    setColor(60, 60, 66);
    SDL_Rect border{8, 8, kWinW - 16, kWinH - 16};
    SDL_RenderDrawRect(g_ren, &border);

    // LCD: blit the LVGL framebuffer, with a frame around it.
    if (g_lcdPx) {
        SDL_UpdateTexture(g_lcdTex, nullptr, g_lcdPx, (int)g_lcdStride);
        SDL_RenderCopy(g_ren, g_lcdTex, nullptr, &kLcdRect);
    }
    setColor(90, 90, 96);
    SDL_Rect lcdBorder{kLcdRect.x - 2, kLcdRect.y - 2, kLcdRect.w + 4, kLcdRect.h + 4};
    SDL_RenderDrawRect(g_ren, &lcdBorder);

    // Encoders: body, rim, and a notch line at the current angle. The notch
    // points up (12 o'clock) at zero and turns clockwise with the detent count.
    for (const auto& k : kKnobs) {
        bool pressed = k.hasBtn && sim_input::button_pressed(k.btn);
        if (pressed) setColor(110, 110, 122); else setColor(78, 78, 86);
        fillCircle(k.cx, k.cy, k.r);
        setColor(150, 150, 160);
        strokeCircle(k.cx, k.cy, k.r);

        double ang = (sim_input::encoder_total(k.enc) * kDegPerDetent - 90.0) *
                     M_PI / 180.0;
        int nx = k.cx + (int)std::lround(std::cos(ang) * (k.r - 6));
        int ny = k.cy + (int)std::lround(std::sin(ang) * (k.r - 6));
        setColor(235, 235, 240);
        SDL_RenderDrawLine(g_ren, k.cx, k.cy, nx, ny);
    }

    // Footswitches: a tall-button-ish disc that brightens while pressed, plus
    // its status LED above it.
    for (const auto& s : kSwitches) {
        bool pressed = sim_input::button_pressed(s.btn);
        if (pressed) setColor(140, 140, 150); else setColor(70, 70, 78);
        fillCircle(s.cx, s.cy, s.r);
        setColor(160, 160, 170);
        strokeCircle(s.cx, s.cy, s.r);
        strokeCircle(s.cx, s.cy, s.r - 6);

        uint8_t r, g, b;
        sim_input::led_rgb(s.ledIdx, r, g, b);
        drawLed(s.ledCx, s.ledCy, s.ledR, r, g, b);
    }

    // The two spare LEDs top-right.
    for (const auto& l : kLamps) {
        uint8_t r, g, b;
        sim_input::led_rgb(l.idx, r, g, b);
        drawLed(l.cx, l.cy, l.r, r, g, b);
    }

    SDL_RenderPresent(g_ren);
}

// Draw one NeoPixel: a dark socket, then the lit lens. The staged bytes are a
// real NeoPixel brightness (~40% of full at "on"), so scale up ~2x for screen
// punch while keeping the hue, and show a clear unlit state at zero.
void drawLed(int cx, int cy, int r, uint8_t cr, uint8_t cg, uint8_t cb) {
    setColor(30, 30, 34);
    fillCircle(cx, cy, r);
    if (cr || cg || cb) {
        auto boost = [](uint8_t v) -> uint8_t {
            int x = v * 2;
            return (uint8_t)(x > 255 ? 255 : x);
        };
        setColor(boost(cr), boost(cg), boost(cb));
        fillCircle(cx, cy, r - 3);
    }
    setColor(80, 80, 88);
    strokeCircle(cx, cy, r);
}

// ---- mouse handling --------------------------------------------------------
void onMouseDown(int mx, int my) {
    for (int i = 0; i < (int)(sizeof(kKnobs) / sizeof(kKnobs[0])); i++) {
        if (inCircle(mx, my, kKnobs[i].cx, kKnobs[i].cy, kKnobs[i].r)) {
            g_dragKnob = i;
            g_dragStartY = my;
            g_dragInjected = 0;
            g_dragMoved = false;
            return;
        }
    }
    for (int i = 0; i < (int)(sizeof(kSwitches) / sizeof(kSwitches[0])); i++) {
        if (inCircle(mx, my, kSwitches[i].cx, kSwitches[i].cy, kSwitches[i].r)) {
            g_pressSwitch = i;
            sim_input::set_mouse_button(kSwitches[i].btn, true);  // momentary
            return;
        }
    }
}

void onMouseMove(int my) {
    if (g_dragKnob < 0) return;
    int dy = g_dragStartY - my;               // up = clockwise
    if (std::abs(dy) >= kClickSlop) g_dragMoved = true;
    int want = dy / kPxPerDetent;
    int delta = want - g_dragInjected;
    if (delta) {
        sim_input::inject_encoder(kKnobs[g_dragKnob].enc, delta);
        g_dragInjected = want;
    }
}

void onMouseUp() {
    if (g_dragKnob >= 0) {
        const Knob& k = kKnobs[g_dragKnob];
        // A tap (no real drag) on a knob with a push-switch is a button click.
        if (!g_dragMoved && k.hasBtn) {
            sim_input::set_mouse_button(k.btn, true);
            g_btnReleaseAt[(int)k.btn] = SDL_GetTicks() + kClickHoldMs;
        }
        g_dragKnob = -1;
    }
    if (g_pressSwitch >= 0) {
        sim_input::set_mouse_button(kSwitches[g_pressSwitch].btn, false);
        g_pressSwitch = -1;
    }
}

// Release any knob-click buttons whose hold window has elapsed.
void serviceTimedReleases() {
    Uint32 now = SDL_GetTicks();
    for (int b = 0; b < (int)Btn::Count; b++) {
        if (g_btnReleaseAt[b] && now >= g_btnReleaseAt[b]) {
            sim_input::set_mouse_button((Btn)b, false);
            g_btnReleaseAt[b] = 0;
        }
    }
}

// ---- LVGL timers -----------------------------------------------------------
// Read the SDL keyboard into the sim bus (every 5 ms).
void pump_cb(lv_timer_t* /*t*/) { sim_input::pump(); }

// Poll SDL, update mouse-driven controls, and repaint the panel (every ~16 ms).
void render_cb(lv_timer_t* /*t*/) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            std::raise(SIGINT);  // graceful shutdown, same as Ctrl-C
            return;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_CLOSE) { std::raise(SIGINT); return; }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) onMouseDown(e.button.x, e.button.y);
            break;
        case SDL_MOUSEMOTION:
            onMouseMove(e.motion.y);
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) onMouseUp();
            break;
        }
    }
    serviceTimedReleases();
    drawPanel();
}

// ---- LVGL display flush ----------------------------------------------------
// DIRECT render mode: `px_map` is the whole framebuffer, complete on the last
// flush of a refresh. Stash it; drawPanel() uploads it to the texture.
void flush_cb(lv_display_t* disp, const lv_area_t* /*area*/, uint8_t* px_map) {
    if (lv_display_flush_is_last(disp)) g_lcdPx = px_map;
    lv_display_flush_ready(disp);
}

} // namespace

bool lvgl_display::init(Ili9341& lcd) {
    lv_init();

    SDL_Init(SDL_INIT_VIDEO);
    lv_tick_set_cb(SDL_GetTicks);

    g_win = SDL_CreateWindow("pi-Stomp (sim)", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, kWinW, kWinH, 0);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);

    const int w = lcd.width(), h = lcd.height();   // 320 x 240 (landscape)
    g_lcdTex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STREAMING, w, h);

    // LVGL renders to an offscreen full-size buffer (DIRECT mode); we composite
    // it. Color format is RGB565 (LV_COLOR_DEPTH 16, little-endian), which maps
    // straight onto SDL_PIXELFORMAT_RGB565 on this host — no byte-swap needed.
    lv_display_t* disp = lv_display_create(w, h);
    g_lcdStride = lv_draw_buf_width_to_stride(w, lv_display_get_color_format(disp));
    g_fb.assign((size_t)g_lcdStride * h, 0);
    lv_display_set_buffers(disp, g_fb.data(), nullptr, (uint32_t)g_fb.size(),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_timer_create(pump_cb, 5, nullptr);     // keyboard -> sim bus
    lv_timer_create(render_cb, 16, nullptr);  // mouse + paint (~60 fps)
    return true;
}
