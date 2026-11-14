// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PS4_GNM_DEPTH_COMPANION_H
#define PS4_GNM_DEPTH_COMPANION_H

#import <Metal/Metal.h>
#include <stdint.h>

namespace ps4gnm {

// A current companion is a separate allocation, not the guest depth surface it
// represents. Texture views of that companion must still count as attachments.
inline bool IndependentDepthCompanion(id<MTLTexture> companion, uint64_t version, uint64_t source_version,
                                      id<MTLTexture> depth,
                                      __unsafe_unretained id<MTLTexture> const (&color)[8]) {
    if (!companion || version != source_version) return false;
    while (companion.parentTexture) companion = companion.parentTexture;
    auto aliases = [&](id<MTLTexture> attachment) {
        if (!attachment) return false;
        while (attachment.parentTexture) attachment = attachment.parentTexture;
        return companion == attachment;
    };
    if (aliases(depth)) return false;
    for (int i = 0; i < 8; ++i)
        if (aliases(color[i])) return false;
    return true;
}

} // namespace ps4gnm

#endif
