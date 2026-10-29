// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Implementations of the small shims that replace shadPS4 infrastructure (logging, asserts,
// settings, paths, guest memory) for the standalone GCN recompiler.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <fmt/format.h>

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "compat/guest_memory.h"
#include "compat/shim.h"
#include "core/emulator_settings.h"

namespace Common::Log {

std::array<Level, NUM_LOG_CLASSES> g_class_levels = [] {
    std::array<Level, NUM_LOG_CLASSES> a;
    a.fill(Level::Error);
    return a;
}();

void VLog(Class log_class, Level level, const char* file, int line, const char* func,
          fmt::string_view format, fmt::format_args args) {
    std::string msg;
    try {
        msg = fmt::vformat(format, args);
    } catch (...) {
        msg = std::string(format.data(), format.size());
    }
    if (level >= Level::Error) {
        const char* base = file ? std::strrchr(file, '/') : nullptr;
        GcnShim::LastError() = fmt::format("{}:{} {}: {}", base ? base + 1 : (file ? file : "?"),
                                           line, func ? func : "", msg);
        auto& e = GcnShim::LastError();
        while (!e.empty() && e.back() == '\n') {
            e.pop_back();
        }
        std::replace(e.begin(), e.end(), '\n', ' ');
    }
    GcnShim::EmitLog(static_cast<int>(level), file, line, func, msg.c_str());
}

} // namespace Common::Log

void assert_fail_impl() {
    throw GcnShim::CompileError(GcnShim::LastError().empty() ? std::string("assertion failed")
                                                             : GcnShim::LastError());
}

[[noreturn]] void unreachable_impl() {
    throw GcnShim::CompileError(GcnShim::LastError().empty() ? std::string("unreachable code")
                                                             : GcnShim::LastError());
}

namespace GcnShim {

static std::mutex g_cfg_mutex;
static std::string g_dump_dir;
static std::atomic<bool> g_dump_enabled{false};
static LogFn g_log_fn = nullptr;
static void* g_log_ctx = nullptr;

std::string& LastError() {
    thread_local std::string s;
    return s;
}

void SetLogLevel(int level) {
    Common::Log::g_class_levels.fill(static_cast<Common::Log::Level>(level));
}

void SetLogCallback(LogFn fn, void* ctx) {
    std::scoped_lock lk{g_cfg_mutex};
    g_log_fn = fn;
    g_log_ctx = ctx;
}

void EmitLog(int level, const char* file, int line, const char* func, const char* msg) {
    LogFn fn;
    void* ctx;
    {
        std::scoped_lock lk{g_cfg_mutex};
        fn = g_log_fn;
        ctx = g_log_ctx;
    }
    if (fn) {
        fn(ctx, level, msg);
        return;
    }
    static const char* names[] = {"T", "D", "I", "W", "E", "C", "-"};
    const char* base = file ? std::strrchr(file, '/') : nullptr;
    std::fprintf(stderr, "[gcn %s] %s:%d %s: %s\n", names[level < 7 ? level : 6],
                 base ? base + 1 : (file ? file : "?"), line, func ? func : "", msg);
}

void SetDumpDir(const char* dir) {
    std::scoped_lock lk{g_cfg_mutex};
    if (dir && *dir) {
        g_dump_dir = dir;
        g_dump_enabled = true;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(g_dump_dir) / "dumps", ec);
    } else {
        g_dump_dir.clear();
        g_dump_enabled = false;
    }
}

bool DumpShadersEnabled() {
    return g_dump_enabled.load(std::memory_order_relaxed);
}

GuestMemory& CurrentGuestMemory() {
    thread_local GuestMemory gm;
    return gm;
}

namespace {
struct CodeCacheT {
    std::unordered_map<u64, std::vector<u32>> code;
};
CodeCacheT& CodeCache() {
    thread_local CodeCacheT c;
    return c;
}
} // namespace

void ClearGuestCodeCache() {
    CodeCache().code.clear();
}

GuestMemoryScope::~GuestMemoryScope() {
    CurrentGuestMemory() = prev;
    ClearGuestCodeCache();
}

const u32* GuestCode(u64 addr, u32 max_dwords) {
    auto& cache = CodeCache();
    const auto& gm = CurrentGuestMemory();
    if (auto it = cache.code.find(addr); it != cache.code.end()) {
        return it->second.data();
    }
    if (!gm.read) {
        return nullptr;
    }
    std::vector<u32> words;
    // Read in chunks; fall back to dword reads near the end of a mapping.
    constexpr u32 Chunk = 64;
    while (words.size() < max_dwords) {
        u32 buf[Chunk];
        if (gm.read(gm.ctx, addr + words.size() * 4, buf, sizeof(buf))) {
            words.insert(words.end(), buf, buf + Chunk);
            continue;
        }
        for (u32 i = 0; i < Chunk && words.size() < max_dwords; i++) {
            u32 w;
            if (!gm.read(gm.ctx, addr + words.size() * 4, &w, 4)) {
                break;
            }
            words.push_back(w);
        }
        break;
    }
    if (words.empty()) {
        return nullptr;
    }
    // Pad with s_endpgm so a decoder running off the end terminates.
    words.insert(words.end(), 16, 0xBF810000u);
    auto& v = cache.code[addr] = std::move(words);
    return v.data();
}

} // namespace GcnShim

namespace Common::FS {

std::filesystem::path GetUserPath(PathType) {
    std::scoped_lock lk{GcnShim::g_cfg_mutex};
    return GcnShim::g_dump_dir.empty() ? std::filesystem::path(".")
                                       : std::filesystem::path(GcnShim::g_dump_dir);
}

} // namespace Common::FS
