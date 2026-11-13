// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNgs2 HLE: systems, racks and voices, mixed by sceNgs2SystemRender into the render
// buffers the guest passes (float 0x18 or int16 0x12).
//
// Sampler voices (rack 0x1000) decode PCM16 (0x12), PCM24 (0x14) or ATRAC9 (0x40, libatrac9)
// waveform blocks into a small FIFO, resample it linearly to the system rate and send it through
// their patched ports (volume, matrix) to submixer (0x2000) or mastering (0x3000) voices.
// Submixer voices sum their inputs and send them on; their effects are not modelled. Mastering
// voices apply their gain and limiter and write the render buffer of their output param. Reverb
// racks (0x2001) are not modelled: what is sent to them is dropped. Params without a model here
// are logged once per id.
//
// Sampler voice semantics follow a game sound library (SNDX): every waveform blocks param queues
// after the pending blocks; a list without the queue flag and without blocks ends the voice's
// data. A voice whose queue runs dry keeps playing with the empty flag (0x20) until more blocks
// come, or, once its data was ended, finishes (all flags cleared). Stop, stop-immediately and kill
// end a voice at once. The state reports decoded samples (input samples played) and decoded bytes
// (data bytes decoded), which the game uses to pace its streams.
//
// The render path does not allocate: FIFOs, mixer inputs and ATRAC9 decoders are allocated when a
// rack is created or a voice is set up.

#include "ps4_ngs2.h"
#include "../../core/recomp_runtime.h"
#include "../../core/recomp_context.h"

#include "libatrac9.h"
// Atrac9Handle: a voice resets its decoder in place, without reallocating it.
#include "structures.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// Guest layouts.
static_assert(sizeof(OrbisNgs2VoiceParamHeader) == 8);
static_assert(sizeof(OrbisNgs2WaveformFormat) == 24);
static_assert(sizeof(OrbisNgs2WaveformBlock) == 32);
static_assert(sizeof(OrbisNgs2RenderBufferInfo) == 24);
static_assert(sizeof(OrbisNgs2VoiceMatrixLevelsParam) == 24);
static_assert(sizeof(OrbisNgs2VoicePortVolumeParam) == 16);
static_assert(sizeof(OrbisNgs2VoicePortMatrixParam) == 16);
static_assert(sizeof(OrbisNgs2VoicePatchParam) == 24);
static_assert(sizeof(OrbisNgs2VoiceEventParam) == 12);
static_assert(sizeof(OrbisNgs2SamplerVoiceSetupParam) == 40);
static_assert(sizeof(OrbisNgs2SamplerVoiceWaveformBlocksParam) == 32);
static_assert(sizeof(OrbisNgs2SamplerVoicePitchParam) == 16);
static_assert(sizeof(OrbisNgs2SamplerVoiceState) == 48);

constexpr uint32_t MinGrainSamples = 64;
constexpr uint32_t MaxGrainSamples = 1024;

constexpr uint32_t StateInUse = ORBIS_NGS2_VOICE_STATE_FLAG_INUSE;
constexpr uint32_t StatePlaying = ORBIS_NGS2_VOICE_STATE_FLAG_PLAYING;
constexpr uint32_t StatePaused = ORBIS_NGS2_VOICE_STATE_FLAG_PAUSED;
constexpr uint32_t StateEmpty = ORBIS_NGS2_VOICE_STATE_FLAG_EMPTY;

constexpr uint32_t MaxChannels = 8;   // PS4 order: L R C LFE SL SR BL BR
constexpr uint32_t LfeChannel = 3;
constexpr uint32_t MaxPorts = 8;
constexpr uint32_t MaxMatrices = 4;
constexpr uint32_t MaxMatrixLevels = MaxChannels * MaxChannels;
constexpr uint32_t MaxQueuedBlocks = 32;
constexpr uint32_t DecodeChunk = 256;             // samples per decode step: one ATRAC9 frame at most
constexpr uint32_t FifoFrames = 2 * DecodeChunk;  // a step only runs with fewer than 2 samples held
constexpr uint32_t MaxParams = 64;                // params followed per sceNgs2VoiceControl call
constexpr uint32_t MaxMixerPasses = 16;           // depth of submixer chains

// Prints at most `limit` lines from one call site.
#define NGS2_WARN(limit, ...)                                                                      \
    do {                                                                                           \
        static unsigned warned_;                                                                   \
        if (warned_ < (limit)) {                                                                   \
            ++warned_;                                                                             \
            fprintf(stderr, "[ngs2] " __VA_ARGS__);                                                \
        }                                                                                          \
    } while (0)

enum class RackKind {
    Sampler,
    Submixer,
    Reverb,
    Mastering,
    Unknown,
};

enum class Decoder {
    None,
    Pcm16,
    Pcm24,
    Atrac9,
};

struct Rack;
struct Voice;

struct Port {
    Voice* dest = nullptr;
    float volume = 1.0f;
    int32_t matrix = -1;
};

struct QueuedBlock {
    uint64_t data = 0; // guest address the block's dataOffset counts from
    OrbisNgs2WaveformBlock block{};
};

struct Sampler {
    Decoder decoder = Decoder::None;
    uint32_t sample_rate = 0; // 0: the system's
    float pitch = 1.0f;
    QueuedBlock queue[MaxQueuedBlocks];
    uint32_t head = 0;
    uint32_t count = 0;
    bool ended = false; // no more blocks will come: finish when the queue is played
    // The pass through queue[head] being decoded (a block plays 1 + numRepeats passes).
    bool pass_open = false;
    uint32_t byte = 0;         // decoder position in the block's data
    uint32_t skip_left = 0;    // samples still to drop at the start of the pass
    uint32_t pass_samples = 0; // samples the pass put in the FIFO
    uint32_t repeats_done = 0;
    // ATRAC9 decoder and its place in the current superframe.
    Atrac9Handle* at9 = nullptr;
    Atrac9CodecInfo at9_info{};
    uint32_t at9_frames = 0;
    uint32_t at9_left = 0;
    // Decoded samples (MaxChannels floats each); the resampler sits between the first two.
    float* fifo = nullptr;
    uint32_t fifo_read = 0;
    uint32_t fifo_count = 0;
    double phase = 0.0;
    uint64_t decoded_samples = 0; // input samples played since setup
    uint64_t decoded_bytes = 0;   // data bytes decoded since setup
    float peak = 0.0f;            // of the last render
};

struct Voice {
    OrbisNgs2Handle handle = 0;
    Rack* rack = nullptr;
    uint32_t voice_index = 0;
    uint32_t state = 0;
    uint32_t channels = 2;
    Port ports[MaxPorts];
    uint32_t matrix_levels[MaxMatrices] = {};
    float matrix[MaxMatrices][MaxMatrixLevels] = {};
    // Mixer voices: this render's inputs, MaxChannels planes of MaxGrainSamples.
    float* input = nullptr;
    int pending = 0; // submixers feeding this one that have not sent yet, this render
    Sampler sampler;
    // Mastering voices.
    uint32_t output = 0;
    float fbw_level = 1.0f;
    float lfe_level = 1.0f;
    bool limiter = false;
    float limiter_threshold = 1.0f;
    float limiter_gain = 1.0f;
};

struct Rack {
    OrbisNgs2Handle handle = 0;
    OrbisNgs2Handle system_handle = 0;
    uint32_t rack_id = 0;
    RackKind kind = RackKind::Unknown;
    OrbisNgs2RackOption option{};
    uint64_t buffer_addr = 0;
    uint64_t buffer_size = 0;
    uint64_t user_data = 0;
    std::vector<Voice> voices;
    std::vector<float> storage; // sampler FIFOs or mixer inputs of the voices
};

struct System {
    OrbisNgs2Handle handle = 0;
    OrbisNgs2SystemOption option{};
    uint64_t buffer_addr = 0;
    uint64_t buffer_size = 0;
    uint64_t user_data = 0;
    uint32_t uid = 0;
    int64_t render_count = 0;
    bool render_logged = false;
    std::vector<Rack*> racks;
};

struct Registry {
    std::mutex mtx;
    OrbisNgs2Handle next_handle = 0x100;
    uint32_t next_uid = 1;
    std::unordered_map<OrbisNgs2Handle, System> systems;
    std::unordered_map<OrbisNgs2Handle, Rack> racks; // nodes: Rack and Voice pointers stay valid
    std::unordered_map<OrbisNgs2Handle, Voice*> voices;
    float scratch[DecodeChunk * MaxChannels]; // one decode step, interleaved
};

static Registry g_ngs2;

static inline bool ValidGrain(uint32_t grain) {
    return grain >= MinGrainSamples && grain <= MaxGrainSamples && (grain % MinGrainSamples) == 0;
}

static inline size_t SystemBufferSize(const OrbisNgs2SystemOption& option) {
    return sizeof(OrbisNgs2SystemInfo) + (32 * option.maxGrainSamples * sizeof(float));
}

