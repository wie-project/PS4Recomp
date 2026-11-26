// SPDX-License-Identifier: GPL-2.0-or-later
//
// libSceVdecCore (the low-level decoder library under libSceVideodec2 / libSceVdecsw, also
// linked directly by some titles) on the shared VideoToolbox decode core
// (libSceVideodec/ps4_h264_vt.c), H.264/AVC and HEVC.
//
// Struct layouts, error codes, sizes and call behaviour follow shadPS4
// (src/core/libraries/videodec/vdeccore.{h,cpp}, video_utils.cpp, videodec_error.h,
// Copyright 2026 shadPS4 Emulator Project, GPL-2.0-or-later). [S] = as shadPS4, [I] = our
// choice where shadPS4 relies on something that does not exist here.
//
// Struct layouts (offsets hex):
//   ConfigInfo 0x58: 00 decoder_type (0..8) 04 codec_type (0 AVC, 4 HEVC) 08 profile
//     0C max_level 10 max_dpb_frame_count 14 max_frame_width 18 max_frame_height
//     1C horizontal_align ... (the rest is not used).
//   MemoryInfo 0x30, ComputeResourceInfo 0x18, FrameBufferInfo 0x18,
//   FrameBuffer 0x10 {frame_buffer, frame_buffer_size}, MemoryBlock 0x18 {addr, size, align, type},
//   InputData 0x30 {au_data, au_size, pts, dts, user_data, flags, stream_id},
//   Picture 0x40, OutputInfo 0xD0, HwPictureInfo 0x400 (below).
//
// Calls:
//   QueryInstanceSize / QueryFrameBufferInfo / QueryComputeResourceInfo: shadPS4's sizes [S].
//   CreateDecoder: codec_type 4 decodes HEVC, 0..3 H.264 [S]; above 4 -> INVALID_CODEC_TYPE,
//     decoder_type above 8 -> INVALID_ARGUMENT [S]. The decoder handle is opaque and tagged with
//     a generation, so a stale or foreign handle is INVALID_ARGUMENT instead of a crash [I].
//   SetDecodeInput: writes status {1, 1} before the access-unit checks [S]; the access unit is
//     decoded synchronously (shadPS4 queues it to a worker thread), so SyncDecode never has to
//     wait and TrySyncDecode never returns DECODE_RUNNING. An input pts/dts of -1 means none
//     [S]; a picture without one reports the last input's value [S].
//   SyncDecode / SyncDecodeWptr / TrySyncDecode / TrySyncDecodeWptr: the number of pictures
//     ready for GetDecodeOutput [S].
//   GetDecodeOutput: pops one picture in display order and copies it as linear NV12 (pitch =
//     align(width, 64), height = align(height, 16), rows below the picture repeat the last row)
//     [S] into frame_buf->frame_buffer, or else into the next slot carved out of the blocks given
//     to MapMemoryBlock (round robin, slot size = QueryFrameBufferInfo's frame_buffer_size) [S].
//     With neither, shadPS4 uses a host buffer the guest cannot address here, so the call is
//     INVALID_ARGUMENT and the picture stays queued [I]; likewise when the picture does not fit
//     the buffer (shadPS4 overruns it) [I]. No picture -> NO_PICTURE with the decoder's maximum
//     size in OutputInfo [S]. The 0x400-byte HwPictureInfo is written at the end of the buffer
//     [S] when it does not overlap the picture [I: shadPS4 writes it whenever the buffer holds
//     0x400 bytes]. Undecodable access units give no picture, as with FFmpeg [S].
//     Colour fields: the stream's VUI when present, else shadPS4's constants (video_format 5,
//     colour_primaries 1, transfer_characteristics 1) [S/I]; the sample aspect ratio is the
//     VUI's (Table E-1), 1:1 without one [S].
//   FlushDecodeOutput: releases the pictures held for reordering, out_count = ready [S]; the
//     next access unit starts a new sequence (FFmpeg's flush) [S].
//   ResetDecoder: drops queued and held pictures, restarts slot rotation [S].
//   AnalyzeStream, InitializeComputeResource, FinalizeComputeResource, SetDecodeOutputSw,
//     SyncDecodeOutputSw, TrySyncDecodeOutputSw: return OK, as shadPS4 [S].
#include "recomp_runtime.h"
#include "ps4_h264_vt.h"
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VDECCORE_OK 0
#define VDECCORE_ERROR_INVALID_ARGUMENT ((int32_t)0x80C00001)
#define VDECCORE_ERROR_INVALID_CODEC_TYPE ((int32_t)0x80C00003)
#define VDECCORE_ERROR_NO_PICTURE ((int32_t)0x80C00010)
#define VDECCORE_ERROR_FATAL ((int32_t)0x80C00012)

#define VDECCORE_CODEC_HEVC 4u
#define VDECCORE_MAX_CODEC 4u
#define VDECCORE_MAX_DECODER_TYPE 8u
#define VDECCORE_INSTANCE_SIZE 0x40000ULL
#define VDECCORE_COMPUTE_SIZE 0x10000ULL
#define VDECCORE_MAX_DECODERS 16
#define VDECCORE_HANDLE_TAG 0x5644434F00000000ULL /* "VDCO" */

