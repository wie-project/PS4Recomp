// Host test for AvPlayerFileStreamer: an FFmpeg AVIOContext over the game's
// AvPlayerFileReplacement callbacks. Build and run:
//   FF=$(brew --prefix ffmpeg)
//   clang++ -std=c++17 -I pkg/runtime/core -I pkg/runtime/modules/libSceAvPlayer -I $FF/include \
//     tests/avplayer_test/main.cpp pkg/runtime/modules/libSceAvPlayer/ps4_avplayer_file_streamer.cpp \
//     -L $FF/lib -lavformat -lavcodec -lavutil -o /tmp/avplayer_test && /tmp/avplayer_test
//
// The callbacks are registered in the guest dispatch table as recompiled functions are, so the
// streamer reaches them through recomp_call_guest with the arguments in the SysV registers, a
// return slot on the guest stack and the read buffer in guest memory. The test movie is encoded
// in the test.

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <vector>

#include "ps4_avplayer_file_streamer.h"
#include "recomp_context.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
}

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "[avplayer-test] FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            abort();                                                                       \
        }                                                                                  \
    } while (0)

extern "C" recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE] = {};

namespace {

constexpr uint64_t kMemSize = 16ULL << 20;
constexpr uint64_t kStackA = 0x100000;  // stack top of the "game" thread context
constexpr uint64_t kStackB = 0x200000;  // stack top of the "demuxer" thread context
constexpr uint64_t kPath = 0x300000;    // guest path string
constexpr uint64_t kVmBase = 0x400000;  // guest VM allocations
constexpr uint64_t kObject = 0xabc000;  // the game's file_replacement.object_ptr
constexpr uint64_t kFnBase = 0x10000000;
constexpr uint64_t kFnOpen = kFnBase + 0x10;
constexpr uint64_t kFnClose = kFnBase + 0x20;
constexpr uint64_t kFnRead = kFnBase + 0x30;
constexpr uint64_t kFnSize = kFnBase + 0x40;
constexpr char kGuestPath[] = "mp4/avplayer_test.mp4";

constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr int kFrames = 48;
constexpr int kFps = 24;
constexpr int kSampleRate = 48000;

uint8_t *g_mem = nullptr;

// The game's file. Faults are injected through the fields below.
struct FakeFile {
    std::vector<uint8_t> data;
    std::atomic<GuestContext *> expected_ctx{nullptr};
    int open_calls = 0;
    int close_calls = 0;
    int size_calls = 0;
    std::atomic<int> read_calls{0};
    int32_t open_result = 3;
    int32_t close_result = 0;
    int32_t read_error = 0;          // returned for reads at or past fail_at
    uint64_t fail_at = UINT64_MAX;
    uint64_t short_at = UINT64_MAX;  // reads at or past it return 0 bytes
    bool over_read = false;          // return one byte more than asked
} g_file;

uint64_t g_vm_next = kVmBase;
int g_vm_allocs = 0;
int g_vm_frees = 0;

// Guest RET: pop the return slot recomp_call_guest pushed.
void GuestReturn(GuestContext *ctx, uint64_t rax) {
    CHECK(MEM_U64(ctx->rsp) == 0);
    ctx->rsp += 8;
    ctx->rax = rax;
}

void CheckCaller(GuestContext *ctx) {
    CHECK(ctx == g_file.expected_ctx.load());
    CHECK(ctx->rdi == kObject);
}

void FakeOpen(GuestContext *ctx) {
    CheckCaller(ctx);
    CHECK(ctx->rsi < kMemSize);
    CHECK(strcmp((const char *)(g_mem + ctx->rsi), kGuestPath) == 0);
    g_file.open_calls++;
    GuestReturn(ctx, (uint64_t)(int64_t)g_file.open_result);
}

void FakeClose(GuestContext *ctx) {
    CheckCaller(ctx);
    g_file.close_calls++;
    GuestReturn(ctx, (uint64_t)(int64_t)g_file.close_result);
}

void FakeSize(GuestContext *ctx) {
    CheckCaller(ctx);
    g_file.size_calls++;
    GuestReturn(ctx, g_file.data.size());
}

// read_offset(object, buffer, position, length): the buffer is a guest address.
void FakeRead(GuestContext *ctx) {
    CheckCaller(ctx);
    const uint64_t buf = ctx->rsi;
    const uint64_t pos = ctx->rdx;
    const uint32_t len = (uint32_t)ctx->rcx;
    CHECK(buf >= kVmBase && buf + len <= kMemSize);
    CHECK(len > 0 && len <= AvPlayerFileStreamer::kChunkSize);
    g_file.read_calls++;
    if (pos >= g_file.fail_at) {
        GuestReturn(ctx, (uint64_t)(int64_t)g_file.read_error);
        return;
    }
    if (pos >= g_file.short_at) {
        GuestReturn(ctx, 0);
        return;
    }
    CHECK(pos < g_file.data.size());
    uint64_t n = std::min<uint64_t>(len, g_file.data.size() - pos);
    memcpy(g_mem + buf, g_file.data.data() + pos, n);
    GuestReturn(ctx, g_file.over_read ? n + 1 : n);
}

void RegisterGuestFunction(uint64_t addr, recomp_fn_t fn) {
    recomp_fn_t **&l2 = g_dispatch_l1[(addr >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK];
    if (!l2) l2 = (recomp_fn_t **)calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t *));
    recomp_fn_t *&l3 = l2[(addr >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK];
    if (!l3) l3 = (recomp_fn_t *)calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t));
    l3[addr & DISPATCH_LEVEL_MASK] = fn;
}

