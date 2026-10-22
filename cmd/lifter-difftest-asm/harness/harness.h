// Shared state generation for the lifter differential test (native x86-64 and lifted arm64 asm).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define DT_SCRATCH_BASE 0x800000000ULL
#define DT_SCRATCH_SIZE 0x100000ULL
#define DT_MEM_TARGET (DT_SCRATCH_BASE + 0x80000ULL)
#define DT_MEM_WINDOW 128 /* bytes compared, starting at DT_MEM_TARGET - 32 */
#define DT_TRIALS 8
#define DT_FAKE_TCB (DT_SCRATCH_BASE + 0xE0000ULL) /* x27 (FS) on the arm64 side; [FS+0x70] = GuestContext* */

typedef struct DtState {
    uint64_t gpr[16];
    uint64_t flags;
    uint32_t mxcsr;
    uint32_t pad; /* input CL (shift/rotate count), set by dt_make_state */
    uint8_t ymm[16][32];
    /* x87 state, kept by the C drivers (the test stubs do not touch it): ST(0)..ST(7) in stack
       order as doubles, TOP, control and status words, and which ST(i) hold a value (the native
       tag word; the lifter does not track tags). Inputs: ST(0)..ST(6) hold values, ST(7) is
       empty so that one push does not overflow the stack (all eight hold values for an
       instruction that names ST(7)). */
    double st[8];
    uint32_t ftop;
    uint16_t fcw, fsw;
    uint32_t ftag;
    uint32_t pad2;
} DtState;

typedef struct DtResult {
    uint32_t status; /* 0 ok, 1 SIGILL, 2 SIGSEGV/other */
    uint32_t pad;
    DtState st;
    uint8_t mem[DT_MEM_WINDOW];
} DtResult;