static inline size_t RackBufferSize(const OrbisNgs2RackOption& option) {
    return sizeof(OrbisNgs2RackInfo) + (option.maxVoices * sizeof(uint64_t) * 2);
}

static RackKind KindOfRack(uint32_t rack_id) {
    switch (rack_id) {
    case ORBIS_NGS2_RACK_ID_SAMPLER: return RackKind::Sampler;
    case ORBIS_NGS2_RACK_ID_SUBMIXER: return RackKind::Submixer;
    case ORBIS_NGS2_RACK_ID_REVERB: return RackKind::Reverb;
    case ORBIS_NGS2_RACK_ID_MASTERING: return RackKind::Mastering;
    default: return RackKind::Unknown;
    }
}

// Host pointer to [addr, addr + size) of guest memory, or null when it is not all there.
static uint8_t* GuestRange(const GuestContext* ctx, uint64_t addr, uint64_t size) {
    if (!addr || addr > ctx->mem_size || size > ctx->mem_size - addr) return nullptr;
    return ctx->mem_base + addr;
}

// ---- Sampler voices ----

static void ClearQueue(Sampler& s) {
    s.head = s.count = 0;
    s.ended = false;
    s.pass_open = false;
    s.repeats_done = 0;
    s.fifo_read = s.fifo_count = 0;
    s.phase = 0.0;
}

// Stop, kill and the end of a voice's data: not in use any more, queue dropped.
static void EndVoice(Voice& v) {
    v.state = 0;
    ClearQueue(v.sampler);
}

// A fresh ATRAC9 decoder for config word `config` (its bytes, most significant first, are the
// ATRAC9 config data starting with 0xFE). Reuses the voice's decoder memory.
static bool SetupAtrac9(Voice& v, uint32_t config) {
    Sampler& s = v.sampler;
    unsigned char cfg[4] = {(unsigned char)(config >> 24), (unsigned char)(config >> 16),
                            (unsigned char)(config >> 8), (unsigned char)config};
    if (cfg[0] != 0xFE) {
        NGS2_WARN(16, "voice 0x%llx: ATRAC9 config 0x%08x has no 0xFE header; voice is silent\n",
                  (unsigned long long)v.handle, config);
        return false;
    }
    if (!s.at9) s.at9 = (Atrac9Handle*)Atrac9GetHandle();
    if (!s.at9) {
        NGS2_WARN(4, "voice 0x%llx: out of memory for an ATRAC9 decoder\n", (unsigned long long)v.handle);
        return false;
    }
    memset(s.at9, 0, sizeof(*s.at9)); // what Atrac9GetHandle returns: no state of the last waveform
    // Atrac9InitDecoder also rewrites libatrac9's global tables (with the same values every
    // time), which libSceAjm's worker may be reading under its own lock.
    Atrac9CodecInfo info{};
    if (Atrac9InitDecoder(s.at9, cfg) != 0 || Atrac9GetCodecInfo(s.at9, &info) != 0 || info.channels <= 0 ||
        (uint32_t)info.channels > MaxChannels || info.frameSamples <= 0 ||
        (uint32_t)info.frameSamples > DecodeChunk || info.framesInSuperframe <= 0 || info.superframeSize <= 0) {
        NGS2_WARN(16, "voice 0x%llx: ATRAC9 config 0x%08x rejected by libatrac9; voice is silent\n",
                  (unsigned long long)v.handle, config);
        return false;
    }
    if ((uint32_t)info.channels != v.channels) {
        NGS2_WARN(16, "voice 0x%llx: ATRAC9 config has %d channels, the waveform format %u; using the config's\n",
                  (unsigned long long)v.handle, info.channels, v.channels);
        v.channels = (uint32_t)info.channels;
    }
    s.at9_info = info;
    return true;
}

static void SetupSampler(Voice& v, const OrbisNgs2WaveformFormat& format) {
    EndVoice(v);
    Sampler& s = v.sampler;
    s.decoder = Decoder::None;
    s.sample_rate = format.sampleRate;
    s.decoded_samples = s.decoded_bytes = 0;
    s.peak = 0.0f;
    if (format.numChannels == 0 || format.numChannels > MaxChannels) {
        NGS2_WARN(16, "voice 0x%llx: waveform with %u channels; voice is silent\n", (unsigned long long)v.handle,
                  format.numChannels);
        return;
    }
    v.channels = format.numChannels;
    switch (format.waveformType) {
    case ORBIS_NGS2_WAVEFORM_TYPE_PCM_I16L: s.decoder = Decoder::Pcm16; break;
    case ORBIS_NGS2_WAVEFORM_TYPE_PCM_I24L: s.decoder = Decoder::Pcm24; break;
    case ORBIS_NGS2_WAVEFORM_TYPE_ATRAC9:
        if (SetupAtrac9(v, format.configData)) s.decoder = Decoder::Atrac9;
        break;
    default:
        NGS2_WARN(16, "voice 0x%llx: waveform type 0x%x is not supported; voice is silent\n",
                  (unsigned long long)v.handle, format.waveformType);
        break;
    }
}

static void QueueBlocks(GuestContext* ctx, Voice& v, const OrbisNgs2SamplerVoiceWaveformBlocksParam& p) {
    Sampler& s = v.sampler;
    if (!(p.flags & ORBIS_NGS2_WAVEFORM_BLOCKS_FLAG_QUEUE) && p.numBlocks == 0) {
        s.ended = true;
        return;
    }
    const auto* blocks = (const OrbisNgs2WaveformBlock*)GuestRange(ctx, p.aBlock,
                                                                    (uint64_t)p.numBlocks * sizeof(OrbisNgs2WaveformBlock));
    if (!blocks) {
        NGS2_WARN(16, "voice 0x%llx: waveform blocks at 0x%llx (%u) are not in guest memory\n",
                  (unsigned long long)v.handle, (unsigned long long)p.aBlock, p.numBlocks);
        return;
    }
    uint32_t added = 0;
    for (; added < p.numBlocks && s.count < MaxQueuedBlocks; ++added) {
        QueuedBlock& q = s.queue[(s.head + s.count++) % MaxQueuedBlocks];
        q.data = p.data;
        memcpy(&q.block, &blocks[added], sizeof(q.block));
    }
    if (added < p.numBlocks) {
        NGS2_WARN(16, "voice 0x%llx: waveform block queue full (%u); %u blocks dropped\n",
                  (unsigned long long)v.handle, MaxQueuedBlocks, p.numBlocks - added);
    }
    if (added) {
        s.ended = false;
        v.state &= ~StateEmpty;
    }
}

// Puts samples [0, n) of the interleaved `src` (`channels` each) in the FIFO, after the pass's
// skipped samples and up to its numSamples.
static void PushSamples(Sampler& s, const QueuedBlock& q, const float* src, uint32_t n, uint32_t channels) {
    for (uint32_t i = 0; i < n; ++i) {
        if (s.skip_left) {
            --s.skip_left;
            continue;
        }
        if (q.block.numSamples && s.pass_samples >= q.block.numSamples) break;
        if (s.fifo_count == FifoFrames) break;
        float* dst = s.fifo + (size_t)((s.fifo_read + s.fifo_count) % FifoFrames) * MaxChannels;
        memcpy(dst, src + (size_t)i * channels, channels * sizeof(float));
        ++s.fifo_count;
        ++s.pass_samples;
    }
}

static void OpenPass(Sampler& s, const QueuedBlock& q) {
    s.pass_open = true;
    s.byte = 0;
    s.skip_left = q.block.numSkipSamples;
    s.pass_samples = 0;
    if (s.decoder == Decoder::Atrac9) {
        // A block starts on a superframe, and so must the decoder (libatrac9 rejects a superframe
        // start in the middle of one).
        s.at9->Frame.IndexInSuperframe = 0;
        s.at9_frames = 0;
        s.at9_left = (uint32_t)s.at9_info.superframeSize;
    }
}

// The end of a pass: the block plays again, or the next one comes. A pass that gave no samples
// ends the block's repeats (an endless repeat of nothing would never let the voice go on).
static void ClosePass(Sampler& s) {
    const OrbisNgs2WaveformBlock& b = s.queue[s.head].block;
    s.pass_open = false;
    if (s.pass_samples && (b.numRepeats == 0xFFFFFFFFu || s.repeats_done < b.numRepeats)) {
        ++s.repeats_done;
        return;
    }
    s.repeats_done = 0;
    s.head = (s.head + 1) % MaxQueuedBlocks;
    --s.count;
}

