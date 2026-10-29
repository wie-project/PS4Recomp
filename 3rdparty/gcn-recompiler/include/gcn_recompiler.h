/* SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * gcn_recompiler.h - narrow C API over shadPS4's GCN shader recompiler.
 *
 *   GCN (PS4 Liverpool) shader + register state  ->  SPIR-V (+ optional MSL via SPIRV-Cross)
 *                                                 +  resource table describing what to bind.
 *
 * Model (mirrors shadPS4 src/video_core/renderer_vulkan/vk_pipeline_cache.cpp + vk_rasterizer.cpp):
 *
 *  1. Fill a gcn_runtime_info for the stage, normally with gcn_runtime_info_from_regs() from the
 *     Liverpool register file (dword-indexed: SH regs at 0x2C00+, context regs at 0xA000+,
 *     uconfig at 0xC000+; exactly the layout of ps4_gnm_metal.mm's g_liverpool_regs).
 *  2. gcn_compile() with the code address, the 16 USER_DATA SGPRs of the stage and a guest
 *     memory reader. The recompiler follows the fetch shader (s_swappc) and reads V#/T#/S#
 *     sharps through the reader, so a compiled shader is SPECIALIZED to the bound resources
 *     (buffer formats/strides, image types, vertex attribute number classes, ...). Before
 *     reusing a cached shader for another draw, call gcn_shader_is_compatible(); if it returns
 *     false compile a new permutation (shadPS4's StageSpecialization logic).
 *  3. At draw time, per stage:
 *       - gcn_shader_build_flatbuf() -> the "flattened user data" buffer (user SGPRs followed
 *         by every dword the shader reads through pointer chains). All sharps are fetched from
 *         it, and it is itself bound as the GCN_BUFFER_FLATBUF storage buffer if present.
 *       - for each buffer/image/sampler entry, fetch the sharp from the flatbuf with
 *         gcn_shader_fetch_*_sharp(), decode it (gcn_decode_*) and bind the resource at the
 *         MSL slot(s) reported in the binding entry (or at the SPIR-V binding for Vulkan-style
 *         backends: descriptor set 0, binding = spirv_binding).
 *       - vertex shaders with a fetch shader: one Metal vertex buffer per vertex attribute
 *         (location = attribute index), V# from gcn_shader_fetch_vertex_attrib_sharp().
 *       - push constants: struct gcn_push_data (see below).
 *
 * Stages: shadPS4 maps hardware stages to logical (SPIR-V) stages:
 *   VS-only pipelines:  HW VS -> SW Vertex,           HW PS -> SW Fragment,  HW CS -> SW Compute
 *   ES/GS pipelines:    HW ES -> SW Vertex (writes the ES->GS ring), HW GS -> SW Geometry, and the
 *                       "copy shader" (the HW VS program) is consumed by the GS translation.
 *   LS/HS (tess):       HW LS -> SW Vertex, HW HS -> SW TessControl, HW VS -> SW TessEval
 *                       (or HW ES -> SW TessEval when GS is also enabled).
 *   Only VS/PS/CS are expected to be useful for a Metal backend (Metal has no GS; tessellation
 *   would need SPIRV-Cross's compute-based tess emulation). Other stages compile but are untested.
 *
 * Errors inside the recompiler (asserts/unimplemented instructions) are caught and returned as
 * GCN_ERR_COMPILE with a message; they never abort the process.
 *
 * Thread safety: gcn_compile and the gcn_shader_* queries are re-entrant; a gcn_shader is
 * immutable after creation and may be used from several threads. The log/dump configuration
 * functions are global.
 */
#ifndef GCN_RECOMPILER_H
#define GCN_RECOMPILER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define GCN_API __attribute__((visibility("default")))
#else
#define GCN_API
#endif

#define GCN_RECOMPILER_API_VERSION 1

typedef enum gcn_status {
    GCN_OK = 0,
    GCN_ERR_INVALID_ARG = -1,
    GCN_ERR_READ_CODE = -2, /* shader code not readable / OrbShdr footer not found */
    GCN_ERR_COMPILE = -3,   /* recompiler failure (message in err buffer)          */
    GCN_ERR_MSL = -4,       /* SPIR-V produced, but SPIRV-Cross failed (shader still returned) */
} gcn_status;

/* Values equal shadPS4 Shader::HwStage. */
typedef enum gcn_hw_stage {
    GCN_HW_PS = 0,
    GCN_HW_VS = 1,
    GCN_HW_GS = 2,
    GCN_HW_ES = 3,
    GCN_HW_HS = 4,
    GCN_HW_LS = 5,
    GCN_HW_CS = 6,
} gcn_hw_stage;

/* Values equal shadPS4 Shader::SwStage. */
typedef enum gcn_sw_stage {
    GCN_SW_FRAGMENT = 0,
    GCN_SW_TESS_CONTROL = 1,
    GCN_SW_TESS_EVAL = 2,
    GCN_SW_VERTEX = 3,
    GCN_SW_GEOMETRY = 4,
    GCN_SW_COMPUTE = 5,
} gcn_sw_stage;

/* Guest memory reader: copy `size` bytes from guest GPU address `guest_addr` to `dst`.
 * Return false if [guest_addr, guest_addr+size) is not mapped. PS4Recomp:
 *   if (addr + size > ctx->mem_size) return false; memcpy(dst, ctx->mem_base + addr, size);
 * Called synchronously from gcn_compile / gcn_shader_build_flatbuf / ... only. */
typedef bool (*gcn_read_guest_fn)(void* ctx, uint64_t guest_addr, void* dst, uint64_t size);

