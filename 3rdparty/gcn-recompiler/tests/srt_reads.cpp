// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "shader_recompiler/ir/passes/srt.h"

namespace {
using Shader::LoopCompare;
using Shader::SrtOp;
constexpr u32 Op(SrtOp op) { return static_cast<u32>(op); }

[[noreturn]] void Fail(const char* test, const char* check) {
    std::fprintf(stderr, "FAIL: %s: %s\n", test, check);
    std::exit(EXIT_FAILURE);
}

// Explicit checks remain active in the Release/NDEBUG configuration.
void Check(bool condition, const char* test, const char* check) {
    if (!condition) Fail(test, check);
}

struct Read {
    u64 addr;
    u64 size;
    bool operator==(const Read&) const = default;
};

struct Fixture {
    const char* name;
    std::array<u8, 4096> memory{};
    std::array<u32, 16> ud{};
    std::array<u32, 256> flat{};
    std::vector<Read> observed;
    std::vector<Read> requested;
    std::vector<u64> failures;

    explicit Fixture(const char* test) : name(test) { flat.fill(0xCCCCCCCCu); }

    template <typename T> void Put(u64 addr, T value) {
        Check(addr <= memory.size() && sizeof(T) <= memory.size() - addr,
              name, "fixture write in range");
        std::memcpy(memory.data() + addr, &value, sizeof(value));
    }

    void Pointer(u32 reg, u64 addr) {
        ud[reg] = static_cast<u32>(addr);
        ud[reg + 1] = static_cast<u32>(addr >> 32);
    }

    void Buffer(u32 vs, u64 addr, u32 size, u32 stride = 0) {
        flat[vs] = static_cast<u32>(addr);
        flat[vs + 1] = static_cast<u32>(addr >> 32) | (stride << 16);
        flat[vs + 2] = size;
        flat[vs + 3] = 0;
    }

    static void Observe(void* ctx, u64 addr, u64 size) {
        static_cast<Fixture*>(ctx)->observed.push_back({addr, size});
    }

    static bool ReadMemory(void* ctx, u64 addr, void* dst, u64 size) {
        auto& f = *static_cast<Fixture*>(ctx);
        f.requested.push_back({addr, size});
        Check(!f.observed.empty() && f.observed.back() == Read{addr, size},
              f.name, "observer runs before the memory reader");
        if (addr == 0 || addr >= f.memory.size() || size > f.memory.size() - addr ||
            std::find(f.failures.begin(), f.failures.end(), addr) != f.failures.end()) {
            // A failing reader need not preserve the destination. The walker must still return zero.
            std::memset(dst, 0xA5, static_cast<size_t>(size));
            return false;
        }
        std::memcpy(dst, f.memory.data() + addr, static_cast<size_t>(size));
        return true;
    }

    bool Run(std::initializer_list<u32> program) {
        return Shader::RunSrtProgram(std::span<const u32>(program.begin(), program.size()),
                                     ud.data(), flat.data(), ReadMemory, this, Observe, this);
    }

