// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// PS4Recomp: predicated execution of EXEC-divergent regions of a wave64 compute shader that runs
// as 32-wide subgroups.
//
// The translator turns an EXEC scope (v_cmpx + s_cbranch_execz) into a per-lane branch, so one
// subgroup of a wave can skip a scope or leave an EXEC loop earlier than the other. Inside such a
// region the halves cannot exchange values (v_readlane, v_readfirstlane, ballots), a barrier
// (s_barrier, or the exchange's own) would only be reached by some subgroups, and the LDS
// accesses of the two halves are not ordered as GCN's lock-step wave orders them.
//
// A divergent if or loop whose code needs the whole wave runs here in predicated mode instead:
// - every invocation of the workgroup runs its blocks; the lane's own execution state is the
//   predicate P (a U1 virtual register);
// - a predicated if is entered when any invocation of the workgroup has P && cond
//   (WorkgroupAny), and a predicated loop iterates while any invocation still runs it;
// - register writes keep their old value where P is false, and side effects (memory writes,
//   atomics, image writes, guest memory, status reports) only happen where P is true, ballots
//   and v_readfirstlane see only the lanes with P;
// - a value computed in a predicated loop and used after it is read back through a register,
//   so a lane that left the loop earlier keeps the value of its own last iteration.
// Divergent ifs nested in a predicated region whose code does not need the whole wave keep their
// real branch, with P && cond as condition, when no scalar state they write is read after the
// scope it was written in (ScalarStateStaysInScope). A v_readlane / v_readfirstlane of an if
// whose operands are computed before the if is computed in the if's header instead, so that the
// if itself need not be predicated (HoistLaneReads). The pass runs on the structured program
// before SSA is rebuilt (SsaRewritePass turns the predicate and the masked registers back into
// values).

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "common/logging/log.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/ir/basic_block.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/ir/passes/wave_uniformity.h"
#include "shader_recompiler/ir/post_order.h"
#include "shader_recompiler/ir/program.h"
#include "shader_recompiler/profile.h"

namespace Shader::Optimization {
namespace {

using NodeType = IR::AbstractSyntaxNode::Type;

enum class Mode {
    Unchanged,    // outside predicated regions: translated as before
    Predicated,   // run by every invocation of the workgroup, effects masked
    MaskedBranch, // an if in a predicated region with no barrier in it: a real branch taken by
                  // the waves that run it (W', the same in both subgroups of a wave), effects
                  // masked
    RealBranch,   // an if in a predicated region with no barriers in it and no scalar state
                  // read outside the scope that wrote it: a real branch for P && cond,
                  // unchanged inside
    InBranch,     // nested in a RealBranch if: unchanged code that only its lanes run
};

// What a condition can differ between: nothing in the workgroup, waves, or lanes.
enum class CondKind { Workgroup, Wave, Lane };

struct Region {
    NodeType type; // If or Loop
    size_t open;   // the If / Loop node
    size_t close;  // the EndIf / Repeat node
    int parent;    // enclosing region, -1 at the top level
    bool divergent;
    CondKind cond = CondKind::Lane; // an if's condition
    bool needs_wave = false;       // its code (nested regions included) needs the whole wave
    bool child_needs_wave = false; // a nested region needs it (or has lane reads in its header)
    bool scalar_writes = false;    // its code (nested regions included) writes scalar registers
    bool barrier_free = false;     // an if whose code needs no barrier in predicated mode
    bool children_barrier_free = true;
    bool exits = false;         // an if or loop that a break nested in it leaves (to a loop around it)
    bool nested_breaks = false; // a loop left by breaks nested in ifs in it
    Mode mode = Mode::Unchanged;
    IR::VirtualReg alive{};      // nested_breaks: the lane has not left the loop (a U1)
    IR::VirtualReg alive_wave{}; // nested_breaks: the lane's wave has not left it
    std::vector<size_t> blocks;        // Block nodes directly in it (a loop's header included)
    std::vector<size_t> breaks;        // Break nodes directly in it
    std::vector<IR::Inst*> wave_insts; // instructions directly in it that need the whole wave
    std::vector<IR::Inst*> hoisted;    // lane reads computed in its header (HoistLaneReads)
};

IR::Value Unwrapped(const IR::U1& cond) {
    IR::Value value = cond;
    if (const IR::Inst* ref = value.TryInst(); ref && ref->GetOpcode() == IR::Opcode::ConditionRef) {
        value = ref->Arg(0);
    }
    return value;
}

bool IsSharedAccess(IR::Opcode op) {
    switch (op) {
    case IR::Opcode::LoadSharedU16:
    case IR::Opcode::LoadSharedU32:
    case IR::Opcode::LoadSharedU64:
    case IR::Opcode::WriteSharedU16:
    case IR::Opcode::WriteSharedU32:
    case IR::Opcode::WriteSharedU64:
        return true;
    default:
        return op >= IR::Opcode::SharedAtomicIAdd32 && op <= IR::Opcode::SharedAtomicCmpSwap64;
    }
}

bool IsSharedWrite(const IR::Inst& inst) {
    const IR::Opcode op = inst.GetOpcode();
    if (op >= IR::Opcode::SharedAtomicIAdd32 && op <= IR::Opcode::SharedAtomicCmpSwap64) {
        return !inst.Flags<bool>(); // not GDS
    }
    return op == IR::Opcode::WriteSharedU16 || op == IR::Opcode::WriteSharedU32 ||
           op == IR::Opcode::WriteSharedU64;
}

// Side effects that are not masked by wrapping them in PredicatedBegin / PredicatedEnd: barriers
// (every invocation reaches them here), structural instructions, and the instructions with an
// enable operand (which get P in it).
bool IsUnwrappedSideEffect(IR::Opcode op) {
    switch (op) {
    case IR::Opcode::Barrier:
    case IR::Opcode::WorkgroupMemoryBarrier:
    case IR::Opcode::DeviceMemoryBarrier:
    case IR::Opcode::SubgroupBarrier:
    case IR::Opcode::LdsBarrier:
    case IR::Opcode::ConditionRef:
    case IR::Opcode::Prologue:
    case IR::Opcode::Epilogue:
    case IR::Opcode::StoreGuestU32:
    case IR::Opcode::GuestReportStatus:
        return true;
    default:
        return false;
    }
}

// Side effects a compute shader cannot mask (they do not occur in compute shaders).
bool IsUnsupportedSideEffect(IR::Opcode op) {
    switch (op) {
    case IR::Opcode::Discard:
    case IR::Opcode::DiscardCond:
    case IR::Opcode::SetAttribute:
    case IR::Opcode::SetTcsGenericAttribute:
    case IR::Opcode::SetPatch:
    case IR::Opcode::EmitVertex:
    case IR::Opcode::EmitPrimitive:
    case IR::Opcode::SetVcc:
        return true;
    default:
        return false;
    }
}

// The number of 32-bit components of a vector of U32 / F32, or 0.
size_t VectorComponents(IR::Type type) {
    switch (type) {
    case IR::Type::U32x2:
    case IR::Type::F32x2:
        return 2;
    case IR::Type::U32x3:
    case IR::Type::F32x3:
        return 3;
    case IR::Type::U32x4:
    case IR::Type::F32x4:
        return 4;
    default:
        return 0;
    }
}

// A value a masked register write can keep or replace (SelectValue): the scalar types with a
// Select, and vectors of them (e.g. a buffer_load_dwordx3 result kept in a register by a lowered
// phi), selected per component.
bool IsSelectable(IR::Type type) {
    return type == IR::Type::U1 || type == IR::Type::U32 || type == IR::Type::F32 ||
           type == IR::Type::U64 || VectorComponents(type) != 0;
}

// `cond ? a : b` for a value of a selectable type.
IR::Value SelectValue(IR::IREmitter& ir, const IR::U1& cond, const IR::Value& a, const IR::Value& b) {
    const size_t n = VectorComponents(a.Type());
    if (n == 0) {
        return ir.Select(cond, a, b);
    }
    std::vector<IR::Value> parts;
    parts.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        parts.push_back(ir.Select(cond, ir.CompositeExtract(a, i), ir.CompositeExtract(b, i)));
    }
    return ir.CompositeConstruct(parts);
}

// The register a Set* instruction writes, read back with the value's type; the index of the
// written value's operand.
IR::Value ReadOld(IR::IREmitter& ir, const IR::Inst& set, const IR::Value& value) {
    const bool f32 = value.Type() == IR::Type::F32;
    switch (set.GetOpcode()) {
    case IR::Opcode::SetScalarRegister:
        return f32 ? IR::Value{ir.GetScalarReg<IR::F32>(set.Arg(0).ScalarReg())}
                   : IR::Value{ir.GetScalarReg(set.Arg(0).ScalarReg())};
    case IR::Opcode::SetVectorRegister:
        return f32 ? IR::Value{ir.GetVectorReg<IR::F32>(set.Arg(0).VectorReg())}
                   : IR::Value{ir.GetVectorReg(set.Arg(0).VectorReg())};
    case IR::Opcode::SetVirtualRegister:
        return ir.GetVirtualReg(set.Arg(0).VirtualReg());
    case IR::Opcode::SetGotoVariable:
        return ir.GetGotoVariable(set.Arg(0).U32());
    case IR::Opcode::SetExec:
        return ir.GetExec();
    case IR::Opcode::SetScc:
        return ir.GetScc();
    case IR::Opcode::SetVccLo:
        return ir.GetVccLo();
    case IR::Opcode::SetVccHi:
        return ir.GetVccHi();
    case IR::Opcode::SetM0:
        return ir.GetM0();
    default:
        UNREACHABLE_MSG("Not a register write: {}", set.GetOpcode());
    }
}

// A write of the scalar state of the wave: SGPRs, SCC, the VCC halves and M0. GCN runs scalar
// instructions for the whole wave whenever any of its lanes is in EXEC.
bool IsScalarWrite(IR::Opcode op) {
    return op == IR::Opcode::SetScalarRegister || op == IR::Opcode::SetScc ||
           op == IR::Opcode::SetVccLo || op == IR::Opcode::SetVccHi || op == IR::Opcode::SetM0;
}

// A value the program keeps in a scalar register (a virtual register holding it is masked per
// wave too).
bool IsScalarValue(const IR::Value& value) {
    if (value.IsImmediate()) {
        return false;
    }
    return std::ranges::any_of(value.Inst()->Uses(), [](const IR::Use& use) {
        return IsScalarWrite(use.user->GetOpcode());
    });
}

bool IsLoadBuffer(IR::Opcode op) {
    switch (op) {
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
        return true;
    default:
        return false;
    }
}

bool IsLaneRead(IR::Opcode op) {
    return op == IR::Opcode::ReadLane || op == IR::Opcode::QuadBroadcast ||
           op == IR::Opcode::Shuffle || op == IR::Opcode::ShuffleXor;
}

std::optional<size_t> WrittenOperand(IR::Opcode op) {
    switch (op) {
    case IR::Opcode::SetScalarRegister:
    case IR::Opcode::SetVectorRegister:
    case IR::Opcode::SetVirtualRegister:
    case IR::Opcode::SetGotoVariable:
        return 1;
    case IR::Opcode::SetExec:
    case IR::Opcode::SetScc:
    case IR::Opcode::SetVccLo:
    case IR::Opcode::SetVccHi:
    case IR::Opcode::SetM0:
        return 0;
    default:
        return std::nullopt;
    }
}

class Pass {
public:
    Pass(IR::Program& program_, u32 num_threads, bool force_fallback_)
        : program{program_}, syntax{program_.syntax_list}, uniformity{program_.info},
          wave_uniformity{program_.info, true}, lds_needs_wave{num_threads == 64},
          force_fallback{force_fallback_} {}

