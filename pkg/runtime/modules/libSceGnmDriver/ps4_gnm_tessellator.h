// SPDX-License-Identifier: GPL-2.0-or-later AND MIT
//
// Compute tessellator: the D3D11 fixed-function tessellator (which GCN implements) as Metal
// compute kernels, for backends without hardware tessellation (Metal 4 has none). The kernels
// are a port of Microsoft's D3D11 tessellator reference (CHWTessellator, see the MIT notice in
// ps4_gnm_tessellator.mm).
//
// Per tessellated draw (one HS instance), after the HS wrote its factors:
//   1. ps4_tess_count   one thread per patch: domain point and index count       -> counts
//   2. ps4_tess_offsets one thread: per-patch bases (patches past the capacity are dropped and
//                     reported), the indexed indirect draw arguments            -> offsets, result,
//                     and the needs (atomic max)                                -> feedback
//   3. ps4_tess_emit    one thread per patch: domain points and indices           -> points, indices
// with a dispatch barrier between them, then an indexed indirect draw of `result.draw` over
// `indices` (uint32) with the pull variant of the domain shader, which reads
// points[vertex_id] = (u, v, w, as_type<float>(patch)) (quads: w = 0).
#ifndef PS4_GNM_TESSELLATOR_H
#define PS4_GNM_TESSELLATOR_H

#import <Metal/Metal.h>

#include <cstdint>
#include <string>

namespace ps4gnm_tess {

// Buffer slots of the kernels.
enum : uint32_t {
    kParamsSlot = 0,  // Params (bytes)
    kFactorsSlot = 1, // the HS's MTLTriangleTessellationFactorsHalf / MTLQuadTessellationFactorsHalf per patch
    kCountsSlot = 2,  // uint2 per patch: points, indices
    kOffsetsSlot = 3, // uint2 per patch: point base, index base (0xffffffff: dropped)
    kResultSlot = 4,  // Result
    kPointsSlot = 5,  // float4 per domain point
    kIndicesSlot = 6, // uint32 per index (point indices, the draw's base already added)
    kFeedbackSlot = 7, // Feedback, kept across draws: the largest needs seen (atomic max)
};

enum Domain : uint32_t { kTriangle = 0, kQuad = 1 };
// D3D11_TESSELLATOR_PARTITIONING
enum Partitioning : uint32_t { kInteger = 0, kPow2 = 1, kFractionalOdd = 2, kFractionalEven = 3 };
// D3D11_TESSELLATOR_OUTPUT_PRIMITIVE (lines only exist for isolines, which Metal cannot run)
enum Output : uint32_t { kPoints = 0, kTrianglesCw = 2, kTrianglesCcw = 3 };

// The most points / indices one patch can produce (D3D11 MAX_POINT_COUNT / MAX_INDEX_COUNT).
constexpr uint32_t kMaxPointsPerPatch = 65 * 65;
constexpr uint32_t kMaxIndicesPerPatch = 64 * 64 * 2 * 3;

struct Params {
    uint32_t domain;         // Domain
    uint32_t partitioning;   // Partitioning
    uint32_t output;         // Output
    uint32_t patch_count;
    uint32_t point_capacity; // float4s at kPointsSlot
    uint32_t index_capacity; // uint32s at kIndicesSlot
    uint32_t base_instance;  // the draw's base instance
    uint32_t pad;
};

struct Result {
    uint32_t draw[5];        // MTLDrawIndexedPrimitivesIndirectArguments
    uint32_t dropped;        // patches that did not fit the capacities
    uint32_t needed_points;  // what all patches need
    uint32_t needed_indices;
};

// What the draws of one pipeline needed at most: the caller grows its capacities from it.
struct Feedback {
    uint32_t max_points;
    uint32_t max_indices;
};

struct Kernels {
    id<MTLComputePipelineState> count = nil;
    id<MTLComputePipelineState> offsets = nil;
    id<MTLComputePipelineState> emit = nil;
};

// The kernels' MSL (for tests and diagnostics).
const char* KernelSource();
// Compiles the kernels (IEEE float math: the reference's clamps rely on NaN semantics).
bool MakeKernels(id<MTLDevice> device, Kernels* out, std::string* why);

} // namespace ps4gnm_tess

#endif