static void DecodePcm(Sampler& s, const QueuedBlock& q, const uint8_t* data, uint32_t channels) {
    const uint32_t bytes = s.decoder == Decoder::Pcm16 ? 2 : 3;
    const uint32_t frame_bytes = bytes * channels;
    const uint32_t frames_left = (q.block.dataSize - s.byte) / frame_bytes;
    uint32_t n = std::min(frames_left, DecodeChunk);
    if (q.block.numSamples) n = std::min(n, s.skip_left + (q.block.numSamples - s.pass_samples));
    if (n == 0) { // a partial frame at the end
        s.decoded_bytes += q.block.dataSize - s.byte;
        s.byte = q.block.dataSize;
        return;
    }
    const uint8_t* p = data + s.byte;
    float* out = g_ngs2.scratch;
    for (uint32_t i = 0; i < n * channels; ++i, p += bytes) {
        if (bytes == 2) {
            int16_t x;
            memcpy(&x, p, 2);
            out[i] = (float)x * (1.0f / 32768.0f);
        } else {
            const int32_t x = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;
            out[i] = (float)x * (1.0f / 8388608.0f);
        }
    }
    s.byte += n * frame_bytes;
    s.decoded_bytes += n * frame_bytes;
    PushSamples(s, q, out, n, channels);
}

static void DecodeAtrac9(Voice& v, const QueuedBlock& q, const uint8_t* data) {
    Sampler& s = v.sampler;
    const uint32_t left = q.block.dataSize - s.byte;
    int used = 0;
    const int err = Atrac9DecodeF32(s.at9, data + s.byte, (int)left, g_ngs2.scratch, &used, 0);
    if (err != 0 || used <= 0 || (uint32_t)used > left) {
        NGS2_WARN(32, "voice 0x%llx: ATRAC9 decode error 0x%x at byte %u of %u; rest of the block skipped\n",
                  (unsigned long long)v.handle, (unsigned)err, s.byte, q.block.dataSize);
        s.decoded_bytes += left;
        s.byte = q.block.dataSize;
        return;
    }
    s.byte += (uint32_t)used;
    s.decoded_bytes += (uint32_t)used;
    s.at9_left -= std::min((uint32_t)used, s.at9_left);
    if (++s.at9_frames == (uint32_t)s.at9_info.framesInSuperframe) { // skip the superframe's padding
        const uint32_t pad = std::min(s.at9_left, q.block.dataSize - s.byte);
        s.byte += pad;
        s.decoded_bytes += pad;
        s.at9_frames = 0;
        s.at9_left = (uint32_t)s.at9_info.superframeSize;
    }
    PushSamples(s, q, g_ngs2.scratch, (uint32_t)s.at9_info.frameSamples, (uint32_t)s.at9_info.channels);
}

// One step through the voice's queue: decodes a chunk of the current block into the FIFO, or
// moves on to the next pass or block. False when the queue is empty.
static bool DecodeStep(GuestContext* ctx, Voice& v) {
    Sampler& s = v.sampler;
    if (s.count == 0) return false;
    const QueuedBlock& q = s.queue[s.head];
    if (!s.pass_open) OpenPass(s, q);
    if (s.byte >= q.block.dataSize || (q.block.numSamples && s.pass_samples >= q.block.numSamples)) {
        ClosePass(s);
        return true;
    }
    const uint8_t* data = GuestRange(ctx, q.data + q.block.dataOffset, q.block.dataSize);
    if (!data || s.decoder == Decoder::None) {
        if (!data) {
            NGS2_WARN(16, "voice 0x%llx: block data 0x%llx+%u (%u bytes) is not in guest memory; skipped\n",
                      (unsigned long long)v.handle, (unsigned long long)q.data, q.block.dataOffset, q.block.dataSize);
        }
        s.decoded_bytes += q.block.dataSize - s.byte;
        s.byte = q.block.dataSize;
        return true;
    }
    if (s.decoder == Decoder::Atrac9) DecodeAtrac9(v, q, data);
    else DecodePcm(s, q, data, v.channels);
    return true;
}

// Gains from input channel c to output channel o of `port`: its matrix when it has one with
// levels (level[o * in + c], missing levels are 0), else channel to same channel; times its volume.
static void PortGains(const Voice& v, const Port& port, uint32_t in, uint32_t out,
                      float g[MaxChannels][MaxChannels]) {
    const int32_t m = port.matrix;
    const bool matrix = m >= 0 && (uint32_t)m < MaxMatrices && v.matrix_levels[m] > 0;
    for (uint32_t c = 0; c < in; ++c) {
        for (uint32_t o = 0; o < out; ++o) {
            float level;
            if (matrix) {
                const uint32_t i = o * in + c;
                level = i < v.matrix_levels[m] ? v.matrix[m][i] : 0.0f;
            } else {
                level = c == o ? 1.0f : 0.0f;
            }
            g[c][o] = level * port.volume;
        }
    }
}

struct Send {
    Voice* dest;
    uint32_t out;
    float g[MaxChannels][MaxChannels];
};

// The voice's ports that lead to a mixing input; returns how many.
static uint32_t CollectSends(const Voice& v, Send* sends) {
    uint32_t n = 0;
    for (const Port& port : v.ports) {
        if (!port.dest || !port.dest->input || port.volume == 0.0f) continue;
        Send& snd = sends[n++];
        snd.dest = port.dest;
        snd.out = port.dest->channels;
        PortGains(v, port, v.channels, snd.out, snd.g);
    }
    return n;
}

static void RenderSampler(GuestContext* ctx, Voice& v, uint32_t frames, uint32_t system_rate) {
    Sampler& s = v.sampler;
    if ((v.state & (StatePlaying | StatePaused)) != StatePlaying) return;
    const uint32_t channels = v.channels;
    const double step = (double)s.pitch * (double)(s.sample_rate ? s.sample_rate : system_rate) / (double)system_rate;
    Send sends[MaxPorts];
    const uint32_t num_sends = CollectSends(v, sends);
    float peak = 0.0f;
    for (uint32_t f = 0; f < frames; ++f) {
        while (s.fifo_count < 2 && DecodeStep(ctx, v)) {
        }
        if (s.fifo_count == 0) break;
        const float* a = s.fifo + (size_t)s.fifo_read * MaxChannels;
        const float* b = s.fifo_count > 1 ? s.fifo + (size_t)((s.fifo_read + 1) % FifoFrames) * MaxChannels : a;
        const float t = (float)s.phase;
        float x[MaxChannels];
        for (uint32_t c = 0; c < channels; ++c) {
            x[c] = a[c] + (b[c] - a[c]) * t;
            peak = std::max(peak, std::fabs(x[c]));
        }
        for (uint32_t k = 0; k < num_sends; ++k) {
            const Send& snd = sends[k];
            for (uint32_t o = 0; o < snd.out; ++o) {
                float acc = 0.0f;
                for (uint32_t c = 0; c < channels; ++c) acc += x[c] * snd.g[c][o];
                snd.dest->input[(size_t)o * MaxGrainSamples + f] += acc;
            }
        }
        s.phase += step;
        while (s.phase >= 1.0 && s.fifo_count) {
            s.phase -= 1.0;
            s.fifo_read = (s.fifo_read + 1) % FifoFrames;
            --s.fifo_count;
            ++s.decoded_samples;
        }
    }
    s.peak = peak;
    if (s.fifo_count == 0 && s.count == 0) {
        if (s.ended) EndVoice(v);
        else v.state |= StateEmpty;
    }
}

// ---- Mixer voices ----

static void SendMixer(Voice& v, uint32_t frames) {
    Send sends[MaxPorts];
    const uint32_t num_sends = CollectSends(v, sends);
    for (uint32_t k = 0; k < num_sends; ++k) {
        const Send& snd = sends[k];
        for (uint32_t o = 0; o < snd.out; ++o) {
            float* dst = snd.dest->input + (size_t)o * MaxGrainSamples;
            for (uint32_t c = 0; c < v.channels; ++c) {
                const float g = snd.g[c][o];
                if (g == 0.0f) continue;
                const float* src = v.input + (size_t)c * MaxGrainSamples;
                for (uint32_t f = 0; f < frames; ++f) dst[f] += src[f] * g;
            }
        }
    }
}

