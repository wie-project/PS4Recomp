// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// gcn_dis: debugging disassembler for dumped GCN programs, built on shadPS4's decoder
// (src/shader_recompiler/frontend/decode.*). Prints one decoded instruction per line with
// its dword offset, so translated IR/MSL can be compared against the original program.
//
//   gcn_dis <shader.bin>      (raw program dwords, as written by PS4_DUMP_SHADERS)

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <magic_enum/magic_enum.hpp>

#include "shader_recompiler/frontend/decode.h"

using namespace Shader::Gcn;

namespace {

bool Is64(ScalarType t) {
    return t == ScalarType::Uint64 || t == ScalarType::Sint64 || t == ScalarType::Float64;
}

std::string Op(const InstOperand& o, u32 literal_hint) {
    char b[64];
    const bool w = Is64(o.type);
    switch (o.field) {
    case OperandField::ScalarGPR:
        if (w) std::snprintf(b, sizeof(b), "s[%u:%u]", o.code, o.code + 1);
        else std::snprintf(b, sizeof(b), "s%u", o.code);
        break;
    case OperandField::VectorGPR:
        if (w) std::snprintf(b, sizeof(b), "v[%u:%u]", o.code, o.code + 1);
        else std::snprintf(b, sizeof(b), "v%u", o.code);
        break;
    case OperandField::VccLo: std::snprintf(b, sizeof(b), w ? "vcc" : "vcc_lo"); break;
    case OperandField::VccHi: std::snprintf(b, sizeof(b), "vcc_hi"); break;
    case OperandField::M0: std::snprintf(b, sizeof(b), "m0"); break;
    case OperandField::ExecLo: std::snprintf(b, sizeof(b), w ? "exec" : "exec_lo"); break;
    case OperandField::ExecHi: std::snprintf(b, sizeof(b), "exec_hi"); break;
    case OperandField::ConstZero: std::snprintf(b, sizeof(b), "0"); break;
    case OperandField::SignedConstIntPos: std::snprintf(b, sizeof(b), "%d", int(o.code) - 128); break;
    case OperandField::SignedConstIntNeg: std::snprintf(b, sizeof(b), "%d", -(int(o.code) - 192)); break;
    case OperandField::ConstFloatPos_0_5: std::snprintf(b, sizeof(b), "0.5"); break;
    case OperandField::ConstFloatNeg_0_5: std::snprintf(b, sizeof(b), "-0.5"); break;
    case OperandField::ConstFloatPos_1_0: std::snprintf(b, sizeof(b), "1.0"); break;
    case OperandField::ConstFloatNeg_1_0: std::snprintf(b, sizeof(b), "-1.0"); break;
    case OperandField::ConstFloatPos_2_0: std::snprintf(b, sizeof(b), "2.0"); break;
    case OperandField::ConstFloatNeg_2_0: std::snprintf(b, sizeof(b), "-2.0"); break;
    case OperandField::ConstFloatPos_4_0: std::snprintf(b, sizeof(b), "4.0"); break;
    case OperandField::ConstFloatNeg_4_0: std::snprintf(b, sizeof(b), "-4.0"); break;
    case OperandField::LiteralConst: {
        float f;
        std::memcpy(&f, &o.code, 4);
        std::snprintf(b, sizeof(b), "0x%08x(%g)", o.code, f);
        break;
    }
    case OperandField::Scc: std::snprintf(b, sizeof(b), "scc"); break;
    case OperandField::VccZ: std::snprintf(b, sizeof(b), "vccz"); break;
    case OperandField::ExecZ: std::snprintf(b, sizeof(b), "execz"); break;
    case OperandField::Undefined: return "";
    default: std::snprintf(b, sizeof(b), "%s:%u", std::string(magic_enum::enum_name(o.field)).c_str(), o.code);
    }
    std::string s = b;
    if (o.input_modifier.abs) s = "|" + s + "|";
    if (o.input_modifier.neg) s = "-" + s;
    (void)literal_hint;
    return s;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: gcn_dis <shader.bin>\n");
        return 2;
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<u32> code((std::istreambuf_iterator<char>(f)), {});
    std::vector<char> bytes;
    {
        std::ifstream g(argv[1], std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(g), {});
    }
    std::vector<u32> w(bytes.size() / 4);
    std::memcpy(w.data(), bytes.data(), w.size() * 4);
    GcnCodeSlice slice(w.data(), w.data() + w.size());
    GcnDecodeContext dec;
    u32 pc = 0;
    try {
        while (!slice.atEnd()) {
            const GcnInst inst = dec.decodeInstruction(slice);
            std::string line = std::string(magic_enum::enum_name(inst.opcode));
            for (auto& c : line) c = char(std::tolower(c));
            std::string ops;
            for (u32 i = 0; i < inst.dst_count; i++) {
                const std::string s = Op(inst.dst[i], 0);
                if (!s.empty()) ops += (ops.empty() ? " " : ", ") + s;
            }
            for (u32 i = 0; i < inst.src_count; i++) {
                const std::string s = Op(inst.src[i], 0);
                if (!s.empty()) ops += (ops.empty() ? " " : ", ") + s;
            }
            char extra[160] = "";
            switch (inst.encoding) {
            case InstEncoding::SOPP:
            case InstEncoding::SOPK:
                std::snprintf(extra, sizeof(extra), "  simm=%d", inst.control.sopp.simm);
                if (inst.IsUnconditionalBranch() || inst.IsConditionalBranch())
                    std::snprintf(extra, sizeof(extra), "  -> %u", inst.BranchTarget(pc * 4) / 4);
                break;
            case InstEncoding::SMRD:
                std::snprintf(extra, sizeof(extra), "  off=%u imm=%u cnt=%u", inst.control.smrd.offset,
                              inst.control.smrd.imm, inst.control.smrd.count);
                break;
            case InstEncoding::MUBUF:
                std::snprintf(extra, sizeof(extra), "  off=%u offen=%u idxen=%u glc=%u cnt=%u",
                              unsigned(inst.control.mubuf.offset), unsigned(inst.control.mubuf.offen),
                              unsigned(inst.control.mubuf.idxen), unsigned(inst.control.mubuf.glc),
                              unsigned(inst.control.mubuf.count));
                break;
            case InstEncoding::MTBUF:
                std::snprintf(extra, sizeof(extra), "  off=%u offen=%u idxen=%u dfmt=%u nfmt=%u cnt=%u",
                              unsigned(inst.control.mtbuf.offset), unsigned(inst.control.mtbuf.offen),
                              unsigned(inst.control.mtbuf.idxen), unsigned(inst.control.mtbuf.dfmt),
                              unsigned(inst.control.mtbuf.nfmt), unsigned(inst.control.mtbuf.count));
                break;
            case InstEncoding::DS:
                std::snprintf(extra, sizeof(extra), "  off0=%u off1=%u gds=%u",
                              unsigned(inst.control.ds.offset0), unsigned(inst.control.ds.offset1),
                              unsigned(inst.control.ds.gds));
                break;
            case InstEncoding::VOP3:
                if (inst.control.vop3.omod || inst.control.vop3.clmp)
                    std::snprintf(extra, sizeof(extra), "  omod=%u clamp=%u", unsigned(inst.control.vop3.omod),
                                  unsigned(inst.control.vop3.clmp));
                break;
            default:
                break;
            }
            std::printf("%5u: %s%s%s\n", pc, line.c_str(), ops.c_str(), extra);
            pc += inst.length / 4;
            if (inst.opcode == Opcode::S_ENDPGM) break;
        }
    } catch (const std::exception& e) {
        std::printf("decode error at %u: %s\n", pc, e.what());
        return 1;
    }
    return 0;
}
