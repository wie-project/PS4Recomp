// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_ajm.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdbool.h>

#include "libatrac9.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#define MAX_AJM_CONTEXTS 16
#define MAX_AJM_INSTANCES 128
#define MAX_AJM_BATCHES 256
#define INSTANCE_ID_MASK 0x3FFF

#define FOURCC(a,b,c,d) ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

enum AjmIdent {
    AjmIdentJob = 0,
    AjmIdentInputRunBuf = 1,
    AjmIdentInputControlBuf = 2,
    AjmIdentControlFlags = 3,
    AjmIdentRunFlags = 4,
    AjmIdentReturnAddressBuf = 6,
    AjmIdentInlineBuf = 7,
    AjmIdentOutputRunBuf = 17,
    AjmIdentOutputControlBuf = 18,
};

typedef struct {
    uint32_t val; // 6 bits ident, 20 bits payload, 6 bits reserved
} AjmChunkHeader;

static inline uint8_t chunk_ident(AjmChunkHeader h) { return (uint8_t)(h.val & 0x3F); }
static inline uint32_t chunk_payload(AjmChunkHeader h) { return (h.val >> 6) & 0xFFFFF; }
static inline AjmChunkHeader make_chunk_header(uint8_t ident, uint32_t payload) {
    AjmChunkHeader h;
    h.val = (ident & 0x3F) | ((payload & 0xFFFFF) << 6);
    return h;
}

typedef struct {
    AjmChunkHeader header;
    uint32_t size;
} AjmChunkJob;

typedef struct {
    AjmChunkHeader header;
    uint32_t flags_low;
} AjmChunkFlags;

typedef struct {
    AjmChunkHeader header;
    uint32_t size;
    void *p_address;
} AjmChunkBuffer;

typedef struct {
    uint8_t *cur;
    uint8_t *end;
} AjmStream;

static inline bool stream_has(AjmStream *s, size_t sz) {
    return (s->cur + sz <= s->end);
}

static inline void *stream_consume(AjmStream *s, size_t sz) {
    if (!stream_has(s, sz)) return NULL;
    void *p = s->cur;
    s->cur += sz;
    return p;
}

static const uint8_t s_at9_guid[16] = {
    0xD2, 0x42, 0xE1, 0x47, 0xBA, 0x36, 0x8D, 0x4D,
    0x88, 0xFC, 0x61, 0x65, 0x4F, 0x8C, 0x83, 0x6C
};

typedef struct {
    uint16_t fmt_type;
    uint16_t num_channels;
    uint32_t avg_sample_rate;
    uint32_t avg_byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint16_t ext_size;
    uint16_t samples_per_block;
    uint32_t channel_mask;
    uint8_t guid[16];
    uint32_t version;
    uint8_t config_data[4];
    uint32_t reserved2;
} __attribute__((packed)) WaveFormatExtensible;

typedef struct {
    uint32_t sample_length;
    uint32_t encoder_delay;
    uint32_t encoder_padding;
} __attribute__((packed)) FactSampleData;

typedef struct {
    bool in_use;
    uint32_t id;
    uint32_t context_id;
    OrbisAjmCodecType codec_type;
    OrbisAjmFormatEncoding format;
    uint32_t channels;
    uint64_t flags;
    bool is_initialized;

    // AT9 state
    void *at9_handle;
    Atrac9CodecInfo at9_info;
    uint8_t at9_config[4];
    uint32_t superframe_bytes_remain;

    // MP3 state
    mp3dec_t mp3d;
    bool mp3_inited;

    // Common PCM intermediate buffer
    uint8_t *pcm_buffer;
    size_t pcm_buffer_cap;
} AjmInstance;

typedef struct {
    uint32_t instance_id;
    uint64_t flags;
    void *input_buf;
    size_t input_size;
    void *output_buf;
    size_t output_size;
    void *control_in_buf;
    size_t control_in_size;
    void *control_out_buf;
    size_t control_out_size;
    void *return_addr;
} AjmParsedJob;

#define MAX_JOBS_PER_BATCH 32

typedef struct AjmBatch {
    bool in_use;
    uint32_t id;
    uint32_t context_id;
    bool finished;
    bool canceled;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    AjmParsedJob jobs[MAX_JOBS_PER_BATCH];
    size_t num_jobs;
    struct AjmBatch *next;
} AjmBatch;

typedef struct {
    bool in_use;
    uint32_t id;
    bool registered_codecs[AJM_CODEC_MAX + 1];

    pthread_t worker_thread;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    bool worker_stop;
    AjmBatch *queue_head;
    AjmBatch *queue_tail;
} AjmContext;

static pthread_mutex_t g_ajm_lock = PTHREAD_MUTEX_INITIALIZER;
static AjmContext g_ajm_contexts[MAX_AJM_CONTEXTS];
static AjmInstance g_ajm_instances[MAX_AJM_INSTANCES];
static AjmBatch g_ajm_batches[MAX_AJM_BATCHES];
static uint32_t g_next_batch_id = 1;

