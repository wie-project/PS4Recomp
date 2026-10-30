// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4_avplayer.h"
#include "ps4_vfs.h"
#include "recomp_runtime.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#define AVPLAYER_LOG(fmt, ...) \
    fprintf(stderr, "[libSceAvPlayer] " fmt "\n", ##__VA_ARGS__)

static inline uint32_t align_up(uint32_t val, uint32_t alignment) {
    return (val + alignment - 1) & ~(alignment - 1);
}

static inline void *recomp_guest_to_host(const GuestContext *ctx, uint64_t gaddr) {
    if (!ctx || !ctx->mem_base || gaddr == 0) return nullptr;
    if (gaddr >= (uintptr_t)ctx->mem_base && gaddr < (uintptr_t)ctx->mem_base + ctx->mem_size) {
        return (void *)gaddr;
    }
    if (gaddr < ctx->mem_size) {
        return (void *)(ctx->mem_base + gaddr);
    }
    return nullptr;
}

static inline uint64_t recomp_host_to_guest(const GuestContext *ctx, const void *hptr) {
    if (!ctx || !ctx->mem_base || !hptr) return 0;
    uintptr_t p = (uintptr_t)hptr;
    uintptr_t base = (uintptr_t)ctx->mem_base;
    if (p >= base && p < base + ctx->mem_size) {
        return (uint64_t)(p - base);
    }
    return (uint64_t)p;
}

static uint64_t call_guest_allocate(GuestContext *ctx, uint64_t fn_addr, uint64_t obj_ptr, uint32_t align, uint32_t size) {
    if (!fn_addr) return 0;
    uint64_t saved_rdi = ctx->rdi;
    uint64_t saved_rsi = ctx->rsi;
    uint64_t saved_rdx = ctx->rdx;
    uint64_t saved_rax = ctx->rax;

    ctx->rdi = obj_ptr;
    ctx->rsi = align;
    ctx->rdx = size;
    recomp_call_guest(ctx, fn_addr);
    uint64_t res = ctx->rax;

    ctx->rdi = saved_rdi;
    ctx->rsi = saved_rsi;
    ctx->rdx = saved_rdx;
    ctx->rax = saved_rax;
    return res;
}

static void call_guest_deallocate(GuestContext *ctx, uint64_t fn_addr, uint64_t obj_ptr, uint64_t mem) {
    if (!fn_addr || !mem) return;
    uint64_t saved_rdi = ctx->rdi;
    uint64_t saved_rsi = ctx->rsi;
    uint64_t saved_rax = ctx->rax;

    ctx->rdi = obj_ptr;
    ctx->rsi = mem;
    recomp_call_guest(ctx, fn_addr);

    ctx->rdi = saved_rdi;
    ctx->rsi = saved_rsi;
    ctx->rax = saved_rax;
}

static void copy_nv12_data(uint8_t *dst, uint64_t max_size, const AVFrame *src) {
    const uint32_t dst_pitch = align_up((uint32_t)src->width, 64);
    const uint32_t dst_height = align_up((uint32_t)src->height, 16);

    if (((uint64_t)dst_pitch * dst_height * 3) / 2 > max_size) {
        return;
    }

    uint8_t *luma_dst = dst;
    uint8_t *chroma_dst = dst + dst_pitch * dst_height;

    if ((uint32_t)src->width != dst_pitch) {
        for (int y = 0; y < src->height; ++y) {
            std::memcpy(luma_dst + y * dst_pitch, src->data[0] + y * src->linesize[0], src->width);
        }
        for (int y = 0; y < src->height / 2; ++y) {
            std::memcpy(chroma_dst + y * dst_pitch, src->data[1] + y * src->linesize[1], src->width);
        }
    } else {
        std::memcpy(luma_dst, src->data[0], src->width * src->height);
        std::memcpy(chroma_dst, src->data[1], (src->width * src->height) / 2);
    }

    if ((uint32_t)src->height != dst_height) {
        const int ly = src->height - 1;
        for (uint32_t y = (uint32_t)src->height; y < dst_height; ++y) {
            std::memcpy(luma_dst + y * dst_pitch, src->data[0] + ly * src->linesize[0], src->width);
        }
        const int cy = (src->height / 2) - 1;
        for (uint32_t y = (uint32_t)(src->height / 2); y < dst_height / 2; ++y) {
            std::memcpy(chroma_dst + y * dst_pitch, src->data[1] + cy * src->linesize[1], src->width);
        }
    }
}

static AvPlayerStreamType codec_type_to_stream_type(AVMediaType codec_type) {
    switch (codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        return AVPLAYER_STREAM_TYPE_VIDEO;
    case AVMEDIA_TYPE_AUDIO:
        return AVPLAYER_STREAM_TYPE_AUDIO;
    case AVMEDIA_TYPE_SUBTITLE:
        return AVPLAYER_STREAM_TYPE_TIMED_TEXT;
    default:
        return AVPLAYER_STREAM_TYPE_UNKNOWN;
    }
}

static uint64_t timestamp_to_millis(int64_t timestamp, AVRational time_base) {
    if (timestamp == AV_NOPTS_VALUE || timestamp <= 0 || time_base.num <= 0 || time_base.den <= 0) {
        return 0;
    }
    int64_t millis = av_rescale_q(timestamp, time_base, AVRational{1, 1000});
    return millis > 0 ? (uint64_t)millis : 0;
}

static uint64_t frame_timestamp_millis(const AVFrame *frame, AVRational time_base) {
    int64_t ts = frame->best_effort_timestamp;
    if (ts == AV_NOPTS_VALUE) ts = frame->pts;
    if (ts == AV_NOPTS_VALUE) ts = frame->pkt_dts;
    return timestamp_to_millis(ts, time_base);
}

struct GuestBuffer {
    uint64_t guest_addr = 0;
    uint8_t *host_ptr = nullptr;
    uint64_t size = 0;
    bool is_texture = false;
    bool vm_allocated = false;

    GuestBuffer() = default;
    GuestBuffer(uint64_t gaddr, uint8_t *hptr, uint64_t sz, bool tex, bool vm)
        : guest_addr(gaddr), host_ptr(hptr), size(sz), is_texture(tex), vm_allocated(vm) {}

    GuestBuffer(const GuestBuffer &) = delete;
    GuestBuffer &operator=(const GuestBuffer &) = delete;

    GuestBuffer(GuestBuffer &&o) noexcept
        : guest_addr(o.guest_addr), host_ptr(o.host_ptr), size(o.size),
          is_texture(o.is_texture), vm_allocated(o.vm_allocated) {
        o.guest_addr = 0;
        o.host_ptr = nullptr;
        o.size = 0;
    }

    GuestBuffer &operator=(GuestBuffer &&o) noexcept {
        if (this != &o) {
            guest_addr = o.guest_addr;
            host_ptr = o.host_ptr;
            size = o.size;
            is_texture = o.is_texture;
            vm_allocated = o.vm_allocated;
            o.guest_addr = 0;
            o.host_ptr = nullptr;
            o.size = 0;
        }
        return *this;
    }
};

template <typename T>
class SafeQueue {
public:
    void push(T &&item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::move(item));
        m_cv.notify_one();
    }

    std::optional<T> pop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) return std::nullopt;
        T val = std::move(m_queue.front());
        m_queue.pop();
        return val;
    }

    bool wait_pop(const std::atomic<bool> &stop_flag, T &out) {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_queue.empty() && !stop_flag.load()) {
            m_cv.wait_for(lock, std::chrono::milliseconds(10));
        }
        if (m_queue.empty() || stop_flag.load()) return false;
        out = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!m_queue.empty()) m_queue.pop();
        m_cv.notify_all();
    }

    void notify_all() {
        m_cv.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<T> m_queue;
};

