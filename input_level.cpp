// input_level.cpp — Pi backend for InputLevel. See input_level.h.
//
// MCP3008 single-ended reads on /dev/spidev0.1 (bus 0, CE1), same transaction as
// footswitch.cpp -- the two clip detectors live on ADC ch 6 and 7. One fd serves
// both channels (the channel is selected per-transfer in the command bytes).

#include "input_level.h"
#include "board_v3.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

namespace pistomp {

namespace {
constexpr uint32_t SPEED = 1'000'000;   // 1 MHz, within MCP3008 spec at 3.3V
// Map meter index 0/1 -> ADC channel (input 1 = L = ch6, input 2 = R = ch7).
constexpr int kChan[InputLevel::COUNT] = { board::kClipL.channel, board::kClipR.channel };
} // namespace

bool InputLevel::init(std::mutex* spiLock) {
  spiLock_ = spiLock;
  fd_ = ::open("/dev/spidev0.1", O_RDWR);   // bus 0, CE1 = the MCP3008
  if (fd_ < 0) { perror("open /dev/spidev0.1"); available_ = false; return false; }
  uint8_t mode = SPI_MODE_0, bits = 8;
  uint32_t speed = SPEED;
  ioctl(fd_, SPI_IOC_WR_MODE, &mode);
  ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits);
  ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
  available_ = true;
  return true;
}

int InputLevel::read(int i) {
  if (fd_ < 0 || i < 0 || i >= COUNT) return -1;
  const int channel = kChan[i];
  // start bit, (8|channel)<<4 selects the channel in single-ended mode, third
  // byte clocks the answer back (SPI is full-duplex).
  uint8_t tx[3] = {1, (uint8_t)((8 + channel) << 4), 0};
  uint8_t rx[3] = {0, 0, 0};
  struct spi_ioc_transfer tr;
  memset(&tr, 0, sizeof(tr));
  tr.tx_buf = (unsigned long)tx;
  tr.rx_buf = (unsigned long)rx;
  tr.len = 3;
  tr.speed_hz = SPEED;
  tr.bits_per_word = 8;
  // Hold the shared SPI lock for the transfer so it can't overlap an LCD transfer
  // holding its software CS low.
  {
    std::unique_lock<std::mutex> lk;
    if (spiLock_) lk = std::unique_lock<std::mutex>(*spiLock_);
    if (ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) < 0) { perror("spi transfer"); return -1; }
  }
  return ((rx[1] & 0x03) << 8) | rx[2];     // 10-bit result
}

void InputLevel::close() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
  available_ = false;
}

} // namespace pistomp
