// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_ngs2.h"
#include "../../core/recomp_runtime.h"
#include "../../core/recomp_context.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

constexpr uint32_t MinGrainSamples = 64;
constexpr uint32_t MaxGrainSamples = 1024;
constexpr uint32_t StateFlagInUse = 1;

enum class HandleType {
    System,
    Rack,
    Voice,
};

struct Voice {
    OrbisNgs2Handle handle = 0;
    OrbisNgs2Handle rack_handle = 0;
    uint32_t voice_index = 0;
    uint32_t state = StateFlagInUse;
};

struct Rack {
    OrbisNgs2Handle handle = 0;
    OrbisNgs2Handle system_handle = 0;
    uint32_t rack_id = 0;
    OrbisNgs2RackOption option{};
    uint64_t buffer_addr = 0;
    uint64_t buffer_size = 0;
    uint64_t user_data = 0;
    std::vector<Voice> voices;
};

struct System {
    OrbisNgs2Handle handle = 0;
    OrbisNgs2SystemOption option{};
    uint64_t buffer_addr = 0;
    uint64_t buffer_size = 0;
    uint64_t user_data = 0;
    uint32_t uid = 0;
    int64_t render_count = 0;
    std::vector<OrbisNgs2Handle> racks;
};

struct Registry {
    std::mutex mtx;
    OrbisNgs2Handle next_handle = 0x100;
    uint32_t next_uid = 1;
    std::unordered_map<OrbisNgs2Handle, System> systems;
    std::unordered_map<OrbisNgs2Handle, Rack> racks;
    std::unordered_map<OrbisNgs2Handle, Voice> voices;
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

    OrbisNgs2SystemOption norm = *opt;
    if (norm.maxGrainSamples == 0) norm.maxGrainSamples = 512;
    if (norm.numGrainSamples == 0) norm.numGrainSamples = 256;
    if (norm.sampleRate == 0) norm.sampleRate = 48000;

    size_t req_sz = SystemBufferSize(norm);

    // Prepare buffer info
    uint64_t allocated_guest_addr = 0;
    if (alloc->allocHandler) {
        // Place temporary OrbisNgs2ContextBufferInfo on guest stack
        uint64_t info_guest_addr = ctx->rsp - 128;
        auto* info = (OrbisNgs2ContextBufferInfo*)(ctx->mem_base + info_guest_addr);
        memset(info, 0, sizeof(*info));
        info->hostBufferSize = req_sz;
        info->userData = alloc->userData;

        int32_t alloc_res = CallGuestAlloc(ctx, alloc->allocHandler, info_guest_addr);
        if (alloc_res == 0 && info->hostBuffer) {
            allocated_guest_addr = info->hostBuffer;
        }
    }

    if (!allocated_guest_addr) {
        allocated_guest_addr = recomp_vm_alloc_named_aligned(ctx, req_sz, 4096, 0x3, 0, "Ngs2System");
    }

    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    OrbisNgs2Handle h = g_ngs2.next_handle++;
    System sys;
    sys.handle = h;
    sys.option = norm;
    sys.buffer_addr = allocated_guest_addr;
    sys.buffer_size = req_sz;
    sys.uid = g_ngs2.next_uid++;
    sys.render_count = 0;
    g_ngs2.systems[h] = sys;

    *out_handle = h;
    fprintf(stderr, "[ngs2] SystemCreateWithAllocator: handle=0x%llx, grain=%u, rate=%u, buf=0x%llx (%zu bytes)\n",
            (unsigned long long)h, norm.numGrainSamples, norm.sampleRate, (unsigned long long)allocated_guest_addr, req_sz);

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