/* ------------------------------------------------------------------------------------------ */
/* Profile: host capabilities (shadPS4 Shader::Profile). gcn_profile_default_metal() fills the  */
/* values matching Metal/SPIRV-Cross MSL 2.3 on Apple silicon (what shadPS4 effectively uses    */
/* on macOS via MoltenVK/KosmicKrisp minus the extensions SPIRV-Cross can't express).           */
/* ------------------------------------------------------------------------------------------ */
typedef struct gcn_profile {
    uint32_t supported_spirv; /* 0x00010600 */
    uint32_t subgroup_size;   /* 32 (Apple SIMD width); wave64 ops are lowered accordingly */
    uint32_t max_viewport_width, max_viewport_height; /* 16384 */
    uint32_t max_shared_memory_size;                  /* 32768 */
    bool support_int8, support_int16, support_int64, support_float16, support_float64;
    bool supports_image_load_store_lod;               /* false: AMD ext not in SPIRV-Cross */
    bool supports_native_cube_calc;                   /* false (AMD_gcn_shader)            */
    bool supports_trinary_minmax;                     /* false (AMD ext)                   */
    bool supports_buffer_fp32_atomic_min_max;         /* false */
    bool supports_image_fp32_atomic_min_max;          /* false */
    bool supports_buffer_int64_atomics;               /* false (no 64-bit atomics on Metal) */
    bool supports_shared_int64_atomics;               /* false */
    bool supports_workgroup_explicit_memory_layout;   /* false */
    bool supports_amd_shader_explicit_vertex_parameter; /* false */
    bool supports_fragment_shader_barycentric;        /* false (true => [[barycentric_coord]]) */
    bool supports_shader_stencil_export;              /* true  ([[stencil]]) */
    bool supports_depth_clip_control;                 /* false => VS remaps z for [-w,w] clip space */
    bool needs_lds_barriers;                          /* true  (as shadPS4 on Metal/KosmicKrisp) */
    bool needs_buffer_offsets;                        /* false: Metal binds buffers at any 4B offset */
    bool needs_buffer_bounds_checks;                  /* true: emulate GCN buffer range checking
                                                         (no robust buffer access on Metal);
                                                         sizes come from gcn_push_data.buf_sizes */
    bool needs_unorm_fixup;                           /* true  (as shadPS4 on Metal/KosmicKrisp) */
    bool needs_clip_distance_emulation;               /* false */
    bool lower_left_origin_mode;                      /* false */
} gcn_profile;

GCN_API void gcn_profile_default_metal(gcn_profile* out);

/* ------------------------------------------------------------------------------------------ */
/* Runtime info (shadPS4 Shader::RuntimeInfo) - register-derived state the shader depends on.   */
/* ------------------------------------------------------------------------------------------ */

/* Output slot kinds for VS/GS misc & clip/cull vectors (Shader::Output). */
enum {
    GCN_OUT_NONE = 0, GCN_OUT_POINT_SIZE, GCN_OUT_EDGE_FLAG, GCN_OUT_KILL_FLAG, GCN_OUT_GS_CUT_FLAG,
    GCN_OUT_RENDER_TARGET_INDEX, GCN_OUT_VIEWPORT_INDEX,
    GCN_OUT_CULL_DIST0, /* ..7 = +7 */
    GCN_OUT_CLIP_DIST0 = GCN_OUT_CULL_DIST0 + 8 /* ..7 = +7 */
};

typedef struct gcn_ps_input { /* SPI_PS_INPUT_CNTL_n */
    uint8_t param_index;      /* OFFSET     */
    uint8_t is_default;       /* DEFAULT_VAL enable */
    uint8_t is_flat;          /* FLAT_SHADE */
    uint8_t default_value;    /* DEFAULT_VAL (0..3) */
} gcn_ps_input;

typedef struct gcn_color_buffer { /* Shader::PsColorBuffer, from CB_COLORn_INFO etc. */
    uint8_t data_format;    /* AmdGpu::DataFormat (0 = invalid/unbound) */
    uint8_t num_format;     /* AmdGpu::NumberFormat */
    uint8_t num_conversion; /* AmdGpu::NumberConversion */
    uint8_t export_format;  /* SPI_SHADER_COL_FORMAT nibble (AmdGpu::ShaderExportFormat) */
    uint8_t blend_self_scale;
    uint8_t swizzle[4];     /* AmdGpu::CompSwizzle for r,g,b,a (0=0,1=1,4=R,5=G,6=B,7=A) */
} gcn_color_buffer;

typedef struct gcn_runtime_info {
    uint32_t hw_stage; /* gcn_hw_stage */
    uint32_t sw_stage; /* gcn_sw_stage */

    /* From SPI_SHADER_PGM_RSRC1/2_xS (COMPUTE_PGM_RSRC1/2 for CS). */
    uint8_t fp_denorm_mode32, fp_denorm_mode16_64, fp_round_mode32, fp_round_mode16_64;
    uint32_t num_allocated_vgprs; /* (VGPRS+1)*4 */
    uint32_t num_user_data;       /* USER_SGPR */
    uint32_t num_input_vgprs;     /* VGPR_COMP_CNT */
    bool dx10_clamp;

    /* SW Vertex */
    struct {
        uint32_t step_rate_0, step_rate_1; /* VGT_INSTANCE_STEP_RATE_0/1 */
        uint16_t vertex_sgpr_offset, instance_sgpr_offset; /* indirect draws only */
        bool is_indirect;
        bool tess_emulated_primitive; /* primitive type is RectList/QuadList */
    } vs_sw;
    /* SW TessControl / TessEval (ls_stride/hs_* come from the tess constant buffer V#,
     * resolved inside gcn_compile) */
    struct {
        uint32_t num_input_control_points, num_threads, tess_type;
        bool offchip_lds_enable;
    } tcs;
    struct {
        uint32_t tess_type, tess_topology, tess_partitioning;
    } tes;

    /* HW VS */
    struct {
        uint32_t num_outputs;
        uint8_t outputs[3][4]; /* GCN_OUT_* per POS1..3 export vector (MapOutputs) */
        bool emulate_depth_negative_one_to_one; /* clip_space [-w,w] and no depth clip control */
        bool clip_disable; /* PA_CL_CLIP_CNTL.CLIP_DISABLE || RectList: shader converts screen
                              space to NDC; set the Metal viewport to (0,0,min(maxw,16384),
                              min(maxh,16384)) as shadPS4 does */
        uint32_t user_clip_plane_mask;
    } vs;
    /* HW ES / LS */
    struct { uint32_t vertex_data_size; } es; /* VGT_ESGS_RING_ITEMSIZE */
    struct { uint32_t ls_stride; } ls;
    /* HW GS */
    struct {
        uint32_t num_outputs;
        uint8_t outputs[3][4];
        uint32_t num_invocations, output_vertices, in_vertex_data_size, out_vertex_data_size;
        uint32_t in_primitive;     /* AmdGpu::PrimitiveType */
        uint32_t out_primitive[4]; /* AmdGpu::GsOutputPrimitiveType per stream */
        uint32_t mode;             /* VGT_GS_MODE.MODE */
        uint64_t vs_copy_addr;     /* guest address of the copy shader (HW VS program) */
        uint32_t vs_copy_size;     /* bytes (0 = from OrbShdr footer) */
        uint64_t vs_copy_hash;
    } gs;
    /* HW PS */
    struct {
        uint32_t en_flags;   /* SPI_PS_INPUT_ENA  */
        uint32_t addr_flags; /* SPI_PS_INPUT_ADDR */
        uint32_t num_inputs; /* SPI_PS_IN_CONTROL.NUM_INTERP */
        gcn_ps_input inputs[32];
        gcn_color_buffer color_buffers[8];
        uint32_t z_export_format; /* SPI_SHADER_Z_FORMAT */
        uint32_t num_samples;
        uint32_t mrtz_mask; /* bit0 Z, bit1 stencil, bit2 mask, bit3 coverage_to_mask */
        bool front_face_all_bits, dual_source_blending, clip_distance_emulation,
            depth_before_shader;
    } fs;
    /* HW CS */
    struct {
        uint32_t shared_memory_size; /* bytes (LDS_SIZE * 512) */
        uint32_t workgroup_size[3];  /* COMPUTE_NUM_THREAD_X/Y/Z */
        bool tgid_enable[3];
    } cs;
} gcn_runtime_info;

