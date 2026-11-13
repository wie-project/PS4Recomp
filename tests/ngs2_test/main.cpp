// Host test for libSceNgs2: the create entry points that take a caller-supplied context buffer,
// and sampler voices (PCM16, PCM24, ATRAC9) played through submixer and mastering voices into
// sceNgs2SystemRender's buffers. Build and run (from the repository root, after
// ./scripts/build.sh has built 3rdparty/libatrac9/libatrac9.a):
//   clang++ -std=c++17 -I pkg/runtime/core -I pkg/runtime/modules/libSceNgs2 -I 3rdparty/libatrac9/C/src \
//     tests/ngs2_test/main.cpp pkg/runtime/modules/libSceNgs2/ps4_ngs2.cpp 3rdparty/libatrac9/libatrac9.a \
//     -o /tmp/ngs2_test && /tmp/ngs2_test

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "libatrac9.h"
#include "ps4_ngs2.h"
#include "recomp_context.h"

// The caller-buffer variants must neither run guest code nor allocate guest memory. An empty
// dispatch table makes any guest call abort with "Unresolved indirect jump/call".
extern "C" recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE] = {};

extern "C" uint64_t recomp_vm_alloc_named_aligned(GuestContext *, uint64_t size, uint64_t, int, int, const char *) {
    fprintf(stderr, "[ngs2-test] FAIL: runtime allocation of %llu bytes\n", (unsigned long long)size);
    abort();
}