// The mastering voice's inputs through its gain and limiter, added to its render buffer.
static void WriteMastering(GuestContext* ctx, Voice& v, const OrbisNgs2RenderBufferInfo* bufs, uint32_t num_bufs,
                           uint32_t frames) {
    if (v.output >= num_bufs || !bufs[v.output].buffer) {
        NGS2_WARN(8, "mastering voice 0x%llx: output %u has no render buffer (%u given)\n",
                  (unsigned long long)v.handle, v.output, num_bufs);
        return;
    }
    const OrbisNgs2RenderBufferInfo& b = bufs[v.output];
    const uint32_t channels = std::min(v.channels, b.numChannels);
    float level[MaxChannels];
    float peak = 0.0f;
    for (uint32_t c = 0; c < v.channels; ++c) {
        level[c] = c == LfeChannel ? v.lfe_level : v.fbw_level;
        const float* in = v.input + (size_t)c * MaxGrainSamples;
        for (uint32_t f = 0; f < frames; ++f) peak = std::max(peak, std::fabs(in[f] * level[c]));
    }
    // Limiter: a render whose peak passes the threshold is brought under it, the gain ramping
    // across the render (no clicks); the gain recovers by 5% of the way to 1 per render.
    float from = v.limiter_gain, to = 1.0f;
    if (v.limiter) {
        to = std::min(1.0f, from + (1.0f - from) * 0.05f);
        if (peak * to > v.limiter_threshold) to = v.limiter_threshold / peak;
    } else {
        from = 1.0f;
    }
    v.limiter_gain = to;
    uint8_t* dst = ctx->mem_base + b.buffer; // checked by the render
    for (uint32_t f = 0; f < frames; ++f) {
        const float k = from + (to - from) * (float)(f + 1) / (float)frames;
        for (uint32_t c = 0; c < channels; ++c) {
            const float x = v.input[(size_t)c * MaxGrainSamples + f] * level[c] * k;
            const size_t i = (size_t)f * b.numChannels + c;
            if (b.waveformType == ORBIS_NGS2_WAVEFORM_TYPE_PCM_F32L) {
                float y;
                memcpy(&y, dst + i * 4, 4);
                y += x;
                memcpy(dst + i * 4, &y, 4);
            } else {
                int16_t y;
                memcpy(&y, dst + i * 2, 2);
                const float sum = std::clamp((float)y + x * 32767.0f, -32768.0f, 32767.0f);
                y = (int16_t)std::lrintf(sum);
                memcpy(dst + i * 2, &y, 2);
            }
        }
    }
}

// Checks the render buffers and clears them. Returns 0 or the ORBIS_NGS2_ERROR code.
static uint32_t PrepareRenderBuffers(GuestContext* ctx, const OrbisNgs2RenderBufferInfo* bufs, uint32_t n,
                                     uint32_t frames) {
    for (uint32_t i = 0; i < n; ++i) {
        const OrbisNgs2RenderBufferInfo& b = bufs[i];
        if (!b.buffer) continue;
        uint32_t bytes;
        if (b.waveformType == ORBIS_NGS2_WAVEFORM_TYPE_PCM_F32L) bytes = 4;
        else if (b.waveformType == ORBIS_NGS2_WAVEFORM_TYPE_PCM_I16L) bytes = 2;
        else {
            NGS2_WARN(8, "render buffer %u: waveform type 0x%x is not supported\n", i, b.waveformType);
            return ORBIS_NGS2_ERROR_INVALID_BUFFER_INFO;
        }
        if (b.numChannels == 0 || b.numChannels > MaxChannels) return ORBIS_NGS2_ERROR_INVALID_NUM_CHANNELS;
        if (b.bufferSize < (uint64_t)frames * b.numChannels * bytes) {
            NGS2_WARN(8, "render buffer %u: %llu bytes, %u samples of %u channels need %llu\n", i,
                      (unsigned long long)b.bufferSize, frames, b.numChannels,
                      (unsigned long long)frames * b.numChannels * bytes);
            return ORBIS_NGS2_ERROR_INVALID_BUFFER_SIZE;
        }
        uint8_t* dst = GuestRange(ctx, b.buffer, b.bufferSize);
        if (!dst) return ORBIS_NGS2_ERROR_INVALID_BUFFER_ADDRESS;
        memset(dst, 0, b.bufferSize);
    }
    return 0;
}

static void Render(GuestContext* ctx, System& sys, const OrbisNgs2RenderBufferInfo* bufs, uint32_t num_bufs) {
    const uint32_t frames = sys.option.numGrainSamples;
    const uint32_t rate = sys.option.sampleRate;
    for (Rack* r : sys.racks) {
        if (r->kind == RackKind::Sampler || r->kind == RackKind::Unknown) continue;
        for (Voice& v : r->voices) {
            // Sends and the mastering write only touch the voice's channels.
            for (uint32_t c = 0; c < v.channels; ++c) memset(v.input + (size_t)c * MaxGrainSamples, 0, frames * sizeof(float));
            v.pending = 0;
        }
    }
    for (Rack* r : sys.racks) {
        if (r->kind != RackKind::Submixer) continue;
        for (Voice& v : r->voices) {
            for (const Port& port : v.ports) {
                if (port.dest && port.dest->rack->kind == RackKind::Submixer) ++port.dest->pending;
            }
        }
    }
    for (Rack* r : sys.racks) {
        if (r->kind != RackKind::Sampler) continue;
        for (Voice& v : r->voices) RenderSampler(ctx, v, frames, rate);
    }
    // Submixers send once everything feeding them is in.
    bool waiting = false;
    for (uint32_t pass = 0; pass < MaxMixerPasses; ++pass) {
        bool progress = false;
        waiting = false;
        for (Rack* r : sys.racks) {
            if (r->kind != RackKind::Submixer) continue;
            for (Voice& v : r->voices) {
                if (v.pending < 0) continue;
                if (v.pending > 0) {
                    waiting = true;
                    continue;
                }
                v.pending = -1;
                progress = true;
                SendMixer(v, frames);
                for (const Port& port : v.ports) {
                    if (port.dest && port.dest->rack->kind == RackKind::Submixer) --port.dest->pending;
                }
            }
        }
        if (!waiting || !progress) break;
    }
    if (waiting) NGS2_WARN(4, "system 0x%llx: submixer patches form a cycle; those submixers are silent\n",
                           (unsigned long long)sys.handle);
    for (Rack* r : sys.racks) {
        if (r->kind != RackKind::Mastering) continue;
        for (Voice& v : r->voices) WriteMastering(ctx, v, bufs, num_bufs, frames);
    }
}

// ---- Voice control ----

template <typename T>
static bool ReadParam(const uint8_t* p, uint16_t size, T* out) {
    if (size < sizeof(T)) {
        NGS2_WARN(16, "param 0x%x: size %u, expected %zu\n", ((const OrbisNgs2VoiceParamHeader*)p)->id, size,
                  sizeof(T));
        return false;
    }
    memcpy(out, p, sizeof(T));
    return true;
}

static void WarnUnknownParam(const Voice& v, uint32_t id) {
    static uint32_t seen[64];
    static uint32_t num_seen;
    for (uint32_t i = 0; i < num_seen; ++i) {
        if (seen[i] == id) return;
    }
    if (num_seen == 64) return;
    seen[num_seen++] = id;
    fprintf(stderr, "[ngs2] rack 0x%x voice param 0x%x is not modelled; ignored\n", v.rack->rack_id, id);
}

// The voice a patch leads to: a voice handle, or a rack handle for its first voice.
static Voice* PatchDestination(OrbisNgs2Handle h) {
    auto vit = g_ngs2.voices.find(h);
    if (vit != g_ngs2.voices.end()) return vit->second;
    auto rit = g_ngs2.racks.find(h);
    if (rit != g_ngs2.racks.end() && !rit->second.voices.empty()) return &rit->second.voices[0];
    return nullptr;
}

static void ApplyEvent(Voice& v, uint32_t event) {
    switch (event) {
    case ORBIS_NGS2_VOICE_EVENT_PLAY: v.state = StateInUse | StatePlaying; break;
    case ORBIS_NGS2_VOICE_EVENT_STOP:
    case ORBIS_NGS2_VOICE_EVENT_STOP_IMM:
    case ORBIS_NGS2_VOICE_EVENT_KILL: EndVoice(v); break;
    case ORBIS_NGS2_VOICE_EVENT_PAUSE:
        if (v.state) v.state |= StatePaused;
        break;
    case ORBIS_NGS2_VOICE_EVENT_RESUME: v.state &= ~StatePaused; break;
    default: NGS2_WARN(16, "voice 0x%llx: unknown event %u\n", (unsigned long long)v.handle, event); break;
    }
}