struct DecodedVideoFrame {
    GuestBuffer buffer;
    AvPlayerFrameInfoEx info{};
};

struct DecodedAudioFrame {
    GuestBuffer buffer;
    AvPlayerFrameInfo info{};
};

struct StreamDesc {
    uint32_t ffmpeg_index;
    AvPlayerStreamInfo info;
};

enum class AvState {
    Unknown,
    Initial,
    AddingSource,
    Ready,
    Play,
    Pause,
    Stop,
    EndOfFile,
    Buffering,
    Error,
};

class AvPlayer {
public:
    AvPlayer(GuestContext *ctx, const AvPlayerInitData &data)
        : m_init_data(data), m_main_ctx(ctx) {
        if (ctx) {
            m_proc_ctx = ctx->process_ctx ? ctx->process_ctx : ctx;
            m_controller_ctx = recomp_create_thread_context(m_proc_ctx, 1024 * 1024);
        }
        if (m_init_data.default_language && ctx) {
            const char *lang = (const char *)recomp_guest_to_host(ctx, m_init_data.default_language);
            if (lang) {
                strncpy(m_default_language, lang, sizeof(m_default_language) - 1);
            }
        }
        m_state.store(AvState::Initial);
        m_controller_stop.store(false);
        m_controller_thread = std::thread([this]() {
            controller_worker();
        });
    }

    ~AvPlayer() {
        stop_playback(m_main_ctx);
        m_controller_stop.store(true);
        m_event_queue.notify_all();
        if (m_controller_thread.joinable()) {
            m_controller_thread.join();
        }
        if (m_controller_ctx) {
            recomp_free_thread_context(m_controller_ctx);
            m_controller_ctx = nullptr;
        }
    }

    void post_init(const AvPlayerPostInitData &data) {
        m_post_init_data = data;
    }