// C++ heap allocations, to check that rendering makes none.
static size_t g_news;
void *operator new(size_t n) {
    ++g_news;
    if (void *p = malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "[ngs2-test] FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

namespace {

constexpr uint64_t kMemSize = 4 << 20;
constexpr uint64_t kStack = 0x80000;     // guest stack top
constexpr uint64_t kInfo = 0x10000;      // OrbisNgs2ContextBufferInfo
constexpr uint64_t kOut = 0x11000;       // out handle
constexpr uint64_t kSysInfo = 0x12000;   // OrbisNgs2SystemInfo / OrbisNgs2RackInfo
constexpr uint64_t kSysBuf = 0x20000;    // system context buffer
constexpr uint64_t kRackBuf = 0x40000;   // rack context buffers
constexpr uint64_t kBufSize = 0x20000;   // fits the default 512-sample system context
constexpr uint64_t kParams = 0x90000;    // VoiceControl param chain
constexpr uint64_t kOption = 0x91000;    // OrbisNgs2RackOption
constexpr uint64_t kState = 0x92000;     // OrbisNgs2SamplerVoiceState
constexpr uint64_t kRender = 0x93000;    // OrbisNgs2RenderBufferInfo
constexpr uint64_t kOutBuf = 0x94000;    // render buffer
constexpr uint64_t kBlocks = 0x98000;    // OrbisNgs2WaveformBlock arrays
constexpr uint64_t kLevels = 0x99000;    // matrix levels
constexpr uint64_t kWave = 0x100000;     // waveform data
constexpr uint32_t kGrain = 256;         // the default numGrainSamples
constexpr uint32_t kSamplerVoices = 4;

GuestContext g_ctx;

template <typename T>
T *At(uint64_t addr) {
    return (T *)(g_ctx.mem_base + addr);
}

// Calls a shim the way guest code does: with a return address on the stack.
uint32_t Call(void (*shim)(GuestContext *), uint64_t a, uint64_t b, uint64_t c, uint64_t d = 0, uint64_t e = 0) {
    GuestContext &ctx = g_ctx;
    ctx.rsp = kStack - 8;
    ctx.rdi = a;
    ctx.rsi = b;
    ctx.rdx = c;
    ctx.rcx = d;
    ctx.r8 = e;
    shim(&ctx);
    CHECK(ctx.rsp == kStack);
    return (uint32_t)ctx.rax;
}

void SetBufferInfo(uint64_t host_buffer, uint64_t size) {
    auto *info = At<OrbisNgs2ContextBufferInfo>(kInfo);
    memset(info, 0, sizeof(*info));
    info->hostBuffer = host_buffer;
    info->hostBufferSize = size;
}

OrbisNgs2Handle OutHandle() { return *At<OrbisNgs2Handle>(kOut); }

// A VoiceControl param chain at kParams.
class Params {
  public:
    template <typename T>
    Params &Add(uint32_t id, T p) {
        p.header.size = sizeof(T);
        p.header.next = 0;
        p.header.id = id;
        if (last_) At<OrbisNgs2VoiceParamHeader>(last_)->next = (int16_t)(at_ - last_);
        memcpy(At<T>(at_), &p, sizeof(T));
        last_ = at_;
        at_ += (sizeof(T) + 7) & ~size_t(7);
        return *this;
    }
    uint32_t Send(OrbisNgs2Handle voice) { return Call(shim_sceNgs2VoiceControl, voice, kParams, 0); }

  private:
    uint64_t at_ = kParams;
    uint64_t last_ = 0;
};

Params &Event(Params &p, uint32_t event) {
    OrbisNgs2VoiceEventParam e{};
    e.eventId = event;
    return p.Add(ORBIS_NGS2_VOICE_PARAM_EVENT, e);
}

Params &Patch(Params &p, uint32_t port, OrbisNgs2Handle dest) {
    OrbisNgs2VoicePatchParam m{};
    m.port = port;
    m.destHandle = dest;
    return p.Add(ORBIS_NGS2_VOICE_PARAM_PATCH, m);
}

Params &Setup(Params &p, uint32_t type, uint32_t channels, uint32_t rate, uint32_t config = 0) {
    OrbisNgs2SamplerVoiceSetupParam s{};
    s.format.waveformType = type;
    s.format.numChannels = channels;
    s.format.sampleRate = rate;
    s.format.configData = config;
    return p.Add(ORBIS_NGS2_SAMPLER_VOICE_PARAM_SETUP, s);
}

// Queues `blocks` (written at kBlocks + slot * 0x100) of data at `data`.
Params &Blocks(Params &p, uint64_t data, uint32_t flags, const std::vector<OrbisNgs2WaveformBlock> &blocks,
               uint32_t slot = 0) {
    const uint64_t addr = kBlocks + slot * 0x100;
    if (!blocks.empty()) memcpy(At<OrbisNgs2WaveformBlock>(addr), blocks.data(), blocks.size() * sizeof(blocks[0]));
    OrbisNgs2SamplerVoiceWaveformBlocksParam b{};
    b.data = data;
    b.flags = flags;
    b.numBlocks = (uint32_t)blocks.size();
    b.aBlock = blocks.empty() ? 0 : addr;
    return p.Add(ORBIS_NGS2_SAMPLER_VOICE_PARAM_WAVEFORM_BLOCKS, b);
}

OrbisNgs2WaveformBlock Block(uint32_t offset, uint32_t size, uint32_t samples, uint32_t skip = 0, uint32_t repeats = 0,
                             uint64_t user = 0) {
    OrbisNgs2WaveformBlock b{};
    b.dataOffset = offset;
    b.dataSize = size;
    b.numSamples = samples;
    b.numSkipSamples = skip;
    b.numRepeats = repeats;
    b.userData = user;
    return b;
}

OrbisNgs2SamplerVoiceState State(OrbisNgs2Handle voice) {
    CHECK(Call(shim_sceNgs2VoiceGetState, voice, kState, sizeof(OrbisNgs2SamplerVoiceState)) == 0);
    return *At<OrbisNgs2SamplerVoiceState>(kState);
}

uint32_t Flags(OrbisNgs2Handle voice) {
    CHECK(Call(shim_sceNgs2VoiceGetStateFlags, voice, kState, 0) == 0);
    return *At<uint32_t>(kState);
}

constexpr uint32_t kPlaying = ORBIS_NGS2_VOICE_STATE_FLAG_INUSE | ORBIS_NGS2_VOICE_STATE_FLAG_PLAYING;
constexpr uint32_t kEmpty = ORBIS_NGS2_VOICE_STATE_FLAG_EMPTY;

// A system with sampler voices -> one stereo submixer -> one stereo mastering voice -> buffer 0.
struct Graph {
    OrbisNgs2Handle system = 0;
    OrbisNgs2Handle mastering = 0;
    OrbisNgs2Handle submixer = 0;
    OrbisNgs2Handle voices[kSamplerVoices] = {};
    OrbisNgs2Handle racks[3] = {};
};

OrbisNgs2Handle CreateRack(OrbisNgs2Handle sys, uint32_t rack_id, uint32_t voices, uint32_t slot) {
    auto *opt = At<OrbisNgs2RackOption>(kOption);
    memset(opt, 0, sizeof(*opt));
    opt->size = sizeof(*opt);
    opt->maxVoices = voices;
    SetBufferInfo(kRackBuf + slot * 0x2000, 0x2000);
    CHECK(Call(shim_sceNgs2RackCreate, sys, rack_id, kOption, kInfo, kOut) == 0);
    return OutHandle();
}

OrbisNgs2Handle VoiceOf(OrbisNgs2Handle rack, uint32_t index) {
    CHECK(Call(shim_sceNgs2RackGetVoiceHandle, rack, index, kOut) == 0);
    return OutHandle();
}

Graph CreateGraph() {
    Graph g;
    SetBufferInfo(kSysBuf, kBufSize);
    CHECK(Call(shim_sceNgs2SystemCreate, 0, kInfo, kOut) == 0);
    g.system = OutHandle();
    g.racks[0] = CreateRack(g.system, ORBIS_NGS2_RACK_ID_MASTERING, 1, 0);
    g.racks[1] = CreateRack(g.system, ORBIS_NGS2_RACK_ID_SUBMIXER, 1, 1);
    g.racks[2] = CreateRack(g.system, ORBIS_NGS2_RACK_ID_SAMPLER, kSamplerVoices, 2);
    g.mastering = VoiceOf(g.racks[0], 0);
    g.submixer = VoiceOf(g.racks[1], 0);
    {
        Params p;
        OrbisNgs2MasteringVoiceSetupParam s{};
        s.numInputChannels = 2;
        p.Add(ORBIS_NGS2_MASTERING_VOICE_PARAM_SETUP, s);
        OrbisNgs2MasteringVoiceOutputParam o{};
        o.outputId = 0;
        p.Add(ORBIS_NGS2_MASTERING_VOICE_PARAM_OUTPUT, o);
        CHECK(p.Send(g.mastering) == 0);
    }
    {
        Params p;
        OrbisNgs2SubmixerVoiceSetupParam s{};
        s.numIoChannels = 2;
        p.Add(ORBIS_NGS2_SUBMIXER_VOICE_PARAM_SETUP, s);
        Patch(p, 0, g.racks[0]); // a rack handle stands for its first voice
        CHECK(p.Send(g.submixer) == 0);
    }
    for (uint32_t i = 0; i < kSamplerVoices; ++i) {
        g.voices[i] = VoiceOf(g.racks[2], i);
        Params p;
        CHECK(Patch(p, 0, g.submixer).Send(g.voices[i]) == 0);
        CHECK(Flags(g.voices[i]) == 0); // a voice nobody played is not in use
    }
    return g;
}

void DestroyGraph(const Graph &g) { CHECK(Call(shim_sceNgs2SystemDestroy, g.system, 0, 0) == 0); }

// Renders one grain into a stereo float (or int16) buffer at kOutBuf; checks it allocates nothing.
uint32_t RenderTo(const Graph &g, uint32_t type, uint32_t channels, uint64_t size) {
    auto *info = At<OrbisNgs2RenderBufferInfo>(kRender);
    memset(info, 0, sizeof(*info));
    info->buffer = kOutBuf;
    info->bufferSize = size;
    info->waveformType = type;
    info->numChannels = channels;
    memset(At<uint8_t>(kOutBuf), 0x5A, size); // the render clears it
    const size_t news = g_news;
    const uint32_t r = Call(shim_sceNgs2SystemRender, g.system, kRender, 1);
    CHECK(g_news == news);
    return r;
}

const float *Render(const Graph &g) {
    CHECK(RenderTo(g, ORBIS_NGS2_WAVEFORM_TYPE_PCM_F32L, 2, kGrain * 2 * sizeof(float)) == 0);
    return At<float>(kOutBuf);
}

int16_t Pcm16Value(uint32_t i, uint32_t c) { return (int16_t)((int32_t)(i * 97 + c * 5000) % 30000 - 15000); }
int32_t Pcm24Value(uint32_t i, uint32_t c) { return (int32_t)((i * 39119u + c * 1000003u) % 8000000u) - 4000000; }

void WritePcm16(uint64_t addr, uint32_t frames, uint32_t channels) {
    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t c = 0; c < channels; ++c) {
            int16_t x = Pcm16Value(i, c);
            memcpy(At<uint8_t>(addr + (i * channels + c) * 2), &x, 2);
        }
    }
}

void WritePcm24(uint64_t addr, uint32_t frames, uint32_t channels) {
    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t c = 0; c < channels; ++c) {
            const uint32_t x = (uint32_t)Pcm24Value(i, c);
            uint8_t *p = At<uint8_t>(addr + (i * channels + c) * 3);
            p[0] = (uint8_t)x;
            p[1] = (uint8_t)(x >> 8);
            p[2] = (uint8_t)(x >> 16);
        }
    }
}

