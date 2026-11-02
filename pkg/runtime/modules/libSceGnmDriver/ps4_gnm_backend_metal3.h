// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - the Metal 3 renderer backend (ps4_gnm_backend.h): MTLCommandQueue, one
// MTLCommandBuffer per seq (plus the upload one), Metal's hazard tracking between encoders,
// setBytes, useResource. The renderer's Metal 3 calls, moved here unchanged. Asynchronous
// compute: a second MTLCommandQueue for the AsyncCompute target, whose command buffers are seqs of
// the same numbering (CommitAsync); Metal tracks nothing between the two queues.
#ifndef PS4_GNM_BACKEND_METAL3_H
#define PS4_GNM_BACKEND_METAL3_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

#include "ps4_gnm_backend.h"

namespace ps4gnm {

class Metal3Backend final : public GnmBackend {
public:
    Metal3Backend();
    ~Metal3Backend() override;
    GnmBackendKind Kind() const override { return GnmBackendKind::Metal3; }
    uint32_t Caps() const override { return kGnmCapAll | kGnmCapAsyncCompute; }
    bool Init(id<MTLDevice> dev, std::string* why) override;
    id<MTLDevice> Device() const override { return dev_; }
    id<MTLCommandQueue> SideQueue() const override { return queue_; }

    void BeginCmd(GnmCmdTarget t, NSString* label, bool encoder_status) override;
    bool CmdOpen(GnmCmdTarget t) const override { return t_[(int)t].cmd != nil; }
    void UseTarget(GnmCmdTarget t) override {
        cur_ = t;
        NoteEncoderTarget(t);
    }
    GnmCmdTarget Target() const override { return cur_; }
    void CommitUpload() override;
    void Commit(uint64_t seq, GnmCompletionFn done, void* ctx) override;
    void CommitAsync(uint64_t seq, GnmCompletionFn done, void* ctx) override;
    uint64_t CompletedSeq() const override;
    // The queue was made with kGnmMaxCommandBuffers: BeginCmd itself blocks while that many are
    // uncompleted (Metal's own limit), so there is nothing to wait for here.
    bool WaitForRoom(uint64_t, uint64_t, double) override { return true; }
    void QueueSignal(id<MTLSharedEvent> ev, uint64_t value) override;
    void QueueWait(id<MTLSharedEvent> ev, uint64_t value) override;

    bool BeginRender(const GnmPassDesc& d) override;
    void BeginCompute(bool serial) override;
    void BeginCopies() override;
    void EndEncoder() override;
    void SetEncoderLabel(NSString* label) override;
    void Barrier(uint32_t after, uint32_t before, uint8_t scope) override;

    void SetRenderPipeline(id<MTLRenderPipelineState> p) override { [Cur().renc setRenderPipelineState:p]; }
    void SetDepthStencil(id<MTLDepthStencilState> d) override { [Cur().renc setDepthStencilState:d]; }
    void SetFixed(const GnmFixedState& f, uint32_t changed) override;
    void SetVisibilityResultMode(MTLVisibilityResultMode mode, uint64_t offset) override {
        [Cur().renc setVisibilityResultMode:mode offset:offset];
    }
    void SetComputePipeline(id<MTLComputePipelineState> p) override { [Cur().cenc setComputePipelineState:p]; }

    void Draw(const GnmDraw& d) override;
    void SetStageInRegion(MTLRegion region) override;
    void SetStageInRegionIndirect(id<MTLBuffer> buf, uint64_t offset) override;
    void SetTessellationFactorBuffer(id<MTLBuffer> buf, uint64_t offset, uint64_t instance_stride) override;
    void DrawPatches(uint32_t num_control_points, uint32_t patch_start, uint32_t patch_count,
                     id<MTLBuffer> patch_index_buffer, uint64_t patch_index_buffer_offset,
                     uint32_t instance_count, uint32_t base_instance) override;
    void Dispatch(MTLSize groups, MTLSize tpt) override {
        BeforeCommand();
        [Cur().cenc dispatchThreadgroups:groups threadsPerThreadgroup:tpt];
    }
    void DispatchThreads(MTLSize threads, MTLSize tpt) override {
        BeforeCommand();
        [Cur().cenc dispatchThreads:threads threadsPerThreadgroup:tpt];
    }
    void DispatchIndirect(id<MTLBuffer> args, uint64_t off, MTLSize tpt) override {
        BeforeCommand();
        [Cur().cenc dispatchThreadgroupsWithIndirectBuffer:args indirectBufferOffset:off threadsPerThreadgroup:tpt];
    }
    void CopyBuffer(id<MTLBuffer> src, uint64_t src_off, id<MTLBuffer> dst, uint64_t dst_off, uint64_t n) override {
        BeforeCommand();
        [Cur().blit copyFromBuffer:src sourceOffset:src_off toBuffer:dst destinationOffset:dst_off size:n];
    }
    void FillBuffer(id<MTLBuffer> dst, uint64_t off, uint64_t n, uint8_t value) override {
        BeforeCommand();
        [Cur().blit fillBuffer:dst range:NSMakeRange(off, n) value:value];
    }
    void CopyTexture(id<MTLTexture> src, id<MTLTexture> dst) override {
        BeforeCommand();
        [Cur().blit copyFromTexture:src toTexture:dst];
    }
    void CopyTextureRegion(id<MTLTexture> src, uint32_t src_slice, uint32_t src_level, MTLOrigin src_origin, MTLSize size,
                           id<MTLTexture> dst, uint32_t dst_slice, uint32_t dst_level, MTLOrigin dst_origin) override;
    void CopyBufferToTexture(id<MTLBuffer> src, uint64_t src_off, uint64_t row_bytes, uint64_t image_bytes, MTLSize size,
                             id<MTLTexture> dst, uint32_t slice, uint32_t level, MTLOrigin origin) override;

