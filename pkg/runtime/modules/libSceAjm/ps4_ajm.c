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
    OrbisAjmSidebandGaplessDecode init;
    OrbisAjmSidebandGaplessDecode current;
} AjmGapless;

typedef struct {
    bool in_use;
    uint32_t id;
    uint32_t context_id;
    OrbisAjmCodecType codec_type;
    OrbisAjmFormatEncoding format;
    uint32_t channels;
    uint64_t flags;
    uint32_t codec_flags; // AjmInstanceFlags.codec (bits 32..59)
    bool is_initialized;

    uint64_t total_samples; // per-channel samples produced since last reset
    AjmGapless gapless;

    // AT9 state
    void *at9_handle;
    Atrac9CodecInfo at9_info;
    uint8_t at9_config[4];
    uint32_t superframe_bytes_remain;
    uint32_t at9_num_frames;

    // MP3 state
    mp3dec_t mp3d;
    bool mp3_inited;
    uint32_t mp3_channels;
    uint32_t mp3_hz;
    uint32_t mp3_bitrate_kbps;

    // Common PCM intermediate buffer (one decoded frame)
    uint8_t *pcm_buffer;
    size_t pcm_buffer_cap;
} AjmInstance;

#define MAX_AJM_OUTPUT_BUFS 16

typedef struct {
    uint32_t instance_id;
    uint64_t flags;
    void *input_buf;
    size_t input_size;
    void *output_bufs[MAX_AJM_OUTPUT_BUFS];
    size_t output_sizes[MAX_AJM_OUTPUT_BUFS];
    uint32_t num_outputs;
    void *control_in_buf;
    size_t control_in_size;
    void *control_out_buf;
    size_t control_out_size;
    void *return_addr;
} AjmParsedJob;

