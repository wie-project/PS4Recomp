// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shim replacing shadPS4's common/path_util.h. Only PathType::ShaderDir is used by the
// shader recompiler (for debug dumps); it resolves to the directory set via gcn_set_dump_dir().

#pragma once

#include <filesystem>

namespace Common::FS {

enum class PathType {
    ShaderDir,
};

// Dumps are written to GetUserPath(ShaderDir) / "dumps"; the shim returns the parent of the
// configured dump dir's "dumps" child, i.e. the configured dir itself is <ret>/dumps.
std::filesystem::path GetUserPath(PathType type);

} // namespace Common::FS
