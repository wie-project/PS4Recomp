// SPDX-License-Identifier: GPL-2.0-or-later

// HEVC through libSceVideodec2, libSceVdecsw and libSceVdecCore, and H.264 through libSceVdecCore,
// on the VideoToolbox core. The stream is made here with VideoToolbox's encoder (frame
// reordering on, 1000x562 so the SPS carries a conformance window / frame cropping): every frame
// has its own flat luma level and a moving box, so a decoded picture names the frame it shows.
// The guest structures are written at the PS4 offsets.
#include "ps4_h264_vt.h"
#include "ps4_hevc.h"
#include "ps4_hevc_picture_info.h"
#include "recomp_runtime.h"
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Runtime symbol the decoders link against (GPU cache invalidation of the written frames).
void ps4_gnm_notify_cpu_write(uint64_t addr, uint64_t size) {
    (void)addr;
    (void)size;
}

void shim_sceVideodec2QueryDecoderMemoryInfo(GuestContext *ctx);
void shim_sceVideodec2CreateHevcDecoder(GuestContext *ctx);
void shim_sceVideodec2Decode(GuestContext *ctx);
void shim_sceVideodec2Flush(GuestContext *ctx);
void shim_sceVideodec2GetHevcPictureInfo(GuestContext *ctx);
void shim_sceVideodec2DeleteDecoder(GuestContext *ctx);
void shim_sceVdecswQueryDecoderMemoryInfo(GuestContext *ctx);
void shim_sceVdecswCreateDecoder(GuestContext *ctx);
void shim_sceVdecswSetDecodeInput(GuestContext *ctx);
void shim_sceVdecswTrySyncDecodeInput(GuestContext *ctx);
void shim_sceVdecswSetDecodeOutput(GuestContext *ctx);
void shim_sceVdecswTrySyncDecodeOutput(GuestContext *ctx);
void shim_sceVdecswFinalizeDecodeSequence(GuestContext *ctx);
void shim_sceVdecswGetHevcPictureInfo(GuestContext *ctx);
void shim_sceVdecswDeleteDecoder(GuestContext *ctx);
void shim_sceVdecCoreQueryInstanceSize(GuestContext *ctx);
void shim_sceVdecCoreQueryFrameBufferInfo(GuestContext *ctx);
void shim_sceVdecCoreQueryComputeResourceInfo(GuestContext *ctx);
void shim_sceVdecCoreCreateDecoder(GuestContext *ctx);
void shim_sceVdecCoreDeleteDecoder(GuestContext *ctx);
void shim_sceVdecCoreResetDecoder(GuestContext *ctx);
void shim_sceVdecCoreSetDecodeInput(GuestContext *ctx);
void shim_sceVdecCoreSyncDecode(GuestContext *ctx);
void shim_sceVdecCoreTrySyncDecode(GuestContext *ctx);
void shim_sceVdecCoreGetDecodeOutput(GuestContext *ctx);
void shim_sceVdecCoreFlushDecodeOutput(GuestContext *ctx);
void shim_sceVdecCoreMapMemoryBlock(GuestContext *ctx);

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            exit(1);                                                              \
        }                                                                         \
    } while (0)

#define W 1000
#define H 562
#define FRAMES 30
#define CODEC_HEVC 974921u
#define MEM_SIZE 0x1000000ull
#define STACK_TOP 0x10000ull
#define CFG 0x20000ull
#define MINFO 0x21000ull
#define OUT 0x22000ull
#define IN 0x23000ull
#define FB 0x24000ull
#define INFO 0x25000ull
#define PIC 0x26000ull
#define AU 0x100000ull
#define FB_DATA 0x400000ull
#define FB_SIZE 0x200000ull

static uint8_t *mem;
static GuestContext ctx;

static uint64_t *u64_at(uint64_t a) { return (uint64_t *)(mem + a); }
static uint32_t *u32_at(uint64_t a) { return (uint32_t *)(mem + a); }

static int32_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    ctx.rsp = STACK_TOP;
    ctx.rdi = a0, ctx.rsi = a1, ctx.rdx = a2, ctx.rcx = a3;
    shim(&ctx);
    CHECK(ctx.rsp == STACK_TOP + 8);
    return (int32_t)ctx.rax;
}

// ---- the stream ------------------------------------------------------------------------------

