// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Geometry-shader draws on Metal: the geometry shader becomes a compute kernel with one thread
// per input primitive, reading its input vertices from the buffer written by the export shader's
// kernel and writing vertex records and a strip index list.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "common/types.h"

namespace GcnSpirv {

/// Descriptor set of the buffers the rewrite adds (the recompiler uses set 0 only) and the
/// bindings in it; binding b is MSL buffer GCN_GEO_ES_BUFFER_SLOT + b.
constexpr u32 kGeometryKernelSet = 7;
constexpr u32 kGeometryBindingEs = 0;      ///< the export shader's outputs (read)
constexpr u32 kGeometryBindingRecords = 1; ///< vertex records (written)
constexpr u32 kGeometryBindingIndices = 2; ///< strip indices (written)
constexpr u32 kGeometryBindingParams = 3;  ///< uniform {prim_count, first_prim}
constexpr u32 kGeometryThreads = 64;       ///< LocalSize x

enum class ScalarType : u8 { Float, Int, Uint, Other };

enum class GsInputPrimitive : u8 { Points, Lines, Triangles };
enum class GsOutputPrimitive : u8 { TriangleStrip, LineStrip };

/// One member of the export shader kernel's output struct (SPIRV-Cross' `main0_out`).
struct InterfaceMember {
    u32 location{~0u}; ///< ~0u: a builtin
    bool position{};   ///< the Position builtin
    u32 offset{};      ///< bytes from the start of the struct (MSL layout)
    u32 components{};
    ScalarType type{ScalarType::Float};
    bool operator==(const InterfaceMember&) const = default;
};

/// What the geometry kernel needs to know about the export shader's output buffer.
struct GeometryEsLayout {
    u32 stride{}; ///< bytes of one element (one export vertex)
    std::vector<InterfaceMember> members;
    bool operator==(const GeometryEsLayout&) const = default;
};

/// One output location the geometry shader writes: record slot `1 + index` (slot 0 is the
/// position), each slot 16 bytes of raw 32-bit words.
struct GeometryVarying {
    u32 location{};
    u32 components{};
    ScalarType type{ScalarType::Float};
    bool operator==(const GeometryVarying&) const = default;
};

/// The shape of a geometry shader and of the records its kernel writes.
struct GeometryInfo {
    GsInputPrimitive input{GsInputPrimitive::Points};
    u32 input_vertices{1};
    GsOutputPrimitive output{GsOutputPrimitive::TriangleStrip};
    u32 max_vertices{};
    std::vector<GeometryVarying> varyings; ///< sorted by location
    /// Bytes of one vertex record: 16 (position) + 16 per varying.
    u32 RecordStride() const {
        return 16 + 16 * static_cast<u32>(varyings.size());
    }
    /// Index slots per primitive (a vertex or a restart each).
    u32 IndexSlots() const {
        return 2 * max_vertices;
    }
};

/// Result of AnalyzeGeometryShader / RewriteGeometryShader.
struct GeometryRewrite {
    bool ok{};              ///< false: `error` says why (an unsupported shape, not a bug)
    std::vector<u32> spirv; ///< the compute kernel (RewriteGeometryShader only)
    std::string error;
    GeometryInfo info;
};

/// One location the fragment shader reads (for the geometry pass-through vertex function).
struct StageInput {
    u32 location{};
    ScalarType type{ScalarType::Float};
    u32 components{};
    bool operator==(const StageInput&) const = default;
};

/// Checks the geometry shader's shape without rewriting: input points / lines / triangles,
/// output triangle strip / line strip, Invocations 1, OutputVertices > 0, no streams, a plain
/// Position output, outputs and inputs that are 32-bit scalars or vectors.
GeometryRewrite AnalyzeGeometryShader(std::span<const u32> spirv);

/// The geometry shader as a GLCompute module (LocalSize 64, 1, 1), one thread per primitive:
///   - threads with `thread >= prim_count` return at once; gl_PrimitiveID = first_prim + thread;
///   - every Input variable becomes a Private one filled at the start from the export outputs
///     (thread * input_vertices + v, `es` gives the byte offset of each location and of the
///     Position; locations the export shader does not write read as zero, gl_in's PointSize and
///     ClipDistance are zero);
///   - every Output variable becomes Private; OpEmitVertex stores the position and each varying
///     as raw words into record `thread * max_vertices + n` and appends the record index to the
///     primitive's 2 * max_vertices index slots, OpEndPrimitive appends 0xFFFFFFFF; at each
///     return of the entry function the remaining slots are filled with 0xFFFFFFFF (Metal
///     restarts a strip at that index, so the draw is one indexed strip draw over all slots).
/// Buffers: set kGeometryKernelSet, bindings kGeometryBinding*.
GeometryRewrite RewriteGeometryShader(std::span<const u32> spirv, const GeometryEsLayout& es);

/// MSL source of the vertex function that reads record `[[vertex_id]]` from `[[buffer(0)]]` and
/// returns the position and each location in `fs_inputs` (empty on an unsupported input type).
/// Both `main0` and `bb_gs_pass` entry points are emitted in the returned library source.
std::string PassThroughSource(const GeometryInfo& info, std::span<const StageInput> fs_inputs,
                              bool clip_minus_w_to_w, std::string& error);

/// Overload that derives `fs_inputs` directly from `info.varyings` (all GS outputs exported).
std::string PassThroughSource(const GeometryInfo& info, bool clip_minus_w_to_w,
                              std::string& error);

} // namespace GcnSpirv