typedef struct VdecCoreConfigInfo {
    uint32_t decoder_type;
    uint32_t codec_type;
    int32_t profile;
    int32_t max_level;
    int32_t max_dpb_frame_count;
    int32_t max_frame_width;
    int32_t max_frame_height;
    int32_t horizontal_align;
    int32_t surface_format;
    int32_t field_0x24;
    uint8_t hevc_tier_type;
    uint8_t frame_height_alignment;
    uint8_t max_bit_depth_luma;
    uint8_t max_bit_depth_chroma;
    int32_t hevc_max_temporal_id_to_decode;
    uint8_t gpu_load_level;
    int32_t field_0x34;
    int32_t decode_pipeline_depth;
    int32_t cpu_thread_priority;
    int32_t no_optimize_progressive_video;
    int32_t extra_dpb_frame_count;
    int64_t cpu_affinity_mask;
    int32_t max_pending_sync_count;
} VdecCoreConfigInfo;
_Static_assert(sizeof(VdecCoreConfigInfo) == 0x58, "ConfigInfo");
_Static_assert(offsetof(VdecCoreConfigInfo, horizontal_align) == 0x1C, "ConfigInfo.horizontal_align");

typedef struct VdecCoreComputeResourceInfo {
    uint64_t memory_type;
    uint64_t size;
    uint32_t alignment;
    uint32_t reserved;
} VdecCoreComputeResourceInfo;
_Static_assert(sizeof(VdecCoreComputeResourceInfo) == 0x18, "ComputeResourceInfo");

typedef struct VdecCoreFrameBufferInfo {
    uint32_t frame_buffer_size;
    uint32_t frame_buffer_alignment;
    uint32_t min_frame_buffer_count;
    uint32_t horizontal_alignment;
    uint32_t frame_width;
    uint32_t frame_height;
} VdecCoreFrameBufferInfo;
_Static_assert(sizeof(VdecCoreFrameBufferInfo) == 0x18, "FrameBufferInfo");

typedef struct VdecCoreFrameBuffer {
    uint64_t frame_buffer;
    uint64_t frame_buffer_size;
} VdecCoreFrameBuffer;
_Static_assert(sizeof(VdecCoreFrameBuffer) == 0x10, "FrameBuffer");

typedef struct VdecCoreInputData {
    uint64_t au_data;
    uint64_t au_size;
    int64_t pts;
    int64_t dts;
    uint64_t user_data;
    uint32_t flags;
    uint32_t stream_id;
} VdecCoreInputData;
_Static_assert(sizeof(VdecCoreInputData) == 0x30, "InputData");

typedef struct VdecCorePicture {
    uint32_t flags;
    uint32_t reserved0;
    uint64_t pts;
    uint64_t dts;
    uint64_t user_data;
    uint64_t picture_info;
    uint64_t frame_buffer;
    uint32_t frame_buffer_size;
    uint32_t frame_pitch;
    uint64_t reserved1;
} VdecCorePicture;
_Static_assert(sizeof(VdecCorePicture) == 0x40, "Picture");

typedef struct VdecCoreHwPictureInfo {
    uint32_t codec_type; // 0 AVC, 1 HEVC
    uint32_t reserved0;
    uint64_t user_data;
    uint64_t pts;
    uint64_t dts;
    uint32_t valid;
    uint32_t reserved1;
    uint32_t width;
    uint32_t height;
    uint32_t crop_left;
    uint32_t crop_right;
    uint32_t crop_top;
    uint32_t crop_bottom;
    uint32_t sar_width;
    uint32_t sar_height;
    uint32_t reserved2;
    uint32_t flags; // bit 0 valid, bit 1 cropped
    uint8_t reserved3[0x400 - 0x50];
} VdecCoreHwPictureInfo;
_Static_assert(sizeof(VdecCoreHwPictureInfo) == 0x400, "HwPictureInfo");

typedef struct VdecCoreOutputInfo {
    uint32_t frame_width;
    uint32_t frame_pitch;
    uint32_t frame_height;
    uint32_t picture_count;
    uint64_t frame_buffer;
    uint32_t frame_format;
    uint32_t reserved0;
    VdecCorePicture pictures[2];
    uint32_t aspect_ratio;
    uint32_t sar_width;
    uint32_t video_format;
    uint32_t colour_primaries;
    uint32_t transfer_characteristics;
    uint32_t frame_pitch_in_bytes;
    uint64_t luma_buffer;
    uint64_t chroma_buffer;
    uint32_t video_full_range_flag;
    uint32_t colour_description_present_flag;
} VdecCoreOutputInfo;
_Static_assert(sizeof(VdecCoreOutputInfo) == 0xD0, "OutputInfo");
_Static_assert(offsetof(VdecCoreOutputInfo, aspect_ratio) == 0xA0, "OutputInfo.aspect_ratio");

