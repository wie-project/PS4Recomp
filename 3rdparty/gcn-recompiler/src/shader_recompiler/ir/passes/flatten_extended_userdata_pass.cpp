// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Modified for PS4Recomp's standalone gcn-recompiler (arm64 macOS): the x86-64 Xbyak JIT
// "SRT walker" (plus its SIGSEGV patching handler and Zydis disassembly dump) was replaced by
// a portable bytecode program (Shader::SrtOp, see srt.h) that is interpreted on the host by
// Shader::RunSrtProgram(). The IR analysis part of the pass is unchanged.
//
// PS4Recomp also handles reads the walker cannot flatten because their offset is only known
// in the shader (an s_load whose offset register is a loop counter, i.e. a Phi; shadPS4 reads
// those through direct memory access, which the Metal profile does not have): the walker
// stores the pointer they read through, and they become reads of a guest buffer whose V# has
// that pointer as its base.

#include <unordered_map>
#include <boost/container/flat_map.hpp>
#include <queue>
#include <magic_enum/magic_enum.hpp>
#include "common/logging/log.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/opcodes.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/ir/passes/srt.h"
#include "shader_recompiler/ir/program.h"
#include "shader_recompiler/ir/reg.h"
#include "shader_recompiler/ir/srt_gvn_table.h"
#include "shader_recompiler/ir/value.h"

namespace {

using namespace Shader;

struct PassInfo {
    struct PointerListCompare {
        bool operator()(const IR::Value& a, const IR::Value& b) const {
            // Keep order with the following criteria:
            // 1. If immediate, sort by immediate value
            // 2. If not immediate, sort by instruction order
            // 3. Immediates come first, then instructions
            if (a.IsImmediate() && b.IsImmediate()) {
                return a.U32() < b.U32();
            } else if (!a.IsImmediate() && !b.IsImmediate()) {
                auto a_inst = a.Inst();
                auto b_inst = b.Inst();
                auto a_block = a_inst->GetParent();
                auto b_block = b_inst->GetParent();
                if (a_block != b_block) {
                    return a_block < b_block; // Sort by block address
                }

                auto it = a_block->Instructions().iterator_to(*a_inst);
                auto end = a_block->Instructions().end();
                for (; it != end; ++it) {
                    if (&*it == b_inst) {
                        return true;
                    }
                }
                return false;
            } else {
                return a.IsImmediate(); // Immediates come first
            }
        }
    };

    // map offset to inst
    using PtrUserList =
        boost::container::flat_map<IR::Value, Shader::IR::Inst*, PointerListCompare>;

    Optimization::SrtGvnTable gvn_table;
    // keys are GetUserData, ReadConst or ReadConstBuffer instructions that are used as pointers
    std::unordered_map<IR::Inst*, PtrUserList> pointer_uses;
    // GetUserData instructions corresponding to sgpr_base of SRT roots
    boost::container::small_flat_map<IR::ScalarReg, IR::Inst*, 1> srt_roots;

    // pick a single inst for a given value number
    std::unordered_map<u32, IR::Inst*> vn_to_inst;

    // Bumped during codegen to assign offsets to readconsts
    u16 dst_off_dw;

    // Readconsts with an offset the walker cannot compute -> flatbuf dword holding the 64-bit
    // pointer they read through (stored by SrtOp::StorePtr)
    std::unordered_map<IR::Inst*, u16> dynamic_reads;
    // A pointer was read at such an offset (logged once per shader)
    bool lost_pointer_logged = false;

    PtrUserList* GetUsesAsPointer(IR::Inst* inst) {
        auto it = pointer_uses.find(inst);
        if (it != pointer_uses.end()) {
            return &it->second;
        }
        return nullptr;
    }

    // Return a single instruction that this instruction is identical to, according
    // to value number
    // The "original" is arbitrary. Here it's the first instruction found for a given value number
    IR::Inst* DeduplicateInstruction(IR::Inst* inst) {
        auto it = vn_to_inst.try_emplace(gvn_table.GetValueNumber(inst), inst);
        return it.first->second;
    }
};
} // namespace

