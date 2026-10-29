// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "shader_recompiler/info.h"
#include "shader_recompiler/ir/microinstruction.h"
#include "shader_recompiler/ir/value.h"

namespace Shader::Optimization {

// PS4Recomp: whether every lane of a wave holds the same value. Conservative: a value is uniform
// only when nothing it is computed from can differ between lanes: lane and invocation ids,
// per-lane attributes, phis, cross-lane operations (including per-lane tests of 64-bit masks,
// i.e. EXEC), and memory other lanes may write (workgroup memory, guest memory, images, buffers
// the shader writes, atomics). User data and buffer loads (vector or scalar) are uniform when
// their address is, if nothing can write the memory during the dispatch: the buffers the CPU
// fills (the flattened user data, clip planes) always; other buffers only when the shader writes
// no buffer and no guest memory (buffer atomics count as writes). Images do not count: the
// renderer binds every image as a Metal texture of its own, never as a view of a guest buffer, so
// an image store cannot change what a buffer load reads during the dispatch. Otherwise a store
// through another binding
// (two V#s over one buffer), through a V# loaded at run time, or from another workgroup could
// land between the loads of the two 32-lane halves of a wave, which on Metal read memory at
// different times where GCN's one wave-wide load gives every lane the same value; an `if` on such
// a value around a barrier would then hang the GPU. Writes by other GPU work running at the same
// time are a race in the guest program and are not considered. Everything it calls uniform is
// also the same in every wave of the workgroup.
class WaveUniformity {
public:
    // `scalar_state_` (predicate_wave64_pass.cpp): scalar registers, SCC, the VCC halves, M0 and
    // the results of v_readlane / v_readfirstlane count as the same in every lane of a wave, as
    // they are where scalar register writes are masked per wave. Such values can differ between
    // waves, so with it IsUniform says "the same in every lane of each wave".
    // `wave_gotos` (with scalar_state): the structurizer's goto variables that hold one value per
    // wave (predicate_wave64_pass.cpp masks their writes per wave).
    explicit WaveUniformity(const Info& info_, bool scalar_state_ = false,
                            const std::unordered_set<u32>* wave_gotos_ = nullptr)
        : info{info_}, scalar_state{scalar_state_}, wave_gotos{wave_gotos_},
          writes_memory{WritesMemory(info_)} {}

    // The shader may write memory a buffer binding reads: a buffer it writes (other than the
    // renderer's own fault, GDS and workgroup memory buffers, which no V# reaches; buffer atomics
    // mark their buffer written), or guest memory through V#s loaded at run time.
    static bool WritesMemory(const Info& info) {
        if (info.uses_guest_memory) {
            return true;
        }
        for (const BufferResource& b : info.buffers) {
            if (b.is_written && (b.buffer_type == BufferType::Guest || b.buffer_type == BufferType::BdaPagetable)) {
                return true;
            }
        }
        return false;
    }

    bool IsUniform(const IR::Value& value) {
        if (value.IsImmediate()) {
            return true;
        }
        IR::Inst* const root = value.Inst();
        if (const auto it = known.find(root); it != known.end()) {
            return it->second;
        }
        // Post-order walk: an instruction is decided once its arguments are, or by itself.
        stack.clear();
        stack.push_back({root, 0});
        while (!stack.empty()) {
            Frame& top = stack.back();
            IR::Inst* const inst = top.inst;
            if (top.next_arg == 0) {
                if (const std::optional<bool> own = OwnUniformity(*inst)) {
                    known[inst] = *own;
                    stack.pop_back();
                    continue;
                }
            }
            std::optional<bool> decided;
            while (top.next_arg < inst->NumArgs()) {
                const IR::Value arg = inst->Arg(top.next_arg);
                if (!arg.IsImmediate()) {
                    const auto it = known.find(arg.Inst());
                    if (it == known.end()) {
                        break; // decide the argument first
                    }
                    if (!it->second) {
                        decided = false;
                        break;
                    }
                }
                ++top.next_arg;
            }
            if (!decided && top.next_arg < inst->NumArgs()) {
                IR::Inst* const arg_inst = inst->Arg(top.next_arg).Inst();
                stack.push_back({arg_inst, 0}); // invalidates `top`
                continue;
            }
            known[inst] = decided.value_or(true);
            stack.pop_back();
        }
        return known.at(root);
    }

private:
    struct Frame {
        IR::Inst* inst;
        size_t next_arg;
    };

