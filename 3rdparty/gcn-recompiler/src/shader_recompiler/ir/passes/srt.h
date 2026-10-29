// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Modified for PS4Recomp's standalone gcn-recompiler: the x86-64 JIT walker function pointer is
// replaced by a portable bytecode program interpreted by RunSrtProgram().

#pragma once

#include <boost/container/set.hpp>
#include <boost/container/small_vector.hpp>
#include <functional>
#include <span>
#include <vector>
#include "common/types.h"

namespace Serialization {
struct Archive;
}

namespace Shader {

// Portable SRT walker bytecode. Operands follow the opcode word.
// Pointer ops read a 64-bit guest pointer (masked to 48 bits) and push the previous one.
// Value ops work on a 32-bit value stack (dynamic offsets, in dwords).
enum class SrtOp : u32 {
    End = 0,
    PushPtrUd,  // sgpr: cur = user_data[sgpr] | user_data[sgpr+1] << 32
    PushPtrImm, // off_dw: cur = *(u64*)(cur + off_dw*4)
    PushPtrDyn, // (pop off_dw): cur = *(u64*)(cur + off_dw*4)
    PopPtr,
    CopyImm, // src_off_dw, dst: flat[dst] = *(u32*)(cur + src_off_dw*4)
    CopyDyn, // dst, (pop src_off_dw): flat[dst] = *(u32*)(cur + src_off_dw*4)
    LoadFlat, // idx: push flat[idx]
    Imm,      // value: push value
    Add, Sub, Mul, Shl, Shr, And, Or, Xor, Not, UMin, UMax,
    BfeU, // pop count, offset, base: push (base >> offset) & ((1 << count) - 1)
    // PS4Recomp: dst: flat[dst] = low dword of cur, flat[dst + 1] = bits 32..43 (V# dwords
    // 0-1 with stride 0). The pointer becomes the base of a guest buffer for reads whose
    // offset only the shader knows.
    StorePtr,
    // PS4Recomp: vs, size, byte_off, count, dst, guard, k: flat[dst + i] = the dword at byte
    // byte_off + 4 * i of the guest buffer whose V# is flat[vs..vs + 3], for i < count, as
    // s_buffer_load reads it: 0 for a dword at or past the buffer's size (`size` bytes if not 0,
    // else the V#'s: stride * num_records, num_records bytes when stride is 0; with `size` set
    // only V# dwords 0-1 are read). With `guard` (SrtTableGuard) not 0, entry k of a table
    // indexed by a loop counter is copied only if k < the loop's entry count (all 0 otherwise).
    // Copies entry k of a T# table the shader indexes at run time (LowerDynamicImageTable).
    CopyBufferDwords,
    // PS4Recomp: ivs, isize, ibyte, istride, max, dst: the distinct indices an index buffer
    // holds (Uncharted 4's reflection probes: record j of a constant buffer holds the T# table
    // index t of probe j). The index of record k is the dword at byte ibyte + istride * k of the
    // buffer whose V# is flat[ivs..] (size as in CopyBufferDwords), for every record whose dword
    // lies below its size. 0 is always one of them (an index read past the buffer reads 0).
    // flat[dst] = how many distinct indices (counted up to SrtMaxTableIndices + 1), or ~0u if
    // the buffer has more than `max` records; flat[dst + 1 + i] = the i-th distinct index in
    // record order (index 0 first), 0 for i at or past the count, i < SrtMaxTableIndices.
    CollectIndices,
    // PS4Recomp: tvs, tsize, tfirst, tstride, idx, count, dst, cmp: the T# table entry of index
    // t = flat[idx]: flat[cmp] = tfirst + tstride * t (the dword offset the shader loads it
    // from, mod 2^32); flat[dst + i] = the dword at dword offset flat[cmp] + i of the table
    // buffer (V# flat[tvs..], size as in CopyBufferDwords: 0 past it), for i < count.
    CopyIndexedDwords,
};

// Distinct indices CollectIndices keeps (the entries of a table bound at most).
constexpr u32 SrtMaxTableIndices = 8;

// How a loop compares its counter (k + 1) with its bound N to run again.
enum class LoopCompare : u32 {
    ULess = 1,    // k + 1 < N, unsigned
    SLess = 2,    // k + 1 < N, signed
    NotEqual = 3, // k + 1 != N
};
// Iterations of a loop `k = 0; do { ...; } while (k + 1 <compare> N);` (at least one): the
// entries of a T# table it indexes with k.
inline u64 DynamicLoopEntries(u32 n, LoopCompare compare) {
    switch (compare) {
    case LoopCompare::SLess:
        return static_cast<s32>(n) <= 0 ? 1 : n;
    case LoopCompare::NotEqual:
        return n == 0 ? (1ull << 32) : n;
    case LoopCompare::ULess:
    default:
        return n == 0 ? 1 : n;
    }
}
// CopyBufferDwords guard: bits 0..15 the flatbuf dword holding the bound N, bits 16..17 the
// LoopCompare; 0: no guard.
constexpr u32 SrtTableGuard(u32 count_dw, LoopCompare compare) {
    return count_dw | static_cast<u32>(compare) << 16;
}

// Bytes of a guest buffer as s_buffer_load bounds it: `size_imm` if not 0, else from its V#
// (dwords 1-2: stride * num_records, num_records bytes when stride is 0).
inline u64 SrtBufferBytes(const u32* vsharp, u32 size_imm) {
    if (size_imm) {
        return size_imm;
    }
    const u32 stride = (vsharp[1] >> 16) & 0x3FFFu;
    return stride ? u64(stride) * vsharp[2] : vsharp[2];
}

// Guest memory reader: copy `size` bytes at guest address `addr` into `dst`.
// Return false if the address is not mapped (the destination is then treated as zero).
using SrtReadFn = bool (*)(void* ctx, u64 addr, void* dst, u64 size);

// Guest memory the host maps as one host range (gcn_set_direct_guest_memory): reads through
// that reader are done here instead of calling it for every dword.
struct SrtDirectMemory {
    SrtReadFn read{};
    void* ctx{};
    const u8* base{};
    u64 size{};
};
extern SrtDirectMemory g_srt_direct;

// Same contract as calling read(ctx, ...): false for address 0 or an unmapped range.
inline bool SrtRead(SrtReadFn read, void* ctx, u64 addr, void* dst, u64 size) {
    const SrtDirectMemory& d = g_srt_direct;
    if (read && read == d.read && ctx == d.ctx) {
        if (addr == 0 || addr >= d.size || size > d.size - addr) return false;
        __builtin_memcpy(dst, d.base + addr, size);
        return true;
    }
    return read && read(ctx, addr, dst, size);
}

// PS4Recomp: the guest memory the T# table copies of an SRT program read for a draw whose
// flattened user data (built by RunSrtProgram) is `flat`: fn(addr, bytes) for the records an
// index buffer scan reads (CollectIndices) and every entry copied (CopyBufferDwords,
// CopyIndexedDwords) inside its buffer's size.
void ForEachSrtTableRead(std::span<const u32> program, const u32* flat,
                         const std::function<void(u64, u64)>& fn);

// Runs the walker. flat_dst must hold flattened_bufsize_dw dwords with the user data
// already copied to the first NUM_USER_DATA_REGS dwords. Returns false if any read failed.
bool RunSrtProgram(std::span<const u32> program, const u32* user_data, u32* flat_dst,
                   SrtReadFn read, void* read_ctx);

struct PersistentSrtInfo {
    // Special case when fetch shader uses step rates.
    struct SrtSharpReservation {
        u32 sgpr_base;
        u32 dword_offset;
        u32 num_dwords;
    };

    std::vector<u32> program{}; // empty when the shader has no extended user data
    u32 flattened_bufsize_dw = 16; // NumUserDataRegs

    void Serialize(Serialization::Archive& ar) const;
    bool Deserialize(Serialization::Archive& ar);
};

} // namespace Shader