/* Fill `out` for (hw_stage, sw_stage) from a Liverpool register file of `num_regs` dwords
 * (must be >= 0xD000), exactly like shadPS4 PipelineCache::BuildRuntimeInfo plus the colour
 * buffer part of RefreshGraphicsKey. Returns false on bad arguments. Fields that come from
 * draw packets rather than registers (vs_sw.is_indirect / *_sgpr_offset) are left 0. */
GCN_API bool gcn_runtime_info_from_regs(const uint32_t* regs, size_t num_regs, gcn_hw_stage hw,
                                        gcn_sw_stage sw, const gcn_profile* profile,
                                        gcn_runtime_info* out);

/* Program code/user data for a HW stage from the register file:
 *   code_addr  = (SPI_SHADER_PGM_LO_xS | PGM_HI_xS << 32) << 8   (COMPUTE_PGM_LO/HI for CS)
 *   user_data  = SPI_SHADER_USER_DATA_xS_0..15                    (COMPUTE_USER_DATA_0..15) */
GCN_API bool gcn_stage_from_regs(const uint32_t* regs, size_t num_regs, gcn_hw_stage hw,
                                 uint64_t* code_addr, uint32_t user_data[16]);

/* ------------------------------------------------------------------------------------------ */
/* MSL conversion options (SPIRV-Cross CompilerMSL)                                            */
/* ------------------------------------------------------------------------------------------ */
typedef struct gcn_msl_options {
    uint32_t msl_version_major, msl_version_minor; /* default 2.3 */
    bool argument_buffers;   /* default false (discrete [[buffer/texture/sampler(n)]] slots) */
    bool pad_fragment_output_components; /* default true */
    bool enable_point_size_builtin;      /* default true */
    bool invariant_float_math;           /* default false */
    /* Slot assignment is automatic & sequential per stage (buffers 0.., textures 0..,
     * samplers 0..); the push constant block takes the next buffer slot after resources.
     * Vertex buffers (stage_in) are NOT in MSL: use any slot >= info.msl_first_free_buffer. */
    /* Buffer slots a stage may use (Metal: [[buffer(0..30)]] -> default 31). A shader whose
     * discrete buffers need more slots gets its guest-memory storage buffers moved into one
     * argument buffer instead (see gcn_shader_info.msl_argument_buffer). */
    uint32_t max_buffer_slots;
} gcn_msl_options;

GCN_API void gcn_msl_options_default(gcn_msl_options* out);

/* ------------------------------------------------------------------------------------------ */
/* Compilation                                                                                  */
/* ------------------------------------------------------------------------------------------ */
enum {
    GCN_COMPILE_EMIT_MSL = 1u << 0, /* also run SPIRV-Cross (if the library was built with it) */
    GCN_COMPILE_ALLOW_NO_FOOTER = 1u << 1, /* code_size given, don't require OrbShdr footer */
    /* testing: the wave64 predication takes its fallback path, as for a shader it cannot
     * predicate (counted per subgroup; a barrier there fails the translation). Not part of any
     * shader cache key: for direct compiles in tests only. */
    GCN_COMPILE_WAVE64_FALLBACK = 1u << 2,
};

typedef struct gcn_compile_desc {
    uint32_t hw_stage;        /* gcn_hw_stage */
    uint32_t sw_stage;        /* gcn_sw_stage */
    uint64_t code_addr;       /* guest address of the program (used for OffsetByProgramBase too) */
    const uint32_t* code;     /* optional host pointer to the code; NULL => read via read_guest */
    uint32_t code_size;       /* bytes; 0 => length from the OrbShdr footer */
    uint64_t hash;            /* 0 => shader_hash from the OrbShdr footer (or FNV of the code) */
    uint32_t user_data[16];   /* USER_DATA SGPRs for this stage */
    const gcn_runtime_info* runtime; /* required */
    const gcn_profile* profile;      /* NULL => gcn_profile_default_metal */
    gcn_read_guest_fn read_guest;    /* required for fetch shaders / pointer-loaded sharps */
    void* read_ctx;
    uint32_t flags;                  /* GCN_COMPILE_* */
    const gcn_msl_options* msl;      /* NULL => defaults */
} gcn_compile_desc;

typedef struct gcn_shader gcn_shader;

/* On GCN_OK or GCN_ERR_MSL, *out is a new shader (destroy with gcn_shader_destroy). On error a
 * NUL-terminated message is written to err (if err_size > 0). */
GCN_API gcn_status gcn_compile(const gcn_compile_desc* desc, gcn_shader** out, char* err,
                               size_t err_size);
GCN_API void gcn_shader_destroy(gcn_shader* shader);

GCN_API const uint32_t* gcn_shader_spirv(const gcn_shader* shader, size_t* num_words);
/* NULL if MSL was not requested or failed. Entry point name is "main0". */
GCN_API const char* gcn_shader_msl(const gcn_shader* shader);
GCN_API const char* gcn_shader_msl_error(const gcn_shader* shader);

/* Convert arbitrary SPIR-V (e.g. a gcn_shader_spirv result) with the same settings. Returns a
 * malloc'd string (free with gcn_free) or NULL with a message in err. */
GCN_API char* gcn_spirv_to_msl(const uint32_t* spirv, size_t num_words, const gcn_msl_options* opt,
                               char* err, size_t err_size);
GCN_API void gcn_free(void* p);

enum {
    GCN_FETCH_INLINE_STRIDE = 1,  /* V# stride not a multiple of 4 */
    GCN_FETCH_INLINE_ADDRESS = 2, /* V# address (with the instruction offset) not a multiple of 4 */
    GCN_FETCH_INLINE_FORMAT = 4,  /* no Metal vertex format for the V#'s format */
};

