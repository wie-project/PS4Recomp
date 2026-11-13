// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PS4_AVPLAYER_FILE_STREAMER_H
#define PS4_AVPLAYER_FILE_STREAMER_H

#include "ps4_avplayer.h"
#include "recomp_context.h"

struct AVIOContext;

// Calls a guest function with up to four SysV integer arguments and returns RAX. The argument
// registers and RAX of ctx are restored afterwards, so a shim that is running on ctx keeps its
// own inputs.
static inline uint64_t avplayer_call_guest(GuestContext *ctx, uint64_t fn, uint64_t a0, uint64_t a1,
                                           uint64_t a2, uint64_t a3) {
    const uint64_t saved_rdi = ctx->rdi;
    const uint64_t saved_rsi = ctx->rsi;
    const uint64_t saved_rdx = ctx->rdx;
    const uint64_t saved_rcx = ctx->rcx;
    const uint64_t saved_rax = ctx->rax;
    ctx->rdi = a0;
    ctx->rsi = a1;
    ctx->rdx = a2;
    ctx->rcx = a3;
    recomp_call_guest(ctx, fn);
    const uint64_t res = ctx->rax;
    ctx->rdi = saved_rdi;
    ctx->rsi = saved_rsi;
    ctx->rdx = saved_rdx;
    ctx->rcx = saved_rcx;
    ctx->rax = saved_rax;
    return res;
}

// Streams a source through the game's AvPlayerFileReplacement callbacks
// (open/close/read_offset/size) as an FFmpeg AVIOContext.
//
// The callbacks are guest code, so they run on the GuestContext of the host thread that drives
// the AVFormatContext. Only one thread drives a source at a time (the caller of AddSource, then
// the demuxer thread, then the caller of Close); each binds its context with bind() before it
// touches the source. read_offset writes into guest memory, so reads go through a bounce buffer
// allocated in guest memory.
class AvPlayerFileStreamer {
public:
    // Bytes requested from the guest per read_offset call (also the AVIO buffer size).
    static constexpr uint32_t kChunkSize = 64 * 1024;

    explicit AvPlayerFileStreamer(const AvPlayerFileReplacement &callbacks);
    ~AvPlayerFileStreamer();

    AvPlayerFileStreamer(const AvPlayerFileStreamer &) = delete;
    AvPlayerFileStreamer &operator=(const AvPlayerFileStreamer &) = delete;

    // True when the game supplied all four callbacks.
    static bool complete(const AvPlayerFileReplacement &callbacks);

    // Opens path_gaddr (a guest string) with the guest open callback on ctx, queries the size
    // and creates the AVIO context. Binds ctx. On failure nothing stays open.
    bool open(GuestContext *ctx, uint64_t path_gaddr);

    // Calls the guest close callback on ctx and releases the AVIO context and bounce buffer.
    // Does nothing when the streamer is not open.
    void close(GuestContext *ctx);

    // Makes the guest callbacks run on ctx from now on.
    void bind(GuestContext *ctx) { m_ctx = ctx; }

    AVIOContext *avio() const { return m_avio; }
    uint64_t file_size() const { return m_file_size; }
    // The last negative value a guest callback returned (0 when none did).
    int32_t last_guest_error() const { return m_last_guest_error; }

private:
    static int read_packet(void *opaque, uint8_t *buf, int buf_size);
    static int64_t seek(void *opaque, int64_t offset, int whence);
    void release(GuestContext *ctx);

    AvPlayerFileReplacement m_callbacks{};
    GuestContext *m_ctx = nullptr;
    AVIOContext *m_avio = nullptr;
    uint64_t m_bounce_gaddr = 0;
    uint8_t *m_bounce = nullptr;
    uint64_t m_position = 0;
    uint64_t m_file_size = 0;
    int32_t m_last_guest_error = 0;
    bool m_guest_open = false;
};

#endif // PS4_AVPLAYER_FILE_STREAMER_H