typedef struct AjmBatch {
    bool in_use;
    uint32_t id;
    uint32_t context_id;
    bool finished;
    bool canceled;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    AjmParsedJob *jobs;
    size_t num_jobs;
    size_t cap_jobs;
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

static inline size_t ajm_sample_size(OrbisAjmFormatEncoding fmt) {
    return fmt == AJM_FORMAT_S16 ? 2 : 4;
}

static uint32_t ajm_channel_mask(uint32_t channels) {
    switch (channels) {
    case 1: return 0x0004;
    case 2: return 0x0003;
    case 4: return 0x0033;
    case 6: return 0x060F;
    case 8: return 0x063F;
    default: return 0;
    }
}

static bool ajm_ensure_pcm(AjmInstance *inst, size_t bytes) {
    if (inst->pcm_buffer_cap >= bytes) return true;
    uint8_t *p = (uint8_t *)realloc(inst->pcm_buffer, bytes);
    if (!p) return false;
    inst->pcm_buffer = p;
    inst->pcm_buffer_cap = bytes;
    return true;
}

// --- sparse output writer (split output buffers) ---
typedef struct {
    AjmParsedJob *job;
    uint32_t idx;
    size_t off;
} AjmOut;

static size_t ajm_out_remaining(const AjmOut *o) {
    size_t rem = 0;
    for (uint32_t i = o->idx; i < o->job->num_outputs; i++) {
        rem += o->job->output_sizes[i] - (i == o->idx ? o->off : 0);
    }
    return rem;
}

static size_t ajm_out_write(AjmOut *o, const uint8_t *src, size_t bytes) {
    size_t written = 0;
    while (bytes > 0 && o->idx < o->job->num_outputs) {
        size_t room = o->job->output_sizes[o->idx] - o->off;
        if (room == 0) { o->idx++; o->off = 0; continue; }
        size_t n = bytes < room ? bytes : room;
        memcpy((uint8_t *)o->job->output_bufs[o->idx] + o->off, src, n);
        o->off += n; src += n; bytes -= n; written += n;
    }
    return written;
}

// --- ATRAC9 helpers ---
static bool at9_init(AjmInstance *inst, const uint8_t *cfg) {
    memcpy(inst->at9_config, cfg, 4);
    if (inst->at9_handle) {
        Atrac9ReleaseHandle(inst->at9_handle);
        inst->at9_handle = NULL;
    }
    inst->is_initialized = false;
    inst->at9_handle = Atrac9GetHandle();
    if (!inst->at9_handle) return false;
    int ret = Atrac9InitDecoder(inst->at9_handle, inst->at9_config);
    if (ret != 0) {
        fprintf(stderr, "[AJM] Atrac9InitDecoder failed: 0x%x (config %02x %02x %02x %02x)\n",
                ret, cfg[0], cfg[1], cfg[2], cfg[3]);
        return false;
    }
    Atrac9GetCodecInfo(inst->at9_handle, &inst->at9_info);
    if (inst->at9_info.channels <= 0 || inst->at9_info.frameSamples <= 0 ||
        inst->at9_info.framesInSuperframe <= 0) {
        return false;
    }
    if (!ajm_ensure_pcm(inst, (size_t)inst->at9_info.frameSamples * inst->at9_info.channels * 4)) {
        return false;
    }
    inst->superframe_bytes_remain = (uint32_t)inst->at9_info.superframeSize;
    inst->at9_num_frames = 0;
    inst->is_initialized = true;
    return true;
}

// Parse a RIFF/WAVE AT9 header; on success advances *pin past the header to the
// start of the 'data' chunk payload.
static bool at9_parse_riff(AjmInstance *inst, uint8_t **pin, size_t *prem) {
    const uint8_t *data = *pin;
    size_t size = *prem;
    if (size < 12) return false;
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) return false;

    size_t offset = 12;
    while (offset + 8 <= size) {
        uint32_t chunk_tag, chunk_len;
        memcpy(&chunk_tag, data + offset, 4);
        memcpy(&chunk_len, data + offset + 4, 4);
        offset += 8;
        if (chunk_tag == FOURCC('d','a','t','a')) {
            *pin += offset;
            *prem -= offset;
            return true;
        }
        if (offset + chunk_len > size) break;

        if (chunk_tag == FOURCC('f','m','t',' ') && chunk_len >= sizeof(WaveFormatExtensible)) {
            WaveFormatExtensible fmt;
            memcpy(&fmt, data + offset, sizeof(fmt));
            if (fmt.fmt_type == 0xFFFE && memcmp(fmt.guid, s_at9_guid, 16) == 0) {
                at9_init(inst, fmt.config_data);
            }
        } else if (chunk_tag == FOURCC('f','a','c','t') && chunk_len >= 8) {
            FactSampleData fact = {0};
            memcpy(&fact, data + offset, chunk_len >= sizeof(fact) ? sizeof(fact) : chunk_len);
            inst->gapless.init.total_samples = fact.sample_length;
            inst->gapless.init.skip_samples = (uint16_t)fact.encoder_delay;
            inst->gapless.init.skipped_samples = 0;
            inst->gapless.current = inst->gapless.init;
        }
        offset += (chunk_len + 1) & ~1u;
    }
    // No data chunk found inside this buffer: consume what we parsed.
    if (offset > size) offset = size;
    *pin += offset;
    *prem -= offset;
    return true;
}

static void ajm_instance_reset(AjmInstance *inst) {
    inst->total_samples = 0;
    memset(&inst->gapless, 0, sizeof(inst->gapless));
    if (inst->codec_type == AJM_CODEC_AT9_DEC) {
        if (inst->is_initialized) {
            uint8_t cfg[4];
            memcpy(cfg, inst->at9_config, 4);
            at9_init(inst, cfg);
        }
    } else if (inst->codec_type == AJM_CODEC_MP3_DEC) {
        mp3dec_init(&inst->mp3d);
        inst->mp3_inited = true;
    }
}

static inline bool ajm_gapless_end(const AjmInstance *inst) {
    return inst->gapless.init.total_samples != 0 && inst->gapless.current.total_samples == 0;
}

