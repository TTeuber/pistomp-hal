// passthrough — the "hello, audio" of the HAL.
//
// Open the codec, then in the realtime callback copy the guitar input to both
// output channels. That is the whole signal path: I/O lives in AudioIO, the DSP
// (here, a copy) lives in this callback. Runs until Ctrl-C.
//
// The callback runs on the realtime thread: no malloc, locks, syscalls or
// printf in it. It only reads the input buffer and writes the output buffer.

#include "pistomp/audio_io.h"
#include "pistomp/realtime.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {
volatile std::sig_atomic_t g_stop = 0;
void on_sigint(int) { g_stop = 1; }

// macOS has many host devices, so pick the system default by name (DAW-style:
// an explicit choice, never a silent substitution). On the Pi these lists are
// empty — the codec is fixed hardware and open() already bound it — so we skip
// the reopen and the already-open codec stands.
std::string default_device(const std::vector<pistomp::AudioDeviceInfo>& ds) {
    for (const auto& d : ds) if (d.isDefault) return d.name;
    return ds.empty() ? std::string{} : ds.front().name;
}
} // namespace

int main() {
    pistomp::AudioIO audio;
    pistomp::AudioConfig cfg;   // codec defaults: 48 kHz, 2ch, 64-frame period
    if (!audio.open(cfg)) {
        std::fprintf(stderr, "audio open failed: %s\n", audio.lastError());
        return 1;
    }

    // On the Mac, bind the host's default input/output; no-op on the Pi codec.
    auto caps = audio.captureDevices();
    auto plays = audio.playbackDevices();
    if (!caps.empty() || !plays.empty()) {
        cfg.captureName = default_device(caps);
        cfg.playbackName = default_device(plays);
        if (!audio.reopen(cfg)) {
            std::fprintf(stderr, "audio reopen failed: %s\n", audio.lastError());
            return 1;
        }
    }

    const int period = audio.period();
    std::printf("passthrough: %u Hz, %d-frame period. Ctrl-C to stop.\n",
                cfg.rate, period);

    // Block SIGINT before the audio thread spawns so it inherits the block and
    // its read()/write() never return EINTR; main alone fields Ctrl-C.
    pistomp::realtime::block_signal(SIGINT);

    // in[0] is the guitar (Aux-Left). Fan it to every output channel. The input
    // buffer is always valid (zero-filled when no capture device is present).
    bool ok = audio.start([](const float* const* in, float* const* out, int n) {
        for (int f = 0; f < n; f++) {
            out[0][f] = in[0][f];
            out[1][f] = in[0][f];
        }
    });
    if (!ok) {
        std::fprintf(stderr, "audio start failed: %s\n", audio.lastError());
        return 1;
    }

    pistomp::realtime::unblock_signal(SIGINT);
    std::signal(SIGINT, on_sigint);

    while (!g_stop && audio.running())
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    audio.stop();
    std::printf("\nstopped (%u xruns).\n", audio.xruns());
    return 0;
}
