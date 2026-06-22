// encoder.cpp — see encoder.h. The decode logic is a faithful C++ port of
// pistomp/encoder.py's _process_gpios(), which itself adapts the classic
// table-based rotary decoder. Annotated so the algorithm is understandable.

#include "encoder.h"

#include <cstdio>
#include <gpiod.h>   // libgpiod v1 C API

namespace {
// 16 possible 4-bit transitions (old A,B -> new A,B). 1 = a legal Gray-code
// step, 0 = an illegal jump (i.e. contact bounce) we should ignore. Indexing
// the table with the 4-bit code filters bounce for free.
const int rot_enc_table[16] = {0,1,1,0, 1,0,0,1, 1,0,0,1, 0,1,1,0};
} // namespace

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
    // Shift the previous reading up and OR in the current (A,B) bits, keeping a
    // 4-bit window = [old A,B | new A,B].
    prev_next_code_ <<= 2;
    if (gpiod_line_get_value(d_))   prev_next_code_ |= 0x02;   // A -> bit 1
    if (gpiod_line_get_value(clk_)) prev_next_code_ |= 0x01;   // B -> bit 0
    prev_next_code_ &= 0x0f;

    int direction = 0;
    if (rot_enc_table[prev_next_code_]) {       // legal transition only
        // Accumulate valid codes; a complete detent is a known 2-code tail.
        store_ <<= 4;
        store_ |= prev_next_code_;
        if ((store_ & 0xff) == 0x2b) direction = 1;    // CW  full sequence
        if ((store_ & 0xff) == 0x17) direction = -1;   // CCW full sequence
    }
    return direction;
}

void Encoder::close() {
    if (d_)   gpiod_line_release(d_);
    if (clk_) gpiod_line_release(clk_);
    if (chip_) gpiod_chip_close(chip_);
    chip_ = nullptr; d_ = clk_ = nullptr;
}
