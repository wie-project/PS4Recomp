// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - the Metal 4 renderer backend (ps4_gnm_backend_metal4.h).
#include "ps4_gnm_backend_metal4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <chrono>

namespace ps4gnm {

// What this backend advertises: a capability only once the renderer makes
// everything it touches resident. Every operation is implemented (Tasks 2.2-4.1). Clears: render
// targets, presenter textures and built-ins (Task 2.4). Compute and copies: also the guest windows,
// the upload arena, the guest page table, the guest memory dispatch slots and the detile / DMA
// buffers (Task 3.3). Draws: the renderer's own textures too (Task 4.2), checked by every
// renderer-level host test on Metal 4 with PS4_GNM_RESIDENCY_CHECK=1 (Task 4.3).
// Backend tests drive the backend directly.
// The second queue (kGnmCapAsyncCompute) since Task 10.2.1.
const uint32_t kMetal4BackendCaps = kGnmCapAll | kGnmCapAsyncCompute;

// The visibility of the renderer's barriers (encoder starts, the renderer's own barriers, the
// barriers between copies and dispatches of one encoder): guest memory is reached through several
// no-copy buffers over the same pages (a 64 MiB window and a buffer spanning windows), i.e. at more
// than one GPU virtual address, and MTL4VisibilityOptionResourceAlias is what makes aliased virtual
// addresses consistent (MTL4CommandEncoder.h). On the M1 Pro the difference is not observable
// (gnm_residency_test TestAliasedViews passes even with execution-only barriers), so this follows
// the header rather than a measurement (docs/metal4-backend.md).
constexpr MTL4VisibilityOptions kGuestVisibility = MTL4VisibilityOptionDevice | MTL4VisibilityOptionResourceAlias;

std::unique_ptr<GnmBackend> MakeMetal4Backend() { return std::make_unique<Metal4Backend>(); }

static bool EnvOn(const char* name) {
    const char* v = getenv(name);
    return v && !strcmp(v, "1");
}

static MTLLoadAction LoadOf(GnmLoad l) {
    return l == GnmLoad::Clear ? MTLLoadActionClear : l == GnmLoad::DontCare ? MTLLoadActionDontCare : MTLLoadActionLoad;
}
static MTLStoreAction StoreOf(GnmStore s) {
    return s == GnmStore::MultisampleResolve ? MTLStoreActionMultisampleResolve : MTLStoreActionStore;
}

// Every stage an earlier encoder may have run work in.
constexpr MTLStages kAllStages = MTLStageVertex | MTLStageFragment | MTLStageDispatch | MTLStageBlit;

// How long the destructor waits for outstanding commit feedback before it reports a GPU that
// never finishes (destroying the state the feedback handlers use would be a use after free).
constexpr auto kFeedbackDrainTimeout = std::chrono::seconds(10);

Metal4Backend::Metal4Backend() {
    ops_.buffer = OpBuffer;
    ops_.buffer_offset = OpBufferOffset;
    ops_.bytes = OpBytes;
    ops_.texture = OpTexture;
    ops_.sampler = OpSampler;
    ops_.use = OpUse;
}

// A GPU that never finishes is FATAL here after kFeedbackDrainTimeout: the destructor waits for every commit
// feedback before the members its handlers use die. That includes a
// queue-level wait for an event nobody signals any more (QueueWait issues it at once, e.g. a guest
// memory chain's closing wait): a process that destroys its backend then aborts at exit after
// 10 s instead of returning. The game leaves through _exit and is not affected.
Metal4Backend::~Metal4Backend() {
    // Command buffers still being recorded are dropped, not committed.
    for (TargetState& x : t_) {
        if (x.renc) [x.renc endEncoding];
        if (x.cenc) [x.cenc endEncoding];
        if (x.cb) [x.cb endCommandBuffer];
    }
    const long long secs = (long long)std::chrono::duration_cast<std::chrono::seconds>(kFeedbackDrainTimeout).count();
    {
        std::unique_lock<std::mutex> lk(seq_mu_);
        if (!feedback_cv_.wait_for(lk, kFeedbackDrainTimeout, [&] { return feedback_pending_ == 0; })) {
            fprintf(stderr, "[gnm-draw] FATAL: Metal4Backend destroyed with the feedback of %llu commits outstanding "
                            "after %lld s (a GPU hang?)\n", (unsigned long long)feedback_pending_, secs);
            abort();
        }
    }
    // Queue operations issued after the last commit (the timeline signal, deferred signals) may
    // still be pending after its feedback: one more signal runs after all of them.
    for (id<MTL4CommandQueue> q : {queue_, async_queue_}) {
        if (!q) continue;
        id<MTLSharedEvent> drained = [dev_ newSharedEvent];
        [q signalEvent:drained value:1];
        if (![drained waitUntilSignaledValue:1 timeoutMS:(uint64_t)secs * 1000]) {
            fprintf(stderr, "[gnm-draw] FATAL: Metal4Backend destroyed with queue operations still pending after %lld s "
                            "(a GPU hang?)\n", secs);
            abort();
        }
    }
    // The command queues go first: they use feedback_q_ without holding a reference to it.
    queue_ = nil;
    async_queue_ = nil;
    feedback_q_ = nullptr;
}

bool Metal4Backend::Init(id<MTLDevice> dev, std::string* why) {
    dev_ = dev;
    if (![dev supportsFamily:MTLGPUFamilyMetal4]) {
        if (why) *why = "the GPU does not support MTLGPUFamilyMetal4";
        return false;
    }
    // a pool per work item: CommitDoneFn and the fence actions it runs autorelease (the failure
    // path's error description among them), and the worker of a busy queue may not drain its own
    feedback_q_ = dispatch_queue_create(
        "gnm.mtl4.feedback",
        dispatch_queue_attr_make_with_autorelease_frequency(
            dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0),
            DISPATCH_AUTORELEASE_FREQUENCY_WORK_ITEM));
    MTL4CommandQueueDescriptor* qd = [MTL4CommandQueueDescriptor new];
    qd.label = @"gnm";
    // Ownership of the feedback queue (measured on macOS 27: the dispatch queue's retain count):
    // feedbackQueue is declared assign, yet the descriptor releases the queue when it is freed,
    // and the command queue made from it holds no reference. So the descriptor gets a +1 of its
    // own, which its release consumes, and feedback_q_ (strong) keeps the dispatch queue alive
    // until the destructor has released queue_. Probed with the macOS 27.0 SDK. Should a later SDK
    // stop releasing it, this +1 is one dispatch queue leaked per backend, never an over-release:
    // gnm_backend_cmd_test (TestFeedbackQueueFreed) then fails.
    // The second queue's descriptor (AsyncCompute) gets its own +1 the same way.
    NSError* err = nil;
    for (int q = 0; q < 2; ++q) {
        MTL4CommandQueueDescriptor* d = q ? [MTL4CommandQueueDescriptor new] : qd;
        if (q) d.label = @"gnm async compute";
        (void)CFBridgingRetain(feedback_q_);
        d.feedbackQueue = feedback_q_;
        id<MTL4CommandQueue> made = [dev newMTL4CommandQueueWithDescriptor:d error:&err];
        d = nil;
        qd = nil;
        if (!made) {
            if (why) *why = err ? err.localizedDescription.UTF8String : "newMTL4CommandQueueWithDescriptor failed";
            return false;
        }
        (q ? async_queue_ : queue_) = made;
    }
    side_queue_ = [dev newCommandQueue];
    side_queue_.label = @"gnm side (Metal 3)";
    timeline_ = [dev newSharedEvent];
    timeline_.label = @"gnm timeline";
    async_timeline_ = [dev newSharedEvent];
    async_timeline_.label = @"gnm async compute timeline";
    if (!res_.Init(dev, why)) return false;
    for (int c = 0; c < kGnmResidencyClasses; ++c) {
        [queue_ addResidencySet:res_.Set((GnmResidencyClass)c)];
        [async_queue_ addResidencySet:res_.Set((GnmResidencyClass)c)];
    }
    MTL4ArgumentTableDescriptor* ad = [MTL4ArgumentTableDescriptor new];
    ad.maxBufferBindCount = 31;
    ad.maxTextureBindCount = 128;
    ad.maxSamplerStateBindCount = 16;
    for (auto& t : t_)
        for (auto& tab : t.table) {
            tab = [dev newArgumentTableWithDescriptor:ad error:&err];
            if (!tab) {
                if (why) *why = err ? err.localizedDescription.UTF8String : "newArgumentTableWithDescriptor failed";
                return false;
            }
        }
    full_barriers_ = EnvOn("PS4_GNM_MTL4_FULL_BARRIERS");
    no_barriers_ = EnvOn("PS4_GNM_MTL4_NO_BARRIERS");
    check_residency_ = EnvOn("PS4_GNM_RESIDENCY_CHECK");
    return true;
}