static void ajm_apply_gapless_input(AjmInstance *inst, const OrbisAjmSidebandGaplessDecode *p, bool fresh) {
    AjmGapless *g = &inst->gapless;
    if (fresh) {
        g->init.total_samples = p->total_samples;
        g->init.skip_samples = p->skip_samples;
        g->init.skipped_samples = 0;
        g->current = g->init;
        return;
    }
    if (p->total_samples != 0) {
        uint32_t mx = p->total_samples > g->init.total_samples ? p->total_samples : g->init.total_samples;
        g->current.total_samples += mx - g->init.total_samples;
        g->init.total_samples = mx;
    }
    if (p->skip_samples != 0) {
        uint16_t mx = p->skip_samples > g->init.skip_samples ? p->skip_samples : g->init.skip_samples;
        g->current.skip_samples += mx - g->init.skip_samples;
        g->init.skip_samples = mx;
    }
}

// Decode AT9 frames. Returns AJM result bits.
static int32_t at9_decode(AjmInstance *inst, AjmParsedJob *job, uint8_t **pin, size_t *prem,
                          AjmOut *out, uint32_t run_flags, bool has_mframe, uint32_t *frames_out,
                          int32_t *internal_result) {
    int32_t result = 0;
    uint32_t frames = 0;
    (void)job;

    if (*prem >= 12 && memcmp(*pin, "RIFF", 4) == 0) {
        if (at9_parse_riff(inst, pin, prem)) {
            inst->total_samples = 0;
        }
    }
    if (!inst->is_initialized || !inst->at9_handle) {
        *frames_out = 0;
        return ORBIS_AJM_RESULT_NOT_INITIALIZED;
    }

    const uint32_t ch = (uint32_t)inst->at9_info.channels;
    const uint32_t fs = (uint32_t)inst->at9_info.frameSamples;
    const size_t ss = ajm_sample_size(inst->format);
    const int nointerleave = (inst->codec_flags & AJM_AT9_FLAG_NON_INTERLEAVED) ? 1 : 0;

    while (*prem > 0 && ajm_out_remaining(out) > 0 && !ajm_gapless_end(inst)) {
        AjmGapless *g = &inst->gapless;
        // AT9 frames inside a superframe are variable length; only the superframe
        // size is fixed. Never decode from an incomplete superframe: libatrac9 would
        // zero-fill past the end, produce garbage and desync the stream. Leave the
        // remainder unconsumed so the game resubmits it with more data.
        if (*prem < inst->superframe_bytes_remain) {
            if (frames == 0) result |= ORBIS_AJM_RESULT_PARTIAL_INPUT;
            break;
        }
        uint32_t max_samples = g->init.total_samples != 0
                                   ? (g->current.total_samples < fs ? g->current.total_samples : fs)
                                   : fs;
        uint32_t skip_need = g->current.skip_samples < max_samples ? g->current.skip_samples : max_samples;
        size_t need = (size_t)(max_samples - skip_need) * ch * ss;
        if (ajm_out_remaining(out) < need) {
            if (!has_mframe || frames == 0) result |= ORBIS_AJM_RESULT_NOT_ENOUGH_ROOM;
            break;
        }

        int bytes_used = 0;
        int ret;
        int in_sz = *prem > 0x7FFFFFFF ? 0x7FFFFFFF : (int)*prem;
        if (inst->format == AJM_FORMAT_S32) {
            ret = Atrac9DecodeS32(inst->at9_handle, *pin, in_sz, (int *)inst->pcm_buffer, &bytes_used, nointerleave);
        } else if (inst->format == AJM_FORMAT_FLOAT) {
            ret = Atrac9DecodeF32(inst->at9_handle, *pin, in_sz, (float *)inst->pcm_buffer, &bytes_used, nointerleave);
        } else {
            ret = Atrac9Decode(inst->at9_handle, *pin, in_sz, (short *)inst->pcm_buffer, &bytes_used, nointerleave);
        }
        if (ret != 0) {
            result |= ORBIS_AJM_RESULT_CODEC_ERROR | ORBIS_AJM_RESULT_INVALID_DATA;
            *internal_result = ret;
            break;
        }
        if (bytes_used < 0) bytes_used = 0;
        if ((size_t)bytes_used > *prem) bytes_used = (int)*prem;
        *pin += bytes_used;
        *prem -= (size_t)bytes_used;
        inst->superframe_bytes_remain = (uint32_t)bytes_used >= inst->superframe_bytes_remain
                                            ? 0 : inst->superframe_bytes_remain - (uint32_t)bytes_used;

        uint32_t skip = g->current.skip_samples < fs ? g->current.skip_samples : fs;
        g->current.skip_samples -= (uint16_t)skip;
        uint32_t avail = fs - skip;
        uint32_t n = avail;
        if (g->init.total_samples != 0 && g->current.total_samples < n) n = g->current.total_samples;

        size_t written = ajm_out_write(out, inst->pcm_buffer + (size_t)skip * ch * ss, (size_t)n * ch * ss);
        uint32_t wrote_samples = (uint32_t)(written / (ch * ss));
        g->current.skipped_samples = (uint16_t)(g->current.skipped_samples + (fs - wrote_samples));
        if (g->init.total_samples != 0) g->current.total_samples -= wrote_samples;
        inst->total_samples += wrote_samples;
        frames++;

        inst->at9_num_frames++;
        if (inst->at9_num_frames % (uint32_t)inst->at9_info.framesInSuperframe == 0) {
            size_t pad = inst->superframe_bytes_remain;
            if (pad > *prem) pad = *prem;
            *pin += pad;
            *prem -= pad;
            inst->superframe_bytes_remain = (uint32_t)inst->at9_info.superframeSize;
            inst->at9_num_frames = 0;
        }

        if (!(run_flags & AJM_RUN_MULTIPLE_FRAMES)) break;
    }

    if ((inst->flags & AJM_INSTANCE_FLAG_GAPLESS_LOOP) && ajm_gapless_end(inst)) {
        *pin += *prem;
        *prem = 0;
        inst->gapless.current = inst->gapless.init;
        uint8_t cfg[4];
        memcpy(cfg, inst->at9_config, 4);
        at9_init(inst, cfg);
    }

    *frames_out = frames;
    return result;
}

