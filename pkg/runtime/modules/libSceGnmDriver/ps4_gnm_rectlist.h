// RectList draws on Metal (GCN primitive type 17: three vertices per rectangle, the GPU makes
// the fourth). Metal has no such primitive and no stage between the vertex and the fragment
// shader, so a RectList draw is encoded as two draws in the open render pass (see
// gcn_shader_rectlist_msl in gcn_recompiler.h for the two vertex shaders):
//  1. the guest vertex shader's capture variant (rasterization disabled) runs the draw as given
//     and stores every vertex's outputs in a buffer;
//  2. after a vertex-to-vertex memory barrier, the expansion vertex shader emits two triangles per
//     rectangle from those vertices, with the guest fragment shader and the draw's state.
// Shared by the renderer (ps4_gnm_draw.mm) and its host test (tests/host/gnm_rectlist_test.mm).
#pragma once

#import <Metal/Metal.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#include "ps4_gnm_backend.h"

namespace ps4gnm_rect {

// Capture index of every vertex of a RectList draw of `count` vertices (3 per rectangle):
// the index buffer values of an indexed draw (indices != nullptr; 32-bit if i32),
// vertex_offset..vertex_offset+count-1 otherwise (since Metal non-indexed draws have
// gl_BaseVertex == 0, the capture vertex shader writes to vertex_offset + i).
// Returns the number of capture slots per instance (largest index + 1), or 0 if
// count is not a positive multiple of 3.
inline uint32_t BuildSlots(const void* indices, bool i32, uint32_t count, uint32_t* slots, uint32_t vertex_offset = 0) {
    if (count == 0 || count % 3) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t v = !indices ? (vertex_offset + i) : i32 ? static_cast<const uint32_t*>(indices)[i]
                                                               : static_cast<const uint16_t*>(indices)[i];
        slots[i] = v;
        if (v >= n) n = v + 1;
    }
    return n;
}

// The guest draw as Metal parameters.
struct Geometry {
    uint32_t count = 0;         // vertices (indices of an indexed draw), 3 per rectangle
    uint32_t instances = 1;
    uint32_t base_instance = 0;
    uint32_t vertex_start = 0;  // non-indexed draws
    int32_t base_vertex = 0;    // indexed draws
    id<MTLBuffer> index_buffer = nil; // nil: non-indexed
    NSUInteger index_offset = 0;
    uint64_t index_length = 0; // bytes readable at index_offset (Metal 4 needs it)
    MTLIndexType index_type = MTLIndexTypeUInt16;
};

// Buffers of the two draws.
struct Buffers {
    id<MTLBuffer> vertices; // per_instance * instances * vertex_stride bytes, written by draw 1
    NSUInteger vertices_off;
    id<MTLBuffer> slots;    // uint32 per vertex of the draw (BuildSlots)
    NSUInteger slots_off;
    id<MTLBuffer> params;   // uint32 per_instance (BuildSlots' result)
    NSUInteger params_off;
    uint32_t capture_slot, params_slot; // gcn_rectlist_msl capture_buffer / params_buffer
};

// Encodes both draws on the backend's open render encoder. Before the call the caller has bound
// everything the guest vertex shader reads on the vertex stage (resources and vertex buffers) and
// the fragment shader's resources, and set the draw's fixed-function state. Afterwards the
// encoder's pipeline is `expand` and vertex buffer slots 0, 1, 2, capture_slot and params_slot
// hold `b`.
inline void Encode(ps4gnm::GnmBackend& be, id<MTLRenderPipelineState> capture, id<MTLRenderPipelineState> expand,
                   const Geometry& g, const Buffers& b) {
    using namespace ps4gnm;
    be.SetRenderPipeline(capture);
    be.BindBuffer(GnmStage::Vertex, b.capture_slot, b.vertices, b.vertices_off);
    be.BindBuffer(GnmStage::Vertex, b.params_slot, b.params, b.params_off);
    GnmDraw d;
    d.prim = MTLPrimitiveTypePoint;
    d.count = g.count;
    d.instances = g.instances;
    d.base_instance = g.base_instance;
    if (g.index_buffer) {
        d.kind = GnmDrawKind::Indexed;
        d.index_type = g.index_type;
        d.index_buffer = g.index_buffer;
        d.index_offset = g.index_offset;
        d.index_length = g.index_length;
        d.base_vertex = g.base_vertex;
    } else {
        d.vertex_start = g.vertex_start;
    }
    be.Draw(d);
    // The expansion's vertex shader reads what the capture's vertex shader wrote.
    be.Barrier(kGnmStageVertex, kGnmStageVertex, kGnmScopeBuffers);
    be.SetRenderPipeline(expand);
    be.BindBuffer(GnmStage::Vertex, 0, b.vertices, b.vertices_off);
    be.BindBuffer(GnmStage::Vertex, 1, b.slots, b.slots_off);
    be.BindBuffer(GnmStage::Vertex, 2, b.params, b.params_off);
    GnmDraw e;
    e.count = g.count / 3 * 6;
    e.instances = g.instances;
    e.base_instance = g.base_instance;
    be.Draw(e);
}