static inline void *guest_to_host(GuestContext *ctx, uint64_t gaddr) {
    if (!ctx || !ctx->mem_base || gaddr == 0) return NULL;
    if (gaddr >= (uintptr_t)ctx->mem_base && gaddr < (uintptr_t)ctx->mem_base + ctx->mem_size) {
        return (void *)gaddr;
    }
    return (void *)(ctx->mem_base + gaddr);
}

static AjmInstance *get_instance_locked(uint32_t instance_id) {
    uint32_t idx = instance_id & INSTANCE_ID_MASK;
    if (idx >= MAX_AJM_INSTANCES) return NULL;
    if (!g_ajm_instances[idx].in_use) return NULL;
    return &g_ajm_instances[idx];
}

static void at9_parse_riff(AjmInstance *inst, const uint8_t *data, size_t size) {
    if (size < 12) return;
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) return;

    size_t offset = 12;
    while (offset + 8 <= size) {
        uint32_t chunk_tag = *(const uint32_t *)(data + offset);
        uint32_t chunk_len = *(const uint32_t *)(data + offset + 4);
        offset += 8;
        if (offset + chunk_len > size) break;

        if (chunk_tag == FOURCC('f','m','t',' ') && chunk_len >= sizeof(WaveFormatExtensible)) {
            const WaveFormatExtensible *fmt = (const WaveFormatExtensible *)(data + offset);
            if (fmt->fmt_type == 0xFFFE && memcmp(fmt->guid, s_at9_guid, 16) == 0) {
                memcpy(inst->at9_config, fmt->config_data, 4);
                if (!inst->at9_handle) {
                    inst->at9_handle = Atrac9GetHandle();
                }
                if (inst->at9_handle && Atrac9InitDecoder(inst->at9_handle, inst->at9_config) == 0) {
                    Atrac9GetCodecInfo(inst->at9_handle, &inst->at9_info);
                    inst->is_initialized = true;
                    inst->superframe_bytes_remain = inst->at9_info.superframeSize;
                }
            }
        }
        offset += (chunk_len + 1) & ~1;
    }
}

static void process_single_job(AjmParsedJob *job) {
    if (job->instance_id == AJM_INSTANCE_STATISTICS) {
        if (job->control_out_buf && job->control_out_size >= sizeof(OrbisAjmSidebandResult)) {
            OrbisAjmSidebandResult *res = (OrbisAjmSidebandResult *)job->control_out_buf;
            res->result = 0;
            res->internal_result = 0;
        }
        return;
    }

    pthread_mutex_lock(&g_ajm_lock);
    AjmInstance *inst = get_instance_locked(job->instance_id);
    pthread_mutex_unlock(&g_ajm_lock);

    if (!inst) {
        if (job->control_out_buf && job->control_out_size >= sizeof(OrbisAjmSidebandResult)) {
            OrbisAjmSidebandResult *res = (OrbisAjmSidebandResult *)job->control_out_buf;
            res->result = ORBIS_AJM_RESULT_NOT_INITIALIZED;
        }
        return;
    }

    // Handle Control Flags
    if (job->flags & (1ULL << 0)) { // Reset
        inst->superframe_bytes_remain = inst->at9_info.superframeSize;
    }
    if (job->flags & (1ULL << 1)) { // Initialize
        if (job->control_in_buf && job->control_in_size >= 4 && inst->codec_type == AJM_CODEC_AT9_DEC) {
            memcpy(inst->at9_config, job->control_in_buf, 4);
            if (!inst->at9_handle) {
                inst->at9_handle = Atrac9GetHandle();
            }
            if (inst->at9_handle && Atrac9InitDecoder(inst->at9_handle, inst->at9_config) == 0) {
                Atrac9GetCodecInfo(inst->at9_handle, &inst->at9_info);
                inst->is_initialized = true;
                inst->superframe_bytes_remain = inst->at9_info.superframeSize;
            }
        }
    }

    int32_t result_code = 0;
    int32_t total_samples_written = 0;
    int32_t total_input_consumed = 0;

    // Process Run Data
    if (job->input_buf && job->input_size > 0 && job->output_buf && job->output_size > 0) {
        uint8_t *in_data = (uint8_t *)job->input_buf;
        size_t in_size = job->input_size;
        uint8_t *out_data = (uint8_t *)job->output_buf;
        size_t out_capacity = job->output_size;

        if (inst->codec_type == AJM_CODEC_AT9_DEC) {
            if (in_size >= 12 && memcmp(in_data, "RIFF", 4) == 0) {
                at9_parse_riff(inst, in_data, in_size);
            }
            if (inst->is_initialized && inst->at9_handle) {
                int bytes_used = 0;
                int channels = inst->at9_info.channels ? inst->at9_info.channels : 2;
                int frame_samples = inst->at9_info.frameSamples ? inst->at9_info.frameSamples : 256;
                size_t pcm_frame_bytes = (size_t)frame_samples * channels * 2; // S16 default

                if (out_capacity >= pcm_frame_bytes) {
                    int ret = 0;
                    if (inst->format == AJM_FORMAT_S16) {
                        ret = Atrac9Decode(inst->at9_handle, in_data, (int)in_size, (short *)out_data, &bytes_used, 0);
                    } else if (inst->format == AJM_FORMAT_S32) {
                        ret = Atrac9DecodeS32(inst->at9_handle, in_data, (int)in_size, (int *)out_data, &bytes_used, 0);
                    } else if (inst->format == AJM_FORMAT_FLOAT) {
                        ret = Atrac9DecodeF32(inst->at9_handle, in_data, (int)in_size, (float *)out_data, &bytes_used, 0);
                    }
                    if (ret == 0) {
                        total_input_consumed = bytes_used;
                        total_samples_written = frame_samples;
                    } else {
                        result_code = ORBIS_AJM_RESULT_CODEC_ERROR;
                    }
                } else {
                    result_code = ORBIS_AJM_RESULT_NOT_ENOUGH_ROOM;
                }
            } else {
                result_code = ORBIS_AJM_RESULT_NOT_INITIALIZED;
            }
        } else if (inst->codec_type == AJM_CODEC_MP3_DEC) {
            mp3dec_frame_info_t info;
            short pcm_frame[MINIMP3_MAX_SAMPLES_PER_FRAME];
            int samples = mp3dec_decode_frame(&inst->mp3d, in_data, (int)in_size, pcm_frame, &info);
            if (samples > 0) {
                total_input_consumed = info.frame_bytes;
                total_samples_written = samples;
                size_t bytes_to_copy = (size_t)samples * info.channels * sizeof(short);
                if (bytes_to_copy <= out_capacity) {
                    memcpy(out_data, pcm_frame, bytes_to_copy);
                }
            } else {
                result_code = ORBIS_AJM_RESULT_CODEC_ERROR;
            }
        }
    }

    // Write sideband stream result
    if (job->control_out_buf && job->control_out_size >= sizeof(OrbisAjmSidebandResult)) {
        OrbisAjmSidebandResult *res = (OrbisAjmSidebandResult *)job->control_out_buf;
        res->result = result_code;
        res->internal_result = 0;
    }
}