typedef struct Au {
    uint8_t *data;
    size_t size;
    int64_t pts; // frame index
    int64_t dts;
} Au;

static Au g_aus[FRAMES * 2];
static int g_au_count;
static bool g_reordered;
static bool g_sync[FRAMES]; // the encoder's sync samples, by frame
static bool g_avc;          // encode H.264 instead of HEVC

static uint8_t level(int frame) { return (uint8_t)(40 + 6 * frame); }

static void put_nal(uint8_t **p, size_t *n, size_t *cap, const uint8_t *nal, size_t len) {
    if (*n + len + 4 > *cap) {
        *cap = (*n + len + 4) * 2;
        *p = realloc(*p, *cap);
        CHECK(*p);
    }
    memcpy(*p + *n, "\0\0\0\1", 4);
    memcpy(*p + *n + 4, nal, len);
    *n += len + 4;
}

// Encoder output (decode order): length-prefixed NAL units to Annex-B, with the parameter sets
// in front of every sync sample.
static void encoded(void *refcon, void *frame_refcon, OSStatus status, VTEncodeInfoFlags flags, CMSampleBufferRef sb) {
    (void)refcon, (void)frame_refcon, (void)flags;
    CHECK(status == noErr && sb && g_au_count < FRAMES * 2);
    Au *a = &g_aus[g_au_count++];
    size_t cap = 0;
    a->pts = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sb)) * 30 + 0.5;
    const CMTime dts = CMSampleBufferGetDecodeTimeStamp(sb);
    a->dts = CMTIME_IS_VALID(dts) ? (int64_t)(CMTimeGetSeconds(dts) * 30 + 0.5) : a->pts;
    if (a->pts != g_au_count - 1) g_reordered = true;
    CFArrayRef att = CMSampleBufferGetSampleAttachmentsArray(sb, false);
    const bool sync = !att || !CFDictionaryContainsKey(CFArrayGetValueAtIndex(att, 0), kCMSampleAttachmentKey_NotSync);
    if (a->pts >= 0 && a->pts < FRAMES) g_sync[a->pts] = sync;
    if (sync) {
        CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(sb);
        size_t count = 0;
        if (g_avc) CHECK(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 0, NULL, NULL, &count, NULL) == noErr);
        else CHECK(CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fmt, 0, NULL, NULL, &count, NULL) == noErr);
        for (size_t i = 0; i < count; i++) {
            const uint8_t *ps;
            size_t len;
            if (g_avc) CHECK(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, i, &ps, &len, NULL, NULL) == noErr);
            else CHECK(CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fmt, i, &ps, &len, NULL, NULL) == noErr);
            put_nal(&a->data, &a->size, &cap, ps, len);
        }
    }
    CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sb);
    size_t total = 0;
    char *p = NULL;
    CHECK(CMBlockBufferGetDataPointer(bb, 0, NULL, &total, &p) == noErr);
    for (size_t o = 0; o + 4 <= total;) {
        const uint32_t len = (uint32_t)(uint8_t)p[o] << 24 | (uint32_t)(uint8_t)p[o + 1] << 16 |
                             (uint32_t)(uint8_t)p[o + 2] << 8 | (uint8_t)p[o + 3];
        put_nal(&a->data, &a->size, &cap, (const uint8_t *)p + o + 4, len);
        o += 4 + len;
    }
}

