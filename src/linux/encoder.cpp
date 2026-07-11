// encoder.cpp — see encoder.h. The decode logic is a faithful C++ port of
// pistomp/encoder.py's _process_gpios(), which itself adapts the classic
// table-based rotary decoder. Annotated so the algorithm is understandable.

#include "pistomp/encoder.h"
#include "pistomp/detail/quadrature.h"   // shared, host-tested decode logic

#include <cstdio>
#include <gpiod.h>   // libgpiod v1 C API

bool Encoder::init(int d_pin, int clk_pin, const char* consumer) {
    chip_ = gpiod_chip_open_by_label("pinctrl-rp1");
    if (!chip_) { perror("encoder gpiod_chip_open_by_label"); return false; }
    d_   = gpiod_chip_get_line(chip_, d_pin);
    clk_ = gpiod_chip_get_line(chip_, clk_pin);
    if (!d_ || !clk_) { fprintf(stderr, "encoder get_line failed\n"); return false; }

    // Inputs with internal PULL-UP: the encoder switches pull each line to GND
    // when closed, so idle reads high, active reads low (same convention as the
    // reference gpiozero Button).
    if (gpiod_line_request_input_flags(d_, consumer, GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP) < 0 ||
        gpiod_line_request_input_flags(clk_, consumer, GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP) < 0) {
        perror("encoder request_input (lines already held?)");
        return false;
    }
    return true;
}

int Encoder::poll() {
    // Read the two lines once and advance the shared decoder. a = A/data,
    // b = B/clock (idle high, active low, filtered by the transition table).
    bool a = gpiod_line_get_value(d_);
    bool b = gpiod_line_get_value(clk_);
    return pistomp::detail::quadrature_step(prev_next_code_, store_, a, b);
}

void Encoder::close() {
    if (d_)   gpiod_line_release(d_);
    if (clk_) gpiod_line_release(clk_);
    if (chip_) gpiod_chip_close(chip_);
    chip_ = nullptr; d_ = clk_ = nullptr;
}
