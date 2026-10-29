// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4Recomp - the renderer backend seam (docs/metal4-backend.md).
 *
 * The GNM renderer (ps4_gnm_draw.mm) decides what is encoded: render passes and their clears,
 * bindings, barriers, residency and the seq timeline of committed work. A GnmBackend executes it
 * on one Metal API: Metal3Backend (ps4_gnm_backend_metal3.mm, MTLCommandQueue) or Metal4Backend
 * (ps4_gnm_backend_metal4.mm, MTL4CommandQueue). Both use the same device objects: buffers,
 * textures, samplers and Metal 3 pipeline states. ps4draw::Init chooses one from PS4_GNM_METAL
 * (ChooseBackend).
 *
 * Threading: everything runs on the encoder (under encq::state_mu), except CompletedSeq and Stats,
 * which any thread may call, and the residency calls, which are thread-safe.
 *
 * Lifetime: the renderer may drop an object it passed in as soon as the call returns. The GPU still
 * gets it: a Metal 3 command buffer retains what is encoded into it; on Metal 4 every buffer is in a
 * GnmResidency set (which holds it until the seq of its removal completed), and Metal4Backend keeps
 * every texture an encoder bound, attached or copied (views are in no set) until the seq of its
 * command buffer completed.
 */
#ifndef PS4_GNM_BACKEND_H
#define PS4_GNM_BACKEND_H

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>

