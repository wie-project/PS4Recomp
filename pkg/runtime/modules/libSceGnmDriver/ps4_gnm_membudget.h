// GPU memory accounting and budget (docs/gpu-memory-budget.md).
//
// Guest memory reaches Metal as no-copy MTLBuffers over 64 MiB windows (ps4_gnm_guest_window.h).
// Such a buffer is counted in full by MTLDevice.currentAllocatedSize, and the first command
// buffer using it wires every page of its range (also pages the guest never touched) until the
// buffer is released. The wiring is not part of the process footprint, so it stays invisible in
// the usual memory statistics while it takes physical memory from the whole system.
//
// This module holds the pure accounting and policy decisions (tested in
// tests/host/gpu_membudget_test.mm) plus the few system queries they are fed with. The renderer
// (ps4_gnm_draw.mm) owns the buffers, the sampler thread and the eviction.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One measurement. Byte counts unless noted.
typedef struct ps4_gpumem_sample {
    uint64_t t_ns;            // monotonic time of the sample
    uint64_t wired;           // system-wide wired memory (host_statistics64)
    uint64_t footprint;       // this process's phys_footprint (TASK_VM_INFO)
    uint64_t device;          // MTLDevice.currentAllocatedSize (includes no-copy lengths)
    uint64_t nocopy_count;    // live no-copy guest buffers
    uint64_t nocopy_sum;      // sum of their lengths (buffers spanning several windows overlap)
    uint64_t nocopy_distinct; // guest memory they cover (each window once)
} ps4_gpumem_sample;

// Memory charged against the budget: the device's own allocations (textures, private and
// shared buffers: currentAllocatedSize without the no-copy lengths, which it counts once per
// buffer) plus the guest memory pinned by no-copy buffers (each window once). That is the
// memory the GPU keeps resident or wired on behalf of this process.
uint64_t ps4_gpumem_charged(const ps4_gpumem_sample *s);

typedef struct ps4_gpumem_policy {
    uint64_t mem_total;   // hw.memsize
    uint64_t budget;      // charged bytes allowed
    uint64_t nocopy_warn; // no-copy ranges longer than this are created with a warning (1 GiB)
    uint64_t nocopy_max;  // longest no-copy range created at all (2 GiB, as before the budget)
    uint64_t idle_ns;     // windows unused this long are released (lazy residency)
    uint64_t press_ns;    // over budget: windows unused this long are released
} ps4_gpumem_policy;

// budget_pct / budget_mb are the values of PS4_GNM_GPU_BUDGET_PCT / PS4_GNM_GPU_BUDGET_MB
// (NULL when unset). A valid MB value wins over the percentage; the percentage defaults to 40
// (also when it is not a positive number) and is clamped to [5, 90]. With mem_total 0 (unknown)
// and no MB value there is no budget (UINT64_MAX). idle_s is PS4_GNM_GPU_WINDOW_IDLE_S (default 30, 0 = never).
void ps4_gpumem_policy_init(ps4_gpumem_policy *p, uint64_t mem_total, const char *budget_pct,
                            const char *budget_mb, const char *idle_s);

enum ps4_gpumem_admit {
    PS4_GPUMEM_ADMIT = 0,
    PS4_GPUMEM_ADMIT_LARGE = 1,      // longer than nocopy_warn: created, with a warning
    PS4_GPUMEM_REFUSE_TOO_LARGE = 2, // longer than nocopy_max
    PS4_GPUMEM_REFUSE_HOLES = 3,     // part of the range is unmapped or not readable
};
// Decides whether a no-copy buffer of `length` bytes over a range with (has_holes != 0) or
// without holes may be created. The size decides first, so a caller can pass has_holes 0 to
// reject a too large range before it walks the VM regions. Ranges between nocopy_warn and
// nocopy_max are admitted: they were bound before the budget existed and no copy could stand
// in for them, so refusing them would turn working bindings into dummies.
enum ps4_gpumem_admit ps4_gpumem_admit_nocopy(const ps4_gpumem_policy *p, uint64_t length, int has_holes);