static inline uint64_t dt_rng(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static inline uint32_t dt_lane(uint64_t *s, int special) {
    uint64_t r = dt_rng(s);
    unsigned mode = (unsigned)(r % 100);
    union { float f; uint32_t u; } v;
    if (special && mode < 10) {
        static const uint32_t sp[] = {0x7f800000u, 0xff800000u, 0x7fc00000u, 0x00000001u, 0x80000000u, 0x7f7fffffu,
                                      0x4f000000u, 0xcf000000u, 0x5f000000u, 0x3f000000u};
        return sp[(r >> 8) % 10];
    }
    if (mode < 65) {
        double m = (double)((r >> 11) & 0xFFFFF) / (double)0x100000; /* [0,1) */
        int e = (int)((r >> 32) % 24) - 12;
        v.f = (float)((m * 2.0 - 1.0) * (double)(1 << 10) * __builtin_ldexp(1.0, e));
        return v.u;
    }
    if (mode < 80) return (uint32_t)(r >> 16);
    if (mode < 92) return (uint32_t)((int32_t)((r >> 20) % 200) - 100);
    static const uint32_t simple[] = {0, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x40000000u, 0x3f000000u, 1, 0xffffffffu};
    return simple[(r >> 8) % 8];
}

/* A double for an x87 register: integers, fractions over a wide exponent range, simple
   constants and, with special, infinities, quiet NaN, signed zero, denormals and extremes.
   Signaling NaNs are not used (loading one raises #IA and quiets it). */
static inline double dt_double(uint64_t *s, int special) {
    uint64_t r = dt_rng(s);
    unsigned mode = (unsigned)(r % 100);
    union { double d; uint64_t u; } v;
    if (special && mode < 10) {
        static const uint64_t sp[] = {0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000000ull,
                                      0xfff8000000000000ull, 0x8000000000000000ull, 0x0000000000000001ull,
                                      0x7fefffffffffffffull, 0x43e0000000000000ull, 0xc3e0000000000000ull,
                                      0x40dfffc000000000ull};
        v.u = sp[(r >> 8) % 10];
        return v.d;
    }
    if (mode < 60) {
        double m = (double)((r >> 11) & 0xFFFFFFFFFull) / (double)0x1000000000ull; /* [0,1) */
        int e = (int)((r >> 48) % 40) - 20;
        return (m * 2.0 - 1.0) * __builtin_ldexp(1.0, e);
    }
    if (mode < 80) return (double)((int64_t)((r >> 20) % 200000) - 100000);
    if (mode < 90) return (double)(int64_t)(r >> 1) * (r & 1 ? -1.0 : 1.0) / 8.0;
    static const double simple[] = {0.0, 1.0, -1.0, 0.5, 2.0, 1e10, -3.25, 65535.5};
    return simple[(r >> 8) % 8];
}

/* Sets the quiet bit of every signaling NaN in buf (as float lanes and as double lanes). */
static inline void dt_quiet_snans(uint8_t *buf, size_t len) {
    for (size_t off = 0; off + 8 <= len; off += 8) {
        uint64_t d;
        memcpy(&d, buf + off, 8);
        if ((d & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL && (d & 0xfffffffffffffULL) &&
            !(d & 0x8000000000000ULL))
            d |= 0x8000000000000ULL;
        for (int h = 0; h < 2; h++) {
            uint32_t f = (uint32_t)(d >> (32 * h));
            if ((f & 0x7f800000u) == 0x7f800000u && (f & 0x7fffffu) && !(f & 0x400000u))
                d |= (uint64_t)0x400000u << (32 * h);
        }
        memcpy(buf + off, &d, 8);
    }
}

static inline void dt_make_state(DtState *st, uint8_t *scratch, unsigned test, unsigned trial, int special,
                                 const uint64_t regset[17]) {
    uint64_t s = 0x1234567ULL * (test + 1) + 0x9876543ULL * (trial + 1);
    memset(st, 0, sizeof(*st));
    for (int i = 0; i < 16; i++) st->gpr[i] = dt_rng(&s);
    st->gpr[4] = DT_SCRATCH_BASE + 0xF0000; /* rsp (unused) */
    if (trial & 1) for (int i = 0; i < 16; i++) st->gpr[i] &= 0xFFFFFFFFULL;   /* mix in small values */
    if (trial & 2) for (int i = 0; i < 16; i++) st->gpr[i] &= 0xFFULL;
    st->gpr[4] = DT_SCRATCH_BASE + 0xF0000;
    for (int i = 0; i < 16; i++)
        if (regset[0] >> i & 1) st->gpr[i] = regset[1 + i];
    if (regset[0] >> 60 & 1 && trial < 2) /* shift/rotate by CL: trials 0 and 1 use CL = 0, 1 */
        st->gpr[1] = (st->gpr[1] & ~0xffULL) | trial;
    st->pad = (uint32_t)(st->gpr[1] & 0xff);
    uint64_t r = dt_rng(&s);
    st->flags = 0x202 | (r & 0x8D5); /* CF PF AF ZF SF OF */
    if (regset[0] >> 63 & 1) {
        /* FP-compare result (greater, less, equal, unordered): ZF PF CF, OF = SF = AF = 0 */
        static const uint64_t fc[4] = {0x000, 0x001, 0x040, 0x045};
        st->flags = 0x202 | fc[(r >> 20) & 3];
    }
    if (regset[0] >> 62 & 1) st->flags &= ~0x801ULL; /* after a logical op: CF = OF = 0 */
    st->mxcsr = 0x1F80;
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 8; k++) {
            uint32_t u = dt_lane(&s, special);
            memcpy(&st->ymm[i][k * 4], &u, 4);
        }
    for (uint64_t off = 0; off < 0x400; off += 4) {
        uint32_t u = dt_lane(&s, special);
        memcpy(scratch + (DT_MEM_TARGET - DT_SCRATCH_BASE) - 0x200 + off, &u, 4);
    }
    /* No signaling NaNs, as float or as double: with a quiet NaN first operand and a signaling
       NaN second, x86 SSE returns the first operand and ARM the quieted signaling one (a known,
       accepted deviation; see the Task 2b report). Arithmetic never produces SNaNs. */
    dt_quiet_snans(&st->ymm[0][0], sizeof(st->ymm));
    dt_quiet_snans(scratch + (DT_MEM_TARGET - DT_SCRATCH_BASE) - 0x200, 0x400);
    for (int i = 0; i < 7; i++) st->st[i] = dt_double(&s, special);
    st->st[7] = 0;
    st->ftop = (test * 5 + trial * 3) & 7;
    st->ftag = 0x7f;
    if (regset[0] >> 57 & 1) { /* the instruction names ST(7): all eight registers hold values */
        st->st[7] = dt_double(&s, special);
        st->ftag = 0xff;
    }
    st->fcw = 0x37f;
    st->fsw = (uint16_t)(st->ftop << 11);
    if (regset[0] >> 58 & 1) { /* POPFQ: a valid RFLAGS image at the stack top (DT_MEM_TARGET + 64) */
        uint64_t f = 0x202 | (dt_rng(&s) & 0x8D5);
        memcpy(scratch + (DT_MEM_TARGET - DT_SCRATCH_BASE) + 64, &f, 8);
    }
    if (regset[0] >> 59 & 1) { /* a valid x87 control word for FLDCW (reserved bits clear) */
        uint64_t m = dt_rng(&s);
        uint16_t v = (uint16_t)(0x0040 | (m & 0x3f) | (m & 0x0f00));
        memcpy(scratch + (DT_MEM_TARGET - DT_SCRATCH_BASE), &v, 2);
    }
    if (regset[0] >> 56 & 1 && trial & 4) { /* CMPXCHG/CMPXCHG16B: m = RAX, [m+8] = RDX (compare succeeds) */
        memcpy(scratch + (DT_MEM_TARGET - DT_SCRATCH_BASE), &st->gpr[0], 8);
        memcpy(scratch + (DT_MEM_TARGET - DT_SCRATCH_BASE) + 8, &st->gpr[2], 8);
    }
    if (regset[0] >> 55 & 1 && trial & 4) { /* CMPXCHG8B: m64 = EDX:EAX (compare succeeds) */
        uint64_t v = (st->gpr[0] & 0xFFFFFFFFULL) | (st->gpr[2] << 32);
        memcpy(scratch + (DT_MEM_TARGET - DT_SCRATCH_BASE), &v, 8);
    }
    if (regset[0] >> 61 & 1) { /* a valid MXCSR value for (V)LDMXCSR */
        uint64_t m = dt_rng(&s);
        uint32_t v = 0x1f80 | (uint32_t)(m & 0x3f) | (uint32_t)(m & 0x40) | (uint32_t)(m & 0x6000) |
                     (uint32_t)(m & 0x8000);
        memcpy(scratch + (DT_MEM_TARGET - DT_SCRATCH_BASE), &v, 4);
    }
}
