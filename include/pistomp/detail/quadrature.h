// quadrature.h — table-based rotary-encoder decoder, extracted from
// src/linux/encoder.cpp so it can be unit-tested on any host (std only, no
// platform includes). Behaviour is identical to the driver: the caller feeds
// each raw (A,B) reading in; a completed detent is reported once, contact
// bounce is filtered by the transition table.
//
// The algorithm is the classic decoder from best-microcontroller-projects.com,
// the same one the reference pi-Stomp Python uses (pistomp/encoder.py):
//   * prev_next_code holds a 4-bit window [old A,B | new A,B].
//   * rot_enc_table gates that window to the 8 legal Gray-code steps (1) and
//     rejects the 8 illegal jumps / no-changes (0) that bounce produces.
//   * store accumulates legal codes; a full detent ends on a known 2-code tail
//     (0x2b = CW, 0x17 = CCW).

#pragma once

namespace pistomp {
namespace detail {

// Advance the decoder one reading. a = A/data line, b = B/clock line (true =
// high). prev_next_code and store are the caller-owned decoder state (both
// start 0). Returns +1 (CW detent), -1 (CCW detent), or 0 (no completed detent
// / illegal transition).
inline int quadrature_step(unsigned& prev_next_code, unsigned& store,
                           bool a, bool b) {
    // 16 possible 4-bit transitions (old A,B -> new A,B). 1 = a legal Gray-code
    // step, 0 = an illegal jump (contact bounce) to ignore.
    static const int rot_enc_table[16] = {0,1,1,0, 1,0,0,1, 1,0,0,1, 0,1,1,0};

    // Shift the previous reading up and OR in the current (A,B) bits, keeping a
    // 4-bit window = [old A,B | new A,B].
    prev_next_code <<= 2;
    if (a) prev_next_code |= 0x02;   // A -> bit 1
    if (b) prev_next_code |= 0x01;   // B -> bit 0
    prev_next_code &= 0x0f;

    int direction = 0;
    if (rot_enc_table[prev_next_code]) {       // legal transition only
        // Accumulate valid codes; a complete detent is a known 2-code tail.
        store <<= 4;
        store |= prev_next_code;
        if ((store & 0xff) == 0x2b) direction = 1;    // CW  full sequence
        if ((store & 0xff) == 0x17) direction = -1;   // CCW full sequence
    }
    return direction;
}

// Convenience wrapper owning the two state words, for callers (and tests) that
// want an object rather than loose state. step() is the same code path the
// driver runs via quadrature_step().
class QuadratureDecoder {
public:
    // +1 CW, -1 CCW, 0 none. See quadrature_step().
    int step(bool a, bool b) { return quadrature_step(prev_next_code_, store_, a, b); }
    void reset() { prev_next_code_ = 0; store_ = 0; }

private:
    unsigned prev_next_code_ = 0;
    unsigned store_ = 0;
};

} // namespace detail
} // namespace pistomp
