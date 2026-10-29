// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shim replacing shadPS4's core/emulator_settings.h for the standalone GCN recompiler.
// Only the flags the shader recompiler queries are provided. All are compile-time
// constants except shader dumping, which is toggled through gcn_set_dump_dir().

#pragma once

namespace GcnShim {

bool DumpShadersEnabled();

struct EmulatorSettingsShim {
    bool IsDumpShaders() const {
        return DumpShadersEnabled();
    }
    // Inline fetch shader is a debug path in shadPS4 (every fetch shader inline). The Metal
    // backend uses the parsed fetch-shader attribute description (vertex descriptor), and
    // inlines only a fetch shader with an attribute Metal's vertex fetch cannot read
    // (Gcn::IsVertexFetchable, Translator::EmitFetch).
    constexpr bool IsInlineFetchShader() const {
        return false;
    }
    // Direct memory access (BDA page table + fault buffer) needs
    // PhysicalStorageBuffer64, which SPIRV-Cross/Metal cannot express reliably. Disabled.
    constexpr bool IsDirectMemoryAccessEnabled() const {
        return false;
    }
};

inline constexpr EmulatorSettingsShim g_emulator_settings{};

} // namespace GcnShim

#define EmulatorSettings (::GcnShim::g_emulator_settings)
