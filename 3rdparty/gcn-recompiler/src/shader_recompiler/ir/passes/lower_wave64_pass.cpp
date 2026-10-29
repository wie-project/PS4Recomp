// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <unordered_map>
#include <unordered_set>
#include "common/div_ceil.h"
#include "common/logging/classes.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/ir/basic_block.h"
#include "shader_recompiler/ir/breadth_first_search.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/ir/passes/wave_uniformity.h"
#include "shader_recompiler/ir/program.h"
#include "shader_recompiler/profile.h"

namespace Shader::Optimization {

// Blocks that every invocation of the workgroup runs: outside loops and outside ifs whose
// condition can differ between lanes. PS4Recomp: an EXEC scope's condition is the lane's EXEC bit,
// which is divergent unless it is wave-uniform (e.g. all ones); an exchange through workgroup
// memory there could have a barrier that only one of the wave's subgroups reaches.
static std::vector<IR::Block*> FindUniformBlocks(const IR::Program& program,
                                                 WaveUniformity& uniformity) {
    using Type = IR::AbstractSyntaxNode::Type;

    struct ConditionalScope {
        const IR::Block* merge;
        bool divergent;
    };

    std::vector<IR::Block*> blocks;
    std::vector<ConditionalScope> conditionals;
    std::vector<const IR::Block*> loops;
    u32 divergence_depth{};
    // PS4Recomp: a Return or Unreachable in divergent control flow (a divergent if or a loop)
    // can end some lanes, e.g. one subgroup at an s_endpgm inside an EXEC scope. Every later block
    // may then run without them: an exchange barrier there could wait for a subgroup that ended.
    bool lanes_may_have_ended = false;
    for (const IR::AbstractSyntaxNode& node : program.syntax_list) {
        switch (node.type) {
        case Type::Return:
        case Type::Unreachable:
            if (divergence_depth > 0 || !loops.empty()) {
                lanes_may_have_ended = true;
            }
            break;
        case Type::If: {
            IR::Value cond = node.data.if_node.cond;
            if (const IR::Inst* ref = cond.TryInst();
                ref && ref->GetOpcode() == IR::Opcode::ConditionRef) {
                cond = ref->Arg(0);
            }
            // a predicated if runs in every invocation (predicate_wave64_pass.cpp)
            const bool divergent = !program.predicated_merges.contains(node.data.if_node.merge) &&
                                   !uniformity.IsUniform(cond);
            conditionals.push_back({node.data.if_node.merge, divergent});
            divergence_depth += static_cast<u32>(divergent);
            break;
        }
        case Type::EndIf:
            ASSERT(!conditionals.empty() && conditionals.back().merge == node.data.end_if.merge);
            divergence_depth -= static_cast<u32>(conditionals.back().divergent);
            conditionals.pop_back();
            break;
        case Type::Loop:
            // a predicated loop iterates in every invocation until none runs it any more
            if (!program.predicated_merges.contains(node.data.loop.merge)) {
                loops.push_back(node.data.loop.merge);
            }
            break;
        case Type::Repeat:
            if (program.predicated_merges.contains(node.data.repeat.merge)) {
                break;
            }
            if (loops.empty() || loops.back() != node.data.repeat.merge) {
                return {};
            }
            loops.pop_back();
            break;
        case Type::Block:
            if (divergence_depth == 0 && loops.empty() && !lanes_may_have_ended) {
                blocks.push_back(node.data.block);
            }
            break;
        default:
            break;
        }
    }
    if (!conditionals.empty() || !loops.empty()) {
        return {};
    }
    return blocks;
}

static IR::Inst* FindBallotForMaskedBitCount(const IR::Inst& mbcnt) {
    IR::Value value = mbcnt.Arg(0);
    if (value.IsImmediate()) {
        return nullptr;
    }
    IR::Inst* inst = value.Inst();
    if (inst->GetOpcode() != IR::Opcode::CompositeExtractU32x2 || inst->Arg(0).IsImmediate()) {
        return nullptr;
    }
    inst = inst->Arg(0).Inst();
    if (inst->GetOpcode() != IR::Opcode::UnpackUint2x32 || inst->Arg(0).IsImmediate()) {
        return nullptr;
    }
    inst = inst->Arg(0).Inst();
    return inst->GetOpcode() == IR::Opcode::Ballot ? inst : nullptr;
}


// PS4Recomp: v_readfirstlane carries EXEC (it ignores the EXEC scope, so it can run where some
// executing lanes are outside EXEC). Inside a scope whose condition is that same EXEC value, the
// executing lanes are the EXEC lanes: the operand becomes true and the plain subgroup broadcast
// of the first executing lane is emitted.
static void DropRedundantReadFirstLaneExec(IR::Program& program) {
    using Type = IR::AbstractSyntaxNode::Type;
    std::vector<IR::Value> conds;
    for (const IR::AbstractSyntaxNode& node : program.syntax_list) {
        switch (node.type) {
        case Type::If: {
            // the structurizer refers to the condition through ConditionRef
            IR::Value cond = node.data.if_node.cond;
            if (const IR::Inst* ref = cond.TryInst();
                ref && ref->GetOpcode() == IR::Opcode::ConditionRef) {
                cond = ref->Arg(0);
            }
            conds.push_back(cond);
            break;
        }
        case Type::EndIf:
            conds.pop_back();
            break;
        case Type::Block:
            for (IR::Inst& inst : node.data.block->Instructions()) {
                if (inst.GetOpcode() != IR::Opcode::ReadFirstLane) {
                    continue;
                }
                const IR::Value exec = inst.Arg(1);
                if (!exec.IsImmediate() && std::ranges::find(conds, exec) != conds.end()) {
                    inst.SetArg(1, IR::Value{true});
                }
            }
            break;
        default:
            break;
        }
    }
}

// PS4Recomp: whether `value` is computed (through any chain of instructions) from one of `insts`.
static bool ReachesAny(const IR::Value& value, const std::unordered_set<const IR::Inst*>& insts) {
    if (value.IsImmediate()) {
        return false;
    }
    std::unordered_set<const IR::Inst*> visited{value.Inst()};
    std::vector<const IR::Inst*> pending{value.Inst()};
    while (!pending.empty()) {
        const IR::Inst* const inst = pending.back();
        pending.pop_back();
        if (insts.contains(inst)) {
            return true;
        }
        for (size_t i = 0; i < inst->NumArgs(); ++i) {
            const IR::Value arg = inst->Arg(i);
            if (!arg.IsImmediate() && visited.insert(arg.Inst()).second) {
                pending.push_back(arg.Inst());
            }
        }
    }
    return false;
}

// PS4Recomp: GCN lane of this invocation (0..63). A wave64 compute shader on a smaller subgroup
// runs each wave as several subgroups, and its GCN lane is the local invocation index modulo 64.
IR::U32 ComputeWaveLane(IR::IREmitter& ir) {
    return ir.BitwiseAnd(ir.GetAttributeU32(IR::Attribute::LocalInvocationIndex), ir.Imm32(63u));
}


// Cross-lane operations whose result depends on the GCN wave layout. All stages: v_readfirstlane
// drops EXEC where it is implied. Wave64 compute
// shaders run as two 32-wide subgroups when the host subgroup is smaller: their operations that
// cross the halves (ballots, readlane, readfirstlane, mbcnt) exchange through workgroup memory in
// uniform control flow, and 64-bit masks applied per lane test the lane's own bit.
void LowerWaveOpsPass(IR::Program& program, const RuntimeInfo& runtime_info,
                      const Profile& profile) {
    const auto [size_x, size_y, size_z] = runtime_info.hw.cs.workgroup_size;
    const u32 num_threads = size_x * size_y * size_z;
    const bool split = program.info.hw_stage == HwStage::Compute && profile.subgroup_size < 64 &&
                       num_threads > 32;

    DropRedundantReadFirstLaneExec(program);
    // v_writelane, in every stage: the lane named by the scalar operand (its low 6 bits) takes the
    // new value, the others keep theirs, whatever EXEC is. A wave64 compute shader split into
    // subgroups names the GCN lane (the local invocation index modulo 64).
    for (IR::Block* block : program.blocks) {
        for (auto it = block->begin(); it != block->end();) {
            IR::Inst& inst = *it++;
            if (inst.GetOpcode() != IR::Opcode::WriteLane) {
                continue;
            }
            IR::IREmitter ir{*block, IR::Block::InstructionList::s_iterator_to(inst)};
            const IR::U32 lane = split ? ComputeWaveLane(ir) : ir.LaneId();
            const IR::U1 is_lane =
                ir.IEqual(lane, ir.BitwiseAnd(IR::U32{inst.Arg(2)}, ir.Imm32(63u)));
            inst.ReplaceUsesWithAndRemove(ir.Select(is_lane, inst.Arg(1), inst.Arg(0)));
        }
    }
    if (split) {
        // A value that is the same in every lane needs no exchange: any lane reads it. Code that
        // only fed such reads (e.g. a ballot picking the lane) is dead afterwards.
        WaveUniformity uniformity{program.info};
        for (IR::Block* block : program.blocks) {
            for (IR::Inst& inst : block->Instructions()) {
                if ((inst.GetOpcode() == IR::Opcode::ReadLane ||
                     inst.GetOpcode() == IR::Opcode::ReadFirstLane) &&
                    uniformity.IsUniform(inst.Arg(0))) {
                    inst.ReplaceUsesWith(inst.Arg(0));
                }
            }
        }
        DeadCodeEliminationPass(program);
    }

    // A workgroup whose last wave has 32 lanes or fewer: that wave has no upper half.
    const bool last_wave_half = num_threads % 64 != 0 && num_threads % 64 <= 32;
    std::vector<IR::Inst*> inverse_ballots;
    std::vector<IR::Inst*> worklist;
    // ballots left to each subgroup (non-uniform control flow): their bits are in the
    // subgroup's own layout, lanes 32-63 in the low word
    std::unordered_set<const IR::Inst*> subgroup_ballots;
    WaveUniformity cond_uniformity{program.info};
    const auto uniform_blocks =
        split ? FindUniformBlocks(program, cond_uniformity) : std::vector<IR::Block*>{};
    for (IR::Block* block : program.blocks) {
        const bool is_uniform = std::ranges::contains(uniform_blocks, block);
        const auto push_worklist = [&](IR::Inst& inst) {
            if (is_uniform) {
                worklist.push_back(&inst);
            } else {
                LOG_WARNING(Render_Recompiler, "{} instruction in non uniform control flow",
                            inst.GetOpcode());
                if (inst.GetOpcode() == IR::Opcode::Ballot) {
                    subgroup_ballots.insert(&inst);
                } else {
                    // a known gap, counted (Info::per_subgroup_lane_ops)
                    ++program.info.per_subgroup_lane_ops;
                }
            }
        };
        for (IR::Inst& inst : block->Instructions()) {
            const IR::Opcode op = inst.GetOpcode();
            if (!split) {
                continue;
            }
            if (op == IR::Opcode::InverseBallot) {
                inverse_ballots.push_back(&inst);
            } else if (op == IR::Opcode::ReadLane || op == IR::Opcode::ReadFirstLane) {
                push_worklist(inst);
            } else if (op == IR::Opcode::Ballot) {
                const IR::Value bit = inst.Arg(0);
                if (program.subgroup_ballots.contains(&inst)) {
                    // predicated code whose mask is only tested per lane: no exchange needed
                    subgroup_ballots.insert(&inst);
                } else if (is_uniform && bit.IsImmediate() && bit.U1()) {
                    // every lane of the wave runs this: the mask of the lanes the wave has
                    IR::IREmitter ir{*block, IR::Block::InstructionList::s_iterator_to(inst)};
                    IR::U64 mask = ir.Imm64(~u64{0});
                    if (num_threads % 64 != 0) {
                        const IR::U32 wave = ir.ShiftRightLogical(
                            ir.GetAttributeU32(IR::Attribute::LocalInvocationIndex), ir.Imm32(6u));
                        mask = IR::U64{ir.Select(ir.IEqual(wave, ir.Imm32(num_threads / 64)),
                                                 ir.Imm64((u64{1} << (num_threads % 64)) - 1), mask)};
                    }
                    inst.ReplaceUsesWithAndRemove(mask);
                } else {
                    push_worklist(inst);
                }
            } else if (op == IR::Opcode::WorkgroupAny || op == IR::Opcode::WaveAny) {
                // predicate_wave64_pass.cpp only emits them where every invocation runs
                ASSERT_MSG(is_uniform, "{} in non uniform control flow", op);
                worklist.push_back(&inst);
            } else if (op == IR::Opcode::MaskedBitCount32) {
                IR::Inst* const ballot = FindBallotForMaskedBitCount(inst);
                if (ballot == nullptr ||
                    (std::ranges::contains(uniform_blocks, ballot->GetParent()) &&
                     !program.subgroup_ballots.contains(ballot))) {
                    worklist.push_back(&inst);
                } else {
                    ++program.info.per_subgroup_lane_ops;
                }
            }
        }
    }

    for (IR::Inst* inst : inverse_ballots) {
        // A mask built from a ballot left to each subgroup keeps the subgroup's own bit (the
        // layout it was made in): testing the wave lane's bit there reads 0 in lanes 32-63, and
        // loops waiting for their bit never ended (Uncharted 4 async CS with spilled V#s).
        if (!subgroup_ballots.empty() && ReachesAny(inst->Arg(0), subgroup_ballots)) {
            continue;
        }
        // a 64-bit mask applied per lane: this lane's bit, from the half holding the lane
        IR::IREmitter ir{*inst->GetParent(), IR::Block::InstructionList::s_iterator_to(*inst)};
        const IR::U32 lane = ComputeWaveLane(ir);
        const IR::Value halves = ir.UnpackUint2x32(IR::U64{inst->Arg(0)});
        const IR::U32 word = IR::U32{ir.Select(ir.ILessThan(lane, ir.Imm32(32u), false),
                                               ir.CompositeExtract(halves, 0),
                                               ir.CompositeExtract(halves, 1))};
        const IR::U32 bit =
            ir.BitFieldExtract(word, ir.BitwiseAnd(lane, ir.Imm32(31u)), ir.Imm32(1u));
        inst->ReplaceUsesWithAndRemove(ir.INotEqual(bit, ir.Imm32(0u)));
    }
    // A lane index or mask computed from a ballot left to each subgroup is in the subgroup's own
    // layout: the subgroup operation that matches it stays.
    if (!subgroup_ballots.empty()) {
        std::erase_if(worklist, [&](const IR::Inst* inst) {
            return (inst->GetOpcode() == IR::Opcode::ReadLane &&
                    ReachesAny(inst->Arg(1), subgroup_ballots)) ||
                   (inst->GetOpcode() == IR::Opcode::MaskedBitCount32 &&
                    ReachesAny(inst->Arg(0), subgroup_ballots));
        });
    }
    if (worklist.empty()) {
        return;
    }

    // Workgroup memory after the shader's own: one dword per subgroup, and for readfirstlane a
    // second array with each subgroup's value.
    const bool needs_values = std::ranges::any_of(worklist, [](const IR::Inst* inst) {
        return inst->GetOpcode() == IR::Opcode::ReadFirstLane;
    });
    const u32 num_subgroups = Common::AlignUp(num_threads, 64) / profile.subgroup_size;
    // the subgroups the workgroup has (its last wave may have no upper half)
    const u32 num_running_subgroups = Common::DivCeil(num_threads, profile.subgroup_size);
    const u32 scratch_base = Common::AlignUp(runtime_info.hw.cs.shared_memory_size, sizeof(u64));
    const u32 values_base = scratch_base + num_subgroups * u32(sizeof(u32));
    const u32 scratch_size = (needs_values ? 2 : 1) * num_subgroups * u32(sizeof(u32));
    program.info.shared_memory_scratch_size =
        scratch_base + scratch_size - runtime_info.hw.cs.shared_memory_size;

    std::unordered_set<const IR::Inst*> merged_wave_any;
    for (IR::Inst* inst : worklist) {
        if (merged_wave_any.contains(inst)) {
            continue; // lowered with its WorkgroupAny
        }
        LOG_INFO(Render_Recompiler, "Lowering {} instruction for wave64", inst->GetOpcode());
        IR::IREmitter ir{*inst->GetParent(), IR::Block::InstructionList::s_iterator_to(*inst)};
        const IR::U32 invocation_index = ir.GetAttributeU32(IR::Attribute::LocalInvocationIndex);
        const IR::U32 subgroup_id = ir.ShiftRightLogical(invocation_index, ir.Imm32(5));
        // byte offset of this subgroup's dword in an array, and of its wave's lower subgroup's
        const IR::U32 slot = ir.ShiftLeftLogical(subgroup_id, ir.Imm32(2u));
        const IR::U32 pair = ir.BitwiseAnd(slot, ir.Imm32(~7u));
        const IR::U32 offset = ir.IAdd(ir.Imm32(scratch_base), slot);
        const auto load_lo = [&](u32 base) {
            return IR::U32{ir.LoadShared(32, false, ir.IAdd(ir.Imm32(base), pair))};
        };
        // the dword of the wave's upper subgroup, 0 when the wave has no upper half
        const auto load_hi = [&](u32 base) {
            const IR::U32 hi = IR::U32{ir.LoadShared(32, false, ir.IAdd(ir.Imm32(base + 4u), pair))};
            if (!last_wave_half) {
                return hi;
            }
            const IR::U32 hi_first = ir.IAdd(ir.ShiftLeftLogical(pair, ir.Imm32(3u)), ir.Imm32(32u));
            return IR::U32{ir.Select(ir.ILessThan(hi_first, ir.Imm32(num_threads), false), hi,
                                     ir.Imm32(0u))};
        };
        if (inst->GetOpcode() == IR::Opcode::WorkgroupAny ||
            inst->GetOpcode() == IR::Opcode::WaveAny) {
            // Each subgroup publishes whether any of its invocations has the bit, then every
            // invocation reads all of them (WorkgroupAny) or its wave's two (WaveAny). A WaveAny
            // of the same bit later in the block reads the same publication.
            const IR::Value bit = inst->Arg(0);
            const IR::U1 any = ir.GroupAny(IR::U1{bit});
            ir.WriteShared(32, ir.Select(any, ir.Imm32(1u), ir.Imm32(0u)), offset);
            ir.LdsBarrier();
            IR::Inst* wave_any = inst->GetOpcode() == IR::Opcode::WaveAny ? inst : nullptr;
            if (!wave_any) {
                for (auto it = std::next(IR::Block::InstructionList::s_iterator_to(*inst));
                     it != inst->GetParent()->end(); ++it) {
                    if (it->GetOpcode() == IR::Opcode::WaveAny && it->Arg(0) == bit) {
                        wave_any = &*it;
                        merged_wave_any.insert(wave_any);
                        break;
                    }
                }
            }
            IR::U1 wave_result{};
            if (wave_any) {
                wave_result = ir.INotEqual(ir.BitwiseOr(load_lo(scratch_base), load_hi(scratch_base)),
                                           ir.Imm32(0u));
            }
            IR::U1 all_result{};
            if (inst->GetOpcode() == IR::Opcode::WorkgroupAny) {
                IR::U32 all = IR::U32{ir.LoadShared(32, false, ir.Imm32(scratch_base))};
                for (u32 k = 1; k < num_running_subgroups; ++k) {
                    all = ir.BitwiseOr(all, IR::U32{ir.LoadShared(32, false, ir.Imm32(scratch_base + 4 * k))});
                }
                all_result = ir.INotEqual(all, ir.Imm32(0u));
            }
            ir.LdsBarrier();
            if (wave_any && wave_any != inst) {
                wave_any->ReplaceUsesWithAndRemove(wave_result);
            }
            inst->ReplaceUsesWithAndRemove(inst->GetOpcode() == IR::Opcode::WorkgroupAny
                                               ? all_result
                                               : wave_result);
        } else if (inst->GetOpcode() == IR::Opcode::Ballot) {
            const IR::U32 mask_low =
                IR::U32{ir.CompositeExtract(ir.UnpackUint2x32(ir.Ballot(IR::U1{inst->Arg(0)})), 0)};
            ir.WriteShared(32, mask_low, offset);
            ir.LdsBarrier();
            // PS4Recomp: two 32-bit loads of the subgroup pair instead of one 64-bit load. A
            // u64 view of workgroup memory next to the u32 one needs explicit workgroup layouts
            // (aliased blocks), which the MSL backend drops: the variables went undeclared and
            // Metal rejected the shader (Uncharted 4 async CS 0x130015c400).
            const IR::U32 lo = load_lo(scratch_base);
            const IR::U32 hi = load_hi(scratch_base);
            const IR::U64 mask = ir.PackUint2x32(ir.CompositeConstruct(lo, hi));
            ir.LdsBarrier();
            inst->ReplaceUsesWithAndRemove(mask);
        } else if (inst->GetOpcode() == IR::Opcode::ReadLane) {
            // v_readlane: the lane is the low 6 bits of a scalar operand (the same in every lane)
            // made the same in the subgroup: a slot is written by one value even where the
            // lane operand differed between lanes (e.g. an SGPR written in divergent code)
            const IR::U32 lane =
                ir.BitwiseAnd(ir.ReadFirstLane(IR::U32{inst->Arg(1)}, ir.Imm1(true)), ir.Imm32(63u));
            const IR::U32 lane32 = ir.BitwiseAnd(lane, ir.Imm32(31u));
            const IR::U32 half = ir.ShiftRightLogical(lane, ir.Imm32(5u));
            ir.WriteShared(32, ir.ReadLane(IR::U32{inst->Arg(0)}, lane32), offset);
            ir.LdsBarrier();
            const IR::U32 value = IR::U32{ir.LoadShared(
                32, false,
                ir.IAdd(ir.IAdd(ir.Imm32(scratch_base), pair), ir.ShiftLeftLogical(half, ir.Imm32(2u))))};
            ir.LdsBarrier();
            inst->ReplaceUsesWithAndRemove(value);
        } else if (inst->GetOpcode() == IR::Opcode::ReadFirstLane) {
            // v_readfirstlane: the first lane in EXEC across both halves, lane 0 when EXEC is 0.
            // EXEC 0 in the whole wave: neither half publishes an EXEC lane, the lower subgroup
            // publishes its lane 0's value (subgroup ReadFirstLane with no EXEC lane) and that is
            // the result, GCN lane 0. This is reached by both subgroups because the block is
            // uniform (FindUniformBlocks): no EXEC scope around it can leave a subgroup out.
            // Each subgroup publishes the value of its first EXEC lane (of its lane 0 without
            // one) and its half of EXEC. When EXEC is the executing lanes (true here), a lower
            // subgroup running this has the wave's first EXEC lane, and its value is the result.
            const IR::U32 value{inst->Arg(0)};
            const IR::U1 exec{inst->Arg(1)};
            const bool exec_is_executing = exec.IsImmediate() && exec.U1();
            ir.WriteShared(32, ir.ReadFirstLane(value, exec), ir.IAdd(ir.Imm32(values_base), slot));
            if (!exec_is_executing) {
                const IR::U32 exec_half =
                    IR::U32{ir.CompositeExtract(ir.UnpackUint2x32(ir.Ballot(exec)), 0)};
                ir.WriteShared(32, exec_half, offset);
            }
            ir.LdsBarrier();
            IR::U32 result = load_lo(values_base);
            if (!exec_is_executing) {
                // no EXEC lane in the lower half: the upper half's first, if it has one
                const IR::U1 take_hi =
                    ir.LogicalAnd(ir.IEqual(load_lo(scratch_base), ir.Imm32(0u)),
                                  ir.INotEqual(load_hi(scratch_base), ir.Imm32(0u)));
                result = IR::U32{ir.Select(take_hi, load_hi(values_base), result)};
            }
            ir.LdsBarrier();
            inst->ReplaceUsesWithAndRemove(result);
        } else if (inst->GetOpcode() == IR::Opcode::MaskedBitCount32) {
            const IR::U32 subgroup_invocation_id = ir.BitwiseAnd(invocation_index, ir.Imm32(63));
            const IR::U64 mask = ir.ISub(
                ir.ShiftLeftLogical(ir.Imm64(u64{1}), subgroup_invocation_id), ir.Imm64(u64{1}));
            const IR::U32 thread_mask{
                ir.CompositeExtract(ir.UnpackUint2x32(mask), inst->Arg(2).U1() ? 1u : 0u)};
            const IR::U32 masked_value{
                ir.BitCount(ir.BitwiseAnd(IR::U32{inst->Arg(0)}, thread_mask))};
            inst->ReplaceUsesWithAndRemove(ir.IAdd(masked_value, IR::U32{inst->Arg(1)}));
        }
    }
}

} // namespace Shader::Optimization
