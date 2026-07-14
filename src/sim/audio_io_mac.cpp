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
//
// Mixed devices (e.g. interface in, speakers out) run on two unsynchronized
// clocks, which miniaudio's duplex mode bridges with a plain ring buffer and no
// drift compensation — the clocks' ppm difference slips the buffer until it
// wraps, an audible click every few minutes. So for that case this backend does
// what DAWs do: compose a PRIVATE CoreAudio aggregate device from the two
// chosen devices (visible only to this process), clock it from the capture
// side, and let CoreAudio drift-correct playback. miniaudio then opens the
// aggregate as one ordinary single-clock duplex device.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "pistomp/audio_io.h"

#include <CoreAudio/CoreAudio.h>

#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Renamed in the macOS 12 SDK; same on-the-wire value ("master").
#ifndef kAudioAggregateDeviceMainSubDeviceKey
#define kAudioAggregateDeviceMainSubDeviceKey kAudioAggregateDeviceMasterSubDeviceKey
#endif

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

    // Device-frame layout for the callback. An aggregate concatenates ALL
    // sub-device streams in list order (capture device first), so the device
    // frame can be wider than the DSP's `ch` channels and the slice we want
    // sits at an offset: e.g. Fender(2in/2out) + speakers(2out) = a 4-channel
    // playback frame where the speakers are channels 2-3. Plain devices are
    // the degenerate case: stride == ch, offset 0.
    unsigned  inStride  = 2;  // interleaved channels per device capture frame
    unsigned  inOffset  = 0;  // first channel of the capture device's inputs
    unsigned  inAvail   = 2;  // its input channel count (mono -> duplicated)
    unsigned  outStride = 2;  // interleaved channels per device playback frame
    unsigned  outOffset = 0;  // first channel of the playback device's outputs
    unsigned  outAvail  = 2;  // its output channel count

    // PISTOMP_TEST_TONE=1 (read at open/reopen): replace the DSP's output with
    // a -14 dB 440 Hz sine, for verifying the output routing independently of
    // the input and the effect chain.
    bool      testTone  = false;
    float     tonePhase = 0.0f;

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

    // interleaved device frame -> deinterleaved float views, reading the
    // capture device's slice (a mono source is duplicated across DSP channels).
    if (in && d->inAvail > 0) {
        for (ma_uint32 f = 0; f < frames; f++)
            for (unsigned c = 0; c < ch; c++) {
                const unsigned s = d->inOffset + (c < d->inAvail ? c : d->inAvail - 1);
                d->inF[c][f] = in[d->inStride * f + s];
            }
    } else {
        for (unsigned c = 0; c < ch; c++)
            memset(d->inF[c].data(), 0, frames * sizeof(float));
    }

    if (d->cb) d->cb(d->inPtrs.data(), d->outPtrs.data(), (int)frames);

    if (d->testTone) {
        const float inc = 440.0f / (float)dev->sampleRate;
        for (ma_uint32 f = 0; f < frames; f++) {
            const float v = 0.2f * sinf(6.2831853f * d->tonePhase);
            d->tonePhase += inc;
            if (d->tonePhase >= 1.0f) d->tonePhase -= 1.0f;
            for (unsigned c = 0; c < ch; c++) d->outF[c][f] = v;
        }
    }

    // deinterleaved float -> the playback device's slice of the interleaved
    // device frame, clamped to [-1, 1]; any other channels (e.g. the capture
    // device's own outputs inside an aggregate) stay silent. pOut is null on a
    // capture-only device (output set to "No Output") -- nothing to do.
    if (!out) return;
    memset(out, 0, frames * d->outStride * sizeof(float));
    const unsigned n = ch < d->outAvail ? ch : d->outAvail;
    for (ma_uint32 f = 0; f < frames; f++)
        for (unsigned c = 0; c < n; c++) {
            float v = d->outF[c][f];
            if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
            out[d->outStride * f + d->outOffset + c] = v;
        }
}

