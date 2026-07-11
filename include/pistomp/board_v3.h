// board_v3.h — the pi-Stomp v3 control-surface map (single source of truth).
//
// Every pin number, ADC channel and press threshold for the v3 board lives HERE,
// once, instead of being re-derived in each program. These are immutable facts of
// the hardware (BCM/GPIO numbering, matching pistomp/pistomptre.py), not choices a
// program makes — so a new program should pull them from here rather than copy a
// block of constants and risk it drifting. The behavioural wrapper that turns
// these into ready-to-poll, lock-wired controls is Board (see board.h).
//
// The control surface is deliberately heterogeneous, which is exactly why it pays
// to centralise it:
//   * 4 rotary encoders, 3 of them with a push-switch.
//   * Two of those push-switches are raw GPIO lines (enc1, enc2); the navigation
//     encoder's push-switch is instead an ADC channel with a threshold; enc3 has
//     no switch at all.
//   * 4 footswitches are NOT on GPIO — they ride MCP3008 ADC channels 0..3.
// A caller should never have to know which substrate a given control lives on;
// that is Board's job.

#pragma once

namespace pistomp::board {

// A rotary encoder's two quadrature lines (BCM/GPIO). See Encoder.
struct EncoderPins { int d, clk; };

// A control read through the MCP3008 SPI ADC: which channel, and the 0..1023
// reading at/below which we call it "pressed". See Footswitch.
struct AdcChannel { int channel, threshold; };

// ---- rotary encoders -------------------------------------------------------
inline constexpr EncoderPins kNavEnc{17, 4};   // navigation / cursor
inline constexpr EncoderPins kEnc1{12, 25};    // tweak knob 1
inline constexpr EncoderPins kEnc2{24, 23};    // tweak knob 2
inline constexpr EncoderPins kEnc3{22, 27};    // tweak knob 3 (no push-switch)

// ---- encoder push-switches -------------------------------------------------
inline constexpr int kEnc1SwGpio = 16;   // raw GPIO line, active-low
inline constexpr int kEnc2SwGpio = 26;   // raw GPIO line (no consumer yet)

// The navigation encoder's push-switch is an ADC channel, NOT a GPIO line. It
// rests high (~1022) and is pulled to ~0 when pressed, so <=512 means pressed.
//
// IMPORTANT (hard-won): the legacy pistomp.py names channel 7 for this switch,
// but on the v3 board ch7 is a *knob* resting at ~513 — right on the threshold —
// which previously caused a phantom press that auto-launched the first program at
// boot. The correct channel is 4. Do not "fix" this back to 7.
inline constexpr AdcChannel kNavSw{4, 512};

// ---- footswitches ----------------------------------------------------------
// FS0..3 on MCP3008 channels 0..3. The 800 threshold is the pi-Stomp footswitch
// convention (a firmer, lower-impedance switch than the encoder pushes).
inline constexpr int kFootswitchCount = 4;
inline constexpr AdcChannel kFootsw[kFootswitchCount]{
    {0, 800}, {1, 800}, {2, 800}, {3, 800},
};

// ---- analog input-level detectors ------------------------------------------
// Each audio input has a pre-gain analog peak-detector on its own MCP3008
// channel, read for the input-level meter LEDs (see pistomp-hal/input_level.*).
// Matches pistomptre.py: EXPRESSION=5, CLIP_L=6, CLIP_R=7. These carry a 0..1023
// *level*, not a press, so the threshold field is unused (kept 0).
inline constexpr AdcChannel kExpression{5, 0};   // expression pedal (no consumer yet)
inline constexpr AdcChannel kClipL{6, 0};        // input 1 / Left  -> LED index 5
inline constexpr AdcChannel kClipR{7, 0};        // input 2 / Right -> LED index 4

} // namespace pistomp::board
