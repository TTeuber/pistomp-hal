// leds.cpp — see leds.h. Byte layout reverse-engineered in the sandbox:
// exactly 24 bytes = 6 pixels x 4 bytes, order R,G,B,W, no brightness byte
// (brightness is the overlay's global multiplier). A write renders immediately.

#include "leds.h"

#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

bool Leds::init() {
    fd_ = ::open("/dev/leds0", O_WRONLY);
    if (fd_ < 0) { perror("open /dev/leds0 (run with sudo?)"); return false; }
    clear();
    return show();
}

void Leds::set(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < 0 || i >= COUNT) return;
    uint8_t* p = &frame_[i * 4];
    p[0] = r; p[1] = g; p[2] = b; p[3] = 0;
}

void Leds::clear() {
    for (int i = 0; i < COUNT; i++) set(i, 0, 0, 0);
}

bool Leds::show() {
    if (fd_ < 0) return false;
    if (pwrite(fd_, frame_, sizeof(frame_), 0) != (ssize_t)sizeof(frame_)) {
        perror("led write");
        return false;
    }
    return true;
}

void Leds::close() {
    if (fd_ >= 0) {
        clear();
        show();          // leave the strip dark
        ::close(fd_);
    }
    fd_ = -1;
}
