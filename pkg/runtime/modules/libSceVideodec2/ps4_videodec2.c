// SPDX-License-Identifier: GPL-2.0-or-later
//
// libSceVideodec2 (H.264/AVC) backed by the shared VideoToolbox decode core (ps4_h264_vt.c).
// Struct layouts and error codes follow shadPS4 (src/core/libraries/videodec/videodec2.{h,cpp},
// videodec2_impl.cpp, videodec_error.h).

#include "ps4_videodec2.h"
#include "ps4_h264_vt.h"
#include "recomp_runtime.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEODEC2_OK                          0
#define VIDEODEC2_ERROR_API_FAIL              ((int32_t)0x811D0100)
#define VIDEODEC2_ERROR_STRUCT_SIZE           ((int32_t)0x811D0101)
#define VIDEODEC2_ERROR_ARGUMENT_POINTER      ((int32_t)0x811D0102)
#define VIDEODEC2_ERROR_DECODER_INSTANCE      ((int32_t)0x811D0103)
#define VIDEODEC2_ERROR_MEMORY_SIZE           ((int32_t)0x811D0104)
#define VIDEODEC2_ERROR_MEMORY_POINTER        ((int32_t)0x811D0105)
#define VIDEODEC2_ERROR_FRAME_BUFFER_SIZE     ((int32_t)0x811D0106)
#define VIDEODEC2_ERROR_FRAME_BUFFER_POINTER  ((int32_t)0x811D0107)
#define VIDEODEC2_ERROR_ACCESS_UNIT_SIZE      ((int32_t)0x811D010D)
#define VIDEODEC2_ERROR_ACCESS_UNIT_POINTER   ((int32_t)0x811D010E)
#define VIDEODEC2_ERROR_CONFIG_INFO           ((int32_t)0x811D0200)
#define VIDEODEC2_ERROR_COMPUTE_PIPE_ID       ((int32_t)0x811D0201)
#define VIDEODEC2_ERROR_COMPUTE_QUEUE_ID      ((int32_t)0x811D0202)
#define VIDEODEC2_ERROR_CODEC_TYPE            ((int32_t)0x811D0204)
#define VIDEODEC2_ERROR_PIPELINE_DEPTH        ((int32_t)0x811D0206)
#define VIDEODEC2_ERROR_THREAD_PRIORITY       ((int32_t)0x811D0208)

#define VIDEODEC2_CODEC_AVC                   1u
#define VIDEODEC2_CODEC_HEVC                  974921u

#define VIDEODEC2_COMPUTE_MEM_SIZE            0x4000ULL
#define VIDEODEC2_DECODER_MEM_SIZE            0x10000ULL
#define VIDEODEC2_FRAME_BUFFER_ALIGN          0x100u

typedef struct Videodec2DecoderConfigInfo {
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
    uint8_t reserved1;
    uint64_t extra_config_info;
} Videodec2DecoderConfigInfo;
_Static_assert(sizeof(Videodec2DecoderConfigInfo) == 0x48, "Videodec2DecoderConfigInfo");

typedef struct Videodec2DecoderMemoryInfo {
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
} Videodec2DecoderMemoryInfo;
_Static_assert(sizeof(Videodec2DecoderMemoryInfo) == 0x48, "Videodec2DecoderMemoryInfo");

typedef struct Videodec2InputData {
    uint64_t this_size;
    uint64_t au_data;
    uint64_t au_size;
    uint64_t pts_data;
    uint64_t dts_data;
    uint64_t attached_data;
} Videodec2InputData;
_Static_assert(sizeof(Videodec2InputData) == 0x30, "Videodec2InputData");

typedef struct Videodec2OutputInfo {
    uint64_t this_size;
    uint8_t is_valid;
    uint8_t is_error_frame;
    uint8_t picture_count;
    uint32_t codec_type;
    uint32_t frame_width;
    uint32_t frame_pitch;
    uint32_t frame_height;
    uint64_t frame_buffer;
    uint64_t frame_buffer_size;
    uint32_t frame_format;         // 0x38 layout only
    uint32_t frame_pitch_in_bytes; // 0x38 layout only
} Videodec2OutputInfo;
_Static_assert(sizeof(Videodec2OutputInfo) == 0x38, "Videodec2OutputInfo");
_Static_assert(offsetof(Videodec2OutputInfo, frame_buffer) == 0x20, "Videodec2OutputInfo.frame_buffer");
_Static_assert(offsetof(Videodec2OutputInfo, frame_format) == 0x30, "Videodec2OutputInfo.frame_format");

