// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - the Metal 3 renderer backend (ps4_gnm_backend.h): MTLCommandQueue, one
// MTLCommandBuffer per seq (plus the upload one), Metal's hazard tracking between encoders,
// setBytes, useResource. The renderer's Metal 3 calls, moved here unchanged.
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
    uint32_t Caps() const override { return kGnmCapAll; }
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
    void SetComputePipeline(id<MTLComputePipelineState> p) override { [Cur().cenc setComputePipelineState:p]; }

    void Draw(const GnmDraw& d) override;
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
    id<MTLCommandQueue> queue_ = nil;
    TargetState t_[2];
    GnmCmdTarget cur_ = GnmCmdTarget::Main;
    mutable std::mutex inflight_mu_;
    mutable std::deque<std::pair<uint64_t, id<MTLCommandBuffer>>> inflight_; // committed, by seq
    mutable uint64_t completed_ = 0;
    // the highest seq whose completed handler started (shared with the handlers, which may outlive
    // the backend)
    std::shared_ptr<std::atomic<uint64_t>> handled_ = std::make_shared<std::atomic<uint64_t>>(0);
    // completion handlers that ran to their end (after done()), against the main commits made: the
    // destructor waits for every one, as CompletedSeq counts a seq when its handler starts
    struct Finished {
        std::mutex mu;
        std::condition_variable cv;
        uint64_t n = 0;
    };
    std::shared_ptr<Finished> finished_ = std::make_shared<Finished>();
    uint64_t main_commits_ = 0; // encoder thread
    std::atomic<uint64_t> commits_{0};
    // AccessFence (guest windows that alias, docs/gnm-ordering.md): the fences of
    // live ids, id first_fence_ first; each with the target of its encoder and, for Main, the
    // command buffer (main_commits_ then: the upload command buffer of a seq is committed before
    // its main one). The fences of a seq are retired (their objects reused) once it completed.
    // Encoder thread.
    struct LiveFence {
        id<MTLFence> fence;
        GnmCmdTarget target;
        uint64_t main_cb;
    };
    std::deque<LiveFence> fences_;
    uint64_t first_fence_ = 1;
    std::deque<std::pair<uint64_t, uint64_t>> fences_by_seq_; // (seq, the last id made before its commit)
    std::vector<id<MTLFence>> free_fences_;
    void RetireFences();
    uint64_t access_fences_ = 0, alias_waits_ = 0;
};

} // namespace ps4gnm

#endif // PS4_GNM_BACKEND_METAL3_H
