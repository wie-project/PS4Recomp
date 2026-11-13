// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - the attachments of a render pass that its draws write, for the pass hazard rule of
// ps4_gnm_draw.mm (BeginPass, PassKey::CanSubsume).
//
// A draw may sample a texture the open render pass renders into: soft particles and decals read
// the depth buffer they are depth-tested against, with depth writes off. Inside a Metal render
// pass such a read sees the attachment's memory as it was when the pass began; what draws of the
// pass wrote is still in tile memory until the pass stores it. So the draw continues the open
// pass unless a draw of that pass (or its load action, a clear) wrote an attachment the draw
// samples; then the pass ends first and the next one begins with what it stored. A new pass does
// not change what a draw that writes the attachment it samples reads, so the rule looks only at
// the writes before the draw.
//
// Plain C++17, no Metal (tested by pkg/hosttest).
#ifndef PS4_GNM_PASS_WRITES_H
#define PS4_GNM_PASS_WRITES_H

#include <stdint.h>

#include "ps4_gnm_resources.h"

namespace ps4gnm {

// Attachment bits of a render pass: color slot i is kPassColor0 << i; depth and stencil are one
// Metal texture, kPassDepth.
constexpr uint32_t kPassColor0 = 1u;
constexpr uint32_t kPassDepth = 1u << 8;
constexpr uint32_t kPassAllAttachments = 0x1FFu;

// A stencil face writes when its write mask is not 0 and one of its operations is not Keep
// (MTLStencilOperationKeep is 0).
inline bool StencilFaceWrites(const StencilFace& f) {
    return f.write_mask != 0 && (f.fail != 0 || f.depth_fail != 0 || f.pass != 0);
}

// True if a draw with this state writes its depth/stencil target: depth writes (DepthStencilState
// depth_write, as the Metal depth-stencil state gets it), a stencil operation that writes, or a DB
// clear. A stencil operation Metal cannot express counts as a write.
inline bool WritesDepthStencil(const RenderState& rs) {
    const DepthStencilState& ds = rs.ds;
    if (ds.depth_write || rs.depth.depth_clear_enable || rs.depth.stencil_clear_enable) return true;
    return ds.stencil_test &&
           (ds.unsupported_stencil_op || StencilFaceWrites(ds.front) || StencilFaceWrites(ds.back));
}

// The attachments of its pass a draw writes. bound: the pass attachments the draw is encoded with
// (kPassColor0 << i, kPassDepth); write_mask: the draw's MTLColorWriteMask per color slot.
inline uint32_t DrawPassWrites(uint32_t bound, const uint32_t write_mask[8], const RenderState& rs) {
    uint32_t w = 0;
    for (int i = 0; i < 8; ++i)
        if ((bound & (kPassColor0 << i)) && write_mask[i]) w |= kPassColor0 << i;
    if ((bound & kPassDepth) && WritesDepthStencil(rs)) w |= kPassDepth;
    return w;
}

} // namespace ps4gnm

#endif
