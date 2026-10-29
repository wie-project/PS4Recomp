// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <unordered_set>
#include "shader_recompiler/frontend/instruction.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/ir/abstract_syntax_list.h"
#include "shader_recompiler/ir/basic_block.h"

namespace Shader::IR {

struct Program {
    explicit Program(Info& info_) : info{info_} {}

    AbstractSyntaxList syntax_list;
    BlockList blocks;
    BlockList post_order_blocks;
    std::vector<Gcn::GcnInst> ins_list;
    u32 next_reg_index{};
    // PS4Recomp (predicate_wave64_pass.cpp): merge blocks of the ifs and loops that run in
    // predicated mode. Every invocation of the workgroup runs their blocks (outside the real
    // branches nested in them).
    std::unordered_set<const Block*> predicated_merges;
    // PS4Recomp (predicate_wave64_pass.cpp): the blocks of predicated ifs and loops (outside the
    // branches nested in them), and their ballots that stay per subgroup (no exchange needed)
    std::unordered_set<const Block*> predicated_blocks;
    // merge blocks of the real branches nested in predicated code (masked branches on the wave
    // predicate and branches on P && cond): not taken by every invocation
    std::unordered_set<const Block*> branch_merges;
    std::unordered_set<const Inst*> subgroup_ballots;
    Info& info;
};

void DumpProgram(const Program& program, const Info& info, const std::string& type = "");

} // namespace Shader::IR
