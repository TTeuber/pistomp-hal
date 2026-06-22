// ili9341.h — minimal ILI9341 LCD driver for the pi-Stomp v3.
//
// Harvested from sandbox/lcd_test.cpp (which has the full teaching comments).
// The contract here is deliberately tiny — just what a graphics layer (LVGL)
// needs: bring the panel up, and blit a rectangle of RGB565 pixels.
//
// Pins / bus (pi-Stomp v3, verified by the sandbox bring-up):
//   SPI  = /dev/spidev0.0 @ 24 MHz
//   DC   = GPIO6   (data/command select, we toggle)
//   RST  = GPIO5   (reset, we toggle)
//   CS   = GPIO8   (chip-select, driven in SOFTWARE — the kernel's CE0 was
//                   remapped to GPIO0 by the boot overlay; see the memory notes)
//
// IMPORTANT: pixel bytes handed to flush() must be BIG-ENDIAN RGB565 (high byte
// first), which is what the ILI9341 expects on the wire. LVGL renders
// little-endian, so the caller byte-swaps before calling us (see lvgl_display.cpp).

#pragma once
#include <cstdint>
#include <cstddef>
#include <mutex>

struct gpiod_chip;
struct gpiod_line;

class Ili9341 {
public:
    // rotation: 0 = portrait 240x320, 1 = landscape 320x240 (the pi-Stomp's
    // intended orientation). After init(), width()/height() reflect the choice.
    bool init(int rotation = 1);
    void close();

    int width()  const { return w_; }
    int height() const { return h_; }

    // Share an SPI-bus lock with any OTHER device on the same SPI controller
    // (here: the footswitch ADC on /dev/spidev0.1). When set, the LCD holds it
    // for the whole chip-select window of each transfer, so another thread's ADC
    // read can't interleave while our software CS (GPIO8) is asserted. Optional:
    // leave unset for standalone use (e.g. the sandbox), where nothing contends.
    void set_spi_lock(std::mutex* m) { spi_lock_ = m; }

    // Blit a rectangle. (x0,y0)..(x1,y1) inclusive. `data` is
    // (x1-x0+1)*(y1-y0+1)*2 bytes of big-endian RGB565.
    void flush(int x0, int y0, int x1, int y1, const uint8_t* data);

    // Fill the whole screen one color (RGB565, native uint16 — used for
    // clear-on-exit; not on the hot path).
    void fill(uint16_t color);

private:
    void write_command(uint8_t cmd);
    void write_command(uint8_t cmd, const uint8_t* params, size_t n);
    void write_data(const uint8_t* buf, size_t len);
    void set_addr_window(int x0, int y0, int x1, int y1);
    void hardware_reset();

    int spi_fd_ = -1;
    gpiod_chip* chip_ = nullptr;
    gpiod_line* dc_  = nullptr;   // GPIO6
    gpiod_line* rst_ = nullptr;   // GPIO5
    gpiod_line* cs_  = nullptr;   // GPIO8 (software CS)
    std::mutex* spi_lock_ = nullptr;   // optional shared SPI-bus lock
    int w_ = 0, h_ = 0;
};