float Pcm16(uint32_t i, uint32_t c) { return (float)Pcm16Value(i, c) * (1.0f / 32768.0f); }
float Pcm24(uint32_t i, uint32_t c) { return (float)Pcm24Value(i, c) * (1.0f / 8388608.0f); }

void TestCreateEntryPoints() {
    // sceNgs2SystemCreate: invalid buffer info is rejected.
    CHECK(Call(shim_sceNgs2SystemCreate, 0, 0, kOut) == ORBIS_NGS2_ERROR_INVALID_BUFFER_INFO);
    SetBufferInfo(0, kBufSize);
    CHECK(Call(shim_sceNgs2SystemCreate, 0, kInfo, kOut) == ORBIS_NGS2_ERROR_INVALID_BUFFER_ADDRESS);
    SetBufferInfo(kSysBuf, 16);
    CHECK(Call(shim_sceNgs2SystemCreate, 0, kInfo, kOut) == ORBIS_NGS2_ERROR_INVALID_BUFFER_SIZE);
    CHECK(Call(shim_sceNgs2SystemCreate, 0, kInfo, 0) == ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS);
    printf("[+] sceNgs2SystemCreate rejects invalid buffer info\n");

    // sceNgs2SystemCreate: the context lives in the caller's buffer.
    SetBufferInfo(kSysBuf, kBufSize);
    CHECK(Call(shim_sceNgs2SystemCreate, 0, kInfo, kOut) == 0);
    OrbisNgs2Handle sys = OutHandle();
    CHECK(sys != 0);
    CHECK(Call(shim_sceNgs2SystemGetInfo, sys, kSysInfo, sizeof(OrbisNgs2SystemInfo)) == 0);
    const auto *sys_info = At<const OrbisNgs2SystemInfo>(kSysInfo);
    CHECK(sys_info->systemHandle == sys && sys_info->sampleRate == 48000 && sys_info->rackCount == 0);
    printf("[+] sceNgs2SystemCreate uses the caller buffer\n");

    // sceNgs2RackCreate: invalid buffer info is rejected.
    CHECK(Call(shim_sceNgs2RackCreate, sys, 0x1000, 0, 0, kOut) == ORBIS_NGS2_ERROR_INVALID_BUFFER_INFO);
    SetBufferInfo(0, kBufSize);
    CHECK(Call(shim_sceNgs2RackCreate, sys, 0x1000, 0, kInfo, kOut) == ORBIS_NGS2_ERROR_INVALID_BUFFER_ADDRESS);
    SetBufferInfo(kRackBuf, 16);
    CHECK(Call(shim_sceNgs2RackCreate, sys, 0x1000, 0, kInfo, kOut) == ORBIS_NGS2_ERROR_INVALID_BUFFER_SIZE);
    SetBufferInfo(kRackBuf, kBufSize);
    CHECK(Call(shim_sceNgs2RackCreate, sys + 1000, 0x1000, 0, kInfo, kOut) == ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE);
    printf("[+] sceNgs2RackCreate rejects invalid arguments\n");

    // sceNgs2RackCreate: this used to treat bufferInfo as an allocator and jump to hostBuffer.
    CHECK(Call(shim_sceNgs2RackCreate, sys, 0x1000, 0, kInfo, kOut) == 0);
    OrbisNgs2Handle rack = OutHandle();
    CHECK(rack != 0 && rack != sys);
    CHECK(Call(shim_sceNgs2RackGetInfo, rack, kSysInfo, sizeof(OrbisNgs2RackInfo)) == 0);
    const auto *rack_info = At<const OrbisNgs2RackInfo>(kSysInfo);
    CHECK(rack_info->rackHandle == rack && rack_info->ownerSystemHandle == sys);
    CHECK(rack_info->rackId == 0x1000 && rack_info->maxVoices == 64 && rack_info->activeVoiceCount == 0);
    CHECK(Call(shim_sceNgs2SystemGetInfo, sys, kSysInfo, sizeof(OrbisNgs2SystemInfo)) == 0);
    CHECK(sys_info->rackCount == 1);
    printf("[+] sceNgs2RackCreate uses the caller buffer\n");

    CHECK(Call(shim_sceNgs2RackDestroy, rack, 0, 0) == 0);
    CHECK(Call(shim_sceNgs2SystemDestroy, sys, 0, 0) == 0);
}

