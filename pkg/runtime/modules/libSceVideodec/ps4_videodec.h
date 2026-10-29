// SPDX-License-Identifier: GPL-2.0-or-later
//
// libSceVideodec per-decoder counters. Every decoder keeps them; sceVideodecDeleteDecoder prints
// them as one line, and PS4_VIDEODEC_LOG=1 adds a trace (the config, the first access units,
// and a progress line every 2 s). Times are raw CLOCK_MONOTONIC seconds, the clock of the
// screenshot cadence ("time cadence: ... mono X"), so they map onto sec_<s>.png names.
#ifndef PS4_VIDEODEC_H
#define PS4_VIDEODEC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Ps4VideodecStats {
    uint64_t aus;               // access units passed to sceVideodecDecode
    uint64_t slices;            // coded slices in them
    uint32_t max_slices_per_au; // most slices in one Decode call
    uint64_t mid_picture_aus;   // inputs whose first slice has first_mb_in_slice != 0 (slice-wise input)
    uint64_t pictures;          // pictures handed to the guest (Decode and Flush)
    uint64_t error_pictures;    // access units that gave no picture (undecodable, dropped)
    uint64_t flush_pictures;    // pictures handed out by sceVideodecFlush (part of `pictures`)
    uint64_t refused_pictures;  // pictures dropped: the guest's frame buffer was too small (Decode/Flush
                                // returned SCE_VIDEODEC_ERROR_FRAME_BUFFER_SIZE)
    double first_picture_mono;  // CLOCK_MONOTONIC seconds of the first and last picture; 0 if none
    double last_picture_mono;
    int64_t first_pts;          // ptsData of the first and last picture; -1 if none
    int64_t last_pts;
    uint64_t min_fb_size;       // smallest frameBufferSize the guest passed; 0 if none
    uint64_t need_fb_size;      // largest NV12 size a picture needed
} Ps4VideodecStats;

// Counters of the decoder whose SceVideodecCtrl is at guest address ctrl_addr (mem_base is the
// guest memory base). Returns 0, or -1 when the ctrl holds no live decoder. For tests: the
// counters themselves are read under the decoder's lock, but the call is not synchronised with
// sceVideodecDeleteDecoder on another thread, so the caller must not race the two.
// pictures + error_pictures + refused_pictures equals the access units with a slice, once the
// decoder is flushed.
int ps4_videodec_stats(const void *mem_base, uint64_t ctrl_addr, Ps4VideodecStats *out);

#ifdef __cplusplus
}
#endif

#endif // PS4_VIDEODEC_H
