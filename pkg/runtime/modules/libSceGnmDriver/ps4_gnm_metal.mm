// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4 GNM command processor for PS4Recomp.
 *
 * Walks the PM4 streams submitted through libSceGnmDriver (graphics DCB/CCB and the compute
 * rings), maintains the Liverpool register file, executes CP DMA, writes fences/labels and raises
 * interrupts. Draws and dispatches are executed natively on Metal by ps4_gnm_draw.mm.
 *
 * Packet handling follows shadPS4 src/video_core/amdgpu/liverpool.cpp (ProcessGraphics,
 * ProcessCeUpdate, ProcessCompute) and pm4_cmds.h.
 * SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#import <Foundation/Foundation.h>

#include <functional>
#include <thread>
#include <deque>
#include "ps4_gnm_metal.h"
#include "ps4_gnm_draw.h"
#include "ps4_gnm_resources.h"
#include "ps4_gnm_ib.h"
#include "ps4_gnm_wait_queue.h"
#include "ps4_gnmdriver.h"
#include "ps4_videoout.h"
#include "gcn_recompiler.h"

extern "C" int32_t sceKernelIsNeoMode(void);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mutex>
#include <errno.h>
#include <pthread.h>
#include <atomic>
#include <time.h>
#include <unordered_map>
#include <vector>
#include <string>

// PM4 Liverpool Opcode constants
#define PM4_OP_NOP                         0x10
#define PM4_OP_SET_BASE                    0x11
#define PM4_OP_CLEAR_STATE                 0x12
#define PM4_OP_COND_EXEC                   0x22
#define PM4_OP_INDEX_BUFFER_SIZE           0x13
#define PM4_OP_DISPATCH_DIRECT             0x15
#define PM4_OP_DISPATCH_INDIRECT           0x16
#define PM4_OP_DRAW_INDIRECT               0x24
#define PM4_OP_DRAW_INDEX_INDIRECT         0x25
#define PM4_OP_INDEX_BASE                  0x26
#define PM4_OP_DRAW_INDEX_2                0x27
#define PM4_OP_CONTEXT_CONTROL             0x28
#define PM4_OP_INDEX_TYPE                  0x2A
#define PM4_OP_DRAW_INDIRECT_MULTI         0x2C
#define PM4_OP_DRAW_INDEX_AUTO             0x2D
#define PM4_OP_NUM_INSTANCES               0x2F
#define PM4_OP_INDIRECT_BUFFER_CONST       0x33
#define PM4_OP_DRAW_INDEX_OFFSET_2         0x35
#define PM4_OP_WRITE_DATA                  0x37
#define PM4_OP_DRAW_INDEX_INDIRECT_MULTI   0x38
#define PM4_OP_WAIT_REG_MEM                0x3C
#define PM4_OP_INDIRECT_BUFFER             0x3F
#define PM4_OP_EVENT_WRITE                 0x46
#define PM4_OP_EVENT_WRITE_EOP             0x47
#define PM4_OP_EVENT_WRITE_EOS             0x48
#define PM4_OP_RELEASE_MEM                 0x49
#define PM4_OP_DMA_DATA                    0x50
#define PM4_OP_ACQUIRE_MEM                 0x58
#define PM4_OP_SET_CONFIG_REG              0x68
#define PM4_OP_SET_CONTEXT_REG             0x69
#define PM4_OP_SET_SH_REG                  0x76
#define PM4_OP_SET_UCONFIG_REG             0x79
#define PM4_OP_LOAD_CONST_RAM              0x80
#define PM4_OP_WRITE_CONST_RAM             0x81
#define PM4_OP_DUMP_CONST_RAM              0x83
#define PM4_OP_INCREMENT_CE_COUNTER        0x84
#define PM4_OP_INCREMENT_DE_COUNTER        0x85
#define PM4_OP_WAIT_ON_CE_COUNTER          0x86
#define PM4_OP_WAIT_ON_DE_COUNTER_DIFF     0x88

// Compute program registers (COMPUTE_DISPATCH_INITIATOR .. COMPUTE_USER_DATA_15 and beyond):
// kept per queue, like shadPS4's per-queue cs_state.
#define CS_REG_BLOCK_BASE 0x2E00u
#define CS_REG_BLOCK_SIZE 0x80u

namespace {

struct GnmMetalState {
    std::recursive_mutex mutex;
};

static GnmMetalState g_state;

} // namespace

// ---------------------------------------------------------------------------
// Fences, labels and interrupts (EVENT_WRITE_EOP / RELEASE_MEM / EVENT_WRITE_EOS /
// WRITE_DATA / WAIT_REG_MEM). Packet layouts follow AMD GCN (Sea Islands) PM4 as used by
// Liverpool; cf. shadPS4 src/video_core/amdgpu/pm4_cmds.h.
//
// The command processor runs synchronously on the submitting thread. Draws/dispatches are
// encoded into Metal command buffers; before a label is written or an interrupt raised, all
// GPU work encoded so far is committed and waited for (SyncGpu), so "end of pipe" really
// means the GPU results are visible to the CPU.
// ---------------------------------------------------------------------------

