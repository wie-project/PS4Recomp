// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - the renderer's utility shaders (present conversion, depth and stencil copies,
// depth companions, EQAA copies, GPU texture detile) and the full-screen pass every helper pass
// is drawn with, on any GnmBackend.
#ifndef PS4_GNM_UTIL_PASSES_H
#define PS4_GNM_UTIL_PASSES_H

#include <string>

#include "ps4_gnm_backend.h"

namespace ps4gnm {

extern const char* const kGnmUtilMsl; // the MSL (the renderer's former kPresent)

struct GnmUtil {
    id<MTLLibrary> lib = nil;
    id<MTLRenderPipelineState> present = nil;     // present_vs + present_fs, BGRA8Unorm target
    id<MTLRenderPipelineState> depth2color = nil; // present_vs + depth2color_fs, R32Float target
    id<MTLComputePipelineState> detile = nil;     // detile_gather
    id<MTLComputePipelineState> quad_prep = nil;    // quad_list_prep (indirect quad-list draws)
    id<MTLComputePipelineState> quad_indices = nil; // quad_list_indices (their indexed conversion)
    id<MTLComputePipelineState> args_patch = nil;   // indirect_args_patch (record offsets)
};
// Compiles kGnmUtilMsl and builds the three pipelines. False (why set) only when the library does
// not compile; a pipeline that fails stays nil and why says which.
bool MakeGnmUtil(id<MTLDevice> dev, GnmUtil* out, std::string* why);

// VideoOut reads encoded display bytes, independent of a shader's storage-image type.
id<MTLTexture> PresentTextureView(id<MTLTexture> src, uint32_t pixel_format);

// A full-screen triangle (present_vs) into the pass `pd`, with `pso`, `dss` (nil: none set) and
// `src` at fragment texture 0. The caller opened the command buffer and ended the open encoder; the
// pass is ended on return. False: no render encoder (nothing encoded).
bool FullscreenPass(GnmBackend& be, const GnmPassDesc& pd, id<MTLRenderPipelineState> pso,
                    id<MTLDepthStencilState> dss, id<MTLTexture> src);

} // namespace ps4gnm

#endif // PS4_GNM_UTIL_PASSES_H
