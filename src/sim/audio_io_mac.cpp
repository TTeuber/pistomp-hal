// audio_io_mac.cpp — the miniaudio (CoreAudio) implementation behind audio_io.h.
//
// The macOS counterpart to audio_io_alsa.cpp / audio_io.cpp. Same AudioIO
// contract: open a duplex device, then start() spins a realtime callback that
// hands the DSP deinterleaved float buffers. miniaudio owns the realtime thread
// and (with fixed-size callbacks, its default) delivers exactly periodSize
// frames per call, so the DSP sees the same block size it was prepared with —
// just like the ALSA period on the Pi. No SCHED_FIFO/pthread plumbing here:
// CoreAudio manages the audio thread's priority.
//
// miniaudio gives us float32 directly (vs ALSA's S16), so there's no integer
// conversion — only deinterleave on the way in and interleave on the way out.
//
// Unlike the Pi's fixed codec, the Mac has many host devices, so this backend
// keeps an explicit ma_context (instead of letting ma_device_init create a
// throwaway one). The context both enumerates devices for the settings UI and
// lets us re-open on a chosen device by name — live, via reopen().

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "pistomp/audio_io.h"

#include <vector>

namespace pistomp {
namespace {

// Userdata behind ma_device.pUserData. Defined in an anonymous namespace (not as
// AudioIO::Impl) so the C-style data callback can name it without tripping over
// AudioIO::Impl being a private nested type.
struct MacDev {
    ma_device device;
    bool      inited = false;       // a ma_device is live (any direction)
    bool      capActive = false;    // capture direction is open
    bool      playActive = false;   // playback direction is open
    unsigned  ch = 2;
    AudioCallback cb;

    // Deinterleaved float views handed to the DSP callback. Sized to maxFrames
    // up front so the realtime callback never allocates.
    std::vector<std::vector<float>> inF, outF;
    std::vector<const float*>       inPtrs;
    std::vector<float*>             outPtrs;

    void alloc(unsigned c, int maxFrames) {
        ch = c;
        inF.assign(c, std::vector<float>(maxFrames, 0.0f));
        outF.assign(c, std::vector<float>(maxFrames, 0.0f));
        inPtrs.resize(c);
        outPtrs.resize(c);
        for (unsigned i = 0; i < c; i++) {
            inPtrs[i]  = inF[i].data();
            outPtrs[i] = outF[i].data();
        }
    }
};

void data_cb(ma_device* dev, void* pOut, const void* pIn, ma_uint32 frames) {
    auto* d = static_cast<MacDev*>(dev->pUserData);
    const unsigned ch = d->ch;
    const float*   in  = static_cast<const float*>(pIn);
    float*         out = static_cast<float*>(pOut);

    // interleaved float -> deinterleaved float views.
    for (ma_uint32 f = 0; f < frames; f++)
        for (unsigned c = 0; c < ch; c++)
            d->inF[c][f] = in ? in[ch * f + c] : 0.0f;

    if (d->cb) d->cb(d->inPtrs.data(), d->outPtrs.data(), (int)frames);

    // deinterleaved float -> interleaved float out, clamped to [-1, 1]. pOut is
    // null on a capture-only device (output set to "No Output") -- nothing to do.
    if (!out) return;
    for (ma_uint32 f = 0; f < frames; f++)
        for (unsigned c = 0; c < ch; c++) {
            float v = d->outF[c][f];
            if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
            out[ch * f + c] = v;
        }
}

} // namespace

struct AudioIO::Impl {
    ma_context ctx;
    bool       ctxInited = false;
    MacDev     d;

    // Resolve a device name (from AudioConfig) to its ma_device_id within the
    // given direction's enumerated list. Returns true and fills `out` on a hit;
    // false on an empty name (= "No device" for that direction) or a name that's
    // no longer present (unplugged). The caller leaves that direction CLOSED on
    // false -- it is never substituted with the system default.
    bool resolve(ma_device_type type, const std::string& name, ma_device_id& out) {
        if (name.empty() || !ctxInited) return false;
        ma_device_info* infos = nullptr;
        ma_uint32 count = 0;
        ma_result r = (type == ma_device_type_playback)
            ? ma_context_get_devices(&ctx, &infos, &count, nullptr, nullptr)
            : ma_context_get_devices(&ctx, nullptr, nullptr, &infos, &count);
        if (r != MA_SUCCESS) return false;
        for (ma_uint32 i = 0; i < count; i++)
            if (name == infos[i].name) { out = infos[i].id; return true; }
        return false;
    }

