// SPDX-License-Identifier: GPL-2.0-or-later
// Host test for AvPlayerAudioConverter: the PCM and AvPlayerAudio details that
// sceAvPlayerGetAudioData hands out. Build and run:
//   FF=$(brew --prefix ffmpeg)
//   clang++ -std=c++17 -I pkg/runtime/core -I pkg/runtime/modules/libSceAvPlayer -I $FF/include \
//     tests/avplayer_audio_test/main.cpp pkg/runtime/modules/libSceAvPlayer/ps4_avplayer_audio.cpp \
//     -L $FF/lib -lavcodec -lavutil -lswresample -o /tmp/avplayer_audio_test && /tmp/avplayer_audio_test
//
// Each case encodes AAC in the test with a tone of a distinct level per channel, decodes it with
// FFmpeg's AAC decoder (planar float, FFmpeg channel order) and checks the level of every
// interleaved s16 output slot against the channel the PS4 player puts there.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ps4_avplayer_audio.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
}

#define CHECK(cond)                                                                              \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            fprintf(stderr, "[avplayer-audio-test] FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            abort();                                                                             \
        }                                                                                        \
    } while (0)

namespace {

constexpr int kRate = 48000;
constexpr int kFrames = 40;  // AAC frames of 1024 samples
constexpr int kSkipFrames = 4; // encoder priming and ramp-up
// The player's audio buffer: 8 channels of 32-bit samples per 1024-sample frame.
constexpr uint32_t kBufferSize = 8 * sizeof(int32_t) * 1024;

struct Tone {
    AVChannel channel;
    double amplitude;
    double frequency;
};

// Encodes the tones (one per channel of layout) to AAC and returns the decoded frames.
std::vector<AVFrame *> EncodeAndDecode(const AVChannelLayout &layout, const std::vector<Tone> &tones) {
    const AVCodec *enc_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    const AVCodec *dec_codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    CHECK(enc_codec && dec_codec);

    AVCodecContext *enc = avcodec_alloc_context3(enc_codec);
    enc->sample_fmt = AV_SAMPLE_FMT_FLTP;
    enc->sample_rate = kRate;
    enc->bit_rate = 64000 * layout.nb_channels;
    enc->time_base = {1, kRate};
    CHECK(av_channel_layout_copy(&enc->ch_layout, &layout) == 0);
    CHECK(avcodec_open2(enc, enc_codec, nullptr) == 0);
    CHECK(enc->frame_size == 1024);

    AVCodecContext *dec = avcodec_alloc_context3(dec_codec);
    dec->sample_rate = kRate;
    CHECK(av_channel_layout_copy(&dec->ch_layout, &layout) == 0);
    dec->extradata = (uint8_t *)av_mallocz(enc->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    std::memcpy(dec->extradata, enc->extradata, enc->extradata_size);
    dec->extradata_size = enc->extradata_size;
    CHECK(avcodec_open2(dec, dec_codec, nullptr) == 0);

    std::vector<AVFrame *> decoded;
    AVPacket *pkt = av_packet_alloc();
    auto drain = [&](AVCodecContext *e) {
        while (avcodec_receive_packet(e, pkt) == 0) {
            CHECK(avcodec_send_packet(dec, pkt) == 0);
            av_packet_unref(pkt);
            for (;;) {
                AVFrame *out = av_frame_alloc();
                if (avcodec_receive_frame(dec, out) != 0) {
                    av_frame_free(&out);
                    break;
                }
                decoded.push_back(out);
            }
        }
    };

    AVFrame *in = av_frame_alloc();
    for (int f = 0; f < kFrames; ++f) {
        in->format = AV_SAMPLE_FMT_FLTP;
        in->nb_samples = 1024;
        in->sample_rate = kRate;
        CHECK(av_channel_layout_copy(&in->ch_layout, &layout) == 0);
        CHECK(av_frame_get_buffer(in, 0) == 0);
        for (int c = 0; c < layout.nb_channels; ++c) std::memset(in->data[c], 0, 1024 * sizeof(float));
        for (const Tone &t : tones) {
            const int idx = av_channel_layout_index_from_channel(&layout, t.channel);
            CHECK(idx >= 0);
            float *data = (float *)in->data[idx];
            for (int s = 0; s < 1024; ++s) {
                const double time = (double)(f * 1024 + s) / kRate;
                data[s] = (float)(t.amplitude * std::sin(2.0 * M_PI * t.frequency * time));
            }
        }
        in->pts = (int64_t)f * 1024;
        CHECK(avcodec_send_frame(enc, in) == 0);
        av_frame_unref(in);
        drain(enc);
    }
    CHECK(avcodec_send_frame(enc, nullptr) == 0);
    drain(enc);

    av_frame_free(&in);
    av_packet_free(&pkt);
    avcodec_free_context(&enc);
    avcodec_free_context(&dec);
    return decoded;
}

// Converts the frames, checks the details of each, and returns the RMS of every output slot.
std::vector<double> ConvertAndMeasure(const std::vector<AVFrame *> &frames, int channels) {
    AvPlayerAudioConverter converter;
    std::vector<uint8_t> buffer(kBufferSize);
    std::vector<double> sum(channels, 0.0);
    uint64_t samples = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        const AVFrame *frame = frames[i];
        CHECK(frame->format == AV_SAMPLE_FMT_FLTP);
        CHECK(frame->ch_layout.nb_channels == channels);

        AvPlayerAudio details;
        std::memset(&details, 0xAB, sizeof(details));
        const int bytes = converter.convert(frame, buffer.data(), buffer.size(), &details);
        CHECK(bytes == frame->nb_samples * channels * (int)sizeof(int16_t));
        CHECK(details.channel_count == channels);
        CHECK(details.sample_rate == (uint32_t)kRate);
        CHECK(details.size == (uint32_t)bytes);

        if ((int)i < kSkipFrames) continue;
        const int16_t *pcm = (const int16_t *)buffer.data();
        for (int s = 0; s < frame->nb_samples; ++s) {
            for (int c = 0; c < channels; ++c) {
                const double v = pcm[s * channels + c] / 32768.0;
                sum[c] += v * v;
            }
        }
        samples += (uint64_t)frame->nb_samples;
    }
    CHECK(samples > 0);
    std::vector<double> rms(channels);
    for (int c = 0; c < channels; ++c) rms[c] = std::sqrt(sum[c] / (double)samples);
    return rms;
}

void CheckLevels(const char *name, const std::vector<double> &rms, const std::vector<double> &expected) {
    CHECK(rms.size() == expected.size());
    for (size_t c = 0; c < rms.size(); ++c) {
        const double want = expected[c] / std::sqrt(2.0);
        fprintf(stderr, "[avplayer-audio-test]   %s slot %zu: rms %.4f (tone %.4f)\n", name, c, rms[c], want);
        CHECK(std::fabs(rms[c] - want) <= want * 0.15);
    }
}

void FreeFrames(std::vector<AVFrame *> &frames) {
    for (AVFrame *f : frames) av_frame_free(&f);
    frames.clear();
}

void TestStereo() {
    const AVChannelLayout layout = AV_CHANNEL_LAYOUT_STEREO;
    std::vector<AVFrame *> frames = EncodeAndDecode(layout, {
                                                                {AV_CHAN_FRONT_LEFT, 0.40, 440.0},
                                                                {AV_CHAN_FRONT_RIGHT, 0.10, 1000.0},
                                                            });
    CHECK(frames.size() >= (size_t)kFrames - 1);
    // Stereo is interleaved L, R.
    CheckLevels("stereo", ConvertAndMeasure(frames, 2), {0.40, 0.10});
    FreeFrames(frames);
    fprintf(stderr, "[avplayer-audio-test] [+] stereo AAC: interleaved L, R with the reported details\n");
}

void TestFivePointOne() {
    const AVChannelLayout layout = AV_CHANNEL_LAYOUT_5POINT1_BACK;
    std::vector<AVFrame *> frames = EncodeAndDecode(layout, {
                                                                {AV_CHAN_FRONT_LEFT, 0.40, 440.0},
                                                                {AV_CHAN_FRONT_RIGHT, 0.10, 1000.0},
                                                                {AV_CHAN_FRONT_CENTER, 0.25, 660.0},
                                                                {AV_CHAN_LOW_FREQUENCY, 0.30, 50.0},
                                                                {AV_CHAN_BACK_LEFT, 0.05, 880.0},
                                                                {AV_CHAN_BACK_RIGHT, 0.15, 550.0},
                                                            });
    CHECK(frames.size() >= (size_t)kFrames - 1);
    // The PS4 player's AAC order: C, L, R, Ls, Rs, LFE.
    CheckLevels("5.1", ConvertAndMeasure(frames, 6), {0.25, 0.40, 0.10, 0.05, 0.15, 0.30});
    FreeFrames(frames);
    fprintf(stderr, "[avplayer-audio-test] [+] 5.1 AAC: interleaved C, L, R, Ls, Rs, LFE\n");
}

void TestStereoAsFiveOneMovie() {
    // A 5.1 track with only L and R used, as the Gravity Rush movies have.
    const AVChannelLayout layout = AV_CHANNEL_LAYOUT_5POINT1_BACK;
    std::vector<AVFrame *> frames = EncodeAndDecode(layout, {
                                                                {AV_CHAN_FRONT_LEFT, 0.40, 440.0},
                                                                {AV_CHAN_FRONT_RIGHT, 0.20, 1000.0},
                                                            });
    const std::vector<double> rms = ConvertAndMeasure(frames, 6);
    const double l = 0.40 / std::sqrt(2.0);
    const double r = 0.20 / std::sqrt(2.0);
    CHECK(std::fabs(rms[1] - l) <= l * 0.15);
    CHECK(std::fabs(rms[2] - r) <= r * 0.15);
    for (int c : {0, 3, 4, 5}) {
        fprintf(stderr, "[avplayer-audio-test]   silent slot %d: rms %.5f\n", c, rms[c]);
        CHECK(rms[c] < 0.001);
    }
    FreeFrames(frames);
    fprintf(stderr, "[avplayer-audio-test] [+] 5.1 movie with only L/R: L and R land in AAC slots 1 and 2\n");
}

void TestErrors() {
    const AVChannelLayout layout = AV_CHANNEL_LAYOUT_STEREO;
    std::vector<AVFrame *> frames = EncodeAndDecode(layout, {{AV_CHAN_FRONT_LEFT, 0.40, 440.0}});
    CHECK(!frames.empty());
    AvPlayerAudioConverter converter;
    AvPlayerAudio details{};
    std::vector<uint8_t> buffer(kBufferSize);
    const uint32_t need = (uint32_t)frames[0]->nb_samples * 2 * sizeof(int16_t);
    CHECK(converter.convert(frames[0], buffer.data(), need - 1, &details) == AVERROR(ENOSPC));
    CHECK(converter.convert(frames[0], buffer.data(), need, &details) == (int)need);

    // A layout change mid-stream reconfigures the converter.
    std::vector<AVFrame *> mono = EncodeAndDecode(AV_CHANNEL_LAYOUT_MONO, {{AV_CHAN_FRONT_CENTER, 0.30, 440.0}});
    CHECK(converter.convert(mono[0], buffer.data(), buffer.size(), &details) == mono[0]->nb_samples * 2);
    CHECK(details.channel_count == 1);
    FreeFrames(frames);
    FreeFrames(mono);
    fprintf(stderr, "[avplayer-audio-test] [+] errors: short buffer rejected, layout change reconfigures\n");
}

} // namespace

int main() {
    av_log_set_level(AV_LOG_ERROR);
    TestStereo();
    TestFivePointOne();
    TestStereoAsFiveOneMovie();
    TestErrors();
    fprintf(stderr, "[avplayer-audio-test] all tests passed\n");
    return 0;
}
