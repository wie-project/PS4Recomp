// libSceVideodec (H.264/AVC) on top of the shared VideoToolbox core (ps4_h264_vt.c).
//
// The guest feeds Annex-B access units; the core decodes them in display order (the movies use
// B-frames). Decoded pictures are queued and handed out one per Decode/Flush call as linear
// NV12 (pitch = align(width, 64), height = align(height, 16)), matching what shadPS4 returns.
// Struct layouts follow shadPS4 videodec.
//
// Each decoder counts its input and output (Ps4VideodecStats, ps4_videodec.h) and prints the
// counts when it is deleted; PS4_VIDEODEC_LOG=1 adds a trace.
//
// PS4_VIDEODEC_DUMP=<dir>:<i> writes picture i (0-based, in the order handed to the guest) of
// every decoder to <dir>/dec<n>_pic<i>.nv12, n being the decoder's number in the process: a
// 16-byte little-endian header (u16 width, u16 height, u32 pitch, i64 pts) and the NV12 bytes
// the guest frame buffer received (luma pitch x align(height, 16), then chroma). The log line
// gives the time (mono), to find the screenshot of the flip that shows it.
#include "recomp_runtime.h"
#include "ps4_h264_vt.h"
#include "ps4_videodec.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VIDEODEC_ERROR_API_FAIL          ((int32_t)0x80C10000)
#define VIDEODEC_ERROR_HANDLE            ((int32_t)0x80C10003)
#define VIDEODEC_ERROR_ARGUMENT_POINTER  ((int32_t)0x80C1000F)
#define VIDEODEC_ERROR_AU_POINTER        ((int32_t)0x80C1000A)
#define VIDEODEC_ERROR_AU_SIZE           ((int32_t)0x80C1000B)
#define VIDEODEC_ERROR_FRAME_BUFFER_SIZE ((int32_t)0x80C10008)

typedef struct VideodecConfigInfo {
    uint64_t thisSize;
    uint32_t codecType;
    uint32_t profile;
    uint32_t maxLevel;
    int32_t maxFrameWidth;
    int32_t maxFrameHeight;
    int32_t maxDpbFrameCount;
    uint64_t videodecFlags;
} VideodecConfigInfo;

typedef struct VideodecResourceInfo {
    uint64_t thisSize;
    uint64_t cpuMemorySize;
    uint64_t pCpuMemory;
    uint64_t cpuGpuMemorySize;
    uint64_t pCpuGpuMemory;
    uint64_t maxFrameBufferSize;
    uint32_t frameBufferAlignment;
    uint32_t reserved0;
} VideodecResourceInfo;

typedef struct VideodecCtrl {
    uint64_t thisSize;
    uint64_t handle;
    uint64_t version;
} VideodecCtrl;

typedef struct VideodecFrameBuffer {
    uint64_t thisSize;
    uint64_t pFrameBuffer;
    uint64_t frameBufferSize;
} VideodecFrameBuffer;

typedef struct VideodecInputData {
    uint64_t thisSize;
    uint64_t pAuData;
    uint64_t auSize;
    uint64_t ptsData;
    uint64_t dtsData;
    uint64_t attachedData;
} VideodecInputData;

typedef struct VideodecAvcInfo {
    uint32_t numUnitsInTick;
    uint32_t timeScale;
    uint8_t fixedFrameRateFlag;
    uint8_t aspectRatioIdc;
    uint16_t sarWidth;
    uint16_t sarHeight;
    uint8_t colourPrimaries;
    uint8_t transferCharacteristics;
    uint8_t matrixCoefficients;
    uint8_t videoFullRangeFlag;
    uint32_t frameCropLeftOffset;
    uint32_t frameCropRightOffset;
    uint32_t frameCropTopOffset;
    uint32_t frameCropBottomOffset;
} VideodecAvcInfo;

typedef struct VideodecPictureInfo {
    uint64_t thisSize;
    uint32_t isValid;
    uint32_t codecType;
    uint32_t frameWidth;
    uint32_t framePitch;
    uint32_t frameHeight;
    uint32_t isErrorPic;
    uint64_t ptsData;
    uint64_t attachedData;
    union {
        uint8_t reserved[64];
        VideodecAvcInfo avc;
    } codec;
} VideodecPictureInfo;

