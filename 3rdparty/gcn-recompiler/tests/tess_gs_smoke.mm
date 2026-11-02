// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smoke test for Task 7A:
// - gcn_read_tess_ls_stride & GcnShaderInfo::tess_consts_dword_offset / tess_consts_ptr_base
// - gcn_compile_tess_shaders_msl / gcn_free_tess_shaders_msl (both <= 8 direct buffers and > 8 argument buffers)
// - gcn_compile_gs_shaders_msl / gcn_free_gs_shaders_msl (both <= 8 direct buffers and > 8 argument buffers,
//   plus pass-through VS with and without emulate_depth_negative_one_to_one)
// - Metal runtime compilation (MTLDevice newLibraryWithSource:) of every generated MSL string.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gcn_recompiler.h"

namespace {

struct Region {
    uint64_t base;
    std::vector<uint8_t> data;
};

struct FakeMem {
    std::vector<Region> regions;

    void Add(uint64_t base, const void* p, size_t n) {
        Region r{base, std::vector<uint8_t>(n)};
        std::memcpy(r.data.data(), p, n);
        regions.push_back(std::move(r));
    }

    void Add32(uint64_t base, const std::vector<uint32_t>& w) {
        Add(base, w.data(), w.size() * sizeof(uint32_t));
    }

    static bool Read(void* ctx, uint64_t addr, void* dst, uint64_t size) {
        auto* m = static_cast<FakeMem*>(ctx);
        for (const auto& r : m->regions) {
            if (addr >= r.base && addr + size <= r.base + r.data.size()) {
                std::memcpy(dst, r.data.data() + (addr - r.base), size);
                return true;
            }
        }
        return false;
    }

