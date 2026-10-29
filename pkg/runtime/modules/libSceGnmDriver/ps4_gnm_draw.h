// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4Recomp - native Metal rendering backend for the GNM command processor.
 *
 * ps4_gnm_metal.mm walks the PM4 streams (register file, fences, DMA, ...) and calls into this
 * module for every draw / dispatch. Here the GCN shaders are recompiled to MSL (gcn_recompiler),
 * pipelines are built from the Liverpool register file (ps4_gnm_resources), resources are bound
 * straight out of guest memory (UMA, no-copy MTLBuffers) and the work is encoded into Metal
 * command buffers.
 *
 * The draw/dispatch flow is a port of shadPS4's Vulkan rasterizer
 * (src/video_core/renderer_vulkan/vk_rasterizer.cpp, vk_pipeline_cache.cpp,
 *  vk_graphics_pipeline.cpp, vk_compute_pipeline.cpp, vk_pipeline_common.cpp).
 * SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * All functions must be called with the GNM processing lock held (ps4_gnm_metal.mm's
 * g_state.mutex), except where noted.
 */
#ifndef PS4_GNM_DRAW_H
#define PS4_GNM_DRAW_H

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <functional>
#include <vector>

#include "recomp_runtime.h"
#include "ps4_gnm_capture.h"

