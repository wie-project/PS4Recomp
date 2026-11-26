// SPDX-License-Identifier: GPL-2.0-or-later

// libSceDiscMap after shadPS4. A title installed on the host has no disc: there is no bitmap of
// which package chunks are on the HDD, so the bitmap queries report NO_BITMAP_INFO, and the
// location query (Func_7C980FFB0AA27E7A) reports every range as present.

#include "ps4_disc_map.h"

static void disc_map_return(GuestContext *ctx, int32_t rc) {
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

// int sceDiscMapGetPackageSize(int64_t fflags, int *ret1, int *ret2)
void shim_sceDiscMapGetPackageSize(GuestContext *ctx) {
    disc_map_return(ctx, ORBIS_DISC_MAP_ERROR_NO_BITMAP_INFO);
}

// int sceDiscMapIsRequestOnHDD(const char *path, int64_t offset, int64_t nbytes, int *ret)
void shim_sceDiscMapIsRequestOnHDD(GuestContext *ctx) {
    disc_map_return(ctx, ORBIS_DISC_MAP_ERROR_NO_BITMAP_INFO);
}

// int Func_7C980FFB0AA27E7A(const char *path, int64_t offset, int64_t nbytes, int *flags,
//                           int *ret1, int *ret2): all three outputs are zeroed.
void shim_Func_7C980FFB0AA27E7A(GuestContext *ctx) {
    const uint64_t outs[3] = {ctx->rcx, ctx->r8, ctx->r9};
    for (int i = 0; i < 3; i++) {
        if (outs[i]) {
            *(int32_t *)(ctx->mem_base + outs[i]) = 0;
        }
    }
    disc_map_return(ctx, 0);
}

void shim_Func_8A828CAEE7EDD5E9(GuestContext *ctx) {
    disc_map_return(ctx, ORBIS_DISC_MAP_ERROR_NO_BITMAP_INFO);
}

void shim_Func_E7EBCE96E92F91F8(GuestContext *ctx) {
    disc_map_return(ctx, ORBIS_DISC_MAP_ERROR_NO_BITMAP_INFO);
}
