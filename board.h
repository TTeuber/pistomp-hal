// board.h — the pi-Stomp v3 hardware front door.
//
// Board turns the raw wiring facts in board_v3.h into ready-to-use controls, and
// owns the one piece of shared state every program otherwise had to wire by hand:
// the SPI0 bus lock.
//
// WHY THE LOCK LIVES HERE: SPI0 is physically shared by the LCD (/dev/spidev0.0,
// software chip-select on GPIO8) and the MCP3008 ADC (/dev/spidev0.1) that backs
// the footswitches and the nav push-switch. If an ADC read interleaves inside the
// LCD's CS window the bus corrupts. The fix is a single mutex shared by every
// device on the bus — and every program was previously declaring that mutex and
// remembering to inject it into the LCD and each ADC device by hand. Forgetting
// one call produced intermittent, hard-to-trace corruption with no compile error.
// Board makes that impossible: the open*() helpers that touch the SPI bus wire the
// lock automatically, so a caller cannot forget.
//
// Board does NOT own the lifetime of the driver objects — callers still declare
// their own Encoder/Footswitch/etc and call close() on them. This keeps the
// launcher's fork/exec dance working: it closes every control before exec'ing a
// child (libgpiod lines are exclusive) and re-open*()s them afterwards. Board
// (and its mutex) simply outlives those close/reopen cycles.
//
// One Board instance per process; its mutex is shared across threads (e.g. the
// pedalboard reads the ADC on its input thread while the UI thread drives the LCD).

#pragma once
#include <mutex>

#include "board_v3.h"
#include "encoder.h"
#include "footswitch.h"
#include "gpio_button.h"
#include "ili9341.h"
#include "input_level.h"

namespace pistomp {

class Board {
public:
    // ---- SPI-bus devices: these wire the shared lock for you ----------------

    // Bring up the LCD and share the bus lock. Returns false if the panel init
    // fails. `rotation`: 1 = landscape 320x240 (the pi-Stomp orientation).
    bool openLcd(Ili9341& lcd, int rotation = 1) {
        if (!lcd.init(rotation)) return false;
        lcd.set_spi_lock(&spi_lock_);
        return true;
    }

    // The navigation encoder's push-switch (ADC ch4, threshold 512).
    bool openNavSwitch(Footswitch& sw) {
        if (!sw.init(board::kNavSw.channel, board::kNavSw.threshold)) return false;
        sw.set_spi_lock(&spi_lock_);
        return true;
    }

    // Footswitch index 0..3 (ADC ch0..3). Out-of-range returns false.
    bool openFootswitch(Footswitch& fs, int index) {
        if (index < 0 || index >= board::kFootswitchCount) return false;
        const auto& c = board::kFootsw[index];
        if (!fs.init(c.channel, c.threshold)) return false;
        fs.set_spi_lock(&spi_lock_);
        return true;
    }

    // The two analog input-level detectors (ADC ch6/7), for the meter LEDs.
    // Returns false in the sim (no ADC) -> InputLevel::available() stays false.
    bool openInputLevel(InputLevel& lvl) { return lvl.init(&spi_lock_); }

    // ---- GPIO controls: no shared lock needed, but vended here so a program
    //      never has to know the pin numbers --------------------------------
    bool openNavEncoder(Encoder& e, const char* consumer) { return openEnc(e, board::kNavEnc, consumer); }
    bool openEnc1(Encoder& e, const char* consumer)        { return openEnc(e, board::kEnc1, consumer); }
    bool openEnc2(Encoder& e, const char* consumer)        { return openEnc(e, board::kEnc2, consumer); }
    bool openEnc3(Encoder& e, const char* consumer)        { return openEnc(e, board::kEnc3, consumer); }

    bool openEnc1Button(GpioButton& b, const char* consumer) { return b.init(board::kEnc1SwGpio, consumer); }
    bool openEnc2Button(GpioButton& b, const char* consumer) { return b.init(board::kEnc2SwGpio, consumer); }

    // Escape hatch for any future device on SPI0 that Board doesn't model yet.
    std::mutex& spiLock() { return spi_lock_; }

private:
    static bool openEnc(Encoder& e, board::EncoderPins p, const char* consumer) {
        return e.init(p.d, p.clk, consumer);
    }

    std::mutex spi_lock_;   // guards SPI0 (LCD CS window vs MCP3008 ADC reads)
};

} // namespace pistomp
