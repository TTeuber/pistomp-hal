// gpio_button.h — a single momentary push-switch on a real GPIO line.
//
// Some pi-Stomp controls aren't on the MCP3008 ADC like the footswitches: the
// tweak-encoder push-switches ride dedicated GPIO lines (enc1 = GPIO16, enc2 =
// GPIO26). This is the GPIO counterpart to Footswitch — same "was just pressed"
// edge semantics and hold timing, but read straight off a libgpiod line instead
// of an SPI ADC channel. Wiring matches the encoders: input with internal
// pull-up, so idle reads high and a press pulls the line to ground (active-low).
//
// Pins are BCM/GPIO numbers.

#pragma once

struct gpiod_chip;
struct gpiod_line;

class GpioButton {
public:
    bool init(int pin, const char* consumer);
    void close();

    // Poll the line once. Returns true exactly on the released->pressed edge.
    bool poll_pressed_edge();

    // Current state (no edge). Use for hold / long-press timing.
    bool is_pressed();

private:
    gpiod_chip* chip_ = nullptr;
    gpiod_line* line_ = nullptr;
    bool was_pressed_ = false;
};