GuestContext MakeContext(uint64_t stack_top) {
    GuestContext ctx = {};
    ctx.mem_base = g_mem;
    ctx.mem_size = kMemSize;
    ctx.rsp = stack_top;
    return ctx;
}

AvPlayerFileReplacement Callbacks() {
    AvPlayerFileReplacement fr{};
    fr.object_ptr = kObject;
    fr.open = kFnOpen;
    fr.close = kFnClose;
    fr.read_offset = kFnRead;
    fr.size = kFnSize;
    return fr;
}

void ResetFile() {
    g_file.open_calls = g_file.close_calls = g_file.size_calls = 0;
    g_file.read_calls = 0;
    g_file.open_result = 3;
    g_file.close_result = 0;
    g_file.read_error = 0;
    g_file.fail_at = UINT64_MAX;
    g_file.short_at = UINT64_MAX;
    g_file.over_read = false;
}

// A demuxed packet, compared between FFmpeg's own file I/O and the streamer.
struct PacketKey {
    int stream;
    int64_t pts;
    int size;
    uint64_t hash;
    bool operator==(const PacketKey &o) const {
        return stream == o.stream && pts == o.pts && size == o.size && hash == o.hash;
    }
};

PacketKey KeyOf(const AVPacket *pkt) {
    uint64_t h = 1469598103934665603ULL; // FNV-1a
    for (int i = 0; i < pkt->size; ++i) h = (h ^ pkt->data[i]) * 1099511628211ULL;
    return PacketKey{pkt->stream_index, pkt->pts, pkt->size, h};
}

std::vector<PacketKey> g_reference; // the test movie demuxed from a host file