typedef struct gcn_shader_info {
    uint32_t hw_stage, sw_stage;
    uint64_t pgm_hash;
    uint64_t code_addr;
    uint32_t code_size;
    uint32_t num_buffers, num_images, num_samplers, num_fmasks, num_vertex_attribs;
    uint32_t flatbuf_size_dw;      /* size of the flattened user data buffer (>= 16) */
    bool has_fetch_shader;
    bool fetch_shader_inlined;        /* the VS calls a fetch shader translated into it: an attribute
                                         V# Metal's vertex fetch cannot read (a stride or address
                                         that is not a multiple of 4) makes the shader read every
                                         attribute from its buffers (no vertex attributes,
                                         has_fetch_shader false; the offset SGPRs are added by the
                                         shader, not taken from the draw's base vertex/instance) */
    uint8_t fetch_inline_reason;      /* when inlined: why, for the first attribute Metal cannot
                                         fetch (GCN_FETCH_INLINE_* bits) */
    uint8_t fetch_inline_attrib;      /* ... and which attribute that is */
    uint32_t fetch_shader_sgpr_base;  /* user SGPR pair holding the fetch shader address (also when
                                         inlined) */
    int32_t vertex_offset_sgpr;       /* fetch shader adds this user SGPR to the vertex index (-1) */
    int32_t instance_offset_sgpr;     /* ... to the instance index (-1) */
    uint32_t mrt_mask;                /* PS: render targets exported (bit n = MRTn => [[color(n)]]) */
    bool has_discard;
    bool uses_dma;                    /* always false with the Metal profile */
    bool uses_guest_memory;           /* buffer accesses through V#s loaded at run time: see
                                         GCN_GUEST_* (bind the page table and fault buffer) */
    uint32_t null_image_reads;        /* image reads through a T# loaded at run time: they read the
                                         null image (no binding can stand for that T#) */
    uint32_t num_image_tables;        /* T# tables indexed at run time (gcn_shader_get_image_table) */
    uint32_t zeroed_write_lanes;      /* always 0: v_writelane results used to be emitted as 0 (a
                                         known gap, closed: a per-lane select, LowerWaveOpsPass);
                                         kept for the API and the cache format */
    uint32_t unpredicated_wave_regions; /* wave64 CS: EXEC-divergent ifs and loops that need the
                                         whole wave but stay per 32-wide subgroup (known gap:
                                         the predication fell back) */
    uint32_t per_subgroup_lane_ops;   /* wave64 CS: v_readlane / v_readfirstlane / v_mbcnt left per
                                         subgroup in non-uniform control flow (known gap) */
    uint32_t per_lane_ends;           /* wave64 CS: s_endpgm in EXEC-divergent code that ends only
                                         the lanes that reach it, not their whole wave (known gap:
                                         kept so where nothing after it needs the wave) */
    bool translation_failed;          /* some instructions were unknown (shader may be wrong) */
    uint32_t shared_memory_size;      /* CS: bytes of threadgroup memory used (incl. scratch) */
    uint32_t workgroup_size[3];
    /* MSL slot info (valid when MSL was emitted) */
    int32_t msl_push_constant_buffer; /* [[buffer(n)]] of push_data, -1 if not referenced */
    uint32_t msl_first_free_buffer;   /* first buffer slot not used by the shader */
    uint32_t msl_num_textures, msl_num_samplers;
    /* [[buffer(n)]] of the argument buffer holding the guest storage buffers that did not fit
     * in max_buffer_slots discrete slots, -1 if none. Its layout (Metal 3, tier 2) is a plain
     * array of msl_argument_buffer_entries 64-bit GPU addresses: bind
     * entry[gcn_buffer_binding.msl_arg_index] = MTLBuffer.gpuAddress + offset of the V# base
     * (unused entries: any valid buffer address), and make each buffer resident with
     * useResource:usage: on the encoder. */
    int32_t msl_argument_buffer;
    uint32_t msl_argument_buffer_entries;
} gcn_shader_info;

GCN_API void gcn_shader_get_info(const gcn_shader* shader, gcn_shader_info* out);

/* ---- Resource table ---------------------------------------------------------------------- */

/* How to assemble a sharp from the flattened user data buffer (Shader::SharpFetch):
 * kind 0: single load -> memcpy(out, flatbuf + offsets[0], num_dwords*4)
 * kind 1: multi load  -> out[i] = (load_mask >> i & 1) ? flatbuf[offsets[i]] : immediates[i]
 * kind 2: invalid     -> null resource
 * Prefer gcn_shader_fetch_*_sharp(), which also applies post_op. */
typedef struct gcn_sharp_fetch {
    uint8_t kind, num_dwords, load_mask, _pad;
    uint16_t offsets[8];
    uint32_t immediates[8];
} gcn_sharp_fetch;

typedef enum gcn_buffer_type { /* Shader::BufferType */
    GCN_BUFFER_GUEST = 0,          /* guest memory described by the V#                    */
    GCN_BUFFER_FLATBUF = 1,        /* bind gcn_shader_build_flatbuf() contents (read-only) */
    GCN_BUFFER_BDA_PAGETABLE = 2,  /* guest memory page table (GCN_GUEST_*), read-only     */
    GCN_BUFFER_FAULT_BUFFER = 3,   /* guest memory fault buffer (GCN_GUEST_*), written     */
    GCN_BUFFER_GDS = 4,            /* global data share (64 KiB, shared by all draws)     */
    GCN_BUFFER_SHARED_MEMORY = 5,  /* CS LDS spilled to a storage buffer: size =
                                      runtime.cs.shared_memory_size * num_workgroups, zeroed */
    GCN_BUFFER_CLIP_PLANES = 6,    /* float4[6] from PA_CL_UCP_n_X..W                       */
} gcn_buffer_type;

/* Guest memory (gcn_shader_info.uses_guest_memory). A buffer access through a V# the shader
 * loads at run time, or a MUBUF/MTBUF access with ADDR64 (address = V#.base + a 64-bit VGPR
 * pointer + soffset + inst_offset), cannot be bound before the work runs: the shader reads and
 * writes guest memory through a page table instead. Compute shaders load and store; draw stages
 * (VS, PS) only load (a guest memory store outside compute fails to translate), and the renderer
 * binds them one fault buffer that stays in mode 0 (no probe; an unmapped page reads 0 and is
 * reported in GCN_GUEST_UNMAPPED, to be mapped for later draws).
 * - GCN_BUFFER_BDA_PAGETABLE: GCN_GUEST_PAGES uint64 entries, one per 64 MiB page of the 40-bit
 *   guest address space: the GPU address (MTLBuffer.gpuAddress + offset) of host memory holding
 *   guest [p << GCN_GUEST_PAGE_SHIFT, (p + 1) << GCN_GUEST_PAGE_SHIFT), or 0 if not mapped. Make
 *   every buffer it points into resident (useResource:usage:): read and write for compute, read
 *   only for draw stages (they only load).
 * - GCN_BUFFER_FAULT_BUFFER: GCN_GUEST_FAULT_DWORDS uint32, zero before a dispatch except the
 *   mode dword. With mode 1 (a probe) the shader writes nothing (no guest, buffer or image
 *   store, no atomic) but records the pages it would access: run it, map the pages of the
 *   GCN_GUEST_UNMAPPED bitmap, repeat until that bitmap stays empty, then run with mode 0.
 *   GCN_GUEST_READ / GCN_GUEST_WRITTEN: 1 MiB pages read / written (also in a probe).
 *   GCN_GUEST_STATUS: GCN_GUEST_STATUS_* bits; every flagged access was not made. */
