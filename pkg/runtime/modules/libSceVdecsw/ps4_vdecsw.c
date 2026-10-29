// SPDX-License-Identifier: GPL-2.0-or-later
//
// libSceVdecsw (the PS4 "software" video decoder API, used by Uncharted 4) on the shared
// VideoToolbox H.264 core (libSceVideodec/ps4_h264_vt.c). H.264/AVC only; HEVC is refused.
//
// Struct layouts, error codes and the picture-info trailer follow shadPS4
// (src/core/libraries/videodec/vdecsw.{h,cpp}, vdecsw_impl.cpp, videodec_error.h,
// Copyright 2026 shadPS4 Emulator Project, GPL-2.0-or-later); the call contract below was
// checked against Uncharted 4 (CUSA04529 v1.32) eboot call sites.
//
// ---------------------------------------------------------------------------------------------
// API contract. [V] = verified in U4's code, [S] = from shadPS4, [I] = inferred (no reference
// shows it; chosen to fit how U4 handles the result). Offsets are hex.
//
// DecoderConfigInfo (this_size 0x50, V1 0x48 [V: U4 passes 0x48]):
//   00 this_size  08 resource_type u32  0C codec_type u32 (1 = AVC, 974921 = HEVC)  10 profile
//   14 max_level  18 max_frame_width s32  1C max_frame_height s32  20 max_dpb_frame_count s32
//   24 decode_pipeline_depth u32  28 compute_queue  30 cpu_affinity_mask  38 cpu_thread_priority
//   3C optimize_progressive_video u8  3D check_memory_type u8  3E reserved0  3F extra_dpb s8
//   40 extra_config_info ptr  [0x50 only: 48 disable_sync_decode_output u8, 4C max_pending_sync]
//   U4 [V]: player A {1, AVC, 100, 42, 1920x1088, dpb 4, depth 3, affinity 0x3f, progressive 1,
//   check_memory_type 1, extra_dpb 1}; player B {1, AVC, 100, 31, 1280x720, dpb -1, depth 4}.
// DecoderMemoryInfo 0x48: 00 this_size 08 cpu_memory_size 10 cpu_memory 18 gpu_memory_size
//   20 gpu_memory 28 cpu_gpu_memory_size 30 cpu_gpu_memory 38 max_frame_buffer_size
//   40 frame_buffer_alignment u32. U4 [V] allocates cpu / gpu / cpu_gpu itself from the sizes
//   returned (cpu 16-byte aligned, the others 2 MiB) inside fixed arenas with bound checks.
//   This implementation needs no guest memory, so it reports 64 KiB each [I: small on purpose,
//   shadPS4's fake 16 MiB could overrun U4's arenas]. max_frame_buffer_size =
//   align(align(w,64) * align(h,16) * 3/2, 256) + 0x4000 [S]; for 1920x1088 that is 0x301000,
//   which rounded to 2 MiB is the 0x400000 U4 asserts for its frame buffers [V].
// ComputeConfigInfo 0x10 {00 this_size, 08 pipe u16, 0A queue u16, 0C check_memory_type u8,
//   0D reserved0, 0E reserved1 u16} [S, V: U4 pipe 3 queue 3 check 1]; ComputeMemoryInfo 0x18
//   {00 this_size, 08 cpu_gpu_memory_size, 10 cpu_gpu_memory} [S]. The queue handle is the
//   cpu_gpu_memory address [S]; reported size 16 KiB [I] (U4 rounds it to 16 KiB).
// InputData 0x30 {00 this_size, 08 au_data, 10 au_size, 18 pts, 20 dts, 28 attached_data} [S,V].
//   U4 [V]: Annex-B access units (it writes 00 00 00 01 before every NAL unit); player A passes
//   pts = dts = -1 and a frame counter as attached_data, player B a real pts and dts = -1.
// InputResult 0x18 {00 this_size, 08 decoded_au, 10 output_frame_count u32} [S]. Player A [V]
//   wakes its output thread when output_frame_count != 0; player B [V] ignores it and expects
//   exactly one picture per accepted access unit.
// FrameBuffer 0x18 {00 this_size, 08 frame_buffer, 10 frame_buffer_size} [S,V].
// OutputInfo 0x38 (old 0x30) {00 this_size, 08 is_valid, 09 is_last_frame, 0A is_error_frame,
//   0B picture_count, 0C codec_type, 10 frame_width, 14 frame_pitch, 18 frame_height,
//   1C is_discarded_frame, 20 frame_buffer, 28 frame_buffer_size, [0x38 only: 30 frame_format,
//   34 frame_pitch_in_bytes]} [S]. U4 passes this_size 0x30 [V] and keeps its first picture-info
//   buffer right after it, so nothing past this_size may be written [V].
// AvcPictureInfo 0x78, written as a trailer at frame_buffer + frame_buffer_size [S]; layout in
//   VdecswAvcPictureInfo below. U4 asks for this_size 0x68 [V] and GetAvcPictureInfo copies
//   this_size - 8 bytes after the caller's this_size [S]. U4 player B reads pts (0x10) [V].
//
// Calls and return codes:
//   QueryComputeMemoryInfo / AllocateComputeQueue / ReleaseComputeQueue: bookkeeping [S].
//   QueryDecoderMemoryInfo / CreateDecoder: size checks as shadPS4 [S]; codec != AVC ->
//     CODEC_TYPE [I]; memory sizes below the query -> MEMORY_SIZE, NULL block -> MEMORY_POINTER [I].
//   SetDecodeInput: decodes the access unit synchronously (it is copied into the sample, so the
//     guest may reuse its buffer at once [S]) and queues an input completion. Every access unit
//     with a slice yields exactly one picture (player B relies on it [V]); one that cannot be
//     decoded (VideoToolbox error, missing or rejected SPS/PPS) yields an error picture at its
//     POC: OutputInfo is_error_frame 1, frame buffer untouched, trailer written [I]. Returns
//     INPUT_QUEUE_FULL when decode_pipeline_depth completions are not yet synced, and
//     OUTPUT_BUFFER_FULL when 16 decoded pictures wait for output buffers [I: both codes are
//     handled by U4 player A as "retry later" (0x169a5a1) and by player B as "not accepted,
//     resubmit" (0x16ced73) [V]]. U4 treats every other non-zero code as fatal (assert) [V],
//     and NEW_SEQUENCE (0x81510300) as "reset the decoder" [V]; neither is produced here.
//   TrySyncDecodeInput: pops one completion {decoded_au = the guest AU address, frame count,
//     result}; INPUT_QUEUE_EMPTY when none [S]. DECODE_PENDING is never needed (decoding is
//     synchronous); U4 accepts both codes [V].
//   SetDecodeOutput: one pending buffer at a time, else OUTPUT_BUFFER_FULL [S]; U4 player B
//     ignores that error and retries [V].
//   TrySyncDecodeOutput: a buffer too small for the picture -> FRAME_BUFFER_SIZE, keeping both
//     the picture and the buffer [I]; no pending buffer -> OUTPUT_BUFFER_EMPTY [S]; no picture yet ->
//     DECODE_PENDING [S] (U4 also accepts OUTPUT_PENDING 0x81510115 [V], never produced); after
//     FinalizeDecodeSequence with nothing left -> OK with is_valid 0, is_last_frame 1 [S];
//     otherwise writes one NV12 picture (see ps4_h264_vt.h), OutputInfo and the trailer. The
//     last picture after a finalize has is_last_frame 1 [S].
//   FinalizeDecodeSequence: flushes the pictures held for reordering [S]. A later
//     SetDecodeInput starts a new sequence [I].
//   ResetDecoder: drops queued pictures, completions and the pending output buffer [S].
//   DeleteDecoder: a call in progress on another thread keeps the decoder alive until it
//     returns; handles carry a generation, so a stale one never reaches a newer decoder [I].
//   Guest ranges (AU, frame buffer, trailer) are checked without wrap-around.
//   GetAvcPictureInfo: (OutputInfo.this_size | 8) == 0x38, picture_count 0 -> OK without
//     writing [S]; the second picture (interlaced) is never produced.
// Picture-info values: pts/dts/attached_data of the access unit the picture came from [S];
// SPS/VUI fields parsed from the SPS in force [I: shadPS4 leaves them zero]; NAL flags of that
// access unit [I]; crop offsets as shadPS4: left/top 0, right = pitch - width, bottom =
// align(height,16) - height, in pixels [S, unverified on hardware]; pic_struct 0 (frame).
// ---------------------------------------------------------------------------------------------
#include "recomp_runtime.h"
#include "ps4_h264_vt.h"
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VDECSW_OK 0
#define VDECSW_ERROR_STRUCT_SIZE ((int32_t)0x81510101)
#define VDECSW_ERROR_ARGUMENT_POINTER ((int32_t)0x81510102)
#define VDECSW_ERROR_DECODER_INSTANCE ((int32_t)0x81510103)
#define VDECSW_ERROR_MEMORY_SIZE ((int32_t)0x81510104)
#define VDECSW_ERROR_MEMORY_POINTER ((int32_t)0x81510105)
#define VDECSW_ERROR_FRAME_BUFFER_SIZE ((int32_t)0x81510106)
#define VDECSW_ERROR_FRAME_BUFFER_POINTER ((int32_t)0x81510107)
#define VDECSW_ERROR_ACCESS_UNIT_SIZE ((int32_t)0x8151010D)
#define VDECSW_ERROR_ACCESS_UNIT_POINTER ((int32_t)0x8151010E)
#define VDECSW_ERROR_INPUT_QUEUE_FULL ((int32_t)0x81510113)
#define VDECSW_ERROR_OUTPUT_BUFFER_FULL ((int32_t)0x81510114)
#define VDECSW_ERROR_INPUT_QUEUE_EMPTY ((int32_t)0x81510116)
#define VDECSW_ERROR_DECODE_PENDING ((int32_t)0x81510117)
#define VDECSW_ERROR_OUTPUT_BUFFER_EMPTY ((int32_t)0x81510118)
#define VDECSW_ERROR_CONFIG_INFO ((int32_t)0x81510200)
#define VDECSW_ERROR_COMPUTE_PIPE_ID ((int32_t)0x81510201)
#define VDECSW_ERROR_COMPUTE_QUEUE_ID ((int32_t)0x81510202)
#define VDECSW_ERROR_CODEC_TYPE ((int32_t)0x81510204)
#define VDECSW_ERROR_API_FAIL ((int32_t)0x81510100)

