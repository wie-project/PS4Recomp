// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PS4_AVPLAYER_AUDIO_H
#define PS4_AVPLAYER_AUDIO_H

#include <stdint.h>
#include "ps4_avplayer.h"

extern "C" {
#include <libavutil/channel_layout.h>
}

struct AVFrame;
struct SwrContext;

// Converts decoded audio frames to the PCM sceAvPlayerGetAudioData hands out: interleaved s16
// at the source rate and channel count, channels in the PS4 player's order.
//
// The PS4 player keeps AAC's own channel order, so 5.1 comes out as C, L, R, Ls, Rs, LFE and
// games remap it from there. FFmpeg decodes 5.1 as L, R, C, LFE, Ls, Rs, so 5.1 frames are
// permuted to AAC order. Other layouts keep FFmpeg's order (mono and stereo are the same in
// both).
class AvPlayerAudioConverter {
public:
    static constexpr int kMaxChannels = 8;

    AvPlayerAudioConverter() = default;
    ~AvPlayerAudioConverter();

    AvPlayerAudioConverter(const AvPlayerAudioConverter &) = delete;
    AvPlayerAudioConverter &operator=(const AvPlayerAudioConverter &) = delete;

    // Writes frame to dst and fills details (channel_count, sample_rate, size) as
    // sceAvPlayerGetAudioData reports them. Returns the byte count, or a negative AVERROR when
    // the layout is unsupported, dst is too small or the conversion fails.
    int convert(const AVFrame *frame, uint8_t *dst, uint64_t dst_capacity, AvPlayerAudio *details);

    // Drops the conversion state, for a new source.
    void reset();

private:
    bool configure(const AVFrame *frame);

    SwrContext *m_swr = nullptr;
    int m_in_format = -1;
    int m_in_rate = 0;
    AVChannelLayout m_in_layout{};
    // m_order[i] is the FFmpeg channel index written to output slot i (when m_reorder).
    int m_order[kMaxChannels] = {};
    bool m_reorder = false;
};

#endif // PS4_AVPLAYER_AUDIO_H
