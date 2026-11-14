// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4_avplayer_file_streamer.h"
#include "recomp_runtime.h"

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <cstdio>
#include <cstring>
#include <sys/mman.h>

#define AVPLAYER_FILE_LOG(fmt, ...) \
    fprintf(stderr, "[libSceAvPlayer] " fmt "\n", ##__VA_ARGS__)

AvPlayerFileStreamer::AvPlayerFileStreamer(const AvPlayerFileReplacement &callbacks)
    : m_callbacks(callbacks) {}

AvPlayerFileStreamer::~AvPlayerFileStreamer() {
    // close() is the owner's job: it needs the GuestContext of the calling thread.
    if (m_guest_open) {
        AVPLAYER_FILE_LOG("File streamer destroyed while the guest file is still open");
    }
}

bool AvPlayerFileStreamer::complete(const AvPlayerFileReplacement &callbacks) {
    return callbacks.open && callbacks.close && callbacks.read_offset && callbacks.size;
}

bool AvPlayerFileStreamer::open(GuestContext *ctx, uint64_t path_gaddr) {
    if (!ctx || m_guest_open) return false;
    bind(ctx);
    m_last_guest_error = 0;
    m_position = 0;

    const int32_t fd = (int32_t)avplayer_call_guest(ctx, m_callbacks.open, m_callbacks.object_ptr,
                                                    path_gaddr, 0, 0);
    if (fd < 0) {
        m_last_guest_error = fd;
        AVPLAYER_FILE_LOG("Guest file open failed: 0x%08x", (uint32_t)fd);
        return false;
    }
    m_guest_open = true;
    const uint64_t size = avplayer_call_guest(ctx, m_callbacks.size, m_callbacks.object_ptr, 0, 0, 0);
    // A set sign bit is a guest error code; an empty file has nothing to demux.
    if (size == 0 || (int64_t)size < 0) {
        if (size != 0) m_last_guest_error = (int32_t)size;
        AVPLAYER_FILE_LOG("Guest file size query returned 0x%llx", (unsigned long long)size);
        release(ctx);
        return false;
    }
    m_file_size = size;

    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    const uint64_t gaddr = recomp_vm_alloc_named(proc, kChunkSize, PROT_READ | PROT_WRITE, 0,
                                                 "avplayer_file");
    if (gaddr == (uint64_t)-1) {
        AVPLAYER_FILE_LOG("Out of guest memory for the file read buffer");
        release(ctx);
        return false;
    }
    m_bounce_gaddr = gaddr;
    m_bounce = ctx->mem_base + gaddr;

    // avio_context_free releases this buffer (or the one AVIO replaced it with).
    uint8_t *avio_buf = (uint8_t *)av_malloc(kChunkSize);
    if (avio_buf) {
        m_avio = avio_alloc_context(avio_buf, (int)kChunkSize, 0, this, &read_packet, nullptr, &seek);
    }
    if (!m_avio) {
        av_free(avio_buf);
        AVPLAYER_FILE_LOG("Failed to allocate the AVIO context");
        release(ctx);
        return false;
    }
    return true;
}

void AvPlayerFileStreamer::close(GuestContext *ctx) {
    if (!m_guest_open) return;
    bind(ctx);
    release(ctx);
}

void AvPlayerFileStreamer::release(GuestContext *ctx) {
    if (m_avio) {
        avio_context_free(&m_avio);
    }
    if (m_bounce_gaddr) {
        GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
        recomp_vm_free(proc, m_bounce_gaddr, kChunkSize);
        m_bounce_gaddr = 0;
        m_bounce = nullptr;
    }
    if (m_guest_open) {
        const int32_t res = (int32_t)avplayer_call_guest(ctx, m_callbacks.close, m_callbacks.object_ptr,
                                                         0, 0, 0);
        if (res < 0) {
            m_last_guest_error = res;
            AVPLAYER_FILE_LOG("Guest file close failed: 0x%08x", (uint32_t)res);
        }
        m_guest_open = false;
    }
    m_position = 0;
    m_file_size = 0;
}

int AvPlayerFileStreamer::read_packet(void *opaque, uint8_t *buf, int buf_size) {
    auto *self = (AvPlayerFileStreamer *)opaque;
    if (self->m_position >= self->m_file_size) {
        return AVERROR_EOF;
    }
    uint64_t want = (uint64_t)buf_size;
    if (want > kChunkSize) want = kChunkSize;
    if (want > self->m_file_size - self->m_position) want = self->m_file_size - self->m_position;

    const int32_t got = (int32_t)avplayer_call_guest(self->m_ctx, self->m_callbacks.read_offset,
                                                     self->m_callbacks.object_ptr, self->m_bounce_gaddr,
                                                     self->m_position, want);
    if (got < 0) {
        self->m_last_guest_error = got;
        AVPLAYER_FILE_LOG("Guest file read at 0x%llx (0x%llx bytes) failed: 0x%08x",
                          (unsigned long long)self->m_position, (unsigned long long)want, (uint32_t)got);
        return AVERROR(EIO);
    }
    if ((uint64_t)got > want) {
        AVPLAYER_FILE_LOG("Guest file read returned 0x%x bytes for a 0x%llx byte request", (uint32_t)got,
                          (unsigned long long)want);
        return AVERROR(EIO);
    }
    if (got == 0) {
        // The guest has no data below the size it reported.
        return AVERROR_EOF;
    }
    std::memcpy(buf, self->m_bounce, (size_t)got);
    self->m_position += (uint64_t)got;
    return got;
}

int64_t AvPlayerFileStreamer::seek(void *opaque, int64_t offset, int whence) {
    auto *self = (AvPlayerFileStreamer *)opaque;
    if (whence & AVSEEK_SIZE) {
        return (int64_t)self->m_file_size;
    }
    int64_t base = 0;
    switch (whence & ~AVSEEK_FORCE) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = (int64_t)self->m_position;
        break;
    case SEEK_END:
        base = (int64_t)self->m_file_size;
        break;
    default:
        return AVERROR(EINVAL);
    }
    const int64_t target = base + offset;
    if (target < 0) {
        return AVERROR(EINVAL);
    }
    self->m_position = (uint64_t)target;
    return target;
}