static void encode_stream(void) {
    VTCompressionSessionRef s = NULL;
    CHECK(VTCompressionSessionCreate(NULL, W, H, g_avc ? kCMVideoCodecType_H264 : kCMVideoCodecType_HEVC, NULL, NULL, NULL, encoded, NULL, &s) == noErr);
    VTSessionSetProperty(s, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanTrue);
    VTSessionSetProperty(s, kVTCompressionPropertyKey_RealTime, kCFBooleanFalse);
    const int32_t gop = 15;
    CFNumberRef g = CFNumberCreate(NULL, kCFNumberSInt32Type, &gop);
    VTSessionSetProperty(s, kVTCompressionPropertyKey_MaxKeyFrameInterval, g);
    CFRelease(g);
    for (int i = 0; i < FRAMES; i++) {
        CVPixelBufferRef pb = NULL;
        CHECK(CVPixelBufferCreate(NULL, W, H, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, NULL, &pb) == kCVReturnSuccess);
        CVPixelBufferLockBaseAddress(pb, 0);
        uint8_t *y = CVPixelBufferGetBaseAddressOfPlane(pb, 0);
        uint8_t *c = CVPixelBufferGetBaseAddressOfPlane(pb, 1);
        const size_t ys = CVPixelBufferGetBytesPerRowOfPlane(pb, 0), cs = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
        for (int r = 0; r < H; r++) {
            memset(y + r * ys, level(i), W);
            if (r >= 300 && r < 400) memset(y + r * ys + 100 + 20 * i, 235, 100); // the moving box
        }
        for (int r = 0; r < H / 2; r++) memset(c + r * cs, 128, W);
        CVPixelBufferUnlockBaseAddress(pb, 0);
        CHECK(VTCompressionSessionEncodeFrame(s, pb, CMTimeMake(i, 30), CMTimeMake(1, 30), NULL, NULL, NULL) == noErr);
        CVPixelBufferRelease(pb);
    }
    CHECK(VTCompressionSessionCompleteFrames(s, kCMTimeInvalid) == noErr);
    VTCompressionSessionInvalidate(s);
    CFRelease(s);
    CHECK(g_au_count == FRAMES);
}

// ---- checks of one decoded picture -------------------------------------------------------------

static int g_next_frame;

// The NV12 picture at fb must be the next frame in display order; returns its index.
static int check_nv12(uint64_t fb, uint32_t width, uint32_t pitch, uint32_t height, uint64_t fb_size) {
    CHECK(width == 1008 && pitch == 1024 && height == 576); // align(1000, 16), align(1000, 64), align(562, 16)
    CHECK(fb_size == (uint64_t)pitch * height * 3 / 2);
    const int frame = g_next_frame++;
    const uint8_t *y = mem + fb;
    CHECK(abs(y[10 * pitch + 10] - level(frame)) <= 2);                  // background
    CHECK(abs(y[350 * pitch + 100 + 20 * frame + 50] - 235) <= 4);       // inside the box
    // Near the bottom. (The last rows are lossy: the encoder pads the coded picture below them.
    // The decode itself matches FFmpeg byte for byte on this stream.)
    CHECK(abs(y[540 * pitch + 900] - level(frame)) <= 2);
    CHECK(y[(H + 5) * pitch + 900] == y[(H - 1) * pitch + 900]);         // rows past the picture repeat it
    const uint8_t *c = mem + fb + (uint64_t)pitch * height;
    CHECK(abs(c[10 * pitch + 10] - 128) <= 2);
    return frame;
}

// The NV12 picture at fb, then its HEVC picture info right after it.
static void check_picture(uint64_t fb, uint32_t width, uint32_t pitch, uint32_t height, uint64_t fb_size) {
    const int frame = check_nv12(fb, width, pitch, height, fb_size);
    const Ps4HevcPictureInfo *pi = (const Ps4HevcPictureInfo *)(mem + fb + fb_size);
    CHECK(pi->this_size == sizeof(Ps4HevcPictureInfo) && pi->is_valid == 1);
    CHECK(pi->pts_data == (uint64_t)frame && pi->attached_data == 0x1000u + (uint64_t)frame);
    CHECK(pi->general_profile_idc == 1 && pi->bit_depth_luma_minus8 == 0); // Main
    CHECK(pi->pic_width_in_luma_samples >= W && pi->pic_height_in_luma_samples >= H);
    CHECK(pi->conformance_window_flag == 1);
    CHECK(pi->pic_height_in_luma_samples - 2 * (pi->conf_win_top_offset + pi->conf_win_bottom_offset) == H);
    CHECK(pi->frame_crop_right_offset == pitch - W && pi->frame_crop_bottom_offset == height - H);
    CHECK(pi->irap_picture_flag == g_sync[frame]);
    CHECK(pi->sequence_parameter_set_present_flag == pi->irap_picture_flag);
    CHECK(pi->video_parameter_set_present_flag == pi->irap_picture_flag);
}

// ---- libSceVideodec2 ---------------------------------------------------------------------------