typedef struct Videodec2FrameBuffer {
    uint64_t this_size;
    uint64_t frame_buffer;
    uint64_t frame_buffer_size;
    uint8_t is_accepted;
} Videodec2FrameBuffer;
_Static_assert(sizeof(Videodec2FrameBuffer) == 0x20, "Videodec2FrameBuffer");

typedef struct Videodec2DirectMemoryInfo {
    uint64_t this_size;
    uint64_t memory_size;
    uint64_t memory_addr;
    uint64_t reserved;
} Videodec2DirectMemoryInfo;
_Static_assert(sizeof(Videodec2DirectMemoryInfo) == 0x20, "Videodec2DirectMemoryInfo");

typedef struct Videodec2ComputeMemoryInfo {
    uint64_t this_size;
    uint64_t cpu_gpu_memory_size;
    uint64_t cpu_gpu_memory;
} Videodec2ComputeMemoryInfo;
_Static_assert(sizeof(Videodec2ComputeMemoryInfo) == 0x18, "Videodec2ComputeMemoryInfo");

typedef struct Videodec2ComputeConfigInfo {
    uint64_t this_size;
    uint16_t compute_pipe_id;
    uint16_t compute_queue_id;
    uint8_t check_memory_type;
    uint8_t reserved0;
    uint16_t reserved1;
} Videodec2ComputeConfigInfo;
_Static_assert(sizeof(Videodec2ComputeConfigInfo) == 0x10, "Videodec2ComputeConfigInfo");

typedef struct Videodec2AvcPictureInfo {
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
} Videodec2AvcPictureInfo;
_Static_assert(sizeof(Videodec2AvcPictureInfo) == 0x78, "Videodec2AvcPictureInfo");

#define VIDEODEC2_MAGIC 0x56444332u /* "VDC2" */
#define VIDEODEC2_MAX_DECODERS 16

typedef struct Vdec2Decoder {
    uint32_t magic;
    uint32_t generation;
    uint32_t codec_type;
    uint32_t ordinal;
    H264VtDecoder *core;
    pthread_mutex_t call_lock;
    uint64_t trace_n;
    uint64_t trace_warn;
} Vdec2Decoder;

static pthread_mutex_t g_vdec2_table_lock = PTHREAD_MUTEX_INITIALIZER;
static Vdec2Decoder *g_vdec2_slots[VIDEODEC2_MAX_DECODERS];
static _Atomic uint32_t g_vdec2_ordinal;

void ps4_gnm_notify_cpu_write(uint64_t addr, uint64_t size) __attribute__((weak));

static inline uint32_t align_up_u32(uint32_t v, uint32_t a) { return (v + a - 1u) & ~(a - 1u); }
static inline uint64_t align_up_u64(uint64_t v, uint64_t a) { return (v + a - 1ULL) & ~(a - 1ULL); }

static inline bool grange_ok(const GuestContext *ctx, uint64_t addr, uint64_t size) {
    if (!ctx || !ctx->mem_base || addr == 0) return false;
    return addr < ctx->mem_size && size <= ctx->mem_size - addr;
}

static inline void *gptr(const GuestContext *ctx, uint64_t addr) {
    if (!ctx || !ctx->mem_base || addr == 0 || addr >= ctx->mem_size) return NULL;
    return (void *)(ctx->mem_base + addr);
}

