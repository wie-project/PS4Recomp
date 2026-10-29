// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdexcept>
#include <string>

namespace GcnShim {

// Thrown by ASSERT/UNREACHABLE inside the recompiler; caught at the C API boundary.
struct CompileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using LogFn = void (*)(void* ctx, int level, const char* msg);

std::string& LastError();
void SetLogLevel(int level);
void SetLogCallback(LogFn fn, void* ctx);
void EmitLog(int level, const char* file, int line, const char* func, const char* msg);
void SetDumpDir(const char* dir);

} // namespace GcnShim