// Encodes kFrames intra-only MPEG-4 frames and an AAC track into an MP4, demuxes it from the
// host file into g_reference and returns its bytes. Frame i has luma 16 + 4 * i in its left
// half; the right half is noise, so the file spans many AVIO chunks.
std::vector<uint8_t> EncodeTestMovie() {
    char path[] = "/tmp/avplayer_test_XXXXXX.mp4";
    int fd = mkstemps(path, 4);
    CHECK(fd >= 0);
    close(fd);

    AVFormatContext *mux = nullptr;
    CHECK(avformat_alloc_output_context2(&mux, nullptr, "mp4", path) >= 0);

    const AVCodec *venc = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    const AVCodec *aenc = avcodec_find_encoder(AV_CODEC_ID_AAC);
    CHECK(venc && aenc);

    AVCodecContext *vctx = avcodec_alloc_context3(venc);
    vctx->width = kWidth;
    vctx->height = kHeight;
    vctx->pix_fmt = AV_PIX_FMT_YUV420P;
    vctx->time_base = AVRational{1, kFps};
    vctx->framerate = AVRational{kFps, 1};
    vctx->gop_size = 1;
    vctx->max_b_frames = 0;
    vctx->bit_rate = 8000000;
    vctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    CHECK(avcodec_open2(vctx, venc, nullptr) >= 0);

    AVCodecContext *actx = avcodec_alloc_context3(aenc);
    actx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    actx->sample_rate = kSampleRate;
    av_channel_layout_default(&actx->ch_layout, 2);
    actx->bit_rate = 128000;
    actx->time_base = AVRational{1, kSampleRate};
    actx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    CHECK(avcodec_open2(actx, aenc, nullptr) >= 0);

    AVStream *vst = avformat_new_stream(mux, nullptr);
    AVStream *ast = avformat_new_stream(mux, nullptr);
    CHECK(avcodec_parameters_from_context(vst->codecpar, vctx) >= 0);
    CHECK(avcodec_parameters_from_context(ast->codecpar, actx) >= 0);
    vst->time_base = vctx->time_base;
    ast->time_base = actx->time_base;
    CHECK(avio_open(&mux->pb, path, AVIO_FLAG_WRITE) >= 0);
    CHECK(avformat_write_header(mux, nullptr) >= 0);

    AVPacket *pkt = av_packet_alloc();
    auto drain = [&](AVCodecContext *enc, AVStream *st) {
        while (avcodec_receive_packet(enc, pkt) == 0) {
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            pkt->stream_index = st->index;
            CHECK(av_interleaved_write_frame(mux, pkt) >= 0);
        }
    };

    AVFrame *vf = av_frame_alloc();
    vf->format = vctx->pix_fmt;
    vf->width = kWidth;
    vf->height = kHeight;
    CHECK(av_frame_get_buffer(vf, 0) >= 0);
    AVFrame *af = av_frame_alloc();
    af->format = actx->sample_fmt;
    af->sample_rate = kSampleRate;
    av_channel_layout_copy(&af->ch_layout, &actx->ch_layout);
    af->nb_samples = actx->frame_size;
    CHECK(av_frame_get_buffer(af, 0) >= 0);

    uint32_t lcg = 12345;
    int64_t audio_pts = 0;
    for (int i = 0; i < kFrames; ++i) {
        CHECK(av_frame_make_writable(vf) >= 0);
        for (int y = 0; y < kHeight; ++y) {
            uint8_t *row = vf->data[0] + y * vf->linesize[0];
            memset(row, 16 + 4 * i, kWidth / 2);
            for (int x = kWidth / 2; x < kWidth; ++x) {
                lcg = lcg * 1664525u + 1013904223u;
                row[x] = (uint8_t)(lcg >> 24);
            }
        }
        for (int y = 0; y < kHeight / 2; ++y) {
            memset(vf->data[1] + y * vf->linesize[1], 128, kWidth / 2);
            memset(vf->data[2] + y * vf->linesize[2], 128, kWidth / 2);
        }
        vf->pts = i;
        CHECK(avcodec_send_frame(vctx, vf) >= 0);
        drain(vctx, vst);

        // Audio up to the end of this video frame.
        while (audio_pts < (int64_t)(i + 1) * kSampleRate / kFps) {
            CHECK(av_frame_make_writable(af) >= 0);
            for (int c = 0; c < 2; ++c) {
                float *s = (float *)af->data[c];
                for (int n = 0; n < af->nb_samples; ++n) {
                    s[n] = 0.25f * (float)(((audio_pts + n) % 100) - 50) / 50.0f;
                }
            }
            af->pts = audio_pts;
            audio_pts += af->nb_samples;
            CHECK(avcodec_send_frame(actx, af) >= 0);
            drain(actx, ast);
        }
    }
    CHECK(avcodec_send_frame(vctx, nullptr) >= 0);
    drain(vctx, vst);
    CHECK(avcodec_send_frame(actx, nullptr) >= 0);
    drain(actx, ast);
    CHECK(av_write_trailer(mux) >= 0);

    av_frame_free(&vf);
    av_frame_free(&af);
    av_packet_free(&pkt);
    avcodec_free_context(&vctx);
    avcodec_free_context(&actx);
    avio_closep(&mux->pb);
    avformat_free_context(mux);

    AVFormatContext *demux = nullptr;
    CHECK(avformat_open_input(&demux, path, nullptr, nullptr) == 0);
    CHECK(avformat_find_stream_info(demux, nullptr) >= 0);
    AVPacket *rpkt = av_packet_alloc();
    while (av_read_frame(demux, rpkt) >= 0) {
        g_reference.push_back(KeyOf(rpkt));
        av_packet_unref(rpkt);
    }
    av_packet_free(&rpkt);
    avformat_close_input(&demux);

    FILE *f = fopen(path, "rb");
    CHECK(f);
    fseek(f, 0, SEEK_END);
    std::vector<uint8_t> bytes((size_t)ftell(f));
    fseek(f, 0, SEEK_SET);
    CHECK(fread(bytes.data(), 1, bytes.size(), f) == bytes.size());
    fclose(f);
    unlink(path);
    return bytes;
}