// Total input or output channels of the device with this UID, 0 if the device
// (or the query) fails. Used to locate each sub-device's slice within the
// aggregate's concatenated channel layout.
unsigned channelCount(const char* uid, AudioObjectPropertyScope scope) {
    CFStringRef s = CFStringCreateWithCString(kCFAllocatorDefault, uid,
                                              kCFStringEncodingUTF8);
    AudioDeviceID dev = kAudioObjectUnknown;
    // sizeof s (a CFStringRef) is right: the translation input IS the pointer.
    AudioValueTranslation tr{&s, sizeof s, &dev, sizeof dev};
    UInt32 size = sizeof tr;
    AudioObjectPropertyAddress addr{kAudioHardwarePropertyDeviceForUID,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    OSStatus st = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr,
                                             0, nullptr, &size, &tr);
    CFRelease(s);
    if (st != noErr || dev == kAudioObjectUnknown) return 0;

    addr = {kAudioDevicePropertyStreamConfiguration, scope,
            kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyDataSize(dev, &addr, 0, nullptr, &size) != noErr)
        return 0;
    std::vector<char> buf(size);
    auto* abl = reinterpret_cast<AudioBufferList*>(buf.data());
    if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, abl) != noErr)
        return 0;
    unsigned n = 0;
    for (UInt32 i = 0; i < abl->mNumberBuffers; i++)
        n += abl->mBuffers[i].mNumberChannels;
    return n;
}

// Identity for the process's private aggregates. The UID prefix doubles as the
// filter key that keeps them out of the device lists we show the user. Each
// aggregate gets a UNIQUE uid (prefix + counter): CoreAudio destroys aggregates
// asynchronously, so a reopen() that recreated a fixed UID could collide with
// its still-dying predecessor.
constexpr const char* kAggregateUIDPrefix = "com.pistomp.pedalboard.aggregate";
constexpr const char* kAggregateName      = "pi-Stomp (drift-corrected)";

// Compose a private aggregate from two device UIDs: capture is the clock
// master, playback follows it through CoreAudio's drift-compensating
// resampler. Returns kAudioObjectUnknown on failure (caller falls back to the
// plain unsynchronized duplex pair) with the error in *stOut.
AudioObjectID createAggregate(const char* uid, const char* capUID,
                              const char* playUID, OSStatus* stOut) {
    auto str = [](const char* s) {
        return CFStringCreateWithCString(kCFAllocatorDefault, s,
                                         kCFStringEncodingUTF8);
    };
    auto dict = [] {
        return CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                         &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);
    };
    CFStringRef nameStr = str(kAggregateName);
    CFStringRef uidStr  = str(uid);
    CFStringRef capStr  = str(capUID);
    CFStringRef playStr = str(playUID);
    int one = 1;
    CFNumberRef oneNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &one);

    CFMutableDictionaryRef capDict = dict();   // clock master: no drift key
    CFDictionarySetValue(capDict, CFSTR(kAudioSubDeviceUIDKey), capStr);

    CFMutableDictionaryRef playDict = dict();  // resampled onto the capture clock
    CFDictionarySetValue(playDict, CFSTR(kAudioSubDeviceUIDKey), playStr);
    CFDictionarySetValue(playDict, CFSTR(kAudioSubDeviceDriftCompensationKey), oneNum);

    // PLAYBACK device first: miniaudio routes playback channels by speaker
    // position (the AudioUnit's channel map), and only the FIRST sub-device's
    // channels sit at positions (FL/FR) that a stereo client stream maps onto
    // -- trailing channels are dropped, not passed by index (verified: a tone
    // written there records as digital silence). Capture-side conversion IS
    // index-based (miniaudio can't read capture AU maps and assumes defaults),
    // so the capture device tolerates being second, at an offset. The clock
    // master is set by the Main key above, not by list order.
    const void* subs[] = { playDict, capDict };
    CFArrayRef subList =
        CFArrayCreate(kCFAllocatorDefault, subs, 2, &kCFTypeArrayCallBacks);

    CFMutableDictionaryRef desc = dict();
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceNameKey), nameStr);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceUIDKey), uidStr);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceIsPrivateKey), oneNum);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceMainSubDeviceKey), capStr);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceSubDeviceListKey), subList);

    AudioObjectID aggId = kAudioObjectUnknown;
    OSStatus st = AudioHardwareCreateAggregateDevice(desc, &aggId);
    if (stOut) *stOut = st;

    CFRelease(desc);
    CFRelease(subList);
    CFRelease(playDict);
    CFRelease(capDict);
    CFRelease(oneNum);
    CFRelease(playStr);
    CFRelease(capStr);
    CFRelease(uidStr);
    CFRelease(nameStr);
    return st == noErr ? aggId : kAudioObjectUnknown;
}

} // namespace

