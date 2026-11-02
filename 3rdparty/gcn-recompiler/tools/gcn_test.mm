// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// gcn_test: host test tool for libgcn_recompiler.
//
//   gcn_test --selftest                 built-in hand-written GCN VS/PS/CS programs
//   gcn_test [opts] <shader.bin>...     dumped shaders: <stage>_<addr>.bin + <stage>_<addr>.txt
//                                       (.txt = "# <stage> program at 0x<addr>, ..." header and
//                                        "<reg hex> <value hex>" Liverpool register lines;
//                                        optional "mem <hexaddr> <file>" lines map guest memory)
// options:
//   --zero-mem    unmapped guest reads succeed (zeros; 256-byte code reads return s_setpc_b64,
//                 32-byte aligned data looks like a 2D RGBA8 T#) - lets shaders whose sharps
//                 live in guest memory compile without a memory dump. TEST ONLY.
//   --quiet       only print one line per shader
//   --msl-out D   write <name>.metal (and .spv) into directory D
//   --no-metal    skip MTLDevice newLibraryWithSource validation
//   --dump-dir D  write shadPS4 IR dumps into D/dumps
//   --max-buffer-slots N  lower gcn_msl_options.max_buffer_slots (exercises the argument
//                 buffer fallback used by shaders with more buffers than Metal slots)
//
// Every MSL is validated by compiling it with MTLDevice newLibraryWithSource (the offline
// `xcrun metal` toolchain is not installed on this machine).

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
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
    bool zero_mem = false;

    void Add(uint64_t base, const void* p, size_t n) {
        Region r{base, std::vector<uint8_t>(n)};
        std::memcpy(r.data.data(), p, n);
        regions.push_back(std::move(r));
    }
    void Add32(uint64_t base, const std::vector<uint32_t>& w) {
        Add(base, w.data(), w.size() * 4);
    }

    static bool Read(void* ctx, uint64_t addr, void* dst, uint64_t size) {
        auto* m = static_cast<FakeMem*>(ctx);
        for (const auto& r : m->regions) {
            if (addr >= r.base && addr + size <= r.base + r.data.size()) {
                std::memcpy(dst, r.data.data() + (addr - r.base), size);
                return true;
            }
        }
        if (!m->zero_mem) {
            return false;
        }
        auto* d = static_cast<uint32_t*>(dst);
        if (size == 256) {
            for (uint64_t i = 0; i < size / 4; i++) {
                d[i] = 0xBE802000u; // s_setpc_b64 s[0:1] -> empty fetch shader
            }
            return true;
        }
        static const uint32_t tsharp[8] = {0x00001000, 0x00A00000, 0x003FC0FF, 0x90000FAC,
                                           0x001FE000, 0, 0, 0};
        auto* b = static_cast<uint8_t*>(dst);
        for (uint64_t i = 0; i < size; i++) {
            const uint64_t a = addr + i;
            b[i] = reinterpret_cast<const uint8_t*>(tsharp)[a & 31];
        }
        return true;
    }
};

struct Options {
    bool quiet = false;
    bool metal = true;
    bool zero_mem = false;
    std::string msl_out;
};

id<MTLDevice> g_device = nil;
uint32_t g_max_buffer_slots = 0; // --max-buffer-slots (0 = library default)

bool ValidateMsl(const char* src, std::string& err) {
    if (!g_device) {
        err = "no Metal device";
        return false;
    }
    @autoreleasepool {
        NSError* e = nil;
        MTLCompileOptions* o = [MTLCompileOptions new];
        o.languageVersion = MTLLanguageVersion2_3;
        id<MTLLibrary> lib = [g_device newLibraryWithSource:[NSString stringWithUTF8String:src]
                                                    options:o
                                                      error:&e];
        if (!lib) {
            err = e ? e.localizedDescription.UTF8String : "unknown";
            return false;
        }
        if (![lib newFunctionWithName:@"main0"]) {
            err = "main0 not found";
            return false;
        }
    }
    return true;
}

const char* BufType(uint32_t t) {
    static const char* n[] = {"guest", "flatbuf", "bda_pagetable", "fault", "gds", "shared_mem",
                              "clip_planes"};
    return t < 7 ? n[t] : "?";
}

