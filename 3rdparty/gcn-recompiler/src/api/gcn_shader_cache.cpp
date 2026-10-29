// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Persistent cache support for compiled shaders (include/gcn_recompiler.h, "Persistent shader
// cache support"): serialization of the whole gcn_shader state that the C API reads after
// gcn_compile(), in the spirit of shadPS4's vk_pipeline_serialization.cpp.
//
// Layout: header (magic, format version, build ID), then every member of gcn_shader in
// declaration order. Trivially copyable members are stored as raw bytes; that is safe because a
// blob is only accepted by the exact library build that wrote it (GCN_BUILD_ID covers every
// source file, so any layout change also changes the ID).

#include <bitset>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "gcn_build_id.h"
#include "gcn_recompiler.h"

#include "api/gcn_shader_internal.h"

using namespace Shader;

namespace {

constexpr u32 kMagic = 0x534E4347; // "GCNS"
// Bump when the serialized layout below changes without a source change elsewhere.
#define GCN_SHADER_CACHE_FORMAT "3"
constexpr u32 kFormatVersion = 10; // 2: Info::uses_guest_memory, 3: Info::null_image_reads,
                                   // 4: Info::zeroed_write_lanes,
                                   // 5: Info::unpredicated_wave_regions / per_subgroup_lane_ops,
                                   // 6: Info::per_lane_ends (u4-graphics) or Info::image_tables,
                                   //    ImageResource::table / table_entry,
                                   //    StageSpecialization::image_tables (u4-bindless): two
                                   //    different layouts used 6,
                                   // 7: both
                                   // 8: Info::fetch_shader_inlined,
                                   //    VsAttribSpecialization::fetchable
                                   // 9: StageSpecialization::inlined_fetch_hash,
                                   //    VertexAttribute::inst_offset 16 bits
                                   // 10: Info::fetch_inline_reason / fetch_inline_attrib
constexpr char kCacheId[] = GCN_BUILD_ID "-f" GCN_SHADER_CACHE_FORMAT;

// Tripwires: a new member in one of these types must be added to Write()/Read() below.
static_assert(sizeof(InfoPersistent) == 8696, "update gcn_shader_cache.cpp for InfoPersistent");
static_assert(sizeof(Info) == 9128, "update gcn_shader_cache.cpp for Info");
static_assert(sizeof(StageSpecialization) == 1536, "update gcn_shader_cache.cpp for StageSpecialization");
static_assert(sizeof(gcn_shader) == 11208, "update gcn_shader_cache.cpp for gcn_shader");
static_assert(std::is_trivially_copyable_v<RuntimeInfo>);
static_assert(std::is_trivially_copyable_v<Profile>);
static_assert(StageSpecialization::MaxStageResources % 64 == 0);

struct Writer {
    std::vector<u8> buf;

    void Raw(const void* p, size_t n) {
        const auto* b = static_cast<const u8*>(p);
        buf.insert(buf.end(), b, b + n);
    }
    template <typename T>
    void Pod(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        Raw(&v, sizeof(T));
    }
    // Contiguous container of trivially copyable elements.
    template <typename C>
    void Seq(const C& c) {
        using T = std::remove_cvref_t<decltype(*c.data())>;
        static_assert(std::is_trivially_copyable_v<T>);
        Pod(static_cast<u32>(c.size()));
        if (c.size()) {
            Raw(c.data(), c.size() * sizeof(T));
        }
    }
    void Str(const std::string& s) {
        Pod(static_cast<u32>(s.size()));
        Raw(s.data(), s.size());
    }
    // Runtime info without the host pointer of the GS copy shader span (restored on read).
    void Runtime(const RuntimeInfo& r) {
        RuntimeInfo c = r;
        if (c.hw_stage == HwStage::Geometry) {
            c.hw.gs.vs_copy = {};
        }
        Pod(c);
    }
    template <size_t N>
    void Bits(const std::bitset<N>& b) {
        for (size_t w = 0; w < N / 64; w++) {
            u64 v = 0;
            for (size_t i = 0; i < 64; i++) {
                v |= u64(b[w * 64 + i]) << i;
            }
            Pod(v);
        }
    }
};

struct Reader {
    const u8* p;
    size_t size;
    size_t off = 0;
    bool ok = true;