// Opens the test movie through the streamer on ctx, as sceAvPlayerAddSource does.
AVFormatContext *OpenInput(AvPlayerFileStreamer &streamer, GuestContext &ctx) {
    g_file.expected_ctx = &ctx;
    CHECK(streamer.open(&ctx, kPath));
    AVFormatContext *fmt = avformat_alloc_context();
    fmt->pb = streamer.avio();
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    CHECK(avformat_open_input(&fmt, kGuestPath, nullptr, nullptr) == 0);
    CHECK(avformat_find_stream_info(fmt, nullptr) >= 0);
    return fmt;
}

int FindStream(AVFormatContext *fmt, AVMediaType type) {
    int idx = av_find_best_stream(fmt, type, -1, -1, nullptr, 0);
    CHECK(idx >= 0);
    return idx;
}

AVCodecContext *OpenDecoder(AVStream *st) {
    const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
    CHECK(dec);
    AVCodecContext *c = avcodec_alloc_context3(dec);
    CHECK(avcodec_parameters_to_context(c, st->codecpar) >= 0);
    CHECK(avcodec_open2(c, dec, nullptr) >= 0);
    return c;
}

// The frame index a decoded test frame carries in its flat half.
int FrameIndexFromLuma(const AVFrame *f) {
    uint32_t sum = 0;
    for (int y = kHeight / 2 - 4; y < kHeight / 2 + 4; ++y) {
        for (int x = kWidth / 4 - 4; x < kWidth / 4 + 4; ++x) {
            sum += f->data[0][y * f->linesize[0] + x];
        }
    }
    const int luma = (int)(sum / 64);
    return (luma - 16 + 2) / 4;
}

int64_t FrameIndexFromPts(const AVFrame *f, AVStream *st) {
    return av_rescale_q(f->best_effort_timestamp, st->time_base, AVRational{1, kFps});
}