    static bool ReadGuestMem(void* user, uint64_t guest_va, void* dst, size_t size) {
        return Read(user, guest_va, dst, size);
    }
};

std::vector<uint32_t> WithFooter(std::vector<uint32_t> body, uint64_t hash) {
    std::vector<uint32_t> code = {0xBEEB03FFu, 0u};
    code.insert(code.end(), body.begin(), body.end());
    while (code.size() % 2) {
        code.push_back(0xBF800000u); // s_nop
    }
    code[1] = uint32_t(code.size() / 2 - 1);
    uint8_t bi[32] = {'O', 'r', 'b', 'S', 'h', 'd', 'r', 7};
    const uint32_t length = uint32_t(code.size() * 4);
    uint32_t w = (length & 0xFFFFFFu) << 8;
    std::memcpy(bi + 8, &w, 4);
    std::memcpy(bi + 16, &hash, 8);
    const size_t at = code.size();
    code.resize(at + 8);
    std::memcpy(&code[at], bi, 32);
    return code;
}

std::vector<uint32_t> MakeVSharp(uint64_t base, uint32_t stride, uint32_t num_records,
                                 uint32_t dfmt = 14 /*32_32_32_32*/, uint32_t nfmt = 7 /*float*/) {
    std::vector<uint32_t> v(4);
    v[0] = uint32_t(base);
    v[1] = uint32_t(base >> 32) | (stride << 16);
    v[2] = num_records;
    v[3] = 4u | (5u << 3) | (6u << 6) | (7u << 9) | (nfmt << 12) | (dfmt << 15);
    return v;
}

// GCN instruction encoding helpers
uint32_t EncSMRDLoadDwordx4(uint32_t sdst, uint32_t sbase_reg, uint8_t dw_offset) {
    // s_load_dwordx4 s[sdst:sdst+3], s[sbase_reg:sbase_reg+1], dw_offset
    return (0x18u << 27) | (2u << 22) | (sdst << 15) | ((sbase_reg / 2) << 9) | (1u << 8) | dw_offset;
}

uint32_t EncSMRDBufferLoadDword(uint32_t sdst, uint32_t sbase_reg, uint8_t dw_offset) {
    // s_buffer_load_dword s[sdst], s[sbase_reg:sbase_reg+3], dw_offset
    return (0x18u << 27) | (8u << 22) | (sdst << 15) | ((sbase_reg / 2) << 9) | (1u << 8) | dw_offset;
}

uint32_t EncSOPKMovkI32(uint32_t sdst, uint16_t simm) {
    // s_movk_i32 s[sdst], simm
    return (0xBu << 28) | (0u << 23) | (sdst << 16) | simm;
}

uint32_t EncSOPPSendMsg(uint16_t simm) {
    // s_sendmsg simm (op = 16)
    return (0x17Fu << 23) | (16u << 16) | simm;
}

uint32_t EncVMovB32Imm0(uint32_t vdst) {
    // v_mov_b32 v[vdst], 0 (0x80 = ConstZero)
    return (0x3Fu << 25) | (1u << 9) | (vdst << 17) | 0x80u;
}

uint32_t EncVMovB32FromSgpr(uint32_t vdst, uint32_t sgpr) {
    // v_mov_b32 v[vdst], s[sgpr]
    return (0x3Fu << 25) | (1u << 9) | (vdst << 17) | sgpr;
}

uint32_t EncVAddF32(uint32_t vdst, uint32_t src0, uint32_t vsrc1) {
    // v_add_f32 v[vdst], src0, v[vsrc1] (op = 3)
    return (3u << 25) | (vdst << 17) | (vsrc1 << 9) | src0;
}

void AppendDSWriteB32(std::vector<uint32_t>& code, uint32_t vaddr, uint32_t vdata, uint16_t offset) {
    const uint32_t w0 = (0x36u << 26) | (13u << 18) | uint32_t(offset);
    const uint32_t w1 = (vdata << 8) | vaddr;
    code.push_back(w0);
    code.push_back(w1);
}

void AppendDSReadB32(std::vector<uint32_t>& code, uint32_t vdst, uint32_t vaddr, uint16_t offset) {
    const uint32_t w0 = (0x36u << 26) | (54u << 18) | uint32_t(offset);
    const uint32_t w1 = (vdst << 24) | vaddr;
    code.push_back(w0);
    code.push_back(w1);
}

void AppendMubufLoadDword(std::vector<uint32_t>& code, uint32_t vdst, uint32_t vaddr,
                          uint32_t srsrc_quad, uint32_t soffset_code, uint16_t offset,
                          bool offen, bool glc, bool slc) {
    const uint32_t w0 = (0x38u << 26) | (12u << 18) |
                        (glc ? (1u << 14) : 0u) |
                        (offen ? (1u << 12) : 0u) |
                        (offset & 0xFFFu);
    const uint32_t w1 = (soffset_code << 24) |
                        (slc ? (1u << 22) : 0u) |
                        (srsrc_quad << 16) |
                        (vdst << 8) |
                        vaddr;
    code.push_back(w0);
    code.push_back(w1);
}

void AppendMubufStoreDword(std::vector<uint32_t>& code, uint32_t vdata, uint32_t vaddr,
                           uint32_t srsrc_quad, uint32_t soffset_code, uint16_t offset,
                           bool offen, bool glc, bool slc) {
    const uint32_t w0 = (0x38u << 26) | (28u << 18) |
                        (glc ? (1u << 14) : 0u) |
                        (offen ? (1u << 12) : 0u) |
                        (offset & 0xFFFu);
    const uint32_t w1 = (soffset_code << 24) |
                        (slc ? (1u << 22) : 0u) |
                        (srsrc_quad << 16) |
                        (vdata << 8) |
                        vaddr;
    code.push_back(w0);
    code.push_back(w1);
}

void AppendExp(std::vector<uint32_t>& code, uint32_t target, uint32_t v0, uint32_t v1,
               uint32_t v2, uint32_t v3, bool done) {
    const uint32_t w0 = (0x3Eu << 26) | (done ? (1u << 11) : 0u) | (target << 4) | 0xFu;
    const uint32_t w1 = v0 | (v1 << 8) | (v2 << 16) | (v3 << 24);
    code.push_back(w0);
    code.push_back(w1);
}

bool ValidateMsl(id<MTLDevice> dev, const char* label, const char* src,
                 const char* fn_name = "main0") {
    if (!src || src[0] == '\0') {
        std::fprintf(stderr, "[FAIL] %s: empty MSL\n", label);
        return false;
    }
    if (!dev) {
        return true;
    }
    @autoreleasepool {
        NSError* err = nil;
        MTLCompileOptions* opts = [MTLCompileOptions new];
        opts.languageVersion = MTLLanguageVersion2_3;
        id<MTLLibrary> lib = [dev newLibraryWithSource:[NSString stringWithUTF8String:src]
                                               options:opts
                                                 error:&err];
        if (!lib) {
            std::fprintf(stderr, "[FAIL] %s Metal compile error: %s\n--- MSL ---\n%s\n-----------\n",
                         label, err ? err.localizedDescription.UTF8String : "unknown", src);
            return false;
        }
        id<MTLFunction> fn = [lib newFunctionWithName:[NSString stringWithUTF8String:fn_name]];
        if (!fn) {
            std::fprintf(stderr, "[FAIL] %s: function '%s' not found in library\n", label, fn_name);
            return false;
        }
    }
    return true;
}

bool TestTessShadersAndStride(id<MTLDevice> dev) {
    FakeMem mem;
    const uint64_t ls_addr = 0x100000;
    const uint64_t fs_addr = 0x101000;
    const uint64_t hs_addr = 0x102000;
    const uint64_t vs_addr = 0x103000;
    const uint64_t vtab_addr = 0x104000;
    const uint64_t tess_sharp_table_addr = 0x105000;
    const uint64_t tess_cb_addr = 0x106000;
    const uint64_t extra_cb_table_addr = 0x107000;
    const uint64_t extra_cb_addr = 0x108000;
    const uint64_t vb0_addr = 0x200000;

    // Fetch shader for LS: loads 1 float4 vertex attribute into v[4:7]
    const std::vector<uint32_t> fetch = {
        0xC0840300u, // s_load_dwordx4 s[8:11], s[2:3], 0x0
        0xBF8C007Fu, // s_waitcnt lgkmcnt(0)
        0xE00C2000u, 0x80020400u, // buffer_load_format_xyzw v[4:7], v0, s[8:11], 0 idxen
        0xBF8C0000u, // s_waitcnt 0
        0xBE802000u, // s_setpc_b64 s[0:1]
    };
    mem.Add32(fs_addr, fetch);
    mem.Add32(vtab_addr, MakeVSharp(vb0_addr, 16, 64, 14, 7));

    // Extra cbuffer V# and data so LS/HS/VS have guest constant buffers as well
    mem.Add32(extra_cb_table_addr, MakeVSharp(extra_cb_addr, 0, 64, 14, 7));
    mem.Add32(extra_cb_addr, std::vector<uint32_t>(16, 0x3F800000u));

    // TessellationDataConstantBuffer in guest memory (ls_stride = 16, hs_cp_stride = 16, etc.)
    const std::vector<uint32_t> tess_consts_data = {
        16u,         // [0] ls_stride = 16 bytes (1 float4 attribute)
        16u,         // [1] hs_cp_stride = 16 bytes
        1u,          // [2] num_patches = 1
        48u,         // [3] hs_output_base = 48 (non-zero => non-passthrough)
        16u,         // [4] patch_const_size = 16
        96u,         // [5] patch_const_base = 96
        48u,         // [6] patch_output_size = 48
        0x3F800000u, // [7] off_chip_tessellation_factor_threshold = 1.0f
        0u,          // [8] first_edge_tess_factor_index = 0
    };
    mem.Add32(tess_cb_addr, tess_consts_data);
    // Put the TessConstants V# at dword offset 4 inside tess_sharp_table_addr
    std::vector<uint32_t> tess_sharp_table(8, 0);
    const auto tess_vsharp = MakeVSharp(tess_cb_addr, 0, 36, 4, 4);
    std::memcpy(&tess_sharp_table[4], tess_vsharp.data(), 16);
    mem.Add32(tess_sharp_table_addr, tess_sharp_table);

    // 1. Build LS shader (GCN_HW_LS / GCN_SW_VERTEX):
    //    Calls fetch shader (v[4:7]), loads cbuffer s[12] from s[4:5], adds to v4, writes v[4:7] to LDS 0..12
    std::vector<uint32_t> ls_body = {
        0xBE802100u,                        // s_swappc_b64 s[0:1], s[0:1]
        EncSMRDLoadDwordx4(8, 4, 0),        // s_load_dwordx4 s[8:11], s[4:5], 0
        0xBF8C007Fu,                        // s_waitcnt lgkmcnt(0)
        EncSMRDBufferLoadDword(12, 8, 0),   // s_buffer_load_dword s12, s[8:11], 0
        0xBF8C007Fu,                        // s_waitcnt lgkmcnt(0)
        EncVAddF32(4, 12, 4),               // v_add_f32 v4, s12, v4
        EncVMovB32Imm0(2),                  // v_mov_b32 v2, 0
    };
    AppendDSWriteB32(ls_body, 2, 4, 0);
    AppendDSWriteB32(ls_body, 2, 5, 4);
    AppendDSWriteB32(ls_body, 2, 6, 8);
    AppendDSWriteB32(ls_body, 2, 7, 12);
    ls_body.push_back(0xBF8C0000u); // s_waitcnt 0
    ls_body.push_back(0xBF810000u); // s_endpgm
    mem.Add32(ls_addr, WithFooter(ls_body, 0xA001A002A003A004ULL));

    // 2. Build HS shader (GCN_HW_HS / GCN_SW_TESS_CONTROL):
    //    User data: s[2:3] = pointer to tess_sharp_table_addr (V# at dword offset 4),
    //               s[4:5] = pointer to extra_cb_table_addr.
    //    num_user_data = 6 -> s6 is TessFactorsBufferBase (set by EmitPrologue).
    //    Reads LsStride (dw 0), HsOutputBase (dw 3), PatchConstBase (dw 5) from TessConstants V#.
    //    Reads input CP 0 from LDS, writes output CP to LDS at HsOutputBase + 0..12,
    //    writes PatchConst0 to LDS at PatchConstBase + 0, and writes 4 triangle tess factors to factor buffer.
    std::vector<uint32_t> hs_body = {
        EncSMRDLoadDwordx4(8, 2, 4),        // s_load_dwordx4 s[8:11], s[2:3], 4 (TessConstants V#)
        EncSMRDLoadDwordx4(16, 4, 0),       // s_load_dwordx4 s[16:19], s[4:5], 0 (extra cbuf V#)
        0xBF8C007Fu,                        // s_waitcnt lgkmcnt(0)
        EncSMRDBufferLoadDword(12, 8, 0),   // s12 = TessConstants[0] (LsStride)
        EncSMRDBufferLoadDword(13, 8, 3),   // s13 = TessConstants[3] (HsOutputBase)
        EncSMRDBufferLoadDword(14, 8, 5),   // s14 = TessConstants[5] (PatchConstBase)
        EncSMRDBufferLoadDword(15, 16, 0),  // s15 = extra_cb[0]
        0xBF8C007Fu,                        // s_waitcnt lgkmcnt(0)
        EncVMovB32FromSgpr(2, 12),          // v2 = LsStride (used in InputCP LDS read address)
        EncVAddF32(2, 128 /*0*/, 2),        // keep v2 connected to s12
    };
    // InputCP reads at v2 + 0..12 (since v2 = LsStride = 16, control_point_index = 16 / 16 = 1)
    AppendDSReadB32(hs_body, 4, 2, 0);
    AppendDSReadB32(hs_body, 5, 2, 4);
    AppendDSReadB32(hs_body, 6, 2, 8);
    AppendDSReadB32(hs_body, 7, 2, 12);
    hs_body.push_back(0xBF8C0000u);         // s_waitcnt 0
    hs_body.push_back(EncVAddF32(4, 15, 4)); // v4 = s15 + v4
    // OutputCP writes at v3 = HsOutputBase (replaced by 0 in TessellationPreprocess) + 0..12
    hs_body.push_back(EncVMovB32FromSgpr(3, 13));
    AppendDSWriteB32(hs_body, 3, 4, 0);
    AppendDSWriteB32(hs_body, 3, 5, 4);
    AppendDSWriteB32(hs_body, 3, 6, 8);
    AppendDSWriteB32(hs_body, 3, 7, 12);
    // PatchConst write at v8 = PatchConstBase (replaced by 0 in TessellationPreprocess) + 0
    hs_body.push_back(EncVMovB32FromSgpr(8, 14));
    AppendDSWriteB32(hs_body, 8, 4, 0);
    // Store 4 tessellation factors (3 edge + 1 interior for Triangle domain):
    // v9 = 0 (voffset), soffset = s6 (TessFactorsBufferBase), vdata = v4
    hs_body.push_back(EncVMovB32Imm0(9));
    AppendMubufStoreDword(hs_body, 4, 9, 2 /*s[8:11]*/, 6 /*s6 = TessFactorsBufferBase*/, 0, true, false, false);
    AppendMubufStoreDword(hs_body, 5, 9, 2 /*s[8:11]*/, 6 /*s6 = TessFactorsBufferBase*/, 4, true, false, false);
    AppendMubufStoreDword(hs_body, 6, 9, 2 /*s[8:11]*/, 6 /*s6 = TessFactorsBufferBase*/, 8, true, false, false);
    AppendMubufStoreDword(hs_body, 7, 9, 2 /*s[8:11]*/, 6 /*s6 = TessFactorsBufferBase*/, 12, true, false, false);
    hs_body.push_back(0xBF8C0000u); // s_waitcnt 0
    hs_body.push_back(0xBF810000u); // s_endpgm
    mem.Add32(hs_addr, WithFooter(hs_body, 0xB001B002B003B004ULL));

    // 3. Build TES/VS shader (GCN_HW_VS / GCN_SW_TESSELLATION_EVAL):
    //    Reads HsOutputBase (dw 3) and PatchConstBase (dw 5) from TessConstants V# at s[2:3]+4,
    //    reads OutputCP[0].xyzw and PatchConst0.x from LDS, combines with domain (v0, v1) and extra_cb,
    //    and exports pos0 and param0.
    std::vector<uint32_t> vs_body = {
        EncSMRDLoadDwordx4(8, 2, 4),        // s_load_dwordx4 s[8:11], s[2:3], 4 (TessConstants V#)
        EncSMRDLoadDwordx4(16, 4, 0),       // s_load_dwordx4 s[16:19], s[4:5], 0 (extra cbuf V#)
        0xBF8C007Fu,                        // s_waitcnt lgkmcnt(0)
        EncSMRDBufferLoadDword(12, 8, 3),   // s12 = TessConstants[3] (HsOutputBase)
        EncSMRDBufferLoadDword(13, 8, 5),   // s13 = TessConstants[5] (PatchConstBase)
        EncSMRDBufferLoadDword(14, 16, 0),  // s14 = extra_cb[0]
        0xBF8C007Fu,                        // s_waitcnt lgkmcnt(0)
        EncVMovB32FromSgpr(4, 12),          // v4 = HsOutputBase
        EncVMovB32FromSgpr(5, 13),          // v5 = PatchConstBase
    };
    AppendDSReadB32(vs_body, 6, 4, 0);
    AppendDSReadB32(vs_body, 7, 4, 4);
    AppendDSReadB32(vs_body, 8, 4, 8);
    AppendDSReadB32(vs_body, 9, 4, 12);
    AppendDSReadB32(vs_body, 10, 5, 0);     // read PatchConst0.x
    vs_body.push_back(0xBF8C0000u);         // s_waitcnt 0
    vs_body.push_back(EncVAddF32(6, 256 /*v0*/, 6)); // v6 += tess_coord.x
    vs_body.push_back(EncVAddF32(7, 257 /*v1*/, 7)); // v7 += tess_coord.y
    vs_body.push_back(EncVAddF32(6, 14 /*s14*/, 6)); // v6 += extra_cb[0]
    AppendExp(vs_body, 0 /*Param0 (32)*/, 10, 7, 8, 9, false);
    // Note: in GCN EXP target, 12..15 is Pos0..Pos3, 32..63 is Param0..Param31
    vs_body.pop_back();
    vs_body.pop_back();
    AppendExp(vs_body, 32 /*Param0*/, 10, 7, 8, 9, false);
    AppendExp(vs_body, 12 /*Pos0*/, 6, 7, 8, 9, true);
    vs_body.push_back(0xBF810000u); // s_endpgm
    mem.Add32(vs_addr, WithFooter(vs_body, 0xC001C002C003C004ULL));

    gcn_profile prof;
    gcn_profile_default_metal(&prof);

    // Compile LS
    gcn_runtime_info ls_rt{};
    ls_rt.hw_stage = GCN_HW_LS;
    ls_rt.sw_stage = GCN_SW_VERTEX;
    ls_rt.num_allocated_vgprs = 16;
    ls_rt.num_user_data = 6;
    ls_rt.num_input_vgprs = 2;
    ls_rt.ls.ls_stride = 16;

    gcn_compile_desc ls_desc{};
    ls_desc.hw_stage = GCN_HW_LS;
    ls_desc.sw_stage = GCN_SW_VERTEX;
    ls_desc.code_addr = ls_addr;
    ls_desc.user_data[0] = uint32_t(fs_addr);
    ls_desc.user_data[1] = uint32_t(fs_addr >> 32);
    ls_desc.user_data[2] = uint32_t(vtab_addr);
    ls_desc.user_data[3] = uint32_t(vtab_addr >> 32);
    ls_desc.user_data[4] = uint32_t(extra_cb_table_addr);
    ls_desc.user_data[5] = uint32_t(extra_cb_table_addr >> 32);
    ls_desc.runtime = &ls_rt;
    ls_desc.profile = &prof;
    ls_desc.read_guest = FakeMem::Read;
    ls_desc.read_ctx = &mem;
    ls_desc.flags = GCN_COMPILE_EMIT_MSL;

    char err[1024] = {};
    gcn_shader* ls_sh = nullptr;
    if (gcn_compile(&ls_desc, &ls_sh, err, sizeof(err)) != GCN_OK || !ls_sh) {
        std::fprintf(stderr, "[FAIL] LS compile failed: %s\n", err);
        return false;
    }

    // Compile HS
    gcn_runtime_info hs_rt{};
    hs_rt.hw_stage = GCN_HW_HS;
    hs_rt.sw_stage = GCN_SW_TESS_CONTROL;
    hs_rt.num_allocated_vgprs = 16;
    hs_rt.num_user_data = 6;
    hs_rt.tcs.num_input_control_points = 3;
    hs_rt.tcs.num_threads = 3;
    hs_rt.tcs.tess_type = 1; // Triangle

    gcn_compile_desc hs_desc{};
    hs_desc.hw_stage = GCN_HW_HS;
    hs_desc.sw_stage = GCN_SW_TESS_CONTROL;
    hs_desc.code_addr = hs_addr;
    hs_desc.user_data[2] = uint32_t(tess_sharp_table_addr);
    hs_desc.user_data[3] = uint32_t(tess_sharp_table_addr >> 32);
    hs_desc.user_data[4] = uint32_t(extra_cb_table_addr);
    hs_desc.user_data[5] = uint32_t(extra_cb_table_addr >> 32);
    hs_desc.runtime = &hs_rt;
    hs_desc.profile = &prof;
    hs_desc.read_guest = FakeMem::Read;
    hs_desc.read_ctx = &mem;
    hs_desc.flags = GCN_COMPILE_EMIT_MSL;

    gcn_shader* hs_sh = nullptr;
    if (gcn_compile(&hs_desc, &hs_sh, err, sizeof(err)) != GCN_OK || !hs_sh) {
        std::fprintf(stderr, "[FAIL] HS compile failed: %s\n", err);
        gcn_shader_destroy(ls_sh);
        return false;
    }

    // Verify Deliverable 1: tess_consts_dword_offset, tess_consts_ptr_base, and gcn_read_tess_ls_stride
    gcn_shader_info hs_info{};
    gcn_shader_get_info(hs_sh, &hs_info);
    if (hs_info.tess_consts_dword_offset != 4 || hs_info.tess_consts_ptr_base != 2) {
        std::fprintf(stderr,
                     "[FAIL] HS info tess_consts mismatch: dword_offset=%d (expected 4), ptr_base=%u (expected 2)\n",
                     hs_info.tess_consts_dword_offset, hs_info.tess_consts_ptr_base);
        gcn_shader_destroy(hs_sh);
        gcn_shader_destroy(ls_sh);
        return false;
    }
    GcnGuestMemory guest_mem{};
    guest_mem.read = FakeMem::Read;
    guest_mem.ctx = &mem;
    const uint32_t ls_stride = gcn_read_tess_ls_stride(&hs_info, hs_desc.user_data, 16, guest_mem);
    if (ls_stride != 16) {
        std::fprintf(stderr, "[FAIL] gcn_read_tess_ls_stride returned %u (expected 16)\n", ls_stride);
        gcn_shader_destroy(hs_sh);
        gcn_shader_destroy(ls_sh);
        return false;
    }
    // Also test inline V# case and invalid (< 0x10000ULL) V# case for gcn_read_tess_ls_stride
    {
        gcn_shader_info inline_info = hs_info;
        inline_info.tess_consts_ptr_base = UINT32_MAX; // inline in user_data
        inline_info.tess_consts_dword_offset = 0;
        uint32_t inline_ud[4];
        std::memcpy(inline_ud, tess_vsharp.data(), 16);
        const uint32_t stride_inline = gcn_read_tess_ls_stride(&inline_info, inline_ud, 4, guest_mem);
        if (stride_inline != 16) {
            std::fprintf(stderr, "[FAIL] inline gcn_read_tess_ls_stride returned %u (expected 16)\n",
                         stride_inline);
            return false;
        }
        const auto bad_vsharp = MakeVSharp(0x8000ULL /* < 0x10000ULL */, 0, 36, 4, 4);
        std::memcpy(inline_ud, bad_vsharp.data(), 16);
        if (gcn_read_tess_ls_stride(&inline_info, inline_ud, 4, guest_mem) != 0) {
            std::fprintf(stderr, "[FAIL] low-address V# should return 0\n");
            return false;
        }
    }

    // Compile TES (VS)
    gcn_runtime_info vs_rt{};
    vs_rt.hw_stage = GCN_HW_VS;
    vs_rt.sw_stage = GCN_SW_TESS_EVAL;
    vs_rt.num_allocated_vgprs = 16;
    vs_rt.num_user_data = 6;
    vs_rt.tes.tess_type = 1; // Triangle
    vs_rt.tes.tess_partitioning = 0; // Integer
    vs_rt.tes.tess_topology = 2; // TriangleCw

    gcn_compile_desc vs_desc{};
    vs_desc.hw_stage = GCN_HW_VS;
    vs_desc.sw_stage = GCN_SW_TESS_EVAL;
    vs_desc.code_addr = vs_addr;
    vs_desc.user_data[2] = uint32_t(tess_sharp_table_addr);
    vs_desc.user_data[3] = uint32_t(tess_sharp_table_addr >> 32);
    vs_desc.user_data[4] = uint32_t(extra_cb_table_addr);
    vs_desc.user_data[5] = uint32_t(extra_cb_table_addr >> 32);
    vs_desc.runtime = &vs_rt;
    vs_desc.profile = &prof;
    vs_desc.read_guest = FakeMem::Read;
    vs_desc.read_ctx = &mem;
    vs_desc.flags = GCN_COMPILE_EMIT_MSL;

    gcn_shader* vs_sh = nullptr;
    if (gcn_compile(&vs_desc, &vs_sh, err, sizeof(err)) != GCN_OK || !vs_sh) {
        std::fprintf(stderr, "[FAIL] TES compile failed: %s\n", err);
        gcn_shader_destroy(hs_sh);
        gcn_shader_destroy(ls_sh);
        return false;
    }

    // Test gcn_compile_tess_shaders_msl in both direct-binding mode (<= 8 buffers)
    // and argument-buffer mode (max_buffer_slots = 1 so > max_buffer_slots triggers argbuf)
    for (int mode = 0; mode < 2; ++mode) {
        GcnMslOptions opts;
        gcn_msl_options_default(&opts);
        if (mode == 1) {
            opts.max_buffer_slots = 1; // force argument buffer for set 0 while keeping 13..18 discrete
        }
        GcnTessShaderSet set{};
        if (!gcn_compile_tess_shaders_msl(ls_sh, hs_sh, vs_sh, &opts, &set)) {
            std::fprintf(stderr, "[FAIL] gcn_compile_tess_shaders_msl failed (mode=%d): %s\n",
                         mode, set.error ? set.error : "unknown");
            gcn_free_tess_shaders_msl(&set);
            gcn_shader_destroy(vs_sh);
            gcn_shader_destroy(hs_sh);
            gcn_shader_destroy(ls_sh);
            return false;
        }
        const bool ok =
            ValidateMsl(dev, mode ? "tess_ls_argbuf" : "tess_ls_direct", set.ls_msl) &&
            ValidateMsl(dev, mode ? "tess_ls_u16_argbuf" : "tess_ls_u16_direct", set.ls_u16_msl) &&
            ValidateMsl(dev, mode ? "tess_ls_u32_argbuf" : "tess_ls_u32_direct", set.ls_u32_msl) &&
            ValidateMsl(dev, mode ? "tess_hs_argbuf" : "tess_hs_direct", set.hs_msl) &&
            ValidateMsl(dev, mode ? "tess_vs_argbuf" : "tess_vs_direct", set.vs_msl);
        if (!ok || set.output_control_points != 3 || set.cp_stride == 0 ||
            set.domain != GCN_TESS_DOMAIN_TRIANGLES) {
            std::fprintf(stderr,
                         "[FAIL] tess shader set validation failed (mode=%d, cp_count=%u, cp_stride=%u, domain=%u)\n",
                         mode, set.output_control_points, set.cp_stride, unsigned(set.domain));
            gcn_free_tess_shaders_msl(&set);
            gcn_shader_destroy(vs_sh);
            gcn_shader_destroy(hs_sh);
            gcn_shader_destroy(ls_sh);
            return false;
        }
        if (mode == 1) {
            if (!std::strstr(set.ls_msl, "spvDescriptorSet0") ||
                !std::strstr(set.hs_msl, "spvDescriptorSet0") ||
                !std::strstr(set.vs_msl, "spvDescriptorSet0") ||
                !std::strstr(set.ls_msl, "[[buffer(13)]]") ||
                !std::strstr(set.hs_msl, "[[buffer(13)]]") ||
                !std::strstr(set.hs_msl, "[[buffer(14)]]") ||
                !std::strstr(set.hs_msl, "[[buffer(15)]]") ||
                !std::strstr(set.vs_msl, "[[buffer(14)]]")) {
                std::fprintf(stderr,
                             "[FAIL] tess argument-buffer mode missing expected descriptor set or discrete inter-stage buffer slots\n");
                gcn_free_tess_shaders_msl(&set);
                return false;
            }
        }
        std::printf("[OK] tessellation MSL set (%s): ls_stride=%u cp_count=%u cp_stride=%u patch_stride=%u\n",
                    mode ? "argbuf" : "direct", ls_stride, set.output_control_points, set.cp_stride,
                    set.patch_stride);
        gcn_free_tess_shaders_msl(&set);
    }

    gcn_shader_destroy(vs_sh);
    gcn_shader_destroy(hs_sh);
    gcn_shader_destroy(ls_sh);
    return true;
}

bool TestGsShaders(id<MTLDevice> dev) {
    FakeMem mem;
    const uint64_t es_addr = 0x300000;
    const uint64_t fs_addr = 0x301000;
    const uint64_t gs_addr = 0x302000;
    const uint64_t vtab_addr = 0x304000;
    const uint64_t extra_cb_table_addr = 0x307000;
    const uint64_t extra_cb_addr = 0x308000;
    const uint64_t vb0_addr = 0x400000;

    // Fetch shader for ES: loads 1 float4 vertex attribute into v[4:7]
    const std::vector<uint32_t> fetch = {
        0xC0840300u, // s_load_dwordx4 s[8:11], s[2:3], 0x0
        0xBF8C007Fu, // s_waitcnt lgkmcnt(0)
        0xE00C2000u, 0x80020400u, // buffer_load_format_xyzw v[4:7], v0, s[8:11], 0 idxen
        0xBF8C0000u, // s_waitcnt 0
        0xBE802000u, // s_setpc_b64 s[0:1]
    };
    mem.Add32(fs_addr, fetch);
    mem.Add32(vtab_addr, MakeVSharp(vb0_addr, 16, 64, 14, 7));
    mem.Add32(extra_cb_table_addr, MakeVSharp(extra_cb_addr, 0, 64, 14, 7));
    mem.Add32(extra_cb_addr, std::vector<uint32_t>(16, 0x3F800000u));

    // 1. Build ES shader (GCN_HW_ES / GCN_SW_VERTEX):
    //    Calls fetch shader (v[4:7]), reads extra_cb[0] into s12, adds to v4,
    //    and writes Position0 (offsets 0..12) and Param0 (offsets 16..28) to ESGS ring (glc=1, slc=1).
    std::vector<uint32_t> es_body = {
        0xBE802100u,                        // s_swappc_b64 s[0:1], s[0:1]
        EncSMRDLoadDwordx4(8, 4, 0),        // s_load_dwordx4 s[8:11], s[4:5], 0
        0xBF8C007Fu,                        // s_waitcnt lgkmcnt(0)
        EncSMRDBufferLoadDword(12, 8, 0),   // s12 = extra_cb[0]
        0xBF8C007Fu,                        // s_waitcnt lgkmcnt(0)
        EncVAddF32(4, 12, 4),               // v4 += s12
        EncVMovB32Imm0(2),                  // v2 = 0
    };
    // ESGS ring stores (glc=1, slc=1): Position0 (0..12) and Param0 (16..28)
    AppendMubufStoreDword(es_body, 4, 2, 2, 128 /*0*/, 0, true, true, true);
    AppendMubufStoreDword(es_body, 5, 2, 2, 128 /*0*/, 4, true, true, true);
    AppendMubufStoreDword(es_body, 6, 2, 2, 128 /*0*/, 8, true, true, true);
    AppendMubufStoreDword(es_body, 7, 2, 2, 128 /*0*/, 12, true, true, true);
    AppendMubufStoreDword(es_body, 4, 2, 2, 128 /*0*/, 16, true, true, true);
    AppendMubufStoreDword(es_body, 5, 2, 2, 128 /*0*/, 20, true, true, true);
    AppendMubufStoreDword(es_body, 6, 2, 2, 128 /*0*/, 24, true, true, true);
    AppendMubufStoreDword(es_body, 7, 2, 2, 128 /*0*/, 28, true, true, true);
    es_body.push_back(0xBF8C0000u); // s_waitcnt 0
    es_body.push_back(0xBF810000u); // s_endpgm
    mem.Add32(es_addr, WithFooter(es_body, 0xD001D002D003D004ULL));

    // 2. Build VS copy shader (for rt.gs.vs_copy_addr):
    //    3 output vertices -> comp_stride = 3 * 64 = 192.
    //    8 components: 0..3 -> Pos0 (offsets 0, 192, 384, 576), 4..7 -> Param0 (offsets 768, 960, 1152, 1344).
    const uint64_t copy_addr = 0x303000;
    std::vector<uint32_t> copy_body;
    for (uint32_t c = 0; c < 8; ++c) {
        AppendMubufLoadDword(copy_body, c, 0, 0, 128 /*ConstZero*/, uint16_t(c * 192), true, false, false);
    }
    copy_body.push_back(0xBF8C0000u); // s_waitcnt 0
    AppendExp(copy_body, 32 /*Param0*/, 4, 5, 6, 7, false);
    AppendExp(copy_body, 12 /*Pos0*/, 0, 1, 2, 3, true);
    copy_body.push_back(0xBF810000u); // s_endpgm
    const auto copy_shader = WithFooter(copy_body, 0xE001E002E003E004ULL);
    mem.Add32(copy_addr, copy_shader);

    // 3. Build GS shader (GCN_HW_GS / GCN_SW_GEOMETRY):
    //    Loads extra_cb[0] from s[4:5], reads Position0.xyzw and Param0.xyzw of input vertex 0
    //    from ESGS ring (glc=1, slc=1, bucket*256 in soffset), emits 3 vertices and cuts primitive.
    std::vector<uint32_t> gs_body = {
        EncSMRDLoadDwordx4(16, 4, 0),       // s_load_dwordx4 s[16:19], s[4:5], 0 (extra cbuf V#)
        0xBF8C007Fu,                        // s_waitcnt lgkmcnt(0)
        EncSMRDBufferLoadDword(20, 16, 0),  // s20 = extra_cb[0]
        0xBF8C007Fu,                        // s_waitcnt lgkmcnt(0)
    };
    for (uint32_t c = 0; c < 8; ++c) {
        gs_body.push_back(EncSOPKMovkI32(8 + c, uint16_t(c * 256)));
    }
    // v10 = 0 (vertex 0 offset: 0 >> 2 == 0)
    gs_body.push_back(EncVMovB32Imm0(10));
    for (uint32_t c = 0; c < 8; ++c) {
        AppendMubufLoadDword(gs_body, 4 + c, 10, 0, 8 + c, 0, true, true, true);
    }
    gs_body.push_back(0xBF8C0000u); // s_waitcnt 0
    gs_body.push_back(EncVAddF32(4, 20, 4)); // v4 += s20 (extra_cb[0])

    // Emit 3 vertices (comp_ofs = 3 * 4 = 12, so component c of vertex v is at offset c * 12 + v * 4)
    for (uint32_t vtx = 0; vtx < 3; ++vtx) {
        for (uint32_t c = 0; c < 8; ++c) {
            AppendMubufStoreDword(gs_body, 4 + c, 10, 0, 128 /*0*/, uint16_t(c * 12 + vtx * 4),
                                  true, true, true);
        }
        // s_sendmsg sendmsg(MSG_GS=2, GS_OP_EMIT=2) => 0x22
        gs_body.push_back(EncSOPPSendMsg(0x22));
    }
    // s_sendmsg sendmsg(MSG_GS=2, GS_OP_CUT=1) => 0x12
    gs_body.push_back(EncSOPPSendMsg(0x12));
    gs_body.push_back(0xBF810000u); // s_endpgm
    mem.Add32(gs_addr, WithFooter(gs_body, 0xF001F002F003F004ULL));

    gcn_profile prof;
    gcn_profile_default_metal(&prof);

    // Compile ES
    gcn_runtime_info es_rt{};
    es_rt.hw_stage = GCN_HW_ES;
    es_rt.sw_stage = GCN_SW_VERTEX;
    es_rt.num_allocated_vgprs = 16;
    es_rt.num_user_data = 6;
    es_rt.es.vertex_data_size = 8; // 8 dwords (Pos0 + Param0)

    gcn_compile_desc es_desc{};
    es_desc.hw_stage = GCN_HW_ES;
    es_desc.sw_stage = GCN_SW_VERTEX;
    es_desc.code_addr = es_addr;
    es_desc.user_data[0] = uint32_t(fs_addr);
    es_desc.user_data[1] = uint32_t(fs_addr >> 32);
    es_desc.user_data[2] = uint32_t(vtab_addr);
    es_desc.user_data[3] = uint32_t(vtab_addr >> 32);
    es_desc.user_data[4] = uint32_t(extra_cb_table_addr);
    es_desc.user_data[5] = uint32_t(extra_cb_table_addr >> 32);
    es_desc.runtime = &es_rt;
    es_desc.profile = &prof;
    es_desc.read_guest = FakeMem::Read;
    es_desc.read_ctx = &mem;
    es_desc.flags = GCN_COMPILE_EMIT_MSL;

    char err[1024] = {};
    gcn_shader* es_sh = nullptr;
    if (gcn_compile(&es_desc, &es_sh, err, sizeof(err)) != GCN_OK || !es_sh) {
        std::fprintf(stderr, "[FAIL] ES compile failed: %s\n", err);
        return false;
    }

    // Compile GS
    gcn_runtime_info gs_rt{};
    gs_rt.hw_stage = GCN_HW_GS;
    gs_rt.sw_stage = GCN_SW_GEOMETRY;
    gs_rt.num_allocated_vgprs = 16;
    gs_rt.num_user_data = 6;
    gs_rt.gs.output_vertices = 3;
    gs_rt.gs.num_invocations = 1;
    gs_rt.gs.in_primitive = 4; // TriangleList
    gs_rt.gs.out_primitive[0] = 2; // TriangleStrip
    gs_rt.gs.in_vertex_data_size = 8;
    gs_rt.gs.out_vertex_data_size = 8;
    gs_rt.gs.vs_copy_addr = copy_addr;
    gs_rt.gs.vs_copy_size = 0; // use OrbShdr footer

    gcn_compile_desc gs_desc{};
    gs_desc.hw_stage = GCN_HW_GS;
    gs_desc.sw_stage = GCN_SW_GEOMETRY;
    gs_desc.code_addr = gs_addr;
    gs_desc.user_data[4] = uint32_t(extra_cb_table_addr);
    gs_desc.user_data[5] = uint32_t(extra_cb_table_addr >> 32);
    gs_desc.runtime = &gs_rt;
    gs_desc.profile = &prof;
    gs_desc.read_guest = FakeMem::Read;
    gs_desc.read_ctx = &mem;
    gs_desc.flags = GCN_COMPILE_EMIT_MSL;

    gcn_shader* gs_sh = nullptr;
    if (gcn_compile(&gs_desc, &gs_sh, err, sizeof(err)) != GCN_OK || !gs_sh) {
        std::fprintf(stderr, "[FAIL] GS compile failed: %s\n", err);
        gcn_shader_destroy(es_sh);
        return false;
    }

    // Test gcn_compile_gs_shaders_msl in both direct-binding mode (<= 8 buffers)
    // and argument-buffer mode (max_buffer_slots = 1), and both depth emulation modes
    for (int mode = 0; mode < 2; ++mode) {
        GcnMslOptions opts;
        gcn_msl_options_default(&opts);
        if (mode == 1) {
            opts.max_buffer_slots = 1; // force argument buffer for set 0 while keeping set 7 (13..16) discrete
        }
        GcnGsShaderSet set{};
        const bool depth_neg_one = (mode == 1);
        if (!gcn_compile_gs_shaders_msl(es_sh, gs_sh, depth_neg_one, &opts, &set)) {
            std::fprintf(stderr, "[FAIL] gcn_compile_gs_shaders_msl failed (mode=%d): %s\n",
                         mode, set.error ? set.error : "unknown");
            gcn_free_gs_shaders_msl(&set);
            gcn_shader_destroy(gs_sh);
            gcn_shader_destroy(es_sh);
            return false;
        }
        const bool ok =
            ValidateMsl(dev, mode ? "gs_es_argbuf" : "gs_es_direct", set.es_msl) &&
            ValidateMsl(dev, mode ? "gs_es_u16_argbuf" : "gs_es_u16_direct", set.es_u16_msl) &&
            ValidateMsl(dev, mode ? "gs_es_u32_argbuf" : "gs_es_u32_direct", set.es_u32_msl) &&
            ValidateMsl(dev, mode ? "gs_kernel_argbuf" : "gs_kernel_direct", set.gs_msl) &&
            ValidateMsl(dev, mode ? "gs_passthrough_vs_depth" : "gs_passthrough_vs", set.vs_msl) &&
            ValidateMsl(dev, mode ? "gs_passthrough_vs_bb_gs_pass" : "gs_passthrough_vs_bb_gs_pass",
                        set.vs_msl, "bb_gs_pass");
        if (!ok || set.input_vertices != 3 || set.max_vertices != 3 ||
            set.es_stride == 0 || set.record_stride == 0 ||
            set.input_primitive != GCN_GS_INPUT_TRIANGLES ||
            set.output_primitive != GCN_GS_OUTPUT_TRIANGLE_STRIP) {
            std::fprintf(stderr,
                         "[FAIL] GS shader set validation failed (mode=%d, in_verts=%u, max_out=%u, es_stride=%u, rec_stride=%u)\n",
                         mode, set.input_vertices, set.max_vertices, set.es_stride, set.record_stride);
            gcn_free_gs_shaders_msl(&set);
            gcn_shader_destroy(gs_sh);
            gcn_shader_destroy(es_sh);
            return false;
        }
        if (mode == 1) {
            if (!std::strstr(set.es_msl, "spvDescriptorSet0") ||
                !std::strstr(set.gs_msl, "spvDescriptorSet0") ||
                !std::strstr(set.es_msl, "[[buffer(13)]]") ||
                !std::strstr(set.gs_msl, "[[buffer(13)]]") ||
                !std::strstr(set.gs_msl, "[[buffer(14)]]") ||
                !std::strstr(set.gs_msl, "[[buffer(15)]]") ||
                !std::strstr(set.gs_msl, "[[buffer(16)]]") ||
                !std::strstr(set.vs_msl, "0.5f")) {
                std::fprintf(stderr,
                             "[FAIL] GS argument-buffer / depth-emulation mode missing expected MSL bindings\n");
                gcn_free_gs_shaders_msl(&set);
                return false;
            }
        }
        std::printf("[OK] geometry MSL set (%s): in_verts=%u max_out=%u es_stride=%u rec_stride=%u\n",
                    mode ? "argbuf+depth_neg1" : "direct", set.input_vertices, set.max_vertices,
                    set.es_stride, set.record_stride);
        gcn_free_gs_shaders_msl(&set);
    }

    gcn_shader_destroy(gs_sh);
    gcn_shader_destroy(es_sh);
    return true;
}

} // namespace

int main() {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) {
        std::printf("warning: no Metal device available; skipping MTLLibrary compilation check\n");
    }
    if (!TestTessShadersAndStride(dev)) {
        return 1;
    }
    if (!TestGsShaders(dev)) {
        return 1;
    }
    std::printf("ALL TESS & GS SMOKE TESTS PASSED\n");
    return 0;
}