#define VDECSW_CODEC_AVC 1u

typedef struct VdecswDecoderConfigInfo {
    uint64_t this_size;
    uint32_t resource_type;
    uint32_t codec_type;
    uint32_t profile;
    uint32_t max_level;
    int32_t max_frame_width;
    int32_t max_frame_height;
    int32_t max_dpb_frame_count;
    uint32_t decode_pipeline_depth;
    uint64_t compute_queue;
    uint64_t cpu_affinity_mask;
    int32_t cpu_thread_priority;
    uint8_t optimize_progressive_video;
    uint8_t check_memory_type;
    uint8_t reserved0;
    int8_t extra_dpb_frame_count;
    uint64_t extra_config_info;
    uint8_t disable_sync_decode_output; // 0x50 layout only
    uint32_t max_pending_sync_count;    // 0x50 layout only
} VdecswDecoderConfigInfo;
_Static_assert(sizeof(VdecswDecoderConfigInfo) == 0x50, "DecoderConfigInfo");
_Static_assert(offsetof(VdecswDecoderConfigInfo, disable_sync_decode_output) == 0x48, "DecoderConfigInfo V1");

typedef struct VdecswDecoderMemoryInfo {
    uint64_t this_size;
    uint64_t cpu_memory_size;
    uint64_t cpu_memory;
    uint64_t gpu_memory_size;
    uint64_t gpu_memory;
    uint64_t cpu_gpu_memory_size;
    uint64_t cpu_gpu_memory;
    uint64_t max_frame_buffer_size;
    uint32_t frame_buffer_alignment;
    uint32_t reserved0;
} VdecswDecoderMemoryInfo;
_Static_assert(sizeof(VdecswDecoderMemoryInfo) == 0x48, "DecoderMemoryInfo");

