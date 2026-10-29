// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - the Metal 3 renderer backend (ps4_gnm_backend_metal3.h). Each method is the Metal 3
// code of the renderer site named in its comment (ps4_gnm_draw.mm before the backend seam).
#include "ps4_gnm_backend_metal3.h"

#include <stdio.h>

#include <algorithm>
#include <chrono>
#include <memory>

namespace ps4gnm {

Metal3Backend::Metal3Backend() {
    ops_.buffer = OpBuffer;
    ops_.buffer_offset = OpBufferOffset;
    ops_.bytes = OpBytes;
    ops_.texture = OpTexture;
    ops_.sampler = OpSampler;
    ops_.use = OpUse;
}

// Every completion handler of a main commit has returned before the backend's members (and the
// renderer state its done() callbacks use) go: WaitAll may return while one is still running, as
// CompletedSeq counts a seq when its handler starts. A GPU that never finishes is FATAL after 10 s,
// as on Metal 4.
Metal3Backend::~Metal3Backend() {
    std::unique_lock<std::mutex> lk(finished_->mu);
    if (!finished_->cv.wait_for(lk, std::chrono::seconds(10), [&] { return finished_->n >= main_commits_; })) {
        fprintf(stderr, "[gnm-draw] FATAL: Metal3Backend destroyed with %llu of %llu completion handlers not finished "
                        "after 10 s (a GPU hang?)\n",
                (unsigned long long)(main_commits_ - finished_->n), (unsigned long long)main_commits_);
        abort();
    }
}

std::unique_ptr<GnmBackend> MakeMetal3Backend() { return std::make_unique<Metal3Backend>(); }

// Init: S.queue = [S.dev newCommandQueueWithMaxCommandBufferCount:kMaxCommandBuffers]; label "gnm"
bool Metal3Backend::Init(id<MTLDevice> dev, std::string* why) {
    dev_ = dev;
    queue_ = [dev newCommandQueueWithMaxCommandBufferCount:kGnmMaxCommandBuffers];
    if (!queue_) {
        if (why) *why = "newCommandQueueWithMaxCommandBufferCount failed";
        return false;
    }
    queue_.label = @"gnm";
    return true;
}

// EnsureCmd / SampledTexture (S.upload_cmd): commandBuffer or commandBufferWithDescriptor with
// MTLCommandBufferErrorOptionEncoderExecutionStatus; the upload command buffer logs its failure.
void Metal3Backend::BeginCmd(GnmCmdTarget t, NSString* label, bool encoder_status) {
    TargetState& x = t_[(int)t];
    if (encoder_status) {
        MTLCommandBufferDescriptor* d = [MTLCommandBufferDescriptor new];
        d.errorOptions = MTLCommandBufferErrorOptionEncoderExecutionStatus;
        x.cmd = [queue_ commandBufferWithDescriptor:d];
    } else {
        x.cmd = [queue_ commandBuffer];
    }
    x.cmd.label = label;
    if (t == GnmCmdTarget::Upload)
        [x.cmd addCompletedHandler:^(id<MTLCommandBuffer> done) {
            if (done.status == MTLCommandBufferStatusError)
                fprintf(stderr, "[gnm-draw] texture upload command buffer failed: %s\n",
                        done.error.localizedDescription.UTF8String);
        }];
}

// Commit: [S.upload_cmd commit] before S.cmd (same queue: the uploads run first)
void Metal3Backend::CommitUpload() {
    TargetState& u = t_[(int)GnmCmdTarget::Upload];
    if (!u.cmd) return;
    [u.cmd commit];
    u.cmd = nil;
    commits_.fetch_add(1, std::memory_order_relaxed);
}

// Commit: addCompletedHandler (GPU times, status) + commit; S.inflight
void Metal3Backend::Commit(uint64_t seq, GnmCompletionFn done, void* ctx) {
    CommitUpload();
    if (!fences_.empty()) {
        // every fence made so far is in this seq's command buffers (or an earlier one's)
        fences_by_seq_.push_back({seq, first_fence_ + fences_.size() - 1});
        RetireFences();
    }
    TargetState& m = t_[(int)GnmCmdTarget::Main];
    id<MTLCommandBuffer> cb = m.cmd;
    m.cmd = nil;
    // CompletedSeq() reaches seq before its completion runs (the status its walk reads may not say
    // Completed yet inside this handler; handlers of one queue run in commit order). The handler
    // holds its own reference to the counter: it may run after the backend is gone.
    std::shared_ptr<std::atomic<uint64_t>> handled = handled_;
    std::shared_ptr<Finished> finished = finished_;
    main_commits_++;
    [cb addCompletedHandler:^(id<MTLCommandBuffer> c) {
        uint64_t h = handled->load(std::memory_order_relaxed);
        while (h < seq && !handled->compare_exchange_weak(h, seq, std::memory_order_release)) {
        }
        GnmCompletion r;
        r.seq = seq;
        r.failed = c.status == MTLCommandBufferStatusError;
        r.gpu_start = c.GPUStartTime;
        r.gpu_end = c.GPUEndTime;
        r.error = c.error;
        done(ctx, r);
        {
            std::lock_guard<std::mutex> lk(finished->mu);
            finished->n++;
        }
        finished->cv.notify_all();
    }];
    [cb commit];
    commits_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(inflight_mu_);
    inflight_.emplace_back(seq, cb);
}

// WaitAll's status walk over S.inflight: the longest completed prefix of the committed seqs
uint64_t Metal3Backend::CompletedSeq() const {
    std::lock_guard<std::mutex> lk(inflight_mu_);
    const uint64_t h = handled_->load(std::memory_order_acquire);
    while (!inflight_.empty() && (inflight_.front().first <= h ||
                                  inflight_.front().second.status >= MTLCommandBufferStatusCompleted)) {
        completed_ = std::max(completed_, inflight_.front().first);
        inflight_.pop_front();
    }
    completed_ = std::max(completed_, h);
    return completed_;
}

// GmCloseChain / DebugStallGpu / GmWaitForCommitted: encodeSignalEvent / encodeWaitForEvent
void Metal3Backend::QueueSignal(id<MTLSharedEvent> ev, uint64_t value) { [Cur().cmd encodeSignalEvent:ev value:value]; }
void Metal3Backend::QueueWait(id<MTLSharedEvent> ev, uint64_t value) { [Cur().cmd encodeWaitForEvent:ev value:value]; }

static MTLLoadAction LoadOf(GnmLoad l) {
    return l == GnmLoad::Clear ? MTLLoadActionClear : l == GnmLoad::DontCare ? MTLLoadActionDontCare : MTLLoadActionLoad;
}
static MTLStoreAction StoreOf(GnmStore s) {
    return s == GnmStore::MultisampleResolve ? MTLStoreActionMultisampleResolve : MTLStoreActionStore;
}

// BeginPass (:5664-5710) and every helper pass's MTLRenderPassDescriptor
bool Metal3Backend::BeginRender(const GnmPassDesc& d) {
    TargetState& x = Cur();
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
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
    x.renc = [x.cmd renderCommandEncoderWithDescriptor:rp];
    if (!x.renc) return false;
    if (d.label) x.renc.label = d.label;
    NoteEncoderOpened(GnmEncoder::Render);
    return true;
}

// ComputeEncoder (:5745-5747) and EncodeGpuUploadAt's side encoder (:5258)
void Metal3Backend::BeginCompute(bool serial) {
    TargetState& x = Cur();
    x.cenc = serial ? [x.cmd computeCommandEncoder] : [x.cmd computeCommandEncoderWithDispatchType:MTLDispatchTypeConcurrent];
    NoteEncoderOpened(x.cenc ? GnmEncoder::Compute : GnmEncoder::None);
}

void Metal3Backend::BeginCopies() {
    TargetState& x = Cur();
    x.blit = [x.cmd blitCommandEncoder];
    NoteEncoderOpened(x.blit ? GnmEncoder::Copy : GnmEncoder::None);
}

// EndEncoders (:1671-1679) and every [e endEncoding] / [bl endEncoding]
void Metal3Backend::EndEncoder() {
    TargetState& x = Cur();
    if (x.fence) {
        id<MTLFence> f = fences_[x.fence - first_fence_].fence;
        if (x.renc) [x.renc updateFence:f afterStages:MTLRenderStageVertex | MTLRenderStageFragment];
        else if (x.cenc) [x.cenc updateFence:f];
        else if (x.blit) [x.blit updateFence:f];
        x.fence = 0;
    }
    x.waited.clear();
    if (x.renc) [x.renc endEncoding], x.renc = nil;
    if (x.cenc) [x.cenc endEncoding], x.cenc = nil;
    if (x.blit) [x.blit endEncoding], x.blit = nil;
    NoteEncoderEnded();
}

void Metal3Backend::SetEncoderLabel(NSString* label) {
    TargetState& x = Cur();
    if (x.renc) x.renc.label = label;
    else if (x.cenc) x.cenc.label = label;
    else if (x.blit) x.blit.label = label;
}

static MTLRenderStages RenderStagesOf(uint32_t s) {
    MTLRenderStages r = 0;
    if (s & kGnmStageVertex) r |= MTLRenderStageVertex;
    if (s & kGnmStageFragment) r |= MTLRenderStageFragment;
    return r;
}

// ComputeBarrierImpl (:6538), the guest memory chain (:7861, :7993), the upload detile (:5261):
// memoryBarrierWithScope on the compute encoder; ps4gnm_rect::Encode: the render-stage form
void Metal3Backend::Barrier(uint32_t after, uint32_t before, uint8_t scope) {
    TargetState& x = Cur();
    if (x.cenc) [x.cenc memoryBarrierWithScope:(MTLBarrierScope)scope];
    else if (x.renc)
        [x.renc memoryBarrierWithScope:(MTLBarrierScope)scope afterStages:RenderStagesOf(after)
                          beforeStages:RenderStagesOf(before)];
}

// DrawEncode (:7473-7483)
void Metal3Backend::SetFixed(const GnmFixedState& f, uint32_t changed) {
    id<MTLRenderCommandEncoder> e = Cur().renc;
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

// DrawEncode (:7522-7550), ps4gnm_rect::Encode, the helper passes' full-screen triangles
void Metal3Backend::Draw(const GnmDraw& d) {
    BeforeCommand();
    id<MTLRenderCommandEncoder> e = Cur().renc;
    switch (d.kind) {
    case GnmDrawKind::Direct:
        if (d.instances == 1 && d.base_instance == 0)
            [e drawPrimitives:d.prim vertexStart:d.vertex_start vertexCount:d.count];
        else
            [e drawPrimitives:d.prim vertexStart:d.vertex_start vertexCount:d.count instanceCount:d.instances
                 baseInstance:d.base_instance];
        break;
    case GnmDrawKind::Indexed:
        [e drawIndexedPrimitives:d.prim indexCount:d.count indexType:d.index_type indexBuffer:d.index_buffer
               indexBufferOffset:d.index_offset instanceCount:d.instances baseVertex:d.base_vertex
                    baseInstance:d.base_instance];
        break;
    case GnmDrawKind::Indirect:
        [e drawPrimitives:d.prim indirectBuffer:d.indirect_buffer indirectBufferOffset:d.indirect_offset];
        break;
    case GnmDrawKind::IndexedIndirect:
        [e drawIndexedPrimitives:d.prim indexType:d.index_type indexBuffer:d.index_buffer indexBufferOffset:d.index_offset
                  indirectBuffer:d.indirect_buffer indirectBufferOffset:d.indirect_offset];
        break;
    }
}

// MipChainFor (:5070), DrawEncode's grown depth array (:7131), ResolvePass (:6060)
void Metal3Backend::CopyTextureRegion(id<MTLTexture> src, uint32_t src_slice, uint32_t src_level, MTLOrigin src_origin,
                                      MTLSize size, id<MTLTexture> dst, uint32_t dst_slice, uint32_t dst_level,
                                      MTLOrigin dst_origin) {
    BeforeCommand();
    [Cur().blit copyFromTexture:src sourceSlice:src_slice sourceLevel:src_level sourceOrigin:src_origin sourceSize:size
                      toTexture:dst destinationSlice:dst_slice destinationLevel:dst_level destinationOrigin:dst_origin];
}

// EncodeGpuUploadAt (:5292)
void Metal3Backend::CopyBufferToTexture(id<MTLBuffer> src, uint64_t src_off, uint64_t row_bytes, uint64_t image_bytes,
                                        MTLSize size, id<MTLTexture> dst, uint32_t slice, uint32_t level, MTLOrigin origin) {
    BeforeCommand();
    [Cur().blit copyFromBuffer:src sourceOffset:src_off sourceBytesPerRow:row_bytes sourceBytesPerImage:image_bytes
                    sourceSize:size toTexture:dst destinationSlice:slice destinationLevel:level destinationOrigin:origin];
}

GnmBackendStats Metal3Backend::Stats() const {
    GnmBackendStats s;
    s.commits = commits_.load(std::memory_order_relaxed);
    s.access_fences = access_fences_;
    s.alias_waits = alias_waits_;
    return s;
}

// ---- Binder (:2903-2965): set*Buffer / set*BufferOffset / set*Bytes / set*Texture / set*SamplerState / useResource
void Metal3Backend::OpBuffer(GnmBackend* b, GnmStage st, uint32_t slot, __unsafe_unretained id<MTLBuffer> buf, uint64_t off) {
    TargetState& x = static_cast<Metal3Backend*>(b)->Cur();
    if (st == GnmStage::Vertex) [x.renc setVertexBuffer:buf offset:off atIndex:slot];
    else if (st == GnmStage::Fragment) [x.renc setFragmentBuffer:buf offset:off atIndex:slot];
    else [x.cenc setBuffer:buf offset:off atIndex:slot];
}
void Metal3Backend::OpBufferOffset(GnmBackend* b, GnmStage st, uint32_t slot, uint64_t off) {
    TargetState& x = static_cast<Metal3Backend*>(b)->Cur();
    if (st == GnmStage::Vertex) [x.renc setVertexBufferOffset:off atIndex:slot];
    else if (st == GnmStage::Fragment) [x.renc setFragmentBufferOffset:off atIndex:slot];
    else [x.cenc setBufferOffset:off atIndex:slot];
}
void Metal3Backend::OpBytes(GnmBackend* b, GnmStage st, uint32_t slot, const void* p, size_t n) {
    TargetState& x = static_cast<Metal3Backend*>(b)->Cur();
    if (st == GnmStage::Vertex) [x.renc setVertexBytes:p length:n atIndex:slot];
    else if (st == GnmStage::Fragment) [x.renc setFragmentBytes:p length:n atIndex:slot];
    else [x.cenc setBytes:p length:n atIndex:slot];
}
void Metal3Backend::OpTexture(GnmBackend* b, GnmStage st, uint32_t slot, __unsafe_unretained id<MTLTexture> t) {
    TargetState& x = static_cast<Metal3Backend*>(b)->Cur();
    if (st == GnmStage::Vertex) [x.renc setVertexTexture:t atIndex:slot];
    else if (st == GnmStage::Fragment) [x.renc setFragmentTexture:t atIndex:slot];
    else [x.cenc setTexture:t atIndex:slot];
}
void Metal3Backend::OpSampler(GnmBackend* b, GnmStage st, uint32_t slot, __unsafe_unretained id<MTLSamplerState> s) {
    TargetState& x = static_cast<Metal3Backend*>(b)->Cur();
    if (st == GnmStage::Vertex) [x.renc setVertexSamplerState:s atIndex:slot];
    else if (st == GnmStage::Fragment) [x.renc setFragmentSamplerState:s atIndex:slot];
    else [x.cenc setSamplerState:s atIndex:slot];
}
void Metal3Backend::OpUse(GnmBackend* b, GnmStage st, __unsafe_unretained id<MTLBuffer> buf, bool written) {
    TargetState& x = static_cast<Metal3Backend*>(b)->Cur();
    const MTLResourceUsage u = written ? (MTLResourceUsageRead | MTLResourceUsageWrite) : MTLResourceUsageRead;
    if (st == GnmStage::Compute) [x.cenc useResource:buf usage:u];
    else [x.renc useResource:buf usage:u stages:(st == GnmStage::Vertex ? MTLRenderStageVertex : MTLRenderStageFragment)];
}

// The open encoder's fence, made at the first call and updated at its end (EndEncoder).
uint64_t Metal3Backend::AccessFence() {
    TargetState& x = Cur();
    if (x.fence) return x.fence;
    id<MTLFence> f = nil;
    if (!free_fences_.empty()) {
        f = free_fences_.back();
        free_fences_.pop_back();
    } else {
        f = [dev_ newFence];
    }
    fences_.push_back({f, cur_, main_commits_});
    x.fence = first_fence_ + fences_.size() - 1;
    access_fences_++;
    return x.fence;
}

// Not from the upload command buffer for a fence of the open main one: that one is committed
// after it (the upload runs first, as it does for any one object).
void Metal3Backend::WaitAccessFence(uint64_t id) {
    TargetState& x = Cur();
    if (!id || id < first_fence_ || id == x.fence) return; // none, its seq completed, or this encoder's own
    if (id >= first_fence_ + fences_.size()) return;
    const LiveFence& l = fences_[id - first_fence_];
    if (cur_ == GnmCmdTarget::Upload && l.target == GnmCmdTarget::Main && l.main_cb == main_commits_) return;
    for (uint64_t w : x.waited)
        if (w == id) return;
    if (x.renc) [x.renc waitForFence:l.fence beforeStages:MTLRenderStageVertex | MTLRenderStageFragment];
    else if (x.cenc) [x.cenc waitForFence:l.fence];
    else if (x.blit) [x.blit waitForFence:l.fence];
    else return;
    x.waited.push_back(id);
    alias_waits_++;
}

// The fences of completed seqs go back to the free list.
void Metal3Backend::RetireFences() {
    const uint64_t done = CompletedSeq();
    while (!fences_by_seq_.empty() && fences_by_seq_.front().first <= done) {
        const uint64_t last = fences_by_seq_.front().second;
        while (first_fence_ <= last && !fences_.empty()) {
            free_fences_.push_back(fences_.front().fence);
            fences_.pop_front();
            first_fence_++;
        }
        fences_by_seq_.pop_front();
    }
}

} // namespace ps4gnm