#define GCN_GUEST_PAGE_SHIFT 26u
#define GCN_GUEST_PAGES (1u << (40u - GCN_GUEST_PAGE_SHIFT))
/* guest addresses below this are never accessed (reported unmapped); the entry of page 0 is the
 * GPU address of guest address 0, though only [GCN_GUEST_FIRST_MAPPED, 64 MiB) is mapped; an
 * address below it is reported as GCN_GUEST_STATUS_RANGE */
#define GCN_GUEST_FIRST_MAPPED 0x10000ull
#define GCN_GUEST_TRACK_SHIFT 20u
#define GCN_GUEST_TRACK_PAGES (1u << (40u - GCN_GUEST_TRACK_SHIFT))
#define GCN_GUEST_MODE 0u
#define GCN_GUEST_STATUS 1u
#define GCN_GUEST_UNMAPPED 4u
#define GCN_GUEST_READ (GCN_GUEST_UNMAPPED + GCN_GUEST_PAGES / 32u)
#define GCN_GUEST_WRITTEN (GCN_GUEST_READ + GCN_GUEST_TRACK_PAGES / 32u)
#define GCN_GUEST_FAULT_DWORDS (GCN_GUEST_WRITTEN + GCN_GUEST_TRACK_PAGES / 32u)
#define GCN_GUEST_STATUS_UNMAPPED 0x1u  /* mode 0: an access hit an unmapped page             */
#define GCN_GUEST_STATUS_FORMAT 0x2u    /* a V# data/number format other than 32-bit u/s/float */
#define GCN_GUEST_STATUS_SWIZZLE 0x4u   /* a swizzled V# (swizzle_enable)                     */
#define GCN_GUEST_STATUS_RANGE 0x8u     /* an address at or above 2^40 or below GCN_GUEST_FIRST_MAPPED */
#define GCN_GUEST_STATUS_UNALIGNED 0x10u /* a dword at an address that is not 4-byte aligned  */

enum { /* used_types bits (Shader::IR::Type subset) */
    GCN_USES_U8 = 1u << 0, GCN_USES_U16 = 1u << 1, GCN_USES_U32 = 1u << 2,
    GCN_USES_F32 = 1u << 3, GCN_USES_U64 = 1u << 4,
};

enum { /* Shader::SharpFetchPostOp */
    GCN_POSTOP_NONE = 0, GCN_POSTOP_OR_DW1_IMM, GCN_POSTOP_OFFSET_BY_PROGRAM_BASE,
    GCN_POSTOP_CLEAR_STRIDE_SHIFT_DW2, GCN_POSTOP_CUBE_TO_2D_ARRAY,
    GCN_POSTOP_DISABLE_ANISO_IF_SINGLE_LOD, GCN_POSTOP_FORCE_REPEAT_XYZ,
    GCN_POSTOP_FORCE_LAST_TEXEL_XY, GCN_POSTOP_CLEAR_ANISO,
};

typedef struct gcn_buffer_binding {
    uint32_t spirv_binding; /* descriptor set 0, Binding decoration (StorageBuffer)       */
    uint32_t buffer_slot;   /* index among buffers (gcn_push_data.buf_offsets byte index) */
    uint32_t type;          /* gcn_buffer_type */
    uint32_t used_types;    /* GCN_USES_* : the SPIR-V declares one aliased SSBO per type */
    bool is_written, is_formatted;
    uint32_t post_op, post_op_dw1_mask, post_op_dw2_shift;
    gcn_sharp_fetch sharp;
    /* MSL slot(s). SPIRV-Cross merges the per-type aliases of one binding into a single
     * "device void* spvBufferAliasSet0BindingN [[buffer(n)]]", so this is normally 1 entry
     * (0 entries = declared but unused after specialization: bind nothing). If more than one
     * is listed, bind the same MTLBuffer+offset to each. Bind so that shader offset 0 is the
     * V# base address (guest address -> host buffer offset; dword aligned). */
    uint32_t num_msl_buffers;
    uint32_t msl_buffers[5];
    /* Entry in the argument buffer (gcn_shader_info.msl_argument_buffer) when the buffer is
     * reached through it instead of a discrete slot (num_msl_buffers is then 0);
     * UINT32_MAX otherwise. */
    uint32_t msl_arg_index;
} gcn_buffer_binding;

typedef struct gcn_image_binding {
    uint32_t spirv_binding; /* first binding; array of num_bindings when mip_fallback == 1 */
    uint32_t num_bindings;  /* >1: one view per mip level (storage writes to explicit mips) */
    bool is_storage;        /* written => storage image (read_write texture), else sampled */
    bool is_depth, is_atomic, is_array, is_r128, is_integer;
    uint8_t mip_fallback_mode; /* 0 none, 1 dynamic index (array of mips), 2 constant index */
    uint8_t constant_mip_index;
    uint32_t view_type;        /* AmdGpu::ImageType the shader was specialized for */
    uint32_t post_op;
    gcn_sharp_fetch sharp;     /* 8 dwords (4 when is_r128) */
    uint32_t msl_texture;      /* [[texture(n)]] (first of num_bindings) */
    /* Entry table_entry of T# table `table` (gcn_shader_get_image_table): a T# the SRT walker
     * copies from a table the shader indexes at run time; UINT32_MAX (both) for other images.
     * Entries that hold no T# Metal can create fetch as false (gcn_shader_fetch_image_sharp). */
    uint32_t table, table_entry;
} gcn_image_binding;

/* A T# table a shader indexes at run time (dword i of the T# at dword first + i + stride *
 * index of a guest buffer). Its entries a draw can reach are images (gcn_image_binding.table);
 * the shader uses the entry whose offset it loaded the T# from. */