void PrintFetch(const gcn_sharp_fetch& f) {
    if (f.kind == 0) {
        std::printf("flatbuf[%u..%u]", f.offsets[0], f.offsets[0] + f.num_dwords - 1);
    } else if (f.kind == 2) {
        std::printf("invalid");
    } else {
        std::printf("multi{");
        for (int i = 0; i < f.num_dwords; i++) {
            if ((f.load_mask >> i) & 1) {
                std::printf("%sflat[%u]", i ? "," : "", f.offsets[i]);
            } else {
                std::printf("%s0x%x", i ? "," : "", f.immediates[i]);
            }
        }
        std::printf("}");
    }
}

void PrintTable(const gcn_shader* sh, const uint32_t ud[16], FakeMem& mem) {
    gcn_shader_info info;
    gcn_shader_get_info(sh, &info);
    std::printf("  hash=%016llx flatbuf=%u dw buffers=%u images=%u samplers=%u fmasks=%u "
                "attribs=%u mrt_mask=0x%x discard=%d failed=%d null_image_reads=%u tables=%u\n",
                (unsigned long long)info.pgm_hash, info.flatbuf_size_dw, info.num_buffers,
                info.num_images, info.num_samplers, info.num_fmasks, info.num_vertex_attribs,
                info.mrt_mask, info.has_discard, info.translation_failed, info.null_image_reads,
                info.num_image_tables);
    std::printf("  msl: push_constant buffer(%d) first_free_buffer=%u textures=%u samplers=%u\n",
                info.msl_push_constant_buffer, info.msl_first_free_buffer, info.msl_num_textures,
                info.msl_num_samplers);
    std::vector<uint32_t> flat(info.flatbuf_size_dw);
    const bool flat_ok = gcn_shader_build_flatbuf(sh, ud, FakeMem::Read, &mem, flat.data(),
                                                  flat.size());
    if (info.flatbuf_size_dw > 16) {
        std::printf("  flatbuf(%s):", flat_ok ? "ok" : "partial");
        for (uint32_t i = 16; i < info.flatbuf_size_dw && i < 48; i++) {
            std::printf(" %08x", flat[i]);
        }
        std::printf("\n");
    }
    for (uint32_t i = 0; i < info.num_buffers; i++) {
        gcn_buffer_binding b;
        gcn_shader_get_buffer(sh, i, &b);
        std::printf("  buffer[%u] spirv_binding=%u type=%s written=%d formatted=%d types=0x%x "
                    "postop=%u sharp=",
                    i, b.spirv_binding, BufType(b.type), b.is_written, b.is_formatted,
                    b.used_types, b.post_op);
        PrintFetch(b.sharp);
        std::printf(" msl=[");
        for (uint32_t k = 0; k < b.num_msl_buffers; k++) {
            std::printf("%sbuffer(%u)", k ? "," : "", b.msl_buffers[k]);
        }
        std::printf("]");
        uint32_t v[4];
        if (gcn_shader_fetch_buffer_sharp(sh, i, flat.data(), info.code_addr, v) &&
            b.type == 0) {
            gcn_buffer_desc d;
            gcn_decode_buffer(v, &d);
            std::printf(" -> V# addr=0x%llx stride=%u records=%u size=%u dfmt=%u nfmt=%u",
                        (unsigned long long)d.base_address, d.stride, d.num_records,
                        d.size_bytes, d.data_format, d.num_format);
        }
        std::printf("\n");
    }
    for (uint32_t i = 0; i < info.num_images; i++) {
        gcn_image_binding b;
        gcn_shader_get_image(sh, i, &b);
        std::printf("  image[%u] spirv_binding=%u n=%u storage=%d depth=%d array=%d int=%d "
                    "view_type=%u sharp=",
                    i, b.spirv_binding, b.num_bindings, b.is_storage, b.is_depth, b.is_array,
                    b.is_integer, b.view_type);
        PrintFetch(b.sharp);
        std::printf(" msl=texture(%d)", (int)b.msl_texture);
        if (b.table != UINT32_MAX) {
            std::printf(" table=%u entry=%u", b.table, b.table_entry);
        }
        uint32_t t[8];
        if (gcn_shader_fetch_image_sharp(sh, i, flat.data(), t)) {
            gcn_image_desc d;
            gcn_decode_image(t, &d);
            std::printf(" -> T# addr=0x%llx %ux%ux%u type=%u dfmt=%u nfmt=%u tile=%u",
                        (unsigned long long)d.base_address, d.width, d.height, d.depth, d.type,
                        d.data_format, d.num_format, d.tiling_index);
        }
        std::printf("\n");
    }
    static const char* const kTableKinds[] = {"none", "records", "indexed", "loop"};
    for (uint32_t i = 0; i < info.num_image_tables; i++) {
        gcn_image_table t;
        gcn_shader_get_image_table(sh, i, &t);
        std::printf("  T# table[%u] kind=%s buffer=%u dword=%u+%u*index", i,
                    t.kind < 4 ? kTableKinds[t.kind] : "?", t.table_buffer, t.first_dw, t.stride_dw);
        if (t.index_buffer != UINT32_MAX) {
            std::printf(" index_buffer=%u", t.index_buffer);
        }
        std::printf(" entries=%u reached=%u sites=%u\n", t.num_entries, t.reached, t.num_sites);
    }
    for (uint32_t i = 0; i < info.num_samplers; i++) {
        gcn_sampler_binding b;
        gcn_shader_get_sampler(sh, i, &b);
        std::printf("  sampler[%u] spirv_binding=%u sharp=", i, b.spirv_binding);
        PrintFetch(b.sharp);
        std::printf(" msl=sampler(%d)\n", (int)b.msl_sampler);
    }
    for (uint32_t i = 0; i < info.num_vertex_attribs; i++) {
        gcn_vertex_attrib a;
        gcn_shader_get_vertex_attrib(sh, i, &a);
        uint32_t v[4];
        gcn_buffer_desc d{};
        if (gcn_shader_fetch_vertex_attrib_sharp(sh, i, ud, FakeMem::Read, &mem, v)) {
            gcn_decode_buffer(v, &d);
        }
        std::printf("  attrib[%u] location=%u vgpr=v%u n=%u V#=s[%u]+%udw step=%u divisor=%u "
                    "-> addr=0x%llx stride=%u dfmt=%u nfmt=%u\n",
                    i, a.location, a.dest_vgpr, a.num_elements, a.sgpr_base, a.dword_offset,
                    a.step_rate, a.divisor, (unsigned long long)d.base_address, d.stride,
                    d.data_format, d.num_format);
    }
}

