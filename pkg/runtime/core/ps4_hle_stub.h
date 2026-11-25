// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_HLE_STUB_H
#define PS4_HLE_STUB_H

#include <stdio.h>
#include "recomp_runtime.h"

// A library function that shadPS4 implements as a stub as well ("(STUBBED) called", returns
// ORBIS_OK): it has no effect on a desktop host. The first call is logged, so a title that
// depends on one shows up in the log.
#define PS4_HLE_STUB(name)                                                         \
    void shim_##name(GuestContext *ctx) {                                          \
        static int logged_;                                                        \
        if (!logged_) {                                                            \
            logged_ = 1;                                                           \
            fprintf(stderr, "[ps4-stub] %s -> 0 (stubbed, as in shadPS4)\n", #name); \
        }                                                                          \
        ctx->rax = 0;                                                              \
        SHIM_RETURN();                                                             \
    }

#endif // PS4_HLE_STUB_H