// The Metal libraries of a RectList draw's two vertex shaders (capture and expansion), compiled in
// the background by CompileLibrary: how many are still compiling, and whether Metal rejected one.
struct LibraryStatus {
    std::atomic<int> compiling{2};
    std::atomic<bool> failed{false};
};

// The edits the renderer makes to every translated shader's MSL (FinishShader): no optnone
// helpers, and for the expansion shader (the one that is rasterized) an invariant position.
inline std::string PrepareMsl(const char* src, bool expand) {
    std::string msl = src;
    const std::string tag = "[[clang::optnone]] ";
    for (size_t pos; (pos = msl.find(tag)) != std::string::npos;) msl.erase(pos, tag.size());
    if (expand) {
        const std::string pos = "[[position]]", inv = "[[position, invariant]]";
        for (size_t at = 0; (at = msl.find(pos, at)) != std::string::npos; at += inv.size())
            msl.replace(at, pos.size(), inv);
    }
    return msl;
}

// The end of a RectList library compile (CompileLibrary, CompileLibraryNow): *fn holds the
// library's main0 (nil if Metal rejected it: logged and status->failed set), *pending is false,
// and then status->compiling drops by one (release: who reads it at 0 sees *fn).
inline void LibraryDone(id<MTLLibrary> lib, NSError* e, __strong id<MTLFunction>* fn, bool* pending,
                        LibraryStatus* status, uint64_t code_addr, bool expand) {
    *fn = lib ? [lib newFunctionWithName:@"main0"] : nil;
    if (!*fn) {
        std::fprintf(stderr, "[gnm-draw] VS 0x%llx: Metal rejected its RectList %s shader: %s\n",
                     (unsigned long long)code_addr, expand ? "expansion" : "capture",
                     e ? e.localizedDescription.UTF8String : "no main0");
        status->failed.store(true, std::memory_order_relaxed);
    }
    *pending = false;
    status->compiling.fetch_sub(1, std::memory_order_release);
}

inline MTLCompileOptions* LibraryOptions() {
    MTLCompileOptions* opt = [MTLCompileOptions new];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    opt.fastMathEnabled = NO;
#pragma clang diagnostic pop
    if (@available(macOS 11.0, *)) opt.preserveInvariance = YES;
    return opt;
}

// Starts compiling `msl` (the capture or, if `expand`, the expansion shader of the guest vertex
// shader at code_addr) and returns at once. When Metal is done, on one of its threads, the
// library is finished as LibraryDone describes.
//
// The completion runs after this function and its caller have returned, so the block captures
// only the values passed here, by value: fn, pending and status must point into objects that live
// until the compile is done (the caller's heap objects), never into a stack frame.
inline void CompileLibrary(id<MTLDevice> dev, const std::string& msl, __strong id<MTLFunction>* fn, bool* pending,
                           LibraryStatus* status, uint64_t code_addr, bool expand) {
    [dev newLibraryWithSource:[NSString stringWithUTF8String:msl.c_str()]
                      options:LibraryOptions()
            completionHandler:^(id<MTLLibrary> lib, NSError* e) {
                LibraryDone(lib, e, fn, pending, status, code_addr, expand);
            }];
}

// The same compile on the calling thread (PS4_GNM_SYNC_COMPILE=1, which builds everything
// inline): the library is finished as LibraryDone describes when this returns.
inline void CompileLibraryNow(id<MTLDevice> dev, const std::string& msl, __strong id<MTLFunction>* fn, bool* pending,
                              LibraryStatus* status, uint64_t code_addr, bool expand) {
    NSError* e = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:[NSString stringWithUTF8String:msl.c_str()]
                                           options:LibraryOptions()
                                             error:&e];
    LibraryDone(lib, e, fn, pending, status, code_addr, expand);
}

} // namespace ps4gnm_rect