namespace ps4gnm {

class GnmBackend;

// Shader stage of a binding (the renderer's former ps4draw::Stage).
enum class GnmStage : uint8_t { Vertex = 0, Fragment = 1, Compute = 2 };

enum class GnmBackendKind : uint8_t { Metal3 = 3, Metal4 = 4 };

// What a backend implements. Metal3Backend: kGnmCapAll.
enum GnmCaps : uint32_t {
    kGnmCapClears = 1u << 0,  // render passes (load, clear, store, resolve), direct draws, CopyTexture: present
    kGnmCapCompute = 1u << 1, // compute encoders, compute bindings, Dispatch*
    kGnmCapCopies = 1u << 2,  // CopyBuffer, FillBuffer, CopyTextureRegion, CopyBufferToTexture
    kGnmCapDraws = 1u << 3,   // every GnmDraw kind, SetFixed, barriers inside a render pass
    kGnmCapAll = kGnmCapClears | kGnmCapCompute | kGnmCapCopies | kGnmCapDraws,
};

// A Metal 3 queue holds at most this many uncompleted command buffers; the Metal 4 backend applies
// the same limit to its seq timeline (WaitForRoom).
constexpr uint64_t kGnmMaxCommandBuffers = 64;

// The command buffers being recorded: the main one (one seq of the timeline) and the texture
// upload one, committed right before it (the renderer's former S.upload_cmd).
enum class GnmCmdTarget : uint8_t { Main = 0, Upload = 1 };

// The encoder open on the current target. At most one is open per target.
enum class GnmEncoder : uint8_t { None = 0, Render, Compute, Copy };

// Barrier stages, as MTLStages values (Metal 4 passes them on, Metal 3 maps them).
enum GnmStageBits : uint32_t {
    kGnmStageVertex = 1u << 0,
    kGnmStageFragment = 1u << 1,
    kGnmStageDispatch = 1u << 27,
    kGnmStageBlit = 1u << 28,
};
// The memory a barrier orders on Metal 3 (MTLBarrierScope values). Metal 4 barriers order all
// memory with device visibility.
enum GnmScopeBits : uint8_t { kGnmScopeBuffers = 1, kGnmScopeTextures = 2 };

// The residency set an allocation belongs to (Metal 4; Metal 3 makes bound resources resident by itself).
enum class GnmResidencyClass : uint8_t { Guest = 0, Texture = 1, Builtin = 2 };
constexpr int kGnmResidencyClasses = 3;

enum class GnmLoad : uint8_t { Load, Clear, DontCare };
enum class GnmStore : uint8_t { Store, MultisampleResolve };

struct GnmAttachment {
    __unsafe_unretained id<MTLTexture> tex = nil;
    __unsafe_unretained id<MTLTexture> resolve = nil; // GnmStore::MultisampleResolve
    uint32_t slice = 0;
    GnmLoad load = GnmLoad::Load;
    GnmStore store = GnmStore::Store;
};

// One render pass (MTLRenderPassDescriptor / MTL4RenderPassDescriptor).
struct GnmPassDesc {
    GnmAttachment color[8];
    MTLClearColor clear_color[8] = {}; // color[i].load == Clear
    GnmAttachment depth, stencil;
    double clear_depth = 0;            // depth.load == Clear (in [0, 1])
    uint32_t clear_stencil = 0;        // stencil.load == Clear
    uint32_t width = 0, height = 0;    // renderTargetWidth / renderTargetHeight; 0: not set
    uint32_t default_samples = 0;      // defaultRasterSampleCount; 0: not set
    uint32_t num_sample_positions = 0; // programmable sample positions; 0: the defaults
    MTLSamplePosition sample_positions[4] = {};
    __unsafe_unretained NSString* label = nil;
};

// Fixed-function state of a render encoder (the renderer's former EncoderState::Fixed). The
// renderer compares it with the state it set last and passes the changed fields (GnmFixedBits).
// A default-constructed state is all zero, so a comparison with it is always defined.
struct GnmFixedState {
    MTLViewport vp{};
    MTLScissorRect sc{};
    uint32_t stencil_ref[2]{}; // front, back
    uint32_t cull = 0, winding = 0, fill = 0, clip = 0; // MTLCullMode, MTLWinding, MTLTriangleFillMode, MTLDepthClipMode
    float bias[3]{};  // constant, slope, clamp
    float blend[4]{}; // blend color
};
enum GnmFixedBits : uint32_t {
    kGnmFixedStencilRef = 1u << 0,
    kGnmFixedViewport = 1u << 1,
    kGnmFixedScissor = 1u << 2,
    kGnmFixedCull = 1u << 3,
    kGnmFixedWinding = 1u << 4,
    kGnmFixedFill = 1u << 5,
    kGnmFixedClip = 1u << 6,
    kGnmFixedBias = 1u << 7,
    kGnmFixedBlend = 1u << 8,
};

enum class GnmDrawKind : uint8_t { Direct, Indexed, Indirect, IndexedIndirect };
struct GnmDraw {
    GnmDrawKind kind = GnmDrawKind::Direct;
    MTLPrimitiveType prim = MTLPrimitiveTypeTriangle;
    uint32_t vertex_start = 0;   // Direct
    uint32_t count = 0;          // vertices (Direct) or indices (Indexed)
    uint32_t instances = 1;      // Direct, Indexed
    uint32_t base_instance = 0;  // Direct, Indexed
    int32_t base_vertex = 0;     // Indexed
    MTLIndexType index_type = MTLIndexTypeUInt16;
    __unsafe_unretained id<MTLBuffer> index_buffer = nil; // Indexed, IndexedIndirect
    uint64_t index_offset = 0;
    uint64_t index_length = 0;   // bytes readable at index_offset (Metal 4 needs the length)
    __unsafe_unretained id<MTLBuffer> indirect_buffer = nil; // Indirect, IndexedIndirect
    uint64_t indirect_offset = 0;
};

// The end of one seq of the timeline: called once, on a Metal thread, after all its command
// buffers (upload and main, and Metal 4's split parts) completed or failed.
struct GnmCompletion {
    uint64_t seq = 0;
    bool failed = false;
    double gpu_start = 0, gpu_end = 0;        // seconds (MTLCommandBuffer GPUStartTime / GPUEndTime)
    __unsafe_unretained NSError* error = nil; // valid during the call
};
using GnmCompletionFn = void (*)(void* ctx, const GnmCompletion& c);

struct GnmBackendStats {
    uint64_t commits = 0;         // queue commits so far (all targets)
    uint64_t split_commits = 0;   // of those, made inside one seq for a queue-level event (Metal 4)
    uint64_t allocators = 0;      // command allocators alive (Metal 4): created - released
    uint64_t allocators_created = 0, allocators_released = 0; // since startup (the trim, Task 5.4)
    uint64_t allocator_resets = 0;
    uint64_t allocator_bytes = 0; // their allocatedSize, summed
    uint64_t resident = 0;        // allocations in the residency sets (Metal 4)
    uint64_t resident_textures = 0; // of those, in the Texture set (texture cache, presenter, renderer textures)
    uint64_t resident_bytes = 0;  // the sets' allocatedSize, summed
    uint64_t residency_asserts = 0; // Metal 4, PS4_GNM_RESIDENCY_CHECK=1: non-resident resources used
    uint64_t access_fences = 0;     // AccessFence fences made (updated at an encoder's end) so far
    uint64_t alias_waits = 0;       // WaitAccessFence waits encoded so far
};

// The defaults of every GnmBindOps entry: they abort, naming the op and the backend, so a backend
// that leaves an op unset fails at its first bind with a message instead of a call through null.
void GnmUnsetBindBuffer(GnmBackend*, GnmStage, uint32_t, __unsafe_unretained id<MTLBuffer>, uint64_t);
void GnmUnsetBindBufferOffset(GnmBackend*, GnmStage, uint32_t, uint64_t);
void GnmUnsetBindBytes(GnmBackend*, GnmStage, uint32_t, const void*, size_t);
void GnmUnsetBindTexture(GnmBackend*, GnmStage, uint32_t, __unsafe_unretained id<MTLTexture>);
void GnmUnsetBindSampler(GnmBackend*, GnmStage, uint32_t, __unsafe_unretained id<MTLSamplerState>);
void GnmUnsetBindUse(GnmBackend*, GnmStage, __unsafe_unretained id<MTLBuffer>, bool);

// The per-bind operations, called through this table: one indirect call per bind, no virtual
// dispatch (the renderer's dedupe has already dropped unchanged binds). A backend fills the table
// in Init; an entry it does not name keeps its aborting default (GnmUnsetBind*).
struct GnmBindOps {
    void (*buffer)(GnmBackend*, GnmStage, uint32_t slot, __unsafe_unretained id<MTLBuffer> b,
                   uint64_t off) = GnmUnsetBindBuffer;
    void (*buffer_offset)(GnmBackend*, GnmStage, uint32_t slot, uint64_t off) = GnmUnsetBindBufferOffset; // same buffer, new offset
    void (*bytes)(GnmBackend*, GnmStage, uint32_t slot, const void* p, size_t n) = GnmUnsetBindBytes;
    void (*texture)(GnmBackend*, GnmStage, uint32_t slot, __unsafe_unretained id<MTLTexture> t) = GnmUnsetBindTexture;
    void (*sampler)(GnmBackend*, GnmStage, uint32_t slot,
                    __unsafe_unretained id<MTLSamplerState> s) = GnmUnsetBindSampler;
    // a buffer reached through an argument buffer (gpuAddress tables): Metal 3 useResource
    void (*use)(GnmBackend*, GnmStage, __unsafe_unretained id<MTLBuffer> b, bool written) = GnmUnsetBindUse;
};

class GnmBackend {
public:
    virtual ~GnmBackend() = default;