// What a binding gets when its window span was refused (adm is a REFUSE value): 1 = a read-only
// copy of the bound range, 0 = nothing (the caller binds its dummy or fails, as before the budget).
// Only reads of ranges with holes, up to 64 MiB, are copied: GPU writes to a copy would never
// reach guest memory, and a span over nocopy_max implies a range far over the copy limit.
#define PS4_GPUMEM_COPY_MAX (64ull << 20)
int ps4_gpumem_refused_copy(enum ps4_gpumem_admit adm, int written, uint64_t size);

// Should a window last used at last_use_ns be released at now_ns? Windows idle for idle_ns are
// released always (idle_ns 0: never); while over budget, windows idle for press_ns.
int ps4_gpumem_should_release(const ps4_gpumem_policy *p, uint64_t now_ns, uint64_t last_use_ns,
                              int over_budget);

// Budget state with hysteresis: over when charged > budget, back under only below 90% of it,
// so a value at the limit does not flip the state (and the log) on every sample.
typedef struct ps4_gpumem_budget_state {
    int over;
    uint64_t times_over; // transitions into the over state
} ps4_gpumem_budget_state;
// Returns +1 when the state became over, -1 when it recovered, 0 when unchanged.
int ps4_gpumem_budget_update(ps4_gpumem_budget_state *st, const ps4_gpumem_policy *p, uint64_t charged);

// Over-budget purges with a back-off: the transient caches are purged at every over-budget sweep
// until 10 purges in a row did not lower the charged total (the memory over budget is then the
// device's own, e.g. textures, which purging cannot reach). The count restarts once the state is
// under budget or a purge helped again.
typedef struct ps4_gpumem_purge_state {
    uint32_t futile;          // consecutive purges without a lower charged total
    int have;                 // last_charged is valid
    uint64_t last_charged;    // charged total at the previous purge
} ps4_gpumem_purge_state;
// Returns 1 when a purge should run now (and records it).
int ps4_gpumem_purge_due(ps4_gpumem_purge_state *st, int over, uint64_t charged);

// Telemetry rate: a line is due on the first sample, every period_ns, and whenever one of
// wired, footprint, device or nocopy_distinct grew by step bytes since the last line.
typedef struct ps4_gpumem_log_state {
    int have;
    uint64_t t_ns;
    ps4_gpumem_sample last;
} ps4_gpumem_log_state;
int ps4_gpumem_log_due(ps4_gpumem_log_state *st, const ps4_gpumem_sample *s, uint64_t period_ns,
                       uint64_t step);

// Live no-copy buffers over guest windows: per-window reference counts give the distinct
// coverage without walking the buffers. The counters are written with atomic stores, so a
// sampler thread can read them (ps4_gpumem_windows_read) while the renderer adds and removes
// buffers; add/remove are called from one thread.
typedef struct ps4_gpumem_windows {
    uint16_t *refs;   // one count per window
    uint64_t nwin;    // windows in guest memory
    uint64_t win_size;
    uint64_t count, sum, distinct; // live buffers, their total length, covered bytes
} ps4_gpumem_windows;
int ps4_gpumem_windows_init(ps4_gpumem_windows *w, uint64_t mem_size, uint64_t win_size);
void ps4_gpumem_windows_free(ps4_gpumem_windows *w);
// A buffer of `length` bytes starting in window first_win and spanning n windows.
void ps4_gpumem_windows_add(ps4_gpumem_windows *w, uint64_t first_win, uint64_t n, uint64_t length);
void ps4_gpumem_windows_remove(ps4_gpumem_windows *w, uint64_t first_win, uint64_t n, uint64_t length);
// Copies the counters into s->nocopy_count / nocopy_sum / nocopy_distinct (any thread).
void ps4_gpumem_windows_read(const ps4_gpumem_windows *w, ps4_gpumem_sample *s);

