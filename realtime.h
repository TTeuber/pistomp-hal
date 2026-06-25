// realtime.h — small helpers for the low-latency-audio discipline.
//
// These are the process-global steps an audio program takes around its realtime
// thread; they were copy-pasted into every NAM/pedalboard main(). They are NOT
// audio-specific enough to hide inside AudioIO (signal policy and memory locking
// are whole-process decisions the app owns), so they live here as thin, explicit
// inline helpers instead.
//
// Header-only: no .cpp, no link cost for consumers that don't use it.

#pragma once
#include <csignal>
#include <pthread.h>
#include <sys/mman.h>

namespace pistomp::realtime {

// Lock all current + future pages into RAM so the audio thread never page-faults
// to disk mid-block. Call once, before going realtime. Returns false if the rlimit
// is too low (run as root, or raise memlock) — callers usually just warn and go on.
inline bool lock_all_memory() {
    return mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
}

// Block / unblock a signal in the CALLING thread. The pattern: block SIGINT in
// main BEFORE spawning the audio (and any other) thread, so every worker inherits
// the block and the audio thread's read()/write() never returns EINTR; then
// unblock in main and install a handler, so only main fields Ctrl-C.
inline void block_signal(int sig) {
    sigset_t s; sigemptyset(&s); sigaddset(&s, sig);
    pthread_sigmask(SIG_BLOCK, &s, nullptr);
}
inline void unblock_signal(int sig) {
    sigset_t s; sigemptyset(&s); sigaddset(&s, sig);
    pthread_sigmask(SIG_UNBLOCK, &s, nullptr);
}

} // namespace pistomp::realtime