    virtual GnmBackendKind Kind() const = 0;
    virtual uint32_t Caps() const = 0; // GnmCaps
    virtual bool Init(id<MTLDevice> dev, std::string* why) = 0;
    virtual id<MTLDevice> Device() const = 0;
    // A Metal 3 queue for work outside the timeline that the CPU waits for at once after a drain
    // (TextureCache readbacks and raw writes, capture). Metal 3: the timeline's queue itself.
    virtual id<MTLCommandQueue> SideQueue() const = 0;

    // ---- command buffers and the seq timeline
    // Opens target t's command buffer (it must not be open). encoder_status: Metal 3
    // MTLCommandBufferErrorOptionEncoderExecutionStatus (PS4_GNM_ENCODER_STATUS); Metal 4 has none.
    virtual void BeginCmd(GnmCmdTarget t, NSString* label, bool encoder_status) = 0;
    virtual bool CmdOpen(GnmCmdTarget t) const = 0;
    // The target that encoder, binding, work and event calls go to (Main unless switched).
    virtual void UseTarget(GnmCmdTarget t) = 0;
    virtual GnmCmdTarget Target() const = 0;
    // Commits the upload command buffer alone (no main one is open); it runs before the next seq.
    virtual void CommitUpload() = 0;
    // Commits the upload command buffer (if open), then the main one, as seq (the previous seq + 1).
    // done(ctx, …) runs once when everything of seq has completed. Encoders must be ended.
    virtual void Commit(uint64_t seq, GnmCompletionFn done, void* ctx) = 0;
    // The highest seq whose command buffers and every earlier one have completed (any thread).
    virtual uint64_t CompletedSeq() const = 0;
    // Before opening a main command buffer while `committed` seqs were committed: waits until fewer
    // than max_outstanding are uncompleted. False: none completed within timeout_ms (a GPU hang).
    virtual bool WaitForRoom(uint64_t committed, uint64_t max_outstanding, double timeout_ms) = 0;
    // Queue-level event operations in the current target's stream, after the work encoded so far
    // (signal) / before the work encoded from now on (wait). Metal 3 encodes them into the command
    // buffer; Metal 4 has them only between command buffers and splits the seq there.
    virtual void QueueSignal(id<MTLSharedEvent> ev, uint64_t value) = 0;
    virtual void QueueWait(id<MTLSharedEvent> ev, uint64_t value) = 0;