struct AudioIO::Impl {
    ma_context ctx;
    bool       ctxInited = false;
    MacDev     d;

    // The live private aggregate, if the current device pair needed one.
    AudioObjectID aggId = kAudioObjectUnknown;

    void destroyAggregate() {
        if (aggId == kAudioObjectUnknown) return;
        AudioHardwareDestroyAggregateDevice(aggId);
        aggId = kAudioObjectUnknown;
    }

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
        destroyAggregate(); // the fixed UID can only exist once

        ma_device_id playId{}, capId{};
        const bool playWanted = resolve(ma_device_type_playback, cfg.playbackName, playId);
        const bool capWanted  = resolve(ma_device_type_capture,  cfg.captureName,  capId);

        if (!playWanted && !capWanted)
            return true; // idle: nothing present to open -> silence, no feedback

        const ma_device_type type = (playWanted && capWanted) ? ma_device_type_duplex
                                   : playWanted               ? ma_device_type_playback
                                                              : ma_device_type_capture;
        // The aggregate path passes each direction's FULL channel count as
        // queried from CoreAudio (the callback then picks each sub-device's
        // slice out of the concatenated frame). miniaudio's own native-count
        // detection sees only the aggregate's first stream (= the capture
        // device's), which would leave the playback device's channels
        // unaddressed. The plain-device path just asks for cfg.channels and
        // lets miniaudio convert. Callback strides always come from the
        // negotiated ma_device fields, never from what was requested.
        auto tryInit = [&](const ma_device_id* cap, const ma_device_id* play,
                           unsigned capCh, unsigned playCh) {
            ma_device_config c = ma_device_config_init(type);
            c.sampleRate = cfg.rate;
            if (capWanted) {
                c.capture.format    = ma_format_f32;
                c.capture.channels  = capCh;
                c.capture.pDeviceID = cap;
            }
            if (playWanted) {
                c.playback.format    = ma_format_f32;
                c.playback.channels  = playCh;
                c.playback.pDeviceID = play;
            }
            c.periodSizeInFrames = cfg.wantPeriod;   // fixed-size callbacks (miniaudio default)
            c.dataCallback       = data_cb;
            c.pUserData          = &d;
            if (ma_device_init(&ctx, &c, &d.device) != MA_SUCCESS) return false;
            d.inStride  = capWanted  ? d.device.capture.channels  : cfg.channels;
            d.outStride = playWanted ? d.device.playback.channels : cfg.channels;
            d.inOffset  = d.outOffset = 0;
            d.inAvail   = d.inStride;
            d.outAvail  = d.outStride;
            return true;
        };