typedef enum gcn_image_table_kind {
    GCN_IMAGE_TABLE_NONE = 0,    /* no bound fits (more than 8 entries): its reads read the null
                                    image (counted in gcn_shader_info.null_image_reads) */
    GCN_IMAGE_TABLE_RECORDS = 1, /* the table buffer's V# bounds it */
    GCN_IMAGE_TABLE_INDEXED = 2, /* the index is read from a bounded buffer (index_buffer) */
    GCN_IMAGE_TABLE_LOOP = 3,    /* the index is a loop counter with a count in the user data */
} gcn_image_table_kind;

typedef struct gcn_image_table {
    uint32_t kind;          /* gcn_image_table_kind */
    uint32_t table_buffer;  /* gcn_shader_get_buffer index of the buffer holding the T#s */
    uint32_t index_buffer;  /* INDEXED: buffer index holding the indices, else UINT32_MAX */
    uint32_t first_dw, stride_dw, num_dwords;
    uint32_t num_entries;   /* images bound for it */
    uint32_t reached;       /* entries the draw the shader was compiled for reaches */
    uint32_t num_sites;     /* image instructions reading it */
} gcn_image_table;

GCN_API bool gcn_shader_get_image_table(const gcn_shader* s, uint32_t index, gcn_image_table* out);
/* The guest memory the SRT walker reads for the T# tables of a draw whose flattened user data is
 * flatbuf (gcn_shader_build_flatbuf): (address, bytes) of every table entry and index it copied,
 * the first `max` into out. Returns their number. A renderer that runs GPU work out of order with
 * the CPU's reads checks them against pending GPU writes: the flattened data is a CPU snapshot. */
GCN_API uint32_t gcn_shader_image_table_reads(const gcn_shader* s, const uint32_t* flatbuf,
                                              uint64_t (*out)[2], uint32_t max);

typedef struct gcn_sampler_binding {
    uint32_t spirv_binding;
    bool is_depth;
    uint32_t post_op;
    uint32_t post_op_tsharp_dw3_off;
    gcn_sharp_fetch sharp; /* 4 dwords */
    uint32_t msl_sampler;  /* [[sampler(n)]] */
} gcn_sampler_binding;

typedef struct gcn_vertex_attrib { /* Shader::Gcn::VertexAttribute (fetch shader) */
    uint32_t location;     /* SPIR-V Input location == MSL [[attribute(n)]] == attribute index */
    uint8_t dest_vgpr, num_elements;
    uint8_t sgpr_base;     /* user SGPR pair holding the V# table pointer (or V# itself) */
    uint8_t dword_offset;  /* dword offset of the V# in that table */
    uint8_t step_rate;     /* 0 per-vertex, 1 instance/step_rate_0, 2 instance/step_rate_1,
                              3 per-instance (divisor 1) */
    uint16_t inst_offset;  /* byte offset added to the V# base address (12-bit instruction field) */
    uint8_t data_format, num_format; /* typed-load overrides (0 = use V#) */
    uint32_t divisor;      /* instance step divisor resolved from the runtime info (0 = vertex) */
} gcn_vertex_attrib;

GCN_API bool gcn_shader_get_buffer(const gcn_shader* s, uint32_t index, gcn_buffer_binding* out);
GCN_API bool gcn_shader_get_image(const gcn_shader* s, uint32_t index, gcn_image_binding* out);
GCN_API bool gcn_shader_get_sampler(const gcn_shader* s, uint32_t index, gcn_sampler_binding* out);
/* Whether Metal's vertex fetch has a vertex format for a V#'s raw 4-bit data format and 3-bit
 * number format (Gcn::IsVertexFormatFetchable after the formats' remapping). A fetch shader with
 * an attribute that has none is translated inline (gcn_shader_info.fetch_shader_inlined). The
 * renderer's table (ps4_gnm_resources.mm GetVertexFormat) must agree. */
GCN_API bool gcn_vertex_format_fetchable(uint32_t dfmt, uint32_t nfmt);

GCN_API bool gcn_shader_get_vertex_attrib(const gcn_shader* s, uint32_t index,
                                          gcn_vertex_attrib* out);

/* Push constant block (std430, 280 bytes; MSL: `constant AuxData& push_data`).
 * xoffset..yscale: PA_CL_VPORT_XOFFSET/YOFFSET/XSCALE/YSCALE of viewport 0 (or 0/1 when the
 * corresponding PA_CL_VTE_CNTL enable bit is clear) - only read by VS with clip_disable.
 * buf_offsets: per buffer_slot byte offset (only with profile.needs_buffer_offsets).
 * buf_sizes: per buffer_slot size in bytes of guest buffers (V# num_records, times stride if
 * non-zero) - accesses at/after it are dropped (stores/atomics) or read as 0, like GCN does.
 * Only with profile.needs_buffer_bounds_checks; 0 = every access out of range. */
#define GCN_MAX_BUFFERS 56
typedef struct gcn_push_data {
    float xoffset, yoffset, xscale, yscale;
    uint8_t buf_offsets[40];
    uint32_t buf_sizes[GCN_MAX_BUFFERS];
} gcn_push_data;

/* ---- Draw-time helpers ------------------------------------------------------------------- */

/* Build the flattened user data for the current user SGPRs: out[0..15] = user_data, then the
 * dwords gathered by the shader's SRT walker program via `read`. out_dw must be >=
 * info.flatbuf_size_dw. Returns false if a guest read failed (those dwords are 0). */
GCN_API bool gcn_shader_build_flatbuf(const gcn_shader* s, const uint32_t user_data[16],
                                      gcn_read_guest_fn read, void* ctx, uint32_t* out,
                                      size_t out_dw);

/* Sharps with post-ops applied (shadPS4 *Resource::GetSharp). Return false for a null/invalid
 * resource (out then holds the null descriptor; bind a dummy). code_addr is the shader's guest
 * code address (OffsetByProgramBase: inline constant buffers embedded in the shader). */
GCN_API bool gcn_shader_fetch_buffer_sharp(const gcn_shader* s, uint32_t index,
                                           const uint32_t* flatbuf, uint64_t code_addr,
                                           uint32_t out_vsharp[4]);
GCN_API bool gcn_shader_fetch_image_sharp(const gcn_shader* s, uint32_t index,
                                          const uint32_t* flatbuf, uint32_t out_tsharp[8]);
/* false (out zeroed) for an S# the shader loads at run time (no sharp to fetch) or an invalid one */
GCN_API bool gcn_shader_fetch_sampler_sharp(const gcn_shader* s, uint32_t index,
                                            const uint32_t* flatbuf, uint32_t out_ssharp[4]);
GCN_API bool gcn_shader_fetch_vertex_attrib_sharp(const gcn_shader* s, uint32_t index,
                                                  const uint32_t user_data[16],
                                                  gcn_read_guest_fn read, void* ctx,
                                                  uint32_t out_vsharp[4]);

