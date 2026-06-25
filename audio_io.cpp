// audio_io.cpp — the ALSA + SCHED_FIFO implementation behind audio_io.h.
//
// All the realtime-audio boilerplate that used to live in each app's main()/
// audio_thread() is here, once. The numbered [n] notes mirror the original
// rt_passthrough.cpp teaching comments.

#include "audio_io.h"

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <vector>

namespace pistomp {

// Per-instance ALSA/pthread state + the conversion buffers. Hidden from the
// header so libasound and pthread never leak into consumers.
struct AudioIO::Impl {
    snd_pcm_t* cap  = nullptr;
    snd_pcm_t* play = nullptr;
    pthread_t  tid{};
    bool       started = false;

    // S16 interleaved wire buffer + deinterleaved float views handed to the cb.
    std::vector<int16_t>            i16;
    std::vector<std::vector<float>> inF, outF;
    std::vector<const float*>       inPtrs;
    std::vector<float*>             outPtrs;

    // Allocated + zero-filled (= page-faulted in) before the thread goes RT, so
    // the realtime loop only ever touches resident memory.
    void alloc(unsigned ch, int maxFrames) {
        i16.assign(static_cast<size_t>(maxFrames) * ch, 0);
        inF.assign(ch, std::vector<float>(maxFrames, 0.0f));
        outF.assign(ch, std::vector<float>(maxFrames, 0.0f));
        inPtrs.resize(ch);
        outPtrs.resize(ch);
        for (unsigned c = 0; c < ch; c++) {
            inPtrs[c]  = inF[c].data();
            outPtrs[c] = outF[c].data();
        }
    }
};

// Negotiate hw params on one stream; report back the granted period/buffer.
static int configure_pcm(snd_pcm_t* pcm, const AudioConfig& cfg,
                         snd_pcm_uframes_t* period_out,
                         snd_pcm_uframes_t* buffer_out) {
    snd_pcm_hw_params_t* hw = nullptr;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, hw, cfg.channels);
    unsigned rate = cfg.rate;
    snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, nullptr);
    snd_pcm_uframes_t period = cfg.wantPeriod;
    snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, nullptr);
    snd_pcm_uframes_t buffer = cfg.wantBuffer;
    snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer);
    int err = snd_pcm_hw_params(pcm, hw);
    if (err < 0) return err;
    snd_pcm_hw_params_get_period_size(hw, &period, nullptr);
    snd_pcm_hw_params_get_buffer_size(hw, &buffer);
    *period_out = period;
    *buffer_out = buffer;
    return 0;
}

AudioIO::~AudioIO() {
    stop();
    if (impl_) {
        if (impl_->play) { snd_pcm_drain(impl_->play); snd_pcm_close(impl_->play); }
        if (impl_->cap)  { snd_pcm_close(impl_->cap); }
        delete impl_;
        impl_ = nullptr;
    }
}

bool AudioIO::open(const AudioConfig& cfg) {
    cfg_ = cfg;
    impl_ = new Impl();

    int err = snd_pcm_open(&impl_->cap, cfg.device, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) { lastError_ = snd_strerror(err); return false; }
    err = snd_pcm_open(&impl_->play, cfg.device, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) { lastError_ = snd_strerror(err); return false; }

    snd_pcm_uframes_t cap_p, cap_b, play_p, play_b;
    if (configure_pcm(impl_->cap, cfg, &cap_p, &cap_b) < 0 ||
        configure_pcm(impl_->play, cfg, &play_p, &play_b) < 0) {
        lastError_ = "configure failed";
        return false;
    }

    snd_pcm_uframes_t period = cap_p < play_p ? cap_p : play_p;
    if (static_cast<int>(period) > cfg.maxFrames) period = cfg.maxFrames;
    period_ = static_cast<int>(period);

    // Playback software params: start only once a full buffer is queued, and wake
    // us when `period` frames of room are free.
    {
        snd_pcm_sw_params_t* sw = nullptr;
        snd_pcm_sw_params_alloca(&sw);
        snd_pcm_sw_params_current(impl_->play, sw);
        snd_pcm_sw_params_set_start_threshold(impl_->play, sw, play_b);
        snd_pcm_sw_params_set_avail_min(impl_->play, sw, period);
        if ((err = snd_pcm_sw_params(impl_->play, sw)) < 0) {
            lastError_ = snd_strerror(err);
            return false;
        }
    }
    snd_pcm_prepare(impl_->cap);
    snd_pcm_prepare(impl_->play);

    impl_->alloc(cfg.channels, cfg.maxFrames);
    return true;
}

// pthread entry trampoline -> member run().
void* AudioIO::thread_entry(void* self) {
    static_cast<AudioIO*>(self)->run();
    return nullptr;
}

bool AudioIO::start(AudioCallback cb) {
    cb_ = std::move(cb);
    running_.store(true);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);  // [2] THE gotcha
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);               // [3]
    sched_param sp;
    sp.sched_priority = cfg_.rtPriority;
    pthread_attr_setschedparam(&attr, &sp);
    int rc = pthread_create(&impl_->tid, &attr, &AudioIO::thread_entry, this);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        lastError_ = strerror(rc);
        running_.store(false);
        return false;
    }
    impl_->started = true;
    return true;
}

void AudioIO::stop() {
    if (!impl_ || !impl_->started) { running_.store(false); return; }
    running_.store(false);
    pthread_join(impl_->tid, nullptr);
    impl_->started = false;
}

// ---------------- THE REALTIME AUDIO THREAD ----------------
// No malloc/printf/locks; only readi/writei may block. The DSP is the caller's
// cb_, invoked once per block on deinterleaved float buffers.
void AudioIO::run() {
    Impl* d = impl_;
    const unsigned ch = cfg_.channels;
    const float to_float = 1.0f / 32768.0f;

    while (running_.load(std::memory_order_relaxed)) {
        snd_pcm_sframes_t got = snd_pcm_readi(d->cap, d->i16.data(), period_);
        if (got == -EPIPE) { xruns_.fetch_add(1, std::memory_order_relaxed);
                             snd_pcm_prepare(d->cap); continue; }
        if (got == -EINTR) continue;
        if (got < 0) { fatal_.store(snd_strerror((int)got), std::memory_order_relaxed);
                       running_.store(false); break; }

        // S16 interleaved -> deinterleaved float [-1, 1].
        for (int f = 0; f < got; f++)
            for (unsigned c = 0; c < ch; c++)
                d->inF[c][f] = d->i16[ch * f + c] * to_float;

        cb_(d->inPtrs.data(), d->outPtrs.data(), static_cast<int>(got));

        // float -> S16 interleaved, clamped.
        for (int f = 0; f < got; f++)
            for (unsigned c = 0; c < ch; c++) {
                int s = static_cast<int>(d->outF[c][f] * 32768.0f);
                if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
                d->i16[ch * f + c] = static_cast<int16_t>(s);
            }

        snd_pcm_sframes_t wrote = snd_pcm_writei(d->play, d->i16.data(), got);
        if (wrote == -EPIPE) { xruns_.fetch_add(1, std::memory_order_relaxed);
                               snd_pcm_prepare(d->play); continue; }
        if (wrote == -EINTR) continue;
        if (wrote < 0) { fatal_.store(snd_strerror((int)wrote), std::memory_order_relaxed);
                         running_.store(false); break; }
    }
}

} // namespace pistomp
