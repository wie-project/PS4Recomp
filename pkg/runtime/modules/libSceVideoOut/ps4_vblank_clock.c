// SPDX-License-Identifier: GPL-2.0-or-later
// Guest vblank timeline (see ps4_vblank_clock.h).
#include "ps4_vblank_clock.h"

#include <mach/mach_time.h>
#include <stdatomic.h>

// libsystem caches the timebase: no system call
static mach_timebase_info_data_t timebase(void) {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    return tb;
}

uint64_t ps4_vblank_now_ns(void) {
    const mach_timebase_info_data_t tb = timebase();
    return (uint64_t)((unsigned __int128)mach_absolute_time() * tb.numer / tb.denom);
}

uint64_t ps4_vblank_epoch_ns(void) {
    static _Atomic uint64_t epoch;
    uint64_t e = atomic_load_explicit(&epoch, memory_order_acquire);
    if (e) return e;
    uint64_t expected = 0;
    const uint64_t now = ps4_vblank_now_ns();
    return atomic_compare_exchange_strong(&epoch, &expected, now) ? now : expected;
}

void ps4_vblank_wait_until_ns(uint64_t t_ns) {
    const mach_timebase_info_data_t tb = timebase();
    // rounded up: once mach time reaches it, ps4_vblank_now_ns() >= t_ns
    const uint64_t deadline = (uint64_t)(((unsigned __int128)t_ns * tb.denom + tb.numer - 1) / tb.numer);
    // mach_wait_until returns early when the thread is interrupted: sleep again
    while (mach_absolute_time() < deadline) mach_wait_until(deadline);
}