typedef struct VdecCoreMemoryBlock {
    uint64_t addr;
    uint64_t size;
    uint32_t align;
    uint32_t type;
} VdecCoreMemoryBlock;
_Static_assert(sizeof(VdecCoreMemoryBlock) == 0x18, "MemoryBlock");

void ps4_gnm_notify_cpu_write(uint64_t addr, uint64_t size) __attribute__((weak));

typedef struct VdecCore {
    int refs; // the table's reference + one per call in progress (g_table_lock)
    int slot;
    uint64_t handle; // tag | generation << 8 | slot + 1
    H264VtDecoder *core;
    uint32_t codec_type; // the config's codec_type (0..4)
    uint32_t max_width;
    uint32_t max_height;
    uint32_t frame_buffer_size;
    pthread_mutex_t in_lock; // serialises decode / flush / reset
    pthread_mutex_t lock;    // everything below
    uint64_t *slots;         // frame-buffer slots from MapMemoryBlock (guest addresses)
    uint32_t slot_count;
    uint32_t slot_cap;
    uint32_t slot_index;
    int64_t last_pts;
    int64_t last_dts;
    uint64_t frames_out;
} VdecCore;

static pthread_mutex_t g_table_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_generation;
static VdecCore *g_decoders[VDECCORE_MAX_DECODERS];

#define VDECCORE_RETURN(code)                                                                  \
    do {                                                                                       \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(code);                                         \
        SHIM_RETURN();                                                                         \
    } while (0)

static inline void *gptr(GuestContext *ctx, uint64_t addr) { return addr ? ctx->mem_base + addr : NULL; }

// Guest range [addr, addr + size) inside guest memory, without wrapping around.
static inline bool grange_ok(GuestContext *ctx, uint64_t addr, uint64_t size) {
    return addr <= ctx->mem_size && size <= ctx->mem_size - addr;
}

static void vdeccore_free(VdecCore *d) {
    h264vt_destroy(d->core);
    pthread_mutex_destroy(&d->in_lock);
    pthread_mutex_destroy(&d->lock);
    free(d->slots);
    free(d);
}

// Takes a reference on the decoder behind a guest handle (NULL if none).
static VdecCore *vdeccore_acquire(uint64_t handle) {
    if ((handle & 0xFFFFFFFF00000000ULL) != VDECCORE_HANDLE_TAG) return NULL;
    uint64_t slot = (handle & 0xFF) - 1;
    if (slot >= VDECCORE_MAX_DECODERS) return NULL;
    pthread_mutex_lock(&g_table_lock);
    VdecCore *d = g_decoders[slot];
    if (d && d->handle != handle) d = NULL;
    if (d) d->refs++;
    pthread_mutex_unlock(&g_table_lock);
    return d;
}

static void vdeccore_release(VdecCore *d) {
    pthread_mutex_lock(&g_table_lock);
    const bool last = --d->refs == 0;
    pthread_mutex_unlock(&g_table_lock);
    if (last) vdeccore_free(d);
}

// Runs fn on the decoder named by ctx->rdi with a reference held for the duration of the call.
#define VDECCORE_WITH_DECODER(fn)                                                              \
    do {                                                                                       \
        VdecCore *d_ = vdeccore_acquire(ctx->rdi);                                             \
        if (!d_) VDECCORE_RETURN(VDECCORE_ERROR_INVALID_ARGUMENT);                             \
        int32_t rc_ = fn(ctx, d_);                                                             \
        vdeccore_release(d_);                                                                  \
        VDECCORE_RETURN(rc_);                                                                  \
    } while (0)

static inline uint32_t align_up(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }

static uint32_t cfg_width(const VdecCoreConfigInfo *c) { return c->max_frame_width ? (uint32_t)c->max_frame_width : 1920; }
static uint32_t cfg_height(const VdecCoreConfigInfo *c) { return c->max_frame_height ? (uint32_t)c->max_frame_height : 1080; }
static uint32_t cfg_dpb(const VdecCoreConfigInfo *c) {
    return c->max_dpb_frame_count > 0 && c->max_dpb_frame_count <= 16 ? (uint32_t)c->max_dpb_frame_count : 16;
}
static uint32_t cfg_halign(const VdecCoreConfigInfo *c) { return c->horizontal_align > 0 ? (uint32_t)c->horizontal_align : 256; }

// align(align(w, horizontal_align) * align(h, 16) * 3/2 + 0x1000, 0x10000) [S].
static uint32_t cfg_frame_buffer_size(const VdecCoreConfigInfo *c) {
    const uint32_t pitch = align_up(cfg_width(c), cfg_halign(c));
    const uint32_t size = pitch * align_up(cfg_height(c), 16) * 3 / 2;
    return align_up(size + 0x1000, 0x10000);
}