struct Result {
    int total = 0, compiled = 0, msl_ok = 0, metal_ok = 0;
};

// Persistent cache round trip: a deserialized shader (and a clone) must answer every query
// exactly like the compiled one, and serialize to the same bytes. Returns an error or "".
std::string CheckRoundTrip(const gcn_shader* sh, const gcn_compile_desc& d, FakeMem& mem) {
    void* blob = nullptr;
    size_t size = 0;
    if (gcn_shader_serialize(sh, &blob, &size) != GCN_OK) {
        return "serialize failed";
    }
    gcn_shader* rt = nullptr;
    char err[256] = {};
    const gcn_status st = gcn_shader_deserialize(blob, size, d.code_addr, &rt, err, sizeof(err));
    std::string why;
    gcn_shader* cl = rt ? gcn_shader_clone(rt, d.code_addr) : nullptr;
    for (const gcn_shader* c : {static_cast<const gcn_shader*>(rt), static_cast<const gcn_shader*>(cl)}) {
        if (st != GCN_OK || !c) {
            why = std::string("deserialize failed: ") + err;
            break;
        }
        gcn_shader_info a, b;
        gcn_shader_get_info(sh, &a);
        gcn_shader_get_info(c, &b);
        const char* ma = gcn_shader_msl(sh);
        const char* mb = gcn_shader_msl(c);
        size_t na = 0, nb = 0;
        const uint32_t* sa = gcn_shader_spirv(sh, &na);
        const uint32_t* sb = gcn_shader_spirv(c, &nb);
        if (std::memcmp(&a, &b, sizeof(a)) != 0) why = "info";
        else if ((ma == nullptr) != (mb == nullptr) || (ma && std::strcmp(ma, mb) != 0)) why = "msl";
        else if (na != nb || std::memcmp(sa, sb, na * 4) != 0) why = "spirv";
        for (uint32_t i = 0; why.empty() && i < a.num_buffers; i++) {
            gcn_buffer_binding x, y;
            gcn_shader_get_buffer(sh, i, &x);
            gcn_shader_get_buffer(c, i, &y);
            if (std::memcmp(&x, &y, sizeof(x)) != 0) why = "buffer binding";
        }
        for (uint32_t i = 0; why.empty() && i < a.num_images; i++) {
            gcn_image_binding x, y;
            gcn_shader_get_image(sh, i, &x);
            gcn_shader_get_image(c, i, &y);
            if (std::memcmp(&x, &y, sizeof(x)) != 0) why = "image binding";
        }
        for (uint32_t i = 0; why.empty() && i < a.num_samplers; i++) {
            gcn_sampler_binding x, y;
            gcn_shader_get_sampler(sh, i, &x);
            gcn_shader_get_sampler(c, i, &y);
            if (std::memcmp(&x, &y, sizeof(x)) != 0) why = "sampler binding";
        }
        for (uint32_t i = 0; why.empty() && i < a.num_vertex_attribs; i++) {
            gcn_vertex_attrib x, y;
            gcn_shader_get_vertex_attrib(sh, i, &x);
            gcn_shader_get_vertex_attrib(c, i, &y);
            if (std::memcmp(&x, &y, sizeof(x)) != 0) why = "vertex attribute";
        }
        if (why.empty() && gcn_shader_is_compatible(sh, d.user_data, d.runtime, FakeMem::Read, &mem) !=
                               gcn_shader_is_compatible(c, d.user_data, d.runtime, FakeMem::Read, &mem)) {
            why = "is_compatible";
        }
        if (why.empty()) {
            std::vector<uint32_t> fa(a.flatbuf_size_dw), fb(b.flatbuf_size_dw);
            gcn_shader_build_flatbuf(sh, d.user_data, FakeMem::Read, &mem, fa.data(), fa.size());
            gcn_shader_build_flatbuf(c, d.user_data, FakeMem::Read, &mem, fb.data(), fb.size());
            if (fa != fb) why = "flatbuf";
        }
        if (why.empty()) {
            void* again = nullptr;
            size_t again_size = 0;
            gcn_shader_serialize(c, &again, &again_size);
            if (again_size != size || std::memcmp(again, blob, size) != 0) why = "re-serialized bytes";
            gcn_free(again);
        }
        if (!why.empty()) {
            why = (c == rt ? "deserialized shader differs: " : "cloned shader differs: ") + why;
            break;
        }
    }
    gcn_shader_destroy(cl);
    gcn_shader_destroy(rt);
    gcn_free(blob);
    return why;
}