// A PCM16 one-shot: samples in time, the state flags of a voice whose queue runs dry, then ends.
void TestPcm16StateAndTiming() {
    Graph g = CreateGraph();
    const OrbisNgs2Handle v = g.voices[0];
    constexpr uint32_t kFrames = 600;
    WritePcm16(kWave, kFrames, 1);
    {
        Params p;
        Setup(p, ORBIS_NGS2_WAVEFORM_TYPE_PCM_I16L, 1, 48000);
        Blocks(p, kWave, ORBIS_NGS2_WAVEFORM_BLOCKS_FLAG_QUEUE, {Block(0, kFrames * 2, kFrames, 0, 0, 77)});
        Event(p, ORBIS_NGS2_VOICE_EVENT_PLAY);
        CHECK(p.Send(v) == 0);
    }
    OrbisNgs2SamplerVoiceState s = State(v);
    CHECK(s.stateFlags == kPlaying && s.numDecodedSamples == 0 && s.numDecodedBytes == 0);
    CHECK(s.userData == 77 && s.waveformData == kWave);
    CHECK(Call(shim_sceNgs2RackGetInfo, g.racks[2], kSysInfo, sizeof(OrbisNgs2RackInfo)) == 0);
    CHECK(At<OrbisNgs2RackInfo>(kSysInfo)->activeVoiceCount == 1);

    uint32_t played = 0;
    for (int r = 0; r < 3; ++r) {
        const float *out = Render(g);
        for (uint32_t f = 0; f < kGrain; ++f, ++played) {
            // Mono through identity ports: the left channel only.
            const float want = played < kFrames ? Pcm16(played, 0) : 0.0f;
            CHECK(out[f * 2] == want);
            CHECK(out[f * 2 + 1] == 0.0f);
        }
        s = State(v);
        if (r == 0) {
            // Decoding runs one 256-sample chunk ahead of playback.
            CHECK(s.stateFlags == kPlaying && s.numDecodedSamples == kGrain && s.numDecodedBytes == 2 * kGrain * 2);
            CHECK(s.peakHeight > 0.0f && s.envelopeHeight == 1.0f);
        }
    }
    // The queue ran dry and no end of data came: still in use and playing, flagged empty.
    CHECK(s.stateFlags == (kPlaying | kEmpty));
    CHECK(s.numDecodedSamples == kFrames && s.numDecodedBytes == kFrames * 2);
    CHECK(s.waveformData == 0);
    printf("[+] PCM16 voice plays its block sample for sample and reports decoded samples / bytes\n");

    // More data while empty: the flag clears and the voice plays on.
    {
        Params p;
        CHECK(Blocks(p, kWave, ORBIS_NGS2_WAVEFORM_BLOCKS_FLAG_QUEUE, {Block(0, 20, 10)}).Send(v) == 0);
    }
    CHECK(Flags(v) == kPlaying);
    const float *out = Render(g);
    for (uint32_t f = 0; f < 10; ++f) CHECK(out[f * 2] == Pcm16(f, 0));
    CHECK(out[10 * 2] == 0.0f);
    CHECK(Flags(v) == (kPlaying | kEmpty));
    // End of data (no queue flag, no blocks): the voice finishes at the next render.
    {
        Params p;
        CHECK(Blocks(p, kWave, 0, {}).Send(v) == 0);
    }
    CHECK(Flags(v) == (kPlaying | kEmpty));
    Render(g);
    s = State(v);
    CHECK(s.stateFlags == 0 && s.numDecodedSamples == kFrames + 10);
    CHECK(Call(shim_sceNgs2RackGetInfo, g.racks[2], kSysInfo, sizeof(OrbisNgs2RackInfo)) == 0);
    CHECK(At<OrbisNgs2RackInfo>(kSysInfo)->activeVoiceCount == 0);
    printf("[+] an empty voice resumes with new blocks and finishes after the end of its data\n");
    DestroyGraph(g);
}