static void s16_convert(const short *src, uint8_t *dst, size_t count, OrbisAjmFormatEncoding fmt) {
    if (fmt == AJM_FORMAT_S32) {
        int32_t *d = (int32_t *)dst;
        for (size_t i = 0; i < count; i++) d[i] = (int32_t)src[i] << 16;
    } else if (fmt == AJM_FORMAT_FLOAT) {
        float *d = (float *)dst;
        for (size_t i = 0; i < count; i++) d[i] = (float)src[i] / 32768.0f;
    } else {
        memcpy(dst, src, count * sizeof(short));
    }
}

static int32_t mp3_decode(AjmInstance *inst, uint8_t **pin, size_t *prem, AjmOut *out,
                          uint32_t run_flags, bool has_mframe, uint32_t *frames_out) {
    int32_t result = 0;
    uint32_t frames = 0;
    const size_t ss = ajm_sample_size(inst->format);
    if (!inst->mp3_inited) {
        mp3dec_init(&inst->mp3d);
        inst->mp3_inited = true;
    }
    if (!ajm_ensure_pcm(inst, MINIMP3_MAX_SAMPLES_PER_FRAME * 4)) {
        *frames_out = 0;
        return ORBIS_AJM_RESULT_FATAL;
    }
    while (*prem > 0 && ajm_out_remaining(out) > 0) {
        mp3dec_frame_info_t info;
        short pcm_frame[MINIMP3_MAX_SAMPLES_PER_FRAME];
        int in_sz = *prem > 0x7FFFFFFF ? 0x7FFFFFFF : (int)*prem;
        int samples = mp3dec_decode_frame(&inst->mp3d, *pin, in_sz, pcm_frame, &info);
        if (info.frame_bytes <= 0) break; // need more data
        size_t fb = (size_t)info.frame_bytes > *prem ? *prem : (size_t)info.frame_bytes;
        *pin += fb;
        *prem -= fb;
        if (samples <= 0) continue; // skipped junk / ID3
        inst->mp3_channels = (uint32_t)info.channels;
        inst->mp3_hz = (uint32_t)info.hz;
        inst->mp3_bitrate_kbps = (uint32_t)info.bitrate_kbps;
        size_t count = (size_t)samples * (size_t)info.channels;
        s16_convert(pcm_frame, inst->pcm_buffer, count, inst->format);
        size_t bytes = count * ss;
        if (ajm_out_remaining(out) < bytes) {
            if (!has_mframe || frames == 0) result |= ORBIS_AJM_RESULT_NOT_ENOUGH_ROOM;
        }
        size_t written = ajm_out_write(out, inst->pcm_buffer, bytes);
        inst->total_samples += written / ((size_t)info.channels * ss);
        frames++;
        if (result || !(run_flags & AJM_RUN_MULTIPLE_FRAMES)) break;
    }
    *frames_out = frames;
    return result;
}