    int32_t add_source(const char *path) {
        if (!path || path[0] == '\0') return ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        m_state.store(AvState::AddingSource);
        m_event_queue.push(AVPLAYER_EVENT_STATE_READY);

        char host_path[1024] = {0};
        const char *open_path = path;
        if (ps4_vfs_resolve(path, host_path, sizeof(host_path)) == 0) {
            open_path = host_path;
        }

        AVFormatContext *context = avformat_alloc_context();
        if (!context) {
            AVPLAYER_LOG("Failed to allocate AVFormatContext for %s", path);
            m_state.store(AvState::Error);
            return ORBIS_AVPLAYER_ERROR_OPERATION_FAILED;
        }

        if (avformat_open_input(&context, open_path, nullptr, nullptr) < 0) {
            AVPLAYER_LOG("Failed to open video file %s (open_path: %s)", path, open_path);
            avformat_free_context(context);
            m_state.store(AvState::Error);
            return ORBIS_AVPLAYER_ERROR_OPERATION_FAILED;
        }

        if (avformat_find_stream_info(context, nullptr) < 0) {
            AVPLAYER_LOG("Failed to find stream info for %s", path);
            avformat_close_input(&context);
            m_state.store(AvState::Error);
            return ORBIS_AVPLAYER_ERROR_OPERATION_FAILED;
        }

        m_fmt_ctx = context;
        m_streams.clear();
        for (uint32_t i = 0; i < m_fmt_ctx->nb_streams; ++i) {
            AVStream *stream = m_fmt_ctx->streams[i];
            auto codec_type = stream->codecpar->codec_type;
            if (codec_type != AVMEDIA_TYPE_VIDEO && codec_type != AVMEDIA_TYPE_AUDIO) {
                continue;
            }
            StreamDesc desc{};
            desc.ffmpeg_index = i;
            desc.info.type = codec_type_to_stream_type(codec_type);
            desc.info.start_time = 0;
            desc.info.duration = timestamp_to_millis(stream->duration, stream->time_base);
            if (desc.info.duration == 0 && m_fmt_ctx->duration > 0) {
                desc.info.duration = (uint64_t)(m_fmt_ctx->duration / (AV_TIME_BASE / 1000));
            }

            AVDictionaryEntry *lang_entry = av_dict_get(stream->metadata, "language", nullptr, 0);
            if (desc.info.type == AVPLAYER_STREAM_TYPE_VIDEO) {
                desc.info.details.video.width = align_up((uint32_t)stream->codecpar->width, 16);
                desc.info.details.video.height = align_up((uint32_t)stream->codecpar->height, 16);
                if (stream->codecpar->height > 0) {
                    desc.info.details.video.aspect_ratio = (float)stream->codecpar->width / (float)stream->codecpar->height;
                }
                if (lang_entry && lang_entry->value) {
                    strncpy(desc.info.details.video.language_code, lang_entry->value, 3);
                }
            } else if (desc.info.type == AVPLAYER_STREAM_TYPE_AUDIO) {
                desc.info.details.audio.channel_count = (uint16_t)stream->codecpar->ch_layout.nb_channels;
                desc.info.details.audio.sample_rate = (uint32_t)stream->codecpar->sample_rate;
                desc.info.details.audio.size = 0;
                if (lang_entry && lang_entry->value) {
                    strncpy(desc.info.details.audio.language_code, lang_entry->value, 3);
                }
            }
            m_streams.push_back(desc);
        }

        m_state.store(AvState::Ready);
        AVPLAYER_LOG("Source opened successfully: %s, found %zu supported streams", path, m_streams.size());
        return ORBIS_OK;
    }

    int32_t get_stream_count() const {
        return (int32_t)m_streams.size();
    }

    bool get_stream_info(uint32_t stream_index, AvPlayerStreamInfo *info) const {
        if (!info || stream_index >= m_streams.size()) return false;
        *info = m_streams[stream_index].info;
        return true;
    }

    bool enable_stream(uint32_t stream_index) {
        if (stream_index >= m_streams.size()) return false;
        const auto &s = m_streams[stream_index];
        if (s.info.type == AVPLAYER_STREAM_TYPE_VIDEO) {
            m_video_stream_idx = (int)s.ffmpeg_index;
            AVPLAYER_LOG("Enabled video stream #%u (ffmpeg index %u)", stream_index, s.ffmpeg_index);
        } else if (s.info.type == AVPLAYER_STREAM_TYPE_AUDIO) {
            m_audio_stream_idx = (int)s.ffmpeg_index;
            AVPLAYER_LOG("Enabled audio stream #%u (ffmpeg index %u)", stream_index, s.ffmpeg_index);
        }
        return true;
    }

    bool start_playback(GuestContext *ctx) {
        std::unique_lock<std::mutex> lock(m_state_mutex);
        if (m_video_stream_idx < 0 && m_audio_stream_idx < 0) {
            AVPLAYER_LOG("Cannot start playback: no streams enabled");
            return false;
        }

        stop_playback_internal(ctx);

        uint32_t max_fb = (uint32_t)std::min(std::max(2, m_init_data.num_output_video_framebuffers), 16);

        if (m_video_stream_idx >= 0) {
            AVStream *stream = m_fmt_ctx->streams[m_video_stream_idx];
            const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!decoder) {
                AVPLAYER_LOG("Could not find video decoder for codec id %d", stream->codecpar->codec_id);
                return false;
            }
            m_video_codec_ctx = avcodec_alloc_context3(decoder);
            avcodec_parameters_to_context(m_video_codec_ctx, stream->codecpar);
            if (avcodec_open2(m_video_codec_ctx, decoder, nullptr) < 0) {
                AVPLAYER_LOG("Failed to open video codec");
                return false;
            }

            uint32_t pitch = align_up((uint32_t)m_video_codec_ctx->width, 64);
            uint32_t height = align_up((uint32_t)m_video_codec_ctx->height, 16);
            uint32_t frame_size = (pitch * height * 3) / 2;

            for (uint32_t i = 0; i < max_fb; ++i) {
                GuestBuffer buf = allocate_buffer(ctx, 0x100, frame_size, true);
                if (!buf.host_ptr) {
                    AVPLAYER_LOG("Failed to allocate video framebuffer %u", i);
                    return false;
                }
                m_free_video_buffers.push(std::move(buf));
            }
        }

        if (m_audio_stream_idx >= 0) {
            AVStream *stream = m_fmt_ctx->streams[m_audio_stream_idx];
            const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!decoder) {
                AVPLAYER_LOG("Could not find audio decoder for codec id %d", stream->codecpar->codec_id);
                return false;
            }
            m_audio_codec_ctx = avcodec_alloc_context3(decoder);
            avcodec_parameters_to_context(m_audio_codec_ctx, stream->codecpar);
            if (avcodec_open2(m_audio_codec_ctx, decoder, nullptr) < 0) {
                AVPLAYER_LOG("Failed to open audio codec");
                return false;
            }

