// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>

#include "shader_recompiler/ir/basic_block.h"
#include "shader_recompiler/ir/passes/resource_pass.h"
#include "shader_recompiler/ir/program.h"

namespace Shader::IR {
class IREmitter;
}

namespace Shader {
struct Profile;
void InjectClipDistanceAttributes(IR::Program& program, RuntimeInfo& runtime_info);
} // namespace Shader

namespace Shader::Optimization {

// PS4Recomp (resource_discover_pass.cpp): the spilled SGPR value a v_readlane_b32 of a sharp or
// pointer dword reads back, or `value` itself if it cannot be resolved to exactly one.
IR::Value SpilledLaneValue(const IR::Value& value);


// PS4Recomp: the VGPR a cross-lane read (ReadLane, QuadBroadcast, Shuffle, ShuffleXor) reads,
// recorded by the translator in the instruction's flags (index + 1; 0: not recorded). A lane
// outside EXEC keeps that register's old value: predicate_wave64_pass.cpp reads it there.
inline void SetLaneSourceVgpr(const IR::Value& read, IR::VectorReg vgpr) {
    read.Inst()->SetFlags<u32>(static_cast<u32>(vgpr) + 1);
}
inline std::optional<IR::VectorReg> LaneSourceVgpr(const IR::Inst& read) {
    const u32 flags = read.Flags<u32>();
    return flags == 0 ? std::nullopt : std::optional{static_cast<IR::VectorReg>(flags - 1)};
}

void SsaRewritePass(IR::Program& program);
void SsaRepairPass(IR::Program& program);
void PhiSimplificationPass(IR::Program& program);
void InverseBallotEliminationPass(IR::Program& program);
// PS4Recomp (predicate_wave64_pass.cpp): runs the EXEC-divergent ifs and loops of a wave64
// compute shader on 32-wide subgroups in predicated mode where their code needs the whole wave
// (cross-lane operations, barriers, LDS of a one-wave workgroup). Structured program, before
// SsaRewritePass.
void PredicateWave64Pass(IR::Program& program, const RuntimeInfo& runtime_info,
                         const Profile& profile);
void LowerWaveOpsPass(IR::Program& program, const RuntimeInfo& runtime_info,
                      const Profile& profile);
// PS4Recomp (lower_wave64_pass.cpp): the GCN lane (0..63) of a compute invocation. A compute wave
// is 64 consecutive local invocations; the host subgroup lane differs from it when a wave64 runs
// as two 32-wide subgroups.
IR::U32 ComputeWaveLane(IR::IREmitter& ir);
void LowerHardwareIntrinsics(IR::Program& program);
void LowerPhisToRegsPass(IR::Program& program);
void DeadCodeEliminationPass(IR::Program& program);
void ConstantPropagationPass(IR::BlockList& program);
void FlattenExtendedUserdataPass(IR::Program& program);
void ReadLaneEliminationPass(IR::Program& program);
ResourceDiscoveryList ResourceDiscoverPass(IR::Program& program, const Profile& profile);
void ResourcePatchingPass(Shader::Info& info, const ResourceDiscoveryList& sharp_usages,
                          const Profile& profile);
void CollectShaderInfoPass(IR::Program& program, const Profile& profile);
void LowerBufferFormatToRaw(IR::Program& program);
void LowerUserClipPlanes(IR::Program& program, const RuntimeInfo& runtime_info);
void LowerFp64ToFp32(IR::Program& program);
void RingAccessElimination(const IR::Program& program, const RuntimeInfo& runtime_info);
void TessellationPreprocess(IR::Program& program, RuntimeInfo& runtime_info);
void HullShaderTransform(IR::Program& program, const RuntimeInfo& runtime_info);
void DomainShaderTransform(const IR::Program& program, const RuntimeInfo& runtime_info);
void SharedMemoryBarrierPass(IR::Program& program, const RuntimeInfo& runtime_info,
                             const Profile& profile);
void SharedMemorySimplifyPass(IR::Program& program, const Profile& profile);
void SharedMemoryToStoragePass(IR::Program& program, const RuntimeInfo& runtime_info,
                               const Profile& profile);

} // namespace Shader::Optimization