    // ---- encoders (on the current target)
    // These two queries are asked several times per draw and dispatch: they read state kept here,
    // which the backend updates (NoteEncoder*), and cost no virtual call.
    GnmEncoder OpenEncoder() const { return enc_[(int)enc_target_].open; }
    // Per target: the current target's serial, which changes whenever an encoder is opened on that
    // target. Encoders opened on Upload (texture uploads, alias copies) never change Main's serial.
    // The renderer's dedupe is valid only while Main's serial holds.
    uint64_t EncoderSerial() const { return enc_[(int)enc_target_].serial; }
    virtual bool BeginRender(const GnmPassDesc& d) = 0;
    virtual void BeginCompute(bool serial) = 0; // serial: dispatches in order (PS4_GNM_SERIAL_COMPUTE, uploads)
    virtual void BeginCopies() = 0;
    virtual void EndEncoder() = 0;               // the open encoder, if any
    virtual void SetEncoderLabel(NSString* label) = 0;
    // Orders work inside the open encoder: `before` stages after `after` stages (GnmStageBits);
    // scope: GnmScopeBits (Metal 3). Nothing when no encoder is open (encoder boundaries are
    // ordered by the backend: Metal 3 tracking, Metal 4 a barrier at each encoder start).
    virtual void Barrier(uint32_t after, uint32_t before, uint8_t scope) = 0;
    // Guest windows that alias (docs/gnm-ordering.md, "Aliasing guest windows"): no-copy buffers over
    // the same guest memory, which object-based ordering does not order; the renderer orders the
    // accesses through different windows itself. AccessFence: the id of a fence the open encoder
    // updates at its end, after all its work (one per encoder, made at the first call; ids grow
    // by one per fence). WaitAccessFence: the open encoder's later commands wait for the encoder
    // that has fence `id` (nothing for 0, a fence whose seq completed, the open encoder's own, or
    // one it waited for already). Every backend states how it orders these accesses: a backend
    // that orders every encoder after all earlier ones (Metal 4's encoder-start barriers) returns
    // 0 and waits for nothing, and says why.
    virtual uint64_t AccessFence() = 0;
    virtual void WaitAccessFence(uint64_t id) = 0;
    // Called by every command (draw, dispatch, copy, fill) before it is encoded, with its encoder
    // open: the renderer orders the guest windows the command uses there (AccessFence /
    // WaitAccessFence). Encoder thread.
    using GnmBeforeCommandFn = void (*)(void* ctx);
    void SetBeforeCommand(GnmBeforeCommandFn fn, void* ctx) {
        before_cmd_ = fn;
        before_ctx_ = ctx;
    }

    // ---- pipeline and fixed-function state (on the open encoder)
    virtual void SetRenderPipeline(id<MTLRenderPipelineState> p) = 0;
    virtual void SetDepthStencil(id<MTLDepthStencilState> d) = 0;
    virtual void SetFixed(const GnmFixedState& f, uint32_t changed) = 0; // changed: GnmFixedBits
    virtual void SetComputePipeline(id<MTLComputePipelineState> p) = 0;