// Sample aspect ratio of aspect_ratio_idc (ITU-T H.264 / H.265 Table E-1); 1:1 when unknown.
static void sample_aspect(uint8_t present, uint8_t idc, uint16_t ext_w, uint16_t ext_h, uint32_t *w, uint32_t *h) {
    static const uint8_t table[17][2] = {{0, 1},   {1, 1},   {12, 11}, {10, 11}, {16, 11}, {40, 33},
                                         {24, 11}, {20, 11}, {32, 11}, {80, 33}, {18, 11}, {15, 11},
                                         {64, 33}, {160, 99}, {4, 3},  {3, 2},   {2, 1}};
    uint32_t n = 0, dd = 0;
    if (present) {
        if (idc == 255) {
            n = ext_w;
            dd = ext_h;
        } else if (idc < 17) {
            n = table[idc][0];
            dd = table[idc][1];
        }
    }
    *w = n > 0 ? n : 1;
    *h = dd > 0 ? dd : 1;
}

// Stream colour and aspect information of a picture.
typedef struct VdecCoreVui {
    uint8_t aspect_present;
    uint8_t aspect_idc;
    uint32_t sar_w;
    uint32_t sar_h;
    uint8_t signal_present;
    uint8_t video_format;
    uint8_t full_range;
    uint8_t colour_present;
    uint8_t colour_primaries;
    uint8_t transfer;
} VdecCoreVui;

static void picture_vui(const H264VtFrame *f, VdecCoreVui *v) {
    if (f->codec == H264VT_CODEC_HEVC) {
        const HevcSpsInfo *s = &f->hevc;
        v->aspect_present = s->aspect_ratio_info_present_flag;
        v->aspect_idc = s->aspect_ratio_idc;
        sample_aspect(s->aspect_ratio_info_present_flag, s->aspect_ratio_idc, s->sar_width, s->sar_height, &v->sar_w,
                      &v->sar_h);
        v->signal_present = s->video_signal_type_present_flag;
        v->video_format = s->video_format;
        v->full_range = s->video_full_range_flag;
        v->colour_present = s->colour_description_present_flag;
        v->colour_primaries = s->colour_primaries;
        v->transfer = s->transfer_characteristics;
    } else {
        const H264SpsInfo *s = &f->sps;
        v->aspect_present = s->aspect_ratio_info_present_flag;
        v->aspect_idc = s->aspect_ratio_idc;
        sample_aspect(s->aspect_ratio_info_present_flag, s->aspect_ratio_idc, s->sar_width, s->sar_height, &v->sar_w,
                      &v->sar_h);
        v->signal_present = s->video_signal_type_present_flag;
        v->video_format = s->video_format;
        v->full_range = s->video_full_range_flag;
        v->colour_present = s->colour_description_present_flag;
        v->colour_primaries = s->colour_primaries;
        v->transfer = s->transfer_characteristics;
    }
}

// int sceVdecCoreQueryInstanceSize(const SceVdecCoreConfigInfo *config, u64 *instance_size,
//                                  u64 *compute_size, u64 *reserved_size)
void shim_sceVdecCoreQueryInstanceSize(GuestContext *ctx) {
    const VdecCoreConfigInfo *cfg = (const VdecCoreConfigInfo *)gptr(ctx, ctx->rdi);
    uint64_t *instance = (uint64_t *)gptr(ctx, ctx->rsi);
    uint64_t *compute = (uint64_t *)gptr(ctx, ctx->rdx);
    uint64_t *reserved = (uint64_t *)gptr(ctx, ctx->rcx);
    if (!cfg) VDECCORE_RETURN(VDECCORE_ERROR_INVALID_ARGUMENT);
    if (cfg->codec_type > VDECCORE_MAX_CODEC) VDECCORE_RETURN(VDECCORE_ERROR_INVALID_CODEC_TYPE);
    if (instance) *instance = VDECCORE_INSTANCE_SIZE;
    if (compute) *compute = VDECCORE_COMPUTE_SIZE;
    if (reserved) *reserved = 0;
    VDECCORE_RETURN(VDECCORE_OK);
}

// int sceVdecCoreQueryFrameBufferInfo(const SceVdecCoreConfigInfo *config, SceVdecCoreFrameBufferInfo *info)
void shim_sceVdecCoreQueryFrameBufferInfo(GuestContext *ctx) {
    const VdecCoreConfigInfo *cfg = (const VdecCoreConfigInfo *)gptr(ctx, ctx->rdi);
    VdecCoreFrameBufferInfo *info = (VdecCoreFrameBufferInfo *)gptr(ctx, ctx->rsi);
    if (!cfg || !info) VDECCORE_RETURN(VDECCORE_ERROR_INVALID_ARGUMENT);
    if (cfg->codec_type > VDECCORE_MAX_CODEC) VDECCORE_RETURN(VDECCORE_ERROR_INVALID_CODEC_TYPE);
    if (cfg->max_dpb_frame_count < -1 || cfg->max_dpb_frame_count > 16) VDECCORE_RETURN(VDECCORE_ERROR_INVALID_ARGUMENT);
    info->frame_buffer_size = cfg_frame_buffer_size(cfg);
    info->frame_buffer_alignment = 256;
    info->min_frame_buffer_count = cfg_dpb(cfg) + 1;
    info->horizontal_alignment = cfg_halign(cfg);
    info->frame_width = cfg_width(cfg);
    info->frame_height = cfg_height(cfg);
    VDECCORE_RETURN(VDECCORE_OK);
}