    bool Raw(void* dst, size_t n) {
        if (!ok || n > size - off) {
            ok = false;
            return false;
        }
        std::memcpy(dst, p + off, n);
        off += n;
        return true;
    }
    template <typename T>
    void Pod(T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        Raw(&v, sizeof(T));
    }
    // `max` bounds the element count (fixed-capacity containers, corrupt input).
    template <typename C>
    void Seq(C& c, size_t max = SIZE_MAX) {
        using T = std::remove_cvref_t<decltype(*c.data())>;
        static_assert(std::is_trivially_copyable_v<T>);
        u32 n = 0;
        Pod(n);
        if (!ok || n > max || size_t(n) * sizeof(T) > size - off) {
            ok = false;
            return;
        }
        c.resize(n);
        if (n) {
            Raw(c.data(), size_t(n) * sizeof(T));
        }
    }
    void Str(std::string& s) {
        u32 n = 0;
        Pod(n);
        if (!ok || n > size - off) {
            ok = false;
            return;
        }
        s.assign(reinterpret_cast<const char*>(p + off), n);
        off += n;
    }
    void Runtime(RuntimeInfo& r) {
        Pod(r);
    }
    template <size_t N>
    void Bits(std::bitset<N>& b) {
        b.reset();
        for (size_t w = 0; w < N / 64; w++) {
            u64 v = 0;
            Pod(v);
            for (size_t i = 0; i < 64; i++) {
                b[w * 64 + i] = (v >> i) & 1;
            }
        }
    }
};

template <typename K, typename V>
void WriteMap(Writer& w, const std::map<K, V>& m) {
    w.Pod(static_cast<u32>(m.size()));
    for (const auto& [k, v] : m) {
        w.Pod(k);
        w.Pod(v);
    }
}

template <typename K, typename V>
void ReadMap(Reader& r, std::map<K, V>& m) {
    m.clear();
    u32 n = 0;
    r.Pod(n);
    for (u32 i = 0; i < n && r.ok; i++) {
        K k{};
        V v{};
        r.Pod(k);
        r.Pod(v);
        m.emplace(k, v);
    }
}

void WriteInfo(Writer& w, const Info& info) {
    // InfoPersistent
    w.Seq(info.buffers);
    w.Seq(info.images);
    w.Seq(info.samplers);
    w.Seq(info.fmasks);
    w.Pod(info.fetch_shader_sgpr_base);
    w.Pod(info.shared_memory_scratch_size);
    w.Pod(info.pgm_hash);
    w.Pod(info.tess_consts_dword_offset);
    w.Pod(info.tess_consts_ptr_base);
    w.Pod(info.hw_stage);
    w.Pod(info.sw_stage);
    w.Pod(info.mrt_mask);
    w.Pod(info.has_fetch_shader);
    w.Pod(info.fetch_shader_inlined);
    w.Pod(info.fetch_inline_reason);
    w.Pod(info.fetch_inline_attrib);
    w.Pod(info.has_bitwise_xor);
    w.Pod(info.uses_dma);
    w.Pod(info.uses_guest_memory);
    w.Pod(info.null_image_reads);
    w.Seq(info.image_tables);
    w.Pod(info.zeroed_write_lanes);
    w.Pod(info.unpredicated_wave_regions);
    w.Pod(info.per_subgroup_lane_ops);
    w.Pod(info.per_lane_ends);
    // Info. user_data and flattened_ud_buf belong to the compiling draw and pgm_base to the
    // code address; they are re-established on read.
    w.Seq(info.srt_info.program);
    w.Pod(info.srt_info.flattened_bufsize_dw);
    w.Pod(info.loads);
    w.Pod(info.stores);
    w.Pod(info.readconst_types);
    w.Pod(static_cast<u32>(info.gs_copy_data.attr_map.size()));
    for (const auto& [k, v] : info.gs_copy_data.attr_map) {
        w.Pod(k);
        w.Pod(v.first);
        w.Pod(v.second);
    }
    w.Pod(info.gs_copy_data.num_attrs);
    w.Pod(info.gs_copy_data.output_vertices);
    w.Pod(info.gs_copy_data.num_comps);
    w.Pod(info.uses_patches);
    w.Pod(info.has_storage_images);
    w.Pod(info.has_discard);
    w.Pod(info.has_image_gather);
    w.Pod(info.has_image_query);
    w.Pod(info.has_flatbuf);
    w.Pod(info.uses_buffer_atomic_float_min_max);
    w.Pod(info.uses_image_atomic_float_min_max);
    w.Pod(info.uses_lane_id);
    w.Pod(info.uses_shader_clock);
    w.Pod(info.uses_group_quad);
    w.Pod(info.uses_group_shuffle);
    w.Pod(info.uses_group_ballot);
    w.Pod(info.shared_types);
    w.Pod(info.uses_fp16);
    w.Pod(info.uses_fp64);
    w.Pod(info.uses_pack_10_11_11);
    w.Pod(info.uses_unpack_10_11_11);
    w.Pod(info.uses_buffer_int64_atomics);
    w.Pod(info.uses_shared_int64_atomics);
    w.Pod(info.stores_tess_level_outer);
    w.Pod(info.stores_tess_level_inner);
    w.Pod(info.translation_failed);
    w.Pod(info.fs_interpolation);
}

void ReadInfo(Reader& r, Info& info) {
    r.Seq(info.buffers, NUM_BUFFERS);
    r.Seq(info.images, NUM_IMAGES);
    r.Seq(info.samplers, NUM_SAMPLERS);
    r.Seq(info.fmasks, NUM_FMASKS);
    r.Pod(info.fetch_shader_sgpr_base);
    r.Pod(info.shared_memory_scratch_size);
    r.Pod(info.pgm_hash);
    r.Pod(info.tess_consts_dword_offset);
    r.Pod(info.tess_consts_ptr_base);
    r.Pod(info.hw_stage);
    r.Pod(info.sw_stage);
    r.Pod(info.mrt_mask);
    r.Pod(info.has_fetch_shader);
    r.Pod(info.fetch_shader_inlined);
    r.Pod(info.fetch_inline_reason);
    r.Pod(info.fetch_inline_attrib);
    r.Pod(info.has_bitwise_xor);
    r.Pod(info.uses_dma);
    r.Pod(info.uses_guest_memory);
    r.Pod(info.null_image_reads);
    r.Seq(info.image_tables, info.image_tables.capacity());
    r.Pod(info.zeroed_write_lanes);
    r.Pod(info.unpredicated_wave_regions);
    r.Pod(info.per_subgroup_lane_ops);
    r.Pod(info.per_lane_ends);
    r.Seq(info.srt_info.program);
    r.Pod(info.srt_info.flattened_bufsize_dw);
    r.Pod(info.loads);
    r.Pod(info.stores);
    r.Pod(info.readconst_types);
    u32 n = 0;
    r.Pod(n);
    info.gs_copy_data.attr_map.clear();
    for (u32 i = 0; i < n && r.ok; i++) {
        u32 k{};
        IR::Attribute a{};
        u32 c{};
        r.Pod(k);
        r.Pod(a);
        r.Pod(c);
        info.gs_copy_data.attr_map.emplace(k, std::make_pair(a, c));
    }
    r.Pod(info.gs_copy_data.num_attrs);
    r.Pod(info.gs_copy_data.output_vertices);
    r.Pod(info.gs_copy_data.num_comps);
    r.Pod(info.uses_patches);
    r.Pod(info.has_storage_images);
    r.Pod(info.has_discard);
    r.Pod(info.has_image_gather);
    r.Pod(info.has_image_query);
    r.Pod(info.has_flatbuf);
    r.Pod(info.uses_buffer_atomic_float_min_max);
    r.Pod(info.uses_image_atomic_float_min_max);
    r.Pod(info.uses_lane_id);
    r.Pod(info.uses_shader_clock);
    r.Pod(info.uses_group_quad);
    r.Pod(info.uses_group_shuffle);
    r.Pod(info.uses_group_ballot);
    r.Pod(info.shared_types);
    r.Pod(info.uses_fp16);
    r.Pod(info.uses_fp64);
    r.Pod(info.uses_pack_10_11_11);
    r.Pod(info.uses_unpack_10_11_11);
    r.Pod(info.uses_buffer_int64_atomics);
    r.Pod(info.uses_shared_int64_atomics);
    r.Pod(info.stores_tess_level_outer);
    r.Pod(info.stores_tess_level_inner);
    r.Pod(info.translation_failed);
    r.Pod(info.fs_interpolation);
}

void WriteSpec(Writer& w, const StageSpecialization& spec) {
    w.Runtime(spec.runtime_info);
    w.Bits(spec.bitset);
    w.Pod(spec.fetch_shader_data.size);
    w.Seq(spec.fetch_shader_data.attributes);
    w.Pod(spec.fetch_shader_data.vertex_offset_sgpr);
    w.Pod(spec.fetch_shader_data.instance_offset_sgpr);
    w.Pod(spec.inlined_fetch_hash);
    w.Seq(spec.vs_attribs);
    w.Seq(spec.buffers);
    w.Seq(spec.images);
    w.Seq(spec.fmasks);
    w.Seq(spec.samplers);
    w.Seq(spec.image_tables);
    w.Pod(spec.start);
}

void ReadSpec(Reader& r, StageSpecialization& spec) {
    r.Runtime(spec.runtime_info);
    r.Bits(spec.bitset);
    r.Pod(spec.fetch_shader_data.size);
    r.Seq(spec.fetch_shader_data.attributes, 256);
    r.Pod(spec.fetch_shader_data.vertex_offset_sgpr);
    r.Pod(spec.fetch_shader_data.instance_offset_sgpr);
    r.Pod(spec.inlined_fetch_hash);
    r.Seq(spec.vs_attribs, 256);
    r.Seq(spec.buffers, NUM_BUFFERS);
    r.Seq(spec.images, NUM_IMAGES);
    r.Seq(spec.fmasks, NUM_FMASKS);
    r.Seq(spec.samplers, NUM_SAMPLERS);
    r.Seq(spec.image_tables, 8 * 3);
    r.Pod(spec.start);
}

// Re-points the members that reference other members or the code address.
void Rebind(gcn_shader& sh, u64 code_addr) {
    sh.code_addr = code_addr;
    sh.info.pgm_base = static_cast<VAddr>(code_addr);
    sh.info.user_data = std::span<const u32>(sh.user_data);
    sh.spec.info = &sh.info;
    if (sh.runtime.hw_stage == HwStage::Geometry) {
        const std::span<const u32> copy = sh.gs_copy.empty() ? std::span<const u32>{}
                                                             : std::span<const u32>(sh.gs_copy);
        sh.runtime.hw.gs.vs_copy = copy;
        if (sh.spec.runtime_info.hw_stage == HwStage::Geometry) {
            sh.spec.runtime_info.hw.gs.vs_copy = copy;
        }
    }
}

void CopyErr(char* err, size_t err_size, const char* msg) {
    if (err && err_size) {
        std::snprintf(err, err_size, "%s", msg);
    }
}

} // namespace

