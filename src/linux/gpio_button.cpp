// gpio_button.cpp — see gpio_button.h. Mirrors encoder.cpp's libgpiod usage:
// open the RP1 pinctrl chip, request the line as an input with a pull-up, then
// poll its level. Active-low (pressed = 0) like the reference gpiozero Button.

#include "pistomp/gpio_button.h"
#include "pistomp/detail/debounce.h"   // shared edge logic

#include <cstdio>
#include <gpiod.h>   // libgpiod v1 C API

bool GpioButton::init(int pin, const char* consumer) {
    chip_ = gpiod_chip_open_by_label("pinctrl-rp1");
    if (!chip_) { perror("gpio_button gpiod_chip_open_by_label"); return false; }
    line_ = gpiod_chip_get_line(chip_, pin);
    if (!line_) { fprintf(stderr, "gpio_button get_line failed\n"); return false; }
    if (gpiod_line_request_input_flags(line_, consumer,
                                       GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP) < 0) {
        perror("gpio_button request_input (line already held?)");
        return false;
    }
    return true;
}

bool GpioButton::is_pressed() {
    return line_ && gpiod_line_get_value(line_) == 0;   // active-low
}

bool GpioButton::poll_pressed_edge() {
    return pistomp::detail::rising_edge(was_pressed_, is_pressed());
}

void GpioButton::close() {
    if (line_) gpiod_line_release(line_);
    if (chip_) gpiod_chip_close(chip_);
    chip_ = nullptr; line_ = nullptr;
}
