// footswitch.cpp — see footswitch.h. SPI transaction logic unchanged from
// sandbox/adc_read.cpp; adds edge detection on top.

#include "footswitch.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

namespace {
constexpr uint32_t SPEED = 1'000'000;   // 1 MHz, within MCP3008 spec at 3.3V
} // namespace

bool Footswitch::init(int channel, int threshold) {
    channel_ = channel;
    threshold_ = threshold;
    fd_ = ::open("/dev/spidev0.1", O_RDWR);   // bus 0, CE1 = the MCP3008
    if (fd_ < 0) { perror("open /dev/spidev0.1"); return false; }
    uint8_t mode = SPI_MODE_0, bits = 8;
    uint32_t speed = SPEED;
    ioctl(fd_, SPI_IOC_WR_MODE, &mode);
    ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    return true;
}

int Footswitch::read_raw() {
    // MCP3008 single-ended read: start bit, (8|channel)<<4 selects channel in
    // single-ended mode, third byte clocks the answer back (SPI is full-duplex).
    uint8_t tx[3] = {1, (uint8_t)((8 + channel_) << 4), 0};
    uint8_t rx[3] = {0, 0, 0};
    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long)tx;
    tr.rx_buf = (unsigned long)rx;
    tr.len = 3;
    tr.speed_hz = SPEED;
    tr.bits_per_word = 8;
    // Take the shared SPI lock (if any) for the duration of the transfer so it
    // can't overlap an LCD transfer holding its software CS low.
    {
        std::unique_lock<std::mutex> lk;
        if (spi_lock_) lk = std::unique_lock<std::mutex>(*spi_lock_);
        if (ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) < 0) { perror("spi transfer"); return -1; }
    }
    return ((rx[1] & 0x03) << 8) | rx[2];     // 10-bit result
}

bool Footswitch::poll_pressed_edge() {
    int v = read_raw();
    if (v < 0) return false;
    bool pressed = (v <= threshold_);
    bool edge = pressed && !was_pressed_;     // released -> pressed transition
    was_pressed_ = pressed;
    return edge;
}

bool Footswitch::is_pressed() {
    int v = read_raw();
    if (v < 0) return false;
    return v <= threshold_;
}

void Footswitch::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}