        // Duplex across two DIFFERENT devices = two unsynchronized clocks. Wrap
        // them in a drift-corrected private aggregate and open that instead;
        // same device on both sides already shares one clock and needs nothing.
        bool ok = false;
        if (playWanted && capWanted &&
            strcmp(capId.coreaudio, playId.coreaudio) != 0) {
            // Unique UID per aggregate (see prefix note). The pid guards the
            // quit-and-relaunch race: the old process's aggregate dies
            // asynchronously and could collide with a same-UID newcomer.
            static unsigned s_aggSeq = 0;
            char uid[128];
            snprintf(uid, sizeof uid, "%s.%d.%u", kAggregateUIDPrefix,
                     (int)getpid(), ++s_aggSeq);

            OSStatus st = noErr;
            aggId = createAggregate(uid, capId.coreaudio, playId.coreaudio, &st);
            if (aggId != kAudioObjectUnknown) {
                ma_device_id a{};
                strncpy(a.coreaudio, uid, sizeof a.coreaudio - 1);
                // The aggregate's frame per direction is every sub-device's
                // channels concatenated in LIST order (playback device first --
                // see createAggregate). Expected totals, from the sub-devices:
                const unsigned capIn   = channelCount(capId.coreaudio,
                                           kAudioObjectPropertyScopeInput);
                const unsigned playIn  = channelCount(playId.coreaudio,
                                           kAudioObjectPropertyScopeInput);
                const unsigned playOut = channelCount(playId.coreaudio,
                                           kAudioObjectPropertyScopeOutput);
                // A just-created aggregate takes a moment to publish its
                // streams; wait for the full channel complement (partial =
                // wrong offsets), then open every channel in each direction.
                unsigned aggIn = 0, aggOut = 0;
                for (int i = 0; i < 25; i++) {
                    aggIn  = channelCount(uid, kAudioObjectPropertyScopeInput);
                    aggOut = channelCount(uid, kAudioObjectPropertyScopeOutput);
                    if (aggIn >= playIn + capIn && aggOut >= playOut) break;
                    usleep(20 * 1000);
                }
                ok = tryInit(&a, &a, aggIn, aggOut);
                if (ok) {
                    // Playback: OUR device's channels lead the frame; whatever
                    // trails (the capture device's own outputs) stays silent.
                    // Capture: skip past the playback device's inputs to reach
                    // the capture device's.
                    d.outOffset = 0;
                    d.outAvail  = playOut && playOut <= d.outStride ? playOut
                                                                    : d.outStride;
                    d.inOffset  = playIn < d.inStride ? playIn : 0;
                    d.inAvail   = d.inStride - d.inOffset;
                    if (capIn && capIn < d.inAvail) d.inAvail = capIn;
                    fprintf(stderr,
                            "audio: drift-corrected aggregate for \"%s\" + \"%s\""
                            " (in %u@%u/%u, out %u@%u/%u)\n",
                            cfg.captureName.c_str(), cfg.playbackName.c_str(),
                            d.inAvail, d.inOffset, d.inStride,
                            d.outAvail, d.outOffset, d.outStride);
                } else {
                    fprintf(stderr, "audio: device init on aggregate failed\n");
                    destroyAggregate();
                }
            } else {
                fprintf(stderr, "audio: aggregate creation failed (OSStatus %d)\n",
                        (int)st);
            }
            if (!ok)
                fprintf(stderr, "audio: falling back to unsynchronized duplex "
                                "(clock drift may click)\n");
        }
        if (!ok) ok = tryInit(&capId, &playId, cfg.channels, cfg.channels);

        if (!ok) {
            if (err) *err = "miniaudio: ma_device_init failed";
            return false;
        }
        d.inited     = true;
        d.capActive  = capWanted;
        d.playActive = playWanted;
        d.testTone   = getenv("PISTOMP_TEST_TONE") != nullptr;
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
    for (ma_uint32 i = 0; i < count; i++) {
        // Our private aggregates are invisible to other apps but not to the
        // process that owns them -- keep the plumbing out of the settings UI.
        if (strncmp(infos[i].id.coreaudio, kAggregateUIDPrefix,
                    strlen(kAggregateUIDPrefix)) == 0) continue;
        out.push_back({ infos[i].name, infos[i].isDefault != 0 });
    }
    return out;
}

} // namespace

AudioIO::~AudioIO() {
    stop();
    if (impl_) {
        if (impl_->d.inited) ma_device_uninit(&impl_->d.device);
        impl_->destroyAggregate();
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
    impl_->destroyAggregate(); // a new one is composed if the next pair needs it

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