    void MakeResident(id<MTLAllocation>, GnmResidencyClass) override {} // bound resources are resident by themselves
    void Evict(id<MTLAllocation>, uint64_t) override {}
    uint64_t AccessFence() override;
    void WaitAccessFence(uint64_t id) override;
    bool AccessFenceOtherQueue(uint64_t id) const override;
    bool IsResident(id<MTLAllocation>) const override { return true; }
    GnmBackendStats Stats() const override;


private:
    // One command buffer being recorded and its open encoder. (Not named Target: the Target()
    // method would hide it.)
    struct TargetState {
        id<MTLCommandBuffer> cmd = nil;
        id<MTLRenderCommandEncoder> renc = nil;
        id<MTLComputeCommandEncoder> cenc = nil;
        id<MTLBlitCommandEncoder> blit = nil;
        uint64_t fence = 0;          // the open encoder's AccessFence id, 0: none
        std::vector<uint64_t> waited; // AccessFence ids the open encoder waited for
    };
    TargetState& Cur() { return t_[(int)cur_]; }
    const TargetState& Cur() const { return t_[(int)cur_]; }
    static void OpBuffer(GnmBackend* b, GnmStage st, uint32_t slot, __unsafe_unretained id<MTLBuffer> buf, uint64_t off);
    static void OpBufferOffset(GnmBackend* b, GnmStage st, uint32_t slot, uint64_t off);
    static void OpBytes(GnmBackend* b, GnmStage st, uint32_t slot, const void* p, size_t n);
    static void OpTexture(GnmBackend* b, GnmStage st, uint32_t slot, __unsafe_unretained id<MTLTexture> t);
    static void OpSampler(GnmBackend* b, GnmStage st, uint32_t slot, __unsafe_unretained id<MTLSamplerState> s);
    static void OpUse(GnmBackend* b, GnmStage st, __unsafe_unretained id<MTLBuffer> buf, bool written);

    id<MTLDevice> dev_ = nil;
    id<MTLCommandQueue> queue_ = nil, async_queue_ = nil;
    TargetState t_[3];
    GnmCmdTarget cur_ = GnmCmdTarget::Main;
    // committed seqs in seq order, with the queue they went to (0 main, 1 async)
    struct Inflight {
        uint64_t seq;
        id<MTLCommandBuffer> cb;
        int queue;
    };
    mutable std::mutex inflight_mu_;
    mutable std::deque<Inflight> inflight_;
    mutable uint64_t completed_ = 0;
    // per queue: the highest seq whose completed handler started (handlers of one queue run in
    // commit order; shared with the handlers, which may outlive the backend)
    std::shared_ptr<std::atomic<uint64_t>> handled_[2] = {std::make_shared<std::atomic<uint64_t>>(0),
                                                          std::make_shared<std::atomic<uint64_t>>(0)};
    void CommitOn(int queue, id<MTLCommandBuffer> cb, uint64_t seq, GnmCompletionFn done, void* ctx);
    // completion handlers that ran to their end (after done()), against the main commits made: the
    // destructor waits for every one, as CompletedSeq counts a seq when its handler starts
    struct Finished {
        std::mutex mu;
        std::condition_variable cv;
        uint64_t n = 0;
    };
    std::shared_ptr<Finished> finished_ = std::make_shared<Finished>();
    uint64_t main_commits_ = 0; // commits with a completion handler (both queues); encoder thread
    uint64_t main_cbs_ = 0;     // main command buffers committed; encoder thread
    std::atomic<uint64_t> commits_{0};
    // AccessFence (guest windows that alias, docs/gnm-ordering.md): the fences of
    // live ids, id first_fence_ first; each with the target of its encoder, for Main the command
    // buffer (main_cbs_ then: the upload command buffer of a seq is committed before its main
    // one), and the seq of its command buffer once committed (kOpenSeq before: Commit stamps the
    // fences of Main and Upload, CommitAsync those of AsyncCompute). Fences are retired (their
    // objects reused) in id order, each once its own seq completed. Encoder thread.
    static constexpr uint64_t kOpenSeq = UINT64_MAX;
    struct LiveFence {
        id<MTLFence> fence;
        GnmCmdTarget target;
        uint64_t main_cb;
        uint64_t seq;
    };
    std::deque<LiveFence> fences_;
    uint64_t first_fence_ = 1;
    std::vector<id<MTLFence>> free_fences_;
    void StampFences(bool async, uint64_t seq);
    void RetireFences();
    uint64_t access_fences_ = 0, alias_waits_ = 0;
};

} // namespace ps4gnm

#endif // PS4_GNM_BACKEND_METAL3_H