bool RunOne(const std::string& name, const gcn_compile_desc& desc, FakeMem& mem,
            const Options& opt, Result& res) {
    res.total++;
    char err[1024] = {};
    gcn_shader* sh = nullptr;
    gcn_compile_desc d = desc;
    gcn_msl_options mo;
    if (g_max_buffer_slots) {
        if (d.msl) {
            mo = *d.msl;
        } else {
            gcn_msl_options_default(&mo);
        }
        mo.max_buffer_slots = g_max_buffer_slots;
        d.msl = &mo;
    }
    const gcn_status st = gcn_compile(&d, &sh, err, sizeof(err));
    if (st != GCN_OK && st != GCN_ERR_MSL) {
        std::printf("[FAIL compile] %s: %s\n", name.c_str(), err);
        return false;
    }
    res.compiled++;
    size_t nwords = 0;
    const uint32_t* spv = gcn_shader_spirv(sh, &nwords);
    const char* msl = gcn_shader_msl(sh);
    bool ok = msl != nullptr;
    std::string merr;
    if (msl) {
        res.msl_ok++;
        if (opt.metal) {
            if (ValidateMsl(msl, merr)) {
                res.metal_ok++;
            } else {
                ok = false;
            }
        }
    }
    const std::string cache_err = CheckRoundTrip(sh, d, mem);
    if (!cache_err.empty()) {
        std::printf("  cache round trip: %s\n", cache_err.c_str());
        ok = false;
    }
    std::printf("[%s] %s: spirv=%zu words, msl=%s%s%s\n", ok ? "OK" : "FAIL", name.c_str(),
                nwords, msl ? "yes" : "no", opt.metal ? (msl && merr.empty() ? ", metal=ok" : ", metal=FAIL") : "",
                msl ? "" : (std::string(" (") + err + ")").c_str());
    if (!merr.empty()) {
        std::printf("  metal error: %.2000s\n", merr.c_str());
    }
    {
        gcn_shader_info info;
        gcn_shader_get_info(sh, &info);
        if (info.msl_argument_buffer >= 0) {
            std::printf("  argument buffer [[buffer(%d)]]: %u guest buffers\n", info.msl_argument_buffer,
                        info.msl_argument_buffer_entries);
        }
    }
    if (!opt.quiet) {
        PrintTable(sh, desc.user_data, mem);
        if (msl) {
            std::printf("----- MSL -----\n%s---------------\n", msl);
        }
    }
    if (!opt.msl_out.empty()) {
        const std::string base = opt.msl_out + "/" + name;
        if (msl) {
            std::ofstream(base + ".metal") << msl;
        }
        std::ofstream(base + ".spv", std::ios::binary)
            .write(reinterpret_cast<const char*>(spv), nwords * 4);
    }
    // Specialization round trip: same state must be compatible.
    if (!gcn_shader_is_compatible(sh, desc.user_data, desc.runtime, desc.read_guest,
                                  desc.read_ctx)) {
        std::printf("  WARNING: shader not compatible with its own compile state\n");
    }
    gcn_shader_destroy(sh);
    return ok;
}

