// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp
// SPDX-License-Identifier: GPL-2.0-or-later

// PS4Recomp: the per-lane bits of 64-bit scalar masks. InverseBallotEliminationPass keeps an SGPR
// pair, VCC or a copy of EXEC as one U1 bit per lane, and the virtual registers that carry such
// bits are listed in Program::scalar_mask_vregs (LowerPhisToRegsPass, SsaRepairPass). A scalar
// instruction writes all 64 bits of its mask for the wave, a lane outside EXEC included;
// predicate_wave64_pass.cpp writes those lanes' bits with ClassifyOutsideBit.

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "shader_recompiler/ir/value.h"

namespace Shader::Optimization {

// What a lane outside EXEC gets as the bit of the mask computed by `inst`.
enum class OutsideBit {
    Zero,      // EXEC's bit, a VALU result (v_cmp / v_cmp_class write 0 outside EXEC), undefined
    Own,       // a bit of a mask held in SGPRs or a mask virtual register: the lane's own bit
    Combine,   // LogicalNot/And/Or/Xor of mask bits, s_cselect (SelectU1; SCC is kept)
    Unhandled, // anything else: not known to be a mask bit's source
};

inline OutsideBit ClassifyOutsideBit(const IR::Inst& inst, const std::unordered_set<u64>& mask_vregs) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::GetExec:
    case IR::Opcode::UndefU1: // any value is the undefined one: 0
    case IR::Opcode::FPOrdEqual32:
    case IR::Opcode::FPOrdEqual64:
    case IR::Opcode::FPUnordEqual32:
    case IR::Opcode::FPUnordEqual64:
    case IR::Opcode::FPOrdNotEqual32:
    case IR::Opcode::FPOrdNotEqual64:
    case IR::Opcode::FPUnordNotEqual32:
    case IR::Opcode::FPUnordNotEqual64:
    case IR::Opcode::FPOrdLessThan32:
    case IR::Opcode::FPOrdLessThan64:
    case IR::Opcode::FPUnordLessThan32:
    case IR::Opcode::FPUnordLessThan64:
    case IR::Opcode::FPOrdGreaterThan32:
    case IR::Opcode::FPOrdGreaterThan64:
    case IR::Opcode::FPUnordGreaterThan32:
    case IR::Opcode::FPUnordGreaterThan64:
    case IR::Opcode::FPOrdLessThanEqual32:
    case IR::Opcode::FPOrdLessThanEqual64:
    case IR::Opcode::FPUnordLessThanEqual32:
    case IR::Opcode::FPUnordLessThanEqual64:
    case IR::Opcode::FPOrdGreaterThanEqual32:
    case IR::Opcode::FPOrdGreaterThanEqual64:
    case IR::Opcode::FPUnordGreaterThanEqual32:
    case IR::Opcode::FPUnordGreaterThanEqual64:
    case IR::Opcode::FPIsNan32:
    case IR::Opcode::FPIsNan64:
    case IR::Opcode::FPIsInf32:
    case IR::Opcode::FPIsInf64:
    case IR::Opcode::FPCmpClass32:
    case IR::Opcode::SLessThan32:
    case IR::Opcode::SLessThan64:
    case IR::Opcode::ULessThan32:
    case IR::Opcode::ULessThan64:
    case IR::Opcode::IEqual32:
    case IR::Opcode::IEqual64:
    case IR::Opcode::SLessThanEqual32:
    case IR::Opcode::SLessThanEqual64:
    case IR::Opcode::ULessThanEqual32:
    case IR::Opcode::ULessThanEqual64:
    case IR::Opcode::SGreaterThan32:
    case IR::Opcode::SGreaterThan64:
    case IR::Opcode::UGreaterThan32:
    case IR::Opcode::UGreaterThan64:
    case IR::Opcode::INotEqual32:
    case IR::Opcode::INotEqual64:
    case IR::Opcode::SGreaterThanEqual32:
    case IR::Opcode::SGreaterThanEqual64:
    case IR::Opcode::UGreaterThanEqual32:
    case IR::Opcode::UGreaterThanEqual64:
        return OutsideBit::Zero;
    case IR::Opcode::InverseBallot:
        return OutsideBit::Own;
    case IR::Opcode::GetVirtualRegister:
        return mask_vregs.contains(inst.Arg(0).VirtualReg().Key()) ? OutsideBit::Own
                                                                   : OutsideBit::Unhandled;
    case IR::Opcode::LogicalNot:
    case IR::Opcode::LogicalAnd:
    case IR::Opcode::LogicalOr:
    case IR::Opcode::LogicalXor:
    case IR::Opcode::SelectU1:
        return OutsideBit::Combine;
    default:
        return OutsideBit::Unhandled;
    }
}