static void *ajm_worker_thread(void *arg) {
    AjmContext *ctx = (AjmContext *)arg;
    while (1) {
        pthread_mutex_lock(&ctx->queue_mutex);
        while (!ctx->worker_stop && !ctx->queue_head) {
            pthread_cond_wait(&ctx->queue_cond, &ctx->queue_mutex);
        }
        if (ctx->worker_stop) {
            pthread_mutex_unlock(&ctx->queue_mutex);
            break;
        }
        AjmBatch *batch = ctx->queue_head;
        ctx->queue_head = batch->next;
        if (!ctx->queue_head) ctx->queue_tail = NULL;
        pthread_mutex_unlock(&ctx->queue_mutex);

        if (!batch->canceled) {
            for (size_t i = 0; i < batch->num_jobs; i++) {
                process_single_job(&batch->jobs[i]);
            }
        }

        pthread_mutex_lock(&batch->mutex);
        batch->finished = true;
        pthread_cond_broadcast(&batch->cond);
        pthread_mutex_unlock(&batch->mutex);
    }
    return NULL;
}

void shim_sceAjmInitialize(GuestContext *ctx) {
    uint64_t reserved = ctx->rdi;
    uint64_t out_context_gaddr = ctx->rsi;

    if (reserved != 0 || out_context_gaddr == 0) {
        ctx->rax = ORBIS_AJM_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    uint32_t *p_context_id = (uint32_t *)guest_to_host(ctx, out_context_gaddr);
    if (!p_context_id) {
        ctx->rax = ORBIS_AJM_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_ajm_lock);
    int slot = -1;
    for (int i = 1; i < MAX_AJM_CONTEXTS; i++) {
        if (!g_ajm_contexts[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        pthread_mutex_unlock(&g_ajm_lock);
        ctx->rax = ORBIS_AJM_ERROR_OUT_OF_RESOURCES;
        SHIM_RETURN();
    }

    AjmContext *ajm_ctx = &g_ajm_contexts[slot];
    memset(ajm_ctx, 0, sizeof(*ajm_ctx));
    ajm_ctx->in_use = true;
    ajm_ctx->id = (uint32_t)slot;
    pthread_mutex_init(&ajm_ctx->queue_mutex, NULL);
    pthread_cond_init(&ajm_ctx->queue_cond, NULL);
    pthread_create(&ajm_ctx->worker_thread, NULL, ajm_worker_thread, ajm_ctx);

    *p_context_id = (uint32_t)slot;
    pthread_mutex_unlock(&g_ajm_lock);

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmFinalize(GuestContext *ctx) {
    uint32_t context_id = (uint32_t)ctx->rdi;
    pthread_mutex_lock(&g_ajm_lock);
    if (context_id >= MAX_AJM_CONTEXTS || !g_ajm_contexts[context_id].in_use) {
        pthread_mutex_unlock(&g_ajm_lock);
        ctx->rax = ORBIS_AJM_ERROR_INVALID_CONTEXT;
        SHIM_RETURN();
    }

    AjmContext *ajm_ctx = &g_ajm_contexts[context_id];
    pthread_mutex_lock(&ajm_ctx->queue_mutex);
    ajm_ctx->worker_stop = true;
    pthread_cond_broadcast(&ajm_ctx->queue_cond);
    pthread_mutex_unlock(&ajm_ctx->queue_mutex);

    pthread_join(ajm_ctx->worker_thread, NULL);
    pthread_mutex_destroy(&ajm_ctx->queue_mutex);
    pthread_cond_destroy(&ajm_ctx->queue_cond);
    ajm_ctx->in_use = false;

    pthread_mutex_unlock(&g_ajm_lock);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmModuleRegister(GuestContext *ctx) {
    uint32_t context_id = (uint32_t)ctx->rdi;
    uint32_t codec_type = (uint32_t)ctx->rsi;
    uint64_t reserved = ctx->rdx;

    if (reserved != 0 || codec_type >= AJM_CODEC_MAX) {
        ctx->rax = ORBIS_AJM_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_ajm_lock);
    if (context_id >= MAX_AJM_CONTEXTS || !g_ajm_contexts[context_id].in_use) {
        pthread_mutex_unlock(&g_ajm_lock);
        ctx->rax = ORBIS_AJM_ERROR_INVALID_CONTEXT;
        SHIM_RETURN();
    }

    AjmContext *ajm_ctx = &g_ajm_contexts[context_id];
    if (ajm_ctx->registered_codecs[codec_type]) {
        pthread_mutex_unlock(&g_ajm_lock);
        ctx->rax = ORBIS_AJM_ERROR_CODEC_ALREADY_REGISTERED;
        SHIM_RETURN();
    }

    ajm_ctx->registered_codecs[codec_type] = true;
    pthread_mutex_unlock(&g_ajm_lock);

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmModuleUnregister(GuestContext *ctx) {
    uint32_t context_id = (uint32_t)ctx->rdi;
    uint32_t codec_type = (uint32_t)ctx->rsi;

    if (codec_type >= AJM_CODEC_MAX) {
        ctx->rax = ORBIS_AJM_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_ajm_lock);
    if (context_id >= MAX_AJM_CONTEXTS || !g_ajm_contexts[context_id].in_use) {
        pthread_mutex_unlock(&g_ajm_lock);
        ctx->rax = ORBIS_AJM_ERROR_INVALID_CONTEXT;
        SHIM_RETURN();
    }

    g_ajm_contexts[context_id].registered_codecs[codec_type] = false;
    pthread_mutex_unlock(&g_ajm_lock);

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmInstanceCreate(GuestContext *ctx) {
    uint32_t context_id = (uint32_t)ctx->rdi;
    uint32_t codec_type = (uint32_t)ctx->rsi;
    uint64_t flags = ctx->rdx;
    uint64_t out_instance_gaddr = ctx->rcx;

    if (codec_type >= AJM_CODEC_MAX || out_instance_gaddr == 0) {
        ctx->rax = ORBIS_AJM_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    uint32_t *p_out_instance = (uint32_t *)guest_to_host(ctx, out_instance_gaddr);
    if (!p_out_instance) {
        ctx->rax = ORBIS_AJM_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_ajm_lock);
    if (context_id >= MAX_AJM_CONTEXTS || !g_ajm_contexts[context_id].in_use) {
        pthread_mutex_unlock(&g_ajm_lock);
        ctx->rax = ORBIS_AJM_ERROR_INVALID_CONTEXT;
        SHIM_RETURN();
    }

    if (!g_ajm_contexts[context_id].registered_codecs[codec_type]) {
        pthread_mutex_unlock(&g_ajm_lock);
        ctx->rax = ORBIS_AJM_ERROR_CODEC_NOT_REGISTERED;
        SHIM_RETURN();
    }

    int slot = -1;
    for (int i = 1; i < MAX_AJM_INSTANCES; i++) {
        if (!g_ajm_instances[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        pthread_mutex_unlock(&g_ajm_lock);
        ctx->rax = ORBIS_AJM_ERROR_OUT_OF_RESOURCES;
        SHIM_RETURN();
    }

    AjmInstance *inst = &g_ajm_instances[slot];
    memset(inst, 0, sizeof(*inst));
    inst->in_use = true;
    inst->id = (uint32_t)slot;
    inst->context_id = context_id;
    inst->codec_type = (OrbisAjmCodecType)codec_type;
    inst->format = (OrbisAjmFormatEncoding)((flags >> 7) & 0x7);
    inst->channels = (uint32_t)((flags >> 3) & 0xF);
    inst->flags = flags;

    if (inst->codec_type == AJM_CODEC_AT9_DEC) {
        inst->at9_handle = Atrac9GetHandle();
    } else if (inst->codec_type == AJM_CODEC_MP3_DEC) {
        mp3dec_init(&inst->mp3d);
        inst->mp3_inited = true;
    }

    uint32_t full_id = (uint32_t)slot | (codec_type << 14);
    *p_out_instance = full_id;
    pthread_mutex_unlock(&g_ajm_lock);

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmInstanceDestroy(GuestContext *ctx) {
    uint32_t context_id = (uint32_t)ctx->rdi;
    uint32_t instance_id = (uint32_t)ctx->rsi;

    pthread_mutex_lock(&g_ajm_lock);
    AjmInstance *inst = get_instance_locked(instance_id);
    if (!inst || inst->context_id != context_id) {
        pthread_mutex_unlock(&g_ajm_lock);
        ctx->rax = ORBIS_AJM_ERROR_INVALID_INSTANCE;
        SHIM_RETURN();
    }

    if (inst->at9_handle) {
        Atrac9ReleaseHandle(inst->at9_handle);
        inst->at9_handle = NULL;
    }
    if (inst->pcm_buffer) {
        free(inst->pcm_buffer);
        inst->pcm_buffer = NULL;
    }
    inst->in_use = false;
    pthread_mutex_unlock(&g_ajm_lock);

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmInstanceCodecType(GuestContext *ctx) {
    uint32_t instance_id = (uint32_t)ctx->rdi;
    ctx->rax = (instance_id >> 14) & 0x1F;
    SHIM_RETURN();
}

void shim_sceAjmInstanceExtend(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmInstanceSwitch(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmMemoryRegister(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmMemoryUnregister(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmBatchJobControlBufferRa(GuestContext *ctx) {
    uint64_t buf_gaddr = ctx->rdi;
    uint32_t instance_id = (uint32_t)ctx->rsi;
    uint64_t flags = ctx->rdx;
    uint64_t sideband_in_gaddr = ctx->rcx;
    uint64_t sideband_in_sz = ctx->r8;
    uint64_t sideband_out_gaddr = ctx->r9;
    uint64_t rsp = ctx->rsp;
    uint64_t sideband_out_sz = *(uint64_t *)(ctx->mem_base + rsp + 8);
    uint64_t ret_addr = *(uint64_t *)(ctx->mem_base + rsp + 16);

    uint8_t *p_buf = (uint8_t *)guest_to_host(ctx, buf_gaddr);
    if (!p_buf) {
        ctx->rax = 0;
        SHIM_RETURN();
    }

    AjmChunkJob *job_chunk = (AjmChunkJob *)p_buf;
    job_chunk->header = make_chunk_header(AjmIdentJob, instance_id);
    uint8_t *cur = p_buf + sizeof(AjmChunkJob);

    if (ret_addr != 0) {
        AjmChunkBuffer *ra = (AjmChunkBuffer *)cur;
        ra->header = make_chunk_header(AjmIdentReturnAddressBuf, 0);
        ra->size = 0;
        ra->p_address = guest_to_host(ctx, ret_addr);
        cur += sizeof(AjmChunkBuffer);
    }
    {
        AjmChunkBuffer *in_buf = (AjmChunkBuffer *)cur;
        in_buf->header = make_chunk_header(AjmIdentInputControlBuf, 0);
        in_buf->size = (uint32_t)sideband_in_sz;
        in_buf->p_address = guest_to_host(ctx, sideband_in_gaddr);
        cur += sizeof(AjmChunkBuffer);
    }
    {
        AjmChunkFlags *fl = (AjmChunkFlags *)cur;
        fl->header = make_chunk_header(AjmIdentControlFlags, (uint32_t)(flags >> 32));
        fl->flags_low = (uint32_t)flags;
        cur += sizeof(AjmChunkFlags);
    }
    {
        AjmChunkBuffer *out_buf = (AjmChunkBuffer *)cur;
        out_buf->header = make_chunk_header(AjmIdentOutputControlBuf, 0);
        out_buf->size = (uint32_t)sideband_out_sz;
        out_buf->p_address = guest_to_host(ctx, sideband_out_gaddr);
        cur += sizeof(AjmChunkBuffer);
    }

    job_chunk->size = (uint32_t)(cur - (p_buf + sizeof(AjmChunkJob)));
    ctx->rax = (uint64_t)(cur - ctx->mem_base);
    SHIM_RETURN();
}

void shim_sceAjmBatchJobRunBufferRa(GuestContext *ctx) {
    uint64_t buf_gaddr = ctx->rdi;
    uint32_t instance_id = (uint32_t)ctx->rsi;
    uint64_t flags = ctx->rdx;
    uint64_t data_in_gaddr = ctx->rcx;
    uint64_t data_in_sz = ctx->r8;
    uint64_t data_out_gaddr = ctx->r9;
    uint64_t rsp = ctx->rsp;
    uint64_t data_out_sz = *(uint64_t *)(ctx->mem_base + rsp + 8);
    uint64_t sideband_out_gaddr = *(uint64_t *)(ctx->mem_base + rsp + 16);
    uint64_t sideband_out_sz = *(uint64_t *)(ctx->mem_base + rsp + 24);
    uint64_t ret_addr = *(uint64_t *)(ctx->mem_base + rsp + 32);

    uint8_t *p_buf = (uint8_t *)guest_to_host(ctx, buf_gaddr);
    if (!p_buf) {
        ctx->rax = 0;
        SHIM_RETURN();
    }

    AjmChunkJob *job_chunk = (AjmChunkJob *)p_buf;
    job_chunk->header = make_chunk_header(AjmIdentJob, instance_id);
    uint8_t *cur = p_buf + sizeof(AjmChunkJob);

    if (ret_addr != 0) {
        AjmChunkBuffer *ra = (AjmChunkBuffer *)cur;
        ra->header = make_chunk_header(AjmIdentReturnAddressBuf, 0);
        ra->size = 0;
        ra->p_address = guest_to_host(ctx, ret_addr);
        cur += sizeof(AjmChunkBuffer);
    }
    {
        AjmChunkBuffer *in_buf = (AjmChunkBuffer *)cur;
        in_buf->header = make_chunk_header(AjmIdentInputRunBuf, 0);
        in_buf->size = (uint32_t)data_in_sz;
        in_buf->p_address = guest_to_host(ctx, data_in_gaddr);
        cur += sizeof(AjmChunkBuffer);
    }
    {
        AjmChunkFlags *fl = (AjmChunkFlags *)cur;
        fl->header = make_chunk_header(AjmIdentRunFlags, (uint32_t)(flags >> 32));
        fl->flags_low = (uint32_t)flags;
        cur += sizeof(AjmChunkFlags);
    }
    {
        AjmChunkBuffer *out_buf = (AjmChunkBuffer *)cur;
        out_buf->header = make_chunk_header(AjmIdentOutputRunBuf, 0);
        out_buf->size = (uint32_t)data_out_sz;
        out_buf->p_address = guest_to_host(ctx, data_out_gaddr);
        cur += sizeof(AjmChunkBuffer);
    }
    if (sideband_out_gaddr != 0 && sideband_out_sz > 0) {
        AjmChunkBuffer *sb_buf = (AjmChunkBuffer *)cur;
        sb_buf->header = make_chunk_header(AjmIdentOutputControlBuf, 0);
        sb_buf->size = (uint32_t)sideband_out_sz;
        sb_buf->p_address = guest_to_host(ctx, sideband_out_gaddr);
        cur += sizeof(AjmChunkBuffer);
    }

    job_chunk->size = (uint32_t)(cur - (p_buf + sizeof(AjmChunkJob)));
    ctx->rax = (uint64_t)(cur - ctx->mem_base);
    SHIM_RETURN();
}

void shim_sceAjmBatchJobInlineBuffer(GuestContext *ctx) {
    uint64_t buf_gaddr = ctx->rdi;
    uint64_t data_in_gaddr = ctx->rsi;
    size_t data_sz = (size_t)ctx->rdx;
    uint64_t out_batch_addr_gaddr = ctx->rcx;

    uint8_t *p_buf = (uint8_t *)guest_to_host(ctx, buf_gaddr);
    const void *p_in = guest_to_host(ctx, data_in_gaddr);
    if (!p_buf) {
        ctx->rax = 0;
        SHIM_RETURN();
    }

    AjmChunkJob *job_chunk = (AjmChunkJob *)p_buf;
    job_chunk->header = make_chunk_header(AjmIdentInlineBuf, 0);
    size_t aligned_sz = (data_sz + 7) & ~7;
    job_chunk->size = (uint32_t)aligned_sz;

    uint8_t *dst = p_buf + sizeof(AjmChunkJob);
    if (out_batch_addr_gaddr) {
        uint64_t *p_out_addr = (uint64_t *)guest_to_host(ctx, out_batch_addr_gaddr);
        if (p_out_addr) *p_out_addr = (uint64_t)(dst - ctx->mem_base);
    }
    if (p_in && data_sz > 0) {
        memcpy(dst, p_in, data_sz);
    }
    ctx->rax = (uint64_t)((dst + aligned_sz) - ctx->mem_base);
    SHIM_RETURN();
}

void shim_sceAjmBatchJobRunSplitBufferRa(GuestContext *ctx) {
    shim_sceAjmBatchJobRunBufferRa(ctx);
}

void shim_sceAjmBatchStartBuffer(GuestContext *ctx) {
    uint32_t context_id = (uint32_t)ctx->rdi;
    uint64_t batch_gaddr = ctx->rsi;
    uint32_t batch_size = (uint32_t)ctx->rdx;
    int priority = (int)ctx->rcx;
    uint64_t batch_err_gaddr = ctx->r8;
    uint64_t out_batch_id_gaddr = ctx->r9;
    (void)priority;
    (void)batch_err_gaddr;

    if ((batch_size & 7) != 0 || out_batch_id_gaddr == 0) {
        ctx->rax = ORBIS_AJM_ERROR_MALFORMED_BATCH;
        SHIM_RETURN();
    }

    uint8_t *p_batch = (uint8_t *)guest_to_host(ctx, batch_gaddr);
    uint32_t *p_out_batch_id = (uint32_t *)guest_to_host(ctx, out_batch_id_gaddr);
    if (!p_batch || !p_out_batch_id) {
        ctx->rax = ORBIS_AJM_ERROR_INVALID_PARAMETER;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_ajm_lock);
    if (context_id >= MAX_AJM_CONTEXTS || !g_ajm_contexts[context_id].in_use) {
        pthread_mutex_unlock(&g_ajm_lock);
        ctx->rax = ORBIS_AJM_ERROR_INVALID_CONTEXT;
        SHIM_RETURN();
    }

    AjmContext *ajm_ctx = &g_ajm_contexts[context_id];
    int slot = -1;
    for (int i = 1; i < MAX_AJM_BATCHES; i++) {
        if (!g_ajm_batches[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        pthread_mutex_unlock(&g_ajm_lock);
        ctx->rax = ORBIS_AJM_ERROR_OUT_OF_MEMORY;
        SHIM_RETURN();
    }

    AjmBatch *b = &g_ajm_batches[slot];
    memset(b, 0, sizeof(*b));
    b->in_use = true;
    b->id = g_next_batch_id++;
    b->context_id = context_id;
    pthread_mutex_init(&b->mutex, NULL);
    pthread_cond_init(&b->cond, NULL);

    // Parse jobs from p_batch
    AjmStream stream = { .cur = p_batch, .end = p_batch + batch_size };
    while (stream_has(&stream, sizeof(AjmChunkJob))) {
        AjmChunkJob *job_chunk = (AjmChunkJob *)stream_consume(&stream, sizeof(AjmChunkJob));
        uint8_t ident = chunk_ident(job_chunk->header);
        if (ident == AjmIdentInlineBuf) {
            stream_consume(&stream, job_chunk->size);
            continue;
        }
        if (ident != AjmIdentJob) break;

        if (b->num_jobs < MAX_JOBS_PER_BATCH) {
            AjmParsedJob *pj = &b->jobs[b->num_jobs++];
            pj->instance_id = chunk_payload(job_chunk->header);

            AjmStream sub = { .cur = stream.cur, .end = stream.cur + job_chunk->size };
            while (stream_has(&sub, sizeof(AjmChunkHeader))) {
                AjmChunkHeader *h = (AjmChunkHeader *)sub.cur;
                uint8_t sub_ident = chunk_ident(*h);
                if (sub_ident == AjmIdentInputRunBuf) {
                    AjmChunkBuffer *buf = (AjmChunkBuffer *)stream_consume(&sub, sizeof(AjmChunkBuffer));
                    if (buf) { pj->input_buf = buf->p_address; pj->input_size = buf->size; }
                } else if (sub_ident == AjmIdentOutputRunBuf) {
                    AjmChunkBuffer *buf = (AjmChunkBuffer *)stream_consume(&sub, sizeof(AjmChunkBuffer));
                    if (buf) { pj->output_buf = buf->p_address; pj->output_size = buf->size; }
                } else if (sub_ident == AjmIdentInputControlBuf) {
                    AjmChunkBuffer *buf = (AjmChunkBuffer *)stream_consume(&sub, sizeof(AjmChunkBuffer));
                    if (buf) { pj->control_in_buf = buf->p_address; pj->control_in_size = buf->size; }
                } else if (sub_ident == AjmIdentOutputControlBuf) {
                    AjmChunkBuffer *buf = (AjmChunkBuffer *)stream_consume(&sub, sizeof(AjmChunkBuffer));
                    if (buf) { pj->control_out_buf = buf->p_address; pj->control_out_size = buf->size; }
                } else if (sub_ident == AjmIdentControlFlags || sub_ident == AjmIdentRunFlags) {
                    AjmChunkFlags *fl = (AjmChunkFlags *)stream_consume(&sub, sizeof(AjmChunkFlags));
                    if (fl) { pj->flags = ((uint64_t)chunk_payload(fl->header) << 32) | fl->flags_low; }
                } else if (sub_ident == AjmIdentReturnAddressBuf) {
                    AjmChunkBuffer *buf = (AjmChunkBuffer *)stream_consume(&sub, sizeof(AjmChunkBuffer));
                    if (buf) { pj->return_addr = buf->p_address; }
                } else {
                    sub.cur += 4;
                }
            }
        }
        stream_consume(&stream, job_chunk->size);
    }

    *p_out_batch_id = b->id;

    // Queue batch to worker
    pthread_mutex_lock(&ajm_ctx->queue_mutex);
    if (!ajm_ctx->queue_head) {
        ajm_ctx->queue_head = b;
        ajm_ctx->queue_tail = b;
    } else {
        ajm_ctx->queue_tail->next = b;
        ajm_ctx->queue_tail = b;
    }
    pthread_cond_signal(&ajm_ctx->queue_cond);
    pthread_mutex_unlock(&ajm_ctx->queue_mutex);

    pthread_mutex_unlock(&g_ajm_lock);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmBatchWait(GuestContext *ctx) {
    uint32_t context_id = (uint32_t)ctx->rdi;
    uint32_t batch_id = (uint32_t)ctx->rsi;
    uint32_t timeout_ms = (uint32_t)ctx->rdx;
    uint64_t batch_err_gaddr = ctx->rcx;
    (void)context_id;
    (void)batch_err_gaddr;

    pthread_mutex_lock(&g_ajm_lock);
    AjmBatch *b = NULL;
    for (int i = 1; i < MAX_AJM_BATCHES; i++) {
        if (g_ajm_batches[i].in_use && g_ajm_batches[i].id == batch_id) {
            b = &g_ajm_batches[i];
            break;
        }
    }
    if (!b) {
        pthread_mutex_unlock(&g_ajm_lock);
        ctx->rax = ORBIS_AJM_ERROR_INVALID_BATCH;
        SHIM_RETURN();
    }
    pthread_mutex_unlock(&g_ajm_lock);

    pthread_mutex_lock(&b->mutex);
    int ret = 0;
    if (timeout_ms == 0xFFFFFFFFU) {
        while (!b->finished && !b->canceled) {
            pthread_cond_wait(&b->cond, &b->mutex);
        }
    } else {
        struct timeval now;
        gettimeofday(&now, NULL);
        struct timespec ts;
        uint64_t nsec = (uint64_t)now.tv_usec * 1000 + (uint64_t)(timeout_ms % 1000) * 1000000;
        ts.tv_sec = now.tv_sec + (timeout_ms / 1000) + (nsec / 1000000000);
        ts.tv_nsec = nsec % 1000000000;

        while (!b->finished && !b->canceled) {
            if (pthread_cond_timedwait(&b->cond, &b->mutex, &ts) != 0) {
                ret = ORBIS_AJM_ERROR_IN_PROGRESS;
                break;
            }
        }
    }
    bool was_canceled = b->canceled;
    pthread_mutex_unlock(&b->mutex);

    if (ret == 0) {
        pthread_mutex_lock(&g_ajm_lock);
        pthread_mutex_destroy(&b->mutex);
        pthread_cond_destroy(&b->cond);
        b->in_use = false;
        pthread_mutex_unlock(&g_ajm_lock);
    }

    if (was_canceled) {
        ctx->rax = ORBIS_AJM_ERROR_CANCELLED;
    } else {
        ctx->rax = ret;
    }
    SHIM_RETURN();
}

void shim_sceAjmBatchCancel(GuestContext *ctx) {
    uint32_t context_id = (uint32_t)ctx->rdi;
    uint32_t batch_id = (uint32_t)ctx->rsi;
    (void)context_id;

    pthread_mutex_lock(&g_ajm_lock);
    AjmBatch *b = NULL;
    for (int i = 1; i < MAX_AJM_BATCHES; i++) {
        if (g_ajm_batches[i].in_use && g_ajm_batches[i].id == batch_id) {
            b = &g_ajm_batches[i];
            break;
        }
    }
    if (!b) {
        pthread_mutex_unlock(&g_ajm_lock);
        ctx->rax = ORBIS_AJM_ERROR_INVALID_BATCH;
        SHIM_RETURN();
    }
    pthread_mutex_lock(&b->mutex);
    b->canceled = true;
    pthread_cond_broadcast(&b->cond);
    pthread_mutex_unlock(&b->mutex);
    pthread_mutex_unlock(&g_ajm_lock);

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmBatchErrorDump(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmDecAt9ParseConfigData(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmDecMp3ParseFrame(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAjmStrError(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void ps4_ajm_destroy(void) {
    pthread_mutex_lock(&g_ajm_lock);
    for (int i = 1; i < MAX_AJM_CONTEXTS; i++) {
        if (g_ajm_contexts[i].in_use) {
            AjmContext *ajm_ctx = &g_ajm_contexts[i];
            pthread_mutex_lock(&ajm_ctx->queue_mutex);
            ajm_ctx->worker_stop = true;
            pthread_cond_broadcast(&ajm_ctx->queue_cond);
            pthread_mutex_unlock(&ajm_ctx->queue_mutex);
            pthread_join(ajm_ctx->worker_thread, NULL);
            pthread_mutex_destroy(&ajm_ctx->queue_mutex);
            pthread_cond_destroy(&ajm_ctx->queue_cond);
            ajm_ctx->in_use = false;
        }
    }
    for (int i = 1; i < MAX_AJM_INSTANCES; i++) {
        if (g_ajm_instances[i].in_use) {
            if (g_ajm_instances[i].at9_handle) {
                Atrac9ReleaseHandle(g_ajm_instances[i].at9_handle);
                g_ajm_instances[i].at9_handle = NULL;
            }
            if (g_ajm_instances[i].pcm_buffer) {
                free(g_ajm_instances[i].pcm_buffer);
                g_ajm_instances[i].pcm_buffer = NULL;
            }
            g_ajm_instances[i].in_use = false;
        }
    }
    for (int i = 1; i < MAX_AJM_BATCHES; i++) {
        if (g_ajm_batches[i].in_use) {
            pthread_mutex_destroy(&g_ajm_batches[i].mutex);
            pthread_cond_destroy(&g_ajm_batches[i].cond);
            g_ajm_batches[i].in_use = false;
        }
    }
    pthread_mutex_unlock(&g_ajm_lock);
}
