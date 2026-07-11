# Examples

Four small programs, each compiling **unchanged** for both backends: on the Pi
they drive the real pi-Stomp v3 hardware, on the Mac the HAL's simulator
backend stands in (SDL window, host audio, keyboard/mouse controls). There is
no `#ifdef` in any example — platform selection happens entirely inside the
HAL's CMake.

Build them from the repo root:

```
cmake -G Ninja -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DPISTOMP_HAL_BUILD_EXAMPLES=ON
cmake --build build
```

Binaries land in `build/examples/<name>/<name>`.

## passthrough — the audio contract

Open the codec, copy the guitar (`in[0]`, Aux-Left) to both outputs in the
realtime callback, print the negotiated rate/period and the xrun count on
Ctrl-C. The callback is the whole API surface an app's DSP sees.

On the Mac it binds your default input/output devices (the Pi's codec is fixed
hardware, so the same code is a no-op branch there). **Heads up:** default
input is often the built-in mic — speakers + mic = feedback, use headphones.

## controls — the vended control surface

No screen: `Board` vends the nav encoder and all four footswitches, `Leds`
drives the NeoPixels. Turning the nav encoder cycles the hue; each footswitch
toggles its LED. Every event prints to stdout.

This one is **hardware-first**: in the simulator, keyboard/mouse input rides
the SDL window's LVGL timer, so a screenless program gets no sim input (it
still builds and runs everywhere — it just has nothing to listen to on the
Mac). Run it on the Pi.

## display — LVGL on the panel

Bring up the ILI9341 through `Board::openLcd` + `lvgl_display::init`, draw a
label and an animated `lv_bar`, pump `lv_timer_handler()` until Ctrl-C. On the
Mac this opens the simulator window; on the Pi it's the real 320×240 SPI
panel.

## mini_pedal — a complete tiny pedal

Mono guitar in → stereo out. Encoder 1 sets output gain (−30…+12 dB, 1 dB per
detent), footswitch 1 is a true-bypass toggle with its NeoPixel as the
indicator (green = engaged, dark = bypassed), and the screen shows gain,
bypass state, and a live output meter.

The point of this example is the threading discipline: the realtime callback
reads the gain/bypass controls via relaxed atomics and publishes a block peak;
all LVGL, LED, and control polling happen on the ~50 Hz UI loop. No locks, no
allocation, no LVGL calls on the audio thread.

Simulator keys: `Q`/`W` turn the gain encoder, `1` stomps the bypass switch —
or drag the on-screen encoder / click the on-screen footswitch.