typedef struct VdecswInputData {
    uint64_t this_size;
    uint64_t au_data;
    uint64_t au_size;
    uint64_t pts_data;
    uint64_t dts_data;
    uint64_t attached_data;
} VdecswInputData;
_Static_assert(sizeof(VdecswInputData) == 0x30, "InputData");

typedef struct VdecswInputResult {
    uint64_t this_size;
    uint64_t decoded_au;
    uint32_t output_frame_count;
    uint32_t reserved0;
} VdecswInputResult;
_Static_assert(sizeof(VdecswInputResult) == 0x18, "InputResult");

typedef struct VdecswOutputInfo {
    uint64_t this_size;
    uint8_t is_valid;
    uint8_t is_last_frame;
    uint8_t is_error_frame;
    uint8_t picture_count;
    uint32_t codec_type;
    uint32_t frame_width;
    uint32_t frame_pitch;
    uint32_t frame_height;
    uint8_t is_discarded_frame;
    uint64_t frame_buffer;
    uint64_t frame_buffer_size;
    uint32_t frame_format;         // 0x38 layout only
    uint32_t frame_pitch_in_bytes; // 0x38 layout only
} VdecswOutputInfo;
_Static_assert(sizeof(VdecswOutputInfo) == 0x38, "OutputInfo");
_Static_assert(offsetof(VdecswOutputInfo, frame_buffer) == 0x20, "OutputInfo.frame_buffer");
_Static_assert(offsetof(VdecswOutputInfo, frame_format) == 0x30, "OutputInfo old size");

typedef struct VdecswFrameBuffer {
    uint64_t this_size;
    uint64_t frame_buffer;
    uint64_t frame_buffer_size;
} VdecswFrameBuffer;
_Static_assert(sizeof(VdecswFrameBuffer) == 0x18, "FrameBuffer");

typedef struct VdecswComputeMemoryInfo {
    uint64_t this_size;
    uint64_t cpu_gpu_memory_size;
    uint64_t cpu_gpu_memory;
} VdecswComputeMemoryInfo;
_Static_assert(sizeof(VdecswComputeMemoryInfo) == 0x18, "ComputeMemoryInfo");

typedef struct VdecswComputeConfigInfo {
    uint64_t this_size;
    uint16_t compute_pipe_id;
    uint16_t compute_queue_id;
    uint8_t check_memory_type;
    uint8_t reserved0;
    uint16_t reserved1;
} VdecswComputeConfigInfo;
_Static_assert(sizeof(VdecswComputeConfigInfo) == 0x10, "ComputeConfigInfo");

typedef struct VdecswAvcPictureInfo {
    uint64_t this_size;
    uint8_t is_valid;
    uint64_t pts_data;
    uint64_t dts_data;
    uint64_t attached_data;
    uint8_t idr_pictureflag;
    uint8_t profile_idc;
    uint8_t level_idc;
    uint32_t pic_width_in_mbs_minus1;
    uint32_t pic_height_in_map_units_minus1;
    uint8_t frame_mbs_only_flag;
    uint8_t frame_cropping_flag;
    uint32_t frame_crop_left_offset;
    uint32_t frame_crop_right_offset;
    uint32_t frame_crop_top_offset;
    uint32_t frame_crop_bottom_offset;
    uint8_t aspect_ratio_info_present_flag;
    uint8_t aspect_ratio_idc;
    uint16_t sar_width;
    uint16_t sar_height;
    uint8_t video_signal_type_present_flag;
    uint8_t video_format;
    uint8_t video_full_range_flag;
    uint8_t colour_description_present_flag;
    uint8_t colour_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coefficients;
    uint8_t timing_info_present_flag;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    uint8_t fixed_frame_rate_flag;
    uint8_t bitstream_restriction_flag;
    uint8_t max_dec_frame_buffering;
    uint8_t pic_struct_present_flag;
    uint8_t pic_struct;
    uint8_t field_pic_flag;
    uint8_t bottom_field_flag;
    uint8_t sequence_parameter_set_present_flag;
    uint8_t picture_parameter_set_present_flag;
    uint8_t au_delimiter_present_flag;
    uint8_t end_of_sequence_present_flag;
    uint8_t end_of_stream_present_flag;
    uint8_t filler_data_present_flag;
    uint8_t picture_timing_sei_present_flag;
    uint8_t buffering_period_sei_present_flag;
    uint8_t constraint_set0_flag;
    uint8_t constraint_set1_flag;
    uint8_t constraint_set2_flag;
    uint8_t constraint_set3_flag;
    uint8_t constraint_set4_flag;
    uint8_t constraint_set5_flag;
} VdecswAvcPictureInfo;
_Static_assert(sizeof(VdecswAvcPictureInfo) == 0x78, "AvcPictureInfo");
_Static_assert(offsetof(VdecswAvcPictureInfo, pts_data) == 0x10, "AvcPictureInfo.pts");
_Static_assert(offsetof(VdecswAvcPictureInfo, frame_crop_left_offset) == 0x38, "AvcPictureInfo.crop");
_Static_assert(offsetof(VdecswAvcPictureInfo, num_units_in_tick) == 0x58, "AvcPictureInfo.timing");
_Static_assert(offsetof(VdecswAvcPictureInfo, constraint_set0_flag) == 0x6f, "AvcPictureInfo.constraints");