// PCM24 stereo through a port matrix that swaps the channels at half level, with skipped samples,
// a repeat and the mastering gain.
void TestPcm24MatrixSkipRepeat() {
    Graph g = CreateGraph();
    const OrbisNgs2Handle v = g.voices[1];
    constexpr uint32_t kFrames = 150, kSkip = 10, kSamples = 100;
    WritePcm24(kWave, kFrames, 2);
    const float levels[4] = {0.0f, 0.5f, 0.5f, 0.0f}; // level[out * 2 + in]
    memcpy(At<float>(kLevels), levels, sizeof(levels));
    {
        Params p;
        OrbisNgs2MasteringVoiceGainParam gain{};
        gain.fbwLevel = 0.5f;
        gain.lfeLevel = 1.0f;
        CHECK(p.Add(ORBIS_NGS2_MASTERING_VOICE_PARAM_GAIN, gain).Send(g.mastering) == 0);
    }
    {
        Params p;
        Setup(p, ORBIS_NGS2_WAVEFORM_TYPE_PCM_I24L, 2, 48000);
        OrbisNgs2VoiceMatrixLevelsParam m{};
        m.matrixId = 1;
        m.numLevels = 4;
        m.aLevel = kLevels;
        p.Add(ORBIS_NGS2_VOICE_PARAM_MATRIX_LEVELS, m);
        OrbisNgs2VoicePortMatrixParam pm{};
        pm.port = 0;
        pm.matrixId = 1;
        p.Add(ORBIS_NGS2_VOICE_PARAM_PORT_MATRIX, pm);
        Blocks(p, kWave, ORBIS_NGS2_WAVEFORM_BLOCKS_FLAG_QUEUE, {Block(0, kFrames * 6, kSamples, kSkip, 1)});
        Blocks(p, kWave, 0, {}, 1); // the only block: end of data
        Event(p, ORBIS_NGS2_VOICE_EVENT_PLAY);
        CHECK(p.Send(v) == 0);
    }
    const float *out = Render(g);
    for (uint32_t f = 0; f < kGrain; ++f) {
        float l = 0.0f, r = 0.0f;
        if (f < 2 * kSamples) {
            const uint32_t i = kSkip + f % kSamples;
            l = Pcm24(i, 1) * 0.25f;
            r = Pcm24(i, 0) * 0.25f;
        }
        CHECK(out[f * 2] == l);
        CHECK(out[f * 2 + 1] == r);
    }
    const OrbisNgs2SamplerVoiceState s = State(v);
    CHECK(s.stateFlags == 0);
    CHECK(s.numDecodedSamples == 2 * kSamples);
    CHECK(s.numDecodedBytes == 2 * (kSkip + kSamples) * 6); // each pass reads what it plays
    printf("[+] PCM24 stereo voice: matrix levels, skip, repeat and mastering gain\n");
    DestroyGraph(g);
}