            uint32_t max_channels = 8;
            uint32_t audio_buf_size = max_channels * sizeof(int32_t) * 1024;
            for (uint32_t i = 0; i < max_fb * 2; ++i) {
                GuestBuffer buf = allocate_buffer(ctx, 0x10, audio_buf_size, false);
                if (!buf.host_ptr) {
                    AVPLAYER_LOG("Failed to allocate audio buffer %u", i);
                    return false;
                }
                m_free_audio_buffers.push(std::move(buf));
            }
        }

        m_is_eof.store(false);
        m_is_paused.store(false);
        m_workers_stop.store(false);
        m_total_pause_duration = std::chrono::steady_clock::duration(0);
        m_start_time = std::chrono::steady_clock::now();
        m_state.store(AvState::Play);

        m_demuxer_thread = std::thread([this]() { demuxer_worker(); });
        if (m_video_stream_idx >= 0) {
            m_video_decoder_thread = std::thread([this]() { video_decoder_worker(); });
        }
        if (m_audio_stream_idx >= 0) {
            m_audio_decoder_thread = std::thread([this]() { audio_decoder_worker(); });
        }

        m_event_queue.push(AVPLAYER_EVENT_STATE_PLAY);
        AVPLAYER_LOG("Playback started successfully (video_stream=%d, audio_stream=%d)", m_video_stream_idx, m_audio_stream_idx);
        return true;
    }

    bool stop_playback(GuestContext *ctx) {
        std::unique_lock<std::mutex> lock(m_state_mutex);
        return stop_playback_internal(ctx);
    }

    void pause_playback() {
        if (m_state.load() == AvState::Play) {
            m_pause_time = std::chrono::steady_clock::now();
            m_is_paused.store(true);
            m_state.store(AvState::Pause);
            m_event_queue.push(AVPLAYER_EVENT_STATE_PAUSE);
            AVPLAYER_LOG("Playback paused");
        }
    }

    void resume_playback() {
        if (m_state.load() == AvState::Pause) {
            m_total_pause_duration += (std::chrono::steady_clock::now() - m_pause_time);
            m_is_paused.store(false);
            m_state.store(AvState::Play);
            m_event_queue.push(AVPLAYER_EVENT_STATE_PLAY);
            AVPLAYER_LOG("Playback resumed");
        }
    }

    void set_looping(bool loop) {
        m_is_looping.store(loop);
    }

    uint64_t current_time() {
        if (m_state.load() != AvState::Play && m_state.load() != AvState::Pause) {
            return 0;
        }
        auto now = m_is_paused.load() ? m_pause_time : std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start_time - m_total_pause_duration).count();
        return elapsed > 0 ? (uint64_t)elapsed : 0;
    }

    bool is_active() {
        AvState st = m_state.load();
        return (st == AvState::Play || st == AvState::Pause || st == AvState::Buffering ||
                m_ready_video_frames.size() > 0 || m_ready_audio_frames.size() > 0);
    }

    bool get_video_data_ex(AvPlayerFrameInfoEx *info) {
        if (!info || !is_active() || m_is_paused.load()) return false;
        if (m_ready_video_frames.size() == 0) return false;

        uint64_t now_ms = current_time();
        auto front = m_ready_video_frames.pop();
        if (!front.has_value()) return false;

        if (m_sync_mode == AVPLAYER_AV_SYNC_MODE_DEFAULT && front->info.timestamp > now_ms + 40) {
            m_ready_video_frames.push(std::move(*front));
            return false;
        }

        if (m_curr_video_buf.has_value()) {
            m_free_video_buffers.push(std::move(*m_curr_video_buf));
            m_curr_video_buf.reset();
        }

        *info = front->info;
        m_curr_video_buf = std::move(front->buffer);
        return true;
    }

    bool get_video_data(AvPlayerFrameInfo *info) {
        if (!info) return false;
        AvPlayerFrameInfoEx ex{};
        if (!get_video_data_ex(&ex)) return false;
        info->p_data = ex.p_data;
        info->timestamp = ex.timestamp;
        info->details.video.width = ex.details.video.width;
        info->details.video.height = ex.details.video.height;
        info->details.video.aspect_ratio = ex.details.video.aspect_ratio;
        memcpy(info->details.video.language_code, ex.details.video.language_code, 4);
        return true;
    }

    bool get_audio_data(AvPlayerFrameInfo *info) {
        if (!info || !is_active() || m_is_paused.load()) return false;
        auto front = m_ready_audio_frames.pop();
        if (!front.has_value()) return false;

        if (m_curr_audio_buf.has_value()) {
            m_free_audio_buffers.push(std::move(*m_curr_audio_buf));
            m_curr_audio_buf.reset();
        }

        *info = front->info;
        m_curr_audio_buf = std::move(front->buffer);
        return true;
    }

    void set_av_sync_mode(AvPlayerAvSyncMode sync_mode) {
        m_sync_mode = sync_mode;
    }