    void Run() {
        if (!BuildRegions()) {
            return;
        }
        if (lanes_may_end && !EndsNeedTheWave()) {
            // the ends stay per lane (as returns of the invocations that reach them): counted
            for (const auto& [node, region] : ends) {
                for (int r = region; r >= 0; r = regions[r].parent) {
                    if (regions[r].divergent) {
                        ++program.info.per_lane_ends;
                        break;
                    }
                }
            }
            lanes_may_end = false;
        }
        if (!AssignModes()) {
            return;
        }
        if (const char* reason =
                force_fallback ? "forced (GCN_COMPILE_WAVE64_FALLBACK)" : Unsupported()) {
            // counted: Info::unpredicated_wave_regions (logged per shader by the renderer too)
            // the predicated ones, and those that would run unchanged after their lane reads
            // were computed in a header outside predicated code
            const auto roots = std::ranges::count_if(regions, [&](const Region& region) {
                return region.mode == Mode::Predicated ||
                       (!region.hoisted.empty() &&
                        (region.parent < 0 || regions[region.parent].mode != Mode::Predicated));
            });
            program.info.unpredicated_wave_regions += u32(roots);
            LOG_WARNING(Render_Recompiler,
                        "wave64: {} EXEC-divergent ifs and loops left per subgroup (not "
                        "predicated): {}",
                        roots, reason);
            RefuseBarrierPerSubgroup(reason);
            return;
        }
        Transform();
    }

private:
    // Whether the ends in divergent code need the whole program predicated (PredicateEnd): some
    // code after the first one, or in a divergent region around one, needs the whole wave (a
    // barrier, a lane read or ballot, LDS of a one-wave workgroup). Otherwise an end stays a
    // return of the invocations that reach it: nothing after it waits for them or reads their
    // lanes, and the rest of the program keeps its cost. (GCN ends the whole wave there; the
    // lanes outside EXEC of a wave that ended then still run the code after it, as before.)
    bool EndsNeedTheWave() {
        const auto needs_wave = [&](const IR::Block* block) {
            return std::ranges::any_of(block->Instructions(),
                                       [&](const IR::Inst& inst) { return NeedsWave(inst); });
        };
        bool first = true;
        for (const auto& [node, region] : ends) {
            bool divergent = false;
            for (int r = region; r >= 0; r = regions[r].parent) {
                if (!regions[r].divergent) {
                    continue;
                }
                divergent = true;
                for (const IR::Block* block : SubtreeBlocks(regions[r])) {
                    if (needs_wave(block)) {
                        return true;
                    }
                }
            }
            if (divergent && first) {
                first = false;
                for (size_t i = node + 1; i < syntax.size(); ++i) {
                    if (IsBlockNode(i) && needs_wave(BlockAt(i))) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // On the fallback path: a guest barrier in code that runs per subgroup would be reached by
    // some subgroups only and hang the GPU, so the shader does not translate (the renderer skips
    // its dispatches, as for any shader that does not translate) instead. Per-subgroup code is
    // every block of a region with an EXEC-divergent region at or above it (a uniform if or loop
    // nested in one included), and with an end in divergent code every block after it (the
    // invocations that ended never reach them).
    void RefuseBarrierPerSubgroup(const char* reason) const {
        std::vector<bool> per_subgroup(syntax.size(), false);
        for (size_t r = 0; r < regions.size(); ++r) {
            bool divergent = false;
            for (int i = int(r); i >= 0 && !divergent; i = regions[i].parent) {
                divergent = regions[i].divergent;
            }
            if (divergent) {
                for (const size_t b : regions[r].blocks) {
                    per_subgroup[b] = true;
                }
            }
        }
        for (const auto& [node, region] : ends) {
            bool divergent = false;
            for (int i = region; i >= 0 && !divergent; i = regions[i].parent) {
                divergent = regions[i].divergent;
            }
            if (divergent) {
                for (size_t i = node + 1; i < syntax.size(); ++i) {
                    per_subgroup[i] = true;
                }
                break; // ends are in syntax order: the first one covers the rest
            }
        }
        for (size_t i = 0; i < syntax.size(); ++i) {
            if (!per_subgroup[i] || !IsBlockNode(i)) {
                continue;
            }
            for (const IR::Inst& inst : BlockAt(i)->Instructions()) {
                if (inst.GetOpcode() == IR::Opcode::Barrier) {
                    UNREACHABLE_MSG("wave64: a barrier in per-subgroup control flow, which cannot be "
                                    "predicated ({}): the shader does not translate and its "
                                    "dispatches are skipped",
                                    reason);
                }
            }
        }
    }

    IR::Block* BlockAt(size_t index) const {
        return syntax[index].data.block;
    }

    bool IsBlockNode(size_t index) const {
        return index < syntax.size() && syntax[index].type == NodeType::Block;
    }

    bool NeedsWave(const IR::Inst& inst) {
        switch (inst.GetOpcode()) {
        case IR::Opcode::ReadLane:
        case IR::Opcode::ReadFirstLane:
            return !uniformity.IsUniform(inst.Arg(0));
        case IR::Opcode::Ballot:
            return MaskNeedsWave(inst);
        case IR::Opcode::Barrier:
            return true;
        default:
            // A 64-thread workgroup is one wave: its code relies on lock-step LDS order
            return lds_needs_wave && IsSharedAccess(inst.GetOpcode());
        }
    }

    // A register a Set* / Get* instruction accesses (kind and index), for masks moved through
    // registers.
    static std::optional<std::pair<IR::Opcode, u64>> RegisterKey(const IR::Inst& inst) {
        switch (inst.GetOpcode()) {
        case IR::Opcode::SetScalarRegister:
        case IR::Opcode::GetScalarRegister:
            return std::pair{IR::Opcode::GetScalarRegister, u64(inst.Arg(0).ScalarReg())};
        case IR::Opcode::SetVectorRegister:
        case IR::Opcode::GetVectorRegister:
            return std::pair{IR::Opcode::GetVectorRegister, u64(inst.Arg(0).VectorReg())};
        case IR::Opcode::SetVirtualRegister:
        case IR::Opcode::GetVirtualRegister:
            return std::pair{IR::Opcode::GetVirtualRegister, inst.Arg(0).VirtualReg().Key()};
        case IR::Opcode::SetVccLo:
        case IR::Opcode::GetVccLo:
            return std::pair{IR::Opcode::GetVccLo, u64{0}};
        case IR::Opcode::SetVccHi:
        case IR::Opcode::GetVccHi:
            return std::pair{IR::Opcode::GetVccHi, u64{0}};
        case IR::Opcode::SetM0:
        case IR::Opcode::GetM0:
            return std::pair{IR::Opcode::GetM0, u64{0}};
        case IR::Opcode::SetScc:
        case IR::Opcode::GetScc:
            return std::pair{IR::Opcode::GetScc, u64{0}};
        default:
            return std::nullopt;
        }
    }

    static bool IsRegisterRead(IR::Opcode op) {
        return op == IR::Opcode::GetScalarRegister || op == IR::Opcode::GetVectorRegister ||
               op == IR::Opcode::GetVirtualRegister || op == IR::Opcode::GetVccLo ||
               op == IR::Opcode::GetVccHi || op == IR::Opcode::GetM0 ||
               op == IR::Opcode::GetScc;
    }

    // The register reads the write `set` reaches (on a path without another write of the
    // register). After the first SSA pass they are the reads of lowered phis and of values whose
    // definition does not dominate their use.
    template <typename F>
    static void ForEachReachedRead(const IR::Inst& set, F&& func) {
        const auto key = RegisterKey(set);
        // scans [it, end) of a block; false when the register is written again there
        const auto scan = [&](const IR::Block* block, IR::Block::const_iterator it) {
            for (; it != block->end(); ++it) {
                if (RegisterKey(*it) != key) {
                    continue;
                }
                if (!IsRegisterRead(it->GetOpcode())) {
                    return false;
                }
                func(&*it);
            }
            return true;
        };
        const IR::Block* const start = set.GetParent();
        if (!scan(start, std::next(IR::Block::InstructionList::s_iterator_to(set)))) {
            return;
        }
        std::vector<const IR::Block*> pending(start->ImmSuccessors().begin(),
                                              start->ImmSuccessors().end());
        std::unordered_set<const IR::Block*> visited(pending.begin(), pending.end());
        while (!pending.empty()) {
            const IR::Block* const block = pending.back();
            pending.pop_back();
            if (!scan(block, block->begin())) {
                continue;
            }
            for (const IR::Block* succ : block->ImmSuccessors()) {
                if (visited.insert(succ).second) {
                    pending.push_back(succ);
                }
            }
        }
    }

    static bool IsMaskPassThrough(IR::Opcode op) {
        switch (op) {
        case IR::Opcode::BitwiseAnd32:
        case IR::Opcode::BitwiseAnd64:
        case IR::Opcode::BitwiseOr32:
        case IR::Opcode::BitwiseOr64:
        case IR::Opcode::BitwiseXor32:
        case IR::Opcode::BitwiseXor64:
        case IR::Opcode::BitwiseNot32:
        case IR::Opcode::BitwiseNot64:
        case IR::Opcode::PackUint2x32:
        case IR::Opcode::UnpackUint2x32:
        case IR::Opcode::CompositeExtractU32x2:
        case IR::Opcode::CompositeConstructU32x2:
        case IR::Opcode::SelectU32:
        case IR::Opcode::SelectU64:
            return true;
        default:
            return false;
        }
    }

    static bool IsRegisterWrite(IR::Opcode op) {
        return op == IR::Opcode::SetScalarRegister || op == IR::Opcode::SetVectorRegister ||
               op == IR::Opcode::SetVirtualRegister || op == IR::Opcode::SetVccLo ||
               op == IR::Opcode::SetVccHi || op == IR::Opcode::SetM0;
    }

    // Walks the values a ballot's mask flows into through lane-wise bit operations and registers,
    // calling `stop` on every user of them; returns true as soon as `stop` does. `reached` (when
    // given) receives the values walked.
    template <typename F>
    static bool WalkMask(const IR::Inst& ballot, F&& stop,
                         std::unordered_set<const IR::Inst*>* reached = nullptr) {
        std::vector<const IR::Inst*> pending{&ballot};
        std::unordered_set<const IR::Inst*> seen{&ballot};
        const auto push = [&](const IR::Inst* inst) {
            if (seen.insert(inst).second) {
                pending.push_back(inst);
            }
        };
        while (!pending.empty()) {
            const IR::Inst* const inst = pending.back();
            pending.pop_back();
            for (const IR::Use& use : inst->Uses()) {
                const IR::Inst* const user = use.user;
                const IR::Opcode op = user->GetOpcode();
                if (stop(*user)) {
                    return true;
                }
                if (IsMaskPassThrough(op)) {
                    push(user);
                } else if (IsRegisterWrite(op)) {
                    ForEachReachedRead(*user, push);
                }
            }
        }
        if (reached) {
            reached->insert(seen.begin(), seen.end());
        }
        return false;
    }

    // Every value computed from a ballot by WalkMask: masks in a subgroup's own layout when the
    // ballot stays per subgroup.
    const std::unordered_set<const IR::Inst*>& MaskValues() {
        if (!mask_values_known) {
            mask_values_known = true;
            for (IR::Block* block : program.blocks) {
                for (const IR::Inst& inst : block->Instructions()) {
                    if (inst.GetOpcode() != IR::Opcode::Ballot) {
                        continue;
                    }
                    WalkMask(inst, [](const IR::Inst&) { return false; }, &mask_values);
                }
            }
        }
        return mask_values;
    }

    // Whether a lane-wise operation combines a mask with something in the wave's layout: an
    // immediate (other than a 64-bit 0 or ~0, the same in both layouts) or a value not computed
    // from a ballot (user data, memory).
    bool MixesLayouts(const IR::Inst& inst) {
        const IR::Opcode op = inst.GetOpcode();
        const bool select = op == IR::Opcode::SelectU32 || op == IR::Opcode::SelectU64;
        const bool wide = op == IR::Opcode::BitwiseAnd64 || op == IR::Opcode::BitwiseOr64 ||
                          op == IR::Opcode::BitwiseXor64 || op == IR::Opcode::SelectU64;
        // the mask operands: not a select's condition, not the index of a half
        const size_t first = select ? 1 : 0;
        const size_t end = op == IR::Opcode::CompositeExtractU32x2 ? 1 : inst.NumArgs();
        for (size_t i = first; i < end; ++i) {
            const IR::Value arg = inst.Arg(i);
            if (arg.IsImmediate()) {
                if (!wide || (arg.U64() != 0 && arg.U64() != ~u64{0})) {
                    return true;
                }
                continue;
            }
            if (!MaskValues().contains(arg.Inst())) {
                return true;
            }
        }
        return false;
    }

    // Whether a ballot's mask is used beyond per-lane tests of the lane's own bit. A mask that
    // only goes through lane-wise bit operations with other masks and through registers into
    // InverseBallot (an EXEC save and restore, a v_cndmask on a compare result) gives every lane
    // the same bit from its own subgroup's ballot: it needs no exchange. Any other use (s_bcnt1,
    // s_ff1, a compare of the mask, a shift, memory, a constant such as an exec_hi write) reads
    // or builds the whole wave's mask.
    bool MaskNeedsWave(const IR::Inst& ballot) {
        return WalkMask(ballot, [&](const IR::Inst& user) {
            const IR::Opcode op = user.GetOpcode();
            if (op == IR::Opcode::InverseBallot || IsRegisterWrite(op)) {
                return false;
            }
            return !IsMaskPassThrough(op) || MixesLayouts(user);
        });
    }

    // A register the wave keeps once for all its lanes: an SGPR, SCC, a VCC half, M0, or a
    // virtual register holding a scalar value (MaskBlock keeps these per wave too).
    bool IsScalarRegisterAccess(const IR::Inst& inst) {
        switch (inst.GetOpcode()) {
        case IR::Opcode::SetScalarRegister:
        case IR::Opcode::GetScalarRegister:
        case IR::Opcode::SetScc:
        case IR::Opcode::GetScc:
        case IR::Opcode::SetVccLo:
        case IR::Opcode::GetVccLo:
        case IR::Opcode::SetVccHi:
        case IR::Opcode::GetVccHi:
        case IR::Opcode::SetM0:
        case IR::Opcode::GetM0:
            return true;
        case IR::Opcode::SetVirtualRegister:
        case IR::Opcode::GetVirtualRegister:
            return ScalarVirtualRegisters().contains(inst.Arg(0).VirtualReg().Key());
        default:
            return false;
        }
    }

    const std::unordered_set<u64>& ScalarVirtualRegisters() {
        if (!scalar_virtual_known) {
            scalar_virtual_known = true;
            for (IR::Block* block : program.blocks) {
                for (const IR::Inst& inst : block->Instructions()) {
                    if (inst.GetOpcode() == IR::Opcode::SetVirtualRegister &&
                        IsScalarValue(inst.Arg(1))) {
                        scalar_virtual.insert(inst.Arg(0).VirtualReg().Key());
                    }
                }
            }
        }
        return scalar_virtual;
    }

    static u64 PackedKey(const std::pair<IR::Opcode, u64>& key) {
        return key.second << 16 | u64(key.first);
    }

    // The scalar registers live at the start of each block: read on some path from there before
    // they are written (backward data flow over the control flow graph).
    const std::unordered_set<u64>& ScalarLiveIn(const IR::Block* block) {
        if (scalar_live_in.empty()) {
            struct Local {
                std::unordered_set<u64> uses; // read before written in the block
                std::unordered_set<u64> defs;
            };
            std::unordered_map<const IR::Block*, Local> local;
            for (IR::Block* b : program.blocks) {
                Local& l = local[b];
                for (const IR::Inst& inst : b->Instructions()) {
                    if (!IsScalarRegisterAccess(inst)) {
                        continue;
                    }
                    const u64 key = PackedKey(*RegisterKey(inst));
                    if (IsRegisterRead(inst.GetOpcode())) {
                        if (!l.defs.contains(key)) {
                            l.uses.insert(key);
                        }
                    } else {
                        l.defs.insert(key);
                    }
                }
                scalar_live_in[b] = l.uses;
            }
            for (bool changed = true; changed;) {
                changed = false;
                for (IR::Block* b : program.post_order_blocks) {
                    const Local& l = local[b];
                    auto& in = scalar_live_in[b];
                    for (const IR::Block* succ : b->ImmSuccessors()) {
                        for (const u64 key : scalar_live_in[succ]) {
                            if (!l.defs.contains(key) && in.insert(key).second) {
                                changed = true;
                            }
                        }
                    }
                }
            }
        }
        const auto it = scalar_live_in.find(block);
        return it != scalar_live_in.end() ? it->second : empty_set;
    }

    IR::Block* MergeOf(const Region& region) const {
        const auto& node = syntax[region.open];
        return region.type == NodeType::If ? node.data.if_node.merge : node.data.loop.merge;
    }

    // The blocks of a region, nested regions included (a loop's header too).
    std::vector<const IR::Block*> SubtreeBlocks(const Region& region) const {
        std::vector<const IR::Block*> blocks;
        for (size_t i = region.type == NodeType::Loop ? region.open - 1 : region.open;
             i < region.close; ++i) {
            if (IsBlockNode(i)) {
                blocks.push_back(BlockAt(i));
            }
        }
        return blocks;
    }

    // Whether running the region as unchanged code, taken per lane, keeps the scalar state of
    // every subgroup of a wave as GCN's wave has it. GCN runs a scope's scalar instructions for
    // the whole wave when any of its lanes runs the scope, a subgroup with no lane in the scope
    // skips them here: that is invisible when every scalar register written in a scope (the
    // region or a scope nested in it) is dead wherever the scope can be left: at its merge block,
    // and at the merge of every loop around it (a break nested in the scope leaves through
    // there). A subgroup that runs a scope runs every block of it its lanes reach, so the values
    // read in the scope are the ones its own lanes wrote.
    bool ScalarStateStaysInScope(const Region& region) {
        if (innermost.empty()) {
            for (size_t r = 0; r < regions.size(); ++r) {
                for (const size_t b : regions[r].blocks) {
                    innermost[BlockAt(b)] = int(r);
                }
            }
        }
        for (const IR::Block* block : SubtreeBlocks(region)) {
            // the exits of the block's scope: its merge, and the merges of the loops around it
            std::vector<const std::unordered_set<u64>*> exits;
            const int scope = innermost.at(block);
            exits.push_back(&ScalarLiveIn(MergeOf(regions[scope])));
            for (int r = regions[scope].parent; r >= 0; r = regions[r].parent) {
                if (regions[r].type == NodeType::Loop) {
                    exits.push_back(&ScalarLiveIn(MergeOf(regions[r])));
                }
            }
            for (const IR::Inst& inst : block->Instructions()) {
                if (!IsScalarRegisterAccess(inst) || IsRegisterRead(inst.GetOpcode())) {
                    continue;
                }
                const u64 key = PackedKey(*RegisterKey(inst));
                if (std::ranges::any_of(exits, [&](const auto* live) { return live->contains(key); })) {
                    return false;
                }
            }
        }
        return true;
    }

    // A v_readlane / v_readfirstlane whose operands are all computed before the region: the
    // same value is computed in the region's header for the lanes that enter the region. A
    // v_readlane also reads lanes outside EXEC, which keep their register: its source VGPR must
    // be known and not written in the region, or the header would read the register before the
    // region's write (the lanes in EXEC) or the SSA value of that write (the others).
    bool IsHoistable(const Region& region, const IR::Inst& inst) const {
        const IR::Opcode op = inst.GetOpcode();
        if (op != IR::Opcode::ReadLane && op != IR::Opcode::ReadFirstLane) {
            return false;
        }
        const auto blocks = SubtreeBlocks(region);
        if (op == IR::Opcode::ReadLane) {
            const auto vgpr = LaneSourceVgpr(inst);
            if (!vgpr) {
                return false;
            }
            for (const IR::Block* block : blocks) {
                for (const IR::Inst& other : block->Instructions()) {
                    if (other.GetOpcode() == IR::Opcode::SetVectorRegister &&
                        other.Arg(0).VectorReg() == *vgpr) {
                        return false;
                    }
                }
            }
        }
        for (size_t i = 0; i < inst.NumArgs(); ++i) {
            const IR::Value arg = inst.Arg(i);
            if (!arg.IsImmediate() && std::ranges::contains(blocks, arg.Inst()->GetParent())) {
                return false;
            }
        }
        return true;
    }

    // Whether the region (nested regions included) accesses workgroup memory.
    bool HasSharedAccess(const Region& region) const {
        for (const IR::Block* block : SubtreeBlocks(region)) {
            for (const IR::Inst& inst : block->Instructions()) {
                if (IsSharedAccess(inst.GetOpcode())) {
                    return true;
                }
            }
        }
        return false;
    }

    // A divergent if that needs the whole wave only for lane reads that can be computed in its
    // header, and that can then run per lane (ScalarStateStaysInScope). Not when it accesses
    // workgroup memory in a workgroup of several waves: there LDS order alone does not make code
    // predicated (lds_needs_wave), but predicated code keeps the two halves of a wave in GCN's
    // lock-step LDS order, and running the region per lane would lose that.
    bool CanHoistLaneReads(const Region& region) {
        return region.type == NodeType::If && region.divergent && region.cond == CondKind::Lane &&
               region.needs_wave && !region.child_needs_wave &&
               std::ranges::all_of(region.wave_insts,
                                   [&](const IR::Inst* inst) { return IsHoistable(region, *inst); }) &&
               (lds_needs_wave || !HasSharedAccess(region)) && ScalarStateStaysInScope(region);
    }

    bool BuildRegions() {
        std::vector<int> stack;
        bool has_regions = false;
        for (size_t i = 0; i < syntax.size(); ++i) {
            const IR::AbstractSyntaxNode& node = syntax[i];
            const int current = stack.empty() ? -1 : stack.back();
            switch (node.type) {
            case NodeType::Block:
                if (current >= 0) {
                    regions[current].blocks.push_back(i);
                }
                break;
            case NodeType::If: {
                const bool divergent = !uniformity.IsUniform(Unwrapped(node.data.if_node.cond));
                regions.push_back({NodeType::If, i, 0, current, divergent});
                stack.push_back(int(regions.size() - 1));
                has_regions = true;
                break;
            }
            case NodeType::EndIf:
                if (current < 0 || regions[current].type != NodeType::If ||
                    syntax[regions[current].open].data.if_node.merge != node.data.end_if.merge) {
                    return false;
                }
                regions[current].close = i;
                stack.pop_back();
                break;
            case NodeType::Loop: {
                // the loop header is the Block node before the Loop node
                regions.push_back({NodeType::Loop, i, 0, current, true});
                if (i > 0 && IsBlockNode(i - 1)) {
                    if (current >= 0) {
                        auto& parent_blocks = regions[current].blocks;
                        if (!parent_blocks.empty() && parent_blocks.back() == i - 1) {
                            parent_blocks.pop_back();
                        }
                    }
                    regions.back().blocks.push_back(i - 1);
                }
                stack.push_back(int(regions.size() - 1));
                has_regions = true;
                break;
            }
            case NodeType::Repeat:
                if (current < 0 || regions[current].type != NodeType::Loop ||
                    syntax[regions[current].open].data.loop.merge != node.data.repeat.merge) {
                    return false;
                }
                regions[current].close = i;
                stack.pop_back();
                break;
            case NodeType::Break:
                if (current < 0) {
                    return false;
                }
                regions[current].breaks.push_back(i);
                break;
            case NodeType::Return:
            case NodeType::Unreachable:
                for (int r = current; r >= 0; r = regions[r].parent) {
                    if (regions[r].divergent) {
                        lanes_may_end = true;
                    }
                }
                if (current >= 0) {
                    ends.push_back({i, current});
                    for (int r = current; r >= 0; r = regions[r].parent) {
                        regions[r].exits = true;
                    }
                }
                break;
            }
        }
        return has_regions && stack.empty();
    }

    CondKind Kind(const IR::U1& cond) {
        const IR::Value value = Unwrapped(cond);
        if (uniformity.IsUniform(value)) {
            return CondKind::Workgroup;
        }
        WaveUniformity& wave_level = late_wave_uniformity ? *late_wave_uniformity : wave_uniformity;
        return wave_level.IsUniform(value) ? CondKind::Wave : CondKind::Lane;
    }

    // The goto variables (the structurizer's control-flow state) that hold one value per wave:
    // every write of one is of a wave-uniform value, in predicated code or outside every region.
    // MaskBlock masks a write of a wave-uniform value to a goto variable per wave: it is a
    // branch, and GCN branches per wave (a lane outside EXEC follows its wave). A goto variable
    // written in a per-lane branch, or with a per-lane value (an EXEC test), stays per lane.
    void FindWaveGotos() {
        std::unordered_map<u32, bool> ok;
        for (size_t r = 0; r < regions.size(); ++r) {
            for (const size_t b : regions[r].blocks) {
                innermost.try_emplace(BlockAt(b), int(r));
            }
        }
        for (IR::Block* block : program.blocks) {
            const auto it = innermost.find(block);
            const bool wave_masked =
                it == innermost.end() || regions[it->second].mode == Mode::Predicated ||
                regions[it->second].mode == Mode::MaskedBranch;
            for (const IR::Inst& inst : block->Instructions()) {
                if (inst.GetOpcode() != IR::Opcode::SetGotoVariable) {
                    continue;
                }
                const u32 id = inst.Arg(0).U32();
                const bool uniform = wave_masked && wave_uniformity.IsUniform(inst.Arg(1));
                const auto [entry, inserted] = ok.try_emplace(id, uniform);
                if (!inserted) {
                    entry->second = entry->second && uniform;
                }
            }
        }
        for (const auto& [id, uniform] : ok) {
            if (uniform) {
                wave_gotos.insert(id);
            }
        }
        late_wave_uniformity.emplace(program.info, true, &wave_gotos);
    }

    // Marks the regions to predicate; false when there are none (and no lane reads to hoist).
    bool AssignModes() {
        // A break nested in ifs of its loop: the loop is left by it, the ifs are left through it.
        for (size_t r = 0; r < regions.size(); ++r) {
            for (const size_t b : regions[r].breaks) {
                const int target = BreakTarget(b, int(r));
                if (target < 0 || target == int(r)) {
                    continue;
                }
                regions[target].nested_breaks = true;
                for (int i = int(r); i != target; i = regions[i].parent) {
                    regions[i].exits = true;
                }
            }
        }
        for (Region& region : regions) {
            if (region.type == NodeType::If) {
                region.cond = Kind(syntax[region.open].data.if_node.cond);
            }
            for (const size_t b : region.blocks) {
                for (IR::Inst& inst : BlockAt(b)->Instructions()) {
                    if (NeedsWave(inst)) {
                        region.needs_wave = true;
                        region.wave_insts.push_back(&inst);
                    }
                    region.scalar_writes |= IsScalarWrite(inst.GetOpcode());
                }
            }
        }
        // A nested region follows its parent in `regions`: the regions nested in one are done
        // when it is reached here. An if needs a barrier in predicated mode when something nested
        // in it does (a loop or code that needs the whole wave).
        for (size_t r = regions.size(); r-- > 0;) {
            Region& region = regions[r];
            if (CanHoistLaneReads(region)) {
                region.hoisted = std::move(region.wave_insts);
                region.wave_insts.clear();
                region.needs_wave = false;
            }
            // an if left by a break needs the whole wave there (predicated, see PredicateBreak)
            region.barrier_free = region.type == NodeType::If && !region.needs_wave &&
                                  region.children_barrier_free && !region.exits;
            if (region.parent < 0) {
                continue;
            }
            // the header of a region with hoisted lane reads is a block of its parent
            const bool header_needs_wave = !region.hoisted.empty();
            Region& parent = regions[region.parent];
            parent.needs_wave |= region.needs_wave || header_needs_wave;
            parent.child_needs_wave |= region.needs_wave || header_needs_wave;
            parent.scalar_writes |= region.scalar_writes;
            parent.children_barrier_free &= region.barrier_free;
        }
        bool any = false;
        for (Region& region : regions) {
            // with a return in divergent code the whole program is predicated (PredicateEnd)
            const Mode root = lanes_may_end ? Mode::Predicated : Mode::Unchanged;
            const Mode parent = region.parent < 0 ? root : regions[region.parent].mode;
            const bool branch = region.type == NodeType::If && region.barrier_free;
            const bool plain = region.cond == CondKind::Lane &&
                               (!region.scalar_writes || ScalarStateStaysInScope(region));
            switch (parent) {
            case Mode::Unchanged:
                region.mode = region.divergent && region.needs_wave ? Mode::Predicated
                                                                    : Mode::Unchanged;
                break;
            case Mode::Predicated:
                if (!branch) {
                    region.mode = Mode::Predicated;
                } else {
                    region.mode = plain ? Mode::RealBranch : Mode::MaskedBranch;
                }
                break;
            case Mode::MaskedBranch:
                ASSERT(branch); // a masked branch is barrier free, so is everything in it
                region.mode = plain ? Mode::RealBranch : Mode::MaskedBranch;
                break;
            case Mode::RealBranch:
            case Mode::InBranch:
                region.mode = Mode::InBranch;
                break;
            }
            // a region with hoisted lane reads needs its header transformed too
            any |= region.mode == Mode::Predicated || !region.hoisted.empty();
        }
        return any;
    }

    // The loop a Break leaves.
    int BreakTarget(size_t break_index, int innermost) const {
        const IR::Block* const merge = syntax[break_index].data.break_node.merge;
        for (int r = innermost; r >= 0; r = regions[r].parent) {
            if (regions[r].type == NodeType::Loop && syntax[regions[r].open].data.loop.merge == merge) {
                return r;
            }
        }
        return -1;
    }

    std::vector<IR::Block*> LoopBlocks(const Region& loop) const {
        std::vector<IR::Block*> blocks;
        for (size_t i = loop.open - 1; i < loop.close; ++i) {
            if (IsBlockNode(i)) {
                blocks.push_back(BlockAt(i));
            }
        }
        return blocks;
    }

    // Why the shader cannot be predicated, or nullptr.
    const char* Unsupported() {
        if (lanes_may_end) {
            for (const auto& [node, region] : ends) {
                if (!IsBlockNode(node - 1)) {
                    return "a return in divergent control flow without a block before it";
                }
                const NodeType next = node + 1 < syntax.size() ? syntax[node + 1].type : NodeType::Return;
                if (next != NodeType::Block && next != NodeType::EndIf) {
                    return "a return in divergent control flow before a break or loop end";
                }
                for (int r = region; r >= 0; r = regions[r].parent) {
                    if (regions[r].mode != Mode::Predicated) {
                        return "a return in a branch of predicated code";
                    }
                }
            }
        }
        for (size_t r = 0; r < regions.size(); ++r) {
            const Region& region = regions[r];
            for (const size_t b : region.breaks) {
                const int target = BreakTarget(b, int(r));
                if (target < 0) {
                    return "a break without its loop";
                }
                if (regions[target].mode != Mode::Predicated) {
                    continue;
                }
                if (!IsBlockNode(b - 1)) {
                    return "a break of a predicated loop without a block before it";
                }
                for (int i = int(r); i != target; i = regions[i].parent) {
                    if (regions[i].mode != Mode::Predicated) {
                        return "a break of a predicated loop in a branch";
                    }
                }
            }
            if (region.mode == Mode::Predicated || region.mode == Mode::RealBranch ||
                region.mode == Mode::MaskedBranch) {
                if (!IsBlockNode(region.open - 1)) {
                    return "an if or loop without a header block";
                }
            }
            if (region.mode != Mode::Predicated && region.mode != Mode::MaskedBranch) {
                continue;
            }
            if (region.type == NodeType::Loop &&
                (region.open < 2 || !IsBlockNode(region.open - 2) || !IsBlockNode(region.close - 1))) {
                return "a loop without a preheader or continue block";
            }
            for (const size_t b : region.blocks) {
                for (const IR::Inst& inst : BlockAt(b)->Instructions()) {
                    const IR::Opcode op = inst.GetOpcode();
                    if (IsUnsupportedSideEffect(op)) {
                        return "a side effect that cannot be masked";
                    }
                    if (const auto operand = WrittenOperand(op);
                        operand && !IsSelectable(inst.Arg(*operand).Type())) {
                        LOG_WARNING(Render_Recompiler, "wave64: {} of a {} value", op, inst.Arg(*operand).Type());
                        return "a register of a type that cannot be selected";
                    }
                    if (inst.MayHaveSideEffects() && !IsUnwrappedSideEffect(op) &&
                        inst.Type() != IR::Type::Void && inst.Type() != IR::Type::U32 &&
                        inst.Type() != IR::Type::U64 && inst.Type() != IR::Type::F32) {
                        return "a side effect with a value of an unsupported type";
                    }
                }
            }
            if (region.type == NodeType::Loop) {
                const auto blocks = LoopBlocks(region);
                for (IR::Block* block : blocks) {
                    for (const IR::Inst& inst : block->Instructions()) {
                        if (inst.Type() == IR::Type::Void || IsSelectable(inst.Type())) {
                            continue;
                        }
                        for (const IR::Use& use : inst.Uses()) {
                            if (!std::ranges::contains(blocks, use.user->GetParent())) {
                                return "a loop result of a type that cannot be selected";
                            }
                        }
                    }
                }
            }
        }
        return nullptr;
    }

    // A value of a predicated loop used after it: through a register written where P is true.
    void KeepLoopResults(const Region& loop) {
        const auto blocks = LoopBlocks(loop);
        for (IR::Block* block : blocks) {
            for (auto it = block->begin(); it != block->end(); ++it) {
                IR::Inst& inst = *it;
                if (inst.Type() == IR::Type::Void || !inst.HasUses()) {
                    continue;
                }
                std::vector<IR::Use> outside;
                for (const IR::Use& use : inst.Uses()) {
                    if (!std::ranges::contains(blocks, use.user->GetParent())) {
                        outside.push_back(use);
                    }
                }
                if (outside.empty()) {
                    continue;
                }
                const IR::VirtualReg reg{program.next_reg_index++, inst.Type()};
                IR::IREmitter{*block, std::next(it)}.SetVirtualReg(reg, IR::Value{&inst});
                for (const auto& [user, operand] : outside) {
                    IR::IREmitter ir{*user->GetParent(), IR::Block::InstructionList::s_iterator_to(*user)};
                    user->SetArg(operand, ir.GetVirtualReg(reg));
                }
            }
        }
    }

    IR::U1 Predicate(IR::IREmitter& ir) const {
        return IR::U1{ir.GetVirtualReg(pred)};
    }

    IR::U1 WavePredicate(IR::IREmitter& ir) const {
        return IR::U1{ir.GetVirtualReg(wave)};
    }

    // `predicated`: a block of a Predicated region (every invocation runs it), not of a masked
    // branch.
    void MaskBlock(IR::Block* block, bool predicated) {
        std::vector<IR::Inst*> insts;
        for (IR::Inst& inst : block->Instructions()) {
            insts.push_back(&inst);
        }
        for (IR::Inst* inst : insts) {
            const IR::Opcode op = inst->GetOpcode();
            const auto at = IR::Block::InstructionList::s_iterator_to(*inst);
            if (const auto operand = WrittenOperand(op)) {
                const IR::Value value = inst->Arg(*operand);
                IR::IREmitter ir{*block, at};
                if (const IR::Inst* source = value.TryInst();
                    source && source->GetOpcode() == IR::Opcode::WriteLane) {
                    // v_writelane writes its lane whatever EXEC is, but only in a wave that runs
                    // it (not in one that skipped the scope or left the loop)
                    inst->SetArg(*operand, SelectValue(ir, WavePredicate(ir), value, ReadOld(ir, *inst, value)));
                    continue;
                }
                // scalar state: kept per wave (any lane of the wave runs scalar instructions)
                const bool scalar = IsScalarWrite(op) ||
                                    (op == IR::Opcode::SetVirtualRegister && IsScalarValue(value)) ||
                                    (op == IR::Opcode::SetGotoVariable &&
                                     wave_uniformity.IsUniform(value));
                const IR::U1 mask = scalar ? WavePredicate(ir) : Predicate(ir);
                inst->SetArg(*operand, SelectValue(ir, mask, value, ReadOld(ir, *inst, value)));
                continue;
            }
            if (IsLaneRead(op)) {
                // a lane outside EXEC keeps its register's old value: read the register, whose
                // writes are masked
                if (const auto vgpr = LaneSourceVgpr(*inst)) {
                    IR::IREmitter ir{*block, at};
                    inst->SetArg(0, ir.GetVectorReg(*vgpr));
                }
                continue;
            }
            if (IsLoadBuffer(op) && inst->Arg(0).IsImmediate() &&
                inst->Arg(0).U32() < program.info.buffers.size() &&
                program.info.buffers[inst->Arg(0).U32()].buffer_type == BufferType::SharedMemory) {
                // workgroup memory in a storage buffer has no bounds check: a lane outside P
                // reads offset 0 instead of an address it computed from stale registers
                IR::IREmitter ir{*block, at};
                inst->SetArg(1, ir.Select(Predicate(ir), IR::U32{inst->Arg(1)}, ir.Imm32(0u)));
                continue;
            }
            switch (op) {
            case IR::Opcode::Ballot:
            case IR::Opcode::GuestReportStatus: {
                // a lane outside the predicate gives 0 (GCN: a lane outside EXEC)
                IR::IREmitter ir{*block, at};
                inst->SetArg(0, ir.LogicalAnd(IR::U1{inst->Arg(0)}, Predicate(ir)));
                continue;
            }
            case IR::Opcode::ReadFirstLane:
            case IR::Opcode::LoadGuestU32:
            case IR::Opcode::StoreGuestU32: {
                IR::IREmitter ir{*block, at};
                inst->SetArg(1, ir.LogicalAnd(IR::U1{inst->Arg(1)}, Predicate(ir)));
                continue;
            }
            default:
                break;
            }
            if (!inst->MayHaveSideEffects() || IsUnwrappedSideEffect(op)) {
                continue;
            }
            if (predicated && IsSharedWrite(*inst)) {
                WriteSharedInHalves(block, inst);
                continue;
            }
            IR::IREmitter before{*block, at};
            before.PredicatedBegin(Predicate(before));
            IR::IREmitter after{*block, std::next(at)};
            if (inst->Type() == IR::Type::Void) {
                after.PredicatedEnd();
                continue;
            }
            const std::vector<IR::Use> uses(inst->Uses().begin(), inst->Uses().end());
            const IR::Value result = after.PredicatedEnd(IR::Value{inst});
            for (const auto& [user, operand] : uses) {
                user->SetArg(operand, result);
            }
        }
    }

    // An LDS write or atomic is one step of the wave: the lower subgroup of each wave runs it,
    // then (after a barrier) the upper one, so the halves never interleave: a 64-bit write keeps
    // its two dwords together, and where lanes of both halves write the same address the result
    // is the same in every run (the upper half's write lands last).
    void WriteSharedInHalves(IR::Block* block, IR::Inst* inst) {
        const auto at = IR::Block::InstructionList::s_iterator_to(*inst);
        const auto next = std::next(at);
        IR::IREmitter before{*block, at};
        const IR::U1 p = Predicate(before);
        const IR::U1 lower = before.ILessThan(ComputeWaveLane(before), before.Imm32(32u), false);
        before.PredicatedBegin(before.LogicalAnd(p, lower));
        const bool has_value = inst->Type() != IR::Type::Void;
        const std::vector<IR::Use> uses(inst->Uses().begin(), inst->Uses().end());
        IR::IREmitter after{*block, next};
        IR::Value lower_value;
        if (has_value) {
            lower_value = after.PredicatedEnd(IR::Value{inst});
        } else {
            after.PredicatedEnd();
        }
        after.LdsBarrier();
        after.PredicatedBegin(after.LogicalAnd(p, after.LogicalNot(lower)));
        IR::Inst* const upper = &*block->PrependNewInst(next, *inst);
        if (!has_value) {
            after.PredicatedEnd();
            return;
        }
        const IR::Value upper_value = after.PredicatedEnd(IR::Value{upper});
        const IR::Value result = after.Select(lower, lower_value, upper_value);
        for (const auto& [user, operand] : uses) {
            user->SetArg(operand, result);
        }
    }

    // The predicates of a region entered where `cond` holds: P' = P && cond and the wave's W'.
    // Returns W' and sets `any_lane` (when given) to "some invocation of the workgroup runs the
    // region", emitted before the wave exchange, which LowerWaveOpsPass merges into it.
    // A scalar condition (the same in the wave) keeps or drops the whole wave: W' = W && cond.
    // For a per-lane condition `exact` gives W' = "some lane of the wave has P'" (WaveAny): a
    // wave with no lane in an EXEC scope skips its scalar code and leaves a loop whose lanes all
    // left it, as on GCN. Otherwise (a masked branch, which has no barrier for an exchange) W'
    // = W: the scope's scalar code runs for every wave that runs the code around it, as on GCN
    // when a wave runs the scope with EXEC 0.
    IR::U1 Enter(IR::IREmitter& ir, CondKind kind, const IR::U1& p, const IR::U1& w,
                 const IR::U1& cond, IR::U1& p_inside, IR::U1* any_lane, bool exact) {
        p_inside = ir.LogicalAnd(p, cond);
        if (kind != CondKind::Lane) {
            const IR::U1 w_inside = ir.LogicalAnd(w, cond);
            if (any_lane) {
                *any_lane = kind == CondKind::Workgroup ? cond : ir.WorkgroupAny(w_inside);
            }
            return w_inside;
        }
        if (any_lane) {
            *any_lane = ir.WorkgroupAny(p_inside);
        }
        return exact ? ir.WaveAny(p_inside) : w;
    }

    void PredicateIf(const Region& region) {
        auto& node = syntax[region.open];
        IR::IREmitter ir{*BlockAt(region.open - 1)};
        const IR::U1 p = Predicate(ir);
        const IR::U1 cond = node.data.if_node.cond;
        if (region.mode == Mode::RealBranch) {
            node.data.if_node.cond = ir.ConditionRef(ir.LogicalAnd(p, cond));
            program.branch_merges.insert(node.data.if_node.merge);
            return;
        }
        const IR::U1 w = WavePredicate(ir);
        IR::U1 p_inside;
        IR::U1 any;
        const bool predicated = region.mode == Mode::Predicated;
        // exact for a predicated if (merged into its WorkgroupAny exchange, no extra barrier): a
        // wave with no lane in the scope skips its scalar code, as GCN's s_cbranch_execz does
        const IR::U1 w_inside =
            Enter(ir, region.cond, p, w, cond, p_inside, predicated ? &any : nullptr, predicated);
        ir.SetVirtualReg(pred, p_inside);
        ir.SetVirtualReg(wave, w_inside);
        if (predicated) {
            if (region.cond != CondKind::Workgroup) {
                node.data.if_node.cond = ir.ConditionRef(any);
            }
        } else {
            // a real branch per wave: no barrier inside (Region::barrier_free)
            node.data.if_node.cond = ir.ConditionRef(w_inside);
        }
        IR::Block* const merge = node.data.if_node.merge;
        IR::IREmitter restore{*merge, merge->begin()};
        restore.SetVirtualReg(pred, StillInLoops(restore, region.parent, p, false));
        restore.SetVirtualReg(wave, StillInLoops(restore, region.parent, w, true));
        if (predicated) {
            program.predicated_merges.insert(merge);
        } else {
            program.branch_merges.insert(merge);
        }
    }

    // `value` (a predicate restored at a merge) and "the lane, or with `wave_level` its wave, has
    // not left a predicated loop around `from` through a nested break, nor ended": a lane that
    // left a loop in an if stays out of it after the if's merge, until the loop's merge, and a
    // lane that ended stays out of everything (PredicateEnd).
    IR::U1 StillInLoops(IR::IREmitter& ir, int from, IR::U1 value, bool wave_level) const {
        if (lanes_may_end) {
            value = ir.LogicalAnd(value, IR::U1{ir.GetVirtualReg(wave_level ? running_wave : running)});
        }
        for (int r = from; r >= 0; r = regions[r].parent) {
            const Region& loop = regions[r];
            if (loop.type == NodeType::Loop && loop.mode == Mode::Predicated && loop.nested_breaks) {
                value = ir.LogicalAnd(value, IR::U1{ir.GetVirtualReg(wave_level ? loop.alive_wave : loop.alive)});
            }
        }
        return value;
    }

    // A break nested in predicated ifs of a predicated loop. Every invocation keeps running the
    // loop until none is in it any more, so the break no longer branches: the lanes that take it
    // leave the loop's predicate (Region::alive) and P, and stay out of both until the loop's
    // merge (StillInLoops at every merge in between). GCN branches per wave: a break the wave
    // reaches takes its lanes out of the loop whatever EXEC is, as s_branch does; a lane whose
    // per-lane condition (from the structurizer's per-lane goto variables) does not hold stays.
    void PredicateBreak(size_t b, const Region& loop) {
        auto& node = syntax[b];
        IR::IREmitter ir{*BlockAt(b - 1)};
        const IR::U1 cond = node.data.break_node.cond;
        const IR::U1 p = Predicate(ir);
        const IR::U1 w = WavePredicate(ir);
        // the wave that reaches the break branches: every lane of it whose condition holds
        // leaves (all of them for a wave-uniform condition)
        const IR::U1 stays = ir.LogicalNot(ir.LogicalAnd(w, cond));
        const IR::U1 alive = ir.LogicalAnd(IR::U1{ir.GetVirtualReg(loop.alive)}, stays);
        const IR::U1 alive_wave{ir.GetVirtualReg(loop.alive_wave)};
        const IR::U1 p_stay = ir.LogicalAnd(p, stays);
        ir.SetVirtualReg(loop.alive, alive);
        ir.SetVirtualReg(pred, p_stay);
        if (Kind(cond) == CondKind::Lane) {
            // the wave stays in the loop while one of its lanes does
            ir.SetVirtualReg(loop.alive_wave, ir.LogicalAnd(alive_wave, ir.WaveAny(alive)));
            ir.SetVirtualReg(wave, ir.WaveAny(p_stay));
        } else {
            ir.SetVirtualReg(loop.alive_wave, ir.LogicalAnd(alive_wave, stays));
            ir.SetVirtualReg(wave, ir.LogicalAnd(w, stays));
        }
        node.data.break_node.cond = ir.ConditionRef(ir.Imm1(false));
    }

    // A return (s_endpgm) or an unreachable end in divergent code. Every invocation keeps running
    // the program, which is predicated as a whole, so that no barrier after it waits for an
    // invocation that ended: the end clears the predicates of the waves that reach it (GCN ends
    // the whole wave, whatever EXEC is) for good (`running`, ANDed in at every merge). The node
    // itself is removed afterwards (RemoveEnds).
    void PredicateEnd(size_t node) {
        IR::IREmitter ir{*BlockAt(node - 1)};
        const IR::U1 stays = ir.LogicalNot(WavePredicate(ir));
        ir.SetVirtualReg(running, ir.LogicalAnd(IR::U1{ir.GetVirtualReg(running)}, stays));
        ir.SetVirtualReg(running_wave, ir.LogicalAnd(IR::U1{ir.GetVirtualReg(running_wave)}, stays));
        ir.SetVirtualReg(pred, ir.LogicalAnd(Predicate(ir), stays));
        ir.SetVirtualReg(wave, ir.Imm1(false));
    }

    // The block before a removed end now falls through to the next node: a block, or the merge of
    // the if the end was in (Unsupported allows nothing else).
    void RemoveEnds() {
        for (auto it = ends.rbegin(); it != ends.rend(); ++it) {
            const size_t node = it->first;
            IR::Block* const block = BlockAt(node - 1);
            const auto& next = syntax[node + 1];
            block->AddBranch(next.type == NodeType::Block ? next.data.block : next.data.end_if.merge);
            syntax.erase(syntax.begin() + std::ptrdiff_t(node));
        }
        program.post_order_blocks = IR::PostOrder(syntax.front().data.block);
    }

    void PredicateLoop(const Region& region) {
        IR::IREmitter pre{*BlockAt(region.open - 2)};
        const IR::U1 p_entry = Predicate(pre);
        const IR::U1 w_entry = WavePredicate(pre);
        if (region.nested_breaks) {
            pre.SetVirtualReg(region.alive, IR::Value{true});
            pre.SetVirtualReg(region.alive_wave, IR::Value{true});
        }
        // The lanes (and waves) that stay where `cond` holds; the new loop condition: some
        // invocation of the workgroup still runs the loop.
        const auto stay = [&](IR::IREmitter& ir, const IR::U1& cond,
                              bool negate) -> std::optional<IR::U1> {
            const CondKind kind = Kind(cond);
            IR::U1 p_stay;
            IR::U1 any;
            const IR::U1 w_stay = Enter(ir, kind, Predicate(ir), WavePredicate(ir),
                                        negate ? ir.LogicalNot(cond) : cond, p_stay, &any, true);
            ir.SetVirtualReg(pred, p_stay);
            ir.SetVirtualReg(wave, w_stay);
            return kind == CondKind::Workgroup ? std::nullopt : std::optional{any};
        };
        for (const size_t b : region.breaks) {
            auto& node = syntax[b];
            IR::IREmitter ir{*BlockAt(b - 1)};
            const IR::U1 cond = node.data.break_node.cond;
            if (const auto any = stay(ir, cond, true)) {
                node.data.break_node.cond = ir.ConditionRef(ir.LogicalNot(*any));
            }
        }
        auto& repeat = syntax[region.close];
        IR::IREmitter ir{*BlockAt(region.close - 1)};
        if (const auto any = stay(ir, repeat.data.repeat.cond, false)) {
            repeat.data.repeat.cond = ir.ConditionRef(*any);
        } else if (region.nested_breaks || region.exits) {
            // a workgroup-uniform condition: the loop is also left when every invocation left it
            // (or a loop around it) through a nested break (PredicateBreak), which no longer
            // branches
            repeat.data.repeat.cond = ir.ConditionRef(ir.WorkgroupAny(Predicate(ir)));
        }
        IR::Block* const merge = syntax[region.open].data.loop.merge;
        IR::IREmitter restore{*merge, merge->begin()};
        restore.SetVirtualReg(pred, StillInLoops(restore, region.parent, p_entry, false));
        restore.SetVirtualReg(wave, StillInLoops(restore, region.parent, w_entry, true));
        program.predicated_merges.insert(merge);
    }

    // Ballots in predicated code whose masks need no exchange (EXEC save and restore, compare
    // masks tested per lane): left to each subgroup, in its own layout. Not when a mask that
    // does need the wave's layout can meet them (through the same registers or operations).
    void FindSubgroupBallots() {
        std::vector<const IR::Inst*> candidates;
        std::unordered_set<const IR::Inst*> wave_masks;
        // ballots outside predicated code: LowerWaveOpsPass exchanges those in uniform blocks,
        // so they count as the wave's layout (all of them, the others too: conservative)
        std::unordered_set<const IR::Block*> predicated;
        for (const Region& region : regions) {
            if (region.mode == Mode::Predicated) {
                for (const size_t b : region.blocks) {
                    predicated.insert(BlockAt(b));
                }
            }
        }
        for (IR::Block* block : program.blocks) {
            if (predicated.contains(block)) {
                continue;
            }
            for (const IR::Inst& inst : block->Instructions()) {
                if (inst.GetOpcode() == IR::Opcode::Ballot) {
                    WalkMask(inst, [](const IR::Inst&) { return false; }, &wave_masks);
                }
            }
        }
        for (const Region& region : regions) {
            if (region.mode != Mode::Predicated) {
                continue;
            }
            for (const size_t b : region.blocks) {
                for (const IR::Inst& inst : BlockAt(b)->Instructions()) {
                    if (inst.GetOpcode() != IR::Opcode::Ballot) {
                        continue;
                    }
                    if (MaskNeedsWave(inst)) {
                        WalkMask(inst, [](const IR::Inst&) { return false; }, &wave_masks);
                    } else {
                        candidates.push_back(&inst);
                    }
                }
            }
        }
        for (const IR::Inst* ballot : candidates) {
            std::unordered_set<const IR::Inst*> walked;
            WalkMask(*ballot, [](const IR::Inst&) { return false; }, &walked);
            if (std::ranges::none_of(walked, [&](const IR::Inst* v) { return wave_masks.contains(v); })) {
                program.subgroup_ballots.insert(ballot);
            }
        }
    }

    // The lane reads of a region that need the whole wave, computed at the end of its header:
    // v_readfirstlane with EXEC limited to the lanes that enter the region (P && cond),
    // v_readlane as it is (it reads its lane whatever EXEC is). The header is run by every
    // invocation (a block of predicated code or of uniform code), so the reads are exchanged
    // between the subgroups there (LowerWaveOpsPass).
    void HoistLaneReads(const Region& region) {
        IR::IREmitter ir{*BlockAt(region.open - 1)};
        const IR::U1 enters = ir.LogicalAnd(Predicate(ir), syntax[region.open].data.if_node.cond);
        for (IR::Inst* inst : region.hoisted) {
            IR::U32 value;
            if (inst->GetOpcode() == IR::Opcode::ReadFirstLane) {
                value = ir.ReadFirstLane(IR::U32{inst->Arg(0)},
                                         ir.LogicalAnd(IR::U1{inst->Arg(1)}, enters));
            } else {
                value = ir.ReadLane(IR::U32{inst->Arg(0)}, IR::U32{inst->Arg(1)});
                if (const auto vgpr = LaneSourceVgpr(*inst)) {
                    SetLaneSourceVgpr(value, *vgpr);
                }
            }
            inst->ReplaceUsesWithAndRemove(value);
        }
    }

    void Transform() {
        pred = IR::VirtualReg{program.next_reg_index++, IR::Type::U1};
        wave = IR::VirtualReg{program.next_reg_index++, IR::Type::U1};
        FindWaveGotos();
        if (lanes_may_end) {
            // the code outside every region is predicated too (masked before the predicates'
            // own writes exist)
            for (size_t i = 0; i < syntax.size(); ++i) {
                if (IsBlockNode(i) && !innermost.contains(BlockAt(i))) {
                    MaskBlock(BlockAt(i), true);
                    program.predicated_blocks.insert(BlockAt(i));
                }
            }
            running = IR::VirtualReg{program.next_reg_index++, IR::Type::U1};
            running_wave = IR::VirtualReg{program.next_reg_index++, IR::Type::U1};
        }
        IR::Block* const entry = BlockAt(0);
        IR::IREmitter init{*entry, entry->begin()};
        init.SetVirtualReg(pred, IR::Value{true});
        init.SetVirtualReg(wave, IR::Value{true});
        if (lanes_may_end) {
            init.SetVirtualReg(running, IR::Value{true});
            init.SetVirtualReg(running_wave, IR::Value{true});
        }

        for (Region& region : regions) {
            if (region.type == NodeType::Loop && region.mode == Mode::Predicated && region.nested_breaks) {
                region.alive = IR::VirtualReg{program.next_reg_index++, IR::Type::U1};
                region.alive_wave = IR::VirtualReg{program.next_reg_index++, IR::Type::U1};
            }
        }
        FindSubgroupBallots();
        for (const Region& region : regions) {
            if (!region.hoisted.empty()) {
                HoistLaneReads(region);
            }
        }
        size_t num_predicated = 0;
        for (const Region& region : regions) {
            if (region.mode == Mode::Predicated && region.type == NodeType::Loop) {
                KeepLoopResults(region);
            }
        }
        for (const Region& region : regions) {
            const bool predicated = region.mode == Mode::Predicated;
            if (!predicated && region.mode != Mode::MaskedBranch) {
                continue;
            }
            num_predicated += predicated;
            for (const size_t b : region.blocks) {
                MaskBlock(BlockAt(b), predicated);
                if (predicated) {
                    program.predicated_blocks.insert(BlockAt(b));
                }
            }
        }
        // after the masking: the predicates' own updates are not masked
        for (const Region& region : regions) {
            if (region.type == NodeType::If &&
                (region.mode == Mode::Predicated || region.mode == Mode::MaskedBranch ||
                 region.mode == Mode::RealBranch)) {
                PredicateIf(region);
            } else if (region.type == NodeType::Loop && region.mode == Mode::Predicated) {
                PredicateLoop(region);
            }
        }
        for (size_t r = 0; r < regions.size(); ++r) {
            for (const size_t b : regions[r].breaks) {
                const int target = BreakTarget(b, int(r));
                if (target != int(r) && regions[target].mode == Mode::Predicated) {
                    PredicateBreak(b, regions[target]);
                }
            }
        }
        if (lanes_may_end) {
            for (const auto& [node, region] : ends) {
                PredicateEnd(node);
            }
            RemoveEnds();
        }
        LOG_INFO(Render_Recompiler, "wave64: {} EXEC-divergent ifs and loops run predicated",
                 num_predicated);
    }

    IR::Program& program;
    IR::AbstractSyntaxList& syntax;
    WaveUniformity uniformity;      // the same in the whole workgroup
    WaveUniformity wave_uniformity; // the same in every lane of each wave
    const bool lds_needs_wave;
    const bool force_fallback; // GCN_COMPILE_WAVE64_FALLBACK (tests)
    std::vector<Region> regions;
    std::unordered_set<const IR::Inst*> mask_values;
    bool mask_values_known = false;
    std::unordered_set<u64> scalar_virtual;
    bool scalar_virtual_known = false;
    std::unordered_map<const IR::Block*, std::unordered_set<u64>> scalar_live_in;
    const std::unordered_set<u64> empty_set;
    std::unordered_map<const IR::Block*, int> innermost; // the region a block is directly in
    std::unordered_set<u32> wave_gotos;                   // FindWaveGotos
    std::optional<WaveUniformity> late_wave_uniformity;   // wave_uniformity knowing wave_gotos
    bool lanes_may_end = false;
    std::vector<std::pair<size_t, int>> ends; // Return / Unreachable nodes in regions, their region
    IR::VirtualReg running{};      // lanes_may_end: the lane has not ended (PredicateEnd)
    IR::VirtualReg running_wave{}; // lanes_may_end: its wave has not ended
    IR::VirtualReg pred{}; // P: the lane runs this code
    IR::VirtualReg wave{}; // W: some lane of the lane's wave runs this code
};

} // Anonymous namespace

void PredicateWave64Pass(IR::Program& program, const RuntimeInfo& runtime_info,
                         const Profile& profile) {
    const auto [size_x, size_y, size_z] = runtime_info.hw.cs.workgroup_size;
    const u32 num_threads = size_x * size_y * size_z;
    if (program.info.hw_stage != HwStage::Compute || profile.subgroup_size >= 64 ||
        num_threads <= 32) {
        return;
    }
    Pass{program, num_threads, profile.wave64_force_fallback}.Run();
}

} // namespace Shader::Optimization