// The allocator sizes are counted where they change on this thread: after a part's recording
// (Submit), after its reset (Recycle) and for a new allocator (AcquirePart). Unsigned arithmetic:
// a smaller size subtracts.
void Metal4Backend::NoteAllocatorSize(Part& p) {
    const uint64_t now = p.alloc.allocatedSize;
    alloc_bytes_.fetch_add(now - p.bytes, std::memory_order_relaxed);
    p.bytes = now;
}

// After its reset an allocator still holds the largest part it ever recorded. One over the trim
// threshold is kept on the large list (a few large parts per frame need one) or, the list full,
// it or the largest one listed is released; then
// free allocators beyond the recent in-flight peak are released, the largest first. Only parts
// of completed seqs are touched: a busy allocator is never released.
void Metal4Backend::Recycle() {
    const uint64_t done = CompletedSeq();
    const uint64_t limit = TrimThreshold();
    while (!busy_.empty() && busy_.front().second <= done) {
        Part p = std::move(busy_.front().first);
        busy_.pop_front();
        busy_popped_++;
        [p.alloc reset];
        p.keep.clear(); // the GPU is done with what the part's encoders used
        NoteAllocatorSize(p);
        resets_.fetch_add(1, std::memory_order_relaxed);
        if (p.bytes <= limit) {
            free_small_.push_back(std::move(p));
        } else if (free_large_.size() < kTrimLargeKeep) {
            free_large_.push_back(std::move(p));
        } else {
            // the list is full: the largest of the list and this one goes, so a pathological
            // part's allocator does not stay for good while smaller ones are released
            size_t big = 0;
            for (size_t i = 1; i < free_large_.size(); ++i)
                if (free_large_[i].bytes > free_large_[big].bytes) big = i;
            if (free_large_[big].bytes > p.bytes) std::swap(free_large_[big], p);
            ReleasePart(p);
            released_at_reset_++;
        }
    }
    const size_t keep = std::max(peak_cur_, peak_prev_) + kTrimPeakMargin;
    while (allocators_.load(std::memory_order_relaxed) > keep && !(free_small_.empty() && free_large_.empty())) {
        std::vector<Part>& from = free_large_.empty() ? free_small_ : free_large_;
        size_t big = 0;
        for (size_t i = 1; i < from.size(); ++i)
            if (from[i].bytes > from[big].bytes) big = i;
        std::swap(from[big], from.back());
        ReleasePart(from.back());
        from.pop_back();
    }
    while (!chunks_busy_.empty() && chunks_busy_.front().last_seq <= done) {
        chunks_free_.push_back(chunks_busy_.front().buf);
        chunks_busy_.pop_front();
    }
    res_.Retire(done);
}

void Metal4Backend::ReleasePart(Part& p) {
    alloc_bytes_.fetch_sub(p.bytes, std::memory_order_relaxed);
    allocators_.fetch_sub(1, std::memory_order_relaxed);
    released_.fetch_add(1, std::memory_order_relaxed);
    p = Part{};
}

void Metal4Backend::NoteFreshSize(uint64_t bytes) {
    size_ring_[size_samples_ % kTrimSizeSamples] = bytes;
    ++size_samples_;
    // the median of the samples so far, every 32 samples (and for each of the first 32)
    if (size_samples_ > 32 && size_samples_ % 32) return;
    const size_t n = (size_t)std::min<uint64_t>(size_samples_, kTrimSizeSamples);
    std::copy(size_ring_.begin(), size_ring_.begin() + n, size_scratch_.begin());
    std::nth_element(size_scratch_.begin(), size_scratch_.begin() + n / 2, size_scratch_.begin() + n);
    size_median_ = size_scratch_[n / 2];
}

// A part is predicted to have a render pass when the last part submitted on its target had one,
// or (Main) a part near its position in the previous frame had one. One predicted to have a
// render pass takes a large free allocator, else the smallest small one; a compute part takes the
// smallest small one, else a new allocator, never a large one (it would keep it large). A compute
// part predicted as render costs a large allocator for one seq; a render part predicted as
// compute grows a small allocator, which its reset then moves to the large list or releases.
Metal4Backend::Part Metal4Backend::AcquirePart(GnmCmdTarget t) {
    Recycle();
    auto take = [](std::vector<Part>& from, bool smallest) {
        size_t k = from.size() - 1;
        if (smallest)
            for (size_t i = 0; i + 1 < from.size(); ++i)
                if (from[i].bytes < from[k].bytes) k = i;
        std::swap(from[k], from.back());
        Part p = std::move(from.back());
        from.pop_back();
        p.rendered = false;
        return p;
    };
    // Main: a render part at this position of the previous frame or up to kFrameSlack positions
    // around it (a frame's parts shift by a few from frame to frame), or a render predecessor
    bool render = last_rendered_[(int)t];
    if (t == GnmCmdTarget::Main)
        for (size_t i = frame_idx_ > kFrameSlack ? frame_idx_ - kFrameSlack : 0;
             !render && i <= frame_idx_ + kFrameSlack && i < frame_prev_n_; ++i)
            render = frame_prev_[i] != 0;
    if (render && !free_large_.empty()) return take(free_large_, false);
    if (!free_small_.empty()) return take(free_small_, true);
    // none free for its class: a new allocator (a compute part never takes a large one, it would
    // keep it large)
    Part p;
    p.cb = [dev_ newCommandBuffer];
    p.alloc = [dev_ newCommandAllocator];
    if (!p.cb || !p.alloc) {
        fprintf(stderr, "[gnm-draw] FATAL: Metal4Backend: %s returned nil (%llu allocators alive)\n",
                p.cb ? "newCommandAllocator" : "newCommandBuffer",
                (unsigned long long)allocators_.load(std::memory_order_relaxed));
        abort();
    }
    p.fresh = true;
    allocators_.fetch_add(1, std::memory_order_relaxed);
    created_.fetch_add(1, std::memory_order_relaxed);
    NoteAllocatorSize(p);
    return p;
}

