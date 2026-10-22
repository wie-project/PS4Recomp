// Host time spent in calls that can stall a frame, summed over all threads between two
// presents and printed in the long-frame breakdown ("[gnm-draw] hitch").
#pragma once
#include <errno.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PS4_HITCH_IO = 0, PS4_HITCH_VM, PS4_HITCH_MTLBUF, PS4_HITCH_TEXLOCK, PS4_HITCH_SHADER_FS, PS4_HITCH_KINDS };

uint64_t ps4_hitch_now_ns(void);
void ps4_hitch_add_ns(int kind, uint64_t ns);
uint64_t ps4_hitch_take_ns(int kind);

// Runs one statement and adds its duration to a counter, e.g.
//   ssize_t ret; PS4_HITCH_TIMED(PS4_HITCH_IO, ret = pread(fd, buf, n, off));
// errno set by stmt is kept (the timing calls after it may change errno).
#define PS4_HITCH_TIMED(kind, stmt)                                  \
  do {                                                               \
    const uint64_t ps4_hitch_t0_ = ps4_hitch_now_ns();               \
    stmt;                                                            \
    const int ps4_hitch_errno_ = errno;                              \
    ps4_hitch_add_ns((kind), ps4_hitch_now_ns() - ps4_hitch_t0_);    \
    errno = ps4_hitch_errno_;                                        \
  } while (0)

#ifdef __cplusplus
}
#endif