// ---------------------------------------------------------------- self tests

std::vector<uint32_t> WithFooter(std::vector<uint32_t> body, uint64_t hash) {
    // s_mov_b32 vcc_hi, #imm header: footer at code + (imm + 1) * 2 dwords.
    std::vector<uint32_t> code = {0xBEEB03FF, 0};
    code.insert(code.end(), body.begin(), body.end());
    while (code.size() % 2) {
        code.push_back(0xBF800000); // s_nop
    }
    code[1] = uint32_t(code.size() / 2 - 1);
    // AmdGpu::BinaryInfo ("OrbShdr")
    uint8_t bi[32] = {'O', 'r', 'b', 'S', 'h', 'd', 'r', 7};
    const uint32_t length = uint32_t(code.size() * 4);
    uint32_t w = (length & 0xFFFFFF) << 8; // pssl_or_cg:1 cached:1 type:4 source_type:2 length:24
    std::memcpy(bi + 8, &w, 4);
    std::memcpy(bi + 16, &hash, 8);
    const size_t at = code.size();
    code.resize(at + 8);
    std::memcpy(&code[at], bi, 32);
    return code;
}

void SelfTest(const Options& opt, Result& res) {
    gcn_profile prof;
    gcn_profile_default_metal(&prof);
    const uint32_t flags = GCN_COMPILE_EMIT_MSL;

    // ---- Vertex shader: fetch shader (2 attributes) + constant buffer V# behind a pointer.
    {
        FakeMem mem;
        const uint64_t vs_addr = 0x100000, fs_addr = 0x101000, vtab = 0x102000, srt = 0x103000,
                       cb = 0x104000, vb0 = 0x200000, vb1 = 0x210000;
        // main VS:
        //   s_swappc_b64 s[0:1], s[0:1]        ; call fetch shader (address in s[0:1])
        //   s_load_dwordx4 s[8:11], s[4:5], 0  ; cbuffer V# via pointer s[4:5]
        //   s_waitcnt lgkmcnt(0)
        //   s_buffer_load_dwordx4 s[12:15], s[8:11], 0
        //   s_waitcnt lgkmcnt(0)
        //   v_mul_f32 v4, s12, v4              ; scale x by cb[0].x
        //   exp param0, v8, v9, v10, v11
        //   exp pos0, v4, v5, v6, v7 done
        //   s_endpgm
        const auto vs = WithFooter({0xBE802100, 0xC0840500, 0xBF8C007F, 0xC2860900, 0xBF8C007F,
                                    0x1008080C, 0xF800020F, 0x0B0A0908, 0xF80008CF, 0x07060504,
                                    0xBF810000},
                                   0x1111222233334444ULL);
        // fetch shader:
        //   s_load_dwordx4 s[8:11], s[2:3], 0x0
        //   s_load_dwordx4 s[12:15], s[2:3], 0x4
        //   s_waitcnt lgkmcnt(0)
        //   buffer_load_format_xyzw v[4:7], v0, s[8:11], 0 idxen
        //   buffer_load_format_xyzw v[8:11], v0, s[12:15], 0 idxen
        //   s_waitcnt 0
        //   s_setpc_b64 s[0:1]
        const std::vector<uint32_t> fetch = {0xC0840300, 0xC0860304, 0xBF8C007F, 0xE00C2000,
                                             0x80020400, 0xE00C2000, 0x80030800, 0xBF8C0000,
                                             0xBE802000};
        // V#: base, stride, num_records, dst_sel xyzw, nfmt float(7), dfmt 32_32_32_32 (14)
        const auto vsharp = [](uint64_t base, uint32_t stride, uint32_t n, uint32_t dfmt,
                               uint32_t nfmt) {
            std::vector<uint32_t> v(4);
            v[0] = uint32_t(base);
            v[1] = uint32_t(base >> 32) | (stride << 16);
            v[2] = n;
            v[3] = 4 | (5 << 3) | (6 << 6) | (7 << 9) | (nfmt << 12) | (dfmt << 15);
            return v;
        };
        std::vector<uint32_t> table = vsharp(vb0, 16, 3, 14, 7);
        const auto v1 = vsharp(vb1, 8, 3, 11 /*32_32*/, 7);
        table.insert(table.end(), v1.begin(), v1.end());
        mem.Add32(vs_addr, vs);
        mem.Add32(fs_addr, fetch);
        mem.Add32(vtab, table);
        mem.Add32(srt, vsharp(cb, 0, 64, 14, 7));
        mem.Add32(cb, std::vector<uint32_t>(16, 0x3F800000));

        gcn_runtime_info rt{};
        rt.hw_stage = GCN_HW_VS;
        rt.sw_stage = GCN_SW_VERTEX;
        rt.num_allocated_vgprs = 16;
        rt.num_user_data = 6;
        rt.dx10_clamp = true;
        gcn_compile_desc d{};
        d.hw_stage = GCN_HW_VS;
        d.sw_stage = GCN_SW_VERTEX;
        d.code_addr = vs_addr;
        d.user_data[0] = uint32_t(fs_addr);
        d.user_data[1] = uint32_t(fs_addr >> 32);
        d.user_data[2] = uint32_t(vtab);
        d.user_data[3] = 0;
        d.user_data[4] = uint32_t(srt);
        d.user_data[5] = 0;
        d.runtime = &rt;
        d.profile = &prof;
        d.read_guest = FakeMem::Read;
        d.read_ctx = &mem;
        d.flags = flags;
        RunOne("selftest_vs_fetch_cbuf", d, mem, opt, res);
    }

    // ---- Pixel shader: interpolate param0, sample texture (T#/S# in user data), export mrt0.
    {
        FakeMem mem;
        const uint64_t ps_addr = 0x300000;
        //   v_interp_p1/p2_f32 v2..v5 <- attr0.xyzw (v0/v1 = I/J)
        //   image_sample v[6:9], v[2:3], s[4:11], s[12:15] dmask:0xf
        //   s_waitcnt 0
        //   v_mul_f32 v6, s0, v6 ... (keep simple: export directly)
        //   exp mrt0, v6, v7, v8, v9 done vm
        //   s_endpgm
        const auto ps = WithFooter({0xC8080000, 0xC8090001, 0xC80C0100, 0xC80D0101, 0xC8100200,
                                    0xC8110201, 0xC8140300, 0xC8150301, 0xF0800F00, 0x00610602,
                                    0xBF8C0000, 0xF800180F, 0x09080706, 0xBF810000},
                                   0x5555666677778888ULL);
        mem.Add32(ps_addr, ps);
        gcn_runtime_info rt{};
        rt.hw_stage = GCN_HW_PS;
        rt.sw_stage = GCN_SW_FRAGMENT;
        rt.num_allocated_vgprs = 12;
        rt.num_user_data = 16;
        rt.fs.en_flags = 0x2;   // PERSP_CENTER_ENA
        rt.fs.addr_flags = 0x2;
        rt.fs.num_inputs = 1;
        rt.fs.inputs[0].param_index = 0;
        rt.fs.num_samples = 1;
        rt.fs.color_buffers[0] = {10 /*8_8_8_8*/, 0 /*unorm*/, 0, 9 /*ABGR_32*/, 0, {4, 5, 6, 7}};
        gcn_compile_desc d{};
        d.hw_stage = GCN_HW_PS;
        d.sw_stage = GCN_SW_FRAGMENT;
        d.code_addr = ps_addr;
        // T# at s[4:11]: 2D RGBA8 256x256 at 0x400000, S# at s[12:15]: linear filter, wrap
        const uint32_t t[8] = {0x00004000, 0x00A00000, 0x003FC0FF, 0x90000FAC,
                               0x001FE000, 0,          0,          0};
        std::memcpy(&d.user_data[4], t, 32);
        d.user_data[12] = 0;                             // clamp wrap
        d.user_data[13] = 0x00FFF000;                    // max_lod
        d.user_data[14] = (1u << 20) | (1u << 22);       // xy mag/min bilinear
        d.user_data[15] = 0;
        d.runtime = &rt;
        d.profile = &prof;
        d.read_guest = FakeMem::Read;
        d.read_ctx = &mem;
        d.flags = flags;
        RunOne("selftest_ps_interp_sample", d, mem, opt, res);
    }

    // ---- Compute shader: buffer[v0] = v0 with V# in user data s[0:3].
    {
        FakeMem mem;
        const uint64_t cs_addr = 0x500000;
        //   v_lshlrev_b32 v1, 2, v0
        //   buffer_store_dword v0, v1, s[0:3], 0 offen
        //   s_endpgm
        const auto cs = WithFooter({0x34020082, 0xE0701000, 0x80000001, 0xBF810000},
                                   0x9999AAAABBBBCCCCULL);
        mem.Add32(cs_addr, cs);
        gcn_runtime_info rt{};
        rt.hw_stage = GCN_HW_CS;
        rt.sw_stage = GCN_SW_COMPUTE;
        rt.num_allocated_vgprs = 4;
        rt.num_user_data = 4;
        rt.cs.workgroup_size[0] = 64;
        rt.cs.workgroup_size[1] = 1;
        rt.cs.workgroup_size[2] = 1;
        rt.cs.tgid_enable[0] = true;
        gcn_compile_desc d{};
        d.hw_stage = GCN_HW_CS;
        d.sw_stage = GCN_SW_COMPUTE;
        d.code_addr = cs_addr;
        d.user_data[0] = 0x00600000; // base
        d.user_data[1] = 0;          // stride 0 => raw
        d.user_data[2] = 4096;       // num_records (bytes)
        d.user_data[3] = 4 | (5 << 3) | (6 << 6) | (7 << 9) | (4 << 12) | (4 << 15); // u32
        d.runtime = &rt;
        d.profile = &prof;
        d.read_guest = FakeMem::Read;
        d.read_ctx = &mem;
        d.flags = flags;
        RunOne("selftest_cs_store", d, mem, opt, res);
    }

    // ---- Compute shader mixing u32/f32/u16 accesses to one buffer (aliased SSBO bindings).
    {
        FakeMem mem;
        const uint64_t cs_addr = 0x700000;
        //   v_lshlrev_b32 v1, 2, v0
        //   buffer_load_dword v2, v1, s[0:3], 0 offen
        //   buffer_load_ushort v3, v1, s[0:3], 0 offen
        //   s_waitcnt vmcnt(0)
        //   v_add_f32 v2, 1.0, v2
        //   v_cvt_f32_u32 v3, v3
        //   v_add_f32 v2, v2, v3
        //   buffer_store_dword v2, v1, s[0:3], 0 offen
        //   s_endpgm
        const auto cs = WithFooter({0x34020082, 0xE0301000, 0x80000201, 0xE0281000, 0x80000301,
                                    0xBF8C0F70, 0x060404F2, 0x7E060D03, 0x06040702, 0xE0701000,
                                    0x80000201, 0xBF810000},
                                   0x1212343456567878ULL);
        mem.Add32(cs_addr, cs);
        gcn_runtime_info rt{};
        rt.hw_stage = GCN_HW_CS;
        rt.sw_stage = GCN_SW_COMPUTE;
        rt.num_allocated_vgprs = 4;
        rt.num_user_data = 4;
        rt.cs.workgroup_size[0] = 64;
        rt.cs.workgroup_size[1] = 1;
        rt.cs.workgroup_size[2] = 1;
        gcn_compile_desc d{};
        d.hw_stage = GCN_HW_CS;
        d.sw_stage = GCN_SW_COMPUTE;
        d.code_addr = cs_addr;
        d.user_data[0] = 0x00800000;
        d.user_data[2] = 4096;
        d.user_data[3] = 4 | (5 << 3) | (6 << 6) | (7 << 9) | (4 << 12) | (4 << 15);
        d.runtime = &rt;
        d.profile = &prof;
        d.read_guest = FakeMem::Read;
        d.read_ctx = &mem;
        d.flags = flags;
        RunOne("selftest_cs_aliased_buffer", d, mem, opt, res);
    }
}