void TestStreamAndDecode() {
    ResetFile();
    GuestContext game = MakeContext(kStackA);
    GuestContext demuxer = MakeContext(kStackB);
    game.rdi = 0x1111;
    game.rsi = 0x2222;
    game.rax = 0x3333;

    AvPlayerFileStreamer streamer(Callbacks());
    AVFormatContext *fmt = OpenInput(streamer, game);
    CHECK(g_file.open_calls == 1 && g_file.size_calls == 1);
    CHECK(streamer.file_size() == g_file.data.size());
    CHECK(game.rsp == kStackA);
    CHECK(game.rdi == 0x1111 && game.rsi == 0x2222 && game.rax == 0x3333);
    CHECK(g_vm_allocs - g_vm_frees == 1);
    const int probe_reads = g_file.read_calls.load();
    CHECK(probe_reads > 0);

    const int vidx = FindStream(fmt, AVMEDIA_TYPE_VIDEO);
    const int aidx = FindStream(fmt, AVMEDIA_TYPE_AUDIO);
    CHECK(fmt->streams[vidx]->codecpar->width == kWidth);
    CHECK(fmt->streams[aidx]->codecpar->sample_rate == kSampleRate);

    // Demux and decode on another thread with its own context, as the demuxer worker does.
    int video_frames = 0;
    int audio_frames = 0;
    std::vector<PacketKey> packets;
    std::thread worker([&]() {
        streamer.bind(&demuxer);
        g_file.expected_ctx = &demuxer;
        AVCodecContext *vdec = OpenDecoder(fmt->streams[vidx]);
        AVCodecContext *adec = OpenDecoder(fmt->streams[aidx]);
        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        auto receive = [&](AVCodecContext *dec) {
            while (avcodec_receive_frame(dec, frame) == 0) {
                if (dec == vdec) {
                    CHECK(FrameIndexFromLuma(frame) == video_frames);
                    CHECK(FrameIndexFromPts(frame, fmt->streams[vidx]) == video_frames);
                    video_frames++;
                } else {
                    audio_frames++;
                }
                av_frame_unref(frame);
            }
        };
        int ret;
        while ((ret = av_read_frame(fmt, pkt)) >= 0) {
            packets.push_back(KeyOf(pkt));
            AVCodecContext *dec = pkt->stream_index == vidx ? vdec : pkt->stream_index == aidx ? adec : nullptr;
            if (dec) {
                CHECK(avcodec_send_packet(dec, pkt) >= 0);
                receive(dec);
            }
            av_packet_unref(pkt);
        }
        CHECK(ret == AVERROR_EOF);
        CHECK(avcodec_send_packet(vdec, nullptr) >= 0);
        receive(vdec);
        CHECK(avcodec_send_packet(adec, nullptr) >= 0);
        receive(adec);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&vdec);
        avcodec_free_context(&adec);
    });
    worker.join();
    // The streamer delivers exactly what FFmpeg's file I/O demuxes; every video packet decodes
    // to its frame. (The MP4 muxer's edit list drops the last video frame on demux.)
    CHECK(packets == g_reference);
    int reference_video = 0;
    for (const PacketKey &k : g_reference) reference_video += k.stream == vidx;
    CHECK(video_frames == reference_video && video_frames >= kFrames - 1);
    CHECK(audio_frames >= kSampleRate * 2 / 1024 - 4);
    CHECK(g_file.read_calls.load() > probe_reads);
    CHECK(demuxer.rsp == kStackB);

    // Close on the closing thread's context.
    avformat_close_input(&fmt);
    g_file.expected_ctx = &game;
    streamer.close(&game);
    CHECK(g_file.close_calls == 1);
    CHECK(streamer.avio() == nullptr);
    CHECK(g_vm_allocs == g_vm_frees);
    streamer.close(&game);
    CHECK(g_file.close_calls == 1);
    printf("[+] streams and decodes %d video and %d audio frames through the guest callbacks "
           "(%zu bytes, %d reads)\n",
           video_frames, audio_frames, g_file.data.size(), g_file.read_calls.load());
}

