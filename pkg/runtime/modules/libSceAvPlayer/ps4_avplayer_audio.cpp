// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4_avplayer_audio.h"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <cstring>

AvPlayerAudioConverter::~AvPlayerAudioConverter() {
    reset();
}

void AvPlayerAudioConverter::reset() {
    swr_free(&m_swr);
    av_channel_layout_uninit(&m_in_layout);
    m_in_format = -1;
    m_in_rate = 0;
    m_reorder = false;
}

bool AvPlayerAudioConverter::configure(const AVFrame *frame) {
    if (m_swr && frame->format == m_in_format && frame->sample_rate == m_in_rate &&
        av_channel_layout_compare(&frame->ch_layout, &m_in_layout) == 0) {
        return true;
    }
    reset();

    const AVChannelLayout *layout = &frame->ch_layout;
    if (swr_alloc_set_opts2(&m_swr, layout, AV_SAMPLE_FMT_S16, frame->sample_rate, layout,
                            (AVSampleFormat)frame->format, frame->sample_rate, 0, nullptr) < 0 ||
        swr_init(m_swr) < 0) {
        swr_free(&m_swr);
        return false;
    }
    if (av_channel_layout_copy(&m_in_layout, layout) < 0) {
        reset();
        return false;
    }
    m_in_format = frame->format;
    m_in_rate = frame->sample_rate;

    // 5.1 (back or side surrounds) goes out in AAC order: C, L, R, Ls, Rs, LFE.
    if (layout->nb_channels == 6) {
        const int c = av_channel_layout_index_from_channel(layout, AV_CHAN_FRONT_CENTER);
        const int l = av_channel_layout_index_from_channel(layout, AV_CHAN_FRONT_LEFT);
        const int r = av_channel_layout_index_from_channel(layout, AV_CHAN_FRONT_RIGHT);
        const int lfe = av_channel_layout_index_from_channel(layout, AV_CHAN_LOW_FREQUENCY);
        int ls = av_channel_layout_index_from_channel(layout, AV_CHAN_SIDE_LEFT);
        int rs = av_channel_layout_index_from_channel(layout, AV_CHAN_SIDE_RIGHT);
        if (ls < 0 || rs < 0) {
            ls = av_channel_layout_index_from_channel(layout, AV_CHAN_BACK_LEFT);
            rs = av_channel_layout_index_from_channel(layout, AV_CHAN_BACK_RIGHT);
        }
        const int order[6] = {c, l, r, ls, rs, lfe};
        m_reorder = true;
        for (int i = 0; i < 6; ++i) {
            if (order[i] < 0) m_reorder = false;
            m_order[i] = order[i];
        }
    }
    return true;
}

int AvPlayerAudioConverter::convert(const AVFrame *frame, uint8_t *dst, uint64_t dst_capacity,
                                    AvPlayerAudio *details) {
    const int channels = frame->ch_layout.nb_channels;
    if (channels < 1 || channels > kMaxChannels || frame->nb_samples <= 0) {
        return AVERROR(EINVAL);
    }
    const uint64_t frame_bytes = (uint64_t)channels * sizeof(int16_t);
    if ((uint64_t)frame->nb_samples * frame_bytes > dst_capacity) {
        return AVERROR(ENOSPC);
    }
    if (!configure(frame)) {
        return AVERROR(EINVAL);
    }

    uint8_t *out[1] = {dst};
    const int samples = swr_convert(m_swr, out, frame->nb_samples, (const uint8_t **)frame->extended_data,
                                    frame->nb_samples);
    if (samples < 0) {
        return samples;
    }

    if (m_reorder) {
        int16_t *pcm = (int16_t *)dst;
        for (int s = 0; s < samples; ++s, pcm += channels) {
            int16_t in[kMaxChannels];
            std::memcpy(in, pcm, frame_bytes);
            for (int c = 0; c < channels; ++c) {
                pcm[c] = in[m_order[c]];
            }
        }
    }
    const uint32_t bytes = (uint32_t)((uint64_t)samples * frame_bytes);
    details->channel_count = (uint16_t)channels;
    details->sample_rate = (uint32_t)frame->sample_rate;
    details->size = bytes;
    return (int)bytes;
}