namespace Shader::Optimization {

namespace {

static inline u16 GetFlatbufOffset(const IR::Inst* inst) {
    if (inst->GetOpcode() == IR::Opcode::ReadConstBuffer) {
        auto inst_info = inst->Flags<IR::BufferInstInfo>();
        return inst_info.flatbuf_off_dw;
    }
    if (inst->GetOpcode() == IR::Opcode::ReadConst) {
        return inst->Flags<u16>();
    }
    UNREACHABLE_MSG("Instruction not supported");
}

static inline void SetFlatbufOffset(IR::Inst* inst, u16 offset) {
    if (inst->GetOpcode() == IR::Opcode::ReadConstBuffer) {
        auto inst_info = inst->Flags<IR::BufferInstInfo>();
        inst_info.flatbuf_off_dw.Assign(offset);
        inst->SetFlags(inst_info);
        return;
    }
    if (inst->GetOpcode() == IR::Opcode::ReadConst) {
        inst->SetFlags(offset);
        return;
    }
    UNREACHABLE_MSG("Instruction not supported");
}


// ---- Portable SRT program emission (replaces the Xbyak code generator) ----

struct SrtEmitter {
    std::vector<u32>& code;
    void Op(SrtOp op) {
        code.push_back(static_cast<u32>(op));
    }
    void Op(SrtOp op, u32 a) {
        code.push_back(static_cast<u32>(op));
        code.push_back(a);
    }
    void Op(SrtOp op, u32 a, u32 b) {
        code.push_back(static_cast<u32>(op));
        code.push_back(a);
        code.push_back(b);
    }
};

static bool IsAllowedOffsetInstruction(const IR::Inst* inst);

// Pushes the 32-bit value of off_dw on the SRT program value stack.
static bool ComputeOffset(SrtEmitter& c, PassInfo& pass_info, const IR::Value& off_dw) {
    if (off_dw.IsImmediate()) {
        c.Op(SrtOp::Imm, off_dw.U32());
        return true;
    }
    auto inst = off_dw.Inst();
    const auto binary = [&](SrtOp op) {
        if (!ComputeOffset(c, pass_info, inst->Arg(0)) ||
            !ComputeOffset(c, pass_info, inst->Arg(1))) {
            return false;
        }
        c.Op(op);
        return true;
    };
    switch (inst->GetOpcode()) {
    case IR::Opcode::GetUserData:
        c.Op(SrtOp::LoadFlat, static_cast<u32>(inst->Arg(0).ScalarReg()));
        return true;
    case IR::Opcode::ReadConst:
    case IR::Opcode::ReadConstBuffer:
        if (u16 offset = GetFlatbufOffset(pass_info.DeduplicateInstruction(inst)); offset != 0) {
            c.Op(SrtOp::LoadFlat, offset);
            return true;
        }
        return false;
    case IR::Opcode::IAdd32:
        return binary(SrtOp::Add);
    case IR::Opcode::ISub32:
        return binary(SrtOp::Sub);
    case IR::Opcode::IMul32:
        return binary(SrtOp::Mul);
    case IR::Opcode::ShiftLeftLogical32:
        return binary(SrtOp::Shl);
    case IR::Opcode::ShiftRightLogical32:
        return binary(SrtOp::Shr);
    case IR::Opcode::BitwiseAnd32:
        return binary(SrtOp::And);
    case IR::Opcode::BitwiseOr32:
        return binary(SrtOp::Or);
    case IR::Opcode::BitwiseXor32:
        return binary(SrtOp::Xor);
    case IR::Opcode::UMin32:
        return binary(SrtOp::UMin);
    case IR::Opcode::UMax32:
        return binary(SrtOp::UMax);
    case IR::Opcode::BitwiseNot32:
        if (!ComputeOffset(c, pass_info, inst->Arg(0))) {
            return false;
        }
        c.Op(SrtOp::Not);
        return true;
    case IR::Opcode::BitFieldUExtract:
        if (!ComputeOffset(c, pass_info, inst->Arg(0)) ||
            !ComputeOffset(c, pass_info, inst->Arg(1)) ||
            !ComputeOffset(c, pass_info, inst->Arg(2))) {
            return false;
        }
        c.Op(SrtOp::BfeU);
        return true;
    default:
        // PS4Recomp: not an error by itself. A plain read with such an offset is done by the
        // shader (LowerDynamicReadConst); a pointer read that way loses the reads through it
        // (logged once per shader by VisitPointer).
        LOG_DEBUG(Render_Recompiler, "Unexpected instruction for offset computation, {}",
                  magic_enum::enum_name(inst->GetOpcode()));
        return false;
    }
}

static bool IsAllowedOffsetInstruction(const IR::Inst* inst) {
    switch (inst->GetOpcode()) {
    case IR::Opcode::GetUserData:
    case IR::Opcode::ReadConst:
    case IR::Opcode::ReadConstBuffer:
    case IR::Opcode::IAdd32:
    case IR::Opcode::ISub32:
    case IR::Opcode::IMul32:
    case IR::Opcode::ShiftLeftLogical32:
    case IR::Opcode::ShiftRightLogical32:
    case IR::Opcode::BitwiseAnd32:
    case IR::Opcode::BitwiseOr32:
    case IR::Opcode::BitwiseXor32:
    case IR::Opcode::BitwiseNot32:
    case IR::Opcode::UMin32:
    case IR::Opcode::UMax32:
    case IR::Opcode::BitFieldUExtract:
        return true;
    default:
        return false;
    }
}

static inline bool PushPtr(SrtEmitter& c, PassInfo& pass_info, const IR::Value& off_dw,
                           bool is_root) {
    if (is_root) {
        ASSERT(off_dw.IsImmediate());
        c.Op(SrtOp::PushPtrUd, off_dw.U32());
        return true;
    }
    if (off_dw.IsImmediate()) {
        c.Op(SrtOp::PushPtrImm, off_dw.U32());
        return true;
    }
    const size_t mark = c.code.size();
    if (!ComputeOffset(c, pass_info, off_dw)) {
        c.code.resize(mark);
        return false;
    }
    c.Op(SrtOp::PushPtrDyn);
    return true;
}

static void VisitPointer(const IR::Value& off_dw, IR::Inst* subtree, PassInfo& pass_info,
                         SrtEmitter& c, bool is_root) {
    if (subtree->GetOpcode() == IR::Opcode::ReadConst && subtree->Flags<u16>() == 0 ||
        subtree->GetOpcode() == IR::Opcode::ReadConstBuffer &&
            subtree->Flags<IR::BufferInstInfo>().flatbuf_off_dw == 0) {
        return;
    }

    if (!PushPtr(c, pass_info, off_dw, is_root)) {
        LOG_ERROR(Render_Recompiler, "Failed to compute offset for SRT walker");
        return;
    }
    PassInfo::PtrUserList* use_list = pass_info.GetUsesAsPointer(subtree);
    ASSERT(use_list);

    // First copy all the src data from this tree level
    // That way, all data that was contiguous in the guest SRT is also contiguous in the
    // flattened buffer.
    u16 ptr_slot = 0; // flatbuf copy of this level's pointer, for reads with a shader offset
    for (auto [src_off_dw, use] : *use_list) {
        if (src_off_dw.IsImmediate()) {
            c.Op(SrtOp::CopyImm, src_off_dw.U32(), pass_info.dst_off_dw);
        } else {
            const size_t mark = c.code.size();
            if (!ComputeOffset(c, pass_info, src_off_dw)) {
                c.code.resize(mark);
                if (ptr_slot == 0) {
                    ptr_slot = pass_info.dst_off_dw;
                    pass_info.dst_off_dw += 2;
                    c.Op(SrtOp::StorePtr, ptr_slot);
                }
                pass_info.dynamic_reads.emplace(use, ptr_slot);
                if (pass_info.GetUsesAsPointer(use) && !pass_info.lost_pointer_logged) {
                    // The pointer value itself is read correctly (a guest buffer read), but the
                    // reads through it cannot be flattened without direct memory access: they
                    // keep flatbuf offset 0 and read user data dword 0.
                    pass_info.lost_pointer_logged = true;
                    LOG_ERROR(Render_Recompiler,
                              "SRT pointer read at an offset only the shader knows: the reads "
                              "through it are not flattened (they read user data dword 0)");
                }
                continue;
            }
            c.Op(SrtOp::CopyDyn, pass_info.dst_off_dw);
        }

        SetFlatbufOffset(use, pass_info.dst_off_dw);
        pass_info.dst_off_dw++;
    }

    // Then visit any children used as pointers
    for (const auto [src_off_dw, use] : *use_list) {
        if (pass_info.GetUsesAsPointer(use)) {
            VisitPointer(src_off_dw, use, pass_info, c, false);
        }
    }

    c.Op(SrtOp::PopPtr);
}

static void GenerateSrtProgram(Info& info, PassInfo& pass_info) {
    if (pass_info.srt_roots.empty()) {
        return;
    }

    info.srt_info.program.clear();
    SrtEmitter c{info.srt_info.program};
    pass_info.dst_off_dw = NUM_USER_DATA_REGS;
    ASSERT(pass_info.dst_off_dw == info.srt_info.flattened_bufsize_dw);

    for (const auto& [sgpr_base, root] : pass_info.srt_roots) {
        VisitPointer(IR::Value(static_cast<u32>(sgpr_base)), root, pass_info, c, true);
    }
    c.Op(SrtOp::End);

    info.srt_info.flattened_bufsize_dw = pass_info.dst_off_dw;
}

static bool IsReadConstSource(const IR::Value base) {
    auto* inst = base.TryInst();
    if (!inst) {
        return false;
    }
    return inst->GetOpcode() == IR::Opcode::GetUserData ||
           inst->GetOpcode() == IR::Opcode::ReadConst ||
           inst->GetOpcode() == IR::Opcode::ReadConstBuffer;
}

void SimplifyReadConstAddressAdd(IR::Inst& inst) {
    // This handles the following pattern by combining the addition with the offset
    // s_add_u32    s12, s2, <offset>
    // s_addc_u32   s13, s3, 0
    // s_load_dword s16, s[12:13], 0x0

    if (inst.GetOpcode() != IR::Opcode::ReadConst) {
        return;
    }
    IR::Inst* addr = inst.Arg(0).Inst();
    IR::Inst* hi = addr->Arg(1).TryInst();
    IR::Inst* lo = addr->Arg(0).TryInst();
    if (!hi || !lo) {
        return;
    }

    if (lo->GetOpcode() != IR::Opcode::IAdd32 || hi->GetOpcode() != IR::Opcode::IAdd32) {
        return;
    }

    IR::Value add_offset;
    IR::Value base;
    if (base = lo->Arg(0); IsReadConstSource(base)) {
        add_offset = lo->Arg(1);
    } else if (base = lo->Arg(1); IsReadConstSource(base)) {
        add_offset = lo->Arg(0);
    } else {
        return;
    }

    IR::Inst* sel = hi->Arg(1).TryInst();
    if (!sel || sel->GetOpcode() != IR::Opcode::SelectU32) {
        return;
    }

    IR::Value sel_true = sel->Arg(1);
    IR::Value sel_false = sel->Arg(2);
    if (!sel_true.IsImmediate() || !sel_false.IsImmediate() || sel_true.U32() != 1 ||
        sel_false.U32() != 0) {
        return;
    }

    addr->SetArg(0, base);
    addr->SetArg(1, hi->Arg(0));
    for (auto [user, operand] : addr->Uses()) {
        ASSERT(user->GetOpcode() == IR::Opcode::ReadConst && operand == 0);
        IR::IREmitter ir{*user->GetParent(), IR::Block::InstructionList::s_iterator_to(*user)};
        IR::U32 offset = IR::U32{user->Arg(1)};
        IR::U32 dw_add_offset =
            add_offset.IsImmediate()
                ? ir.Imm32(add_offset.U32() >> 2u)
                : IR::U32{ir.ShiftRightLogical(IR::U32{add_offset}, ir.Imm32(2u))};
        if (offset.IsImmediate() && dw_add_offset.IsImmediate()) {
            user->SetArg(1, ir.Imm32(offset.U32() + dw_add_offset.U32()));
        } else {
            user->SetArg(1, ir.IAdd(offset, dw_add_offset));
        }
    }
}

// Bytes readable through the buffer of a dynamic read, from the pointer it reads through.
// The shader's offsets are unknown; tables indexed this way (light lists, per-tile arrays)
// are far smaller.
constexpr u32 DynamicReadBufferBytes = 1u << 20;

// A raw V# for a guest buffer whose base address is in flatbuf[ptr_slot..+1] (stride
// 0, so num_records is a byte count). dword3: dst_sel XYZW, NUM_FORMAT_FLOAT,
// DATA_FORMAT_32, type 0 (buffer).
static BufferResource DynamicReadBuffer(u16 ptr_slot) {
    BufferResource res{};
    res.sharp_fetch.summary = SharpFetch<AmdGpu::Buffer>::Summary::MultiLoad;
    res.sharp_fetch.offsets = {};
    res.sharp_fetch.immediates = {};
    res.sharp_fetch.offsets[0] = ptr_slot;
    res.sharp_fetch.offsets[1] = static_cast<SharpLocation>(ptr_slot + 1);
    res.sharp_fetch.immediates[2] = DynamicReadBufferBytes;
    res.sharp_fetch.immediates[3] = 0x00027FACu;
    res.sharp_fetch.load_mask = 0b0011;
    res.used_types = IR::Type::U32;
    res.buffer_type = BufferType::Guest;
    return res;
}

// ReadConst(pointer, dword offset) whose offset only the shader knows -> ReadConstBuffer of
// the guest buffer based at that pointer (same dword index).
static void LowerDynamicReadConst(Info& info, IR::Inst& readconst, u16 ptr_slot,
                                  std::unordered_map<u16, u32>& buffers) {
    auto it = buffers.find(ptr_slot);
    if (it == buffers.end()) {
        if (info.buffers.size() >= NUM_BUFFERS) {
            LOG_ERROR(Render_Recompiler, "No buffer slot left for a dynamic SRT read");
            return;
        }
        info.buffers.push_back(DynamicReadBuffer(ptr_slot));
        it = buffers.emplace(ptr_slot, static_cast<u32>(info.buffers.size() - 1)).first;
    }
    // In place, so that the sharp references resource discovery took through this read stay
    // valid: a T# loaded from a table at an offset only the shader knows is then seen as a read
    // of this buffer (LowerDynamicImageTable).
    const IR::Value offset = readconst.Arg(1);
    readconst.ReplaceOpcode(IR::Opcode::ReadConstBuffer);
    readconst.SetArg(0, IR::Value{it->second});
    readconst.SetArg(1, offset);
    readconst.SetFlags(IR::BufferInstInfo{});
}

} // Anonymous namespace

void FlattenExtendedUserdataPass(IR::Program& program) {
    auto& post_order = program.post_order_blocks;
    PassInfo pass_info;

    // traverse at end and assign offsets to duplicate readconsts, using
    // vn_to_inst as the source
    boost::container::small_vector<IR::Inst*, 32> all_readconsts, visited;
    std::queue<IR::Inst*> queue;

    for (auto it = post_order.rbegin(); it != post_order.rend(); it++) {
        IR::Block* block{*it};
        for (IR::Inst& inst : *block) {
            if (inst.GetOpcode() != IR::Opcode::ReadConst &&
                inst.GetOpcode() != IR::Opcode::ReadConstBuffer) {
                continue;
            }

            if (inst.GetOpcode() == IR::Opcode::ReadConstBuffer) {
                // Only flatten ReadConstBuffer if it was marked as a sharp source in the
                // resource discovery pass
                auto inst_info = inst.Flags<IR::BufferInstInfo>();
                if (!inst_info.sharp_source) {
                    continue;
                }
            }

            SimplifyReadConstAddressAdd(inst);

            all_readconsts.push_back(&inst);

            auto offset = inst.Arg(1);
            if (offset.IsImmediate()) {
                continue;
            }

            queue.push(offset.Inst());
            while (!queue.empty()) {
                IR::Inst* inst{queue.front()};
                queue.pop();

                if (inst->GetOpcode() == IR::Opcode::ReadConstBuffer) {
                    auto buffer_inst_info = inst->Flags<IR::BufferInstInfo>();
                    if (!buffer_inst_info.sharp_source.Value()) {
                        all_readconsts.push_back(inst);
                        auto offset = inst->Arg(1);
                        if (!offset.IsImmediate()) {
                            auto arg_inst = offset.Inst();
                            if (std::ranges::find(visited, arg_inst) == visited.end()) {
                                visited.push_back(arg_inst);
                                queue.push(arg_inst);
                            }
                        }
                    }
                    continue;
                }
                if (inst->GetOpcode() == IR::Opcode::GetUserData ||
                    inst->GetOpcode() == IR::Opcode::ReadConst ||
                    !IsAllowedOffsetInstruction(inst)) {
                    continue;
                }
                for (size_t arg = inst->NumArgs(); arg--;) {
                    const IR::Value arg_value = inst->Arg(arg);
                    if (arg_value.IsImmediate()) {
                        continue;
                    }
                    IR::Inst* arg_inst = arg_value.Inst();
                    if (std::ranges::find(visited, arg_inst) == visited.end()) {
                        visited.push_back(arg_inst);
                        queue.push(arg_inst);
                    }
                }
            }
        }
    }

    for (auto inst : all_readconsts) {
        if (pass_info.DeduplicateInstruction(inst) != inst) {
            // This is a duplicate of a readconst we've already visited
            continue;
        }

        IR::Inst* base = inst->Arg(0).Inst();
        if (auto* inst = base->Arg(0).TryInst();
            inst && inst->GetOpcode() == IR::Opcode::ReadFirstLane) {
            continue;
        }
        for (size_t k = 0; k < 2; ++k) {
            // PS4Recomp: a pointer spilled to VGPR lanes and read back (see SpilledLaneValue)
            if (const IR::Value v = SpilledLaneValue(base->Arg(k)); !(v == base->Arg(k))) {
                base->SetArg(k, v);
            }
        }
        ASSERT_MSG(IsReadConstSource(base->Arg(0)), "ReadConst base low not from constant memory");
        ASSERT_MSG(IsReadConstSource(base->Arg(1)), "ReadConst base high not from constant memory");

        IR::Inst* ptr_lo = base->Arg(0).Inst();
        ptr_lo = pass_info.DeduplicateInstruction(ptr_lo);

        auto ptr_uses_kv = pass_info.pointer_uses.try_emplace(ptr_lo, PassInfo::PtrUserList{});
        PassInfo::PtrUserList& user_list = ptr_uses_kv.first->second;

        user_list[inst->Arg(1)] = inst;

        if (ptr_lo->GetOpcode() == IR::Opcode::GetUserData) {
            IR::ScalarReg ud_reg = ptr_lo->Arg(0).ScalarReg();
            pass_info.srt_roots[ud_reg] = ptr_lo;
        }
    }

    GenerateSrtProgram(program.info, pass_info);

    // Assign offsets to duplicate readconsts
    std::unordered_map<u16, u32> dynamic_buffers; // pointer slot -> buffer resource index
    for (IR::Inst* readconst : all_readconsts) {
        ASSERT(pass_info.vn_to_inst.contains(pass_info.gvn_table.GetValueNumber(readconst)));
        IR::Inst* original = pass_info.DeduplicateInstruction(readconst);
        const auto dyn = pass_info.dynamic_reads.find(original);
        if (dyn != pass_info.dynamic_reads.end() &&
            readconst->GetOpcode() == IR::Opcode::ReadConst) {
            LowerDynamicReadConst(program.info, *readconst, dyn->second, dynamic_buffers);
            continue;
        }
        SetFlatbufOffset(readconst, GetFlatbufOffset(original));
    }

    program.info.RefreshFlatBuf();
}

} // namespace Shader::Optimization