namespace {

enum : uint32_t {
    kDataSelNone = 0,
    kDataSel32 = 1,
    kDataSel64 = 2,
    kDataSelGpuClock64 = 3,
    kDataSelPerfCounter = 4,
    kDataSelGdsMemStore = 5,
};

static std::atomic<bool> s_logged_first_eop{false};
static std::atomic<bool> s_logged_first_irq{false};
static std::atomic<bool> s_logged_first_release_mem{false};
static std::atomic<bool> s_logged_first_compute_irq{false};
static std::atomic<bool> s_logged_wait_reg_mem{false};
static std::atomic<bool> s_logged_bad_data_sel{false};
static std::atomic<bool> s_logged_bad_addr{false};
static std::atomic<bool> s_logged_ib_depth{false};

static inline bool LogOnce(std::atomic<bool> &flag) {
    bool expected = false;
    return flag.compare_exchange_strong(expected, true);
}

// Guest GPU addresses are guest virtual addresses (offsets from mem_base); same convention
// as WRITE_DATA and WrapGuestMemory.
static inline void *GuestMemPtr(GuestContext *ctx, uint64_t addr, size_t len) {
    if (!ctx || !ctx->mem_base || addr == 0) return nullptr;
    if (addr >= ctx->mem_size || len > ctx->mem_size - addr) {
        if (LogOnce(s_logged_bad_addr)) {
            fprintf(stderr, "[ps4-gnm] fence/label address 0x%llx out of guest range; ignoring (logged once)\n",
                    (unsigned long long)addr);
        }
        return nullptr;
    }
    return ctx->mem_base + addr;
}

static inline uint64_t GpuClock64() {
    // shadPS4 reports nanoseconds for GpuClock64 / PerfCounter.
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

// Performs the memory write selected by data_sel. Returns true if a write happened.
static bool WriteFenceData(GuestContext *ctx, uint64_t addr, uint32_t data_sel,
                           uint32_t data_lo, uint32_t data_hi) {
    switch (data_sel) {
    case kDataSelNone:
        return false;
    case kDataSel32: {
        uint32_t *dst = (uint32_t *)GuestMemPtr(ctx, addr & ~3ULL, 4);
        if (!dst) return false;
        __atomic_store_n(dst, data_lo, __ATOMIC_SEQ_CST);
        return true;
    }
    case kDataSel64:
    case kDataSelGpuClock64:
    case kDataSelPerfCounter: {
        uint64_t value = (data_sel == kDataSel64) ? ((uint64_t)data_lo | ((uint64_t)data_hi << 32)) : GpuClock64();
        // 64-bit fences must be qword aligned on HW; the low bits are ignored there.
        uint64_t *dst = (uint64_t *)GuestMemPtr(ctx, addr & ~7ULL, 8);
        if (!dst) return false;
        __atomic_store_n(dst, value, __ATOMIC_SEQ_CST);
        return true;
    }
    case kDataSelGdsMemStore:
    default:
        if (LogOnce(s_logged_bad_data_sel)) {
            fprintf(stderr, "[ps4-gnm] fence data_sel=%u not supported (GDS store?); skipping write (logged once)\n",
                    data_sel);
        }
        return false;
    }
}

// Make all encoded GPU work complete before the CPU can observe a fence.
static std::atomic<uint64_t> g_sync_counts[8];
static const char *const kSyncNames[8] = {"eop", "release_mem", "eos", "write_data", "dma", "ce_dump", "cond_exec", "other"};
static void SyncGpu(int reason = 7) {
    if (ps4draw::HasPendingWork()) {
        g_sync_counts[reason & 7]++;
        ps4draw::Flush(true);
    }
}
// Host time spent processing PM4 streams (command processor + Metal encoding), for perf stats.
static std::atomic<uint64_t> g_cp_ns{0};
extern "C" uint64_t ps4_gnm_take_cp_ns(void) { return g_cp_ns.exchange(0); }
struct CpTimer {
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    ~CpTimer() { g_cp_ns.fetch_add(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0, std::memory_order_relaxed); }
};
extern "C" void ps4_gnm_print_sync_stats(void) {
    fprintf(stderr, "[ps4-gnm] GPU syncs by reason:");
    for (int i = 0; i < 8; i++) fprintf(stderr, " %s=%llu", kSyncNames[i], (unsigned long long)g_sync_counts[i].exchange(0));
    fprintf(stderr, "\n");
}

static void RaiseInterrupt(uint32_t irq_id, const char *src) {
    std::atomic<bool> &flag = (irq_id == PS4_GNM_EVENT_GFX_EOP) ? s_logged_first_irq : s_logged_first_compute_irq;
    if (LogOnce(flag)) {
        fprintf(stderr, "[ps4-gnm] first %s interrupt delivered (irq/event id 0x%x)\n", src, irq_id);
    }
    ps4_gnm_signal_interrupt(irq_id);
}

// EVENT_WRITE_EOP (0x47), 5 payload dwords:
//   p[0] event_cntl : event_type[5:0], event_index[11:8]
//   p[1] addr_lo
//   p[2] data_cntl  : addr_hi[15:0], int_sel[25:24], data_sel[31:29]
//   p[3] data_lo
//   p[4] data_hi
// Fence values that encoded but not yet completed GPU work will write (EOP / RELEASE_MEM /
// EOS). All GNM queues execute in order on one Metal queue, so for a WAIT_REG_MEM of another
// queue such a value is as good as written: work encoded after the wait runs after it.
static std::mutex g_pending_mu;
static std::unordered_map<uint64_t, uint32_t> g_pending_labels; // dword address -> value

static void PendingLabel(uint64_t addr, uint32_t data_sel, uint32_t lo, uint32_t hi, bool add) {
    if (data_sel != kDataSel32 && data_sel != kDataSel64) return;
    std::lock_guard<std::mutex> lk(g_pending_mu);
    auto set = [&](uint64_t a, uint32_t v) {
        if (add) {
            g_pending_labels[a] = v;
        } else {
            auto it = g_pending_labels.find(a);
            if (it != g_pending_labels.end() && it->second == v) g_pending_labels.erase(it);
        }
    };
    if (data_sel == kDataSel32) {
        set(addr & ~3ULL, lo);
    } else {
        set(addr & ~7ULL, lo);
        set((addr & ~7ULL) + 4, hi);
    }
}

static bool PendingLabelValue(uint64_t addr, uint32_t *out) {
    std::lock_guard<std::mutex> lk(g_pending_mu);
    auto it = g_pending_labels.find(addr & ~3ULL);
    if (it == g_pending_labels.end()) return false;
    *out = it->second;
    return true;
}

static void HandleEventWriteEop(GuestContext *ctx, const uint32_t *p, uint32_t payload_dw, uint32_t irq_id) {
    if (payload_dw < 4) return;
    const uint64_t addr = (uint64_t)p[1] | ((uint64_t)(p[2] & 0xFFFFu) << 32);
    const uint32_t int_sel = (p[2] >> 24) & 0x3u;
    const uint32_t data_sel = (p[2] >> 29) & 0x7u;
    const uint32_t data_lo = p[3];
    const uint32_t data_hi = payload_dw >= 5 ? p[4] : 0;

    // End of pipe: performed when all GPU work submitted before it has completed.
    PendingLabel(addr, data_sel, data_lo, data_hi, true);
    ps4draw::AfterGpu([=]() {
        bool wrote = WriteFenceData(ctx, addr, data_sel, data_lo, data_hi);
        PendingLabel(addr, data_sel, data_lo, data_hi, false);
        if (LogOnce(s_logged_first_eop)) {
            fprintf(stderr, "[ps4-gnm] first EVENT_WRITE_EOP: addr=0x%llx data_sel=%u int_sel=%u data=0x%08x%08x wrote=%d\n",
                    (unsigned long long)addr, data_sel, int_sel, data_hi, data_lo, wrote ? 1 : 0);
        }
        if (int_sel != 0) {
            RaiseInterrupt(irq_id, "EOP");
        }
    });
}

// RELEASE_MEM (0x49), 6 payload dwords:
//   p[0] event_cntl : event_type[5:0], event_index[11:8], cache action bits
//   p[1] dst_sel[17:16], int_sel[26:24], data_sel[31:29]
//   p[2] addr_lo
//   p[3] addr_hi
//   p[4] data_lo (or gds_index[15:0] / num_dw[31:16] for data_sel=5)
//   p[5] data_hi
static void HandleReleaseMem(GuestContext *ctx, const uint32_t *p, uint32_t payload_dw, uint32_t irq_id) {
    if (payload_dw < 5) return;
    const uint32_t int_sel = (p[1] >> 24) & 0x7u;
    const uint32_t data_sel = (p[1] >> 29) & 0x7u;
    const uint64_t addr = (uint64_t)p[2] | ((uint64_t)p[3] << 32);
    const uint32_t data_lo = p[4];
    const uint32_t data_hi = payload_dw >= 6 ? p[5] : 0;

    PendingLabel(addr, data_sel, data_lo, data_hi, true);
    ps4draw::AfterGpu([=]() {
        bool wrote = WriteFenceData(ctx, addr, data_sel, data_lo, data_hi);
        PendingLabel(addr, data_sel, data_lo, data_hi, false);
        if (LogOnce(s_logged_first_release_mem)) {
            fprintf(stderr, "[ps4-gnm] first RELEASE_MEM: addr=0x%llx data_sel=%u int_sel=%u data=0x%08x%08x wrote=%d irq=0x%x\n",
                    (unsigned long long)addr, data_sel, int_sel, data_hi, data_lo, wrote ? 1 : 0, irq_id);
        }
        if (int_sel != 0) {
            RaiseInterrupt(irq_id, irq_id == PS4_GNM_EVENT_GFX_EOP ? "EOP(RELEASE_MEM)" : "compute RELEASE_MEM");
        }
    });
}

// EVENT_WRITE_EOS (0x48), 4 payload dwords:
//   p[0] event_cntl
//   p[1] addr_lo
//   p[2] addr_hi[15:0], command[31:29] (1 = GDS store, 2 = signal fence)
//   p[3] data (or gds_index[15:0] / size[31:16])
static void HandleEventWriteEos(GuestContext *ctx, const uint32_t *p, uint32_t payload_dw) {
    if (payload_dw < 4) return;
    const uint64_t addr = (uint64_t)p[1] | ((uint64_t)(p[2] & 0xFFFFu) << 32);
    const uint32_t command = (p[2] >> 29) & 0x7u;
    if (command == 2) {
        const uint32_t v = p[3];
        PendingLabel(addr, kDataSel32, v, 0, true);
        ps4draw::AfterGpu([=]() {
            WriteFenceData(ctx, addr, kDataSel32, v, 0);
            PendingLabel(addr, kDataSel32, v, 0, false);
        });
    }
}

// WRITE_DATA (0x37):
//   p[0] control: dst_sel[11:8] (0 reg, 1 mem sync, 2 TC/L2, 3 GDS, 5 mem async),
//                 wr_one_addr[16], wr_confirm[20], engine_sel[31:30]
//   p[1] dst_addr_lo, p[2] dst_addr_hi, p[3..] data
static void HandleWriteData(GuestContext *ctx, const uint32_t *p, uint32_t payload_dw) {
    if (payload_dw < 3) return;
    const uint32_t dst_sel = (p[0] >> 8) & 0xFu;
    const bool wr_one_addr = ((p[0] >> 16) & 1u) != 0;
    if (dst_sel != 1 && dst_sel != 2 && dst_sel != 5) {
        return; // register / GDS destinations are not guest memory
    }
    const uint64_t dst_addr = (uint64_t)p[1] | ((uint64_t)p[2] << 32);
    const uint32_t ndw = payload_dw - 3;
    if (ndw == 0) return;
    uint32_t *dst = (uint32_t *)GuestMemPtr(ctx, dst_addr & ~3ULL, wr_one_addr ? 4 : (size_t)ndw * 4);
    if (!dst) return;
    SyncGpu(3); // previously encoded GPU work may still read the old contents
    ps4draw::NotifyGuestWrite(dst_addr & ~3ULL, wr_one_addr ? 4 : (uint64_t)ndw * 4, false, 0);
    for (uint32_t w = 0; w < ndw; w++) {
        if (wr_one_addr) {
            __atomic_store_n(dst, p[3 + w], __ATOMIC_SEQ_CST);
        } else {
            __atomic_store_n(dst + w, p[3 + w], __ATOMIC_SEQ_CST);
        }
    }
}

// WAIT_REG_MEM (0x3C), 6 payload dwords:
//   p[0] function[2:0], mem_space[4], engine[8]
//   p[1] poll_addr_lo / reg, p[2] poll_addr_hi, p[3] reference, p[4] mask, p[5] poll_interval
// Returns true when the stream may continue. The graphics queue (can_suspend) is suspended
// while the value is neither in memory nor pending from encoded GPU work: it is typically
// produced by an async compute job the game submits right after this command buffer. Other
// callers never block: evaluate, log the first unsatisfied wait, and continue.
static bool WaitSatisfied(uint32_t function, uint32_t v, uint32_t ref) {
    switch (function) {
    case 1: return v < ref;
    case 2: return v <= ref;
    case 3: return v == ref;
    case 4: return v != ref;
    case 5: return v >= ref;
    case 6: return v > ref;
    default: return true;
    }
}

static bool WaitRegMemReady(GuestContext *ctx, const uint32_t *p, uint32_t payload_dw) {
    if (payload_dw < 5) return true;
    const uint32_t function = p[0] & 0x7u;
    const bool mem_space = ((p[0] >> 4) & 1u) != 0;
    if (!mem_space) return true;
    const uint64_t addr = ((uint64_t)p[1] & ~3ULL) | ((uint64_t)(p[2] & 0xFFFFu) << 32);
    const uint32_t *src = (const uint32_t *)GuestMemPtr(ctx, addr, 4);
    if (!src) return true;
    if (WaitSatisfied(function, __atomic_load_n(src, __ATOMIC_SEQ_CST) & p[4], p[3])) return true;
    uint32_t pv;
    return PendingLabelValue(addr, &pv) && WaitSatisfied(function, pv & p[4], p[3]);
}

static bool HandleWaitRegMem(GuestContext *ctx, const uint32_t *p, uint32_t payload_dw, bool can_suspend) {
    if (payload_dw < 5) return true;
    const uint32_t function = p[0] & 0x7u;
    const bool mem_space = ((p[0] >> 4) & 1u) != 0;
    if (!mem_space) return true;
    const uint64_t addr = ((uint64_t)p[1] & ~3ULL) | ((uint64_t)(p[2] & 0xFFFFu) << 32);
    const uint32_t ref = p[3];
    const uint32_t mask = p[4];
    const uint32_t *src = (const uint32_t *)GuestMemPtr(ctx, addr, 4);
    if (!src) return true;
    if (WaitRegMemReady(ctx, p, payload_dw)) return true;
    static int suspend = -1;
    if (suspend < 0) {
        const char *e = getenv("PS4_GNM_SUSPEND_WAIT");
        suspend = e ? atoi(e) != 0 : 1;
    }
    if (can_suspend && suspend) return false;
    uint32_t v = __atomic_load_n(src, __ATOMIC_SEQ_CST) & mask;
    bool ok = false;
    static int strict = -1;
    if (strict < 0) strict = getenv("PS4_GNM_STRICT_WAIT") != NULL;
    if (ps4draw::HasPendingWork() && !strict) {
        // The value is most likely produced by GPU work submitted earlier (any queue: all GNM
        // queues execute in order on one Metal queue), so work encoded after this packet is
        // ordered behind it anyway. No CPU-side wait.
        return true;
    }
    if (ps4draw::HasPendingWork()) {
        // the value may be produced by GPU work encoded earlier: drain and re-check
        ps4draw::Flush(true);
        v = __atomic_load_n(src, __ATOMIC_SEQ_CST) & mask;
        ok = WaitSatisfied(function, v, ref);
    }
    if (!ok && LogOnce(s_logged_wait_reg_mem)) {
        fprintf(stderr, "[ps4-gnm] WAIT_REG_MEM not satisfied at submit (addr=0x%llx func=%u ref=0x%x mask=0x%x val=0x%x); "
                        "continuing without waiting (logged once)\n",
                (unsigned long long)addr, function, ref, mask, v);
    }
    return true;
}

static inline bool DecodeIndirectBuffer(GuestContext *ctx, const uint32_t *p, uint32_t payload_dw,
                                        const uint32_t **out_words, uint32_t *out_dw) {
    if (payload_dw < 3) return false;
    const uint64_t ib_addr = ((uint64_t)p[0] & ~3ULL) | ((uint64_t)(p[1] & 0xFFFFu) << 32);
    const uint32_t ib_size_dw = p[2] & 0xFFFFFu;
    if (ib_size_dw == 0) return false;
    const uint32_t *w = (const uint32_t *)GuestMemPtr(ctx, ib_addr, (size_t)ib_size_dw * 4);
    if (!w) return false;
    *out_words = w;
    *out_dw = ib_size_dw;
    return true;
}

static const int kMaxIbDepth = ps4gnm_ib::kMaxDepth;
// Concurrent guest dispatches: end-of-pipe signals (EOP/EOS/RELEASE_MEM) and CP writes do not
// order the dispatches after them, as on GCN; waits, cache/partial flushes and CP DMA do. The
// guest pairs each dependency with a wait, e.g. skinning dispatches: RELEASE_MEM ... WAIT_REG_MEM.
// PS4_GNM_STRICT_BARRIERS=1 puts a barrier at every sync packet.
static const bool g_min_barriers = getenv("PS4_GNM_STRICT_BARRIERS") == nullptr;

// CP DMA (IT_DMA_DATA): memory/GDS copies and fills, executed synchronously
// on the CPU (guest GPU memory is host memory) after draining encoded GPU work.
// Layout as in shadPS4 src/video_core/amdgpu/pm4_cmds.h (PM4DmaData).
static void HandleDmaData(GuestContext *ctx, const uint32_t *p, uint32_t payload_dw) {
    if (payload_dw < 6 || !ctx || !ctx->mem_base) return;
    uint32_t ctl = p[0];
    uint32_t dst_sel = (ctl >> 20) & 3;
    uint32_t src_sel = (ctl >> 29) & 3;
    uint64_t src = (uint64_t)p[1] | ((uint64_t)p[2] << 32);
    uint64_t dst = (uint64_t)p[3] | ((uint64_t)p[4] << 32);
    uint32_t cmd = p[5];
    uint32_t n = cmd & 0x1FFFFF;
    bool sas = (cmd >> 26) & 1, das = (cmd >> 27) & 1;
    if (n == 0 || p[3] == 0x3022C || das || sas) return;
    uint8_t *gds = ps4draw::Gds();
    bool dst_gds = dst_sel == 1;
    bool src_gds = src_sel == 1;
    uint8_t *d;
    if (dst_gds) {
        if ((uint64_t)p[3] + n > 0x10000) return;
        d = gds + p[3];
    } else {
        if (dst + n > ctx->mem_size || dst == 0) return;
        d = ctx->mem_base + dst;
    }
    // With GPU work pending, run the copy on the GPU timeline behind it instead of draining
    // (games read GDS append counters back this way every frame).
    if (ps4draw::HasPendingWork() &&
        ps4draw::GpuDma(src_gds, src_sel == 2, src_sel == 2 ? 0 : (src_gds ? p[1] : src), p[1], dst_gds,
                        dst_gds ? p[3] : dst, n))
        return;
    SyncGpu(4);
    if (src_sel == 2) {
        uint32_t v = p[1];
        for (uint32_t i = 0; i + 4 <= n; i += 4) memcpy(d + i, &v, 4);
        if (!dst_gds) ps4draw::NotifyGuestWrite(dst, n, true, v, true);
        return;
    }
    const uint8_t *sp;
    if (src_gds) {
        if ((uint64_t)p[1] + n > 0x10000) return;
        sp = gds + p[1];
    } else {
        if (src + n > ctx->mem_size || src == 0) return;
        sp = ctx->mem_base + src;
    }
    memmove(d, sp, n);
    if (!dst_gds) ps4draw::NotifyGuestWrite(dst, n, false, 0, true);
}

// Full Liverpool register file, indexed by dword register offset (config
// 0x2000+, SH 0x2C00+, context 0xA000+, uconfig 0xC000+). Maintained for every
// submit so draws can be decoded regardless of the Metal path.
static uint32_t g_liverpool_regs[0x10000];
static uint32_t g_cb_extent[8];
static uint32_t g_db_extent;
static int g_gnm_stats = -1;
static std::atomic<uint64_t> g_gnm_hist[256];

// Per compute queue COMPUTE_* register blocks (shadPS4 mapped_queues[].cs_state).
static const uint32_t kMaxComputeQueues = 64;
static uint32_t g_cs_regs[kMaxComputeQueues][CS_REG_BLOCK_SIZE];

static void ApplySetReg(uint32_t base, const uint32_t *payload, uint32_t payload_dw) {
    if (payload_dw < 1) return;
    uint32_t reg = base + (payload[0] & 0xFFFF);
    for (uint32_t r = 1; r < payload_dw && reg + r - 1 < 0x10000; r++) {
        g_liverpool_regs[reg + r - 1] = payload[r];
    }
}

static bool ReadGuestForDump(void *c, uint64_t addr, void *dst, uint64_t size) {
    GuestContext *ctx = (GuestContext *)c;
    if (!ctx || addr == 0 || addr >= ctx->mem_size || size > ctx->mem_size - addr) return false;
    memcpy(dst, ctx->mem_base + addr, size);
    return true;
}

// PS4_DUMP_SHADERS=<dir>: write each distinct VS/PS/CS program seen at a draw or
// dispatch as <stage>_<addr>.bin plus <stage>_<addr>.txt with the non-zero SH,
// context and uconfig registers at that moment. The program length comes from the
// OrbShdr footer (falls back to the first s_endpgm).
static void DumpShaderAtDraw(GuestContext *ctx, const char *stage, uint32_t pgm_lo_reg, const char *draw) {
    static const char *dir = (const char *)-1;
    if (dir == (const char *)-1) dir = getenv("PS4_DUMP_SHADERS");
    if (!dir || !ctx || !ctx->mem_base) return;
    uint64_t addr = ((uint64_t)g_liverpool_regs[pgm_lo_reg] | ((uint64_t)(g_liverpool_regs[pgm_lo_reg + 1] & 0xFF) << 32)) << 8;
    if (addr == 0 || addr >= ctx->mem_size) return;
    static std::mutex& dump_mutex = *new std::mutex;
    static std::unordered_map<uint64_t, int>& seen = *new std::unordered_map<uint64_t, int>;
    std::lock_guard<std::mutex> lk(dump_mutex);
    if (seen.count(addr) || seen.size() > 4096) return;
    seen[addr] = 1;
    const uint32_t *code = (const uint32_t *)(ctx->mem_base + addr);
    size_t max_dw = (ctx->mem_size - addr) / 4;
    if (max_dw > 65536) max_dw = 65536;
    size_t n = 0;
    uint32_t size_bytes = 0;
    uint64_t hash = 0;
    if (gcn_find_binary_info(ReadGuestForDump, ctx, addr, &size_bytes, &hash) && size_bytes &&
        size_bytes / 4 <= max_dw) {
        n = size_bytes / 4;
    } else {
        while (n < max_dw) {
            if (code[n] == 0xBF810000u) { n++; break; } // s_endpgm
            n++;
        }
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s_%llx.bin", dir, stage, (unsigned long long)addr);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(code, 4, n, f);
    fclose(f);
    snprintf(path, sizeof(path), "%s/%s_%llx.txt", dir, stage, (unsigned long long)addr);
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# %s program at 0x%llx, %zu dwords, first seen at %s\n", stage, (unsigned long long)addr, n, draw);
    for (uint32_t r = 0x2C00; r < 0x10000; r++) {
        if (g_liverpool_regs[r] != 0 && (r < 0x3000 || r >= 0xA000)) fprintf(f, "%04x %08x\n", r, g_liverpool_regs[r]);
    }
    fclose(f);
}

static void DumpShadersForDraw(GuestContext *ctx, const char *draw, bool compute) {
    if (compute) {
        DumpShaderAtDraw(ctx, "cs", 0x2E0C, draw);
    } else {
        DumpShaderAtDraw(ctx, "vs", 0x2C48, draw);
        DumpShaderAtDraw(ctx, "ps", 0x2C08, draw);
        DumpShaderAtDraw(ctx, "es", 0x2CC8, draw);
        DumpShaderAtDraw(ctx, "gs", 0x2C88, draw);
        DumpShaderAtDraw(ctx, "ls", 0x2D48, draw);
        DumpShaderAtDraw(ctx, "hs", 0x2D08, draw);
    }
}

// ---------------------------------------------------------------------------
// Constant engine (CCB): constant RAM writes/dumps, interleaved with the DE through the
// CE/DE counters (shadPS4 Liverpool::ProcessCeUpdate + WaitOnCeCounter/IncrementDeCounter).
// ---------------------------------------------------------------------------
static uint8_t g_const_ram[0x10000];

struct CeState {
    using Frame = ps4gnm_ib::Frame;
    std::vector<Frame> stack;
    uint32_t ce_count = 0, de_count = 0;
    bool Done() const { return stack.empty(); }
};

static std::atomic<bool> s_logged_ce{false};

// Runs the CE until it blocks on WAIT_ON_DE_COUNTER_DIFF (unless `force`) or finishes.
// Returns true if any packet was consumed.
static bool RunCe(GuestContext *ctx, CeState &ce, bool force) {
    bool progress = false;
    while (!ce.stack.empty()) {
        CeState::Frame &f = ce.stack.back();
        if (f.pc >= f.dw) {
            ce.stack.pop_back();
            continue;
        }
        const uint32_t header = f.words[f.pc];
        if ((header >> 30) != 3) {
            f.pc++;
            continue;
        }
        const uint32_t opcode = (header >> 8) & 0xFF;
        const uint32_t payload_dw = ((header >> 16) & 0x3FFF) + 1;
        if (f.pc + 1 + payload_dw > f.dw) {
            ce.stack.pop_back();
            continue;
        }
        const uint32_t *p = &f.words[f.pc + 1];
        if (LogOnce(s_logged_ce)) fprintf(stderr, "[ps4-gnm] constant engine (CCB) in use (first opcode 0x%02x)\n", opcode);
        switch (opcode) {
        case PM4_OP_WRITE_CONST_RAM: {
            const uint32_t off = p[0] & 0xFFFF;
            const uint32_t n = (payload_dw - 1) * 4;
            if ((uint64_t)off + n <= sizeof(g_const_ram)) memcpy(g_const_ram + off, p + 1, n);
            break;
        }
        case PM4_OP_DUMP_CONST_RAM: {
            if (payload_dw < 4) break;
            const uint32_t off = p[0] & 0xFFFF;
            const uint32_t n = (p[1] & 0x7FFF) * 4;
            const uint64_t addr = (uint64_t)p[2] | ((uint64_t)p[3] << 32);
            void *dst = GuestMemPtr(ctx, addr, n);
            if (dst && (uint64_t)off + n <= sizeof(g_const_ram)) {
                // draws encoded earlier may still read the memory being overwritten
                SyncGpu(5);
                memcpy(dst, g_const_ram + off, n);
            }
            break;
        }
        case PM4_OP_LOAD_CONST_RAM: {
            if (payload_dw < 4) break;
            const uint64_t addr = (uint64_t)p[0] | ((uint64_t)p[1] << 32);
            const uint32_t n = (p[2] & 0x7FFF) * 4;
            const uint32_t off = p[3] & 0xFFFF;
            const void *src = GuestMemPtr(ctx, addr, n);
            if (src && (uint64_t)off + n <= sizeof(g_const_ram)) memcpy(g_const_ram + off, src, n);
            break;
        }
        case PM4_OP_INCREMENT_CE_COUNTER:
            ce.ce_count++;
            break;
        case PM4_OP_WAIT_ON_DE_COUNTER_DIFF:
            if (!force && (ce.de_count - ce.ce_count) >= p[0]) return progress;
            break;
        case PM4_OP_INDIRECT_BUFFER_CONST:
        case PM4_OP_INDIRECT_BUFFER: {
            const uint32_t *ib = nullptr;
            uint32_t ib_dw = 0;
            f.pc += 1 + payload_dw;
            progress = true;
            if (DecodeIndirectBuffer(ctx, p, payload_dw, &ib, &ib_dw))
                ps4gnm_ib::Enter(ce.stack, ib, ib_dw, ps4gnm_ib::IsChained(p, payload_dw));
            continue;
        }
        case PM4_OP_WRITE_DATA:
            HandleWriteData(ctx, p, payload_dw);
            break;
        default:
            break;
        }
        f.pc += 1 + payload_dw;
        progress = true;
    }
    return progress;
}

// ---------------------------------------------------------------------------
// Draw state that lives outside the register file (index buffer, instances, indirect base)
// ---------------------------------------------------------------------------
struct DrawState {
    uint64_t index_base = 0;
    uint32_t index_type = 0;
    uint32_t index_buffer_size = 0;
    uint32_t num_instances = 1;
    uint64_t indirect_base = 0;
};
static DrawState g_draw;

static std::atomic<bool> s_logged_indirect_draw{false};

static void DoDraw(GuestContext *ctx, const char *name, bool indexed, uint32_t index_offset, uint32_t count,
                   uint32_t max_size) {
    DumpShadersForDraw(ctx, name, false);
    if (!ps4draw::Enabled() && !ps4draw::Init(ctx)) return;
    ps4draw::DrawArgs a;
    a.indexed = indexed;
    a.index_base = g_draw.index_base;
    a.index_type = g_draw.index_type;
    a.index_offset = index_offset;
    a.count = count;
    a.max_index = max_size;
    a.num_instances = g_draw.num_instances ? g_draw.num_instances : 1;
    a.name = name;
    (void)ps4draw::Draw(ctx, g_liverpool_regs, a);
}

// Executes one DRAW_INDIRECT / DRAW_INDEX_INDIRECT argument record at guest address `args`:
//   indexed:     {indexCount, instanceCount, startIndex, baseVertex, startInstance}
//   non-indexed: {vertexCount, instanceCount, startVertex, startInstance}
// The CP writes baseVertex / startInstance into the user SGPRs named by bvl / sil (register
// offsets relative to 0x2C00, 0 = none). Preferably the GPU reads the arguments itself (no
// CPU/GPU round trip); otherwise pending GPU work is drained once (*synced) because the
// arguments may be GPU-produced (culling compute), and they are read on the CPU.
static void DrawIndirectRecord(GuestContext *ctx, bool indexed, uint64_t args, uint32_t bvl, uint32_t sil,
                               bool *synced) {
    const char *name = indexed ? "DRAW_INDEX_INDIRECT" : "DRAW_INDIRECT";
    const auto ud_slot = [](uint32_t reg) {
        // SPI_SHADER_USER_DATA_VS_0..15 (0x2C4C..0x2C5B, relative to 0x2C00)
        return reg == 0 ? -1 : (reg >= 0x4C && reg < 0x5C) ? (int)(reg - 0x4C) : -2;
    };
    const int bv_ud = ud_slot(bvl), si_ud = ud_slot(sil);
    if (bv_ud != -2 && si_ud != -2 && (ps4draw::Enabled() || ps4draw::Init(ctx))) {
        DumpShadersForDraw(ctx, name, false);
        ps4draw::DrawArgs a;
        a.indexed = indexed;
        a.index_base = g_draw.index_base;
        a.index_type = g_draw.index_type;
        a.max_index = indexed ? g_draw.index_buffer_size : 0;
        a.name = name;
        a.indirect_addr = args;
        a.base_vertex_ud = bv_ud;
        a.start_instance_ud = si_ud;
        if (ps4draw::Draw(ctx, g_liverpool_regs, a)) return;
    }
    if (!*synced) {
        SyncGpu(7);
        *synced = true;
    }
    const uint32_t *ia = (const uint32_t *)GuestMemPtr(ctx, args, 20);
    if (!ia || !ia[0] || !ia[1]) return;
    const uint32_t base_vertex = indexed ? ia[3] : ia[2];
    const uint32_t start_inst = indexed ? ia[4] : ia[3];
    if (bvl && bvl < 0x400) g_liverpool_regs[0x2C00 + bvl] = base_vertex;
    if (sil && sil < 0x400) g_liverpool_regs[0x2C00 + sil] = start_inst;
    const uint32_t saved_inst = g_draw.num_instances;
    g_draw.num_instances = ia[1];
    if (indexed)
        DoDraw(ctx, name, true, ia[2], ia[0], g_draw.index_buffer_size);
    else
        DoDraw(ctx, name, false, 0, ia[0], 0);
    g_draw.num_instances = saved_inst;
}

// A graphics command buffer in progress: the DCB with its INDIRECT_BUFFER nesting and the CE
// state. It is kept when a WAIT_REG_MEM suspends the queue and continued later.
struct GfxStream {
    using Frame = ps4gnm_ib::Frame;
    std::vector<Frame> frames;
    CeState ce;
};

// Runs the stream until it finishes (true) or a WAIT_REG_MEM suspends it (false).
static bool ProcessGfxPackets(GuestContext *ctx, GfxStream &st) {
    CeState *ce = &st.ce;
    uint32_t hist_pc[8] = {}, hist_n = 0;
    while (!st.frames.empty()) {
        GfxStream::Frame &f = st.frames.back();
        const uint32_t *dcb_words = f.words;
        const uint32_t dcb_dw = f.dw;
        const int depth = (int)st.frames.size() - 1;
        uint32_t pc = f.pc;
        if (pc >= dcb_dw) {
            st.frames.pop_back();
            continue;
        }
        const uint32_t *push_ib = nullptr;
        uint32_t push_ib_dw = 0;
        bool push_chain = false;
        uint32_t header = dcb_words[pc];
        hist_pc[hist_n++ & 7] = pc;
        uint32_t type = (header >> 30) & 0x3;

        if (type == 3) {
            uint32_t opcode = (header >> 8) & 0xFF;
            uint32_t payload_dw = ((header >> 16) & 0x3FFF) + 1;
            if (pc + 1 + payload_dw > dcb_dw) {
                st.frames.pop_back(); // truncated packet: drop the rest of this buffer
                continue;
            }
            const uint32_t *payload = &dcb_words[pc + 1];

            // PS4_GNM_STATS=1: histogram of type-3 opcodes, printed every 600 submits.
            if (g_gnm_stats) g_gnm_hist[opcode]++;

            switch (opcode) {
            case PM4_OP_SET_CONFIG_REG: ApplySetReg(0x2000, payload, payload_dw); break;
            case PM4_OP_SET_SH_REG: ApplySetReg(0x2C00, payload, payload_dw); break;
            case PM4_OP_SET_CONTEXT_REG: {
                ApplySetReg(0xA000, payload, payload_dw);
                // Surface extent hints: the guest emits a NOP (0xc0001000) carrying width|height<<16
                // right after the CB/DB setup (shadPS4 liverpool.cpp SetContextReg).
                const uint32_t reg = payload[0] & 0xFFFF;
                const uint32_t count = payload_dw - 1;
                const uint32_t *vals = payload + 1;
                const uint32_t avail = dcb_dw - (pc + 2); // dwords from vals[0] to the end
                auto hint = [&](uint32_t idx, uint32_t *out) {
                    if (idx + 1 < avail && vals[idx] == 0xc0001000u) *out = vals[idx + 1];
                    else *out = 0;
                };
                bool changed = false;
                for (uint32_t n = 0; n < 8; n++) {
                    if (reg == 0x318 + 15 * n) {
                        if (count == 0x0e || count == 0x0d || count == 0x0b) hint(count, &g_cb_extent[n]);
                        else g_cb_extent[n] = 0;
                        changed = true;
                    } else if (reg == 0x318 + 15 * n + 7 && count == 4) {
                        hint(4, &g_cb_extent[n]);
                        changed = true;
                    }
                }
                if (reg == 0x10) { // DB_Z_INFO
                    if (count == 8) hint(20, &g_db_extent);
                    else g_db_extent = 0;
                    changed = true;
                }
                if (changed) ps4draw::SetExtentHints(g_cb_extent, g_db_extent);
                break;
            }
            case PM4_OP_SET_UCONFIG_REG: ApplySetReg(0xC000, payload, payload_dw); break;
            case PM4_OP_CLEAR_STATE: {
                // shadPS4 Regs::SetDefaults; the graphics queue's compute state is separate there
                uint32_t cs[CS_REG_BLOCK_SIZE];
                memcpy(cs, &g_liverpool_regs[CS_REG_BLOCK_BASE], sizeof(cs));
                ps4draw::SetRegDefaults(g_liverpool_regs);
                memcpy(&g_liverpool_regs[CS_REG_BLOCK_BASE], cs, sizeof(cs));
                break;
            }
            case PM4_OP_EVENT_WRITE_EOP:
                if (!g_min_barriers) ps4draw::ComputeBarrier(ps4draw::kQueueGfx);
                HandleEventWriteEop(ctx, payload, payload_dw, PS4_GNM_EVENT_GFX_EOP);
                break;
            case PM4_OP_RELEASE_MEM:
                if (!g_min_barriers) ps4draw::ComputeBarrier(ps4draw::kQueueGfx);
                HandleReleaseMem(ctx, payload, payload_dw, PS4_GNM_EVENT_GFX_EOP);
                break;
            case PM4_OP_EVENT_WRITE_EOS:
                if (!g_min_barriers) ps4draw::ComputeBarrier(ps4draw::kQueueGfx);
                HandleEventWriteEos(ctx, payload, payload_dw);
                break;
            case PM4_OP_ACQUIRE_MEM:
                ps4draw::ComputeBarrier(ps4draw::kQueueGfx);
                break;
            case PM4_OP_EVENT_WRITE:
                // Cache flushes / VS/PS/CS partial flushes: Metal tracks hazards between encoders;
                // concurrent dispatches get a barrier.
                ps4draw::ComputeBarrier(ps4draw::kQueueGfx);
                // Occlusion queries (ZPASS_DONE / PIXEL_PIPE_STAT_DUMP): report fake, growing
                // pixel counts like shadPS4 (liverpool.cpp EventWrite) so queries never read 0.
                if (payload_dw >= 3 && ((payload[0] >> 8) & 0xF) == 1 && (payload[0] & 0x3F) == 57) {
                    static uint64_t pixel_counter = 0;
                    const uint64_t qaddr = ((uint64_t)payload[1] & ~7ull) | ((uint64_t)payload[2] << 32);
                    const int pairs = sceKernelIsNeoMode() ? 16 : 8;
                    const uint64_t value = pixel_counter | 0x8000000000000000ULL;
                    pixel_counter += 0x2FFFFFFULL;
                    ps4draw::AfterGpu([=]() {
                        uint64_t *r = (uint64_t *)GuestMemPtr(ctx, qaddr, (size_t)pairs * 16);
                        if (!r) return;
                        for (int i = 0; i < pairs; i++) r[i * 2] = value;
                    });
                }
                break;
            case PM4_OP_WAIT_REG_MEM:
                ps4draw::ComputeBarrier(ps4draw::kQueueGfx);
                if (!HandleWaitRegMem(ctx, payload, payload_dw, true)) {
                    f.pc = pc; // re-evaluate this packet when the stream resumes
                    return false;
                }
                break;
            case PM4_OP_WRITE_DATA:
                if (!g_min_barriers) ps4draw::ComputeBarrier(ps4draw::kQueueGfx);
                HandleWriteData(ctx, payload, payload_dw);
                break;
            case PM4_OP_DMA_DATA:
                ps4draw::ComputeBarrier(ps4draw::kQueueGfx);
                HandleDmaData(ctx, payload, payload_dw);
                break;
            case PM4_OP_INDIRECT_BUFFER: {
                const uint32_t *ib = nullptr;
                uint32_t ib_dw = 0;
                const bool chain = ps4gnm_ib::IsChained(payload, payload_dw);
                if (!chain && depth >= kMaxIbDepth) {
                    if (LogOnce(s_logged_ib_depth)) {
                        fprintf(stderr, "[ps4-gnm] INDIRECT_BUFFER nesting too deep; skipping (logged once)\n");
                    }
                } else if (DecodeIndirectBuffer(ctx, payload, payload_dw, &ib, &ib_dw)) {
                    push_ib = ib; // entered after this packet is consumed
                    push_ib_dw = ib_dw;
                    push_chain = chain;
                }
                break;
            }
            case PM4_OP_INCREMENT_DE_COUNTER:
                if (ce) ce->de_count++;
                break;
            case PM4_OP_WAIT_ON_CE_COUNTER:
                if (ce) {
                    while (ce->ce_count <= ce->de_count && !ce->Done()) {
                        if (!RunCe(ctx, *ce, false)) break;
                    }
                }
                break;
            case PM4_OP_COND_EXEC: {
                // shadPS4 PM4CmdCondExec: skip exec_count dwords if the bool in memory is zero
                if (payload_dw >= 3) {
                    const uint64_t baddr = ((uint64_t)payload[0] & ~3ull) | ((uint64_t)(payload[1] & 0xFFFF) << 32);
                    const uint32_t exec_count = payload[2] & 0x3FFF;
                    const uint8_t *bp = (const uint8_t *)GuestMemPtr(ctx, baddr, 1);
                    if (bp && *bp == 0 && ps4draw::HasPendingWork()) {
                        SyncGpu(6); // the condition may be GPU-produced
                    }
                    if (!bp || *bp == 0) {
                        f.pc = pc + 1 + payload_dw + exec_count;
                        continue;
                    }
                }
                break;
            }
            case PM4_OP_INDEX_BASE:
                if (payload_dw >= 2) g_draw.index_base = ((uint64_t)payload[0] & ~1ull) | ((uint64_t)(payload[1] & 0xFFFF) << 32);
                break;
            case PM4_OP_INDEX_BUFFER_SIZE:
                g_draw.index_buffer_size = payload[0];
                break;
            case PM4_OP_INDEX_TYPE:
                g_draw.index_type = payload[0] & 3;
                break;
            case PM4_OP_NUM_INSTANCES:
                g_draw.num_instances = payload[0];
                break;
            case PM4_OP_SET_BASE:
                if (payload_dw >= 3 && (payload[0] & 0xF) == 1)
                    g_draw.indirect_base = (uint64_t)payload[1] | ((uint64_t)(payload[2] & 0xFFFF) << 32);
                break;
            case PM4_OP_DRAW_INDEX_2:
                if (payload_dw >= 4) {
                    g_draw.index_base = (uint64_t)payload[1] | ((uint64_t)(payload[2] & 0xFFFF) << 32);
                    DoDraw(ctx, "DRAW_INDEX_2", true, 0, payload[3], payload[0]);
                }
                break;
            case PM4_OP_DRAW_INDEX_OFFSET_2:
                if (payload_dw >= 3 && payload[2] > (16u << 20)) {
                    static int dumped = 0;
                    if (dumped++ < 4) {
                        fprintf(stderr, "[ps4-gnm] odd DRAW_INDEX_OFFSET_2 at %p (pc %u/%u):", (const void *)&dcb_words[pc], pc, dcb_dw);
                        for (int k = -40; k < 16; k++)
                            if ((int)pc + k >= 0 && pc + k < dcb_dw) fprintf(stderr, " %s%08x", k == 0 ? "*" : "", dcb_words[pc + k]);
                        fprintf(stderr, "\n[ps4-gnm]   previous packets:");
                        for (uint32_t k = hist_n > 8 ? hist_n - 8 : 0; k < hist_n; k++)
                            fprintf(stderr, " pc%u=%08x", hist_pc[k & 7], dcb_words[hist_pc[k & 7]]);
                        fprintf(stderr, " depth=%d\n", depth);
                    }
                }
                if (payload_dw >= 3) DoDraw(ctx, "DRAW_INDEX_OFFSET_2", true, payload[1], payload[2], payload[0]);
                break;
            case PM4_OP_DRAW_INDEX_AUTO:
                if (payload_dw >= 1) DoDraw(ctx, "DRAW_INDEX_AUTO", false, 0, payload[0], 0);
                break;
            case PM4_OP_DRAW_INDIRECT:
            case PM4_OP_DRAW_INDEX_INDIRECT:
                // {data_offset, base_vtx_loc, start_inst_loc, draw_initiator}
                if (payload_dw >= 3 && g_draw.indirect_base) {
                    bool synced = false;
                    DrawIndirectRecord(ctx, opcode == PM4_OP_DRAW_INDEX_INDIRECT, g_draw.indirect_base + payload[0],
                                       payload[1] & 0xFFFF, payload[2] & 0xFFFF, &synced);
                }
                break;
            case PM4_OP_DRAW_INDIRECT_MULTI:
            case PM4_OP_DRAW_INDEX_INDIRECT_MULTI:
                // {data_offset, base_vtx_loc, start_inst_loc, count, stride, draw_initiator}: the CP
                // walks `count` argument records `stride` bytes apart, each executed like a single
                // DRAW_[INDEX_]INDIRECT (shadPS4 Liverpool::ProcessGraphics, DrawIndirect).
                if (payload_dw >= 5 && g_draw.indirect_base) {
                    const bool indexed = opcode == PM4_OP_DRAW_INDEX_INDIRECT_MULTI;
                    const uint32_t count = payload[3];
                    const uint32_t stride = payload[4] ? payload[4] : (indexed ? 20u : 16u);
                    if (count > 65536) {
                        if (LogOnce(s_logged_indirect_draw))
                            fprintf(stderr, "[ps4-gnm] implausible indirect multi draw count %u; skipped (logged once)\n", count);
                        break;
                    }
                    bool synced = false;
                    for (uint32_t i = 0; i < count; i++)
                        DrawIndirectRecord(ctx, indexed, g_draw.indirect_base + payload[0] + (uint64_t)i * stride,
                                           payload[1] & 0xFFFF, payload[2] & 0xFFFF, &synced);
                }
                break;
            case PM4_OP_DISPATCH_DIRECT:
                DumpShadersForDraw(ctx, "DISPATCH_DIRECT", true);
                if (payload_dw >= 4 && (payload[3] & 1) && (ps4draw::Enabled() || ps4draw::Init(ctx)))
                    ps4draw::Dispatch(ctx, g_liverpool_regs, payload[0], payload[1], payload[2], 0, "gfx", ps4draw::kQueueGfx);
                break;
            case PM4_OP_DISPATCH_INDIRECT:
                DumpShadersForDraw(ctx, "DISPATCH_INDIRECT", true);
                if (payload_dw >= 2 && (payload[1] & 1) && g_draw.indirect_base &&
                    (ps4draw::Enabled() || ps4draw::Init(ctx)))
                    ps4draw::Dispatch(ctx, g_liverpool_regs, 0, 0, 0, g_draw.indirect_base + payload[0], "gfx", ps4draw::kQueueGfx);
                break;
            default:
                break;
            }

            pc += 1 + payload_dw;
        } else if (type == 2) {
            pc += 1;
        } else if (type == 0) {
            uint32_t count_dw = (header >> 16) & 0x3FFF;
            pc += 1 + count_dw + 1;
        } else {
            pc += 1;
        }
        st.frames.back().pc = pc;
        if (push_ib) ps4gnm_ib::Enter(st.frames, push_ib, push_ib_dw, push_chain);
    }
    return true;
}

static void ComputeDispatch(GuestContext *ctx, uint32_t slot, uint32_t x, uint32_t y, uint32_t z, uint64_t indirect,
                            const char *name) {
    if (!ps4draw::Enabled() && !ps4draw::Init(ctx)) return;
    uint32_t saved[CS_REG_BLOCK_SIZE];
    memcpy(saved, &g_liverpool_regs[CS_REG_BLOCK_BASE], sizeof(saved));
    memcpy(&g_liverpool_regs[CS_REG_BLOCK_BASE], g_cs_regs[slot], sizeof(saved));
    DumpShadersForDraw(ctx, name, true);
    ps4draw::Dispatch(ctx, g_liverpool_regs, x, y, z, indirect, name, ps4draw::kQueueCompute); // the asc rings
    memcpy(&g_liverpool_regs[CS_REG_BLOCK_BASE], saved, sizeof(saved));
}

// ---- Compute queues. A submission (a ring span) runs in order per queue; a WAIT_REG_MEM whose
// value is neither in memory nor pending from encoded GPU work suspends the queue, like the
// graphics queue: Uncharted 4's async compute copies the depth buffer after waiting for the
// graphics depth prepass, and the game kicks that job before the graphics command buffer, so
// the copy ran first and copied stale depth (later passes then tested against a depth buffer
// without the characters, and the water was drawn over Drake). A suspended queue continues when
// graphics work produces labels, on its next submission, or from the watchdog (after
// kGfxWaitLimitMs it continues without the wait, as before).
struct ComputeSubmit {
    GuestContext *ctx = nullptr;
    uint32_t pipe_id = 0, slot = 0;
    uint32_t *read_ptr = nullptr; // ring read pointer, advanced as depth-0 packets complete
    uint32_t ring_size_dw = 0, read_base_dw = 0;
    std::vector<ps4gnm_ib::Frame> frames;
};
using ComputeQueueState = ps4gnm_wq::WaitQueue<ComputeSubmit>;
static ComputeQueueState g_cq[kMaxComputeQueues];
static uint64_t g_cq_suspends = 0, g_cq_timeouts = 0;
static uint64_t g_queue_progress = 0; // bumped whenever a suspended queue runs again
static constexpr uint64_t kComputeWaitLimitMs = 50;

// Runs the submission until it finishes (true) or a WAIT_REG_MEM suspends it (false).
// skip_wait: the first WAIT_REG_MEM reached continues without waiting (watchdog timeout).
static bool ProcessComputePackets(ComputeSubmit &sub, bool skip_wait) {
    GuestContext *ctx = sub.ctx;
    const uint32_t pipe_id = sub.pipe_id, slot = sub.slot;
    auto exec = [&](const ps4gnm_ib::Frame &f, int depth, ps4gnm_ib::Packet &pk) {
        const uint32_t header = f.words[f.pc];
        const uint32_t type = (header >> 30) & 0x3;
        if (type == 0) {
            pk.step = ((header >> 16) & 0x3FFF) + 2;
            return;
        }
        if (type != 3) {
            pk.step = 1;
            return;
        }
        const uint32_t opcode = (header >> 8) & 0xFF;
        const uint32_t payload_dw = ((header >> 16) & 0x3FFF) + 1;
        if (f.pc + 1 + payload_dw > f.dw) return; // truncated: step 0 drops the rest
        const uint32_t *payload = &f.words[f.pc + 1];
        pk.step = 1 + payload_dw;
        switch (opcode) {
        case PM4_OP_RELEASE_MEM:
            if (!g_min_barriers) ps4draw::ComputeBarrier(ps4draw::kQueueCompute);
            HandleReleaseMem(ctx, payload, payload_dw, pipe_id);
            break;
        case PM4_OP_EVENT_WRITE_EOP: // not expected on MEC, but harmless
            if (!g_min_barriers) ps4draw::ComputeBarrier(ps4draw::kQueueCompute);
            HandleEventWriteEop(ctx, payload, payload_dw, pipe_id);
            break;
        case PM4_OP_EVENT_WRITE_EOS:
            if (!g_min_barriers) ps4draw::ComputeBarrier(ps4draw::kQueueCompute);
            HandleEventWriteEos(ctx, payload, payload_dw);
            break;
        case PM4_OP_EVENT_WRITE:
        case PM4_OP_ACQUIRE_MEM:
            ps4draw::ComputeBarrier(ps4draw::kQueueCompute); // CS partial flush / cache flushes
            break;
        case PM4_OP_WRITE_DATA:
            if (!g_min_barriers) ps4draw::ComputeBarrier(ps4draw::kQueueCompute);
            HandleWriteData(ctx, payload, payload_dw);
            break;
        case PM4_OP_WAIT_REG_MEM:
            ps4draw::ComputeBarrier(ps4draw::kQueueCompute);
            if (skip_wait) {
                skip_wait = false; // timed out: continue as before suspension existed
            } else if (!HandleWaitRegMem(ctx, payload, payload_dw, true)) {
                pk.suspend = true; // the queue stops at this packet
                return;
            }
            break;
        case PM4_OP_DMA_DATA:
            ps4draw::ComputeBarrier(ps4draw::kQueueCompute);
            HandleDmaData(ctx, payload, payload_dw);
            break;
        case PM4_OP_SET_SH_REG: {
            const uint32_t off = payload[0] & 0xFFFF;
            for (uint32_t r = 1; r < payload_dw; r++) {
                const uint32_t reg = 0x2C00 + off + r - 1;
                if (reg >= CS_REG_BLOCK_BASE && reg < CS_REG_BLOCK_BASE + CS_REG_BLOCK_SIZE)
                    g_cs_regs[slot][reg - CS_REG_BLOCK_BASE] = payload[r];
                else if (reg < 0x10000)
                    g_liverpool_regs[reg] = payload[r];
            }
            break;
        }
        case PM4_OP_DISPATCH_DIRECT:
            if (payload_dw >= 4 && (payload[3] & 1))
                ComputeDispatch(ctx, slot, payload[0], payload[1], payload[2], 0, "asc");
            break;
        case PM4_OP_DISPATCH_INDIRECT:
            if (payload_dw >= 3 && (payload[2] & 1)) {
                const uint64_t addr = (uint64_t)payload[0] | ((uint64_t)(payload[1] & 0xFFFF) << 32);
                ComputeDispatch(ctx, slot, 0, 0, 0, addr, "asc-indirect");
            }
            break;
        case PM4_OP_INDIRECT_BUFFER: {
            // chained inside an IB: continue in the new buffer and do not return. At ring
            // level (depth 0, with or without a read pointer) the CP returns to the ring, so
            // the packet is a call.
            pk.chain = ps4gnm_ib::IsChained(payload, payload_dw);
            if (!ps4gnm_ib::ComputeChainReplaces(pk.chain, depth) && depth >= kMaxIbDepth) {
                if (LogOnce(s_logged_ib_depth)) {
                    fprintf(stderr, "[ps4-gnm] INDIRECT_BUFFER nesting too deep; skipping (logged once)\n");
                }
            } else if (!DecodeIndirectBuffer(ctx, payload, payload_dw, &pk.ib, &pk.ib_dw)) {
                pk.ib = nullptr;
            }
            break;
        }
        default:
            // ACQUIRE_MEM, EVENT_WRITE, NOP, ...: no-op here.
            break;
        }
    };
    auto progress = [&](uint32_t pc) {
        if (sub.read_ptr && sub.ring_size_dw)
            __atomic_store_n(sub.read_ptr, (sub.read_base_dw + pc) % sub.ring_size_dw, __ATOMIC_SEQ_CST);
    };
    return ps4gnm_ib::Walk(sub.frames, true, exec, progress);
}

static void StartGfxWatchdog();

// Runs the queue's submissions in order until one suspends. force: the suspended one continues
// past its wait.
static void RunComputeQueue(uint32_t slot, bool force) {
    ComputeQueueState &q = g_cq[slot];
    const uint64_t timeouts = q.timeouts;
    const bool stopped = q.Run(force, clock_gettime_nsec_np(CLOCK_UPTIME_RAW),
                               [](ComputeSubmit &sub, bool skip) { return ProcessComputePackets(sub, skip); });
    g_cq_timeouts += q.timeouts - timeouts;
    if (stopped) {
        g_cq_suspends++;
        if ((g_cq_suspends & (g_cq_suspends - 1)) == 0)
            fprintf(stderr, "[ps4-gnm] a compute queue waits for another queue: %llu times, %llu timed out\n",
                    (unsigned long long)g_cq_suspends, (unsigned long long)g_cq_timeouts);
        StartGfxWatchdog();
    }
}

// Continues suspended compute queues whose wait is satisfied (or timed out). Called with
// g_state.mutex held.
static void ResumeComputeQueues() {
    const uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    for (uint32_t slot = 0; slot < kMaxComputeQueues; ++slot) {
        ComputeQueueState &q = g_cq[slot];
        if (!q.Suspended()) continue;
        const ComputeSubmit &sub = q.pending.front();
        if (sub.frames.empty()) continue;
        const ps4gnm_ib::Frame &f = sub.frames.back();
        const uint32_t payload_dw = ((f.words[f.pc] >> 16) & 0x3FFF) + 1;
        const bool ready = WaitRegMemReady(sub.ctx, &f.words[f.pc + 1], payload_dw);
        if (!ready && !q.TimedOut(now, kComputeWaitLimitMs * 1000000ull)) continue;
        if (ready) q.suspended_at = 0;
        RunComputeQueue(slot, !ready);
        g_queue_progress++;
    }
}

static bool AnyComputeSuspended() {
    for (const auto &q : g_cq)
        if (q.Suspended()) return true;
    return false;
}

// ---- Graphics queue: command buffers run in order; a WAIT_REG_MEM that is not satisfied yet
// suspends the queue (later submissions wait behind it). The queue continues after compute
// submissions, on the next graphics submission, or from a watchdog thread, which also gives
// up after kGfxWaitLimitMs (kFlipWaitLimitMs for a flip label) so a wait for something that never
// comes cannot stall the game.
struct GfxSubmit {
    GuestContext *ctx = nullptr;
    const uint32_t *dcb = nullptr, *ccb = nullptr;
    uint32_t dcb_dw = 0, ccb_dw = 0;
    std::function<void()> present; // a flip queued behind the command buffers before it
};
static struct {
    std::deque<GfxSubmit> pending;
    bool active = false; // `cur` holds a started command buffer
    GfxSubmit cur_sub;
    GfxStream cur;
    uint64_t suspended_at = 0; // ns, 0 = running
    uint64_t suspends = 0, timeouts = 0;
    bool flip_wait = false; // the wait it is suspended at polls a VideoOut flip label
} g_gfx;
// Flip-label waits (sceGnmInsertWaitFlipDone): suspensions, waits given up after kFlipWaitLimitMs,
// and wake-ups of the wait thread by a released label (ps4_gnm_metal_flip_label_released): label
// releases seen while a flip-label wait was suspended, not resumptions (one release may find the
// wait still unsatisfied, and the 1 ms poll may resume a wait before the wake-up is counted).
// The counters are ps4draw::FlipWaits(); read without the CP lock by ps4_gnm_metal_flip_wait_stats.
static std::atomic<bool> g_flip_wait_suspended{false};
// The wait thread (StartGfxWatchdog) sleeps on g_wake_cv for at most 1 ms; a released flip label
// wakes it at once. Its own lock, never the CP lock: the flip completes on the presenter's or the
// GPU's completion thread.
static pthread_mutex_t g_wake_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake_cv = PTHREAD_COND_INITIALIZER;
static bool g_wake_pending = false;
// The wait thread's end at exit (StopGfxWatchdog): it takes the CP lock and reads the queues'
// state, all static objects that exit() destroys; locking the destroyed recursive_mutex threw
// ("recursive_mutex lock failed", an abort after the process's last line). Under g_wake_mu.
static bool g_watchdog_exit = false, g_watchdog_stopped = false;
static pthread_cond_t g_watchdog_stopped_cv = PTHREAD_COND_INITIALIZER;
static constexpr uint64_t kGfxWaitLimitMs = 50;
// A wait for a VideoOut flip label (sceGnmInsertWaitFlipDone) lasts until the display takes the
// next frame, as on the PS4: every flip completes within the held-frame timeout of the Metal
// screen (1 s) or when the GPU watchdog gives the renderer up. Only a wait that outlasts both (no
// queued flip can release the label) is given up.
static constexpr uint64_t kFlipWaitLimitMs = 2000;

// The guest address a WAIT_REG_MEM packet (header at words[0]) polls in memory; 0 for a register.
static uint64_t WaitPollAddress(const uint32_t *words) {
    const uint32_t *p = words + 1;
    if (((p[0] >> 4) & 1u) == 0) return 0;
    return ((uint64_t)p[1] & ~3ULL) | ((uint64_t)(p[2] & 0xFFFFu) << 32);
}

static void StartGfxWatchdog();

// Runs queued graphics work until the queue is empty or suspended. `force` continues a
// suspended stream past its wait. Called with the CP lock held.
static void RunGfxQueue(bool force) {
    for (;;) {
        if (!g_gfx.active) {
            if (g_gfx.pending.empty()) break;
            g_gfx.cur_sub = g_gfx.pending.front();
            g_gfx.pending.pop_front();
            if (g_gfx.cur_sub.present) {
                g_gfx.cur_sub.present();
                continue;
            }
            g_gfx.cur = GfxStream{};
            GuestContext *ctx = g_gfx.cur_sub.ctx;
            if (g_gfx.cur_sub.ccb && g_gfx.cur_sub.ccb_dw) {
                g_gfx.cur.ce.stack.push_back({g_gfx.cur_sub.ccb, g_gfx.cur_sub.ccb_dw, 0});
                RunCe(ctx, g_gfx.cur.ce, false); // kick off the CE as far as it can go
            }
            if (g_gfx.cur_sub.dcb && g_gfx.cur_sub.dcb_dw) g_gfx.cur.frames.push_back({g_gfx.cur_sub.dcb, g_gfx.cur_sub.dcb_dw, 0});
            g_gfx.active = true;
        }
        GuestContext *ctx = g_gfx.cur_sub.ctx;
        if (force && g_gfx.suspended_at) {
            // skip the wait packet this stream is stopped at
            GfxStream::Frame &f = g_gfx.cur.frames.back();
            f.pc += ((f.words[f.pc] >> 16) & 0x3FFF) + 2;
            g_gfx.timeouts++;
            force = false;
            // the next wait the stream reaches is a new suspension with its own kind and limit
            g_gfx.suspended_at = 0;
            g_gfx.flip_wait = false;
            g_flip_wait_suspended.store(false, std::memory_order_relaxed);
        }
        if (!ProcessGfxPackets(ctx, g_gfx.cur)) {
            if (!g_gfx.suspended_at) {
                g_gfx.suspended_at = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
                g_gfx.suspends++;
                const GfxStream::Frame &wf = g_gfx.cur.frames.back();
                g_gfx.flip_wait = ps4_videoout_is_flip_label(WaitPollAddress(&wf.words[wf.pc]));
                // Stored for every new suspension: a resume after a release does not clear it.
                g_flip_wait_suspended.store(g_gfx.flip_wait, std::memory_order_release);
                if (g_gfx.flip_wait)
                    ps4draw::FlipWaits().suspends.fetch_add(1, std::memory_order_relaxed);
                if ((g_gfx.suspends & (g_gfx.suspends - 1)) == 0)
                    fprintf(stderr, "[ps4-gnm] graphics queue waits for another queue: %llu times, %llu timed out\n",
                            (unsigned long long)g_gfx.suspends, (unsigned long long)g_gfx.timeouts);
                StartGfxWatchdog();
            }
            // let the work before the wait reach the GPU
            if (ps4draw::HasPendingWork()) ps4draw::Flush(false);
            return;
        }
        g_gfx.suspended_at = 0;
        g_gfx.flip_wait = false;
        g_flip_wait_suspended.store(false, std::memory_order_relaxed);
        if (!g_gfx.cur.ce.Done()) RunCe(ctx, g_gfx.cur.ce, true);
        g_gfx.active = false;
    }
    // Commit whatever is still open (no wait: the next fence waits).
    if (ps4draw::HasPendingWork()) ps4draw::Flush(false);
}

// Continues a suspended graphics queue if its wait is satisfied now (or timed out).
static void ResumeGfxQueue() {
    if (!g_gfx.active || !g_gfx.suspended_at) return;
    const GfxStream::Frame &f = g_gfx.cur.frames.back();
    const uint32_t payload_dw = ((f.words[f.pc] >> 16) & 0x3FFF) + 1;
    const bool ready = WaitRegMemReady(g_gfx.cur_sub.ctx, &f.words[f.pc + 1], payload_dw);
    const uint64_t limit_ms = g_gfx.flip_wait ? kFlipWaitLimitMs : kGfxWaitLimitMs;
    const uint64_t waited_ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - g_gfx.suspended_at;
    const bool timed_out = waited_ns > limit_ms * 1000000ull;
    if (!ready && !timed_out) return;
    if (!ready && g_gfx.flip_wait) {
        // no queued flip released the label: the guest waits for a buffer that stays on screen
        const uint64_t n = ps4draw::FlipWaits().timeouts.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((n & (n - 1)) == 0) {
            int32_t handle = -1, index = -1, prev = -1;
            ps4_videoout_flip_label_info(WaitPollAddress(&f.words[f.pc]), &handle, &index, &prev);
            fprintf(stderr, "[ps4-gnm] flip-label wait given up after %.0f ms: handle %d, buffer %d, last flipped buffer %d "
                            "(%llu so far)\n", waited_ns / 1e6, handle, index, prev, (unsigned long long)n);
        }
    }
    if (ready) g_gfx.suspended_at = 0;
    RunGfxQueue(!ready);
    g_queue_progress++;
    if (ready || !g_gfx.suspended_at) ps4draw::OnSubmit();
}

// Lets suspended queues wait for each other: runs whichever can continue until none does.
// Called with the CP lock held.
static void PumpQueues() {
    const uint64_t start = g_queue_progress;
    for (int i = 0; i < 64; ++i) {
        const uint64_t before = g_queue_progress;
        ResumeComputeQueues();
        ResumeGfxQueue();
        if (g_queue_progress == before) break;
    }
    // commit what the queues that continued encoded (nothing new otherwise: the watchdog calls
    // this every millisecond while a queue waits)
    if (g_queue_progress != start && ps4draw::HasPendingWork()) ps4draw::Flush(false);
}

// Exit handler (registered by StartGfxWatchdog, after the static objects the thread uses were
// constructed, so it runs before they are destroyed): the thread ends at its next turn, and exit
// waits until it has. Its turn takes the CP lock: a thread that holds it forever (a crashed
// process) bounds the wait at 2 s, after which exit goes on.
static void StopGfxWatchdog() {
    pthread_mutex_lock(&g_wake_mu);
    g_watchdog_exit = true;
    g_wake_pending = true;
    pthread_cond_broadcast(&g_wake_cv);
    const struct timespec s2 = {2, 0};
    while (!g_watchdog_stopped)
        if (pthread_cond_timedwait_relative_np(&g_watchdog_stopped_cv, &g_wake_mu, &s2) == ETIMEDOUT) {
            fprintf(stderr, "[ps4-gnm] exit: the queue wait thread did not end within 2 s\n");
            break;
        }
    pthread_mutex_unlock(&g_wake_mu);
}

static void StartGfxWatchdog() {
    static bool started = false;
    if (started) return;
    started = true;
    atexit(StopGfxWatchdog);
    std::thread([] {
        for (;;) {
            pthread_mutex_lock(&g_wake_mu);
            if (!g_wake_pending) {
                const struct timespec ms1 = {0, 1000000};
                pthread_cond_timedwait_relative_np(&g_wake_cv, &g_wake_mu, &ms1);
            }
            g_wake_pending = false;
            if (g_watchdog_exit) {
                g_watchdog_stopped = true;
                pthread_cond_broadcast(&g_watchdog_stopped_cv);
                pthread_mutex_unlock(&g_wake_mu);
                return;
            }
            pthread_mutex_unlock(&g_wake_mu);
            std::scoped_lock lock(g_state.mutex);
            if ((g_gfx.active && g_gfx.suspended_at) || AnyComputeSuspended()) {
                @autoreleasepool {
                    PumpQueues();
                    ps4draw::WaitKernelCompiles();
                }
            }
        }
    }).detach();
}

} // namespace

extern "C" void ps4_gnm_metal_flip_label_released(void) {
    if (g_flip_wait_suspended.load(std::memory_order_acquire)) ps4draw::FlipWaits().wakes.fetch_add(1, std::memory_order_relaxed);
    pthread_mutex_lock(&g_wake_mu);
    g_wake_pending = true;
    pthread_cond_signal(&g_wake_cv);
    pthread_mutex_unlock(&g_wake_mu);
}

extern "C" void ps4_gnm_metal_flip_wait_stats(uint64_t *suspends, uint64_t *timeouts, uint64_t *wakes) {
    if (suspends) *suspends = ps4draw::FlipWaits().suspends.load(std::memory_order_relaxed);
    if (timeouts) *timeouts = ps4draw::FlipWaits().timeouts.load(std::memory_order_relaxed);
    if (wakes) *wakes = ps4draw::FlipWaits().wakes.load(std::memory_order_relaxed);
}

// PS4_TEST_HOOKS=1 enables the host tests' hooks (ps4_gnm_metal_debug_stall_gpu). Read once, at init.
static std::atomic<bool> g_test_hooks{false};

int ps4_gnm_metal_init(GuestContext *ctx) {
    std::scoped_lock lock(g_state.mutex);
    static std::once_flag hooks_once;
    std::call_once(hooks_once, [] {
        const char *e = getenv("PS4_TEST_HOOKS");
        g_test_hooks.store(e && strcmp(e, "1") == 0, std::memory_order_relaxed);
    });
    return ps4draw::Init(ctx) ? 0 : -1;
}

int32_t ps4_gnm_metal_process_compute_packets(GuestContext *ctx, const uint32_t *words, uint32_t size_dw,
                                              uint32_t pipe_id, uint32_t queue_slot, uint32_t *read_ptr,
                                              uint32_t ring_size_dw, uint32_t read_base_dw) {
    if (!ctx || !words || size_dw == 0) return 0;
    {
        static std::atomic<int> logged{0};
        if (logged.fetch_add(1) < 3) {
            char nm[64] = {0};
            pthread_getname_np(pthread_self(), nm, sizeof(nm));
            fprintf(stderr, "[ps4-gnm] compute queue submit from thread '%s'\n", nm);
        }
    }
    std::scoped_lock lock(g_state.mutex);
    CpTimer cp_timer;
    const uint32_t slot = queue_slot % kMaxComputeQueues;
    ComputeQueueState &q = g_cq[slot];
    @autoreleasepool {
        ComputeSubmit sub;
        sub.ctx = ctx;
        sub.pipe_id = pipe_id;
        sub.slot = slot;
        sub.read_ptr = read_ptr;
        sub.ring_size_dw = ring_size_dw;
        sub.read_base_dw = read_base_dw;
        sub.frames.push_back({words, size_dw, 0});
        q.pending.push_back(std::move(sub));
        if (!q.Suspended()) RunComputeQueue(slot, false);
        // compute work without a trailing fence still has to reach the GPU: committed now, or held
        // for the next submissions while the GPU is behind (ps4draw::FlushCompute: 0.5 ms at most,
        // plus the encoder jobs queued before the release);
        // a queue that now waits for another one commits now (what it waits for may be in it)
        if (ps4draw::HasPendingWork()) {
            if (q.Suspended()) ps4draw::Flush(false);
            else ps4draw::FlushCompute();
        }
        PumpQueues();
        ps4draw::WaitKernelCompiles();
    }
    // nonzero: part of the submission still waits; the caller must not report it consumed
    return q.pending.empty() ? 0 : 1;
}

void ps4_gnm_metal_reset_compute_queue(uint32_t queue_slot) {
    std::scoped_lock lock(g_state.mutex);
    g_cq[queue_slot % kMaxComputeQueues].Reset();
}

int32_t ps4_gnm_metal_process_command_buffers(GuestContext *ctx, uint32_t count,
                                             const uint64_t *dcb_gpu_addrs, const uint32_t *dcb_sizes_in_bytes,
                                             const uint64_t *ccb_gpu_addrs, const uint32_t *ccb_sizes_in_bytes) {
    if (!ctx || count == 0) return 0;
    std::scoped_lock lock(g_state.mutex);
    CpTimer cp_timer;
    if (g_gnm_stats < 0) g_gnm_stats = getenv("PS4_GNM_STATS") != NULL;
    if (g_gnm_stats) {
        static uint64_t submits;
        if (++submits % 600 == 0) {
            fprintf(stderr, "[ps4-gnm] PM4 opcode counts over 600 submits:");
            for (int i = 0; i < 256; i++) {
                uint64_t c = g_gnm_hist[i].exchange(0);
                if (c) fprintf(stderr, " %02x:%llu", i, (unsigned long long)c);
            }
            fprintf(stderr, "\n");
        }
    }

    @autoreleasepool {
        for (uint32_t b = 0; b < count; b++) {
            GfxSubmit sub;
            sub.ctx = ctx;
            if (ccb_gpu_addrs && ccb_sizes_in_bytes && ccb_gpu_addrs[b] && ccb_sizes_in_bytes[b]) {
                sub.ccb = (const uint32_t *)GuestMemPtr(ctx, ccb_gpu_addrs[b], ccb_sizes_in_bytes[b]);
                sub.ccb_dw = ccb_sizes_in_bytes[b] / 4;
            }
            // DCB (Draw Command Buffer); fences/interrupts are raised in stream order.
            if (dcb_gpu_addrs && dcb_sizes_in_bytes && dcb_gpu_addrs[b] && dcb_sizes_in_bytes[b]) {
                sub.dcb = (const uint32_t *)GuestMemPtr(ctx, dcb_gpu_addrs[b], dcb_sizes_in_bytes[b]);
                sub.dcb_dw = dcb_sizes_in_bytes[b] / 4;
            }
            g_gfx.pending.push_back(sub);
        }
        RunGfxQueue(false);
        PumpQueues();
        ps4draw::OnSubmit();
        ps4draw::WaitKernelCompiles();
    }
    return 0;
}

int ps4_gnm_metal_present(GuestContext *ctx, uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch,
                          uint32_t tiling_mode, uint32_t pixel_format, void (*on_reached)(void *),
                          void (*on_done)(void *), void *arg) {
    std::scoped_lock lock(g_state.mutex);
    std::function<void()> fn;
    if (on_done) fn = [on_done, arg]() { on_done(arg); };
    if (g_gfx.active || !g_gfx.pending.empty()) {
        // the frame is still being rendered behind a suspended wait: flip after it
        GfxSubmit sub;
        sub.ctx = ctx;
        sub.present = [=]() {
            if (on_reached) on_reached(arg);
            std::function<void()> done = fn;
            if (!ps4draw::Present(ctx, addr, width, height, pitch, tiling_mode, pixel_format, std::move(done)) && fn)
                fn(); // no GPU target any more: complete the flip without a new picture
        };
        g_gfx.pending.push_back(std::move(sub));
        return 1;
    }
    if (on_reached) on_reached(arg); // the command processor is at the flip now
    return ps4draw::Present(ctx, addr, width, height, pitch, tiling_mode, pixel_format, std::move(fn)) ? 1 : 0;
}

int ps4_gnm_metal_debug_stall_gpu(void) {
    if (!g_test_hooks.load(std::memory_order_relaxed)) {
        fprintf(stderr, "[gnm-metal] debug GPU stall refused: the process did not start with PS4_TEST_HOOKS=1\n");
        return -1;
    }
    std::scoped_lock lock(g_state.mutex);
    ps4draw::DebugStallGpu();
    return 0;
}

int ps4_gnm_metal_debug_release_gpu(void) {
    if (!g_test_hooks.load(std::memory_order_relaxed)) {
        fprintf(stderr, "[gnm-metal] debug GPU release refused: the process did not start with PS4_TEST_HOOKS=1\n");
        return -1;
    }
    ps4draw::DebugReleaseGpu();
    return 0;
}

void ps4_gnm_metal_wait_gpu_idle(void) {
    std::scoped_lock lock(g_state.mutex);
    ps4draw::Flush(true);
}

// ps4_host_quit: the capture is written under the CP lock (no pass is then half recorded). The
// lock is waited for at most timeout_s: a queue stuck holding it must not block the quit.
extern "C" void ps4_gnm_capture_save_for_quit(double timeout_s) {
    const double t0 = CFAbsoluteTimeGetCurrent();
    while (!g_state.mutex.try_lock()) {
        if (CFAbsoluteTimeGetCurrent() - t0 > timeout_s) {
            fprintf(stderr, "[gnm-capture] quit: CP lock busy for %.1f s; capture not written\n", timeout_s);
            return;
        }
        usleep(1000);
    }
    ps4draw::CaptureAtQuit();
    g_state.mutex.unlock();
}

void ps4_gnm_metal_destroy(void) {
    std::scoped_lock lock(g_state.mutex);
    ps4draw::Shutdown();
}

// CPU fallback for presentation: detile a VideoOut buffer into BGRA8 (tiling_mode 0 = tiled).
extern "C" int ps4_gnm_read_display_cpu(GuestContext *ctx, uint64_t addr, uint32_t width, uint32_t height,
                                        uint32_t pitch, uint32_t tiling_mode, uint32_t pixel_format,
                                        uint8_t *out_bgra) {
    if (!ctx || !out_bgra) return 0;
    return ps4gnm::ReadVideoOutBufferBGRA8(ctx->mem_base, ctx->mem_size, addr, width, height, pitch,
                                           tiling_mode == 0, pixel_format, out_bgra, width * 4) ? 1 : 0;
}