#define VIDEODEC_MEM_SIZE 0x10000ULL

void ps4_gnm_notify_cpu_write(uint64_t addr, uint64_t size) __attribute__((weak));

static inline uint32_t align_up(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

static void videodec_mark_no_picture(GuestContext *ctx, uint64_t pic_info) {
    if (pic_info) ((VideodecPictureInfo *)(ctx->mem_base + pic_info))->isValid = 0;
}

typedef struct Vdec {
    uint32_t magic;
    H264VtDecoder *core;
    uint32_t ordinal; // the decoder's number in the process, from 1
    bool trace;       // PS4_VIDEODEC_LOG=1 when the decoder was created
    double next_report;
    // Written by the guest thread that calls Decode/Flush; read by ps4_videodec_stats and the
    // summary. The core's own lock is private to it, so the counters have their own.
    pthread_mutex_t stats_lock;
    Ps4VideodecStats st; // min_fb_size is UINT64_MAX until a buffer was passed
    int64_t dump_index;  // PS4_VIDEODEC_DUMP picture, -1 for none
    char dump_dir[512];
} Vdec;

#define VDEC_MAGIC 0x56444543u /* "VDEC" */
#define VDEC_TRACE_AUS 8       // access units traced per decoder
#define VDEC_REPORT_SEC 2.0    // period of the progress line

static _Atomic uint32_t g_vdec_ordinal;

static double vdec_mono(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void vdec_snapshot(Vdec *d, Ps4VideodecStats *out) {
    pthread_mutex_lock(&d->stats_lock);
    *out = d->st;
    pthread_mutex_unlock(&d->stats_lock);
    if (out->min_fb_size == UINT64_MAX) out->min_fb_size = 0;
}

static void vdec_note_fb(Vdec *d, uint64_t size) {
    pthread_mutex_lock(&d->stats_lock);
    if (size < d->st.min_fb_size) d->st.min_fb_size = size;
    pthread_mutex_unlock(&d->stats_lock);
}

static void vdec_note_need(Vdec *d, uint64_t need) {
    pthread_mutex_lock(&d->stats_lock);
    if (need > d->st.need_fb_size) d->st.need_fb_size = need;
    pthread_mutex_unlock(&d->stats_lock);
}

// PS4_VIDEODEC_DUMP=<dir>:<i>, read when a decoder is created.
static void vdec_dump_config(Vdec *d) {
    d->dump_index = -1;
    const char *v = getenv("PS4_VIDEODEC_DUMP");
    if (!v || !*v) return;
    const char *colon = strrchr(v, ':');
    char *end = NULL;
    const long long i = colon ? strtoll(colon + 1, &end, 10) : -1;
    if (!colon || colon == v || (size_t)(colon - v) >= sizeof(d->dump_dir) || end == colon + 1 || *end || i < 0) {
        fprintf(stderr, "[libSceVideodec] PS4_VIDEODEC_DUMP=%s ignored: expected <dir>:<picture index>\n", v);
        return;
    }
    memcpy(d->dump_dir, v, (size_t)(colon - v));
    d->dump_dir[colon - v] = 0;
    d->dump_index = i;
}

static void put_le(uint8_t *p, uint64_t v, int n) {
    for (int k = 0; k < n; k++) p[k] = (uint8_t)(v >> (8 * k));
}

// Writes the picture the guest frame buffer just received (see PS4_VIDEODEC_DUMP above).
static void vdec_dump(const Vdec *d, uint64_t index, const uint8_t *nv12, uint64_t size, uint32_t w, uint32_t h,
                      uint32_t pitch, int64_t pts) {
    char path[sizeof(d->dump_dir) + 48];
    snprintf(path, sizeof(path), "%s/dec%u_pic%llu.nv12", d->dump_dir, d->ordinal, (unsigned long long)index);
    uint8_t hdr[16];
    put_le(hdr, w, 2);
    put_le(hdr + 2, h, 2);
    put_le(hdr + 4, pitch, 4);
    put_le(hdr + 8, (uint64_t)pts, 8);
    FILE *f = fopen(path, "wb");
    const bool ok = f && fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr) && fwrite(nv12, 1, size, f) == size;
    if (f && fclose(f) != 0) f = NULL;
    if (!ok || !f) {
        fprintf(stderr, "[libSceVideodec] could not write %s\n", path);
        return;
    }
    fprintf(stderr, "[libSceVideodec] decoder %u picture %llu (%ux%u pitch %u, pts %lld) dumped to %s at mono %.3f\n",
            d->ordinal, (unsigned long long)index, w, h, pitch, (long long)pts, path, vdec_mono());
}

// A picture handed to the guest. Returns its index.
static uint64_t vdec_note_picture(Vdec *d, int64_t pts, bool flush) {
    const double now = vdec_mono();
    pthread_mutex_lock(&d->stats_lock);
    Ps4VideodecStats *s = &d->st;
    const uint64_t i = s->pictures++;
    if (i == 0) {
        s->first_picture_mono = now;
        s->first_pts = pts;
    }
    s->last_picture_mono = now;
    s->last_pts = pts;
    if (flush) s->flush_pictures++;
    pthread_mutex_unlock(&d->stats_lock);
    return i;
}

// An access unit passed to Decode. Returns its index.
static uint64_t vdec_note_au(Vdec *d, const H264AuCensus *c) {
    pthread_mutex_lock(&d->stats_lock);
    Ps4VideodecStats *s = &d->st;
    const uint64_t i = s->aus++;
    s->slices += c->slices;
    if (c->slices > s->max_slices_per_au) s->max_slices_per_au = c->slices;
    if (c->first_mb_first_slice > 0) s->mid_picture_aus++;
    pthread_mutex_unlock(&d->stats_lock);
    return i;
}

int ps4_videodec_stats(const void *mem_base, uint64_t ctrl_addr, Ps4VideodecStats *out) {
    if (!mem_base || !ctrl_addr || !out) return -1;
    const VideodecCtrl *c = (const VideodecCtrl *)((const uint8_t *)mem_base + ctrl_addr);
    Vdec *d = (Vdec *)(uintptr_t)c->handle;
    if (!d || d->magic != VDEC_MAGIC) return -1;
    vdec_snapshot(d, out);
    return 0;
}

// Copy a decoded picture into the guest frame buffer and fill the picture info.
static int32_t vdec_emit(GuestContext *ctx, Vdec *d, const H264VtFrame *f, uint64_t fb_addr, uint64_t pic_addr,
                         bool flush) {
    VideodecFrameBuffer *fb = (VideodecFrameBuffer *)(ctx->mem_base + fb_addr);
    VideodecPictureInfo *pic = pic_addr ? (VideodecPictureInfo *)(ctx->mem_base + pic_addr) : NULL;
    const uint32_t w = f->width;
    const uint32_t h = f->height;
    const uint32_t pitch = h264vt_pitch(w);
    const uint32_t ah = h264vt_aligned_height(h);
    const uint64_t need = h264vt_nv12_size(w, h);
    vdec_note_need(d, need);
    if (!fb->pFrameBuffer || fb->frameBufferSize < need) {
        fprintf(stderr, "[libSceVideodec] frame buffer too small (0x%llx < 0x%llx)\n",
                (unsigned long long)fb->frameBufferSize, (unsigned long long)need);
        pthread_mutex_lock(&d->stats_lock);
        d->st.refused_pictures++;
        pthread_mutex_unlock(&d->stats_lock);
        return VIDEODEC_ERROR_FRAME_BUFFER_SIZE;
    }
    h264vt_copy_nv12(f, ctx->mem_base + fb->pFrameBuffer);
    if (ps4_gnm_notify_cpu_write) ps4_gnm_notify_cpu_write(fb->pFrameBuffer, need);
    if (pic) {
        memset(&pic->codec, 0, sizeof(pic->codec));
        pic->isValid = 1;
        pic->codecType = 0;
        pic->frameWidth = align_up(w, 16);
        pic->framePitch = pitch;
        pic->frameHeight = ah;
        pic->isErrorPic = 0;
        pic->ptsData = (uint64_t)f->pts;
        pic->attachedData = f->attached;
        pic->codec.avc.colourPrimaries = 1;
        pic->codec.avc.transferCharacteristics = 1;
        pic->codec.avc.matrixCoefficients = 1;
        pic->codec.avc.frameCropRightOffset = pitch - w;
        pic->codec.avc.frameCropBottomOffset = ah - h;
    }
    const uint64_t index = vdec_note_picture(d, f->pts, flush);
    if (index == 0) fprintf(stderr, "[libSceVideodec] first picture decoded\n");
    if ((int64_t)index == d->dump_index)
        vdec_dump(d, index, ctx->mem_base + fb->pFrameBuffer, need, w, h, pitch, f->pts);
    return 0;
}

// Next decoded picture. Error pictures (access units that could not be decoded) are dropped:
// this API has no picture for them, as before the shared core.
static H264VtFrame *videodec_pop(Vdec *d) {
    H264VtFrame *f;
    while ((f = h264vt_pop(d->core)) != NULL && f->error) {
        h264vt_frame_free(f);
        pthread_mutex_lock(&d->stats_lock);
        d->st.error_pictures++;
        pthread_mutex_unlock(&d->stats_lock);
    }
    return f;
}

static Vdec *vdec_from_ctrl(GuestContext *ctx, uint64_t ctrl_addr) {
    if (!ctrl_addr) return NULL;
    Vdec *d = (Vdec *)(uintptr_t)((VideodecCtrl *)(ctx->mem_base + ctrl_addr))->handle;
    return (d && d->magic == VDEC_MAGIC) ? d : NULL;
}

// int sceVideodecQueryResourceInfo(const SceVideodecConfigInfo *cfg, SceVideodecResourceInfo *rsrc)
void shim_sceVideodecQueryResourceInfo(GuestContext *ctx) {
    if (!ctx->rdi || !ctx->rsi) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    const VideodecConfigInfo *cfg = (const VideodecConfigInfo *)(ctx->mem_base + ctx->rdi);
    VideodecResourceInfo *ri = (VideodecResourceInfo *)(ctx->mem_base + ctx->rsi);
    uint64_t w = cfg->maxFrameWidth > 0 ? (uint64_t)cfg->maxFrameWidth : 1920;
    uint64_t h = cfg->maxFrameHeight > 0 ? (uint64_t)cfg->maxFrameHeight : 1088;
    uint64_t pitch = (w + 255) & ~255ULL;
    uint64_t fb = (pitch * ((h + 15) & ~15ULL) * 3) / 2; // NV12
    ri->thisSize = sizeof(*ri);
    ri->cpuMemorySize = VIDEODEC_MEM_SIZE;
    ri->pCpuMemory = 0;
    ri->cpuGpuMemorySize = VIDEODEC_MEM_SIZE;
    ri->pCpuGpuMemory = 0;
    ri->maxFrameBufferSize = (fb + 0xFFFF) & ~0xFFFFULL;
    ri->frameBufferAlignment = 0x100;
    ri->reserved0 = 0;
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVideodecCreateDecoder(const SceVideodecConfigInfo *cfg, const SceVideodecResourceInfo *rsrc,
//                              SceVideodecCtrl *ctrl)
void shim_sceVideodecCreateDecoder(GuestContext *ctx) {
    if (!ctx->rdi || !ctx->rsi || !ctx->rdx) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    VideodecCtrl *c = (VideodecCtrl *)(ctx->mem_base + ctx->rdx);
    Vdec *d = (Vdec *)calloc(1, sizeof(Vdec));
    if (d) d->core = h264vt_create("libSceVideodec");
    if (!d || !d->core) {
        free(d);
        ctx->rax = (uint64_t)(int64_t)VIDEODEC_ERROR_API_FAIL;
        SHIM_RETURN();
    }
    pthread_mutex_init(&d->stats_lock, NULL);
    d->st.first_pts = d->st.last_pts = -1;
    d->st.min_fb_size = UINT64_MAX;
    d->ordinal = atomic_fetch_add(&g_vdec_ordinal, 1) + 1;
    const char *log = getenv("PS4_VIDEODEC_LOG");
    d->trace = log && log[0] == '1';
    d->next_report = vdec_mono() + VDEC_REPORT_SEC;
    vdec_dump_config(d);
    if (d->trace) {
        const VideodecConfigInfo *cfg = (const VideodecConfigInfo *)(ctx->mem_base + ctx->rdi);
        const VideodecResourceInfo *ri = (const VideodecResourceInfo *)(ctx->mem_base + ctx->rsi);
        fprintf(stderr,
                "[libSceVideodec] decoder %u created: codec %u profile 0x%02x level 0x%02x max %dx%d dpb %d -> rsrc cpu "
                "0x%llx cpuGpu 0x%llx maxFrameBuffer 0x%llx align 0x%x\n",
                d->ordinal, cfg->codecType, cfg->profile, cfg->maxLevel, cfg->maxFrameWidth, cfg->maxFrameHeight,
                cfg->maxDpbFrameCount, (unsigned long long)ri->cpuMemorySize, (unsigned long long)ri->cpuGpuMemorySize,
                (unsigned long long)ri->maxFrameBufferSize, ri->frameBufferAlignment);
    }
    d->magic = VDEC_MAGIC;
    c->handle = (uint64_t)(uintptr_t)d;
    c->version = 0;
    ctx->rax = 0;
    SHIM_RETURN();
}

// PS4_VIDEODEC_LOG=1: the first access units of a decoder, and a progress line every 2 s
// (checked on the Decode call itself, no thread).
static void vdec_trace(Vdec *d, const H264AuCensus *c, uint64_t au_index, const VideodecInputData *in) {
    if (au_index < VDEC_TRACE_AUS) {
        char types[32 * 3 + 8];
        size_t o = 0;
        const uint32_t shown = c->nal_count < 32 ? c->nal_count : 32;
        for (uint32_t k = 0; k < shown; k++)
            o += (size_t)snprintf(types + o, sizeof(types) - o, "%s%u", k ? " " : "", c->nal_types[k]);
        if (c->nal_count > shown) snprintf(types + o, sizeof(types) - o, " ...");
        fprintf(stderr,
                "[libSceVideodec] decoder %u au %llu: %llu bytes, NAL [%s], slices %u, first_mb %d, pts %lld dts %lld\n",
                d->ordinal, (unsigned long long)au_index, (unsigned long long)in->auSize, types, c->slices,
                c->first_mb_first_slice, (long long)in->ptsData, (long long)in->dtsData);
    }
    const double now = vdec_mono();
    if (now < d->next_report) return;
    d->next_report = now + VDEC_REPORT_SEC;
    Ps4VideodecStats s;
    vdec_snapshot(d, &s);
    fprintf(stderr, "[libSceVideodec] decoder %u at mono %.3f: AUs %llu, pictures %llu, last pts %lld\n", d->ordinal, now,
            (unsigned long long)s.aus, (unsigned long long)s.pictures, (long long)s.last_pts);
}

// int sceVideodecDecode(SceVideodecCtrl *ctrl, const SceVideodecInputData *in,
//                       SceVideodecFrameBuffer *fb, SceVideodecPictureInfo *pic)
void shim_sceVideodecDecode(GuestContext *ctx) {
    videodec_mark_no_picture(ctx, ctx->rcx);
    Vdec *d = vdec_from_ctrl(ctx, ctx->rdi);
    if (!d) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC_ERROR_HANDLE;
        SHIM_RETURN();
    }
    if (!ctx->rsi || !ctx->rdx) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    const VideodecInputData *in = (const VideodecInputData *)(ctx->mem_base + ctx->rsi);
    if (!in->pAuData) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC_ERROR_AU_POINTER;
        SHIM_RETURN();
    }
    if (in->auSize == 0) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC_ERROR_AU_SIZE;
        SHIM_RETURN();
    }
    const uint8_t *au = ctx->mem_base + in->pAuData;
    H264AuCensus census;
    h264_au_census(au, (size_t)in->auSize, &census);
    const uint64_t au_index = vdec_note_au(d, &census);
    vdec_note_fb(d, ((const VideodecFrameBuffer *)(ctx->mem_base + ctx->rdx))->frameBufferSize);
    if (d->trace) vdec_trace(d, &census, au_index, in);
    // A failed access unit is logged by the core; like before the refactor, Decode still hands
    // out a picture decoded earlier and reports success.
    h264vt_decode(d->core, au, (size_t)in->auSize, (int64_t)in->ptsData, (int64_t)in->dtsData, in->attachedData,
                  NULL);
    int32_t rc = 0;
    H264VtFrame *f = videodec_pop(d);
    if (f) {
        rc = vdec_emit(ctx, d, f, ctx->rdx, ctx->rcx, false);
        h264vt_frame_free(f);
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

// int sceVideodecFlush(SceVideodecCtrl *ctrl, SceVideodecFrameBuffer *fb, SceVideodecPictureInfo *pic)
// Drains delayed pictures, one per call.
void shim_sceVideodecFlush(GuestContext *ctx) {
    if (!ctx->rdi) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    videodec_mark_no_picture(ctx, ctx->rdx);
    int32_t rc = 0;
    Vdec *d = vdec_from_ctrl(ctx, ctx->rdi);
    if (d) {
        h264vt_finish(d->core);
        if (ctx->rsi) vdec_note_fb(d, ((const VideodecFrameBuffer *)(ctx->mem_base + ctx->rsi))->frameBufferSize);
        H264VtFrame *f = ctx->rsi ? videodec_pop(d) : NULL;
        if (f) {
            rc = vdec_emit(ctx, d, f, ctx->rsi, ctx->rdx, true);
            h264vt_frame_free(f);
        }
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

// int sceVideodecReset(SceVideodecCtrl *ctrl)
void shim_sceVideodecReset(GuestContext *ctx) {
    if (!ctx->rdi) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    Vdec *d = vdec_from_ctrl(ctx, ctx->rdi);
    if (d) h264vt_reset(d->core);
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVideodecDeleteDecoder(SceVideodecCtrl *ctrl)
void shim_sceVideodecDeleteDecoder(GuestContext *ctx) {
    if (!ctx->rdi) {
        ctx->rax = (uint64_t)(int64_t)VIDEODEC_ERROR_ARGUMENT_POINTER;
        SHIM_RETURN();
    }
    Vdec *d = vdec_from_ctrl(ctx, ctx->rdi);
    if (d) {
        d->magic = 0;
        Ps4VideodecStats s;
        vdec_snapshot(d, &s);
        fprintf(stderr,
                "[libSceVideodec] decoder %u deleted: AUs %llu (slices %llu, max per AU %u, mid-picture %llu), pictures "
                "%llu (flushed %llu, error %llu, refused %llu), first picture mono %.3f, last mono %.3f, pts %lld..%lld, frame buffer "
                "min 0x%llx need 0x%llx\n",
                d->ordinal, (unsigned long long)s.aus, (unsigned long long)s.slices, s.max_slices_per_au,
                (unsigned long long)s.mid_picture_aus, (unsigned long long)s.pictures,
                (unsigned long long)s.flush_pictures, (unsigned long long)s.error_pictures,
                (unsigned long long)s.refused_pictures, s.first_picture_mono,
                s.last_picture_mono, (long long)s.first_pts, (long long)s.last_pts, (unsigned long long)s.min_fb_size,
                (unsigned long long)s.need_fb_size);
        h264vt_destroy(d->core);
        pthread_mutex_destroy(&d->stats_lock);
        free(d);
    }
    ((VideodecCtrl *)(ctx->mem_base + ctx->rdi))->handle = 0;
    ctx->rax = 0;
    SHIM_RETURN();
}
