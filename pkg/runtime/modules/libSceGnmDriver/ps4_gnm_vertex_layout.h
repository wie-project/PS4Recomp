// Metal vertex buffer layout of one GCN vertex input (ps4_gnm_draw.mm).
#pragma once

#import <Metal/Metal.h>

#include <cstdint>

namespace ps4gnm_vertex {

// Fills the layout of the Metal vertex buffer that feeds one vertex input.
// A V# with stride 0 gives every vertex the same element; the renderer maps it to the constant
// step function. Metal rejects a layout without a stride even then (pipeline validation and
// binary archive serialization assert "references a buffer ... that has no stride"), and a
// constant layout reads the element at offset 0 for every vertex whatever its stride, so such a
// layout gets the smallest valid stride, 4.
inline void SetLayout(MTLVertexBufferLayoutDescriptor* l, uint32_t stride, MTLVertexStepFunction fn,
                      uint32_t rate) {
    l.stride = (stride == 0 && fn == MTLVertexStepFunctionConstant) ? 4 : stride;
    l.stepFunction = fn;
    l.stepRate = rate;
}

} // namespace ps4gnm_vertex
