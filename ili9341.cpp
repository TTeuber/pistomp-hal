// ili9341.cpp — see ili9341.h. Direct port of sandbox/lcd_test.cpp, wrapped in
// a class so it can be reused and harvested. The SPI write chunking, software
// CS, DC toggling, and the Adafruit init sequence are unchanged from the
// known-good sandbox version.

#include "ili9341.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <gpiod.h>   // libgpiod v1 C API (project standard)

namespace {
// ILI9341 command opcodes we use.
enum : uint8_t {
    SWRESET = 0x01, SLPOUT = 0x11, DISPON = 0x29,
    CASET = 0x2A, PASET = 0x2B, RAMWR = 0x2C,
    MADCTL = 0x36, PIXFMT = 0x3A,
};
} // namespace

// ---- low-level SPI: data bytes only, DC already set by caller --------------
void Ili9341::write_data(const uint8_t* buf, size_t len) {
    // Hold the shared SPI lock (if any) for the WHOLE chip-select window — across
    // every chunk — so an ADC read on the other CS can't slip in while GPIO8 is
    // low (which would corrupt the panel; see set_spi_lock()).
    std::unique_lock<std::mutex> lk;
    if (spi_lock_) lk = std::unique_lock<std::mutex>(*spi_lock_);

    gpiod_line_set_value(dc_, 1);        // DC high = data
    gpiod_line_set_value(cs_, 0);        // assert CS
    const size_t CHUNK = 4096;           // spidev per-transfer cap
    while (len > 0) {
        size_t n = len < CHUNK ? len : CHUNK;
        ssize_t wrote = ::write(spi_fd_, buf, n);
        if (wrote < 0) { perror("spi write"); break; }
        buf += wrote;
        len -= wrote;
    }
    gpiod_line_set_value(cs_, 1);        // release CS
}

void Ili9341::write_command(uint8_t cmd) {
    std::unique_lock<std::mutex> lk;
    if (spi_lock_) lk = std::unique_lock<std::mutex>(*spi_lock_);

    gpiod_line_set_value(dc_, 0);        // DC low = command
    gpiod_line_set_value(cs_, 0);
    ::write(spi_fd_, &cmd, 1);
    gpiod_line_set_value(cs_, 1);
}

void Ili9341::write_command(uint8_t cmd, const uint8_t* params, size_t n) {
    write_command(cmd);
    if (n) write_data(params, n);
}

void Ili9341::hardware_reset() {
    gpiod_line_set_value(rst_, 1); usleep(5000);
    gpiod_line_set_value(rst_, 0); usleep(20000);
    gpiod_line_set_value(rst_, 1); usleep(150000);
}

void Ili9341::set_addr_window(int x0, int y0, int x1, int y1) {
    uint8_t c[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
    uint8_t p[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
    write_command(CASET, c, 4);
    write_command(PASET, p, 4);
    write_command(RAMWR);   // "pixels follow"
}

bool Ili9341::init(int rotation) {
    // 1. SPI bus.
    spi_fd_ = ::open("/dev/spidev0.0", O_RDWR);
    if (spi_fd_ < 0) { perror("open /dev/spidev0.0"); return false; }
    uint8_t mode = SPI_MODE_0, bits = 8;
    uint32_t speed = 24'000'000;
    ioctl(spi_fd_, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    // 2. GPIO control lines via libgpiod v1, opening the header bank by label.
    chip_ = gpiod_chip_open_by_label("pinctrl-rp1");
    if (!chip_) { perror("gpiod_chip_open_by_label pinctrl-rp1"); return false; }
    dc_  = gpiod_chip_get_line(chip_, 6);
    rst_ = gpiod_chip_get_line(chip_, 5);
    cs_  = gpiod_chip_get_line(chip_, 8);
    if (!dc_ || !rst_ || !cs_) { fprintf(stderr, "lcd get_line failed\n"); return false; }
    if (gpiod_line_request_output(dc_, "synth_demo_lcd", 0) < 0 ||
        gpiod_line_request_output(rst_, "synth_demo_lcd", 1) < 0 ||
        gpiod_line_request_output(cs_, "synth_demo_lcd", 1) < 0) {
        perror("lcd request_output (is something else holding GPIO5/6/8?)");
        return false;
    }

    // 3. Init sequence (Adafruit ILI9341 — magic numbers are vendor tuning).
    hardware_reset();
    write_command(SWRESET); usleep(150000);
    auto cmd = [&](uint8_t c, std::initializer_list<uint8_t> p) {
        std::vector<uint8_t> v(p);
        write_command(c, v.data(), v.size());
    };
    cmd(0xEF, {0x03, 0x80, 0x02});
    cmd(0xCF, {0x00, 0xC1, 0x30});
    cmd(0xED, {0x64, 0x03, 0x12, 0x81});
    cmd(0xE8, {0x85, 0x00, 0x78});
    cmd(0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02});
    cmd(0xF7, {0x20});
    cmd(0xEA, {0x00, 0x00});
    cmd(0xC0, {0x23});
    cmd(0xC1, {0x10});
    cmd(0xC5, {0x3E, 0x28});
    cmd(0xC7, {0x86});

    // MADCTL controls orientation + RGB/BGR order. Bit 0x08 = BGR panel.
    //   0x48 = portrait 240x320 (the sandbox default)
    //   0xE8 = landscape 320x240 rotated 180° (MV|MX|MY) — correct for the
    //          pi-Stomp's panel mounting. (0x28 is the other landscape, upside-down.)
    if (rotation == 1) {
        cmd(MADCTL, {0xE8});
        w_ = 320; h_ = 240;
    } else {
        cmd(MADCTL, {0x48});
        w_ = 240; h_ = 320;
    }

    cmd(PIXFMT, {0x55});             // 16 bits/pixel (RGB565)
    cmd(0xB1, {0x00, 0x18});
    cmd(0xB6, {0x08, 0x82, 0x27});
    cmd(0xF2, {0x00});
    cmd(0x26, {0x01});
    cmd(0xE0, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00});
    cmd(0xE1, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F});

    write_command(SLPOUT); usleep(120000);
    write_command(DISPON); usleep(100000);
    return true;
}

void Ili9341::flush(int x0, int y0, int x1, int y1, const uint8_t* data) {
    set_addr_window(x0, y0, x1, y1);
    size_t len = (size_t)(x1 - x0 + 1) * (size_t)(y1 - y0 + 1) * 2;
    write_data(data, len);
}

void Ili9341::fill(uint16_t color) {
    set_addr_window(0, 0, w_ - 1, h_ - 1);
    std::vector<uint8_t> frame((size_t)w_ * h_ * 2);
    uint8_t hi = color >> 8, lo = color & 0xFF;
    for (size_t i = 0; i < frame.size(); i += 2) { frame[i] = hi; frame[i + 1] = lo; }
    write_data(frame.data(), frame.size());
}

void Ili9341::close() {
    if (dc_)  gpiod_line_release(dc_);
    if (rst_) gpiod_line_release(rst_);
    if (cs_)  gpiod_line_release(cs_);
    if (chip_) gpiod_chip_close(chip_);
    if (spi_fd_ >= 0) ::close(spi_fd_);
    chip_ = nullptr; dc_ = rst_ = cs_ = nullptr; spi_fd_ = -1;
}
