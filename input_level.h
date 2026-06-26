// input_level.h — read the pi-Stomp's analog input-level detectors.
//
// The v3 board feeds each audio input through a pre-gain analog peak-detector
// wired to the MCP3008 SPI ADC: ch 6 = input 1 (Left, CLIP_L), ch 7 = input 2
// (Right, CLIP_R). This is the level source the input-level meter LEDs use on
// hardware (see pedalboard/input_vu.h for the meter policy).
//
// There are two builds, selected by CMake (mirroring leds.cpp / leds_stub.cpp):
//   * input_level.cpp (Pi)      -- real MCP3008 reads over /dev/spidev0.1.
//   * input_level_stub.cpp (Mac)-- no ADC; available() is false so the app falls
//     back to the digital audio peak instead.
//
// The ADC shares SPI0 with the LCD, so reads take the board's shared SPI lock
// (wired automatically by Board::openInputLevel).

#pragma once
#include <mutex>

namespace pistomp {

class InputLevel {
public:
  static constexpr int COUNT = 2;   // 0 = ch6 / input1 / L, 1 = ch7 / input2 / R

  // Open the ADC and adopt the shared SPI lock. Returns false on the Pi if the
  // SPI device can't be opened; always a no-op (returns false) in the sim.
  bool init(std::mutex* spiLock);
  void close();

  // True when a real ADC backs this object (Pi). False in the sim -> the caller
  // should use the digital audio peak instead.
  bool available() const { return available_; }

  // Raw 10-bit reading (0..1023) for channel i (0 or 1); -1 on error / unavailable.
  int read(int i);

private:
  int fd_ = -1;
  bool available_ = false;
  std::mutex* spiLock_ = nullptr;
};

} // namespace pistomp