// ---------------------------------------------------------------- dumped shaders

bool LoadFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

void RunDump(const std::string& bin, const Options& opt, Result& res) {
    std::string base = bin.substr(0, bin.size() - 4);
    std::string name = base.substr(base.find_last_of('/') + 1);
    std::vector<uint8_t> code;
    if (!LoadFile(bin, code) || code.size() < 8) {
        std::printf("[FAIL] %s: cannot read\n", bin.c_str());
        res.total++;
        return;
    }
    std::vector<uint32_t> regs(0x10000, 0);
    FakeMem mem;
    mem.zero_mem = opt.zero_mem;
    uint64_t code_addr = 0;
    std::ifstream txt(base + ".txt");
    std::string line;
    while (std::getline(txt, line)) {
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            const auto p = line.find("at 0x");
            if (p != std::string::npos) {
                code_addr = std::strtoull(line.c_str() + p + 3, nullptr, 16);
            }
            continue;
        }
        std::istringstream ss(line);
        std::string a, b, c;
        ss >> a >> b >> c;
        if (a == "mem") {
            std::vector<uint8_t> data;
            if (LoadFile(c, data) || LoadFile(base.substr(0, base.find_last_of('/') + 1) + c, data)) {
                mem.Add(std::strtoull(b.c_str(), nullptr, 16), data.data(), data.size());
            }
            continue;
        }
        const uint32_t r = std::strtoul(a.c_str(), nullptr, 16);
        if (r < regs.size()) {
            regs[r] = std::strtoul(b.c_str(), nullptr, 16);
        }
    }
    gcn_hw_stage hw;
    gcn_sw_stage sw;
    if (name.rfind("vs_", 0) == 0) {
        hw = GCN_HW_VS;
        sw = GCN_SW_VERTEX;
    } else if (name.rfind("ps_", 0) == 0) {
        hw = GCN_HW_PS;
        sw = GCN_SW_FRAGMENT;
    } else if (name.rfind("cs_", 0) == 0) {
        hw = GCN_HW_CS;
        sw = GCN_SW_COMPUTE;
    } else {
        std::printf("[SKIP] %s: unknown stage prefix\n", name.c_str());
        return;
    }
    gcn_profile prof;
    gcn_profile_default_metal(&prof);
    gcn_runtime_info rt;
    gcn_runtime_info_from_regs(regs.data(), regs.size(), hw, sw, &prof, &rt);
    gcn_compile_desc d{};
    d.hw_stage = hw;
    d.sw_stage = sw;
    uint64_t reg_addr = 0;
    gcn_stage_from_regs(regs.data(), regs.size(), hw, &reg_addr, d.user_data);
    d.code_addr = code_addr ? code_addr : reg_addr;
    mem.Add(d.code_addr, code.data(), code.size()); // the shader itself is guest memory too
    d.code = reinterpret_cast<const uint32_t*>(code.data());
    d.code_size = uint32_t(code.size() & ~3u);
    d.runtime = &rt;
    d.profile = &prof;
    d.read_guest = FakeMem::Read;
    d.read_ctx = &mem;
    d.flags = GCN_COMPILE_EMIT_MSL;
    RunOne(name, d, mem, opt, res);
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    bool selftest = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--selftest") {
            selftest = true;
        } else if (a == "--quiet") {
            opt.quiet = true;
        } else if (a == "--no-metal") {
            opt.metal = false;
        } else if (a == "--zero-mem") {
            opt.zero_mem = true;
        } else if (a == "--msl-out" && i + 1 < argc) {
            opt.msl_out = argv[++i];
        } else if (a == "--max-buffer-slots" && i + 1 < argc) {
            g_max_buffer_slots = uint32_t(std::atoi(argv[++i]));
        } else if (a == "--dump-dir" && i + 1 < argc) {
            gcn_set_dump_dir(argv[++i]); // shadPS4 IR dumps into <dir>/dumps
        } else if (a == "--verbose-log") {
            gcn_set_log_level(2);
        } else if (a.size() > 4 && a.substr(a.size() - 4) == ".bin") {
            files.push_back(a);
        } else {
            std::fprintf(stderr, "unknown argument %s\n", a.c_str());
            return 2;
        }
    }
    if (!selftest && files.empty()) {
        std::fprintf(stderr,
                     "usage: gcn_test [--selftest] [--zero-mem] [--quiet] [--no-metal] "
                     "[--msl-out dir] [--dump-dir dir] [shader.bin...]\n");
        return 2;
    }
    if (opt.quiet) {
        gcn_set_log_level(4); // errors only
    }
    std::printf("%s\n", gcn_version());
    if (opt.metal) {
        g_device = MTLCreateSystemDefaultDevice();
        if (!g_device) {
            std::printf("no Metal device; skipping MSL validation\n");
            opt.metal = false;
        }
    }
    Result res;
    if (selftest) {
        SelfTest(opt, res);
    }
    for (const auto& f : files) {
        RunDump(f, opt, res);
    }
    std::printf("\nsummary: %d shaders, %d compiled to SPIR-V, %d converted to MSL, %d accepted by "
                "Metal\n",
                res.total, res.compiled, res.msl_ok, res.metal_ok);
    return (res.compiled == res.total && res.msl_ok == res.total &&
            (!opt.metal || res.metal_ok == res.total))
               ? 0
               : 1;
}