void Metal4Backend::OpenGroup(int q) {
    std::lock_guard<std::mutex> lk(evict_mu_);
    open_queues_ |= uint8_t(1u << q);
}

void Metal4Backend::MakeResident(id<MTLAllocation> a, GnmResidencyClass c) {
    {
        std::lock_guard<std::mutex> lk(evict_mu_);
        // held again: the waiting removals are cancelled
        pending_evicts_.erase(std::remove_if(pending_evicts_.begin(), pending_evicts_.end(),
                                             [a](const PendingEvict& e) { return e.a == a; }),
                              pending_evicts_.end());
    }
    res_.Add(a, c);
}

void Metal4Backend::Evict(id<MTLAllocation> a, uint64_t after_seq) {
    {
        std::lock_guard<std::mutex> lk(evict_mu_);
        if (open_queues_) {
            pending_evicts_.push_back({a, after_seq, open_queues_});
            return;
        }
    }
    res_.Remove(a, after_seq);
}

void Metal4Backend::BeginPart(GnmCmdTarget t, NSString* label) {
    OpenGroup(QueueOf(t));
    TargetState& x = t_[(int)t];
    x.part = AcquirePart(t);
    x.cb = x.part.cb;
    [x.cb beginCommandBufferWithAllocator:x.part.alloc];
    x.cb.label = label;
    x.work = false;
}

// encoder_status: Metal 4 has no per-encoder status (MTL4CommandBufferOptions holds only a log
// state); the renderer warns once at startup that PS4_GNM_ENCODER_STATUS is Metal 3 only.
void Metal4Backend::BeginCmd(GnmCmdTarget t, NSString* label, bool) { BeginPart(t, label); }

// On Metal's feedback queue (serial; both queues use it). A seq is done when it is final and every
// part's feedback arrived. Completions of one queue run in seq order: a seq done before an earlier
// one of its queue waits for it; the other queue's seqs do not hold it. fed_ covers every seq
// about to be delivered whose earlier seqs (both queues) are delivered too, before the first
// completion runs. Each completion runs before this commit stops counting as pending, so the
// destructor never returns while one may still run.
void Metal4Backend::OnFeedback(uint64_t gid, id<MTL4CommitFeedback> fb) {
    std::unique_lock<std::mutex> serial(feedback_mu_); // released before this commit stops counting as pending
    ready_.clear();
    {
        std::lock_guard<std::mutex> lk(seq_mu_);
        SeqRec& r = recs_[gid];
        r.done++;
        if (fb.error && !r.error) r.error = fb.error;
        r.failed |= fb.error != nil;
        if (fb.GPUStartTime > 0 && (r.start == 0 || fb.GPUStartTime < r.start)) r.start = fb.GPUStartTime;
        r.end = std::max(r.end, (double)fb.GPUEndTime);
        for (std::deque<uint64_t>& order : order_)
            while (!order.empty()) {
                auto it = recs_.find(order.front());
                if (!it->second.final || it->second.done < it->second.parts) break;
                ready_.push_back(std::move(it->second));
                recs_.erase(it);
                order.pop_front();
            }
        // every seq up to final_seq_ is final; one not delivered is at the front of its queue's order
        uint64_t fed = final_seq_;
        for (const std::deque<uint64_t>& order : order_)
            if (!order.empty()) fed = std::min(fed, recs_[order.front()].seq - 1);
        fed_.store(std::max(fed, fed_.load(std::memory_order_relaxed)), std::memory_order_release);
    }
    // outside seq_mu_: a callback may commit, wait or take the renderer's locks
    for (SeqRec& r : ready_) {
        if (!r.fn) continue;
        GnmCompletion c;
        c.seq = r.seq;
        c.failed = r.failed;
        c.gpu_start = r.start;
        c.gpu_end = r.end;
        c.error = r.error; // r keeps it alive during the call
        r.fn(r.ctx, c);
    }
    ready_.clear(); // releases the errors
    serial.unlock(); // the destructor may free the backend once feedback_pending_ reaches 0
    std::lock_guard<std::mutex> lk(seq_mu_);
    if (--feedback_pending_ == 0) feedback_cv_.notify_all();
}