static Vdec2Decoder *vdec2_lookup(uint64_t handle) {
    if (handle == 0) return NULL;
    pthread_mutex_lock(&g_vdec2_table_lock);
    Vdec2Decoder *found = NULL;
    for (int i = 0; i < VIDEODEC2_MAX_DECODERS; i++) {
        if ((uint64_t)(uintptr_t)g_vdec2_slots[i] == handle &&
            g_vdec2_slots[i]->magic == VIDEODEC2_MAGIC) {
            found = g_vdec2_slots[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_vdec2_table_lock);
    return found;
}

static void vdec2_fill_avc_pic_info(Videodec2AvcPictureInfo *p, const H264VtFrame *f) {
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
    p->pic_struct = 0;
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

static int32_t vdec2_emit_picture(GuestContext *ctx, Vdec2Decoder *d,
                                  Videodec2FrameBuffer *fb, Videodec2OutputInfo *out) {
    H264VtFrame *f = NULL;
    uint64_t need = 0;
    const int pr = h264vt_pop_fitting(d->core, fb->frame_buffer_size, sizeof(Videodec2AvcPictureInfo), &f, &need);
    if (pr == H264VT_POP_EMPTY) {
        return VIDEODEC2_OK;
    }
    if (pr == H264VT_POP_TOO_SMALL) {
        fprintf(stderr, "[libSceVideodec2] frame buffer too small (0x%llx < 0x%llx)\n",
                (unsigned long long)fb->frame_buffer_size, (unsigned long long)need);
        return VIDEODEC2_ERROR_FRAME_BUFFER_SIZE;
    }

    const uint64_t frame_size = h264vt_nv12_size(f->width, f->height);
    if (!grange_ok(ctx, fb->frame_buffer, frame_size + sizeof(Videodec2AvcPictureInfo))) {
        h264vt_frame_free(f);
        return VIDEODEC2_ERROR_FRAME_BUFFER_SIZE;
    }

    uint8_t *dst = ctx->mem_base + fb->frame_buffer;
    if (!f->error) {
        h264vt_copy_nv12(f, dst);
    }

    Videodec2AvcPictureInfo pic;
    vdec2_fill_avc_pic_info(&pic, f);
    memcpy(dst + frame_size, &pic, sizeof(pic));
    if (ps4_gnm_notify_cpu_write) {
        if (f->error) ps4_gnm_notify_cpu_write(fb->frame_buffer + frame_size, sizeof(pic));
        else ps4_gnm_notify_cpu_write(fb->frame_buffer, frame_size + sizeof(pic));
    }

    fb->is_accepted = 1;
    out->is_valid = 1;
    out->is_error_frame = f->error ? 1 : 0;
    out->picture_count = 1;
    out->codec_type = d->codec_type;
    out->frame_width = align_up_u32(f->width, 16u);
    out->frame_pitch = h264vt_pitch(f->width);
    out->frame_height = h264vt_aligned_height(f->height);
    out->frame_buffer = fb->frame_buffer;
    out->frame_buffer_size = frame_size;
    if (out->this_size == sizeof(Videodec2OutputInfo)) {
        out->frame_format = 0;
        out->frame_pitch_in_bytes = out->frame_pitch;
    }

    h264vt_frame_free(f);
    return VIDEODEC2_OK;
}

void shim_sceVideodec2QueryComputeMemoryInfo(GuestContext *ctx) {
    Videodec2ComputeMemoryInfo *info = (Videodec2ComputeMemoryInfo *)gptr(ctx, ctx->rdi);
    if (!info) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    if (info->this_size != sizeof(*info)) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_STRUCT_SIZE;
        SHIM_RETURN();
    }
    info->cpu_gpu_memory = 0;
    info->cpu_gpu_memory_size = VIDEODEC2_COMPUTE_MEM_SIZE;
    ctx->rax = VIDEODEC2_OK;
    SHIM_RETURN();
}

void shim_sceVideodec2AllocateComputeQueue(GuestContext *ctx) {
    const Videodec2ComputeConfigInfo *cfg = (const Videodec2ComputeConfigInfo *)gptr(ctx, ctx->rdi);
    const Videodec2ComputeMemoryInfo *mem = (const Videodec2ComputeMemoryInfo *)gptr(ctx, ctx->rsi);
    uint64_t *out_queue = (uint64_t *)gptr(ctx, ctx->rdx);
    if (!cfg || !mem || !out_queue) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    if (cfg->this_size != sizeof(*cfg) || mem->this_size != sizeof(*mem)) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_STRUCT_SIZE;
        SHIM_RETURN();
    }
    if (cfg->reserved0 != 0 || cfg->reserved1 != 0) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_CONFIG_INFO;
        SHIM_RETURN();
    }
    if (cfg->compute_pipe_id > 4) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_COMPUTE_PIPE_ID;
        SHIM_RETURN();
    }
    if (cfg->compute_queue_id > 7) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_COMPUTE_QUEUE_ID;
        SHIM_RETURN();
    }
    if (!mem->cpu_gpu_memory) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_MEMORY_POINTER;
        SHIM_RETURN();
    }
    *out_queue = mem->cpu_gpu_memory;
    ctx->rax = VIDEODEC2_OK;
    SHIM_RETURN();
}

void shim_sceVideodec2ReleaseComputeQueue(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = VIDEODEC2_OK;
    SHIM_RETURN();
}

