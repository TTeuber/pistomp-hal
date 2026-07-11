// encoder.h — rotary-encoder quadrature decoder (NEW for this project).
//
// A rotary encoder has two switches, A ("data") and B ("clock"), arranged so
// that turning the shaft makes them open/close in a 4-step Gray-code sequence.
// The DIRECTION of the turn is encoded in the ORDER the two edges arrive:
//   CW : ...A then B...      CCW: ...B then A...
// One physical "click" (detent) of the knob is a full 4-state sequence. We can't
// just count edges — contact bounce produces spurious transitions — so we feed
// each (A,B) reading into a small state machine that only reports a step once it
// has seen a complete, valid detent sequence. This is the well-known table-based
// decoder from best-microcontroller-projects.com, the same one the reference
// pi-Stomp Python uses (pistomp/encoder.py).
//
// We POLL the two GPIO lines (like the footswitch ADC) rather than use edge
// interrupts: the caller's input thread reads each encoder a few hundred to a
// thousand times a second, which easily catches a hand-turned knob.
//
// Pins are BCM/GPIO numbers. pi-Stomp v3 encoder rotation pairs:
//   encoder 1: D=12, CLK=25   (default: pitch)
//   encoder 2: D=24, CLK=23   (default: level)

#pragma once

struct gpiod_chip;
struct gpiod_line;

class Encoder {
public:
    bool init(int d_pin, int clk_pin, const char* consumer);
    void close();

    // Read the lines once and advance the decoder. Returns +1 (CW), -1 (CCW),
    // or 0 (no completed detent this call). Call it repeatedly in a poll loop.
    int poll();

private:
    gpiod_chip* chip_ = nullptr;
    gpiod_line* d_   = nullptr;   // A / data
    gpiod_line* clk_ = nullptr;   // B / clock

    // Decoder state: a sliding window of the last reads.
    unsigned prev_next_code_ = 0;  // last 2 raw (A,B) readings, 2 bits each
    unsigned store_ = 0;           // last few valid codes, to spot a full detent
};
