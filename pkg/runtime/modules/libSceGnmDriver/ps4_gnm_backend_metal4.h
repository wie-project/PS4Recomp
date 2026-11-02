// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - the Metal 4 renderer backend (ps4_gnm_backend.h, docs/metal4-backend.md):
// two MTL4CommandQueues (Main and Upload on one, AsyncCompute on the other, kGnmCapAsyncCompute);
// one or more MTL4CommandBuffers per seq (split at queue-level events), each with a command
// allocator that is reset only after its seq completed; a timeline MTLSharedEvent per queue,
// signalled with each of its seqs after the seq's last commit; commit feedback for errors and GPU
// times, with completions delivered in seq order per queue; argument tables per target and stage;
// a bytes arena for setBytes; a barrier at each encoder start; copies and fills on compute
// encoders; residency sets (GnmResidency) on both queues.
#ifndef PS4_GNM_BACKEND_METAL4_H
#define PS4_GNM_BACKEND_METAL4_H

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ps4_gnm_backend.h"
#include "ps4_gnm_residency.h"

namespace ps4gnm {

class Metal4Backend final : public GnmBackend {
public:
    Metal4Backend();
    // Waits until the feedback of every commit was delivered (and its completion ran) and every
    // queue operation issued so far ran, then releases the command queue before the feedback
    // dispatch queue it uses. Queue signals still waiting for an uncommitted part are dropped
    // with that part.
    ~Metal4Backend() override;
    GnmBackendKind Kind() const override { return GnmBackendKind::Metal4; }
    uint32_t Caps() const override { return kMetal4BackendCaps; }
    bool Init(id<MTLDevice> dev, std::string* why) override;
    id<MTLDevice> Device() const override { return dev_; }
    id<MTLCommandQueue> SideQueue() const override { return side_queue_; }

    void BeginCmd(GnmCmdTarget t, NSString* label, bool encoder_status) override;
    bool CmdOpen(GnmCmdTarget t) const override { return t_[(int)t].cb != nil; }
    void UseTarget(GnmCmdTarget t) override {
        cur_ = t;
        NoteEncoderTarget(t);
    }
    GnmCmdTarget Target() const override { return cur_; }
    void CommitUpload() override;
    void Commit(uint64_t seq, GnmCompletionFn done, void* ctx) override;
    // On the second queue. A seq must be above every seq committed so far, on either queue.
    void CommitAsync(uint64_t seq, GnmCompletionFn done, void* ctx) override;
    // The longest prefix of committed seqs, of both queues, that the timelines show completed, or
    // the prefix whose feedback arrived (fed_) if that is further: the feedback and the timeline
    // signal reach the CPU in either order. With one queue in use, a completion never runs while
    // CompletedSeq is below its seq; with both, a seq's completion may run while an earlier seq of
    // the other queue still runs (as on Metal 3).
    uint64_t CompletedSeq() const override;
    bool WaitForRoom(uint64_t committed, uint64_t max_outstanding, double timeout_ms) override;
    void QueueSignal(id<MTLSharedEvent> ev, uint64_t value) override;
    void QueueWait(id<MTLSharedEvent> ev, uint64_t value) override;

    bool BeginRender(const GnmPassDesc& d) override;
    void BeginCompute(bool serial) override;
    void BeginCopies() override;
    void EndEncoder() override;
    void SetEncoderLabel(NSString* label) override;
    void Barrier(uint32_t after, uint32_t before, uint8_t scope) override;
    // Guest windows that alias need no fence here: every encoder starts with a barrier after all
    // earlier work on the queue, so an access through one window is ordered after every in-flight
    // access through another (docs/gnm-ordering.md, "Aliasing guest windows"). The renderer's
    // waits are therefore nothing, and 0 is "no fence" to it.
    uint64_t AccessFence() override { return 0; }
    void WaitAccessFence(uint64_t) override {}

    void SetRenderPipeline(id<MTLRenderPipelineState> p) override;
    void SetDepthStencil(id<MTLDepthStencilState> d) override;
    void SetFixed(const GnmFixedState& f, uint32_t changed) override;
    void SetVisibilityResultMode(MTLVisibilityResultMode mode, uint64_t offset) override;
    void SetComputePipeline(id<MTLComputePipelineState> p) override;

