// PM4 INDIRECT_BUFFER nesting for the command processor walkers in ps4_gnm_metal.mm.
#pragma once
#include <stdint.h>
#include <vector>

namespace ps4gnm_ib {

// A command buffer being walked: its words, size in dwords and the next packet.
struct Frame {
    const uint32_t *words;
    uint32_t dw;
    uint32_t pc;
};

// Limit for IB packets that call a sub-buffer and return (IB1 -> IB2 on GCN). It only guards
// against cycles: frames on the stack, the top-level buffer included, are at most
// kMaxDepth + 1.
constexpr int kMaxDepth = 4;

// The CHAIN bit of an INDIRECT_BUFFER packet (dw2 bit 20; field layout as in shadPS4
// src/video_core/amdgpu/pm4_cmds.h PM4CmdIndirectBuffer). A chained IB does not nest: the CP
// continues in the new buffer and never returns to the one holding the packet. Gnm builds
// large command buffers from chunks chained this way (Uncharted 4 chains dozens per frame).
inline bool IsChained(const uint32_t *payload, uint32_t payload_dw) {
    return payload_dw >= 3 && ((payload[2] >> 20) & 1u) != 0;
}

// Whether an INDIRECT_BUFFER packet replaces the buffer that holds it. CHAIN means "do not
// return to this IB"; a ring is not an IB, so the CP always returns to the ring after the IB
// (a CHAIN bit on a ring-level packet makes it a plain call).
inline bool ChainReplacesTop(bool chain, bool top_is_ring) {
    return chain && !top_is_ring;
}

// The compute walker (ProcessComputePackets, recursive): `depth` is 0 for the ring of a queue,
// whether or not the queue has a read pointer to publish, and > 0 inside an IB.
// (Queues mapped with a NULL read pointer walk their ring with read_ptr == nullptr too.)
inline bool ComputeChainReplaces(bool chain, int depth) {
    return ChainReplacesTop(chain, depth == 0);
}

// Enters the IB [ib, ib + ib_dw) after the top frame has consumed the packet: a chained IB
// replaces the top frame, a call pushes a new frame unless the nesting limit is reached.
// bottom_is_ring: frames[0] is a ring (compute queue), not a submitted command buffer.
// Returns false when the call is skipped.
inline bool Enter(std::vector<Frame> &frames, const uint32_t *ib, uint32_t ib_dw, bool chain,
                  bool bottom_is_ring = false) {
    if (!frames.empty() && ChainReplacesTop(chain, bottom_is_ring && frames.size() == 1)) {
        frames.back() = {ib, ib_dw, 0};
        return true;
    }
    if (frames.size() >= (size_t)kMaxDepth + 1) return false;
    frames.push_back({ib, ib_dw, 0});
    return true;
}

// What the executor of a walk reports for the packet at frames.back().pc.
struct Packet {
    uint32_t step = 0;     // dwords the packet takes; 0 = truncated: drop the rest of the buffer
    bool suspend = false;  // stop at this packet; the next walk starts with it again
    const uint32_t *ib = nullptr; // INDIRECT_BUFFER target (nullptr: none or not decodable)
    uint32_t ib_dw = 0;
    bool chain = false;
};

// Walks `frames` (resumable: the frames are the whole state) until they are exhausted (true)
// or the executor suspends at a packet (false). exec(const Frame &top, int depth, Packet &out)
// runs the packet at top.pc; progress(pc) reports that frames[0] is consumed up to pc (a
// ring's read pointer). Used by the compute queues, whose WAIT_REG_MEM suspends the queue.
template <class Exec, class Progress>
inline bool Walk(std::vector<Frame> &frames, bool bottom_is_ring, Exec &&exec, Progress &&progress) {
    while (!frames.empty()) {
        Frame &f = frames.back();
        if (f.pc >= f.dw) {
            frames.pop_back();
            continue;
        }
        const int depth = (int)frames.size() - 1;
        Packet pk;
        exec(static_cast<const Frame &>(f), depth, pk);
        if (pk.suspend) return false;
        if (pk.step == 0) {
            frames.pop_back();
            continue;
        }
        f.pc += pk.step;
        if (depth == 0) progress(f.pc < f.dw ? f.pc : f.dw);
        if (pk.ib) Enter(frames, pk.ib, pk.ib_dw, pk.chain, bottom_is_ring);
    }
    return true;
}

} // namespace ps4gnm_ib
