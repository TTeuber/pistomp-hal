// encoder_sim.cpp — desktop stand-in for encoder.cpp.
//
// Same Encoder contract; instead of decoding two gpiod lines it reads detents
// from the keyboard-fed sim_input bus. The logical encoder (Nav/E1/E2/E3) is
// resolved from the D pin at init() and stashed in store_ (the header's private
// decoder-state field, unused here) so the header stays unchanged.

#include "encoder.h"
#include "sim_input.h"

bool Encoder::init(int d_pin, int /*clk_pin*/, const char* /*consumer*/) {
    store_ = (unsigned)sim_input::encoder_for_dpin(d_pin);
    return true;
}

void Encoder::close() {}

int Encoder::poll() {
    return sim_input::encoder_step((sim_input::Enc)store_);
}
