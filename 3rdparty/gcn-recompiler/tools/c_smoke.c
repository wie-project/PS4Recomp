/* SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Plain-C smoke test: proves gcn_recompiler.h is consumable from C and shows the link line
 * (libgcn_recompiler.a + libspirv-cross.a + -lc++). Compiles a tiny compute shader. */
#include <stdio.h>
#include <string.h>
#include "gcn_recompiler.h"

static const uint32_t k_cs[] = {
    0x34020082u, /* v_lshlrev_b32 v1, 2, v0                     */
    0xE0701000u, 0x80000001u, /* buffer_store_dword v0, v1, s[0:3], 0 offen */
    0xBF810000u, /* s_endpgm                                    */
};

int main(void) {
    gcn_runtime_info rt;
    gcn_compile_desc d;
    gcn_shader* sh = NULL;
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
    d.code = k_cs;
    d.code_size = sizeof(k_cs);
    d.code_addr = 0x1000;
    d.user_data[0] = 0x600000;
    d.user_data[2] = 4096;
    d.user_data[3] = 4 | (5 << 3) | (6 << 6) | (7 << 9) | (4 << 12) | (4 << 15);
    d.runtime = &rt;
    d.flags = GCN_COMPILE_EMIT_MSL;
    if (gcn_compile(&d, &sh, err, sizeof(err)) != GCN_OK) {
        printf("compile failed: %s\n", err);
        return 1;
    }
    printf("%s\n%s", gcn_version(), gcn_shader_msl(sh));
    gcn_shader_destroy(sh);
    return 0;
}