// int sceVdecCoreCreateDecoder(const SceVdecCoreConfigInfo *config, const SceVdecCoreMemoryInfo *mem,
//                              SceVdecCoreComputeQueue queue, SceVdecCoreDecoder *decoder)
void shim_sceVdecCoreCreateDecoder(GuestContext *ctx) {
    const VdecCoreConfigInfo *cfg = (const VdecCoreConfigInfo *)gptr(ctx, ctx->rdi);
    uint64_t *out = (uint64_t *)gptr(ctx, ctx->rcx);
    if (!cfg || !out) VDECCORE_RETURN(VDECCORE_ERROR_INVALID_ARGUMENT);
    if (cfg->decoder_type > VDECCORE_MAX_DECODER_TYPE) VDECCORE_RETURN(VDECCORE_ERROR_INVALID_ARGUMENT);
    if (cfg->codec_type > VDECCORE_MAX_CODEC) {
        fprintf(stderr, "[libSceVdecCore] CreateDecoder: codec_type %u is not supported (AVC 0, HEVC 4)\n",
                cfg->codec_type);
        VDECCORE_RETURN(VDECCORE_ERROR_INVALID_CODEC_TYPE);
    }
    VdecCore *d = (VdecCore *)calloc(1, sizeof(*d));
    if (d)
        d->core = h264vt_create_codec("libSceVdecCore",
                                      cfg->codec_type == VDECCORE_CODEC_HEVC ? H264VT_CODEC_HEVC : H264VT_CODEC_AVC);
    if (!d || !d->core) {
        free(d);
        VDECCORE_RETURN(VDECCORE_ERROR_FATAL);
    }
    d->codec_type = cfg->codec_type;
    d->max_width = cfg_width(cfg);
    d->max_height = cfg_height(cfg);
    d->frame_buffer_size = cfg_frame_buffer_size(cfg);
    d->last_pts = H264VT_NO_TIME;
    d->last_dts = H264VT_NO_TIME;
    pthread_mutex_init(&d->in_lock, NULL);
    pthread_mutex_init(&d->lock, NULL);
    int slot = -1;
    pthread_mutex_lock(&g_table_lock);
    for (int i = 0; i < VDECCORE_MAX_DECODERS; i++)
        if (!g_decoders[i]) {
            g_decoders[i] = d;
            slot = i;
            d->slot = i;
            d->refs = 1;
            d->handle = VDECCORE_HANDLE_TAG | (uint64_t)(++g_generation & 0xFFFFFF) << 8 | (uint64_t)(i + 1);
            break;
        }
    pthread_mutex_unlock(&g_table_lock);
    if (slot < 0) {
        vdeccore_free(d);
        VDECCORE_RETURN(VDECCORE_ERROR_FATAL);
    }
    *out = d->handle;
    fprintf(stderr, "[libSceVdecCore] decoder %d: type %u codec %u max %ux%u dpb %u\n", slot, cfg->decoder_type,
            cfg->codec_type, d->max_width, d->max_height, cfg_dpb(cfg));
    VDECCORE_RETURN(VDECCORE_OK);
}

// int sceVdecCoreDeleteDecoder(SceVdecCoreDecoder decoder)
void shim_sceVdecCoreDeleteDecoder(GuestContext *ctx) {
    VdecCore *d = vdeccore_acquire(ctx->rdi);
    if (!d) VDECCORE_RETURN(VDECCORE_ERROR_INVALID_ARGUMENT);
    pthread_mutex_lock(&g_table_lock);
    const bool mine = g_decoders[d->slot] == d; // a concurrent Delete may have won
    if (mine) {
        g_decoders[d->slot] = NULL;
        d->refs--; // the table's reference; calls in progress keep the decoder until they return
    }
    pthread_mutex_unlock(&g_table_lock);
    vdeccore_release(d);
    VDECCORE_RETURN(mine ? VDECCORE_OK : VDECCORE_ERROR_INVALID_ARGUMENT);
}

// int sceVdecCoreResetDecoder(SceVdecCoreDecoder decoder, void *reset_param)
static int32_t vdeccore_ResetDecoder(GuestContext *ctx, VdecCore *d) {
    pthread_mutex_lock(&d->in_lock);
    h264vt_reset(d->core);
    pthread_mutex_lock(&d->lock);
    d->slot_index = 0;
    pthread_mutex_unlock(&d->lock);
    pthread_mutex_unlock(&d->in_lock);
    return VDECCORE_OK;
}
void shim_sceVdecCoreResetDecoder(GuestContext *ctx) { VDECCORE_WITH_DECODER(vdeccore_ResetDecoder); }