#define VDECSW_DECODER_MEM_SIZE 0x10000ULL
#define VDECSW_COMPUTE_MEM_SIZE 0x4000ULL
#define VDECSW_FRAME_BUFFER_ALIGN 0x100u
#define VDECSW_MAX_READY_PICTURES 16
#define VDECSW_MAX_INPUTS 32
#define VDECSW_MAX_DECODERS 16
#define VDECSW_HANDLE_TAG 0x5644535700000000ULL /* "VDSW" */

void ps4_gnm_notify_cpu_write(uint64_t addr, uint64_t size) __attribute__((weak));

typedef struct VdecswCompletion {
    uint64_t au;
    uint32_t frames;
    int32_t result;
} VdecswCompletion;

typedef struct Vdecsw {
    int refs;     // the table's reference + one per call in progress (g_table_lock)
    int slot;
    uint64_t handle; // tag | generation << 8 | slot + 1, so a stale handle never names a new decoder
    H264VtDecoder *core;
    uint32_t pipeline_depth;
    pthread_mutex_t in_lock; // serialises SetDecodeInput / Finalize / Reset
    pthread_mutex_t lock;    // everything below
    VdecswCompletion done[VDECSW_MAX_INPUTS];
    int done_head;
    int done_count;
    bool have_output;
    VdecswFrameBuffer output;
    bool finalized;
    uint64_t frames_out;
} Vdecsw;

static pthread_mutex_t g_table_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_generation;
static Vdecsw *g_decoders[VDECSW_MAX_DECODERS];

#define VDECSW_RETURN(code)                                                                    \
    do {                                                                                       \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(code);                                         \
        SHIM_RETURN();                                                                         \
    } while (0)

static inline void *gptr(GuestContext *ctx, uint64_t addr) { return addr ? ctx->mem_base + addr : NULL; }

static void vdecsw_free(Vdecsw *d) {
    h264vt_destroy(d->core);
    pthread_mutex_destroy(&d->in_lock);
    pthread_mutex_destroy(&d->lock);
    free(d);
}

// Takes a reference on the decoder behind a guest handle (NULL if none). A decoder deleted
// while another guest thread is inside a call is freed when that call releases it.
static Vdecsw *vdecsw_acquire(uint64_t handle) {
    if ((handle & 0xFFFFFFFF00000000ULL) != VDECSW_HANDLE_TAG) return NULL;
    uint64_t slot = (handle & 0xFF) - 1;
    if (slot >= VDECSW_MAX_DECODERS) return NULL;
    pthread_mutex_lock(&g_table_lock);
    Vdecsw *d = g_decoders[slot];
    if (d && d->handle != handle) d = NULL;
    if (d) d->refs++;
    pthread_mutex_unlock(&g_table_lock);
    return d;
}

static void vdecsw_release(Vdecsw *d) {
    pthread_mutex_lock(&g_table_lock);
    const bool last = --d->refs == 0;
    pthread_mutex_unlock(&g_table_lock);
    if (last) vdecsw_free(d);
}

// Runs fn on the decoder named by ctx->rdi with a reference held for the duration of the call.
#define VDECSW_WITH_DECODER(fn)                                                                \
    do {                                                                                       \
        Vdecsw *d_ = vdecsw_acquire(ctx->rdi);                                                 \
        if (!d_) VDECSW_RETURN(VDECSW_ERROR_DECODER_INSTANCE);                                 \
        int32_t rc_ = fn(ctx, d_);                                                             \
        vdecsw_release(d_);                                                                    \
        VDECSW_RETURN(rc_);                                                                    \
    } while (0)

// Guest range [addr, addr + size) inside guest memory, without wrapping around.
static inline bool grange_ok(GuestContext *ctx, uint64_t addr, uint64_t size) {
    return addr <= ctx->mem_size && size <= ctx->mem_size - addr;
}

static uint64_t max_frame_buffer_size(const VdecswDecoderConfigInfo *cfg) {
    int32_t w = cfg->max_frame_width, h = cfg->max_frame_height;
    if (w <= 0 || h <= 0) { // [S] defaults
        w = 1920;
        h = 1080;
        if (cfg->max_level >= 150) {
            w = 3840;
            h = 2160;
        }
    }
    uint64_t frame = h264vt_nv12_size((uint32_t)w, (uint32_t)h);
    return ((frame + 255) & ~255ULL) + 0x4000;
}

static bool config_size_ok(const VdecswDecoderConfigInfo *cfg) { return cfg->this_size == 0x50 || cfg->this_size == 0x48; }

// int sceVdecswQueryComputeMemoryInfo(SceVdecswComputeMemoryInfo *info)
void shim_sceVdecswQueryComputeMemoryInfo(GuestContext *ctx) {
    VdecswComputeMemoryInfo *info = (VdecswComputeMemoryInfo *)gptr(ctx, ctx->rdi);
    if (!info) VDECSW_RETURN(VDECSW_ERROR_ARGUMENT_POINTER);
    if (info->this_size != sizeof(*info)) VDECSW_RETURN(VDECSW_ERROR_STRUCT_SIZE);
    info->cpu_gpu_memory_size = VDECSW_COMPUTE_MEM_SIZE;
    info->cpu_gpu_memory = 0;
    VDECSW_RETURN(VDECSW_OK);
}