/* True if the shader can be reused for a draw with these user SGPRs / runtime state
 * (shadPS4 StageSpecialization equality: runtime info, fetch shader layout, vertex attribute
 * number classes, buffer strides/formats, image types, sampler flags). */
GCN_API bool gcn_shader_is_compatible(const gcn_shader* s, const uint32_t user_data[16],
                                      const gcn_runtime_info* runtime, gcn_read_guest_fn read,
                                      void* ctx);

/* Index of the first shader in list[0..count) that gcn_shader_is_compatible() accepts, or -1.
 * The permutations of one program share its resource layout, so the draw's specialization is
 * built once (one pass over the pointer chains) and compared with each permutation, instead of
 * once per permutation. */
GCN_API int gcn_shader_find_compatible(const gcn_shader* const* list, uint32_t count,
                                       const uint32_t user_data[16], const gcn_runtime_info* runtime,
                                       gcn_read_guest_fn read, void* ctx);

/* Same as gcn_shader_find_compatible() with the draw's flattened user data already built by
 * gcn_shader_build_flatbuf() for list[0], so the pointer chains are walked once per draw for both
 * the check and the binding. The flattened data is only valid for permutations with list[0]'s SRT
 * program (gcn_shader_same_srt); the others are checked against guest memory on their own. */
GCN_API int gcn_shader_find_compatible_flat(const gcn_shader* const* list, uint32_t count,
                                            const uint32_t user_data[16], const uint32_t* flatbuf,
                                            size_t flatbuf_dw, const gcn_runtime_info* runtime,
                                            gcn_read_guest_fn read, void* ctx);

/* ---- Indexed permutation lookup --------------------------------------------------------- */
/* For caches with many permutations of one program: the draw's specialization is built once
 * (one pass over the pointer chains) against `base`, a permutation of the program, and hashed;
 * permutations are indexed by the same hash, so a lookup compares only the permutations with an
 * equal hash instead of all of them.
 *
 * gcn_shader_is_compatible() compares a buffer or image slot only where the draw has a resource
 * bound, so hashes are taken under a slot mask: the draw's own (gcn_draw_spec_mask). For every
 * permutation s with gcn_shader_same_resource_layout(s, base) that is compatible with the draw,
 * gcn_shader_spec_hash(s, mask) == gcn_draw_spec_hash(spec, mask). The hash covers only part of
 * what is compared, so equal hashes are confirmed with gcn_shader_matches_draw_spec(), which is
 * exactly gcn_shader_is_compatible(). Permutations with another resource layout must be checked
 * with gcn_shader_matches_draw_spec() directly. */
typedef struct gcn_draw_spec gcn_draw_spec;
/* NULL if the specialization cannot be built (bad arguments, geometry shaders): fall back to
 * gcn_shader_is_compatible() for every permutation. */
GCN_API gcn_draw_spec* gcn_draw_spec_create(const gcn_shader* base, const uint32_t user_data[16],
                                            const gcn_runtime_info* runtime, gcn_read_guest_fn read,
                                            void* ctx);
GCN_API void gcn_draw_spec_destroy(gcn_draw_spec* spec);
GCN_API void gcn_draw_spec_mask(const gcn_draw_spec* spec, uint64_t mask[2]);
GCN_API uint64_t gcn_draw_spec_hash(const gcn_draw_spec* spec, const uint64_t mask[2]);
GCN_API uint64_t gcn_shader_spec_hash(const gcn_shader* s, const uint64_t mask[2]);
GCN_API bool gcn_shader_same_resource_layout(const gcn_shader* a, const gcn_shader* b);
/* gcn_shader_is_compatible(s, user_data, runtime, read, ctx) for the draw `spec` was built for. */
GCN_API bool gcn_shader_matches_draw_spec(const gcn_shader* s, const gcn_draw_spec* spec,
                                          const uint32_t user_data[16], const gcn_runtime_info* runtime,
                                          gcn_read_guest_fn read, void* ctx);

/* Debug: why gcn_shader_is_compatible() refuses s for a draw: the first differing part of the
 * specialization ("buffer 2: stride 12 vs 32", ...), written to out. Returns false if s is
 * compatible (out = ""). */
GCN_API bool gcn_shader_explain_mismatch(const gcn_shader* s, const uint32_t user_data[16],
                                         const gcn_runtime_info* runtime, gcn_read_guest_fn read,
                                         void* ctx, char* out, size_t out_size);

/* True if a and b flatten user data the same way (same SRT walker program and flattened size),
 * so the flattened user data built for one is valid for the other. */
GCN_API bool gcn_shader_same_srt(const gcn_shader* a, const gcn_shader* b);

/* ---- Sharp decoding (AmdGpu::Buffer / Image / Sampler) ----------------------------------- */
typedef struct gcn_buffer_desc {
    uint64_t base_address;
    uint32_t stride, num_records, size_bytes; /* size = stride ? stride*num_records : num_records */
    uint8_t dst_sel[4];  /* CompSwizzle */
    uint8_t num_format, data_format; /* AmdGpu::NumberFormat / DataFormat (buffer encoding) */
    uint8_t element_size, index_stride;
    bool swizzle_enable, add_tid_enable, valid;
    uint8_t mtype;
} gcn_buffer_desc;

typedef struct gcn_image_desc {
    uint64_t base_address; /* bytes (T# address << 8) */
    uint32_t width, height, depth, pitch; /* already +1 */
    uint32_t base_level, last_level, base_array, last_array, num_levels, num_layers, num_samples;
    uint32_t type;          /* AmdGpu::ImageType (8=1D,9=2D,10=3D,11=Cube,12=1DArray,13=2DArray,
                               14=2DMsaa,15=2DMsaaArray) */
    uint8_t data_format, num_format; /* AmdGpu::DataFormat / NumberFormat (image encoding) */
    uint8_t dst_sel[4];
    uint8_t tiling_index;   /* GB_TILE_MODEn index (AmdGpu::TileMode) */
    bool pow2pad, valid;
    float min_lod;
} gcn_image_desc;

typedef struct gcn_sampler_desc {
    uint8_t clamp_x, clamp_y, clamp_z; /* AmdGpu::ClampMode */
    uint8_t max_aniso, depth_compare_func, xy_mag_filter, xy_min_filter, z_filter, mip_filter;
    uint8_t filter_mode, border_color_type;
    bool force_unnormalized, force_degamma, trunc_coord, disable_cube_wrap;
    float lod_bias, min_lod, max_lod;
    uint32_t border_color_ptr;
} gcn_sampler_desc;