namespace ps4draw {

struct DrawArgs {
    bool indexed = false;
    uint64_t index_base = 0;   // guest address of the index buffer (INDEX_BASE / DRAW_INDEX_2)
    uint32_t index_type = 0;   // VGT_INDEX_TYPE: 0 = 16 bit, 1 = 32 bit
    uint32_t index_offset = 0; // first index (DRAW_INDEX_OFFSET_2)
    uint32_t count = 0;        // index / vertex count
    uint32_t max_index = 0;    // max_size of the index buffer (indices), 0 = unknown
    uint32_t num_instances = 1;
    const char* name = "draw";
    // DRAW_INDIRECT / DRAW_INDEX_INDIRECT executed on the GPU: guest address of the argument
    // record (layout identical to MTLDraw[Indexed]PrimitivesIndirectArguments), and the user
    // data slots (0..15) the CP would write baseVertex / startInstance into (-1: none).
    uint64_t indirect_addr = 0;
    int base_vertex_ud = -1;
    int start_instance_ud = -1;
};

// Initializes Metal state on first use. Returns false if the backend is disabled
// (PS4_GNM_METAL_DRAW=0) or Metal is unavailable.
bool Init(GuestContext* ctx);
bool Enabled();

// CLEAR_STATE: reset the register file to the hardware defaults (shadPS4 Regs::SetDefaults).
void SetRegDefaults(uint32_t* regs);

// Returns false only for an indirect draw that cannot take its arguments from GPU memory
// (nothing was encoded; the caller reads the arguments on the CPU and draws directly).
bool Draw(GuestContext* ctx, const uint32_t* regs, const DrawArgs& args);
// The queues whose synchronization points order the CP's reads (ComputeBarrier, Dispatch): the
// graphics ring and the compute (asc) rings.
enum { kQueueGfx = 0, kQueueCompute = 1 };
// Compute dispatch with the COMPUTE_* registers in `regs`. indirect_addr != 0: read the group
// counts from guest memory at that address (GPU side). name: the queue in logs ("gfx", "asc",
// "asc-indirect"); queue: kQueueGfx or kQueueCompute.
void Dispatch(GuestContext* ctx, const uint32_t* regs, uint32_t dim_x, uint32_t dim_y,
              uint32_t dim_z, uint64_t indirect_addr, const char* name, int queue);

// Run `fn` once all GPU work encoded so far has completed (fences/labels/interrupts: EOP,
// RELEASE_MEM, EOS). Commits the open command buffer. Actions run in submission order, on the
// calling thread when nothing is in flight, otherwise on Metal's completion thread.
void AfterGpu(std::function<void()> fn);

// Surface extents from the NOP hints after CB/DB register setup (0 = unknown); raw = w | h << 16.
void SetExtentHints(const uint32_t cb[8], uint32_t db);

// Called once per sceGnmSubmit* (trace windows, stats).
void OnSubmit();

// End of a command stream run (CP thread): waits for compute kernels that its dispatches still
// compile in the background (ps4_gnm_kernel_compile.h), so the CP does not run further ahead.
void WaitKernelCompiles();

// True if GPU work was encoded since the last Flush(true).
bool HasPendingWork();
// Guest dispatches run concurrently (as consecutive dispatches do on GCN); the command
// processor calls this where the guest synchronizes (cache flushes, partial flushes, fences,
// waits, CP writes), so later dispatches see the memory written by earlier ones. `queue`: the
// queue whose packet it is; from here on the CP reads that queue does for its later draws and
// dispatches see what the guest memory dispatches handed out so far write.
void ComputeBarrier(int queue);
// Commit the open command buffer; wait for all submitted work to finish when `wait`.
void Flush(bool wait);
// The end of a compute submission (CP thread): its work is committed now, or held in the open
// command buffer while at least 2 seqs are in flight (the GPU is behind: committing later starts
// it no later), so that back-to-back submissions share a command buffer and an encoder. Held work
// is committed by the next Flush (a graphics submission, a CPU sync, a CP wait, a present), when
// fewer than 2 seqs are in flight, or 0.5 ms after it was first held, whichever comes first
// (docs/compute-batching.md). Its labels and interrupts come later by at most that, plus the
// release thread's wake-up and the encoder jobs already queued before the release: the release
// is an encoder job. Later submissions while it is held do not move the bound.
// PS4_GNM_COMPUTE_BATCH=0 turns the hold off: every call commits at once (docs/compute-batching.md).
void FlushCompute();
// Test hook: compute commits held (FlushCompute) and released by the time bound and by the
// backlog draining, since Init; whether work is held now.
struct ComputeHoldStats {
    uint64_t held = 0, by_time = 0, by_backlog = 0;
    bool holding = false;
};
ComputeHoldStats GetComputeHoldStats();
// Test hook: the bound on held compute work, in ns (0: the default, 0.5 ms).
void DebugSetComputeHoldNs(uint64_t ns);
// Test hook: committed seqs, compute encoders opened and compute barriers encoded, since Init.
struct EncodeCounts {
    uint64_t committed = 0, compute_encoders = 0, compute_barriers = 0;
};
EncodeCounts GetEncodeCounts();

// CP DMA fill / CPU-side writes of guest memory (fast-clear metadata tracking, texture
// invalidation).
// cp_dma: the write is a CP DMA done on the CPU (exact bytes written by GPU work; see
// TextureCache::NoteBufferWrite).
void NotifyGuestWrite(uint64_t addr, uint64_t size, bool is_fill, uint32_t fill_value, bool cp_dma = false);

// 64 KiB GDS backing store, shared between CP DMA (CPU) and shaders (GPU).
uint8_t* Gds();
// Encodes a CP DMA_DATA copy (or a 32-bit fill when src_fill) between guest memory and GDS on
// the GPU timeline, behind the work encoded so far, so the CPU does not wait for the GPU.
// Returns false if it cannot (unaligned range, unmapped memory); the caller then drains and
// copies on the CPU.
bool GpuDma(bool src_gds, bool src_fill, uint64_t src, uint32_t fill_value, bool dst_gds, uint64_t dst, uint32_t n);

// Presentation: the display buffer at `addr` (a GPU render target, or guest memory copied on the
// GPU behind the frame's work) is rendered into a BGRA8 texture and handed to the Metal screen
// without waiting for the GPU or for the display (a frame that finds no free present slot is
// held by the screen and handed over later). `on_done` (the flip completion) runs once the frame
// has reached a present slot, or was dropped, in the order of the Present calls. Returns false
// only without a renderer; the caller then presents on the CPU and `on_done` is not called.
// Flip-label waits of the command processor (sceGnmInsertWaitFlipDone, counted by
// ps4_gnm_metal.mm): graphics queue suspensions, waits given up, wake-ups of the wait thread by a
// released label. Kept here so the per-300-presents perf line can print them.
struct FlipWaitCounters {
    std::atomic<uint64_t> suspends{0}, timeouts{0}, wakes{0};
};
FlipWaitCounters& FlipWaits();

bool Present(GuestContext* ctx, uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch,
             uint32_t tiling_mode, uint32_t pixel_format, std::function<void()> on_done);
// Test hook: the GPU work encoded from now on waits for an event that is never signalled (a hung GPU).
void DebugStallGpu();
void DebugReleaseGpu();
// Test hook: the GPU work encoded from now on waits until `shared_event` (an id<MTLSharedEvent>
// of the renderer's device) reaches `value`.
void DebugGpuWait(void* shared_event, uint64_t value);
// Test hook: a GPU fill of `value` into a fresh chunk of the upload arena (larger than a recycled
// one, so made for this call), waited for; `out` gets the chunk's first `n` bytes as the CPU then
// reads them. False without a renderer.
bool DebugArenaGpuFill(uint8_t value, size_t n, std::vector<uint8_t>* out);
// Test hook (tests/host/gnm_alias_dma_test.mm): one renderer job with two encoders, as a draw
// whose bind encodes an upload first: it binds guest [src, src + n) and [dst, dst + n), fills an
// arena buffer in one copy encoder, then copies src to dst in another (not waited for).
bool DebugTwoEncoderCopy(uint64_t src, uint64_t dst, uint32_t n);

// ---- Pass replay (tools/gnm_replay, docs/gnm-capture.md). Call after Init, like Draw. ----
// A GPU-owned texture-cache entry with the captured identity (created if missing), filled with
// `raw` (ps4gnm_capture::RawSize layout) when given.
bool ReplayAdoptTexture(const ps4gnm_capture::GpuTextureId& gid, const std::vector<uint8_t>* raw);
// Contents of the entry with that identity; false if the renderer has none.
bool ReplayReadTexture(const ps4gnm_capture::GpuTextureId& gid, std::vector<uint8_t>* raw);
// The fast clear of the CMASK/HTILE at this address is pending (as the captured pass found it),
// for `slice` only (the slice whose draw took it; -1: not recorded, every slice).
void ReplaySetMetaCleared(uint64_t cmask_address, int slice);
// It is no longer pending (after a replayed pass whose capture did not record the slice).
void ReplayDisarmMetaCleared(uint64_t cmask_address);
// Draws and dispatches skipped so far (a replayed pass must not add one).
uint64_t SkippedCount();
// Why a draw or dispatch was skipped: one reason per skip site (or group of sites with one cause).
// The per-reason counts add up to SkippedCount; the "present #" totals line is followed by a
// "skipped by reason:" line with the non-zero ones. X(enumerator, name printed in the log).
#define PS4_GNM_SKIP_REASONS(X)                                                                 \
    X(PendingCompile, "pending-compile")   /* a shader or pipeline compiles in the background */  \
    X(DrawCount, "implausible-count")      /* draw count or instances out of range */            \
    X(ShaderStages, "shader-stages")       /* LS/HS/ES/GS enabled (VGT_SHADER_STAGES_EN) */      \
    X(VsUnavailable, "vs-unavailable")     /* VS translation or library failed */                \
    X(RectVariant, "rect-variant")         /* VS without a RectList variant */                   \
    X(PsUnavailable, "ps-unavailable")     /* PS translation or library failed */                \
    X(VertexFormat, "vertex-format")       /* attribute without a Metal vertex format or slot */ \
    X(VertexAlign, "vertex-align")         /* attribute stride or address not a multiple of 4 */ \
    X(PipelineFailed, "pipeline-failed")   /* render pipeline build failed */                    \
    X(ScissorEmpty, "empty-scissor")       /* the scissor covers no pixel of the target */       \
    X(IndirectArgs, "indirect-args")       /* indirect draw arguments or indices not bindable */ \
    X(QuadIndices, "quad-indices")         /* QuadList index expansion failed */                 \
    X(Primitive, "primitive")              /* primitive type not supported */                    \
    X(IndexBuffer, "index-buffer")         /* index buffer not in guest memory */                \
    X(RectCount, "rect-count")             /* RectList without 3 vertices per rectangle */       \
    X(RectSize, "rect-size")               /* RectList capture over its 64 MiB bound */          \
    X(Arena, "arena")                      /* no arena memory for the draw's buffers */          \
    X(TableHazard, "table-hazard")         /* draw: T# tables no longer fit the permutation */   \
    X(VsBind, "vs-bind")                   /* VS resources could not be bound */                 \
    X(PsBind, "ps-bind")                   /* PS resources could not be bound */                 \
    X(Reencode, "reencode")                /* the encoder restarted after a hazard flush */      \
    X(PassBegin, "pass-begin")             /* the render pass could not begin */                 \
    X(NoCompute, "no-compute")             /* backend without compute */                         \
    X(CsUnavailable, "cs-unavailable")     /* CS translation failed */                           \
    X(CsLibrary, "cs-library")             /* Metal rejected the CS's MSL */                     \
    X(CsPipeline, "cs-pipeline")           /* compute pipeline build failed */                   \
    X(CsWorkgroup, "cs-workgroup")         /* workgroup larger than the pipeline allows */       \
    X(CsTableHazard, "cs-table-hazard")    /* dispatch: T# tables no longer fit the permutation */ \
    X(CsBind, "cs-bind")                   /* CS resources could not be bound */                 \
    X(CsIndirectArgs, "cs-indirect-args")  /* indirect dispatch arguments not bindable */        \
    X(GmNoState, "gm-no-state")            /* guest memory dispatch: no guest memory state */    \
    X(GmNoSlot, "gm-no-slot")              /* guest memory dispatch: no fault buffer */          \
    X(GmPages, "gm-pages")                 /* guest pages missing or not mappable */             \
    X(GmStatus, "gm-status")               /* refused for a guest memory access status */        \
    X(GmIndirectSize, "gm-indirect-size")  /* indirect dispatch size unreadable */               \
    X(GmCmdFailed, "gm-cmdbuf-failed")     /* its command buffer failed */
enum class SkipReason : uint8_t {
#define PS4_GNM_SKIP_ENUM(e, n) e,
    PS4_GNM_SKIP_REASONS(PS4_GNM_SKIP_ENUM)
#undef PS4_GNM_SKIP_ENUM
    Count
};
const char* SkipReasonName(SkipReason r);
// Draws and dispatches skipped for this reason so far.
uint64_t SkippedCount(SkipReason r);
// The part of SkippedCount that is draws deferred while a shader or pipeline they need compiles in
// the background (they are drawn once it is ready); SkippedCount() - PendingCompileCount() is the
// work really lost.
uint64_t PendingCompileCount();
// Draws skipped by PS4_GNM_MTL4_ONLY_CLEARS (debug) after their render pass began, so far.
uint64_t OnlyClearsSkippedCount();
// Sampled textures copied from guest memory on the GPU while a draw or dispatch was being bound
// (into the texture-upload command buffer) so far.
uint64_t BindUploadCount();

// Guest memory dispatches (shaders with gcn_shader_info.uses_guest_memory) so far: on the sync
// path (waited for) and on the fast path (no CPU wait), fast ones re-run after their probe met an
// unmapped page or a page outside the prediction, fast ones skipped for a status, dispatches that
// wrote outside their prediction, CP waits for fast ones (and their time); and the CPU waits for
// the GPU (WaitAll and re-runs).
struct GuestMemoryStats {
    uint64_t sync = 0, fast = 0, reruns = 0, refused = 0, mispredicted = 0;
    uint64_t cp_waits = 0, cp_wait_us = 0, waits = 0;
    uint64_t chains = 0;            // points where the GPU waits for the CPU's verdict on fast ones
    uint64_t rerun_wait_us = 0;     // from a miss's verdict to its re-run, summed
    uint64_t rerun_wait_max_us = 0; // the longest of them
    uint64_t no_slot = 0, no_indirect = 0, pass_failed = 0; // skipped: no fault buffer, no
                                                            // indirect argument buffer, binding failed
    uint64_t final_ticket = 0; // the last fast dispatch whose result is final
    uint64_t draw_mapped = 0;   // 64 MiB pages mapped after draws met them unmapped (GmDrawSlot)
    uint64_t draw_harvests = 0; // harvests of the draws' fault slot (one per present with such draws)
    uint64_t draw_unmappable = 0; // tries to map a 64 MiB page for draws that failed (one per page
                                  // until a remap or the refused spans' idle reset)
};
GuestMemoryStats GetGuestMemoryStats();
// Renderer gave up on the GPU (a GPU watchdog fired): nothing more is rendered.
bool Hung();
// Tests: the result of the next fast guest memory dispatch is never made final (as if the CPU
// never got its verdict), to test the waits that would otherwise never end.
void TestWithholdNextTicket();
// Tests: locks (hold) or unlocks the texture cache's lock on the calling thread.
void TestHoldTextureCacheLock(bool hold);

// T# tables a shader indexes at run time (gcn_image_table): their entries as bound so far.
// bound: a texture was bound; empty: the entry's T# is all zero (past the table, or an unused
// loop entry); rejected: a T# was there but no texture could be bound for it (not a valid T#, or
// not one Metal can create); unmapped: a T# at a provably unmapped guest address.
// hazard_drains: draws / dispatches whose table reads overlapped GPU writes that had not completed
// when the CP copied the tables (the GPU was drained if they were still pending, and the tables of
// every stage read again), one per draw or dispatch; hazard_skips: of those, the ones skipped
// because the tables read again no longer fit a stage's specialization. Guest memory dispatches
// count their entries once per probe (their pass binds what the probe bound).
struct ImageTableStats {
    uint64_t bound = 0, empty = 0, rejected = 0, unmapped = 0, hazard_drains = 0, hazard_skips = 0;
};
ImageTableStats GetImageTableStats();

void Shutdown();
// Quit (ps4_host_quit): a frame capture whose frame has not presented writes its manifest.
// Called with the CP lock held.
void CaptureAtQuit();

} // namespace ps4draw

#endif // PS4_GNM_DRAW_H