static void vdec2_input(int i) {
    memcpy(mem + AU, g_aus[i].data, g_aus[i].size);
    memset(mem + IN, 0, 0x30);
    *u64_at(IN) = 0x30;
    *u64_at(IN + 0x08) = AU;
    *u64_at(IN + 0x10) = g_aus[i].size;
    *u64_at(IN + 0x18) = (uint64_t)g_aus[i].pts;
    *u64_at(IN + 0x20) = (uint64_t)g_aus[i].dts;
    *u64_at(IN + 0x28) = 0x1000u + (uint64_t)g_aus[i].pts;
}

static void vdec2_frame_buffer(void) {
    memset(mem + FB, 0, 0x20);
    *u64_at(FB) = 0x20;
    *u64_at(FB + 0x08) = FB_DATA;
    *u64_at(FB + 0x10) = FB_SIZE;
    memset(mem + OUT, 0, 0x38);
    *u64_at(OUT) = 0x38;
}

// OutputInfo 0x38: is_valid 0x08, picture_count 0x0A, codec_type 0x0C, width 0x10, pitch 0x14,
// height 0x18, frame_buffer 0x20, frame_buffer_size 0x28.
static bool vdec2_take(uint64_t dec) {
    if (!mem[OUT + 0x08]) return false;
    CHECK(mem[FB + 0x18] == 1 && mem[OUT + 0x0A] == 1 && *u32_at(OUT + 0x0C) == CODEC_HEVC);
    CHECK(*u64_at(OUT + 0x20) == FB_DATA);
    check_picture(FB_DATA, *u32_at(OUT + 0x10), *u32_at(OUT + 0x14), *u32_at(OUT + 0x18), *u64_at(OUT + 0x28));
    // GetHevcPictureInfo copies the trailer after the caller's this_size.
    memset(mem + PIC, 0, sizeof(Ps4HevcPictureInfo));
    *u64_at(PIC) = sizeof(Ps4HevcPictureInfo);
    CHECK(call(shim_sceVideodec2GetHevcPictureInfo, OUT, PIC, 0, 0) == 0);
    CHECK(memcmp(mem + PIC + 8, mem + FB_DATA + *u64_at(OUT + 0x28) + 8, sizeof(Ps4HevcPictureInfo) - 8) == 0);
    (void)dec;
    return true;
}

static int test_videodec2(void) {
    // DecoderConfigInfo 0x48: codec 0x0C, size 0x18 / 0x1C, pipeline depth 0x24, priority 0x38.
    memset(mem + CFG, 0, 0x48);
    *u64_at(CFG) = 0x48;
    *u32_at(CFG + 0x0C) = CODEC_HEVC;
    *(int32_t *)(mem + CFG + 0x18) = W;
    *(int32_t *)(mem + CFG + 0x1C) = H;
    *u32_at(CFG + 0x24) = 1;
    *(int32_t *)(mem + CFG + 0x38) = -1;
    memset(mem + MINFO, 0, 0x48);
    *u64_at(MINFO) = 0x48;
    CHECK(call(shim_sceVideodec2QueryDecoderMemoryInfo, CFG, MINFO, 0, 0) == 0);
    CHECK(*u64_at(MINFO + 0x38) >= (uint64_t)1024 * 576 * 3 / 2 + sizeof(Ps4HevcPictureInfo));
    CHECK(call(shim_sceVideodec2CreateHevcDecoder, CFG, MINFO, CFG + 0x100, 0) == 0);
    const uint64_t dec = *u64_at(CFG + 0x100);
    for (int i = 0; i < g_au_count; i++) {
        vdec2_input(i);
        vdec2_frame_buffer();
        CHECK(call(shim_sceVideodec2Decode, dec, IN, FB, OUT) == 0);
        vdec2_take(dec);
    }
    for (;;) {
        vdec2_frame_buffer();
        CHECK(call(shim_sceVideodec2Flush, dec, FB, OUT, 0) == 0);
        if (!vdec2_take(dec)) break;
    }
    CHECK(g_next_frame == FRAMES);
    CHECK(call(shim_sceVideodec2DeleteDecoder, dec, 0, 0, 0) == 0);
    printf("PASS videodec2\n");
    return 0;
}

// ---- libSceVdecsw ------------------------------------------------------------------------------