// Ends and commits the open command buffers of queue q as one more part of its open seq, the upload
// one first (its copies run before the main command buffer's work: one commit array is executed in
// order); with_main = false leaves the main command buffer open (CommitUpload). The queue signals
// deferred by QueueSignal for the committed targets follow the commit. final: the seq's last part,
// numbered seq: q's timeline is signalled with it after the part, the parts, bytes chunks and
// evictions waiting for the number get it, and its completion runs when every part's feedback
// arrived. Encoders must have been ended by the caller (FATAL otherwise: ending one here would
// leave the renderer's encoder state stale).
void Metal4Backend::Submit(int q, bool final, uint64_t seq, bool with_main, GnmCompletionFn fn, void* ctx) {
    static const GnmCmdTarget kTargets[2][2] = {{GnmCmdTarget::Upload, GnmCmdTarget::Main},
                                                {GnmCmdTarget::AsyncCompute, GnmCmdTarget::AsyncCompute}};
    const int nt = q ? 1 : 2;
    Group& g = grp_[q];
    id<MTL4CommandBuffer> list[2];
    NSUInteger n = 0;
    bool committed[3] = {false, false, false};
    for (int i = 0; i < nt; ++i) {
        const GnmCmdTarget t = kTargets[q][i];
        TargetState& x = t_[(int)t];
        if (!x.cb || (t == GnmCmdTarget::Main && !with_main)) continue;
        if (x.renc || x.cenc) {
            fprintf(stderr, "[gnm-draw] FATAL: Metal4Backend: a commit with an open encoder (%s target)\n",
                    t == GnmCmdTarget::Main ? "main" : t == GnmCmdTarget::Upload ? "upload" : "async compute");
            abort();
        }
        [x.cb endCommandBuffer];
        list[n++] = x.cb;
        NoteAllocatorSize(x.part);
        if (x.part.fresh) {
            if (!x.part.rendered) NoteFreshSize(x.part.bytes); // the estimate is of compute-only parts
            x.part.fresh = false;
        }
        last_rendered_[(int)t] = x.part.rendered;
        if (t == GnmCmdTarget::Main && frame_idx_ < kFrameParts) frame_cur_[frame_idx_++] = x.part.rendered;
        g.busy_at.push_back(busy_popped_ + busy_.size());
        busy_.emplace_back(std::move(x.part), UINT64_MAX);
        x.cb = nil;
        x.part = Part{};
        x.work = false; // nothing recorded on this target until its next part
        committed[(int)t] = true;
    }
    // the in-flight peak the trim keeps allocators for (Recycle)
    peak_cur_ = std::max(peak_cur_, busy_.size());
    if (++peak_submits_ >= kTrimPeakWindow) {
        peak_prev_ = peak_cur_;
        peak_cur_ = busy_.size();
        peak_submits_ = 0;
    }
    uint64_t gid;
    {
        std::lock_guard<std::mutex> lk(seq_mu_);
        if (!g.gid) g.gid = ++next_gid_;
        gid = g.gid;
        SeqRec& r = recs_[gid];
        if (n) {
            r.parts++;
            feedback_pending_++;
        }
        if (final) {
            r.final = true;
            r.seq = seq;
            r.fn = fn;
            r.ctx = ctx;
            order_[q].push_back(gid);
            final_seq_ = seq;
        }
    }
    if (final) {
        std::lock_guard<std::mutex> lk(flight_mu_);
        flight_.push_back({seq, q});
    }
    res_.Commit(); // every allocation the parts use is in a set before the queue sees them
    id<MTL4CommandQueue> queue = Queue(q);
    if (n) {
        MTL4CommitOptions* opts = [MTL4CommitOptions new];
        [opts addFeedbackHandler:^(id<MTL4CommitFeedback> fb) { OnFeedback(gid, fb); }];
        [queue commit:list count:n options:opts];
        commits_.fetch_add(1, std::memory_order_relaxed);
        if (!final) split_commits_.fetch_add(1, std::memory_order_relaxed);
    }
    for (int t = 0; t < 3; ++t) {
        if (!committed[t]) continue;
        for (auto& [ev, v] : t_[t].signals) [queue signalEvent:ev value:v];
        t_[t].signals.clear();
    }
    if (!final) return;
    [queue signalEvent:(q ? async_timeline_ : timeline_) value:seq];
    last_seq_ = seq;
    for (size_t at : g.busy_at) busy_[at - busy_popped_].second = seq;
    g.busy_at.clear();
    for (Chunk& c : g.chunks) {
        c.last_seq = seq;
        chunks_busy_.push_back(c);
    }
    g.chunks.clear();
    if (g.chunk_dirty) g.chunk.last_seq = seq;
    g.chunk_dirty = false;
    g.gid = 0;
    // evictions that waited for this seq's number
    std::lock_guard<std::mutex> lk(evict_mu_);
    open_queues_ &= uint8_t(~(1u << q));
    size_t keep = 0;
    for (PendingEvict& e : pending_evicts_) {
        if (e.queues & (1u << q)) {
            e.after = std::max(e.after, seq);
            e.queues &= uint8_t(~(1u << q));
        }
        if (!e.queues) {
            res_.Remove(e.a, e.after);
            continue;
        }
        if (&pending_evicts_[keep] != &e) pending_evicts_[keep] = std::move(e);
        keep++;
    }
    pending_evicts_.resize(keep);
}

// A split inside the open seq (a signal that must follow recorded work, a wait that must not hold
// it back): commits every open command buffer of the current queue and opens new ones on the same
// targets. On the first queue only with Main current: the renderer issues queue-level events on
// Main (RequireMainTarget) or AsyncCompute, and a split on Upload would also commit Main, which may
// have its render encoder open while an upload encoder records (FATAL in Submit, far from the
// cause), where Metal 3 touches the upload command buffer only.
void Metal4Backend::Split() {
    if (cur_ == GnmCmdTarget::Upload) {
        fprintf(stderr, "[gnm-draw] FATAL: Metal4Backend: a queue-level event splits the seq on the Upload target "
                        "(events are issued on Main only)\n");
        abort();
    }
    if (cur_ == GnmCmdTarget::AsyncCompute) {
        TargetState& a = t_[(int)GnmCmdTarget::AsyncCompute];
        NSString* label = a.cb.label;
        Submit(1, false, 0, false, nullptr, nullptr);
        BeginPart(GnmCmdTarget::AsyncCompute, label);
        return;
    }
    NSString* labels[2] = {t_[0].cb.label, t_[1].cb.label};
    const bool open[2] = {t_[0].cb != nil, t_[1].cb != nil};
    Submit(0, false, 0, true, nullptr, nullptr);
    for (int t = 0; t < 2; ++t)
        if (open[t]) BeginPart((GnmCmdTarget)t, labels[t]);
}

void Metal4Backend::CommitUpload() {
    if (t_[(int)GnmCmdTarget::Upload].cb) Submit(0, false, 0, false, nullptr, nullptr);
}

void Metal4Backend::Commit(uint64_t seq, GnmCompletionFn done, void* ctx) {
    if (seq <= last_seq_) {
        fprintf(stderr, "[gnm-draw] FATAL: Metal4Backend::Commit of seq %llu, after seq %llu\n", (unsigned long long)seq,
                (unsigned long long)last_seq_);
        abort();
    }
    // every seq ends with a committed command buffer: its feedback is the seq's completion
    if (!t_[(int)GnmCmdTarget::Main].cb) BeginPart(GnmCmdTarget::Main, nil);
    Submit(0, true, seq, true, done, ctx);
}

void Metal4Backend::CommitAsync(uint64_t seq, GnmCompletionFn done, void* ctx) {
    if (seq <= last_seq_) {
        fprintf(stderr, "[gnm-draw] FATAL: Metal4Backend::CommitAsync of seq %llu, after seq %llu\n",
                (unsigned long long)seq, (unsigned long long)last_seq_);
        abort();
    }
    if (!t_[(int)GnmCmdTarget::AsyncCompute].cb) BeginPart(GnmCmdTarget::AsyncCompute, nil);
    Submit(1, true, seq, false, done, ctx);
}

uint64_t Metal4Backend::CompletedSeq() const {
    const uint64_t t[2] = {timeline_.signaledValue, async_timeline_.signaledValue};
    uint64_t done;
    {
        std::lock_guard<std::mutex> lk(flight_mu_);
        while (!flight_.empty() && flight_.front().seq <= t[flight_.front().q]) {
            flight_done_ = std::max(flight_done_, flight_.front().seq);
            flight_.pop_front();
        }
        // with nothing in flight, every seq a timeline reached is complete
        if (flight_.empty()) flight_done_ = std::max(flight_done_, std::max(t[0], t[1]));
        done = flight_done_;
    }
    return std::max(done, fed_.load(std::memory_order_acquire));
}