GCN_API void gcn_decode_buffer(const uint32_t vsharp[4], gcn_buffer_desc* out);
GCN_API void gcn_decode_image(const uint32_t tsharp[8], gcn_image_desc* out);
GCN_API void gcn_decode_sampler(const uint32_t ssharp[4], gcn_sampler_desc* out);

/* ---- Persistent shader cache support ------------------------------------------------------ */
/* A compiled shader can be stored on disk and recreated in a later session without running the
 * GCN translation again. The serialized form holds everything the gcn_shader_* queries and the
 * draw-time helpers use (resource tables, SRT flattening program, fetch shader layout, the
 * StageSpecialization used by gcn_shader_is_compatible, runtime info, profile, SPIR-V, MSL and
 * MSL slot assignment), so a deserialized shader behaves exactly like the compiled one.
 *
 * Draw-specific values are not part of it: the code address is given at deserialization, and the
 * user data / flattened user data of the compiling draw are not kept (every query takes the
 * current ones). A shader is still specialized to the bound resources it was compiled for:
 * reuse it only for draws where gcn_shader_is_compatible() returns true, as with a shader
 * compiled in the same session.
 *
 * Blobs are only valid for the library build that wrote them. gcn_shader_cache_id() identifies
 * that build (a hash of the recompiler, Sirit and SPIRV-Cross sources plus the serialization
 * format version); put it into the cache key or directory so that a rebuilt recompiler starts
 * from an empty cache. gcn_shader_deserialize() also rejects blobs written by another build. */

/* NUL-terminated identifier of this library build and its serialization format. */
GCN_API const char* gcn_shader_cache_id(void);

/* Optional fast path: guest addresses [0, size) are host memory at base, and `read` (with
 * `ctx`) is a plain bounds-checked copy from it. Reads the recompiler would make through that
 * reader (SRT walks, sharps, fetch shaders) are then done directly. Call once, before any
 * other thread uses the recompiler; read == NULL disables it. */
GCN_API void gcn_set_direct_guest_memory(gcn_read_guest_fn read, void* ctx, const void* base, uint64_t size);

/* Optional: `mapped(ctx, addr)` tells whether guest address addr is mapped. An entry of a T#
 * table (gcn_image_binding.table) whose T# starts at an unmapped address is then unbound (the
 * null image), in the specialization and in gcn_shader_fetch_image_sharp alike: the unused
 * records of a table hold data that is no T# and often decodes to a far-off address. Call once,
 * before any other thread uses the recompiler; mapped == NULL (the default) counts every
 * address as mapped. */
typedef bool (*gcn_guest_mapped_fn)(void* ctx, uint64_t addr);
GCN_API void gcn_set_image_address_check(gcn_guest_mapped_fn mapped, void* ctx);

/* Serialize a shader. On GCN_OK *out_data is a malloc'd buffer of *out_size bytes (free with
 * gcn_free). The output depends only on the translation result, not on the draw that
 * triggered it. */
GCN_API gcn_status gcn_shader_serialize(const gcn_shader* s, void** out_data, size_t* out_size);

/* Recreate a shader from gcn_shader_serialize() output for the program at guest address
 * code_addr. Returns GCN_ERR_INVALID_ARG (message in err) for truncated, corrupt or
 * foreign-build data. */
GCN_API gcn_status gcn_shader_deserialize(const void* data, size_t size, uint64_t code_addr,
                                          gcn_shader** out, char* err, size_t err_size);

/* Independent copy of a shader for the same program loaded at guest address code_addr. */
GCN_API gcn_shader* gcn_shader_clone(const gcn_shader* s, uint64_t code_addr);

/* ---- RectList ---------------------------------------------------------------------------- */

/* A RectList draw (primitive type 17) gives 3 vertices per rectangle; the GPU makes the 4th.
 * Metal has no rectangle primitive and no stage between the vertex and the fragment shader
 * (shadPS4 adds the vertex in a tessellation control shader, emit_spirv_quad_rect.cpp), so the
 * draw becomes two draws in the same render pass:
 *  1. capture_msl: the vertex shader with rasterization disabled. Same resource slots and vertex
 *     inputs as gcn_shader_msl(); it stores the output struct (vertex_stride bytes) of vertex v
 *     of instance i at [[buffer(capture_buffer)]] + (i - base instance) * n + (v - base vertex),
 *     with n = ((device uint*)[[buffer(params_buffer)]])[0].
 *  2. A vertex-to-vertex buffer memory barrier, then expand_msl: 6 vertices (two triangles) per
 *     rectangle and no vertex inputs. [[buffer(0)]] the captured vertices, [[buffer(1)]] uint
 *     slots[3 * rectangles] (capture index of each rectangle's vertices, the index buffer values
 *     of an indexed draw), [[buffer(2)]] uint n as above. The corner is the vertex that shares x
 *     with one of the others and y with the other (vertex 0 if none does); the 4th vertex is the
 *     sum of the other two minus the corner, for the position and every output. The output
 *     struct is the vertex shader's, so the fragment shader links unchanged.
 * Both entry points are "main0". opt: the options the shader was compiled with (NULL =>
 * defaults). Free with gcn_rectlist_msl_free(). */
typedef struct gcn_rectlist_msl {
    char* capture_msl;
    char* expand_msl;
    uint32_t vertex_stride;  /* bytes per captured vertex */
    uint32_t capture_buffer; /* [[buffer(n)]] of the capture variant's output */
    uint32_t params_buffer;  /* [[buffer(n)]] of its uint n */
} gcn_rectlist_msl;

GCN_API gcn_status gcn_shader_rectlist_msl(const gcn_shader* vs, const gcn_msl_options* opt,
                                           gcn_rectlist_msl* out, char* err, size_t err_size);
GCN_API void gcn_rectlist_msl_free(gcn_rectlist_msl* m);

/* ---- Misc -------------------------------------------------------------------------------- */

/* Locate the OrbShdr footer of a shader binary (AmdGpu::SearchBinaryInfo). */
GCN_API bool gcn_find_binary_info(gcn_read_guest_fn read, void* ctx, uint64_t code_addr,
                                  uint32_t* size_bytes, uint64_t* shader_hash);

/* level: 0 trace .. 3 warning (default) .. 5 critical, 6 off. Default sink: stderr. */
typedef void (*gcn_log_fn)(void* ctx, int level, const char* msg);
GCN_API void gcn_set_log_level(int level);
GCN_API void gcn_set_log_callback(gcn_log_fn fn, void* ctx);
/* Enable shadPS4-style debug dumps (IR, fetch shaders) into <dir>/dumps; NULL disables. */
GCN_API void gcn_set_dump_dir(const char* dir);

GCN_API const char* gcn_version(void);

#ifdef __cplusplus
}
#endif

#endif /* GCN_RECOMPILER_H */