// int sceVdecCoreSetDecodeInput(SceVdecCoreDecoder decoder, const SceVdecCoreInputData *input,
//                               void *reserved, u32 *status)
static int32_t vdeccore_SetDecodeInput(GuestContext *ctx, VdecCore *d) {
    const VdecCoreInputData *in = (const VdecCoreInputData *)gptr(ctx, ctx->rsi);
    uint32_t *status = (uint32_t *)gptr(ctx, ctx->rcx);
    if (!in) return VDECCORE_ERROR_INVALID_ARGUMENT;
    if (status) {
        status[0] = 1;
        status[1] = 1;
    }
    if (!in->au_data || in->au_size == 0 || !grange_ok(ctx, in->au_data, in->au_size))
        return VDECCORE_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&d->in_lock);
    pthread_mutex_lock(&d->lock);
    d->last_pts = in->pts;
    d->last_dts = in->dts;
    pthread_mutex_unlock(&d->lock);
    const int err = h264vt_decode(d->core, ctx->mem_base + in->au_data, (size_t)in->au_size, in->pts, in->dts,
                                  in->user_data, NULL);
    pthread_mutex_unlock(&d->in_lock);
    if (err != 0) {
        // The access unit is consumed, as FFmpeg's decoder does with a broken one: it yields no
        // picture (its error picture is dropped before output).
        static int warned;
        if (warned++ < 8)
            fprintf(stderr, "[libSceVdecCore] access unit at 0x%llx (%llu bytes) failed: %d\n",
                    (unsigned long long)in->au_data, (unsigned long long)in->au_size, err);
    }
    return VDECCORE_OK;
}
void shim_sceVdecCoreSetDecodeInput(GuestContext *ctx) { VDECCORE_WITH_DECODER(vdeccore_SetDecodeInput); }

// int sceVdecCoreSyncDecode(SceVdecCoreDecoder decoder, u32 *out_count), and the Try / Wptr
// variants: decoding is finished when SetDecodeInput returns, so all report the ready pictures.
static int32_t vdeccore_SyncDecode(GuestContext *ctx, VdecCore *d) {
    uint32_t *count = (uint32_t *)gptr(ctx, ctx->rsi);
    const int n = h264vt_drop_errors(d->core);
    if (count) *count = (uint32_t)n;
    return VDECCORE_OK;
}
void shim_sceVdecCoreSyncDecode(GuestContext *ctx) { VDECCORE_WITH_DECODER(vdeccore_SyncDecode); }
// int sceVdecCoreSyncDecodeWptr(SceVdecCoreDecoder decoder, u32 *out_count, void *wptr, void *rptr)
void shim_sceVdecCoreSyncDecodeWptr(GuestContext *ctx) { VDECCORE_WITH_DECODER(vdeccore_SyncDecode); }
// int sceVdecCoreTrySyncDecode(SceVdecCoreDecoder decoder, u32 *out_count)
void shim_sceVdecCoreTrySyncDecode(GuestContext *ctx) { VDECCORE_WITH_DECODER(vdeccore_SyncDecode); }
// int sceVdecCoreTrySyncDecodeWptr(SceVdecCoreDecoder decoder, u32 *out_count, void *wptr, void *rptr)
void shim_sceVdecCoreTrySyncDecodeWptr(GuestContext *ctx) { VDECCORE_WITH_DECODER(vdeccore_SyncDecode); }