// OutputInfo 0x38 as libSceVideodec2's, plus is_last_frame 0x09.
static bool vdecsw_take(uint64_t dec, bool *last) {
    *u64_at(FB) = 0x18;
    *u64_at(FB + 0x08) = FB_DATA;
    *u64_at(FB + 0x10) = FB_SIZE;
    int32_t rc = call(shim_sceVdecswSetDecodeOutput, dec, FB, 0, 0);
    CHECK(rc == 0 || rc == (int32_t)0x81510114); // OUTPUT_BUFFER_FULL: the earlier one is still pending
    memset(mem + OUT, 0, 0x38);
    *u64_at(OUT) = 0x38;
    rc = call(shim_sceVdecswTrySyncDecodeOutput, dec, OUT, 0, 0);
    if (rc == (int32_t)0x81510117) return false; // DECODE_PENDING
    CHECK(rc == 0);
    *last = mem[OUT + 0x09] != 0;
    if (!mem[OUT + 0x08]) return false;
    CHECK(*u32_at(OUT + 0x0C) == CODEC_HEVC);
    check_picture(FB_DATA, *u32_at(OUT + 0x10), *u32_at(OUT + 0x14), *u32_at(OUT + 0x18), *u64_at(OUT + 0x28));
    memset(mem + PIC, 0, sizeof(Ps4HevcPictureInfo));
    *u64_at(PIC) = sizeof(Ps4HevcPictureInfo);
    CHECK(call(shim_sceVdecswGetHevcPictureInfo, OUT, PIC, 0, 0) == 0);
    CHECK(memcmp(mem + PIC + 8, mem + FB_DATA + *u64_at(OUT + 0x28) + 8, sizeof(Ps4HevcPictureInfo) - 8) == 0);
    return true;
}

static int test_vdecsw(void) {
    memset(mem + CFG, 0, 0x50);
    *u64_at(CFG) = 0x48;
    *u32_at(CFG + 0x0C) = CODEC_HEVC;
    *(int32_t *)(mem + CFG + 0x18) = W;
    *(int32_t *)(mem + CFG + 0x1C) = H;
    *u32_at(CFG + 0x24) = 4;
    memset(mem + MINFO, 0, 0x48);
    *u64_at(MINFO) = 0x48;
    CHECK(call(shim_sceVdecswQueryDecoderMemoryInfo, CFG, MINFO, 0, 0) == 0);
    *u64_at(MINFO + 0x10) = 0x300000;
    *u64_at(MINFO + 0x20) = 0x310000;
    *u64_at(MINFO + 0x30) = 0x320000;
    CHECK(call(shim_sceVdecswCreateDecoder, CFG, MINFO, CFG + 0x100, 0) == 0);
    const uint64_t dec = *u64_at(CFG + 0x100);
    bool last = false;
    for (int i = 0; i < g_au_count; i++) {
        vdec2_input(i);
        CHECK(call(shim_sceVdecswSetDecodeInput, dec, IN, 0, 0) == 0);
        memset(mem + PIC, 0, 0x18);
        *u64_at(PIC) = 0x18;
        CHECK(call(shim_sceVdecswTrySyncDecodeInput, dec, PIC, 0, 0) == 0);
        CHECK(*u64_at(PIC + 8) == AU);
        while (vdecsw_take(dec, &last)) {}
    }
    CHECK(call(shim_sceVdecswFinalizeDecodeSequence, dec, 0, 0, 0) == 0);
    while (!last) vdecsw_take(dec, &last);
    CHECK(g_next_frame == FRAMES);
    CHECK(call(shim_sceVdecswDeleteDecoder, dec, 0, 0, 0) == 0);
    printf("PASS vdecsw\n");
    return 0;
}

// ---- libSceVdecCore ---------------------------------------------------------------------------

#define VC_ERR_ARG ((int32_t)0x80C00001)
#define VC_ERR_CODEC ((int32_t)0x80C00003)
#define VC_ERR_NO_PICTURE ((int32_t)0x80C00010)
#define VC_FB_SIZE 0xE0000ull // align(1024 * 576 * 3/2 + 0x1000, 0x10000)
#define VC_SLOTS FB_DATA        // MapMemoryBlock: two slots
#define VC_OWN_FB (FB_DATA + 0x400000ull)
#define VC_STATUS (IN + 0x100)