// int sceVdecswAllocateComputeQueue(const SceVdecswComputeConfigInfo *cfg,
//                                   const SceVdecswComputeMemoryInfo *mem, SceVdecswComputeQueue *queue)
// No GPU compute queue is used: decoding runs on VideoToolbox. The handle is the memory block.
void shim_sceVdecswAllocateComputeQueue(GuestContext *ctx) {
    const VdecswComputeConfigInfo *cfg = (const VdecswComputeConfigInfo *)gptr(ctx, ctx->rdi);
    const VdecswComputeMemoryInfo *mem = (const VdecswComputeMemoryInfo *)gptr(ctx, ctx->rsi);
    uint64_t *queue = (uint64_t *)gptr(ctx, ctx->rdx);
    if (!cfg || !mem || !queue) VDECSW_RETURN(VDECSW_ERROR_ARGUMENT_POINTER);
    if (cfg->this_size != sizeof(*cfg) || mem->this_size != sizeof(*mem)) VDECSW_RETURN(VDECSW_ERROR_STRUCT_SIZE);
    if (cfg->reserved0 != 0 || cfg->reserved1 != 0) VDECSW_RETURN(VDECSW_ERROR_CONFIG_INFO);
    if (cfg->compute_pipe_id > 4) VDECSW_RETURN(VDECSW_ERROR_COMPUTE_PIPE_ID);
    if (cfg->compute_queue_id > 7) VDECSW_RETURN(VDECSW_ERROR_COMPUTE_QUEUE_ID);
    if (!mem->cpu_gpu_memory) VDECSW_RETURN(VDECSW_ERROR_MEMORY_POINTER);
    if (mem->cpu_gpu_memory_size < VDECSW_COMPUTE_MEM_SIZE) VDECSW_RETURN(VDECSW_ERROR_MEMORY_SIZE);
    *queue = mem->cpu_gpu_memory;
    VDECSW_RETURN(VDECSW_OK);
}

// int sceVdecswReleaseComputeQueue(SceVdecswComputeQueue queue)
void shim_sceVdecswReleaseComputeQueue(GuestContext *ctx) { VDECSW_RETURN(VDECSW_OK); }

// int sceVdecswQueryDecoderMemoryInfo(const SceVdecswDecoderConfigInfo *cfg, SceVdecswDecoderMemoryInfo *info)
void shim_sceVdecswQueryDecoderMemoryInfo(GuestContext *ctx) {
    const VdecswDecoderConfigInfo *cfg = (const VdecswDecoderConfigInfo *)gptr(ctx, ctx->rdi);
    VdecswDecoderMemoryInfo *info = (VdecswDecoderMemoryInfo *)gptr(ctx, ctx->rsi);
    if (!cfg || !info) VDECSW_RETURN(VDECSW_ERROR_ARGUMENT_POINTER);
    if (!config_size_ok(cfg) || info->this_size != sizeof(*info)) VDECSW_RETURN(VDECSW_ERROR_STRUCT_SIZE);
    if (cfg->codec_type != VDECSW_CODEC_AVC) VDECSW_RETURN(VDECSW_ERROR_CODEC_TYPE);
    info->cpu_memory_size = VDECSW_DECODER_MEM_SIZE;
    info->cpu_memory = 0;
    info->gpu_memory_size = VDECSW_DECODER_MEM_SIZE;
    info->gpu_memory = 0;
    info->cpu_gpu_memory_size = VDECSW_DECODER_MEM_SIZE;
    info->cpu_gpu_memory = 0;
    info->max_frame_buffer_size = max_frame_buffer_size(cfg);
    info->frame_buffer_alignment = VDECSW_FRAME_BUFFER_ALIGN;
    info->reserved0 = 0;
    VDECSW_RETURN(VDECSW_OK);
}

// int sceVdecswCreateDecoder(const SceVdecswDecoderConfigInfo *cfg, const SceVdecswDecoderMemoryInfo *mem,
//                            SceVdecswDecoder *decoder)
void shim_sceVdecswCreateDecoder(GuestContext *ctx) {
    const VdecswDecoderConfigInfo *cfg = (const VdecswDecoderConfigInfo *)gptr(ctx, ctx->rdi);
    const VdecswDecoderMemoryInfo *mem = (const VdecswDecoderMemoryInfo *)gptr(ctx, ctx->rsi);
    uint64_t *out = (uint64_t *)gptr(ctx, ctx->rdx);
    if (!cfg || !mem || !out) VDECSW_RETURN(VDECSW_ERROR_ARGUMENT_POINTER);
    if (!config_size_ok(cfg) || mem->this_size != sizeof(*mem)) VDECSW_RETURN(VDECSW_ERROR_STRUCT_SIZE);
    if (cfg->codec_type != VDECSW_CODEC_AVC) {
        fprintf(stderr, "[libSceVdecsw] CreateDecoder: codec %u is not supported (H.264 only)\n", cfg->codec_type);
        VDECSW_RETURN(VDECSW_ERROR_CODEC_TYPE);
    }
    if (mem->cpu_memory_size < VDECSW_DECODER_MEM_SIZE || mem->gpu_memory_size < VDECSW_DECODER_MEM_SIZE ||
        mem->cpu_gpu_memory_size < VDECSW_DECODER_MEM_SIZE)
        VDECSW_RETURN(VDECSW_ERROR_MEMORY_SIZE);
    if (!mem->cpu_memory || !mem->gpu_memory || !mem->cpu_gpu_memory) VDECSW_RETURN(VDECSW_ERROR_MEMORY_POINTER);

    Vdecsw *d = (Vdecsw *)calloc(1, sizeof(*d));
    if (d) d->core = h264vt_create("libSceVdecsw");
    if (!d || !d->core) {
        free(d);
        VDECSW_RETURN(VDECSW_ERROR_API_FAIL);
    }
    d->pipeline_depth = cfg->decode_pipeline_depth ? cfg->decode_pipeline_depth : 1;
    if (d->pipeline_depth > VDECSW_MAX_INPUTS) d->pipeline_depth = VDECSW_MAX_INPUTS;
    pthread_mutex_init(&d->in_lock, NULL);
    pthread_mutex_init(&d->lock, NULL);
    int slot = -1;
    pthread_mutex_lock(&g_table_lock);
    for (int i = 0; i < VDECSW_MAX_DECODERS; i++)
        if (!g_decoders[i]) {
            g_decoders[i] = d;
            slot = i;
            d->slot = i;
            d->refs = 1;
            d->handle = VDECSW_HANDLE_TAG | (uint64_t)(++g_generation & 0xFFFFFF) << 8 | (uint64_t)(i + 1);
            break;
        }
    pthread_mutex_unlock(&g_table_lock);
    if (slot < 0) {
        h264vt_destroy(d->core);
        pthread_mutex_destroy(&d->in_lock);
        pthread_mutex_destroy(&d->lock);
        free(d);
        VDECSW_RETURN(VDECSW_ERROR_API_FAIL);
    }
    *out = d->handle;
    fprintf(stderr, "[libSceVdecsw] decoder %d: max %dx%d, profile %u level %u, pipeline depth %u\n", slot,
            cfg->max_frame_width, cfg->max_frame_height, cfg->profile, cfg->max_level, d->pipeline_depth);
    VDECSW_RETURN(VDECSW_OK);
}

