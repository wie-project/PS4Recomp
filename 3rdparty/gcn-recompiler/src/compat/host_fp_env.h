// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The default IEEE floating-point environment for the duration of a C API call that translates.
//
// The recompiler carries GCN register values through float-typed IR values (a VGPR has no type;
// a format store takes F32), folds float constants, and SPIRV-Cross prints float literals
// through a double. All of that must see the exact bits. A caller may run with flush-to-zero
// set: PS4Recomp translates on guest threads, which run host code with the guest's MXCSR.FZ in
// FPCR.FZ, and on AArch64 FZ flushes denormal inputs too (float -> double included). An integer
// constant below 0x00800000 is a denormal float, so under FZ it came out of the translator as 0.
// The scope sets the host default (round to nearest, no flush to zero, no default NaN, IEEE
// half precision) and restores the caller's environment when it ends.

#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#elif !defined(__aarch64__)
#error "HostFpEnvScope: no floating-point environment code for this architecture"
#endif

namespace GcnShim {

class HostFpEnvScope {
public:
    HostFpEnvScope() {
#if defined(__aarch64__)
        asm volatile("mrs %0, fpcr" : "=r"(saved_));
        if (saved_ != kDefault) asm volatile("msr fpcr, %0" : : "r"(kDefault));
#elif defined(__x86_64__) || defined(_M_X64)
        saved_ = _mm_getcsr();
        if ((saved_ & kControlMask) != kDefault) _mm_setcsr((saved_ & ~kControlMask) | kDefault);
#endif
    }
    ~HostFpEnvScope() {
#if defined(__aarch64__)
        if (saved_ != kDefault) asm volatile("msr fpcr, %0" : : "r"(saved_));
#elif defined(__x86_64__) || defined(_M_X64)
        if ((saved_ & kControlMask) != kDefault) _mm_setcsr(saved_);
#endif
    }
    HostFpEnvScope(const HostFpEnvScope&) = delete;
    HostFpEnvScope& operator=(const HostFpEnvScope&) = delete;

private:
#if defined(__aarch64__)
    // FPCR 0: RMode round to nearest, FZ/FZ16/DN/AHP clear, no trapped exceptions, FEAT_AFP
    // controls (FIZ, AH, NEP) clear.
    static constexpr uint64_t kDefault = 0;
    uint64_t saved_ = 0;
#elif defined(__x86_64__) || defined(_M_X64)
    // MXCSR control bits: DAZ (6), exception masks (7-12), RC (13-14), FTZ (15). The default
    // masks every exception and rounds to nearest, without DAZ or FTZ; status flags are kept.
    static constexpr uint32_t kControlMask = 0xFFC0;
    static constexpr uint32_t kDefault = 0x1F80;
    uint32_t saved_ = 0;
#endif
};

} // namespace GcnShim