// InputData 0x30: au 0x00, size 0x08, pts 0x10, dts 0x18, user_data 0x20, flags 0x28, stream 0x2C.
static void vc_input(uint64_t dec, int i) {
    memcpy(mem + AU, g_aus[i].data, g_aus[i].size);
    memset(mem + IN, 0, 0x30);
    *u64_at(IN) = AU;
    *u64_at(IN + 0x08) = g_aus[i].size;
    *u64_at(IN + 0x10) = (uint64_t)g_aus[i].pts;
    *u64_at(IN + 0x18) = (uint64_t)g_aus[i].dts;
    *u64_at(IN + 0x20) = 0x1000u + (uint64_t)g_aus[i].pts;
    *u64_at(VC_STATUS) = 0;
    CHECK(call(shim_sceVdecCoreSetDecodeInput, dec, IN, 0, VC_STATUS) == 0);
    CHECK(*u32_at(VC_STATUS) == 1 && *u32_at(VC_STATUS + 4) == 1);
}

static uint32_t vc_ready(uint64_t dec) {
    *u32_at(PIC) = 0xFFFFFFFFu;
    CHECK(call(shim_sceVdecCoreSyncDecode, dec, PIC, 0, 0) == 0);
    const uint32_t n = *u32_at(PIC);
    *u32_at(PIC) = 0xFFFFFFFFu;
    CHECK(call(shim_sceVdecCoreTrySyncDecode, dec, PIC, 0, 0) == 0 && *u32_at(PIC) == n);
    return n;
}

static uint32_t g_slot_uses;

// GetDecodeOutput into a mapped slot, or (every third picture) into the caller's frame buffer.
// OutputInfo 0xD0: width 0x00, pitch 0x04, height 0x08, picture_count 0x0C, frame_buffer 0x10,
// pictures[0] 0x20 {flags, pts 0x08, dts 0x10, user_data 0x18, frame_buffer 0x28, size 0x30,
// pitch 0x34}, sar_width 0xA4, frame_pitch_in_bytes 0xB4, luma 0xB8, chroma 0xC0.
static void vc_take(uint64_t dec, uint32_t codec) {
    const bool own = g_next_frame % 3 == 2;
    uint64_t fb_arg = 0;
    if (own) {
        // A buffer too small for the picture is refused and the picture stays queued.
        *u64_at(FB) = VC_OWN_FB;
        *u64_at(FB + 8) = 0x1000;
        CHECK(call(shim_sceVdecCoreGetDecodeOutput, dec, FB, 0, OUT) == VC_ERR_ARG);
        *u64_at(FB + 8) = VC_FB_SIZE;
        fb_arg = FB;
    }
    memset(mem + OUT, 0xCC, 0xD0);
    CHECK(call(shim_sceVdecCoreGetDecodeOutput, dec, fb_arg, 0, OUT) == 0);
    const uint64_t buf = own ? VC_OWN_FB : VC_SLOTS + (g_slot_uses++ % 2) * VC_FB_SIZE;
    CHECK(*u64_at(OUT + 0x10) == buf && *u32_at(OUT + 0x0C) == 1);
    CHECK(*u64_at(OUT + 0x48) == buf && *u64_at(OUT + 0xB8) == buf);
    CHECK(*u64_at(OUT + 0xC0) == buf + 1024ull * 576);
    CHECK(*u32_at(OUT + 0x54) == 1024 && *u32_at(OUT + 0xB4) == 1024);
    const int frame = check_nv12(buf, *u32_at(OUT + 0x00), *u32_at(OUT + 0x04), *u32_at(OUT + 0x08), *u32_at(OUT + 0x50));
    CHECK(*u32_at(OUT + 0x20) == codec);
    CHECK(*u64_at(OUT + 0x28) == (uint64_t)frame && *u64_at(OUT + 0x38) == 0x1000u + (uint64_t)frame);
    CHECK(*u64_at(OUT + 0x88) == 0); // pictures[1].frame_buffer (0x60 + 0x28)
    // HwPictureInfo 0x400 at the end of the buffer.
    const uint8_t *hw = mem + buf + VC_FB_SIZE - 0x400;
    CHECK(*(const uint32_t *)hw == (codec == 4 ? 1u : 0u));
    CHECK(*(const uint64_t *)(hw + 0x08) == 0x1000u + (uint64_t)frame && *(const uint64_t *)(hw + 0x10) == (uint64_t)frame);
    CHECK(*(const uint32_t *)(hw + 0x20) == 1);                                            // valid
    CHECK(*(const uint32_t *)(hw + 0x28) == 1008 && *(const uint32_t *)(hw + 0x2C) == 576); // size
    CHECK(*(const uint32_t *)(hw + 0x34) == 12 && *(const uint32_t *)(hw + 0x3C) == 7);     // crop right, bottom
    CHECK(*(const uint32_t *)(hw + 0x40) == *u32_at(OUT + 0xA4));                           // sar_width
    CHECK(*(const uint32_t *)(hw + 0x4C) == 3);                                            // valid | cropped
}

