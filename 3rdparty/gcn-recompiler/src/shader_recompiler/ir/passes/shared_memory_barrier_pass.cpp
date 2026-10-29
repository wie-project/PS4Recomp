// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <unordered_set>
#include "shader_recompiler/ir/breadth_first_search.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/program.h"
#include "shader_recompiler/profile.h"

namespace Shader::Optimization {

static bool IsLoadShared(const IR::Inst& inst) {
    return inst.GetOpcode() == IR::Opcode::LoadSharedU16 ||
           inst.GetOpcode() == IR::Opcode::LoadSharedU32 ||
           inst.GetOpcode() == IR::Opcode::LoadSharedU64;
}

static bool IsWriteShared(const IR::Inst& inst) {
    const IR::Opcode opcode = inst.GetOpcode();
    if (opcode >= IR::Opcode::SharedAtomicIAdd32 && opcode <= IR::Opcode::SharedAtomicCmpSwap64) {
        return !inst.Flags<bool>();
    }
    return opcode == IR::Opcode::WriteSharedU16 || opcode == IR::Opcode::WriteSharedU32 ||
           opcode == IR::Opcode::WriteSharedU64;
}

static bool IsPredicatedEnd(const IR::Inst& inst) {
    const IR::Opcode op = inst.GetOpcode();
    return op == IR::Opcode::PredicatedEnd || op == IR::Opcode::PredicatedEndU32 ||
           op == IR::Opcode::PredicatedEndU64 || op == IR::Opcode::PredicatedEndF32;
}

// PS4Recomp: where a barrier before `it` goes. An access masked by predicate_wave64_pass.cpp
// runs inside a selection (PredicatedBegin ... PredicatedEnd): the barrier goes before it, where
// every invocation runs.
static IR::Block::iterator BarrierPointBefore(IR::Block* block, IR::Block::iterator it) {
    if (it != block->begin() && std::prev(it)->GetOpcode() == IR::Opcode::PredicatedBegin) {
        return std::prev(it);
    }
    return it;
}

// Inserts barriers when a shared memory write and read occur in the same basic block.
// PS4Recomp: `lock_step` (a block of predicated code) also orders a write after a write, so every
// write of the wave is one step as in GCN's lock-step wave.
static void EmitBarrierInBlock(IR::Block* block, bool subgroup = false, bool lock_step = false) {
    const auto emit_barrier = [subgroup](IR::IREmitter& ir) {
        if (subgroup) {
            ir.SubgroupBarrier();
        } else {
            ir.Barrier();
        }
    };
    enum class BarrierAction : u32 {
        None,
        BarrierOnWrite,
        BarrierOnRead,
    };
    BarrierAction action{};
    for (IR::Inst& inst : block->Instructions()) {
        const auto at = IR::Block::InstructionList::s_iterator_to(inst);
        if (lock_step && (inst.GetOpcode() == IR::Opcode::Barrier ||
                          inst.GetOpcode() == IR::Opcode::LdsBarrier)) {
            // orders every access before it (e.g. between the halves of a write)
            action = BarrierAction::None;
            continue;
        }
        if (IsLoadShared(inst)) {
            if (action == BarrierAction::BarrierOnRead) {
                IR::IREmitter ir{*block, BarrierPointBefore(block, at)};
                emit_barrier(ir);
            }
            action = BarrierAction::BarrierOnWrite;
            continue;
        }
        if (IsWriteShared(inst)) {
            if (action == BarrierAction::BarrierOnWrite ||
                (lock_step && action == BarrierAction::BarrierOnRead)) {
                IR::IREmitter ir{*block, BarrierPointBefore(block, at)};
                emit_barrier(ir);
            }
            action = BarrierAction::BarrierOnRead;
        }
    }
    if (action != BarrierAction::None) {
        // after a masked access that ends the block, not inside its selection
        auto last = --block->end();
        IR::IREmitter ir{*block, IsPredicatedEnd(*last) ? block->end() : last};
        emit_barrier(ir);
    }
}

using NodeSet = std::unordered_set<const IR::Block*>;

static void EmitBarrierAtBlockStart(IR::Block* block) {
    auto insert_point = std::ranges::find_if_not(block->Instructions(), IR::IsPhi);
    IR::IREmitter ir{*block, insert_point};
    ir.Barrier();
}

static bool IsDivergent(const IR::U1& cond) {
    return IR::BreadthFirstSearch(cond, [](IR::Inst* inst) -> std::optional<bool> {
               if (inst->GetOpcode() == IR::Opcode::GetAttributeU32 &&
                   inst->Arg(0).Attribute() == IR::Attribute::LocalInvocationId) {
                   return true;
               }
               return std::nullopt;
           }) == true;
}

// Inserts a barrier after divergent conditional blocks to avoid undefined
// behavior when some threads write and others read from shared memory.
static void EmitBarrierInMergeBlock(const IR::Program& program,
                                    const IR::AbstractSyntaxNode::Data& data,
                                    NodeSet& divergence_end, u32& divergence_depth) {
    const IR::U1 cond = data.if_node.cond;
    // PS4Recomp: a predicated if runs in every invocation; a branch nested in predicated code
    // (predicate_wave64_pass.cpp) may be taken by one subgroup only (e.g. on an SGPR written in
    // earlier divergent code), whatever its condition looks like
    if (!program.predicated_merges.contains(data.if_node.merge) &&
        (program.branch_merges.contains(data.if_node.merge) || IsDivergent(cond))) {
        if (divergence_depth == 0) {
            EmitBarrierAtBlockStart(data.if_node.merge);
        }
        ++divergence_depth;
        divergence_end.emplace(data.if_node.merge);
    }
}

// A barrier inside a loop is invalid when different invocations leave on different iterations.
// Mark such loops so their shared-memory synchronization can be deferred to the merge block.
static NodeSet FindDivergentLoops(const IR::Program& program) {
    NodeSet divergent_loops;
    for (const IR::AbstractSyntaxNode& node : program.syntax_list) {
        switch (node.type) {
        case IR::AbstractSyntaxNode::Type::Repeat:
            // PS4Recomp: a predicated loop iterates in every invocation
            if (program.predicated_merges.contains(node.data.repeat.merge)) {
                break;
            }
            if (IsDivergent(node.data.repeat.cond)) {
                divergent_loops.emplace(node.data.repeat.merge);
            }
            break;
        case IR::AbstractSyntaxNode::Type::Break:
            if (!program.predicated_merges.contains(node.data.break_node.merge) &&
                IsDivergent(node.data.break_node.cond)) {
                divergent_loops.emplace(node.data.break_node.merge);
            }
            break;
        default:
            break;
        }
    }
    return divergent_loops;
}

static constexpr u32 GcnSubgroupSize = 64;

void SharedMemoryBarrierPass(IR::Program& program, const RuntimeInfo& runtime_info,
                             const Profile& profile) {
    if (program.info.hw_stage != HwStage::Compute) {
        return;
    }
    const auto& cs_info = runtime_info.hw.cs;
    const u32 shared_memory_size = cs_info.shared_memory_size;
    const u32 threadgroup_size =
        cs_info.workgroup_size[0] * cs_info.workgroup_size[1] * cs_info.workgroup_size[2];
    if (shared_memory_size == 0) {
        return;
    }
    // The compiler can only omit barriers when the local workgroup size is the same as the HW
    // subgroup.
    if (threadgroup_size != GcnSubgroupSize || !profile.needs_lds_barriers) {
        // GCN waves can use shared memory between invocations without explicit barriers because
        // they run in lockstep. Host compilers can reorder these accesses, so keep their order
        // with subgroup memory barriers.
        // PS4Recomp: the blocks of predicated code (predicate_wave64_pass.cpp) run in every
        // invocation of the workgroup: their LDS accesses are ordered for the whole workgroup,
        // one step per write, as in GCN's lock-step wave.
        for (const IR::AbstractSyntaxNode& node : program.syntax_list) {
            if (node.type == IR::AbstractSyntaxNode::Type::Block) {
                const bool predicated = program.predicated_blocks.contains(node.data.block);
                EmitBarrierInBlock(node.data.block, !predicated, predicated);
            }
        }
        return;
    }
    using Type = IR::AbstractSyntaxNode::Type;
    u32 divergence_depth{};
    NodeSet divergence_end;
    const NodeSet divergent_loops = FindDivergentLoops(program);
    for (const IR::AbstractSyntaxNode& node : program.syntax_list) {
        if (node.type == Type::EndIf) {
            if (divergence_end.contains(node.data.end_if.merge)) {
                --divergence_depth;
            }
            continue;
        }
        // Check if branch depth is zero, we don't want to insert barrier in potentially divergent
        // code.
        if (node.type == Type::If) {
            EmitBarrierInMergeBlock(program, node.data, divergence_end, divergence_depth);
            continue;
        }
        if (node.type == Type::Loop && divergent_loops.contains(node.data.loop.merge)) {
            ++divergence_depth;
            continue;
        }
        if (node.type == Type::Repeat && divergent_loops.contains(node.data.repeat.merge)) {
            ASSERT(divergence_depth > 0);
            --divergence_depth;
            if (divergence_depth == 0) {
                EmitBarrierAtBlockStart(node.data.repeat.merge);
            }
            continue;
        }
        if (node.type == Type::Block && divergence_depth == 0) {
            EmitBarrierInBlock(node.data.block, false,
                               program.predicated_blocks.contains(node.data.block));
        }
    }
}

} // namespace Shader::Optimization