// Pitch: a 24 kHz waveform plays at half speed with linear interpolation; pitch 2 skips samples.
void TestPitch() {
    Graph g = CreateGraph();
    const OrbisNgs2Handle v = g.voices[2];
    WritePcm16(kWave, 1000, 1);
    {
        Params p;
        Setup(p, ORBIS_NGS2_WAVEFORM_TYPE_PCM_I16L, 1, 24000);
        Blocks(p, kWave, ORBIS_NGS2_WAVEFORM_BLOCKS_FLAG_QUEUE, {Block(0, 2000, 1000)});
        Event(p, ORBIS_NGS2_VOICE_EVENT_PLAY);
        CHECK(p.Send(v) == 0);
    }
    const float *out = Render(g);
    for (uint32_t f = 0; f < kGrain; ++f) {
        const float a = Pcm16(f / 2, 0), b = Pcm16(f / 2 + 1, 0);
        CHECK(out[f * 2] == (f % 2 ? a + (b - a) * 0.5f : a));
    }
    CHECK(State(v).numDecodedSamples == kGrain / 2);
    {
        Params p;
        OrbisNgs2SamplerVoicePitchParam pitch{};
        pitch.ratio = 4.0f; // 24 kHz at pitch 4: two input samples per output sample
        CHECK(p.Add(ORBIS_NGS2_SAMPLER_VOICE_PARAM_PITCH, pitch).Send(v) == 0);
    }
    out = Render(g);
    for (uint32_t f = 0; f < kGrain; ++f) CHECK(out[f * 2] == Pcm16(kGrain / 2 + 2 * f, 0));
    CHECK(State(v).numDecodedSamples == kGrain / 2 + 2 * kGrain);
    printf("[+] sample rate and pitch resample the voice\n");
    DestroyGraph(g);
}

// Pause holds the voice where it is; stop and kill end it at once.
void TestPauseStopKill() {
    Graph g = CreateGraph();
    const OrbisNgs2Handle a = g.voices[0], b = g.voices[3];
    WritePcm16(kWave, 4000, 1);
    for (OrbisNgs2Handle v : {a, b}) {
        Params p;
        Setup(p, ORBIS_NGS2_WAVEFORM_TYPE_PCM_I16L, 1, 48000);
        Blocks(p, kWave, ORBIS_NGS2_WAVEFORM_BLOCKS_FLAG_QUEUE, {Block(0, 8000, 4000)});
        Event(p, ORBIS_NGS2_VOICE_EVENT_PLAY);
        CHECK(p.Send(v) == 0);
    }
    {
        Params p;
        CHECK(Event(p, ORBIS_NGS2_VOICE_EVENT_PAUSE).Send(b) == 0);
    }
    CHECK(Flags(b) == (kPlaying | ORBIS_NGS2_VOICE_STATE_FLAG_PAUSED));
    const float *out = Render(g);
    for (uint32_t f = 0; f < kGrain; ++f) CHECK(out[f * 2] == Pcm16(f, 0)); // voice a alone
    CHECK(State(b).numDecodedSamples == 0);
    {
        Params p;
        CHECK(Event(p, ORBIS_NGS2_VOICE_EVENT_RESUME).Send(b) == 0);
    }
    out = Render(g);
    for (uint32_t f = 0; f < kGrain; ++f) CHECK(out[f * 2] == Pcm16(kGrain + f, 0) + Pcm16(f, 0));
    CHECK(State(a).numDecodedSamples == 2 * kGrain && State(b).numDecodedSamples == kGrain);
    {
        Params p;
        CHECK(Event(p, ORBIS_NGS2_VOICE_EVENT_STOP).Send(a) == 0);
    }
    {
        Params p;
        CHECK(Event(p, ORBIS_NGS2_VOICE_EVENT_KILL).Send(b) == 0);
    }
    CHECK(Flags(a) == 0 && Flags(b) == 0);
    out = Render(g);
    for (uint32_t f = 0; f < kGrain * 2; ++f) CHECK(out[f] == 0.0f);
    printf("[+] pause holds a voice, stop and kill clear its in-use flag\n");
    DestroyGraph(g);
}

// An int16 render buffer, and render buffers the system cannot fill.
void TestRenderBuffers() {
    Graph g = CreateGraph();
    const OrbisNgs2Handle v = g.voices[0];
    WritePcm16(kWave, kGrain, 1);
    {
        Params p;
        Setup(p, ORBIS_NGS2_WAVEFORM_TYPE_PCM_I16L, 1, 48000);
        Blocks(p, kWave, ORBIS_NGS2_WAVEFORM_BLOCKS_FLAG_QUEUE, {Block(0, kGrain * 2, kGrain)});
        Event(p, ORBIS_NGS2_VOICE_EVENT_PLAY);
        CHECK(p.Send(v) == 0);
    }
    CHECK(RenderTo(g, ORBIS_NGS2_WAVEFORM_TYPE_PCM_F32L, 2, kGrain * 2 * 4 - 4) == ORBIS_NGS2_ERROR_INVALID_BUFFER_SIZE);
    CHECK(RenderTo(g, 0x14, 2, kGrain * 2 * 4) == ORBIS_NGS2_ERROR_INVALID_BUFFER_INFO);
    CHECK(State(v).numDecodedSamples == 0); // a failed render plays nothing
    CHECK(RenderTo(g, ORBIS_NGS2_WAVEFORM_TYPE_PCM_I16L, 2, kGrain * 2 * 2) == 0);
    const auto *out = At<int16_t>(kOutBuf);
    for (uint32_t f = 0; f < kGrain; ++f) {
        CHECK(out[f * 2] == (int16_t)lrintf(Pcm16(f, 0) * 32767.0f));
        CHECK(out[f * 2 + 1] == 0);
    }
    printf("[+] int16 render buffer; undersized and unsupported buffers are rejected\n");
    DestroyGraph(g);
}