void shim_sceVideodec2QueryDecoderMemoryInfo(GuestContext *ctx) {
    const Videodec2DecoderConfigInfo *cfg = (const Videodec2DecoderConfigInfo *)gptr(ctx, ctx->rdi);
    Videodec2DecoderMemoryInfo *mem = (Videodec2DecoderMemoryInfo *)gptr(ctx, ctx->rsi);
    if (!cfg || !mem) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    if (cfg->this_size != sizeof(*cfg) || mem->this_size != sizeof(*mem)) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_STRUCT_SIZE;
        SHIM_RETURN();
    }
    if (cfg->codec_type != VIDEODEC2_CODEC_AVC && cfg->codec_type != VIDEODEC2_CODEC_HEVC) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_CODEC_TYPE;
        SHIM_RETURN();
    }
    if (cfg->decode_pipeline_depth == 0 || cfg->decode_pipeline_depth > 8) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_PIPELINE_DEPTH;
        SHIM_RETURN();
    }
    if (cfg->cpu_thread_priority != -1 &&
        ((uint32_t)(cfg->cpu_thread_priority - 0x100) >= 0x200u)) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_THREAD_PRIORITY;
        SHIM_RETURN();
    }

    int32_t w = cfg->max_frame_width;
    int32_t h = cfg->max_frame_height;
    if (w <= 0 || h <= 0) {
        w = (cfg->max_level >= 150) ? 3840 : 1920;
        h = (cfg->max_level >= 150) ? 2160 : 1080;
    }
    const uint64_t fb = h264vt_nv12_size((uint32_t)w, (uint32_t)h);

    mem->cpu_memory = 0;
    mem->gpu_memory = 0;
    mem->cpu_gpu_memory = 0;
    mem->cpu_memory_size = VIDEODEC2_DECODER_MEM_SIZE;
    mem->gpu_memory_size = VIDEODEC2_DECODER_MEM_SIZE;
    mem->cpu_gpu_memory_size = VIDEODEC2_DECODER_MEM_SIZE;
    mem->max_frame_buffer_size = align_up_u64(fb, 256ULL) + 0x4000ULL;
    mem->frame_buffer_alignment = VIDEODEC2_FRAME_BUFFER_ALIGN;

    ctx->rax = VIDEODEC2_OK;
    SHIM_RETURN();
}

void shim_sceVideodec2QueryHevcDecoderMemoryInfo(GuestContext *ctx) {
    shim_sceVideodec2QueryDecoderMemoryInfo(ctx);
}

void shim_sceVideodec2CreateDecoder(GuestContext *ctx) {
    const Videodec2DecoderConfigInfo *cfg = (const Videodec2DecoderConfigInfo *)gptr(ctx, ctx->rdi);
    const Videodec2DecoderMemoryInfo *mem = (const Videodec2DecoderMemoryInfo *)gptr(ctx, ctx->rsi);
    uint64_t *out_dec = (uint64_t *)gptr(ctx, ctx->rdx);
    if (!cfg || !mem || !out_dec) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    if (cfg->this_size != sizeof(*cfg) || mem->this_size != sizeof(*mem)) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_STRUCT_SIZE;
        SHIM_RETURN();
    }
    if (cfg->codec_type != VIDEODEC2_CODEC_AVC) {
        fprintf(stderr, "[libSceVideodec2] unsupported codec_type=%u\n", cfg->codec_type);
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_CODEC_TYPE;
        SHIM_RETURN();
    }

    Vdec2Decoder *d = (Vdec2Decoder *)calloc(1, sizeof(Vdec2Decoder));
    if (!d) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_API_FAIL;
        SHIM_RETURN();
    }
    d->core = h264vt_create("libSceVideodec2");
    if (!d->core) {
        free(d);
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_API_FAIL;
        SHIM_RETURN();
    }
    pthread_mutex_init(&d->call_lock, NULL);
    d->magic = VIDEODEC2_MAGIC;
    d->codec_type = cfg->codec_type;
    d->ordinal = atomic_fetch_add(&g_vdec2_ordinal, 1u) + 1u;

    pthread_mutex_lock(&g_vdec2_table_lock);
    int slot = -1;
    for (int i = 0; i < VIDEODEC2_MAX_DECODERS; i++) {
        if (g_vdec2_slots[i] == NULL) {
            g_vdec2_slots[i] = d;
            slot = i;
            break;
        }
    }
    pthread_mutex_unlock(&g_vdec2_table_lock);
    if (slot < 0) {
        h264vt_destroy(d->core);
        pthread_mutex_destroy(&d->call_lock);
        free(d);
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_API_FAIL;
        SHIM_RETURN();
    }

    *out_dec = (uint64_t)(uintptr_t)d;
    fprintf(stderr, "[libSceVideodec2] decoder %u created: codec %u max %dx%d handle=0x%llx\n",
            d->ordinal, cfg->codec_type, cfg->max_frame_width, cfg->max_frame_height,
            (unsigned long long)*out_dec);
    ctx->rax = VIDEODEC2_OK;
    SHIM_RETURN();
}