// 1 if [p, p + len) has a hole: a page that is not mapped or not readable. That is what
// newBufferWithBytesNoCopy rejects (it accepts read-only pages); a copy must skip such pages.
// Read-only pages are not reported: host guest memory is always read-write (guest mprotect only
// changes bookkeeping), so a GPU-written binding over one cannot arise today.
int ps4_gpumem_range_has_holes(const void *p, uint64_t len);

// Calls fn(ctx, offset, n) for each mapped readable piece of [p, p + len), in order; returns
// the number of readable bytes. Used to copy a range with holes (the holes read as zero).
uint64_t ps4_gpumem_for_each_readable(const void *p, uint64_t len,
                                      void (*fn)(void *ctx, uint64_t offset, uint64_t n), void *ctx);

// ---- Texture cache eviction ----
// Sampled textures are detiled copies of guest memory: an evicted one is created and uploaded
// again on its next use. GPU-owned surfaces (render targets, depth, storage images) hold the only
// copy of what the GPU wrote; the cache evicts them only when superseded
// (TextureCache::EvictSuperseded), never by this policy.
typedef struct ps4_texcache_policy {
    uint64_t budget;   // sampled texture bytes kept; above it the cache evicts down to low_water
    uint64_t low_water;// 90% of budget
    uint64_t keep_ns;  // cache over its budget: textures used within keep_ns stay
    uint64_t press_ns; // GPU memory over budget: textures used within press_ns stay, all others go
} ps4_texcache_policy;
// tex_mb: PS4_GNM_TEX_CACHE_MB (NULL when unset; "0" = no cache budget, only GPU budget
// pressure evicts). Default: a twelfth of the GPU budget gp->budget, 546 MiB on 16 GB (U4 chapter 1
// keeps its 2 s working set under 512 MiB without an fps cost; 256 MiB thrashes). UINT64_MAX stays unlimited.
// keep_ms: PS4_GNM_TEX_KEEP_MS (default 2000). Both ages are at least 100 ms: a texture just
// looked up for the open command buffer is never a candidate, even before it is bound.
void ps4_texcache_policy_init(ps4_texcache_policy *p, const ps4_gpumem_policy *gp, const char *tex_mb,
                              const char *keep_ms);

// 1 when an eviction pass is worth running: GPU memory over budget, or the sampled bytes over
// the cache budget.
int ps4_texcache_evict_due(const ps4_texcache_policy *p, uint64_t sampled_bytes, int gpu_over);

typedef struct ps4_texcache_item {
    uint64_t last_ns;  // time of the last lookup (the commit time it was stamped with)
    uint64_t last_seq; // sequence number of the command buffer that last used it
    uint64_t bytes;
    uint64_t id;       // caller's handle
} ps4_texcache_item;
// Chooses the textures to evict out of items[0..n), whose bytes sum to `total`: it reorders
// items so that the victims come first and returns their count (their bytes in *freed).
// A texture is a candidate only when every command buffer that may use it has completed
// (last_seq <= completed_seq) and it was not used within keep_ns (press_ns when gpu_over).
// Least recently used candidates go first, until total - freed <= low_water; with gpu_over,
// every candidate goes.
uint64_t ps4_texcache_select(ps4_texcache_item *items, uint64_t n, uint64_t total, const ps4_texcache_policy *p,
                             int gpu_over, uint64_t now_ns, uint64_t completed_seq, uint64_t *freed);

// System inputs of a sample: wired (system-wide), phys_footprint, hw.memsize.
uint64_t ps4_gpumem_wired_bytes(void);
uint64_t ps4_gpumem_footprint_bytes(void);
uint64_t ps4_gpumem_mem_total(void);

// Formats one telemetry line (without newline) into buf.
void ps4_gpumem_format(char *buf, unsigned long size, const ps4_gpumem_sample *s, uint64_t wired_base,
                       const ps4_gpumem_policy *p);

#ifdef __cplusplus
}
#endif