// ---- ATRAC9 ----
//
// There is no ATRAC9 encoder here and no sample may come from a game, so the test writes its own
// bitstream: mono 48 kHz frames with valid band, gradient and scale factor headers followed by
// pseudo-random spectral bits, two frames per superframe. libatrac9 decodes them to some
// non-silent signal; the reference is that same stream decoded by libatrac9 directly, and the
// test checks that the voice delivers exactly it (superframe padding, block boundaries, skip).

constexpr uint32_t kAt9FrameBytes = 256;
constexpr uint32_t kAt9SuperframeBytes = 2 * kAt9FrameBytes;
constexpr uint32_t kAt9Config = 0xFEu << 24 | 7u << 20 | 0u << 17 | (kAt9FrameBytes - 1) << 5 | 1u << 3;

struct BitWriter {
    uint8_t *data;
    size_t bit = 0;
    void Put(uint32_t value, int bits) {
        for (int i = bits - 1; i >= 0; --i, ++bit) {
            if ((value >> i) & 1) data[bit >> 3] |= (uint8_t)(0x80 >> (bit & 7));
        }
    }
};

// Writes `superframes` superframes at `dst` and their decoded samples to `pcm`.
void BuildAtrac9(uint8_t *dst, uint32_t superframes, std::vector<float> *pcm) {
    unsigned char cfg[4] = {(unsigned char)(kAt9Config >> 24), (unsigned char)(kAt9Config >> 16),
                            (unsigned char)(kAt9Config >> 8), (unsigned char)kAt9Config};
    void *h = Atrac9GetHandle();
    CHECK(h && Atrac9InitDecoder(h, cfg) == 0);
    Atrac9CodecInfo info;
    CHECK(Atrac9GetCodecInfo(h, &info) == 0);
    CHECK(info.channels == 1 && info.samplingRate == 48000 && info.frameSamples == 256 &&
          info.framesInSuperframe == 2 && info.superframeSize == (int)kAt9SuperframeBytes);
    memset(dst, 0, (size_t)superframes * kAt9SuperframeBytes);
    uint32_t seed = 0x2468ace1u;
    float frame[256];
    for (uint32_t sf = 0; sf < superframes; ++sf) {
        size_t off = (size_t)sf * kAt9SuperframeBytes;
        for (int f = 0; f < 2; ++f) {
            BitWriter w{dst + off};
            w.Put(f ? 1 : 0, 1); // first frame of the superframe
            w.Put(0, 1);         // band params follow
            w.Put(5, 4);         // 8 bands
            w.Put(0, 1);         // no band extension
            w.Put(1, 2);         // gradient mode 1, start unit 0, start value 0, boundary 0
            w.Put(0, 5);
            w.Put(0, 5);
            w.Put(0, 4);
            w.Put(0, 1);  // no extension data
            w.Put(1, 2);  // constant-length scale factors: 2 bits over a base of 22
            w.Put(0, 2);
            w.Put(22, 5);
            while (w.bit < 100 * 8) {
                seed = seed * 1103515245u + 12345u;
                w.Put((seed >> 16) & 1, 1);
            }
            int used = 0;
            CHECK(Atrac9DecodeF32(h, dst + off, (int)((size_t)(sf + 1) * kAt9SuperframeBytes - off), frame, &used, 0) == 0);
            CHECK(used > 0 && (uint32_t)used < kAt9FrameBytes);
            memset(dst + off + used, 0, (size_t)(sf + 1) * kAt9SuperframeBytes - off - used); // bits past the frame
            off += (size_t)used;
            pcm->insert(pcm->end(), frame, frame + 256);
        }
    }
    Atrac9ReleaseHandle(h);
    float peak = 0.0f;
    for (float x : *pcm) peak = std::max(peak, std::fabs(x));
    CHECK(peak > 0.01f && peak < 1.0f);
}

