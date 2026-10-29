// Guest memory windows for no-copy MTLBuffers (see GuestBuffer in ps4_gnm_draw.mm).
#pragma once
#include <stdint.h>

// Guest memory is exposed to Metal in 64 MiB aligned windows. The guest NULL page area
// [0, PS4_GNM_WINDOW0_BASE) may be PROT_NONE (PS4_NULL_GUARD=1), and
// newBufferWithBytesNoCopy fails on a range containing inaccessible pages, so window 0
// starts at PS4_GNM_WINDOW0_BASE (16 KiB aligned, as no-copy buffers require).
#define PS4_GNM_WINDOW_SIZE (64ull << 20)
#define PS4_GNM_WINDOW0_BASE 0x10000ull

// Computes the buffer range [*base, *end) covering guest [addr, addr + size). *base is the
// window start (PS4_GNM_WINDOW0_BASE for window 0), *end a window end clamped to
// mem_size (16 KiB aligned). *nwin is the number of windows spanned. Returns 0 when the
// range cannot be mapped (empty, out of guest memory, or inside the NULL page area).
static inline int ps4_gnm_guest_window_range(uint64_t addr, uint64_t size, uint64_t mem_size,
                                             uint64_t *base, uint64_t *end, uint64_t *nwin) {
    if (addr < PS4_GNM_WINDOW0_BASE || size == 0 || addr >= mem_size || size > mem_size - addr) return 0;
    const uint64_t aligned = addr & ~(PS4_GNM_WINDOW_SIZE - 1);
    uint64_t e = (addr + size + PS4_GNM_WINDOW_SIZE - 1) & ~(PS4_GNM_WINDOW_SIZE - 1);
    if (e > mem_size) e = mem_size & ~16383ull;
    if (e <= addr) return 0;
    *nwin = (e - aligned + PS4_GNM_WINDOW_SIZE - 1) / PS4_GNM_WINDOW_SIZE;
    *base = aligned ? aligned : PS4_GNM_WINDOW0_BASE;
    *end = e;
    return 1;
}