// int sceVdecswDeleteDecoder(SceVdecswDecoder decoder)
void shim_sceVdecswDeleteDecoder(GuestContext *ctx) {
    Vdecsw *d = vdecsw_acquire(ctx->rdi);
    if (!d) VDECSW_RETURN(VDECSW_ERROR_DECODER_INSTANCE);
    pthread_mutex_lock(&g_table_lock);
    const bool mine = g_decoders[d->slot] == d; // a concurrent Delete may have won
    if (mine) {
        g_decoders[d->slot] = NULL;
        d->refs--; // the table's reference
    }
    pthread_mutex_unlock(&g_table_lock);
    vdecsw_release(d);
    VDECSW_RETURN(mine ? VDECSW_OK : VDECSW_ERROR_DECODER_INSTANCE);
}

// int sceVdecswResetDecoder(SceVdecswDecoder decoder)
static int32_t vdecsw_ResetDecoder(GuestContext *ctx, Vdecsw *d) {
    pthread_mutex_lock(&d->in_lock);
    h264vt_reset(d->core);
    pthread_mutex_lock(&d->lock);
    d->done_head = d->done_count = 0;
    d->have_output = false;
    d->finalized = false;
    pthread_mutex_unlock(&d->lock);
    pthread_mutex_unlock(&d->in_lock);
    return VDECSW_OK;
}
void shim_sceVdecswResetDecoder(GuestContext *ctx) { VDECSW_WITH_DECODER(vdecsw_ResetDecoder); }

// int sceVdecswSetDecodeInput(SceVdecswDecoder decoder, const SceVdecswInputData *in)
static int32_t vdecsw_SetDecodeInput(GuestContext *ctx, Vdecsw *d) {
    const VdecswInputData *in = (const VdecswInputData *)gptr(ctx, ctx->rsi);
    if (!in) return VDECSW_ERROR_ARGUMENT_POINTER;
    if (in->this_size != sizeof(*in)) return VDECSW_ERROR_STRUCT_SIZE;
    if (!in->au_data) return VDECSW_ERROR_ACCESS_UNIT_POINTER;
    if (in->au_size == 0 || !grange_ok(ctx, in->au_data, in->au_size)) return VDECSW_ERROR_ACCESS_UNIT_SIZE;

    pthread_mutex_lock(&d->in_lock);
    pthread_mutex_lock(&d->lock);
    const bool full = d->done_count >= (int)d->pipeline_depth;
    pthread_mutex_unlock(&d->lock);
    int32_t rc = VDECSW_OK;
    if (full) {
        rc = VDECSW_ERROR_INPUT_QUEUE_FULL;
    } else if (h264vt_queued(d->core) >= VDECSW_MAX_READY_PICTURES) {
        rc = VDECSW_ERROR_OUTPUT_BUFFER_FULL;
    } else {
        int ready = 0;
        int err = h264vt_decode(d->core, ctx->mem_base + in->au_data, (size_t)in->au_size, (int64_t)in->pts_data,
                                (int64_t)in->dts_data, in->attached_data, &ready);
        if (err != 0) {
            // U4 asserts on any unexpected code, so the access unit is consumed; the core has
            // queued an error picture for it (is_error_frame), keeping one picture per AU.
            static int warned;
            if (warned++ < 8) fprintf(stderr, "[libSceVdecsw] access unit at 0x%llx (%llu bytes) failed: %d\n",
                                      (unsigned long long)in->au_data, (unsigned long long)in->au_size, err);
        }
        pthread_mutex_lock(&d->lock);
        d->finalized = false;
        VdecswCompletion *c = &d->done[(d->done_head + d->done_count) % VDECSW_MAX_INPUTS];
        c->au = in->au_data;
        c->frames = (uint32_t)ready;
        c->result = VDECSW_OK;
        d->done_count++;
        pthread_mutex_unlock(&d->lock);
    }
    pthread_mutex_unlock(&d->in_lock);
    return rc;
}
void shim_sceVdecswSetDecodeInput(GuestContext *ctx) { VDECSW_WITH_DECODER(vdecsw_SetDecodeInput); }

// int sceVdecswTrySyncDecodeInput(SceVdecswDecoder decoder, SceVdecswInputResult *result)
static int32_t vdecsw_TrySyncDecodeInput(GuestContext *ctx, Vdecsw *d) {
    VdecswInputResult *res = (VdecswInputResult *)gptr(ctx, ctx->rsi);
    if (!res) return VDECSW_ERROR_ARGUMENT_POINTER;
    pthread_mutex_lock(&d->lock);
    if (d->done_count == 0) {
        pthread_mutex_unlock(&d->lock);
        return VDECSW_ERROR_INPUT_QUEUE_EMPTY;
    }
    VdecswCompletion c = d->done[d->done_head];
    d->done_head = (d->done_head + 1) % VDECSW_MAX_INPUTS;
    d->done_count--;
    pthread_mutex_unlock(&d->lock);
    res->decoded_au = c.au;
    res->output_frame_count = c.frames;
    res->reserved0 = 0;
    return c.result;
}
void shim_sceVdecswTrySyncDecodeInput(GuestContext *ctx) { VDECSW_WITH_DECODER(vdecsw_TrySyncDecodeInput); }

