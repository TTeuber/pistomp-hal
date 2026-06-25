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

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "audio_io.h"

#include <vector>

namespace pistomp {
namespace {

// Userdata behind ma_device.pUserData. Defined in an anonymous namespace (not as
// AudioIO::Impl) so the C-style data callback can name it without tripping over
// AudioIO::Impl being a private nested type.
struct MacDev {
    ma_device device;
    bool      inited = false;
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

    // deinterleaved float -> interleaved float out, clamped to [-1, 1].
    for (ma_uint32 f = 0; f < frames; f++)
        for (unsigned c = 0; c < ch; c++) {
            float v = d->outF[c][f];
            if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
            out[ch * f + c] = v;
        }
}

} // namespace

struct AudioIO::Impl {
    MacDev d;
};

AudioIO::~AudioIO() {
    stop();
    if (impl_) {
        if (impl_->d.inited) ma_device_uninit(&impl_->d.device);
        delete impl_;
        impl_ = nullptr;
    }
}

bool AudioIO::open(const AudioConfig& cfg) {
    cfg_ = cfg;
    impl_ = new Impl();
    MacDev& d = impl_->d;
    d.alloc(cfg.channels, cfg.maxFrames);

    ma_device_config c = ma_device_config_init(ma_device_type_duplex);
    c.sampleRate         = cfg.rate;
    c.capture.format     = ma_format_f32;
    c.capture.channels   = cfg.channels;
    c.playback.format    = ma_format_f32;
    c.playback.channels  = cfg.channels;
    c.periodSizeInFrames = cfg.wantPeriod;   // fixed-size callbacks (miniaudio default)
    c.dataCallback       = data_cb;
    c.pUserData          = &d;

    if (ma_device_init(nullptr, &c, &d.device) != MA_SUCCESS) {
        lastError_ = "miniaudio: ma_device_init failed (no audio device?)";
        return false;
    }
    d.inited = true;

    // With fixed-size callbacks the DSP block equals the requested period.
    period_ = (int)cfg.wantPeriod;
    if (period_ <= 0 || period_ > cfg.maxFrames) period_ = cfg.maxFrames;
    return true;
}

bool AudioIO::start(AudioCallback cb) {
    if (!impl_ || !impl_->d.inited) { lastError_ = "miniaudio: device not open"; return false; }
    impl_->d.cb = std::move(cb);
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

} // namespace pistomp