    void Draw(const GnmDraw& d) override;
    void SetStageInRegion(MTLRegion) override {}
    void SetTessellationFactorBuffer(id<MTLBuffer>, uint64_t, uint64_t) override {}
    void DrawPatches(uint32_t, uint32_t, uint32_t, id<MTLBuffer>, uint64_t, uint32_t, uint32_t) override {}
    void Dispatch(MTLSize groups, MTLSize tpt) override;
    void DispatchThreads(MTLSize threads, MTLSize tpt) override;
    void DispatchIndirect(id<MTLBuffer> args, uint64_t off, MTLSize tpt) override;
    void CopyBuffer(id<MTLBuffer> src, uint64_t src_off, id<MTLBuffer> dst, uint64_t dst_off, uint64_t n) override;
    void FillBuffer(id<MTLBuffer> dst, uint64_t off, uint64_t n, uint8_t value) override;
    void CopyTexture(id<MTLTexture> src, id<MTLTexture> dst) override;
    void CopyTextureRegion(id<MTLTexture> src, uint32_t src_slice, uint32_t src_level, MTLOrigin src_origin, MTLSize size,
                           id<MTLTexture> dst, uint32_t dst_slice, uint32_t dst_level, MTLOrigin dst_origin) override;
    void CopyBufferToTexture(id<MTLBuffer> src, uint64_t src_off, uint64_t row_bytes, uint64_t image_bytes, MTLSize size,
                             id<MTLTexture> dst, uint32_t slice, uint32_t level, MTLOrigin origin) override;

    void MakeResident(id<MTLAllocation> a, GnmResidencyClass c) override;
    // after_seq, or for a queue whose seq is open now (being recorded, or split parts committed)
    // that seq once it is committed, whichever is later (any thread)
    void Evict(id<MTLAllocation> a, uint64_t after_seq) override;
    bool IsResident(id<MTLAllocation> a) const override { return res_.Contains(a); }
    GnmBackendStats Stats() const override;
    void NoteFrameEnd() override;

    // tests: the residency sets; the distinct command allocators in use (committed parts not yet
    // recycled, and the open ones; encoder thread); the feedback dispatch queue
    GnmResidency& Residency() { return res_; }
    size_t PartsInUse() const {
        std::unordered_set<const void*> in_use;
        for (auto& [part, seq] : busy_) in_use.insert((__bridge const void*)part.alloc);
        for (const TargetState& x : t_)
            if (x.cb) in_use.insert((__bridge const void*)x.part.alloc);
        return in_use.size();
    }
    dispatch_queue_t FeedbackQueue() const { return feedback_q_; }
    // the allocatedSize of every allocator, summed now (encoder thread; Stats keeps it in an atomic)
    uint64_t AllocatorBytesNow() const {
        uint64_t n = 0;
        for (auto& [part, seq] : busy_) n += part.alloc.allocatedSize;
        for (const Part& p : free_small_) n += p.alloc.allocatedSize;
        for (const Part& p : free_large_) n += p.alloc.allocatedSize;
        for (const TargetState& x : t_)
            if (x.cb) n += x.part.alloc.allocatedSize;
        return n;
    }