// int sceVdecCoreGetDecodeOutput(SceVdecCoreDecoder decoder, SceVdecCoreFrameBuffer *frame_buf, u32 flags,
//                                SceVdecCoreOutputInfo *output, void *reserved)
static int32_t vdeccore_GetDecodeOutput(GuestContext *ctx, VdecCore *d) {
    const VdecCoreFrameBuffer *fb = (const VdecCoreFrameBuffer *)gptr(ctx, ctx->rsi);
    VdecCoreOutputInfo *out = (VdecCoreOutputInfo *)gptr(ctx, ctx->rcx);
    if (!out) return VDECCORE_ERROR_INVALID_ARGUMENT;

    if (h264vt_drop_errors(d->core) == 0) {
        out->frame_width = d->max_width;
        out->frame_pitch = h264vt_pitch(d->max_width);
        out->frame_height = d->max_height;
        out->picture_count = 0;
        out->frame_buffer = 0;
        out->frame_format = 0;
        out->reserved0 = 0;
        out->pictures[0].frame_buffer = 0;
        out->pictures[1].frame_buffer = 0;
        out->luma_buffer = 0;
        out->chroma_buffer = 0;
        return VDECCORE_ERROR_NO_PICTURE;
    }

    // The destination: the caller's buffer, else the next mapped slot [S].
    const uint64_t capacity = fb && fb->frame_buffer_size > 0 ? fb->frame_buffer_size : d->frame_buffer_size;
    uint64_t dest = 0;
    pthread_mutex_lock(&d->lock);
    const bool use_slot = !(fb && fb->frame_buffer) && d->slot_count > 0;
    if (fb && fb->frame_buffer) dest = fb->frame_buffer;
    else if (use_slot) dest = d->slots[d->slot_index % d->slot_count];
    pthread_mutex_unlock(&d->lock);
    if (!dest || !grange_ok(ctx, dest, capacity)) {
        static int warned;
        if (warned++ < 8)
            fprintf(stderr, "[libSceVdecCore] GetDecodeOutput: no usable frame buffer (0x%llx, 0x%llx bytes)\n",
                    (unsigned long long)dest, (unsigned long long)capacity);
        return VDECCORE_ERROR_INVALID_ARGUMENT;
    }

    H264VtFrame *f = NULL;
    uint64_t need = 0;
    const int pr = h264vt_pop_fitting(d->core, capacity, 0, &f, &need);
    if (pr == H264VT_POP_EMPTY) return VDECCORE_ERROR_NO_PICTURE; // a concurrent Reset dropped it
    if (pr == H264VT_POP_TOO_SMALL) {
        static int warned;
        if (warned++ < 8)
            fprintf(stderr, "[libSceVdecCore] picture does not fit the frame buffer (0x%llx < 0x%llx)\n",
                    (unsigned long long)capacity, (unsigned long long)need);
        return VDECCORE_ERROR_INVALID_ARGUMENT;
    }
    if (use_slot) {
        pthread_mutex_lock(&d->lock);
        d->slot_index++;
        pthread_mutex_unlock(&d->lock);
    }

    const uint32_t width = f->width, height = f->height;
    const uint32_t aligned_width = align_up(width, 16);
    const uint32_t pitch = h264vt_pitch(width);
    const uint32_t aligned_height = h264vt_aligned_height(height);
    const uint64_t fb_size = h264vt_nv12_size(width, height);
    pthread_mutex_lock(&d->lock);
    const int64_t pts = f->pts != H264VT_NO_TIME ? f->pts : d->last_pts;
    const int64_t dts = f->dts != H264VT_NO_TIME ? f->dts : d->last_dts;
    pthread_mutex_unlock(&d->lock);
    VdecCoreVui vui;
    picture_vui(f, &vui);

    uint8_t *dst = ctx->mem_base + dest;
    h264vt_copy_nv12(f, dst);
    uint64_t written = fb_size;
    if (capacity >= fb_size + sizeof(VdecCoreHwPictureInfo)) {
        VdecCoreHwPictureInfo meta;
        memset(&meta, 0, sizeof(meta));
        meta.codec_type = d->codec_type == VDECCORE_CODEC_HEVC ? 1 : 0;
        meta.user_data = f->attached;
        meta.pts = (uint64_t)pts;
        meta.dts = (uint64_t)dts;
        meta.valid = 1;
        meta.width = aligned_width;
        meta.height = aligned_height;
        meta.crop_right = (pitch - width) / 2;
        meta.crop_bottom = (aligned_height - height) / 2;
        meta.sar_width = vui.sar_w;
        meta.sar_height = vui.sar_h;
        meta.flags = 0x01;
        if (meta.crop_right > 0 || meta.crop_bottom > 0) meta.flags |= 0x02;
        memcpy(dst + capacity - sizeof(meta), &meta, sizeof(meta));
        written = capacity;
    }
    if (ps4_gnm_notify_cpu_write) ps4_gnm_notify_cpu_write(dest, written);

    out->frame_width = aligned_width;
    out->frame_pitch = pitch;
    out->frame_height = aligned_height;
    out->picture_count = 1;
    out->frame_buffer = dest;
    out->frame_format = 0; // NV12
    out->reserved0 = 0;
    VdecCorePicture *p = &out->pictures[0];
    p->flags = d->codec_type;
    p->reserved0 = 0;
    p->pts = (uint64_t)pts;
    p->dts = (uint64_t)dts;
    p->user_data = f->attached;
    p->picture_info = 0;
    p->frame_buffer = dest;
    p->frame_buffer_size = (uint32_t)fb_size;
    p->frame_pitch = pitch;
    p->reserved1 = 0;
    out->pictures[1].frame_buffer = 0;
    out->aspect_ratio = vui.aspect_present ? vui.aspect_idc : 1;
    out->sar_width = vui.sar_w;
    out->video_format = vui.signal_present ? vui.video_format : 5;
    out->colour_primaries = vui.colour_present ? vui.colour_primaries : 1;
    out->transfer_characteristics = vui.colour_present ? vui.transfer : 1;
    out->frame_pitch_in_bytes = pitch;
    out->luma_buffer = dest;
    out->chroma_buffer = dest + (uint64_t)pitch * aligned_height;
    out->video_full_range_flag = vui.signal_present ? vui.full_range : 0;
    out->colour_description_present_flag = 1;
    if (d->frames_out++ == 0) fprintf(stderr, "[libSceVdecCore] first picture decoded (%ux%u)\n", width, height);
    h264vt_frame_free(f);
    return VDECCORE_OK;
}
void shim_sceVdecCoreGetDecodeOutput(GuestContext *ctx) { VDECCORE_WITH_DECODER(vdeccore_GetDecodeOutput); }

// int sceVdecCoreFlushDecodeOutput(SceVdecCoreDecoder decoder, u32 *out_count, void *reserved)
static int32_t vdeccore_FlushDecodeOutput(GuestContext *ctx, VdecCore *d) {
    uint32_t *count = (uint32_t *)gptr(ctx, ctx->rsi);
    pthread_mutex_lock(&d->in_lock);
    h264vt_finish(d->core);
    pthread_mutex_unlock(&d->in_lock);
    const int n = h264vt_drop_errors(d->core);
    if (count) *count = (uint32_t)n;
    return VDECCORE_OK;
}
void shim_sceVdecCoreFlushDecodeOutput(GuestContext *ctx) { VDECCORE_WITH_DECODER(vdeccore_FlushDecodeOutput); }