#define SB_TAKE(T, ptr)                                   \
    do {                                                  \
        if (sb_cur && sb_cur + sizeof(T) <= sb_end) {     \
            (ptr) = (T *)sb_cur;                          \
            sb_cur += sizeof(T);                          \
        } else {                                          \
            truncated = true;                             \
        }                                                 \
    } while (0)

static void process_single_job(AjmParsedJob *job) {
    const uint32_t run_flags = AJM_JOB_RUN_FLAGS(job->flags);
    const uint32_t control_flags = AJM_JOB_CONTROL_FLAGS(job->flags);
    const uint32_t sideband_flags = AJM_JOB_SIDEBAND_FLAGS(job->flags);

    // Lay out the output sideband: result, [stream], [format], [gapless], [mframe], [codec info]
    OrbisAjmSidebandResult *p_result = NULL;
    OrbisAjmSidebandStream *p_stream = NULL;
    OrbisAjmSidebandFormat *p_format = NULL;
    OrbisAjmSidebandGaplessDecode *p_gapless = NULL;
    OrbisAjmSidebandMFrame *p_mframe = NULL;
    uint8_t *p_codec_info = NULL;
    size_t codec_info_room = 0;
    bool truncated = false;

    uint8_t *sb_cur = (uint8_t *)job->control_out_buf;
    uint8_t *sb_end = sb_cur ? sb_cur + job->control_out_size : NULL;
    if (sb_cur) {
        SB_TAKE(OrbisAjmSidebandResult, p_result);
        if (p_result) memset(p_result, 0, sizeof(*p_result));
        if (sideband_flags & AJM_SIDEBAND_STREAM) {
            SB_TAKE(OrbisAjmSidebandStream, p_stream);
            if (p_stream) memset(p_stream, 0, sizeof(*p_stream));
        }
        if (sideband_flags & AJM_SIDEBAND_FORMAT) SB_TAKE(OrbisAjmSidebandFormat, p_format);
        if (sideband_flags & AJM_SIDEBAND_GAPLESS) SB_TAKE(OrbisAjmSidebandGaplessDecode, p_gapless);
        if (run_flags & AJM_RUN_MULTIPLE_FRAMES) {
            SB_TAKE(OrbisAjmSidebandMFrame, p_mframe);
            if (p_mframe) memset(p_mframe, 0, sizeof(*p_mframe));
        }
        if (run_flags & AJM_RUN_GET_CODEC_INFO) {
            p_codec_info = sb_cur;
            codec_info_room = (size_t)(sb_end - sb_cur);
        }
    }

    if (job->instance_id == AJM_INSTANCE_STATISTICS) {
        // Statistics sidebands are not modelled; result is already zeroed.
        return;
    }

    pthread_mutex_lock(&g_ajm_lock);
    AjmInstance *inst = get_instance_locked(job->instance_id);
    if (!inst) {
        pthread_mutex_unlock(&g_ajm_lock);
        if (p_result) p_result->result = ORBIS_AJM_RESULT_INVALID_PARAM | ORBIS_AJM_RESULT_FATAL;
        return;
    }

    int32_t result_code = 0;
    int32_t internal_result = 0;

    // Control flags
    if (control_flags & AJM_CONTROL_RESET) {
        ajm_instance_reset(inst);
    }
    // Input control sideband: [init params], [format], [gapless]
    {
        uint8_t *in_cur = (uint8_t *)job->control_in_buf;
        uint8_t *in_end = in_cur ? in_cur + job->control_in_size : NULL;
        if (in_cur && (control_flags & AJM_CONTROL_INITIALIZE)) {
            size_t rem = (size_t)(in_end - in_cur);
            if (inst->codec_type == AJM_CODEC_AT9_DEC) {
                if (rem >= 4) {
                    if (!at9_init(inst, in_cur)) {
                        result_code |= ORBIS_AJM_RESULT_INVALID_PARAM;
                    }
                } else {
                    result_code |= ORBIS_AJM_RESULT_INVALID_PARAM;
                }
            } else if (inst->codec_type == AJM_CODEC_MP3_DEC) {
                mp3dec_init(&inst->mp3d);
                inst->mp3_inited = true;
                inst->is_initialized = true;
            }
            // AjmDecAt9InitializeParameters / generic init params are 8 bytes
            in_cur += rem < 8 ? rem : 8;
        }
        if (in_cur && (sideband_flags & AJM_SIDEBAND_FORMAT) &&
            in_cur + sizeof(OrbisAjmSidebandFormat) <= in_end) {
            in_cur += sizeof(OrbisAjmSidebandFormat); // output format request: not modelled
        }
        if (in_cur && (sideband_flags & AJM_SIDEBAND_GAPLESS) &&
            in_cur + sizeof(OrbisAjmSidebandGaplessDecode) <= in_end) {
            OrbisAjmSidebandGaplessDecode gp;
            memcpy(&gp, in_cur, sizeof(gp));
            ajm_apply_gapless_input(inst, &gp, (control_flags & AJM_CONTROL_RESET) != 0);
            in_cur += sizeof(gp);
        }
    }

    // Run data
    if (job->input_buf && job->input_size > 0 && job->num_outputs > 0) {
        uint8_t *in_data = (uint8_t *)job->input_buf;
        size_t in_rem = job->input_size;
        AjmOut out = { .job = job, .idx = 0, .off = 0 };
        size_t out_total = ajm_out_remaining(&out);
        uint32_t frames = 0;

        if (inst->codec_type == AJM_CODEC_AT9_DEC) {
            result_code |= at9_decode(inst, job, &in_data, &in_rem, &out, run_flags, p_mframe != NULL,
                                      &frames, &internal_result);
        } else if (inst->codec_type == AJM_CODEC_MP3_DEC) {
            result_code |= mp3_decode(inst, &in_data, &in_rem, &out, run_flags, p_mframe != NULL, &frames);
        }

        if (p_mframe) p_mframe->num_frames = frames;
        if (p_stream) {
            p_stream->input_consumed = (int32_t)(job->input_size - in_rem);
            p_stream->output_written = (int32_t)(out_total - ajm_out_remaining(&out));
            p_stream->total_decoded_samples = inst->total_samples;
        }
    } else if (p_stream) {
        p_stream->total_decoded_samples = inst->total_samples;
    }

    if (p_format) {
        memset(p_format, 0, sizeof(*p_format));
        if (inst->codec_type == AJM_CODEC_AT9_DEC && inst->is_initialized) {
            const Atrac9CodecInfo *ci = &inst->at9_info;
            p_format->num_channels = (uint32_t)ci->channels;
            p_format->channel_mask = ajm_channel_mask((uint32_t)ci->channels);
            p_format->sample_freq = (uint32_t)ci->samplingRate;
            p_format->sample_encoding = inst->format;
            uint64_t denom = (uint64_t)ci->framesInSuperframe * (uint64_t)ci->frameSamples;
            p_format->bitrate = denom ? (uint32_t)(((uint64_t)ci->samplingRate * ci->superframeSize * 8) / denom) : 0;
        } else if (inst->codec_type == AJM_CODEC_MP3_DEC) {
            p_format->num_channels = inst->mp3_channels;
            p_format->channel_mask = ajm_channel_mask(inst->mp3_channels);
            p_format->sample_freq = inst->mp3_hz;
            p_format->sample_encoding = inst->format;
            p_format->bitrate = inst->mp3_bitrate_kbps * 1000;
        }
    }
    if (p_gapless) {
        *p_gapless = inst->gapless.current;
    }
    if (p_codec_info) {
        if (inst->codec_type == AJM_CODEC_AT9_DEC) {
            if (codec_info_room >= sizeof(OrbisAjmSidebandDecAt9CodecInfo)) {
                OrbisAjmSidebandDecAt9CodecInfo info;
                info.super_frame_size = (uint32_t)inst->at9_info.superframeSize;
                info.frames_in_super_frame = (uint32_t)inst->at9_info.framesInSuperframe;
                info.next_frame_size = inst->superframe_bytes_remain;
                info.frame_samples = (uint32_t)inst->at9_info.frameSamples;
                memcpy(p_codec_info, &info, sizeof(info));
            } else {
                truncated = true;
            }
        }
    }
    pthread_mutex_unlock(&g_ajm_lock);

    if (truncated) {
        // shadPS4 never reports SIDEBAND_TRUNC; we only write the sections that fit.
        // Keep a rate-limited diagnostic so a layout mismatch can still be spotted.
        static int s_trunc_logs = 0;
        if (s_trunc_logs < 16) {
            s_trunc_logs++;
            fprintf(stderr,
                    "[AJM] sideband truncated: inst=%x flags=%016llx run=%x ctrl=%x sb=%x out_sb_size=%zu\n",
                    job->instance_id, (unsigned long long)job->flags, run_flags, control_flags,
                    sideband_flags, job->control_out_size);
        }
    }
    if (p_result) {
        p_result->result = result_code;
        p_result->internal_result = internal_result;
    }
}