namespace Shader {

SrtDirectMemory g_srt_direct;
ImageAddressCheck g_image_address_check;

void ForEachSrtTableRead(std::span<const u32> program, const u32* flat,
                         const std::function<void(u64, u64)>& fn) {
    const auto base_of = [&](u32 vs) {
        return u64(flat[vs]) | (u64(flat[vs + 1] & 0xFFFu) << 32);
    };
    size_t pc = 0;
    while (pc < program.size()) {
        const auto op = static_cast<SrtOp>(program[pc++]);
        switch (op) {
        case SrtOp::End:
            return;
        case SrtOp::PushPtrUd:
        case SrtOp::PushPtrImm:
        case SrtOp::CopyDyn:
        case SrtOp::LoadFlat:
        case SrtOp::Imm:
        case SrtOp::StorePtr:
            pc += 1;
            break;
        case SrtOp::CopyImm:
            pc += 2;
            break;
        case SrtOp::CopyBufferDwords: {
            const u32* a = &program[pc];
            pc += 7;
            const u32 vs = a[0], byte_off = a[2], count = a[3];
            const u64 base = base_of(vs), size = SrtBufferBytes(&flat[vs], a[1]);
            if (base != 0 && byte_off < size) {
                fn(base + byte_off, std::min<u64>(u64(count) * 4, size - byte_off));
            }
            break;
        }
        case SrtOp::CollectIndices: {
            const u32* a = &program[pc];
            pc += 6;
            const u32 ivs = a[0], ibyte = a[2], istride = a[3], dst = a[5];
            const u64 ibase = base_of(ivs), isize = SrtBufferBytes(&flat[ivs], a[1]);
            if (ibase != 0 && flat[dst] != ~0u && u64(ibyte) + 4 <= isize && istride) {
                const u64 records = (isize - ibyte - 4) / istride + 1;
                fn(ibase + ibyte, (records - 1) * istride + 4);
            }
            break;
        }
        case SrtOp::CopyIndexedDwords: {
            const u32* a = &program[pc];
            pc += 8;
            const u32 tvs = a[0], count = a[5], cmp = a[7];
            const u64 tbase = base_of(tvs), tsize = SrtBufferBytes(&flat[tvs], a[1]);
            const u64 off = u64(flat[cmp]) * 4;
            if (tbase != 0 && off < tsize) {
                fn(tbase + off, std::min<u64>(u64(count) * 4, tsize - off));
            }
            break;
        }
        default: // value ops without operands
            break;
        }
    }
}

bool RunSrtProgram(std::span<const u32> program, const u32* user_data, u32* flat_dst,
                   SrtReadFn read, void* read_ctx) {
    constexpr u64 AddrMask = 0xFFFFFFFFFFFFULL;
    u32 stack[64];
    u32 sp = 0;
    u64 ptr_stack[64];
    u32 psp = 0;
    u64 cur = 0;
    bool ok = true;
    const auto read32 = [&](u64 addr) -> u32 {
        u32 v = 0;
        if (!SrtRead(read, read_ctx, addr, &v, sizeof(v))) {
            ok = false;
            return 0;
        }
        return v;
    };
    const auto read64 = [&](u64 addr) -> u64 {
        u64 v = 0;
        if (!SrtRead(read, read_ctx, addr, &v, sizeof(v))) {
            ok = false;
            return 0;
        }
        return v;
    };
    const auto pop = [&]() -> u32 { return sp ? stack[--sp] : 0; };
    const auto push = [&](u32 v) {
        if (sp < 64) {
            stack[sp++] = v;
        }
    };
    size_t pc = 0;
    while (pc < program.size()) {
        const auto op = static_cast<SrtOp>(program[pc++]);
        switch (op) {
        case SrtOp::End:
            return ok;
        case SrtOp::PushPtrUd: {
            const u32 sgpr = program[pc++];
            ptr_stack[psp++ & 63] = cur;
            u64 v = 0;
            if (sgpr + 1 < NUM_USER_DATA_REGS) {
                v = u64(user_data[sgpr]) | (u64(user_data[sgpr + 1]) << 32);
            }
            cur = v & AddrMask;
            break;
        }
        case SrtOp::PushPtrImm: {
            const u32 off = program[pc++];
            ptr_stack[psp++ & 63] = cur;
            cur = read64(cur + u64(off) * 4) & AddrMask;
            break;
        }
        case SrtOp::PushPtrDyn: {
            const u32 off = pop();
            ptr_stack[psp++ & 63] = cur;
            cur = read64(cur + u64(off) * 4) & AddrMask;
            break;
        }
        case SrtOp::PopPtr:
            cur = psp ? ptr_stack[--psp & 63] : 0;
            break;
        case SrtOp::CopyImm: {
            const u32 src = program[pc++];
            const u32 dst = program[pc++];
            flat_dst[dst] = read32(cur + u64(src) * 4);
            break;
        }
        case SrtOp::CopyDyn: {
            const u32 dst = program[pc++];
            const u32 src = pop();
            flat_dst[dst] = read32(cur + u64(src) * 4);
            break;
        }
        case SrtOp::LoadFlat:
            push(flat_dst[program[pc++]]);
            break;
        case SrtOp::Imm:
            push(program[pc++]);
            break;
        case SrtOp::Not:
            push(~pop());
            break;
        case SrtOp::StorePtr: {
            const u32 dst = program[pc++];
            // V# dwords 0-1: base address bits 0..43; the stride field above them stays 0
            flat_dst[dst] = static_cast<u32>(cur);
            flat_dst[dst + 1] = static_cast<u32>(cur >> 32) & 0xFFFu;
            break;
        }
        case SrtOp::CopyBufferDwords: {
            const u32 vs = program[pc++];
            const u32 size_imm = program[pc++];
            const u32 byte_off = program[pc++];
            const u32 count = program[pc++];
            const u32 dst = program[pc++];
            const u32 guard = program[pc++];
            const u32 k = program[pc++];
            const u32* v = &flat_dst[vs];
            const u64 base = u64(v[0]) | (u64(v[1] & 0xFFFu) << 32);
            const u64 size = SrtBufferBytes(v, size_imm);
            const bool used =
                guard == 0 || k < DynamicLoopEntries(flat_dst[guard & 0xFFFFu],
                                                     static_cast<LoopCompare>(guard >> 16));
            for (u32 i = 0; i < count; ++i) {
                const u64 off = u64(byte_off) + 4ull * i;
                flat_dst[dst + i] = (used && base != 0 && off + 4 <= size) ? read32(base + off) : 0u;
            }
            break;
        }
        case SrtOp::CollectIndices: {
            const u32 ivs = program[pc++];
            const u32 isize_imm = program[pc++];
            const u32 ibyte = program[pc++];
            const u32 istride = program[pc++];
            const u32 max_records = program[pc++];
            const u32 dst = program[pc++];
            const u32* iv = &flat_dst[ivs];
            const u64 ibase = u64(iv[0]) | (u64(iv[1] & 0xFFFu) << 32);
            const u64 isize = ibase != 0 ? SrtBufferBytes(iv, isize_imm) : 0;
            const u64 records =
                isize >= u64(ibyte) + 4 && istride ? (isize - ibyte - 4) / istride + 1 : 0;
            u32 found[SrtMaxTableIndices] = {0};
            u32 n = 1; // index 0
            if (records > max_records) {
                n = ~0u;
            } else {
                for (u64 k = 0; k < records && n <= SrtMaxTableIndices; ++k) {
                    const u32 t = read32(ibase + ibyte + istride * k);
                    bool seen = false;
                    for (u32 i = 0; i < std::min(n, SrtMaxTableIndices); ++i) {
                        seen |= found[i] == t;
                    }
                    if (!seen) {
                        if (n < SrtMaxTableIndices) {
                            found[n] = t;
                        }
                        ++n;
                    }
                }
            }
            flat_dst[dst] = n;
            for (u32 i = 0; i < SrtMaxTableIndices; ++i) {
                flat_dst[dst + 1 + i] = n != ~0u && i < n ? found[i] : 0u;
            }
            break;
        }
        case SrtOp::CopyIndexedDwords: {
            const u32 tvs = program[pc++];
            const u32 tsize_imm = program[pc++];
            const u32 tfirst = program[pc++];
            const u32 tstride = program[pc++];
            const u32 idx = program[pc++];
            const u32 count = program[pc++];
            const u32 dst = program[pc++];
            const u32 cmp = program[pc++];
            const u32 first_dw = tfirst + tstride * flat_dst[idx];
            flat_dst[cmp] = first_dw;
            const u32* tv = &flat_dst[tvs];
            const u64 tbase = u64(tv[0]) | (u64(tv[1] & 0xFFFu) << 32);
            const u64 tsize = SrtBufferBytes(tv, tsize_imm);
            for (u32 i = 0; i < count; ++i) {
                // a dword index, as the shader's ReadConstBuffer reads it
                const u64 off = u64(first_dw + i) * 4;
                flat_dst[dst + i] = (tbase != 0 && off + 4 <= tsize) ? read32(tbase + off) : 0u;
            }
            break;
        }
        case SrtOp::BfeU: {
            const u32 count = pop();
            const u32 offset = pop();
            const u32 base = pop();
            const u32 shifted = offset >= 32 ? 0 : (base >> offset);
            push(count >= 32 ? shifted : (shifted & ((1U << count) - 1)));
            break;
        }
        default: {
            const u32 b = pop();
            const u32 a = pop();
            u32 r = 0;
            switch (op) {
            case SrtOp::Add: r = a + b; break;
            case SrtOp::Sub: r = a - b; break;
            case SrtOp::Mul: r = a * b; break;
            case SrtOp::Shl: r = a << (b & 31); break;
            case SrtOp::Shr: r = a >> (b & 31); break;
            case SrtOp::And: r = a & b; break;
            case SrtOp::Or: r = a | b; break;
            case SrtOp::Xor: r = a ^ b; break;
            case SrtOp::UMin: r = a < b ? a : b; break;
            case SrtOp::UMax: r = a > b ? a : b; break;
            default:
                return false;
            }
            push(r);
            break;
        }
        }
    }
    return ok;
}

} // namespace Shader