// Waits for the first uncompleted seq up to `need` on its own queue's timeline, then the next.
bool Metal4Backend::WaitForRoom(uint64_t committed, uint64_t max_outstanding, double timeout_ms) {
    if (committed < max_outstanding) return true;
    const uint64_t need = committed - max_outstanding + 1;
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double, std::milli>(timeout_ms);
    for (;;) {
        if (CompletedSeq() >= need) return true;
        Flight f{0, 0};
        bool any;
        {
            std::lock_guard<std::mutex> lk(flight_mu_);
            any = !flight_.empty();
            if (any) f = flight_.front();
        }
        if (!any) return CompletedSeq() >= need; // every committed seq completed meanwhile
        const double left = std::chrono::duration<double, std::milli>(end - std::chrono::steady_clock::now()).count();
        if (left <= 0) return false;
        if (![(f.q ? async_timeline_ : timeline_) waitUntilSignaledValue:f.seq timeoutMS:(uint64_t)std::max(1.0, left)])
            return CompletedSeq() >= need;
    }
}

// Queue-level events exist only between command buffers. A signal with no work recorded in the
// current part is issued right here (every earlier part is committed); after recorded work it waits
// for that part's commit (Submit, or the split BeforeWork makes when work starts again). A wait
// after recorded work or a pending signal commits the part first (a split), so the work so far
// does not wait. On Main, work recorded on the open Upload command buffer comes first, as on
// Metal 3 (which commits the upload command buffer before the main one, where the event is
// encoded): UploadFirst commits it before a signal or a wait. A wait holds the whole queue: upload
// work recorded after it waits too, while on Metal 3 only the main command buffer does.
void Metal4Backend::QueueSignal(id<MTLSharedEvent> ev, uint64_t value) {
    TargetState& x = Cur();
    if (x.work) {
        x.signals.emplace_back(ev, value); // after this part's commit, which includes the uploads
        return;
    }
    UploadFirst();
    [Queue(QueueOf(cur_)) signalEvent:ev value:value];
}

void Metal4Backend::QueueWait(id<MTLSharedEvent> ev, uint64_t value) {
    TargetState& x = Cur();
    if (x.work || !x.signals.empty()) Split(); // the uploads too
    else UploadFirst();
    [Queue(QueueOf(cur_)) waitForEvent:ev value:value];
}

// The current target is Main and Upload holds recorded work or signals: commit Upload alone (a
// split commit of the open seq) and open its next part.
void Metal4Backend::UploadFirst() {
    TargetState& u = t_[(int)GnmCmdTarget::Upload];
    if (cur_ != GnmCmdTarget::Main || !u.cb || (!u.work && u.signals.empty())) return;
    NSString* label = u.cb.label;
    Submit(0, false, 0, false, nullptr, nullptr);
    BeginPart(GnmCmdTarget::Upload, label);
}

void Metal4Backend::NoteFrameEnd() {
    frame_prev_ = frame_cur_;
    frame_prev_n_ = frame_idx_;
    frame_idx_ = 0;
}

GnmBackendStats Metal4Backend::Stats() const {
    GnmBackendStats s;
    s.commits = commits_.load(std::memory_order_relaxed);
    s.split_commits = split_commits_.load(std::memory_order_relaxed);
    s.allocators = allocators_.load(std::memory_order_relaxed);
    s.allocators_created = created_.load(std::memory_order_relaxed);
    s.allocators_released = released_.load(std::memory_order_relaxed);
    s.allocator_resets = resets_.load(std::memory_order_relaxed);
    s.allocator_bytes = alloc_bytes_.load(std::memory_order_relaxed);
    s.residency_asserts = residency_asserts_.load(std::memory_order_relaxed);
    s.resident = res_.Count();
    s.resident_textures = res_.Count(GnmResidencyClass::Texture);
    s.resident_bytes = res_.AllocatedSize();
    return s;
}

// Signals waiting for the work recorded so far are issued after that work's commit: if work is
// about to start again on this target, that commit happens now (a split of the seq).
void Metal4Backend::BeforeWork() {
    TargetState& x = Cur();
    if (!x.signals.empty()) Split();
    Cur().work = true;
}

// PS4_GNM_RESIDENCY_CHECK=1: a resource the GPU will touch that is in no residency set. On Metal 4
// its writes are lost and its reads may fault or return stale data without an error (probe P4; the
// cold-only command buffer timeouts of Task 3.3), so this is a bug of the caller that made it (a
// creation path without MakeResident, or an Evict too early). Resident means for the whole of the
// seq being recorded: a resource evicted after an earlier seq leaves its set when that seq
// completes, maybe while this one runs. The holder of the encoder's state.
void Metal4Backend::CheckResident(id<MTLResource> r, const char* what) {
    if (!r || res_.ResidentFor(r, open_seq())) return;
    residency_asserts_.fetch_add(1, std::memory_order_relaxed);
    if (!reported_.insert((__bridge const void*)r).second) return;
    const char* label = r.label.length ? r.label.UTF8String : nullptr;
    if (label) fprintf(stderr, "[gnm-residency] ASSERT %s %s not resident\n", what, label);
    else if ([r conformsToProtocol:@protocol(MTLBuffer)])
        fprintf(stderr, "[gnm-residency] ASSERT %s buffer 0x%llx (%llu bytes) not resident\n", what,
                (unsigned long long)((id<MTLBuffer>)r).gpuAddress, (unsigned long long)((id<MTLBuffer>)r).length);
    else
        fprintf(stderr, "[gnm-residency] ASSERT %s texture %p (%lux%lu fmt %lu) not resident\n", what,
                (__bridge const void*)r, (unsigned long)((id<MTLTexture>)r).width,
                (unsigned long)((id<MTLTexture>)r).height, (unsigned long)((id<MTLTexture>)r).pixelFormat);
}

