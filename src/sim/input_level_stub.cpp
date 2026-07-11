// input_level_stub.cpp — macOS simulator backend for InputLevel. See input_level.h.
//
// The sim has no ADC. available() stays false so the app meters the digital audio
// peak instead (computed in the audio callback); read() never produces a value.

#include "pistomp/input_level.h"

namespace pistomp {

bool InputLevel::init(std::mutex* spiLock) {
  spiLock_ = spiLock;
  available_ = false;
  return false;
}

int InputLevel::read(int /*i*/) { return -1; }

void InputLevel::close() {}

} // namespace pistomp
