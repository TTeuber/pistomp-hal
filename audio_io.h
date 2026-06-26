// audio_io.h — realtime duplex audio on the pi-Stomp's codec, as float buffers.
//
// The IQAudio Codec Zero is one of the board's hardware lines, so bringing it up
// belongs in the HAL alongside the LCD and the encoders. AudioIO owns everything
// that was copy-pasted into every NAM/pedalboard main(): opening capture+playback
// on the ALSA device, negotiating the period/buffer, the playback start-threshold,
// the SCHED_FIFO realtime thread, xrun recovery, and the S16<->float conversion.
// What it does NOT own is the DSP — that stays in YOUR callback. I/O in the HAL,
// signal processing in the app (so the HAL still pulls in no DSP framework).
//
// This header is deliberately ALSA-free: no <alsa/asoundlib.h>, no snd_pcm_t in
// sight (they hide behind a pimpl). libasound is an implementation detail of
// audio_io.cpp, so consumers don't take it into their headers, and if this ever
// wants to become its own library it's a file move, not a rewrite.
//
// Threading: open() and start() run on a normal thread (allocation is fine).
// The callback runs on the realtime thread and MUST stay realtime-safe — no
// malloc, locks, syscalls, or printf. Read controls via relaxed atomics.

#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace pistomp {

struct AudioConfig {
    const char* device     = "plughw:CARD=IQaudIOCODEC"; // the Codec Zero
    unsigned    rate       = 48000;
    unsigned    channels   = 2;
    unsigned    wantPeriod = 64;    // frames/block (~1.3 ms @48k); xrun-clean
    unsigned    wantBuffer = 256;   // ring size in frames (4 periods)
    int         maxFrames  = 8192;  // hard cap on the negotiated period
    int         rtPriority = 80;    // SCHED_FIFO priority for the audio thread

    // macOS simulator only (ignored by the ALSA backend): pick host devices by
    // name. Empty = system default. Resolved against playbackDevices()/
    // captureDevices() at open()/reopen() time; an unknown name falls back to
    // the default so a saved-but-unplugged device doesn't break startup.
    std::string playbackName;
    std::string captureName;
};

// A host audio device, as reported by the platform backend. macOS only; the
// ALSA backend returns no devices (the codec is fixed hardware).
struct AudioDeviceInfo {
    std::string name;
    bool        isDefault = false;
};

// The realtime DSP callback. `frames` valid samples of DEINTERLEAVED float audio
// are provided per channel: in[c][f], out[c][f] for c in [0, channels). Input is
// normalized to [-1, 1]; output is clamped to [-1, 1] on the way back to S16. The
// guitar arrives on channel 0 (Aux-Left) — fan your result to all out channels.
// Runs on the SCHED_FIFO thread: keep it allocation-, lock- and syscall-free.
using AudioCallback =
    std::function<void(const float* const* in, float* const* out, int frames)>;

class AudioIO {
public:
    AudioIO() = default;
    ~AudioIO();
    AudioIO(const AudioIO&) = delete;
    AudioIO& operator=(const AudioIO&) = delete;

    // Open + configure capture and playback on the codec. False on failure
    // (see lastError()). After this, period() holds the negotiated block size.
    bool open(const AudioConfig& cfg = {});

    // Negotiated frames-per-block, valid after open(). Apps that size a model to
    // the block (e.g. NAM's ResetAndPrewarm) read this before start().
    int period() const { return period_; }

    // Spawn the realtime audio thread running `cb`. False if the SCHED_FIFO
    // thread couldn't be created (needs rtprio: run as root or in the audio
    // group). PRECONDITION: block SIGINT in the caller first (realtime.h) so the
    // audio thread inherits the block and its read()/write() stay off EINTR.
    bool start(AudioCallback cb);

    // Ask the audio thread to finish its current block and join it. Idempotent;
    // also called by the destructor (which then drains + closes the device).
    void stop();

    // Host audio devices available for capture/playback (macOS simulator). The
    // ALSA backend returns empty — the Pi's codec is fixed. Safe to call before
    // or after open(); enumeration uses the backend's device context.
    std::vector<AudioDeviceInfo> playbackDevices();
    std::vector<AudioDeviceInfo> captureDevices();

    // Live device/format switch (macOS simulator): stop the running device,
    // tear it down, and re-open on `cfg` (new playback/capture names, rate, or
    // period). After this returns true, period() reflects the new block size —
    // the caller must re-prepare its DSP for that block and call start() again
    // with a callback sized to the new rate. False on failure (see lastError());
    // the previous device is left closed. The ALSA backend returns false.
    bool reopen(const AudioConfig& cfg);

    bool        running() const { return running_.load(std::memory_order_relaxed); }
    unsigned    xruns()   const { return xruns_.load(std::memory_order_relaxed); }
    // Non-null once the audio thread has died on a fatal ALSA error.
    const char* fatalError() const { return fatal_.load(std::memory_order_relaxed); }
    // Last setup-time error (open/configure/start), for diagnostics.
    const char* lastError() const { return lastError_; }

private:
    void run();                        // the realtime loop (defined in audio_io.cpp)
    static void* thread_entry(void*);  // pthread trampoline -> run()

    struct Impl;                       // hides snd_pcm_t*, pthread_t, buffers
    Impl*                    impl_ = nullptr;
    AudioCallback            cb_;
    AudioConfig              cfg_;
    std::atomic<bool>        running_{false};
    std::atomic<unsigned>    xruns_{0};
    std::atomic<const char*> fatal_{nullptr};
    const char*              lastError_ = nullptr;
    int                      period_ = 0;
};

} // namespace pistomp