// int sceVdecswSetDecodeOutput(SceVdecswDecoder decoder, SceVdecswFrameBuffer *fb)
static int32_t vdecsw_SetDecodeOutput(GuestContext *ctx, Vdecsw *d) {
    const VdecswFrameBuffer *fb = (const VdecswFrameBuffer *)gptr(ctx, ctx->rsi);
    if (!fb) return VDECSW_ERROR_ARGUMENT_POINTER;
    if (fb->this_size != sizeof(*fb)) return VDECSW_ERROR_STRUCT_SIZE;
    if (!fb->frame_buffer) return VDECSW_ERROR_FRAME_BUFFER_POINTER;
    if (fb->frame_buffer_size == 0 || !grange_ok(ctx, fb->frame_buffer, fb->frame_buffer_size))
        return VDECSW_ERROR_FRAME_BUFFER_SIZE;
    pthread_mutex_lock(&d->lock);
    int32_t rc = VDECSW_OK;
    if (d->have_output) {
        rc = VDECSW_ERROR_OUTPUT_BUFFER_FULL;
    } else {
        d->output = *fb;
        d->have_output = true;
    }
    pthread_mutex_unlock(&d->lock);
    return rc;
}
void shim_sceVdecswSetDecodeOutput(GuestContext *ctx) { VDECSW_WITH_DECODER(vdecsw_SetDecodeOutput); }

static void fill_picture_info(VdecswAvcPictureInfo *p, const H264VtFrame *f) {
    const uint32_t pitch = h264vt_pitch(f->width);
    const uint32_t ah = h264vt_aligned_height(f->height);
    const H264SpsInfo *s = &f->sps;
    memset(p, 0, sizeof(*p));
    p->this_size = sizeof(*p);
    p->is_valid = 1;
    p->pts_data = (uint64_t)f->pts;
    p->dts_data = (uint64_t)f->dts;
    p->attached_data = f->attached;
    p->idr_pictureflag = f->au.idr;
    p->profile_idc = s->profile_idc;
    p->level_idc = s->level_idc;
    p->pic_width_in_mbs_minus1 = s->pic_width_in_mbs_minus1;
    p->pic_height_in_map_units_minus1 = s->pic_height_in_map_units_minus1;
    p->frame_mbs_only_flag = s->frame_mbs_only_flag;
    p->frame_crop_left_offset = 0;
    p->frame_crop_top_offset = 0;
    p->frame_crop_right_offset = pitch - f->width;
    p->frame_crop_bottom_offset = ah - f->height;
    p->frame_cropping_flag = (p->frame_crop_right_offset || p->frame_crop_bottom_offset) ? 1 : 0;
    p->aspect_ratio_info_present_flag = s->aspect_ratio_info_present_flag;
    p->aspect_ratio_idc = s->aspect_ratio_idc;
    p->sar_width = s->sar_width;
    p->sar_height = s->sar_height;
    p->video_signal_type_present_flag = s->video_signal_type_present_flag;
    p->video_format = s->video_format;
    p->video_full_range_flag = s->video_full_range_flag;
    p->colour_description_present_flag = s->colour_description_present_flag;
    p->colour_primaries = s->colour_primaries;
    p->transfer_characteristics = s->transfer_characteristics;
    p->matrix_coefficients = s->matrix_coefficients;
    p->timing_info_present_flag = s->timing_info_present_flag;
    p->num_units_in_tick = s->num_units_in_tick;
    p->time_scale = s->time_scale;
    p->fixed_frame_rate_flag = s->fixed_frame_rate_flag;
    p->bitstream_restriction_flag = s->bitstream_restriction_flag;
    p->max_dec_frame_buffering = s->max_dec_frame_buffering;
    p->pic_struct_present_flag = s->pic_struct_present_flag;
    p->pic_struct = 0; // a progressive frame
    p->field_pic_flag = 0;
    p->bottom_field_flag = 0;
    p->sequence_parameter_set_present_flag = f->au.sps;
    p->picture_parameter_set_present_flag = f->au.pps;
    p->au_delimiter_present_flag = f->au.aud;
    p->end_of_sequence_present_flag = f->au.end_of_sequence;
    p->end_of_stream_present_flag = f->au.end_of_stream;
    p->filler_data_present_flag = f->au.filler;
    p->picture_timing_sei_present_flag = f->au.sei_pic_timing;
    p->buffering_period_sei_present_flag = f->au.sei_buffering_period;
    p->constraint_set0_flag = (s->constraint_set_flags >> 0) & 1;
    p->constraint_set1_flag = (s->constraint_set_flags >> 1) & 1;
    p->constraint_set2_flag = (s->constraint_set_flags >> 2) & 1;
    p->constraint_set3_flag = (s->constraint_set_flags >> 3) & 1;
    p->constraint_set4_flag = (s->constraint_set_flags >> 4) & 1;
    p->constraint_set5_flag = (s->constraint_set_flags >> 5) & 1;
}