void TestSeek() {
    ResetFile();
    GuestContext game = MakeContext(kStackA);
    AvPlayerFileStreamer streamer(Callbacks());
    AVFormatContext *fmt = OpenInput(streamer, game);
    AVIOContext *pb = streamer.avio();
    const int64_t size = (int64_t)g_file.data.size();

    // AVSEEK_SIZE, SEEK_SET, SEEK_CUR and SEEK_END on the callback itself (avio_seek forwards
    // only SEEK_SET and SEEK_CUR).
    CHECK(pb->seek(pb->opaque, 0, AVSEEK_SIZE) == size);
    CHECK(avio_size(pb) == size);
    CHECK(pb->seek(pb->opaque, -16, SEEK_END) == size - 16);
    CHECK(pb->seek(pb->opaque, 100, SEEK_SET) == 100);
    CHECK(pb->seek(pb->opaque, 50, SEEK_CUR) == 150);
    CHECK(pb->seek(pb->opaque, 200, SEEK_SET | AVSEEK_FORCE) == 200);
    CHECK(pb->seek(pb->opaque, -1, SEEK_SET) == AVERROR(EINVAL));
    CHECK(pb->seek(pb->opaque, -(size + 1), SEEK_END) == AVERROR(EINVAL));
    CHECK(pb->seek(pb->opaque, 0, 7) == AVERROR(EINVAL));

    // Byte reads after seeks return the file's bytes; reads past the end hit EOF.
    uint8_t tail[16];
    CHECK(avio_seek(pb, size - 16, SEEK_SET) == size - 16);
    CHECK(avio_read(pb, tail, 16) == 16);
    CHECK(memcmp(tail, g_file.data.data() + size - 16, 16) == 0);
    CHECK(avio_read(pb, tail, 1) == AVERROR_EOF);
    const int64_t mid = size / 2 + 3;
    CHECK(avio_seek(pb, mid, SEEK_SET) == mid);
    CHECK(avio_seek(pb, 5, SEEK_CUR) == mid + 5);
    CHECK(avio_read(pb, tail, 16) == 16);
    CHECK(memcmp(tail, g_file.data.data() + mid + 5, 16) == 0);

    // Demuxer seek to frame 30 (every frame is a key frame).
    const int vidx = FindStream(fmt, AVMEDIA_TYPE_VIDEO);
    AVStream *vst = fmt->streams[vidx];
    const int64_t ts = av_rescale_q(30, AVRational{1, kFps}, vst->time_base);
    CHECK(av_seek_frame(fmt, vidx, ts, AVSEEK_FLAG_BACKWARD) >= 0);
    AVCodecContext *vdec = OpenDecoder(vst);
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int first = -1;
    while (first < 0 && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vidx) {
            CHECK(avcodec_send_packet(vdec, pkt) >= 0);
            if (avcodec_receive_frame(vdec, frame) == 0) {
                first = FrameIndexFromLuma(frame);
                CHECK(FrameIndexFromPts(frame, vst) == first);
            }
        }
        av_packet_unref(pkt);
    }
    CHECK(first == 30);

    // Back to the start, as looping playback does.
    CHECK(avformat_seek_file(fmt, -1, 0, 0, 0, 0) >= 0);
    avcodec_flush_buffers(vdec);
    first = -1;
    while (first < 0 && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vidx) {
            CHECK(avcodec_send_packet(vdec, pkt) >= 0);
            if (avcodec_receive_frame(vdec, frame) == 0) first = FrameIndexFromLuma(frame);
        }
        av_packet_unref(pkt);
    }
    CHECK(first == 0);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&vdec);
    avformat_close_input(&fmt);
    streamer.close(&game);
    CHECK(g_file.close_calls == 1 && g_vm_allocs == g_vm_frees);
    printf("[+] seeks: AVSEEK_SIZE, SEEK_SET/CUR/END, invalid targets, demuxer seek and loop\n");
}