    void Trace(std::initializer_list<Read> expected) const {
        Check(observed == std::vector<Read>(expected), name, "exact ordered attempted-read trace");
        Check(requested == observed, name, "callback and observer traces agree");
    }
};

void ScalarAndPointers() {
    Fixture f("scalar, intermediate pointers, provenance and pointer stack");
    f.Pointer(0, 0xABCD000000000100ULL); // User pointers are masked to 48 bits.
    f.Put<u64>(0x108, 0xFFFF000000000200ULL);
    f.Put<u64>(0x20C, 0xDEAD000000000300ULL);
    f.Put<u32>(0x304, 0x11223344);
    f.Put<u32>(0x204, 0x55667788);
    f.Put<u32>(0x114, 0x99AABBCC);
    Check(f.Run({Op(SrtOp::PushPtrUd), 0,
                 Op(SrtOp::PushPtrImm), 2,
                 Op(SrtOp::Imm), 1, Op(SrtOp::Imm), 2, Op(SrtOp::Add),
                 Op(SrtOp::PushPtrDyn),
                 Op(SrtOp::Imm), 1, Op(SrtOp::CopyDyn), 32,
                 Op(SrtOp::StorePtr), 40,
                 Op(SrtOp::PopPtr), Op(SrtOp::CopyImm), 1, 33,
                 Op(SrtOp::PopPtr), Op(SrtOp::CopyImm), 5, 34,
                 Op(SrtOp::StorePtr), 40,
                 Op(SrtOp::End), Op(SrtOp::CopyImm), 0, 35}), f.name, "walker succeeds");
    Check(f.flat[32] == 0x11223344 && f.flat[33] == 0x55667788 &&
          f.flat[34] == 0x99AABBCC, f.name, "copied scalar values");
    Check(f.flat[40] == 0x100 && f.flat[41] == 0 && f.flat[35] == 0xCCCCCCCCu,
          f.name, "StorePtr and End semantics");
    // The final pointer is 0x100, not either intermediate pointer that supplied earlier reads.
    f.Trace({{0x108, 8}, {0x20C, 8}, {0x304, 4}, {0x204, 4}, {0x114, 4}});
}

void ValueOperations() {
    struct BinaryCase { SrtOp op; u32 a; u32 b; u32 expected; };
    constexpr BinaryCase cases[] = {
        {SrtOp::Add, 0xFFFFFFFFu, 3, 2}, {SrtOp::Sub, 5, 3, 2},
        {SrtOp::Mul, 2, 3, 6}, {SrtOp::Shl, 1, 33, 2},
        {SrtOp::Shr, 8, 34, 2}, {SrtOp::And, 7, 2, 2},
        {SrtOp::Or, 2, 4, 6}, {SrtOp::Xor, 7, 5, 2},
        {SrtOp::UMin, 0xFFFFFFFFu, 2, 2}, {SrtOp::UMax, 2, 6, 6},
    };
    for (const auto& c : cases) {
        Fixture f("binary dynamic offset");
        f.Pointer(0, 0x100);
        f.flat[20] = c.a;
        f.Put<u32>(0x100 + 4 * c.expected, 0x12345678);
        Check(f.Run({Op(SrtOp::PushPtrUd), 0, Op(SrtOp::LoadFlat), 20,
                     Op(SrtOp::Imm), c.b, Op(c.op), Op(SrtOp::CopyDyn), 32,
                     Op(SrtOp::End)}), f.name, "walker succeeds");
        Check(f.flat[32] == 0x12345678, f.name, "computed offset selects value");
        f.Trace({{0x100 + 4 * c.expected, 4}});
    }
    Fixture f("Not and BfeU dynamic offsets");
    f.Pointer(0, 0x100);
    f.Put<u32>(0x108, 22);
    f.Put<u32>(0x100, 11);
    Check(f.Run({Op(SrtOp::PushPtrUd), 0,
                 Op(SrtOp::Imm), 0xFFFFFFFDu, Op(SrtOp::Not), Op(SrtOp::CopyDyn), 32,
                 Op(SrtOp::Imm), 0x28, Op(SrtOp::Imm), 2, Op(SrtOp::Imm), 2,
                 Op(SrtOp::BfeU), Op(SrtOp::CopyDyn), 33,
                 Op(SrtOp::Imm), 2, Op(SrtOp::Imm), 0, Op(SrtOp::Imm), 32,
                 Op(SrtOp::BfeU), Op(SrtOp::CopyDyn), 34,
                 Op(SrtOp::Imm), 0xFFFFFFFFu, Op(SrtOp::Imm), 32, Op(SrtOp::Imm), 32,
                 Op(SrtOp::BfeU), Op(SrtOp::CopyDyn), 35, Op(SrtOp::End)}),
          f.name, "walker succeeds");
    Check(f.flat[32] == 22 && f.flat[33] == 22 && f.flat[34] == 22 && f.flat[35] == 11,
          f.name, "unary and bitfield results");
    f.Trace({{0x108, 4}, {0x108, 4}, {0x108, 4}, {0x100, 4}});
}

void BufferCopies() {
    Fixture f("buffer bounds, descriptor sizes and original provenance");
    f.Pointer(0, 0x300);
    f.Buffer(16, 0x100, 3, 4); // Three four-byte records, with non-address descriptor bits.
    f.Buffer(20, 0x200, 6);    // Stride zero means a byte size.
    f.Buffer(24, 0, 100);
    f.Put<u32>(0x104, 101);
    f.Put<u32>(0x108, 102);
    f.Put<u32>(0x200, 201);
    f.Put<u32>(0x300, 0x900);
    Check(f.Run({Op(SrtOp::CopyBufferDwords), 16, 0, 4, 3, 40, 0, 0,
                 Op(SrtOp::CopyBufferDwords), 20, 0, 0, 2, 44, 0, 0,
                 Op(SrtOp::CopyBufferDwords), 20, 4, 0, 2, 46, 0, 0,
                 Op(SrtOp::CopyBufferDwords), 24, 0, 0, 1, 48, 0, 0,
                 Op(SrtOp::PushPtrUd), 0, Op(SrtOp::CopyImm), 0, 16,
                 Op(SrtOp::End)}), f.name, "bounds exclusions do not fail reads");
    Check(f.flat[40] == 101 && f.flat[41] == 102 && f.flat[42] == 0 &&
          f.flat[44] == 201 && f.flat[45] == 0 && f.flat[46] == 201 &&
          f.flat[47] == 0 && f.flat[48] == 0 && f.flat[16] == 0x900,
          f.name, "buffer results and later descriptor overwrite");
    f.Trace({{0x104, 4}, {0x108, 4}, {0x200, 4}, {0x200, 4}, {0x300, 4}});
}

void Guards() {
    struct GuardCase { LoopCompare compare; u32 bound; u32 k; bool used; };
    constexpr GuardCase cases[] = {
        {LoopCompare::ULess, 0, 0, true}, {LoopCompare::ULess, 0, 1, false},
        {LoopCompare::ULess, 2, 1, true}, {LoopCompare::ULess, 2, 2, false},
        {LoopCompare::SLess, 0xFFFFFFFFu, 0, true},
        {LoopCompare::SLess, 0xFFFFFFFFu, 1, false},
        {LoopCompare::SLess, 2, 1, true}, {LoopCompare::SLess, 2, 2, false},
        {LoopCompare::NotEqual, 0, 0xFFFFFFFFu, true},
        {LoopCompare::NotEqual, 2, 1, true}, {LoopCompare::NotEqual, 2, 2, false},
    };
    for (const auto& c : cases) {
        Fixture f("table loop guard");
        f.Buffer(16, 0x100, 4);
        f.flat[30] = c.bound;
        f.Put<u32>(0x100, 77);
        Check(f.Run({Op(SrtOp::CopyBufferDwords), 16, 0, 0, 1, 40,
                     Shader::SrtTableGuard(30, c.compare), c.k, Op(SrtOp::End)}),
              f.name, "walker succeeds");
        Check(f.flat[40] == (c.used ? 77u : 0u), f.name, "guarded value");
        if (c.used) f.Trace({{0x100, 4}});
        else f.Trace({});
    }
}

void IndexedCopies() {
    Fixture f("collect distinct indices and copy indexed table");
    f.Buffer(16, 0x100, 4, 8);
    f.Buffer(20, 0x200, 28);
    f.Put<u32>(0x104, 2);
    f.Put<u32>(0x10C, 2);
    f.Put<u32>(0x114, 1);
    f.Put<u32>(0x11C, 0);
    f.Put<u32>(0x214, 51);
    f.Put<u32>(0x218, 52);
    Check(f.Run({Op(SrtOp::CollectIndices), 16, 0, 4, 8, 4, 40,
                 Op(SrtOp::CopyIndexedDwords), 20, 0, 1, 2, 42, 3, 60, 70,
                 // Wrap tfirst + tstride * index in 32 bits: UINT_MAX + 2 * 2 == 3.
                 Op(SrtOp::CopyIndexedDwords), 20, 16, 0xFFFFFFFFu, 2, 42, 2, 64, 71,
                 Op(SrtOp::End)}), f.name, "walker succeeds");
    Check(f.flat[40] == 3 && f.flat[41] == 0 && f.flat[42] == 2 && f.flat[43] == 1,
          f.name, "zero first, deduplication and record order");
    for (u32 i = 44; i <= 48; ++i) Check(f.flat[i] == 0, f.name, "unused indices zero");
    Check(f.flat[70] == 5 && f.flat[60] == 51 && f.flat[61] == 52 && f.flat[62] == 0 &&
          f.flat[71] == 3 && f.flat[64] == 0 && f.flat[65] == 0,
          f.name, "indexed offset, integer wrap and bounds");
    f.Trace({{0x104, 4}, {0x10C, 4}, {0x114, 4}, {0x11C, 4},
             {0x214, 4}, {0x218, 4}, {0x20C, 4}});
}

void CollectionLimits() {
    Fixture f("collection record limit and distinct-index overflow");
    f.Buffer(16, 0x100, 40);
    for (u32 i = 0; i < 10; ++i) f.Put<u32>(0x100 + 4 * i, i + 1);
    Check(f.Run({Op(SrtOp::CollectIndices), 16, 0, 0, 4, 9, 40,
                 Op(SrtOp::CollectIndices), 16, 0, 0, 4, 10, 60,
                 Op(SrtOp::CollectIndices), 16, 0, 0, 0, 10, 80,
                 Op(SrtOp::CollectIndices), 16, 3, 0, 4, 10, 100,
                 Op(SrtOp::End)}), f.name, "walker succeeds");
    Check(f.flat[40] == ~0u && f.flat[60] == Shader::SrtMaxTableIndices + 1 &&
          f.flat[80] == 1 && f.flat[100] == 1, f.name, "limit and empty scan counts");
    for (u32 i = 0; i < Shader::SrtMaxTableIndices; ++i) {
        Check(f.flat[41 + i] == 0, f.name, "over-limit output cleared");
        Check(f.flat[61 + i] == i, f.name, "retained distinct indices");
        Check(f.flat[81 + i] == 0 && f.flat[101 + i] == 0, f.name, "empty output cleared");
    }
    f.Trace({{0x100, 4}, {0x104, 4}, {0x108, 4}, {0x10C, 4},
             {0x110, 4}, {0x114, 4}, {0x118, 4}, {0x11C, 4}});
}

void FailedReads() {
    Fixture f("failed reads are observed, zeroed and do not abort the walker");
    f.Pointer(0, 0x100);
    f.Buffer(16, 0x200, 8);
    f.Buffer(20, 0x300, 8);
    f.failures = {0x100, 0x108, 0x200, 0x300};
    f.Put<u32>(0x204, 9);
    f.Put<u32>(0x304, 5);
    Check(!f.Run({Op(SrtOp::PushPtrUd), 0, Op(SrtOp::CopyImm), 0, 32,
                  Op(SrtOp::PushPtrImm), 2, Op(SrtOp::StorePtr), 34,
                  Op(SrtOp::CopyImm), 0, 36, Op(SrtOp::PopPtr),
                  Op(SrtOp::Imm), 2, Op(SrtOp::PushPtrDyn), Op(SrtOp::PopPtr),
                  Op(SrtOp::CopyBufferDwords), 16, 0, 0, 2, 40, 0, 0,
                  Op(SrtOp::CollectIndices), 20, 0, 0, 4, 2, 50,
                  Op(SrtOp::CopyIndexedDwords), 16, 0, 0, 1, 51, 2, 70, 80,
                  Op(SrtOp::End)}), f.name, "any failed read makes the result false");
    Check(f.flat[32] == 0 && f.flat[34] == 0 && f.flat[35] == 0 && f.flat[36] == 0 &&
          f.flat[40] == 0 && f.flat[41] == 9 && f.flat[50] == 2 &&
          f.flat[51] == 0 && f.flat[52] == 5 && f.flat[70] == 0 && f.flat[71] == 9,
          f.name, "failed scalar, pointer, table and index reads use zero");
    f.Trace({{0x100, 4}, {0x108, 8}, {0, 4}, {0x108, 8}, {0x200, 4},
             {0x204, 4}, {0x300, 4}, {0x304, 4}, {0x200, 4}, {0x204, 4}});

    Fixture missing("null reader still observes attempted reads");
    missing.Pointer(0, 0x100);
    const u32 program[] = {Op(SrtOp::PushPtrUd), 0, Op(SrtOp::CopyImm), 0, 32,
                           Op(SrtOp::PushPtrImm), 2, Op(SrtOp::StorePtr), 34, Op(SrtOp::End)};
    Check(!Shader::RunSrtProgram(program, missing.ud.data(), missing.flat.data(), nullptr, nullptr,
                                Fixture::Observe, &missing), missing.name, "null reader fails");
    Check(missing.flat[32] == 0 && missing.flat[34] == 0 && missing.flat[35] == 0,
          missing.name, "null reader outputs zero");
    Check(missing.observed == std::vector<Read>{{0x100, 4}, {0x108, 8}},
          missing.name, "null reader observes both widths");
}

bool ForbiddenReader(void*, u64, void*, u64) {
    Fail("direct memory fast path", "matching reader callback must never execute");
}

void DirectMemory() {
    Fixture f("direct memory fast path");
    f.Pointer(0, 0x100);
    f.Put<u64>(0x108, 0x200);
    f.Put<u32>(0x204, 88);
    Shader::g_srt_direct = {ForbiddenReader, &f, f.memory.data(), f.memory.size()};
    const u32 program[] = {Op(SrtOp::PushPtrUd), 0, Op(SrtOp::PushPtrImm), 2,
                           Op(SrtOp::CopyImm), 1, 32, Op(SrtOp::End)};
    Check(Shader::RunSrtProgram(program, f.ud.data(), f.flat.data(), ForbiddenReader, &f,
                               Fixture::Observe, &f), f.name, "direct read succeeds");
    Check(f.flat[32] == 88 && f.requested.empty(), f.name, "direct memory copied without callback");
    Check(f.observed == std::vector<Read>{{0x108, 8}, {0x204, 4}},
          f.name, "direct path retains read64 and read32 observer");
    // The default observer arguments preserve the existing non-observing call contract.
    Check(Shader::RunSrtProgram(program, f.ud.data(), f.flat.data(), ForbiddenReader, &f),
          f.name, "direct path without observer");
    f.observed.clear();
    f.Pointer(0, f.memory.size() - 4);
    Check(!Shader::RunSrtProgram(program, f.ud.data(), f.flat.data(), ForbiddenReader, &f,
                                Fixture::Observe, &f), f.name, "direct unmapped reads fail");
    Check(f.flat[32] == 0 && f.observed == std::vector<Read>{{4100, 8}, {4, 4}},
          f.name, "failed direct pointer observed at original address and zeroed");
    Shader::g_srt_direct = {};
}

} // namespace

int main() {
    // Restore process-global state even if this executable is embedded in a larger test runner.
    const auto saved_direct = Shader::g_srt_direct;
    Shader::g_srt_direct = {};
    ScalarAndPointers();
    ValueOperations();
    BufferCopies();
    Guards();
    IndexedCopies();
    CollectionLimits();
    FailedReads();
    DirectMemory();
    Shader::g_srt_direct = saved_direct;
    std::puts("SRT interpreter read regression tests passed");
    return EXIT_SUCCESS;
}