void shim_sceVideodec2CreateHevcDecoder(GuestContext *ctx) {
    shim_sceVideodec2CreateDecoder(ctx);
}

void shim_sceVideodec2DeleteDecoder(GuestContext *ctx) {
    Vdec2Decoder *d = vdec2_lookup(ctx->rdi);
    if (!d) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_DECODER_INSTANCE;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_vdec2_table_lock);
    for (int i = 0; i < VIDEODEC2_MAX_DECODERS; i++) {
        if (g_vdec2_slots[i] == d) {
            g_vdec2_slots[i] = NULL;
            break;
        }
    }
    d->magic = 0;
    pthread_mutex_unlock(&g_vdec2_table_lock);

    if (getenv("PS4_VIDEODEC2_TRACE")) {
        fprintf(stderr, "[libSceVideodec2] DeleteDecoder(handle=%p)\n", (void *)d);
    }
    pthread_mutex_lock(&d->call_lock);
    h264vt_destroy(d->core);
    pthread_mutex_unlock(&d->call_lock);
    pthread_mutex_destroy(&d->call_lock);
    free(d);

    ctx->rax = VIDEODEC2_OK;
    SHIM_RETURN();
}

void shim_sceVideodec2Decode(GuestContext *ctx) {
    Vdec2Decoder *d = vdec2_lookup(ctx->rdi);
    const Videodec2InputData *in = (const Videodec2InputData *)gptr(ctx, ctx->rsi);
    Videodec2FrameBuffer *fb = (Videodec2FrameBuffer *)gptr(ctx, ctx->rdx);
    Videodec2OutputInfo *out = (Videodec2OutputInfo *)gptr(ctx, ctx->rcx);

    if (!d) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_DECODER_INSTANCE;
        SHIM_RETURN();
    }
    if (!in || !fb || !out) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    if (in->this_size != sizeof(*in) || fb->this_size != sizeof(*fb) ||
        (out->this_size | 8ULL) != sizeof(*out)) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_STRUCT_SIZE;
        SHIM_RETURN();
    }

    fb->is_accepted = 0;
    out->is_valid = 0;
    out->is_error_frame = 0;
    out->picture_count = 0;

    if (!in->au_data) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_ACCESS_UNIT_POINTER;
        SHIM_RETURN();
    }
    if (in->au_size == 0 || !grange_ok(ctx, in->au_data, in->au_size)) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_ACCESS_UNIT_SIZE;
        SHIM_RETURN();
    }
    if (!fb->frame_buffer) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_FRAME_BUFFER_POINTER;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&d->call_lock);
    const uint8_t *au = ctx->mem_base + in->au_data;
    int dec_rc = h264vt_decode(d->core, au, (size_t)in->au_size, (int64_t)in->pts_data,
                               (int64_t)in->dts_data, in->attached_data, NULL);
    int32_t rc = vdec2_emit_picture(ctx, d, fb, out);
    int qn = h264vt_queued(d->core);
    pthread_mutex_unlock(&d->call_lock);
    if (getenv("PS4_VIDEODEC2_TRACE")) {
        const uint64_t idx = d->trace_n++;
        const bool anom = (rc != 0 || dec_rc != 0 || !out->is_valid);
        if (idx < 15 || (anom && d->trace_warn++ < 50)) {
            fprintf(stderr, "[libSceVideodec2] Decode(dec=%u #%llu au=%llu pts=%lld dts=%lld) dec_rc=%d rc=0x%x acc=%u valid=%u q=%d\n",
                    d->ordinal, (unsigned long long)idx, (unsigned long long)in->au_size,
                    (long long)in->pts_data, (long long)in->dts_data,
                    dec_rc, (unsigned)rc, (unsigned)fb->is_accepted, (unsigned)out->is_valid, qn);
        }
    }

    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceVideodec2Flush(GuestContext *ctx) {
    Vdec2Decoder *d = vdec2_lookup(ctx->rdi);
    Videodec2FrameBuffer *fb = (Videodec2FrameBuffer *)gptr(ctx, ctx->rsi);
    Videodec2OutputInfo *out = (Videodec2OutputInfo *)gptr(ctx, ctx->rdx);

    if (!d) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_DECODER_INSTANCE;
        SHIM_RETURN();
    }
    if (!fb || !out) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    if (fb->this_size != sizeof(*fb) || (out->this_size | 8ULL) != sizeof(*out)) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_STRUCT_SIZE;
        SHIM_RETURN();
    }

    fb->is_accepted = 0;
    out->is_valid = 0;
    out->is_error_frame = 0;
    out->picture_count = 0;

    pthread_mutex_lock(&d->call_lock);
    h264vt_finish(d->core);
    int32_t rc = vdec2_emit_picture(ctx, d, fb, out);
    int qn = h264vt_queued(d->core);
    pthread_mutex_unlock(&d->call_lock);
    if (getenv("PS4_VIDEODEC2_TRACE")) {
        fprintf(stderr, "[libSceVideodec2] Flush(handle=%p) rc=0x%x acc=%u valid=%u q=%d\n",
                (void *)d, (unsigned)rc, (unsigned)fb->is_accepted, (unsigned)out->is_valid, qn);
    }

    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceVideodec2Reset(GuestContext *ctx) {
    Vdec2Decoder *d = vdec2_lookup(ctx->rdi);
    if (!d) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_DECODER_INSTANCE;
        SHIM_RETURN();
    }
    if (getenv("PS4_VIDEODEC2_TRACE")) {
        fprintf(stderr, "[libSceVideodec2] Reset(handle=%p)\n", (void *)d);
    }
    pthread_mutex_lock(&d->call_lock);
    h264vt_reset(d->core);
    pthread_mutex_unlock(&d->call_lock);
    ctx->rax = VIDEODEC2_OK;
    SHIM_RETURN();
}