bool Metal4Backend::BeginRender(const GnmPassDesc& d) {
    if (check_residency_) {
        for (int i = 0; i < 8; ++i) {
            CheckResident(d.color[i].tex, "attachment");
            if (d.color[i].store == GnmStore::MultisampleResolve) CheckResident(d.color[i].resolve, "resolve attachment");
        }
        CheckResident(d.depth.tex, "attachment");
        CheckResident(d.stencil.tex, "attachment");
        if (d.visibility) CheckResident(d.visibility, "visibility result buffer");
    }
    BeforeWork();
    TargetState& x = Cur();
    MTL4RenderPassDescriptor* rp = [MTL4RenderPassDescriptor new];
    for (int i = 0; i < 8; ++i) {
        const GnmAttachment& a = d.color[i];
        if (!a.tex) continue;
        MTLRenderPassColorAttachmentDescriptor* c = rp.colorAttachments[i];
        c.texture = a.tex;
        c.slice = a.slice;
        c.loadAction = LoadOf(a.load);
        if (a.load == GnmLoad::Clear) c.clearColor = d.clear_color[i];
        c.storeAction = StoreOf(a.store);
        if (a.store == GnmStore::MultisampleResolve) c.resolveTexture = a.resolve;
    }
    if (d.depth.tex) {
        rp.depthAttachment.texture = d.depth.tex;
        rp.depthAttachment.slice = d.depth.slice;
        rp.depthAttachment.loadAction = LoadOf(d.depth.load);
        if (d.depth.load == GnmLoad::Clear) rp.depthAttachment.clearDepth = d.clear_depth;
        rp.depthAttachment.storeAction = StoreOf(d.depth.store);
    }
    if (d.stencil.tex) {
        rp.stencilAttachment.texture = d.stencil.tex;
        rp.stencilAttachment.slice = d.stencil.slice;
        rp.stencilAttachment.loadAction = LoadOf(d.stencil.load);
        if (d.stencil.load == GnmLoad::Clear) rp.stencilAttachment.clearStencil = d.clear_stencil;
        rp.stencilAttachment.storeAction = StoreOf(d.stencil.store);
    }
    if (d.width) rp.renderTargetWidth = d.width;
    if (d.height) rp.renderTargetHeight = d.height;
    if (d.default_samples) rp.defaultRasterSampleCount = d.default_samples;
    if (d.num_sample_positions) [rp setSamplePositions:d.sample_positions count:d.num_sample_positions];
    if (d.visibility) {
        rp.visibilityResultBuffer = d.visibility;
        rp.visibilityResultType = MTLVisibilityResultTypeReset; // as Metal 3: reset when the encoder is made
    }
    x.renc = [x.cb renderCommandEncoderWithDescriptor:rp];
    x.part.rendered = true;
    if (!x.renc) return false; // as Metal3Backend: no encoder, no serial change
    for (int i = 0; i < 8; ++i) {
        Keep(x, d.color[i].tex);
        if (d.color[i].store == GnmStore::MultisampleResolve) Keep(x, d.color[i].resolve);
    }
    Keep(x, d.depth.tex);
    Keep(x, d.stencil.tex);
    if (d.label) x.renc.label = d.label;
    [x.renc setArgumentTable:x.table[(int)GnmStage::Vertex] atStages:MTLRenderStageVertex];
    [x.renc setArgumentTable:x.table[(int)GnmStage::Fragment] atStages:MTLRenderStageFragment];
    // what Metal 3 orders between encoders by itself: everything before (render, compute, copies) first
    if (!no_barriers_)
        [x.renc barrierAfterQueueStages:kAllStages beforeStages:MTLStageVertex | MTLStageFragment
                      visibilityOptions:kGuestVisibility];
    NoteEncoderOpened(GnmEncoder::Render);
    return true;
}

// Requirement: these encoder-start barriers are also what orders guest windows that alias (several
// no-copy buffers over the same guest memory, docs/gnm-ordering.md, "Aliasing guest windows"). The
// renderer orders them itself only on Metal 3 (AccessFence / WaitAccessFence, which Metal4Backend
// defines to do nothing, for this reason): a barrier model that narrows or skips these barriers
// (per-object tracking, a barrier only when something is known pending) must still order an access
// through one window after in-flight accesses through another window over the same bytes (a read
// after writes, a write after writes and reads). gnm_backend_alias_test checks it on this backend
// (its ordered tries pass here only through these barriers).

// Copies are on a compute encoder (Metal 4 has no blit encoder). It starts with a barrier after
// every stage of earlier encoders: what Metal 3 orders between encoders by itself.
void Metal4Backend::BeginCopies() {
    BeforeWork();
    TargetState& x = Cur();
    x.cenc = [x.cb computeCommandEncoder];
    if (!x.cenc) {
        NoteEncoderOpened(GnmEncoder::None);
        return;
    }
    [x.cenc setArgumentTable:x.table[(int)GnmStage::Compute]];
    if (!no_barriers_)
        [x.cenc barrierAfterQueueStages:kAllStages beforeStages:MTLStageDispatch | MTLStageBlit
                      visibilityOptions:kGuestVisibility];
    x.serial = false;
    x.copied = x.dispatched = false;
    NoteEncoderOpened(GnmEncoder::Copy);
}

// A compute encoder starts like a copy encoder: a barrier after every stage of earlier encoders.
// serial: a barrier after every dispatch, the Metal 3 serial dispatch type (the renderer passes
// true when PS4_GNM_SERIAL_COMPUTE is set, to any value, and for uploads).
void Metal4Backend::BeginCompute(bool serial) {
    BeforeWork();
    TargetState& x = Cur();
    x.cenc = [x.cb computeCommandEncoder];
    if (!x.cenc) {
        NoteEncoderOpened(GnmEncoder::None);
        return;
    }
    [x.cenc setArgumentTable:x.table[(int)GnmStage::Compute]];
    if (!no_barriers_)
        [x.cenc barrierAfterQueueStages:kAllStages beforeStages:MTLStageDispatch | MTLStageBlit
                      visibilityOptions:kGuestVisibility];
    x.serial = serial;
    x.copied = x.dispatched = false;
    NoteEncoderOpened(GnmEncoder::Compute);
}

void Metal4Backend::SetComputePipeline(id<MTLComputePipelineState> p) { [Cur().cenc setComputePipelineState:p]; }

// A barrier between the dispatch and blit stages of the open compute encoder; nothing ran since.
void Metal4Backend::EncoderBarrier(TargetState& x) {
    [x.cenc barrierAfterEncoderStages:MTLStageDispatch | MTLStageBlit beforeEncoderStages:MTLStageDispatch | MTLStageBlit
                    visibilityOptions:kGuestVisibility];
    x.copied = x.dispatched = false;
}

// Copies and dispatches inside one compute encoder are separated by barriers (the spec's
// synchronisation rule; Metal 3's blit encoder orders dependent copies by itself): before a
// dispatch if a copy ran since the last barrier, before a copy if anything ran. Dispatches after
// dispatches are ordered by the renderer's Barrier calls (or a serial encoder), as on Metal 3.
void Metal4Backend::BeforeDispatch(TargetState& x) {
    if (x.copied) EncoderBarrier(x);
}
void Metal4Backend::BeforeCopy(TargetState& x) {
    if (x.copied || x.dispatched) EncoderBarrier(x);
}

// After a dispatch: a serial encoder's barrier, or the debug mode's (PS4_GNM_MTL4_FULL_BARRIERS).
void Metal4Backend::AfterDispatch(TargetState& x) {
    x.dispatched = true;
    if (x.serial || full_barriers_) EncoderBarrier(x);
}

void Metal4Backend::Dispatch(MTLSize groups, MTLSize tpt) {
    BeforeCommand();
    TargetState& x = Cur();
    BeforeDispatch(x);
    [x.cenc dispatchThreadgroups:groups threadsPerThreadgroup:tpt];
    AfterDispatch(x);
}

void Metal4Backend::DispatchThreads(MTLSize threads, MTLSize tpt) {
    BeforeCommand();
    TargetState& x = Cur();
    BeforeDispatch(x);
    [x.cenc dispatchThreads:threads threadsPerThreadgroup:tpt];
    AfterDispatch(x);
}

// The arguments (3 uint32) at gpuAddress + off: Metal 4 takes an address (4-byte aligned, which
// DrawEncode / EncodeCs check for guest arguments); args must be resident.
void Metal4Backend::DispatchIndirect(id<MTLBuffer> args, uint64_t off, MTLSize tpt) {
    BeforeCommand();
    if (check_residency_) CheckResident(args, "indirect buffer");
    TargetState& x = Cur();
    BeforeDispatch(x);
    [x.cenc dispatchThreadgroupsWithIndirectBuffer:args.gpuAddress + off threadsPerThreadgroup:tpt];
    AfterDispatch(x);
}