    // Build + init the ma_device for `cfg`, opening only the direction(s) whose
    // chosen device is currently present: both -> duplex, one -> playback/capture
    // only, neither -> no device at all (idle; the app stays up and re-attaches
    // when a device reappears). Sets d.inited / d.capActive / d.playActive.
    // Returns false (and *err) only on a genuine ma_device_init failure.
    bool initDevice(const AudioConfig& cfg, const char** err) {
        d.inited = d.capActive = d.playActive = false;

        ma_device_id playId{}, capId{};
        const bool playWanted = resolve(ma_device_type_playback, cfg.playbackName, playId);
        const bool capWanted  = resolve(ma_device_type_capture,  cfg.captureName,  capId);

        if (!playWanted && !capWanted)
            return true; // idle: nothing present to open -> silence, no feedback

        const ma_device_type type = (playWanted && capWanted) ? ma_device_type_duplex
                                   : playWanted               ? ma_device_type_playback
                                                              : ma_device_type_capture;
        ma_device_config c = ma_device_config_init(type);
        c.sampleRate = cfg.rate;
        if (capWanted) {
            c.capture.format    = ma_format_f32;
            c.capture.channels  = cfg.channels;
            c.capture.pDeviceID = &capId;
        }
        if (playWanted) {
            c.playback.format    = ma_format_f32;
            c.playback.channels  = cfg.channels;
            c.playback.pDeviceID = &playId;
        }
        c.periodSizeInFrames = cfg.wantPeriod;   // fixed-size callbacks (miniaudio default)
        c.dataCallback       = data_cb;
        c.pUserData          = &d;

        if (ma_device_init(&ctx, &c, &d.device) != MA_SUCCESS) {
            if (err) *err = "miniaudio: ma_device_init failed";
            return false;
        }
        d.inited     = true;
        d.capActive  = capWanted;
        d.playActive = playWanted;
        return true;
    }
};

namespace {

// Enumerate one direction into the public AudioDeviceInfo list. Shared by
// playbackDevices()/captureDevices().
std::vector<AudioDeviceInfo> enumerate(ma_context* ctx, bool ctxInited,
                                       ma_device_type type) {
    std::vector<AudioDeviceInfo> out;
    if (!ctxInited) return out;
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    ma_result r = (type == ma_device_type_playback)
        ? ma_context_get_devices(ctx, &infos, &count, nullptr, nullptr)
        : ma_context_get_devices(ctx, nullptr, nullptr, &infos, &count);
    if (r != MA_SUCCESS) return out;
    out.reserve(count);
    for (ma_uint32 i = 0; i < count; i++)
        out.push_back({ infos[i].name, infos[i].isDefault != 0 });
    return out;
}

} // namespace

AudioIO::~AudioIO() {
    stop();
    if (impl_) {
        if (impl_->d.inited) ma_device_uninit(&impl_->d.device);
        if (impl_->ctxInited) ma_context_uninit(&impl_->ctx);
        delete impl_;
        impl_ = nullptr;
    }
}

bool AudioIO::open(const AudioConfig& cfg) {
    cfg_ = cfg;
    impl_ = new Impl();
    MacDev& d = impl_->d;
    d.alloc(cfg.channels, cfg.maxFrames);

    if (ma_context_init(nullptr, 0, nullptr, &impl_->ctx) != MA_SUCCESS) {
        lastError_ = "miniaudio: ma_context_init failed";
        return false;
    }
    impl_->ctxInited = true;

    // Open whichever of the chosen devices are present (or none -> idle). A
    // chosen-but-absent device does NOT block startup; the reconnect poll
    // re-attaches it when it reappears.
    const char* err = nullptr;
    if (!impl_->initDevice(cfg, &err)) { lastError_ = err; return false; }

    // With fixed-size callbacks the DSP block equals the requested period.
    period_ = (int)cfg.wantPeriod;
    if (period_ <= 0 || period_ > cfg.maxFrames) period_ = cfg.maxFrames;
    return true;
}

bool AudioIO::start(AudioCallback cb) {
    if (!impl_) { lastError_ = "miniaudio: device not open"; return false; }
    impl_->d.cb = std::move(cb);
    // Idle (no device present/selected): there's nothing to start, but report
    // success and "running" so the app stays up and can re-attach later.
    if (!impl_->d.inited) { running_.store(true); return true; }
    if (ma_device_start(&impl_->d.device) != MA_SUCCESS) {
        lastError_ = "miniaudio: ma_device_start failed";
        return false;
    }
    running_.store(true);
    return true;
}

void AudioIO::stop() {
    if (!impl_) return;
    if (impl_->d.inited && running_.load())
        ma_device_stop(&impl_->d.device);
    running_.store(false);
}

std::vector<AudioDeviceInfo> AudioIO::playbackDevices() {
    if (!impl_) return {};
    return enumerate(&impl_->ctx, impl_->ctxInited, ma_device_type_playback);
}

std::vector<AudioDeviceInfo> AudioIO::captureDevices() {
    if (!impl_) return {};
    return enumerate(&impl_->ctx, impl_->ctxInited, ma_device_type_capture);
}

bool AudioIO::reopen(const AudioConfig& cfg) {
    if (!impl_ || !impl_->ctxInited) { lastError_ = "miniaudio: not open"; return false; }
    MacDev& d = impl_->d;

    // Tear the live device down (the callback is preserved in d.cb so the caller
    // doesn't have to re-supply it; start() will rebind whatever it's given).
    stop();
    if (d.inited) { ma_device_uninit(&d.device); d.inited = false; }

    cfg_ = cfg;
    // Only the block size and channel count affect the preallocated buffers;
    // re-alloc when either changes so the RT callback stays allocation-free.
    if (cfg.channels != d.ch || d.inF.empty() || d.inF[0].size() < (size_t)cfg.maxFrames)
        d.alloc(cfg.channels, cfg.maxFrames);

    // Re-open whichever chosen devices are present now (or none -> idle). Same
    // per-direction logic as open(); used by the settings switch and by the
    // reconnect poll when a device is plugged/unplugged.
    const char* err = nullptr;
    if (!impl_->initDevice(cfg, &err)) { lastError_ = err; return false; }

    period_ = (int)cfg.wantPeriod;
    if (period_ <= 0 || period_ > cfg.maxFrames) period_ = cfg.maxFrames;
    return true;
}

bool AudioIO::captureActive()  const { return impl_ && impl_->d.capActive; }
bool AudioIO::playbackActive() const { return impl_ && impl_->d.playActive; }

} // namespace pistomp