// The operands of a Combine instruction that are mask bits (SelectU1's condition is SCC).
inline size_t FirstMaskOperand(const IR::Inst& inst) {
    return inst.GetOpcode() == IR::Opcode::SelectU1 ? 1 : 0;
}

// Buffers for IsLaneMaskBit / FeedsLaneMask / CarriesLaneMaskBit, held by the caller and reused
// across calls (no allocation per call once they have grown).
struct LaneMaskScratch {
    std::unordered_map<const IR::Inst*, bool> known; // IsLaneMaskBit's answers
    std::vector<const IR::Inst*> tree_stack;
    std::vector<std::pair<const IR::Inst*, size_t>> use_stack;
    std::unordered_set<const IR::Inst*> seen;
};

// Whether the U1 value `root` is (built from) the lane's bit of a 64-bit mask: an InverseBallot,
// a mask virtual register, or LogicalNot/And/Or/Xor / SelectU1 with such a bit among their mask
// operands. A compare or EXEC alone is not: it is a lane condition unless a mask operation uses
// it. `scratch.known` caches the answers (shared subexpressions are decided once).
inline bool IsLaneMaskBit(const IR::Inst* root, const std::unordered_set<u64>& mask_vregs,
                          LaneMaskScratch& scratch) {
    auto& known = scratch.known;
    if (const auto it = known.find(root); it != known.end()) {
        return it->second;
    }
    auto& stack = scratch.tree_stack;
    stack.clear();
    stack.push_back(root);
    while (!stack.empty()) {
        const IR::Inst* const inst = stack.back();
        if (known.contains(inst)) {
            stack.pop_back();
            continue;
        }
        const OutsideBit kind = ClassifyOutsideBit(*inst, mask_vregs);
        if (kind != OutsideBit::Combine) {
            known.emplace(inst, kind == OutsideBit::Own);
            stack.pop_back();
            continue;
        }
        bool pending = false;
        bool any = false;
        for (size_t i = FirstMaskOperand(*inst); i < inst->NumArgs(); ++i) {
            const IR::Value arg = inst->Arg(i);
            if (arg.IsImmediate()) {
                continue;
            }
            const auto it = known.find(arg.Inst());
            if (it == known.end()) {
                stack.push_back(arg.Inst());
                pending = true;
            } else {
                any = any || it->second;
            }
        }
        if (!pending) {
            known.emplace(inst, any);
            stack.pop_back();
        }
    }
    return known.at(root);
}

// Whether a value with the use (`user`, `operand`) ends up in a 64-bit mask: the use writes it
// into a mask virtual register, or combines it (as a mask operand of LogicalNot/And/Or/Xor/
// SelectU1) into a value that does.
inline bool FeedsLaneMask(const IR::Inst* user, size_t operand, const std::unordered_set<u64>& mask_vregs,
                          LaneMaskScratch& scratch) {
    auto& stack = scratch.use_stack;
    auto& seen = scratch.seen;
    stack.clear();
    seen.clear();
    stack.emplace_back(user, operand);
    while (!stack.empty()) {
        const auto [u, op] = stack.back();
        stack.pop_back();
        if (u->GetOpcode() == IR::Opcode::SetVirtualRegister) {
            if (op == 1 && mask_vregs.contains(u->Arg(0).VirtualReg().Key())) {
                return true;
            }
            continue;
        }
        if (ClassifyOutsideBit(*u, mask_vregs) != OutsideBit::Combine || op < FirstMaskOperand(*u) ||
            !seen.insert(u).second) {
            continue;
        }
        for (const IR::Use& use : u->Uses()) {
            stack.emplace_back(use.user, use.operand);
        }
    }
    return false;
}

// Whether a register that carries `inst` to `uses` (SsaRepairPass: uses it no longer dominates)
// carries a 64-bit mask's bit: `inst` is built from mask bits, or a use ends up in a mask
// (FeedsLaneMask) or combines it with mask bits. A copy of EXEC or a v_cmp result that no mask
// operation uses is a lane condition. `scratch.known` must not hold answers from before the uses
// were last changed.
template <typename Uses>
bool CarriesLaneMaskBit(const IR::Inst& inst, const Uses& uses, const std::unordered_set<u64>& mask_vregs,
                        LaneMaskScratch& scratch) {
    if (inst.Type() != IR::Type::U1) {
        return false;
    }
    if (IsLaneMaskBit(&inst, mask_vregs, scratch)) {
        return true;
    }
    for (const auto& [user, operand] : uses) {
        if (FeedsLaneMask(user, operand, mask_vregs, scratch)) {
            return true;
        }
        if (ClassifyOutsideBit(*user, mask_vregs) == OutsideBit::Combine &&
            operand >= FirstMaskOperand(*user) && IsLaneMaskBit(user, mask_vregs, scratch)) {
            return true;
        }
    }
    return false;
}

} // namespace Shader::Optimization