    // ---- binding (on the open encoder of the stage)
    void BindBuffer(GnmStage st, uint32_t slot, __unsafe_unretained id<MTLBuffer> b, uint64_t off) {
        ops_.buffer(this, st, slot, b, off);
    }
    void BindBufferOffset(GnmStage st, uint32_t slot, uint64_t off) { ops_.buffer_offset(this, st, slot, off); }
    void BindBytes(GnmStage st, uint32_t slot, const void* p, size_t n) { ops_.bytes(this, st, slot, p, n); }
    void BindTexture(GnmStage st, uint32_t slot, __unsafe_unretained id<MTLTexture> t) { ops_.texture(this, st, slot, t); }
    void BindSampler(GnmStage st, uint32_t slot, __unsafe_unretained id<MTLSamplerState> s) {
        ops_.sampler(this, st, slot, s);
    }
    void UseResource(GnmStage st, __unsafe_unretained id<MTLBuffer> b, bool written) { ops_.use(this, st, b, written); }

    // ---- work
    virtual void Draw(const GnmDraw& d) = 0;
    virtual void Dispatch(MTLSize groups, MTLSize threads_per_group) = 0;
    virtual void DispatchThreads(MTLSize threads, MTLSize threads_per_group) = 0;
    virtual void DispatchIndirect(id<MTLBuffer> args, uint64_t off, MTLSize threads_per_group) = 0;
    // copies and fills (inside BeginCopies … EndEncoder)
    virtual void CopyBuffer(id<MTLBuffer> src, uint64_t src_off, id<MTLBuffer> dst, uint64_t dst_off, uint64_t n) = 0;
    virtual void FillBuffer(id<MTLBuffer> dst, uint64_t off, uint64_t n, uint8_t value) = 0;
    virtual void CopyTexture(id<MTLTexture> src, id<MTLTexture> dst) = 0;
    virtual void CopyTextureRegion(id<MTLTexture> src, uint32_t src_slice, uint32_t src_level, MTLOrigin src_origin,
                                   MTLSize size, id<MTLTexture> dst, uint32_t dst_slice, uint32_t dst_level,
                                   MTLOrigin dst_origin) = 0;
    virtual void CopyBufferToTexture(id<MTLBuffer> src, uint64_t src_off, uint64_t row_bytes, uint64_t image_bytes,
                                     MTLSize size, id<MTLTexture> dst, uint32_t slice, uint32_t level,
                                     MTLOrigin origin) = 0;

    // ---- residency (thread-safe). Metal 3: bound resources are resident by themselves, these do
    // nothing and IsResident is true. Metal 4: GnmResidency.
    virtual void MakeResident(id<MTLAllocation> a, GnmResidencyClass c) = 0;
    // a leaves its set once every seq up to after_seq has completed (the renderer passes the seq
    // of the open command buffer: committed + 1)
    virtual void Evict(id<MTLAllocation> a, uint64_t after_seq) = 0;
    virtual bool IsResident(id<MTLAllocation> a) const = 0;

    virtual GnmBackendStats Stats() const = 0; // any thread
    // The renderer's frame ended (its present; the holder of the encoder's state). Metal 4 predicts
    // the kind of each command buffer part from the part at the same position of the previous
    // frame (the allocator trim); Metal 3 does nothing.
    virtual void NoteFrameEnd() {}

protected:
    GnmBindOps ops_{};
    void BeforeCommand() {
        if (before_cmd_) before_cmd_(before_ctx_);
    }
    GnmBeforeCommandFn before_cmd_ = nullptr;
    void* before_ctx_ = nullptr;