// int sceVdecswTrySyncDecodeOutput(SceVdecswDecoder decoder, SceVdecswOutputInfo *info)
static int32_t vdecsw_TrySyncDecodeOutput(GuestContext *ctx, Vdecsw *d) {
    VdecswOutputInfo *info = (VdecswOutputInfo *)gptr(ctx, ctx->rsi);
    if (!info) return VDECSW_ERROR_ARGUMENT_POINTER;
    if (info->this_size != 0x30 && info->this_size != 0x38) return VDECSW_ERROR_STRUCT_SIZE;
    info->is_valid = 0;
    info->is_last_frame = 0;
    info->is_error_frame = 0;
    info->is_discarded_frame = 0;
    info->picture_count = 0;

    pthread_mutex_lock(&d->lock);
    if (!d->have_output) {
        pthread_mutex_unlock(&d->lock);
        return VDECSW_ERROR_OUTPUT_BUFFER_EMPTY;
    }
    // Size check and pop in one step: ResetDecoder can drop the queue at any time (it holds
    // only in_lock), so a picture must never be looked at outside the queue lock before it is ours.
    H264VtFrame *f = NULL;
    uint64_t need = 0;
    const int pr = h264vt_pop_fitting(d->core, d->output.frame_buffer_size, sizeof(VdecswAvcPictureInfo), &f, &need);
    if (pr == H264VT_POP_EMPTY) {
        int32_t rc = VDECSW_ERROR_DECODE_PENDING;
        if (d->finalized) {
            info->is_last_frame = 1;
            d->have_output = false;
            rc = VDECSW_OK;
        }
        pthread_mutex_unlock(&d->lock);
        return rc;
    }
    if (pr == H264VT_POP_TOO_SMALL) {
        // The picture and the output buffer both stay; the guest can retry with a larger
        // buffer only after a reset, as on hardware an oversized stream is an error.
        static int warned;
        if (warned++ < 8)
            fprintf(stderr, "[libSceVdecsw] picture does not fit the frame buffer (0x%llx < 0x%llx)\n",
                    (unsigned long long)d->output.frame_buffer_size, (unsigned long long)need);
        pthread_mutex_unlock(&d->lock);
        return VDECSW_ERROR_FRAME_BUFFER_SIZE;
    }
    const uint64_t frame_size = h264vt_nv12_size(f->width, f->height);
    const VdecswFrameBuffer fb = d->output;
    d->have_output = false;
    const bool last = d->finalized && h264vt_queued(d->core) == 0;
    pthread_mutex_unlock(&d->lock);

    uint8_t *dst = ctx->mem_base + fb.frame_buffer;
    // An error picture leaves the frame buffer as it is; the trailer is still written.
    if (!f->error) h264vt_copy_nv12(f, dst);
    VdecswAvcPictureInfo pic;
    fill_picture_info(&pic, f);
    memcpy(dst + frame_size, &pic, sizeof(pic));
    if (ps4_gnm_notify_cpu_write) {
        if (f->error) ps4_gnm_notify_cpu_write(fb.frame_buffer + frame_size, sizeof(pic));
        else ps4_gnm_notify_cpu_write(fb.frame_buffer, frame_size + sizeof(pic));
    }

    info->is_valid = 1;
    info->is_last_frame = last ? 1 : 0;
    info->is_error_frame = f->error ? 1 : 0;
    info->picture_count = 1;
    info->codec_type = VDECSW_CODEC_AVC;
    info->frame_width = (f->width + 15u) & ~15u;
    info->frame_pitch = h264vt_pitch(f->width);
    info->frame_height = h264vt_aligned_height(f->height);
    info->frame_buffer = fb.frame_buffer;
    info->frame_buffer_size = frame_size;
    if (info->this_size == sizeof(VdecswOutputInfo)) {
        info->frame_format = 0;
        info->frame_pitch_in_bytes = info->frame_pitch;
    }
    if (!f->error && d->frames_out++ == 0)
        fprintf(stderr, "[libSceVdecsw] first picture decoded (%ux%u)\n", f->width, f->height);
    h264vt_frame_free(f);
    return VDECSW_OK;
}
void shim_sceVdecswTrySyncDecodeOutput(GuestContext *ctx) { VDECSW_WITH_DECODER(vdecsw_TrySyncDecodeOutput); }

// int sceVdecswFinalizeDecodeSequence(SceVdecswDecoder decoder)
static int32_t vdecsw_FinalizeDecodeSequence(GuestContext *ctx, Vdecsw *d) {
    pthread_mutex_lock(&d->in_lock);
    h264vt_finish(d->core);
    pthread_mutex_lock(&d->lock);
    d->finalized = true;
    pthread_mutex_unlock(&d->lock);
    pthread_mutex_unlock(&d->in_lock);
    return VDECSW_OK;
}
void shim_sceVdecswFinalizeDecodeSequence(GuestContext *ctx) { VDECSW_WITH_DECODER(vdecsw_FinalizeDecodeSequence); }

// int sceVdecswGetAvcPictureInfo(const SceVdecswOutputInfo *info, SceVdecswAvcPictureInfo *first,
//                                SceVdecswAvcPictureInfo *second)
void shim_sceVdecswGetAvcPictureInfo(GuestContext *ctx) {
    const VdecswOutputInfo *info = (const VdecswOutputInfo *)gptr(ctx, ctx->rdi);
    uint8_t *first = (uint8_t *)gptr(ctx, ctx->rsi);
    if (!info) VDECSW_RETURN(VDECSW_ERROR_ARGUMENT_POINTER);
    if ((info->this_size | 8) != sizeof(VdecswOutputInfo)) VDECSW_RETURN(VDECSW_ERROR_STRUCT_SIZE);
    if (info->picture_count == 0) VDECSW_RETURN(VDECSW_OK);
    if (!first) VDECSW_RETURN(VDECSW_ERROR_ARGUMENT_POINTER);
    uint64_t size;
    memcpy(&size, first, sizeof(size));
    if (size <= 8 || size > sizeof(VdecswAvcPictureInfo)) VDECSW_RETURN(VDECSW_ERROR_STRUCT_SIZE);
    if (!info->frame_buffer || !grange_ok(ctx, info->frame_buffer, info->frame_buffer_size) ||
        !grange_ok(ctx, info->frame_buffer + info->frame_buffer_size, sizeof(VdecswAvcPictureInfo)))
        VDECSW_RETURN(VDECSW_ERROR_FRAME_BUFFER_POINTER);
    // The caller's this_size stays; the rest is the start of the trailer's layout.
    memcpy(first + 8, ctx->mem_base + info->frame_buffer + info->frame_buffer_size + 8, size - 8);
    VDECSW_RETURN(VDECSW_OK);
}
