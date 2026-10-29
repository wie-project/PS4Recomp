// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4Recomp - native Metal rendering backend for the GNM command processor (see ps4_gnm_draw.h).
 *
 * The draw / dispatch logic follows shadPS4's Vulkan renderer:
 *   src/video_core/renderer_vulkan/vk_rasterizer.cpp      (Draw, DispatchDirect, BindResources,
 *                                                          BindBuffers, BindTextures,
 *                                                          BeginRendering, FilterDraw,
 *                                                          UpdateViewportScissorState)
 *   src/video_core/renderer_vulkan/vk_pipeline_cache.cpp  (shader permutations, runtime info)
 *   src/video_core/renderer_vulkan/vk_graphics_pipeline.cpp (vertex inputs, pipeline state)
 *   src/video_core/renderer_vulkan/liverpool_to_vk.cpp    (ColorBufferClearValue, quad lists)
 *   src/video_core/amdgpu/regs.cpp                        (Regs::SetDefaults)
 * SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Environment:
 *   PS4_GNM_METAL_DRAW=0     disable GPU work (fences/interrupts still produced)
 *   PS4_GNM_TRACE=<n>        log every draw/dispatch of the first n submits that contain work
 *   PS4_GNM_MSL_DUMP=<dir>   write the generated MSL of every compiled shader
 *   PS4_GNM_GPU_TIMEOUT=<ms> command buffer watchdog (default 4000)
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>

#include <dispatch/dispatch.h>
#include <dirent.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <optional>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <unistd.h>

#include <set>

#include "gcn_recompiler.h"
#include "ps4_gnm_draw.h"
#include "ps4_gnm_guest_window.h"
#include "ps4_gnm_membudget.h"
#include "ps4_gnm_pending_writes.h"
#include "ps4_gnm_rectlist.h"
#include "ps4_gnm_push_data.h"
#include "ps4_gnm_clear_color.h"
#include "ps4_gnm_resources.h"
#include "ps4_gnm_shader_cache.h"
#include "ps4_gnm_pipeline_archive.h"
#include "ps4_gnm_library_store.h"
#include "ps4_gnm_capture.h"
#include "ps4_gnm_kernel_compile.h"
#include "ps4_gnm_backend.h"
#include "ps4_gnm_backend_metal3.h"
#include "ps4_gnm_util_passes.h"
#include "ps4_gnm_vertex_layout.h"
#include "ps4_metal_screen.h"
#include "ps4_hitch.h"

extern "C" uint64_t ps4_gnm_take_cp_ns(void);
extern "C" void ps4_gnm_print_sync_stats(void);
extern "C" void ps4_fiber_print_stats(int frames);
extern "C" void ps4_sync_print_stats(int frames);

// Debug request from PS4_PAD_CTL: bit 0 = dump render targets of the next present, bit 1 =
// dump the color target after every draw of the next present (needs PS4_SCREENSHOT_DIR),
// bit 2 = trace the work of the next present ([gnm-trace] lines).
extern "C" volatile int g_ps4_gnm_dump_request;
volatile int g_ps4_gnm_dump_request;

extern "C" void ps4_host_quit(int code) __attribute__((noreturn));
extern "C" int32_t sceKernelIsNeoMode(void);

namespace ps4draw {
using namespace ps4gnm;

namespace {

// =====================================================================================
// Logging (rate limited per key: first 8 occurrences, then at powers of two)
// =====================================================================================
std::mutex g_log_mu;
std::unordered_map<std::string, uint64_t> g_log_counts;

void RLog(const char* key, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void RLog(const char* key, const char* fmt, ...) {
    uint64_t n;
    {
        std::lock_guard<std::mutex> lk(g_log_mu);
        n = ++g_log_counts[key];
    }
    if (n > 8 && (n & (n - 1)) != 0) return;
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 8)
        fprintf(stderr, "[gnm-draw] %s (x%llu)\n", buf, (unsigned long long)n);
    else
        fprintf(stderr, "[gnm-draw] %s\n", buf);
}

// The registers a draw / dispatch reads after it is handed to the encoder thread: the CP keeps
// changing the live register file meanwhile. Reads outside these ranges are logged.
struct RegSnapRange {
    uint16_t lo, hi;
};
constexpr RegSnapRange kRegSnapRanges[] = {{0xA000, 0xA020}, {0xA100, 0xA190}, {0xA200, 0xA210}, {0xA2D0, 0xA310}};
// index in RegSnap::a of the context registers 0xA000..0xA30F (0xFFFF: not captured)
struct RegSnapIndex {
    uint16_t at[0x310];
    constexpr RegSnapIndex() : at() {
        for (uint32_t i = 0; i < 0x310; ++i) at[i] = 0xFFFF;
        uint32_t o = 0;
        for (const RegSnapRange& g : kRegSnapRanges) {
            for (uint32_t r = g.lo; r < g.hi; ++r) at[r - 0xA000] = (uint16_t)(o + r - g.lo);
            o += g.hi - g.lo;
        }
    }
};
constexpr RegSnapIndex kRegSnapIndex{};

struct RegSnap {
    using Range = RegSnapRange;
    static constexpr const auto& kRanges = kRegSnapRanges;
    static constexpr uint32_t kSize = 0x20 + 0x90 + 0x10 + 0x40;
    uint32_t a[kSize];
    uint32_t cs_num_thread[3]; // COMPUTE_NUM_THREAD_X/Y/Z
    void Capture(const uint32_t* r) {
        uint32_t o = 0;
        for (const Range& g : kRanges) {
            memcpy(a + o, r + g.lo, (g.hi - g.lo) * 4u);
            o += g.hi - g.lo;
        }
        memcpy(cs_num_thread, r + 0x2E07, sizeof(cs_num_thread));
    }
    uint32_t operator[](uint32_t reg) const {
        if (reg - 0xA000u < 0x310u) {
            const uint16_t o = kRegSnapIndex.at[reg - 0xA000u];
            if (o != 0xFFFF) return a[o];
        }
        if (reg >= 0x2E07 && reg < 0x2E0A) return cs_num_thread[reg - 0x2E07];
        RLog("regsnap", "register 0x%x read after the draw left the CP (not in the snapshot); read as 0", reg);
        return 0;
    }
};

// =====================================================================================
// Guest memory dispatch tickets (see "Guest memory dispatches" below). A guest memory dispatch
// encoded without a CPU wait is ticket t: the GPU signals ev = 2t - 1 once it ran, the CPU
// signals 2t once the dispatch's result is final, and work encoded after it waits for 2t. Every
// wait of the CPU for the GPU sleeps on cv, fed by the command buffer completion handlers and
// the ticket listener (state changes are published with GmNotify).
// =====================================================================================
// Callbacks on Metal's completion threads (command buffer completions, shared-event listeners)
// reach this file's static objects. After a crash the backend outlives exit(1) (BackendDelete),
// so its command buffers keep completing while static destruction runs: a late callback locked
// a destroyed std::mutex ("mutex lock failed: Invalid argument", CommitDoneFn). ReleaseBackend
// closes the gate on a crash's way out and waits for the callbacks inside; later ones return at
// once. Atomics only: the gate itself is never destroyed in a way a late callback could see.
// Function-local statics with destructors are heap objects that are never destroyed in this file
// and ps4_gnm_metal.mm (`static T& x = *new T;`): constructed at first use, possibly after the
// exit handlers (ReleaseBackend, StopGfxWatchdog) were registered, they would be destroyed before
// those handlers run, while the queue wait thread, the encoder or a completion still uses them.
struct CompletionGate {
    std::atomic<bool> closed{false};
    std::atomic<int> inside{0};
    bool Enter() {
        inside.fetch_add(1); // seq_cst with closed: either Close sees it, or it sees closed
        if (!closed.load()) return true;
        inside.fetch_sub(1);
        return false;
    }
    void Leave() { inside.fetch_sub(1); }
    // bounded: a callback stuck behind a lock the crashed thread holds must not hang exit
    void Close() {
        closed.store(true);
        for (int i = 0; i < 2000 && inside.load() != 0; ++i) usleep(500);
        if (inside.load() != 0) fprintf(stderr, "[gnm-draw] exit: %d GPU completion callbacks still running\n", inside.load());
    }
};
CompletionGate g_completion_gate;
struct CompletionScope {
    const bool in = g_completion_gate.Enter();
    ~CompletionScope() {
        if (in) g_completion_gate.Leave();
    }
};

struct GmTicket;
struct GmSync {
    id<MTLSharedEvent> ev = nil;
    MTLSharedEventListener* listener = nil;
    std::mutex mu;
    std::condition_variable cv;
    std::mutex signal_mu;           // the CPU's signals of ev
    std::atomic<bool> miss{false};  // a ticket waits for the encoder (GmService)
    std::atomic<bool> released{false}; // the GPU was given up on: every ticket is final
    std::atomic<bool> withhold_next{false}; // tests: TestWithholdNextTicket
    // the CPU's last signal of ev (2t: ticket t final), stored after the signal with release:
    // what happened before a ticket was published (its queued texture invalidations) happens
    // before whatever a thread does after it saw the ticket final (GmResolved, acquire). The GPU
    // only signals 2t - 1, which publishes nothing.
    std::atomic<uint64_t> cpu_signaled{0};
    uint64_t encoded = 0;           // encoder thread: last ticket encoded
    uint64_t committed = 0;         // encoder thread: last ticket in a committed command buffer
    std::vector<std::shared_ptr<GmTicket>> cmd_tickets; // encoder thread: tickets in the open main command buffer
    std::atomic<uint64_t> issued{0}; // CP thread: last ticket handed out
    // encoder thread: fast dispatches are encoded back to back into the open compute encoder as
    // a chain, whose signal and wait are encoded by GmCloseChain, before any other work
    bool chain_open = false;
    bool chain_member = false;  // a chain member is being encoded (its encoders do not close it)
    bool chain_closing = false;
};
GmSync g_gms;

void GmNotify() {
    const CompletionScope gate;
    if (!gate.in) return;
    { std::lock_guard<std::mutex> lk(g_gms.mu); }
    g_gms.cv.notify_all();
}

// Last ticket whose result is final (every ticket while there is no event yet).
uint64_t GmResolved() {
    if (!g_gms.ev || g_gms.released.load(std::memory_order_acquire)) return ~0ull;
    return g_gms.cpu_signaled.load(std::memory_order_acquire) / 2;
}

// Raises ev to v (any thread; never lowers it).
void GmSignal(uint64_t v);
// Encoder state: every unfinished ticket counts as final and skipped (the GPU was given up on).
void GmReleaseAll();
// Encoder state: logs where the guest memory tickets stand (GPU watchdogs).
void GmLogState();

// Encoder thread: re-runs missed tickets and finishes resolved ones (defined below).
void GmService();
// Encoder state: aborts if it is released (encq::Unlocked) inside a guest memory dispatch's encoding.
void GmCheckUnlockable();
// Encoder thread: ends the open chain of fast guest memory dispatches (their signal and the wait
// of everything after them); due before any other work is encoded. EndEncoders, BeginPass and
// ComputeEncoder (but for a chain member) call it: no other encoder can start before.
void GmCloseChain();

// =====================================================================================
// Encoder thread. The CP thread parses the command stream and prepares draws and dispatches
// (render state, shader lookup, resource table walks: work that only reads registers and guest
// memory); everything that touches Metal or the texture cache runs here, in stream order.
// Operations whose result the CP or the guest observes (waits, CPU writes of guest memory,
// presents) wait for the queue to drain. PS4_GNM_SYNC_ENCODE=1 runs everything inline.
// =====================================================================================
namespace encq {
std::mutex mu;
std::condition_variable cv_work, cv_idle;
std::vector<std::function<void()>> q;
bool busy = false, sleeping = false;
std::atomic<uint64_t> pushed{0}, done{0}, busy_ns{0};
// busy_ns split (the perf line): the encoder thread's CPU time while running jobs, and the wall time
// of the blocking calls it makes (EnsureCmd: the command buffer throttle, the backend's in-flight
// limit, opening the command buffer, where Metal 3's queue blocks at its command buffer limit)
std::atomic<uint64_t> cpu_ns{0}, throttle_ns{0}, room_ns{0}, begin_ns{0};
std::atomic<bool> async{false};
thread_local bool t_worker = false;
thread_local bool t_in_job = false; // running encoder work inline (PS4_GNM_SYNC_ENCODE)
constexpr size_t kMaxQueued = 16384;

// The encoder's state (S, g_gmem: everything encoder work touches) belongs to whoever holds
// this lock: the encoder thread while it runs a chunk of jobs, the CP thread while it runs one inline
// (PS4_GNM_SYNC_ENCODE), the guest memory service thread while it re-runs missed dispatches
// (GmServiceThread). Recursive per thread. Released (Unlocked) only while its holder blocks in
// a wait that may need a re-run to end (WaitAll, the command buffer throttle).
std::mutex state_mu;
thread_local int t_depth = 0;
struct Locked {
    Locked() {
        if (t_depth++ == 0) state_mu.lock();
    }
    ~Locked() {
        if (--t_depth == 0) state_mu.unlock();
    }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;
};
// Where it is released mid-operation (Unlocked): WaitAll's and the command buffer throttle's
// waits (PresentImpl does not wait for the display: a frame without a slot is held, see
// docs/present-pacing.md).
// None of them is inside the encoding of a guest memory dispatch (a slot bound as the fault
// buffer, a chain member being encoded): checked (GmCheckUnlockable).
struct Unlocked {
    int depth = t_depth;
    Unlocked() {
        if (depth) GmCheckUnlockable();
        if (depth) {
            t_depth = 0;
            state_mu.unlock();
        }
    }
    ~Unlocked() {
        if (depth) {
            state_mu.lock();
            t_depth = depth;
        }
    }
    Unlocked(const Unlocked&) = delete;
    Unlocked& operator=(const Unlocked&) = delete;
};

inline bool Inline() {
    return t_worker || !async.load(std::memory_order_relaxed);
}

void Worker() {
    t_worker = true;
    pthread_setname_np("GNM Encoder");
    // on the frame's critical path: keep it off the efficiency cores
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    std::vector<std::function<void()>> batch; // swapped with q: both keep their capacity
    for (;;) {
        batch.clear();
        {
            std::unique_lock<std::mutex> lk(mu);
            while (q.empty()) {
                sleeping = true;
                cv_work.wait(lk);
                sleeping = false;
            }
            batch.swap(q);
            busy = true;
        }
        cv_idle.notify_all(); // room for producers waiting on kMaxQueued
        const double t0 = CFAbsoluteTimeGetCurrent();
        const uint64_t c0 = clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID);
        size_t n = 0;
        while (n < batch.size()) {
            @autoreleasepool {
                Locked lk; // free for the service thread between chunks of jobs
                for (size_t k = 0; k < 64 && n < batch.size(); ++k, ++n) {
                    batch[n]();
                    batch[n] = nullptr;
                    if (g_gms.miss.load(std::memory_order_relaxed)) GmService();
                }
            }
        }
        busy_ns.fetch_add((uint64_t)((CFAbsoluteTimeGetCurrent() - t0) * 1e9), std::memory_order_relaxed);
        cpu_ns.fetch_add(clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID) - c0, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(mu);
            done.fetch_add(n, std::memory_order_release);
            if (q.empty()) busy = false;
        }
        cv_idle.notify_all();
    }
}

// Guest memory service thread: re-runs a missed dispatch as soon as the encoder's state is free
// (between chunks of jobs, while the encoder is idle or blocked in WaitAll / the throttle, or, inline,
// while the CP is not encoding), instead of after the encoder's next job (which may be long, or
// not come at all while the CP waits for the guest).
void ServiceLoop() {
    pthread_setname_np("GNM Guest Memory");
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(g_gms.mu);
            g_gms.cv.wait(lk, [] { return g_gms.miss.load(std::memory_order_acquire); });
        }
        Locked lk;
        GmService();
    }
}

void Start() {
    std::thread(ServiceLoop).detach();
    if (getenv("PS4_GNM_SYNC_ENCODE")) {
        fprintf(stderr, "[gnm-draw] encoder thread disabled (PS4_GNM_SYNC_ENCODE)\n");
        return;
    }
    std::thread th(Worker);
    // the encoder is on the frame's critical path like the CP thread
    th.detach();
    async.store(true, std::memory_order_release);
    fprintf(stderr, "[gnm-draw] encoder thread started\n");
}

void Defer(std::function<void()> f) {
    if (Inline()) {
        Locked lk;
        const bool was = t_in_job;
        t_in_job = true;
        f();
        t_in_job = was;
        if (g_gms.miss.load(std::memory_order_relaxed)) GmService();
        return;
    }
    bool wake;
    {
        std::unique_lock<std::mutex> lk(mu);
        if (q.size() >= kMaxQueued) cv_idle.wait(lk, [] { return q.size() < kMaxQueued; });
        q.push_back(std::move(f));
        pushed.fetch_add(1, std::memory_order_relaxed);
        wake = sleeping;
    }
    if (wake) cv_work.notify_one(); // a running worker takes the new work with its next batch
}

// Waits until everything queued so far has run.
void Sync() {
    if (Inline()) return;
    std::unique_lock<std::mutex> lk(mu);
    cv_idle.wait(lk, [] { return q.empty() && !busy; });
}

// Wakes the encoder thread (any thread): it services guest memory tickets after every job.
void Nudge() {
    if (!async.load(std::memory_order_relaxed) || t_worker) return;
    {
        std::lock_guard<std::mutex> lk(mu);
        q.push_back([] {});
        pushed.fetch_add(1, std::memory_order_relaxed);
    }
    cv_work.notify_one();
}

// Runs f on the encoder after the queued work and returns once it ran.
void Run(const std::function<void()>& f) {
    if (Inline()) {
        Locked lk;
        const bool was = t_in_job;
        t_in_job = true;
        f();
        t_in_job = was;
        if (g_gms.miss.load(std::memory_order_relaxed)) GmService();
        return;
    }
    Defer(f);
    Sync();
}

// Work handed to the encoder that has not run yet.
bool Pending() {
    return !Inline() && pushed.load(std::memory_order_relaxed) != done.load(std::memory_order_acquire);
}
} // namespace encq

inline uint64_t Fnv(const void* data, size_t n, uint64_t h = 1469598103934665603ull) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

void TraceVertexStream(uint64_t addr);
inline float AsF(uint32_t v) {
    float f;
    memcpy(&f, &v, 4);
    return f;
}

// =====================================================================================
// Global state
// =====================================================================================
struct UploadArena {
    static const size_t kChunk = 4u << 20;
    struct Chunk {
        id<MTLBuffer> buf;
        size_t used;
    };
    std::vector<Chunk> cur;
    std::mutex free_mu;
    std::vector<id<MTLBuffer>> free_list;
    // residency (Metal 4, set by Init): a chunk made (on_new), dropped by the encoder while no
    // uncommitted work uses it (on_drop), or dropped by CommitDoneFn after the seq that used it
    // completed (on_retired)
    void (*on_new)(id<MTLBuffer>) = nullptr;
    void (*on_drop)(id<MTLBuffer>) = nullptr;
    void (*on_retired)(id<MTLBuffer>, uint64_t seq) = nullptr;

    bool Alloc(id<MTLDevice> dev, size_t size, size_t align, __strong id<MTLBuffer>* out, size_t* off,
               void** ptr) {
        if (size == 0) size = 4;
        if (!cur.empty()) {
            Chunk& c = cur.back();
            size_t o = (c.used + align - 1) & ~(align - 1);
            if (o + size <= c.buf.length) {
                c.used = o + size;
                *out = c.buf;
                *off = o;
                *ptr = (uint8_t*)c.buf.contents + o;
                return true;
            }
        }
        id<MTLBuffer> b = nil;
        if (size <= kChunk) {
            std::lock_guard<std::mutex> lk(free_mu);
            if (!free_list.empty()) {
                b = free_list.back();
                free_list.pop_back();
            }
        }
        if (!b) {
            b = [dev newBufferWithLength:std::max(size, kChunk) options:MTLResourceStorageModeShared];
            if (b && on_new) on_new(b);
        }
        if (!b) return false;
        cur.push_back({b, size});
        *out = b;
        *off = 0;
        *ptr = b.contents;
        return true;
    }
};

struct Shader {
    gcn_shader* sh = nullptr;
    gcn_shader_info info{};
    uint32_t hw = 0;
    uint64_t code_addr = 0;
    uint64_t uid = 0;
    uint64_t msl_hash = 0;    // hash of the final MSL (stable across sessions)
    ps4gnm_lib_store::Key lib_key; // the MSL's key in the library store (lib_key.h1 == msl_hash)
    uint32_t lib_stage = 0;        // ps4gnm_lib_store::Stage of the function, 0: not stored
    bool from_store = false;       // fn was loaded from the library store
    // Translated on the worker thread from the draw's guest memory read later than the draw, so
    // its specialization may describe another draw: never the base of the permutation lookup.
    bool from_worker = false;
    id<MTLFunction> fn = nil;
    bool pending = false;     // Metal library still compiling (fn not yet available)
    // Compute kernel compiling in the background (fn stays nil): the encoder waits for it at the
    // shader's first dispatch (ps4_gnm_kernel_compile.h)
    std::shared_ptr<ps4gnm_kernels::Job> kjob;
    uint64_t fail_key = 0;    // permutation key recorded in shader_fail if compilation fails
    std::vector<gcn_buffer_binding> bufs;
    std::vector<gcn_image_binding> imgs;
    std::vector<gcn_sampler_binding> samps;
    std::vector<gcn_vertex_attrib> attrs;
    std::vector<gcn_image_table> tables; // T# tables indexed at run time (imgs[i].table)
    mutable std::atomic<bool> tables_logged{false}; // their entries were logged at the first binding
    // VS only: the two vertex shaders of RectList draws (ps4_gnm_rectlist.h), made by the first
    // RectList draw that uses this shader. They only carry fn / uid / msl_hash / code_addr.
    // compiling / failed (ps4gnm_rect::LibraryStatus): Metal libraries still compiling (their fn
    // is set before the count drops), and whether one failed.
    struct RectList : ps4gnm_rect::LibraryStatus {
        Shader* capture = nullptr;
        Shader* expand = nullptr;
        uint32_t vertex_stride = 0, capture_slot = 0, params_slot = 0;
    };
    RectList* rect = nullptr;
    bool rect_failed = false;
};

struct PassKey {
    __unsafe_unretained id<MTLTexture> color[8] = {};
    __unsafe_unretained id<MTLTexture> depth = nil;
    uint32_t depth_slice = 0; // DB_DEPTH_VIEW.SLICE_START
    uint32_t width = 0, height = 0;
    uint32_t sample_locs = 0; // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0 of multisampled passes
    bool operator==(const PassKey& o) const {
        for (int i = 0; i < 8; ++i)
            if (color[i] != o.color[i]) return false;
        return depth == o.depth && depth_slice == o.depth_slice && width == o.width && height == o.height &&
               sample_locs == o.sample_locs;
    }
};

struct MetaState {
    bool cleared = false;
    uint64_t cleared_seq = 0; // command buffer the clear (CMASK/HTILE fill) belongs to
    // Bytes the clear wrote from the metadata's base (kAllMeta: not known, all of it), and the
    // slices whose draws already took the clear. A metadata surface holds every slice of an
    // array: one clear of an HTILE of 3 slices clears all 3, each taken by its first draw.
    uint64_t cleared_bytes = 0;
    uint64_t taken_slices = 0;
};
constexpr uint64_t kAllMeta = ~0ull;

// A clear arms the metadata at its base for every slice it covers. A second clear at the same
// base before all covered slices were drawn resets the pending slices: the later clear wins, as
// it rewrites the same HTILE/CMASK bytes on the GPU.
void ArmMetaClear(MetaState& m, uint64_t bytes, uint64_t seq) {
    m.cleared = true;
    m.cleared_seq = seq;
    m.cleared_bytes = bytes;
    m.taken_slices = 0;
}

// Compact record of the work encoded into a command buffer, printed when it fails or hangs
// (formatting text for every draw would cost more than encoding the draw).
struct WorkRec {
    const char* what = "";
    uint64_t a = 0, b = 0;     // draw: VS / PS address; dispatch: CS address
    uint32_t n = 0, inst = 0;  // draw: count / instances; dispatch: groups x / y
    uint32_t extra = 0;        // dispatch: groups z
    bool dispatch = false;
};
struct WorkLog {
    // The most recent records are kept: 64, or 512 with the debug options that print whole
    // command buffers (PS4_GNM_ENCODER_STATUS, PS4_GNM_HANG_REPORT_MS, PS4_GNM_COMMIT_EACH).
    static constexpr size_t kAll = ~size_t(0); // Print(): every record kept
    static size_t Capacity() {
        static const size_t cap = getenv("PS4_GNM_ENCODER_STATUS") || getenv("PS4_GNM_HANG_REPORT_MS") ||
                                          getenv("PS4_GNM_COMMIT_EACH") ? 512 : 64;
        return cap;
    }
    std::vector<WorkRec> rec = std::vector<WorkRec>(Capacity());
    uint64_t count = 0;
    void Add(const WorkRec& r) { rec[count++ % rec.size()] = r; }
    void Print(const char* prefix, size_t last) const {
        const uint64_t kCap = rec.size();
        const uint64_t first = count > std::min<uint64_t>(last, kCap) ? count - std::min<uint64_t>(last, kCap) : 0;
        for (uint64_t i = first; i < count; ++i) {
            const WorkRec& r = rec[i % kCap];
            if (r.dispatch)
                fprintf(stderr, "[gnm-draw]   %s[%llu]: %s dispatch CS=0x%llx groups=%ux%ux%u\n", prefix,
                        (unsigned long long)i, r.what, (unsigned long long)r.a, r.n, r.inst, r.extra);
            else if (r.a)
                fprintf(stderr, "[gnm-draw]   %s[%llu]: %s VS=0x%llx PS=0x%llx n=%u inst=%u\n", prefix,
                        (unsigned long long)i, r.what, (unsigned long long)r.a, (unsigned long long)r.b, r.n, r.inst);
            else
                fprintf(stderr, "[gnm-draw]   %s[%llu]: %s\n", prefix, (unsigned long long)i, r.what);
        }
    }
};

// Guest memory written by GPU work; epochs order the writes against GPU texture uploads.
using GpuWrite = TextureCache::WriteRange;

// A no-copy MTLBuffer over guest windows and the time it was last bound (g_mem.tick).
struct AliasState;
struct GuestWin {
    id<MTLBuffer> buf = nil;
    uint64_t last_use = 0;
    std::shared_ptr<AliasState> alias; // Metal 3: the accesses through it (FlushAliasOrders)
};

struct InflightLog {
    uint64_t seq, cmd_index;
    std::shared_ptr<WorkLog> log;
};

// Deletes the backend, except after a crash (ps4_host_crashing): its destructor waits for the
// GPU's completions, which a crashed process must not do on its way out (static destruction
// after the crash handler's exit(1)); the process ends with it alive.
struct BackendDelete {
    void operator()(GnmBackend* b) const {
        if (!ps4_host_crashing()) delete b;
    }
};

struct State {
    std::unique_ptr<GnmBackend, BackendDelete> be; // the backend chosen by Init (ps4_gnm_backend.h)
    uint64_t cmd_index = 0;         // label number of the open main command buffer ("gnm-<n>")
    bool init_tried = false;
    // the renderer runs: set by Init after everything is set up (release), cleared by ReleaseBackend
    // before the backend goes (release); read by every thread (acquire: a reader that sees true
    // sees what Init set up)
    std::atomic<bool> enabled{false};
    std::atomic<bool> hung{false}; // set by any thread (GPU watchdogs), read by all
    GuestContext* ctx = nullptr;
    id<MTLDevice> dev = nil;
    TextureCache* tex = nullptr;
    gcn_profile profile{};
    gcn_msl_options msl{};
    id<MTLBuffer> dummy = nil;
    id<MTLBuffer> gds_buf = nil;
    uint8_t* gds = nullptr;

    // recording
    PassKey pass;
    id<MTLTexture> pass_hold[9]; // keeps the textures of `pass` alive
    std::shared_ptr<WorkLog> cmd_log;
    std::vector<ps4_metal_screen_present_op> present_ops; // present signals in the open command buffer
    std::vector<uint32_t> flat_scratch[3]; // per-stage flattened user data (VS, PS, CS)
    // flat_scratch[i] already holds this draw's flattened user data, built by GetShader with
    // the SRT program of the shader recorded here (all permutations of a program share it)
    const Shader* flat_from[3] = {};
    bool pending = false;
    UploadArena arena;
    std::vector<GpuWrite> gpu_written; // guest ranges written by work in `cmd`
    uint32_t gpu_written_slot[64] = {}; // NoteGpuWrite: index of a range in gpu_written, by hash
    // ranges written by committed command buffers, by sequence number (dropped once completed)
    std::deque<std::pair<uint64_t, std::vector<GpuWrite>>> inflight_written;
    // gpu_written and inflight_written by range, latest write only: what OverlapsPendingWrite asks
    GnmPendingWrites pending_writes;
    // The same, kept for the T# table hazard check (ResolveTableHazards) until no draw or dispatch
    // whose tables the CP copied before they completed is left to encode (PruneTableWrites).
    std::deque<std::pair<uint64_t, std::shared_ptr<const std::vector<GpuWrite>>>> table_written;
    // completed_seq when the CP copied the tables of the draw or dispatch encoded last
    // (TableReadEncoded): every one still to be encoded copied them later
    uint64_t table_floor = 0;
    uint64_t write_epoch = 0; // last epoch handed out by NoteGpuWrite
    // exactly known buffer stores of the draw or dispatch being bound (NoteBufferStore), handed to
    // the texture cache once its work is really encoded (FlushBufferStores)
    std::vector<std::pair<uint64_t, uint64_t>> buffer_stores;
    // first write epoch of the work recorded since the last Commit (the main command buffer): copies in the upload one
    // run before all of it, so they are current only for writes with smaller epochs
    uint64_t cmd_start_epoch = 1;
    // GPU texture uploads: detile maps and staging buffers per surface layout
    id<MTLComputePipelineState> detile_pso = nil;
    std::unordered_map<uint64_t, id<MTLBuffer>> detile_maps;
    std::unordered_map<uint32_t, id<MTLBuffer>> detile_staging; // by size class (log2 of the length)

    // caches
    std::unordered_map<uint64_t, std::vector<Shader*>> shaders;
    std::unordered_set<uint64_t> shader_fail;
    std::unordered_set<uint64_t> async_retry; // fail_keys whose worker translation failed: retried on the CP thread
    std::unordered_map<uint64_t, id<MTLLibrary>> libs;
    // Asynchronous shader/pipeline compilation: Metal completion handlers push results here and
    // the submitting thread applies them (DrainAsyncCompiles) before each draw/dispatch.
    struct AsyncLib { uint64_t mh; id<MTLLibrary> lib; NSString* err; };
    // retry_vs/retry_ps: the pipeline failed with a function from the library store; the encoder
    // compiles that shader's MSL and builds the pipeline again instead of keeping the failure.
    struct AsyncPso {
        std::string key;
        id<MTLRenderPipelineState> pso;
        NSString* err;
        const Shader* retry_vs = nullptr;
        const Shader* retry_ps = nullptr;
    };
    std::mutex async_mu;
    std::vector<AsyncLib> async_libs_done;
    std::vector<AsyncPso> async_psos_done;
    // results waiting in async_*_done (checked without taking async_mu on every draw)
    std::atomic<uint32_t> async_shaders_ready{0}, async_psos_ready{0};
    std::unordered_map<uint64_t, std::vector<Shader*>> libs_pending;   // mh -> waiting shaders
    std::unordered_set<std::string> psos_pending;
    struct GcnJob {
        gcn_hw_stage hw;
        gcn_sw_stage sw;
        gcn_runtime_info rt;
        uint64_t code_addr = 0, key = 0, fail_key = 0, pend_key = 0;
        uint32_t ud[16];
        gcn_shader* sh = nullptr;
        std::string err;
        int status = 0;
        bool cache_store = false; // store the translation in the on-disk cache under cache_key
        ps4gnm_shader_cache::Key cache_key;
    };
    std::vector<std::shared_ptr<GcnJob>> async_gcn_done;
    std::unordered_set<uint64_t> gcn_pending;
    // programs (GetShader key) with a worker translation in flight: one per program, see GetShader
    std::unordered_set<uint64_t> gcn_pending_prog;
    Shader pending_shader; // returned while a translation is in flight (fn == nil)
    // Shaders made ready ahead of time by the warm-up threads, by MSL hash: functions loaded from
    // the library store (fn) or libraries compiled from the on-disk MSL cache (lib). h2 is the
    // second hash of the store key (0 for a compiled library, whose source hash is h1 itself).
    struct Prewarmed {
        uint64_t h2 = 0;
        id<MTLLibrary> lib = nil;
        id<MTLFunction> fn = nil;
    };
    std::unordered_map<uint64_t, Prewarmed> prewarmed;
    // MSL hashes the CP thread resolved without the warm-up: the warm-up skips them (async_mu).
    std::unordered_set<uint64_t> prewarm_taken;
    // Functions loaded from the library store, by MSL hash (CP thread).
    std::unordered_map<uint64_t, id<MTLFunction>> store_fns;
    // Functions compiled from MSL for shaders whose stored function failed in a pipeline, by MSL
    // hash (encoder thread only: pipelines are built there; see PipelineFn).
    std::unordered_map<uint64_t, id<MTLFunction>> fallback_fns;
    // Pipelines built in earlier sessions (hashes in <cache>/pipelines.bin): Metal's own
    // pipeline cache makes rebuilding them cheap, so they are built synchronously.
    std::unordered_set<uint64_t> known_psos;
    std::string pso_list_path;
    std::unordered_set<uint64_t> msl_on_disk; // hashes of <msl_cache_dir>/<hash>.metal (CP thread)
    std::string msl_cache_dir;
    bool async_compile = true;
    std::unordered_map<std::string, id<MTLRenderPipelineState>> gfx_pipes;
    std::unordered_map<uint64_t, id<MTLComputePipelineState>> cs_pipes;
    // compute kernels compiled in the background (null: on the CP thread, PS4_GNM_SYNC_KERNELS=1)
    std::unique_ptr<ps4gnm_kernels::Compiler> kernels;
    // kernels of dispatches handed to the encoder in the current command stream run that were
    // still compiling (CP thread; WaitKernelCompiles)
    std::vector<std::shared_ptr<ps4gnm_kernels::Job>> kernels_open;
    std::unordered_map<uint64_t, id<MTLDepthStencilState>> ds_states;
    std::unordered_map<uint64_t, id<MTLSamplerState>> samplers;
    std::unordered_map<uint64_t, GuestWin> guest_bufs; // key: window base | windows << 48
    std::unordered_map<uint64_t, MetaState> meta; // cmask / htile address -> state
    uint64_t next_uid = 1;
    // textures with mip chains whose levels were rendered as separate render targets
    struct MipChain {
        id<MTLTexture> tex;
        uint64_t built_submit = ~0ull;
    };
    std::unordered_map<uint64_t, MipChain> mip_chains; // key: hash of T# identity
    uint32_t cb_extent[8] = {};
    uint32_t db_extent = 0;

    // depth surfaces read as color (R32F companions) and depth-copy tracking. The maps keyed by a
    // texture pointer here (depth_comp, depth_version, depth_last_write, eqaa_twin, eqaa_orig) are
    // erased by ForgetTexture when the texture cache drops the texture: add any new one there.
    struct Companion {
        id<MTLTexture> tex;
        uint64_t version = ~0ull;
    };
    std::unordered_map<void*, Companion> depth_comp;
    std::unordered_map<void*, uint64_t> depth_version;
    // order of the last depth write per depth texture (for copies between sample counts)
    // EQAA emulation: multisampled stand-ins ("twins") of single-sample targets, and the twins
    // of the open render pass that are written back when it ends
    std::unordered_map<void*, id<MTLTexture>> eqaa_twin;     // target -> twin
    std::unordered_map<void*, id<MTLTexture>> eqaa_orig;     // twin -> target
    std::vector<std::pair<id<MTLTexture>, id<MTLTexture>>> eqaa_open; // (twin, target)
    std::unordered_map<uint64_t, id<MTLRenderPipelineState>> eqaa_psos;
    // storage images bound by the current dispatch (address, texture)
    std::vector<std::pair<uint64_t, id<MTLTexture>>> storage_bound;
    std::unordered_map<uint32_t, id<MTLRenderPipelineState>> depth_from_color_psos;
    std::unordered_map<void*, uint64_t> depth_last_write;
    uint64_t depth_write_clock = 0;
    std::unordered_map<uint64_t, id<MTLRenderPipelineState>> depth_copy_psos;
    id<MTLDepthStencilState> depth_copy_dss = nil;
    id<MTLLibrary> util_lib = nil;
    std::unordered_map<uint64_t, uint64_t> stencil_to_z;
    id<MTLRenderPipelineState> depth2color_pso = nil;

    // present
    id<MTLRenderPipelineState> present_pso = nil;
    id<MTLTexture> present_tex = nil;
    // display buffers that are not GPU targets, copied from guest memory (GuestDisplayTexture)
    id<MTLTexture> guest_display_tex = nil;
    CompMapping guest_display_swizzle;

    // stats
    uint64_t submit_idx = 0;
    uint64_t present_idx = 0;    // presents so far
    bool trace_by_present = false;
    uint64_t trace_start = 0, trace_count = 0;
    std::deque<InflightLog> inflight_logs; // committed, not known complete (pruned at commits, cleared by WaitAll)
    uint64_t n_draws = 0, n_dispatches = 0, n_cmdbufs = 0;
    // Draws and dispatches skipped (both threads, see EncodeQueue; relaxed), in all and by reason
    // (NoteSkip). skipped_by[PendingCompile] is the part deferred because a shader or pipeline
    // they need is still compiling in the background (a cold cache effect: the next frames draw
    // them); the rest is work really lost.
    std::atomic<uint64_t> n_skipped{0};
    std::atomic<uint64_t> skipped_by[(size_t)SkipReason::Count] = {};
    // draws skipped by PS4_GNM_MTL4_ONLY_CLEARS after their pass began (not in n_skipped); encoder
    // thread, read by OnlyClearsSkippedCount
    std::atomic<uint64_t> n_clear_only{0};
    std::atomic<uint64_t> n_bind_uploads{0}; // GPU texture uploads encoded while binding (SampledTexture)
    double gpu_timeout_ms = 4000;
    uint64_t n_waits = 0, n_commits = 0;
    std::atomic<uint64_t> n_waits_total{0}; // CPU waits for the GPU (never reset; read by any thread)
    // GPU-timeline fence actions
    std::mutex fence_mu;
    std::recursive_mutex fence_run_mu;
    std::deque<std::pair<uint64_t, std::function<void()>>> fence_q;
    uint64_t committed_seq = 0;            // guarded by fence_mu
    uint64_t completed_seq = 0;            // guarded by fence_mu
    std::atomic<uint64_t> completed_pub{0}; // completed_seq, for readers without fence_mu (TableReadBegin)
    double wait_ms = 0;
    std::atomic<uint64_t> gpu_ns{0}; // GPU busy time (union of command buffer execution intervals)
    std::mutex gpu_time_mu;
    double gpu_last_end = 0;
    uint64_t n_passes = 0, n_depth_loads = 0, n_cencs = 0, n_cbarriers = 0, n_cdisp = 0; // per stats period
    uint64_t cencs_total = 0, cbarriers_total = 0; // since Init (GetEncodeCounts)
    double screen_ms = 0; // time spent handing frames to the window (drawable acquisition)
};

State S;

// A draw or dispatch skipped: counted in all and for its reason (State::n_skipped).
inline void NoteSkip(SkipReason r) {
    S.n_skipped.fetch_add(1, std::memory_order_relaxed);
    S.skipped_by[(size_t)r].fetch_add(1, std::memory_order_relaxed);
}
inline uint64_t SkippedFor(SkipReason r) { return S.skipped_by[(size_t)r].load(std::memory_order_relaxed); }
// A draw deferred because a shader or pipeline it needs is still compiling (State::n_skipped).
inline void NoteDrawPendingCompile() { NoteSkip(SkipReason::PendingCompile); }
// True while the background translation or compile of `s` is in flight (not once it failed).
inline bool ShaderCompiling(const Shader* s) { return s == &S.pending_shader || s->pending; }
// S.be for the hooks that run off the encoder thread (CommitDoneFn's arena recycling on a Metal
// completion thread, the presenter's texture hooks): set by Init, cleared by ReleaseBackend before
// it resets S.be. A hook that loaded it before the clear still finds the backend alive: its
// destructor first waits for every completion handler (and the presenter's hooks are unset first).
std::atomic<GnmBackend*> g_hook_be{nullptr};

// PS4_GNM_MTL4_ONLY_CLEARS=1 on the Metal 4 backend (debug, set by Init): draws are skipped once
// their render pass has begun, so the pass's clears and loads happen and the draw does not.
bool g_only_clears = false;
// The backend is Metal 4 (set by Init with the backend; read by completion handlers, which must not
// touch S.be: see g_hook_be)
std::atomic<bool> g_backend_metal4{false};
// PS4_GNM_ENCODER_STATUS=1 on the Metal 3 backend (set by Init; see EncoderStatusEnabled)
bool g_encoder_status = false;

// The backend runs the renderer's work of these kinds (ps4_gnm_backend.h, GnmCap*). Metal 4 lacks
// some until the renderer keeps everything that work touches resident; it runs only with
// PS4_GNM_MTL4_ONLY_CLEARS=1, and the work it lacks is skipped (counted, logged under `what`) or
// done on the CPU. The present's copy into the presenter's slot is not gated: both textures are
// resident (texture hooks), and without it no frame reaches the screen.
bool BackendHas(uint32_t caps, const char* what = nullptr) {
    if ((S.be->Caps() & caps) == caps) return true;
    if (what)
        RLog(what, "%s: the %s backend lacks it (PS4_GNM_MTL4_ONLY_CLEARS); not done on the GPU", what,
             BackendName(S.be->Kind()));
    return false;
}


// Frame pacing diagnostics: CPU time spent in operations that can stall a frame,
// accumulated between presents and reported for long frames (hitches).
struct HitchStats {
    double shader_ms = 0, gcn_ms = 0, mtl_ms = 0, pso_ms = 0, upload_ms = 0, wait_ms = 0;
    double lookup_ms = 0, fn_ms = 0; // translation cache lookup; MTLFunction creation
    // blocked on background kernel compiles: the CP at the end of a submission, the encoder at a
    // dispatch (both may wait for the same compile at the same time)
    double kernel_wait_ms = 0, kernel_enc_wait_ms = 0;
    uint32_t kernel_waits = 0;
    uint32_t shaders = 0, psos = 0, uploads = 0, gpu_uploads = 0, pending_draws = 0;
};
HitchStats g_hitch;

// Session totals of Metal compile work by where it ran (printed with the frame-time lines):
// synchronous work runs on the CP or encoder thread inside a frame and is what hitches.
struct CompileCounter {
    std::atomic<uint64_t> n{0}, us{0}, max_us{0}, over8{0};
    void Add(double ms) {
        const uint64_t u = (uint64_t)(ms * 1000.0);
        n.fetch_add(1, std::memory_order_relaxed);
        us.fetch_add(u, std::memory_order_relaxed);
        if (u > 8000) over8.fetch_add(1, std::memory_order_relaxed);
        for (uint64_t m = max_us.load(std::memory_order_relaxed);
             u > m && !max_us.compare_exchange_weak(m, u, std::memory_order_relaxed);) {
        }
    }
};
struct CompileStats {
    CompileCounter lib_sync, lib_store, pso_sync, cpso_sync;
    // dispatches whose kernel was still compiling when the encoder reached them (time blocked)
    CompileCounter kernel_wait;
    // command stream runs whose end waited for kernels still compiling (time blocked, CP thread)
    CompileCounter kernel_cp_wait;
    std::atomic<uint64_t> kernel_ready{0}; // first dispatches whose kernel was compiled in time
    std::atomic<uint64_t> lib_async{0}, lib_prewarmed{0}, pso_async{0}, store_fallbacks{0};
};
CompileStats g_compile;

void PrintCompileStats() {
    auto c = [](const CompileCounter& k, char* out, size_t n) {
        snprintf(out, n, "%llu (%.0f ms, max %.1f ms, %llu over 8 ms)", (unsigned long long)k.n.load(),
                 k.us.load() / 1000.0, k.max_us.load() / 1000.0, (unsigned long long)k.over8.load());
    };
    char a[96], b[96], d[96], l[96];
    c(g_compile.lib_sync, a, sizeof(a));
    c(g_compile.lib_store, l, sizeof(l));
    c(g_compile.pso_sync, b, sizeof(b));
    c(g_compile.cpso_sync, d, sizeof(d));
    fprintf(stderr, "[gnm-compile] session: libraries in frame %s, loaded from the store in frame %s, in background %llu, "
                    "from warm-up %llu; render pipelines in frame %s, in background %llu; compute pipelines in frame %s; "
                    "stored libraries replaced after a pipeline failure %llu\n",
            a, l, (unsigned long long)g_compile.lib_async.load(), (unsigned long long)g_compile.lib_prewarmed.load(), b,
            (unsigned long long)g_compile.pso_async.load(), d, (unsigned long long)g_compile.store_fallbacks.load());
    if (S.kernels) {
        char w[96], cw[96];
        c(g_compile.kernel_wait, w, sizeof(w));
        c(g_compile.kernel_cp_wait, cw, sizeof(cw));
        using KC = ps4gnm_kernels::Compiler;
        fprintf(stderr, "[gnm-compile] session: compute kernels compiled in the background %llu (%.0f ms, %llu failed), "
                        "%llu ready at their first dispatch, waited for by the encoder %s, by the CP at the end of a "
                        "submission %s; draw shaders through the same queue %llu (%.0f ms, %llu failed); %llu compiles "
                        "at once at most\n",
                (unsigned long long)S.kernels->Started(KC::kDispatch), S.kernels->CompileMs(KC::kDispatch),
                (unsigned long long)S.kernels->Failed(KC::kDispatch), (unsigned long long)g_compile.kernel_ready.load(), w,
                cw, (unsigned long long)S.kernels->Started(KC::kDraw), S.kernels->CompileMs(KC::kDraw),
                (unsigned long long)S.kernels->Failed(KC::kDraw), (unsigned long long)S.kernels->PeakRunning());
    }
    ps4gnm_lib_store::PrintStats();
}

bool g_trace_reads = false;
bool ReadGuest(void* c, uint64_t addr, void* dst, uint64_t size) {
    GuestContext* ctx = (GuestContext*)c;
    if (!ctx || !ctx->mem_base || addr == 0 || addr >= ctx->mem_size || size > ctx->mem_size - addr)
        return false;
    memcpy(dst, ctx->mem_base + addr, size);
    if (g_trace_reads) fprintf(stderr, "[gnm-trace]      read 0x%llx +%llu\n", (unsigned long long)addr, (unsigned long long)size);
    return true;
}

using GmPages = std::vector<uint32_t>; // sorted page numbers
// The textures a dispatch bound (Metal slot, texture), for a re-run that must bind the same ones.
using GmTextures = std::vector<std::pair<uint32_t, id<MTLTexture>>>;

// Counters (since the start; the perf line prints the change per frame).
struct GmStats {
    std::atomic<uint64_t> sync{0}, fast{0}, reruns{0}, refused{0}, mispredicted{0};
    std::atomic<uint64_t> cp_waits{0}, cp_wait_us{0};
    std::atomic<uint64_t> chains{0};          // signal-and-wait points of fast dispatches
    // from a miss's verdict (the listener) to the start of the first re-run it made due: the
    // time a re-run waits for someone to run it; summed, and the longest (reset by the perf line)
    std::atomic<uint64_t> rerun_wait_us{0};
    std::atomic<uint64_t> rerun_wait_max_us{0};
    // from that verdict to the end of the last re-run it made due (a chain's members after the
    // missed one re-run too, one after the other): the longest (reset by the perf line)
    std::atomic<uint64_t> rerun_chain_max_us{0};
    std::atomic<uint64_t> no_slot{0};         // dispatches without a fault buffer (skipped)
    std::atomic<uint64_t> draw_mapped{0};     // 64 MiB pages mapped after draws met them unmapped
    std::atomic<uint64_t> draw_harvests{0};   // harvests of the draws' fault slot (GmDrawSlot)
    std::atomic<uint64_t> draw_unmappable{0}; // tries to map a page for draws that failed (GmMapPages)
    std::atomic<uint64_t> no_indirect{0};     // passes whose indirect arguments had no buffer (skipped)
    std::atomic<uint64_t> pass_failed{0};     // passes whose binding failed after their probe ran
} g_gm_stats;

// ---- Sampled textures over guest memory a fast dispatch's pass wrote outside its prediction ----
// (its predicted pages were noted as pending GPU writes of its command buffer when it was
// encoded). Found once its result is known, before the result is published (2t): by the ticket
// listener or the re-run. They must not take the texture cache's lock there (it is held while
// the cache waits for the GPU, which may be waiting for that very 2t), so the ranges are queued
// and the encoder applies them before it looks up any sampled texture (GmApplyInvalidations):
// work ordered after the dispatch is encoded only after its 2t.
// The exact buffer stores of a fast dispatch's pass (NoteBufferStore) are queued the same way once
// the pass is known to have run, and handed to the texture cache there (NoteBufferWrite): the
// cache's guest-newer marks then come from work that really ran, as on the sync path.
struct GmInvalidations {
    std::mutex mu; // held only to append or take the lists
    std::vector<GpuWrite> ranges;
    std::vector<std::pair<uint64_t, uint64_t>> stores;
    std::atomic<bool> any{false};
} g_gm_inval;

struct GmResult;
void GmInvalidateOutside(const GmResult& res, const GmPages* pred);

// Encoder: applies the queued invalidations. The ranges of a ticket are queued (any = true,
// under mu) before its result is published (GmSignal, release); work ordered after the ticket is
// encoded after its CP saw the ticket final (GmResolved, acquire) and handed the work over
// through encq's queue: so this load sees them. A load that misses ranges queued later only
// misses tickets the work is not ordered after.
void GmApplyInvalidations() {
    if (!g_gm_inval.any.load(std::memory_order_acquire) || !S.tex) return;
    thread_local std::vector<GpuWrite> w;
    thread_local std::vector<std::pair<uint64_t, uint64_t>> st;
    {
        std::lock_guard<std::mutex> lk(g_gm_inval.mu);
        w.swap(g_gm_inval.ranges);
        st.swap(g_gm_inval.stores);
        g_gm_inval.any.store(false, std::memory_order_relaxed);
    }
    if (!w.empty()) S.tex->InvalidateWrites(w);
    for (const auto& [a, n] : st) S.tex->NoteBufferWrite(a, n, S.write_epoch);
    w.clear();
    st.clear();
}

// Queues the exact buffer stores of a fast dispatch's pass that ran (any thread; never blocks on
// the texture cache), before its result is published.
void GmQueueStores(const std::vector<std::pair<uint64_t, uint64_t>>& stores) {
    if (stores.empty()) return;
    std::lock_guard<std::mutex> lk(g_gm_inval.mu);
    g_gm_inval.stores.insert(g_gm_inval.stores.end(), stores.begin(), stores.end());
    g_gm_inval.any.store(true, std::memory_order_release);
}

// ---- CP thread: ordering of the CP's reads after fast guest memory dispatches ----
// The CP reads guest memory for later draws and dispatches (resource table walks, the choice of
// a shader permutation, vertex fetch sharps) and the encoder uploads sampled textures from it.
// On GCN all of that is read on the GPU, after the dispatches the queue synchronized with. Here
// it is read on the CPU when the work is prepared, and what it read is encoded: a later re-run
// cannot undo it. So once a synchronization (ComputeBarrier) made pending tickets visible to a
// queue, the first draw or dispatch prepared for that queue that reads guest memory on the CPU
// waits for all of them, whatever pages they predicted: a pass can write pages outside its
// prediction (its probe could not know them), and its result is known only once it ran.
// Without a synchronization the two are not ordered on GCN either, and the CP goes on.
struct GmCpState {
    std::deque<uint64_t> pending; // tickets handed out and not yet final, oldest first
    uint64_t visible[2] = {};     // per queue (kQueueGfx / kQueueCompute): tickets it waits for
    int queue = kQueueGfx;        // the queue of the draw / dispatch being prepared
} g_gm_cp;

// CP thread: the queue synchronizes (ComputeBarrier).
void GmCpSync(int queue) {
    g_gm_cp.visible[queue] = g_gms.issued.load(std::memory_order_relaxed);
}

// CP thread: the draw / dispatch prepared next is for this queue.
void GmCpQueue(int queue) {
    g_gm_cp.queue = queue;
}

void GmCpPrune() {
    auto& c = g_gm_cp;
    if (c.pending.empty()) return;
    const uint64_t done = GmResolved();
    while (!c.pending.empty() && c.pending.front() <= done) c.pending.pop_front();
}

// The renderer gives up on the GPU (any thread): every ticket counts as final (and skipped), the
// waits for them end and the existing hang handling (S.hung) takes over.
void GmGiveUp(const char* why) {
    fprintf(stderr, "[gnm-draw] GPU watchdog: %s; disabling GPU rendering\n", why);
    S.hung = true;
    ps4_metal_screen_release_held(); // as in WaitAll's watchdog: held frames' flips complete
    g_gms.released.store(true, std::memory_order_release);
    if (g_gms.ev) GmSignal(1ull << 62);
    GmNotify();
}

// CP thread: waits until ticket t's result is final. Gives up on the GPU (GmGiveUp) when no
// ticket becomes final within the GPU timeout.
void GmCpWait(uint64_t t) {
    const double t0 = CFAbsoluteTimeGetCurrent();
    g_gm_stats.cp_waits++;
    Flush(false); // the dispatch may still be in the open command buffer
    const auto timeout = std::chrono::duration<double, std::milli>(S.gpu_timeout_ms);
    uint64_t seen = GmResolved();
    auto deadline = std::chrono::steady_clock::now() + timeout;
    // re-runs due are made by the service thread (and the encoder); inline (no encoder thread)
    // this thread services them too while it waits
    const bool inline_enc = encq::Inline();
    while (GmResolved() < t && !S.hung) {
        if (inline_enc) {
            encq::Locked lk;
            GmService();
        }
        {
            std::unique_lock<std::mutex> lk(g_gms.mu);
            g_gms.cv.wait_until(lk, deadline, [&] {
                return GmResolved() >= t || (inline_enc && g_gms.miss.load(std::memory_order_acquire)) || S.hung;
            });
        }
        const uint64_t now = GmResolved();
        if (now != seen) {
            seen = now;
            deadline = std::chrono::steady_clock::now() + timeout;
        } else if (now < t && std::chrono::steady_clock::now() >= deadline) {
            char why[160];
            snprintf(why, sizeof(why), "the CP waited %.0f ms for guest memory dispatch %llu (final up to %llu)",
                     S.gpu_timeout_ms, (unsigned long long)t, (unsigned long long)now);
            GmGiveUp(why);
        }
    }
    g_gm_stats.cp_wait_us += (uint64_t)((CFAbsoluteTimeGetCurrent() - t0) * 1e6);
    GmCpPrune();
}

// CP thread: the draw / dispatch being prepared reads guest memory on the CPU (or has the encoder
// read it: sampled textures). Waits for the tickets visible to its queue (see GmCpState).
void GmCpOrder() {
    auto& c = g_gm_cp;
    if (encq::t_worker || encq::t_in_job) return; // encoder work: ordered by the encoder
    GmCpPrune();
    if (c.pending.empty() || c.pending.front() > c.visible[c.queue]) return;
    uint64_t t = 0; // the newest pending ticket visible to the queue (they become final in order)
    for (auto it = c.pending.rbegin(); it != c.pending.rend(); ++it)
        if (*it <= c.visible[c.queue]) {
            t = *it;
            break;
        }
    GmCpWait(t);
}

// CP thread: runs f(reader) with a reader of guest memory, ordered after the tickets the queue
// synchronized with.
template <class F>
auto GmCpGuarded(F&& f) {
    GmCpOrder();
    return f(&ReadGuest);
}


// gcn_set_image_address_check: false only for an address that is provably unmapped (in a free
// extent or an untouched reservation, recomp_vm_unmapped_ranges, or past guest memory). The check
// runs for every table entry of every specialization and binding, on the CP and encoder threads:
// each thread keeps its own copy of the unmapped ranges and refreshes it only when the VM
// generation changed, so a query is an atomic load and a binary search, without vm_mutex.
struct UnmappedRanges {
    uint64_t gen = 0;
    std::vector<std::array<uint64_t, 2>> r; // sorted, disjoint [start, end)
};
bool GuestAddressMapped(void* c, uint64_t addr) {
    GuestContext* ctx = (GuestContext*)c;
    if (!ctx || addr == 0 || addr >= ctx->mem_size) return false;
    static thread_local UnmappedRanges u;
    const uint64_t gen = recomp_vm_generation();
    if (gen != u.gen) {
        for (;;) {
            uint64_t got = 0;
            const size_t n = recomp_vm_unmapped_ranges(ctx, reinterpret_cast<uint64_t(*)[2]>(u.r.data()), u.r.size(), &got);
            if (n <= u.r.size()) {
                u.r.resize(n);
                u.gen = got;
                break;
            }
            u.r.resize(n + 16); // grew since the count: ask again
        }
    }
    auto it = std::upper_bound(u.r.begin(), u.r.end(), addr,
                               [](uint64_t a, const std::array<uint64_t, 2>& x) { return a < x[0]; });
    return it == u.r.begin() || addr >= (it - 1)->at(1);
}

// =====================================================================================
// Guest memory as MTLBuffers (UMA, zero copy). Guest memory is one 512 GiB anonymous
// mapping, far larger than a Metal buffer may be, so it is exposed in 64 MiB aligned windows
// (created lazily with newBufferWithBytesNoCopy). A range crossing windows gets a buffer
// spanning all windows it touches. Window 0 starts at PS4_GNM_WINDOW0_BASE, above the guest
// NULL page area (ps4_gnm_guest_window.h).
// =====================================================================================
const uint64_t kWindow = PS4_GNM_WINDOW_SIZE;

// Guest ranges whose host pages were replaced (direct memory remapped/unmapped by the
// kernel layer). A no-copy MTLBuffer keeps referencing the pages it was created over,
// so windows overlapping such a range are dropped and re-created on next use.
std::mutex g_remap_mutex;
std::vector<std::pair<uint64_t, uint64_t>> g_remapped;
std::atomic<bool> g_remap_pending{false};

// The windows looked up last: most draws hit the same few.
struct GuestBufMru {
    uint64_t key = ~0ull;
    GuestWin* win = nullptr; // into S.guest_bufs (the MRU is reset whenever entries are erased)
};
GuestBufMru g_guest_buf_mru[4];

// GPU memory telemetry and budget (ps4_gnm_membudget.h, docs/gpu-memory-budget.md). A sampler
// thread measures wired memory, the process footprint, MTLDevice.currentAllocatedSize and the
// live no-copy guest buffers once a second and keeps the budget state; it runs apart from the
// renderer so it keeps reporting while the GPU or the encoder is stuck. PS4_GNM_MEM_LOG=<seconds>:
// one "[gnm-mem]" line per period (default 10, 0 = off) and whenever a value grew by 256 MiB.
// The encoder thread releases idle windows (and, over budget, every window not bound in the last
// 250 ms plus the transient staging caches) in SweepGuestWindows.
struct GpuMem {
    ps4_gpumem_policy policy{};
    ps4_gpumem_windows wins{};
    uint64_t wired_base = 0;
    bool on = false;
    std::atomic<bool> over{false};          // charged > budget (with hysteresis), set by the sampler
    std::atomic<uint64_t> charged{0};       // latest charged total, set by the sampler
    uint64_t tick = 0;                      // encoder thread: time of the current Commit
    uint64_t last_sweep = 0, last_refused_clear = 0; // encoder thread
    ps4_gpumem_purge_state purge{};         // encoder thread
    // Released buffers stay alive while a command buffer in flight references them, so their
    // accounting is removed only once every command buffer up to `seq` has completed.
    struct Pending {
        uint64_t key, length, seq;
    };
    std::deque<Pending> pending;            // encoder thread, in seq order
    // Window spans refused at creation (key -> ps4_gpumem_admit), so later binds neither walk the
    // VM regions again nor retry; cleared on remaps and every policy.idle_ns.
    std::unordered_map<uint64_t, uint8_t> refused;
    // 64 MiB pages draws met unmapped that could not be mapped (GmMapPages): not tried again by
    // every harvest; cleared with `refused`.
    std::unordered_set<uint32_t> draw_unmappable;
    // Read-only copies made in the open command buffer (they live in its upload chunks), keyed by
    // the exact guest range: one copy per range and command buffer.
    struct Copy {
        uint64_t addr, size;
        id<MTLBuffer> buf;
        uint64_t off;
    };
    std::unordered_map<uint64_t, Copy> copies_open;
    std::atomic<uint64_t> released{0}, released_bytes{0}, purges{0}, large{0};
    // Texture cache eviction (EvictTextures, encoder thread)
    ps4_texcache_policy texpol{};
    uint64_t last_tex_evict = 0, last_gpu_evict = 0;
    bool tex_purge = false; // the over-budget sweep purged: evict idle sampled textures too
    // time Commit spends in SweepGuestWindows + EvictTextures (encoder thread writes, sampler reads)
    std::atomic<uint64_t> sweep_ns{0}, sweep_max_ns{0};
    std::atomic<uint64_t> refused_large{0}, refused_holes{0}, refused_written{0}, copies{0}, copy_bytes{0};
};
GpuMem g_mem;
// The texture cache as seen by the sampler thread (set once the cache exists).
std::atomic<TextureCache*> g_mem_tex{nullptr};

// Window span of a guest_bufs key (base | windows << 48).
inline void KeyWindows(uint64_t key, uint64_t* first, uint64_t* n) {
    *first = (key & ((1ull << 48) - 1)) / kWindow;
    *n = key >> 48;
}

void TakeGpuMemSample(ps4_gpumem_sample* s) {
    memset(s, 0, sizeof(*s));
    s->t_ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    s->wired = ps4_gpumem_wired_bytes();
    s->footprint = ps4_gpumem_footprint_bytes();
    s->device = S.dev ? (uint64_t)S.dev.currentAllocatedSize : 0;
    ps4_gpumem_windows_read(&g_mem.wins, s);
}

void StartGpuMemSampler(uint64_t mem_size) {
    // the policy is needed by GuestBuffer even if the accounting below cannot start
    ps4_gpumem_policy_init(&g_mem.policy, ps4_gpumem_mem_total(), getenv("PS4_GNM_GPU_BUDGET_PCT"),
                           getenv("PS4_GNM_GPU_BUDGET_MB"), getenv("PS4_GNM_GPU_WINDOW_IDLE_S"));
    ps4_texcache_policy_init(&g_mem.texpol, &g_mem.policy, getenv("PS4_GNM_TEX_CACHE_MB"), getenv("PS4_GNM_TEX_KEEP_MS"));
    if (!ps4_gpumem_windows_init(&g_mem.wins, mem_size, kWindow)) return;
    g_mem.wired_base = ps4_gpumem_wired_bytes();
    g_mem.on = true;
    const char* lv = getenv("PS4_GNM_MEM_LOG");
    const uint64_t period_s = lv ? strtoull(lv, nullptr, 10) : 10;
    std::thread([period_s] {
        pthread_setname_np("gnm-mem");
        ps4_gpumem_log_state log{};
        ps4_gpumem_budget_state budget{};
        uint64_t peak_charged = 0, peak_wired = 0, peak_tex = 0;
        const uint64_t t_start = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        for (;;) {
            ps4_gpumem_sample s;
            @autoreleasepool {
                TakeGpuMemSample(&s);
            }
            const uint64_t charged = ps4_gpumem_charged(&s);
            peak_charged = std::max(peak_charged, charged);
            peak_wired = std::max(peak_wired, s.wired);
            TextureCache* tc = g_mem_tex.load(std::memory_order_acquire);
            const TextureCache::MemStats ts = tc ? tc->GetMemStats() : TextureCache::MemStats{};
            peak_tex = std::max(peak_tex, ts.sampled_bytes);
            char line[512];
            const int change = ps4_gpumem_budget_update(&budget, &g_mem.policy, charged);
            g_mem.charged.store(charged, std::memory_order_relaxed);
            g_mem.over.store(budget.over != 0, std::memory_order_release);
            if (change) {
                ps4_gpumem_format(line, sizeof(line), &s, g_mem.wired_base, &g_mem.policy);
                if (change > 0 && budget.times_over == 1)
                    fprintf(stderr, "[gnm-mem] WARNING: over the GPU memory budget (%llu%% of RAM): releasing "
                                    "guest windows not in use and purging staging caches; %s\n",
                            (unsigned long long)(g_mem.policy.budget * 100 / std::max<uint64_t>(1, g_mem.policy.mem_total)),
                            line);
                else if (change > 0)
                    RLog("gpumem-over", "[gnm-mem] over the GPU memory budget again (%llu times): %s",
                         (unsigned long long)budget.times_over, line);
                else
                    RLog("gpumem-under", "[gnm-mem] back under the GPU memory budget: %s", line);
            }
            if (period_s && ps4_gpumem_log_due(&log, &s, period_s * 1000000000ull, 256ull << 20)) {
                ps4_gpumem_format(line, sizeof(line), &s, g_mem.wired_base, &g_mem.policy);
                fprintf(stderr, "[gnm-mem] %s; peak charged %llu MiB, peak wired %llu MiB; released %llu windows "
                                "(%llu MiB), purges %llu, large %llu, refused %llu too large / %llu with holes / %llu "
                                "written, copies %llu (%llu MiB)%s\n",
                        line, (unsigned long long)(peak_charged >> 20), (unsigned long long)(peak_wired >> 20),
                        (unsigned long long)g_mem.released.load(), (unsigned long long)(g_mem.released_bytes.load() >> 20),
                        (unsigned long long)g_mem.purges.load(), (unsigned long long)g_mem.large.load(),
                        (unsigned long long)g_mem.refused_large.load(), (unsigned long long)g_mem.refused_holes.load(),
                        (unsigned long long)g_mem.refused_written.load(), (unsigned long long)g_mem.copies.load(),
                        (unsigned long long)(g_mem.copy_bytes.load() >> 20), budget.over ? "; OVER BUDGET" : "");
                if (tc) {
                    fprintf(stderr, "[gnm-mem] texture cache at %llu s: sampled %llu (%llu MiB, peak %llu MiB, budget %llu MiB), "
                                    "GPU-owned %llu (%llu MiB); evicted %llu (%llu MiB), superseded GPU-owned %llu (%llu MiB)\n",
                            (unsigned long long)((s.t_ns - t_start) / 1000000000ull),
                            (unsigned long long)ts.sampled_count, (unsigned long long)(ts.sampled_bytes >> 20),
                            (unsigned long long)(peak_tex >> 20),
                            (unsigned long long)(g_mem.texpol.budget == UINT64_MAX ? 0 : g_mem.texpol.budget >> 20),
                            (unsigned long long)ts.gpu_count, (unsigned long long)(ts.gpu_bytes >> 20),
                            (unsigned long long)tc->EvictedCount(), (unsigned long long)(tc->EvictedBytes() >> 20),
                            (unsigned long long)tc->EvictedGpuCount(), (unsigned long long)(tc->EvictedGpuBytes() >> 20));
                }
                if (tc) {
                    const TextureCache::GpuOwnedStats go = tc->GetGpuOwnedStats(10000000000ull);
                    fprintf(stderr, "[gnm-mem] GPU-owned surfaces: %llu (%llu MiB) overlapped by newer surfaces, %llu (%llu MiB) "
                                    "unused for 10 s\n",
                            (unsigned long long)go.superseded_count, (unsigned long long)(go.superseded_bytes >> 20),
                            (unsigned long long)go.idle_count, (unsigned long long)(go.idle_bytes >> 20));
                }
                fprintf(stderr, "[gnm-mem] sweeps: %.1f ms total, longest %.2f ms since the last line\n",
                        g_mem.sweep_ns.exchange(0, std::memory_order_relaxed) / 1e6,
                        g_mem.sweep_max_ns.exchange(0, std::memory_order_relaxed) / 1e6);
            }
            usleep(1000000);
        }
    }).detach();
}

uint64_t CompletedSeq();

void AliasWindowReleased(GuestWin& w, uint64_t key);
void AliasPruneReleased();

// Drops a window from guest_bufs; its accounting goes when the GPU can no longer use it.
std::unordered_map<uint64_t, GuestWin>::iterator ReleaseWindow(std::unordered_map<uint64_t, GuestWin>::iterator it) {
    S.be->Evict(it->second.buf, S.committed_seq + 1); // the open command buffer may still use it
    AliasWindowReleased(it->second, it->first);
    if (g_mem.on) g_mem.pending.push_back({it->first, (uint64_t)it->second.buf.length, S.committed_seq + 1});
    return S.guest_bufs.erase(it);
}

// Removes the accounting of released windows whose command buffers have all completed.
void RetirePendingWindows() {
    if (g_mem.pending.empty()) return;
    const uint64_t done = CompletedSeq();
    while (!g_mem.pending.empty() && g_mem.pending.front().seq <= done) {
        const GpuMem::Pending& p = g_mem.pending.front();
        uint64_t first, n;
        KeyWindows(p.key, &first, &n);
        ps4_gpumem_windows_remove(&g_mem.wins, first, n, p.length);
        g_mem.pending.pop_front();
    }
}

void DropRemappedWindows() {
    const uint64_t mb_t0 = ps4_hitch_now_ns();
    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    {
        std::lock_guard<std::mutex> lk(g_remap_mutex);
        ranges.swap(g_remapped);
        g_remap_pending.store(false, std::memory_order_release);
    }
    for (auto it = S.guest_bufs.begin(); it != S.guest_bufs.end();) {
        const uint64_t base = it->first & ((1ull << 48) - 1);
        const uint64_t end = (base & ~(kWindow - 1)) + (it->first >> 48) * kWindow;
        bool hit = false;
        for (auto& r : ranges)
            if (r.first < end && r.first + r.second > base) { hit = true; break; }
        if (hit) it = ReleaseWindow(it);
        else ++it;
    }
    if (!ranges.empty()) { // the new mapping may have no holes
        g_mem.refused.clear();
        g_mem.draw_unmappable.clear();
    }
    for (auto& m : g_guest_buf_mru) m = GuestBufMru{};
    ps4_hitch_add_ns(PS4_HITCH_MTLBUF, ps4_hitch_now_ns() - mb_t0);
}

// Lazy residency and the budget (encoder thread, from Commit): releases windows not bound for
// policy.idle_ns, and while over budget every window not bound for policy.press_ns plus the
// transient staging caches (with a back-off once purging stops helping). A released buffer still
// referenced by a command buffer in flight lives (and stays wired) until that command buffer
// completes; the next use re-creates it.
// Drops the detile map and staging caches (SweepGuestWindows' purge); the open seq may still use them.
void EvictDetileCaches() {
    for (auto& [k, b] : S.detile_maps) S.be->Evict(b, S.committed_seq + 1);
    for (auto& [k, b] : S.detile_staging) S.be->Evict(b, S.committed_seq + 1);
    S.detile_maps.clear();
    S.detile_staging.clear();
}

void SweepGuestWindows() {
    g_mem.copies_open.clear(); // their upload chunks are recycled once this command buffer is done
    if (!g_mem.on) return;
    RetirePendingWindows();
    const bool over = g_mem.over.load(std::memory_order_acquire);
    const uint64_t now = g_mem.tick;
    // once a second; over budget at every commit until a sweep has run in this state
    static bool swept_over = false;
    if (now - g_mem.last_sweep < 1000000000ull && !(over && !swept_over)) return;
    g_mem.last_sweep = now;
    swept_over = over;
    if (g_mem.policy.idle_ns && now - g_mem.last_refused_clear >= g_mem.policy.idle_ns) {
        g_mem.refused.clear();
        g_mem.draw_unmappable.clear();
        g_mem.last_refused_clear = now;
    }
    uint64_t n_rel = 0, bytes = 0;
    for (auto it = S.guest_bufs.begin(); it != S.guest_bufs.end();) {
        if (ps4_gpumem_should_release(&g_mem.policy, now, it->second.last_use, over)) {
            bytes += it->second.buf.length;
            it = ReleaseWindow(it);
            n_rel++;
        } else {
            ++it;
        }
    }
    if (n_rel) {
        for (auto& m : g_guest_buf_mru) m = GuestBufMru{};
        g_mem.released.fetch_add(n_rel, std::memory_order_relaxed);
        g_mem.released_bytes.fetch_add(bytes, std::memory_order_relaxed);
        RLog("gpumem-release", "[gnm-mem] released %llu guest window buffers (%llu MiB)%s", (unsigned long long)n_rel,
             (unsigned long long)(bytes >> 20), over ? " (over budget)" : " (idle)");
    }
    if (ps4_gpumem_purge_due(&g_mem.purge, over, g_mem.charged.load(std::memory_order_relaxed))) {
        // staging caches are re-created on demand (they leave the residency sets after the open seq)
        EvictDetileCaches();
        std::lock_guard<std::mutex> lk(S.arena.free_mu);
        for (id<MTLBuffer> b : S.arena.free_list) S.arena.on_drop(b); // free chunks: no work uses them
        S.arena.free_list.clear();
        g_mem.purges.fetch_add(1, std::memory_order_relaxed);
        g_mem.tex_purge = true;
    } else if (over && g_mem.purge.futile == 10) {
        RLog("gpumem-futile", "[gnm-mem] purging does not lower the charged total (%llu MiB): the memory over "
                              "budget is the device's own (textures); purges paused",
             (unsigned long long)(g_mem.charged.load() >> 20));
    }
}

// Texture cache eviction (encoder thread, from Commit, after SweepGuestWindows): while the
// sampled textures exceed their budget, at most every 100 ms, least recently used first among
// those not used for texpol.keep_ns; when the over-budget sweep purged the transient caches (with
// its back-off), every sampled texture not used for texpol.press_ns. Only textures whose last
// command buffer has completed are evicted (TextureCache::EvictSampled).
// Renderer state keyed by a texture pointer must not outlive the texture: a new texture can get
// the same address. Every such map is erased here (keep this list complete when adding one).
// The renderer's own textures (depth companions, mip chains of render targets, depth textures
// made from memory, EQAA stand-ins): resident in the Texture set from creation (Metal 4) until
// replaced or forgotten, plus the open seq that may still use them. Encoder thread.
void OwnTextureMade(id<MTLTexture> t) {
    if (t && S.be) S.be->MakeResident(t, GnmResidencyClass::Texture);
}
void OwnTextureDropped(id<MTLTexture> t) {
    if (t && S.be) S.be->Evict(t, S.committed_seq + 1);
}

void ForgetTexture(void* key) {
    if (auto dc = S.depth_comp.find(key); dc != S.depth_comp.end()) OwnTextureDropped(dc->second.tex);
    S.depth_comp.erase(key);
    S.depth_version.erase(key);
    S.depth_last_write.erase(key);
    auto tw = S.eqaa_twin.find(key);
    if (tw != S.eqaa_twin.end()) {
        OwnTextureDropped(tw->second);
        S.eqaa_orig.erase((__bridge void*)tw->second);
        S.eqaa_twin.erase(tw);
    }
}

// Textures the cache dropped (superseded evictions, grown depth arrays): forget them, then let
// them go (command buffers that still use them keep them alive).
void ForgetRetiredTextures(TextureCache* tc) {
    std::vector<id<MTLTexture>> gone;
    tc->TakeRetired(&gone);
    for (id<MTLTexture> t : gone) ForgetTexture((__bridge void*)t);
}

void EvictTextures() {
    TextureCache* tc = S.tex;
    if (!tc) return;
    ForgetRetiredTextures(tc);
    const bool gpu_purge = g_mem.tex_purge;
    g_mem.tex_purge = false;
    const uint64_t now = g_mem.tick;
    // superseded GPU-owned surfaces: once a second (with the purge too), idle 10 s (2 s over budget)
    if (gpu_purge || now - g_mem.last_gpu_evict >= 1000000000ull) {
        g_mem.last_gpu_evict = now;
        const TextureCache::EvictResult gr =
            tc->EvictSuperseded(gpu_purge ? 2000000000ull : 10000000000ull, now, CompletedSeq());
        if (gr.count)
            RLog("tex-evict-gpu", "[gnm-mem] evicted %llu superseded GPU-owned surfaces (%llu MiB)",
                 (unsigned long long)gr.count, (unsigned long long)(gr.bytes >> 20));
        ForgetRetiredTextures(tc);
    }
    if (!gpu_purge && (now - g_mem.last_tex_evict < 100000000ull ||
                       !ps4_texcache_evict_due(&g_mem.texpol, tc->GetMemStats().sampled_bytes, 0)))
        return;
    g_mem.last_tex_evict = now;
    const TextureCache::EvictResult r = tc->EvictSampled(g_mem.texpol, gpu_purge, now, CompletedSeq());
    if (r.count)
        RLog("tex-evict", "[gnm-mem] evicted %llu sampled textures (%llu MiB)%s", (unsigned long long)r.count,
             (unsigned long long)(r.bytes >> 20), gpu_purge ? " (over the GPU budget)" : " (over the cache budget)");
}

// How a binding uses guest memory. A GPU-written binding must be the guest memory itself: a copy
// would drop its writes.
enum class GuestAccess { Read, Write };

// Read-only stand-in for a range no no-copy buffer may cover (one with unmapped or unreadable
// pages): a copy of its readable bytes in upload memory, holes as zero, made once per range and
// command buffer (guest writes after the first bind in that command buffer are not seen, as with
// any snapshot). 256 zero bytes follow it, since shader bounds round the size up to the V# end
// (debug bounds modes PS4_GNM_BOUNDS=raw/off can still read past that). The caller decides
// (ps4_gpumem_refused_copy) which ranges may be copied.
bool GuestCopy(uint64_t addr, uint64_t size, __strong id<MTLBuffer>* out, uint64_t* offset) {
    const uint64_t ck = addr ^ (size * 0x9E3779B97F4A7C15ull);
    auto cit = g_mem.copies_open.find(ck);
    if (cit != g_mem.copies_open.end() && cit->second.addr == addr && cit->second.size == size) {
        *out = cit->second.buf;
        *offset = cit->second.off;
        return true;
    }
    void* ptr = nullptr;
    size_t off = 0;
    id<MTLBuffer> b = nil;
    if (!S.arena.Alloc(S.dev, size + 256, 256, &b, &off, &ptr)) return false;
    memset(ptr, 0, size + 256);
    struct Ctx {
        uint8_t* dst;
        const uint8_t* src;
    } c = {(uint8_t*)ptr, S.ctx->mem_base + addr};
    ps4_gpumem_for_each_readable(c.src, size, [](void* p, uint64_t o, uint64_t n) {
        Ctx* x = (Ctx*)p;
        memcpy(x->dst + o, x->src + o, n);
    }, &c);
    g_mem.copies.fetch_add(1, std::memory_order_relaxed);
    g_mem.copy_bytes.fetch_add(size, std::memory_order_relaxed);
    g_mem.copies_open[ck] = {addr, size, b, off};
    *out = b;
    *offset = off;
    return true;
}

// A guest range whose window span got no no-copy buffer (adm: why). Reads of ranges with holes
// get a copy; GPU-written ranges and spans over policy.nocopy_max are not bound (false), as
// before the budget existed. allow_copy false (GuestPageWindow: only the window itself will do):
// nothing is copied and nothing logged here (the caller reports it).
bool RefusedGuestBuffer(uint64_t addr, uint64_t size, uint64_t base, uint64_t end, enum ps4_gpumem_admit adm,
                        GuestAccess access, __strong id<MTLBuffer>* out, uint64_t* offset, bool allow_copy) {
    const bool holes = adm == PS4_GPUMEM_REFUSE_HOLES;
    if (!allow_copy) return false;
    const bool copied =
        ps4_gpumem_refused_copy(adm, access == GuestAccess::Write, size) && GuestCopy(addr, size, out, offset);
    if (holes && access == GuestAccess::Write) g_mem.refused_written.fetch_add(1, std::memory_order_relaxed);
    RLog(!holes ? "guestbuf-big" : access == GuestAccess::Write ? "guestbuf-holes-written" : "guestbuf-holes",
         "[gnm-mem] no no-copy buffer over guest 0x%llx+0x%llx (for 0x%llx+0x%llx): %s; %s",
         (unsigned long long)base, (unsigned long long)(end - base), (unsigned long long)addr,
         (unsigned long long)size, holes ? "range has holes" : "larger than 2 GiB",
         copied ? "bound a read-only copy"
         : !holes ? "not bound"
         : access == GuestAccess::Write ? "not bound (GPU-written: a copy would drop the writes)"
                                        : "not bound (over 64 MiB)");
    return copied;
}

// Guest windows that alias (docs/gnm-ordering.md, "Aliasing guest windows"). Windows are keyed by base and
// window count, so a range crossing a 64 MiB boundary and a range inside one of its windows get two
// MTLBuffers over the same memory, and Metal 3 orders encoders by object only. Each window has an
// AliasState: the accesses through it that may still run, as the AccessFence of their encoder.
// - writes: the last writing encoder's fence and the hull of the bytes written while one is in
//   flight, and of the bytes read by the readers it replaced (Metal orders the encoders that write
//   through one window after every earlier access through it, so the last one's end follows them
//   all);
// - reads since the last write, one entry per encoder (reads are not ordered among themselves).
// Every window access a renderer job binds is listed (NoteWindowUse, one use per window per job,
// its byte range the hull of the job's ranges: a V#'s exact range, a page-table window's whole
// window). Before each command (the backend's BeforeCommand hook, FlushAliasOrders), for each use
// the open encoder has not ordered yet, it waits for the overlapping accesses through the other
// windows over those 64 MiB slots that may still run (a read waits for writes, a write for writes
// and reads: RAW, WAW and WAR), then records its own access with its fence. A job's uses stay
// listed until it ends (AliasJob): every command of a job (its uploads, detiles, the draw itself)
// orders them, which is conservative. The guest memory re-run queue's encoders (queue2, raw
// Metal encoders without the backend) are not ordered by this: the events between the queues
// order them against the main queue (GmCloseChain); their binds' uses go to the job's next
// backend command. Metal 3 only: Metal 4 orders every encoder after all earlier ones (the barrier
// at each encoder start), which keeps these dependencies (see Metal4Backend). Encoder thread.
struct AliasReader {
    uint64_t fence, seq, lo, hi;
};
struct AliasState {
    uint64_t w_fence = 0, w_seq = 0, w_lo = 0, w_hi = 0; // writes in flight (w_seq 0: none)
    std::deque<AliasReader> readers;                      // reads in flight since them, in seq order
    bool released = false;                                // the window was released (ReleaseWindow)
    uint64_t job = 0;                                     // uses[idx] is its use while job is the current one
    uint32_t idx = 0;
    uint64_t stamp = 0; // visited by the use being ordered (FlushAliasOrders)
    void Prune(uint64_t done) {
        if (w_seq && w_seq <= done) w_seq = 0;
        while (!readers.empty() && readers.front().seq <= done) readers.pop_front();
    }
    bool Idle() const { return !w_seq && readers.empty(); }
};
struct AliasUse {
    AliasState* st; // alive for the job: its window's, or kept in its slots while the job uses it
    uint64_t lo, hi;
    bool written;
    uint64_t serial; // the encoder that last ordered it (EncoderSerial << 1 | Upload), 0: none yet
};
struct AliasOrder {
    bool on = false; // Metal 3
    int depth = 0;   // AliasJob nesting
    uint64_t job = 1, stamp = 0;
    std::vector<AliasUse> uses;
    // per 64 MiB slot: the states of the windows over it, live or released with accesses in flight
    std::vector<std::vector<std::shared_ptr<AliasState>>> cover;
    std::vector<std::pair<std::shared_ptr<AliasState>, uint64_t>> released; // with their window base | windows << 48
    uint64_t waits = 0; // waits asked for (the backend counts those it encodes)
};
AliasOrder g_alias;

// A window was created: its state joins the slots it covers.
void AliasWindowCreated(GuestWin& w, uint64_t key) {
    if (!g_alias.on) return;
    w.alias = std::make_shared<AliasState>();
    uint64_t first, n;
    KeyWindows(key, &first, &n);
    if (g_alias.cover.size() < first + n) g_alias.cover.resize(first + n);
    for (uint64_t s = first; s < first + n; ++s) g_alias.cover[s].push_back(w.alias);
}

// Drops a released state from its slots once nothing through it may still run.
bool AliasDropIfIdle(const std::shared_ptr<AliasState>& a, uint64_t key, uint64_t done) {
    a->Prune(done);
    if (!a->Idle() || a->job == g_alias.job) return false;
    uint64_t first, n;
    KeyWindows(key, &first, &n);
    for (uint64_t s = first; s < first + n && s < g_alias.cover.size(); ++s) {
        auto& c = g_alias.cover[s];
        for (size_t i = 0; i < c.size(); ++i)
            if (c[i] == a) {
                c[i] = std::move(c.back());
                c.pop_back();
                break;
            }
    }
    return true;
}

uint64_t CompletedSeq();

// A window was released: its state stays in its slots while accesses through it may still run.
void AliasWindowReleased(GuestWin& w, uint64_t key) {
    if (!w.alias) return;
    w.alias->released = true;
    if (!AliasDropIfIdle(w.alias, key, CompletedSeq())) g_alias.released.push_back({w.alias, key});
    w.alias.reset();
}

// At each Commit: released states whose accesses completed leave their slots.
void AliasPruneReleased() {
    if (g_alias.released.empty()) return;
    const uint64_t done = CompletedSeq();
    size_t k = 0;
    for (size_t i = 0; i < g_alias.released.size(); ++i)
        if (!AliasDropIfIdle(g_alias.released[i].first, g_alias.released[i].second, done))
            g_alias.released[k++] = std::move(g_alias.released[i]);
    g_alias.released.resize(k);
}

void NoteWindowUse(GuestWin& w, uint64_t addr, uint64_t size, GuestAccess access) {
    if (!w.alias) return; // Metal 4 (g_alias.on), or a window made before
    AliasState& a = *w.alias;
    const bool written = access == GuestAccess::Write;
    if (a.job == g_alias.job && a.idx < g_alias.uses.size() && g_alias.uses[a.idx].st == &a) {
        AliasUse& u = g_alias.uses[a.idx];
        if (addr < u.lo || addr + size > u.hi || (written && !u.written)) {
            u.lo = std::min(u.lo, addr);
            u.hi = std::max(u.hi, addr + size);
            u.written |= written;
            u.serial = 0; // more than the encoder that ordered it knows of
        }
        return;
    }
    a.job = g_alias.job;
    a.idx = (uint32_t)g_alias.uses.size();
    g_alias.uses.push_back({&a, addr, addr + size, written, 0});
}

void FlushAliasOrders(void*) {
    if (g_alias.uses.empty()) return;
    // serials count per target
    const uint64_t serial = S.be->EncoderSerial() << 1 | (S.be->Target() == GnmCmdTarget::Upload);
    const uint64_t done = CompletedSeq();
    const uint64_t seq = S.committed_seq + 1;
    uint64_t fence = 0;
    for (AliasUse& u : g_alias.uses) {
        if (u.serial == serial) continue; // this encoder ordered it already
        u.serial = serial;
        AliasState& a = *u.st;
        a.stamp = ++g_alias.stamp;
        for (uint64_t s = u.lo / kWindow; s <= (u.hi - 1) / kWindow && s < g_alias.cover.size(); ++s) {
            for (const std::shared_ptr<AliasState>& xp : g_alias.cover[s]) {
                AliasState& x = *xp;
                if (x.stamp == a.stamp) continue; // this window, or one seen in an earlier slot
                x.stamp = a.stamp;
                x.Prune(done);
                if (x.w_seq && x.w_lo < u.hi && u.lo < x.w_hi) {
                    S.be->WaitAccessFence(x.w_fence);
                    g_alias.waits++;
                }
                if (u.written)
                    for (const AliasReader& r : x.readers)
                        if (r.lo < u.hi && u.lo < r.hi) {
                            S.be->WaitAccessFence(r.fence);
                            g_alias.waits++;
                        }
            }
        }
        if (!fence) fence = S.be->AccessFence();
        a.Prune(done);
        if (u.written) {
            uint64_t lo = a.w_seq ? std::min(a.w_lo, u.lo) : u.lo, hi = a.w_seq ? std::max(a.w_hi, u.hi) : u.hi;
            // the readers still in flight end before this write does (Metal orders it after them: the
            // same window), so its fence covers them: their bytes join the hull, and a later write
            // through another window over them waits for it
            // (written uses are never on the Upload target)
            for (const AliasReader& r : a.readers) lo = std::min(lo, r.lo), hi = std::max(hi, r.hi);
            a.w_lo = lo;
            a.w_hi = hi;
            a.w_fence = fence;
            a.w_seq = seq;
            a.readers.clear();
        } else if (!a.readers.empty() && a.readers.back().fence == fence) {
            a.readers.back().lo = std::min(a.readers.back().lo, u.lo);
            a.readers.back().hi = std::max(a.readers.back().hi, u.hi);
        } else {
            a.readers.push_back({fence, seq, u.lo, u.hi});
        }
    }
}

// A renderer job (a draw, a dispatch, a DMA): its window uses are ordered by all its commands.
struct AliasJob {
    AliasJob() { g_alias.depth++; }
    ~AliasJob() {
        if (--g_alias.depth == 0) {
            g_alias.uses.clear();
            g_alias.job++;
        }
    }
    AliasJob(const AliasJob&) = delete;
    AliasJob& operator=(const AliasJob&) = delete;
};

// `out` is a strong pointer: an (implicitly autoreleasing) plain one would retain and autorelease
// the buffer on every call. `access`: whether the GPU writes the range (a written range is only
// ever bound in place). allow_copy false: a span refused a no-copy buffer gets no read-only copy
// either (false).
bool GuestBuffer(uint64_t addr, uint64_t size, GuestAccess access, __strong id<MTLBuffer>* out, uint64_t* offset,
                 bool allow_copy = true) {
    GuestContext* ctx = S.ctx;
    if (g_remap_pending.load(std::memory_order_acquire)) DropRemappedWindows();
    if (!ctx) return false;
    uint64_t base, end, nwin;
    if (!ps4_gnm_guest_window_range(addr, size, ctx->mem_size, &base, &end, &nwin)) return false;
    const uint64_t key = base | (nwin << 48);
    GuestBufMru& mru = g_guest_buf_mru[(base / kWindow + nwin) & 3];
    if (mru.key == key) {
        mru.win->last_use = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        *out = mru.win->buf;
        *offset = addr - base;
        NoteWindowUse(*mru.win, addr, size, access);
        return true;
    }
    auto it = S.guest_bufs.find(key);
    if (it == S.guest_bufs.end()) {
        auto rit = g_mem.refused.find(key);
        if (rit != g_mem.refused.end())
            return RefusedGuestBuffer(addr, size, base, end, (enum ps4_gpumem_admit)rit->second, access, out, offset,
                                      allow_copy);
        const uint64_t mb_t0 = ps4_hitch_now_ns();
        // size first: a too large span is refused without walking its VM regions
        enum ps4_gpumem_admit adm = ps4_gpumem_admit_nocopy(&g_mem.policy, end - base, 0);
        if (adm != PS4_GPUMEM_REFUSE_TOO_LARGE)
            adm = ps4_gpumem_admit_nocopy(&g_mem.policy, end - base,
                                          ps4_gpumem_range_has_holes(ctx->mem_base + base, end - base));
        if (adm == PS4_GPUMEM_REFUSE_TOO_LARGE || adm == PS4_GPUMEM_REFUSE_HOLES) {
            ps4_hitch_add_ns(PS4_HITCH_MTLBUF, ps4_hitch_now_ns() - mb_t0);
            (adm == PS4_GPUMEM_REFUSE_TOO_LARGE ? g_mem.refused_large : g_mem.refused_holes)
                .fetch_add(1, std::memory_order_relaxed);
            g_mem.refused[key] = (uint8_t)adm;
            return RefusedGuestBuffer(addr, size, base, end, adm, access, out, offset, allow_copy);
        }
        if (adm == PS4_GPUMEM_ADMIT_LARGE) {
            g_mem.large.fetch_add(1, std::memory_order_relaxed);
            RLog("guestbuf-large", "[gnm-mem] no-copy buffer over guest 0x%llx+0x%llx is larger than 1 GiB: it pins "
                                   "(and wires) all of it while bound",
                 (unsigned long long)base, (unsigned long long)(end - base));
        }
        id<MTLBuffer> b = [S.dev newBufferWithBytesNoCopy:ctx->mem_base + base
                                                   length:end - base
                                                  options:MTLResourceStorageModeShared
                                              deallocator:nil];
        ps4_hitch_add_ns(PS4_HITCH_MTLBUF, ps4_hitch_now_ns() - mb_t0);
        if (!b) {
            RLog("guestbuf", "newBufferWithBytesNoCopy failed for 0x%llx+0x%llx",
                 (unsigned long long)base, (unsigned long long)(end - base));
            return false;
        }
        it = S.guest_bufs.emplace(key, GuestWin{b, 0, nullptr}).first;
        AliasWindowCreated(it->second, key);
        S.be->MakeResident(b, GnmResidencyClass::Guest); // until ReleaseWindow
        if (g_mem.on) ps4_gpumem_windows_add(&g_mem.wins, base / kWindow, nwin, end - base);
    }
    it->second.last_use = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    mru.key = key;
    mru.win = &it->second;
    *out = it->second.buf;
    *offset = addr - base;
    NoteWindowUse(it->second, addr, size, access);
    return true;
}

bool ShouldTrace();
void BuildFlat(const struct Shader* s, const uint32_t ud[16], std::vector<uint32_t>* flat);
id<MTLTexture> DepthCompanion(id<MTLTexture> dt, bool encode);
bool HasStencil(MTLPixelFormat f);
id<MTLTexture> DepthFromMemory(const ImageDesc& img, bool encode);
id<MTLTexture> MipChainFor(const ImageDesc& img, bool encode);
uint32_t MipChainSources(const ImageDesc& img, id<MTLTexture> base, __strong id<MTLTexture> (&srcs)[kMaxMips],
                         uint64_t* addrs = nullptr);
void ComputeEncoder();
id<MTLTexture> SampledTexture(const ImageDesc& img);

// The draw or dispatch being bound stores exactly guest [addr, addr + size) as a buffer (a written
// V# with a known size): GPU-owned color surfaces such stores cover completely are refreshed from
// guest memory before their next GPU use (TextureCache::NoteBufferWrite). Recorded here and handed
// to the cache only once the work is encoded (FlushBufferStores): a bind that fails later, a draw
// whose pixel shader does not bind, a guest memory probe pass or a skipped dispatch write nothing.
// Stores through V#s a guest memory dispatch loads at run time are only known as the 1 MiB pages
// they touch (GCN_GUEST_WRITTEN), not as exact ranges: they never mark a surface.
void NoteBufferStore(uint64_t addr, uint64_t size) {
    if (size) S.buffer_stores.emplace_back(addr, size);
}

void FlushBufferStores() {
    if (S.tex)
        for (const auto& [a, n] : S.buffer_stores) S.tex->NoteBufferWrite(a, n, S.write_epoch);
    S.buffer_stores.clear();
}

bool EncodeGpuUploadAt(uint64_t address, const SurfaceDesc& sd, const TextureCache::GpuUpload& up,
                       bool upload_target);

// Before a GPU use of the color surface at `address` (texture `tex`, or nil: any surface there):
// a surface marked guest newer gets guest memory copied into its texture on the GPU, behind the
// writing work and before the use is encoded. Must run while no encoder of the use is open.
// write_site: the use writes the surface (render-target or storage bind, fast clear, resolve or
// copy destination); a copy that cannot be encoded then drops the mark (FinishGuestNewer).
void RefreshGuestNewer(uint64_t address, id<MTLTexture> tex, bool write_site) {
    GmApplyInvalidations(); // marks from fast guest memory dispatches that ran (GmQueueStores)
    if (!S.tex || !S.tex->GuestNewerCount()) return;
    TextureCache::GpuUpload up;
    // tex nil: every marked surface at the address (several surfaces can share it)
    while (S.tex->TakeGuestNewer(address, tex, &up)) {
        const bool ok = EncodeGpuUploadAt(address, up.surf, up, false);
        S.tex->FinishGuestNewer(address, up.tex, ok, write_site);
        if (!ok) {
            RLog("guest-newer", "GPU target 0x%llx (%ux%u) written by buffer stores: the copy from guest memory could "
                 "not be encoded; %s", (unsigned long long)address, up.width, up.height,
                 write_site ? "the GPU write that follows supersedes it (unmarked)" : "it stays marked");
            if (!write_site) return; // the surface is still marked: TakeGuestNewer would hand it out again
        }
        if (tex) return;
    }
}

void NoteGpuWrite(uint64_t addr, uint64_t size) {
    if (!size) return;
    // the same range written again by this command buffer: only its latest write matters (the
    // hazard check and the texture invalidation both take the latest epoch of a range)
    uint32_t& slot = S.gpu_written_slot[((addr >> 8) ^ size) & 63];
    S.pending_writes.Note(addr, size, S.write_epoch + 1);
    if (slot < S.gpu_written.size() && S.gpu_written[slot].addr == addr && S.gpu_written[slot].size == size) {
        S.gpu_written[slot].epoch = ++S.write_epoch;
        return;
    }
    slot = (uint32_t)S.gpu_written.size();
    S.gpu_written.push_back({addr, size, ++S.write_epoch});
}

uint64_t CompletedSeq() {
    std::lock_guard<std::mutex> lk(S.fence_mu);
    return S.completed_seq;
}

// True if GPU work that has not completed yet writes guest memory in [addr, addr + size).
void PruneCompletedWrites() {
    if (S.inflight_written.empty()) return;
    const uint64_t done = CompletedSeq();
    while (!S.inflight_written.empty() && S.inflight_written.front().first <= done) S.inflight_written.pop_front();
    S.pending_writes.Prune(done);
}

// ---- What GPU work was pending when the CP copied a draw's or dispatch's T# tables ----
// The CP copies the tables (the flattened user data) when it prepares the draw; the hazard check
// (ResolveTableHazards) runs when the encoder encodes it, which can be much later: with the
// encoder thread the CP runs ahead, and in a captured frame every pass starts its encoding with
// the GPU drained. So the check judges a write by whether it had completed when the CP read the
// tables: a draw or dispatch takes completed_seq first (TableReadBegin), and every write of a
// command buffer committed after that (or still being recorded) counts, completed since or not.
// S.table_written keeps those writes until no draw or dispatch that may need them is left: while
// some are open, back to the snapshot of the last one encoded (later ones took theirs later: CP
// calls are serialized and encoded in order). Ordering: the CP counts itself open, then reads
// completed_pub; PruneTableWrites reads completed_pub, then the open count. If it sees none open,
// a CP that opens later reads a completed_pub at least as new as the one it pruned to.
std::atomic<uint64_t> g_table_reads_open{0};

uint64_t TableReadBegin() {
    g_table_reads_open.fetch_add(1);
    return S.completed_pub.load();
}
// CP thread: the draw or dispatch is not handed to the encoder after all.
struct TableReadCp {
    bool armed = true;
    ~TableReadCp() {
        if (armed) g_table_reads_open.fetch_sub(1);
    }
};
// Encoder thread, for the draw or dispatch being encoded: on every return path.
struct TableReadEncoded {
    uint64_t snap;
    explicit TableReadEncoded(uint64_t s) : snap(s) {}
    ~TableReadEncoded() {
        if (snap > S.table_floor) S.table_floor = snap;
        g_table_reads_open.fetch_sub(1);
    }
};

void PruneTableWrites() {
    if (S.table_written.empty()) return;
    uint64_t floor = S.completed_pub.load();
    if (g_table_reads_open.load() != 0) floor = std::min(floor, S.table_floor);
    while (!S.table_written.empty() && S.table_written.front().first <= floor) S.table_written.pop_front();
}

// A write to [addr, addr + size) the CP could not have seen when it took `snap`: one recorded
// in the open command buffer, or of a command buffer committed after `snap` completed. *hit:
// one of them; *pending_seq: the newest command buffer among them (UINT64_MAX: the open one).
bool OverlapsWriteAfter(uint64_t addr, uint64_t size, uint64_t snap, GpuWrite* hit, uint64_t* pending_seq) {
    bool any = false;
    auto check = [&](const GpuWrite& r, uint64_t seq) {
        if (r.addr < addr + size && addr < r.addr + r.size) {
            if (!any) *hit = r;
            any = true;
            *pending_seq = std::max(*pending_seq, seq);
        }
    };
    for (auto& r : S.gpu_written) check(r, UINT64_MAX);
    for (auto& [seq, w] : S.table_written)
        if (seq > snap)
            for (auto& r : *w) check(r, seq);
    return any;
}

// `hit` (optional) receives an overlapping write with the latest epoch among the overlaps.
// (The writes of gpu_written and inflight_written, by range: S.pending_writes.)
bool OverlapsPendingWrite(uint64_t addr, uint64_t size, GpuWrite* hit = nullptr) {
    GnmPendingWrites::Range r;
    if (!S.pending_writes.Overlaps(addr, size, hit ? &r : nullptr)) return false;
    if (hit) *hit = GpuWrite{r.addr, r.size, r.epoch};
    return true;
}

// =====================================================================================
// Command buffer management
// =====================================================================================
void ResolveEqaaPass();

void EndEncoders() {
    if (g_gms.chain_open && !g_gms.chain_closing) {
        GmCloseChain(); // ends the encoders itself
        return;
    }
    const GnmEncoder open = S.be->OpenEncoder();
    if (open != GnmEncoder::None) S.be->EndEncoder();
    if (open == GnmEncoder::Render) ResolveEqaaPass();
    S.pass = PassKey{};
    for (auto& t : S.pass_hold) t = nil;
}

std::atomic<bool> g_cmd_open{false}; // S.be->CmdOpen(Main), readable from the CP thread
// S.be->CmdOpen(GnmCmdTarget::Main) without a virtual call, for the per-dispatch and per-fence
// paths of the encoder thread (which writes g_cmd_open itself).
inline bool MainCmdOpen() { return g_cmd_open.load(std::memory_order_relaxed); }

// PS4_GNM_ENCODER_STATUS=1 (debug): command buffers report per-encoder execution status, and
// every encoder is labelled with the index of its first work record, so a failed command buffer
// names the encoder that faulted (see LogEncoderStatus). Costs GPU time; off by default. Metal 3
// only: Metal 4 command buffers have no per-encoder status (Init warns and leaves it off).
bool EncoderStatusEnabled() {
    return g_encoder_status;
}

NSString* WorkLabel(const char* kind) {
    const uint64_t first = S.cmd_log ? S.cmd_log->count : 0;
    return [NSString stringWithFormat:@"%s from work[%llu]", kind, (unsigned long long)first];
}

void LogEncoderStatus(NSError* error) {
    NSArray* infos = error.userInfo[MTLCommandBufferEncoderInfoErrorKey];
    for (id<MTLCommandBufferEncoderInfo> info in infos) {
        const MTLCommandEncoderErrorState st = info.errorState;
        if (st != MTLCommandEncoderErrorStateFaulted && st != MTLCommandEncoderErrorStateAffected) continue;
        fprintf(stderr, "[gnm-draw]   encoder '%s': %s\n", (info.label ?: @"unlabelled").UTF8String,
                st == MTLCommandEncoderErrorStateFaulted ? "FAULTED" : "affected");
    }
}

// The Metal 3 queue holds at most kMaxCommandBuffers command buffers that have not completed; creating
// one more blocks until one completes. While a missed guest memory dispatch holds the GPU at its
// 2t, none completes until it was re-run: so while committed tickets are not final, the encoder
// never blocks there but waits here, with its state free for the re-run (encq::Unlocked) and
// re-running missed dispatches itself too. (Upload command buffers, at most one per main one, are
// within the margin.) Without such tickets nothing needs the CPU, and the queue's own limit
// applies as before (TLOU measured: a throttle at all times cost 8 % fps, the encoder waiting
// here with GPU work to spare). No command buffer completing within the GPU timeout: the GPU is
// given up on (GmGiveUp).
constexpr uint64_t kMaxCommandBuffers = kGnmMaxCommandBuffers;
constexpr uint64_t kThrottleAt = kMaxCommandBuffers / 2 - 4;
void ThrottleCommandBuffers() {
    const auto completed = [] {
        std::lock_guard<std::mutex> lk(S.fence_mu);
        return S.completed_seq;
    };
    const auto outstanding = [] {
        std::lock_guard<std::mutex> lk(S.fence_mu);
        return S.committed_seq - std::min(S.completed_seq, S.committed_seq);
    };
    const auto pending = [] { return g_gms.committed && GmResolved() < g_gms.committed; };
    if (outstanding() < kThrottleAt || !pending()) return;
    const auto timeout = std::chrono::duration<double, std::milli>(S.gpu_timeout_ms);
    uint64_t seen = completed();
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!S.hung && outstanding() >= kThrottleAt && pending()) {
        GmService();
        {
            encq::Unlocked free_state;
            std::unique_lock<std::mutex> lk(g_gms.mu);
            g_gms.cv.wait_until(lk, deadline, [&] {
                return g_gms.miss.load(std::memory_order_relaxed) || outstanding() < kThrottleAt || !pending() || S.hung;
            });
        }
        if (completed() != seen) {
            seen = completed();
            deadline = std::chrono::steady_clock::now() + timeout;
        } else if (std::chrono::steady_clock::now() >= deadline && outstanding() >= kThrottleAt && pending()) {
            char why[128];
            snprintf(why, sizeof(why), "no command buffer of %llu outstanding completed in %.0f ms",
                     (unsigned long long)outstanding(), S.gpu_timeout_ms);
            GmGiveUp(why);
            GmLogState();
            GmReleaseAll();
        }
    }
}

// A new command buffer starts after the result of every guest memory dispatch committed so far
// is final: the current target's stream waits for 2 * committed. (A ticket encoded into the open
// main command buffer is waited for there, right after it: a wait here for a ticket of a command
// buffer committed later would never end.)
// Work that belongs to the main command buffer (the guest memory chain's events, a new main
// command buffer's ticket wait, the GPU stall test hook) is sent to the current target: it must be
// Main there. Only UploadTargetScope switches, and nothing inside one reaches these; one that
// did would put the event into the upload command buffer, so it stops here instead.
void RequireMainTarget(const char* where) {
    if (S.be->Target() == GnmCmdTarget::Main) return;
    fprintf(stderr, "[gnm-draw] FATAL: %s with the upload command buffer as the current target\n", where);
    abort();
}

void GmWaitForCommitted() {
    if (!g_gms.committed || GmResolved() >= g_gms.committed) return;
    S.be->QueueWait(g_gms.ev, 2 * g_gms.committed);
}

// The GPU made no progress within S.gpu_timeout_ms (WaitAll, EnsureCmd): rendering stops, fences
// keep being signalled. `what` names what did not complete; `hung`: the work log of the stuck
// command buffer, printed in full.
void GiveUpGpu(const char* what, const WorkLog* hung = nullptr) {
    fprintf(stderr, "[gnm-draw] GPU watchdog: %s did not complete in %.0f ms; disabling GPU rendering (fences keep "
                    "being signaled)\n", what, S.gpu_timeout_ms);
    S.hung = true;
    // frames held for the display wait on conversions that may never complete: their
    // flips complete now, so CompleteFlip does not queue every later flip behind them
    ps4_metal_screen_release_held();
    GmLogState();
    if (hung) hung->Print("hung work", WorkLog::kAll);
    GmReleaseAll();
}

// Wall time of a blocking call on the encoder thread, for the perf line's encoder split.
struct EncWaitTimer {
    std::atomic<uint64_t>& to;
    const uint64_t t0 = encq::t_worker ? clock_gettime_nsec_np(CLOCK_UPTIME_RAW) : 0;
    explicit EncWaitTimer(std::atomic<uint64_t>& a) : to(a) {}
    ~EncWaitTimer() {
        if (t0) to.fetch_add(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0, std::memory_order_relaxed);
    }
};

void EnsureCmd() {
    if (MainCmdOpen()) return;
    {
        EncWaitTimer w(encq::throttle_ns);
        ThrottleCommandBuffers();
    }
    // The backend's own limit on command buffers in flight (kMaxCommandBuffers): a GPU that
    // completes none of them within the GPU timeout is given up on, instead of blocking forever.
    if (!S.hung) {
        uint64_t committed;
        {
            std::lock_guard<std::mutex> lk(S.fence_mu);
            committed = S.committed_seq;
        }
        bool room;
        {
            EncWaitTimer w(encq::room_ns);
            room = S.be->WaitForRoom(committed, kMaxCommandBuffers, S.gpu_timeout_ms);
        }
        if (!room) {
            char what[96];
            snprintf(what, sizeof(what), "the oldest of %llu outstanding command buffers",
                     (unsigned long long)kMaxCommandBuffers);
            GiveUpGpu(what);
        }
    }
    S.cmd_index = S.n_cmdbufs++;
    {
        EncWaitTimer w(encq::begin_ns);
        S.be->BeginCmd(GnmCmdTarget::Main, [NSString stringWithFormat:@"gnm-%llu", (unsigned long long)S.cmd_index],
                       EncoderStatusEnabled());
    }
    g_cmd_open.store(true, std::memory_order_release);
    S.cmd_log = std::make_shared<WorkLog>();
    // the wait goes into the new main command buffer: Main must be the current target here
    RequireMainTarget("EnsureCmd");
    GmWaitForCommitted();
}

void LogWork(const WorkRec& r) {
    if (S.cmd_log) S.cmd_log->Add(r);
}
void LogWork(const char* what) {
    WorkRec r;
    r.what = what;
    LogWork(r);
}

// Runs queued fence actions whose command buffers completed (in order).
void RunFences(uint64_t upto) {
    std::lock_guard<std::recursive_mutex> run(S.fence_run_mu);
    for (;;) {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lk(S.fence_mu);
            if (upto > S.completed_seq) {
                S.completed_seq = upto;
                S.completed_pub.store(upto);
            }
            if (S.fence_q.empty() || S.fence_q.front().first > S.completed_seq) return;
            fn = std::move(S.fence_q.front().second);
            S.fence_q.pop_front();
        }
        if (fn) fn();
    }
}

void GmCommandBufferFailed(const std::vector<std::shared_ptr<GmTicket>>& tickets);

// What the completion of one seq needs (the former addCompletedHandler block's captures).
struct CommitDone {
    uint64_t seq = 0, cmd_index = 0;
    std::shared_ptr<std::vector<GpuWrite>> written;
    TextureCache* tex_cache = nullptr;
    std::shared_ptr<WorkLog> log;
    std::shared_ptr<std::vector<std::shared_ptr<GmTicket>>> tickets;
    std::vector<id<MTLBuffer>> chunks; // upload arena chunks, recycled now
    UploadArena* arena = nullptr;
    // PS4_GNM_HANG_REPORT_MS only: set at completion, read by the report timer (which may outlive S)
    std::shared_ptr<std::atomic<bool>> completed;
    std::vector<ps4_metal_screen_present_op> present_fail; // present events raised if the seq fails
};

void NoteSeqCompleted();

void CommitDoneFn(void* p, const GnmCompletion& c) {
    const CompletionScope gate;
    if (!gate.in) return; // a crash's exit: its CommitDone is left (its members are this file's)
    std::unique_ptr<CommitDone> d(static_cast<CommitDone*>(p));
    // first, as the present's own completed handler ran before this one: a failed command buffer
    // may not have reached its present signals, and the presenter must not wait for them forever
    if (c.failed)
        for (const ps4_metal_screen_present_op& op : d->present_fail) ps4_metal_screen_present_failed(&op);
    if (d->completed) d->completed->store(true, std::memory_order_release);
    if (d->written && d->tex_cache) d->tex_cache->InvalidateWrites(*d->written);
    {
        // GPU busy time: union of the command buffers' execution intervals (they may overlap)
        std::lock_guard<std::mutex> lk(S.gpu_time_mu);
        const double from = std::max(c.gpu_start, S.gpu_last_end);
        if (c.gpu_end > from) S.gpu_ns.fetch_add((uint64_t)((c.gpu_end - from) * 1e9), std::memory_order_relaxed);
        S.gpu_last_end = std::max(S.gpu_last_end, c.gpu_end);
    }
    RunFences(d->seq);
    NoteSeqCompleted();
    if (c.failed) {
        // how long the seq ran on the GPU tells a hang (about the system's time limit) from a fault
        // (cut short); the error's userInfo carries what Metal says beyond its generic description
        fprintf(stderr, "[gnm-draw] command buffer gnm-%llu failed after %.1f ms on the GPU: %s\n",
                (unsigned long long)d->cmd_index, c.gpu_end > c.gpu_start ? (c.gpu_end - c.gpu_start) * 1e3 : 0.0,
                c.error.localizedDescription.UTF8String);
        if (c.error.userInfo.count)
            fprintf(stderr, "[gnm-draw]   error info: %s\n", c.error.userInfo.description.UTF8String);
        if (EncoderStatusEnabled()) LogEncoderStatus(c.error);
        // Metal 4 has no per-encoder status: the whole log kept for the command buffer (64 records,
        // 512 with the debug options) is the only account of what it held
        const bool all = EncoderStatusEnabled() || g_backend_metal4.load(std::memory_order_relaxed);
        if (d->log) d->log->Print("work", all ? WorkLog::kAll : 12);
        // its guest memory dispatches may not have signaled: they count as skipped
        if (d->tickets) GmCommandBufferFailed(*d->tickets);
    }
    GmNotify();
    std::lock_guard<std::mutex> lk(d->arena->free_mu);
    for (auto& b : d->chunks)
        if (b.length == UploadArena::kChunk && d->arena->free_list.size() < 16) d->arena->free_list.push_back(b);
        else if (d->arena->on_retired) d->arena->on_retired(b, d->seq); // its seq has completed
}

// Batched compute commits (FlushCompute, docs/compute-batching.md). The CP ends every compute
// submission with a flush; while the GPU is behind (kHoldBacklog seqs or more in flight), the
// commit is held instead and the next submissions' work goes into the same command buffer and
// compute encoder (guest sync packets still become barriers in it: ComputeBarrierImpl). Any
// Commit releases it; so does the release thread, kHoldNs after the work was first held or as
// soon as the backlog drops (a completion wakes it).
constexpr uint64_t kHoldBacklog = 2, kHoldNs = 500000; // kHoldNs: the bound (DebugSetComputeHoldNs)
// PS4_GNM_COMPUTE_BATCH=0 (read in Init): no hold, every compute submission is committed at its end
// (the behaviour before batching, docs/compute-batching.md)
bool g_compute_batch = true;
struct ComputeHold {
    std::mutex mu;
    std::condition_variable cv;
    std::atomic<uint64_t> since{0}; // when the held work was first held (ns), 0: none (encoder state writes it)
    bool queued = false;            // a release is queued on the encoder (mu)
    bool woken = false;             // a seq completed since the release thread looked at the backlog (mu)
    bool started = false;           // the release thread runs (mu)
    std::atomic<uint64_t> held{0}, by_time{0}, by_backlog{0};
    std::atomic<uint64_t> bound_ns{kHoldNs};
};
ComputeHold g_hold;

uint64_t SeqsInFlight() {
    std::lock_guard<std::mutex> lk(S.fence_mu);
    return S.committed_seq - S.completed_seq;
}

void Commit();

// Encoder: commits held work once its bound or the backlog says so.
void ReleaseHeldCompute() {
    {
        std::lock_guard<std::mutex> lk(g_hold.mu);
        g_hold.queued = false;
        g_hold.cv.notify_one(); // held still (not released below): the release thread waits again
    }
    const uint64_t since = g_hold.since.load(std::memory_order_relaxed);
    if (!since || !S.enabled.load(std::memory_order_acquire) || !S.be) return;
    const bool late = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - since >= g_hold.bound_ns.load(std::memory_order_relaxed);
    if (!late && SeqsInFlight() >= kHoldBacklog) return; // woken early: the release thread waits again
    (late ? g_hold.by_time : g_hold.by_backlog).fetch_add(1, std::memory_order_relaxed);
    Commit();
}

void HoldReleaseLoop() {
    pthread_setname_np("GNM Compute Hold");
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    std::unique_lock<std::mutex> lk(g_hold.mu);
    for (;;) {
        g_hold.cv.wait(lk, [] { return g_hold.since.load(std::memory_order_relaxed) != 0 && !g_hold.queued; });
        const uint64_t since = g_hold.since.load(std::memory_order_relaxed);
        if (!since) continue;
        const uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        const uint64_t bound = g_hold.bound_ns.load(std::memory_order_relaxed);
        g_hold.woken = false; // a completion from here on wakes the wait below
        lk.unlock();
        const bool drained = SeqsInFlight() < kHoldBacklog;
        lk.lock();
        if (now - since < bound && !drained) {
            // until the bound or a completion (NoteSeqCompleted), or a new hold's notify that finds
            // `since` changed (Commit clears it without the lock or a notify: a commit meanwhile
            // is seen only when the wait ends)
            g_hold.cv.wait_for(lk, std::chrono::nanoseconds(since + bound - now), [since] {
                return g_hold.woken || g_hold.since.load(std::memory_order_relaxed) != since;
            });
            continue;
        }
        if (g_hold.since.load(std::memory_order_relaxed) != since) continue; // committed meanwhile
        g_hold.queued = true;
        lk.unlock();
        encq::Defer([] {
            @autoreleasepool {
                ReleaseHeldCompute();
            }
        });
        lk.lock();
    }
}

// Completion thread: a held commit may go now that fewer seqs are in flight.
void NoteSeqCompleted() {
    if (!g_hold.since.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(g_hold.mu);
    g_hold.woken = true;
    g_hold.cv.notify_one();
}

void FlushComputeImpl() {
    if (!S.enabled.load(std::memory_order_acquire)) return;
    if (!g_compute_batch || !MainCmdOpen() || SeqsInFlight() < kHoldBacklog) {
        Commit();
        return;
    }
    if (g_hold.since.load(std::memory_order_relaxed)) return; // already held: its bound stands
    g_hold.since.store(clock_gettime_nsec_np(CLOCK_UPTIME_RAW), std::memory_order_relaxed);
    g_hold.held.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(g_hold.mu);
    if (!g_hold.started) {
        g_hold.started = true;
        std::thread(HoldReleaseLoop).detach();
    }
    g_hold.cv.notify_one();
}

void Commit() {
    g_hold.since.store(0, std::memory_order_relaxed); // held compute work goes with this commit
    EndEncoders();
    g_mem.tick = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    SweepGuestWindows();
    AliasPruneReleased();
    EvictTextures();
    {
        const uint64_t d = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - g_mem.tick;
        g_mem.sweep_ns.fetch_add(d, std::memory_order_relaxed);
        if (d > g_mem.sweep_max_ns.load(std::memory_order_relaxed)) g_mem.sweep_max_ns.store(d, std::memory_order_relaxed);
    }
    if (!S.be->CmdOpen(GnmCmdTarget::Main)) {
        // texture copies for later work: they run before the next seq (same queue); later lookups
        // belong to the next command buffer
        S.be->CommitUpload();
        if (S.tex) S.tex->SetUse(S.committed_seq + 1, g_mem.tick);
        return;
    }
    S.cmd_start_epoch = S.write_epoch + 1; // writes noted from now on belong to the next command buffer
    g_cmd_open.store(false, std::memory_order_release);
    auto* done = new CommitDone;
    done->cmd_index = S.cmd_index;
    // recycle upload chunks when the GPU is done with them
    done->arena = &S.arena;
    done->chunks.reserve(S.arena.cur.size());
    for (auto& c : S.arena.cur) done->chunks.push_back(c.buf);
    S.arena.cur.clear();
    done->log = std::move(S.cmd_log);
    S.cmd_log.reset();
    uint64_t seq;
    {
        std::lock_guard<std::mutex> lk(S.fence_mu);
        seq = ++S.committed_seq;
    }
    done->seq = seq;
    // textures looked up from now on are used by the next command buffer
    if (S.tex) S.tex->SetUse(seq + 1, g_mem.tick);
    // Guest memory written by this command buffer: sampled textures created from it are stale
    // once the GPU is done (TextureCache is thread-safe).
    if (!S.gpu_written.empty()) {
        done->written = std::make_shared<std::vector<GpuWrite>>(S.gpu_written);
        S.inflight_written.push_back({seq, std::move(S.gpu_written)});
        S.gpu_written.clear();
        S.table_written.push_back({seq, done->written});
    }
    S.pending_writes.Commit(seq);
    PruneTableWrites();
    done->tex_cache = S.tex;
    done->present_fail.swap(S.present_ops);
    if (!g_gms.cmd_tickets.empty()) {
        done->tickets = std::make_shared<std::vector<std::shared_ptr<GmTicket>>>(std::move(g_gms.cmd_tickets));
        g_gms.cmd_tickets.clear();
    }
    std::shared_ptr<WorkLog> log = done->log;
    const uint64_t cmd_index = S.cmd_index;
    // PS4_GNM_HANG_REPORT_MS=<ms> (debug): name the work of a command buffer that has not
    // completed that long after its commit. A shader that never ends shows no error until
    // something else needs the GPU (then macOS kills it as "Impacting Interactivity").
    static const long hang_ms = getenv("PS4_GNM_HANG_REPORT_MS") ? atol(getenv("PS4_GNM_HANG_REPORT_MS")) : 0;
    std::shared_ptr<std::atomic<bool>> completed;
    if (hang_ms > 0) done->completed = completed = std::make_shared<std::atomic<bool>>(false);
    // the upload command buffer (texture copies used by this work) is committed first: same queue
    S.be->Commit(seq, CommitDoneFn, done); // done is deleted by CommitDoneFn
    g_gms.committed = g_gms.encoded;
    S.n_commits++;
    if (hang_ms > 0) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, hang_ms * NSEC_PER_MSEC),
                       dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            if (completed->load(std::memory_order_acquire)) return;
            fprintf(stderr, "[gnm-draw] GPU hang report: command buffer gnm-%llu not complete %ld ms after commit\n",
                    (unsigned long long)cmd_index, hang_ms);
            if (log) log->Print("pending work", WorkLog::kAll);
        });
    }
    // drop the logs of completed command buffers
    if (S.inflight_logs.size() >= 8) {
        const uint64_t c = S.be->CompletedSeq();
        while (!S.inflight_logs.empty() && S.inflight_logs.front().seq <= c) S.inflight_logs.pop_front();
    }
    S.inflight_logs.push_back({seq, cmd_index, log});
}

// Waits until every committed command buffer has completed and the guest memory dispatches in
// them have final results (re-running missed ones meanwhile). Sleeps on g_gms.cv, which the
// completion handlers and the ticket listener notify. Watchdog: S.gpu_timeout_ms without any
// command buffer completing (a GPU hang can never block the guest forever).
void WaitAll() {
    const double t0 = CFAbsoluteTimeGetCurrent();
    if (!S.inflight_logs.empty()) S.n_waits++, S.n_waits_total++;
    const auto committed = [] {
        std::lock_guard<std::mutex> lk(S.fence_mu);
        return S.committed_seq;
    };
    while (!S.hung) {
        GmService();
        const uint64_t target = committed();
        const uint64_t done = S.be->CompletedSeq();
        const auto finished = [&] { return done >= target && GmResolved() >= g_gms.committed; };
        if (finished()) break;
        bool progressed;
        {
            encq::Unlocked free_state; // a re-run may be what it waits for
            std::unique_lock<std::mutex> lk(g_gms.mu);
            progressed = g_gms.cv.wait_for(lk, std::chrono::duration<double, std::milli>(S.gpu_timeout_ms), [&] {
                return g_gms.miss.load(std::memory_order_relaxed) || S.be->CompletedSeq() != done ||
                       (done >= target && GmResolved() >= g_gms.committed);
            });
        }
        if (progressed) continue;
        const InflightLog* stuck = nullptr;
        for (const InflightLog& l : S.inflight_logs)
            if (l.seq > done) {
                stuck = &l;
                break;
            }
        char label[64] = "command buffer (guest memory dispatch result)";
        if (done < target && stuck)
            snprintf(label, sizeof(label), "command buffer gnm-%llu", (unsigned long long)stuck->cmd_index);
        GiveUpGpu(label, stuck ? stuck->log.get() : nullptr);
    }
    S.inflight_logs.clear();
    {
        uint64_t upto;
        {
            std::lock_guard<std::mutex> lk(S.fence_mu);
            upto = S.committed_seq;
        }
        RunFences(upto);
    }
    // everything committed has completed; its completion handlers may still be pending, so
    // drop stale sampled textures here as well
    if (S.tex && !S.inflight_written.empty()) {
        std::vector<GpuWrite> all;
        for (auto& w : S.inflight_written) all.insert(all.end(), w.second.begin(), w.second.end());
        S.tex->InvalidateWrites(all);
    }
    S.inflight_written.clear();
    // everything committed has completed (committed_seq is written by this thread only: Commit)
    S.pending_writes.Prune(S.committed_seq);
    S.pending = false;
    GmService(); // finish the guest memory dispatches whose results came in
    g_hitch.wait_ms += (CFAbsoluteTimeGetCurrent() - t0) * 1000.0;
    S.wait_ms += (CFAbsoluteTimeGetCurrent() - t0) * 1000.0;
}

// =====================================================================================
// Shaders
// =====================================================================================
void DumpMsl(const Shader* s, const char* msl) {
    static const char* dir = getenv("PS4_GNM_MSL_DUMP");
    if (!dir || !msl) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s_%llx_%llu.metal", dir,
             s->hw == GCN_HW_VS ? "vs" : s->hw == GCN_HW_PS ? "ps" : s->hw == GCN_HW_CS ? "cs" : "xx",
             (unsigned long long)s->code_addr, (unsigned long long)s->uid);
    FILE* f = fopen(path, "w");
    if (!f) return;
    fputs(msl, f);
    fclose(f);
    // the GCN binary next to it (for gcn_dis)
    if (s->info.code_size && s->info.code_size <= (1u << 22)) {
        std::vector<uint8_t> code(s->info.code_size);
        if (ReadGuest(S.ctx, s->code_addr, code.data(), code.size())) {
            snprintf(path + strlen(path) - 6, 8, ".bin");
            if (FILE* b = fopen(path, "wb")) {
                fwrite(code.data(), 1, code.size(), b);
                fclose(b);
            }
        }
    }
}

// ---- On-disk MSL cache + warm-up -------------------------------------------------------
// Every MSL source compiled is stored as <cache>/<fnv64>.metal. At startup background threads
// compile the cached sources so shaders seen in earlier sessions are ready (and the system Metal
// shader cache is warm) before the game needs them.
// Returns true if the MSL was new (not in the disk cache yet).
bool StoreMslInDiskCache(uint64_t mh, const char* msl) {
    if (S.msl_cache_dir.empty()) return true;
    const uint64_t fs_t0 = ps4_hitch_now_ns();
    char path[1200];
    snprintf(path, sizeof(path), "%s/%016llx.metal", S.msl_cache_dir.c_str(), (unsigned long long)mh);
    const bool is_new = S.msl_on_disk.insert(mh).second; // CP thread only, filled at startup
    if (!is_new) {
        ps4gnm_shader_cache::MarkUsed(path); // asynchronous
    } else {
        // The frame does not wait for the disk. A write still pending at quit is lost; the
        // shader is then translated again in the next session.
        // Utility QoS: otherwise the queue would run at the QoS of the CP thread that submits.
        static dispatch_queue_t writer = dispatch_queue_create(
            "ps4.gnm.msl_cache.writer", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0));
        auto* job = new std::pair<std::string, std::string>(path, msl);
        dispatch_async(writer, ^{
            char tmp[1300];
            snprintf(tmp, sizeof(tmp), "%s.tmp%d", job->first.c_str(), (int)getpid());
            bool ok = false;
            if (FILE* f = fopen(tmp, "w")) {
                const bool put_ok = fputs(job->second.c_str(), f) >= 0;
                const bool close_ok = fclose(f) == 0;
                ok = put_ok && close_ok;
            }
            if (ok && rename(tmp, job->first.c_str()) != 0) ok = false;
            if (!ok) {
                const int err = errno;
                unlink(tmp);
                fprintf(stderr, "[gnm-draw] msl cache write failed: %s: %s\n", job->first.c_str(), strerror(err));
            }
            delete job;
        });
    }
    ps4_hitch_add_ns(PS4_HITCH_SHADER_FS, ps4_hitch_now_ns() - fs_t0);
    return is_new;
}

MTLCompileOptions* CompileOptions() {
    MTLCompileOptions* opt = [MTLCompileOptions new];
    // Kept as before (not mathMode): the options are part of the system Metal cache's key, so a
    // different spelling of the same options could make the player's cache miss once.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    opt.fastMathEnabled = NO;
#pragma clang diagnostic pop
    opt.preserveInvariance = YES;
    return opt;
}

uint32_t StoreStage(uint32_t hw) {
    return hw == GCN_HW_VS ? ps4gnm_lib_store::kVertex
           : hw == GCN_HW_PS ? ps4gnm_lib_store::kFragment
           : hw == GCN_HW_CS ? ps4gnm_lib_store::kKernel
                             : 0;
}

// Warm-up: background threads make the shaders of earlier sessions ready before the game asks
// for them. Libraries in the store are loaded first (about a millisecond each), then cached MSL
// without a store entry is compiled (hundreds of ms each when the system Metal cache misses).
// Within each group the shaders the game met first in its history (oldest file creation time:
// title screen and menus) come first.
void StartShaderWarmup() {
    if (getenv("PS4_GNM_NO_SHADER_CACHE")) return;
    const std::string title_dir = ps4gnm_shader_cache::TitleDir();
    if (title_dir.empty()) return;
    S.msl_cache_dir = title_dir + "/msl";
    [[NSFileManager defaultManager] createDirectoryAtPath:[NSString stringWithUTF8String:S.msl_cache_dir.c_str()]
                              withIntermediateDirectories:YES attributes:nil error:nil];
    S.pso_list_path = S.msl_cache_dir + "/pipelines.bin";
    // MSL of older recompiler builds is never asked for again; the size limit drops it
    ps4gnm_shader_cache::PruneDir(S.msl_cache_dir, ".metal", ps4gnm_shader_cache::BudgetBytes(48), "MSL");
    if (FILE* pf = fopen(S.pso_list_path.c_str(), "rb")) {
        uint64_t h;
        while (fread(&h, sizeof(h), 1, pf) == 1) S.known_psos.insert(h);
        fclose(pf);
    }
    {
        // PS4_GNM_LIB_STORE_MB: size budget of the library store; not a positive number -> 256
        const char* mb = getenv("PS4_GNM_LIB_STORE_MB");
        char* end = nullptr;
        long long v = mb ? strtoll(mb, &end, 10) : 0;
        if (!mb || end == mb || *end || v <= 0 || v > (1ll << 20)) v = 256;
        // the configuration is derived from these options (ps4gnm_lib_store::OptionsTag)
        ps4gnm_lib_store::Open(S.dev, title_dir, CompileOptions(), (uint64_t)v << 20);
    }
    struct Item {
        std::string path;
        ps4gnm_lib_store::Key key; // store entries: h1/h2 from the file name
        bool store = false;
        struct timespec born;
    };
    auto by_birth = [](const Item& a, const Item& b) {
        return a.born.tv_sec != b.born.tv_sec ? a.born.tv_sec < b.born.tv_sec : a.born.tv_nsec < b.born.tv_nsec;
    };
    std::vector<Item> stored, sources;
    std::unordered_set<uint64_t> stored_h1;
    struct stat st;
    if (ps4gnm_lib_store::Enabled()) {
        const std::string& sd = ps4gnm_lib_store::Dir();
        if (DIR* d = opendir(sd.c_str())) {
            while (dirent* de = readdir(d)) {
                Item it;
                if (!ps4gnm_lib_store::ParseFileName(de->d_name, &it.key)) continue;
                it.path = sd + "/" + de->d_name;
                if (stat(it.path.c_str(), &st) != 0) continue;
                it.store = true;
                it.born = st.st_birthtimespec;
                stored_h1.insert(it.key.h1);
                stored.push_back(it);
            }
            closedir(d);
        }
    }
    if (DIR* d = opendir(S.msl_cache_dir.c_str())) {
        while (dirent* de = readdir(d)) {
            const size_t n = strlen(de->d_name);
            if (n != 22 || strcmp(de->d_name + 16, ".metal") != 0) continue;
            char* end = nullptr;
            const uint64_t h = strtoull(de->d_name, &end, 16);
            if (end != de->d_name + 16) continue;
            S.msl_on_disk.insert(h);
            if (stored_h1.count(h)) continue; // loaded from the store instead
            Item it;
            it.path = S.msl_cache_dir + "/" + de->d_name;
            if (stat(it.path.c_str(), &st) != 0) continue;
            it.key.h1 = h;
            it.born = st.st_birthtimespec;
            sources.push_back(it);
        }
        closedir(d);
    }
    std::sort(stored.begin(), stored.end(), by_birth);
    std::sort(sources.begin(), sources.end(), by_birth);
    const size_t n_stored = stored.size();
    auto items = std::make_shared<std::vector<Item>>(std::move(stored));
    items->insert(items->end(), sources.begin(), sources.end());
    if (items->empty()) return;
    fprintf(stderr, "[gnm-draw] warming up %zu cached shaders in the background (%zu from the library store, %zu from MSL)\n",
            items->size(), n_stored, items->size() - n_stored);
    id<MTLDevice> dev = S.dev;
    const NSUInteger workers = std::max<NSUInteger>(1, [[NSProcessInfo processInfo] activeProcessorCount] / 2);
    auto next = std::make_shared<std::atomic<size_t>>(0);
    auto done = std::make_shared<std::atomic<size_t>>(0);
    auto store_done = std::make_shared<std::atomic<size_t>>(0);
    const double t0 = CFAbsoluteTimeGetCurrent();
    for (NSUInteger w = 0; w < workers; ++w) {
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            for (size_t i; (i = next->fetch_add(1)) < items->size();) {
                const Item& it = (*items)[i];
                @autoreleasepool {
                    bool skip;
                    {
                        std::lock_guard<std::mutex> lk(S.async_mu);
                        skip = S.prewarm_taken.count(it.key.h1) != 0;
                    }
                    if (skip) {
                    } else if (it.store) {
                        ps4gnm_lib_store::Key full;
                        uint32_t stage = 0;
                        id<MTLFunction> fn = nil;
                        if (ps4gnm_lib_store::Peek(it.key, &full, &stage)) fn = ps4gnm_lib_store::Load(full, stage, nullptr);
                        if (fn) {
                            std::lock_guard<std::mutex> lk(S.async_mu);
                            if (!S.prewarm_taken.count(full.h1)) S.prewarmed[full.h1] = {full.h2, nil, fn};
                        }
                    } else {
                        NSString* src = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:it.path.c_str()]
                                                                  encoding:NSUTF8StringEncoding error:nil];
                        const char* c = src ? src.UTF8String : nullptr;
                        const uint64_t mh = c ? Fnv(c, strlen(c)) : 0;
                        id<MTLLibrary> lib = c ? [dev newLibraryWithSource:src options:CompileOptions() error:nil] : nil;
                        if (lib) {
                            std::lock_guard<std::mutex> lk(S.async_mu);
                            if (!S.prewarm_taken.count(mh)) S.prewarmed[mh] = {0, lib, nil};
                        }
                    }
                }
                if (i < n_stored && store_done->fetch_add(1) + 1 == n_stored)
                    fprintf(stderr, "[gnm-draw] shader warm-up: %zu libraries from the store ready in %.2f s\n", n_stored,
                            CFAbsoluteTimeGetCurrent() - t0);
                if (done->fetch_add(1) + 1 == items->size())
                    fprintf(stderr, "[gnm-draw] shader warm-up finished: %zu shaders in %.1f s\n", items->size(),
                            CFAbsoluteTimeGetCurrent() - t0);
            }
        });
    }
}

// Applies finished background compilations (called on the submitting thread).
Shader* FinishShader(gcn_shader* sh, gcn_hw_stage hw, uint64_t code_addr, uint64_t key, uint64_t fail_key,
                     bool allow_async, double t0);

void DrainAsyncCompiles() {
    // shader side (CP thread); finished pipelines are taken by DrainAsyncPsos (encoder)
    std::vector<State::AsyncLib> libs;
    std::vector<State::AsyncPso> psos;
    std::vector<std::shared_ptr<State::GcnJob>> jobs;
    if (S.async_shaders_ready.load(std::memory_order_acquire) == 0) return;
    {
        std::lock_guard<std::mutex> lk(S.async_mu);
        S.async_shaders_ready.store(0, std::memory_order_relaxed);
        if (S.async_libs_done.empty() && S.async_gcn_done.empty()) return;
        libs.swap(S.async_libs_done);
        jobs.swap(S.async_gcn_done);
    }
    for (auto& j : jobs) {
        S.gcn_pending.erase(j->pend_key);
        S.gcn_pending_prog.erase(j->key);
        if (!j->sh) {
            // The worker read the draw's memory later than the draw (it may hold another draw's
            // sharps by then): translate it again on the CP thread at the next draw, and only
            // give up if that fails too.
            RLog("compile-fail-async", "shader 0x%llx (hw %u) failed to compile on the worker: status %d: %s",
                 (unsigned long long)j->code_addr, j->hw, j->status, j->err.c_str());
            S.async_retry.insert(j->fail_key);
            continue;
        }
        if (Shader* fs = FinishShader(j->sh, j->hw, j->code_addr, j->key, j->fail_key, true, CFAbsoluteTimeGetCurrent()))
            fs->from_worker = true;
    }
    for (auto& r : libs) {
        auto it = S.libs_pending.find(r.mh);
        if (it == S.libs_pending.end()) continue;
        if (r.lib) S.libs[r.mh] = r.lib;
        for (Shader* sh : it->second) {
            sh->pending = false;
            if (r.lib) sh->fn = [r.lib newFunctionWithName:@"main0"];
            if (!sh->fn) {
                RLog("msl-fail", "Metal rejected MSL of shader 0x%llx (hw %u): %s", (unsigned long long)sh->code_addr,
                     sh->hw, r.err ? r.err.UTF8String : "no main0");
                S.shader_fail.insert(sh->fail_key);
            }
        }
        S.libs_pending.erase(it);
    }
}

bool UsesStoredFn(const Shader* s);
bool ReplaceStoredFns(const Shader* vs, const Shader* ps);

void DrainAsyncPsos() {
    std::vector<State::AsyncPso> psos;
    if (S.async_psos_ready.load(std::memory_order_acquire) == 0) return;
    {
        std::lock_guard<std::mutex> lk(S.async_mu);
        S.async_psos_ready.store(0, std::memory_order_relaxed);
        if (S.async_psos_done.empty()) return;
        psos.swap(S.async_psos_done);
    }
    for (auto& r : psos) {
        S.psos_pending.erase(r.key);
        if (!r.pso && (r.retry_vs || r.retry_ps)) {
            // built with a stored function: compile its MSL; the next draw builds the pipeline again
            if (ReplaceStoredFns(r.retry_vs, r.retry_ps)) continue;
        }
        if (!r.pso)
            RLog("pso-fail", "render pipeline failed: %s", r.err ? r.err.UTF8String : "unknown error");
        S.gfx_pipes[r.key] = r.pso;
    }
}

bool StoreMslInDiskCache(uint64_t mh, const char* msl);

// The MSL that is compiled for a translated shader: the recompiler's output after the renderer's
// post-processing (its hash keys the MSL cache, the library store and the pipelines).
std::string FinalMsl(const gcn_shader* sh, uint32_t hw) {
    // SPIRV-Cross implements SPIR-V NoContraction with [[clang::optnone]] helper functions
    // (spvFMul/spvFAdd/...), i.e. a real function call per float op on the GPU. Dropping the
    // attribute lets the compiler inline them (fma contraction may change the last ulp).
    static const bool keep_optnone = getenv("PS4_GNM_KEEP_OPTNONE") != nullptr;
    std::string msl_str = gcn_shader_msl(sh);
    if (!keep_optnone) {
        const std::string tag = "[[clang::optnone]] ";
        for (size_t pos; (pos = msl_str.find(tag)) != std::string::npos;) msl_str.erase(pos, tag.size());
    }
    // Vertex positions must be bit-identical between pipelines that draw the same geometry
    // (depth pre-pass followed by a depth-EQUAL pass), like invariant/precise positions on the
    // console: mark the position output invariant (compiled with preserveInvariance).
    if (hw == GCN_HW_VS) {
        const std::string pos = "[[position]]";
        for (size_t at = 0; (at = msl_str.find(pos, at)) != std::string::npos; at += 23)
            msl_str.replace(at, pos.size(), "[[position, invariant]]");
    }
    return msl_str;
}

// The function a pipeline is built with (encoder thread): the shader's own, or the one compiled
// from MSL after its stored function failed in a pipeline.
id<MTLFunction> PipelineFn(const Shader* s) {
    if (!s) return nil;
    if (s->kjob) return s->kjob->Wait(); // DispatchEncode waited for it already
    if (s->from_store)
        if (auto it = S.fallback_fns.find(s->msl_hash); it != S.fallback_fns.end()) return it->second;
    return s->fn;
}

// True if the shader's pipelines are built with a function loaded from the library store.
bool UsesStoredFn(const Shader* s) {
    return s && s->from_store && !S.fallback_fns.count(s->msl_hash);
}

// Compiles a shader's MSL for its pipelines (encoder thread); false if it does not compile.
bool CompileFallbackFn(const Shader* s) {
    const std::string msl = FinalMsl(s->sh, s->hw);
    NSError* e = nil;
    const double t0 = CFAbsoluteTimeGetCurrent();
    id<MTLLibrary> lib = [S.dev newLibraryWithSource:@(msl.c_str()) options:CompileOptions() error:&e];
    g_compile.lib_sync.Add((CFAbsoluteTimeGetCurrent() - t0) * 1000.0);
    id<MTLFunction> fn = [lib newFunctionWithName:@"main0"];
    S.fallback_fns[s->msl_hash] = fn; // nil too: the shader is not retried again
    g_compile.store_fallbacks++;
    if (!fn)
        RLog("msl-fail", "shader 0x%llx: MSL of a failed stored library does not compile: %s",
             (unsigned long long)s->code_addr, e ? e.localizedDescription.UTF8String : "no main0");
    return fn != nil;
}

// One pipeline failed (encoder thread). Of its shaders, those whose pipelines are built with a
// stored function get their MSL compiled instead, and their store entries are deleted and
// reported as one failure toward the store's session circuit breaker
// (ps4gnm_lib_store::ReportPipelineFailure). False if no shader used a stored function (the
// failure is not the store's) or an MSL does not compile; true: build the pipeline again.
bool ReplaceStoredFns(const Shader* vs, const Shader* ps) {
    const bool v = UsesStoredFn(vs), f = UsesStoredFn(ps);
    if (!v && !f) return false;
    ps4gnm_lib_store::ReportPipelineFailure(v ? &vs->lib_key : nullptr, f ? &ps->lib_key : nullptr);
    bool ok = true;
    if (v) ok = CompileFallbackFn(vs) && ok;
    if (f) ok = CompileFallbackFn(ps) && ok;
    return ok;
}

// Second half of shader creation: reflection, MSL post-processing and the Metal library
// (from the cache, the warm-up set, a background compile, or synchronously).
Shader* FinishShader(gcn_shader* sh, gcn_hw_stage hw, uint64_t code_addr, uint64_t key, uint64_t fail_key,
                     bool allow_async, double t0) {
    auto& vec = S.shaders[key];
    Shader* s = new Shader();
    s->sh = sh;
    s->hw = hw;
    s->code_addr = code_addr;
    s->uid = S.next_uid++;
    gcn_shader_get_info(sh, &s->info);
    if (s->info.null_image_reads) {
        // translated now or loaded from the shader cache: the translator's own log line is not
        // repeated for a cached shader
        char k[64];
        snprintf(k, sizeof(k), "null-image-%llx-%u", (unsigned long long)code_addr, (unsigned)hw);
        RLog(k, "shader 0x%llx (hw %u): %u image read(s) through a T# loaded at run time read the null image",
             (unsigned long long)code_addr, (unsigned)hw, s->info.null_image_reads);
    }
    if (s->info.zeroed_write_lanes) {
        // known translator gap: v_writelane results are emitted as 0 (see EmitWriteLane)
        char k[64];
        snprintf(k, sizeof(k), "zero-writelane-%llx-%u", (unsigned long long)code_addr, (unsigned)hw);
        RLog(k, "shader 0x%llx (hw %u): %u v_writelane result(s) emitted as 0 (known translator gap)",
             (unsigned long long)code_addr, (unsigned)hw, s->info.zeroed_write_lanes);
    }
    if (s->info.unpredicated_wave_regions || s->info.per_subgroup_lane_ops || s->info.per_lane_ends) {
        // known translator gaps of wave64 compute shaders on 32-wide simdgroups
        char k[64];
        snprintf(k, sizeof(k), "wave64-gap-%llx-%u", (unsigned long long)code_addr, (unsigned)hw);
        RLog(k, "shader 0x%llx (hw %u): %u EXEC-divergent if/loop(s) and %u cross-lane read(s) left per "
                "32-wide subgroup, %u end(s) in divergent code ending only their lanes (known translator gap)",
             (unsigned long long)code_addr, (unsigned)hw, s->info.unpredicated_wave_regions,
             s->info.per_subgroup_lane_ops, s->info.per_lane_ends);
    }
    s->bufs.resize(s->info.num_buffers);
    for (uint32_t i = 0; i < s->info.num_buffers; ++i) gcn_shader_get_buffer(sh, i, &s->bufs[i]);
    s->imgs.resize(s->info.num_images);
    for (uint32_t i = 0; i < s->info.num_images; ++i) gcn_shader_get_image(sh, i, &s->imgs[i]);
    s->samps.resize(s->info.num_samplers);
    for (uint32_t i = 0; i < s->info.num_samplers; ++i) gcn_shader_get_sampler(sh, i, &s->samps[i]);
    s->tables.resize(s->info.num_image_tables);
    for (uint32_t i = 0; i < s->info.num_image_tables; ++i) gcn_shader_get_image_table(sh, i, &s->tables[i]);
    s->attrs.resize(s->info.num_vertex_attribs);
    for (uint32_t i = 0; i < s->info.num_vertex_attribs; ++i)
        gcn_shader_get_vertex_attrib(sh, i, &s->attrs[i]);

    const std::string msl_str = FinalMsl(sh, hw);
    const char* msl = msl_str.c_str();
    DumpMsl(s, msl);
    const uint64_t mh = Fnv(msl, msl_str.size());
    s->msl_hash = mh;
    s->lib_key = ps4gnm_lib_store::MakeKey(msl, msl_str.size());
    s->lib_stage = StoreStage(hw);
    s->fail_key = fail_key;
    // The function, in this order: built earlier in the session, made ready by the warm-up, loaded
    // from the library store (about a millisecond, no compiler), compiled from the MSL.
    id<MTLLibrary> lib = nil;
    id<MTLFunction> stored_fn = nil;
    const bool store_on = ps4gnm_lib_store::Enabled(); // false once its circuit breaker tripped
    if (auto fit = S.store_fns.find(mh); store_on && fit != S.store_fns.end()) {
        stored_fn = fit->second;
    } else if (auto lit = S.libs.find(mh); lit != S.libs.end()) {
        lib = lit->second;
    } else {
        {
            std::lock_guard<std::mutex> lk(S.async_mu);
            auto pit = S.prewarmed.find(mh);
            if (pit != S.prewarmed.end()) {
                if (pit->second.fn ? store_on && pit->second.h2 == s->lib_key.h2 : pit->second.lib != nil) {
                    lib = pit->second.lib;
                    stored_fn = pit->second.fn;
                    g_compile.lib_prewarmed++;
                }
                S.prewarmed.erase(pit);
            }
        }
        if (!lib && !stored_fn && s->lib_stage && ps4gnm_lib_store::Has(s->lib_key)) {
            const double ld_t0 = CFAbsoluteTimeGetCurrent();
            stored_fn = ps4gnm_lib_store::Load(s->lib_key, s->lib_stage, nullptr);
            const double ld_ms = (CFAbsoluteTimeGetCurrent() - ld_t0) * 1000.0;
            g_hitch.mtl_ms += ld_ms;
            g_compile.lib_store.Add(ld_ms);
        }
        if (stored_fn) {
            S.store_fns[mh] = stored_fn;
        } else if (lib) {
            S.libs[mh] = lib;
        }
        {
            std::lock_guard<std::mutex> lk(S.async_mu);
            S.prewarm_taken.insert(mh);
        }
        if (lib || stored_fn) {
            char path[1200];
            snprintf(path, sizeof(path), "%s/%016llx.metal", S.msl_cache_dir.c_str(), (unsigned long long)mh);
            ps4gnm_shader_cache::MarkUsed(path);
        }
    }
    bool msl_known = false;
    if (!lib && !stored_fn) msl_known = !StoreMslInDiskCache(mh, msl);
    // MSL compiled in an earlier session is cheap to compile again (Metal's shader cache), so
    // only new shaders are compiled in the background. The same MSL already compiling in the
    // background (another permutation or address of the program, in this session) waits for
    // that compile instead of compiling it again inside the frame.
    const bool compiling = !lib && !stored_fn && S.libs_pending.count(mh) != 0;
    if (!lib && !stored_fn && allow_async && S.async_compile && (!msl_known || compiling)) {
        // Compile the Metal library in the background; draws using this shader are skipped
        // until it is ready instead of stalling the frame (a cold Metal compile can take
        // 100+ ms per shader, and new areas bring dozens at once).
        s->pending = true;
        auto& waiters = S.libs_pending[mh];
        const bool first = waiters.empty();
        waiters.push_back(s);
        if (first) {
            g_compile.lib_async++;
            auto done = [mh](id<MTLLibrary> l, NSString* err) {
                std::lock_guard<std::mutex> lk(S.async_mu);
                S.async_libs_done.push_back({mh, l, l ? nil : err});
                S.async_shaders_ready.fetch_add(1, std::memory_order_release);
            };
            if (S.kernels) {
                // the shared compile queue: bounded, behind the kernels dispatches wait for
                bool started = false;
                auto job = S.kernels->Start(mh, msl_str, &started, ps4gnm_kernels::Compiler::kDraw,
                                            [done](const ps4gnm_kernels::Job& j) {
                                                id<MTLLibrary> l = j.Library();
                                                done(l, l ? nil : @(j.Error().c_str()));
                                            });
                // compiled before in this session (a failed library is not cached in S.libs):
                // the waiters take that result (the job of an MSL is never in flight here,
                // libs_pending holds its waiters until it is done)
                if (!started && job->Done()) {
                    done(job->Library(), job->Library() ? nil : @(job->Error().c_str()));
                } else if (!started) {
                    // not expected (see above); its draws would wait for a callback that never comes
                    RLog("msl-fail", "shader MSL %016llx: compile job of another caller still in flight; draws skipped",
                         (unsigned long long)mh);
                }
            } else {
                [S.dev newLibraryWithSource:[NSString stringWithUTF8String:msl]
                                    options:CompileOptions()
                          completionHandler:^(id<MTLLibrary> l, NSError* e) {
                              done(l, l ? nil : e.localizedDescription);
                          }];
            }
        }
    } else if (!lib && !stored_fn && hw == GCN_HW_CS && S.kernels) {
        // A dispatch is not optional (later passes read its results), so it is not skipped
        // while its kernel compiles: the compile starts here, and the encoder waits for it when
        // it reaches the dispatch. The CP thread goes on with the command stream meanwhile, and
        // kernels met before the encoder catches up compile in parallel.
        s->kjob = S.kernels->Start(mh, msl_str);
        if (s->kjob->Done() && !s->kjob->Wait()) {
            // the same MSL failed before in this session
            RLog("msl-fail", "Metal rejected MSL of shader 0x%llx (hw %u): %s", (unsigned long long)code_addr, hw,
                 s->kjob->Error().c_str());
            gcn_shader_destroy(sh);
            delete s;
            S.shader_fail.insert(fail_key);
            return nullptr;
        }
    } else if (!lib && !stored_fn) {
        NSError* e = nil;
        const double mtl_t0 = CFAbsoluteTimeGetCurrent();
        lib = [S.dev newLibraryWithSource:[NSString stringWithUTF8String:msl] options:CompileOptions() error:&e];
        g_hitch.mtl_ms += (CFAbsoluteTimeGetCurrent() - mtl_t0) * 1000.0;
        g_compile.lib_sync.Add((CFAbsoluteTimeGetCurrent() - mtl_t0) * 1000.0);
        if (!lib) {
            RLog("msl-fail", "Metal rejected MSL of shader 0x%llx (hw %u): %s",
                 (unsigned long long)code_addr, hw, e.localizedDescription.UTF8String);
            gcn_shader_destroy(sh);
            delete s;
            S.shader_fail.insert(fail_key);
            return nullptr;
        }
        S.libs[mh] = lib;
    }
    if (stored_fn) {
        s->fn = stored_fn;
        s->from_store = true;
    } else if (lib) {
        const double fn_t0 = CFAbsoluteTimeGetCurrent();
        s->fn = [lib newFunctionWithName:@"main0"];
        g_hitch.fn_ms += (CFAbsoluteTimeGetCurrent() - fn_t0) * 1000.0;
    }
    if (!s->fn && !s->pending && !s->kjob) {
        RLog("msl-fail", "shader 0x%llx: no main0", (unsigned long long)code_addr);
        gcn_shader_destroy(sh);
        delete s;
        S.shader_fail.insert(fail_key);
        return nullptr;
    }
    const double ms = (CFAbsoluteTimeGetCurrent() - t0) * 1000.0;
    g_hitch.shader_ms += ms;
    g_hitch.shaders++;
    RLog("compiled", "compiled %s shader 0x%llx perm %zu: %u bufs %u imgs %u samplers %u attribs%s "
                     "(%.0f ms)",
         hw == GCN_HW_VS ? "VS" : hw == GCN_HW_PS ? "PS" : hw == GCN_HW_CS ? "CS" : "??",
         (unsigned long long)code_addr, vec.size(), s->info.num_buffers, s->info.num_images,
         s->info.num_samplers, s->info.num_vertex_attribs,
         s->info.translation_failed ? " [translation incomplete]" : "", ms);
    if (s->info.fetch_shader_inlined) {
        // a VS whose vertex V#s Metal's vertex fetch cannot read (stride, address or format):
        // its fetch shader's loads run in the shader (gcn Translator::EmitFetch)
        // (RLog counts them: "(xN)" on the powers of two)
        const uint32_t why = s->info.fetch_inline_reason & 7;
        // counted per reason (RLog's "(xN)"): one key per combination
        static const char* const kKeys[8] = {"fetch-inlined", "fetch-inlined-s", "fetch-inlined-a", "fetch-inlined-sa",
                                             "fetch-inlined-f", "fetch-inlined-sf", "fetch-inlined-af",
                                             "fetch-inlined-saf"};
        RLog(kKeys[why], "VS 0x%llx perm %zu reads its vertices in the shader (fetch shader inlined): attribute "
             "%u has %s%s%s", (unsigned long long)code_addr, vec.size(), s->info.fetch_inline_attrib,
             why & GCN_FETCH_INLINE_STRIDE ? "[stride not a multiple of 4]" : "",
             why & GCN_FETCH_INLINE_ADDRESS ? "[address not a multiple of 4]" : "",
             why & GCN_FETCH_INLINE_FORMAT ? "[no Metal vertex format]" : "");
    }
    vec.push_back(s);
    return s;
}

// Identifies the program currently stored at a shader address. Games reuse shader memory for
// different programs (e.g. when they switch render paths), so the address alone is not a key.
// Compiled PS4 shaders start with "s_mov_b32 vcc_hi, <offset>" pointing at the OrbShdr binary
// info, which holds the shader hash; other code is identified by its first 64 bytes.
uint64_t CodeSignature(uint64_t code_addr) {
    uint32_t head[2];
    if (!ReadGuest(S.ctx, code_addr, head, sizeof(head))) return 0;
    if (head[0] == 0xBEEB03FFu) {
        uint32_t size = 0;
        uint64_t hash = 0;
        if (gcn_find_binary_info(ReadGuest, S.ctx, code_addr, &size, &hash) && hash)
            return hash ^ ((uint64_t)size << 32);
    }
    uint8_t prefix[64];
    if (!ReadGuest(S.ctx, code_addr, prefix, sizeof(prefix))) return 0;
    return Fnv(prefix, sizeof(prefix));
}

Shader* GetShader(gcn_hw_stage hw, gcn_sw_stage sw, const gcn_runtime_info& rt, uint64_t code_addr,
                  const uint32_t ud[16], bool allow_async = false) {
    if (code_addr == 0 || code_addr >= S.ctx->mem_size) return nullptr;
    const uint64_t sig = CodeSignature(code_addr);
    const uint64_t key = Fnv(&sig, sizeof(sig), code_addr ^ ((uint64_t)hw << 58));
    auto& vec = S.shaders[key];
    const int fi = hw == GCN_HW_VS ? 0 : hw == GCN_HW_PS ? 1 : hw == GCN_HW_CS ? 2 : -1;
    if (fi >= 0) S.flat_from[fi] = nullptr;
    if (!vec.empty()) {
        // The lookup builds the draw's specialization from list[0]: a permutation translated on
        // this thread from the draw's own memory, not one from the worker (see from_worker).
        const uint32_t n = (uint32_t)std::min<size_t>(vec.size(), 64);
        uint32_t base = 0;
        while (base + 1 < n && vec[base]->from_worker) ++base;
        const gcn_shader* list[64];
        uint32_t order[64];
        order[0] = base;
        for (uint32_t i = 0, k = 1; i < n; ++i)
            if (i != base) order[k++] = i;
        for (uint32_t i = 0; i < n; ++i) list[i] = vec[order[i]]->sh;
        int hit;
        if (fi >= 0) {
            // walk the pointer chains once: the flattened user data serves the check here and
            // the binding of the draw
            BuildFlat(vec[base], ud, &S.flat_scratch[fi]);
            S.flat_from[fi] = vec[base];
            hit = GmCpGuarded([&](gcn_read_guest_fn read) {
                return gcn_shader_find_compatible_flat(list, n, ud, S.flat_scratch[fi].data(),
                                                       S.flat_scratch[fi].size(), &rt, read, S.ctx);
            });
        } else {
            hit = GmCpGuarded([&](gcn_read_guest_fn read) {
                return gcn_shader_find_compatible(list, n, ud, &rt, read, S.ctx);
            });
        }
        if (hit >= 0) return vec[order[hit]];
        static std::set<uint64_t>& explained = *new std::set<uint64_t>; // once per program and count
        if ((n == 8 || n == 16 || n == 32 || n == 47) && explained.insert(key * 64 + n).second) {
            // Many permutations of one program: say why the first and the latest do not fit.
            char why0[256], why1[256];
            gcn_shader_explain_mismatch(vec[base]->sh, ud, &rt, ReadGuest, S.ctx, why0, sizeof(why0));
            gcn_shader_explain_mismatch(vec[n - 1]->sh, ud, &rt, ReadGuest, S.ctx, why1, sizeof(why1));
            fprintf(stderr, "[gnm-draw] shader 0x%llx (hw %u) has %u permutations; no fit: base %u%s: %s; latest%s: %s\n",
                    (unsigned long long)code_addr, hw, n, base, vec[base]->from_worker ? " (worker)" : "", why0,
                    vec[n - 1]->from_worker ? " (worker)" : "", why1);
        }
    }
    const uint64_t fail_key = Fnv(&rt, sizeof(rt), key * 1000003ull);
    if (S.shader_fail.count(fail_key)) return nullptr;
    if (vec.size() >= 48) {
        RLog("perm", "too many permutations for shader 0x%llx stage %u", (unsigned long long)code_addr, hw);
        return nullptr;
    }

    // A translation of this permutation is already in flight.
    const uint64_t pend_key = Fnv(ud, 64, Fnv(&rt, sizeof(rt), key));
    if (allow_async && S.async_compile && S.gcn_pending.count(pend_key)) return &S.pending_shader;

    // Persistent translation cache: a permutation translated in an earlier session that is
    // compatible with this draw replaces the translation (synchronously, it is cheap).
    namespace sc = ps4gnm_shader_cache;
    sc::Key ckey;
    const bool cacheable = sc::Enabled() && sc::MakeKey(hw, sw, rt, code_addr, ReadGuest, S.ctx, &ckey);
    if (cacheable) {
        const double t0 = CFAbsoluteTimeGetCurrent();
        gcn_shader* sh = sc::Lookup(ckey, code_addr, ud, ReadGuest, S.ctx);
        g_hitch.lookup_ms += (CFAbsoluteTimeGetCurrent() - t0) * 1000.0;
        if (sh) {
            if (sc::VerifyMode()) {
                // Debug: the cached shader must equal a fresh translation of this draw.
                static double cache_ms = 0, gcn_ms = 0;
                const double t1 = CFAbsoluteTimeGetCurrent();
                cache_ms += (t1 - t0) * 1000.0;
                gcn_compile_desc vd{};
                vd.hw_stage = hw;
                vd.sw_stage = sw;
                vd.code_addr = code_addr;
                memcpy(vd.user_data, ud, 64);
                vd.runtime = &rt;
                vd.profile = &S.profile;
                vd.read_guest = ReadGuest;
                vd.read_ctx = S.ctx;
                vd.flags = GCN_COMPILE_EMIT_MSL;
                vd.msl = &S.msl;
                char verr[256] = {0};
                gcn_shader* fresh = nullptr;
                std::string why;
                const gcn_status vst = gcn_compile(&vd, &fresh, verr, sizeof(verr));
                gcn_ms += (CFAbsoluteTimeGetCurrent() - t1) * 1000.0;
                if (vst != GCN_OK)
                    why = std::string("fresh translation failed: ") + verr;
                else if (!sc::SameTranslation(sh, fresh, &why))
                    why = "differs in " + why;
                if (fresh) gcn_shader_destroy(fresh);
                if (why.empty())
                    RLog("gcn-cache-verify",
                         "cached %s shader 0x%llx matches a fresh translation (so far: cache %.1f ms, "
                         "translation %.1f ms)",
                         hw == GCN_HW_VS ? "VS" : hw == GCN_HW_PS ? "PS" : hw == GCN_HW_CS ? "CS" : "??",
                         (unsigned long long)code_addr, cache_ms, gcn_ms);
                else
                    fprintf(stderr, "[gnm-draw] gcn cache verify: shader 0x%llx (hw %u): %s\n",
                            (unsigned long long)code_addr, hw, why.c_str());
            }
            return FinishShader(sh, hw, code_addr, key, fail_key, allow_async, t0);
        }
    }

    // A further permutation of a program is translated here, for this draw. Translated on the
    // worker, Uncharted 4's UI vertex shader got permutations that did not fit the draws that
    // asked for them (buffer stride 12 for a stride 32 draw): one new permutation per draw up to
    // the cap of 48, then its draws were skipped (~60 per frame at the first-boot menu). A
    // translation specializes on sharps read through the draw's pointer chains, in memory the
    // game rewrites per draw, so read later they likely belong to another draw (a copy of the
    // existing permutation's SRT reads did not cover them). New programs, the bulk of a new
    // area, still go to the worker.
    if (allow_async && S.async_compile && vec.empty() && !S.async_retry.count(fail_key)) {
        // One worker translation per program: the draws of a new program differ in user data,
        // and one job per draw (cold cache, a busy first frame) made dozens of permutations from
        // memory read after their draws; none fitted, and Uncharted 4's UI vertex shader hit
        // the cap of 48 ("too many permutations", ~400 skipped draws per frame). The other draws
        // wait for the first translation; further permutations are then made on this thread.
        if (!S.gcn_pending_prog.insert(key).second) return &S.pending_shader;
        // Translate GCN -> MSL on a worker thread (1-2 ms per shader, but new areas bring
        // hundreds at once); draws using it are skipped until the result is applied.
        S.gcn_pending.insert(pend_key);
        auto job = std::make_shared<State::GcnJob>();
        job->hw = hw;
        job->sw = sw;
        job->rt = rt;
        job->code_addr = code_addr;
        memcpy(job->ud, ud, 64);
        job->key = key;
        job->fail_key = fail_key;
        job->pend_key = pend_key;
        job->cache_store = cacheable;
        if (cacheable) job->cache_key = ckey;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            gcn_compile_desc jd{};
            jd.hw_stage = job->hw;
            jd.sw_stage = job->sw;
            jd.code_addr = job->code_addr;
            memcpy(jd.user_data, job->ud, 64);
            jd.runtime = &job->rt;
            jd.profile = &S.profile;
            jd.read_guest = ReadGuest;
            jd.read_ctx = S.ctx;
            jd.flags = GCN_COMPILE_EMIT_MSL;
            jd.msl = &S.msl;
            char jerr[1024] = {0};
            gcn_status st = gcn_compile(&jd, &job->sh, jerr, sizeof(jerr));
            if (st != GCN_OK) {
                if (job->sh) gcn_shader_destroy(job->sh);
                job->sh = nullptr;
                job->err = jerr;
                job->status = (int)st;
            } else if (job->cache_store) {
                ps4gnm_shader_cache::Store(job->cache_key, job->sh);
            }
            std::lock_guard<std::mutex> lk(S.async_mu);
            S.async_gcn_done.push_back(job);
            S.async_shaders_ready.fetch_add(1, std::memory_order_release);
        });
        return &S.pending_shader;
    }

    gcn_compile_desc d{};
    d.hw_stage = hw;
    d.sw_stage = sw;
    d.code_addr = code_addr;
    memcpy(d.user_data, ud, 64);
    d.runtime = &rt;
    d.profile = &S.profile;
    d.read_guest = ReadGuest;
    d.read_ctx = S.ctx;
    d.flags = GCN_COMPILE_EMIT_MSL;
    d.msl = &S.msl;
    char err[1024] = {0};
    gcn_shader* sh = nullptr;
    const double t0 = CFAbsoluteTimeGetCurrent();
    const gcn_status st = gcn_compile(&d, &sh, err, sizeof(err));
    g_hitch.gcn_ms += (CFAbsoluteTimeGetCurrent() - t0) * 1000.0;
    if (st != GCN_OK) {
        RLog("compile-fail", "shader 0x%llx (hw %u) failed to compile: status %d: %s",
             (unsigned long long)code_addr, hw, (int)st, err);
        if (sh) gcn_shader_destroy(sh);
        S.shader_fail.insert(fail_key);
        return nullptr;
    }
    if (cacheable) sc::Store(ckey, sh);
    return FinishShader(sh, hw, code_addr, key, fail_key, allow_async, t0);
}


// One of the two RectList vertex shaders of the VS at code_addr (capture, or expansion if
// `expand`), its Metal library compiling in the background (ps4gnm_rect::CompileLibrary). The
// completion runs after RectListShaders has returned: it only reaches the heap objects r and the
// returned Shader, never this function's or its caller's stack.
static Shader* StartRectListShader(Shader::RectList* r, uint64_t code_addr, const char* src, bool expand) {
    const std::string msl = ps4gnm_rect::PrepareMsl(src, expand);
    Shader* s = new Shader();
    s->hw = GCN_HW_VS;
    s->code_addr = code_addr;
    s->uid = S.next_uid++;
    s->msl_hash = Fnv(msl.data(), msl.size());
    s->pending = true;
    DumpMsl(s, msl.c_str());
    if (S.async_compile)
        ps4gnm_rect::CompileLibrary(S.dev, msl, &s->fn, &s->pending, r, code_addr, expand);
    else
        ps4gnm_rect::CompileLibraryNow(S.dev, msl, &s->fn, &s->pending, r, code_addr, expand);
    return s;
}

// The RectList vertex shaders of `vs` (gcn_shader_rectlist_msl), made once per shader on the CP
// thread. Their Metal libraries compile in the background like any new shader's (a synchronous
// compile stalls the CP thread for tens of ms); draws are skipped until both are ready. The call
// that starts the compiles checks at once whether both are done (it used to skip its draw
// unconditionally), so when Metal finishes both at once (its own cache) that first draw
// is drawn. With PS4_GNM_SYNC_COMPILE=1 they compile here and the first draw is never skipped.
// Returns nullptr (logged once per shader) if the shader cannot be expanded, and sets *pending
// while the libraries compile.
Shader::RectList* RectListShaders(Shader* vs, bool* pending) {
    *pending = false;
    if (vs->rect_failed) return nullptr;
    if (!vs->rect) {
        gcn_rectlist_msl m{};
        char err[512] = {0};
        if (gcn_shader_rectlist_msl(vs->sh, &S.msl, &m, err, sizeof(err)) != GCN_OK) {
            RLog("rect-fail", "VS 0x%llx: no RectList variant: %s", (unsigned long long)vs->code_addr, err);
            vs->rect_failed = true;
            return nullptr;
        }
        auto* r = new Shader::RectList();
        r->vertex_stride = m.vertex_stride;
        r->capture_slot = m.capture_buffer;
        r->params_slot = m.params_buffer;
        r->capture = StartRectListShader(r, vs->code_addr, m.capture_msl, false);
        r->expand = StartRectListShader(r, vs->code_addr, m.expand_msl, true);
        gcn_rectlist_msl_free(&m);
        vs->rect = r;
    }
    Shader::RectList* r = vs->rect;
    if (r->compiling.load(std::memory_order_acquire) > 0) {
        *pending = true;
        return nullptr;
    }
    if (r->failed.load(std::memory_order_relaxed)) {
        RLog("rect-fail", "VS 0x%llx: Metal rejected its RectList shaders", (unsigned long long)vs->code_addr);
        vs->rect_failed = true;
        return nullptr;
    }
    return r;
}

// =====================================================================================
// Resource binding (shadPS4 Rasterizer::BindBuffers / BindTextures)
// =====================================================================================
using Stage = GnmStage; // Vertex, Fragment, Compute (indexes the per-stage arrays)

// The state last set on the open render / compute encoder (GnmBackend::EncoderSerial), so that draws and
// dispatches skip identical calls: each is an Objective-C message into the driver, and resource
// bindings also add the resource to the command buffer's list. Reset when the encoder is
// created and by code that sets state on it directly. The objects compared by address are
// alive: the command buffer retains everything bound to its encoders.
struct EncoderState {
    struct Slot {
        const void* p = nullptr;
        NSUInteger off = 0;
        uint32_t gen = 0;
    };
    uint64_t enc = 0; // GnmBackend::EncoderSerial of the encoder described (0: none)
    uint32_t gen = 1; // slots of other generations are unknown
    Slot buf[3][31], tex[3][128], samp[3][16];
    Slot pso, dss;
    using Fixed = GnmFixedState; // fixed-function state of a render encoder
    Fixed fixed;
    uint32_t fixed_gen = 0;
    bool fixed_stencil = false; // fixed.stencil_ref is set
    struct Used {
        const void* p;
        uint32_t how;
    } used[32];
    uint32_t n_used = 0;

    void Reset(uint64_t serial) {
        enc = serial;
        ++gen;
        n_used = 0;
    }
    bool Tracks(uint64_t serial) const { return serial && serial == enc; }
    // true if the slot already holds p; records p otherwise
    bool Same(Slot& sl, const void* p) {
        if (sl.gen == gen && sl.p == p) return true;
        sl.p = p;
        sl.gen = gen;
        return false;
    }
};
EncoderState g_rstate, g_cstate;

struct Binder {
    Stage stage;
    // a Metal 3 compute encoder of the guest memory re-run queue (queue2), which g_cstate does not
    // describe (its object may even reuse the address of a tracked one); nil: the backend's open encoder
    id<MTLComputeCommandEncoder> raw = nil;
    // GnmBackend::EncoderSerial of the encoder this binder was made for: g_rstate / g_cstate describe
    // it only while they were reset with it (an encoder opened later resets them anew). Kept here
    // so that a bind costs no virtual call.
    uint64_t serial = 0;
    GmTextures* textures_out = nullptr;      // records every texture bound
    const GmTextures* textures_in = nullptr; // BindStage binds these instead of looking images up
    EncoderState* Tracked() const {
        if (raw) return nullptr;
        EncoderState& es = stage == Stage::Compute ? g_cstate : g_rstate;
        return es.Tracks(serial) ? &es : nullptr;
    }
    void Buffer(id<MTLBuffer> b, NSUInteger off, uint32_t slot) {
        if (slot > 30) return;
        if (raw) {
            [raw setBuffer:b offset:off atIndex:slot];
            return;
        }
        if (EncoderState* es = Tracked()) {
            EncoderState::Slot& sl = es->buf[(int)stage][slot];
            if (sl.gen == es->gen && sl.p == (__bridge const void*)b) {
                if (sl.off == off || !b) return;
                sl.off = off;
                S.be->BindBufferOffset(stage, slot, off);
                return;
            }
            sl.p = (__bridge const void*)b;
            sl.off = off;
            sl.gen = es->gen;
        }
        S.be->BindBuffer(stage, slot, b, off);
    }
    // Residency (+ hazard tracking on Metal 3) of a buffer reached through an argument buffer.
    void Use(id<MTLBuffer> b, bool written) {
        if (raw) { // queue2 encoders are compute encoders
            [raw useResource:b usage:written ? (MTLResourceUsageRead | MTLResourceUsageWrite) : MTLResourceUsageRead];
            return;
        }
        if (EncoderState* es = Tracked()) {
            // MTLResourceUsageRead (1) or Read | Write (3), and the stage
            const uint32_t how = (written ? 3u : 1u) | ((uint32_t)stage << 8);
            for (uint32_t i = 0; i < es->n_used; ++i)
                if (es->used[i].p == (__bridge const void*)b && es->used[i].how == how) return;
            if (es->n_used < 32) es->used[es->n_used++] = {(__bridge const void*)b, how};
        }
        S.be->UseResource(stage, b, written);
    }
    void Bytes(const void* p, size_t n, uint32_t slot) {
        if (slot > 30) return;
        if (EncoderState* es = Tracked()) es->buf[(int)stage][slot].gen = 0; // replaced by the bytes
        if (n > 4096) {
            id<MTLBuffer> b;
            size_t off;
            void* ptr;
            if (!S.arena.Alloc(S.dev, n, 256, &b, &off, &ptr)) return;
            memcpy(ptr, p, n);
            Buffer(b, off, slot);
            return;
        }
        if (raw) [raw setBytes:p length:n atIndex:slot];
        else S.be->BindBytes(stage, slot, p, n);
    }
    void Texture(id<MTLTexture> t, uint32_t slot) {
        if (slot > 127) return;
        if (textures_out) textures_out->push_back({slot, t});
        if (raw) {
            [raw setTexture:t atIndex:slot];
            return;
        }
        if (EncoderState* es = Tracked(); es && es->Same(es->tex[(int)stage][slot], (__bridge const void*)t)) return;
        S.be->BindTexture(stage, slot, t);
    }
    void Sampler(id<MTLSamplerState> s, uint32_t slot) {
        if (slot > 15) return;
        if (raw) {
            [raw setSamplerState:s atIndex:slot];
            return;
        }
        if (EncoderState* es = Tracked(); es && es->Same(es->samp[(int)stage][slot], (__bridge const void*)s)) return;
        S.be->BindSampler(stage, slot, s);
    }
};

MTLTextureType ShaderTextureType(const gcn_image_binding& b) {
    switch (b.view_type) {
    // Color1D / Color1DArray: the translator declares 1D images as 2D ones of height 1
    // (DecodeImageUncached makes 1D T#s such textures)
    case 8: return MTLTextureType2D;
    case 12: return MTLTextureType2DArray;
    case 10: return MTLTextureType3D;
    // Cube: the translator declares every cube image as a 2D array of its faces (a GCN cube
    // sample names its face; spirv_emit_context ImageType), so the shader gets a 2D array view
    case 11: return MTLTextureType2DArray;
    case 13: return MTLTextureType2DArray;
    case 14: return MTLTextureType2DMultisample;
    case 15: return MTLTextureType2DMultisampleArray;
    case 9:
    default: return b.is_array ? MTLTextureType2DArray : MTLTextureType2D;
    }
}

id<MTLTexture> AdaptTextureUncached(id<MTLTexture> t, MTLTextureType want, const ImageDesc& img,
                                    uint32_t level, uint32_t nlevels) {
    if (!t) return nil;
    const bool type_ok = t.textureType == want;
    const bool level_ok = (level == 0 && (nlevels == 0 || nlevels >= t.mipmapLevelCount));
    // T# BASE_ARRAY / LAST_ARRAY select the slices of a 2D array the shader sees (e.g. one
    // layer of a shadow map array sampled as a plain 2D texture), and the faces of a cube (array)
    // the shader sees as a 2D array of faces (ShaderTextureType: cubes are declared that way). A
    // cube T#'s BASE_ARRAY / LAST_ARRAY count faces, as shadPS4's views take them: TLOU's and
    // Uncharted 4's single cubes carry 0..5, inFAMOUS Second Son's 256-cube array 0..1535.
    const bool cube_src = t.textureType == MTLTextureTypeCube || t.textureType == MTLTextureTypeCubeArray;
    const NSUInteger all_slices = cube_src ? (t.textureType == MTLTextureTypeCube ? 6 : t.arrayLength * 6) : t.arrayLength;
    const bool arr = (t.textureType == MTLTextureType2DArray &&
                      (want == MTLTextureType2D || want == MTLTextureType2DArray)) ||
                     (cube_src && want == MTLTextureType2DArray);
    const NSUInteger first = arr ? std::min<NSUInteger>(img.base_array, all_slices - 1) : 0;
    const NSUInteger last = arr ? std::min<NSUInteger>(std::max(img.last_array, img.base_array), all_slices - 1) : 0;
    const bool slices_ok = !arr || (first == 0 && last == all_slices - 1);
    if (type_ok && level_ok && slices_ok) return t;
    // compatible view types (Metal texture view rules)
    auto compatible = [&](MTLTextureType from, MTLTextureType to) {
        if (from == to) return true;
        const bool from2d = from == MTLTextureType2D || from == MTLTextureType2DArray ||
                            from == MTLTextureTypeCube || from == MTLTextureTypeCubeArray;
        const bool to2d = to == MTLTextureType2D || to == MTLTextureType2DArray ||
                          to == MTLTextureTypeCube || to == MTLTextureTypeCubeArray;
        if (from2d && to2d) {
            const NSUInteger nslices = (from == MTLTextureTypeCube || from == MTLTextureTypeCubeArray)
                                           ? t.arrayLength * 6 : t.arrayLength;
            if ((to == MTLTextureTypeCube || to == MTLTextureTypeCubeArray) && nslices < 6) return false;
            return true;
        }
        const bool from1d = from == MTLTextureType1D || from == MTLTextureType1DArray;
        const bool to1d = to == MTLTextureType1D || to == MTLTextureType1DArray;
        return from1d && to1d;
    };
    if (!compatible(t.textureType, want)) {
        RLog("tex-type", "texture type mismatch: have %lu want %lu (T# type %u, %ux%u)",
             (unsigned long)t.textureType, (unsigned long)want, (uint32_t)img.type, img.width, img.height);
        return nil;
    }
    NSUInteger slices = t.textureType == MTLTextureTypeCube ? 6
                        : t.textureType == MTLTextureTypeCubeArray ? t.arrayLength * 6
                                                                    : t.arrayLength;
    if (want == MTLTextureType2D || want == MTLTextureType1D) slices = 1;
    if (want == MTLTextureTypeCube) slices = 6;
    if (want == MTLTextureTypeCubeArray) slices = (slices / 6) * 6;
    if (slices == 0) return nil;
    if (arr) slices = want == MTLTextureType2D ? 1 : last - first + 1;
    const NSUInteger lv = std::min<NSUInteger>(level, t.mipmapLevelCount - 1);
    const NSUInteger nl = nlevels ? std::min<NSUInteger>(nlevels, t.mipmapLevelCount - lv) : t.mipmapLevelCount - lv;
    id<MTLTexture> v = [t newTextureViewWithPixelFormat:t.pixelFormat
                                            textureType:want
                                                 levels:NSMakeRange(lv, nl)
                                                 slices:NSMakeRange(first, slices)];
    return v;
}

// AdaptTexture results by (texture, view type, levels, T# slice range): creating a view on every
// draw shows up in profiles. Cached views keep their textures alive, so the cache is emptied
// every 60 presents (and when it grows large) to let evicted textures go.
struct ViewKey {
    void* tex;
    uint32_t want, level, nlevels, base_array, last_array;
    bool operator==(const ViewKey& o) const {
        return tex == o.tex && want == o.want && level == o.level && nlevels == o.nlevels &&
               base_array == o.base_array && last_array == o.last_array;
    }
};
struct ViewKeyHash {
    size_t operator()(const ViewKey& k) const { return (size_t)Fnv(&k, sizeof(k)); }
};
std::unordered_map<ViewKey, id<MTLTexture>, ViewKeyHash> g_view_cache;
uint64_t g_view_cache_gen = 0, g_present_gen = 0;

id<MTLTexture> AdaptTexture(id<MTLTexture> t, MTLTextureType want, const ImageDesc& img, uint32_t level,
                            uint32_t nlevels) {
    if (!t) return nil;
    if (g_present_gen - g_view_cache_gen >= 60 || g_view_cache.size() > 4096) {
        g_view_cache.clear();
        g_view_cache_gen = g_present_gen;
    }
    ViewKey k;
    memset(&k, 0, sizeof(k)); // padding is hashed
    k.tex = (__bridge void*)t;
    k.want = (uint32_t)want;
    k.level = level;
    k.nlevels = nlevels;
    k.base_array = img.base_array;
    k.last_array = img.last_array;
    auto it = g_view_cache.find(k);
    if (it != g_view_cache.end()) return it->second;
    id<MTLTexture> v = AdaptTextureUncached(t, want, img, level, nlevels);
    if (v) g_view_cache.emplace(k, v);
    return v;
}

id<MTLSamplerState> GetSampler(const uint32_t ss[4], bool compare) {
    uint64_t h = Fnv(ss, 16, compare ? 7 : 3);
    auto it = S.samplers.find(h);
    if (it != S.samplers.end()) return it->second;
    SamplerDesc sd;
    DecodeSampler(ss, &sd);
    MTLSamplerDescriptor* d = MakeSamplerDescriptor(sd, compare);
    id<MTLSamplerState> st = [S.dev newSamplerStateWithDescriptor:d];
    S.samplers[h] = st;
    return st;
}

// Image descriptor i of a stage (see StageImages below).
bool StageImage(const Shader* s, const std::vector<uint32_t>& flat, uint32_t i, ImageDesc* img,
                uint64_t* approx = nullptr);

struct BindCtx {
    const RegSnap* regs;
    uint32_t num_workgroups = 1; // compute: for the LDS-spill buffer
};

// Returns false if the draw/dispatch must be skipped.
// Flattened user data of a shader stage for the current draw/dispatch (user SGPRs plus the
// values its SRT program fetches from guest memory). Built once per stage and shared by the
// hazard check, depth companions and binding.
void BuildFlat(const Shader* s, const uint32_t ud[16], std::vector<uint32_t>* flat);

// BuildFlat for the shader chosen by GetShader, unless GetShader already built it.
void StageFlat(int fi, const Shader* s, const uint32_t ud[16]) {
    const Shader* from = S.flat_from[fi];
    S.flat_from[fi] = nullptr;
    // Reused only if built with the same SRT program: another permutation's program may place
    // the flattened dwords elsewhere even with the same size.
    if (from && gcn_shader_same_srt(from->sh, s->sh) &&
        S.flat_scratch[fi].size() == std::max<uint32_t>(s->info.flatbuf_size_dw, 16) && !ShouldTrace())
        return;
    BuildFlat(s, ud, &S.flat_scratch[fi]);
}

void BuildFlat(const Shader* s, const uint32_t ud[16], std::vector<uint32_t>* flat) {
    static const bool dumpflat = getenv("PS4_GNM_TRACE_BUFS") != nullptr;
    g_trace_reads = dumpflat && ShouldTrace();
    GmCpGuarded([&](gcn_read_guest_fn read) {
        flat->assign(std::max<uint32_t>(s->info.flatbuf_size_dw, 16), 0);
        gcn_shader_build_flatbuf(s->sh, ud, read, S.ctx, flat->data(), flat->size());
        return true;
    });
    g_trace_reads = false;
}

uint64_t ApproxImageBytes(const ImageDesc& img);

// =====================================================================================
// Frame capture (ps4_gnm_capture.h). PS4_GNM_CAPTURE_FRAME=<present index> records every draw
// and dispatch of that frame. Needs PS4_GNM_SYNC_ENCODE=1 (the CP side and the encoder side of
// a pass then run on one thread, back to back). Every captured pass commits and waits before and
// after its encoding, so its inputs and outputs can be read; nothing here runs when the
// variable is unset (cap::g_writer stays null, cap::g_pass stays null).
// PS4_GNM_CAPTURE_DIR=<dir>      default ./gnm_captures; the frame goes to <dir>/frame_<n>
// PS4_GNM_CAPTURE_REPLAY=<a>-<b>[,<c>-<d>...] | all   passes stored replayable (guest inputs,
//                                raw texture contents before and after); default none
// PS4_GNM_CAPTURE_REPLAY_SHADER=<addr>|pgm:<hash>[,...]  also stored replayable: the passes with
//                                a stage at one of these code addresses or with one of these
//                                program hashes (StageRec::pgm_hash), and the passes of the frame
//                                that may have written what they read (ShaderSelection); the frame before
//                                the captured one is then a learning frame (recorded, not written)
// PS4_GNM_CAPTURE_RAW_MB=<n>     budget of the raw texture files (default 8192); textures that do
//                                not fit have no raw file ("budget") but keep their view PNG;
//                                guest memory blobs are not counted
// PS4_GNM_CAPTURE_THUMB=<px>     width of the view PNGs of passes that are not replayable
//                                (default 480; replayable passes get full size)
// =====================================================================================
namespace cap {
using namespace ps4gnm_capture;

// Every MtlFormat value of ps4_gnm_capture.h is the MTLPixelFormat of the same name.
constexpr bool SameFormat(uint32_t a, MTLPixelFormat b) { return a == (uint32_t)b; }
static_assert(SameFormat(kR8Unorm, MTLPixelFormatR8Unorm) &&
              SameFormat(kR8Snorm, MTLPixelFormatR8Snorm) &&
              SameFormat(kR8Uint, MTLPixelFormatR8Uint) &&
              SameFormat(kR8Sint, MTLPixelFormatR8Sint) &&
              SameFormat(kR16Unorm, MTLPixelFormatR16Unorm) &&
              SameFormat(kR16Snorm, MTLPixelFormatR16Snorm) &&
              SameFormat(kR16Uint, MTLPixelFormatR16Uint) &&
              SameFormat(kR16Sint, MTLPixelFormatR16Sint) &&
              SameFormat(kR16Float, MTLPixelFormatR16Float) &&
              SameFormat(kRG8Unorm, MTLPixelFormatRG8Unorm) &&
              SameFormat(kRG8Snorm, MTLPixelFormatRG8Snorm) &&
              SameFormat(kRG8Uint, MTLPixelFormatRG8Uint) &&
              SameFormat(kR32Uint, MTLPixelFormatR32Uint) &&
              SameFormat(kR32Sint, MTLPixelFormatR32Sint) &&
              SameFormat(kR32Float, MTLPixelFormatR32Float) &&
              SameFormat(kRG16Unorm, MTLPixelFormatRG16Unorm) &&
              SameFormat(kRG16Snorm, MTLPixelFormatRG16Snorm) &&
              SameFormat(kRG16Uint, MTLPixelFormatRG16Uint) &&
              SameFormat(kRG16Float, MTLPixelFormatRG16Float) &&
              SameFormat(kRGBA8Unorm, MTLPixelFormatRGBA8Unorm) &&
              SameFormat(kRGBA8Unorm_sRGB, MTLPixelFormatRGBA8Unorm_sRGB) &&
              SameFormat(kRGBA8Snorm, MTLPixelFormatRGBA8Snorm) &&
              SameFormat(kRGBA8Uint, MTLPixelFormatRGBA8Uint) &&
              SameFormat(kBGRA8Unorm, MTLPixelFormatBGRA8Unorm) &&
              SameFormat(kBGRA8Unorm_sRGB, MTLPixelFormatBGRA8Unorm_sRGB) &&
              SameFormat(kRGB10A2Unorm, MTLPixelFormatRGB10A2Unorm) &&
              SameFormat(kRG11B10Float, MTLPixelFormatRG11B10Float) &&
              SameFormat(kRGB9E5Float, MTLPixelFormatRGB9E5Float) &&
              SameFormat(kRG32Uint, MTLPixelFormatRG32Uint) &&
              SameFormat(kRG32Float, MTLPixelFormatRG32Float) &&
              SameFormat(kRGBA16Unorm, MTLPixelFormatRGBA16Unorm) &&
              SameFormat(kRGBA16Snorm, MTLPixelFormatRGBA16Snorm) &&
              SameFormat(kRGBA16Uint, MTLPixelFormatRGBA16Uint) &&
              SameFormat(kRGBA16Float, MTLPixelFormatRGBA16Float) &&
              SameFormat(kRGBA32Uint, MTLPixelFormatRGBA32Uint) &&
              SameFormat(kRGBA32Float, MTLPixelFormatRGBA32Float) &&
              SameFormat(kDepth16Unorm, MTLPixelFormatDepth16Unorm) &&
              SameFormat(kDepth32Float, MTLPixelFormatDepth32Float) &&
              SameFormat(kStencil8, MTLPixelFormatStencil8) &&
              SameFormat(kDepth32Float_Stencil8, MTLPixelFormatDepth32Float_Stencil8) &&
              SameFormat(kX32_Stencil8, MTLPixelFormatX32_Stencil8),
              "ps4_gnm_capture.h MtlFormat values differ from Metal's");

std::unique_ptr<Writer> g_writer;
std::vector<std::pair<uint32_t, uint32_t>> g_replay; // pass index ranges stored replayable
bool g_replay_all = false;
RawBudget g_budget; // raw texture files (PS4_GNM_CAPTURE_RAW_MB)
uint32_t g_thumb = 480;
PassRec* g_pass = nullptr;                          // the pass being recorded
bool g_encoded = false;                             // g_pass reached its draw / dispatch call
std::vector<std::pair<uint64_t, uint64_t>> g_reads; // guest ranges g_pass reads
std::vector<id<MTLTexture>> g_tex;                  // texture of g_pass->textures[i]
// storage textures g_pass binds (BindStage): index of their ImageRec in g_pass->images, texture
std::vector<std::pair<size_t, id<MTLTexture>>> g_storage;
uint32_t g_next = 0;
// PS4_GNM_CAPTURE_REPLAY_SHADER: g_learning while g_pass belongs to the learning frame (nothing
// is written, committed or waited for); g_keyed once g_pass has its SelectKey
ShaderSelection g_select;
bool g_learning = false, g_keyed = false;
SelectKey g_learn_key;
uint32_t g_unknown = 0; // selected passes of the captured frame whose key the learning frame had not
std::vector<ShaderSelection::Range> g_learn_reads, g_learn_writes;
// a guest memory dispatch (GmRunSync): its inputs were stored after its probe, before
// it ran; the guest ranges it wrote are outputs
bool g_inputs_taken = false;
std::vector<std::pair<uint64_t, uint64_t>> g_guest_written;

// The frame before the captured one, when passes are selected by shader address.
bool LearnFrame() { return g_writer && !g_select.Empty() && g_writer->Frame() > 0 && S.present_idx + 1 == g_writer->Frame(); }
bool InFrame() { return g_writer && (S.present_idx == g_writer->Frame() || LearnFrame()); }

const char* StageName(Stage st) { return st == Stage::Vertex ? "vs" : st == Stage::Fragment ? "ps" : "cs"; }

// Guest reads of the recompiler's SRT walks and fetches, recorded (not the direct path).
bool RecordRead(void*, uint64_t addr, void* dst, uint64_t size) {
    if (!ReadGuest(S.ctx, addr, dst, size)) return false;
    g_reads.push_back({addr, size});
    return true;
}

void Setup(uint64_t frame, uint64_t mem_size) {
    if (!getenv("PS4_GNM_SYNC_ENCODE")) {
        fprintf(stderr, "[gnm-capture] PS4_GNM_CAPTURE_FRAME needs PS4_GNM_SYNC_ENCODE=1; capture disabled\n");
        return;
    }
    const char* dir = getenv("PS4_GNM_CAPTURE_DIR");
    const std::string base = dir && *dir ? dir : "gnm_captures";
    if (const char* r = getenv("PS4_GNM_CAPTURE_REPLAY")) {
        if (!strcmp(r, "all")) g_replay_all = true;
        for (const char* c = r; *c && !g_replay_all;) {
            char* e = nullptr;
            const uint32_t a = (uint32_t)strtoul(c, &e, 10);
            const uint32_t b = (e && *e == '-') ? (uint32_t)strtoul(e + 1, &e, 10) : a;
            g_replay.push_back({a, b});
            c = (e && *e == ',') ? e + 1 : "";
        }
    }
    if (const char* sh = getenv("PS4_GNM_CAPTURE_REPLAY_SHADER"); sh && *sh) {
        std::string err;
        if (!g_select.Parse(sh, &err))
            fprintf(stderr, "[gnm-capture] PS4_GNM_CAPTURE_REPLAY_SHADER: %s; no passes selected by shader\n", err.c_str());
        else if (frame == 0)
            fprintf(stderr, "[gnm-capture] PS4_GNM_CAPTURE_REPLAY_SHADER: frame 0 has no learning frame; only the passes "
                            "using the shaders are selected, not their producers\n");
    }
    if (const char* mb = getenv("PS4_GNM_CAPTURE_RAW_MB")) g_budget.limit = strtoull(mb, nullptr, 10) << 20;
    if (const char* t = getenv("PS4_GNM_CAPTURE_THUMB")) g_thumb = (uint32_t)strtoul(t, nullptr, 10);
    mkdir(base.c_str(), 0755);
    auto w = std::make_unique<Writer>(base + "/frame_" + std::to_string(frame), frame, mem_size,
                                      sceKernelIsNeoMode() != 0, gcn_shader_cache_id());
    std::string err;
    if (!w->Begin(&err)) {
        fprintf(stderr, "[gnm-capture] %s; capture disabled\n", err.c_str());
        return;
    }
    fprintf(stderr, "[gnm-capture] capturing frame %llu into %s\n", (unsigned long long)frame, w->Dir().c_str());
    g_writer = std::move(w);
}

bool Replayable(uint32_t idx) {
    if (g_replay_all) return true;
    for (auto& r : g_replay)
        if (idx >= r.first && idx <= r.second) return true;
    return false;
}

// CP thread, start of Draw / Dispatch (a pass of the captured frame).
void Begin(const char* type, const char* queue, const char* name, const uint32_t* regs) {
    g_pass = new PassRec();
    g_learning = S.present_idx != g_writer->Frame();
    g_keyed = false;
    g_pass->type = type;
    g_pass->queue = queue;
    g_pass->name = name;
    if (!g_learning) {
        g_pass->index = g_next++;
        g_pass->replayable = Replayable(g_pass->index);
        g_pass->regs_file = g_writer->SaveRegs(regs, &g_pass->regs_hash);
    }
    g_encoded = false;
    g_reads.clear();
    g_tex.clear();
    g_storage.clear();
    g_inputs_taken = false;
    g_guest_written.clear();
}

void SetSkipped(const char* why) {
    if (g_pass && g_pass->skipped.empty()) g_pass->skipped = why;
}

// CP thread: a stage's shader, the guest memory its translation and resource tables read.
void NoteStage(const char* stage, const Shader* s, const uint32_t ud[16]) {
    StageRec r;
    r.stage = stage;
    r.code_addr = s->code_addr;
    r.code_size = s->info.code_size;
    r.pgm_hash = s->info.pgm_hash;
    r.msl_hash = s->msl_hash;
    memcpy(r.user_data, ud, sizeof(r.user_data));
    g_pass->stages.push_back(r);
    g_reads.push_back({s->code_addr, std::max<uint32_t>(s->info.code_size, 4)});
    // the translation finds the program's length and hash in its OrbShdr footer, which is not part
    // of code_size and, for a program without the header that points at it, is searched for:
    // record what that search reads (CodeSignature and gcn_compile run the same one)
    uint32_t footer_size = 0;
    uint64_t footer_hash = 0;
    gcn_find_binary_info(RecordRead, nullptr, s->code_addr, &footer_size, &footer_hash);
    if ((s->info.has_fetch_shader || s->info.fetch_shader_inlined) && s->info.fetch_shader_sgpr_base < 15) {
        const uint64_t fs = ((uint64_t)ud[s->info.fetch_shader_sgpr_base + 1] << 32 | ud[s->info.fetch_shader_sgpr_base]) &
                            ((1ull << 40) - 1);
        g_reads.push_back({fs, 4096}); // fetch shaders are a few dozen dwords; their length is not recorded
    }
    std::vector<uint32_t> flat(std::max<uint32_t>(s->info.flatbuf_size_dw, 16));
    gcn_shader_build_flatbuf(s->sh, ud, RecordRead, nullptr, flat.data(), flat.size());
    for (uint32_t i = 0; i < s->attrs.size(); ++i) {
        uint32_t v[4] = {};
        gcn_shader_fetch_vertex_attrib_sharp(s->sh, i, ud, RecordRead, nullptr, v);
        BufferDesc bd;
        DecodeBuffer(v, &bd);
        if (bd.address && bd.size) g_reads.push_back({bd.address, bd.size});
    }
}

// CP thread: the draw's arguments and the index / argument memory it reads.
void NoteDraw(const DrawArgs& a) {
    PassRec& p = *g_pass;
    p.indexed = a.indexed;
    p.index_base = a.index_base;
    p.index_type = a.index_type;
    p.index_offset = a.index_offset;
    p.count = a.count;
    p.max_index = a.max_index;
    p.num_instances = a.num_instances;
    p.base_vertex_ud = a.base_vertex_ud;
    p.start_instance_ud = a.start_instance_ud;
    const uint64_t isz = a.index_type == 1 ? 4 : 2;
    if (a.indexed && a.index_base)
        g_reads.push_back(a.indirect_addr ? std::make_pair(a.index_base, (uint64_t)a.max_index * isz)
                                          : std::make_pair(a.index_base + a.index_offset * isz, (uint64_t)a.count * isz));
    if (a.indirect_addr) g_reads.push_back({a.indirect_addr, 20});
}

void NoteDispatch(uint32_t x, uint32_t y, uint32_t z, uint64_t indirect_addr) {
    g_pass->dim[0] = x;
    g_pass->dim[1] = y;
    g_pass->dim[2] = z;
    if (indirect_addr) g_reads.push_back({indirect_addr, 12});
}

// Raw contents of a texture of the pass, within the budget.
bool ReadForPass(id<MTLTexture> t, TextureRec* tr, std::vector<uint8_t>* raw) {
    if (t.sampleCount > 1) return tr->readback = "msaa", false;
    if (!BytesPerTexel((uint32_t)t.pixelFormat)) return tr->readback = "format", false;
    if (!S.tex->ReadRaw(t, raw)) return tr->readback = "readback failed", false;
    return true;
}

// The GPU-owned entry `gid` (texture t) used by the pass; its contents before the pass when the
// pass is replayable. Returns its index in g_pass->textures.
int AddTexture(const GpuTextureId& gid, id<MTLTexture> t, const char* role, bool written) {
    for (size_t i = 0; i < g_tex.size(); ++i)
        if (g_tex[i] == t) {
            g_pass->textures[i].written |= written;
            return (int)i;
        }
    TextureRec tr;
    tr.role = "t" + std::to_string(g_tex.size()) + "_" + role;
    tr.written = written;
    tr.id = gid;
    if (g_pass->replayable) {
        std::vector<uint8_t> raw;
        if (!g_budget.Fits(RawSize(gid))) tr.readback = kReadbackBudget; // not read at all
        else if (ReadForPass(t, &tr, &raw)) g_writer->SavePre(g_pass->index, &tr, raw, &g_budget);
    }
    g_pass->textures.push_back(std::move(tr));
    g_tex.push_back(t);
    return (int)g_tex.size() - 1;
}

// The pass's SelectKey, once its stages are noted (StartEncode, or Finish for a pass that never
// reached the encoder); in the captured frame, a pass ShaderSelection selects becomes replayable
// (before any of its textures is recorded).
void KeyPass(PassRec* p) {
    if (g_select.Empty() || g_keyed) return;
    g_keyed = true;
    uint64_t code[2] = {}, pgm[2] = {};
    for (size_t i = 0; i < p->stages.size() && i < 2; ++i) {
        code[i] = p->stages[i].code_addr;
        pgm[i] = p->stages[i].pgm_hash;
    }
    const SelectKey k = g_select.Next(code[0], code[1], pgm[0], pgm[1]);
    if (g_learning) {
        g_learn_key = k;
    } else if (g_select.Selects(k)) {
        p->replayable = true;
        if (!g_select.Known(k)) g_unknown++;
    }
}

// Encoder thread, top of DrawEncode / DispatchEncode: everything before the pass has completed.
void StartEncode() {
    KeyPass(g_pass);
    if (g_learning) return;
    Commit();
    WaitAll();
}

// DrawEncode, after the targets are chosen and before anything of the pass is encoded.
// An EQAA twin (a multisampled stand-in the renderer draws into): not a cache entry, not replayable.
void AddTwin(id<MTLTexture> t, const std::string& role, bool written) {
    TextureRec tr;
    tr.role = "t" + std::to_string(g_tex.size()) + "_" + role;
    tr.written = written;
    tr.readback = "not a texture-cache entry (EQAA twin)";
    g_pass->textures.push_back(tr);
    g_tex.push_back(t);
}

void NoteTargets(const PassKey& key, const RenderState& rs, bool clear_depth, bool clear_stencil) {
    if (!g_learning) {
        Commit(); // e.g. the slice copy of a grown depth array (encoded above) is part of the "before"
        WaitAll();
    }
    for (int i = 0; i < 8; ++i) {
        if (!key.color[i]) continue;
        GpuTextureId gid;
        if (S.tex->DescribeGpuTexture(key.color[i], &gid))
            AddTexture(gid, key.color[i], ("color" + std::to_string(i)).c_str(), true);
        else
            AddTwin(key.color[i], "color" + std::to_string(i), true);
    }
    if (key.depth) {
        const bool written = DepthTargetWritten(rs.ds.depth_write, rs.ds.stencil_test, clear_depth, clear_stencil);
        GpuTextureId gid;
        if (S.tex->DescribeGpuTexture(key.depth, &gid))
            AddTexture(gid, key.depth, "depth", written);
        else
            AddTwin(key.depth, "depth", written);
    }
    for (int i = 0; i < 8; ++i) g_pass->cb_extent[i] = rs.color[i].extent_width | rs.color[i].extent_height << 16;
    g_pass->db_extent = rs.depth.extent_width | rs.depth.extent_height << 16;
    StateRec& s = g_pass->state;
    s.prim = (uint32_t)rs.raster.prim;
    s.color_mode = rs.color_mode;
    for (int i = 0; i < 8; ++i) {
        s.blend_enable[i] = rs.blend[i].enable;
        s.blend_raw[i] = rs.blend[i].raw;
        s.write_mask[i] = rs.color[i].mtl_write_mask;
    }
    s.depth_test = rs.ds.depth_test;
    s.depth_write = rs.ds.depth_write;
    s.depth_compare = rs.ds.depth_compare;
    s.stencil_test = rs.ds.stencil_test;
    s.stencil_ref_front = rs.ds.front.reference;
    s.stencil_ref_back = rs.ds.back.reference;
    const Viewport& vp = rs.viewports[0];
    const double v[6] = {vp.origin_x, vp.origin_y, vp.width, vp.height, vp.znear, vp.zfar};
    memcpy(s.viewport, v, sizeof(v));
    s.scissor[0] = rs.scissor.x;
    s.scissor[1] = rs.scissor.y;
    s.scissor[2] = rs.scissor.width;
    s.scissor[3] = rs.scissor.height;
}

// BindStage: a guest buffer as bound (base aligned down, bound_bytes covering the V#).
void NoteBuffer(Stage st, uint32_t index, const uint32_t v[4], uint64_t base, uint32_t bound_bytes, uint32_t stride,
                bool written) {
    BufferRec b;
    b.stage = StageName(st);
    b.index = index;
    b.addr = base;
    b.size = bound_bytes;
    b.stride = stride;
    b.written = written;
    memcpy(b.vsharp, v, sizeof(b.vsharp));
    g_pass->buffers.push_back(b);
    g_reads.push_back({base, bound_bytes});
}

// BindStage: an image before the renderer resolves it to a texture.
void NoteImage(Stage st, uint32_t index, const ImageDesc& img, bool storage, bool depth_image, int32_t table,
               int32_t entry) {
    ImageRec r;
    r.stage = StageName(st);
    r.index = index;
    r.table = table;
    r.entry = entry;
    r.addr = img.address;
    memcpy(r.tsharp, img.raw, sizeof(r.tsharp));
    r.dfmt = img.raw_dfmt;
    r.nfmt = img.raw_nfmt;
    r.width = img.width;
    r.height = img.height;
    r.depth = img.depth;
    r.levels = img.num_levels;
    r.layers = img.num_layers;
    r.type = (uint32_t)img.type;
    r.tile_mode = (uint32_t)img.tile_mode;
    r.storage = storage;
    r.depth_image = depth_image;
    std::vector<GpuTextureId> ids;
    S.tex->DescribeGpuTextures(img.address, &ids);
    for (const GpuTextureId& gid : ids) {
        id<MTLTexture> t = S.tex->FindGpuTexture(gid, false);
        // written: only the entry the renderer binds as the storage image (NoteStorage, Finish)
        if (t) r.textures.push_back(AddTexture(gid, t, storage ? "storage" : "sampled", false));
    }
    // a mip chain assembled from render targets (MipChainFor) also reads the targets of its levels
    id<MTLTexture> mips[kMaxMips];
    const uint32_t nmips = !storage && !depth_image && !ids.empty() && !S.tex->FindDepthTarget(img.address)
                               ? MipChainSources(img, S.tex->FindColorTarget(img.address), mips)
                               : 0;
    for (uint32_t m = 1; m < nmips; ++m) {
        GpuTextureId gid;
        if (mips[m] && S.tex->DescribeGpuTexture(mips[m], &gid))
            r.textures.push_back(AddTexture(gid, mips[m], "sampled", false));
    }
    if (ids.empty()) { // read from guest memory (or created there by GetStorageTexture)
        r.guest_bytes = ApproxImageBytes(img);
        g_reads.push_back({img.address, r.guest_bytes});
    }
    g_pass->images.push_back(std::move(r));
}

// BindStage, right after NoteImage of the same image: the GPU-owned texture the storage image
// resolved to (it may have been created from guest memory just now; S.storage_bound is consumed
// by SyncStorageToDepth before the pass ends).
void NoteStorage(id<MTLTexture> t) {
    if (!g_pass->images.empty()) g_storage.push_back({g_pass->images.size() - 1, t});
}

// DispatchEncode binds a stage again after a hazard flush: the first binding's records go.
void ForgetBindings(Stage st) {
    const std::string name = StageName(st);
    auto drop = [&](auto& v) {
        v.erase(std::remove_if(v.begin(), v.end(), [&](const auto& r) { return r.stage == name; }), v.end());
    };
    drop(g_pass->buffers);
    drop(g_pass->images);
    drop(g_pass->samplers);
    if (st == Stage::Compute) g_storage.clear(); // the rebind notes them again (image indices changed)
}

void NoteSampler(Stage st, uint32_t index, const uint32_t ss[4]) {
    SamplerRec r;
    r.stage = StageName(st);
    r.index = index;
    memcpy(r.ssharp, ss, sizeof(r.ssharp));
    g_pass->samplers.push_back(r);
}

// Next to the draw / dispatch call of DrawEncode / DispatchEncode.
void Encoded(uint64_t indirect_addr) {
    g_encoded = true;
    g_pass->indirect_addr = indirect_addr;
    if (indirect_addr) ReadGuest(S.ctx, indirect_addr, g_pass->indirect_args, sizeof(g_pass->indirect_args));
}

// Finish of a learning-frame pass, for ShaderSelection: what it read (guest ranges; sampled and
// storage textures it does not write) and wrote. A guest memory dispatch's probed writes are
// exact; a written texture or bound V# range is a write that may cover only part of the range.
// The pass's own render and depth targets are not followed: their contents before the pass are
// its "pre" contents, and every earlier draw into them would be a producer.
void Learn(PassRec* p) {
    g_learn_reads.assign(g_reads.begin(), g_reads.end());
    g_learn_writes.clear();
    for (const TextureRec& t : p->textures) {
        if (!t.id.size) continue; // EQAA twin
        if (t.written) g_learn_writes.push_back({t.id.address, t.id.size});
        else if (t.role.find("_color") == std::string::npos && t.role.find("_depth") == std::string::npos)
            g_learn_reads.push_back({t.id.address, t.id.size});
    }
    for (const BufferRec& b : p->buffers)
        if (b.written) g_learn_writes.push_back({b.addr, b.size});
    if (g_keyed) g_select.Learn(g_learn_key, g_learn_reads, g_guest_written, g_learn_writes);
    delete p;
    g_inputs_taken = false;
    g_guest_written.clear();
    g_tex.clear();
    g_reads.clear();
    g_storage.clear();
}

// End of the pass: inputs (before the GPU runs it), then its outputs once it completed.
void Finish() {
    PassRec* p = g_pass;
    g_pass = nullptr;
    if (!g_encoded && p->skipped.empty()) p->skipped = "not encoded (see the [gnm-draw] log)";
    // the textures storage images resolved to are written; ones this pass created
    // (GetStorageTexture with no entry before) are added, and linked from their image
    for (auto& [ii, t] : g_storage) {
        int k = -1;
        for (size_t i = 0; i < g_tex.size(); ++i)
            if (g_tex[i] == t) k = (int)i;
        GpuTextureId gid;
        if (k < 0 && S.tex->DescribeGpuTexture(t, &gid)) {
            TextureRec tr;
            tr.role = "t" + std::to_string(g_tex.size()) + "_storage";
            tr.id = gid;
            p->textures.push_back(std::move(tr)); // pre_file "": created from guest memory
            g_tex.push_back(t);
            k = (int)g_tex.size() - 1;
        }
        if (k < 0) continue;
        p->textures[k].written = true;
        if (ii < p->images.size()) {
            std::vector<int>& links = p->images[ii].textures;
            if (std::find(links.begin(), links.end(), k) == links.end()) links.push_back(k);
        }
    }
    KeyPass(p);
    if (g_learning) {
        Learn(p);
        return;
    }
    // estimated lengths (fetch shaders, ApproxImageBytes) may reach into guard pages: only readable
    // guest memory is stored
    if (p->replayable && !g_inputs_taken)
        for (auto& [a, n] : ClampReadable(S.ctx->mem_base, CoalescePages(g_reads, S.ctx->mem_size)))
            p->inputs.push_back(g_writer->Snapshot(S.ctx->mem_base, a, n));
    Commit();
    WaitAll();
    if (p->replayable) {
        for (const BufferRec& b : p->buffers)
            if (b.written)
                for (auto& [a, n] : ClampReadable(S.ctx->mem_base, {{b.addr, b.size}}))
                    p->outputs.push_back(g_writer->Snapshot(S.ctx->mem_base, a, n));
        for (auto& [a, n] : ClampReadable(S.ctx->mem_base, CoalescePages(g_guest_written, S.ctx->mem_size)))
            p->outputs.push_back(g_writer->Snapshot(S.ctx->mem_base, a, n));
    }
    g_inputs_taken = false;
    g_guest_written.clear();
    if (p->replayable) {
        // the pass has completed: what it read must still be what was stored (a guest thread
        // rewriting a buffer, or other GPU work writing a sampled texture, would make the stored
        // pass differ from the one that ran)
        p->unstable = ChangedInputs(S.ctx->mem_base, p->inputs, p->outputs);
        for (size_t i = 0; i < p->textures.size(); ++i) {
            TextureRec& tr = p->textures[i];
            std::vector<uint8_t> again;
            if (tr.written || tr.pre_file.empty() || !ReadForPass(g_tex[i], &tr, &again)) continue;
            if (Fnv64(again.data(), again.size()) != tr.pre_hash) p->unstable.push_back("texture " + tr.role);
        }
        for (const std::string& u : p->unstable)
            fprintf(stderr, "[gnm-capture] pass %u: %s changed while the pass ran\n", p->index, u.c_str());
    }
    for (size_t i = 0; i < p->textures.size(); ++i) {
        TextureRec& tr = p->textures[i];
        // contents that cannot be read (msaa, format, EQAA twin) have no view either; the budget only
        // limits the raw files: the view is always made
        if (!tr.written || (!tr.readback.empty() && tr.readback != kReadbackBudget)) continue;
        std::vector<uint8_t> raw;
        if (!ReadForPass(g_tex[i], &tr, &raw)) continue;
        g_writer->SavePost(p->index, p->replayable, &tr, raw, &g_budget, g_thumb);
    }
    g_writer->AddPass(std::move(*p));
    delete p;
    g_tex.clear();
    g_reads.clear();
    g_storage.clear();
}

// GmRunSync, after the probe found every page the dispatch accesses and before the
// dispatch runs: what it reads (resource tables and buffers noted so far, the guest ranges it
// reads or writes, read before it writes them) is stored now; what it writes is stored as its
// output in Finish.
void NoteGuestMemory(const std::vector<std::pair<uint64_t, uint64_t>>& accessed,
                     const std::vector<std::pair<uint64_t, uint64_t>>& written) {
    if (!g_pass) return;
    if (g_pass->replayable) {
        std::vector<std::pair<uint64_t, uint64_t>> in = g_reads;
        in.insert(in.end(), accessed.begin(), accessed.end());
        for (auto& [a, n] : ClampReadable(S.ctx->mem_base, CoalescePages(in, S.ctx->mem_size)))
            g_pass->inputs.push_back(g_writer->Snapshot(S.ctx->mem_base, a, n));
        g_inputs_taken = true;
    }
    g_guest_written = written;
}

// PresentImpl, before S.present_idx moves past the captured frame.
void EndFrame() {
    if (LearnFrame()) {
        g_select.FinishLearning();
        g_select.BeginFrame();
        fprintf(stderr, "[gnm-capture] learning frame %llu: %zu passes use the selected shaders, %zu producers selected\n",
                (unsigned long long)S.present_idx, g_select.NumReaders(), g_select.NumProducers());
        return;
    }
    if (!g_writer || S.present_idx != g_writer->Frame()) return;
    if (g_unknown)
        fprintf(stderr, "[gnm-capture] frame %llu: %u selected passes had no matching pass in the learning frame (the "
                        "frames differ: occurrences shifted, producers of those passes may be missing or wrong)\n",
                (unsigned long long)g_writer->Frame(), g_unknown);
    std::string err;
    if (g_writer->Finish(&err))
        fprintf(stderr, "[gnm-capture] frame %llu: %zu passes written to %s (%llu MiB raw, %llu write failures)\n",
                (unsigned long long)g_writer->Frame(), g_writer->NumPasses(), g_writer->Dir().c_str(),
                (unsigned long long)(g_budget.used >> 20), (unsigned long long)g_writer->Failures());
    else
        fprintf(stderr, "[gnm-capture] %s\n", err.c_str());
    g_writer.reset();
}

// Shutdown: the captured frame never presented (the game quit during it); what was recorded is
// written. A pass in progress is dropped.
void AtShutdown() {
    if (!g_writer) return;
    delete g_pass;
    g_pass = nullptr;
    if (S.present_idx != g_writer->Frame()) { // the captured frame never started
        fprintf(stderr, "[gnm-capture] quit before frame %llu started; nothing written\n",
                (unsigned long long)g_writer->Frame());
        g_writer.reset();
        return;
    }
    std::string err;
    if (g_writer->Finish(&err))
        fprintf(stderr, "[gnm-capture] frame %llu did not present: %zu passes written to %s at shutdown (%llu MiB raw, "
                        "%llu write failures)\n",
                (unsigned long long)g_writer->Frame(), g_writer->NumPasses(), g_writer->Dir().c_str(),
                (unsigned long long)(g_budget.used >> 20), (unsigned long long)g_writer->Failures());
    else
        fprintf(stderr, "[gnm-capture] %s\n", err.c_str());
    g_writer.reset();
}

// Draw / Dispatch: records a pass of the captured frame; finishes it if it never reached the
// encoder (skipped on the CP thread).
struct CpScope {
    bool on;
    CpScope(const char* type, const char* queue, const char* name, const uint32_t* regs) : on(InFrame()) {
        if (on) Begin(type, queue, name, regs);
    }
    ~CpScope() {
        if (on && g_pass) Finish();
    }
};
// DrawEncode / DispatchEncode: the pass ends with the function, on every return path.
struct EncodeScope {
    bool on;
    EncodeScope() : on(g_pass != nullptr) {
        if (on) StartEncode();
    }
    ~EncodeScope() {
        if (on && g_pass) Finish();
    }
};
} // namespace cap

// =====================================================================================
// Guest memory dispatches (gcn_shader_info.uses_guest_memory; GCN_GUEST_* in gcn_recompiler.h).
// A compute shader that reads or writes buffers through V#s it loads at run time (Uncharted 4's
// GPU copy shader: a table of source and destination V#s) cannot have them bound: it reaches
// guest memory through a page table of 64 MiB guest windows (the no-copy MTLBuffers of
// GuestBuffer). Which windows it needs is only known in the shader, so every such dispatch is
// four kernels in one compute encoder, with buffer barriers between them, using a fault buffer
// slot of its own (GmSlot):
//  1. the probe: the shader with the slot in mode 1. It writes nothing and records the unmapped
//     64 MiB pages it would access, the 1 MiB pages it would read and write, and the accesses it
//     could not make (GCN_GUEST_STATUS_*);
//  2. the gate (gm_gate): it passes if the probe found every page mapped, no status bit and
//     (fast path) no written page outside the prediction. Then it switches the slot to mode 0
//     and writes the dispatch's size as indirect arguments; otherwise a size of zero;
//  3. the pass: the shader again, dispatched with the gate's arguments, so a refused dispatch
//     runs no thread and has no effect;
//  4. the harvest (gm_harvest): moves the slot's non-zero fault words into its summary and
//     resets the slot, so the CPU neither clears nor scans it.
// Sync path (no prediction for the shader yet, a shader with storage or depth images, a captured
// frame): commit and wait once. A dispatch refused for unmapped pages has them mapped and runs again (up to 8
// times); one refused for another status is skipped (logged). The CP thread waits for it, as
// it reads guest memory for later draws (resource tables) that the dispatch may have written.
// Fast path (the 1 MiB pages the shader writes are predicted from its earlier dispatches): the
// dispatch is ticket t (GmSync). Nothing waits for it on the CPU:
//  - fast dispatches encoded back to back form a chain in the open compute encoder; the gate of
//    each also refuses it when the one before it is to run again. Before any other work
//    (GmCloseChain) the GPU signals 2t - 1 of the chain's last ticket and waits for 2t;
//  - a listener reads the gates' verdicts and signals 2t. If a probe met an unmapped page or a
//    written page outside the prediction, the encoder thread instead maps the pages and runs that
//    dispatch and the ones after it again, in order, on a second queue (the GPU holds every later
//    work at 2t), then signals 2t;
//  - the predicted pages are pending GPU writes for the texture hazard checks, and the CP thread
//    waits for t only before it reads one of them for a queue that synchronized after t was
//    handed out (GmCpGuarded), as GCN orders such reads.
// A shader with storage or depth images, or without a prediction yet, takes the sync path.
// =====================================================================================
struct GmSlot {
    id<MTLBuffer> buf = nil;
    uint32_t* f = nullptr;
};

// The slot's summary, after its GCN_GUEST_FAULT_DWORDS fault dwords (written by gm_gate and
// gm_harvest).
constexpr uint32_t kGmSum = GCN_GUEST_FAULT_DWORDS;
constexpr uint32_t kGmGate = 0;     // 0: no gate ran, 1: passed, 2: refused (kGmWhy* bits in kGmWhy)
constexpr uint32_t kGmWhy = 1;
constexpr uint32_t kGmStatus = 2;   // GCN_GUEST_STATUS after the pass (after the probe if refused)
constexpr uint32_t kGmCount = 3;    // non-zero fault words the harvest found
constexpr uint32_t kGmOverflow = 4; // 1: more than kGmMaxEntries (the rest stays in the slot)
constexpr uint32_t kGmArgs = 8;     // the pass's threadgroups x, y, z (indirect dispatch arguments)
constexpr uint32_t kGmEntries = 12; // (fault dword index, value) pairs
constexpr uint32_t kGmMaxEntries = 4096;
constexpr uint32_t kGmSlotDwords = kGmSum + kGmEntries + 2 * kGmMaxEntries;
constexpr uint32_t kGmWhyUnmapped = 1, kGmWhyStatus = 2, kGmWhyWritten = 4, kGmWhyForced = 8, kGmWhyChain = 16;
// refusals that make the dispatch run again (the others skip it, as on the sync path)
constexpr uint32_t kGmWhyRerun = kGmWhyUnmapped | kGmWhyWritten | kGmWhyChain;
// gm_gate flags
constexpr uint32_t kGmGateIndirect = 1, kGmGateCheckWritten = 2, kGmGateRefuse = 4, kGmGateChain = 8;
// Predicted pages per shader (gm_gate gets them as bytes: at most 4 KiB).
constexpr uint32_t kGmMaxPredicted = 1024;
static_assert(kGmSum % 4 == 0, "the indirect arguments must be 16-byte aligned");

// A dispatch's result, read from its slot's summary.
struct GmResult {
    uint32_t gate = 0, why = 0, status = 0;
    GmPages unmapped;      // 64 MiB pages
    GmPages read, written; // 1 MiB pages
};

struct GuestMemoryState {
    // GCN_GUEST_PAGES uint64 (shared). Once bound it is not changed but replaced (a dispatch that
    // has not run yet may use it); replaced tables are kept until their command buffers completed.
    id<MTLBuffer> table = nil;
    bool table_bound = false;
    std::vector<std::pair<uint64_t, id<MTLBuffer>>> old_tables; // (command buffer sequence, table)
    std::vector<uint32_t> pages;     // mapped 64 MiB pages
    std::vector<uint64_t> last_used; // per page: present index of its last access (0: not mapped)
    uint64_t wrong_guesses = 0;      // passes that met a page their probe had not reported
    id<MTLComputePipelineState> gate_pso = nil, harvest_pso = nil;
    id<MTLCommandQueue> queue2 = nil; // re-runs of missed tickets
    id<MTLBuffer> cur_slot = nil;     // the slot BindStage binds as the fault buffer
    std::vector<GmSlot> free_slots;
    std::deque<std::shared_ptr<GmTicket>> tickets; // encoded, not finished (ticket order)
    std::vector<std::shared_ptr<GmTicket>> chain;  // the open chain (g_gms.chain_open)
    id<MTLBuffer> chain_prev = nil;                // the slot of its last dispatch
    bool servicing = false;
    // Draw stages (GmDrawSlot): one fault slot for every draw, always in pass mode
    GmSlot draw_slot;
    uint64_t draw_harvested = ~0ull; // present index of the last harvest of draw_slot
} g_gmem;


// A mapped page no dispatch accessed for this many presents is dropped from the table (its
// window is then released by the usual idle sweep unless something else uses it).
constexpr uint64_t kGuestPageIdlePresents = 600;

// One threadgroup each; fault words and summary as above. GM_* come from GmLibrarySource.
static const char* kGmKernels = R"(
#include <metal_stdlib>
using namespace metal;
struct GateParams { uint dims[3]; uint flags; uint npred; };
// Passes the dispatch if its probe found every page mapped, no status bit and (flag 2) no
// written page outside pred (sorted). Refused (or flag 4): the pass gets zero threadgroups.
// Flag 8: also refused if the previous dispatch of the chain (prev) is to run again.
kernel void gm_gate(device uint* f [[buffer(0)]], constant GateParams& p [[buffer(1)]],
                    constant uint* pred [[buffer(2)]], device const uint* guest_args [[buffer(3)]],
                    device const uint* prev [[buffer(4)]],
                    uint tid [[thread_index_in_threadgroup]], uint nt [[threads_per_threadgroup]]) {
    threadgroup atomic_uint bad;
    if (tid == 0) atomic_store_explicit(&bad, 0u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    uint b = 0;
    for (uint i = tid; i < GM_PAGES / 32; i += nt)
        if (f[GM_UNMAPPED + i]) b |= 1u;
    if ((p.flags & 2u) != 0) {
        for (uint i = tid; i < GM_TRACK / 32 && (b & 4u) == 0; i += nt) {
            for (uint w = f[GM_WRITTEN + i]; w != 0; w &= w - 1) {
                const uint page = i * 32 + ctz(w);
                uint lo = 0, hi = p.npred;
                while (lo < hi) {
                    const uint mid = (lo + hi) / 2;
                    if (pred[mid] < page) lo = mid + 1;
                    else hi = mid;
                }
                if (lo == p.npred || pred[lo] != page) {
                    b |= 4u;
                    break;
                }
            }
        }
    }
    if (b != 0) atomic_fetch_or_explicit(&bad, b, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (tid != 0) return;
    uint why = atomic_load_explicit(&bad, memory_order_relaxed);
    if (f[GM_STATUS] != 0) why |= 2u;
    if ((p.flags & 4u) != 0) why |= 8u;
    if ((p.flags & 8u) != 0 && prev[GM_SUM + GM_S_GATE] == 2 && (prev[GM_SUM + GM_S_WHY] & GM_WHY_RERUN) != 0)
        why |= 16u;
    device uint* s = f + GM_SUM;
    s[GM_S_WHY] = why;
    if (why == 0) {
        const bool ind = (p.flags & 1u) != 0;
        s[GM_S_ARGS + 0] = ind ? guest_args[0] : p.dims[0];
        s[GM_S_ARGS + 1] = ind ? guest_args[1] : p.dims[1];
        s[GM_S_ARGS + 2] = ind ? guest_args[2] : p.dims[2];
        f[GM_MODE] = 0;
        s[GM_S_GATE] = 1;
    } else {
        s[GM_S_ARGS + 0] = 0;
        s[GM_S_ARGS + 1] = 0;
        s[GM_S_ARGS + 2] = 0;
        s[GM_S_GATE] = 2;
    }
}
// Moves the non-zero fault words (bitmaps) into the summary list and zeroes them, copies the
// status, and resets the slot to a probe.
kernel void gm_harvest(device uint* f [[buffer(0)]], uint tid [[thread_index_in_threadgroup]],
                       uint nt [[threads_per_threadgroup]]) {
    device uint* s = f + GM_SUM;
    device atomic_uint* count = reinterpret_cast<device atomic_uint*>(s + GM_S_COUNT);
    if (tid == 0) {
        s[GM_S_STATUS] = f[GM_STATUS];
        s[GM_S_OVERFLOW] = 0;
        atomic_store_explicit(count, 0u, memory_order_relaxed);
    }
    threadgroup_barrier(mem_flags::mem_device);
    for (uint i = GM_UNMAPPED + tid; i < GM_SUM; i += nt) {
        const uint w = f[i];
        if (w == 0) continue;
        const uint k = atomic_fetch_add_explicit(count, 1u, memory_order_relaxed);
        if (k < GM_MAX_ENTRIES) {
            s[GM_S_ENTRIES + 2 * k] = i;
            s[GM_S_ENTRIES + 2 * k + 1] = w;
            f[i] = 0;
        } else {
            s[GM_S_OVERFLOW] = 1;
        }
    }
    if (tid == 0) {
        f[GM_MODE] = 1;
        f[GM_STATUS] = 0;
    }
}
)";

std::string GmLibrarySource() {
    char defs[1024];
    snprintf(defs, sizeof(defs),
             "#define GM_PAGES %uu\n#define GM_TRACK %uu\n#define GM_MODE %uu\n#define GM_STATUS %uu\n"
             "#define GM_UNMAPPED %uu\n#define GM_WRITTEN %uu\n#define GM_SUM %uu\n#define GM_S_GATE %uu\n"
             "#define GM_S_WHY %uu\n#define GM_S_STATUS %uu\n#define GM_S_COUNT %uu\n#define GM_S_OVERFLOW %uu\n"
             "#define GM_S_ARGS %uu\n#define GM_S_ENTRIES %uu\n#define GM_MAX_ENTRIES %uu\n#define GM_WHY_RERUN %uu\n",
             GCN_GUEST_PAGES, GCN_GUEST_TRACK_PAGES, GCN_GUEST_MODE, GCN_GUEST_STATUS, GCN_GUEST_UNMAPPED,
             GCN_GUEST_WRITTEN, kGmSum, kGmGate, kGmWhy, kGmStatus, kGmCount, kGmOverflow, kGmArgs, kGmEntries,
             kGmMaxEntries, kGmWhyRerun);
    return std::string(defs) + kGmKernels;
}

bool EnsureGuestMemoryState() {
    if (g_gmem.table) return true;
    if (!g_gms.ev) return false;
    NSError* err = nil;
    id<MTLLibrary> lib = [S.dev newLibraryWithSource:[NSString stringWithUTF8String:GmLibrarySource().c_str()]
                                             options:nil
                                               error:&err];
    id<MTLFunction> gate = [lib newFunctionWithName:@"gm_gate"], harvest = [lib newFunctionWithName:@"gm_harvest"];
    if (gate) g_gmem.gate_pso = [S.dev newComputePipelineStateWithFunction:gate error:&err];
    if (harvest) g_gmem.harvest_pso = [S.dev newComputePipelineStateWithFunction:harvest error:&err];
    if (!g_gmem.gate_pso || !g_gmem.harvest_pso) {
        fprintf(stderr, "[gnm-draw] guest memory kernels failed: %s\n",
                err ? err.localizedDescription.UTF8String : "no function");
        g_gmem.gate_pso = g_gmem.harvest_pso = nil;
        return false;
    }
    g_gmem.table = [S.dev newBufferWithLength:GCN_GUEST_PAGES * sizeof(uint64_t) options:MTLResourceStorageModeShared];
    if (!g_gmem.table) return false;
    S.be->MakeResident(g_gmem.table, GnmResidencyClass::Builtin);
    memset(g_gmem.table.contents, 0, g_gmem.table.length);
    g_gmem.last_used.assign(GCN_GUEST_PAGES, 0);
    return true;
}

GmSlot GmAcquireSlot() {
    if (!g_gmem.free_slots.empty()) {
        GmSlot s = g_gmem.free_slots.back();
        g_gmem.free_slots.pop_back();
        return s;
    }
    GmSlot s;
    s.buf = [S.dev newBufferWithLength:kGmSlotDwords * sizeof(uint32_t) options:MTLResourceStorageModeShared];
    if (!s.buf) return s;
    S.be->MakeResident(s.buf, GnmResidencyClass::Builtin); // slots are pooled for the process lifetime
    s.f = (uint32_t*)s.buf.contents;
    memset(s.f, 0, kGmSlotDwords * sizeof(uint32_t));
    s.f[GCN_GUEST_MODE] = 1;
    return s;
}

// Back to the pool once the GPU is done with it (its harvest ran) and its summary was read.
void GmReleaseSlot(GmSlot& s) {
    if (s.buf) g_gmem.free_slots.push_back(s);
    s = GmSlot{};
}

// Reads a slot's summary after its harvest ran, and makes the slot ready for reuse.
void GmReadSlot(const GmSlot& slot, GmResult* r) {
    uint32_t* f = slot.f;
    uint32_t* s = f + kGmSum;
    r->gate = s[kGmGate];
    r->why = s[kGmWhy];
    r->status = s[kGmStatus];
    r->unmapped.clear();
    r->read.clear();
    r->written.clear();
    const auto add = [r](uint32_t i, uint32_t w) {
        GmPages* to = nullptr;
        uint32_t base = 0;
        if (i >= GCN_GUEST_WRITTEN) to = &r->written, base = GCN_GUEST_WRITTEN;
        else if (i >= GCN_GUEST_READ) to = &r->read, base = GCN_GUEST_READ;
        else if (i >= GCN_GUEST_UNMAPPED) to = &r->unmapped, base = GCN_GUEST_UNMAPPED;
        if (!to) return;
        for (; w; w &= w - 1) to->push_back((i - base) * 32 + (uint32_t)__builtin_ctz(w));
    };
    const uint32_t n = std::min(s[kGmCount], kGmMaxEntries);
    for (uint32_t k = 0; k < n; ++k) add(s[kGmEntries + 2 * k], s[kGmEntries + 2 * k + 1]);
    if (s[kGmOverflow]) {
        // the harvest's list was full: the rest is still in the bitmaps
        for (uint32_t i = GCN_GUEST_UNMAPPED; i < kGmSum; ++i)
            if (f[i]) {
                add(i, f[i]);
                f[i] = 0;
            }
    }
    for (GmPages* v : {&r->unmapped, &r->read, &r->written}) {
        std::sort(v->begin(), v->end());
        v->erase(std::unique(v->begin(), v->end()), v->end());
    }
    s[kGmGate] = s[kGmWhy] = s[kGmStatus] = s[kGmCount] = s[kGmOverflow] = 0;
}

// Consecutive pages as (address, bytes) ranges.
std::vector<std::pair<uint64_t, uint64_t>> GmRanges(const GmPages& pages, uint32_t shift) {
    std::vector<std::pair<uint64_t, uint64_t>> out;
    for (uint32_t p : pages) {
        const uint64_t a = (uint64_t)p << shift;
        if (!out.empty() && out.back().first + out.back().second == a) out.back().second += 1ull << shift;
        else out.push_back({a, 1ull << shift});
    }
    return out;
}

bool GmSubset(const GmPages& a, const GmPages& b) {
    return std::includes(b.begin(), b.end(), a.begin(), a.end());
}

// Queues the invalidation of sampled textures over the pages res's pass wrote outside pred (all
// it wrote without one: the sync path). Any thread; never blocks on the texture cache.
void GmInvalidateOutside(const GmResult& res, const GmPages* pred) {
    if (res.written.empty()) return;
    thread_local GmPages extra;
    extra.clear();
    if (pred) std::set_difference(res.written.begin(), res.written.end(), pred->begin(), pred->end(), std::back_inserter(extra));
    const GmPages& stale = pred ? extra : res.written;
    if (stale.empty()) return;
    std::lock_guard<std::mutex> lk(g_gm_inval.mu);
    for (auto [a, n] : GmRanges(stale, GCN_GUEST_TRACK_SHIFT)) g_gm_inval.ranges.push_back({a, n, ~0ull});
    g_gm_inval.any.store(true, std::memory_order_release);
}

// The no-copy window holding guest page p and the table entry for it: the GPU address of guest
// address p << GCN_GUEST_PAGE_SHIFT. Page 0's window starts at PS4_GNM_WINDOW0_BASE
// (= GCN_GUEST_FIRST_MAPPED, below which the shader never accesses). `access`: Write for compute
// (guest memory stores), Read for draw stages, which only load through guest memory (the
// translator refuses their stores): a window noted written orders every later encoder that uses
// it after this one (alias waits on Metal 3).
static_assert(PS4_GNM_WINDOW0_BASE == GCN_GUEST_FIRST_MAPPED, "page 0's first mapped address");
bool GuestPageWindow(uint32_t p, __strong id<MTLBuffer>* mb, uint64_t* entry, GuestAccess access) {
    const uint64_t start = (uint64_t)p << GCN_GUEST_PAGE_SHIFT;
    const uint64_t first = p == 0 ? PS4_GNM_WINDOW0_BASE : start;
    const uint64_t end = start + (1ull << GCN_GUEST_PAGE_SHIFT);
    if (first >= S.ctx->mem_size) return false;
    uint64_t off = 0;
    // only the no-copy window itself: no read-only copy of a page with holes (it would be made,
    // 64 MiB of it, only to be dropped)
    if (!GuestBuffer(first, std::min<uint64_t>(end, S.ctx->mem_size) - first, access, mb, &off, false) || off != 0)
        return false;
    if ((const uint8_t*)(*mb).contents != (const uint8_t*)S.ctx->mem_base + first) return false;
    *entry = (*mb).gpuAddress - (first - start);
    return true;
}

// Adds the 64 MiB pages to the mapped set (their table entries are written by the next bind).
// queue nullptr: for draws (GmDrawSlot), which are not re-run: they read 0 from such a page. A page
// draws could not map is remembered (g_mem.draw_unmappable, cleared with g_mem.refused) and not
// tried again by every later harvest; it is logged once. For draws the other pages are still mapped
// and *mapped (if not null) counts the pages mapped; for dispatches the first failure ends it (false).
bool GmMapPages(const GmPages& pages, const char* queue, uint64_t code_addr, size_t* mapped = nullptr) {
    static std::unordered_set<uint32_t> draw_logged; // encoder thread
    bool ok = true;
    // a remap clears draw_unmappable (DropRemappedWindows): apply it before that is consulted
    if (!queue && g_remap_pending.load(std::memory_order_acquire)) DropRemappedWindows();
    for (uint32_t p : pages) {
        if (!queue && g_mem.draw_unmappable.count(p)) {
            ok = false;
            continue;
        }
        id<MTLBuffer> mb = nil;
        uint64_t entry = 0;
        if (!GuestPageWindow(p, &mb, &entry, queue ? GuestAccess::Write : GuestAccess::Read)) {
            const unsigned long long a = (unsigned long long)((uint64_t)p << GCN_GUEST_PAGE_SHIFT);
            if (!queue) {
                g_mem.draw_unmappable.insert(p);
                g_gm_stats.draw_unmappable.fetch_add(1, std::memory_order_relaxed);
                if (draw_logged.insert(p).second)
                    RLog("guestmem-draw", "guest page 0x%llx (64 MiB) cannot be mapped: draws read 0 there", a);
                ok = false;
                continue;
            }
            RLog("guestmem", "%s CS 0x%llx skipped: guest page 0x%llx (64 MiB) cannot be mapped", queue,
                 (unsigned long long)code_addr, a);
            return false;
        }
        if (!g_gmem.last_used[p]) g_gmem.pages.push_back(p);
        g_gmem.last_used[p] = std::max<uint64_t>(S.present_idx, 1);
        if (mapped) ++*mapped;
    }
    return ok;
}

// The page table with an entry for every mapped page; each window is (re)created through
// GuestBuffer (windows idle for a while are released) and made resident on the encoder. Pages
// idle for kGuestPageIdlePresents are dropped first. A table that changes after it was bound is
// copied first (passes encoded earlier keep theirs).
bool BindGuestPageTable(Binder& b, __strong id<MTLBuffer>* out) {
    if (!EnsureGuestMemoryState()) return false;
    // draw stages only load through guest memory: their windows are used read-only
    const bool written = b.stage == Stage::Compute;
    {
        const uint64_t done = CompletedSeq();
        size_t k = 0;
        for (auto& t : g_gmem.old_tables)
            if (t.first > done) g_gmem.old_tables[k++] = t;
        g_gmem.old_tables.resize(k);
    }
    static std::vector<std::pair<uint32_t, uint64_t>>& want = *new std::vector<std::pair<uint32_t, uint64_t>>; // (page, entry); encoder thread only
    static std::vector<id<MTLBuffer>>& windows = *new std::vector<id<MTLBuffer>>;
    want.clear();
    windows.clear();
    const uint64_t* cur = (const uint64_t*)g_gmem.table.contents;
    bool change = false;
    size_t n = 0;
    for (uint32_t p : g_gmem.pages) {
        if (S.present_idx > g_gmem.last_used[p] + kGuestPageIdlePresents) {
            g_gmem.last_used[p] = 0;
            want.push_back({p, 0});
            change |= cur[p] != 0;
            continue;
        }
        g_gmem.pages[n++] = p;
        id<MTLBuffer> mb = nil;
        uint64_t entry = 0;
        if (!GuestPageWindow(p, &mb, &entry, written ? GuestAccess::Write : GuestAccess::Read))
            entry = 0; // the next probe reports it again
        else windows.push_back(mb);
        want.push_back({p, entry});
        change |= cur[p] != entry;
    }
    g_gmem.pages.resize(n);
    if (change) {
        if (g_gmem.table_bound) {
            id<MTLBuffer> t = [S.dev newBufferWithLength:g_gmem.table.length options:MTLResourceStorageModeShared];
            if (!t) return false;
            memcpy(t.contents, g_gmem.table.contents, g_gmem.table.length);
            S.be->MakeResident(t, GnmResidencyClass::Builtin);
            // passes encoded so far (up to the open seq) keep reading the old table
            S.be->Evict(g_gmem.table, S.committed_seq + 1);
            g_gmem.old_tables.push_back({S.committed_seq + 1, g_gmem.table});
            g_gmem.table = t;
            g_gmem.table_bound = false;
        }
        uint64_t* table = (uint64_t*)g_gmem.table.contents;
        for (auto [p, e] : want) table[p] = e;
    }
    for (id<MTLBuffer> mb : windows) b.Use(mb, written);
    windows.clear();
    g_gmem.table_bound = true;
    *out = g_gmem.table;
    return true;
}

std::string GuestStatusText(uint32_t st) {
    std::string t;
    const auto add = [&](uint32_t bit, const char* what) {
        if (!(st & bit)) return;
        if (!t.empty()) t += ", ";
        t += what;
    };
    add(GCN_GUEST_STATUS_UNMAPPED, "an unmapped guest page");
    add(GCN_GUEST_STATUS_FORMAT, "a V# format other than 32-bit uint/sint/float");
    add(GCN_GUEST_STATUS_SWIZZLE, "a swizzled V#");
    add(GCN_GUEST_STATUS_RANGE, "an address past 40 bits or in the first 64 KiB");
    add(GCN_GUEST_STATUS_UNALIGNED, "an unaligned dword");
    return t;
}

// Marks the 64 MiB pages of the 1 MiB pages read / written as used now.
void TouchGuestPages(const GmResult& r) {
    for (const GmPages* v : {&r.read, &r.written})
        for (uint32_t t : *v) {
            const uint32_t p = t >> (GCN_GUEST_PAGE_SHIFT - GCN_GUEST_TRACK_SHIFT);
            if (g_gmem.last_used[p]) g_gmem.last_used[p] = std::max<uint64_t>(S.present_idx, 1);
        }
}

// ---- Predictions: the 1 MiB pages each shader (permutation) wrote so far ----
struct GmPredictions {
    std::mutex mu;
    std::unordered_map<uint64_t, std::shared_ptr<const GmPages>> by_shader;
} g_gm_pred;

std::shared_ptr<const GmPages> GmPredict(uint64_t uid) {
    std::lock_guard<std::mutex> lk(g_gm_pred.mu);
    auto it = g_gm_pred.by_shader.find(uid);
    return it == g_gm_pred.by_shader.end() ? nullptr : it->second;
}

// Adds pages a dispatch of the shader wrote (a union that only grows; it starts over from these
// pages if it would exceed kGmMaxPredicted).
void GmLearn(uint64_t uid, const GmPages& written) {
    std::lock_guard<std::mutex> lk(g_gm_pred.mu);
    auto& cur = g_gm_pred.by_shader[uid];
    if (cur && GmSubset(written, *cur)) return;
    auto next = std::make_shared<GmPages>();
    if (cur) std::set_union(cur->begin(), cur->end(), written.begin(), written.end(), std::back_inserter(*next));
    else *next = written;
    if (next->size() > kGmMaxPredicted) *next = written;
    if (next->size() > kGmMaxPredicted) next->resize(kGmMaxPredicted + 1); // too many: no fast path
    cur = std::move(next);
}

uint64_t GuestMemoryWrongGuesses() { return g_gmem.wrong_guesses; }

} // namespace

GuestMemoryStats GetGuestMemoryStats() {
    GuestMemoryStats st;
    st.sync = g_gm_stats.sync.load();
    st.fast = g_gm_stats.fast.load();
    st.reruns = g_gm_stats.reruns.load();
    st.refused = g_gm_stats.refused.load();
    st.mispredicted = g_gm_stats.mispredicted.load();
    st.cp_waits = g_gm_stats.cp_waits.load();
    st.cp_wait_us = g_gm_stats.cp_wait_us.load();
    st.chains = g_gm_stats.chains.load();
    st.rerun_wait_us = g_gm_stats.rerun_wait_us.load();
    st.rerun_wait_max_us = g_gm_stats.rerun_wait_max_us.load();
    st.no_slot = g_gm_stats.no_slot.load();
    st.draw_mapped = g_gm_stats.draw_mapped.load();
    st.draw_harvests = g_gm_stats.draw_harvests.load();
    st.draw_unmappable = g_gm_stats.draw_unmappable.load();
    st.no_indirect = g_gm_stats.no_indirect.load();
    st.pass_failed = g_gm_stats.pass_failed.load();
    st.waits = S.n_waits_total.load();
    st.final_ticket = std::min<uint64_t>(GmResolved(), g_gms.issued.load());
    return st;
}

bool Hung() { return S.hung; }

void TestWithholdNextTicket() { g_gms.withhold_next.store(true); }

void TestHoldTextureCacheLock(bool hold) {
    if (!S.tex) return;
    if (hold) S.tex->TestMutex().lock();
    else S.tex->TestMutex().unlock();
}

namespace {

// ---- T# tables a shader indexes at run time (gcn_image_table) ----
// Written by the encoder thread, read by GetImageTableStats from any thread.
struct AtomicTableStats {
    std::atomic<uint64_t> bound{0}, empty{0}, rejected{0}, unmapped{0}, hazard_drains{0}, hazard_skips{0};
} g_table_stats;
bool g_address_check = false; // gcn_set_image_address_check installed (Init)
struct StageImages;
void DecodeStageImages(const Shader* s, const std::vector<uint32_t>& flat, StageImages* out);

// Entries of the tables of one binding of a stage, per table (bound / empty / rejected /
// unmapped, with the first unmapped T# address).
struct TableEntryCounts {
    static constexpr uint32_t kMax = 8;
    uint32_t bound[kMax] = {}, empty[kMax] = {}, rejected[kMax] = {}, unmapped[kMax] = {};
    uint64_t first_unmapped[kMax] = {};
};

// One table entry as BindStage bound it: a texture, an all-zero T# (past the table, or a loop
// entry the draw does not reach), or a T# for which no texture could be bound.
void NoteTableEntry(const Shader* s, const std::vector<uint32_t>& flat, uint32_t i, bool bound, TableEntryCounts& c) {
    const gcn_image_binding& ib = s->imgs[i];
    const uint32_t t = std::min(ib.table, TableEntryCounts::kMax - 1);
    if (bound) {
        c.bound[t]++;
        g_table_stats.bound.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // the raw dwords the SRT program copied (SingleLoad: consecutive flatbuf dwords)
    bool zero = true;
    const uint32_t nd = ib.is_r128 ? 4 : 8;
    for (uint32_t k = 0; k < nd; ++k) {
        const uint32_t at = ib.sharp.offsets[k];
        if (at < flat.size() && flat[at] != 0) zero = false;
    }
    if (zero) {
        c.empty[t]++;
        g_table_stats.empty.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // a T# at a provably unmapped address (gcn_set_image_address_check) is told apart from one no
    // texture could be made of, so a real texture hidden by the address check would show
    const uint32_t d0 = ib.sharp.offsets[0] < flat.size() ? flat[ib.sharp.offsets[0]] : 0;
    const uint32_t d1 = ib.sharp.offsets[1] < flat.size() ? flat[ib.sharp.offsets[1]] : 0;
    // base_address is T# bits 0-37 (dword 1 bits 6-7: mtype_l2, then min_lod)
    const uint64_t addr = ((uint64_t)d0 | (uint64_t)(d1 & 0x3F) << 32) << 8;
    if (g_address_check && !GuestAddressMapped(S.ctx, addr)) {
        if (!c.unmapped[t]) c.first_unmapped[t] = addr;
        c.unmapped[t]++;
        g_table_stats.unmapped.fetch_add(1, std::memory_order_relaxed);
    } else {
        c.rejected[t]++;
        g_table_stats.rejected.fetch_add(1, std::memory_order_relaxed);
    }
}

// Once per shader, at its first binding: what its tables' entries held.
void LogTableEntries(const Shader* s, const TableEntryCounts& c) {
    if (s->tables_logged.exchange(true, std::memory_order_relaxed)) return;
    static const char* const kinds[] = {"none", "records", "indexed", "loop"};
    for (uint32_t t = 0; t < s->tables.size(); ++t) {
        const gcn_image_table& tb = s->tables[t];
        const uint32_t k = std::min(t, TableEntryCounts::kMax - 1);
        fprintf(stderr,
                "[gnm-draw] shader 0x%llx (hw %u) T# table %u (%s, buffer %u dword %u + %u * index, %u site(s)): "
                "%u entries bound for %u reached: %u texture(s), %u empty, %u rejected, %u unmapped (first 0x%llx)\n",
                (unsigned long long)s->code_addr, s->hw, t, kinds[tb.kind < 4 ? tb.kind : 0], tb.table_buffer,
                tb.first_dw, tb.stride_dw, tb.num_sites, tb.num_entries, tb.reached, c.bound[k], c.empty[k],
                c.rejected[k], c.unmapped[k], (unsigned long long)c.first_unmapped[k]);
    }
}

// The SRT walker copied the tables' entries from guest memory when the CP prepared the draw or
// dispatch. If GPU work that had not completed by then writes that memory, the copy is stale
// (the same limitation the flattened user data has elsewhere; Uncharted 4's tables are written
// by the CPU). Whether a write had completed is judged at the CP's copy (`snap`, TableReadBegin),
// not here: by the time the encoder runs this the write may have completed (the CP ran ahead of
// the encoder, a captured pass drained the GPU when it started encoding, or an earlier check
// drained it). One check per draw or dispatch, over the table reads of all its stages: on a hit
// the GPU is drained if the write may still be pending, and every stage with tables copies its
// tables again. Each stage's permutation was chosen from the stale tables (entry-count bucket,
// entry image types), so each is checked against the tables read again: false when one no longer
// fits, and the caller skips the draw (counted, logged once per shader). Encoder thread, before
// the stages' images are used.
// cap::SetSkipped reason of a pass ResolveTableHazards refuses. Reachable in a captured frame
// too: the drain at the start of a captured pass's encoding does not make the CP's copy current.
const char* const kCapTableHazardSkip = "T# tables written by pending GPU work no longer fit the permutation";

struct TableStage {
    const Shader* s = nullptr; // nullptr: no such stage
    const uint32_t* ud = nullptr;
    const gcn_runtime_info* rt = nullptr;
    std::vector<uint32_t>* flat = nullptr;
    StageImages* img = nullptr;
};

bool ResolveTableHazards(const TableStage* st, uint32_t n, uint64_t snap) {
    bool tables = false;
    for (uint32_t k = 0; k < n; ++k) tables |= st[k].s && !st[k].s->tables.empty();
    if (!tables) return true;
    PruneTableWrites();
    if (S.gpu_written.empty() && S.table_written.empty()) return true;
    static thread_local std::vector<std::array<uint64_t, 2>> reads;
    const Shader* hit_s = nullptr;
    GpuWrite hit{};
    uint64_t hit_read[2] = {}, pending_seq = 0;
    for (uint32_t k = 0; k < n && !hit_s; ++k) {
        const Shader* s = st[k].s;
        if (!s || s->tables.empty()) continue;
        std::vector<uint32_t>& flat = *st[k].flat;
        uint32_t nr = gcn_shader_image_table_reads(s->sh, flat.data(), reinterpret_cast<uint64_t(*)[2]>(reads.data()),
                                                   (uint32_t)reads.size());
        if (nr > reads.size()) {
            reads.resize(nr);
            nr = gcn_shader_image_table_reads(s->sh, flat.data(), reinterpret_cast<uint64_t(*)[2]>(reads.data()), nr);
        }
        for (uint32_t i = 0; i < nr; ++i) {
            if (!OverlapsWriteAfter(reads[i][0], reads[i][1], snap, &hit, &pending_seq)) continue;
            hit_s = s;
            hit_read[0] = reads[i][0];
            hit_read[1] = reads[i][1];
            break;
        }
    }
    if (!hit_s) return true;
    g_table_stats.hazard_drains.fetch_add(1, std::memory_order_relaxed);
    char key[64];
    snprintf(key, sizeof(key), "table-hazard-%llx", (unsigned long long)hit_s->code_addr);
    RLog(key, "shader 0x%llx: T# table read 0x%llx+0x%llx written by GPU work pending when the CP read it "
              "(0x%llx+0x%llx): draining, tables read again",
         (unsigned long long)hit_s->code_addr, (unsigned long long)hit_read[0], (unsigned long long)hit_read[1],
         (unsigned long long)hit.addr, (unsigned long long)hit.size);
    if (pending_seq > CompletedSeq()) {
        Commit();
        WaitAll();
    }
    for (uint32_t k = 0; k < n; ++k) {
        const Shader* s = st[k].s;
        if (!s || s->tables.empty()) continue;
        BuildFlat(s, st[k].ud, st[k].flat);
        if (st[k].img) DecodeStageImages(s, *st[k].flat, st[k].img);
    }
    for (uint32_t k = 0; k < n; ++k) {
        const Shader* s = st[k].s;
        if (!s || s->tables.empty() || gcn_shader_is_compatible(s->sh, st[k].ud, st[k].rt, ReadGuest, S.ctx)) continue;
        g_table_stats.hazard_skips.fetch_add(1, std::memory_order_relaxed);
        char why[256];
        gcn_shader_explain_mismatch(s->sh, st[k].ud, st[k].rt, ReadGuest, S.ctx, why, sizeof(why));
        snprintf(key, sizeof(key), "table-hazard-skip-%llx", (unsigned long long)s->code_addr);
        RLog(key, "shader 0x%llx: the T# tables written by the GPU no longer fit its permutation (%s): skipped",
             (unsigned long long)s->code_addr, why);
        return false;
    }
    return true;
}

// Draw stages that load through guest memory (gcn_shader_info.uses_guest_memory in a VS or PS:
// ADDR64 loads, loads through V#s loaded at run time; the translator refuses guest memory stores
// outside compute). They bind the page table like a dispatch, and as their fault buffer one slot
// shared by every such draw that stays in pass mode (GCN_GUEST_MODE 0): a draw is not probed and
// not run again, so a load from a 64 MiB page that is not mapped yet reads 0 and sets the page's
// bit and GCN_GUEST_STATUS_UNMAPPED. Each such draw first reads the status dword the GPU wrote
// so far (before its page table is bound): with a status bit, the pages met unmapped are mapped
// for this draw and the later ones. The GPU sets the bits and only this clears them; a
// bit set while it is cleared is set again by the next draw that meets the page. Once per present
// the 64 MiB pages whose 1 MiB pages the draws read keep their last use (kGuestPageIdlePresents).
// Draws of shaders without guest memory never come here.
id<MTLBuffer> GmDrawSlot() {
    if (!EnsureGuestMemoryState()) return nil;
    GmSlot& slot = g_gmem.draw_slot;
    if (!slot.buf) {
        // Not hazard tracked: the draws only set bits in it (atomic ORs, in any order) and the CPU
        // reads and clears them; tracked, every render pass with such a draw would wait for the
        // previous one (Metal 3), as the slot is the one buffer these draws write.
        slot.buf = [S.dev newBufferWithLength:kGmSlotDwords * sizeof(uint32_t)
                                      options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
        if (!slot.buf) return nil;
        S.be->MakeResident(slot.buf, GnmResidencyClass::Builtin); // kept for the process lifetime
        slot.f = (uint32_t*)slot.buf.contents;
        memset(slot.f, 0, kGmSlotDwords * sizeof(uint32_t));
        slot.f[GCN_GUEST_MODE] = 0;
    }
    uint32_t* f = slot.f;
    if (__atomic_load_n(&f[GCN_GUEST_STATUS], __ATOMIC_RELAXED)) {
        const uint32_t status = __atomic_exchange_n(&f[GCN_GUEST_STATUS], 0u, __ATOMIC_RELAXED);
        if (status & ~GCN_GUEST_STATUS_UNMAPPED)
            RLog("guestmem-draw", "a draw's guest memory access was not made (status 0x%x: format, swizzle, "
                                  "range or alignment; GCN_GUEST_STATUS_*)", status & ~GCN_GUEST_STATUS_UNMAPPED);
        GmPages unmapped;
        for (uint32_t i = 0; i < GCN_GUEST_PAGES / 32; ++i) {
            if (!__atomic_load_n(&f[GCN_GUEST_UNMAPPED + i], __ATOMIC_RELAXED)) continue;
            for (uint32_t w = __atomic_exchange_n(&f[GCN_GUEST_UNMAPPED + i], 0u, __ATOMIC_RELAXED); w; w &= w - 1)
                unmapped.push_back(i * 32 + (uint32_t)__builtin_ctz(w));
        }
        g_gm_stats.draw_harvests.fetch_add(1, std::memory_order_relaxed);
        size_t mapped = 0;
        if (!unmapped.empty()) GmMapPages(unmapped, nullptr, 0, &mapped);
        if (mapped) g_gm_stats.draw_mapped.fetch_add(mapped, std::memory_order_relaxed);
    }
    if (g_gmem.draw_harvested != S.present_idx) {
        g_gmem.draw_harvested = S.present_idx;
        const uint64_t now = std::max<uint64_t>(S.present_idx, 1);
        constexpr uint32_t kTrackPerPage = 1u << (GCN_GUEST_PAGE_SHIFT - GCN_GUEST_TRACK_SHIFT); // 64
        for (uint32_t i = 0; i < GCN_GUEST_TRACK_PAGES / 32; ++i) {
            if (!__atomic_load_n(&f[GCN_GUEST_READ + i], __ATOMIC_RELAXED)) continue;
            // exchange, as the UNMAPPED loop: a bit the GPU sets between a load and a store is kept
            if (!__atomic_exchange_n(&f[GCN_GUEST_READ + i], 0u, __ATOMIC_RELAXED)) continue;
            const uint32_t p = i * 32 / kTrackPerPage;
            if (g_gmem.last_used[p]) g_gmem.last_used[p] = now;
        }
    }
    return slot.buf;
}

bool BindStage(const Shader* s, const uint32_t ud[16], const std::vector<uint32_t>& flat, Binder& b,
               const BindCtx& bc, const gcn_push_data& push, std::string* why) {
    (void)ud;
    static const bool dumpflat = getenv("PS4_GNM_TRACE_BUFS") != nullptr;
    if (ShouldTrace()) {
        if (dumpflat) {
            fprintf(stderr, "[gnm-trace]    flatbuf (%u dw):", s->info.flatbuf_size_dw);
            for (uint32_t k = 0; k < flat.size() && k < 128; ++k)
                fprintf(stderr, "%s%08x", (k % 16) ? " " : "\n[gnm-trace]      ", flat[k]);
            fprintf(stderr, "\n");
        }
    }

    // Per-stage copy: buf_sizes holds this shader's guest buffer sizes. The shaders range
    // check every buffer access against them like GCN does (OOB stores/atomics dropped, OOB
    // loads return 0) - games rely on it, e.g. compute dispatches rounded up to whole thread
    // groups whose extra threads store past the end of the buffer (TLOU cloth simulation
    // clobbering the next heap block's header -> crash on world reload).
    gcn_push_data pd = push;
    static const char* bounds_mode = getenv("PS4_GNM_BOUNDS"); // debug: off | raw
    const bool bounds_off = bounds_mode && !strcmp(bounds_mode, "off");
    memset(pd.buf_sizes, bounds_off ? 0xff : 0, sizeof(pd.buf_sizes));
    // Shaders with more buffers than Metal slots reach their guest buffers through an argument
    // buffer: a table of GPU addresses (gcn_shader_info.msl_argument_buffer).
    std::vector<uint64_t> argtab(s->info.msl_argument_buffer >= 0 ? s->info.msl_argument_buffer_entries : 0,
                                 S.dummy.gpuAddress);
    if (!argtab.empty()) b.Use(S.dummy, false);
    for (uint32_t i = 0; i < s->bufs.size(); ++i) {
        const gcn_buffer_binding& bb = s->bufs[i];
        if (bb.num_msl_buffers == 0 && bb.msl_arg_index >= argtab.size()) continue;
        id<MTLBuffer> mb = nil;
        uint64_t off = 0;
        switch (bb.type) {
        case GCN_BUFFER_GUEST: {
            uint32_t v[4];
            gcn_buffer_desc bd{};
            if (gcn_shader_fetch_buffer_sharp(s->sh, i, flat.data(), s->code_addr, v)) {
                gcn_decode_buffer(v, &bd);
            }
            if (bd.base_address == 0 || bd.size_bytes == 0) {
                mb = S.dummy;
                break;
            }
            uint64_t base = bd.base_address;
            if (static const bool lb = getenv("PS4_GNM_LOG_BIGBUF") != nullptr; lb && bd.size_bytes > (1u << 20)) {
                static std::set<uint64_t>& seen = *new std::set<uint64_t>;
                if (seen.insert(s->code_addr * 64 + i).second)
                    fprintf(stderr, "[gnm-bigbuf] shader 0x%llx buf %u: base 0x%llx size 0x%x stride %u nrec 0x%x%s\n",
                            (unsigned long long)s->code_addr, i, (unsigned long long)bd.base_address, bd.size_bytes,
                            bd.stride, bd.num_records, bb.is_written ? " written" : "");
            }
            const bool unbounded = bd.size_bytes >= 0x80000000u;
            if (unbounded) bd.size_bytes = 64u << 20; // "unbounded" V#: a guess, never an exact write
            if (base & 3) {
                // Bound at the dword below: the shader is specialized on base & 3 and reads its
                // dwords shifted by that many bytes (gcn BufferSpecialization::misalign); the
                // bounds limit below includes the bytes below the base.
                base &= ~3ull;
            }
            // misaligned: the binding also covers the partial dword holding the buffer's last
            // bytes (the shader reads guest dword i from bound dwords i and i+1)
            const uint32_t bound_bytes = base == bd.base_address
                                             ? bd.size_bytes
                                             : (bd.size_bytes + (uint32_t)(bd.base_address - base) + 3) & ~3u;
            if (!GuestBuffer(base, bound_bytes, bb.is_written ? GuestAccess::Write : GuestAccess::Read, &mb, &off)) {
                RLog("buf-range", "shader 0x%llx: buffer %u out of range (0x%llx+0x%x)",
                     (unsigned long long)s->code_addr, i, (unsigned long long)base, bd.size_bytes);
                mb = S.dummy;
                off = 0;
                break;
            }
            if (cap::g_pass) cap::NoteBuffer(b.stage, i, v, base, bound_bytes, bd.stride, bb.is_written);
            if (ShouldTrace()) {
                fprintf(stderr, "[gnm-trace]    buf[%u] 0x%llx size %u stride %u%s\n", i, (unsigned long long)base,
                        bd.size_bytes, bd.stride, bb.is_written ? " written" : "");
                if (bd.stride == 32 && bd.size_bytes > 4096) TraceVertexStream(bd.base_address);
                static const bool dumpbufs = getenv("PS4_GNM_TRACE_BUFS") != nullptr;
                if (dumpbufs && bd.size_bytes <= 512) {
                    const uint32_t* w = (const uint32_t*)(S.ctx->mem_base + base);
                    for (uint32_t k = 0; k < bd.size_bytes / 4; k += 8) {
                        fprintf(stderr, "[gnm-trace]      %3u:", k);
                        for (uint32_t j = k; j < k + 8 && j < bd.size_bytes / 4; ++j) fprintf(stderr, " %08x(%g)", w[j], AsF(w[j]));
                        fprintf(stderr, "\n");
                    }
                }
            }
            if (bb.buffer_slot < GCN_MAX_BUFFERS) {
                // the exact guest bytes from the bound base (the shader derives its dword limits)
                uint32_t lim = bd.size_bytes + (uint32_t)(bd.base_address - base);
                if (bounds_off) lim = 0xffffffffu;
                else if (bounds_mode && !strcmp(bounds_mode, "raw") && bd.stride != 0) lim = 64u << 20;
                pd.buf_sizes[bb.buffer_slot] = lim;
            }
            if (static const char* ww = getenv("PS4_GNM_WATCH_WRITES"); ww && bb.is_written) {
                // debug: which shaders write a buffer containing (or ending right below) addr
                static const uint64_t wa = strtoull(ww, nullptr, 16);
                static int nlog = 0;
                if (nlog < 200 && wa >= bd.base_address && wa < bd.base_address + bd.size_bytes + 0x100000) {
                    nlog++;
                    fprintf(stderr, "[gnm-watchw] submit %llu shader 0x%llx buf %u: base 0x%llx size 0x%x stride %u "
                            "nrec %u swz %d tid %d groups %u thr %u%s\n", (unsigned long long)S.submit_idx,
                            (unsigned long long)s->code_addr, i, (unsigned long long)bd.base_address, bd.size_bytes,
                            bd.stride, bd.num_records, (int)bd.swizzle_enable, (int)bd.add_tid_enable,
                            bc.num_workgroups, (*bc.regs)[0x2E07] & 0xffff, wa < bd.base_address + bd.size_bytes ? " (inside)" : " (just past end)");
                }
            }
            if (bb.is_written) {
                NoteGpuWrite(base, bd.size_bytes);
                if (!unbounded) NoteBufferStore(bd.base_address, bd.size_bytes);
                auto mit = S.meta.find(base);
                if (mit != S.meta.end() && b.stage == Stage::Compute)
                    ArmMetaClear(mit->second, bd.size_bytes, S.committed_seq + 1);
            }
            break;
        }
        case GCN_BUFFER_FLATBUF:
            for (uint32_t k = 0; k < bb.num_msl_buffers; ++k)
                b.Bytes(flat.data(), flat.size() * 4, bb.msl_buffers[k]);
            continue;
        case GCN_BUFFER_GDS:
            mb = S.gds_buf;
            break;
        case GCN_BUFFER_BDA_PAGETABLE:
            // a draw stage: map the pages earlier draws met unmapped before the table is bound
            if (b.stage != Stage::Compute && !GmDrawSlot()) {
                if (why) *why = "no guest memory fault buffer for draws";
                return false;
            }
            if (!BindGuestPageTable(b, &mb)) {
                if (why) *why = "no guest memory page table";
                return false;
            }
            break;
        case GCN_BUFFER_FAULT_BUFFER:
            if (!(mb = b.stage == Stage::Compute ? g_gmem.cur_slot : GmDrawSlot())) {
                if (why) *why = "no guest memory fault buffer";
                return false;
            }
            break;
        case GCN_BUFFER_SHARED_MEMORY: {
            // shadPS4: lds_size = SharedMemSize * NumWorkgroups, zero filled
            uint32_t lds = s->info.shared_memory_size ? s->info.shared_memory_size : 32768;
            size_t sz = (size_t)lds * std::max(1u, bc.num_workgroups);
            if (sz > (256u << 20)) {
                if (why) *why = "LDS spill buffer too large";
                return false;
            }
            void* ptr;
            size_t aoff;
            if (!S.arena.Alloc(S.dev, sz, 256, &mb, &aoff, &ptr)) return false;
            memset(ptr, 0, sz);
            off = aoff;
            break;
        }
        case GCN_BUFFER_CLIP_PLANES: {
            float planes[24];
            for (int k = 0; k < 24; ++k) planes[k] = AsF((*bc.regs)[0xA16F + k]);
            for (uint32_t k = 0; k < bb.num_msl_buffers; ++k) b.Bytes(planes, sizeof(planes), bb.msl_buffers[k]);
            continue;
        }
        default:
            mb = S.dummy;
            break;
        }
        for (uint32_t k = 0; k < bb.num_msl_buffers; ++k) b.Buffer(mb, off, bb.msl_buffers[k]);
        if (bb.msl_arg_index < argtab.size() && mb) {
            argtab[bb.msl_arg_index] = mb.gpuAddress + off;
            if (mb != S.dummy) b.Use(mb, bb.is_written);
        }
    }
    if (!argtab.empty()) b.Bytes(argtab.data(), argtab.size() * sizeof(uint64_t), (uint32_t)s->info.msl_argument_buffer);

    TableEntryCounts table_counts;
    // textures_in (the pass of every guest memory dispatch, which binds what its probe bound, and a
    // re-run): no image lookups; the table entries were counted where they were looked up (at the
    // probe, so a guest memory dispatch counts them once per probe)
    if (b.textures_in)
        for (auto& [slot, t] : *b.textures_in) b.Texture(t, slot);
    for (uint32_t i = 0; !b.textures_in && i < s->imgs.size(); ++i) {
        const gcn_image_binding& ib = s->imgs[i];
        const uint32_t nb = std::max(1u, ib.num_bindings);
        id<MTLTexture> tex = nil;
        ImageDesc img;
        const bool have = StageImage(s, flat, i, &img) && img.valid && img.address && img.address < S.ctx->mem_size;
        if (cap::g_pass && have)
            cap::NoteImage(b.stage, i, img, ib.is_storage, ib.is_depth,
                           ib.table == UINT32_MAX ? -1 : (int32_t)ib.table,
                           ib.table == UINT32_MAX ? -1 : (int32_t)ib.table_entry);
        if (have) {
            if (ib.is_depth) {
                tex = S.tex->FindDepthTarget(img.address);
                if (!tex) tex = DepthFromMemory(img, false);
                if (!tex) RLog("depth-tex", "depth texture at 0x%llx (fmt %u/%u %ux%u tile %u) not a known depth target",
                               (unsigned long long)img.address, img.raw_dfmt, img.raw_nfmt, img.width, img.height,
                               (uint32_t)img.tile_mode);
            } else if (ib.is_storage) {
                tex = S.tex->GetStorageTexture(img);
                if (tex) {
                    // storage writes go to a GPU-owned texture
                    S.storage_bound.emplace_back(img.address, tex);
                    if (cap::g_pass) cap::NoteStorage(tex);
                } else {
                    RLog("storage-tex", "storage image unsupported (fmt %u/%u %ux%u type %u)",
                         img.raw_dfmt, img.raw_nfmt, img.width, img.height, (uint32_t)img.type);
                }
            } else if (auto sz = S.stencil_to_z.find(img.address);
                       sz != S.stencil_to_z.end() && S.tex->FindDepthTarget(sz->second) &&
                       S.tex->FindDepthTarget(sz->second).pixelFormat == MTLPixelFormatDepth32Float_Stencil8) {
                // the stencil plane of a depth target, read as an 8-bit integer image
                id<MTLTexture> dt = S.tex->FindDepthTarget(sz->second);
                tex = [dt newTextureViewWithPixelFormat:MTLPixelFormatX32_Stencil8];
            } else if (id<MTLTexture> dt = S.tex->FindDepthTarget(img.address)) {
                tex = DepthCompanion(dt, false);
                if (!tex) RLog("depth-as-color", "depth surface 0x%llx sampled as color: no companion",
                               (unsigned long long)img.address);
            } else if (id<MTLTexture> mc = MipChainFor(img, false)) {
                tex = (mc.pixelFormat == (MTLPixelFormat)img.pixel.mtl_format)
                          ? [mc newTextureViewWithPixelFormat:mc.pixelFormat textureType:MTLTextureType2D
                                                       levels:NSMakeRange(0, mc.mipmapLevelCount) slices:NSMakeRange(0, 1)
                                                      swizzle:ToMTLSwizzle(img.view_swizzle)]
                          : mc;
            } else {
                tex = SampledTexture(img);
                static const bool texdbg3d = getenv("PS4_GNM_TEXDBG3D") != nullptr;
                if (texdbg3d && tex && tex.textureType == MTLTextureType3D && tex.storageMode == MTLStorageModeShared) {
                    static std::unordered_set<uint64_t>& seen3d = *new std::unordered_set<uint64_t>;
                    if (seen3d.insert(img.address).second) {
                        const size_t w = tex.width, h = tex.height, d = tex.depth;
                        std::vector<uint8_t> px(w * h * d * 4);
                        [tex getBytes:px.data() bytesPerRow:w * 4 bytesPerImage:w * h * 4
                           fromRegion:MTLRegionMake3D(0, 0, 0, w, h, d) mipmapLevel:0 slice:0];
                        uint64_t sum = 0;
                        for (uint8_t v : px) sum += v;
                        fprintf(stderr, "[gnm-draw] 3D tex 0x%llx %zux%zux%zu fmt %lu: avg byte %.2f first px %02x%02x%02x%02x last %02x%02x%02x%02x\n",
                                (unsigned long long)img.address, w, h, d, (unsigned long)tex.pixelFormat, (double)sum / px.size(),
                                px[0], px[1], px[2], px[3], px[px.size() - 4], px[px.size() - 3], px[px.size() - 2], px[px.size() - 1]);
                        const uint8_t* g = S.ctx->mem_base + img.address;
                        fprintf(stderr, "[gnm-draw]   guest bytes: %02x%02x%02x%02x %02x%02x%02x%02x ...\n", g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7]);
                    }
                }
                if (!tex) RLog("tex-fail", "texture unsupported (fmt %u/%u %ux%u type %u tile %u)",
                               img.raw_dfmt, img.raw_nfmt, img.width, img.height, (uint32_t)img.type,
                               (uint32_t)img.tile_mode);
            }
        }
        if (ShouldTrace())
            fprintf(stderr, "[gnm-trace]    %s img[%u] slot %u %s%s addr=0x%llx fmt=%u/%u %ux%u type=%u tile=%u -> %s\n",
                    b.stage == Stage::Vertex ? "VS" : b.stage == Stage::Fragment ? "PS" : "CS", i, ib.msl_texture,
                    ib.is_storage ? "storage" : "sampled", ib.is_depth ? " depth" : "", (unsigned long long)img.address,
                    img.raw_dfmt, img.raw_nfmt, img.width, img.height, (uint32_t)img.type, (uint32_t)img.tile_mode,
                    tex ? "ok" : "nil");
        if (ib.table != UINT32_MAX) NoteTableEntry(s, flat, i, tex != nil, table_counts);
        const MTLTextureType want = ShaderTextureType(ib);
        for (uint32_t k = 0; k < nb; ++k) {
            id<MTLTexture> v = nil;
            if (tex) {
                uint32_t level = 0, nlevels = 0;
                if (ib.mip_fallback_mode == 1) {
                    level = k;
                    nlevels = 1;
                } else if (ib.mip_fallback_mode == 2) {
                    level = ib.constant_mip_index;
                    nlevels = 1;
                } else if (ib.is_storage) {
                    nlevels = 1;
                }
                v = AdaptTexture(tex, want, img, level, nlevels);
            }
            b.Texture(v, ib.msl_texture + k);
        }
    }

    if (!s->tables.empty() && !b.textures_in) LogTableEntries(s, table_counts);
    for (uint32_t i = 0; i < s->samps.size(); ++i) {
        const gcn_sampler_binding& sb = s->samps[i];
        uint32_t ss[4] = {};
        if (!gcn_shader_fetch_sampler_sharp(s->sh, i, flat.data(), ss)) {
            // an S# the shader loads at run time (or an invalid one): the zero S# is bound
            char k[64];
            snprintf(k, sizeof(k), "sampler-unfetched-%llx-%u", (unsigned long long)s->code_addr, i);
            RLog(k, "shader 0x%llx sampler %u: no S# to fetch (loaded at run time or invalid); bound the zero S#",
                 (unsigned long long)s->code_addr, i);
        }
        if (cap::g_pass) cap::NoteSampler(b.stage, i, ss);
        b.Sampler(GetSampler(ss, sb.is_depth), sb.msl_sampler);
    }

    if (s->info.msl_push_constant_buffer >= 0) {
        const ps4gnm_push::Padded padded(pd);
        b.Bytes(padded.bytes, sizeof(padded.bytes), (uint32_t)s->info.msl_push_constant_buffer);
    }
    return true;
}

// Guest bytes a T# covers: the exact surface layout (as uploaded by TextureCache), or a
// conservative estimate if it cannot be computed.
uint64_t ApproxImageBytes(const ImageDesc& img);

// The image descriptors of a stage, decoded by the CP thread while preparing the draw /
// dispatch, so the encoder's hazard check, depth companions and binding do not decode them
// again. StageImage() falls back to decoding when no table of the stage is installed.
struct StageImages {
    const Shader* s = nullptr;
    const uint32_t* flat = nullptr;
    std::vector<ImageDesc> img;
    std::vector<uint8_t> ok;      // fetch and decode succeeded (img.valid is checked by the users)
    std::vector<uint64_t> approx; // ApproxImageBytes
    void Clear() {
        s = nullptr;
        flat = nullptr;
        img.clear();
        ok.clear();
        approx.clear();
    }
};
thread_local const StageImages* t_stage_images[3] = {};

void DecodeStageImages(const Shader* s, const std::vector<uint32_t>& flat, StageImages* out) {
    out->s = s;
    out->flat = flat.data();
    const size_t n = s->imgs.size();
    out->img.resize(n);
    out->ok.assign(n, 0);
    out->approx.assign(n, 0);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t t[8] = {};
        if (!gcn_shader_fetch_image_sharp(s->sh, i, flat.data(), t)) continue;
        if (!DecodeImage(t, s->imgs[i].is_r128 ? 4 : 8, &out->img[i])) continue;
        out->ok[i] = 1;
        if (out->img[i].valid) out->approx[i] = ApproxImageBytes(out->img[i]);
    }
}

bool StageImage(const Shader* s, const std::vector<uint32_t>& flat, uint32_t i, ImageDesc* img,
                uint64_t* approx) {
    for (const StageImages* p : t_stage_images) {
        if (p && p->s == s && p->flat == flat.data() && i < p->img.size()) {
            if (!p->ok[i]) return false;
            *img = p->img[i];
            if (approx) *approx = p->approx[i];
            return true;
        }
    }
    uint32_t t[8] = {};
    if (!gcn_shader_fetch_image_sharp(s->sh, i, flat.data(), t)) return false;
    if (!DecodeImage(t, s->imgs[i].is_r128 ? 4 : 8, img)) return false;
    if (approx) *approx = img->valid ? ApproxImageBytes(*img) : 0;
    return true;
}

// Installs the tables of a draw / dispatch for the encoder while it is being encoded.
struct StageImagesScope {
    StageImagesScope(const StageImages* a, const StageImages* b, const StageImages* c) {
        t_stage_images[0] = a;
        t_stage_images[1] = b;
        t_stage_images[2] = c;
    }
    ~StageImagesScope() { t_stage_images[0] = t_stage_images[1] = t_stage_images[2] = nullptr; }
};

uint64_t ApproxImageBytes(const ImageDesc& img) {
    SurfaceLayout layout;
    if (ComputeSurfaceLayout(img.ToSurfaceDesc(), &layout) && layout.total_size) return layout.total_size;
    return (uint64_t)img.pitch * std::max(1u, img.height) * std::max(1u, NumBitsPerElement(img.dfmt)) / 8 *
               std::max(1u, img.num_layers) * std::max(1u, img.depth) * 2 + 65536;
}

void BumpDepthVersion(id<MTLTexture> t) {
    if (!t) return;
    S.depth_version[(__bridge void*)t]++;
    S.depth_last_write[(__bridge void*)t] = ++S.depth_write_clock;
}

bool HasStencil(MTLPixelFormat f);
id<MTLTexture> DepthFromMemory(const ImageDesc& img, bool encode);

// Depth copy between sample counts: a fullscreen triangle writing [[depth(any)]] from sample 0 of
// a multisampled depth texture (to 1x) or from a 1x depth texture to every sample (to Nx).
id<MTLRenderPipelineState> DepthCopyPso(MTLPixelFormat fmt, NSUInteger samples, bool from_ms) {
    const uint64_t key = ((uint64_t)fmt << 16) | (samples << 1) | (from_ms ? 1 : 0);
    auto it = S.depth_copy_psos.find(key);
    if (it != S.depth_copy_psos.end()) return it->second;
    id<MTLRenderPipelineState> pso = nil;
    if (S.util_lib) {
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [S.util_lib newFunctionWithName:@"present_vs"];
        pd.fragmentFunction = [S.util_lib newFunctionWithName:from_ms ? @"depth_from_ms_fs" : @"depth_to_ms_fs"];
        pd.depthAttachmentPixelFormat = fmt;
        if (HasStencil(fmt)) pd.stencilAttachmentPixelFormat = fmt;
        pd.rasterSampleCount = samples;
        NSError* err = nil;
        pso = [S.dev newRenderPipelineStateWithDescriptor:pd error:&err];
        if (!pso) RLog("depth-copy", "depth copy pipeline failed: %s", err.localizedDescription.UTF8String);
    }
    S.depth_copy_psos[key] = pso;
    return pso;
}

// Before rendering with depth texture t: if the same depth memory was last written through a
// texture of another sample count, copy that depth into t first.
void SyncDepthSibling(const DepthTarget& db, id<MTLTexture> t) {
    id<MTLTexture> sib = S.tex->FindDepthSibling(db, t);
    if (!sib) return;
    const uint64_t sw = S.depth_last_write[(__bridge void*)sib];
    uint64_t& tw = S.depth_last_write[(__bridge void*)t];
    if (sw <= tw) return;
    const bool from_ms = sib.sampleCount > 1;
    if (from_ms == (t.sampleCount > 1)) return;
    id<MTLRenderPipelineState> pso = DepthCopyPso(t.pixelFormat, t.sampleCount, from_ms);
    if (!pso) return;
    if (!S.depth_copy_dss) {
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionAlways;
        dd.depthWriteEnabled = YES;
        S.depth_copy_dss = [S.dev newDepthStencilStateWithDescriptor:dd];
    }
    EndEncoders();
    EnsureCmd();
    GnmPassDesc pd;
    pd.depth = {t, nil, 0, GnmLoad::DontCare, GnmStore::Store};
    if (HasStencil(t.pixelFormat)) pd.stencil = {t, nil, 0, GnmLoad::Load, GnmStore::Store};
    pd.width = (uint32_t)t.width;
    pd.height = (uint32_t)t.height;
    FullscreenPass(*S.be, pd, pso, S.depth_copy_dss, sib);
    S.pending = true;
    tw = sw;
    S.depth_version[(__bridge void*)t]++;
    LogWork("depth sample-count copy");
}

// A compute shader wrote a depth surface's memory through a 32-bit float storage image (e.g. the
// PS4 Pro resolve of the 2x checkerboard depth into a 1x depth buffer): move the values into the
// depth target used for rendering.
void SyncStorageToStencil(uint64_t z_addr, id<MTLTexture> src) {
    id<MTLTexture> dt = S.tex->FindDepthTarget(z_addr);
    if (!dt || dt.sampleCount > 1 || dt.textureType != MTLTextureType2D || !HasStencil(dt.pixelFormat)) return;
    static std::unordered_map<uint32_t, id<MTLRenderPipelineState>>& psos = *new std::unordered_map<uint32_t, id<MTLRenderPipelineState>>;
    static __strong id<MTLDepthStencilState>& dss = *new __strong id<MTLDepthStencilState>(nil);
    __strong id<MTLRenderPipelineState>& pso = psos[(uint32_t)dt.pixelFormat];
    if (!pso && S.util_lib) {
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [S.util_lib newFunctionWithName:@"present_vs"];
        pd.fragmentFunction = [S.util_lib newFunctionWithName:@"stencil_from_color_fs"];
        pd.depthAttachmentPixelFormat = dt.pixelFormat;
        pd.stencilAttachmentPixelFormat = dt.pixelFormat;
        NSError* err = nil;
        pso = [S.dev newRenderPipelineStateWithDescriptor:pd error:&err];
        if (!pso) RLog("stencil-from-color", "pipeline failed: %s", err.localizedDescription.UTF8String);
    }
    if (!pso) return;
    if (!dss) {
        MTLStencilDescriptor* sd = [MTLStencilDescriptor new];
        sd.stencilCompareFunction = MTLCompareFunctionAlways;
        sd.depthStencilPassOperation = MTLStencilOperationReplace;
        sd.readMask = 0xFF;
        sd.writeMask = 0xFF;
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionAlways;
        dd.depthWriteEnabled = NO;
        dd.frontFaceStencil = sd;
        dd.backFaceStencil = sd;
        dss = [S.dev newDepthStencilStateWithDescriptor:dd];
    }
    EndEncoders();
    EnsureCmd();
    GnmPassDesc pd;
    pd.depth = {dt, nil, 0, GnmLoad::Load, GnmStore::Store};
    pd.stencil = {dt, nil, 0, GnmLoad::Load, GnmStore::Store};
    pd.width = (uint32_t)std::min(dt.width, src.width);
    pd.height = (uint32_t)std::min(dt.height, src.height);
    FullscreenPass(*S.be, pd, pso, dss, src);
    S.pending = true;
    BumpDepthVersion(dt);
    LogWork("storage -> stencil copy");
}

void SyncStorageToDepth() {
    for (auto& [addr, src] : S.storage_bound) {
        if (src.pixelFormat == MTLPixelFormatR8Uint && src.sampleCount == 1) {
            if (auto sz = S.stencil_to_z.find(addr); sz != S.stencil_to_z.end()) SyncStorageToStencil(sz->second, src);
            continue;
        }
        if (src.pixelFormat != MTLPixelFormatR32Float || src.sampleCount > 1) continue;
        id<MTLTexture> dt = S.tex->FindDepthTarget(addr);
        if (!dt || dt.sampleCount > 1 || dt.textureType != MTLTextureType2D) continue;
        __strong id<MTLRenderPipelineState>& pso = S.depth_from_color_psos[(uint32_t)dt.pixelFormat];
        if (!pso && S.util_lib) {
            MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
            pd.vertexFunction = [S.util_lib newFunctionWithName:@"present_vs"];
            pd.fragmentFunction = [S.util_lib newFunctionWithName:@"depth_from_color_fs"];
            pd.depthAttachmentPixelFormat = dt.pixelFormat;
            if (HasStencil(dt.pixelFormat)) pd.stencilAttachmentPixelFormat = dt.pixelFormat;
            NSError* err = nil;
            pso = [S.dev newRenderPipelineStateWithDescriptor:pd error:&err];
            if (!pso) RLog("depth-from-color", "pipeline failed: %s", err.localizedDescription.UTF8String);
        }
        if (!pso) continue;
        if (!S.depth_copy_dss) {
            MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
            dd.depthCompareFunction = MTLCompareFunctionAlways;
            dd.depthWriteEnabled = YES;
            S.depth_copy_dss = [S.dev newDepthStencilStateWithDescriptor:dd];
        }
        EndEncoders();
        EnsureCmd();
        GnmPassDesc pd;
        pd.depth = {dt, nil, 0, GnmLoad::Load, GnmStore::Store};
        if (HasStencil(dt.pixelFormat)) pd.stencil = {dt, nil, 0, GnmLoad::Load, GnmStore::Store};
        pd.width = (uint32_t)std::min(dt.width, src.width);
        pd.height = (uint32_t)std::min(dt.height, src.height);
        FullscreenPass(*S.be, pd, pso, S.depth_copy_dss, src);
        S.pending = true;
        BumpDepthVersion(dt);
        LogWork("storage -> depth copy");
    }
    S.storage_bound.clear();
}

// R32F copy of a depth target for shaders that sample the depth surface as a color texture.
// Encodes a conversion pass into the current command buffer when the depth changed.
id<MTLRenderPipelineState> Depth2ColorMsPso(NSUInteger samples) {
    static std::unordered_map<NSUInteger, id<MTLRenderPipelineState>>& psos = *new std::unordered_map<NSUInteger, id<MTLRenderPipelineState>>;
    auto it = psos.find(samples);
    if (it != psos.end()) return it->second;
    id<MTLRenderPipelineState> pso = nil;
    if (S.util_lib) {
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [S.util_lib newFunctionWithName:@"present_vs"];
        pd.fragmentFunction = [S.util_lib newFunctionWithName:@"depth2color_ms_fs"];
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatR32Float;
        pd.rasterSampleCount = samples;
        NSError* err = nil;
        pso = [S.dev newRenderPipelineStateWithDescriptor:pd error:&err];
        if (!pso) RLog("depth-ms", "MSAA depth companion pipeline failed: %s", err.localizedDescription.UTF8String);
    }
    psos[samples] = pso;
    return pso;
}

id<MTLTexture> DepthCompanion(id<MTLTexture> dt, bool encode) {
    if (!dt || !S.depth2color_pso) return nil;
    const bool ms = dt.sampleCount > 1;
    if (ms && dt.textureType != MTLTextureType2DMultisample) return nil;
    if (!ms && dt.textureType != MTLTextureType2D && dt.textureType != MTLTextureType2DArray) return nil;
    void* key = (__bridge void*)dt;
    auto& c = S.depth_comp[key];
    const uint64_t ver = S.depth_version[key];
    if (c.tex && c.version == ver) return c.tex;
    if (!encode) return c.tex;
    id<MTLRenderPipelineState> pso = ms ? Depth2ColorMsPso(dt.sampleCount) : S.depth2color_pso;
    if (!pso) return nil;
    if (!c.tex) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                                                                      width:dt.width
                                                                                     height:dt.height
                                                                                  mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModePrivate;
        if (ms) {
            // per-sample copy (the fragment shader reads [[sample_id]]) for texture2d_ms reads
            td.textureType = MTLTextureType2DMultisample;
            td.sampleCount = dt.sampleCount;
        }
        c.tex = [S.dev newTextureWithDescriptor:td];
        if (!c.tex) return nil;
        OwnTextureMade(c.tex);
    }
    id<MTLTexture> src = dt;
    if (!ms && dt.textureType != MTLTextureType2D)
        src = [dt newTextureViewWithPixelFormat:dt.pixelFormat textureType:MTLTextureType2D
                                         levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1)];
    EndEncoders();
    EnsureCmd();
    GnmPassDesc pd;
    pd.color[0] = {c.tex, nil, 0, GnmLoad::DontCare, GnmStore::Store};
    FullscreenPass(*S.be, pd, pso, nil, src);
    c.version = ver;
    S.pending = true;
    LogWork("depth->color companion");
    return c.tex;
}

uint64_t MipChainKey(const ImageDesc& img) {
    uint64_t k[4] = {img.address, ((uint64_t)img.width << 32) | img.height,
                     ((uint64_t)img.pixel.mtl_format << 32) | img.NumViewLevels(), (uint64_t)img.tile_mode};
    return Fnv(k, sizeof(k));
}

// The render targets a mip chain of `img` is assembled from (MipChainFor), given `base`, the
// color target at img.address (FindColorTarget): srcs[m] is the one at level m's address (nil:
// none; srcs[0] = base). Returns the number of levels, 0 if `img` is not a mipmapped 2D texture
// whose base level is a GPU render target of its size. addrs (optional, kMaxMips): level m's address.
uint32_t MipChainSources(const ImageDesc& img, id<MTLTexture> base, __strong id<MTLTexture> (&srcs)[kMaxMips],
                         uint64_t* addrs) {
    if (img.mtl_texture_type != MTLTextureType2D || img.NumViewLevels() < 2 || img.pixel.compressed) return 0;
    if (!base || base.sampleCount > 1 || base.width != img.width || base.height != img.height) return 0;
    SurfaceLayout layout;
    if (!ComputeSurfaceLayout(img.ToSurfaceDesc(), &layout)) return 0;
    const uint32_t levels = std::min<uint32_t>(std::min<uint32_t>(img.NumViewLevels(), layout.num_mips), kMaxMips);
    srcs[0] = base;
    for (uint32_t m = 1; m < levels; ++m) srcs[m] = S.tex->FindColorTarget(img.address + layout.mips[m].offset);
    if (addrs)
        for (uint32_t m = 0; m < levels; ++m) addrs[m] = img.address + layout.mips[m].offset;
    return levels;
}

// A sampled 2D texture with several mip levels whose base level is a GPU render target: the game
// rendered each level separately (bloom/downsample chains). Assemble a mipmapped copy from the
// render targets found at the mip addresses (blits encoded into the current command buffer).
id<MTLTexture> MipChainFor(const ImageDesc& img, bool encode) {
    if (img.mtl_texture_type != MTLTextureType2D || img.NumViewLevels() < 2 || img.pixel.compressed) return nil;
    id<MTLTexture> base = S.tex->FindColorTarget(img.address);
    if (!base || base.sampleCount > 1 || base.width != img.width || base.height != img.height) return nil;
    const uint64_t key = MipChainKey(img);
    auto it = S.mip_chains.find(key);
    if (!encode) return it != S.mip_chains.end() ? it->second.tex : nil;
    if (!BackendHas(kGnmCapCopies, "mip chain of render targets")) return nil;
    State::MipChain& mc = S.mip_chains[key];
    if (mc.tex && mc.built_submit == S.submit_idx + (S.n_draws << 20)) return mc.tex;
    id<MTLTexture> srcs[kMaxMips];
    uint64_t addrs[kMaxMips] = {};
    const uint32_t levels = MipChainSources(img, base, srcs, addrs);
    if (!levels) return nil;
    // the targets of the other levels are read by the blits below (level 0: ResolveTextureHazards)
    for (uint32_t m = 1; m < levels; ++m)
        if (srcs[m]) RefreshGuestNewer(addrs[m], srcs[m], false);
    if (!mc.tex || mc.tex.mipmapLevelCount != levels) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:base.pixelFormat
                                                                                      width:img.width
                                                                                     height:img.height
                                                                                  mipmapped:YES];
        td.mipmapLevelCount = levels;
        td.usage = MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
        td.storageMode = MTLStorageModePrivate;
        OwnTextureDropped(mc.tex); // the chain with another level count
        mc.tex = [S.dev newTextureWithDescriptor:td];
        if (!mc.tex) return nil;
        OwnTextureMade(mc.tex);
    }
    EndEncoders();
    EnsureCmd();
    S.be->BeginCopies();
    for (uint32_t m = 0; m < levels; ++m) {
        const uint32_t w = std::max(1u, img.width >> m), h = std::max(1u, img.height >> m);
        id<MTLTexture> src = srcs[m];
        if (!src || src.pixelFormat != base.pixelFormat || src.sampleCount > 1 || src.width < w || src.height < h) continue;
        S.be->CopyTextureRegion(src, 0, 0, MTLOriginMake(0, 0, 0), MTLSizeMake(w, h, 1), mc.tex, 0, m,
                                MTLOriginMake(0, 0, 0));
    }
    S.be->EndEncoder();
    S.pending = true;
    mc.built_submit = S.submit_idx + (S.n_draws << 20);
    LogWork("mip chain assembly");
    return mc.tex;
}

// A depth texture (sampled with depth compare) that is not a GPU depth target: 16/32-bit data
// the game loaded or wrote itself. Metal needs a depth pixel format for depth2d bindings, so
// the sampled texture from guest memory is converted into a depth texture (once per frame).
id<MTLTexture> DepthFromMemory(const ImageDesc& img, bool encode) {
    GmApplyInvalidations();
    static std::unordered_map<uint64_t, std::pair<id<MTLTexture>, uint64_t>>& cache = *new std::unordered_map<uint64_t, std::pair<id<MTLTexture>, uint64_t>>;
    const uint64_t key = img.address ^ ((uint64_t)img.width << 40) ^ ((uint64_t)img.height << 52) ^
                         ((uint64_t)img.raw_dfmt << 32);
    auto& ent = cache[key];
    if (ent.first && (ent.second == S.present_idx || !encode)) return ent.first;
    if (!encode) return nil;
    MTLPixelFormat df;
    if (img.raw_dfmt == 2) df = MTLPixelFormatDepth16Unorm;      // Format16
    else if (img.raw_dfmt == 4) df = MTLPixelFormatDepth32Float; // Format32
    else return nil;
    id<MTLTexture> src = S.tex->GetTexture(img);
    if (!src || src.textureType != MTLTextureType2D) return nil;
    if (!ent.first || ent.first.width != src.width || ent.first.height != src.height || ent.first.pixelFormat != df) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:df width:src.width
                                                                                     height:src.height mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModePrivate;
        OwnTextureDropped(ent.first); // the one of another size or format
        ent.first = [S.dev newTextureWithDescriptor:td];
        if (!ent.first) return nil;
        OwnTextureMade(ent.first);
    }
    __strong id<MTLRenderPipelineState>& pso = S.depth_from_color_psos[(uint32_t)df];
    if (!pso && S.util_lib) {
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [S.util_lib newFunctionWithName:@"present_vs"];
        pd.fragmentFunction = [S.util_lib newFunctionWithName:@"depth_from_color_fs"];
        pd.depthAttachmentPixelFormat = df;
        NSError* err = nil;
        pso = [S.dev newRenderPipelineStateWithDescriptor:pd error:&err];
        if (!pso) RLog("depth-from-color", "pipeline failed: %s", err.localizedDescription.UTF8String);
    }
    if (!pso) return nil;
    if (!S.depth_copy_dss) {
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionAlways;
        dd.depthWriteEnabled = YES;
        S.depth_copy_dss = [S.dev newDepthStencilStateWithDescriptor:dd];
    }
    EndEncoders();
    EnsureCmd();
    GnmPassDesc pd;
    pd.depth = {ent.first, nil, 0, GnmLoad::DontCare, GnmStore::Store};
    FullscreenPass(*S.be, pd, pso, S.depth_copy_dss, src);
    S.pending = true;
    ent.second = S.present_idx;
    LogWork("depth texture from memory");
    return ent.first;
}

// Before encoding a draw/dispatch: refresh R32F companions of depth targets that the shader
// samples as color textures, and depth copies of depth textures that live in guest memory.
void PrepareDepthCompanions(const Shader* s, const std::vector<uint32_t>& flat) {
    if (s->imgs.empty()) return;
    for (uint32_t i = 0; i < s->imgs.size(); ++i) {
        if (s->imgs[i].is_storage) continue;
        ImageDesc img;
        if (!StageImage(s, flat, i, &img) || !img.valid) continue;
        if (s->imgs[i].is_depth) {
            if (!S.tex->FindDepthTarget(img.address)) DepthFromMemory(img, true);
            continue;
        }
        if (id<MTLTexture> dt = S.tex->FindDepthTarget(img.address)) DepthCompanion(dt, true);
        else MipChainFor(img, true);
    }
}

// The texture-upload command buffer as the backend's current target for this scope (opened if
// needed; its work is committed right before the main command buffer, see Commit).
struct UploadTargetScope {
    GnmCmdTarget prev;
    UploadTargetScope() : prev(S.be->Target()) {
        const bool opened = !S.be->CmdOpen(GnmCmdTarget::Upload);
        if (opened) S.be->BeginCmd(GnmCmdTarget::Upload, @"texture uploads", false);
        S.be->UseTarget(GnmCmdTarget::Upload);
        if (opened) GmWaitForCommitted();
    }
    ~UploadTargetScope() { S.be->UseTarget(prev); }
    UploadTargetScope(const UploadTargetScope&) = delete;
    UploadTargetScope& operator=(const UploadTargetScope&) = delete;
};

// Encodes the copy of a texture's guest memory into up.tex on the GPU: compute detile for tiled
// surfaces, then blit. upload_target false: into the main command buffer (ends the open encoders;
// a barrier orders the detile after the writing dispatches). true: into the upload command buffer
// with its own encoders; the main encoders stay open (and Main's encoder serial is unchanged).
bool EncodeGpuUpload(const ImageDesc& img, const TextureCache::GpuUpload& up, bool upload_target) {
    return EncodeGpuUploadAt(img.address, img.ToSurfaceDesc(), up, upload_target);
}

// The copy itself: guest memory at `address` with surface `sd` (a T#'s, or the cached surface's
// own for a refresh) into up.tex.
bool EncodeGpuUploadAt(uint64_t address, const SurfaceDesc& sd, const TextureCache::GpuUpload& up,
                       bool upload_target) {
    if (!S.detile_pso || !up.tex || address >= S.ctx->mem_size) return false;
    if (!BackendHas(kGnmCapCompute | kGnmCapCopies, "GPU texture upload")) return false;
    SurfaceLayout layout;
    if (sd.bpp == 0 || (sd.bpp & 7) || !ComputeSurfaceLayout(sd, &layout) || !layout.total_size) return false;
    const uint64_t avail = std::min<uint64_t>(layout.total_size, S.ctx->mem_size - address);
    id<MTLBuffer> gbuf = nil;
    uint64_t goff = 0;
    if (!GuestBuffer(address, avail, GuestAccess::Read, &gbuf, &goff)) return false;
    const bool tiled = sd.tile_mode != TileMode::DisplayLinearAligned && sd.tile_mode != TileMode::DisplayLinearGeneral;
    const uint32_t bpe = sd.bpp / 8;
    id<MTLTexture> tex = up.tex;
    if (up.surf.bpp != sd.bpp || up.layout.total_size != layout.total_size) {
        RLog("gpu-upload", "texture 0x%llx: cached layout differs from the T#", (unsigned long long)address);
    }
    // validate every copy before encoding anything: a surface the copies cannot cover completely
    // is left to the CPU path (the caller marks the texture stale) instead of being marked
    // current with partial contents
    const bool is3d = up.tex_type == MTLTextureType3D;
    const bool is1d = up.tex_type == MTLTextureType1D || up.tex_type == MTLTextureType1DArray;
    const NSUInteger tex_slices = (tex.textureType == MTLTextureTypeCube || tex.textureType == MTLTextureTypeCubeArray)
                                      ? tex.arrayLength * 6 : tex.arrayLength;
    const uint64_t src_size = tiled ? up.layout.total_size : avail; // bytes readable from the copy source
    for (uint32_t m = 0; m < up.levels; ++m) {
        bool bad = m >= up.layout.num_mips;
        uint64_t row = 0, rows = 0, rowbytes = 0, need = 0;
        if (!bad) {
            const MipLayout& ml = up.layout.mips[m];
            const uint32_t w = std::max(1u, up.width >> m), h = is1d ? 1u : std::max(1u, up.height >> m);
            row = (uint64_t)ml.pitch * bpe;
            const uint64_t image = row * ml.height;
            rowbytes = (uint64_t)((w + up.block_width - 1) / up.block_width) * up.bytes_per_block;
            rows = (h + up.block_height - 1) / up.block_height;
            const uint32_t d = is3d ? std::max(1u, up.depth >> m) : 1u;
            const uint32_t slices = is3d ? 1u : std::min<uint32_t>(std::min(up.layers, ml.slices), (uint32_t)tex_slices);
            need = slices ? ml.offset + (uint64_t)(slices - 1) * image + (uint64_t)(d - 1) * image + (rows - 1) * row + rowbytes
                          : 0;
            bad = rowbytes > row || !rows || need > src_size;
        }
        if (bad) {
            static int warned = 0;
            if (warned++ < 16)
                fprintf(stderr, "[gnm-tex] GPU upload of 0x%llx mip %u out of bounds (fmt %u %ux%u levels %u/%u row %llu "
                                "rowbytes %llu rows %llu need %llu have %llu); CPU upload\n",
                        (unsigned long long)address, m, (unsigned)up.tex.pixelFormat, up.width, up.height, up.levels,
                        up.layout.num_mips, (unsigned long long)row, (unsigned long long)rowbytes,
                        (unsigned long long)rows, (unsigned long long)need, (unsigned long long)src_size);
            return false;
        }
    }
    id<MTLBuffer> src = gbuf;
    uint64_t src_base = goff;
    if (tiled) {
        const SurfaceDesc& u = up.surf;
        const uint32_t kf[14] = {(uint32_t)u.tile_mode, u.bpp, u.num_samples, u.width, u.height, u.depth, u.pitch,
                                 u.levels, u.layers, (uint32_t)u.is_block | ((uint32_t)u.is_pow2 << 1) |
                                 ((uint32_t)u.alt_tile << 2), u.bank_swizzle, up.levels, (uint32_t)avail,
                                 (uint32_t)(avail >> 32)};
        uint64_t key = Fnv(kf, sizeof(kf));
        id<MTLBuffer> map = nil;
        auto mit = S.detile_maps.find(key);
        if (mit != S.detile_maps.end()) {
            map = mit->second;
        } else {
            std::vector<uint32_t> m;
            if (!BuildDetileMap(up.surf, up.layout, avail, 0, up.levels, &m) || m.empty()) return false;
            if (S.detile_maps.size() >= 64) {
                for (auto& [k, b] : S.detile_maps) S.be->Evict(b, S.committed_seq + 1);
                S.detile_maps.clear();
            }
            map = [S.dev newBufferWithBytes:m.data() length:m.size() * 4 options:MTLResourceStorageModeShared];
            if (!map) return false;
            S.be->MakeResident(map, GnmResidencyClass::Builtin);
            S.detile_maps[key] = map;
        }
        // one private staging buffer per power-of-two size class, shared by all surfaces of that
        // class: every use (detile write, blit read) is ordered by Metal's hazard tracking (same
        // tracked buffer, same queue), also between the upload command buffer and the current command buffer
        uint32_t cls = 16; // 64 KiB minimum
        while (cls < 40 && (1ull << cls) < up.layout.total_size) ++cls;
        if ((1ull << cls) < up.layout.total_size) return false;
        id<MTLBuffer> staging = nil;
        auto sit = S.detile_staging.find(cls);
        if (sit != S.detile_staging.end()) {
            staging = sit->second;
        } else {
            staging = [S.dev newBufferWithLength:(NSUInteger)(1ull << cls) options:MTLResourceStorageModePrivate];
            if (!staging) return false;
            S.be->MakeResident(staging, GnmResidencyClass::Builtin);
            S.detile_staging[cls] = staging;
        }
        const uint32_t count = (uint32_t)(map.length / 4);
        const uint32_t params[4] = {bpe, count, (uint32_t)avail, 0};
        std::optional<UploadTargetScope> on_upload;
        if (upload_target) {
            on_upload.emplace();
            S.be->BeginCompute(true); // the serial dispatch type, as [cb computeCommandEncoder]
        } else {
            ComputeEncoder();
            S.be->Barrier(kGnmStageDispatch, kGnmStageDispatch, kGnmScopeBuffers); // after the writing dispatches
        }
        S.be->SetComputePipeline(S.detile_pso);
        S.be->BindBuffer(GnmStage::Compute, 0, gbuf, goff);
        S.be->BindBuffer(GnmStage::Compute, 1, staging, 0);
        S.be->BindBuffer(GnmStage::Compute, 2, map, 0);
        S.be->BindBytes(GnmStage::Compute, 3, params, sizeof(params));
        S.be->DispatchThreads(MTLSizeMake(count, 1, 1), MTLSizeMake(256, 1, 1));
        if (upload_target) S.be->EndEncoder();
        else g_cstate.Reset(S.be->EncoderSerial()); // state set behind the dispatches' back
        on_upload.reset();
        src = staging;
        src_base = 0;
    }
    std::optional<UploadTargetScope> on_upload;
    if (upload_target) {
        on_upload.emplace();
    } else {
        EndEncoders();
        EnsureCmd();
    }
    S.be->BeginCopies();
    for (uint32_t m = 0; m < up.levels; ++m) { // bounds validated above
        const MipLayout& ml = up.layout.mips[m];
        const uint32_t w = std::max(1u, up.width >> m), h = is1d ? 1u : std::max(1u, up.height >> m);
        const uint64_t row = (uint64_t)ml.pitch * bpe, image = row * ml.height;
        const uint64_t rowbytes = (uint64_t)((w + up.block_width - 1) / up.block_width) * up.bytes_per_block;
        const uint64_t rows = (h + up.block_height - 1) / up.block_height;
        const uint32_t d = is3d ? std::max(1u, up.depth >> m) : 1u;
        const uint32_t slices = is3d ? 1u : std::min<uint32_t>(std::min(up.layers, ml.slices), (uint32_t)tex_slices);
        for (uint32_t sl = 0; sl < slices; ++sl) {
            const uint64_t off = src_base + ml.offset + (uint64_t)sl * image;
            S.be->CopyBufferToTexture(src, off, row, is3d ? image : 0, MTLSizeMake(w, h, d), tex, sl, m,
                                      MTLOriginMake(0, 0, 0));
        }
    }
    S.be->EndEncoder();
    on_upload.reset();
    if (!upload_target) {
        S.pending = true;
        LogWork("GPU texture upload");
    }
    return true;
}

// Hazard path: the texture's guest memory is written by GPU work that has not completed yet,
// so the copy goes into the current command buffer behind that work. Returns false if nothing
// was encoded and the caller must fall back to draining the GPU.
bool GpuUploadTexture(const ImageDesc& img, uint64_t write_epoch) {
    if (!S.detile_pso || img.address >= S.ctx->mem_size) return false;
    // the caller drains the GPU, then uploads on the CPU (not reached with PS4_GNM_MTL4_ONLY_CLEARS:
    // draws skip ResolveTextureHazards there, dispatches are skipped)
    if (!BackendHas(kGnmCapCompute | kGnmCapCopies)) return false;
    TextureCache::GpuUpload up;
    if (!S.tex->GetTextureForGpuUpload(img, write_epoch, S.write_epoch + 1, &up)) return false;
    if (!up.tex) return true; // already current (or a GPU-owned surface)
    if (EncodeGpuUpload(img, up, false)) return true;
    S.tex->MarkStale(img.address, up.tex);
    return false;
}

const bool g_verify_gpu_upload = getenv("PS4_GNM_VERIFY_GPU_UPLOAD") != nullptr;

// Debug: compares a GPU copy with a CPU upload of the same guest memory once the GPU is done,
// over every mip level and every array/cube/depth slice the uploads write. The game rewriting
// the memory between encoding and execution also shows as a difference, so a single report at
// an address needs a second look.
void VerifyGpuUpload(const ImageDesc& img, const TextureCache::GpuUpload& up) {
    id<MTLTexture> tex = up.tex;
    MTLTextureDescriptor* td = [MTLTextureDescriptor new];
    td.textureType = tex.textureType;
    td.pixelFormat = tex.pixelFormat;
    td.width = tex.width;
    td.height = tex.height;
    td.depth = tex.depth;
    td.mipmapLevelCount = tex.mipmapLevelCount;
    td.arrayLength = tex.arrayLength;
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> ref = [S.dev newTextureWithDescriptor:td];
    if (!ref || !S.tex->DebugCpuUploadInto(img, ref)) return;
    // subresources written by both uploads (same limits as EncodeGpuUpload / UploadLockedImpl)
    const NSUInteger tex_slices = (tex.textureType == MTLTextureTypeCube || tex.textureType == MTLTextureTypeCubeArray)
                                      ? tex.arrayLength * 6 : tex.arrayLength;
    std::vector<uint32_t> slices_per_level;
    for (uint32_t m = 0; m < up.levels && m < up.layout.num_mips && m < tex.mipmapLevelCount; ++m)
        slices_per_level.push_back(up.tex_type == MTLTextureType3D
                                       ? 1u
                                       : std::min<uint32_t>(std::min(up.layers, up.layout.mips[m].slices),
                                                            (uint32_t)tex_slices));
    const uint64_t addr = img.address;
    const uint32_t bpb = img.pixel.bytes_per_block, bw = std::max(1u, img.pixel.block_width),
                   bh = std::max(1u, img.pixel.block_height);
    const uint32_t fmt = img.pixel.mtl_format, tile = (uint32_t)img.tile_mode, type = (uint32_t)up.tex_type;
    AfterGpu([tex, ref, addr, bpb, bw, bh, fmt, tile, type, slices_per_level] {
        static std::atomic<uint32_t> n_same{0}, n_diff{0};
        static std::atomic<uint64_t> n_sub{0};
        const bool is3d = tex.textureType == MTLTextureType3D;
        const bool is1d = tex.textureType == MTLTextureType1D || tex.textureType == MTLTextureType1DArray;
        uint32_t bad_subs = 0, first_m = 0, first_sl = 0, subs = 0;
        uint64_t bad_bytes = 0;
        std::vector<uint8_t> a, b;
        for (uint32_t m = 0; m < slices_per_level.size(); ++m) {
            const NSUInteger w = std::max<NSUInteger>(1, tex.width >> m), h = is1d ? 1 : std::max<NSUInteger>(1, tex.height >> m);
            const NSUInteger d = is3d ? std::max<NSUInteger>(1, tex.depth >> m) : 1;
            const NSUInteger row = (w + bw - 1) / bw * bpb, image = row * ((h + bh - 1) / bh);
            a.resize(image * d);
            b.resize(image * d);
            const MTLRegion r = MTLRegionMake3D(0, 0, 0, w, h, d);
            for (uint32_t sl = 0; sl < slices_per_level[m]; ++sl) {
                [tex getBytes:a.data() bytesPerRow:row bytesPerImage:image fromRegion:r mipmapLevel:m slice:sl];
                [ref getBytes:b.data() bytesPerRow:row bytesPerImage:image fromRegion:r mipmapLevel:m slice:sl];
                ++subs;
                if (a == b) continue;
                if (!bad_subs++) first_m = m, first_sl = sl;
                for (size_t i = 0; i < a.size(); ++i) bad_bytes += a[i] != b[i];
            }
        }
        n_sub += subs;
        if (!bad_subs) {
            n_same++;
        } else {
            n_diff++;
            fprintf(stderr, "[gnm-verify] GPU upload of 0x%llx differs from the CPU upload: fmt %u tile %u type %u "
                            "%lux%lux%lu (depth or array length) levels %zu, %u of %u subresources (first: mip %u slice %u), %llu bytes\n",
                    (unsigned long long)addr, fmt, tile, type, (unsigned long)tex.width, (unsigned long)tex.height,
                    (unsigned long)(is3d ? tex.depth : tex.arrayLength), slices_per_level.size(), bad_subs, subs,
                    first_m, first_sl, (unsigned long long)bad_bytes);
        }
        if ((n_same + n_diff) % 50 == 0)
            fprintf(stderr, "[gnm-verify] GPU uploads checked: %u equal, %u different (%llu subresources)\n",
                    n_same.load(), n_diff.load(), (unsigned long long)n_sub.load());
    });
}

// Sampled texture for BindStage. A large texture that needs an upload from guest memory is
// copied on the GPU in the upload command buffer (committed before the current one): the CPU
// detile of a big texture costs milliseconds on the encoder thread. Small textures, and copies
// that cannot be encoded, keep the CPU upload. PS4_GNM_GPU_UPLOAD_MIN_KB: threshold (default
// 256; 0: every size; -1: off). -1 turns off only this bind-time GPU path: the hazard path
// (GpuUploadTexture, for memory with GPU writes still pending) still copies on the GPU through
// the shared staging pool (S.detile_staging) with EncodeGpuUpload's up-front bounds check.
id<MTLTexture> SampledTexture(const ImageDesc& img) {
    GmApplyInvalidations();
    static const int64_t min_kb = [] {
        const char* v = getenv("PS4_GNM_GPU_UPLOAD_MIN_KB");
        return v ? (int64_t)atoll(v) : (int64_t)256;
    }();
    if (min_kb < 0 || !S.detile_pso || !BackendHas(kGnmCapCompute | kGnmCapCopies)) return S.tex->GetTexture(img);
    // the upload command buffer runs before all of the main one: the copy is current only for the
    // writes before it, so every write recorded in the main one (also earlier in this draw) still invalidates it
    TextureCache::GpuUpload up;
    id<MTLTexture> tex =
        S.tex->GetTextureForGpuRefresh(img, S.cmd_start_epoch, std::max<uint64_t>(1, (uint64_t)min_kb << 10), &up);
    if (!up.tex) return tex;
    // guest memory with GPU writes not yet executed (encoded or in flight), or overlapping a
    // GPU-owned surface: keep the CPU upload at bind time, as before the GPU path
    if (OverlapsPendingWrite(img.address, up.layout.total_size) ||
        S.tex->OverlapsGpuTexture(img.address, up.layout.total_size)) {
        S.tex->MarkStale(img.address, up.tex);
        return S.tex->GetTexture(img);
    }
    if (EncodeGpuUpload(img, up, true)) {
        g_hitch.gpu_uploads++;
        S.n_bind_uploads.fetch_add(1, std::memory_order_relaxed);
        if (g_verify_gpu_upload) VerifyGpuUpload(img, up);
        return tex;
    }
    S.tex->MarkStale(img.address, up.tex);
    return S.tex->GetTexture(img);
}

// Sampled textures are uploaded from guest memory by the CPU when bound. If encoded (not yet
// executed) GPU work writes that memory, the texture is copied on the GPU instead (or, if that
// is not possible, the GPU is drained first). Must run before encoding starts.
void ResolveTextureHazards(const Shader* s, const std::vector<uint32_t>& flat) {
    if (s->imgs.empty()) return;
    GmApplyInvalidations();
    if (S.tex->GuestNewerCount()) {
        // sampled and storage images of a GPU-owned surface that buffer stores overwrote
        for (uint32_t i = 0; i < s->imgs.size(); ++i) {
            if (s->imgs[i].is_depth) continue;
            ImageDesc img;
            if (StageImage(s, flat, i, &img) && img.valid) RefreshGuestNewer(img.address, nil, s->imgs[i].is_storage);
        }
    }
    PruneCompletedWrites();
    if (S.pending_writes.Empty()) return;
    for (uint32_t i = 0; i < s->imgs.size(); ++i) {
        if (s->imgs[i].is_storage || s->imgs[i].is_depth) continue;
        ImageDesc img;
        uint64_t approx = 0;
        if (!StageImage(s, flat, i, &img, &approx) || !img.valid) continue;
        GpuWrite hit{};
        if (OverlapsPendingWrite(img.address, approx, &hit)) {
            if (S.tex->OverlapsGpuTexture(img.address, 1)) continue; // GPU-owned: no CPU upload
            // copy the texture on the GPU behind the writing work; drain only if that fails
            if (GpuUploadTexture(img, hit.epoch)) continue;
            RLog("hazard", "texture 0x%llx (fmt %u/%u %ux%ux%u pitch %u levels %u layers %u tile %u type %u) written by pending GPU work (0x%llx+0x%llx): draining",
                 (unsigned long long)img.address, img.raw_dfmt, img.raw_nfmt, img.width, img.height, img.depth, img.pitch,
                 img.num_levels, img.num_layers, (uint32_t)img.tile_mode, (uint32_t)img.type,
                 (unsigned long long)hit.addr, (unsigned long long)hit.size);
            Commit();
            WaitAll();
            return;
        }
    }
}

template <class Regs>
gcn_push_data MakePushData(const Regs& regs) {
    // shadPS4 MakeUserData
    gcn_push_data p{};
    const uint32_t vte = regs[0xA206];
    p.xscale = (vte & 1) ? AsF(regs[0xA10F]) : 1.f;
    p.xoffset = (vte & 2) ? AsF(regs[0xA110]) : 0.f;
    p.yscale = (vte & 4) ? AsF(regs[0xA111]) : 1.f;
    p.yoffset = (vte & 8) ? AsF(regs[0xA112]) : 0.f;
    return p;
}

// =====================================================================================
// Render passes
// =====================================================================================
struct ClearReq {
    bool color[8] = {};
    MTLClearColor color_value[8];
    bool depth = false;
    double depth_value = 0;
    bool stencil = false;
    uint32_t stencil_value = 0;
    bool any() const {
        for (bool c : color)
            if (c) return true;
        return depth || stencil;
    }
};

bool HasStencil(MTLPixelFormat f) {
    return f == MTLPixelFormatDepth32Float_Stencil8 || f == MTLPixelFormatStencil8 ||
           f == MTLPixelFormatX32_Stencil8;
}

// ---- EQAA (PA_SC_AA_CONFIG multisampled rasterization into single-sample targets) ----------
// The console evaluates coverage and depth of such a pass at sample 0 of the programmed sample
// pattern. PS4 Pro checkerboard rendering depends on that being bit-exact: a 2x depth pre-pass
// is copied (sample 0) into a 1x buffer and the G-buffer pass then tests depth EQUAL against
// it. Metal cannot rasterize a single-sample pass at a custom position with the same depth
// interpolation as a multisampled one, so the pass is drawn into multisampled stand-ins of
// its targets with the same sample positions, and sample 0 is written back afterwards.

static bool IsUintFormat(MTLPixelFormat f) {
    switch (f) {
    case MTLPixelFormatR8Uint: case MTLPixelFormatRG8Uint: case MTLPixelFormatRGBA8Uint:
    case MTLPixelFormatR16Uint: case MTLPixelFormatRG16Uint: case MTLPixelFormatRGBA16Uint:
    case MTLPixelFormatR32Uint: case MTLPixelFormatRG32Uint: case MTLPixelFormatRGBA32Uint:
    case MTLPixelFormatRGB10A2Uint:
        return true;
    default:
        return false;
    }
}

static bool IsSintFormat(MTLPixelFormat f) {
    switch (f) {
    case MTLPixelFormatR8Sint: case MTLPixelFormatRG8Sint: case MTLPixelFormatRGBA8Sint:
    case MTLPixelFormatR16Sint: case MTLPixelFormatRG16Sint: case MTLPixelFormatRGBA16Sint:
    case MTLPixelFormatR32Sint: case MTLPixelFormatRG32Sint: case MTLPixelFormatRGBA32Sint:
        return true;
    default:
        return false;
    }
}

static bool IsDepthFormat(MTLPixelFormat f) {
    return f == MTLPixelFormatDepth16Unorm || f == MTLPixelFormatDepth32Float ||
           f == MTLPixelFormatDepth32Float_Stencil8;
}

id<MTLRenderPipelineState> DepthCopyPso(MTLPixelFormat fmt, NSUInteger samples, bool from_ms);

// Multisampled stand-in of a single-sample color or depth target.
id<MTLTexture> EqaaTwin(id<MTLTexture> t, uint32_t samples) {
    auto it = S.eqaa_twin.find((__bridge void*)t);
    if (it != S.eqaa_twin.end() && it->second.sampleCount == samples) return it->second;
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:t.pixelFormat width:t.width
                                                                                 height:t.height mipmapped:NO];
    td.textureType = MTLTextureType2DMultisample;
    td.sampleCount = samples;
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> twin = [S.dev newTextureWithDescriptor:td];
    if (!twin) return nil;
    OwnTextureMade(twin);
    if (it != S.eqaa_twin.end()) {
        OwnTextureDropped(it->second); // the stand-in with another sample count
        S.eqaa_orig.erase((__bridge void*)it->second);
    }
    S.eqaa_twin[(__bridge void*)t] = twin;
    S.eqaa_orig[(__bridge void*)twin] = t;
    return twin;
}

// Copies src into dst where one of them is multisampled: every sample from the single-sample
// texture, or sample 0 of the multisampled one.
void EqaaCopy(id<MTLTexture> src, id<MTLTexture> dst) {
    const bool to_ms = dst.sampleCount > 1;
    const MTLPixelFormat f = dst.pixelFormat;
    id<MTLRenderPipelineState> pso = nil;
    if (IsDepthFormat(f)) {
        pso = DepthCopyPso(f, dst.sampleCount, !to_ms);
    } else {
        const int kind = IsUintFormat(f) ? 1 : IsSintFormat(f) ? 2 : 0;
        const uint64_t key = ((uint64_t)f << 8) | (dst.sampleCount << 3) | (to_ms ? 4 : 0) | kind;
        __strong id<MTLRenderPipelineState>& p = S.eqaa_psos[key];
        if (!p && S.util_lib) {
            static const char* names[2][3] = {{"eqaa_resolve_f", "eqaa_resolve_u", "eqaa_resolve_i"},
                                              {"eqaa_spread_f", "eqaa_spread_u", "eqaa_spread_i"}};
            MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
            pd.vertexFunction = [S.util_lib newFunctionWithName:@"present_vs"];
            pd.fragmentFunction = [S.util_lib newFunctionWithName:[NSString stringWithUTF8String:names[to_ms][kind]]];
            pd.colorAttachments[0].pixelFormat = f;
            pd.rasterSampleCount = dst.sampleCount;
            NSError* err = nil;
            p = [S.dev newRenderPipelineStateWithDescriptor:pd error:&err];
            if (!p) RLog("eqaa-pso", "EQAA copy pipeline failed: %s", err.localizedDescription.UTF8String);
        }
        pso = p;
    }
    if (!pso) return;
    EnsureCmd();
    GnmPassDesc pd;
    if (IsDepthFormat(f)) {
        if (!S.depth_copy_dss) {
            MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
            dd.depthCompareFunction = MTLCompareFunctionAlways;
            dd.depthWriteEnabled = YES;
            S.depth_copy_dss = [S.dev newDepthStencilStateWithDescriptor:dd];
        }
        pd.depth = {dst, nil, 0, GnmLoad::DontCare, GnmStore::Store};
        if (HasStencil(f)) pd.stencil = {dst, nil, 0, GnmLoad::Load, GnmStore::Store};
    } else {
        pd.color[0] = {dst, nil, 0, GnmLoad::DontCare, GnmStore::Store};
    }
    FullscreenPass(*S.be, pd, pso, IsDepthFormat(f) ? S.depth_copy_dss : nil, src);
    S.pending = true;
}

// Writes sample 0 of the stand-ins of the render pass that just ended back to their targets.
void ResolveEqaaPass() {
    if (S.eqaa_open.empty()) return;
    auto open = std::move(S.eqaa_open);
    S.eqaa_open.clear();
    for (auto& [twin, target] : open) {
        EqaaCopy(twin, target);
        if (IsDepthFormat(target.pixelFormat)) BumpDepthVersion(target);
    }
    LogWork("EQAA write-back");
}

bool BeginPass(const PassKey& key, const ClearReq& clr, id<MTLTexture> const* hold) {
    if (g_gms.chain_open) GmCloseChain();
    const GnmEncoder open = S.be->OpenEncoder();
    if (open == GnmEncoder::Compute) S.be->EndEncoder();
    if (open == GnmEncoder::Render && S.pass == key && !clr.any()) return true;
    if (open == GnmEncoder::Render) {
        S.be->EndEncoder();
        ResolveEqaaPass();
    }
    // EQAA stand-ins start with the contents of their targets (unless cleared by this pass)
    for (int i = 0; i < 9; ++i) {
        id<MTLTexture> t = i < 8 ? key.color[i] : key.depth;
        if (!t) continue;
        auto it = S.eqaa_orig.find((__bridge void*)t);
        if (it == S.eqaa_orig.end()) continue;
        const bool cleared = i < 8 ? clr.color[i] : clr.depth;
        if (!cleared) EqaaCopy(it->second, t);
        S.eqaa_open.emplace_back(t, it->second);
    }
    EnsureCmd();
    GnmPassDesc pd;
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        if (!key.color[i]) continue;
        any = true;
        pd.color[i].tex = key.color[i];
        pd.color[i].load = clr.color[i] ? GnmLoad::Clear : GnmLoad::Load;
        if (clr.color[i]) pd.clear_color[i] = clr.color_value[i];
    }
    if (key.depth) {
        any = true;
        pd.depth.tex = key.depth;
        pd.depth.slice = key.depth_slice;
        pd.depth.load = clr.depth ? GnmLoad::Clear : GnmLoad::Load;
        pd.clear_depth = std::min(1.0, std::max(0.0, clr.depth_value));
        if (HasStencil(key.depth.pixelFormat)) {
            pd.stencil.tex = key.depth;
            pd.stencil.slice = key.depth_slice;
            pd.stencil.load = clr.stencil ? GnmLoad::Clear : GnmLoad::Load;
            pd.clear_stencil = clr.stencil_value;
        }
    }
    if (!any) {
        pd.width = std::max(1u, key.width);
        pd.height = std::max(1u, key.height);
        pd.default_samples = 1;
    }
    // Programmable sample locations (PA_SC_AA_SAMPLE_LOCS): e.g. PS4 Pro checkerboard rendering
    // places the two samples of a 2x target side by side, which later passes rely on.
    if (key.sample_locs && S.dev.programmableSamplePositionsSupported) {
        id<MTLTexture> t = key.depth;
        for (int i = 0; !t && i < 8; ++i) t = key.color[i];
        const NSUInteger n = t ? t.sampleCount : 1;
        if (n > 1 && n <= 4) {
            for (NSUInteger i = 0; i < n; ++i) {
                const uint32_t b = (key.sample_locs >> (8 * i)) & 0xFF;
                const int sx = (int)((b & 0xF) ^ 8) - 8, sy = (int)(((b >> 4) & 0xF) ^ 8) - 8;
                pd.sample_positions[i] =
                    MTLSamplePositionMake(std::min(15, 8 + sx) / 16.0f, std::min(15, 8 + sy) / 16.0f);
            }
            pd.num_sample_positions = (uint32_t)n;
        }
    }
    static const bool labels = getenv("PS4_GNM_LABELS") != nullptr;
    NSString* label = nil; // pd.label is unretained: this keeps it alive through BeginRender
    if (labels) {
        // profiling aid: name passes after their targets (shown by Metal System Trace)
        id<MTLTexture> t0 = key.color[0] ? key.color[0] : key.depth;
        int ncol = 0;
        for (int i = 0; i < 8; ++i) ncol += key.color[i] != nil;
        label = [NSString stringWithFormat:@"pass %llu: %d color%s %lux%lu fmt %lu depth %s%s",
                                           (unsigned long long)S.n_passes, ncol, ncol == 1 ? "" : "s",
                                           (unsigned long)(t0 ? t0.width : key.width),
                                           (unsigned long)(t0 ? t0.height : key.height),
                                           (unsigned long)(key.color[0] ? key.color[0].pixelFormat : 0),
                                           key.depth ? (clr.depth ? "clear" : "load") : "none",
                                           t0 && t0.sampleCount > 1 ? " msaa" : ""];
    } else if (EncoderStatusEnabled()) {
        label = WorkLabel("render");
    }
    pd.label = label;
    if (!S.be->BeginRender(pd)) return false;
    g_rstate.Reset(S.be->EncoderSerial());
    S.n_passes++;
    if (key.depth && !clr.depth) S.n_depth_loads++;
    S.pass = key;
    for (int i = 0; i < 9; ++i) S.pass_hold[i] = hold ? hold[i] : nil;
    return true;
}

void ComputeEncoder() {
    if (g_gms.chain_open && !g_gms.chain_member) GmCloseChain();
    if (S.be->OpenEncoder() == GnmEncoder::Render) {
        S.be->EndEncoder();
        ResolveEqaaPass(); // EQAA stand-ins write sample 0 back before compute reads the targets
        S.pass = PassKey{};
        for (auto& t : S.pass_hold) t = nil;
    }
    EnsureCmd();
    if (S.be->OpenEncoder() != GnmEncoder::Compute) {
        static const bool serial = getenv("PS4_GNM_SERIAL_COMPUTE") != nullptr;
        S.be->BeginCompute(serial);
        if (EncoderStatusEnabled()) S.be->SetEncoderLabel(WorkLabel("compute"));
        g_cstate.Reset(S.be->EncoderSerial());
        S.n_cencs++;
        S.cencs_total++;
    }
}


using ps4gnm_clear::ClearColorFor; // raw clear words -> MTLClearColor (ps4_gnm_clear_color.h)

void RegisterMeta(uint64_t addr) {
    if (addr) S.meta.emplace(addr, MetaState{});
}

// seq (optional): the command buffer the clear belongs to. slice / slice_bytes: the draw's
// slice of an array whose metadata holds slice_bytes per slice (0: not known, one slice).
// The clear covers cleared_bytes / slice_bytes slices from the base. That count does not depend on
// the Metal texture's current array length (which grows only as later slices are drawn) nor on
// the draw's DB_DEPTH_VIEW (a slice-0 view says nothing about slices 1..n): the slice-0 draw of a
// frame whose array has not grown yet must leave the clear pending for slices 1..n. Each covered
// slice takes the clear once; it stays pending until all of them have (as the HTILE of a slice
// no draw touched stays cleared on the GPU).
bool TakeMetaCleared(uint64_t addr, uint64_t* seq = nullptr, uint32_t slice = 0, uint64_t slice_bytes = 0) {
    auto it = S.meta.find(addr);
    if (it == S.meta.end() || !it->second.cleared) return false;
    MetaState& m = it->second;
    uint64_t covered = 1;
    if (slice_bytes)
        covered = std::max<uint64_t>(1, std::min<uint64_t>(64, m.cleared_bytes == kAllMeta ? 64 : m.cleared_bytes / slice_bytes));
    if (slice >= covered || (m.taken_slices >> slice & 1)) return false;
    m.taken_slices |= 1ull << slice;
    if ((uint64_t)__builtin_popcountll(m.taken_slices) >= covered) m.cleared = false;
    if (cap::g_pass) { // replay re-arms the fast clear for this slice
        cap::g_pass->meta_cleared.push_back(addr);
        cap::g_pass->meta_cleared_slices.push_back(slice);
    }
    if (seq) *seq = it->second.cleared_seq;
    return true;
}

// The fast clears a draw took (TakeMetaCleared) before its render pass began: a draw skipped
// before BeginPass encoded them (a pipeline that failed, an empty scissor, a table hazard, ...)
// gives them back, so the slice's next draw still clears. Once the pass began they are in its
// load actions (a later BindStage failure skips only the draw, not the clear): Keep().
struct MetaTakeUndo {
    struct Taken {
        uint64_t addr, seq;
        uint32_t slice;
    };
    Taken taken[9];
    int n = 0;
    void Add(uint64_t addr, uint32_t slice, uint64_t seq) {
        if (n < 9) taken[n++] = {addr, seq, slice};
    }
    void Keep() { n = 0; }
    ~MetaTakeUndo() {
        for (int i = 0; i < n; ++i) {
            auto it = S.meta.find(taken[i].addr);
            // not when another clear re-armed it since (ArmMetaClear: a new seq)
            if (it == S.meta.end() || it->second.cleared_seq != taken[i].seq) continue;
            it->second.taken_slices &= ~(1ull << taken[i].slice);
            it->second.cleared = true;
        }
    }
    MetaTakeUndo() = default;
    MetaTakeUndo(const MetaTakeUndo&) = delete;
    MetaTakeUndo& operator=(const MetaTakeUndo&) = delete;
};

// HTILE bytes per slice of a depth target (0: not known exactly, a clear then covers one slice, as
// before; logged once)
uint64_t HtileSliceBytesOf(const DepthTarget& db) {
    static const bool neo = sceKernelIsNeoMode() != 0;
    const uint64_t b = DepthHtileSliceBytes(db, neo);
    static std::atomic<bool> logged{false};
    if (!b && !logged.exchange(true))
        RLog(neo ? "htile-neo" : "htile-pipes",
             "HTILE slice size not known (%s, tile mode %u): a metadata clear of a depth array covers its first "
             "slice only", neo ? "Neo depth tiling" : "pipe config", (uint32_t)db.tile_mode);
    return b;
}

// =====================================================================================
// Pipelines
// =====================================================================================
struct VertexAttr {
    uint32_t format, offset, buffer, stride, step_fn, step_rate;
};

// Puts the shader libraries of a pipeline that was just built into the library store, unless
// they are there already (any thread).
void StoreLibraries(const ps4gnm_lib_store::Key* vs, const ps4gnm_lib_store::Key* ps, MTLRenderPipelineDescriptor* d) {
    if (!ps4gnm_lib_store::Enabled()) return;
    if (vs && ps4gnm_lib_store::Has(*vs)) vs = nullptr;
    if (ps && ps4gnm_lib_store::Has(*ps)) ps = nullptr;
    if (vs || ps) ps4gnm_lib_store::StoreRender(vs, ps, d);
}

// Appends a pipeline hash to the list of pipelines built so far (any thread).
void RememberPipeline(uint64_t stable) {
    static std::mutex& mu = *new std::mutex;
    std::lock_guard<std::mutex> lk(mu);
    if (S.pso_list_path.empty()) return;
    if (FILE* f = fopen(S.pso_list_path.c_str(), "ab")) {
        fwrite(&stable, sizeof(stable), 1, f);
        fclose(f);
    }
}

// True if the last GetGfxPipeline call returned nil because the pipeline builds in the
// background (a nil for a pipeline whose build failed is a lost draw). Encoder thread.
bool g_gfx_pipe_building = false;
bool PipelineCompiling() { return g_gfx_pipe_building; }

id<MTLRenderPipelineState> GetGfxPipeline(const Shader* vs, const Shader* ps, const RenderState& rs,
                                          const uint32_t color_fmt[8], const uint32_t write_mask[8],
                                          uint32_t depth_fmt, uint32_t samples,
                                          const std::vector<VertexAttr>& attrs,
                                          const std::vector<uint32_t>& attr_locs, bool alpha_to_cov) {
    // only the encoder looks pipelines up (S.gfx_pipes is not locked either)
    g_gfx_pipe_building = false;
    static std::string& key = *new std::string; // reused: no allocation per draw
    key.clear();
    auto put = [&](const void* p, size_t n) { key.append((const char*)p, n); };
    put(&vs->uid, 8);
    uint64_t psu = ps ? ps->uid : 0;
    put(&psu, 8);
    for (int i = 0; i < 8; ++i) {
        put(&color_fmt[i], 4);
        if (!color_fmt[i]) continue;
        put(&write_mask[i], 4);
        const BlendState& b = rs.blend[i];
        uint32_t bl[7] = {b.enable, b.src_rgb, b.dst_rgb, b.op_rgb, b.src_alpha, b.dst_alpha, b.op_alpha};
        put(bl, sizeof(bl));
    }
    put(&depth_fmt, 4);
    put(&samples, 4);
    uint32_t a2c = alpha_to_cov, rd = rs.raster.rasterizer_discard;
    put(&a2c, 4);
    put(&rd, 4);
    for (size_t i = 0; i < attrs.size(); ++i) {
        put(&attrs[i], sizeof(VertexAttr));
        put(&attr_locs[i], 4);
    }
    // consecutive draws mostly use the same pipeline
    static std::string& last_key = *new std::string;
    static __strong id<MTLRenderPipelineState>& last_pso = *new __strong id<MTLRenderPipelineState>(nil);
    if (last_pso && key == last_key) return last_pso;
    auto it = S.gfx_pipes.find(key);
    if (it != S.gfx_pipes.end()) {
        if (it->second) {
            last_key = key;
            last_pso = it->second;
        }
        return it->second;
    }
    // the same key with the shaders' MSL hashes instead of their session ids
    std::string stable_key = key;
    memcpy(&stable_key[0], &vs->msl_hash, 8);
    const uint64_t ps_mh = ps ? ps->msl_hash : 0;
    memcpy(&stable_key[8], &ps_mh, 8);
    const uint64_t stable = Fnv(stable_key.data(), stable_key.size());
    const bool known = S.known_psos.count(stable) != 0;

    MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = PipelineFn(vs);
    d.fragmentFunction = ps ? PipelineFn(ps) : nil;
    for (int i = 0; i < 8; ++i) {
        if (!color_fmt[i]) continue;
        MTLRenderPipelineColorAttachmentDescriptor* ca = d.colorAttachments[i];
        BlendState b = rs.blend[i];
        if (b.unsupported) b.enable = false;
        ApplyBlend(b, color_fmt[i], ca);
        ca.writeMask = (MTLColorWriteMask)write_mask[i];
        // integer formats cannot blend
        if (ca.pixelFormat == MTLPixelFormatR32Uint || ca.pixelFormat == MTLPixelFormatR32Sint ||
            ca.pixelFormat == MTLPixelFormatRG32Uint || ca.pixelFormat == MTLPixelFormatRGBA32Uint ||
            ca.pixelFormat == MTLPixelFormatR16Uint || ca.pixelFormat == MTLPixelFormatRGBA16Uint ||
            ca.pixelFormat == MTLPixelFormatR8Uint || ca.pixelFormat == MTLPixelFormatRGBA8Uint ||
            ca.pixelFormat == MTLPixelFormatRG16Uint || ca.pixelFormat == MTLPixelFormatRG8Uint)
            ca.blendingEnabled = NO;
    }
    if (depth_fmt) {
        d.depthAttachmentPixelFormat = (MTLPixelFormat)depth_fmt;
        if (HasStencil((MTLPixelFormat)depth_fmt)) d.stencilAttachmentPixelFormat = (MTLPixelFormat)depth_fmt;
    }
    d.rasterSampleCount = samples;
    d.alphaToCoverageEnabled = alpha_to_cov;
    d.rasterizationEnabled = !rs.raster.rasterizer_discard;
    if (!attrs.empty()) {
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        for (size_t i = 0; i < attrs.size(); ++i) {
            const VertexAttr& a = attrs[i];
            const uint32_t loc = attr_locs[i];
            vd.attributes[loc].format = (MTLVertexFormat)a.format;
            vd.attributes[loc].offset = a.offset;
            vd.attributes[loc].bufferIndex = a.buffer;
            ps4gnm_vertex::SetLayout(vd.layouts[a.buffer], a.stride, (MTLVertexStepFunction)a.step_fn, a.step_rate);
        }
        d.vertexDescriptor = vd;
    }
    ps4gnm_pso_archive::Attach(d);
    if (S.async_compile && !known) {
        // Build a new pipeline in the background; the draw is skipped until it is ready.
        if (S.psos_pending.insert(key).second) {
            const std::string k = key;
            const ps4gnm_lib_store::Key vk = vs->lib_key, pk = ps ? ps->lib_key : ps4gnm_lib_store::Key{};
            const bool vstore = vs->lib_stage != 0, pstore = ps && ps->lib_stage != 0;
            const Shader* rvs = UsesStoredFn(vs) ? vs : nullptr;
            const Shader* rps = UsesStoredFn(ps) ? ps : nullptr;
            [S.dev newRenderPipelineStateWithDescriptor:d
                                      completionHandler:^(id<MTLRenderPipelineState> ps_, NSError* err) {
                                          if (ps_) {
                                              RememberPipeline(stable);
                                              ps4gnm_pso_archive::Add(stable, d);
                                              StoreLibraries(vstore ? &vk : nullptr, pstore ? &pk : nullptr, d);
                                          }
                                          std::lock_guard<std::mutex> lk(S.async_mu);
                                          S.async_psos_done.push_back(
                                              {k, ps_, ps_ ? nil : err.localizedDescription, ps_ ? nullptr : rvs, ps_ ? nullptr : rps});
                                          S.async_psos_ready.fetch_add(1, std::memory_order_release);
                                      }];
            g_hitch.psos++;
            g_compile.pso_async++;
        }
        g_gfx_pipe_building = true;
        return nil;
    }
    NSError* e = nil;
    const double pso_t0 = CFAbsoluteTimeGetCurrent();
    id<MTLRenderPipelineState> p = [S.dev newRenderPipelineStateWithDescriptor:d error:&e];
    if (!p) {
        // built with a stored function: compile the MSL of the stored shader(s) and retry once
        if (ReplaceStoredFns(vs, ps)) {
            d.vertexFunction = PipelineFn(vs);
            d.fragmentFunction = ps ? PipelineFn(ps) : nil;
            e = nil;
            p = [S.dev newRenderPipelineStateWithDescriptor:d error:&e];
        }
    }
    g_hitch.pso_ms += (CFAbsoluteTimeGetCurrent() - pso_t0) * 1000.0;
    g_hitch.psos++;
    g_compile.pso_sync.Add((CFAbsoluteTimeGetCurrent() - pso_t0) * 1000.0);
    if (p && !known) RememberPipeline(stable);
    // known pipelines missing from the archive are added once (the next session loads them fast)
    if (p && !ps4gnm_pso_archive::Contains(stable)) ps4gnm_pso_archive::Add(stable, d);
    if (p) StoreLibraries(vs->lib_stage ? &vs->lib_key : nullptr, ps && ps->lib_stage ? &ps->lib_key : nullptr, d);
    if (!p) {
        RLog("pso-fail", "render pipeline (VS 0x%llx PS 0x%llx) failed: %s",
             (unsigned long long)vs->code_addr, (unsigned long long)(ps ? ps->code_addr : 0),
             e.localizedDescription.UTF8String);
    }
    S.gfx_pipes[key] = p;
    return p;
}

id<MTLDepthStencilState> GetDepthStencil(const DepthStencilState& ds, bool has_depth, bool has_stencil) {
    DepthStencilState k = ds;
    if (!has_depth) {
        k.depth_test = false;
        k.depth_write = false;
    }
    if (!has_stencil) k.stencil_test = false;
    uint32_t raw[16] = {k.depth_test, k.depth_write, k.depth_compare, k.stencil_test,
                        k.front.compare, k.front.fail, k.front.depth_fail, k.front.pass,
                        (uint32_t)k.front.read_mask | ((uint32_t)k.front.write_mask << 8),
                        k.back.compare, k.back.fail, k.back.depth_fail, k.back.pass,
                        (uint32_t)k.back.read_mask | ((uint32_t)k.back.write_mask << 8), 0, 0};
    const uint64_t h = Fnv(raw, sizeof(raw));
    auto it = S.ds_states.find(h);
    if (it != S.ds_states.end()) return it->second;
    id<MTLDepthStencilState> st = [S.dev newDepthStencilStateWithDescriptor:MakeDepthStencilDescriptor(k)];
    S.ds_states[h] = st;
    return st;
}

// =====================================================================================
// Primitive emulation (quad lists -> triangle lists), cf. shadPS4 liverpool_to_vk.cpp
// =====================================================================================
bool BuildQuadListIndices(const DrawArgs& a, uint32_t count, __strong id<MTLBuffer>* out, size_t* off,
                          uint32_t* out_count) {
    const uint32_t quads = count / 4;
    if (quads == 0) return false;
    if (count > (8u << 20)) {
        RLog("quad-count", "quad list with %u vertices rejected", count);
        return false;
    }
    const uint32_t n = quads * 6;
    void* ptr;
    if (!S.arena.Alloc(S.dev, (size_t)n * 4, 16, out, off, &ptr)) return false;
    uint32_t* dst = (uint32_t*)ptr;
    const bool i32 = a.index_type == 1;
    const uint8_t* src = nullptr;
    if (a.indexed) {
        const uint64_t isz = i32 ? 4 : 2;
        const uint64_t addr = a.index_base + (uint64_t)a.index_offset * isz;
        if (!a.index_base || addr >= S.ctx->mem_size || (uint64_t)count * isz > S.ctx->mem_size - addr)
            return false;
        src = S.ctx->mem_base + addr;
    }
    auto idx = [&](uint32_t i) -> uint32_t {
        if (!src) return i;
        return i32 ? ((const uint32_t*)src)[i] : ((const uint16_t*)src)[i];
    };
    for (uint32_t q = 0; q < quads; ++q) {
        const uint32_t v0 = idx(q * 4 + 0), v1 = idx(q * 4 + 1), v2 = idx(q * 4 + 2), v3 = idx(q * 4 + 3);
        dst[q * 6 + 0] = v0;
        dst[q * 6 + 1] = v1;
        dst[q * 6 + 2] = v2;
        dst[q * 6 + 3] = v0;
        dst[q * 6 + 4] = v2;
        dst[q * 6 + 5] = v3;
    }
    *out_count = n;
    return true;
}

// =====================================================================================
// Fast clear elimination / resolve passes (shadPS4 Rasterizer::FilterDraw)
// =====================================================================================
// A clear writes every byte of the cleared slices of a color target: an exactly known write
// (TextureCache::NoteExactWrite). Only the first slice of the view (base_slice) is credited, which
// is always a subset of what the PS4 clears.
// seq: the command buffer of the CMASK clear, which can precede the draw that resolves it.
void NoteClearedColor(const ColorTarget& c, id<MTLTexture> t, uint64_t seq) {
    if (!c.slice_bytes || !seq) return;
    const uint64_t lo = c.address + (uint64_t)c.base_slice * c.slice_bytes;
    S.tex->NoteExactWrite(c.address, t, lo, lo + c.slice_bytes, seq);
}

void EliminateFastClear(const RenderState& rs) {
    const ColorTarget& c = rs.color[0];
    if (!c.bound || !c.fast_clear) return;
    uint64_t clear_seq = 0;
    if (!TakeMetaCleared(c.cmask_address, &clear_seq)) return;
    id<MTLTexture> t = S.tex->GetColorTarget(c);
    if (!t) return;
    RefreshGuestNewer(c.address, t, true); // the clear of a slice keeps the rest
    ClearReq clr;
    if (!ClearColorFor(t.pixelFormat, c.clear_word0, c.clear_word1, &clr.color_value[0])) {
        RLog("clear-fmt", "fast clear: unsupported format %lu", (unsigned long)t.pixelFormat);
        return;
    }
    clr.color[0] = true;
    NoteClearedColor(c, t, clear_seq);
    PassKey k;
    k.color[0] = t;
    id<MTLTexture> hold[9] = {t};
    BeginPass(k, clr, hold);
    EndEncoders();
    S.pending = true;
    LogWork("fast-clear-elim");
}

void ResolvePass(const RenderState& rs) {
    const ColorTarget& src = rs.color[0];
    const ColorTarget& dst = rs.color[1];
    if (!src.bound || !dst.bound) return;
    id<MTLTexture> s = S.tex->GetColorTarget(src);
    id<MTLTexture> d = S.tex->GetColorTarget(dst);
    if (!s || !d) return;
    RefreshGuestNewer(src.address, s, false);
    RefreshGuestNewer(dst.address, d, true);
    EndEncoders();
    EnsureCmd();
    if (s.sampleCount > 1 && d.sampleCount == 1 && s.pixelFormat == d.pixelFormat) {
        GnmPassDesc pd;
        pd.color[0] = {s, d, 0, GnmLoad::Load, GnmStore::MultisampleResolve};
        if (S.be->BeginRender(pd)) S.be->EndEncoder();
    } else if (s.sampleCount == d.sampleCount && s.pixelFormat == d.pixelFormat) {
        if (!BackendHas(kGnmCapCopies, "resolve by copy")) return;
        S.be->BeginCopies();
        S.be->CopyTextureRegion(s, 0, 0, MTLOriginMake(0, 0, 0),
                                MTLSizeMake(std::min(s.width, d.width), std::min(s.height, d.height), 1), d, 0, 0,
                                MTLOriginMake(0, 0, 0));
        S.be->EndEncoder();
    } else {
        RLog("resolve", "resolve between incompatible targets skipped");
        return;
    }
    S.pending = true;
    LogWork("resolve");
}

bool WriteTexturePng(id<MTLTexture> t, const char* path) {
    std::vector<uint8_t> px;
    if (!S.tex->ReadTextureBGRA8(t, DataFormat::FormatInvalid, NumberFormat::Unorm, CompMapping{}, &px)) return false;
    const size_t w = t.width, h = t.height;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef bc = CGBitmapContextCreate(px.data(), w, h, 8, w * 4, cs, kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = bc ? CGBitmapContextCreateImage(bc) : NULL;
    bool ok = false;
    if (img) {
        CGImageDestinationRef dst = CGImageDestinationCreateWithURL(
            (__bridge CFURLRef)[NSURL fileURLWithPath:[NSString stringWithUTF8String:path]], CFSTR("public.png"), 1, NULL);
        if (dst) {
            CGImageDestinationAddImage(dst, img, NULL);
            ok = CGImageDestinationFinalize(dst);
            CFRelease(dst);
        }
        CGImageRelease(img);
    }
    if (bc) CGContextRelease(bc);
    CGColorSpaceRelease(cs);
    return ok;
}

// PS4_GNM_DUMP_DRAWS=<submit>: after every draw/dispatch of that submit, write the bound
// color target 0 (draws) as PNG (slow; debugging).
// Present whose draws / render targets were requested at runtime (PS4_PAD_CTL "dumpdraws" /
// "dumprt" debug commands, see g_ps4_gnm_dump_request), UINT64_MAX = none.
static uint64_t s_req_draws_present = UINT64_MAX, s_req_rt_present = UINT64_MAX;

void DumpAfterWork(id<MTLTexture> rt, const char* what) {
    static const char* env = getenv("PS4_GNM_DUMP_DRAWS");
    static const char* dir = getenv("PS4_SCREENSHOT_DIR");
    if (!dir || !rt) return;
    const bool requested = S.present_idx == s_req_draws_present;
    if (!env && !requested) return;
    if (!requested) {
        const bool by_present = env[0] == 'p';
        const uint64_t first = strtoull(env + (by_present ? 1 : 0), nullptr, 0);
        const char* c = strchr(env, ':');
        const uint64_t count = c ? strtoull(c + 1, nullptr, 0) : 1;
        const uint64_t idx = by_present ? S.present_idx : S.submit_idx;
        if (idx < first || idx >= first + count) return;
    }
    static int n = 0;
    Commit();
    WaitAll();
    char path[1024];
    snprintf(path, sizeof(path), "%s/draw_%llu_%03d_%s.png", dir, (unsigned long long)S.submit_idx, n++, what);
    if (!WriteTexturePng(rt, path)) fprintf(stderr, "[gnm-draw] dump draw: cannot write %s\n", path);
}

// PS4_GNM_DUMP_RT=<present index>: write every GPU color target as PNG (debugging)
// traced indirect draws: argument address and a label, printed with their final arguments
// when the targets are dumped (after the GPU finished the frame)
static std::vector<std::pair<uint64_t, std::string>> s_traced_indirect;
// vertex-like streams (stride 32) bound by traced work; their first vertices are printed with
// the dumped targets (PS4_GNM_DUMP_RT after the traced present)
static std::set<uint64_t> s_traced_streams;
void TraceVertexStream(uint64_t addr) { s_traced_streams.insert(addr); }

void DumpTargets(uint64_t present_idx) {
    static const char* env = getenv("PS4_GNM_DUMP_RT");
    static const char* dir = getenv("PS4_SCREENSHOT_DIR");
    if (const int req = g_ps4_gnm_dump_request) {
        g_ps4_gnm_dump_request = 0;
        if (req & 1) s_req_rt_present = present_idx + 1;
        if (req & 2) s_req_draws_present = present_idx + 1;
        if (req & 4) { // trace the work of the next present (like PS4_GNM_TRACE=p<next>:1)
            S.trace_by_present = true;
            S.trace_start = present_idx + 1;
            S.trace_count = 1;
        }
    }
    if (!dir || (present_idx != s_req_rt_present && (!env || strtoull(env, nullptr, 0) != present_idx))) return;
    Commit();
    WaitAll();
    for (const auto& [addr, what] : s_traced_indirect) {
        uint32_t a[5] = {};
        ReadGuest(S.ctx, addr, a, sizeof(a));
        fprintf(stderr, "[gnm-draw] indirect args 0x%llx %s: count %u inst %u first %u base %u start %u\n",
                (unsigned long long)addr, what.c_str(), a[0], a[1], a[2], a[3], a[4]);
    }
    s_traced_indirect.clear();
    for (uint64_t a : s_traced_streams) {
        float f[16] = {};
        ReadGuest(S.ctx, a, f, sizeof(f));
        fprintf(stderr, "[gnm-draw] stream 0x%llx: v0 (%g %g %g | %g %g %g %g %g) v1 (%g %g %g | %g %g %g %g %g)\n",
                (unsigned long long)a, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12],
                f[13], f[14], f[15]);
    }
    s_traced_streams.clear();
    // PS4_GNM_DUMP_MEM=<addr>:<bytes>[,<addr>:<bytes>...]: print guest memory as floats (debugging)
    if (const char* dm = getenv("PS4_GNM_DUMP_MEM")) {
        for (const char* c = dm; c && *c;) {
            char* e = nullptr;
            const uint64_t a = strtoull(c, &e, 0);
            const uint64_t n = (e && *e == ':') ? strtoull(e + 1, &e, 0) : 64;
            std::vector<uint32_t> w((n + 3) / 4);
            ReadGuest(S.ctx, a, w.data(), w.size() * 4);
            for (size_t k = 0; k < w.size(); k += 8) {
                fprintf(stderr, "[gnm-mem] 0x%llx:", (unsigned long long)(a + k * 4));
                for (size_t j = k; j < k + 8 && j < w.size(); ++j) fprintf(stderr, " %08x(%g)", w[j], AsF(w[j]));
                fprintf(stderr, "\n");
            }
            c = (e && *e == ',') ? e + 1 : nullptr;
        }
    }
    TextureCache* tc = S.tex;
    tc->ForEachGpuTexture(^(uint64_t addr, int kind, id<MTLTexture> t) {
        if (t.sampleCount > 1) return;
        if (kind != 1 && (t.pixelFormat == MTLPixelFormatDepth16Unorm || t.pixelFormat == MTLPixelFormatDepth32Float ||
                          t.pixelFormat == MTLPixelFormatDepth32Float_Stencil8)) {
            // depth targets as grayscale, normalized to their value range
            const size_t w = t.width, h = t.height;
            const bool d16 = t.pixelFormat == MTLPixelFormatDepth16Unorm;
            const size_t bpp = d16 ? 2 : 4;
            std::vector<uint8_t> raw; // level 0 / slice 0 of the depth plane come first (ReadRaw)
            if (!tc->ReadRaw(t, &raw) || raw.size() < w * h * bpp) {
                fprintf(stderr, "[gnm-draw] dump: cannot read depth 0x%llx fmt %lu\n", (unsigned long long)addr,
                        (unsigned long)t.pixelFormat);
                return;
            }
            std::vector<float> v(w * h);
            for (size_t i = 0; i < w * h; ++i)
                v[i] = d16 ? ((const uint16_t*)raw.data())[i] / 65535.f : ((const float*)raw.data())[i];
            float lo = 1e30f, hi = -1e30f;
            size_t n1 = 0, n0 = 0;
            for (float f : v) {
                lo = std::min(lo, f), hi = std::max(hi, f);
                n1 += f >= 1.f;
                n0 += f <= 0.f;
            }
            fprintf(stderr, "[gnm-draw] dump depth 0x%llx kind %d type %lu layers %lu %zux%zu fmt %lu: min %g max %g, %.1f%% at 1, %.1f%% at 0\n",
                    (unsigned long long)addr, kind, (unsigned long)t.textureType, (unsigned long)t.arrayLength, w, h, (unsigned long)t.pixelFormat, lo, hi, 100.0 * n1 / v.size(),
                    100.0 * n0 / v.size());
            std::vector<uint8_t> g(w * h);
            const float sc = hi > lo ? 255.f / (hi - lo) : 0.f;
            for (size_t i = 0; i < w * h; ++i) g[i] = (uint8_t)((v[i] - lo) * sc);
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceGray();
            CGContextRef bc = CGBitmapContextCreate(g.data(), w, h, 8, w, cs, kCGImageAlphaNone);
            CGImageRef img = bc ? CGBitmapContextCreateImage(bc) : NULL;
            if (img) {
                NSString* path = [NSString stringWithFormat:@"%s/depth_%llu_%llx_%zux%zu.png", dir,
                                                            (unsigned long long)present_idx, (unsigned long long)addr, w, h];
                CGImageDestinationRef dst = CGImageDestinationCreateWithURL(
                    (__bridge CFURLRef)[NSURL fileURLWithPath:path], CFSTR("public.png"), 1, NULL);
                if (dst) {
                    CGImageDestinationAddImage(dst, img, NULL);
                    CGImageDestinationFinalize(dst);
                    CFRelease(dst);
                }
                CGImageRelease(img);
            }
            if (bc) CGContextRelease(bc);
            CGColorSpaceRelease(cs);
            return;
        }
        if (kind != 1) return;
        std::vector<uint8_t> px;
        if (!tc->ReadTextureBGRA8(t, DataFormat::FormatInvalid, NumberFormat::Unorm, CompMapping{}, &px)) {
            // integer formats: write the raw texels (rt_<present>_<addr>_f<fmt>_<w>x<h>_<bpp>.raw)
            size_t bpp = 0;
            switch (t.pixelFormat) {
            case MTLPixelFormatR8Uint: bpp = 1; break;
            case MTLPixelFormatR16Uint: case MTLPixelFormatRG8Uint: bpp = 2; break;
            case MTLPixelFormatR32Uint: case MTLPixelFormatRG16Uint: case MTLPixelFormatRGBA8Uint: bpp = 4; break;
            case MTLPixelFormatRG32Uint: case MTLPixelFormatRGBA16Uint: case MTLPixelFormatRG32Float: bpp = 8; break;
            case MTLPixelFormatR32Float: bpp = 4; break;
            case MTLPixelFormatRGBA32Uint: case MTLPixelFormatRGBA32Float: bpp = 16; break;
            default: break;
            }
            if (!bpp || t.textureType != MTLTextureType2D) {
                fprintf(stderr, "[gnm-draw] dump: cannot read 0x%llx fmt %lu\n", (unsigned long long)addr, (unsigned long)t.pixelFormat);
                return;
            }
            const size_t w = t.width, h = t.height;
            std::vector<uint8_t> raw; // level 0 / slice 0 come first in ReadRaw's layout
            if (!tc->ReadRaw(t, &raw) || raw.size() < w * h * bpp) {
                fprintf(stderr, "[gnm-draw] dump: cannot read 0x%llx fmt %lu\n", (unsigned long long)addr, (unsigned long)t.pixelFormat);
                return;
            }
            char path[1024];
            snprintf(path, sizeof(path), "%s/rt_%llu_%llx_f%lu_%zux%zu_%zu.raw", dir, (unsigned long long)present_idx,
                     (unsigned long long)addr, (unsigned long)t.pixelFormat, w, h, bpp);
            if (FILE* f = fopen(path, "wb")) {
                fwrite(raw.data(), 1, w * h * bpp, f);
                fclose(f);
            }
            return;
        }
        const size_t w = t.width, h = t.height;
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef bc = CGBitmapContextCreate(px.data(), w, h, 8, w * 4, cs,
                                                kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
        CGImageRef img = bc ? CGBitmapContextCreateImage(bc) : NULL;
        if (img) {
            NSString* path = [NSString stringWithFormat:@"%s/rt_%llu_%llx_f%lu_%zux%zu.png", dir,
                                                        (unsigned long long)present_idx, (unsigned long long)addr,
                                                        (unsigned long)t.pixelFormat, w, h];
            CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:path],
                                                                        CFSTR("public.png"), 1, NULL);
            if (dst) {
                CGImageDestinationAddImage(dst, img, NULL);
                CGImageDestinationFinalize(dst);
                CFRelease(dst);
            }
            CGImageRelease(img);
        }
        if (bc) CGContextRelease(bc);
        CGColorSpaceRelease(cs);
    });
    fprintf(stderr, "[gnm-draw] dumped render targets of present %llu\n", (unsigned long long)present_idx);
    if (const char* peek = getenv("PS4_GNM_PEEK")) { // hex address: print 16 floats there
        const uint64_t a = strtoull(peek, nullptr, 16);
        float f[16];
        if (ReadGuest(S.ctx, a, f, sizeof(f))) {
            fprintf(stderr, "[gnm-draw] peek 0x%llx:", (unsigned long long)a);
            for (float v : f) fprintf(stderr, " %g", v);
            fprintf(stderr, "\n");
        }
    }
}

bool ShouldTrace() {
    const uint64_t idx = S.trace_by_present ? S.present_idx : S.submit_idx;
    return S.trace_count && idx >= S.trace_start && idx < S.trace_start + S.trace_count;
}

} // namespace

// =====================================================================================
// Public API
// =====================================================================================
// The residency of the textures the texture cache and the presenter create (Metal 4; Metal 3's
// MakeResident / Evict do nothing). A texture let go now may still be used by the open command
// buffer: it leaves its set once that seq (committed + 1) completed. committed_seq is read without
// fence_mu: these run on the encoder thread, its only writer (the presenter's hooks too: the
// renderer creates slot and held textures from Present). The texture cache's hooks are checked: one
// on a thread that does not hold the encoder's state (encq::Locked) is FATAL, as its committed_seq
// read would race. The presenter's hooks run wherever its public functions are called (the
// renderer's Present; tests/host/gnm_submit_flip_nonblocking_test.mm takes a slot from its own
// thread): they read committed_seq under fence_mu instead. After ReleaseBackend there is no
// residency left to keep.
void RequireEncoderState(const char* hook) {
    if (encq::t_depth > 0) return;
    fprintf(stderr, "[gnm-draw] FATAL: texture hook %s on a thread that does not hold the encoder's state\n", hook);
    abort();
}
void ResidentTexture(void*, id<MTLTexture> t) {
    RequireEncoderState("created");
    if (S.be) S.be->MakeResident(t, GnmResidencyClass::Texture);
}
void ReleasedTexture(void*, id<MTLTexture> t) {
    RequireEncoderState("released");
    if (S.be) S.be->Evict(t, S.committed_seq + 1);
}
void ScreenTextureCreated(void*, void* t) {
    if (GnmBackend* be = g_hook_be.load(std::memory_order_acquire))
        be->MakeResident((__bridge id<MTLTexture>)t, GnmResidencyClass::Texture);
}
void ScreenTextureReleased(void*, void* t) {
    GnmBackend* be = g_hook_be.load(std::memory_order_acquire);
    if (!be) return;
    uint64_t committed;
    {
        std::lock_guard<std::mutex> lk(S.fence_mu);
        committed = S.committed_seq;
    }
    be->Evict((__bridge id<MTLTexture>)t, committed + 1); // a seq not yet committed may use it
}

// TextureCache alias copies (GpuCopyHook): in the upload command buffer, which runs after every
// committed seq and before the open one, as the cache's own command buffer did on Metal 3's queue
// (on Metal 4 the cache's queue is not the renderer's: the copy would not be ordered with its work).
void AliasCopy(void*, id<MTLTexture> src, id<MTLTexture> dst, MTLSize size) {
    UploadTargetScope on_upload;
    S.be->BeginCopies();
    S.be->CopyTextureRegion(src, 0, 0, MTLOriginMake(0, 0, 0), size, dst, 0, 0, MTLOriginMake(0, 0, 0));
    S.be->EndEncoder();
}

void ReleaseBackend();

bool Init(GuestContext* ctx) {
    if (S.init_tried) return S.enabled.load(std::memory_order_acquire);
    S.init_tried = true;
    const char* e = getenv("PS4_GNM_METAL_DRAW");
    if (e && strcmp(e, "0") == 0) {
        fprintf(stderr, "[gnm-draw] GPU rendering disabled (PS4_GNM_METAL_DRAW=0)\n");
        return false;
    }
    if (!ctx || !ctx->mem_base) return false;
    if (const char* b = getenv("PS4_GNM_COMPUTE_BATCH")) {
        if (strcmp(b, "0") && strcmp(b, "1")) {
            fprintf(stderr, "[gnm-draw] FATAL: PS4_GNM_COMPUTE_BATCH=%s (0: commit every compute submission at "
                            "once, 1 or unset: batched compute commits)\n", b);
            abort();
        }
        g_compute_batch = b[0] == '1';
        if (!g_compute_batch) fprintf(stderr, "[gnm-draw] batched compute commits off (PS4_GNM_COMPUTE_BATCH=0)\n");
    }
    GuestContext* proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    S.ctx = proc;
    // ReadGuest is a bounds-checked copy from mem_base: let the recompiler read directly
    // (pointer walks of resource tables run for every draw); not while tracing the reads
    if (!getenv("PS4_GNM_TRACE_BUFS")) gcn_set_direct_guest_memory(ReadGuest, proc, proc->mem_base, proc->mem_size);
    // the unused records of T# tables hold data that is no T#: an entry starting at an unmapped
    // address is unbound (Uncharted 4's light records: floats decoding to 1D textures at
    // 0x3f2b800000 and the like, each a new shader permutation otherwise)
    // A process without a VM extent list (a harness that sets up guest memory by itself) has no
    // mapping to consult: every address counts as mapped there.
    if (proc->vm_extents) {
        gcn_set_image_address_check(GuestAddressMapped, proc);
        g_address_check = true;
    }
    @autoreleasepool {
        S.dev = (__bridge id<MTLDevice>)ps4_metal_screen_get_device();
        if (!S.dev) S.dev = MTLCreateSystemDefaultDevice();
        // New shaders and pipelines are built in the background and draws using them are
        // skipped until they are ready (building them inline stalls for seconds when an area
        // is entered for the first time). Shaders and pipelines seen in an earlier session are
        // cheap to rebuild and are built inline, so work the game renders only once (e.g. its
        // static shadow cache) is not lost once the caches are warm. PS4_GNM_SYNC_COMPILE=1
        // builds everything inline.
        S.async_compile = getenv("PS4_GNM_SYNC_COMPILE") == nullptr;
        if (!S.dev) return false;
        // Compute kernels compile in the background and their dispatches wait for them on the
        // encoder thread (ps4_gnm_kernel_compile.h); PS4_GNM_SYNC_KERNELS=1 (or
        // PS4_GNM_SYNC_COMPILE=1) compiles them on the CP thread as before.
        if (S.async_compile && !getenv("PS4_GNM_SYNC_KERNELS"))
        {
            // PS4_GNM_KERNEL_COMPILES: compiles at once (default 4)
            const char* e = getenv("PS4_GNM_KERNEL_COMPILES");
            const int n = e ? atoi(e) : 0;
            S.kernels = std::make_unique<ps4gnm_kernels::Compiler>(S.dev, CompileOptions,
                                                                   n > 0 && n <= 64 ? (unsigned)n : 4u);
        }
        StartGpuMemSampler(proc->mem_size);
        {
            const bool only_clears = getenv("PS4_GNM_MTL4_ONLY_CLEARS") && !strcmp(getenv("PS4_GNM_MTL4_ONLY_CLEARS"), "1");
            const GnmBackendChoice ch = ChooseBackend(getenv("PS4_GNM_METAL"), [S.dev supportsFamily:MTLGPUFamilyMetal4],
                                                      kMetal4BackendCaps, only_clears, DefaultPrefersMetal4());
            if (!ch.ok) {
                fprintf(stderr, "[gnm-draw] FATAL: %s\n", ch.why.c_str());
                ps4_host_quit(2);
            }
            S.be.reset((ch.kind == GnmBackendKind::Metal4 ? MakeMetal4Backend() : MakeMetal3Backend()).release());
            std::string why;
            if (!S.be->Init(S.dev, &why)) {
                fprintf(stderr, "[gnm-draw] FATAL: the %s backend did not start: %s\n", BackendName(ch.kind), why.c_str());
                ps4_host_quit(2);
            }
            fprintf(stderr, "[gnm-draw] backend %s (%s)\n", BackendName(S.be->Kind()), ch.why.c_str());
            g_alias.on = S.be->Kind() == GnmBackendKind::Metal3;
            if (g_alias.on) S.be->SetBeforeCommand(FlushAliasOrders, nullptr);
            // before S and this file's globals are destroyed (see ReleaseBackend)
            atexit(ReleaseBackend);
            // the upload arena's chunks are resident from creation until their release
            S.arena.on_new = [](id<MTLBuffer> b) { if (S.be) S.be->MakeResident(b, GnmResidencyClass::Guest); };
            S.arena.on_drop = [](id<MTLBuffer> b) { if (S.be) S.be->Evict(b, S.committed_seq + 1); };
            // from CommitDoneFn (a Metal completion thread): through g_hook_be, not S.be
            S.arena.on_retired = [](id<MTLBuffer> b, uint64_t seq) {
                if (GnmBackend* be = g_hook_be.load(std::memory_order_acquire)) be->Evict(b, seq);
            };
            g_hook_be.store(S.be.get(), std::memory_order_release);
            const bool metal4 = S.be->Kind() == GnmBackendKind::Metal4;
            g_backend_metal4.store(metal4, std::memory_order_relaxed);
            g_only_clears = only_clears && metal4;
            g_encoder_status = getenv("PS4_GNM_ENCODER_STATUS") != nullptr && !metal4;
            if (metal4 && getenv("PS4_GNM_ENCODER_STATUS"))
                fprintf(stderr, "[gnm-draw] PS4_GNM_ENCODER_STATUS is Metal 3 only (Metal 4 has no per-encoder "
                                "status); ignored. Bisect with PS4_GNM_COMMIT_EACH=1 instead\n");
        }
        // guest memory dispatch tickets (GmSync); the listener runs on its own serial queue
        g_gms.ev = [S.dev newSharedEvent];
        g_gms.listener = [[MTLSharedEventListener alloc]
            initWithDispatchQueue:dispatch_queue_create("gnm.guest-memory", dispatch_queue_attr_make_with_qos_class(
                                                                                  DISPATCH_QUEUE_SERIAL,
                                                                                  QOS_CLASS_USER_INTERACTIVE, 0))];
        S.tex = new TextureCache(S.dev, S.be->SideQueue(), proc->mem_base, proc->mem_size);
        S.tex->SetHooks(TextureCache::TextureHooks{nullptr, ResidentTexture, ReleasedTexture});
        S.tex->SetGpuCopyHook(TextureCache::GpuCopyHook{nullptr, AliasCopy});
        if (ps4_metal_screen_get_device()) { // not under the replay: it never presents
            ps4_metal_screen_set_texture_hooks(ScreenTextureCreated, ScreenTextureReleased, nullptr);
            // the performance overlay names the backend
            ps4_metal_screen_set_backend_label(S.be->Kind() == GnmBackendKind::Metal4 ? "Metal 4" : "Metal 3");
        }
        g_mem_tex.store(S.tex, std::memory_order_release);
        S.dummy = [S.dev newBufferWithLength:65536 options:MTLResourceStorageModeShared];
        S.gds = (uint8_t*)mmap(nullptr, 0x10000, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (S.gds == MAP_FAILED) S.gds = nullptr;
        if (S.gds)
            S.gds_buf = [S.dev newBufferWithBytesNoCopy:S.gds length:0x10000
                                                options:MTLResourceStorageModeShared deallocator:nil];
        if (!S.gds_buf) S.gds_buf = S.dummy;
        S.be->MakeResident(S.dummy, GnmResidencyClass::Builtin);
        if (S.gds_buf != S.dummy) S.be->MakeResident(S.gds_buf, GnmResidencyClass::Builtin);
        gcn_profile_default_metal(&S.profile);
        // PS4_GNM_QUIT_AFTER_S=<s> (benchmarks): quit through the normal quit path after <s>
        // seconds, so the caches are saved as when the player quits (a killed process loses the
        // pipeline archive additions since its last save).
        if (const char* q = getenv("PS4_GNM_QUIT_AFTER_S"); q && atof(q) > 0) {
            const double secs = atof(q);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(secs * NSEC_PER_SEC)),
                           dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
                               fprintf(stderr, "[gnm-draw] PS4_GNM_QUIT_AFTER_S: quitting after %.0f s\n", secs);
                               PrintCompileStats();
                               ps4_host_quit(0);
                           });
        }
        StartShaderWarmup();
        if (!S.msl_cache_dir.empty()) {
            // PS4_GNM_PSO_ARCHIVE_MB: archive size budget; not a positive number -> 384
            const char* mb = getenv("PS4_GNM_PSO_ARCHIVE_MB");
            char* mb_end = nullptr;
            long long mb_v = mb ? strtoll(mb, &mb_end, 10) : 0;
            if (!mb || mb_end == mb || *mb_end || mb_v <= 0 || mb_v > (1ll << 20)) mb_v = 384;
            const uint64_t budget = (uint64_t)mb_v << 20;
            if (ps4gnm_pso_archive::Open(S.dev, S.msl_cache_dir, budget)) ps4gnm_pso_archive::StartPeriodicSave(300);
        }
        if (getenv("PS4_GNM_NO_BOUNDS_EMIT")) S.profile.needs_buffer_bounds_checks = false; // debug A/B
        gcn_msl_options_default(&S.msl);
        ps4gnm_shader_cache::Start(S.profile, S.msl);
        gcn_set_log_level(4);
        if (const char* cf = getenv("PS4_GNM_CAPTURE_FRAME"); cf && *cf) cap::Setup(strtoull(cf, nullptr, 0), proc->mem_size);
        if (const char* t = getenv("PS4_GNM_TRACE")) {
            // "<count>", "<first_submit>:<count>" or "p<first_present>:<count>" (present indices)
            if (t[0] == 'p') {
                S.trace_by_present = true;
                t++;
            }
            const char* c = strchr(t, ':');
            if (c) {
                S.trace_start = strtoull(t, nullptr, 0);
                S.trace_count = strtoull(c + 1, nullptr, 0);
            } else {
                S.trace_count = strtoull(t, nullptr, 0);
            }
        }
        if (const char* t = getenv("PS4_GNM_GPU_TIMEOUT")) S.gpu_timeout_ms = std::max(100, atoi(t));

        // the utility shaders: present conversion, depth and stencil copies, depth companions,
        // EQAA copies, GPU texture detile (ps4_gnm_util_passes.h)
        GnmUtil util;
        std::string uwhy;
        if (MakeGnmUtil(S.dev, &util, &uwhy)) {
            S.util_lib = util.lib;
            S.present_pso = util.present;
            S.depth2color_pso = util.depth2color;
            S.detile_pso = util.detile;
        }
        if (!S.present_pso) fprintf(stderr, "[gnm-draw] present pipeline failed: %s\n", uwhy.c_str());
    }
    S.enabled.store(true, std::memory_order_release);
    fprintf(stderr, "[gnm-draw] native Metal renderer enabled on %s (%s)\n", S.dev.name.UTF8String,
            gcn_version());
    encq::Start();
    return true;
}

bool Enabled() {
    return S.enabled.load(std::memory_order_acquire) && !S.hung;
}

uint8_t* Gds() {
    static uint8_t fallback[0x10000];
    return S.gds ? S.gds : fallback;
}

void ComputeBarrierImpl() {
    // also reached before Init (the CP's first command buffers): no backend yet, nothing open
    if (!S.enabled.load(std::memory_order_acquire) || S.be->OpenEncoder() != GnmEncoder::Compute) return;
    S.be->Barrier(kGnmStageDispatch | kGnmStageBlit, kGnmStageDispatch | kGnmStageBlit,
                  kGnmScopeBuffers | kGnmScopeTextures);
    S.n_cbarriers++;
    S.cbarriers_total++;
}

void ComputeBarrier(int queue) {
    GmCpSync(queue);
    encq::Defer([] { ComputeBarrierImpl(); });
}

bool GpuDmaImpl(bool src_gds, bool src_fill, uint64_t src, uint32_t fill_value, bool dst_gds, uint64_t dst, uint32_t n) {
    const AliasJob alias_job;
    if (!S.enabled.load(std::memory_order_acquire) || S.hung || !S.gds || n == 0 || (n & 3) || (dst & 3) || (!src_fill && (src & 3))) return false;
    // Metal blits on macOS need 4-byte aligned offsets and sizes
    id<MTLBuffer> db = nil, sb = nil;
    uint64_t doff = 0, soff = 0;
    if (dst_gds) {
        if (dst + n > 0x10000) return false;
        db = S.gds_buf;
        doff = dst;
    } else if (!GuestBuffer(dst, n, GuestAccess::Write, &db, &doff)) {
        return false;
    }
    if (src_fill) {
        const uint8_t b0 = fill_value & 0xFF;
        const bool bytes_equal = fill_value == b0 * 0x01010101u;
        if (!bytes_equal) {
            if (n > 0x10000) return false;
            std::vector<uint32_t> pat(n / 4, fill_value);
            sb = [S.dev newBufferWithBytes:pat.data() length:n options:MTLResourceStorageModeShared];
            if (!sb) return false;
            S.be->MakeResident(sb, GnmResidencyClass::Builtin); // until the copy below has run
        }
    } else if (src_gds) {
        if (src + n > 0x10000) return false;
        sb = S.gds_buf;
        soff = src;
    } else if (!GuestBuffer(src, n, GuestAccess::Read, &sb, &soff)) {
        return false;
    }
    EndEncoders();
    EnsureCmd();
    S.be->BeginCopies();
    if (src_fill && !sb) S.be->FillBuffer(db, doff, n, (uint8_t)(fill_value & 0xFF));
    else S.be->CopyBuffer(sb, soff, db, doff, n);
    S.be->EndEncoder();
    if (src_fill && sb) S.be->Evict(sb, S.committed_seq + 1); // the pattern: used by the open seq only
    LogWork("dma");
    if (!dst_gds) {
        NoteGpuWrite(dst, n);
        if (S.tex) S.tex->NoteBufferWrite(dst, n, S.write_epoch); // encoded above
    }
    return true;
}

bool GuestRangeOk(uint64_t addr, uint64_t size);

// Decides on the CP thread (the caller falls back to a CPU copy) whether GpuDmaImpl can do it.
bool GpuDma(bool src_gds, bool src_fill, uint64_t src, uint32_t fill_value, bool dst_gds, uint64_t dst, uint32_t n) {
    if (!S.enabled.load(std::memory_order_acquire) || S.hung || !S.gds || n == 0 || (n & 3) || (dst & 3) || (!src_fill && (src & 3))) return false;
    if (!BackendHas(kGnmCapCopies)) return false; // the CP copies on the CPU (CP thread: S.be is fixed after Init)
    if (dst_gds ? (uint64_t)dst + n > 0x10000 : !GuestRangeOk(dst, n)) return false;
    if (src_fill) {
        if (fill_value != (fill_value & 0xFF) * 0x01010101u && n > 0x10000) return false;
    } else if (src_gds ? (uint64_t)src + n > 0x10000 : !GuestRangeOk(src, n)) {
        return false;
    }
    encq::Defer([=] {
        if (!GpuDmaImpl(src_gds, src_fill, src, fill_value, dst_gds, dst, n))
            RLog("dma-fail", "GPU DMA 0x%llx -> 0x%llx (%u bytes) failed on the encoder", (unsigned long long)src,
                 (unsigned long long)dst, n);
    });
    return true;
}

// shadPS4 src/video_core/amdgpu/regs.cpp: Regs::SetDefaults (values from fpPS4)
void SetRegDefaults(uint32_t* regs) {
    static const uint32_t kDefaults[] = {
        0x00000000u, 0x80000000u, 0x40004000u, 0xdeadbeefu, 0x00000000u, 0x40004000u, 0x00000000u,
        0x40004000u, 0x00000000u, 0x40004000u, 0x00000000u, 0x40004000u, 0xaa99aaaau, 0x00000000u,
        0xdeadbeefu, 0xdeadbeefu, 0x80000000u, 0x40004000u, 0x00000000u, 0x00000000u, 0x80000000u,
        0x40004000u, 0x80000000u, 0x40004000u, 0x80000000u, 0x40004000u, 0x80000000u, 0x40004000u,
        0x80000000u, 0x40004000u, 0x80000000u, 0x40004000u, 0x80000000u, 0x40004000u, 0x80000000u,
        0x40004000u, 0x80000000u, 0x40004000u, 0x80000000u, 0x40004000u, 0x80000000u, 0x40004000u,
        0x80000000u, 0x40004000u, 0x80000000u, 0x40004000u, 0x80000000u, 0x40004000u, 0x80000000u,
        0x40004000u, 0x80000000u, 0x40004000u, 0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u,
        0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u, 0x00000000u,
        0x3f800000u, 0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u,
        0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u, 0x00000000u,
        0x3f800000u, 0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u,
        0x2a00161au,
    };
    // CLEAR_STATE resets the context registers only (SH/uconfig state persists on hardware;
    // shadPS4 clears the whole array, which loses shader programs set before the clear).
    memset(&regs[0xA000], 0, 0x2000 * sizeof(uint32_t));
    memcpy(&regs[0xA000 + 0x80], kDefaults, sizeof(kDefaults));
    regs[0xA000 + 0x000d] = 0x40004000u;
    regs[0xA000 + 0x01b6] = 0x00000002u;
    regs[0xA000 + 0x0204] = 0x00090000u;
    regs[0xA000 + 0x0205] = 0x00000004u;
    regs[0xA000 + 0x0295] = 0x00000100u;
    regs[0xA000 + 0x0296] = 0x00000080u;
    regs[0xA000 + 0x0297] = 0x00000002u;
    regs[0xA000 + 0x02aa] = 0x00001000u;
    regs[0xA000 + 0x02f7] = 0x00001000u;
    regs[0xA000 + 0x02f9] = 0x00000005u;
    regs[0xA000 + 0x02fa] = 0x3f800000u;
    regs[0xA000 + 0x02fb] = 0x3f800000u;
    regs[0xA000 + 0x02fc] = 0x3f800000u;
    regs[0xA000 + 0x02fd] = 0x3f800000u;
    regs[0xA000 + 0x0316] = 0x0000000eu;
    regs[0xA000 + 0x0317] = 0x00000010u;
}

void AfterGpuImpl(std::function<void()> fn) {
    if (!S.enabled.load(std::memory_order_acquire) || S.hung) {
        if (S.hung) RunFences(~0ull >> 1);
        fn();
        return;
    }
    // The action belongs to the end of everything encoded so far. Work still being recorded
    // goes into the open command buffer, which gets sequence number committed_seq + 1 when it
    // is committed; tag the action with that number instead of committing right away, so a
    // stream with many fences (one per compute dispatch, occlusion query, ...) still ends up in
    // a few command buffers. Every CP entry point commits the open buffer before returning
    // (ps4_gnm_metal.mm), or, at the end of a compute submission, holds it for 0.5 ms at most
    // plus the encoder jobs queued before the release (FlushCompute), so the action cannot be
    // stranded.
    bool now = false;
    {
        std::lock_guard<std::mutex> lk(S.fence_mu);
        const bool open = MainCmdOpen();
        const uint64_t seq = S.committed_seq + (open ? 1 : 0);
        if (!open && S.fence_q.empty() && S.completed_seq >= S.committed_seq) now = true;
        else S.fence_q.push_back({seq, std::move(fn)});
    }
    if (now) {
        std::lock_guard<std::recursive_mutex> run(S.fence_run_mu);
        fn();
    } else if (!MainCmdOpen()) {
        // the command buffers may already have completed before the push (otherwise the
        // completion handler of the last one runs the action)
        uint64_t done;
        bool ready;
        {
            std::lock_guard<std::mutex> lk(S.fence_mu);
            done = S.completed_seq;
            ready = !S.fence_q.empty() && S.fence_q.front().first <= done;
        }
        if (ready) RunFences(done);
    }
}

// Fence actions belong behind the work handed to the encoder before them.
void AfterGpu(std::function<void()> fn) {
    if (encq::Inline()) {
        AfterGpuImpl(std::move(fn));
        return;
    }
    encq::Defer([f = std::move(fn)]() mutable { AfterGpuImpl(std::move(f)); });
}

void SetExtentHints(const uint32_t cb[8], uint32_t db) {
    memcpy(S.cb_extent, cb, sizeof(S.cb_extent));
    S.db_extent = db;
}

void OnSubmit() {
    S.submit_idx++;
}

// The test hooks' wait: the open command buffer waits on the GPU until ev reaches value, and
// everything encoded after it waits too.
void DebugGpuWaitImpl(id<MTLSharedEvent> ev, uint64_t value) {
    EndEncoders();
    EnsureCmd();
    RequireMainTarget("DebugStallGpu");
    S.be->QueueWait(ev, value);
    S.pending = true;
}

// Test hook (tests/host/gnm_submit_flip_nonblocking_test.mm): an event nothing signals, as with a
// hung GPU, until the watchdog (WaitAll, PS4_GNM_GPU_TIMEOUT) gives the renderer up.
static id<MTLSharedEvent> g_stall_event; // created by the first DebugStallGpu, signaled by DebugReleaseGpu

void DebugStallGpu() {
    encq::Run([] {
        if (!S.enabled.load(std::memory_order_acquire)) return;
        if (!g_stall_event) g_stall_event = [S.dev newSharedEvent];
        DebugGpuWaitImpl(g_stall_event, 1);
    });
}

// Test hook: the event DebugStallGpu waits for is signaled, so the stalled command buffers run to
// their end and complete. A test that stalled the GPU calls it when it is done: otherwise the
// stalled work only ends when the system's GPU timeout kills it, at a time the system chooses.
void DebugReleaseGpu() {
    id<MTLSharedEvent> ev = g_stall_event;
    if (ev) ev.signaledValue = 1;
}

void FlushImpl(bool wait);

// Test hook (tests/host/gnm_replay_test.mm TestArenaGpuWrite): the GPU writes an arena chunk.
bool DebugArenaGpuFill(uint8_t value, size_t n, std::vector<uint8_t>* out) {
    bool ok = false;
    encq::Run([&] {
        if (!S.enabled.load(std::memory_order_acquire)) return;
        id<MTLBuffer> b = nil;
        size_t off = 0;
        void* ptr = nullptr;
        const size_t size = UploadArena::kChunk + 4096; // over a chunk: a buffer of its own, never a recycled one
        if (n > size || !S.arena.Alloc(S.dev, size, 256, &b, &off, &ptr)) return;
        memset(ptr, 0, size);
        EndEncoders();
        EnsureCmd();
        S.be->BeginCopies();
        S.be->FillBuffer(b, off, size, value);
        S.be->EndEncoder();
        FlushImpl(true);
        out->assign((const uint8_t*)ptr, (const uint8_t*)ptr + n);
        ok = true;
    });
    return ok;
}

bool DebugTwoEncoderCopy(uint64_t src, uint64_t dst, uint32_t n) {
    bool ok = false;
    encq::Run([&] {
        if (!S.enabled.load(std::memory_order_acquire)) return;
        const AliasJob alias_job;
        id<MTLBuffer> sb = nil, db = nil, ab = nil;
        uint64_t soff = 0, doff = 0;
        size_t aoff = 0;
        void* ptr = nullptr;
        if (!GuestBuffer(src, n, GuestAccess::Read, &sb, &soff) || !GuestBuffer(dst, n, GuestAccess::Write, &db, &doff) ||
            !S.arena.Alloc(S.dev, 256, 256, &ab, &aoff, &ptr))
            return;
        EndEncoders();
        EnsureCmd();
        S.be->BeginCopies();
        S.be->FillBuffer(ab, aoff, 256, 0);
        S.be->EndEncoder();
        S.be->BeginCopies();
        S.be->CopyBuffer(sb, soff, db, doff, n);
        S.be->EndEncoder();
        S.pending = true;
        ok = true;
    });
    return ok;
}

// Test hook (tests/host/gnm_exit_test.mm): an event the test signals.
void DebugGpuWait(void* shared_event, uint64_t value) {
    id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)shared_event;
    encq::Run([=] {
        if (S.enabled.load(std::memory_order_acquire)) DebugGpuWaitImpl(ev, value);
    });
}

bool HasPendingWork() {
    if (encq::Pending() || g_cmd_open.load(std::memory_order_acquire)) return true;
    std::lock_guard<std::mutex> lk(S.fence_mu);
    return S.completed_seq < S.committed_seq;
}

void FlushImpl(bool wait) {
    if (!S.enabled.load(std::memory_order_acquire)) return;
    @autoreleasepool {
        Commit();
        if (wait) {
            WaitAll();
        }
    }
}

// Flush(true) returns once the GPU finished everything submitted so far (CPU reads follow).
void Flush(bool wait) {
    if (wait) encq::Run([] { FlushImpl(true); });
    else encq::Defer([] { FlushImpl(false); });
}

void FlushCompute() {
    encq::Defer([] { FlushComputeImpl(); });
}

void DebugSetComputeHoldNs(uint64_t ns) {
    g_hold.bound_ns.store(ns ? ns : kHoldNs);
}

ComputeHoldStats GetComputeHoldStats() {
    ComputeHoldStats s;
    s.held = g_hold.held.load();
    s.by_time = g_hold.by_time.load();
    s.by_backlog = g_hold.by_backlog.load();
    s.holding = g_hold.since.load() != 0;
    return s;
}

EncodeCounts GetEncodeCounts() {
    EncodeCounts c;
    encq::Run([&] {
        std::lock_guard<std::mutex> lk(S.fence_mu);
        c.committed = S.committed_seq;
        c.compute_encoders = S.cencs_total;
        c.compute_barriers = S.cbarriers_total;
    });
    return c;
}

void NotifyGuestWriteImpl(uint64_t addr, uint64_t size, bool is_fill, uint32_t fill_value, bool cp_dma);

// The guest memory was just written by the CPU: the texture cache must see it before any
// later draw, and after the earlier ones (the callers drained the GPU first).
void NotifyGuestWrite(uint64_t addr, uint64_t size, bool is_fill, uint32_t fill_value, bool cp_dma) {
    encq::Run([=] { NotifyGuestWriteImpl(addr, size, is_fill, fill_value, cp_dma); });
}

void NotifyGuestWriteImpl(uint64_t addr, uint64_t size, bool is_fill, uint32_t fill_value, bool cp_dma) {
    if (!S.enabled.load(std::memory_order_acquire)) return;
    // a CP DMA done on the CPU is GPU work writing exactly these bytes, as on the GPU path (GpuDmaImpl)
    if (cp_dma && S.tex) S.tex->NoteBufferWrite(addr, size, ++S.write_epoch);
    (void)fill_value;
    if (is_fill) {
        auto it = S.meta.find(addr);
        if (it != S.meta.end()) ArmMetaClear(it->second, size, S.committed_seq + 1);
    }
    if (S.tex) S.tex->InvalidateRange(addr, size, false);
}

// A draw prepared by the CP thread (Draw) for the encoder thread (DrawEncode).
struct VtxIn {
    BufferDesc bd;
    VertexFormatInfo vf;
};
struct DrawRec {
    DrawArgs args;
    RenderState rs;
    RegSnap regs;
    bool trace = false;
    Shader* vs = nullptr;
    Shader* ps = nullptr;
    uint64_t vs_addr = 0, ps_addr = 0;
    uint32_t vs_ud[16] = {}, ps_ud[16] = {};
    gcn_runtime_info vs_rt{}, ps_rt{}; // the stages' runtime state (ResolveTableHazards)
    uint64_t table_snap = 0;           // TableReadBegin when the CP read guest memory for it
    std::vector<uint32_t> vs_flat, ps_flat;
    std::vector<VtxIn> vtx;
    StageImages vs_img, ps_img;
    // ready for the next draw, keeping the capacity of the vectors. Draw() sets everything else
    // (rs is reset by DecodeRenderState, regs entirely written by Capture).
    void Recycle() {
        args = DrawArgs{};
        trace = false;
        vs = ps = nullptr;
        vs_addr = ps_addr = 0;
        memset(vs_ud, 0, sizeof(vs_ud));
        memset(ps_ud, 0, sizeof(ps_ud));
        vs_flat.clear();
        ps_flat.clear();
        vtx.clear();
        vs_img.Clear();
        ps_img.Clear();
    }
};
void DrawEncode(DrawRec& r);

// Draw and dispatch records are reused, so preparing and retiring one does not allocate. Taken
// by the CP thread, returned by the encoder.
template <class T>
struct RecPool {
    static constexpr size_t kBatch = 64;
    std::mutex mu;
    std::vector<T*> free; // batches returned by Put
    T* Get() {
        static thread_local std::vector<T*> mine;
        if (mine.empty()) {
            std::lock_guard<std::mutex> lk(mu);
            mine.swap(free);
        }
        if (mine.empty()) return new T();
        T* p = mine.back();
        mine.pop_back();
        return p;
    }
    void Put(T* p) {
        p->Recycle();
        static thread_local std::vector<T*> back;
        back.push_back(p);
        if (back.size() < kBatch) return;
        std::lock_guard<std::mutex> lk(mu);
        if (free.size() < 8 * kBatch) free.insert(free.end(), back.begin(), back.end());
        else
            for (T* q : back) delete q;
        back.clear();
    }
    // returns a record that did not reach the encoder
    struct Back {
        RecPool* pool;
        void operator()(T* p) const { pool->Put(p); }
    };
};
RecPool<DrawRec> g_draw_recs;

bool GuestRangeOk(uint64_t addr, uint64_t size) {
    const GuestContext* ctx = S.ctx;
    return ctx && addr && size && addr < ctx->mem_size && size <= ctx->mem_size - addr;
}

// Frame capture: an indirect draw the GPU path cannot take; the caller reads its arguments and
// issues it again as a direct draw (the next pass of the capture).
constexpr const char* kCapIndirectFallback = "indirect draw replaced by a direct draw (next pass)";

bool Draw(GuestContext* ctx, const uint32_t* regs, const DrawArgs& args) {
    if (!Init(ctx) || S.hung) return true;
    GmCpQueue(kQueueGfx);
    // a draw reads guest memory on the CPU (resource tables, vertex fetch sharps, index buffers,
    // indirect arguments, sampled textures): after the tickets its queue synchronized with
    GmCpOrder();
    @autoreleasepool {
        cap::CpScope cap_scope("draw", "gfx", args.name, regs);
        if (cap::g_pass) cap::NoteDraw(args);
        std::unique_ptr<DrawRec, RecPool<DrawRec>::Back> rp(g_draw_recs.Get(), {&g_draw_recs});
        DrawRec& r = *rp;
        TableReadCp table_read; // before anything here reads guest memory
        r.table_snap = TableReadBegin();
        r.args = args;
        RenderState& rs = r.rs;
        DecodeRenderState(regs, &rs);
        for (int i = 0; i < 8; ++i) {
            rs.color[i].extent_width = S.cb_extent[i] & 0xFFFF;
            rs.color[i].extent_height = S.cb_extent[i] >> 16;
        }
        rs.depth.extent_width = S.db_extent & 0xFFFF;
        rs.depth.extent_height = S.db_extent >> 16;
        const bool trace = r.trace = ShouldTrace();

        // ---- FilterDraw
        if (trace && rs.color_mode != 1)
            fprintf(stderr, "[gnm-trace] %s color_mode=%u cb0=0x%llx %ux%u s%u fmt %u/%u fc=%d cmask=0x%llx cb1=0x%llx %ux%u s%u\n",
                    args.name, rs.color_mode, (unsigned long long)rs.color[0].address, rs.color[0].pitch,
                    rs.color[0].height, rs.color[0].num_samples, rs.color[0].raw_format, rs.color[0].raw_number_type,
                    rs.color[0].fast_clear, (unsigned long long)rs.color[0].cmask_address,
                    (unsigned long long)rs.color[1].address, rs.color[1].pitch, rs.color[1].height,
                    rs.color[1].num_samples);
        if (rs.color_mode == 2 || rs.color_mode == 5) { // EliminateFastClear / FmaskDecompress
            cap::SetSkipped("fast-clear elimination / FMASK decompress (renderer operation, not a shader pass)");
            encq::Defer([rec = rp.release()] {
                EliminateFastClear(rec->rs);
                g_draw_recs.Put(rec);
            });
            return true;
        }
        if (rs.color_mode == 3) {
            cap::SetSkipped("MSAA resolve (renderer operation, not a shader pass)");
            encq::Defer([rec = rp.release()] {
                ResolvePass(rec->rs);
                g_draw_recs.Put(rec);
            });
            return true;
        }
        const bool indirect = args.indirect_addr != 0;
        if (rs.raster.prim == PrimitiveType::None || (!indirect && args.count == 0)) return true;
        if (args.count > (16u << 20) || args.num_instances > (1u << 20)) {
            RLog("draw-count", "%s: implausible draw (count %u instances %u); skipped", args.name, args.count,
                 args.num_instances);
            NoteSkip(SkipReason::DrawCount);
            return true;
        }
        const uint32_t stages = regs[0xA2D5]; // VGT_SHADER_STAGES_EN
        if (stages & 0x3F) {
            RLog("stages", "%s: unsupported shader stages enabled (VGT_SHADER_STAGES_EN=0x%x)", args.name, stages);
            NoteSkip(SkipReason::ShaderStages);
            return true;
        }
        if (rs.raster.cull_front_and_back) return true;

        // ---- shaders
        gcn_runtime_info vs_rt{}, ps_rt{};
        gcn_runtime_info_from_regs(regs, 0x10000, GCN_HW_VS, GCN_SW_VERTEX, &S.profile, &vs_rt);
        vs_rt.vs_sw.tess_emulated_primitive = false; // quad lists are expanded to triangles here
        uint64_t& vs_addr = r.vs_addr;
        uint64_t& ps_addr = r.ps_addr;
        uint32_t* const vs_ud = r.vs_ud;
        uint32_t* const ps_ud = r.ps_ud;
        gcn_stage_from_regs(regs, 0x10000, GCN_HW_VS, &vs_addr, vs_ud);
        gcn_stage_from_regs(regs, 0x10000, GCN_HW_PS, &ps_addr, ps_ud);
        DrainAsyncCompiles();
        Shader* vs = GetShader(GCN_HW_VS, GCN_SW_VERTEX, vs_rt, vs_addr, vs_ud, true);
        if (vs && !vs->fn && ShaderCompiling(vs)) { // still compiling in the background
            NoteDrawPendingCompile();
            g_hitch.pending_draws++;
            return true;
        }
        if (!vs || !vs->fn) {
            NoteSkip(SkipReason::VsUnavailable);
            RLog("skip-vs", "%s skipped: VS 0x%llx unavailable", args.name, (unsigned long long)vs_addr);
            return true;
        }
        if (indirect) {
            // The GPU path cannot write baseVertex / startInstance into user SGPRs. It is exact
            // when those SGPRs only feed the fetch shader's index offsets, which the recompiled
            // shader takes from Metal's base vertex / base instance instead.
            const auto& in = vs->info;
            if ((args.base_vertex_ud >= 0 && (!in.has_fetch_shader || args.base_vertex_ud != in.vertex_offset_sgpr)) ||
                (args.start_instance_ud >= 0 &&
                 (!in.has_fetch_shader || args.start_instance_ud != in.instance_offset_sgpr)) ||
                regs[0xA102] != 0 || (args.indexed && !args.max_index)) {
                cap::SetSkipped(kCapIndirectFallback);
                return false;
            }
        }
        if (rs.raster.prim == PrimitiveType::RectList && !indirect) {
            bool pending = false;
            if (!RectListShaders(vs, &pending)) {
                if (pending) {
                    NoteDrawPendingCompile();
                    g_hitch.pending_draws++;
                } else {
                    NoteSkip(SkipReason::RectVariant);
                    RLog("rect-skip", "%s skipped: VS 0x%llx has no RectList variant", args.name,
                         (unsigned long long)vs_addr);
                }
                return true;
            }
        }
        Shader* ps = nullptr;
        if (ps_addr) {
            gcn_runtime_info_from_regs(regs, 0x10000, GCN_HW_PS, GCN_SW_FRAGMENT, &S.profile, &ps_rt);
            // Metal attachments absorb BGRA component swaps; only the remainder is applied by
            // the shader export.
            for (int i = 0; i < 8; ++i) {
                if (!ps_rt.fs.color_buffers[i].data_format) continue;
                const CompMapping& ex = rs.color[i].export_swizzle;
                for (int k = 0; k < 4; ++k) ps_rt.fs.color_buffers[i].swizzle[k] = (uint8_t)ex[k];
            }
            ps = GetShader(GCN_HW_PS, GCN_SW_FRAGMENT, ps_rt, ps_addr, ps_ud, true);
            if (ps && !ps->fn && ShaderCompiling(ps)) { // still compiling in the background
                NoteDrawPendingCompile();
                g_hitch.pending_draws++;
                return true;
            }
            if (!ps || !ps->fn) {
                NoteSkip(SkipReason::PsUnavailable);
                RLog("skip-ps", "%s skipped: PS 0x%llx unavailable", args.name, (unsigned long long)ps_addr);
                return true;
            }
        }

        if (indirect) {
            // what the encoder's GuestBuffer calls need (CPU fallback otherwise)
            const uint64_t isz = args.index_type == 1 ? 4 : 2;
            if (rs.raster.needs_emulation || !GuestRangeOk(args.indirect_addr, args.indexed ? 20 : 16) ||
                (args.indirect_addr & 3)) {
                cap::SetSkipped(kCapIndirectFallback);
                return false;
            }
            if (args.indexed && (!args.index_base || !GuestRangeOk(args.index_base, (uint64_t)args.max_index * isz) ||
                                 (args.index_base & 3))) {
                cap::SetSkipped(kCapIndirectFallback);
                return false;
            }
        }

        // ---- vertex input sharps (guest memory; the encoder binds the buffers)
        const uint32_t vb_first = vs->info.msl_first_free_buffer;
        r.vtx.resize(vs->attrs.size());
        for (uint32_t i = 0; i < vs->attrs.size(); ++i) {
            VtxIn& vi = r.vtx[i];
            uint32_t v[4] = {};
            GmCpGuarded([&](gcn_read_guest_fn read) {
                return gcn_shader_fetch_vertex_attrib_sharp(vs->sh, i, vs_ud, read, S.ctx, v);
            });
            DecodeBuffer(v, &vi.bd);
            vi.vf = GetVertexFormat(vi.bd.raw_dfmt, vi.bd.raw_nfmt);
            const uint32_t slot = vb_first + i;
            // (gcn IsVertexFormatFetchable mirrors the format table: such a V# is read in the shader)
            if (slot > 30 || !vi.vf.mtl_vertex_format) {
                RLog("vtx-fmt", "%s: VS 0x%llx attribute %u unsupported (fmt %u/%u slot %u)", args.name,
                     (unsigned long long)vs->code_addr, i, vi.bd.raw_dfmt, vi.bd.raw_nfmt, slot);
                NoteSkip(SkipReason::VertexFormat);
                return true;
            }
            // Metal's vertex fetch needs dword multiples; the translator reads any other V# from
            // the buffer in the shader (fetch_shader_inlined), and the permutation lookup never
            // pairs such a V# with a shader that has vertex attributes. Reaching this is a bug.
            if ((vi.bd.stride & 3) || (vi.bd.address & 3)) {
                RLog("vtx-stride", "%s: VS 0x%llx (program 0x%llx%s) attribute %u: stride %u / address 0x%llx not a "
                     "multiple of 4 in a shader with vertex attributes", args.name, (unsigned long long)vs->code_addr,
                     (unsigned long long)vs->info.pgm_hash, vs->from_worker ? ", translated on the worker" : "", i,
                     vi.bd.stride, (unsigned long long)vi.bd.address);
                NoteSkip(SkipReason::VertexAlign);
                return true;
            }
        }

        // ---- resource tables (flattened user data)
        StageFlat(0, vs, vs_ud);
        r.vs_flat = S.flat_scratch[0];
        if (ps) {
            StageFlat(1, ps, ps_ud);
            r.ps_flat = S.flat_scratch[1];
        }
        DecodeStageImages(vs, r.vs_flat, &r.vs_img);
        if (ps) DecodeStageImages(ps, r.ps_flat, &r.ps_img);
        r.vs = vs;
        r.vs_rt = vs_rt;
        r.ps_rt = ps_rt;
        r.ps = ps;
        if (cap::g_pass) {
            cap::NoteStage("vs", vs, vs_ud);
            if (ps) cap::NoteStage("ps", ps, ps_ud);
        }
        r.regs.Capture(regs);
        table_read.armed = false; // DrawEncode ends it
        encq::Defer([rec = rp.release()] {
            DrawEncode(*rec);
            g_draw_recs.Put(rec);
        });
    }
    return true;
}

// PS4_GNM_COMMIT_EACH=1 (debug): one command buffer per draw or dispatch, so a failing or
// hanging command buffer (PS4_GNM_HANG_REPORT_MS, PS4_GNM_ENCODER_STATUS) names one shader.
void CommitEachForDebug() {
    static const bool each = getenv("PS4_GNM_COMMIT_EACH") != nullptr;
    if (each && MainCmdOpen()) Commit();
}

void DrawEncode(DrawRec& r) {
    const AliasJob alias_job;
    const TableReadEncoded table_read(r.table_snap);
    CommitEachForDebug();
    if (S.hung) return;
    cap::EncodeScope cap_scope;
    DrainAsyncPsos();
    const DrawArgs& args = r.args;
    RenderState& rs = r.rs;
    const RegSnap& regs = r.regs;
    const bool trace = r.trace;
    Shader* const vs = r.vs;
    Shader* const ps = r.ps;
    const uint64_t vs_addr = r.vs_addr, ps_addr = r.ps_addr;
    const uint32_t* const vs_ud = r.vs_ud;
    const uint32_t* const ps_ud = r.ps_ud;
    const bool indirect = args.indirect_addr != 0;
    char desc[512];
    StageImagesScope images(&r.vs_img, &r.ps_img, nullptr);
    {

        PassKey key;
        id<MTLTexture> hold[9] = {};
        uint32_t color_fmt[8] = {}, write_mask[8] = {};
        ClearReq clr;
        MetaTakeUndo meta_undo; // the fast clears taken below, given back if the pass does not begin
        uint32_t rt_w = 16384, rt_h = 16384, samples = 0;
        bool any_rt = false;
        const uint32_t mrt_mask = ps ? ps->info.mrt_mask : 0;
        for (int i = 0; i < 8; ++i) {
            const ColorTarget& c = rs.color[i];
            if (!c.enabled || !(mrt_mask & (1u << i))) continue;
            if (!c.mtl_format) {
                RLog("rt-fmt", "%s: color target %d format %u/%u unsupported", args.name, i, c.raw_format, c.raw_number_type);
                continue;
            }
            if (c.fast_clear || c.compression) RegisterMeta(c.cmask_address);
            id<MTLTexture> t = S.tex->GetColorTarget(c);
            if (!t) continue;
            if (samples && t.sampleCount != samples) {
                RLog("rt-samples", "%s: mixed sample counts", args.name);
                continue;
            }
            samples = (uint32_t)t.sampleCount;
            key.color[i] = t;
            hold[i] = t;
            color_fmt[i] = (uint32_t)t.pixelFormat;
            write_mask[i] = c.mtl_write_mask;
            rt_w = std::min<uint32_t>(rt_w, (uint32_t)t.width);
            rt_h = std::min<uint32_t>(rt_h, (uint32_t)t.height);
            any_rt = true;
            uint64_t clear_seq = 0;
            if (c.fast_clear && TakeMetaCleared(c.cmask_address, &clear_seq)) {
                meta_undo.Add(c.cmask_address, 0, clear_seq);
                if (ClearColorFor(t.pixelFormat, c.clear_word0, c.clear_word1, &clr.color_value[i])) {
                    clr.color[i] = true;
                    NoteClearedColor(c, t, clear_seq);
                }
            }
        }
        uint32_t depth_fmt = 0;
        const DepthTarget& db = rs.depth;
        const bool want_depth = rs.ds.depth_test || rs.ds.stencil_test;
        if (trace)
            fprintf(stderr, "[gnm-trace]    AA config 0x%08x locs %08x %08x %08x %08x %08x %08x %08x %08x mask %08x %08x\n",
                    regs[0xA2F8], regs[0xA2FE], regs[0xA2FF], regs[0xA302], regs[0xA303], regs[0xA306], regs[0xA307],
                    regs[0xA30A], regs[0xA30B], regs[0xA30E], regs[0xA30F]);
        if (trace && ((regs[0xA000] & 0xC) || regs[0xA012] != regs[0xA014]))
            fprintf(stderr, "[gnm-trace]    DB copy: render_control 0x%08x z_info 0x%08x z_read 0x%llx z_write 0x%llx eqaa 0x%08x\n",
                    regs[0xA000], regs[0xA010], (unsigned long long)regs[0xA012] << 8,
                    (unsigned long long)regs[0xA014] << 8, regs[0xA201]);
        if (want_depth && db.bound && db.mtl_format) {
            if (db.htile_enable) RegisterMeta(db.htile_address);
            id<MTLTexture> grown_from = nil;
            id<MTLTexture> t = S.tex->GetDepthTarget(db, &grown_from);
            if (t && grown_from && BackendHas(kGnmCapCopies, "copy of a grown depth array's slices")) {
                // The array grew (a view selected a later slice): keep the slices written so far.
                EndEncoders();
                EnsureCmd();
                S.be->BeginCopies();
                for (NSUInteger sl = 0; sl < grown_from.arrayLength && sl < t.arrayLength; ++sl)
                    S.be->CopyTextureRegion(grown_from, (uint32_t)sl, 0, MTLOriginMake(0, 0, 0),
                                            MTLSizeMake(t.width, t.height, 1), t, (uint32_t)sl, 0, MTLOriginMake(0, 0, 0));
                S.be->EndEncoder();
            }
            if (trace)
                fprintf(stderr, "[gnm-trace]    depth target z=0x%llx s=0x%llx %ux%u zfmt %u sfmt %u samples %u htile %d (0x%llx) "
                                "func %u test %d write %d clear %d -> %s\n",
                        (unsigned long long)db.z_read_address, (unsigned long long)db.stencil_read_address, db.pitch,
                        db.height, (uint32_t)db.z_format, (uint32_t)db.stencil_format, db.num_samples, db.htile_enable,
                        (unsigned long long)db.htile_address, (regs[0xA200] >> 4) & 7, (regs[0xA200] >> 1) & 1,
                        (regs[0xA200] >> 2) & 1, db.depth_clear_enable, t ? "ok" : "nil");
            if (trace)
                fprintf(stderr, "[gnm-trace]    stencil enable %u depth_control 0x%08x stencil_control 0x%08x refmask 0x%08x 0x%08x clear %d\n",
                        regs[0xA200] & 1, regs[0xA200], regs[0xA10B], regs[0xA10C], regs[0xA10D], db.stencil_clear_enable);
            if (t && (!samples || t.sampleCount == samples)) {
                SyncDepthSibling(db, t);
                samples = (uint32_t)t.sampleCount;
                key.depth = t;
                key.depth_slice = t.textureType == MTLTextureType2DArray
                                      ? std::min<uint32_t>(db.base_slice, (uint32_t)t.arrayLength - 1)
                                      : 0;
                hold[8] = t;
                depth_fmt = (uint32_t)t.pixelFormat;
                rt_w = std::min<uint32_t>(rt_w, (uint32_t)t.width);
                rt_h = std::min<uint32_t>(rt_h, (uint32_t)t.height);
                any_rt = true;
                if (db.stencil_read_address) S.stencil_to_z[db.stencil_read_address] = db.z_read_address;
                const uint32_t dc = regs[0xA200];
                const bool depth_enable = (dc >> 1) & 1, depth_write = (dc >> 2) & 1;
                if (depth_write || rs.ds.stencil_test || db.depth_clear_enable || db.stencil_clear_enable)
                    BumpDepthVersion(t);
                uint64_t htile_seq = 0;
                const uint32_t htile_slice = std::min<uint32_t>(db.base_slice, 63);
                const bool htile_clear =
                    db.htile_enable && TakeMetaCleared(db.htile_address, &htile_seq, htile_slice, HtileSliceBytesOf(db));
                if (htile_clear) meta_undo.Add(db.htile_address, htile_slice, htile_seq);
                if ((db.depth_clear_enable && depth_enable && depth_write) || htile_clear) {
                    clr.depth = true;
                    clr.depth_value = db.depth_clear;
                    // a clear writes every depth byte of the bound slice (the stencil plane is its
                    // own surface on the PS4); only that slice is credited
                    const uint64_t za = db.z_read_address ? db.z_read_address : db.stencil_read_address;
                    // the register clear belongs to this draw; an HTILE clear to its fill
                    const uint64_t seq = (db.depth_clear_enable && depth_enable && depth_write) ? S.committed_seq + 1
                                                                                                 : htile_seq;
                    if (db.z_read_address && db.slice_bytes && seq)
                        S.tex->NoteExactWrite(za, t, za + (uint64_t)db.base_slice * db.slice_bytes,
                                              za + (uint64_t)(db.base_slice + 1) * db.slice_bytes, seq);
                }
                if (db.stencil_clear_enable && HasStencil(t.pixelFormat)) {
                    clr.stencil = true;
                    clr.stencil_value = db.stencil_clear;
                }
            }
        }
        if (!samples) samples = 1;
        const uint32_t aa_samples = 1u << (regs[0xA2F8] & 7); // PA_SC_AA_CONFIG.MSAA_NUM_SAMPLES
        static const bool eqaa_twins = getenv("PS4_GNM_EQAA_VIEWPORT") == nullptr;
        if (eqaa_twins && aa_samples > 1 && aa_samples <= 4 && samples == 1 && key.depth && !rs.ds.stencil_test &&
            S.dev.programmableSamplePositionsSupported) {
            // EQAA: draw into multisampled stand-ins (see EqaaTwin)
            bool ok = true;
            id<MTLTexture> tw[9] = {};
            for (int i = 0; i < 9 && ok; ++i) {
                id<MTLTexture> t = i < 8 ? key.color[i] : key.depth;
                if (!t) continue;
                if (t.textureType != MTLTextureType2D) ok = false;
                else if (!(tw[i] = EqaaTwin(t, aa_samples))) ok = false;
            }
            if (ok) {
                for (int i = 0; i < 8; ++i)
                    if (tw[i]) key.color[i] = hold[i] = tw[i];
                key.depth = hold[8] = tw[8];
                samples = aa_samples;
            }
        }
        // MSAA passes use the game's sample locations (PA_SC_AA_SAMPLE_LOCS)
        if (samples > 1 && aa_samples > 1) key.sample_locs = regs[0xA2FE];
        if (!any_rt) {
            // UAV-only rendering: size the pass from the screen scissor
            rt_w = std::max(1u, std::min(16384u, rs.scissor.x + rs.scissor.width));
            rt_h = std::max(1u, std::min(16384u, rs.scissor.y + rs.scissor.height));
        }
        key.width = any_rt ? 0 : rt_w;
        key.height = any_rt ? 0 : rt_h;

        // ---- vertex inputs (shadPS4 GraphicsPipeline::GetVertexInputs / BindVertexBuffers)
        // reused from draw to draw (only the encoder runs DrawEncode)
        static std::vector<VertexAttr>& vattrs = *new std::vector<VertexAttr>;
        static std::vector<uint32_t>& vlocs = *new std::vector<uint32_t>;
        static std::vector<std::pair<id<MTLBuffer>, uint64_t>>& vbufs = *new std::vector<std::pair<id<MTLBuffer>, uint64_t>>;
        vattrs.clear();
        vlocs.clear();
        vbufs.clear();
        const uint32_t vb_first = vs->info.msl_first_free_buffer;
        for (uint32_t i = 0; i < vs->attrs.size(); ++i) {
            const gcn_vertex_attrib& a = vs->attrs[i];
            const BufferDesc& bd = r.vtx[i].bd; // read and checked by Draw
            const VertexFormatInfo& vf = r.vtx[i].vf;
            const uint32_t slot = vb_first + i;
            VertexAttr va{};
            va.format = vf.mtl_vertex_format;
            va.offset = 0;
            va.buffer = slot;
            uint32_t stride = bd.stride;
            id<MTLBuffer> mb = S.dummy;
            uint64_t off = 0;
            if (bd.address && bd.size) {
                if (!GuestBuffer(bd.address & ~3ull, bd.size + 4, GuestAccess::Read, &mb, &off)) {
                    mb = S.dummy;
                    off = 0;
                }
            }
            if (a.step_rate == 0) {
                va.step_fn = stride ? MTLVertexStepFunctionPerVertex : MTLVertexStepFunctionConstant;
                va.step_rate = stride ? 1 : 0;
            } else {
                va.step_fn = MTLVertexStepFunctionPerInstance;
                va.step_rate = std::max(1u, a.divisor);
            }
            if (stride == 0) va.step_fn = MTLVertexStepFunctionConstant, va.step_rate = 0;
            va.stride = stride;
            vattrs.push_back(va);
            vlocs.push_back(a.location);
            vbufs.push_back({mb, off});
        }

        // ---- pipeline
        const bool a2c = (regs[0xA2DC] & 1) != 0; // DB_ALPHA_TO_MASK
        // RectList (ps4_gnm_rectlist.h): the VS's capture variant with the draw's vertex inputs
        // and no rasterization, then the expansion VS with the PS and the draw's state.
        const Shader::RectList* rect =
            rs.raster.prim == PrimitiveType::RectList && !indirect ? vs->rect : nullptr;
        id<MTLRenderPipelineState> rect_capture = nil;
        if (rect) {
            RenderState crs = rs;
            crs.raster.rasterizer_discard = true;
            rect_capture = GetGfxPipeline(rect->capture, nullptr, crs, color_fmt, write_mask, depth_fmt, samples,
                                          vattrs, vlocs, false);
            if (!rect_capture) {
                if (PipelineCompiling()) {
                    NoteDrawPendingCompile();
                    g_hitch.pending_draws++;
                } else {
                    NoteSkip(SkipReason::PipelineFailed);
                }
                return;
            }
        }
        static const std::vector<VertexAttr>& no_attrs = *new std::vector<VertexAttr>;
        static const std::vector<uint32_t>& no_locs = *new std::vector<uint32_t>;
        id<MTLRenderPipelineState> pso =
            GetGfxPipeline(rect ? rect->expand : vs, ps, rs, color_fmt, write_mask, depth_fmt, samples,
                           rect ? no_attrs : vattrs, rect ? no_locs : vlocs, a2c);
        if (!pso) {
            if (PipelineCompiling()) {
                NoteDrawPendingCompile();
                g_hitch.pending_draws++;
            } else {
                NoteSkip(SkipReason::PipelineFailed); // its build failed (logged)
            }
            return;
        }

        // ---- viewport / scissor
        MTLViewport vp;
        MTLScissorRect sc;
        const uint32_t pw = any_rt ? rt_w : key.width, ph = any_rt ? rt_h : key.height;
        if (rs.num_viewports > 0) {
            const Viewport& v = rs.viewports[0];
            if (v.flip_y) RLog("flip-y", "%s: positive viewport yscale (y flip) not supported; drawn mirrored", args.name);
            vp = ToMTLViewport(v);
            vp.znear = std::min(1.0, std::max(0.0, vp.znear));
            vp.zfar = std::min(1.0, std::max(0.0, vp.zfar));
            sc = ToMTLScissor(rs.scissors[0], pw, ph);
        } else {
            vp = MTLViewport{0, 0, (double)pw, (double)ph, 0, 1};
            sc = ToMTLScissor(rs.scissor, pw, ph);
        }
        // EQAA (multisampled rasterization, single-sample targets): coverage and depth are
        // evaluated at the location of sample 0, so rasterize the single-sample pass there
        // (Metal has no custom sample positions for single-sample passes).
        if (aa_samples > 1 && samples == 1) {
            const uint32_t b = regs[0xA2FE] & 0xFF;
            const int sx = (int)((b & 0xF) ^ 8) - 8, sy = (int)(((b >> 4) & 0xF) ^ 8) - 8;
            vp.originX -= sx / 16.0;
            vp.originY -= sy / 16.0;
        }
        if (sc.width == 0 || sc.height == 0) {
            RLog("scissor-empty", "%s: empty scissor (%u,%u %ux%u) on %ux%u target; skipped", args.name,
                 rs.scissor.x, rs.scissor.y, rs.scissor.width, rs.scissor.height, pw, ph);
            NoteSkip(SkipReason::ScissorEmpty);
            return;
        }

        // ---- index data / primitive emulation
        MTLPrimitiveType prim = (MTLPrimitiveType)rs.raster.mtl_primitive;
        id<MTLBuffer> ib = nil;
        uint64_t ib_off = 0;
        uint64_t ib_len = 0; // index bytes readable at ib_off
        uint32_t draw_count = args.count;
        MTLIndexType itype = args.index_type == 1 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
        bool indexed = args.indexed;
        id<MTLBuffer> arg_buf = nil;
        uint64_t arg_off = 0;
        if (indirect) {
            const uint64_t isz = args.index_type == 1 ? 4 : 2;
            if (rs.raster.needs_emulation ||
                !GuestBuffer(args.indirect_addr, indexed ? 20 : 16, GuestAccess::Read, &arg_buf, &arg_off) || (arg_off & 3))
                { NoteSkip(SkipReason::IndirectArgs); return; }
            if (indexed &&
                (!args.index_base || !GuestBuffer(args.index_base, (uint64_t)args.max_index * isz, GuestAccess::Read, &ib, &ib_off) ||
                 (ib_off & 3)))
                { NoteSkip(SkipReason::IndirectArgs); return; }
            if (indexed) ib_len = (uint64_t)args.max_index * isz;
        } else if (rs.raster.needs_emulation && !rect) {
            if (rs.raster.prim == PrimitiveType::QuadList) {
                size_t aoff;
                if (!BuildQuadListIndices(args, args.count, &ib, &aoff, &draw_count)) {
                    NoteSkip(SkipReason::QuadIndices);
                    return;
                }
                ib_off = aoff;
                ib_len = (uint64_t)draw_count * 4;
                itype = MTLIndexTypeUInt32;
                indexed = true;
                prim = MTLPrimitiveTypeTriangle;
            } else {
                RLog("prim", "%s: primitive type %s not supported yet; skipped (VS 0x%llx PS 0x%llx n=%u)", args.name,
                     NameOf(rs.raster.prim), (unsigned long long)vs_addr, (unsigned long long)ps_addr, args.count);
                NoteSkip(SkipReason::Primitive);
                return;
            }
        } else if (indexed) {
            const uint64_t isz = args.index_type == 1 ? 4 : 2;
            const uint64_t addr = args.index_base + (uint64_t)args.index_offset * isz;
            const uint64_t bytes = (uint64_t)args.count * isz;
            if (!args.index_base || !GuestBuffer(addr, bytes, GuestAccess::Read, &ib, &ib_off)) {
                RLog("ib", "%s: index buffer 0x%llx unavailable", args.name, (unsigned long long)addr);
                NoteSkip(SkipReason::IndexBuffer);
                return;
            }
            if (ib_off & 3) { // Metal wants 4 byte aligned index buffer offsets
                void* ptr;
                size_t aoff;
                if (!S.arena.Alloc(S.dev, bytes, 16, &ib, &aoff, &ptr)) {
                    NoteSkip(SkipReason::Arena);
                    return;
                }
                memcpy(ptr, S.ctx->mem_base + addr, bytes);
                ib_off = aoff;
            }
            ib_len = bytes;
        }
        const uint32_t inst = std::max(1u, args.num_instances);
        ps4gnm_rect::Buffers rect_bufs{};
        if (rect) {
            // capture index of every vertex, and room for every captured vertex
            const uint32_t n = args.count;
            if (n % 3) {
                RLog("rect-count", "%s: RectList with %u vertices (not 3 per rectangle); skipped", args.name, n);
                NoteSkip(SkipReason::RectCount);
                return;
            }
            const uint8_t* src = nullptr;
            if (indexed) {
                const uint64_t isz = args.index_type == 1 ? 4 : 2;
                src = S.ctx->mem_base + args.index_base + (uint64_t)args.index_offset * isz; // checked above
            }
            void* sp;
            void* pp;
            void* vp_;
            size_t soff, poff, voff;
            if (!S.arena.Alloc(S.dev, (size_t)n * 4, 16, &rect_bufs.slots, &soff, &sp) ||
                !S.arena.Alloc(S.dev, 4, 16, &rect_bufs.params, &poff, &pp)) {
                NoteSkip(SkipReason::Arena);
                return;
            }
            const uint32_t per_instance = ps4gnm_rect::BuildSlots(src, args.index_type == 1, n, (uint32_t*)sp);
            *(uint32_t*)pp = per_instance;
            const uint64_t bytes = (uint64_t)per_instance * inst * rect->vertex_stride;
            if (bytes > (64u << 20)) {
                RLog("rect-size", "%s: RectList of %u vertices x %u instances needs %llu bytes; skipped", args.name, n,
                     inst, (unsigned long long)bytes);
                NoteSkip(SkipReason::RectSize);
                return;
            }
            if (!S.arena.Alloc(S.dev, (size_t)bytes, 16, &rect_bufs.vertices, &voff, &vp_)) {
                RLog("rect-arena", "%s: no arena memory for the %llu bytes of a RectList capture; skipped", args.name,
                     (unsigned long long)bytes);
                NoteSkip(SkipReason::Arena);
                return;
            }
            rect_bufs.slots_off = soff;
            rect_bufs.params_off = poff;
            rect_bufs.vertices_off = voff;
            rect_bufs.capture_slot = rect->capture_slot;
            rect_bufs.params_slot = rect->params_slot;
        }

        // ---- vertex / instance offsets (shadPS4 GetDrawOffsets)
        uint32_t vertex_offset = regs[0xA102]; // VGT_INDX_OFFSET
        uint32_t instance_offset = 0;
        if (vs->info.has_fetch_shader) {
            if (vertex_offset == 0 && vs->info.vertex_offset_sgpr >= 0 && vs->info.vertex_offset_sgpr < 16)
                vertex_offset = vs_ud[vs->info.vertex_offset_sgpr];
            if (vs->info.instance_offset_sgpr >= 0 && vs->info.instance_offset_sgpr < 16)
                instance_offset = vs_ud[vs->info.instance_offset_sgpr];
        }

        // ---- encode
        std::vector<uint32_t>& vs_flat = r.vs_flat;
        std::vector<uint32_t>& ps_flat = r.ps_flat;
        for (int i = 0; i < 8; ++i)
            if (key.color[i]) RefreshGuestNewer(rs.color[i].address, key.color[i], true);
        if (cap::g_pass) cap::NoteTargets(key, rs, clr.depth, clr.stencil);
        const TableStage table_stages[2] = {{vs, vs_ud, &r.vs_rt, &vs_flat, &r.vs_img},
                                            {ps, ps_ud, &r.ps_rt, &ps_flat, &r.ps_img}};
        if (!ResolveTableHazards(table_stages, 2, r.table_snap)) {
            cap::SetSkipped(kCapTableHazardSkip);
            NoteSkip(SkipReason::TableHazard);
            return;
        }
        // the textures the draw samples. PS4_GNM_MTL4_ONLY_CLEARS skips them: the draw is skipped, and
        // the depth companions are renderer textures the Metal 4 backend does not keep resident yet.
        // Their shared bookkeeping (GmApplyInvalidations, RefreshGuestNewer of sampled surfaces) is
        // not lost: the next site that calls it does it (e.g. the render-target refresh above).
        if (!g_only_clears) {
            ResolveTextureHazards(vs, vs_flat);
            if (ps) ResolveTextureHazards(ps, ps_flat);
            PrepareDepthCompanions(vs, vs_flat);
            if (ps) PrepareDepthCompanions(ps, ps_flat);
        }
        if (!BeginPass(key, clr, hold)) {
            NoteSkip(SkipReason::PassBegin);
            return;
        }
        meta_undo.Keep(); // the pass's load actions hold the clears now
        if (g_only_clears) { // debug (Metal 4 without draws): the pass's clears happen, the draw does not
            S.n_clear_only++;
            return;
        }
        const uint64_t enc_serial = S.be->EncoderSerial();
        EncoderState* es = g_rstate.Tracks(enc_serial) ? &g_rstate : nullptr;
        if (!es || !es->Same(es->pso, (__bridge const void*)pso)) S.be->SetRenderPipeline(pso);
        const bool has_depth = depth_fmt != 0;
        const bool has_stencil = has_depth && HasStencil((MTLPixelFormat)depth_fmt);
        static const int dbg_nodepth = getenv("PS4_GNM_DBG_NODEPTH") ? atoi(getenv("PS4_GNM_DBG_NODEPTH")) : 0;
        id<MTLDepthStencilState> dss;
        if (dbg_nodepth) {
            DepthStencilState d0 = rs.ds;
            d0.depth_test = false;
            d0.depth_write = false;
            d0.stencil_test = false;
            dss = GetDepthStencil(d0, has_depth, has_stencil);
        } else {
            dss = GetDepthStencil(rs.ds, has_depth, has_stencil);
        }
        if (!es || !es->Same(es->dss, (__bridge const void*)dss)) S.be->SetDepthStencil(dss);
        {
            EncoderState::Fixed f;
            memset(&f, 0, sizeof(f)); // padding is compared
            f.vp = vp;
            f.sc = sc;
            f.stencil_ref[0] = rs.ds.front.reference;
            f.stencil_ref[1] = rs.ds.back.reference;
            f.cull = dbg_nodepth >= 2 ? (uint32_t)MTLCullModeNone : (uint32_t)rs.raster.cull_mode;
            f.winding = (uint32_t)rs.raster.winding;
            f.fill = (uint32_t)rs.raster.fill_mode;
            f.clip = rs.raster.depth_clip ? (uint32_t)MTLDepthClipModeClip : (uint32_t)MTLDepthClipModeClamp;
            if (rs.raster.depth_bias) {
                f.bias[0] = rs.raster.bias_constant;
                f.bias[1] = rs.raster.bias_slope;
                f.bias[2] = rs.raster.bias_clamp;
            }
            for (int i = 0; i < 4; ++i) f.blend[i] = rs.blend_constants[i];
            const bool known = es && es->fixed_gen == es->gen;
            const EncoderState::Fixed* o = known ? &es->fixed : nullptr;
            uint32_t changed = 0;
            if (has_stencil && (!o || !es->fixed_stencil || memcmp(o->stencil_ref, f.stencil_ref, sizeof(f.stencil_ref))))
                changed |= kGnmFixedStencilRef;
            if (!o || memcmp(&o->vp, &f.vp, sizeof(f.vp))) changed |= kGnmFixedViewport;
            if (!o || memcmp(&o->sc, &f.sc, sizeof(f.sc))) changed |= kGnmFixedScissor;
            if (!o || o->cull != f.cull) changed |= kGnmFixedCull;
            if (!o || o->winding != f.winding) changed |= kGnmFixedWinding;
            if (!o || o->fill != f.fill) changed |= kGnmFixedFill;
            if (!o || o->clip != f.clip) changed |= kGnmFixedClip;
            if (!o || memcmp(o->bias, f.bias, sizeof(f.bias))) changed |= kGnmFixedBias;
            if (!o || memcmp(o->blend, f.blend, sizeof(f.blend))) changed |= kGnmFixedBlend;
            if (changed) S.be->SetFixed(f, changed); // sent in the order of the checks above
            if (es) {
                if (!has_stencil && known && es->fixed_stencil) {
                    // keep the reference values last set on the encoder
                    f.stencil_ref[0] = es->fixed.stencil_ref[0];
                    f.stencil_ref[1] = es->fixed.stencil_ref[1];
                }
                es->fixed_stencil = has_stencil || (known && es->fixed_stencil);
                es->fixed = f;
                es->fixed_gen = es->gen;
            }
        }

        const gcn_push_data push = MakePushData(regs);
        BindCtx bc{&regs, 1};
        std::string why;
        Binder vb{.stage = Stage::Vertex, .serial = enc_serial};
        S.buffer_stores.clear();
        if (!BindStage(vs, vs_ud, vs_flat, vb, bc, push, &why)) {
            RLog("bind", "%s: VS bind failed: %s", args.name, why.c_str());
            NoteSkip(SkipReason::VsBind);
            return;
        }
        if (ps) {
            Binder fb{.stage = Stage::Fragment, .serial = enc_serial};
            if (!BindStage(ps, ps_ud, ps_flat, fb, bc, push, &why)) {
                RLog("bind", "%s: PS bind failed: %s", args.name, why.c_str());
                NoteSkip(SkipReason::PsBind);
                return;
            }
        }
        // BindStage may have flushed (texture hazard) and closed the encoder
        if (S.be->OpenEncoder() != GnmEncoder::Render || S.be->EncoderSerial() != enc_serial) {
            RLog("reencode", "%s: encoder restarted after a hazard flush; skipped this draw", args.name);
            NoteSkip(SkipReason::Reencode);
            return;
        }
        for (size_t i = 0; i < vbufs.size(); ++i) vb.Buffer(vbufs[i].first, vbufs[i].second, vattrs[i].buffer);

        GnmDraw d;
        d.prim = prim;
        d.index_type = itype;
        d.index_buffer = ib;
        d.index_offset = ib_off;
        d.index_length = ib_len;
        if (indirect) {
            d.kind = indexed ? GnmDrawKind::IndexedIndirect : GnmDrawKind::Indirect;
            d.indirect_buffer = arg_buf;
            d.indirect_offset = arg_off;
            S.be->Draw(d);
        } else if (rect) {
            ps4gnm_rect::Geometry g;
            g.count = args.count;
            g.instances = inst;
            g.base_instance = instance_offset;
            g.vertex_start = vertex_offset;
            g.base_vertex = (int32_t)vertex_offset;
            g.index_buffer = indexed ? ib : nil;
            g.index_offset = ib_off;
            g.index_type = itype;
            g.index_length = ib_len;
            ps4gnm_rect::Encode(*S.be, rect_capture, pso, g, rect_bufs);
            if (es) { // Encode bound these vertex buffers itself; the pipeline it leaves is pso
                for (uint32_t k : {0u, 1u, 2u, rect_bufs.capture_slot, rect_bufs.params_slot})
                    es->buf[(int)Stage::Vertex][k].gen = 0;
            }
        } else if (indexed) {
            d.kind = GnmDrawKind::Indexed;
            d.count = draw_count;
            d.instances = inst;
            d.base_vertex = (int32_t)vertex_offset;
            d.base_instance = instance_offset;
            S.be->Draw(d);
        } else {
            d.vertex_start = vertex_offset;
            d.count = draw_count;
            d.instances = inst;
            d.base_instance = instance_offset;
            S.be->Draw(d);
        }
        S.pending = true;
        S.n_draws++;
        FlushBufferStores(); // the draw is encoded: its buffer stores happen
        if (cap::g_pass) cap::Encoded(args.indirect_addr);
        {
            WorkRec wr;
            wr.what = args.name;
            wr.a = vs_addr;
            wr.b = ps_addr;
            wr.n = args.count;
            wr.inst = inst;
            LogWork(wr);
        }
        if (trace)
            snprintf(desc, sizeof(desc),
                     "%s VS=0x%llx PS=0x%llx prim=%s n=%u inst=%u rt0=%s 0x%llx %ux%u depth=%s vp=(%.0f,%.0f %.0fx%.0f) sc=(%lu,%lu %lux%lu)%s%s",
                     args.name, (unsigned long long)vs_addr, (unsigned long long)ps_addr, NameOf(rs.raster.prim),
                     args.count, inst, key.color[0] ? "yes" : "no", (unsigned long long)rs.color[0].address,
                     key.color[0] ? (uint32_t)key.color[0].width : 0, key.color[0] ? (uint32_t)key.color[0].height : 0,
                     key.depth ? "yes" : "no", vp.originX, vp.originY, vp.width, vp.height,
                     (unsigned long)sc.x, (unsigned long)sc.y, (unsigned long)sc.width, (unsigned long)sc.height,
                     clr.any() ? " CLEAR" : "", rs.raster.clip_disabled ? " clipdis" : "");
        static const bool dump_draws = getenv("PS4_GNM_DUMP_DRAWS") != nullptr;
        static const uint64_t dump_rt = getenv("PS4_GNM_DUMP_DRAWS_RT") ? strtoull(getenv("PS4_GNM_DUMP_DRAWS_RT"), nullptr, 0) : 0;
        if (dump_draws && key.color[0] && (!dump_rt || dump_rt == rs.color[0].address)) {
            char what[64];
            snprintf(what, sizeof(what), "ps%llx_rt%llx", (unsigned long long)ps_addr, (unsigned long long)rs.color[0].address);
            DumpAfterWork(key.color[0], what);
        }
        if (trace) {
            fprintf(stderr, "[gnm-trace] %s\n", desc);
            if (indirect) {
                uint32_t a[5] = {};
                ReadGuest(S.ctx, args.indirect_addr, a, sizeof(a));
                fprintf(stderr, "[gnm-trace]    indirect 0x%llx at encode: count %u inst %u first %u base %u\n",
                        (unsigned long long)args.indirect_addr, a[0], a[1], a[2], a[3]);
                char what[64];
                snprintf(what, sizeof(what), "VS=0x%llx PS=0x%llx", (unsigned long long)vs_addr, (unsigned long long)ps_addr);
                s_traced_indirect.emplace_back(args.indirect_addr, what);
            }
            for (int i = 0; i < 8; ++i) {
                const ColorTarget& c = rs.color[i];
                if (!c.bound) continue;
                fprintf(stderr, "[gnm-trace]    MRT%d 0x%llx %ux%u s%u fmt %u/%u swap %u mask %x enabled %d ps_exports %d bound_tex %s blend %d\n",
                        i, (unsigned long long)c.address, c.pitch, c.height, c.num_samples, c.raw_format,
                        c.raw_number_type, c.comp_swap, c.target_mask, c.enabled, (mrt_mask >> i) & 1,
                        key.color[i] ? "yes" : "no", rs.blend[i].enable);
            }
        }
    }
    return;
}


// A dispatch prepared by the CP thread for the encoder thread.
struct DispatchRec {
    gcn_runtime_info rt{};
    uint64_t table_snap = 0; // TableReadBegin when the CP read guest memory for it
    RegSnap regs;
    uint64_t addr = 0;
    uint32_t ud[16] = {};
    Shader* cs = nullptr;
    uint32_t dim_x = 0, dim_y = 0, dim_z = 0;
    uint64_t indirect_addr = 0;
    const char* queue = "";
    std::vector<uint32_t> flat;
    StageImages img;
    // guest memory fast path (GmPlan): the ticket and the shader's predicted written pages
    uint64_t gm_ticket = 0;
    std::shared_ptr<const GmPages> gm_pred;
    // ready for the next dispatch (Dispatch() sets rt, addr, ud, regs), keeping the vectors
    void Recycle() {
        cs = nullptr;
        dim_x = dim_y = dim_z = 0;
        indirect_addr = 0;
        queue = "";
        flat.clear();
        img.Clear();
        gm_ticket = 0;
        gm_pred.reset();
    }
};
void DispatchEncode(DispatchRec*& rp);
RecPool<DispatchRec> g_dispatch_recs;

namespace { // with the declarations of the guest memory section above
// ---- Guest memory dispatches: encoding, the sync and fast paths, tickets (see above) ----
void PutDispatchRec(DispatchRec* r) { g_dispatch_recs.Put(r); }

constexpr int kGmEncoded = 0, kGmFinal = 1, kGmMiss = 2; // GmTicket::state

struct GmTicket {
    uint64_t t = 0;
    GmSlot slot;                // no buffer: a ticket consumed without a dispatch
    DispatchRec* rec = nullptr; // owned until GmFinish
    id<MTLComputePipelineState> pso = nil;
    MTLSize tg{};
    std::shared_ptr<const GmPages> pred;
    GmTextures textures; // what the dispatch bound (a re-run binds the same)
    std::vector<std::pair<uint64_t, uint64_t>> stores; // its pass's exact buffer stores (GmQueueStores once it ran)
    std::atomic<int> state{kGmEncoded};
    std::atomic<bool> failed{false}; // its command buffer failed or the GPU hung: counts as skipped
    bool rerun = false;       // re-run after a miss (res holds the re-run's result)
    bool ran = false;         // the re-run's pass ran
    bool pass_failed = false; // the pass could not be bound (EncodeCs counted it as skipped)
    bool withhold = false;    // tests (TestWithholdNextTicket): never made final
    double miss_at = 0;       // when the listener found it missed (CFAbsoluteTime)
    GmResult res;             // its result (read by the listener once final, or by the re-run)
    bool have_res = false;
};

void GmSignal(uint64_t v) {
    std::lock_guard<std::mutex> lk(g_gms.signal_mu);
    // given up on: the GPU's own 2t - 1 signals (of command buffers committed before) may have
    // lowered it; the waits on 2t of the work after them must still end
    if (g_gms.released.load(std::memory_order_acquire)) v = std::max<uint64_t>(v, 1ull << 62);
    if (g_gms.ev.signaledValue < v) g_gms.ev.signaledValue = v;
    if (g_gms.cpu_signaled.load(std::memory_order_relaxed) < v) g_gms.cpu_signaled.store(v, std::memory_order_release);
}

void GmCheckUnlockable() {
    if (!g_gmem.cur_slot && !g_gms.chain_member) return;
    fprintf(stderr, "[gnm-draw] FATAL: the encoder's state released while a guest memory dispatch is encoded "
                    "(fault buffer bound %d, chain member %d)\n", g_gmem.cur_slot != nil, (int)g_gms.chain_member);
    abort();
}

void GmLogState() {
    const GmTicket* k = g_gmem.tickets.empty() ? nullptr : g_gmem.tickets.front().get();
    fprintf(stderr, "[gnm-draw] guest memory tickets: event %llu (final up to %llu), issued %llu, encoded %llu, committed %llu, "
                    "%zu unfinished (oldest %llu, state %d), re-run due %d, chain open %d\n",
            g_gms.ev ? (unsigned long long)g_gms.ev.signaledValue : 0ull, (unsigned long long)GmResolved(),
            (unsigned long long)g_gms.issued.load(), (unsigned long long)g_gms.encoded, (unsigned long long)g_gms.committed,
            g_gmem.tickets.size(), k ? (unsigned long long)k->t : 0ull, k ? k->state.load() : -1, (int)g_gms.miss.load(),
            (int)g_gms.chain_open);
}

void GmReleaseAll() {
    g_gms.released.store(true, std::memory_order_release);
    if (g_gms.ev) GmSignal(1ull << 62);
    for (auto& k : g_gmem.tickets) {
        k->failed = true;
        k->state.store(kGmFinal, std::memory_order_release);
    }
    GmNotify();
}

// Completion handler of a failed command buffer (any thread).
void GmCommandBufferFailed(const std::vector<std::shared_ptr<GmTicket>>& tickets) {
    fprintf(stderr, "[gnm-draw] the failed command buffer had guest memory dispatches %llu..%llu (states %d..%d); "
                    "final up to %llu, a re-run pending: %d\n",
            (unsigned long long)tickets.front()->t, (unsigned long long)tickets.back()->t,
            tickets.front()->state.load(), tickets.back()->state.load(), (unsigned long long)GmResolved(),
            (int)g_gms.miss.load());
    for (auto& k : tickets) {
        if (k->state.load(std::memory_order_acquire) == kGmFinal) continue;
        k->failed = true;
        k->state.store(kGmFinal, std::memory_order_release);
    }
    if (!tickets.empty()) GmSignal(2 * tickets.back()->t);
    GmNotify();
}

// Binds and encodes one dispatch of r's shader: on `forced` (a re-run on the second queue) or on
// the open compute encoder (nil). args: indirect arguments to dispatch with instead of r's size
// (a guest memory pass: the gate's). tex_out: records the textures bound; tex_in: binds these
// textures instead of looking the images up.
bool EncodeCs(DispatchRec& r, id<MTLComputePipelineState> pso, MTLSize tg, id<MTLComputeCommandEncoder> forced,
              id<MTLBuffer> args, NSUInteger args_off, GmTextures* tex_out = nullptr,
              const GmTextures* tex_in = nullptr) {
    static const bool labels = getenv("PS4_GNM_LABELS") != nullptr;
    const RegSnap& regs = r.regs;
    // profiling aid: one encoder per dispatch, named after the shader
    if (!forced && labels && S.be->OpenEncoder() == GnmEncoder::Compute) S.be->EndEncoder();
    if (!forced) ComputeEncoder();
    const GnmComputeSink enc{S.be.get(), forced}; // forced: a queue2 encoder; nil: the backend's
    uint64_t serial = forced ? 0 : S.be->EncoderSerial();
    if (!forced && labels)
        S.be->SetEncoderLabel([NSString stringWithFormat:@"CS 0x%llx %ux%ux%u x %lux%lux%lu%s", (unsigned long long)r.addr, r.dim_x,
                                               r.dim_y, r.dim_z, (unsigned long)tg.width, (unsigned long)tg.height,
                                               (unsigned long)tg.depth, r.indirect_addr ? " indirect" : ""]);
    if (forced) [forced setComputePipelineState:pso];
    else if (!g_cstate.Tracks(serial) || !g_cstate.Same(g_cstate.pso, (__bridge const void*)pso))
        S.be->SetComputePipeline(pso);
    gcn_push_data push = MakePushData(regs);
    BindCtx bc{&regs, r.indirect_addr ? 1024u : r.dim_x * r.dim_y * r.dim_z};
    Binder b{.stage = Stage::Compute, .raw = forced, .serial = serial, .textures_out = tex_out, .textures_in = tex_in};
    std::string why;
    if (!forced) S.storage_bound.clear();
    S.buffer_stores.clear();
    if (cap::g_pass) cap::ForgetBindings(Stage::Compute);
    if (!BindStage(r.cs, r.ud, r.flat, b, bc, push, &why)) {
        RLog("bind", "%s CS 0x%llx bind failed: %s", r.queue, (unsigned long long)r.addr, why.c_str());
        NoteSkip(SkipReason::CsBind);
        return false;
    }
    if (!forced && (S.be->OpenEncoder() != GnmEncoder::Compute || S.be->EncoderSerial() != serial)) {
        // hazard flush inside BindStage
        ComputeEncoder();
        serial = S.be->EncoderSerial();
        if (!g_cstate.Tracks(serial) || !g_cstate.Same(g_cstate.pso, (__bridge const void*)pso))
            S.be->SetComputePipeline(pso);
        if (tex_out) tex_out->clear();
        Binder b2{.stage = Stage::Compute, .serial = serial, .textures_out = tex_out, .textures_in = tex_in};
        if (cap::g_pass) cap::ForgetBindings(Stage::Compute);
        S.buffer_stores.clear();
        if (!BindStage(r.cs, r.ud, r.flat, b2, bc, push, &why)) {
            RLog("bind", "%s CS 0x%llx bind failed: %s", r.queue, (unsigned long long)r.addr, why.c_str());
            NoteSkip(SkipReason::CsBind);
            return false;
        }
    }
    if (args) {
        enc.DispatchIndirect(args, args_off, tg);
    } else if (r.indirect_addr) {
        id<MTLBuffer> ab;
        uint64_t off;
        if (!GuestBuffer(r.indirect_addr, 12, GuestAccess::Read, &ab, &off)) {
            RLog("bind", "%s CS 0x%llx skipped: no buffer for its indirect arguments at 0x%llx", r.queue,
                 (unsigned long long)r.addr, (unsigned long long)r.indirect_addr);
            NoteSkip(SkipReason::CsIndirectArgs);
            return false;
        }
        enc.DispatchIndirect(ab, off, tg);
    } else {
        enc.Dispatch(MTLSizeMake(r.dim_x, r.dim_y, r.dim_z), tg);
    }
    if (!forced) S.pending = true;
    // the dispatch is encoded: its buffer stores happen. A guest memory dispatch's stores happen
    // only if its pass ran (the gate's verdict): GmRunSync and GmDispatchFast hand them on then.
    if (!r.cs->info.uses_guest_memory) FlushBufferStores();
    return true;
}

NSUInteger GmThreads(id<MTLComputePipelineState> pso) {
    return std::min<NSUInteger>(1024, pso.maxTotalThreadsPerThreadgroup);
}

// gm_gate for r's dispatch. pred: check the probe's written pages against it (fast path).
// refuse: refuse in any case (a captured frame: its inputs are stored between probe and pass).
bool GmEncodeGate(const GnmComputeSink& enc, const GmSlot& slot, const DispatchRec& r, const GmPages* pred,
                  bool refuse, id<MTLBuffer> prev) {
    struct {
        uint32_t dims[3];
        uint32_t flags;
        uint32_t npred;
    } p = {{r.dim_x, r.dim_y, r.dim_z}, 0, 0};
    id<MTLBuffer> ab = S.dummy;
    uint64_t aoff = 0;
    if (r.indirect_addr) {
        if (!GuestBuffer(r.indirect_addr, 12, GuestAccess::Read, &ab, &aoff)) {
            // no gate: no pass (counted as refused with the reason by the caller)
            g_gm_stats.no_indirect++;
            RLog("guestmem", "%s CS 0x%llx: no buffer for its indirect arguments at 0x%llx", r.queue,
                 (unsigned long long)r.cs->code_addr, (unsigned long long)r.indirect_addr);
            return false;
        }
        p.flags |= kGmGateIndirect;
    }
    if (pred) {
        p.flags |= kGmGateCheckWritten;
        p.npred = (uint32_t)pred->size();
    }
    if (refuse) p.flags |= kGmGateRefuse;
    if (prev) p.flags |= kGmGateChain;
    static const uint32_t none = 0;
    enc.Pipeline(g_gmem.gate_pso);
    enc.Buffer(0, slot.buf, 0);
    enc.Bytes(1, &p, sizeof(p));
    if (pred && !pred->empty()) enc.Bytes(2, pred->data(), pred->size() * sizeof(uint32_t));
    else enc.Bytes(2, &none, sizeof(none));
    enc.Buffer(3, ab, aoff);
    enc.Buffer(4, prev ? prev : slot.buf, 0);
    enc.Dispatch(MTLSizeMake(1, 1, 1), MTLSizeMake(GmThreads(g_gmem.gate_pso), 1, 1));
    return true;
}

void GmEncodeHarvest(const GnmComputeSink& enc, const GmSlot& slot) {
    enc.Pipeline(g_gmem.harvest_pso);
    enc.Buffer(0, slot.buf, 0);
    enc.Dispatch(MTLSizeMake(1, 1, 1), MTLSizeMake(GmThreads(g_gmem.harvest_pso), 1, 1));
}

// Where a guest memory dispatch is encoded: the main command buffer, or a command buffer of the second queue.
struct GmTarget {
    bool second = false;
    id<MTLCommandBuffer> cb = nil;
    id<MTLComputeCommandEncoder> enc = nil;
};

GnmComputeSink GmEncoder(GmTarget& t) {
    if (!t.second) {
        ComputeEncoder();
        return GnmComputeSink{S.be.get(), nil};
    }
    if (!t.enc) {
        if (!g_gmem.queue2) {
            g_gmem.queue2 = [S.dev newCommandQueue];
            g_gmem.queue2.label = @"gnm guest memory re-runs";
        }
        t.cb = [g_gmem.queue2 commandBuffer];
        t.cb.label = @"guest memory re-run";
        t.enc = [t.cb computeCommandEncoderWithDispatchType:MTLDispatchTypeConcurrent];
    }
    return GnmComputeSink{S.be.get(), t.enc};
}

// Probe, gate, pass and harvest of r's dispatch with `slot`. False: nothing was encoded (the
// probe could not be bound; logged). textures: filled with the probe's textures (the pass binds
// the same); replay: textures recorded earlier, bound by both (a re-run). pass_ok: false if the
// pass could not be bound (EncodeCs logged and counted it as skipped).
bool GmEncodeDispatch(GmTarget& t, DispatchRec& r, id<MTLComputePipelineState> pso, MTLSize tg, GmSlot& slot,
                      const GmPages* pred, bool refuse, bool* pass_ok, GmTextures* textures = nullptr,
                      const GmTextures* replay = nullptr, id<MTLBuffer> prev = nil) {
    *pass_ok = true;
    const auto barrier = [&](const GnmComputeSink& e) {
        e.Barrier();
        if (!t.second) S.n_cbarriers++, S.cbarriers_total++;
    };
    // the main command buffer first: creating it may wait in the command buffer throttle with the encoder's state
    // free (encq::Unlocked), which must not happen once the slot is bound or a chain member is
    // being encoded (GmCheckUnlockable)
    if (!t.second) EnsureCmd();
    GnmComputeSink enc = GmEncoder(t);
    barrier(enc); // the probe sees what the work before it wrote, as the pass does
    g_gmem.cur_slot = slot.buf;
    if (textures) textures->clear();
    if (!EncodeCs(r, pso, tg, enc.raw, nil, 0, replay ? nullptr : textures, replay)) {
        g_gmem.cur_slot = nil;
        return false;
    }
    const GmTextures* same = replay ? replay : textures;
    enc = GmEncoder(t);
    barrier(enc);
    const bool gate = GmEncodeGate(enc, slot, r, pred, refuse, prev);
    if (!t.second) g_cstate.Reset(S.be->EncoderSerial());
    if (gate) {
        barrier(enc);
        *pass_ok = EncodeCs(r, pso, tg, enc.raw, slot.buf, (kGmSum + kGmArgs) * sizeof(uint32_t), nullptr, same);
        enc = GmEncoder(t);
    }
    barrier(enc);
    GmEncodeHarvest(enc, slot);
    if (!t.second) g_cstate.Reset(S.be->EncoderSerial());
    g_gmem.cur_slot = nil;
    return true;
}

// Commits t and waits for it. False: the GPU failed or hung.
bool GmSubmitWait(GmTarget& t) {
    if (!t.second) {
        EndEncoders();
        Commit();
        WaitAll();
        return !S.hung;
    }
    if (!t.cb) return true;
    [t.enc endEncoding];
    id<MTLCommandBuffer> cb = t.cb;
    t.cb = nil;
    t.enc = nil;
    [cb addCompletedHandler:^(id<MTLCommandBuffer>) { GmNotify(); }];
    [cb commit];
    S.n_waits++, S.n_waits_total++;
    const double t0 = CFAbsoluteTimeGetCurrent();
    bool done;
    {
        std::unique_lock<std::mutex> lk(g_gms.mu);
        done = g_gms.cv.wait_for(lk, std::chrono::duration<double, std::milli>(S.gpu_timeout_ms),
                                 [&] { return cb.status >= MTLCommandBufferStatusCompleted; });
    }
    S.wait_ms += (CFAbsoluteTimeGetCurrent() - t0) * 1000.0;
    if (!done) {
        fprintf(stderr, "[gnm-draw] GPU watchdog: guest memory re-run did not complete in %.0f ms; "
                        "disabling GPU rendering\n", S.gpu_timeout_ms);
        S.hung = true;
        ps4_metal_screen_release_held(); // as in WaitAll's watchdog: held frames' flips complete
        GmReleaseAll();
        return false;
    }
    if (cb.status == MTLCommandBufferStatusError) {
        fprintf(stderr, "[gnm-draw] guest memory re-run failed: %s\n", cb.error.localizedDescription.UTF8String);
        return false;
    }
    return true;
}

// The dispatch, waited for: maps the pages its probe reports missing (res: the result of an
// earlier run, or empty) and runs it again until no page is missing (up to 8 times). True: the
// pass ran (res is its result; stores, if given: the pass's exact buffer stores, NoteBufferStore).
// False: skipped (logged, counted) or the GPU failed.
bool GmRunSync(bool second, DispatchRec& r, id<MTLComputePipelineState> pso, MTLSize tg, GmSlot& slot,
               GmResult* res, const GmTextures* replay = nullptr,
               std::vector<std::pair<uint64_t, uint64_t>>* stores = nullptr) {
    constexpr int kMaxRuns = 8;
    const bool capture = !second && cap::g_pass;
    for (int run = 0;; ++run) {
        if (!res->unmapped.empty()) {
            if (run == kMaxRuns) {
                RLog("guestmem", "%s CS 0x%llx skipped: guest pages still missing after %d probes", r.queue,
                     (unsigned long long)r.cs->code_addr, kMaxRuns);
                NoteSkip(SkipReason::GmPages);
                return false;
            }
            if (!GmMapPages(res->unmapped, r.queue, r.cs->code_addr)) {
                NoteSkip(SkipReason::GmPages);
                return false;
            }
        }
        GmTarget t;
        t.second = second;
        bool pass_ok;
        const bool encoded = GmEncodeDispatch(t, r, pso, tg, slot, nullptr, capture, &pass_ok, nullptr, replay);
        // the pass's stores (its EncodeCs bound last), noted only if the gate let it run
        std::vector<std::pair<uint64_t, uint64_t>> st;
        st.swap(S.buffer_stores);
        if (!encoded) {
            if (t.second) GmSubmitWait(t);
            return false;
        }
        if (!GmSubmitWait(t)) return false;
        GmReadSlot(slot, res);
        if (res->gate == 1) {
            if (!pass_ok) g_gm_stats.pass_failed++; // skipped (counted), nothing learned
            else if (stores) stores->swap(st);
            return pass_ok;
        }
        // what else a probe that met missing pages reported may come from values it read as 0
        // from them (a V# loaded from one, say): judged only once every page is mapped
        if (!res->unmapped.empty()) continue;
        if (res->status) {
            RLog("guestmem", "%s CS 0x%llx skipped: it accesses guest memory through %s", r.queue,
                 (unsigned long long)r.cs->code_addr, GuestStatusText(res->status).c_str());
            NoteSkip(SkipReason::GmStatus);
            return false;
        }
        if (capture && res->why == kGmWhyForced) {
            // a captured pass: its inputs are stored now, before the pass runs (mode 0, direct)
            auto accessed = GmRanges(res->read, GCN_GUEST_TRACK_SHIFT);
            const auto written = GmRanges(res->written, GCN_GUEST_TRACK_SHIFT);
            accessed.insert(accessed.end(), written.begin(), written.end());
            cap::NoteGuestMemory(accessed, written);
            slot.f[GCN_GUEST_MODE] = 0;
            g_gmem.cur_slot = slot.buf;
            const bool ok = EncodeCs(r, pso, tg, nil, nil, 0, nullptr, replay);
            st.clear();
            st.swap(S.buffer_stores);
            ComputeEncoder();
            const GnmComputeSink enc{S.be.get(), nil};
            enc.Barrier(); // memoryBarrierWithScope:Buffers on Metal 3
            GmEncodeHarvest(enc, slot);
            g_cstate.Reset(S.be->EncoderSerial());
            g_gmem.cur_slot = nil;
            GmTarget t2;
            if (!GmSubmitWait(t2)) return false;
            GmReadSlot(slot, res);
            res->gate = ok ? 1 : 0;
            if (ok && stores) stores->swap(st);
            return ok;
        }
        RLog("guestmem", "%s CS 0x%llx skipped: its indirect dispatch size cannot be read", r.queue,
             (unsigned long long)r.cs->code_addr);
        NoteSkip(SkipReason::GmIndirectSize);
        return false;
    }
}

// After a pass ran: page use, the stale sampled textures over the written pages (sync path; the
// fast path's were invalidated before its result was published, GmInvalidateOutside) and what
// the shader's prediction learns. pred: the fast path's prediction.
void GmAfterPass(const DispatchRec& r, const GmResult& res, const GmPages* pred) {
    TouchGuestPages(res);
    if (res.status) {
        // the probe predicted other pages than the pass accessed (an address depending on a
        // value the probe could not produce, e.g. an atomic's result)
        if (res.status & GCN_GUEST_STATUS_UNMAPPED) g_gmem.wrong_guesses++;
        RLog("guestmem", "%s CS 0x%llx: accesses through %s were not made (wrong probe guesses so far: %llu)", r.queue,
             (unsigned long long)r.cs->code_addr, GuestStatusText(res.status).c_str(),
             (unsigned long long)g_gmem.wrong_guesses);
    }
    if (!pred) {
        GmInvalidateOutside(res, nullptr);
    } else if (!GmSubset(res.written, *pred)) {
        // written outside the prediction (its probe could not know the pages): the CP's reads
        // after it waited for its result all the same (GmCpOrder); the prediction learns them
        g_gm_stats.mispredicted++;
        size_t n = 0;
        uint32_t first = 0;
        for (uint32_t p : res.written)
            if (!std::binary_search(pred->begin(), pred->end(), p)) {
                if (!n++) first = p;
            }
        char key[64];
        snprintf(key, sizeof(key), "guestmem-pred-%llx", (unsigned long long)r.cs->code_addr);
        RLog(key, "%s CS 0x%llx wrote %zu MiB outside its predicted pages (first 0x%llx); learned", r.queue,
             (unsigned long long)r.cs->code_addr, n, (unsigned long long)((uint64_t)first << GCN_GUEST_TRACK_SHIFT));
    }
    GmLearn(r.cs->uid, res.written);
}

// Sync path (the CP thread waits for it in encq::Run).
bool GmDispatchSync(DispatchRec& r, id<MTLComputePipelineState> pso, MTLSize tg) {
    g_gm_stats.sync++;
    GmSlot slot = GmAcquireSlot();
    if (!slot.buf) {
        g_gm_stats.no_slot++;
        RLog("guestmem", "%s CS 0x%llx skipped: no fault buffer", r.queue, (unsigned long long)r.cs->code_addr);
        NoteSkip(SkipReason::GmNoSlot);
        return false;
    }
    GmResult res;
    std::vector<std::pair<uint64_t, uint64_t>> stores;
    const bool ran = GmRunSync(false, r, pso, tg, slot, &res, nullptr, &stores);
    if (!S.hung) GmReleaseSlot(slot);
    if (ran) {
        GmAfterPass(r, res, nullptr);
        S.buffer_stores.swap(stores);
        FlushBufferStores(); // the pass ran: its buffer stores happened
    }
    return ran;
}

// The listener: the GPU ran a chain of tickets (2t - 1 of its last). Final up to the first one
// whose gate refused it for a re-run (an unmapped page, a written page outside the prediction,
// or a re-run of the dispatch before it); that one and those after it are re-run by the encoder.
void GmListenChain(const std::shared_ptr<std::vector<std::shared_ptr<GmTicket>>>& members) {
    std::shared_ptr<std::vector<std::shared_ptr<GmTicket>>> m = members;
    [g_gms.ev notifyListener:g_gms.listener
                     atValue:2 * m->back()->t - 1
                       block:^(id<MTLSharedEvent>, uint64_t) {
                           const CompletionScope gate;
                           if (!gate.in) return;
                           // members up to `end` (tests: a withheld one ends the chain unfinished)
                           size_t end = m->size(), miss;
                           for (size_t i = 0; i < m->size(); ++i)
                               if ((*m)[i]->withhold) {
                                   end = i;
                                   break;
                               }
                           miss = end;
                           for (size_t i = 0; i < end && miss == end; ++i) {
                               const GmTicket& k = *(*m)[i];
                               if (!k.slot.f) continue;
                               const uint32_t* s = k.slot.f + kGmSum;
                               if (s[kGmGate] == 2 && (s[kGmWhy] & kGmWhyRerun)) miss = i;
                           }
                           const double now = CFAbsoluteTimeGetCurrent();
                           for (size_t i = 0; i < end; ++i) {
                               GmTicket& k = *(*m)[i];
                               if (i < miss && k.slot.f) {
                                   GmReadSlot(k.slot, &k.res);
                                   k.have_res = true;
                                   if (k.res.gate == 1) {
                                       GmInvalidateOutside(k.res, k.pred.get());
                                       GmQueueStores(k.stores);
                                   }
                               }
                               if (i >= miss) k.miss_at = now;
                               k.state.store(i < miss ? kGmFinal : kGmMiss, std::memory_order_release);
                           }
                           if (miss) GmSignal(2 * (*m)[miss - 1]->t);
                           if (miss < end) {
                               g_gms.miss.store(true, std::memory_order_release);
                               encq::Nudge();
                           }
                           GmNotify();
                       }];
}

// Appends a ticket to the chain in the main command buffer (its dispatch, if any, is encoded).
void GmEncodeTicket(const std::shared_ptr<GmTicket>& k) {
    EnsureCmd();
    g_gmem.chain.push_back(k);
    if (k->slot.buf) g_gmem.chain_prev = k->slot.buf;
    g_gms.chain_open = true;
    g_gms.encoded = k->t;
    g_gms.cmd_tickets.push_back(k);
    g_gmem.tickets.push_back(k);
    S.pending = true;
}

void GmCloseChain() {
    if (!g_gms.chain_open || g_gms.chain_closing) return;
    g_gms.chain_closing = true;
    EndEncoders();
    auto members = std::make_shared<std::vector<std::shared_ptr<GmTicket>>>(std::move(g_gmem.chain));
    g_gmem.chain.clear();
    g_gmem.chain_prev = nil;
    const uint64_t t = members->back()->t;
    RequireMainTarget("GmCloseChain");
    S.be->QueueSignal(g_gms.ev, 2 * t - 1);
    S.be->QueueWait(g_gms.ev, 2 * t);
    GmListenChain(members);
    g_gm_stats.chains++;
    g_gms.chain_open = false;
    g_gms.chain_closing = false;
}

// A ticket whose dispatch was not encoded (skipped): it still gets a final result, in order.
void GmConsumeEmpty(uint64_t t) {
    if (S.hung || !g_gms.ev) return;
    auto k = std::make_shared<GmTicket>();
    k->t = t;
    GmEncodeTicket(k);
}

// Fast path. Takes over rp (null afterwards) when it encoded the dispatch.
bool GmDispatchFast(DispatchRec*& rp, id<MTLComputePipelineState> pso, MTLSize tg) {
    DispatchRec& r = *rp;
    GmService();
    GmSlot slot = GmAcquireSlot();
    if (!slot.buf) {
        g_gm_stats.no_slot++;
        RLog("guestmem", "%s CS 0x%llx skipped: no fault buffer", r.queue, (unsigned long long)r.cs->code_addr);
        NoteSkip(SkipReason::GmNoSlot);
        return false;
    }
    GmTarget t;
    auto k = std::make_shared<GmTicket>();
    k->withhold = g_gms.withhold_next.exchange(false);
    EnsureCmd(); // before chain_member: it may wait in the throttle (see GmEncodeDispatch)
    if (S.hung) { // the throttle gave up on the GPU
        GmReleaseSlot(slot);
        return false;
    }
    g_gms.chain_member = true;
    bool pass_ok;
    const bool encoded = GmEncodeDispatch(t, r, pso, tg, slot, r.gm_pred.get(), false, &pass_ok, &k->textures, nullptr,
                                          g_gms.chain_open ? g_gmem.chain_prev : nil);
    g_gms.chain_member = false;
    k->pass_failed = !pass_ok;
    if (pass_ok) k->stores.swap(S.buffer_stores);
    S.buffer_stores.clear();
    if (!encoded) {
        GmReleaseSlot(slot);
        return false;
    }
    for (auto [a, n] : GmRanges(*r.gm_pred, GCN_GUEST_TRACK_SHIFT)) NoteGpuWrite(a, n);
    k->t = r.gm_ticket;
    k->slot = slot;
    k->rec = rp;
    k->pso = pso;
    k->tg = tg;
    k->pred = r.gm_pred;
    GmEncodeTicket(k);
    rp = nullptr;
    g_gm_stats.fast++;
    return true;
}

// A missed ticket: the GPU holds every later work at 2t, so the dispatch runs again now as on
// the sync path, on the second queue, before 2t is signaled.
void GmStatMax(std::atomic<uint64_t>& m, uint64_t v) {
    uint64_t cur = m.load(std::memory_order_relaxed);
    while (v > cur && !m.compare_exchange_weak(cur, v)) {}
}

void GmRerun(GmTicket& k) {
    g_gm_stats.reruns++;
    static double last_verdict = 0; // the listener's time of the verdict re-run last
    if (k.miss_at > 0 && k.miss_at != last_verdict) {
        last_verdict = k.miss_at;
        const uint64_t us = (uint64_t)std::max(0.0, (CFAbsoluteTimeGetCurrent() - k.miss_at) * 1e6);
        g_gm_stats.rerun_wait_us += us;
        GmStatMax(g_gm_stats.rerun_wait_max_us, us);
    }
    if (!S.hung && k.rec && k.slot.buf && !k.failed) {
        GmReadSlot(k.slot, &k.res); // the fast run's probe: its unmapped pages
        k.have_res = true;
        k.rerun = true;
        std::vector<std::pair<uint64_t, uint64_t>> stores;
        k.ran = GmRunSync(true, *k.rec, k.pso, k.tg, k.slot, &k.res, &k.textures, &stores);
        if (k.ran) {
            GmInvalidateOutside(k.res, k.pred.get());
            GmQueueStores(stores);
        }
    }
    if (k.miss_at > 0)
        GmStatMax(g_gm_stats.rerun_chain_max_us, (uint64_t)std::max(0.0, (CFAbsoluteTimeGetCurrent() - k.miss_at) * 1e6));
    GmSignal(2 * k.t);
    k.state.store(kGmFinal, std::memory_order_release);
    GmNotify();
}

void GmFinish(GmTicket& k) {
    if (k.slot.buf) {
        if (!k.have_res) GmReadSlot(k.slot, &k.res);
        if (k.failed) {
            NoteSkip(SkipReason::GmCmdFailed);
            RLog("guestmem", "%s CS 0x%llx skipped: its command buffer failed", k.rec->queue,
                 (unsigned long long)k.rec->cs->code_addr);
        } else if (k.pass_failed && !k.rerun) {
            // its binding failed after the probe ran (EncodeCs counted it as skipped): what the
            // slot reports is not the pass's, nothing is learned from it
            g_gm_stats.pass_failed++;
        } else if (k.rerun ? k.ran : k.res.gate == 1) {
            GmAfterPass(*k.rec, k.res, k.pred.get());
        } else if (!k.rerun) {
            // refused for a status (GmRunSync logs and counts its own skips)
            g_gm_stats.refused++;
            const bool status = k.res.gate == 2 && (k.res.why & kGmWhyStatus);
            NoteSkip(status ? SkipReason::GmStatus : SkipReason::GmIndirectSize);
            if (status)
                RLog("guestmem", "%s CS 0x%llx skipped: it accesses guest memory through %s", k.rec->queue,
                     (unsigned long long)k.rec->cs->code_addr, GuestStatusText(k.res.status).c_str());
            else
                RLog("guestmem", "%s CS 0x%llx skipped: its indirect dispatch size cannot be read", k.rec->queue,
                     (unsigned long long)k.rec->cs->code_addr);
        }
        if (!k.failed) GmReleaseSlot(k.slot); // a failed command buffer may have left it dirty
    }
    if (k.rec) {
        PutDispatchRec(k.rec);
        k.rec = nullptr;
    }
}

void GmService() {
    if (g_gmem.servicing) return;
    g_gmem.servicing = true;
    // Taken with acquire: the listener stores the missed tickets' states, then miss (release).
    // A plain store could land after the listener's and still read the states from before, so
    // the miss was cleared and never serviced (the CP then waited for it until the watchdog:
    // "the CP waited 4000 ms for guest memory dispatch 68267 (final up to 68250)").
    g_gms.miss.exchange(false, std::memory_order_acq_rel);
    if (g_gms.released.load(std::memory_order_acquire)) { // given up on (GmGiveUp)
        static bool logged = false;
        if (!logged) logged = true, GmLogState();
        GmReleaseAll();
    }
    while (!g_gmem.tickets.empty()) {
        std::shared_ptr<GmTicket> k = g_gmem.tickets.front();
        const int st = k->state.load(std::memory_order_acquire);
        if (st == kGmMiss) {
            GmRerun(*k);
            continue;
        }
        if (st != kGmFinal) break;
        g_gmem.tickets.pop_front();
        GmSignal(2 * k->t); // (final: its result is; a chain may end with it after a re-run)
        GmFinish(*k);
    }
    g_gmem.servicing = false;
}

// CP thread: the fast path for a guest memory dispatch whose shader has a prediction.
// Opt-in: PS4_GNM_GUEST_MEMORY_FAST=1. By default every guest memory dispatch takes the sync path
// (waited for): in U4 soak runs the fast path showed anomalies the sync path did not (a guest
// indirect call through a non-code pointer, a ticket stall, a GPU memory blow-up; 3 in about 35
// runs against 0 in about 30 sync / base runs), not yet explained. PS4_GNM_GUEST_MEMORY_SYNC=1
// forces the sync path even with the fast path requested (A/B runs).
void GmPlan(DispatchRec& r) {
    static const bool sync_only = [] {
        const char* fast = getenv("PS4_GNM_GUEST_MEMORY_FAST");
        const char* sync = getenv("PS4_GNM_GUEST_MEMORY_SYNC");
        return !(fast && fast[0] == '1') || (sync && sync[0] == '1');
    }();
    if (sync_only || cap::g_pass || !g_gms.ev) return;
    // Sampled images only: a re-run binds the textures the dispatch bound (their uploads ran
    // before it). Storage and depth images have effects after the pass on the CPU side
    // (SyncStorageToDepth, depth companions) that a later re-run could not redo in place.
    char key[64];
    uint32_t storage = 0, depth = 0;
    for (const gcn_image_binding& ib : r.cs->imgs) storage += ib.is_storage, depth += ib.is_depth;
    if (storage || depth) {
        snprintf(key, sizeof(key), "guestmem-sync-img-%llx", (unsigned long long)r.cs->code_addr);
        RLog(key, "%s CS 0x%llx: guest memory dispatch waited for (the shader has %u storage, %u depth images)",
             r.queue, (unsigned long long)r.cs->code_addr, storage, depth);
        return;
    }
    std::shared_ptr<const GmPages> pred = GmPredict(r.cs->uid);
    if (!pred) return; // its first dispatch
    if (pred->size() > kGmMaxPredicted) {
        snprintf(key, sizeof(key), "guestmem-sync-big-%llx", (unsigned long long)r.cs->code_addr);
        RLog(key, "%s CS 0x%llx: guest memory dispatch waited for (it wrote more than %u MiB)", r.queue,
             (unsigned long long)r.cs->code_addr, kGmMaxPredicted);
        return;
    }
    r.gm_ticket = g_gms.issued.fetch_add(1, std::memory_order_relaxed) + 1;
    r.gm_pred = pred;
    g_gm_cp.pending.push_back(r.gm_ticket);
}
} // namespace

void Dispatch(GuestContext* ctx, const uint32_t* regs, uint32_t dim_x, uint32_t dim_y, uint32_t dim_z,
              uint64_t indirect_addr, const char* queue, int queue_kind) {
    if (!Init(ctx) || S.hung) return;
    GmCpQueue(queue_kind);
    // a dispatch reads guest memory on the CPU (the translation of a new shader specializes on
    // sharps it reads, the permutation choice, resource tables, sampled textures): after the
    // tickets its queue synchronized with
    GmCpOrder();
    @autoreleasepool {
        if (!indirect_addr && (dim_x == 0 || dim_y == 0 || dim_z == 0)) return;
        cap::CpScope cap_scope("dispatch", queue, queue, regs);
        if (cap::g_pass) cap::NoteDispatch(dim_x, dim_y, dim_z, indirect_addr);
        std::unique_ptr<DispatchRec, RecPool<DispatchRec>::Back> rp(g_dispatch_recs.Get(), {&g_dispatch_recs});
        DispatchRec& r = *rp;
        TableReadCp table_read; // before anything here reads guest memory
        r.table_snap = TableReadBegin();
        gcn_runtime_info& rt = r.rt;
        gcn_runtime_info_from_regs(regs, 0x10000, GCN_HW_CS, GCN_SW_COMPUTE, &S.profile, &rt);
        uint64_t& addr = r.addr;
        uint32_t* const ud = r.ud;
        gcn_stage_from_regs(regs, 0x10000, GCN_HW_CS, &addr, ud);
        DrainAsyncCompiles();
        Shader* cs = GetShader(GCN_HW_CS, GCN_SW_COMPUTE, rt, addr, ud);
        if (!cs) {
            NoteSkip(SkipReason::CsUnavailable);
            RLog("skip-cs", "%s dispatch skipped: CS 0x%llx unavailable", queue, (unsigned long long)addr);
            return;
        }
        StageFlat(2, cs, ud);
        if (cap::g_pass) cap::NoteStage("cs", cs, ud);
        r.flat = S.flat_scratch[2];
        DecodeStageImages(cs, r.flat, &r.img);
        r.cs = cs;
        r.dim_x = dim_x;
        r.dim_y = dim_y;
        r.dim_z = dim_z;
        r.indirect_addr = indirect_addr;
        r.queue = queue;
        r.regs.Capture(regs);
        if (cs->kjob && !cs->kjob->Done()) S.kernels_open.push_back(cs->kjob);
        const bool guest_memory = cs->info.uses_guest_memory;
        if (guest_memory) GmPlan(r);
        const bool cp_waits = guest_memory && !r.gm_ticket;
        auto job = [rec = rp.release()] {
            DispatchRec* p = rec;
            DispatchEncode(p); // a guest memory ticket may keep the record
            if (!p) return;
            if (p->gm_ticket) GmConsumeEmpty(p->gm_ticket);
            g_dispatch_recs.Put(p);
        };
        // A guest memory dispatch may write any guest memory. Sync path: the CP goes on (and
        // reads resource tables for the next draws) once it ran. Fast path: the CP goes on; it
        // waits for the result before it reads guest memory for work ordered after it (GmCpOrder).
        table_read.armed = false; // DispatchEncode ends it
        if (cp_waits) encq::Run(job);
        else encq::Defer(std::move(job));
    }
}

// End of a command stream run (CP thread): waits for the kernels its dispatches still compile.
// Within the run the CP thread went on while they compiled in parallel; it does not go on into
// the next submission, so it never gets further ahead of the encoder (and of the GPU's writes to
// guest memory, which the CP reads for resource tables) than without background kernels.
// Measured without this: in a TLOU loading burst the CP ran seconds ahead and met twice the
// shader permutations.
void WaitKernelCompiles() {
    if (S.kernels_open.empty()) return;
    double total = 0;
    for (auto& j : S.kernels_open) {
        double w = 0;
        j->Wait(&w);
        total += w;
    }
    S.kernels_open.clear();
    if (total > 0) {
        g_compile.kernel_cp_wait.Add(total);
        g_hitch.kernel_wait_ms += total;
        g_hitch.kernel_waits++;
    }
}

void DispatchEncode(DispatchRec*& rp) {
    const AliasJob alias_job;
    DispatchRec& r = *rp;
    const TableReadEncoded table_read(r.table_snap);
    CommitEachForDebug();
    if (S.hung) return;
    cap::EncodeScope cap_scope;
    if (!BackendHas(kGnmCapCompute)) {
        cap::SetSkipped("backend without compute (PS4_GNM_MTL4_ONLY_CLEARS)");
        NoteSkip(SkipReason::NoCompute);
        return;
    }
    const RegSnap& regs = r.regs;
    const gcn_runtime_info& rt = r.rt;
    const uint64_t addr = r.addr;
    Shader* const cs = r.cs;
    const uint32_t dim_x = r.dim_x, dim_y = r.dim_y, dim_z = r.dim_z;
    const uint64_t indirect_addr = r.indirect_addr;
    const char* const queue = r.queue;
    StageImagesScope images(nullptr, nullptr, &r.img);
    {
        id<MTLComputePipelineState> pso = nil;
        auto it = S.cs_pipes.find(cs->uid);
        if (it != S.cs_pipes.end()) {
            pso = it->second;
        } else {
            NSError* e = nil;
            // Ask the Metal compiler to fit the full GCN workgroup (up to 1024 threads); without
            // the hint register-heavy kernels get a lower limit and the dispatch had to be skipped.
            const uint64_t wg = (uint64_t)std::max(1u, rt.cs.workgroup_size[0]) * std::max(1u, rt.cs.workgroup_size[1]) *
                                std::max(1u, rt.cs.workgroup_size[2]);
            if (cs->kjob) {
                // first dispatch of a kernel compiled in the background: wait for it here, in
                // stream order (the dispatch's results are read by the work after it)
                double waited = 0;
                id<MTLFunction> kfn = cs->kjob->Wait(&waited);
                if (waited > 0) {
                    g_compile.kernel_wait.Add(waited);
                    g_hitch.kernel_enc_wait_ms += waited;
                } else {
                    g_compile.kernel_ready++;
                }
                if (!kfn) {
                    RLog("msl-fail", "Metal rejected MSL of shader 0x%llx (hw %u): %s", (unsigned long long)addr,
                         cs->hw, cs->kjob->Error().c_str());
                    S.cs_pipes[cs->uid] = nil;
                    NoteSkip(SkipReason::CsLibrary);
                    return;
                }
            }
            MTLComputePipelineDescriptor* cpd = [MTLComputePipelineDescriptor new];
            cpd.computeFunction = PipelineFn(cs);
            if (wg > 0 && wg <= 1024) cpd.maxTotalThreadsPerThreadgroup = (NSUInteger)wg;
            const uint64_t ck[2] = {cs->msl_hash, wg};
            const uint64_t cstable = Fnv(ck, sizeof(ck), 0x5ca1ab1e5ca1ab1eull);
            ps4gnm_pso_archive::Attach(cpd);
            const double cpso_t0 = CFAbsoluteTimeGetCurrent();
            pso = [S.dev newComputePipelineStateWithDescriptor:cpd options:MTLPipelineOptionNone reflection:nil error:&e];
            if (!pso && ReplaceStoredFns(cs, nullptr)) {
                // built with a stored function: retry once with the function compiled from MSL
                cpd.computeFunction = PipelineFn(cs);
                e = nil;
                pso = [S.dev newComputePipelineStateWithDescriptor:cpd options:MTLPipelineOptionNone reflection:nil error:&e];
            }
            if (pso && !ps4gnm_pso_archive::Contains(cstable)) ps4gnm_pso_archive::Add(cstable, cpd);
            if (pso && cs->lib_stage == ps4gnm_lib_store::kKernel && !ps4gnm_lib_store::Has(cs->lib_key))
                ps4gnm_lib_store::StoreCompute(cs->lib_key, cpd);
            if (!pso) pso = [S.dev newComputePipelineStateWithFunction:PipelineFn(cs) error:&e];
            g_hitch.pso_ms += (CFAbsoluteTimeGetCurrent() - cpso_t0) * 1000.0;
            g_hitch.psos++;
            g_compile.cpso_sync.Add((CFAbsoluteTimeGetCurrent() - cpso_t0) * 1000.0);
            if (!pso)
                RLog("cpso-fail", "compute pipeline 0x%llx failed: %s", (unsigned long long)addr,
                     e.localizedDescription.UTF8String);
            S.cs_pipes[cs->uid] = pso;
        }
        if (!pso) {
            NoteSkip(SkipReason::CsPipeline);
            return;
        }
        const uint32_t wx = std::max(1u, rt.cs.workgroup_size[0]), wy = std::max(1u, rt.cs.workgroup_size[1]),
                       wz = std::max(1u, rt.cs.workgroup_size[2]);
        static std::unordered_map<void*, NSUInteger>& max_threads = *new std::unordered_map<void*, NSUInteger>; // per pipeline (an Objective-C call)
        auto mt = max_threads.find((__bridge void*)pso);
        if (mt == max_threads.end()) mt = max_threads.emplace((__bridge void*)pso, pso.maxTotalThreadsPerThreadgroup).first;
        if ((uint64_t)wx * wy * wz > mt->second) {
            RLog("cs-wg", "%s CS 0x%llx workgroup %ux%ux%u exceeds Metal limit %lu (NUM_THREAD regs %08x %08x %08x, dims %u %u %u)",
                 queue, (unsigned long long)addr, wx, wy, wz, (unsigned long)pso.maxTotalThreadsPerThreadgroup,
                 regs[0x2E07], regs[0x2E08], regs[0x2E09], dim_x, dim_y, dim_z);
            NoteSkip(SkipReason::CsWorkgroup);
            return;
        }

        // Metadata clears (CMASK/HTILE written by a compute shader): track instead of running
        // (shadPS4 Rasterizer::IsComputeMetaClear)
        std::vector<uint32_t>& flat = r.flat;
        {
            bool reads_meta = false;
            uint64_t meta_written = 0, meta_bytes = 0;
            for (uint32_t i = 0; i < cs->bufs.size(); ++i) {
                if (cs->bufs[i].type != GCN_BUFFER_GUEST) continue;
                uint32_t v[4];
                gcn_buffer_desc bd{};
                if (!gcn_shader_fetch_buffer_sharp(cs->sh, i, flat.data(), cs->code_addr, v)) continue;
                gcn_decode_buffer(v, &bd);
                if (!S.meta.count(bd.base_address)) continue;
                if (cs->bufs[i].is_written) meta_written = bd.base_address, meta_bytes = bd.size_bytes;
                else reads_meta = true;
            }
            // Surface copies through buffers (shadPS4 Rasterizer::IsComputeImageCopy): one read and
            // one written buffer of the same size, the source being a GPU-owned surface.
            {
                int n_read = 0, n_written = 0;
                gcn_buffer_desc rd{}, wr{};
                for (uint32_t i = 0; i < cs->bufs.size(); ++i) {
                    if (cs->bufs[i].type != GCN_BUFFER_GUEST) continue;
                    uint32_t v[4];
                    gcn_buffer_desc bd{};
                    if (!gcn_shader_fetch_buffer_sharp(cs->sh, i, flat.data(), cs->code_addr, v)) continue;
                    gcn_decode_buffer(v, &bd);
                    if (cs->bufs[i].is_written) n_written++, wr = bd;
                    else n_read++, rd = bd;
                }
                if (n_read == 1 && n_written == 1 && rd.size_bytes == wr.size_bytes && rd.base_address &&
                    wr.base_address && rd.size_bytes > 4096) {
                    id<MTLTexture> src = nil, dst = nil;
                    int kind = 0;
                    RefreshGuestNewer(rd.base_address, nil, false); // the copy reads the source surface
                    RefreshGuestNewer(wr.base_address, nil, true);
                    if (S.tex->CloneGpuTexture(rd.base_address, wr.base_address, wr.size_bytes, &src, &dst, &kind)) {
                        EndEncoders();
                        EnsureCmd();
                        S.be->BeginCopies();
                        S.be->CopyTexture(src, dst);
                        S.be->EndEncoder();
                        S.pending = true;
                        if (kind == 2) BumpDepthVersion(dst);
                        LogWork("surface copy (compute copy shader)");
                        cap::SetSkipped("surface copy (compute copy shader): a blit, not a shader pass");
                        if (ShouldTrace())
                            fprintf(stderr, "[gnm-trace] %s CS 0x%llx: surface copy 0x%llx -> 0x%llx (%s)\n", queue,
                                    (unsigned long long)addr, (unsigned long long)rd.base_address,
                                    (unsigned long long)wr.base_address, kind == 2 ? "depth" : "color");
                        // remember the stencil plane of the copied depth so its copy is skipped too
                        return;
                    }
                    if (S.stencil_to_z.count(rd.base_address) || S.meta.count(rd.base_address)) {
                        // stencil plane (copied with its depth texture) or HTILE/CMASK metadata
                        if (S.meta.count(rd.base_address)) S.meta.emplace(wr.base_address, MetaState{});
                        cap::SetSkipped("stencil / metadata copy (skipped by the renderer)");
                        if (ShouldTrace())
                            fprintf(stderr, "[gnm-trace] %s CS 0x%llx: stencil/meta copy 0x%llx -> 0x%llx skipped\n",
                                    queue, (unsigned long long)addr, (unsigned long long)rd.base_address,
                                    (unsigned long long)wr.base_address);
                        return;
                    }
                }
            }
            if (meta_written && !reads_meta) {
                cap::SetSkipped("CMASK/HTILE clear by a compute shader (tracked, not run)");
                ArmMetaClear(S.meta[meta_written], meta_bytes, S.committed_seq + 1);
                if (ShouldTrace()) fprintf(stderr, "[gnm-trace] %s CS 0x%llx: meta clear of 0x%llx\n", queue,
                                           (unsigned long long)addr, (unsigned long long)meta_written);
                return;
            }
        }

        const TableStage table_stage = {cs, r.ud, &r.rt, &flat, &r.img};
        if (!ResolveTableHazards(&table_stage, 1, r.table_snap)) {
            cap::SetSkipped(kCapTableHazardSkip);
            NoteSkip(SkipReason::CsTableHazard);
            return;
        }
        ResolveTextureHazards(cs, flat);
        PrepareDepthCompanions(cs, flat);
        const MTLSize tg = MTLSizeMake(wx, wy, wz);
        if (cs->info.uses_guest_memory) {
            if (!EnsureGuestMemoryState()) {
                RLog("guestmem", "%s CS 0x%llx skipped: no guest memory state", queue, (unsigned long long)addr);
                NoteSkip(SkipReason::GmNoState);
                return;
            }
            if (r.gm_ticket) {
                if (!GmDispatchFast(rp, pso, tg)) return;
            } else if (!GmDispatchSync(r, pso, tg)) {
                return;
            }
        } else if (!EncodeCs(r, pso, tg, nil, nil, 0)) {
            return;
        }
        S.n_dispatches++;
        if (cap::g_pass) cap::Encoded(indirect_addr);
        S.n_cdisp++;
        WorkRec wr;
        wr.what = queue;
        wr.a = addr;
        wr.n = dim_x;
        wr.inst = dim_y;
        wr.extra = dim_z;
        wr.dispatch = true;
        LogWork(wr);
        if (!S.storage_bound.empty()) SyncStorageToDepth();
        if (ShouldTrace()) {
            fprintf(stderr, "[gnm-trace] %s dispatch CS=0x%llx groups=%ux%ux%u wg=%ux%ux%u%s\n", queue,
                    (unsigned long long)addr, dim_x, dim_y, dim_z, wx, wy, wz, indirect_addr ? " indirect" : "");
            if (indirect_addr) {
                char what[64];
                snprintf(what, sizeof(what), "CS=0x%llx (dispatch)", (unsigned long long)addr);
                s_traced_indirect.emplace_back(indirect_addr, what);
            }
        }
    }
}

bool PresentImpl(GuestContext* ctx, uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch,
                 uint32_t tiling_mode, uint32_t pixel_format, std::function<void()> on_done);

// Without a GPU target for the display buffer the caller would show the guest memory itself;
// with the encoder thread that happens there instead (the CP does not wait for the answer):
// the same CPU detile as VideoOut's fallback, then the flip is completed.
extern "C" int ps4_gnm_read_display_cpu(GuestContext* ctx, uint64_t addr, uint32_t width, uint32_t height,
                                        uint32_t pitch, uint32_t tiling_mode, uint32_t pixel_format, uint8_t* out_bgra);

// Fallback when the renderer cannot present (disabled or hung): detile on the CPU.
void PresentFromGuestMemory(GuestContext* ctx, uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch,
                            uint32_t tiling_mode, uint32_t pixel_format) {
    thread_local std::vector<uint8_t> buf;
    buf.resize((size_t)width * height * 4);
    if (ps4_gnm_read_display_cpu(ctx, addr, width, height, pitch, tiling_mode, pixel_format, buf.data()))
        ps4_metal_screen_present_frame(buf.data(), (size_t)width * 4, (int)width, (int)height);
    else
        ps4_metal_screen_present_frame(ctx->mem_base + addr, (size_t)pitch * 4, (int)width, (int)height);
}

// ---- Flip order.
// Every flip the renderer takes completes through CompleteFlip, in the order of the Present
// calls. On the PS4, SubmitAndFlip only enqueues the flip and the flip completes when the
// display takes the buffer; nothing here waits for the display (docs/present-pacing.md). A
// frame that gets a present slot at once completes when the GPU has executed its conversion
// (AfterGpu). A frame held for a slot (ps4_metal_screen_queue_held) completes when the Metal
// screen has copied it into a slot, on the screen's own queue, independent of the renderer's
// stream. So a later frame can complete first; its flip then waits here for the earlier ones.
std::mutex g_flip_mu;
uint64_t g_flip_next = 0;  // sequence number of the next flip (renderer context: encoder or CP lock)
uint64_t g_flip_done = 0;  // flips completed, in order (g_flip_mu)
std::map<uint64_t, std::function<void()>> g_flip_ready; // completed before an earlier one (g_flip_mu)

void CompleteFlip(uint64_t seq, std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(g_flip_mu);
    g_flip_ready.emplace(seq, std::move(fn));
    while (!g_flip_ready.empty() && g_flip_ready.begin()->first == g_flip_done) {
        std::function<void()> f = std::move(g_flip_ready.begin()->second);
        g_flip_ready.erase(g_flip_ready.begin());
        ++g_flip_done;
        // under the lock, so flips run one at a time and in order (the VideoOut flip completion
        // takes only VideoOut's own locks)
        if (f) f();
    }
}

struct HeldFlip {
    uint64_t seq;
    std::function<void()> on_done;
};

void HeldFlipDone(void* arg, int shown) {
    (void)shown; // a dropped frame's flip completes too, as on the PS4 when a newer flip replaces it
    std::unique_ptr<HeldFlip> h(static_cast<HeldFlip*>(arg));
    CompleteFlip(h->seq, std::move(h->on_done));
}

// A display buffer that is not a GPU render target (written by the CPU, or by compute shaders
// through guest memory) is copied from guest memory into a texture on the GPU, behind the
// frame's work in the current command buffer (compute detile for a tiled buffer), and then
// presented like a render target. Nothing waits for the GPU. nil if the copy cannot be encoded.
id<MTLTexture> GuestDisplayTexture(uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch,
                                   uint32_t tiling_mode, uint32_t pixel_format) {
    if (!BackendHas(kGnmCapCompute | kGnmCapCopies)) return nil; // the CPU path (PresentImpl)
    // the buffer's memory as an image resource (T#): format and component order of the VideoOut
    // pixel format, linear or Display2DThin with the VideoOut pitch (as the CPU path reads it)
    uint32_t dfmt = (uint32_t)DataFormat::Format8_8_8_8, nfmt = (uint32_t)NumberFormat::Unorm;
    uint32_t sel_x = 6, sel_y = 5, sel_z = 4; // ARGB in memory: x = B, y = G, z = R
    switch (pixel_format) {
    case kVideoOutA8B8G8R8Srgb: sel_x = 4, sel_y = 5, sel_z = 6; break;
    case kVideoOutA2R10G10B10:
    case kVideoOutA2R10G10B10Srgb:
    case kVideoOutA2R10G10B10Bt2020Pq: dfmt = (uint32_t)DataFormat::Format2_10_10_10; break;
    case kVideoOutA16R16G16B16Float:
        dfmt = (uint32_t)DataFormat::Format16_16_16_16;
        nfmt = (uint32_t)NumberFormat::Float;
        break;
    default: break; // A8R8G8B8, also every format the CPU path reads as BGRA8
    }
    const bool tiled = tiling_mode == 0;
    const uint32_t p = tiled ? ((std::max(pitch, width) + 127) & ~127u) : std::max(pitch, width);
    const uint64_t base = addr >> 8;
    uint32_t raw[8] = {};
    raw[0] = (uint32_t)base;
    raw[1] = (uint32_t)((base >> 32) & 0x3F) | (dfmt << 20) | (nfmt << 26);
    raw[2] = (width - 1) | ((height - 1) << 14);
    raw[3] = sel_x | (sel_y << 3) | (sel_z << 6) | (7u << 9) /* w = A */ |
             ((uint32_t)(tiled ? TileMode::Display2DThin : TileMode::DisplayLinearAligned) << 20) |
             ((uint32_t)ImageType::Color2D << 28);
    raw[4] = (p - 1) << 13;
    ImageDesc img;
    if (!DecodeImage(raw, 8, &img) || !img.valid || !img.pixel.mtl_format) return nil;
    TextureCache::GpuUpload up;
    up.surf = img.ToSurfaceDesc();
    if (!ComputeSurfaceLayout(up.surf, &up.layout)) return nil;
    id<MTLTexture> t = S.guest_display_tex;
    if (!t || t.width != width || t.height != height || t.pixelFormat != (MTLPixelFormat)img.pixel.mtl_format ||
        S.guest_display_swizzle != img.view_swizzle) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:(MTLPixelFormat)img.pixel.mtl_format
                                                                                      width:width
                                                                                     height:height
                                                                                  mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModePrivate;
        td.swizzle = ToMTLSwizzle(img.view_swizzle);
        t = [S.dev newTextureWithDescriptor:td];
        if (S.guest_display_tex) ReleasedTexture(nullptr, S.guest_display_tex);
        if (t) ResidentTexture(nullptr, t);
        S.guest_display_tex = t;
        S.guest_display_swizzle = img.view_swizzle;
        if (!t) return nil;
    }
    // one texture for every such frame: the next frame's copy is ordered after this frame's
    // conversion pass by Metal's hazard tracking (same queue)
    up.tex = t;
    up.width = width;
    up.height = height;
    up.tex_type = MTLTextureType2D;
    up.bytes_per_block = img.pixel.bytes_per_block;
    up.block_width = img.pixel.block_width ? img.pixel.block_width : 1;
    up.block_height = img.pixel.block_height ? img.pixel.block_height : 1;
    return EncodeGpuUpload(img, up, false) ? t : nil;
}

// Every present, from a GPU target or read by the CPU (PresentFromGuestMemory): the per-300-presents
// perf, backend and guest memory lines, and every 300th the "present #" totals line
// (tools/metal4_route_report.py reads all of them). describe(buf, size) names the source of this
// present; it is called only for that line.
FlipWaitCounters& FlipWaits() {
    static FlipWaitCounters c;
    return c;
}

template <class Describe> void PresentStats(Describe&& describe) {
    static uint64_t presents = 0;
    S.be->NoteFrameEnd(); // the allocator trim's frame (Metal 4)
    static double last_t = CFAbsoluteTimeGetCurrent();
    if ((presents + 1) % 300 == 0) {
        const double now = CFAbsoluteTimeGetCurrent();
        static uint64_t fw_s0, fw_t0, fw_w0, skip0, pend0;
        const uint64_t skip_now = S.n_skipped.load(std::memory_order_relaxed);
        const uint64_t pend_now = SkippedFor(SkipReason::PendingCompile);
        const uint64_t fw_s = FlipWaits().suspends.load(std::memory_order_relaxed);
        const uint64_t fw_t = FlipWaits().timeouts.load(std::memory_order_relaxed);
        const uint64_t fw_w = FlipWaits().wakes.load(std::memory_order_relaxed);
        fprintf(stderr, "[gnm-draw] perf: %.1f fps over last 300 presents; per frame: %.1f commits, %.1f waits, %.1f ms waiting, %.1f ms GPU busy, %.1f ms CP (CPU), %.1f ms encoder, %.1f ms screen; "
                        "encoder %.1f ms CPU, blocked %.1f ms throttle, %.1f ms in-flight limit, %.1f ms opening command buffers; "
                        "flip-label waits %llu (given up %llu, woken %llu; totals %llu, %llu, %llu); "
                        "skipped %llu (pending compile %llu) in 300 presents\n",
                300.0 / (now - last_t), S.n_commits / 300.0, S.n_waits / 300.0, S.wait_ms / 300.0,
                S.gpu_ns.exchange(0) / 1e6 / 300.0, ps4_gnm_take_cp_ns() / 1e6 / 300.0, encq::busy_ns.exchange(0) / 1e6 / 300.0, S.screen_ms / 300.0,
                encq::cpu_ns.exchange(0) / 1e6 / 300.0, encq::throttle_ns.exchange(0) / 1e6 / 300.0, encq::room_ns.exchange(0) / 1e6 / 300.0,
                encq::begin_ns.exchange(0) / 1e6 / 300.0, (unsigned long long)(fw_s - fw_s0),
                (unsigned long long)(fw_t - fw_t0), (unsigned long long)(fw_w - fw_w0), (unsigned long long)fw_s,
                (unsigned long long)fw_t, (unsigned long long)fw_w, (unsigned long long)(skip_now - skip0),
                (unsigned long long)(pend_now - pend0));
        fw_s0 = fw_s, fw_t0 = fw_t, fw_w0 = fw_w, skip0 = skip_now, pend0 = pend_now;
        S.screen_ms = 0;
        {
            static uint64_t held0, time0, backlog0;
            const uint64_t held = g_hold.held.load(), by_time = g_hold.by_time.load(), by_backlog = g_hold.by_backlog.load();
            fprintf(stderr, "[gnm-draw] per frame: %.1f render passes (%.1f loading depth), %.1f compute encoders, %.1f dispatches, "
                            "%.1f compute barriers; compute commits held %.1f (released by the bound %.1f, the backlog %.1f)\n",
                    S.n_passes / 300.0, S.n_depth_loads / 300.0, S.n_cencs / 300.0, S.n_cdisp / 300.0, S.n_cbarriers / 300.0,
                    (held - held0) / 300.0, (by_time - time0) / 300.0, (by_backlog - backlog0) / 300.0);
            held0 = held, time0 = by_time, backlog0 = by_backlog;
        }
        S.n_passes = S.n_depth_loads = S.n_cencs = S.n_cbarriers = S.n_cdisp = 0;
        {
            static GnmBackendStats last{};
            const GnmBackendStats b = S.be->Stats();
            fprintf(stderr, "[gnm-draw] backend %s: split commits per frame %.1f; allocators %llu (%llu KiB; "
                            "created %llu, released %llu in 300 frames); "
                            "resident %llu (%llu MiB, %llu textures); draws skipped by PS4_GNM_MTL4_ONLY_CLEARS %llu; "
                            "res_assert=%llu; per frame: alias waits %.2f, access fences %.1f\n",
                    BackendName(S.be->Kind()), (b.split_commits - last.split_commits) / 300.0,
                    (unsigned long long)b.allocators, (unsigned long long)(b.allocator_bytes >> 10),
                    (unsigned long long)(b.allocators_created - last.allocators_created),
                    (unsigned long long)(b.allocators_released - last.allocators_released),
                    (unsigned long long)b.resident, (unsigned long long)(b.resident_bytes >> 20),
                (unsigned long long)b.resident_textures,
                    (unsigned long long)S.n_clear_only.load(), (unsigned long long)b.residency_asserts,
                    (b.alias_waits - last.alias_waits) / 300.0, (b.access_fences - last.access_fences) / 300.0);
            last = b;
        }
        {
            // GPU targets fully overwritten by buffer stores (TextureCache::NoteBufferWrite)
            static TextureCache::GuestNewerStats last;
            const TextureCache::GuestNewerStats g = S.tex->GetGuestNewerStats();
            fprintf(stderr, "[gnm-draw] buffer writes over GPU targets in 300 presents: %llu marked, %llu refreshed, "
                            "%llu partial (not refreshed), %llu refreshes not encoded; %zu marked now\n",
                    (unsigned long long)(g.marked - last.marked), (unsigned long long)(g.refreshed - last.refreshed),
                    (unsigned long long)(g.partial - last.partial), (unsigned long long)(g.failed - last.failed),
                    S.tex->GuestNewerCount());
            last = g;
        }
        {
            static GuestMemoryStats last{};
            const GuestMemoryStats now_gm = GetGuestMemoryStats();
            const auto d = [](uint64_t a, uint64_t b) { return (a - b) / 300.0; };
            if (now_gm.fast + now_gm.sync != last.fast + last.sync)
                fprintf(stderr, "[gnm-draw] guest memory per frame: %.1f dispatches (%.1f without a CPU wait, %.1f GPU waits for the CPU), "
                                "%.2f re-runs (longest wait for one %.2f ms, longest until a chain's re-runs ended %.2f ms), %.2f refused, %.2f mispredicted, %.2f CP waits (%.2f ms), "
                                "%.2f skipped (no buffer, binding)\n",
                        d(now_gm.fast + now_gm.sync, last.fast + last.sync), d(now_gm.fast, last.fast), d(now_gm.chains, last.chains),
                        d(now_gm.reruns, last.reruns), g_gm_stats.rerun_wait_max_us.exchange(0) / 1000.0,
                        g_gm_stats.rerun_chain_max_us.exchange(0) / 1000.0,
                        d(now_gm.refused, last.refused), d(now_gm.mispredicted, last.mispredicted),
                        d(now_gm.cp_waits, last.cp_waits), d(now_gm.cp_wait_us, last.cp_wait_us) / 1000.0,
                        d(now_gm.no_slot + now_gm.no_indirect + now_gm.pass_failed, last.no_slot + last.no_indirect + last.pass_failed));
            last = now_gm;
        }
        last_t = now;
        S.n_commits = S.n_waits = 0;
        ps4_gnm_print_sync_stats();
        ps4_fiber_print_stats(300);
        ps4_sync_print_stats(300);
        S.wait_ms = 0;
    }
    if (++presents % 300 == 1) {
        char from[96];
        describe(from, sizeof(from));
        fprintf(stderr, "[gnm-draw] present #%llu from %s; totals: draws %llu dispatches %llu skipped %llu (pending compile %llu) "
                        "shaders %zu pipelines %zu submit %llu\n",
                (unsigned long long)presents, from, (unsigned long long)S.n_draws, (unsigned long long)S.n_dispatches,
                (unsigned long long)S.n_skipped.load(std::memory_order_relaxed), (unsigned long long)SkippedFor(SkipReason::PendingCompile),
                S.shaders.size(), S.gfx_pipes.size(), (unsigned long long)S.submit_idx);
        // the totals' skips by reason (tools/metal4_route_report.py reads the line)
        char line[2048];
        int n = snprintf(line, sizeof(line), "[gnm-draw] skipped by reason:");
        bool any = false;
        for (size_t i = 0; i < (size_t)SkipReason::Count && n > 0 && (size_t)n < sizeof(line); ++i) {
            const uint64_t c = SkippedFor((SkipReason)i);
            if (!c) continue;
            n += snprintf(line + n, sizeof(line) - n, "%s %s %llu", any ? "," : "", SkipReasonName((SkipReason)i),
                          (unsigned long long)c);
            any = true;
        }
        fprintf(stderr, "%s%s\n", line, any ? "" : " none");
    }
}

bool Present(GuestContext* ctx, uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch,
             uint32_t tiling_mode, uint32_t pixel_format, std::function<void()> on_done) {
    static const bool dbg_cpu_present = getenv("PS4_GNM_DBG_CPU_PRESENT") != nullptr; // tests the fallback
    // PresentImpl returns false only when the renderer cannot present at all; the CPU then reads
    // the buffer, and the flip still completes in order behind the renderer's flips
    auto run = [=](std::function<void()> f) {
        if (!dbg_cpu_present && PresentImpl(ctx, addr, width, height, pitch, tiling_mode, pixel_format, f)) return;
        PresentFromGuestMemory(ctx, addr, width, height, pitch, tiling_mode, pixel_format);
        CompleteFlip(g_flip_next++, std::move(f));
        if (S.enabled.load(std::memory_order_acquire))
            PresentStats([&](char* b, size_t n) {
                snprintf(b, n, "guest memory 0x%llx (%ux%u fmt 0x%x, read by the CPU)", (unsigned long long)addr, width,
                         height, pixel_format);
            });
    };
    if (encq::Inline()) {
        if (!S.enabled.load(std::memory_order_acquire)) return false; // no renderer: the caller presents and completes the flip
        encq::Locked lk; // the encoder's state, as for the encoder thread's jobs (encq::Defer)
        run(std::move(on_done));
        return true;
    }
    encq::Defer([run, f = std::move(on_done)]() mutable { run(std::move(f)); });
    return true;
}

bool PresentImpl(GuestContext* ctx, uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch,
                 uint32_t tiling_mode, uint32_t pixel_format, std::function<void()> on_done) {
    g_present_gen++;
    (void)ctx;
    if (!S.enabled.load(std::memory_order_acquire) || S.hung || !S.present_pso || !width || !height) return false;
    @autoreleasepool {
        RefreshGuestNewer(addr, nil, false); // a display buffer last written by buffer stores
        id<MTLTexture> src = S.tex->FindColorTarget(addr, width, height);
        if (!src) src = S.tex->FindColorTarget(addr);
        if (src && (src.sampleCount > 1 ||
                    (src.textureType != MTLTextureType2D && src.textureType != MTLTextureType2DArray)))
            src = nil;
        // A render target of another size at the display address is a stale surface that
        // shared the memory earlier (e.g. a 3200x1800 scene target before a 3840x2160 flip
        // buffer that is written by compute shaders): the display buffer itself is not a GPU
        // target then, so take the guest-memory path below.
        if (src && (src.width != width || src.height != height)) {
            RLog("present-size", "GPU target at display buffer 0x%llx is %lux%lu, the display buffer is %ux%u",
                 (unsigned long long)addr, (unsigned long)src.width, (unsigned long)src.height, width, height);
            src = nil;
        }
        id<MTLTexture> view = nil;
        if (!src) {
            RLog("present-miss", "display buffer 0x%llx is not a GPU target (draws %llu dispatches %llu skipped %llu)",
                 (unsigned long long)addr, (unsigned long long)S.n_draws, (unsigned long long)S.n_dispatches,
                 (unsigned long long)S.n_skipped);
            view = GuestDisplayTexture(addr, width, height, pitch, tiling_mode, pixel_format);
            if (!view) {
                // last resort: the CPU reads the buffer once all GPU work has landed
                RLog("present-guest", "display buffer 0x%llx (%ux%u fmt 0x%x) cannot be copied on the GPU; read on the CPU",
                     (unsigned long long)addr, width, height, pixel_format);
                Commit();
                WaitAll();
                return false;
            }
        } else {
            // read the raw (non-sRGB) values: the display buffer holds encoded pixels
            MTLPixelFormat f = src.pixelFormat;
            MTLPixelFormat lin = f == MTLPixelFormatBGRA8Unorm_sRGB ? MTLPixelFormatBGRA8Unorm
                                 : f == MTLPixelFormatRGBA8Unorm_sRGB ? MTLPixelFormatRGBA8Unorm
                                                                      : f;
            view = src;
            // VideoOut A2R10G10B10 formats (0x88xxxxxx) keep R in the high bits, i.e. Metal's
            // RGB10A2 blue channel: swap R/B when sampling (8-bit A8R8G8B8 targets are already
            // stored as BGRA8, see GetColorTargetPixelFormat).
            const bool rgb10 = f == MTLPixelFormatRGB10A2Unorm;
            const bool swap_rb = rgb10 && (pixel_format & 0xFF000000u) == 0x88000000u;
            if (swap_rb) {
                const MTLTextureSwizzleChannels sw =
                    MTLTextureSwizzleChannelsMake(MTLTextureSwizzleBlue, MTLTextureSwizzleGreen, MTLTextureSwizzleRed,
                                                  MTLTextureSwizzleAlpha);
                view = [src newTextureViewWithPixelFormat:lin textureType:MTLTextureType2D
                                                   levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1) swizzle:sw];
            } else if (lin != f || src.textureType != MTLTextureType2D)
                view = [src newTextureViewWithPixelFormat:lin textureType:MTLTextureType2D
                                                   levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1)];
        }
        // Convert into a present slot of the window's presenter and queue it behind the frame's
        // work: the CPU does not wait for the GPU, so the next frame's command processing
        // overlaps this frame's GPU execution and the presenter's wait for a drawable. While
        // every slot is queued or on screen (back-pressure from the display), or while frames
        // before this one are held, the frame is converted into a held texture and handed over
        // by the Metal screen when a slot frees: no thread waits for the display.
        const double tp0 = CFAbsoluteTimeGetCurrent();
        const uint64_t seq = g_flip_next++;
        id<MTLTexture> dst = nil;
        const bool behind_held = ps4_metal_screen_frames_held();
        if (!behind_held)
            dst = (__bridge id<MTLTexture>)ps4_metal_screen_try_acquire_frame((int)width, (int)height);
        bool hold = false;
        if (!dst) {
            dst = (__bridge id<MTLTexture>)ps4_metal_screen_hold_texture((int)width, (int)height);
            hold = dst != nil;
        }
        const bool drop = !dst && (behind_held || ps4_metal_screen_frames_held());
        if (drop) {
            // no texture to hold it in while earlier frames are held: shown directly it would
            // overtake them, so it is dropped; its flip completes in order behind theirs
            ps4_metal_screen_note_held_drop();
            AfterGpu([seq, f = std::move(on_done)]() mutable { CompleteFlip(seq, std::move(f)); });
        }
        if (!dst && !drop) { // no texture to hold it in: the presenter copies this one if it can, or drops the frame
            if (!S.present_tex || S.present_tex.width != width || S.present_tex.height != height) {
                MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                              width:width
                                                                                             height:height
                                                                                          mipmapped:NO];
                td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
                td.storageMode = MTLStorageModePrivate;
                id<MTLTexture> old = S.present_tex;
                S.present_tex = [S.dev newTextureWithDescriptor:td];
                if (old) ReleasedTexture(nullptr, old);
                if (S.present_tex) ResidentTexture(nullptr, S.present_tex);
            }
            dst = S.present_tex;
        }
        if (!drop) {
            EndEncoders();
            EnsureCmd();
            GnmPassDesc pd;
            pd.color[0] = {dst, nil, 0, GnmLoad::DontCare, GnmStore::Store};
            FullscreenPass(*S.be, pd, S.present_pso, nil, view);
            LogWork("present");
            HeldFlip* held = hold ? new HeldFlip{seq, std::move(on_done)} : nullptr;
            ps4_metal_screen_present_op op{};
            if (held && ps4_metal_screen_held_begin((__bridge void*)dst, &HeldFlipDone, held, &op) != 0) {
                on_done = std::move(held->on_done); // not taken: shown like an unheld frame below
                delete held;
                held = nullptr;
            } else if (held) {
                S.be->QueueSignal((__bridge id<MTLSharedEvent>)op.event, op.value);
                S.present_ops.push_back(op);
            }
            if (!held) {
                const int r = ps4_metal_screen_present_begin((__bridge void*)dst, &op);
                if (r < 0) RLog("present-screen", "the Metal screen could not take the frame");
                if (r == 0) {
                    if (op.copy_dst) {
                        S.be->BeginCopies();
                        S.be->CopyTexture(dst, (__bridge id<MTLTexture>)op.copy_dst);
                        S.be->EndEncoder();
                    }
                    S.be->QueueSignal((__bridge id<MTLSharedEvent>)op.event, op.value);
                    S.present_ops.push_back(op);
                    ps4_metal_screen_present_commit(&op);
                }
                ps4_metal_screen_note_backpressure(0.0);
                AfterGpu([seq, f = std::move(on_done)]() mutable { CompleteFlip(seq, std::move(f)); });
            }
        }
        S.screen_ms += (CFAbsoluteTimeGetCurrent() - tp0) * 1000.0;
        Commit();
        static const bool writeback_all = getenv("PS4_GNM_WRITEBACK_ALL") != nullptr;
        if (writeback_all) { // experiment: make every GPU surface CPU-visible
            WaitAll();
            TextureCache* tc = S.tex;
            tc->ForEachGpuTexture(^(uint64_t a, int kind, id<MTLTexture> t) {
                if (kind == 1 && t.sampleCount == 1) tc->WriteBackColorTarget(a);
            });
        }
        {
            // Frame-time distribution (present-to-present on the submitting thread) and a
            // breakdown of every hitch (> 50 ms) so stutter causes are visible in the log.
            static double last_present = 0;
            static std::vector<float>& intervals = *new std::vector<float>;
            const double now_t = CFAbsoluteTimeGetCurrent();
            if (last_present > 0) {
                const double dt = (now_t - last_present) * 1000.0;
                intervals.push_back((float)dt);
                // PS4_GNM_LOG_LONG_FRAMES=<ms>: one line per frame longer than <ms> (walking
                // benchmark: counts long frames inside its own time window)
                static const double long_ms = [] {
                    const char* v = getenv("PS4_GNM_LOG_LONG_FRAMES");
                    return v ? atof(v) : 0.0;
                }();
                if (long_ms > 0 && dt > long_ms)
                    fprintf(stderr, "[gnm-draw] long frame %.1f ms (present %llu)\n", dt,
                            (unsigned long long)S.present_idx + 1);
                double host_ms[PS4_HITCH_KINDS];
                for (int k = 0; k < PS4_HITCH_KINDS; ++k) host_ms[k] = ps4_hitch_take_ns(k) / 1e6;
                if (dt > (long_ms > 0 && long_ms < 50.0 ? long_ms : 50.0))
                    fprintf(stderr, "[gnm-draw] hitch: frame %.1f ms; shaders %u (%.1f ms: gcn %.1f, metal %.1f, lookup %.1f, fn %.1f), pipelines %u (%.1f ms), "
                                    "texture uploads %u (%.1f ms), gpu uploads %u, gpu waits %.1f ms, draws waiting for shaders %u, "
                                    "kernel waits %u (%.1f ms CP, %.1f ms encoder); io %.1f, vm %.1f, mtlbuf %.1f, texlock %.1f, shader_fs %.1f ms\n",
                            dt, g_hitch.shaders, g_hitch.shader_ms, g_hitch.gcn_ms, g_hitch.mtl_ms, g_hitch.lookup_ms, g_hitch.fn_ms, g_hitch.psos, g_hitch.pso_ms, g_hitch.uploads,
                            g_hitch.upload_ms, g_hitch.gpu_uploads, g_hitch.wait_ms, g_hitch.pending_draws, g_hitch.kernel_waits,
                            g_hitch.kernel_wait_ms, g_hitch.kernel_enc_wait_ms, host_ms[PS4_HITCH_IO], host_ms[PS4_HITCH_VM],
                            host_ms[PS4_HITCH_MTLBUF], host_ms[PS4_HITCH_TEXLOCK], host_ms[PS4_HITCH_SHADER_FS]);
            } else {
                for (int k = 0; k < PS4_HITCH_KINDS; ++k) ps4_hitch_take_ns(k);
            }
            last_present = now_t;
            g_hitch = HitchStats{};
            if (intervals.size() >= 600) {
                std::vector<float> v = intervals;
                std::sort(v.begin(), v.end());
                auto pct = [&](double q) { return v[std::min(v.size() - 1, (size_t)(q * v.size()))]; };
                size_t over33 = 0, over50 = 0, over100 = 0;
                for (float x : v) {
                    over33 += x > 33.4f;
                    over50 += x > 50.0f;
                    over100 += x > 100.0f;
                }
                fprintf(stderr, "[gnm-draw] frame times over %zu frames: p50 %.1f p95 %.1f p99 %.1f max %.1f ms; "
                                ">33ms %zu, >50ms %zu, >100ms %zu\n",
                        v.size(), pct(0.50), pct(0.95), pct(0.99), v.back(), over33, over50, over100);
                ps4gnm_shader_cache::PrintStats();
                PrintCompileStats();
                intervals.clear();
            }
        }
        static uint64_t presents = 0; // from GPU targets: the frame number of captures and idle tracking
        cap::EndFrame();
        S.present_idx = presents + 1;
        DumpTargets(++presents);
        PresentStats([&](char* b, size_t n) {
            snprintf(b, n, "GPU target 0x%llx (%lux%lu fmt %lu)", (unsigned long long)addr, (unsigned long)src.width,
                     (unsigned long)src.height, (unsigned long)src.pixelFormat);
        });
        return true;
    }
}

// The texture cache belongs to the encoder: both run as encoder work (a created texture is reported
// to the residency hooks there).
bool ReplayAdoptTexture(const ps4gnm_capture::GpuTextureId& gid, const std::vector<uint8_t>* raw) {
    if (!S.enabled.load(std::memory_order_acquire) || !S.tex) return false;
    bool ok = false;
    encq::Run([&] {
        id<MTLTexture> t = S.tex->FindGpuTexture(gid, true);
        ok = t && (!raw || S.tex->WriteRaw(t, *raw));
    });
    return ok;
}

bool ReplayReadTexture(const ps4gnm_capture::GpuTextureId& gid, std::vector<uint8_t>* raw) {
    if (!S.enabled.load(std::memory_order_acquire) || !S.tex) return false;
    bool ok = false;
    encq::Run([&] {
        id<MTLTexture> t = S.tex->FindGpuTexture(gid, false);
        ok = t && S.tex->ReadRaw(t, raw);
    });
    return ok;
}

void ReplaySetMetaCleared(uint64_t cmask_address, int slice) {
    if (!S.enabled.load(std::memory_order_acquire)) return;
    // S.meta belongs to the encoder (NotifyGuestWriteImpl): set it there, behind queued work
    encq::Run([=] {
        MetaState& m = S.meta[cmask_address];
        ArmMetaClear(m, kAllMeta, S.committed_seq + 1);
        // only the slice that took it: every other slice counts as taken (slice < 0, a capture
        // without meta_cleared_slices: every slice, until ReplayDisarmMetaCleared after the pass)
        if (slice >= 0 && slice < 64) m.taken_slices = ~(1ull << slice);
    });
}

void ReplayDisarmMetaCleared(uint64_t cmask_address) {
    if (!S.enabled.load(std::memory_order_acquire)) return;
    encq::Run([=] {
        auto it = S.meta.find(cmask_address);
        if (it != S.meta.end()) it->second.cleared = false;
    });
}

uint64_t SkippedCount() { return S.n_skipped.load(std::memory_order_relaxed); }
uint64_t PendingCompileCount() { return SkippedFor(SkipReason::PendingCompile); }
uint64_t SkippedCount(SkipReason r) { return r < SkipReason::Count ? SkippedFor(r) : 0; }
const char* SkipReasonName(SkipReason r) {
    static constexpr const char* kNames[] = {
#define PS4_GNM_SKIP_NAME(e, n) n,
        PS4_GNM_SKIP_REASONS(PS4_GNM_SKIP_NAME)
#undef PS4_GNM_SKIP_NAME
    };
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == (size_t)SkipReason::Count, "a name per reason");
    return r < SkipReason::Count ? kNames[(size_t)r] : "?";
}
uint64_t OnlyClearsSkippedCount() { return S.n_clear_only.load(); }

uint64_t BindUploadCount() { return S.n_bind_uploads.load(std::memory_order_relaxed); }

ImageTableStats GetImageTableStats() {
    ImageTableStats r;
    r.bound = g_table_stats.bound.load(std::memory_order_relaxed);
    r.empty = g_table_stats.empty.load(std::memory_order_relaxed);
    r.rejected = g_table_stats.rejected.load(std::memory_order_relaxed);
    r.unmapped = g_table_stats.unmapped.load(std::memory_order_relaxed);
    r.hazard_drains = g_table_stats.hazard_drains.load(std::memory_order_relaxed);
    r.hazard_skips = g_table_stats.hazard_skips.load(std::memory_order_relaxed);
    return r;
}

// Destroys the backend while everything its completion handlers use is alive: the open work is
// committed and all committed work completes first (FlushImpl: Commit, WaitAll), then the
// renderer stops (S.enabled cleared; its entry points then do nothing, as before Init). Shutdown calls
// it, and so does an exit handler Init registers, for programs that return from main without
// Shutdown (gnm_replay, host tests): exit handlers registered after this file's static objects
// were constructed run before those objects are destroyed. Without it, static destruction would
// destroy the State members after `be` and the globals declared after S first, and the backend's
// destructor (Metal 4 waits for the feedback of every commit) would run the remaining
// CommitDoneFn calls against them.
//
// After a crash (the crash handler's exit(1) runs the exit handlers) it only closes the completion
// gate (CompletionGate): committing, waiting for the GPU or taking the encoder's state from a
// crashed thread can hang or fault again; BackendDelete keeps static destruction from destroying
// the backend too.
void ReleaseBackend() {
    if (ps4_host_crashing()) {
        g_completion_gate.Close();
        return;
    }
    encq::Run([] {
        if (!S.be) return;
        if (S.enabled.load(std::memory_order_acquire)) FlushImpl(true);
        S.enabled.store(false, std::memory_order_release);
        // the presenter's texture hooks go first: a late slot or held texture must not reach a dead backend
        if (ps4_metal_screen_get_device()) ps4_metal_screen_set_texture_hooks(nullptr, nullptr, nullptr);
        g_hook_be.store(nullptr, std::memory_order_release);
        // the cache's alias copies: back to its own queue, which it holds (AliasCopy needs S.be)
        if (S.tex) S.tex->SetGpuCopyHook(TextureCache::GpuCopyHook{});
        g_alias = AliasOrder{}; // its windows, and the hook goes with the backend
        S.be.reset();
    });
}

void Shutdown() {
    if (!S.enabled.load(std::memory_order_acquire)) return;
    Flush(true);
    cap::AtShutdown();
    ReleaseBackend();
}

void CaptureAtQuit() { cap::AtShutdown(); }

} // namespace ps4draw

extern "C" void ps4_gnm_guest_remapped(uint64_t addr, uint64_t size) {
    std::lock_guard<std::mutex> lk(ps4draw::g_remap_mutex);
    ps4draw::g_remapped.push_back({addr, size});
    ps4draw::g_remap_pending.store(true, std::memory_order_release);
}

// Test accessors (tests/host/gnm_guest_memory_test.mm, mode remap): the backend; the windows the
// idle sweep released; the guest window buffers alive; the upload arena's chunks (in use by the
// open command buffer plus its free list).
// The counts are read on the encoder's side (encq::Run), where the renderer changes them.
extern "C" void* ps4_gnm_debug_backend(void) { return ps4draw::S.be.get(); }
// windows the idle sweep released since startup (SweepGuestWindows; remaps are not counted)
extern "C" uint64_t ps4_gnm_debug_sweep_released(void) { return ps4draw::g_mem.released.load(); }
// Tests: maps the 64 MiB guest pages as a draw's harvest does (GmMapPages for draws); returns the
// pages mapped. *copies: read-only copies made so far (g_mem.copies).
extern "C" uint32_t ps4_gnm_debug_map_draw_pages(const uint32_t* pages, uint32_t n, uint64_t* copies) {
    size_t mapped = 0;
    ps4draw::encq::Run([&] {
        if (!ps4draw::EnsureGuestMemoryState()) return;
        ps4draw::GmMapPages(ps4draw::GmPages(pages, pages + n), nullptr, 0, &mapped);
    });
    if (copies) *copies = ps4draw::g_mem.copies.load();
    return (uint32_t)mapped;
}
extern "C" uint32_t ps4_gnm_debug_window_count(void) {
    uint32_t n = 0;
    ps4draw::encq::Run([&] { n = (uint32_t)ps4draw::S.guest_bufs.size(); });
    return n;
}
extern "C" uint32_t ps4_gnm_debug_arena_chunks(void) {
    uint32_t n = 0;
    ps4draw::encq::Run([&] {
        std::lock_guard<std::mutex> lk(ps4draw::S.arena.free_mu);
        n = (uint32_t)(ps4draw::S.arena.cur.size() + ps4draw::S.arena.free_list.size());
    });
    return n;
}

// CPU-side producers (e.g. the video decoder) report guest memory they rewrote so sampled
// textures created from it are re-uploaded. Thread-safe (TextureCache has its own lock).
extern "C" void ps4_gnm_notify_cpu_write(uint64_t addr, uint64_t size) {
    if (ps4draw::S.enabled.load(std::memory_order_acquire) && ps4draw::S.tex) ps4draw::S.tex->InvalidateRange(addr, size, false);
}

// Called by the texture cache (ps4_gnm_resources.mm) for every CPU texture upload.
extern "C" void ps4_gnm_hitch_note_upload(double ms) {
    ps4draw::g_hitch.upload_ms += ms;
    ps4draw::g_hitch.uploads++;
}