void TestAtrac9() {
    std::vector<float> ref;
    BuildAtrac9(At<uint8_t>(kWave), 2, &ref);
    CHECK(ref.size() == 1024);
    Graph g = CreateGraph();
    const OrbisNgs2Handle v = g.voices[0];
    // A stream: one block per superframe, queued separately, then the end of data.
    {
        Params p;
        Setup(p, ORBIS_NGS2_WAVEFORM_TYPE_ATRAC9, 1, 48000, kAt9Config);
        Blocks(p, kWave, ORBIS_NGS2_WAVEFORM_BLOCKS_FLAG_QUEUE, {Block(0, kAt9SuperframeBytes, 0)});
        Event(p, ORBIS_NGS2_VOICE_EVENT_PLAY);
        CHECK(p.Send(v) == 0);
    }
    {
        Params p;
        Blocks(p, kWave + kAt9SuperframeBytes, ORBIS_NGS2_WAVEFORM_BLOCKS_FLAG_QUEUE, {Block(0, kAt9SuperframeBytes, 0)});
        Blocks(p, kWave, 0, {}, 1);
        CHECK(p.Send(v) == 0);
    }
    for (uint32_t r = 0; r < 4; ++r) {
        const float *out = Render(g);
        for (uint32_t f = 0; f < kGrain; ++f) {
            CHECK(out[f * 2] == ref[r * kGrain + f]);
            CHECK(out[f * 2 + 1] == 0.0f);
        }
        if (r < 3) CHECK(Flags(v) == kPlaying);
    }
    OrbisNgs2SamplerVoiceState s = State(v);
    CHECK(s.stateFlags == 0 && s.numDecodedSamples == 1024 && s.numDecodedBytes == 2 * kAt9SuperframeBytes);
    printf("[+] ATRAC9 stream: two queued blocks decode to libatrac9's samples, then the voice ends\n");

    // One block of both superframes, skipping into the first frame and stopping in the fourth.
    {
        Params p;
        Setup(p, ORBIS_NGS2_WAVEFORM_TYPE_ATRAC9, 1, 48000, kAt9Config);
        Blocks(p, kWave, ORBIS_NGS2_WAVEFORM_BLOCKS_FLAG_QUEUE, {Block(0, 2 * kAt9SuperframeBytes, 500, 300)});
        Blocks(p, kWave, 0, {}, 1);
        Event(p, ORBIS_NGS2_VOICE_EVENT_PLAY);
        CHECK(p.Send(v) == 0);
    }
    for (uint32_t r = 0; r < 2; ++r) {
        const float *out = Render(g);
        for (uint32_t f = 0; f < kGrain; ++f) {
            const uint32_t i = r * kGrain + f;
            CHECK(out[f * 2] == (i < 500 ? ref[300 + i] : 0.0f));
        }
    }
    s = State(v);
    CHECK(s.stateFlags == 0 && s.numDecodedSamples == 500);
    printf("[+] ATRAC9 block with skipped samples and a sample count\n");

    // A block that ends inside a superframe, then the next superframe in its own block: the
    // decoder starts that block on a superframe (else libatrac9 rejects all its frames). The
    // decoder's overlap carries on from frame 1, so only timing and sound are checked here.
    {
        Params p;
        Setup(p, ORBIS_NGS2_WAVEFORM_TYPE_ATRAC9, 1, 48000, kAt9Config);
        Blocks(p, kWave, ORBIS_NGS2_WAVEFORM_BLOCKS_FLAG_QUEUE,
               {Block(0, kAt9SuperframeBytes, 200), Block(kAt9SuperframeBytes, kAt9SuperframeBytes, 0)});
        Blocks(p, kWave, 0, {}, 1);
        Event(p, ORBIS_NGS2_VOICE_EVENT_PLAY);
        CHECK(p.Send(v) == 0);
    }
    float peak = 0.0f;
    for (uint32_t r = 0; r < 3; ++r) {
        const float *out = Render(g);
        for (uint32_t f = 0; f < kGrain; ++f) {
            const uint32_t i = r * kGrain + f;
            if (i < 200) CHECK(out[f * 2] == ref[i]);
            else if (i < 712) peak = std::max(peak, std::fabs(out[f * 2]));
            else CHECK(out[f * 2] == 0.0f);
        }
    }
    CHECK(peak > 0.01f);
    s = State(v);
    // Block A's decoding stops after its first frame; block B is decoded whole.
    CHECK(s.stateFlags == 0 && s.numDecodedSamples == 712);
    CHECK(s.numDecodedBytes > kAt9SuperframeBytes && s.numDecodedBytes < kAt9SuperframeBytes + kAt9FrameBytes);
    printf("[+] ATRAC9 block cut inside a superframe; the next block decodes from its superframe\n");

    // A config libatrac9 cannot take leaves the voice silent: its blocks are dropped and it ends.
    {
        Params p;
        Setup(p, ORBIS_NGS2_WAVEFORM_TYPE_ATRAC9, 1, 48000, 0x12345678);
        Blocks(p, kWave, ORBIS_NGS2_WAVEFORM_BLOCKS_FLAG_QUEUE, {Block(0, kAt9SuperframeBytes, 0)});
        Blocks(p, kWave, 0, {}, 1);
        Event(p, ORBIS_NGS2_VOICE_EVENT_PLAY);
        CHECK(p.Send(v) == 0);
    }
    const float *out = Render(g);
    for (uint32_t f = 0; f < kGrain * 2; ++f) CHECK(out[f] == 0.0f);
    CHECK(Flags(v) == 0);
    printf("[+] a rejected ATRAC9 config makes a silent voice that still ends\n");
    DestroyGraph(g);
}

} // namespace

int main() {
    g_ctx.mem_size = kMemSize;
    g_ctx.mem_base = (uint8_t *)calloc(1, kMemSize);
    CHECK(g_ctx.mem_base);

    TestCreateEntryPoints();
    TestPcm16StateAndTiming();
    TestPcm24MatrixSkipRepeat();
    TestPitch();
    TestPauseStopKill();
    TestRenderBuffers();
    TestAtrac9();

    printf("[ngs2-test] all passed\n");
    free(g_ctx.mem_base);
    return 0;
}