    // The allocator trim (Task 5.4, docs/metal4-backend.md "Command allocators"): an allocator
    // keeps the high-water of the largest part it ever recorded (reset gives nothing back). Parts
    // are bimodal (TLOU: about 87 % compute-only at about 0.6 MiB, the rest with a render pass at
    // 2-5.6 MiB, in runs of about 6 per frame), so a part takes an allocator of the class predicted
    // from the previous frame's parts near its position (NoteFrameEnd) and from its predecessor on
    // the target. After its reset, an allocator over TrimThreshold() goes to the large free list
    // (at most kTrimLargeKeep; full, the largest of the list and it is released), one under it to
    // the small list; free allocators beyond the in-flight peak of the recent kTrimPeakWindow
    // submits plus kTrimPeakMargin are released, the largest first.
    static constexpr uint64_t kTrimMinBytes = 3ull << 19; // 1.5 MiB: over a compute-only part
    static constexpr size_t kTrimLargeKeep = 12;
    static constexpr size_t kTrimSizeSamples = 256; // compute-only parts recorded on a fresh allocator
    static constexpr uint64_t kTrimPeakWindow = 8192;
    static constexpr size_t kTrimPeakMargin = 4;
    // max(kTrimMinBytes, 2 x the median recorded size of the last kTrimSizeSamples compute-only parts
    // that ran on a fresh allocator, whose allocatedSize after recording is exactly that part's size)
    uint64_t TrimThreshold() const { return std::max<uint64_t>(kTrimMinBytes, 2 * size_median_); }
    size_t FreeLargeCount() const { return free_large_.size(); }
    // tests: allocators over the threshold released after their reset (the large list full), as
    // opposed to the surplus release; the smallest and largest allocatedSize on a free list
    uint64_t ReleasedAtReset() const { return released_at_reset_; }
    std::pair<uint64_t, uint64_t> FreeSizes(bool large) const {
        const std::vector<Part>& l = large ? free_large_ : free_small_;
        uint64_t lo = UINT64_MAX, hi = 0;
        for (const Part& p : l) lo = std::min<uint64_t>(lo, p.alloc.allocatedSize), hi = std::max<uint64_t>(hi, p.alloc.allocatedSize);
        return {l.empty() ? 0 : lo, hi};
    }
    // tests: the allocatedSize of the open part's allocator on target t (after its reset: what it kept)
    uint64_t OpenAllocatorBytes(GnmCmdTarget t) const { return t_[(int)t].cb ? t_[(int)t].part.alloc.allocatedSize : 0; }
    // tests: the allocators (busy, free, open) whose allocatedSize is over TrimThreshold() now
    size_t LargeAllocatorsNow() const {
        const uint64_t t = TrimThreshold();
        size_t n = 0;
        for (auto& [part, seq] : busy_) n += part.alloc.allocatedSize > t;
        for (const Part& p : free_small_) n += p.alloc.allocatedSize > t;
        for (const Part& p : free_large_) n += p.alloc.allocatedSize > t;
        for (const TargetState& x : t_)
            if (x.cb) n += x.part.alloc.allocatedSize > t;
        return n;
    }

private:
    struct Part { // a command buffer object and its allocator, reused together after their seq completed
        id<MTL4CommandBuffer> cb = nil;
        id<MTL4CommandAllocator> alloc = nil;
        uint64_t bytes = 0; // alloc's allocatedSize as last counted in alloc_bytes_ (NoteAllocatorSize)
        bool fresh = false; // a new allocator not yet submitted: its first part's size is exact
        bool rendered = false; // a render encoder was opened in this part
        // Every texture this part's encoders bound, attached or copied (Keep). A Metal 4 command
        // buffer retains nothing, and a texture view is in no residency set (it counts as its
        // parent), so a view the renderer drops while its seq runs would be freed under the GPU,
        // and its gpuResourceID could go to another texture. Released when the part's seq
        // completed (Recycle); the vector keeps its capacity with the part. Buffers need no entry:
        // every buffer the GPU uses is in a residency set (PS4_GNM_RESIDENCY_CHECK), which holds
        // it until the seq of its removal completed.
        std::vector<id> keep;
    };
    // One command buffer being recorded and its open encoder. (Not named Target: the Target()
    // method would hide it.)
    struct TargetState {
        id<MTL4CommandBuffer> cb = nil; // open (between beginCommandBuffer and the commit)
        Part part;
        id<MTL4RenderCommandEncoder> renc = nil;
        id<MTL4ComputeCommandEncoder> cenc = nil;
        bool work = false; // an encoder was opened since the part began
        bool serial = false; // the open compute encoder orders its dispatches (BeginCompute(true))
        bool copied = false, dispatched = false; // since the open compute encoder's last barrier
        id<MTL4ArgumentTable> table[3] = {}; // GnmStage order
        uint64_t base[3][31] = {};           // gpuAddress bound at each buffer slot (BindBufferOffset)
        // QueueSignal after work not committed yet: issued right after this part's commit
        std::vector<std::pair<id<MTLSharedEvent>, uint64_t>> signals;
    };
    struct SeqRec { // the commits of one seq (a group of parts on one queue) and their feedback
        uint64_t seq = 0; // known once final
        uint32_t parts = 0, done = 0;
        bool final = false, failed = false;
        double start = 0, end = 0;
        NSError* error = nil;
        GnmCompletionFn fn = nullptr;
        void* ctx = nullptr;
    };
    TargetState& Cur() { return t_[(int)cur_]; }
    // keeps t alive until the open part's seq completed (Part::keep); a repeat of the last one is skipped
    static void Keep(TargetState& x, __unsafe_unretained id<MTLTexture> t) {
        if (t && (x.part.keep.empty() || x.part.keep.back() != t)) x.part.keep.push_back(t);
    }
    const TargetState& Cur() const { return t_[(int)cur_]; }
    void Recycle();                       // allocators, command buffers and bytes chunks of completed seqs
    Part AcquirePart(GnmCmdTarget t);
    bool last_rendered_[3] = {false, false, false}; // per target: the last submitted part had a render pass
    // Main's parts of the current and the previous frame (NoteFrameEnd): had a render pass, by
    // position; a part past the previous frame's count is predicted from last_rendered_
    static constexpr size_t kFrameParts = 256;
    static constexpr size_t kFrameSlack = 2;
    std::array<uint8_t, kFrameParts> frame_cur_{}, frame_prev_{};
    size_t frame_idx_ = 0, frame_prev_n_ = 0;
    void ReleasePart(Part& p);            // drops a completed, reset part (allocator and command buffer)
    void NoteFreshSize(uint64_t bytes);   // a part recorded on a fresh allocator: the trim's estimate
    void NoteAllocatorSize(Part& p); // alloc_bytes_ follows p's allocator after recording or a reset
    void BeginPart(GnmCmdTarget t, NSString* label);
    // Queue q's open seq (Main and Upload: 0; AsyncCompute: 1): its commits so far, its bytes chunks
    // and its busy parts wait for the seq number it gets at its final commit.
    struct Chunk {
        id<MTLBuffer> buf = nil;
        uint64_t last_seq = 0;
    };
    struct Group {
        uint64_t gid = 0;               // its SeqRec in recs_ (0: no commit yet)
        std::vector<size_t> busy_at;    // its parts in busy_ (absolute positions, busy_popped_ based)
        std::vector<Chunk> chunks;      // bytes chunks it filled
        Chunk chunk;                    // the bytes chunk being filled
        size_t chunk_used = 0;
        bool chunk_dirty = false;       // chunk used by this seq
    };
    static int QueueOf(GnmCmdTarget t) { return t == GnmCmdTarget::AsyncCompute ? 1 : 0; }
    id<MTL4CommandQueue> Queue(int q) const { return q ? async_queue_ : queue_; }
    // Ends and commits the open command buffers of queue q (q 0: upload first; main only if
    // with_main) as one more part of q's open seq; final: the seq's last part, numbered seq (then
    // q's timeline is signalled with it).
    void Submit(int q, bool final, uint64_t seq, bool with_main, GnmCompletionFn fn, void* ctx);
    void Split();                         // commit and reopen every open target of the current queue inside the seq
    void OpenGroup(int q);                // q's seq is open from here (Evict waits for its number)
    void UploadFirst();                   // on Main: commit recorded Upload work before a queue event
    void OnFeedback(uint64_t gid, id<MTL4CommitFeedback> fb);
    void BeforeWork();                    // a split for signals waiting on the current target
    void AfterDispatch(TargetState& x);   // the barrier after a dispatch of a serial encoder
    void BeforeDispatch(TargetState& x);  // a barrier if a copy ran since the last one
    void BeforeCopy(TargetState& x);      // a barrier if anything ran since the last one
    void EncoderBarrier(TargetState& x);
    static void OpBuffer(GnmBackend* b, GnmStage st, uint32_t slot, __unsafe_unretained id<MTLBuffer> buf, uint64_t off);
    static void OpBufferOffset(GnmBackend* b, GnmStage st, uint32_t slot, uint64_t off);
    static void OpBytes(GnmBackend* b, GnmStage st, uint32_t slot, const void* p, size_t n);
    static void OpTexture(GnmBackend* b, GnmStage st, uint32_t slot, __unsafe_unretained id<MTLTexture> t);
    static void OpSampler(GnmBackend* b, GnmStage st, uint32_t slot, __unsafe_unretained id<MTLSamplerState> s);
    static void OpUse(GnmBackend* b, GnmStage st, __unsafe_unretained id<MTLBuffer> buf, bool written);
    uint64_t BytesAddress(const void* p, size_t n); // the bytes arena: a resident address holding p[0, n)