static void ApplyParam(GuestContext* ctx, Voice& v, const uint8_t* p, uint32_t id, uint16_t size) {
    const RackKind kind = v.rack->kind;
    switch (id) {
    case ORBIS_NGS2_VOICE_PARAM_MATRIX_LEVELS: {
        OrbisNgs2VoiceMatrixLevelsParam m;
        if (!ReadParam(p, size, &m)) return;
        const uint32_t n = std::min(m.numLevels, MaxMatrixLevels);
        const auto* levels = (const float*)GuestRange(ctx, m.aLevel, (uint64_t)n * sizeof(float));
        if (m.matrixId >= MaxMatrices || (n && !levels)) {
            NGS2_WARN(16, "voice 0x%llx: matrix %u with %u levels at 0x%llx rejected\n", (unsigned long long)v.handle,
                      m.matrixId, m.numLevels, (unsigned long long)m.aLevel);
            return;
        }
        if (n) memcpy(v.matrix[m.matrixId], levels, n * sizeof(float));
        v.matrix_levels[m.matrixId] = n;
        return;
    }
    case ORBIS_NGS2_VOICE_PARAM_PORT_VOLUME: {
        OrbisNgs2VoicePortVolumeParam m;
        if (!ReadParam(p, size, &m)) return;
        if (m.port < MaxPorts && std::isfinite(m.level)) v.ports[m.port].volume = m.level;
        else NGS2_WARN(16, "voice 0x%llx: port %u volume rejected\n", (unsigned long long)v.handle, m.port);
        return;
    }
    case ORBIS_NGS2_VOICE_PARAM_PORT_MATRIX: {
        OrbisNgs2VoicePortMatrixParam m;
        if (!ReadParam(p, size, &m)) return;
        if (m.port < MaxPorts) v.ports[m.port].matrix = m.matrixId;
        else NGS2_WARN(16, "voice 0x%llx: port %u matrix rejected\n", (unsigned long long)v.handle, m.port);
        return;
    }
    case ORBIS_NGS2_VOICE_PARAM_PATCH: {
        OrbisNgs2VoicePatchParam m;
        if (!ReadParam(p, size, &m)) return;
        if (m.port >= MaxPorts) {
            NGS2_WARN(16, "voice 0x%llx: patch of port %u rejected\n", (unsigned long long)v.handle, m.port);
            return;
        }
        Voice* dest = m.destHandle ? PatchDestination(m.destHandle) : nullptr;
        if (m.destHandle && (!dest || dest->rack->kind == RackKind::Sampler || dest->rack->kind == RackKind::Unknown)) {
            NGS2_WARN(16, "voice 0x%llx: port %u patched to 0x%llx, which takes no input\n",
                      (unsigned long long)v.handle, m.port, (unsigned long long)m.destHandle);
            dest = nullptr;
        }
        v.ports[m.port].dest = dest;
        return;
    }
    case ORBIS_NGS2_VOICE_PARAM_EVENT: {
        OrbisNgs2VoiceEventParam m;
        if (ReadParam(p, size, &m)) ApplyEvent(v, m.eventId);
        return;
    }
    default: break;
    }

    if (kind == RackKind::Sampler) {
        switch (id) {
        case ORBIS_NGS2_SAMPLER_VOICE_PARAM_SETUP: {
            OrbisNgs2SamplerVoiceSetupParam m;
            if (ReadParam(p, size, &m)) SetupSampler(v, m.format);
            return;
        }
        case ORBIS_NGS2_SAMPLER_VOICE_PARAM_WAVEFORM_BLOCKS: {
            OrbisNgs2SamplerVoiceWaveformBlocksParam m;
            if (ReadParam(p, size, &m)) QueueBlocks(ctx, v, m);
            return;
        }
        case ORBIS_NGS2_SAMPLER_VOICE_PARAM_PITCH: {
            OrbisNgs2SamplerVoicePitchParam m;
            if (!ReadParam(p, size, &m)) return;
            if (m.ratio > 0.0f && m.ratio < 1024.0f) v.sampler.pitch = m.ratio;
            else NGS2_WARN(16, "voice 0x%llx: pitch ratio %g rejected\n", (unsigned long long)v.handle, (double)m.ratio);
            return;
        }
        default: break;
        }
    } else if (kind == RackKind::Submixer && id == ORBIS_NGS2_SUBMIXER_VOICE_PARAM_SETUP) {
        OrbisNgs2SubmixerVoiceSetupParam m;
        if (!ReadParam(p, size, &m)) return;
        if (m.numIoChannels >= 1 && m.numIoChannels <= MaxChannels) v.channels = m.numIoChannels;
        else NGS2_WARN(16, "submixer voice 0x%llx: %u channels rejected\n", (unsigned long long)v.handle, m.numIoChannels);
        return;
    } else if (kind == RackKind::Mastering) {
        switch (id) {
        case ORBIS_NGS2_MASTERING_VOICE_PARAM_SETUP: {
            OrbisNgs2MasteringVoiceSetupParam m;
            if (!ReadParam(p, size, &m)) return;
            if (m.numInputChannels >= 1 && m.numInputChannels <= MaxChannels) v.channels = m.numInputChannels;
            else NGS2_WARN(16, "mastering voice 0x%llx: %u channels rejected\n", (unsigned long long)v.handle,
                           m.numInputChannels);
            return;
        }
        case ORBIS_NGS2_MASTERING_VOICE_PARAM_LIMITER: {
            OrbisNgs2MasteringVoiceLimiterParam m;
            if (!ReadParam(p, size, &m)) return;
            if (m.threshold > 0.0f && std::isfinite(m.threshold)) {
                v.limiter = m.enableFlag != 0;
                v.limiter_threshold = m.threshold;
                v.limiter_gain = 1.0f;
            } else {
                NGS2_WARN(16, "mastering voice 0x%llx: limiter threshold %g rejected\n", (unsigned long long)v.handle,
                          (double)m.threshold);
            }
            return;
        }
        case ORBIS_NGS2_MASTERING_VOICE_PARAM_GAIN: {
            OrbisNgs2MasteringVoiceGainParam m;
            if (!ReadParam(p, size, &m)) return;
            if (std::isfinite(m.fbwLevel) && std::isfinite(m.lfeLevel)) {
                v.fbw_level = m.fbwLevel;
                v.lfe_level = m.lfeLevel;
            }
            return;
        }
        case ORBIS_NGS2_MASTERING_VOICE_PARAM_OUTPUT: {
            OrbisNgs2MasteringVoiceOutputParam m;
            if (ReadParam(p, size, &m)) v.output = m.outputId;
            return;
        }
        default: break;
        }
    }
    WarnUnknownParam(v, id);
}

// ---- Registry ----

static int32_t CallGuestAlloc(GuestContext* ctx, uint64_t fn_addr, uint64_t info_guest_addr) {
    if (!fn_addr) return -1;
    uint64_t saved_rdi = ctx->rdi;
    uint64_t saved_rsi = ctx->rsi;
    uint64_t saved_rdx = ctx->rdx;
    uint64_t saved_rcx = ctx->rcx;
    uint64_t saved_r8  = ctx->r8;
    uint64_t saved_r9  = ctx->r9;
    uint64_t saved_rax = ctx->rax;
    uint64_t saved_rip = ctx->rip;
    uint64_t saved_rsp = ctx->rsp;

    ctx->rsp -= 8;
    *(uint64_t*)(ctx->mem_base + ctx->rsp) = 0xdeadbeefULL;
    ctx->rdi = info_guest_addr;
    ctx->rip = fn_addr;

    recomp_dispatch(ctx, fn_addr);

    int32_t res = (int32_t)ctx->rax;

    ctx->rdi = saved_rdi;
    ctx->rsi = saved_rsi;
    ctx->rdx = saved_rdx;
    ctx->rcx = saved_rcx;
    ctx->r8  = saved_r8;
    ctx->r9  = saved_r9;
    ctx->rax = saved_rax;
    ctx->rip = saved_rip;
    ctx->rsp = saved_rsp;

    return res;
}

static void CallGuestFree(GuestContext* ctx, uint64_t fn_addr, uint64_t info_guest_addr) {
    if (!fn_addr) return;
    uint64_t saved_rdi = ctx->rdi;
    uint64_t saved_rsi = ctx->rsi;
    uint64_t saved_rdx = ctx->rdx;
    uint64_t saved_rcx = ctx->rcx;
    uint64_t saved_r8  = ctx->r8;
    uint64_t saved_r9  = ctx->r9;
    uint64_t saved_rax = ctx->rax;
    uint64_t saved_rip = ctx->rip;
    uint64_t saved_rsp = ctx->rsp;

    ctx->rsp -= 8;
    *(uint64_t*)(ctx->mem_base + ctx->rsp) = 0xdeadbeefULL;
    ctx->rdi = info_guest_addr;
    ctx->rip = fn_addr;

    recomp_dispatch(ctx, fn_addr);

    ctx->rdi = saved_rdi;
    ctx->rsi = saved_rsi;
    ctx->rdx = saved_rdx;
    ctx->rcx = saved_rcx;
    ctx->r8  = saved_r8;
    ctx->r9  = saved_r9;
    ctx->rax = saved_rax;
    ctx->rip = saved_rip;
    ctx->rsp = saved_rsp;
}

static OrbisNgs2SystemOption NormalizeSystemOption(const OrbisNgs2SystemOption* opt) {
    OrbisNgs2SystemOption norm{};
    if (opt) norm = *opt;
    if (norm.maxGrainSamples == 0) norm.maxGrainSamples = 512;
    if (norm.numGrainSamples == 0) norm.numGrainSamples = 256;
    if (norm.sampleRate == 0) norm.sampleRate = 48000;
    return norm;
}