static int test_vdeccore(uint32_t codec) {
    // ConfigInfo 0x58: decoder_type 0x00, codec 0x04, dpb 0x10, width 0x14, height 0x18.
    memset(mem + CFG, 0, 0x58);
    *u32_at(CFG) = 3;
    *u32_at(CFG + 0x04) = codec;
    *u32_at(CFG + 0x10) = 4;
    *u32_at(CFG + 0x14) = W;
    *u32_at(CFG + 0x18) = H;
    CHECK(call(shim_sceVdecCoreQueryInstanceSize, 0, MINFO, MINFO + 8, MINFO + 16) == VC_ERR_ARG);
    CHECK(call(shim_sceVdecCoreQueryInstanceSize, CFG, MINFO, MINFO + 8, MINFO + 16) == 0);
    CHECK(*u64_at(MINFO) == 0x40000 && *u64_at(MINFO + 8) == 0x10000 && *u64_at(MINFO + 16) == 0);
    CHECK(call(shim_sceVdecCoreQueryComputeResourceInfo, MINFO, 0, 0, 0) == 0);
    CHECK(*u64_at(MINFO + 8) == 0x10000 && *u32_at(MINFO + 16) == 0x10000);
    CHECK(call(shim_sceVdecCoreQueryFrameBufferInfo, CFG, MINFO, 0, 0) == 0);
    CHECK(*u32_at(MINFO) == VC_FB_SIZE && *u32_at(MINFO + 4) == 256 && *u32_at(MINFO + 8) == 5);
    CHECK(*u32_at(MINFO + 12) == 256 && *u32_at(MINFO + 16) == W && *u32_at(MINFO + 20) == H);
    *u32_at(CFG + 0x04) = 5;
    CHECK(call(shim_sceVdecCoreQueryFrameBufferInfo, CFG, MINFO, 0, 0) == VC_ERR_CODEC);
    CHECK(call(shim_sceVdecCoreCreateDecoder, CFG, 0, 0, CFG + 0x100) == VC_ERR_CODEC);
    *u32_at(CFG + 0x04) = codec;
    *u32_at(CFG) = 9;
    CHECK(call(shim_sceVdecCoreCreateDecoder, CFG, 0, 0, CFG + 0x100) == VC_ERR_ARG);
    *u32_at(CFG) = 3;
    CHECK(call(shim_sceVdecCoreCreateDecoder, CFG, 0, 0, CFG + 0x100) == 0);
    const uint64_t dec = *u64_at(CFG + 0x100);

    // No picture yet: the maximum size, no buffers.
    CHECK(call(shim_sceVdecCoreGetDecodeOutput, dec, 0, 0, OUT) == VC_ERR_NO_PICTURE);
    CHECK(*u32_at(OUT) == W && *u32_at(OUT + 4) == 1024 && *u32_at(OUT + 8) == H && *u32_at(OUT + 0x0C) == 0);
    CHECK(*u64_at(OUT + 0x10) == 0 && *u64_at(OUT + 0xB8) == 0);
    // No frame buffer and nothing mapped: refused, the picture stays.
    vc_input(dec, 0);
    while (vc_ready(dec) == 0) {
        CHECK(call(shim_sceVdecCoreFlushDecodeOutput, dec, PIC, 0, 0) == 0);
    }
    CHECK(call(shim_sceVdecCoreGetDecodeOutput, dec, 0, 0, OUT) == VC_ERR_ARG);
    CHECK(vc_ready(dec) == 1);
    // Reset drops it.
    vc_input(dec, 1);
    CHECK(call(shim_sceVdecCoreResetDecoder, dec, 0, 0, 0) == 0);
    CHECK(vc_ready(dec) == 0);
    CHECK(call(shim_sceVdecCoreGetDecodeOutput, dec, 0, 0, OUT) == VC_ERR_NO_PICTURE);

    // MemoryBlock {addr, size, align, type}: one block of two frame buffers.
    memset(mem + MINFO, 0, 0x18);
    *u64_at(MINFO) = VC_SLOTS;
    *u64_at(MINFO + 8) = 2 * VC_FB_SIZE;
    CHECK(call(shim_sceVdecCoreMapMemoryBlock, dec, MINFO, 1, 0) == 0);

    for (int i = 0; i < g_au_count; i++) {
        vc_input(dec, i);
        for (uint32_t n = vc_ready(dec); n > 0; n--) vc_take(dec, codec);
    }
    *u32_at(PIC) = 0;
    CHECK(call(shim_sceVdecCoreFlushDecodeOutput, dec, PIC, 0, 0) == 0);
    const uint32_t rest = *u32_at(PIC);
    CHECK(rest == vc_ready(dec));
    for (uint32_t n = 0; n < rest; n++) vc_take(dec, codec);
    CHECK(g_next_frame == FRAMES);
    CHECK(call(shim_sceVdecCoreGetDecodeOutput, dec, 0, 0, OUT) == VC_ERR_NO_PICTURE);

    CHECK(call(shim_sceVdecCoreDeleteDecoder, dec, 0, 0, 0) == 0);
    CHECK(call(shim_sceVdecCoreDeleteDecoder, dec, 0, 0, 0) == VC_ERR_ARG);
    CHECK(call(shim_sceVdecCoreSyncDecode, dec, PIC, 0, 0) == VC_ERR_ARG);
    printf("PASS vdeccore-%s\n", codec == 4 ? "hevc" : "avc");
    return 0;
}

