// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Guest memory access for the standalone GCN recompiler. shadPS4 dereferences guest GPU
// addresses directly (guest VA == host VA). PS4Recomp maps guest memory at an offset, so every
// such access in the recompiler goes through a reader callback installed per thread by the
// C API (gcn_recompiler.h) for the duration of a call.

#pragma once

#include <cstring>
#include "common/types.h"
#include "shader_recompiler/ir/passes/srt.h"

namespace GcnShim {

struct GuestMemory {
    Shader::SrtReadFn read{};
    void* ctx{};
};

GuestMemory& CurrentGuestMemory();

// RAII scope installing a reader for the current thread.
struct GuestMemoryScope {
    GuestMemory prev;
    GuestMemoryScope(Shader::SrtReadFn read, void* ctx) : prev{CurrentGuestMemory()} {
        CurrentGuestMemory() = {read, ctx};
    }
    ~GuestMemoryScope();
};

// Copies guest memory; zero-fills and returns false if unmapped / no reader.
inline bool ReadGuest(u64 addr, void* dst, u64 size) {
    const auto& gm = CurrentGuestMemory();
    if (Shader::SrtRead(gm.read, gm.ctx, addr, dst, size)) {
        return true;
    }
    std::memset(dst, 0, size);
    return false;
}

// Returns a host pointer to a copy of the GCN code at guest address `addr` (up to
// max_dwords, stopping at the first unreadable dword). The copy lives in a thread-local cache
// that stays valid until ClearGuestCodeCache() (called on GuestMemoryScope exit). Returns nullptr if nothing is readable.
const u32* GuestCode(u64 addr, u32 max_dwords = 4096);
void ClearGuestCodeCache();

} // namespace GcnShim