static OrbisNgs2RackOption NormalizeRackOption(const OrbisNgs2RackOption* opt, const System& sys) {
    OrbisNgs2RackOption norm{};
    if (opt) norm = *opt;
    if (norm.maxVoices == 0) norm.maxVoices = 64;
    if (norm.maxGrainSamples == 0) norm.maxGrainSamples = sys.option.maxGrainSamples;
    return norm;
}

// Context buffer of a system or rack in guest memory.
struct ContextBuffer {
    uint64_t addr = 0;
    uint64_t size = 0;
};

// Asks the guest OrbisNgs2BufferAllocator for a context buffer of req_sz bytes (the
// *WithAllocator entry points). Returns an empty buffer when the handler is null or fails.
static ContextBuffer AllocWithGuestAllocator(GuestContext* ctx, const OrbisNgs2BufferAllocator* alloc,
                                             size_t req_sz) {
    if (!alloc->allocHandler) return {};
    // Place a temporary OrbisNgs2ContextBufferInfo on the guest stack.
    uint64_t info_guest_addr = ctx->rsp - 128;
    auto* info = (OrbisNgs2ContextBufferInfo*)(ctx->mem_base + info_guest_addr);
    memset(info, 0, sizeof(*info));
    info->hostBufferSize = req_sz;
    info->userData = alloc->userData;

    int32_t alloc_res = CallGuestAlloc(ctx, alloc->allocHandler, info_guest_addr);
    if (alloc_res != 0 || !info->hostBuffer) return {};
    return {info->hostBuffer, req_sz};
}

// Takes the caller-supplied context buffer of sceNgs2SystemCreate and sceNgs2RackCreate.
// Returns 0, or the ORBIS_NGS2_ERROR code for an invalid OrbisNgs2ContextBufferInfo.
static uint32_t TakeCallerBuffer(GuestContext* ctx, uint64_t info_addr, size_t req_sz, ContextBuffer* out) {
    if (!info_addr) return ORBIS_NGS2_ERROR_INVALID_BUFFER_INFO;
    const auto* info = (const OrbisNgs2ContextBufferInfo*)(ctx->mem_base + info_addr);
    if (!info->hostBuffer) return ORBIS_NGS2_ERROR_INVALID_BUFFER_ADDRESS;
    if (info->hostBufferSize < req_sz) return ORBIS_NGS2_ERROR_INVALID_BUFFER_SIZE;
    *out = {info->hostBuffer, info->hostBufferSize};
    return 0;
}

// Registers a system whose context lives in buf. The caller holds g_ngs2.mtx.
static OrbisNgs2Handle RegisterSystem(const OrbisNgs2SystemOption& norm, ContextBuffer buf) {
    OrbisNgs2Handle h = g_ngs2.next_handle++;
    System& sys = g_ngs2.systems[h];
    sys.handle = h;
    sys.option = norm;
    sys.buffer_addr = buf.addr;
    sys.buffer_size = buf.size;
    sys.uid = g_ngs2.next_uid++;
    sys.render_count = 0;
    return h;
}

// Registers a rack of sys whose context lives in buf. The caller holds g_ngs2.mtx.
static OrbisNgs2Handle RegisterRack(System& sys, uint32_t rack_id, const OrbisNgs2RackOption& norm,
                                    ContextBuffer buf) {
    OrbisNgs2Handle rack_h = g_ngs2.next_handle++;
    Rack& rack = g_ngs2.racks[rack_h];
    rack.handle = rack_h;
    rack.system_handle = sys.handle;
    rack.rack_id = rack_id;
    rack.kind = KindOfRack(rack_id);
    rack.option = norm;
    rack.buffer_addr = buf.addr;
    rack.buffer_size = buf.size;

    size_t per_voice = 0;
    switch (rack.kind) {
    case RackKind::Sampler: per_voice = (size_t)FifoFrames * MaxChannels; break;
    case RackKind::Submixer:
    case RackKind::Reverb:
    case RackKind::Mastering: per_voice = (size_t)MaxChannels * MaxGrainSamples; break;
    case RackKind::Unknown:
        fprintf(stderr, "[ngs2] rack id 0x%x is not modelled: its voices make no sound\n", rack_id);
        break;
    }
    if (rack.kind == RackKind::Reverb) {
        fprintf(stderr, "[ngs2] reverb rack 0x%llx: reverb is not modelled, what is sent to it is dropped\n",
                (unsigned long long)rack_h);
    }
    rack.storage.assign(per_voice * norm.maxVoices, 0.0f);
    rack.voices.resize(norm.maxVoices);
    for (uint32_t i = 0; i < norm.maxVoices; ++i) {
        Voice& v = rack.voices[i];
        v.handle = g_ngs2.next_handle++;
        v.rack = &rack;
        v.voice_index = i;
        float* mem = per_voice ? rack.storage.data() + per_voice * i : nullptr;
        if (rack.kind == RackKind::Sampler) v.sampler.fifo = mem;
        else v.input = mem;
        g_ngs2.voices[v.handle] = &v;
    }
    sys.racks.push_back(&rack);
    return rack_h;
}

// Unregisters a rack's voices, cuts the ports that lead to them and frees their decoders. The
// caller holds g_ngs2.mtx and erases the rack.
static void ReleaseRack(Rack& rack) {
    for (auto& [h, other] : g_ngs2.racks) {
        for (Voice& v : other.voices) {
            for (Port& port : v.ports) {
                if (port.dest && port.dest->rack == &rack) port.dest = nullptr;
            }
        }
    }
    for (Voice& v : rack.voices) {
        if (v.sampler.at9) Atrac9ReleaseHandle(v.sampler.at9);
        g_ngs2.voices.erase(v.handle);
    }
}

} // namespace

extern "C" {

void shim_sceNgs2SystemCreateWithAllocator(GuestContext *ctx) {
    if (!ctx->rdi || !ctx->rsi || !ctx->rdx) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }

    const auto* opt = (const OrbisNgs2SystemOption*)(ctx->mem_base + ctx->rdi);
    const auto* alloc = (const OrbisNgs2BufferAllocator*)(ctx->mem_base + ctx->rsi);
    auto* out_handle = (OrbisNgs2Handle*)(ctx->mem_base + ctx->rdx);

    OrbisNgs2SystemOption norm = NormalizeSystemOption(opt);
    size_t req_sz = SystemBufferSize(norm);

    ContextBuffer buf = AllocWithGuestAllocator(ctx, alloc, req_sz);
    if (!buf.addr) {
        buf = {recomp_vm_alloc_named_aligned(ctx, req_sz, 4096, 0x3, 0, "Ngs2System"), req_sz};
    }

    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    OrbisNgs2Handle h = RegisterSystem(norm, buf);
    *out_handle = h;
    fprintf(stderr, "[ngs2] SystemCreateWithAllocator: handle=0x%llx, grain=%u, rate=%u, buf=0x%llx (%llu bytes)\n",
            (unsigned long long)h, norm.numGrainSamples, norm.sampleRate, (unsigned long long)buf.addr,
            (unsigned long long)buf.size);

    ctx->rax = 0;
    SHIM_RETURN();
}

// sceNgs2SystemCreate(option, bufferInfo, outHandle): the context lives in the caller's buffer.
void shim_sceNgs2SystemCreate(GuestContext *ctx) {
    uint64_t opt_addr = ctx->rdi;
    uint64_t info_addr = ctx->rsi;
    uint64_t out_h_addr = ctx->rdx;

    if (!out_h_addr) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }

    OrbisNgs2SystemOption norm =
        NormalizeSystemOption(opt_addr ? (const OrbisNgs2SystemOption*)(ctx->mem_base + opt_addr) : nullptr);
    ContextBuffer buf;
    if (uint32_t err = TakeCallerBuffer(ctx, info_addr, SystemBufferSize(norm), &buf)) {
        ctx->rax = (uint64_t)err;
        SHIM_RETURN();
    }

    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    OrbisNgs2Handle h = RegisterSystem(norm, buf);
    *(OrbisNgs2Handle*)(ctx->mem_base + out_h_addr) = h;
    fprintf(stderr, "[ngs2] SystemCreate: handle=0x%llx, grain=%u, rate=%u, buf=0x%llx (%llu bytes)\n",
            (unsigned long long)h, norm.numGrainSamples, norm.sampleRate, (unsigned long long)buf.addr,
            (unsigned long long)buf.size);

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2SystemDestroy(GuestContext *ctx) {
    OrbisNgs2Handle h = (OrbisNgs2Handle)ctx->rdi;
    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.systems.find(h);
    if (it == g_ngs2.systems.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
        SHIM_RETURN();
    }

    for (Rack* rack : it->second.racks) {
        OrbisNgs2Handle rh = rack->handle;
        ReleaseRack(*rack);
        g_ngs2.racks.erase(rh);
    }
    g_ngs2.systems.erase(it);

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2SystemSetGrainSamples(GuestContext *ctx) {
    OrbisNgs2Handle h = (OrbisNgs2Handle)ctx->rdi;
    uint32_t grain = (uint32_t)ctx->rsi;
    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.systems.find(h);
    if (it == g_ngs2.systems.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
        SHIM_RETURN();
    }
    if (!ValidGrain(grain)) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_NUM_GRAIN_SAMPLES;
        SHIM_RETURN();
    }
    it->second.option.numGrainSamples = grain;
    ctx->rax = 0;
    SHIM_RETURN();
}