void TestErrors() {
    GuestContext game = MakeContext(kStackA);
    const int32_t kErrNoEnt = (int32_t)0x80020002;
    const int32_t kErrIo = (int32_t)0x80020005;

    // open fails: nothing is opened, allocated or closed.
    ResetFile();
    g_file.expected_ctx = &game;
    g_file.open_result = kErrNoEnt;
    {
        AvPlayerFileStreamer streamer(Callbacks());
        CHECK(!streamer.open(&game, kPath));
        CHECK(streamer.last_guest_error() == kErrNoEnt);
        CHECK(streamer.avio() == nullptr);
        streamer.close(&game);
    }
    CHECK(g_file.open_calls == 1 && g_file.size_calls == 0 && g_file.close_calls == 0);
    CHECK(g_vm_allocs == g_vm_frees);

    // read_offset fails in the middle of the file: the demuxer gets EIO, not EOF.
    ResetFile();
    {
        AvPlayerFileStreamer streamer(Callbacks());
        AVFormatContext *fmt = OpenInput(streamer, game);
        g_file.fail_at = g_file.data.size() / 2;
        g_file.read_error = kErrIo;
        AVPacket *pkt = av_packet_alloc();
        int ret;
        while ((ret = av_read_frame(fmt, pkt)) >= 0) av_packet_unref(pkt);
        CHECK(ret == AVERROR(EIO));
        CHECK(streamer.last_guest_error() == kErrIo);
        av_packet_free(&pkt);
        avformat_close_input(&fmt);
        streamer.close(&game);
        CHECK(g_file.close_calls == 1);
    }

    // A failing open probe leaves nothing open once the owner closes the streamer.
    ResetFile();
    g_file.fail_at = 0;
    g_file.read_error = kErrIo;
    {
        AvPlayerFileStreamer streamer(Callbacks());
        CHECK(streamer.open(&game, kPath));
        AVFormatContext *fmt = avformat_alloc_context();
        fmt->pb = streamer.avio();
        fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
        CHECK(avformat_open_input(&fmt, kGuestPath, nullptr, nullptr) < 0);
        CHECK(fmt == nullptr);
        CHECK(streamer.last_guest_error() == kErrIo);
        streamer.close(&game);
        CHECK(g_file.close_calls == 1);
    }

    // The guest returns more bytes than requested: an I/O error, not a buffer overrun.
    ResetFile();
    g_file.over_read = true;
    {
        AvPlayerFileStreamer streamer(Callbacks());
        CHECK(streamer.open(&game, kPath));
        uint8_t b[16];
        CHECK(avio_read(streamer.avio(), b, sizeof(b)) == AVERROR(EIO));
        streamer.close(&game);
    }

    // The guest has no data below the size it reported: EOF.
    ResetFile();
    g_file.short_at = 0;
    {
        AvPlayerFileStreamer streamer(Callbacks());
        CHECK(streamer.open(&game, kPath));
        uint8_t b[16];
        CHECK(avio_read(streamer.avio(), b, sizeof(b)) == AVERROR_EOF);
        streamer.close(&game);
    }

    // close fails: the error is recorded and the streamer is released.
    ResetFile();
    g_file.close_result = kErrIo;
    {
        AvPlayerFileStreamer streamer(Callbacks());
        CHECK(streamer.open(&game, kPath));
        streamer.close(&game);
        CHECK(streamer.last_guest_error() == kErrIo);
        CHECK(streamer.avio() == nullptr);
    }
    CHECK(g_vm_allocs == g_vm_frees);
    CHECK(game.rsp == kStackA);

    // Completeness of the callback set.
    AvPlayerFileReplacement fr = Callbacks();
    CHECK(AvPlayerFileStreamer::complete(fr));
    fr.size = 0;
    CHECK(!AvPlayerFileStreamer::complete(fr));
    printf("[+] guest open/read/close errors propagate; over-long and empty reads are rejected\n");
}

} // namespace

// Guest VM allocator for the streamer's read buffer: a bump allocator in guest memory.
extern "C" uint64_t recomp_vm_alloc_named(GuestContext *ctx, size_t size, int, int, const char *) {
    CHECK(ctx && ctx->mem_base == g_mem);
    CHECK(size == AvPlayerFileStreamer::kChunkSize);
    const uint64_t addr = g_vm_next;
    g_vm_next += (size + 0xfff) & ~0xfffULL;
    CHECK(g_vm_next <= kMemSize);
    g_vm_allocs++;
    return addr;
}

extern "C" int recomp_vm_free(GuestContext *ctx, uint64_t addr, size_t size) {
    CHECK(ctx && ctx->mem_base == g_mem);
    CHECK(addr >= kVmBase && addr < g_vm_next && size == AvPlayerFileStreamer::kChunkSize);
    g_vm_frees++;
    return 0;
}

int main() {
    av_log_set_level(AV_LOG_ERROR);
    g_mem = (uint8_t *)calloc(1, kMemSize);
    CHECK(g_mem);
    memcpy(g_mem + kPath, kGuestPath, sizeof(kGuestPath));
    RegisterGuestFunction(kFnOpen, FakeOpen);
    RegisterGuestFunction(kFnClose, FakeClose);
    RegisterGuestFunction(kFnRead, FakeRead);
    RegisterGuestFunction(kFnSize, FakeSize);

    g_file.data = EncodeTestMovie();
    CHECK(g_file.data.size() > 8 * AvPlayerFileStreamer::kChunkSize);

    TestStreamAndDecode();
    TestSeek();
    TestErrors();
    printf("[+] avplayer file streamer: all tests passed\n");
    return 0;
}