    for (OrbisNgs2Handle rh : it->second.racks) {
        auto rit = g_ngs2.racks.find(rh);
        if (rit != g_ngs2.racks.end()) {
            for (const auto& v : rit->second.voices) {
                g_ngs2.voices.erase(v.handle);
            }
            g_ngs2.racks.erase(rit);
        }
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

    if (buffers_addr && num_buffers > 0 && num_buffers <= 16) {
        const auto* bufs = (const OrbisNgs2RenderBufferInfo*)(ctx->mem_base + buffers_addr);
        for (uint32_t i = 0; i < num_buffers; ++i) {
            if (bufs[i].buffer && bufs[i].bufferSize > 0) {
                void* dst = (void*)(ctx->mem_base + bufs[i].buffer);
                memset(dst, 0, bufs[i].bufferSize);
            }
        }
    }

    it->second.render_count++;
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

    OrbisNgs2RackOption norm{};
    if (opt_addr) {
        norm = *(const OrbisNgs2RackOption*)(ctx->mem_base + opt_addr);
    }
    if (norm.maxVoices == 0) norm.maxVoices = 64;
    if (norm.maxGrainSamples == 0) norm.maxGrainSamples = sys_it->second.option.maxGrainSamples;

    size_t req_sz = RackBufferSize(norm);

    uint64_t allocated_guest_addr = 0;
    if (alloc_addr) {
        const auto* alloc = (const OrbisNgs2BufferAllocator*)(ctx->mem_base + alloc_addr);
        if (alloc->allocHandler) {
            uint64_t info_guest_addr = ctx->rsp - 128;
            auto* info = (OrbisNgs2ContextBufferInfo*)(ctx->mem_base + info_guest_addr);
            memset(info, 0, sizeof(*info));
            info->hostBufferSize = req_sz;
            info->userData = alloc->userData;

            int32_t alloc_res = CallGuestAlloc(ctx, alloc->allocHandler, info_guest_addr);
            if (alloc_res == 0 && info->hostBuffer) {
                allocated_guest_addr = info->hostBuffer;
            }
        }
    }

    if (!allocated_guest_addr) {
        allocated_guest_addr = recomp_vm_alloc_named_aligned(ctx, req_sz, 4096, 0x3, 0, "Ngs2Rack");
    }

    OrbisNgs2Handle rack_h = g_ngs2.next_handle++;
    Rack rack;
    rack.handle = rack_h;
    rack.system_handle = sys_h;
    rack.rack_id = rack_id;
    rack.option = norm;
    rack.buffer_addr = allocated_guest_addr;
    rack.buffer_size = req_sz;

    rack.voices.resize(norm.maxVoices);
    for (uint32_t i = 0; i < norm.maxVoices; ++i) {
        OrbisNgs2Handle v_h = g_ngs2.next_handle++;
        Voice v;
        v.handle = v_h;
        v.rack_handle = rack_h;
        v.voice_index = i;
        v.state = StateFlagInUse;
        rack.voices[i] = v;
        g_ngs2.voices[v_h] = v;
    }

    g_ngs2.racks[rack_h] = rack;
    sys_it->second.racks.push_back(rack_h);

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

    for (const auto& v : it->second.voices) {
        g_ngs2.voices.erase(v.handle);
    }

    auto sys_it = g_ngs2.systems.find(it->second.system_handle);
    if (sys_it != g_ngs2.systems.end()) {
        auto& rv = sys_it->second.racks;
        rv.erase(std::remove(rv.begin(), rv.end(), h), rv.end());
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

void shim_sceNgs2VoiceControl(GuestContext *ctx) {
    // Process voice parameter controls
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNgs2VoiceGetState(GuestContext *ctx) {
    OrbisNgs2Handle v_h = (OrbisNgs2Handle)ctx->rdi;
    uint64_t state_addr = ctx->rsi;
    size_t state_sz = (size_t)ctx->rdx;

    if (!state_addr || state_sz < sizeof(OrbisNgs2VoiceState)) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS;
        SHIM_RETURN();
    }

    std::lock_guard<std::mutex> lock(g_ngs2.mtx);
    auto it = g_ngs2.voices.find(v_h);
    if (it == g_ngs2.voices.end()) {
        ctx->rax = (uint64_t)ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE;
        SHIM_RETURN();
    }

    auto* s = (OrbisNgs2VoiceState*)(ctx->mem_base + state_addr);
    memset(s, 0, state_sz);
    s->stateFlags = it->second.state;

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

    *(uint32_t*)(ctx->mem_base + flags_addr) = it->second.state;
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

void shim_sceNgs2SystemCreate(GuestContext *ctx) {
    shim_sceNgs2SystemCreateWithAllocator(ctx);
}

void shim_sceNgs2RackCreate(GuestContext *ctx) {
    shim_sceNgs2RackCreateWithAllocator(ctx);
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
    info->stateFlags = StateFlagInUse;
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
    info->stateFlags = StateFlagInUse;
    info->activeVoiceCount = (uint32_t)it->second.voices.size();
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

    if (out_rack_addr) *(OrbisNgs2Handle*)(ctx->mem_base + out_rack_addr) = it->second.rack_handle;
    if (out_voice_id_addr) *(uint32_t*)(ctx->mem_base + out_voice_id_addr) = it->second.voice_index;

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