// sceNgs2SystemRender(systemHandle, bufferInfo, numBufferInfo): renders numGrainSamples samples
// of every voice of the system into the buffers.
void shim_sceNgs2SystemRender(GuestContext *ctx) {
    OrbisNgs2Handle h = (OrbisNgs2Handle)ctx->rdi;
    uint64_t buffers_addr = ctx->rsi;
    uint32_t num_buffers = (uint32_t)ctx->rdx;

    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.systems.find(h);
    if (it == g_ngs2.systems.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
        SHIM_RETURN();
    }
    System& sys = it->second;

    const auto* bufs = (const OrbisNgs2RenderBufferInfo*)GuestRange(
        ctx, buffers_addr, (uint64_t)num_buffers * sizeof(OrbisNgs2RenderBufferInfo));
    if (num_buffers && !bufs) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_BUFFER_INFO;
        SHIM_RETURN();
    }
    if (!sys.render_logged && num_buffers) {
        sys.render_logged = true;
        fprintf(stderr, "[ngs2] SystemRender: system 0x%llx, %u samples, %u buffers, first: type 0x%x, %u channels, %llu bytes\n",
                (unsigned long long)h, sys.option.numGrainSamples, num_buffers, bufs[0].waveformType,
                bufs[0].numChannels, (unsigned long long)bufs[0].bufferSize);
    }
    if (uint32_t err = PrepareRenderBuffers(ctx, bufs, num_buffers, sys.option.numGrainSamples)) {
        ctx->rax = (uint64_t)err;
        SHIM_RETURN();
    }

    Render(ctx, sys, bufs, num_buffers);
    sys.render_count++;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2RackCreateWithAllocator(GuestContext *ctx) {
    OrbisNgs2Handle sys_h = (OrbisNgs2Handle)ctx->rdi;
    uint32_t rack_id = (uint32_t)ctx->rsi;
    uint64_t opt_addr = ctx->rdx;
    uint64_t alloc_addr = ctx->rcx;
    uint64_t out_h_addr = ctx->r8;

    if (!out_h_addr) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }

    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto sys_it = g_ngs2.systems.find(sys_h);
    if (sys_it == g_ngs2.systems.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
        SHIM_RETURN();
    }

    OrbisNgs2RackOption norm = NormalizeRackOption(
        opt_addr ? (const OrbisNgs2RackOption*)(ctx->mem_base + opt_addr) : nullptr, sys_it->second);
    size_t req_sz = RackBufferSize(norm);

    ContextBuffer buf;
    if (alloc_addr) {
        buf = AllocWithGuestAllocator(ctx, (const OrbisNgs2BufferAllocator*)(ctx->mem_base + alloc_addr), req_sz);
    }
    if (!buf.addr) {
        buf = {recomp_vm_alloc_named_aligned(ctx, req_sz, 4096, 0x3, 0, "Ngs2Rack"), req_sz};
    }

    OrbisNgs2Handle rack_h = RegisterRack(sys_it->second, rack_id, norm, buf);
    *(OrbisNgs2Handle*)(ctx->mem_base + out_h_addr) = rack_h;

    fprintf(stderr, "[ngs2] RackCreateWithAllocator: sys=0x%llx, rack_id=0x%x, handle=0x%llx, voices=%u\n",
            (unsigned long long)sys_h, rack_id, (unsigned long long)rack_h, norm.maxVoices);

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2RackDestroy(GuestContext *ctx) {
    OrbisNgs2Handle h = (OrbisNgs2Handle)ctx->rdi;
    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.racks.find(h);
    if (it == g_ngs2.racks.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE;
        SHIM_RETURN();
    }

    Rack* rack = &it->second;
    ReleaseRack(*rack);
    auto sys_it = g_ngs2.systems.find(rack->system_handle);
    if (sys_it != g_ngs2.systems.end()) {
        auto& rv = sys_it->second.racks;
        rv.erase(std::remove(rv.begin(), rv.end(), rack), rv.end());
    }

    g_ngs2.racks.erase(it);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2RackGetVoiceHandle(GuestContext *ctx) {
    OrbisNgs2Handle rack_h = (OrbisNgs2Handle)ctx->rdi;
    uint32_t voice_idx = (uint32_t)ctx->rsi;
    uint64_t out_h_addr = ctx->rdx;

    if (!out_h_addr) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }

    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.racks.find(rack_h);
    if (it == g_ngs2.racks.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE;
        SHIM_RETURN();
    }

    if (voice_idx >= it->second.voices.size()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_VOICE_INDEX;
        SHIM_RETURN();
    }

    *(OrbisNgs2Handle*)(ctx->mem_base + out_h_addr) = it->second.voices[voice_idx].handle;
    ctx->rax = 0;
    SHIM_RETURN();
}

// sceNgs2VoiceControl(voiceHandle, paramList): applies a chain of params, each header's `next`
// giving the byte offset of the following one (0 ends the chain).
void shim_sceNgs2VoiceControl(GuestContext *ctx) {
    OrbisNgs2Handle v_h = (OrbisNgs2Handle)ctx->rdi;
    uint64_t param_addr = ctx->rsi;

    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.voices.find(v_h);
    if (it == g_ngs2.voices.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
        SHIM_RETURN();
    }
    Voice& v = *it->second;

    for (uint32_t n = 0; param_addr && n < MaxParams; ++n) {
        const uint8_t* hp = GuestRange(ctx, param_addr, sizeof(OrbisNgs2VoiceParamHeader));
        if (!hp) {
            NGS2_WARN(16, "voice 0x%llx: param at 0x%llx is not in guest memory\n", (unsigned long long)v_h,
                      (unsigned long long)param_addr);
            break;
        }
        OrbisNgs2VoiceParamHeader header;
        memcpy(&header, hp, sizeof(header));
        const uint8_t* p = GuestRange(ctx, param_addr, header.size);
        if (!p || header.size < sizeof(header)) {
            NGS2_WARN(16, "voice 0x%llx: param 0x%x at 0x%llx has a bad size %u\n", (unsigned long long)v_h,
                      header.id, (unsigned long long)param_addr, header.size);
            break;
        }
        ApplyParam(ctx, v, p, header.id, header.size);
        if (header.next == 0) break;
        param_addr += (int64_t)header.next;
    }

    ctx->rax = 0;
    SHIM_RETURN();
}

// sceNgs2VoiceGetState(voiceHandle, state, stateSize): OrbisNgs2SamplerVoiceState for sampler
// voices, the flags alone for the others.
void shim_sceNgs2VoiceGetState(GuestContext *ctx) {
    OrbisNgs2Handle v_h = (OrbisNgs2Handle)ctx->rdi;
    uint64_t state_addr = ctx->rsi;
    size_t state_sz = (size_t)ctx->rdx;

    uint8_t* dst = state_sz >= sizeof(OrbisNgs2VoiceState) ? GuestRange(ctx, state_addr, state_sz) : nullptr;
    if (!dst) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }

    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.voices.find(v_h);
    if (it == g_ngs2.voices.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
        SHIM_RETURN();
    }
    const Voice& v = *it->second;

    OrbisNgs2SamplerVoiceState s{};
    s.stateFlags = v.state;
    if (v.rack->kind == RackKind::Sampler) {
        const Sampler& smp = v.sampler;
        s.envelopeHeight = (v.state & StatePlaying) ? 1.0f : 0.0f;
        s.peakHeight = smp.peak;
        s.numDecodedSamples = smp.decoded_samples;
        s.numDecodedBytes = smp.decoded_bytes;
        if (smp.count) {
            const QueuedBlock& q = smp.queue[smp.head];
            s.userData = q.block.userData;
            s.waveformData = q.data + q.block.dataOffset;
        }
    }
    const size_t n = v.rack->kind == RackKind::Sampler ? sizeof(s) : sizeof(OrbisNgs2VoiceState);
    memset(dst, 0, state_sz);
    memcpy(dst, &s, std::min(state_sz, n));

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2VoiceGetStateFlags(GuestContext *ctx) {
    OrbisNgs2Handle v_h = (OrbisNgs2Handle)ctx->rdi;
    uint64_t flags_addr = ctx->rsi;

    if (!flags_addr) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }

    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.voices.find(v_h);
    if (it == g_ngs2.voices.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
        SHIM_RETURN();
    }

    *(uint32_t*)(ctx->mem_base + flags_addr) = it->second->state;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2ParseWaveformData(GuestContext *ctx) {
    uint64_t data_addr = ctx->rdi;
    size_t data_sz = (size_t)ctx->rsi;
    uint64_t out_info_addr = ctx->rdx;

    if (!out_info_addr) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }

    auto* info = (OrbisNgs2WaveformInfo*)(ctx->mem_base + out_info_addr);
    memset(info, 0, sizeof(*info));
    info->format.waveformType = 0x12; // PCM 16-bit LE
    info->format.numChannels = 2;
    info->format.sampleRate = 48000;
    info->dataSize = (uint32_t)data_sz;
    info->numSamples = data_sz >= 4 ? (uint32_t)(data_sz / 4) : 0;
    info->numBlocks = 1;
    info->aBlock[0].numSamples = info->numSamples;
    info->aBlock[0].dataSize = (uint32_t)data_sz;

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2CalcWaveformBlock(GuestContext *ctx) {
    uint64_t fmt_addr = ctx->rdi;
    uint32_t sample_pos = (uint32_t)ctx->rsi;
    uint32_t num_samples = (uint32_t)ctx->rdx;
    uint64_t out_block_addr = ctx->rcx;

    if (!out_block_addr) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }

    auto* block = (OrbisNgs2WaveformBlock*)(ctx->mem_base + out_block_addr);
    memset(block, 0, sizeof(*block));
    block->numSamples = num_samples;
    block->dataOffset = sample_pos * 4;

    ctx->rax = 0;
    SHIM_RETURN();
}