    // Uniformity decided by the instruction itself, or nullopt when its arguments decide it.
    std::optional<bool> OwnUniformity(const IR::Inst& inst) const {
        if (inst.MayHaveSideEffects()) {
            return false;
        }
        if (scalar_state) {
            switch (inst.GetOpcode()) {
            case IR::Opcode::GetScalarRegister:
            case IR::Opcode::GetScc:
            case IR::Opcode::GetVccLo:
            case IR::Opcode::GetVccHi:
            case IR::Opcode::GetM0:
            case IR::Opcode::ReadLane:
            case IR::Opcode::ReadFirstLane:
            case IR::Opcode::WaveAny:
                return true;
            case IR::Opcode::GetGotoVariable:
                if (wave_gotos && wave_gotos->contains(inst.Arg(0).U32())) {
                    return true;
                }
                break;
            default:
                break;
            }
        }
        switch (inst.GetOpcode()) {
        case IR::Opcode::LoadBufferU8:
        case IR::Opcode::LoadBufferU16:
        case IR::Opcode::LoadBufferU32:
        case IR::Opcode::LoadBufferU32x2:
        case IR::Opcode::LoadBufferU32x3:
        case IR::Opcode::LoadBufferU32x4:
        case IR::Opcode::LoadBufferU64:
        case IR::Opcode::LoadBufferF32:
        case IR::Opcode::LoadBufferF32x2:
        case IR::Opcode::LoadBufferF32x3:
        case IR::Opcode::LoadBufferF32x4:
        case IR::Opcode::LoadBufferFormatF32:
        case IR::Opcode::ReadConstBuffer: {
            // memory nothing writes during the dispatch: decided by the address
            const IR::Value binding = inst.Arg(0);
            if (!binding.IsImmediate() || binding.U32() >= info.buffers.size()) {
                return false;
            }
            const BufferResource& b = info.buffers[binding.U32()];
            if (b.is_written) {
                return false;
            }
            const bool cpu_filled = b.buffer_type == BufferType::Flatbuf || b.buffer_type == BufferType::ClipPlanes;
            if (!cpu_filled && writes_memory) {
                return false;
            }
            return std::nullopt;
        }
        case IR::Opcode::LoadGuestU32:
        case IR::Opcode::ImageSampleRaw:
        case IR::Opcode::ImageSampleImplicitLod:
        case IR::Opcode::ImageSampleExplicitLod:
        case IR::Opcode::ImageSampleDrefImplicitLod:
        case IR::Opcode::ImageSampleDrefExplicitLod:
        case IR::Opcode::ImageGather:
        case IR::Opcode::ImageGatherDref:
        case IR::Opcode::ImageQueryDimensions:
        case IR::Opcode::ImageQueryLod:
        case IR::Opcode::ImageGradient:
        case IR::Opcode::ImageRead:
        case IR::Opcode::GetAttributeU1:
        case IR::Opcode::GetScc:
        case IR::Opcode::GetExec:
        case IR::Opcode::GetVcc:
        case IR::Opcode::GetVccLo:
        case IR::Opcode::GetVccHi:
        case IR::Opcode::GetM0:
        case IR::Opcode::GetScalarRegister:
        case IR::Opcode::GetVectorRegister:
        case IR::Opcode::GetVirtualRegister:
        case IR::Opcode::GetGotoVariable:
        case IR::Opcode::GetPatch:
        case IR::Opcode::GetTessGenericAttribute:
        case IR::Opcode::ReadTcsGenericOuputAttribute:
        case IR::Opcode::Phi:
        case IR::Opcode::LaneId:
        case IR::Opcode::WarpId:
        case IR::Opcode::QuadBroadcast:
        case IR::Opcode::Shuffle:
        case IR::Opcode::ShuffleXor:
        case IR::Opcode::ReadFirstLane:
        case IR::Opcode::ReadLane:
        case IR::Opcode::WriteLane:
        case IR::Opcode::Ballot:
        case IR::Opcode::BallotFindLsb:
        case IR::Opcode::InverseBallot:
        case IR::Opcode::GroupAny:
        case IR::Opcode::MaskedBitCount32:
        case IR::Opcode::DataAppend:
        case IR::Opcode::DataConsume:
        case IR::Opcode::LoadSharedU16:
        case IR::Opcode::LoadSharedU32:
        case IR::Opcode::LoadSharedU64:
            return false;
        case IR::Opcode::WorkgroupAny:
            // the same result in every invocation of the workgroup (predicate_wave64_pass.cpp)
            return true;
        case IR::Opcode::GetAttribute:
        case IR::Opcode::GetAttributeU32: {
            const IR::Attribute attr = inst.Arg(0).Attribute();
            return attr == IR::Attribute::WorkgroupId || attr == IR::Attribute::WorkgroupIndex;
        }
        default:
            return std::nullopt;
        }
    }

    const Info& info;
    const bool scalar_state;
    const std::unordered_set<u32>* wave_gotos;
    const bool writes_memory; // WritesMemory(info)
    std::unordered_map<IR::Inst*, bool> known;
    std::vector<Frame> stack;
};

} // namespace Shader::Optimization