// Inside a compute (or copy) encoder: after / before stages as given (dispatch and blit only;
// none given: both). scope is ignored: device visibility orders all memory. Nothing without an
// open encoder (encoder starts have their own barrier).
void Metal4Backend::Barrier(uint32_t after, uint32_t before, uint8_t) {
    TargetState& x = Cur();
    if (x.cenc) {
        const MTLStages s = MTLStageDispatch | MTLStageBlit;
        const MTLStages a = (MTLStages)after & s, b = (MTLStages)before & s;
        [x.cenc barrierAfterEncoderStages:a ? a : s beforeEncoderStages:b ? b : s
                        visibilityOptions:kGuestVisibility];
        if (a == s && b == s) x.copied = x.dispatched = false; // a full one orders everything before it
    } else if (x.renc) {
        // inside a render pass: vertex / fragment stages as given (none given: vertex), as the
        // rect list's capture -> expansion barrier (vertex -> vertex)
        const MTLStages s = MTLStageVertex | MTLStageFragment;
        const MTLStages a = (MTLStages)after & s, b = (MTLStages)before & s;
        [x.renc barrierAfterEncoderStages:a ? a : MTLStageVertex beforeEncoderStages:b ? b : MTLStageVertex
                        visibilityOptions:kGuestVisibility];
    }
}

// ---- copies and fills on the open copy (compute) encoder, each ordered after the encoder's
// earlier work (BeforeCopy)
void Metal4Backend::CopyTexture(id<MTLTexture> src, id<MTLTexture> dst) {
    BeforeCommand();
    if (check_residency_) CheckResident(src, "copy source"), CheckResident(dst, "copy destination");
    TargetState& x = Cur();
    Keep(x, src);
    Keep(x, dst);
    BeforeCopy(x);
    [x.cenc copyFromTexture:src toTexture:dst];
    x.copied = true;
}

void Metal4Backend::CopyBuffer(id<MTLBuffer> src, uint64_t src_off, id<MTLBuffer> dst, uint64_t dst_off, uint64_t n) {
    BeforeCommand();
    if (check_residency_) CheckResident(src, "copy source"), CheckResident(dst, "copy destination");
    TargetState& x = Cur();
    BeforeCopy(x);
    [x.cenc copyFromBuffer:src sourceOffset:src_off toBuffer:dst destinationOffset:dst_off size:n];
    x.copied = true;
}

void Metal4Backend::FillBuffer(id<MTLBuffer> dst, uint64_t off, uint64_t n, uint8_t value) {
    BeforeCommand();
    if (check_residency_) CheckResident(dst, "copy destination");
    TargetState& x = Cur();
    BeforeCopy(x);
    [x.cenc fillBuffer:dst range:NSMakeRange(off, n) value:value];
    x.copied = true;
}

void Metal4Backend::CopyTextureRegion(id<MTLTexture> src, uint32_t src_slice, uint32_t src_level, MTLOrigin src_origin,
                                      MTLSize size, id<MTLTexture> dst, uint32_t dst_slice, uint32_t dst_level,
                                      MTLOrigin dst_origin) {
    BeforeCommand();
    if (check_residency_) CheckResident(src, "copy source"), CheckResident(dst, "copy destination");
    TargetState& x = Cur();
    Keep(x, src);
    Keep(x, dst);
    BeforeCopy(x);
    [x.cenc copyFromTexture:src sourceSlice:src_slice sourceLevel:src_level sourceOrigin:src_origin sourceSize:size
                  toTexture:dst destinationSlice:dst_slice destinationLevel:dst_level destinationOrigin:dst_origin];
    x.copied = true;
}

void Metal4Backend::CopyBufferToTexture(id<MTLBuffer> src, uint64_t src_off, uint64_t row_bytes, uint64_t image_bytes,
                                        MTLSize size, id<MTLTexture> dst, uint32_t slice, uint32_t level, MTLOrigin origin) {
    BeforeCommand();
    if (check_residency_) CheckResident(src, "copy source"), CheckResident(dst, "copy destination");
    TargetState& x = Cur();
    Keep(x, dst);
    BeforeCopy(x);
    [x.cenc copyFromBuffer:src sourceOffset:src_off sourceBytesPerRow:row_bytes sourceBytesPerImage:image_bytes
                sourceSize:size toTexture:dst destinationSlice:slice destinationLevel:level destinationOrigin:origin];
    x.copied = true;
}

void Metal4Backend::SetRenderPipeline(id<MTLRenderPipelineState> p) { [Cur().renc setRenderPipelineState:p]; }
void Metal4Backend::SetDepthStencil(id<MTLDepthStencilState> d) { [Cur().renc setDepthStencilState:d]; }

void Metal4Backend::SetVisibilityResultMode(MTLVisibilityResultMode mode, uint64_t offset) {
    [Cur().renc setVisibilityResultMode:mode offset:offset];
}

// Metal 4 takes buffers by address, and an index buffer with its length (index_length: the bytes
// readable at index_offset; reads past it return 0). Every buffer must be resident.
void Metal4Backend::Draw(const GnmDraw& d) {
    BeforeCommand();
    if (check_residency_) {
        if (d.kind == GnmDrawKind::Indexed || d.kind == GnmDrawKind::IndexedIndirect) CheckResident(d.index_buffer, "index buffer");
        if (d.kind == GnmDrawKind::Indirect || d.kind == GnmDrawKind::IndexedIndirect)
            CheckResident(d.indirect_buffer, "indirect buffer");
    }
    TargetState& x = Cur();
    switch (d.kind) {
    case GnmDrawKind::Direct:
        [x.renc drawPrimitives:d.prim vertexStart:d.vertex_start vertexCount:d.count instanceCount:d.instances
                  baseInstance:d.base_instance];
        break;
    case GnmDrawKind::Indexed:
        [x.renc drawIndexedPrimitives:d.prim indexCount:d.count indexType:d.index_type
                          indexBuffer:d.index_buffer.gpuAddress + d.index_offset indexBufferLength:d.index_length
                        instanceCount:d.instances baseVertex:d.base_vertex baseInstance:d.base_instance];
        break;
    case GnmDrawKind::Indirect:
        [x.renc drawPrimitives:d.prim indirectBuffer:d.indirect_buffer.gpuAddress + d.indirect_offset];
        break;
    case GnmDrawKind::IndexedIndirect:
        [x.renc drawIndexedPrimitives:d.prim indexType:d.index_type indexBuffer:d.index_buffer.gpuAddress + d.index_offset
                    indexBufferLength:d.index_length indirectBuffer:d.indirect_buffer.gpuAddress + d.indirect_offset];
        break;
    }
    if (full_barriers_)
        [x.renc barrierAfterEncoderStages:MTLStageVertex | MTLStageFragment
                      beforeEncoderStages:MTLStageVertex | MTLStageFragment
                        visibilityOptions:MTL4VisibilityOptionDevice];
}

