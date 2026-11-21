/* SPDX-License-Identifier: GPL-2.0-or-later
 * gcn_buffer_binding.is_read: a compute shader that only stores to a buffer (a fast clear of
 * CMASK/HTILE) does not read it; a shader that loads and stores the same buffer (a read-modify-
 * write of the metadata, which the renderer must run instead of taking it for a clear) does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gcn_recompiler.h"

static const uint32_t k_store[] = {
    0x34020082u,              /* v_lshlrev_b32 v1, 2, v0                     */
    0xE0701000u, 0x80000001u, /* buffer_store_dword v0, v1, s[0:3], 0 offen */
    0xBF810000u,              /* s_endpgm                                    */
};

static const uint32_t k_load_store[] = {
    0x34020082u,              /* v_lshlrev_b32 v1, 2, v0                     */
    0xE0301000u, 0x80000201u, /* buffer_load_dword v2, v1, s[0:3], 0 offen  */
    0xBF8C0F70u,              /* s_waitcnt vmcnt(0)                          */
    0xE0701000u, 0x80000201u, /* buffer_store_dword v2, v1, s[0:3], 0 offen */
    0xBF810000u,              /* s_endpgm                                    */
};

static gcn_buffer_binding Compile(const uint32_t* code, size_t size) {
    gcn_runtime_info rt;
    gcn_compile_desc d;
    gcn_shader* sh = NULL;
    gcn_buffer_binding b;
    char err[256];
    memset(&rt, 0, sizeof(rt));
    memset(&d, 0, sizeof(d));
    rt.hw_stage = GCN_HW_CS;
    rt.sw_stage = GCN_SW_COMPUTE;
    rt.num_allocated_vgprs = 4;
    rt.num_user_data = 4;
    rt.cs.workgroup_size[0] = 64;
    rt.cs.workgroup_size[1] = rt.cs.workgroup_size[2] = 1;
    d.hw_stage = GCN_HW_CS;
    d.sw_stage = GCN_SW_COMPUTE;
    d.code = code;
    d.code_size = size;
    d.code_addr = 0x1000;
    d.user_data[0] = 0x600000;
    d.user_data[2] = 4096;
    d.user_data[3] = 4 | (5 << 3) | (6 << 6) | (7 << 9) | (4 << 12) | (4 << 15);
    d.runtime = &rt;
    d.flags = GCN_COMPILE_EMIT_MSL | GCN_COMPILE_ALLOW_NO_FOOTER;
    if (gcn_compile(&d, &sh, err, sizeof(err)) != GCN_OK) {
        fprintf(stderr, "FAIL: compile: %s\n", err);
        exit(1);
    }
    if (!gcn_shader_get_buffer(sh, 0, &b)) {
        fprintf(stderr, "FAIL: no buffer binding\n");
        exit(1);
    }
    gcn_shader_destroy(sh);
    return b;
}

int main(void) {
    const gcn_buffer_binding store = Compile(k_store, sizeof(k_store));
    const gcn_buffer_binding rmw = Compile(k_load_store, sizeof(k_load_store));
    if (!store.is_written || store.is_read) {
        fprintf(stderr, "FAIL: store-only buffer: written %d read %d\n", store.is_written, store.is_read);
        return 1;
    }
    if (!rmw.is_written || !rmw.is_read) {
        fprintf(stderr, "FAIL: load+store buffer: written %d read %d\n", rmw.is_written, rmw.is_read);
        return 1;
    }
    puts("buffer read flags: store-only not read, read-modify-write read");
    return 0;
}
