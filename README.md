# pistomp-hal

A JUCE-free C++17 hardware abstraction layer for the [pi-Stomp](https://www.treefallsound.com/) v3 guitar-pedal platform (Raspberry Pi 5). It packages the reusable hardware drivers behind clean headers so a pedal application links one static library and gets the whole control surface, display, and realtime audio path.

Extracted from a larger pedal project where it drives a live multi-effect worship pedalboard.

## What's inside

| Subsystem | Header | Hardware |
|---|---|---|
| Rotary encoders | `encoder.h` | Quadrature encoders via libgpiod |
| Footswitches | `footswitch.h` | Momentary switches read over SPI |
| GPIO buttons | `gpio_button.h` | Push-switches on raw GPIO lines |
| Status LEDs | `leds.h` | NeoPixel (timed-serial RGB) state lights |
| Display | `ili9341.h` + `lvgl_display.h` | ILI9341 SPI TFT driven by LVGL v9 |
| Input metering | `input_level.h` | ADC input-level reads |
| Audio | `audio_io.h` | Realtime duplex I/O on the codec (ALSA), SCHED_FIFO thread, xrun recovery |
| Board wiring | `board.h` / `board_v3.h` | Single source of truth for the v3 pin map |

Design rules:

- **No DSP framework.** The HAL is about hardware lines; effects/DSP live in the app. `audio_io.h` is deliberately ALSA-free (pimpl) — the app supplies one callback and never sees ALSA.
- **The board owns the footguns.** Several peripherals share the SPI bus; `Board` owns the shared SPI mutex and vends fully-wired controls (`openLcd`, `openNavEncoder`, `openFootswitch`, …), so a consumer can't forget the lock and corrupt the bus.
- **Headers are the contract.** Each subsystem's `.cpp` is selected per platform in CMake — porting is a file move, not a rewrite.

## Platforms

- **Linux / Raspberry Pi** — the real drivers: libgpiod v1, spidev, ALSA.
- **macOS simulator** — the same headers backed by a desktop simulator: the LVGL UI in an SDL2 window, audio through miniaudio/CoreAudio, controls fed from the keyboard. Lets an entire pedal app run and be developed with no Pi attached.

## Using it

Consume via CMake FetchContent, pinned to a tag:

```cmake
include(FetchContent)
FetchContent_Declare(pistomp_hal
  GIT_REPOSITORY https://github.com/TTeuber/pistomp-hal.git
  GIT_TAG v0.1.0)
FetchContent_MakeAvailable(pistomp_hal)

target_link_libraries(my_pedal PRIVATE pistomp_hal)
```

Linking `pistomp_hal` PUBLICly propagates LVGL, the driver headers, and (on Linux) libgpiod — no extra wiring in the consumer.

To hack on the HAL and an app together, point the fetch at a local checkout:

```
cmake -B build -DFETCHCONTENT_SOURCE_DIR_PISTOMP_HAL=/path/to/pistomp-hal
```

### Sketch

```cpp
#include "board.h"
#include "audio_io.h"

pistomp::Board board;

Encoder nav;
board.openNavEncoder(nav, "my-app");

pistomp::AudioIO audio;
audio.open();   // negotiate the codec; audio.period() now holds the block size
audio.start([](const float* const* in, float* const* out, int frames) {
    // guitar arrives on in[0]; write out[0]/out[1]. Realtime thread — no locks,
    // no allocation.
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < frames; ++i)
            out[ch][i] = in[0][i];
});
```

## Building standalone

```
# macOS (simulator backend):  brew install cmake ninja sdl2
# Linux (Pi backend):         apt install cmake ninja-build pkg-config libgpiod-dev libasound2-dev
cmake -G Ninja -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build
```

`CMAKE_POLICY_VERSION_MINIMUM=3.5` lets the fetched LVGL configure under CMake 4.x.

Pi binaries are best built in a `debian:bookworm` arm64 container so they link against the same glibc the Pi runs — on Apple Silicon that container runs natively, no cross-compiler needed.

## License

MIT — see [LICENSE](LICENSE).