    // The backend's bookkeeping behind OpenEncoder / EncoderSerial: the target its encoder calls go
    // to (UseTarget), an encoder opened there (e: what it is, None if its creation failed; the
    // serial changes either way), and its end. Encoder thread only (unsynchronised, like every
    // encoder call): no other thread may read OpenEncoder / EncoderSerial.
    void NoteEncoderTarget(GnmCmdTarget t) { enc_target_ = t; }
    void NoteEncoderOpened(GnmEncoder e) {
        EncoderTrack& x = enc_[(int)enc_target_];
        x.open = e;
        ++x.serial;
    }
    void NoteEncoderEnded() { enc_[(int)enc_target_].open = GnmEncoder::None; }

private:
    struct EncoderTrack {
        GnmEncoder open = GnmEncoder::None;
        uint64_t serial = 0;
    };
    EncoderTrack enc_[2]{}; // per GnmCmdTarget
    GnmCmdTarget enc_target_ = GnmCmdTarget::Main;
};

// Compute work of the guest memory protocol: the backend's open compute encoder (raw == nil), or
// a Metal 3 compute encoder of the guest memory re-run queue (queue2 stays on Metal 3).
struct GnmComputeSink {
    GnmBackend* be = nullptr;
    __unsafe_unretained id<MTLComputeCommandEncoder> raw = nil;

    // Parameters are unretained, as on the bind path: the caller keeps the objects alive.
    void Pipeline(__unsafe_unretained id<MTLComputePipelineState> p) const {
        if (raw) [raw setComputePipelineState:p];
        else be->SetComputePipeline(p);
    }
    void Buffer(uint32_t slot, __unsafe_unretained id<MTLBuffer> b, uint64_t off) const {
        if (raw) [raw setBuffer:b offset:off atIndex:slot];
        else be->BindBuffer(GnmStage::Compute, slot, b, off);
    }
    void Bytes(uint32_t slot, const void* p, size_t n) const {
        if (raw) [raw setBytes:p length:n atIndex:slot];
        else be->BindBytes(GnmStage::Compute, slot, p, n);
    }
    void Dispatch(MTLSize groups, MTLSize tpt) const {
        if (raw) [raw dispatchThreadgroups:groups threadsPerThreadgroup:tpt];
        else be->Dispatch(groups, tpt);
    }
    void DispatchIndirect(__unsafe_unretained id<MTLBuffer> args, uint64_t off, MTLSize tpt) const {
        if (raw) [raw dispatchThreadgroupsWithIndirectBuffer:args indirectBufferOffset:off threadsPerThreadgroup:tpt];
        else be->DispatchIndirect(args, off, tpt);
    }
    // the guest memory protocol's ordering between probe, gate, pass and harvest
    void Barrier() const {
        if (raw) [raw memoryBarrierWithScope:MTLBarrierScopeBuffers];
        else be->Barrier(kGnmStageDispatch, kGnmStageDispatch, kGnmScopeBuffers);
    }
};

// ---- selection (ps4draw::Init)
struct GnmBackendChoice {
    bool ok = false;
    GnmBackendKind kind = GnmBackendKind::Metal3;
    std::string why; // !ok: the startup error; ok: how the choice was made (logged)
};
// env: PS4_GNM_METAL (nullptr or "": the default). device_has_metal4: supportsFamily:
// MTLGPUFamilyMetal4. metal4_caps: what this build's Metal 4 backend implements (0: none).
// only_clears: PS4_GNM_MTL4_ONLY_CLEARS=1 (draws skipped; dispatches too without kGnmCapCompute).
// prefer_metal4: the default is 4 when the device and the backend allow it (DefaultPrefersMetal4()).
// An explicit "4" that cannot be honoured is an error, never a silent fallback: it needs every
// capability (kGnmCapAll), or kGnmCapClears with only_clears. only_clears on a Metal 3 choice has
// no effect, and why says so.
GnmBackendChoice ChooseBackend(const char* env, bool device_has_metal4, uint32_t metal4_caps, bool only_clears,
                               bool prefer_metal4);
// The prefer_metal4 that ps4draw::Init passes: whether the backend used when PS4_GNM_METAL is not
// set is Metal 4 (where the device and the backend allow it).
bool DefaultPrefersMetal4();
const char* BackendName(GnmBackendKind k); // "metal3" / "metal4"
// What this build's Metal 4 backend implements (ps4_gnm_backend_metal4.mm).
extern const uint32_t kMetal4BackendCaps;

std::unique_ptr<GnmBackend> MakeMetal3Backend(); // ps4_gnm_backend_metal3.mm (Task 1.2)
std::unique_ptr<GnmBackend> MakeMetal4Backend(); // ps4_gnm_backend_metal4.mm

} // namespace ps4gnm

#endif // PS4_GNM_BACKEND_H