// ---- the parser on the encoder's SPS -----------------------------------------------------------

static int test_parse(void) {
    const uint8_t *a = g_aus[0].data;
    const size_t n = g_aus[0].size;
    HevcSpsInfo sps = {0};
    HevcPpsInfo pps = {0};
    bool have_vps = false;
    for (size_t i = 0; i + 4 < n;) {
        size_t j = i + 4;
        while (j + 4 <= n && memcmp(a + j, "\0\0\0\1", 4) != 0) j++;
        if (j + 4 > n) j = n;
        const uint8_t *nal = a + i + 4;
        if (hevc_nal_type(nal) == HEVC_NAL_VPS) have_vps = true;
        if (hevc_nal_type(nal) == HEVC_NAL_SPS) CHECK(hevc_parse_sps(nal, j - i - 4, &sps));
        if (hevc_nal_type(nal) == HEVC_NAL_PPS) CHECK(hevc_parse_pps(nal, j - i - 4, &pps));
        if (hevc_nal_type(nal) <= HEVC_NAL_VCL_LAST) {
            bool first;
            uint32_t lsb;
            CHECK(hevc_nal_type(nal) == HEVC_NAL_IDR_W_RADL || hevc_nal_type(nal) == HEVC_NAL_IDR_N_LP);
            CHECK(hevc_slice_header(nal, j - i - 4, &sps, &pps, &first, &lsb) && first && lsb == 0);
        }
        i = j;
    }
    CHECK(have_vps && sps.valid && pps.valid);
    CHECK(sps.chroma_format_idc == 1 && sps.general_profile_idc == 1);
    uint32_t w, h;
    hevc_sps_dims(&sps, &w, &h);
    CHECK(w == W && h == H);
    CHECK(sps.log2_max_pic_order_cnt_lsb >= 4 && sps.max_num_reorder_pics <= sps.max_dec_pic_buffering_minus1);
    printf("PASS parse\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    g_avc = strcmp(argv[1], "vdeccore-avc") == 0;
    mem = calloc(1, MEM_SIZE);
    if (!mem) return 1;
    ctx.mem_base = mem;
    ctx.mem_size = MEM_SIZE;
    encode_stream();
    // The encoder must have reordered frames, or the display-order checks would prove nothing.
    CHECK(g_reordered);
    if (strcmp(argv[1], "parse") == 0) return test_parse();
    if (strcmp(argv[1], "videodec2") == 0) return test_videodec2();
    if (strcmp(argv[1], "vdecsw") == 0) return test_vdecsw();
    if (strcmp(argv[1], "vdeccore-hevc") == 0) return test_vdeccore(4);
    if (strcmp(argv[1], "vdeccore-avc") == 0) return test_vdeccore(0);
    return 2;
}