// sceNgs2RackCreate(systemHandle, rackId, option, bufferInfo, outHandle): the context lives in
// the caller's buffer.
void shim_sceNgs2RackCreate(GuestContext *ctx) {
    OrbisNgs2Handle sys_h = (OrbisNgs2Handle)ctx->rdi;
    uint32_t rack_id = (uint32_t)ctx->rsi;
    uint64_t opt_addr = ctx->rdx;
    uint64_t info_addr = ctx->rcx;
    uint64_t out_h_addr = ctx->r8;

    if (!out_h_addr) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }

    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto sys_it = g_ngs2.systems.find(sys_h);
    if (sys_it == g_ngs2.systems.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
        SHIM_RETURN();
    }

    OrbisNgs2RackOption norm = NormalizeRackOption(
        opt_addr ? (const OrbisNgs2RackOption*)(ctx->mem_base + opt_addr) : nullptr, sys_it->second);
    ContextBuffer buf;
    if (uint32_t err = TakeCallerBuffer(ctx, info_addr, RackBufferSize(norm), &buf)) {
        ctx->rax = (uint64_t)err;
        SHIM_RETURN();
    }

    OrbisNgs2Handle rack_h = RegisterRack(sys_it->second, rack_id, norm, buf);
    *(OrbisNgs2Handle*)(ctx->mem_base + out_h_addr) = rack_h;
    fprintf(stderr, "[ngs2] RackCreate: sys=0x%llx, rack_id=0x%x, handle=0x%llx, voices=%u, buf=0x%llx (%llu bytes)\n",
            (unsigned long long)sys_h, rack_id, (unsigned long long)rack_h, norm.maxVoices,
            (unsigned long long)buf.addr, (unsigned long long)buf.size);

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2SystemQueryBufferSize(GuestContext *ctx) {
    if (!ctx->rsi) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }
    OrbisNgs2SystemOption norm{};
    if (ctx->rdi) {
        norm = *(const OrbisNgs2SystemOption*)(ctx->mem_base + ctx->rdi);
    }
    if (norm.maxGrainSamples == 0) norm.maxGrainSamples = 512;
    auto* out = (OrbisNgs2ContextBufferInfo*)(ctx->mem_base + ctx->rsi);
    memset(out, 0, sizeof(*out));
    out->hostBufferSize = SystemBufferSize(norm);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2RackQueryBufferSize(GuestContext *ctx) {
    if (!ctx->rdx) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }
    OrbisNgs2RackOption norm{};
    if (ctx->rsi) {
        norm = *(const OrbisNgs2RackOption*)(ctx->mem_base + ctx->rsi);
    }
    if (norm.maxVoices == 0) norm.maxVoices = 64;
    auto* out = (OrbisNgs2ContextBufferInfo*)(ctx->mem_base + ctx->rdx);
    memset(out, 0, sizeof(*out));
    out->hostBufferSize = RackBufferSize(norm);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2SystemGetInfo(GuestContext *ctx) {
    OrbisNgs2Handle h = (OrbisNgs2Handle)ctx->rdi;
    uint64_t info_addr = ctx->rsi;
    size_t sz = (size_t)ctx->rdx;
    if (!info_addr || sz < sizeof(OrbisNgs2SystemInfo)) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }
    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.systems.find(h);
    if (it == g_ngs2.systems.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE;
        SHIM_RETURN();
    }
    auto* info = (OrbisNgs2SystemInfo*)(ctx->mem_base + info_addr);
    memset(info, 0, sz);
    memcpy(info->name, it->second.option.name, sizeof(info->name));
    info->systemHandle = h;
    info->uid = it->second.uid;
    info->minGrainSamples = MinGrainSamples;
    info->maxGrainSamples = it->second.option.maxGrainSamples;
    info->numGrainSamples = it->second.option.numGrainSamples;
    info->sampleRate = it->second.option.sampleRate;
    info->stateFlags = StateInUse;
    info->rackCount = (uint32_t)it->second.racks.size();
    info->renderCount = it->second.render_count;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2RackGetInfo(GuestContext *ctx) {
    OrbisNgs2Handle h = (OrbisNgs2Handle)ctx->rdi;
    uint64_t info_addr = ctx->rsi;
    size_t sz = (size_t)ctx->rdx;
    if (!info_addr || sz < sizeof(OrbisNgs2RackInfo)) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }
    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.racks.find(h);
    if (it == g_ngs2.racks.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE;
        SHIM_RETURN();
    }
    auto* info = (OrbisNgs2RackInfo*)(ctx->mem_base + info_addr);
    memset(info, 0, sz);
    memcpy(info->name, it->second.option.name, sizeof(info->name));
    info->rackHandle = h;
    info->ownerSystemHandle = it->second.system_handle;
    info->rackId = it->second.rack_id;
    info->maxVoices = it->second.option.maxVoices;
    info->maxGrainSamples = it->second.option.maxGrainSamples;
    info->stateFlags = StateInUse;
    info->activeVoiceCount = (uint32_t)std::count_if(it->second.voices.begin(), it->second.voices.end(),
                                                     [](const Voice& v) { return (v.state & StateInUse) != 0; });
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2SystemLock(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2SystemUnlock(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2RackLock(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2RackUnlock(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2VoiceGetOwner(GuestContext *ctx) {
    OrbisNgs2Handle v_h = (OrbisNgs2Handle)ctx->rdi;
    uint64_t out_rack_addr = ctx->rsi;
    uint64_t out_voice_id_addr = ctx->rdx;

    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.voices.find(v_h);
    if (it == g_ngs2.voices.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
        SHIM_RETURN();
    }

    if (out_rack_addr) *(OrbisNgs2Handle*)(ctx->mem_base + out_rack_addr) = it->second->rack->handle;
    if (out_voice_id_addr) *(uint32_t*)(ctx->mem_base + out_voice_id_addr) = it->second->voice_index;

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2SystemSetUserData(GuestContext *ctx) {
    OrbisNgs2Handle h = (OrbisNgs2Handle)ctx->rdi;
    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.systems.find(h);
    if (it != g_ngs2.systems.end()) it->second.user_data = ctx->rsi;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2SystemGetUserData(GuestContext *ctx) {
    OrbisNgs2Handle h = (OrbisNgs2Handle)ctx->rdi;
    if (ctx->rsi) {
        std::lock_guard<std::mutex> lock(g_ngs2.mtx);
        auto it = g_ngs2.systems.find(h);
        *(uint64_t*)(ctx->mem_base + ctx->rsi) = (it != g_ngs2.systems.end()) ? it->second.user_data : 0;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2RackSetUserData(GuestContext *ctx) {
    OrbisNgs2Handle h = (OrbisNgs2Handle)ctx->rdi;
    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.racks.find(h);
    if (it != g_ngs2.racks.end()) it->second.user_data = ctx->rsi;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2RackGetUserData(GuestContext *ctx) {
    OrbisNgs2Handle h = (OrbisNgs2Handle)ctx->rdi;
    if (ctx->rsi) {
        std::lock_guard<std::mutex> lock(g_ngs2.mtx);
        auto it = g_ngs2.racks.find(h);
        *(uint64_t*)(ctx->mem_base + ctx->rsi) = (it != g_ngs2.racks.end()) ? it->second.user_data : 0;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

} // extern "C"
