// SPDX-License-Identifier: GPL-2.0-or-later
//
// Bit reading for the H.264 and HEVC parameter set / slice header parsers of the VideoToolbox
// decode core: emulation prevention removal and Exp-Golomb codes (H.264 7.2 / 9.1, HEVC 7.2 /
// 9.2). Portable, makes no allocation.
#ifndef PS4_VT_BITSTREAM_H
#define PS4_VT_BITSTREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Removes emulation prevention bytes (00 00 03 -> 00 00). Returns the RBSP length.
static inline size_t nal_to_rbsp(const uint8_t *src, size_t n, uint8_t *dst) {
    size_t o = 0;
    int zeros = 0;
    for (size_t i = 0; i < n; i++) {
        if (zeros >= 2 && src[i] == 3) {
            zeros = 0;
            continue;
        }
        zeros = src[i] == 0 ? zeros + 1 : 0;
        dst[o++] = src[i];
    }
    return o;
}

// Reads past the end set overrun and return 0, so a parser checks overrun once at the end.
typedef struct BitReader {
    const uint8_t *p;
    size_t bits;
    size_t pos;
    bool overrun;
} BitReader;

static inline uint32_t br_u(BitReader *b, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        if (b->pos >= b->bits) {
            b->overrun = true;
            return 0;
        }
        v = (v << 1) | ((b->p[b->pos >> 3] >> (7 - (b->pos & 7))) & 1u);
        b->pos++;
    }
    return v;
}

static inline void br_skip(BitReader *b, size_t n) {
    if (n > b->bits - (b->pos < b->bits ? b->pos : b->bits)) {
        b->pos = b->bits;
        b->overrun = true;
        return;
    }
    b->pos += n;
}

static inline uint32_t br_ue(BitReader *b) {
    int lz = 0;
    while (br_u(b, 1) == 0) {
        if (b->overrun || ++lz > 31) {
            b->overrun = true;
            return 0;
        }
    }
    if (lz == 0) return 0;
    return ((1u << lz) - 1u) + br_u(b, lz);
}

static inline int32_t br_se(BitReader *b) {
    uint32_t k = br_ue(b);
    return (k & 1) ? (int32_t)((k + 1) / 2) : -(int32_t)(k / 2);
}

#endif // PS4_VT_BITSTREAM_H
