// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_font_ft.h"

static int32_t ft_dummy_driver_cb(void) {
    return 0;
}

static void *g_ft_lib_table[32] = {0};
static void *g_ft_renderer_table[32] = {0};
static int g_ft_tables_initialized = 0;

static void init_tables_if_needed(void) {
    if (g_ft_tables_initialized) return;
    for (int i = 0; i < 32; i++) {
        g_ft_lib_table[i] = (void *)(uintptr_t)ft_dummy_driver_cb;
        g_ft_renderer_table[i] = (void *)(uintptr_t)ft_dummy_driver_cb;
    }
    g_ft_tables_initialized = 1;
}

void shim_sceFontSelectLibraryFt(GuestContext *ctx) {
    init_tables_if_needed();
    int val = (int)ctx->rdi;
    if (val == 0) {
        ctx->rax = (uint64_t)(uintptr_t)g_ft_lib_table;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim_sceFontSelectRendererFt(GuestContext *ctx) {
    init_tables_if_needed();
    int val = (int)ctx->rdi;
    if (val == 0) {
        ctx->rax = (uint64_t)(uintptr_t)g_ft_renderer_table;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}
