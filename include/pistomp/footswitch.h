// footswitch.h — read one pi-Stomp switch via the MCP3008 SPI ADC.
//
// Ported from sandbox/adc_read.cpp. The 4 footswitches aren't on GPIO — they
// ride channels 0..3 of an MCP3008 (8-channel, 10-bit SPI ADC) on
// /dev/spidev0.1. We poll the channel, threshold the 0..1023 reading, and turn
// it into a clean "was just pressed" edge so each stomp toggles state exactly
// once (not once per poll while held down).
//
// The same class also reads the encoder push-switches, which ride higher ADC
// channels with a different press threshold (e.g. the navigation encoder is on
// channel 7, pressed below ~512) — hence init() takes an optional threshold.

#pragma once
#include <mutex>

class Footswitch {
public:
    // channel 0..7; pressed when the 0..1023 reading is <= threshold. The 800
    // default is the pi-Stomp footswitch convention; pass 512 for the encoder
    // push-switches.
    bool init(int channel, int threshold = 800);
    void close();

    // Share the SPI-bus lock with the LCD (both ride SPI0). See
    // Ili9341::set_spi_lock(). Optional; unset = no locking (standalone use).
    void set_spi_lock(std::mutex* m) { spi_lock_ = m; }

    // Poll the ADC once. Returns true exactly on the released->pressed edge.
    // Call repeatedly in the input loop.
    bool poll_pressed_edge();

    // Current thresholded state (no edge). Use for hold / long-press timing.
    bool is_pressed();

private:
    int read_raw();           // 0..1023, or -1 on error

    int fd_ = -1;
    int channel_ = 0;
    int threshold_ = 800;
    bool was_pressed_ = false;
    std::mutex* spi_lock_ = nullptr;   // optional shared SPI-bus lock
};