// int sceVdecCoreMapMemoryBlock(SceVdecCoreDecoder decoder, const SceVdecCoreMemoryBlock *blocks, u32 count)
// Every block is cut into frame_buffer_size slots that GetDecodeOutput uses in turn [S].
static int32_t vdeccore_MapMemoryBlock(GuestContext *ctx, VdecCore *d) {
    const uint64_t blocks_addr = ctx->rsi;
    const uint32_t count = (uint32_t)ctx->rdx;
    if (!blocks_addr || count == 0) return VDECCORE_OK;
    if (!grange_ok(ctx, blocks_addr, (uint64_t)count * sizeof(VdecCoreMemoryBlock)))
        return VDECCORE_ERROR_INVALID_ARGUMENT;
    const VdecCoreMemoryBlock *blocks = (const VdecCoreMemoryBlock *)(ctx->mem_base + blocks_addr);
    int32_t rc = VDECCORE_OK;
    pthread_mutex_lock(&d->lock);
    for (uint32_t i = 0; i < count && rc == VDECCORE_OK; i++) {
        const VdecCoreMemoryBlock b = blocks[i];
        if (!b.addr || b.size < d->frame_buffer_size) continue;
        if (!grange_ok(ctx, b.addr, b.size)) {
            rc = VDECCORE_ERROR_INVALID_ARGUMENT;
            break;
        }
        const uint64_t n = b.size / d->frame_buffer_size;
        if (d->slot_count + n > d->slot_cap) {
            const uint64_t cap = (d->slot_count + n) * 2;
            if (cap > UINT32_MAX / sizeof(uint64_t)) {
                rc = VDECCORE_ERROR_INVALID_ARGUMENT;
                break;
            }
            uint64_t *slots = (uint64_t *)realloc(d->slots, cap * sizeof(uint64_t));
            if (!slots) {
                rc = VDECCORE_ERROR_FATAL;
                break;
            }
            d->slots = slots;
            d->slot_cap = (uint32_t)cap;
        }
        for (uint64_t s = 0; s < n; s++) d->slots[d->slot_count++] = b.addr + s * d->frame_buffer_size;
    }
    pthread_mutex_unlock(&d->lock);
    return rc;
}
void shim_sceVdecCoreMapMemoryBlock(GuestContext *ctx) { VDECCORE_WITH_DECODER(vdeccore_MapMemoryBlock); }

// int sceVdecCoreQueryComputeResourceInfo(SceVdecCoreComputeResourceInfo *info)
void shim_sceVdecCoreQueryComputeResourceInfo(GuestContext *ctx) {
    VdecCoreComputeResourceInfo *info = (VdecCoreComputeResourceInfo *)gptr(ctx, ctx->rdi);
    if (!info) VDECCORE_RETURN(VDECCORE_ERROR_INVALID_ARGUMENT);
    info->memory_type = 0;
    info->size = VDECCORE_COMPUTE_SIZE;
    info->alignment = (uint32_t)VDECCORE_COMPUTE_SIZE;
    info->reserved = 0;
    VDECCORE_RETURN(VDECCORE_OK);
}

// The decoder runs on VideoToolbox: there is no GPU compute resource and no separate software
// output path to set up. shadPS4 returns OK from all of these as well.
// int sceVdecCoreAnalyzeStream(void *)
void shim_sceVdecCoreAnalyzeStream(GuestContext *ctx) { VDECCORE_RETURN(VDECCORE_OK); }
// int sceVdecCoreInitializeComputeResource(const SceVdecCoreComputeResourceInfo *, SceVdecCoreComputeQueue)
void shim_sceVdecCoreInitializeComputeResource(GuestContext *ctx) { VDECCORE_RETURN(VDECCORE_OK); }
// int sceVdecCoreFinalizeComputeResource(void *)
void shim_sceVdecCoreFinalizeComputeResource(GuestContext *ctx) { VDECCORE_RETURN(VDECCORE_OK); }
// int sceVdecCoreSetDecodeOutputSw(SceVdecCoreDecoder, void *)
void shim_sceVdecCoreSetDecodeOutputSw(GuestContext *ctx) { VDECCORE_RETURN(VDECCORE_OK); }
// int sceVdecCoreSyncDecodeOutputSw(SceVdecCoreDecoder, void *)
void shim_sceVdecCoreSyncDecodeOutputSw(GuestContext *ctx) { VDECCORE_RETURN(VDECCORE_OK); }
// int sceVdecCoreTrySyncDecodeOutputSw(SceVdecCoreDecoder, void *)
void shim_sceVdecCoreTrySyncDecodeOutputSw(GuestContext *ctx) { VDECCORE_RETURN(VDECCORE_OK); }