#undef SB_TAKE


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
    inst->codec_flags = (uint32_t)((flags >> 32) & 0x0FFFFFFF);

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
    {
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

// void *sceAjmBatchJobRunSplitBufferRa(void *p_buffer, u32 instance_id, u64 flags,
//     const AjmBuffer *p_data_input_buffers, u64 num_data_input_buffers,
//     const AjmBuffer *p_data_output_buffers, u64 num_data_output_buffers,
//     void *p_sideband_output, u64 sideband_output_size, const void *p_return_address)
void shim_sceAjmBatchJobRunSplitBufferRa(GuestContext *ctx) {
    uint64_t buf_gaddr = ctx->rdi;
    uint32_t instance_id = (uint32_t)ctx->rsi;
    uint64_t flags = ctx->rdx;
    uint64_t in_bufs_gaddr = ctx->rcx;
    uint64_t num_in = ctx->r8;
    uint64_t out_bufs_gaddr = ctx->r9;
    uint64_t rsp = ctx->rsp;
    uint64_t num_out = *(uint64_t *)(ctx->mem_base + rsp + 8);
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
    // Guest AjmBuffer = { u8 *p_address (guest); u64 size; }
    const uint64_t *in_arr = (const uint64_t *)guest_to_host(ctx, in_bufs_gaddr);
    for (uint64_t i = 0; in_arr && i < num_in; i++) {
        AjmChunkBuffer *in_buf = (AjmChunkBuffer *)cur;
        in_buf->header = make_chunk_header(AjmIdentInputRunBuf, 0);
        in_buf->size = (uint32_t)in_arr[i * 2 + 1];
        in_buf->p_address = guest_to_host(ctx, in_arr[i * 2]);
        cur += sizeof(AjmChunkBuffer);
    }
    {
        AjmChunkFlags *fl = (AjmChunkFlags *)cur;
        fl->header = make_chunk_header(AjmIdentRunFlags, (uint32_t)(flags >> 32));
        fl->flags_low = (uint32_t)flags;
        cur += sizeof(AjmChunkFlags);
    }
    const uint64_t *out_arr = (const uint64_t *)guest_to_host(ctx, out_bufs_gaddr);
    for (uint64_t i = 0; out_arr && i < num_out; i++) {
        AjmChunkBuffer *out_buf = (AjmChunkBuffer *)cur;
        out_buf->header = make_chunk_header(AjmIdentOutputRunBuf, 0);
        out_buf->size = (uint32_t)out_arr[i * 2 + 1];
        out_buf->p_address = guest_to_host(ctx, out_arr[i * 2]);
        cur += sizeof(AjmChunkBuffer);
    }
    {
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
    AjmParsedJob *saved_jobs = b->jobs;
    size_t saved_cap = b->cap_jobs;
    memset(b, 0, sizeof(*b));
    b->jobs = saved_jobs;
    b->cap_jobs = saved_cap;
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

        if (b->num_jobs == b->cap_jobs) {
            size_t ncap = b->cap_jobs ? b->cap_jobs * 2 : 32;
            AjmParsedJob *nj = (AjmParsedJob *)realloc(b->jobs, ncap * sizeof(AjmParsedJob));
            if (nj) {
                b->jobs = nj;
                b->cap_jobs = ncap;
            }
        }
        if (b->num_jobs < b->cap_jobs) {
            AjmParsedJob *pj = &b->jobs[b->num_jobs++];
            memset(pj, 0, sizeof(*pj));
            pj->instance_id = chunk_payload(job_chunk->header);

            AjmStream sub = { .cur = stream.cur, .end = stream.cur + job_chunk->size };
            while (stream_has(&sub, sizeof(AjmChunkHeader))) {
                AjmChunkHeader *h = (AjmChunkHeader *)sub.cur;
                uint8_t sub_ident = chunk_ident(*h);
                if (sub_ident == AjmIdentInputRunBuf) {
                    AjmChunkBuffer *buf = (AjmChunkBuffer *)stream_consume(&sub, sizeof(AjmChunkBuffer));
                    if (buf && buf->p_address && buf->size > 0 && !pj->input_buf) {
                        pj->input_buf = buf->p_address;
                        pj->input_size = buf->size;
                    }
                } else if (sub_ident == AjmIdentOutputRunBuf) {
                    AjmChunkBuffer *buf = (AjmChunkBuffer *)stream_consume(&sub, sizeof(AjmChunkBuffer));
                    if (buf && buf->p_address && buf->size > 0 && pj->num_outputs < MAX_AJM_OUTPUT_BUFS) {
                        pj->output_bufs[pj->num_outputs] = buf->p_address;
                        pj->output_sizes[pj->num_outputs] = buf->size;
                        pj->num_outputs++;
                    }
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
        while (!b->finished) {
            pthread_cond_wait(&b->cond, &b->mutex);
        }
    } else {
        struct timeval now;
        gettimeofday(&now, NULL);
        struct timespec ts;
        uint64_t nsec = (uint64_t)now.tv_usec * 1000 + (uint64_t)(timeout_ms % 1000) * 1000000;
        ts.tv_sec = now.tv_sec + (timeout_ms / 1000) + (nsec / 1000000000);
        ts.tv_nsec = nsec % 1000000000;

        while (!b->finished) {
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
        if (g_ajm_batches[i].jobs) {
            free(g_ajm_batches[i].jobs);
            g_ajm_batches[i].jobs = NULL;
            g_ajm_batches[i].cap_jobs = 0;
        }
    }
    pthread_mutex_unlock(&g_ajm_lock);
}