void shim_sceVideodec2GetPictureInfo(GuestContext *ctx) {
    const Videodec2OutputInfo *out = (const Videodec2OutputInfo *)gptr(ctx, ctx->rdi);
    uint8_t *p1 = (uint8_t *)gptr(ctx, ctx->rsi);
    (void)ctx->rdx;

    if (!out) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    if ((out->this_size | 8ULL) != sizeof(*out)) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_STRUCT_SIZE;
        SHIM_RETURN();
    }
    if (out->picture_count == 0) {
        ctx->rax = VIDEODEC2_OK;
        SHIM_RETURN();
    }
    if (p1) {
        uint64_t size = *(const uint64_t *)p1;
        if (size > sizeof(uint64_t) &&
            grange_ok(ctx, out->frame_buffer + out->frame_buffer_size, size) &&
            grange_ok(ctx, ctx->rsi, size)) {
            const uint8_t *src = ctx->mem_base + out->frame_buffer + out->frame_buffer_size;
            memcpy(p1 + sizeof(uint64_t), src + sizeof(uint64_t), (size_t)(size - sizeof(uint64_t)));
        }
    }
    ctx->rax = VIDEODEC2_OK;
    SHIM_RETURN();
}

void shim_sceVideodec2GetAvcPictureInfo(GuestContext *ctx) {
    shim_sceVideodec2GetPictureInfo(ctx);
}

void shim_sceVideodec2GetHevcPictureInfo(GuestContext *ctx) {
    shim_sceVideodec2GetPictureInfo(ctx);
}

void shim_sceVideodec2MapDirectMemory(GuestContext *ctx) {
    Vdec2Decoder *d = vdec2_lookup(ctx->rdi);
    const Videodec2DirectMemoryInfo *info = (const Videodec2DirectMemoryInfo *)gptr(ctx, ctx->rsi);
    if (!d) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_DECODER_INSTANCE;
        SHIM_RETURN();
    }
    if (!info) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    if (info->this_size != sizeof(*info)) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_STRUCT_SIZE;
        SHIM_RETURN();
    }
    if (!info->memory_addr || info->memory_size == 0) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC2_ERROR_MEMORY_SIZE;
        SHIM_RETURN();
    }
    ctx->rax = VIDEODEC2_OK;
    SHIM_RETURN();
}