// setBytes: Metal 4 has none. The bytes go into a 256 KiB shared chunk (resident, Builtin) at a
// 256-byte aligned offset, and the table gets their address. A chunk is reused once the last seq
// that used it completed (Recycle); the GPU reads them when the draw runs, so they stay unchanged
// until then.
constexpr size_t kBytesChunk = 256u << 10;
constexpr size_t kBytesAlign = 256; // constant buffer offset alignment
uint64_t Metal4Backend::BytesAddress(const void* p, size_t n) {
    if (n > kBytesChunk) {
        fprintf(stderr, "[gnm-draw] FATAL: Metal4Backend: BindBytes of %zu bytes (a chunk holds %zu; Metal's setBytes "
                        "limit is 4 KiB)\n", n, kBytesChunk);
        abort();
    }
    Group& g = grp_[QueueOf(cur_)];
    size_t off = (g.chunk_used + kBytesAlign - 1) & ~(kBytesAlign - 1);
    if (!g.chunk.buf || off + n > kBytesChunk) {
        if (g.chunk.buf) {
            // used by the open seq: free after it (Submit); else after the seq that used it last
            if (g.chunk_dirty) g.chunks.push_back(g.chunk);
            else chunks_busy_.push_back(g.chunk);
        }
        id<MTLBuffer> b = nil;
        if (!chunks_free_.empty()) {
            b = chunks_free_.back();
            chunks_free_.pop_back();
        } else {
            b = [dev_ newBufferWithLength:kBytesChunk options:MTLResourceStorageModeShared];
            b.label = @"gnm bytes arena";
            res_.Add(b, GnmResidencyClass::Builtin);
        }
        g.chunk = Chunk{b, 0};
        off = 0;
    }
    memcpy((uint8_t*)g.chunk.buf.contents + off, p, n);
    g.chunk_used = off + n;
    g.chunk_dirty = true;
    return g.chunk.buf.gpuAddress + off;
}

void Metal4Backend::EndEncoder() {
    TargetState& x = Cur();
    if (x.renc) [x.renc endEncoding];
    if (x.cenc) [x.cenc endEncoding];
    x.renc = nil;
    x.cenc = nil;
    NoteEncoderEnded();
}

void Metal4Backend::SetEncoderLabel(NSString* label) {
    TargetState& x = Cur();
    if (x.renc) x.renc.label = label;
    else if (x.cenc) x.cenc.label = label;
}

// ---- later tasks
// DrawEncode: the fixed-function state the renderer found changed (GnmFixedBits)
void Metal4Backend::SetFixed(const GnmFixedState& f, uint32_t changed) {
    id<MTL4RenderCommandEncoder> e = Cur().renc;
    if (changed & kGnmFixedStencilRef) [e setStencilFrontReferenceValue:f.stencil_ref[0] backReferenceValue:f.stencil_ref[1]];
    if (changed & kGnmFixedViewport) [e setViewport:f.vp];
    if (changed & kGnmFixedScissor) [e setScissorRect:f.sc];
    if (changed & kGnmFixedCull) [e setCullMode:(MTLCullMode)f.cull];
    if (changed & kGnmFixedWinding) [e setFrontFacingWinding:(MTLWinding)f.winding];
    if (changed & kGnmFixedFill) [e setTriangleFillMode:(MTLTriangleFillMode)f.fill];
    if (changed & kGnmFixedClip) [e setDepthClipMode:(MTLDepthClipMode)f.clip];
    if (changed & kGnmFixedBias) [e setDepthBias:f.bias[0] slopeScale:f.bias[1] clamp:f.bias[2]];
    if (changed & kGnmFixedBlend) [e setBlendColorRed:f.blend[0] green:f.blend[1] blue:f.blend[2] alpha:f.blend[3]];
}
// ---- bind ops: one argument table per target and stage (BeginRender binds the vertex and
// fragment ones, BeginCompute / BeginCopies the compute one); the table is snapshotted at each
// draw and dispatch, so it is written between them

void Metal4Backend::OpBuffer(GnmBackend* b, GnmStage st, uint32_t slot, __unsafe_unretained id<MTLBuffer> buf, uint64_t off) {
    Metal4Backend* m = static_cast<Metal4Backend*>(b);
    if (m->check_residency_) m->CheckResident(buf, "bind buffer");
    TargetState& x = m->Cur();
    const uint64_t base = buf ? buf.gpuAddress : 0;
    x.base[(int)st][slot] = base;
    [x.table[(int)st] setAddress:base ? base + off : 0 atIndex:slot];
}
void Metal4Backend::OpBufferOffset(GnmBackend* b, GnmStage st, uint32_t slot, uint64_t off) {
    TargetState& x = static_cast<Metal4Backend*>(b)->Cur();
    const uint64_t base = x.base[(int)st][slot];
    if (!base) { // as undefined as Metal 3's setBufferOffset after setBytes or without a buffer, but loud
        fprintf(stderr, "[gnm-draw] FATAL: Metal4Backend: BindBufferOffset on stage %d slot %u, which holds no buffer "
                        "(never bound, nil, or bytes)\n", (int)st, slot);
        abort();
    }
    [x.table[(int)st] setAddress:base + off atIndex:slot];
}
void Metal4Backend::OpBytes(GnmBackend* b, GnmStage st, uint32_t slot, const void* p, size_t n) {
    Metal4Backend* m = static_cast<Metal4Backend*>(b);
    TargetState& x = m->Cur();
    x.base[(int)st][slot] = 0;
    [x.table[(int)st] setAddress:m->BytesAddress(p, n) atIndex:slot];
}
void Metal4Backend::OpTexture(GnmBackend* b, GnmStage st, uint32_t slot, __unsafe_unretained id<MTLTexture> t) {
    Metal4Backend* m = static_cast<Metal4Backend*>(b);
    if (m->check_residency_) m->CheckResident(t, "bind texture");
    const MTLResourceID none = {};
    TargetState& x = m->Cur();
    Keep(x, t); // the table holds only its gpuResourceID
    [x.table[(int)st] setTexture:t ? t.gpuResourceID : none atIndex:slot];
}
void Metal4Backend::OpSampler(GnmBackend* b, GnmStage st, uint32_t slot, __unsafe_unretained id<MTLSamplerState> s) {
    const MTLResourceID none = {};
    [static_cast<Metal4Backend*>(b)->Cur().table[(int)st] setSamplerState:s ? s.gpuResourceID : none atIndex:slot];
}
// A buffer reached through an argument buffer (gpuAddress tables): Metal 4 has no useResource;
// the buffer is resident because every window, arena chunk and built-in is in a residency set
// from its creation (GnmResidency). PS4_GNM_RESIDENCY_CHECK=1 checks that here.
void Metal4Backend::OpUse(GnmBackend* b, GnmStage, __unsafe_unretained id<MTLBuffer> buf, bool) {
    Metal4Backend* m = static_cast<Metal4Backend*>(b);
    if (m->check_residency_) m->CheckResident(buf, "use");
}

} // namespace ps4gnm