private:
    GuestBuffer allocate_buffer(GuestContext *ctx, uint32_t align, uint32_t size, bool is_texture) {
        uint64_t gaddr = 0;
        bool is_vm = false;
        if (is_texture && m_init_data.memory_replacement.allocate_texture && ctx) {
            gaddr = call_guest_allocate(ctx, m_init_data.memory_replacement.allocate_texture,
                                        m_init_data.memory_replacement.object_ptr, align, size);
        } else if (!is_texture && m_init_data.memory_replacement.allocate && ctx) {
            gaddr = call_guest_allocate(ctx, m_init_data.memory_replacement.allocate,
                                        m_init_data.memory_replacement.object_ptr, align, size);
        }

        uint8_t *hptr = nullptr;
        if (gaddr && ctx) {
            hptr = (uint8_t *)recomp_guest_to_host(ctx, gaddr);
        }

        if (!hptr && m_proc_ctx) {
            gaddr = recomp_vm_alloc(m_proc_ctx, size);
            if (gaddr != (uint64_t)-1) {
                hptr = (uint8_t *)recomp_guest_to_host(m_proc_ctx, gaddr);
                is_vm = true;
            } else {
                gaddr = 0;
            }
        }

        return GuestBuffer(gaddr, hptr, size, is_texture, is_vm);
    }

    void free_buffer(GuestContext *ctx, GuestBuffer &buf) {
        if (!buf.guest_addr) return;
        if (buf.vm_allocated && m_proc_ctx) {
            recomp_vm_free(m_proc_ctx, buf.guest_addr, buf.size);
        } else if (buf.is_texture && m_init_data.memory_replacement.deallocate_texture && ctx) {
            call_guest_deallocate(ctx, m_init_data.memory_replacement.deallocate_texture,
                                  m_init_data.memory_replacement.object_ptr, buf.guest_addr);
        } else if (!buf.is_texture && m_init_data.memory_replacement.deallocate && ctx) {
            call_guest_deallocate(ctx, m_init_data.memory_replacement.deallocate,
                                  m_init_data.memory_replacement.object_ptr, buf.guest_addr);
        }
        buf.guest_addr = 0;
        buf.host_ptr = nullptr;
    }

    bool stop_playback_internal(GuestContext *ctx) {
        m_workers_stop.store(true);

        m_video_packets.notify_all();
        m_audio_packets.notify_all();
        m_free_video_buffers.notify_all();
        m_free_audio_buffers.notify_all();
        m_ready_video_frames.notify_all();
        m_ready_audio_frames.notify_all();

        if (m_demuxer_thread.joinable()) m_demuxer_thread.join();
        if (m_video_decoder_thread.joinable()) m_video_decoder_thread.join();
        if (m_audio_decoder_thread.joinable()) m_audio_decoder_thread.join();

        while (auto pkt = m_video_packets.pop()) {
            AVPacket *p = *pkt;
            av_packet_free(&p);
        }
        while (auto pkt = m_audio_packets.pop()) {
            AVPacket *p = *pkt;
            av_packet_free(&p);
        }

        while (auto buf = m_free_video_buffers.pop()) {
            free_buffer(ctx, *buf);
        }
        while (auto buf = m_free_audio_buffers.pop()) {
            free_buffer(ctx, *buf);
        }
        while (auto frame = m_ready_video_frames.pop()) {
            free_buffer(ctx, frame->buffer);
        }
        while (auto frame = m_ready_audio_frames.pop()) {
            free_buffer(ctx, frame->buffer);
        }

        if (m_curr_video_buf.has_value()) {
            free_buffer(ctx, *m_curr_video_buf);
            m_curr_video_buf.reset();
        }
        if (m_curr_audio_buf.has_value()) {
            free_buffer(ctx, *m_curr_audio_buf);
            m_curr_audio_buf.reset();
        }

        if (m_video_codec_ctx) {
            avcodec_free_context(&m_video_codec_ctx);
            m_video_codec_ctx = nullptr;
        }
        if (m_audio_codec_ctx) {
            avcodec_free_context(&m_audio_codec_ctx);
            m_audio_codec_ctx = nullptr;
        }
        if (m_sws_ctx) {
            sws_freeContext(m_sws_ctx);
            m_sws_ctx = nullptr;
        }
        if (m_swr_ctx) {
            swr_free(&m_swr_ctx);
            m_swr_ctx = nullptr;
        }

        m_state.store(AvState::Stop);
        m_event_queue.push(AVPLAYER_EVENT_STATE_STOP);
        return true;
    }

    void demuxer_worker() {
        AVPLAYER_LOG("Demuxer worker started");
        while (!m_workers_stop.load()) {
            if (m_video_packets.size() > 30 || m_audio_packets.size() > 60) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            AVPacket *pkt = av_packet_alloc();
            int ret = av_read_frame(m_fmt_ctx, pkt);
            if (ret < 0) {
                av_packet_free(&pkt);
                if (m_is_looping.load()) {
                    AVPLAYER_LOG("Demuxer EOF reached, looping back to start");
                    avformat_seek_file(m_fmt_ctx, -1, 0, 0, 0, 0);
                    if (m_video_codec_ctx) avcodec_flush_buffers(m_video_codec_ctx);
                    if (m_audio_codec_ctx) avcodec_flush_buffers(m_audio_codec_ctx);
                    m_start_time = std::chrono::steady_clock::now();
                    m_total_pause_duration = std::chrono::steady_clock::duration(0);
                    continue;
                }
                AVPLAYER_LOG("Demuxer EOF reached");
                m_is_eof.store(true);
                break;
            }

            if (pkt->stream_index == m_video_stream_idx) {
                m_video_packets.push(std::move(pkt));
            } else if (pkt->stream_index == m_audio_stream_idx) {
                m_audio_packets.push(std::move(pkt));
            } else {
                av_packet_free(&pkt);
            }
        }
        AVPLAYER_LOG("Demuxer worker ended");
    }

    void video_decoder_worker() {
        AVPLAYER_LOG("Video decoder worker started");
        AVFrame *raw_frame = av_frame_alloc();
        AVFrame *nv12_frame = av_frame_alloc();

        while (!m_workers_stop.load()) {
            AVPacket *pkt = nullptr;
            if (!m_video_packets.wait_pop(m_workers_stop, pkt) || !pkt) {
                if (m_is_eof.load()) break;
                continue;
            }

            int ret = avcodec_send_packet(m_video_codec_ctx, pkt);
            av_packet_free(&pkt);
            if (ret < 0) continue;

            while (ret >= 0 && !m_workers_stop.load()) {
                ret = avcodec_receive_frame(m_video_codec_ctx, raw_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;

                GuestBuffer buf;
                if (!m_free_video_buffers.wait_pop(m_workers_stop, buf)) break;

                uint32_t width = align_up((uint32_t)raw_frame->width, 16);
                uint32_t pitch = align_up((uint32_t)raw_frame->width, 64);
                uint32_t height = align_up((uint32_t)raw_frame->height, 16);

                if (raw_frame->format != AV_PIX_FMT_NV12) {
                    if (!m_sws_ctx) {
                        m_sws_ctx = sws_getContext(raw_frame->width, raw_frame->height, (AVPixelFormat)raw_frame->format,
                                                   raw_frame->width, raw_frame->height, AV_PIX_FMT_NV12,
                                                   SWS_BILINEAR, nullptr, nullptr, nullptr);
                    }
                    nv12_frame->format = AV_PIX_FMT_NV12;
                    nv12_frame->width = raw_frame->width;
                    nv12_frame->height = raw_frame->height;
                    av_frame_get_buffer(nv12_frame, 0);
                    sws_scale(m_sws_ctx, raw_frame->data, raw_frame->linesize, 0, raw_frame->height,
                              nv12_frame->data, nv12_frame->linesize);
                    copy_nv12_data(buf.host_ptr, buf.size, nv12_frame);
                    av_frame_unref(nv12_frame);
                } else {
                    copy_nv12_data(buf.host_ptr, buf.size, raw_frame);
                }

                AVStream *st = m_fmt_ctx->streams[m_video_stream_idx];
                uint64_t ts = frame_timestamp_millis(raw_frame, st->time_base);

                DecodedVideoFrame frame{};
                frame.buffer = std::move(buf);
                frame.info.p_data = frame.buffer.guest_addr;
                frame.info.timestamp = ts;
                frame.info.details.video.width = width;
                frame.info.details.video.height = height;
                frame.info.details.video.pitch = pitch;
                frame.info.details.video.aspect_ratio = (raw_frame->height > 0) ? ((float)raw_frame->width / (float)raw_frame->height) : 1.777f;
                frame.info.details.video.luma_bit_depth = 8;
                frame.info.details.video.chroma_bit_depth = 8;

                m_ready_video_frames.push(std::move(frame));
                av_frame_unref(raw_frame);
            }
        }

        av_frame_free(&raw_frame);
        av_frame_free(&nv12_frame);
        AVPLAYER_LOG("Video decoder worker ended");
    }

    void audio_decoder_worker() {
        AVPLAYER_LOG("Audio decoder worker started");
        AVFrame *raw_frame = av_frame_alloc();
        AVFrame *pcm_frame = av_frame_alloc();

        while (!m_workers_stop.load()) {
            AVPacket *pkt = nullptr;
            if (!m_audio_packets.wait_pop(m_workers_stop, pkt) || !pkt) {
                if (m_is_eof.load()) break;
                continue;
            }

            int ret = avcodec_send_packet(m_audio_codec_ctx, pkt);
            av_packet_free(&pkt);
            if (ret < 0) continue;

            while (ret >= 0 && !m_workers_stop.load()) {
                ret = avcodec_receive_frame(m_audio_codec_ctx, raw_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;

                GuestBuffer buf;
                if (!m_free_audio_buffers.wait_pop(m_workers_stop, buf)) break;

                if (!m_swr_ctx) {
                    SwrContext *swr = nullptr;
                    AVChannelLayout in_layout = raw_frame->ch_layout;
                    AVChannelLayout out_layout = raw_frame->ch_layout;
                    swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_S16, raw_frame->sample_rate,
                                        &in_layout, (AVSampleFormat)raw_frame->format, raw_frame->sample_rate,
                                        0, nullptr);
                    m_swr_ctx = swr;
                    swr_init(m_swr_ctx);
                }

                pcm_frame->format = AV_SAMPLE_FMT_S16;
                pcm_frame->ch_layout = raw_frame->ch_layout;
                pcm_frame->sample_rate = raw_frame->sample_rate;
                swr_convert_frame(m_swr_ctx, pcm_frame, raw_frame);

                uint32_t channels = (uint32_t)pcm_frame->ch_layout.nb_channels;
                uint32_t bytes = channels * (uint32_t)pcm_frame->nb_samples * sizeof(int16_t);
                if (bytes <= buf.size) {
                    memcpy(buf.host_ptr, pcm_frame->data[0], bytes);
                }

                AVStream *st = m_fmt_ctx->streams[m_audio_stream_idx];
                uint64_t ts = frame_timestamp_millis(raw_frame, st->time_base);

                DecodedAudioFrame frame{};
                frame.buffer = std::move(buf);
                frame.info.p_data = frame.buffer.guest_addr;
                frame.info.timestamp = ts;
                frame.info.details.audio.channel_count = (uint16_t)channels;
                frame.info.details.audio.sample_rate = (uint32_t)pcm_frame->sample_rate;
                frame.info.details.audio.size = bytes;

                m_ready_audio_frames.push(std::move(frame));
                av_frame_unref(pcm_frame);
                av_frame_unref(raw_frame);
            }
        }

        av_frame_free(&raw_frame);
        av_frame_free(&pcm_frame);
        AVPLAYER_LOG("Audio decoder worker ended");
    }

    void controller_worker() {
        AVPLAYER_LOG("Controller worker started");
        while (!m_controller_stop.load()) {
            AvPlayerEvents ev;
            if (m_event_queue.wait_pop(m_controller_stop, ev)) {
                if (ev == AVPLAYER_EVENT_STATE_READY && (m_init_data.auto_start || !m_init_data.event_replacement.event_callback)) {
                    // AutoPlay logic
                    int v_idx = -1;
                    int a_idx = -1;
                    for (size_t i = 0; i < m_streams.size(); ++i) {
                        if (m_streams[i].info.type == AVPLAYER_STREAM_TYPE_VIDEO && v_idx == -1) {
                            v_idx = (int)i;
                        }
                        if (m_streams[i].info.type == AVPLAYER_STREAM_TYPE_AUDIO && a_idx == -1) {
                            a_idx = (int)i;
                        }
                    }
                    if (v_idx >= 0) enable_stream((uint32_t)v_idx);
                    if (a_idx >= 0) enable_stream((uint32_t)a_idx);
                    start_playback(m_main_ctx);
                } else if (m_init_data.event_replacement.event_callback && m_controller_ctx) {
                    m_controller_ctx->rdi = m_init_data.event_replacement.object_ptr;
                    m_controller_ctx->rsi = (uint64_t)ev;
                    m_controller_ctx->rdx = 0;
                    m_controller_ctx->rcx = 0;
                    recomp_call_guest(m_controller_ctx, m_init_data.event_replacement.event_callback);
                }
            }
        }
        AVPLAYER_LOG("Controller worker ended");
    }

    AvPlayerInitData m_init_data{};
    AvPlayerPostInitData m_post_init_data{};
    GuestContext *m_main_ctx = nullptr;
    GuestContext *m_proc_ctx = nullptr;
    GuestContext *m_controller_ctx = nullptr;
    char m_default_language[8] = {0};

    AVFormatContext *m_fmt_ctx = nullptr;
    std::vector<StreamDesc> m_streams;
    int m_video_stream_idx = -1;
    int m_audio_stream_idx = -1;

    AVCodecContext *m_video_codec_ctx = nullptr;
    AVCodecContext *m_audio_codec_ctx = nullptr;
    SwsContext *m_sws_ctx = nullptr;
    SwrContext *m_swr_ctx = nullptr;

    SafeQueue<AVPacket *> m_video_packets;
    SafeQueue<AVPacket *> m_audio_packets;
    SafeQueue<GuestBuffer> m_free_video_buffers;
    SafeQueue<GuestBuffer> m_free_audio_buffers;
    SafeQueue<DecodedVideoFrame> m_ready_video_frames;
    SafeQueue<DecodedAudioFrame> m_ready_audio_frames;

    std::optional<GuestBuffer> m_curr_video_buf;
    std::optional<GuestBuffer> m_curr_audio_buf;

    std::chrono::steady_clock::time_point m_start_time;
    std::chrono::steady_clock::time_point m_pause_time;
    std::chrono::steady_clock::duration m_total_pause_duration{0};

    std::atomic<bool> m_is_paused{false};
    std::atomic<bool> m_is_looping{false};
    std::atomic<bool> m_is_eof{false};
    std::atomic<bool> m_workers_stop{false};
    std::atomic<bool> m_controller_stop{false};
    std::atomic<AvState> m_state{AvState::Initial};
    AvPlayerAvSyncMode m_sync_mode = AVPLAYER_AV_SYNC_MODE_DEFAULT;

    std::mutex m_state_mutex;
    SafeQueue<AvPlayerEvents> m_event_queue;

    std::thread m_demuxer_thread;
    std::thread m_video_decoder_thread;
    std::thread m_audio_decoder_thread;
    std::thread m_controller_thread;
};