extern "C" {

GCN_API const char* gcn_shader_cache_id(void) {
    return kCacheId;
}

GCN_API gcn_status gcn_shader_serialize(const gcn_shader* s, void** out_data, size_t* out_size) {
    if (!s || !out_data || !out_size) {
        return GCN_ERR_INVALID_ARG;
    }
    Writer w;
    w.buf.reserve(16384 + s->spirv.size() * 4 + s->msl.size());
    w.Pod(kMagic);
    w.Pod(kFormatVersion);
    w.Raw(kCacheId, sizeof(kCacheId));
    w.Pod(s->code_size);
    w.Runtime(s->runtime);
    w.Pod(s->profile);
    WriteInfo(w, s->info);
    WriteSpec(w, s->spec);
    w.Seq(s->gs_copy);
    w.Seq(s->spirv);
    w.Str(s->msl);
    w.Str(s->msl_error);
    // MSL slot assignment
    const auto& b = s->msl_bindings;
    w.Pod(static_cast<u32>(b.buffers.size()));
    for (const auto& [k, v] : b.buffers) {
        w.Pod(k);
        w.Seq(v);
    }
    WriteMap(w, b.textures);
    WriteMap(w, b.samplers);
    w.Pod(b.push_constant_buffer);
    w.Pod(b.first_free_buffer);
    w.Pod(b.num_textures);
    w.Pod(b.num_samplers);
    WriteMap(w, b.arg_entries);
    w.Pod(b.argument_buffer);
    w.Pod(b.num_arg_entries);
    w.Pod(kMagic); // trailer: detects truncation of the last fields

    void* mem = std::malloc(w.buf.size());
    if (!mem) {
        return GCN_ERR_INVALID_ARG;
    }
    std::memcpy(mem, w.buf.data(), w.buf.size());
    *out_data = mem;
    *out_size = w.buf.size();
    return GCN_OK;
}

GCN_API gcn_status gcn_shader_deserialize(const void* data, size_t size, uint64_t code_addr,
                                          gcn_shader** out, char* err, size_t err_size) {
    if (!data || !out) {
        CopyErr(err, err_size, "invalid arguments");
        return GCN_ERR_INVALID_ARG;
    }
    *out = nullptr;
    Reader r{static_cast<const u8*>(data), size};
    u32 magic = 0, version = 0;
    char id[sizeof(kCacheId)] = {};
    r.Pod(magic);
    r.Pod(version);
    r.Raw(id, sizeof(id));
    if (!r.ok || magic != kMagic || version != kFormatVersion ||
        std::memcmp(id, kCacheId, sizeof(kCacheId)) != 0) {
        CopyErr(err, err_size, "not a shader blob of this gcn-recompiler build");
        return GCN_ERR_INVALID_ARG;
    }
    auto sh = std::make_unique<gcn_shader>();
    r.Pod(sh->code_size);
    r.Runtime(sh->runtime);
    r.Pod(sh->profile);
    ReadInfo(r, sh->info);
    ReadSpec(r, sh->spec);
    r.Seq(sh->gs_copy, 1u << 20);
    r.Seq(sh->spirv);
    r.Str(sh->msl);
    r.Str(sh->msl_error);
    auto& b = sh->msl_bindings;
    u32 nbuf = 0;
    r.Pod(nbuf);
    for (u32 i = 0; i < nbuf && r.ok; i++) {
        u32 k = 0;
        r.Pod(k);
        r.Seq(b.buffers[k], 64);
    }
    ReadMap(r, b.textures);
    ReadMap(r, b.samplers);
    r.Pod(b.push_constant_buffer);
    r.Pod(b.first_free_buffer);
    r.Pod(b.num_textures);
    r.Pod(b.num_samplers);
    ReadMap(r, b.arg_entries);
    r.Pod(b.argument_buffer);
    r.Pod(b.num_arg_entries);
    u32 trailer = 0;
    r.Pod(trailer);
    if (!r.ok || trailer != kMagic || r.off != r.size ||
        sh->info.srt_info.flattened_bufsize_dw < NUM_USER_DATA_REGS) {
        CopyErr(err, err_size, "truncated or corrupt shader blob");
        return GCN_ERR_INVALID_ARG;
    }
    sh->info.flattened_ud_buf.assign(sh->info.srt_info.flattened_bufsize_dw, 0);
    Rebind(*sh, code_addr);
    *out = sh.release();
    return GCN_OK;
}

GCN_API gcn_shader* gcn_shader_clone(const gcn_shader* s, uint64_t code_addr) {
    if (!s) {
        return nullptr;
    }
    // Through the serialized form rather than the copy constructor: the copy is then
    // byte-identical to the source (padding included), so it serializes to the same blob.
    void* data = nullptr;
    size_t size = 0;
    if (gcn_shader_serialize(s, &data, &size) != GCN_OK) {
        return nullptr;
    }
    gcn_shader* c = nullptr;
    gcn_shader_deserialize(data, size, code_addr, &c, nullptr, 0);
    std::free(data);
    return c;
}

} // extern "C"