    id<MTLDevice> dev_ = nil;
    id<MTL4CommandQueue> queue_ = nil;
    id<MTL4CommandQueue> async_queue_ = nil;
    id<MTLCommandQueue> side_queue_ = nil;
    id<MTLSharedEvent> timeline_ = nil;       // Main and Upload's queue
    id<MTLSharedEvent> async_timeline_ = nil; // AsyncCompute's queue
    // The serial queue Metal delivers commit feedback on. Owned by the backend (strong); it must
    // outlive queue_, which does not retain it (see Init and the destructor).
    dispatch_queue_t feedback_q_ = nullptr;
    GnmResidency res_;
    TargetState t_[3];
    GnmCmdTarget cur_ = GnmCmdTarget::Main;
    uint64_t last_seq_ = 0; // the last seq committed, on either queue (encoder thread)
    // the lowest seq work recorded now can get (CheckResident)
    uint64_t open_seq() const { return last_seq_ + 1; }
    Group grp_[2];
    // parts of uncompleted seqs, in commit order; UINT64_MAX until their seq's final commit
    std::deque<std::pair<Part, uint64_t>> busy_;
    size_t busy_popped_ = 0; // parts popped from busy_'s front so far
    // committed seqs whose queue's timeline has not reached them, in seq order (CompletedSeq)
    struct Flight {
        uint64_t seq;
        int q;
    };
    mutable std::mutex flight_mu_;
    mutable std::deque<Flight> flight_;
    mutable uint64_t flight_done_ = 0;
    // Evict of an allocation while a queue's seq is open: the removal waits for that seq's number
    struct PendingEvict {
        id<MTLAllocation> a;
        uint64_t after = 0;
        uint8_t queues = 0; // bit q: q's open seq
    };
    std::mutex evict_mu_;
    uint8_t open_queues_ = 0; // under evict_mu_: bit q: q has an open seq
    std::vector<PendingEvict> pending_evicts_; // under evict_mu_ (an allocation may be listed twice)
    std::vector<Part> free_small_;       // reset parts at most TrimThreshold() (any order)
    std::vector<Part> free_large_;       // reset parts over it, at most kTrimLargeKeep
    std::array<uint64_t, kTrimSizeSamples> size_ring_{}, size_scratch_{};
    uint64_t size_samples_ = 0;          // fresh parts sampled so far
    uint64_t size_median_ = 0;
    uint64_t released_at_reset_ = 0;     // ReleasedAtReset (encoder thread)
    uint64_t peak_submits_ = 0;          // submits in the current peak window
    size_t peak_cur_ = 0, peak_prev_ = 0; // most parts in flight in the current and the previous window
    std::mutex seq_mu_;
    std::condition_variable feedback_cv_; // under seq_mu_: feedback_pending_ reached 0
    uint64_t feedback_pending_ = 0;        // under seq_mu_: commits whose feedback has not run yet
    std::unordered_map<uint64_t, SeqRec> recs_; // under seq_mu_: by group id
    uint64_t next_gid_ = 0;                     // under seq_mu_
    std::deque<uint64_t> order_[2];             // under seq_mu_: final groups of each queue not delivered, in seq order
    uint64_t final_seq_ = 0;                    // under seq_mu_: the last seq made final
    std::atomic<uint64_t> fed_{0}; // every seq up to this one delivered, on both queues (CompletedSeq)
    std::vector<SeqRec> ready_;         // under feedback_mu_: completions to run, in seq order
    // OnFeedback runs on the serial feedback queue (probed: dispatch_assert_queue holds in each
    // handler, none overlap), but Metal hands the handlers to it in a way ThreadSanitizer does not
    // see, so it reports ready_ as raced between two handlers. This lock, never contended, states
    // the order it cannot see.
    std::mutex feedback_mu_;
    // bytes arena: 256 KiB shared chunks, resident (Builtin), reused once their last seq completed
    std::deque<Chunk> chunks_busy_;
    std::vector<id<MTLBuffer>> chunks_free_;
    bool full_barriers_ = false, no_barriers_ = false;
    // allocators_: live (created - released); GnmBackendStats::allocators
    std::atomic<uint64_t> commits_{0}, split_commits_{0}, allocators_{0}, resets_{0}, created_{0}, released_{0};
    std::atomic<uint64_t> alloc_bytes_{0}; // GnmBackendStats::allocator_bytes (read by any thread)
    // PS4_GNM_RESIDENCY_CHECK=1 (read once in Init): every resource bound, used, drawn from, copied
    // or attached is looked up in the residency sets (CheckResident); off, the bind path is unchanged
    bool check_residency_ = false;
    std::unordered_set<const void*> reported_; // the holder of the encoder's state: resources reported once each
    std::atomic<uint64_t> residency_asserts_{0};
    void CheckResident(id<MTLResource> r, const char* what);
};

} // namespace ps4gnm

#endif // PS4_GNM_BACKEND_METAL4_H