// ==========================================
// Host Shims
// ==========================================

void shim_sceAvPlayerInit(GuestContext *ctx) {
    if (!ctx->rdi) {
        ctx->rax = 0;
        SHIM_RETURN();
    }
    const AvPlayerInitData *data = (const AvPlayerInitData *)recomp_guest_to_host(ctx, ctx->rdi);
    if (!data) {
        ctx->rax = 0;
        SHIM_RETURN();
    }

    AVPLAYER_LOG("sceAvPlayerInit called (auto_start=%d, num_fb=%d, event_cb=0x%llx)",
                 data->auto_start, data->num_output_video_framebuffers,
                 (unsigned long long)data->event_replacement.event_callback);

    AvPlayer *player = new AvPlayer(ctx, *data);
    ctx->rax = (uint64_t)player;
    SHIM_RETURN();
}

void shim_sceAvPlayerInitEx(GuestContext *ctx) {
    if (!ctx->rdi || !ctx->rsi) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    const AvPlayerInitDataEx *p_data = (const AvPlayerInitDataEx *)recomp_guest_to_host(ctx, ctx->rdi);
    uint64_t *out_handle = (uint64_t *)recomp_guest_to_host(ctx, ctx->rsi);
    if (!p_data || !out_handle) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }

    AvPlayerInitData data{};
    data.memory_replacement = p_data->memory_replacement;
    data.file_replacement = p_data->file_replacement;
    data.event_replacement = p_data->event_replacement;
    data.default_language = p_data->default_language;
    data.debug_level = p_data->debug_level;
    data.num_output_video_framebuffers = p_data->num_output_video_framebuffers;
    data.auto_start = p_data->auto_start;

    AvPlayer *player = new AvPlayer(ctx, data);
    *out_handle = (uint64_t)player;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerPostInit(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    const AvPlayerPostInitData *data = (const AvPlayerPostInitData *)recomp_guest_to_host(ctx, ctx->rsi);
    if (!player || !data) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    player->post_init(*data);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerAddSource(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    const char *path = (const char *)recomp_guest_to_host(ctx, ctx->rsi);
    if (!player || !path) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    AVPLAYER_LOG("sceAvPlayerAddSource: %s", path);
    ctx->rax = (uint64_t)(int64_t)player->add_source(path);
    SHIM_RETURN();
}

void shim_sceAvPlayerAddSourceEx(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    AvPlayerUriType uri_type = (AvPlayerUriType)ctx->rsi;
    const AvPlayerSourceDetails *details = (const AvPlayerSourceDetails *)recomp_guest_to_host(ctx, ctx->rdx);
    if (!player || !details || uri_type != AVPLAYER_URI_TYPE_SOURCE) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    const char *path = (const char *)recomp_guest_to_host(ctx, details->uri.name);
    if (!path) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    ctx->rax = (uint64_t)(int64_t)player->add_source(path);
    SHIM_RETURN();
}

void shim_sceAvPlayerStreamCount(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    if (!player) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    ctx->rax = (uint64_t)(int64_t)player->get_stream_count();
    SHIM_RETURN();
}

void shim_sceAvPlayerGetStreamInfo(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    uint32_t stream_id = (uint32_t)ctx->rsi;
    AvPlayerStreamInfo *info = (AvPlayerStreamInfo *)recomp_guest_to_host(ctx, ctx->rdx);
    if (!player || !info) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    if (player->get_stream_info(stream_id, info)) {
        ctx->rax = 0;
    } else {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_OPERATION_FAILED;
    }
    SHIM_RETURN();
}

void shim_sceAvPlayerEnableStream(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    uint32_t stream_id = (uint32_t)ctx->rsi;
    if (!player) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    if (player->enable_stream(stream_id)) {
        ctx->rax = 0;
    } else {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_OPERATION_FAILED;
    }
    SHIM_RETURN();
}

void shim_sceAvPlayerDisableStream(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerChangeStream(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerStart(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    if (!player) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    if (player->start_playback(ctx)) {
        ctx->rax = 0;
    } else {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_OPERATION_FAILED;
    }
    SHIM_RETURN();
}

void shim_sceAvPlayerStop(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    if (!player) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    player->stop_playback(ctx);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerPause(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    if (!player) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    player->pause_playback();
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerResume(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    if (!player) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    player->resume_playback();
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerSetLooping(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    bool loop = (bool)ctx->rsi;
    if (!player) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    player->set_looping(loop);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerCurrentTime(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    if (!player) {
        ctx->rax = 0;
        SHIM_RETURN();
    }
    ctx->rax = player->current_time();
    SHIM_RETURN();
}

void shim_sceAvPlayerIsActive(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    if (!player) {
        ctx->rax = 0;
        SHIM_RETURN();
    }
    ctx->rax = player->is_active() ? 1 : 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerGetAudioData(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    AvPlayerFrameInfo *info = (AvPlayerFrameInfo *)recomp_guest_to_host(ctx, ctx->rsi);
    if (!player || !info) {
        ctx->rax = 0;
        SHIM_RETURN();
    }
    ctx->rax = player->get_audio_data(info) ? 1 : 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerGetVideoData(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    AvPlayerFrameInfo *info = (AvPlayerFrameInfo *)recomp_guest_to_host(ctx, ctx->rsi);
    if (!player || !info) {
        ctx->rax = 0;
        SHIM_RETURN();
    }
    ctx->rax = player->get_video_data(info) ? 1 : 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerGetVideoDataEx(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    AvPlayerFrameInfoEx *info = (AvPlayerFrameInfoEx *)recomp_guest_to_host(ctx, ctx->rsi);
    if (!player || !info) {
        ctx->rax = 0;
        SHIM_RETURN();
    }
    ctx->rax = player->get_video_data_ex(info) ? 1 : 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerClose(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    if (!player) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    AVPLAYER_LOG("sceAvPlayerClose called");
    delete player;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerSetAvSyncMode(GuestContext *ctx) {
    AvPlayer *player = (AvPlayer *)ctx->rdi;
    AvPlayerAvSyncMode mode = (AvPlayerAvSyncMode)ctx->rsi;
    if (!player) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
        SHIM_RETURN();
    }
    player->set_av_sync_mode(mode);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerSetLogCallback(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerJumpToTime(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerSetTrickSpeed(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerPrintf(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAvPlayerVprintf(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}
