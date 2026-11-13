// Host test for the libSceNgs2 create entry points that take a caller-supplied context buffer
// (sceNgs2SystemCreate, sceNgs2RackCreate). Build and run:
//   clang++ -std=c++17 -I pkg/runtime/core -I pkg/runtime/modules/libSceNgs2 \
//     tests/ngs2_test/main.cpp pkg/runtime/modules/libSceNgs2/ps4_ngs2.cpp -o /tmp/ngs2_test && /tmp/ngs2_test

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ps4_ngs2.h"
#include "recomp_context.h"

// The caller-buffer variants must neither run guest code nor allocate guest memory. An empty
// dispatch table makes any guest call abort with "Unresolved indirect jump/call".
extern "C" recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE] = {};

extern "C" uint64_t recomp_vm_alloc_named_aligned(GuestContext *, uint64_t size, uint64_t, int, int, const char *) {
    fprintf(stderr, "[ngs2-test] FAIL: runtime allocation of %llu bytes\n", (unsigned long long)size);
    abort();
}

namespace {

constexpr uint64_t kStack = 0x80000;   // guest stack top
constexpr uint64_t kInfo = 0x10000;    // OrbisNgs2ContextBufferInfo
constexpr uint64_t kOut = 0x11000;     // out handle
constexpr uint64_t kSysInfo = 0x12000; // OrbisNgs2SystemInfo / OrbisNgs2RackInfo
constexpr uint64_t kSysBuf = 0x20000;  // system context buffer
constexpr uint64_t kRackBuf = 0x40000; // rack context buffer
constexpr uint64_t kBufSize = 0x20000; // fits the default 512-sample system context

// Calls a shim the way guest code does: with a return address on the stack.
uint32_t Call(GuestContext &ctx, void (*shim)(GuestContext *), uint64_t a, uint64_t b, uint64_t c,
              uint64_t d = 0, uint64_t e = 0) {
    ctx.rsp = kStack - 8;
    ctx.rdi = a;
    ctx.rsi = b;
    ctx.rdx = c;
    ctx.rcx = d;
    ctx.r8 = e;
    shim(&ctx);
    assert(ctx.rsp == kStack);
    return (uint32_t)ctx.rax;
}

void SetBufferInfo(GuestContext &ctx, uint64_t host_buffer, uint64_t size) {
    auto *info = (OrbisNgs2ContextBufferInfo *)(ctx.mem_base + kInfo);
    memset(info, 0, sizeof(*info));
    info->hostBuffer = host_buffer;
    info->hostBufferSize = size;
}

OrbisNgs2Handle OutHandle(GuestContext &ctx) {
    return *(OrbisNgs2Handle *)(ctx.mem_base + kOut);
}

} // namespace

int main() {
    GuestContext ctx = {};
    ctx.mem_size = 1 << 20;
    ctx.mem_base = (uint8_t *)calloc(1, ctx.mem_size);
    assert(ctx.mem_base);

    // sceNgs2SystemCreate: invalid buffer info is rejected.
    assert(Call(ctx, shim_sceNgs2SystemCreate, 0, 0, kOut) == ORBIS_NGS2_ERROR_INVALID_BUFFER_INFO);
    SetBufferInfo(ctx, 0, kBufSize);
    assert(Call(ctx, shim_sceNgs2SystemCreate, 0, kInfo, kOut) == ORBIS_NGS2_ERROR_INVALID_BUFFER_ADDRESS);
    SetBufferInfo(ctx, kSysBuf, 16);
    assert(Call(ctx, shim_sceNgs2SystemCreate, 0, kInfo, kOut) == ORBIS_NGS2_ERROR_INVALID_BUFFER_SIZE);
    assert(Call(ctx, shim_sceNgs2SystemCreate, 0, kInfo, 0) == ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS);
    printf("[+] sceNgs2SystemCreate rejects invalid buffer info\n");

    // sceNgs2SystemCreate: the context lives in the caller's buffer.
    SetBufferInfo(ctx, kSysBuf, kBufSize);
    assert(Call(ctx, shim_sceNgs2SystemCreate, 0, kInfo, kOut) == 0);
    OrbisNgs2Handle sys = OutHandle(ctx);
    assert(sys != 0);
    assert(Call(ctx, shim_sceNgs2SystemGetInfo, sys, kSysInfo, sizeof(OrbisNgs2SystemInfo)) == 0);
    const auto *sys_info = (const OrbisNgs2SystemInfo *)(ctx.mem_base + kSysInfo);
    assert(sys_info->systemHandle == sys && sys_info->sampleRate == 48000 && sys_info->rackCount == 0);
    printf("[+] sceNgs2SystemCreate uses the caller buffer\n");

    // sceNgs2RackCreate: invalid buffer info is rejected.
    assert(Call(ctx, shim_sceNgs2RackCreate, sys, 0x1000, 0, 0, kOut) == ORBIS_NGS2_ERROR_INVALID_BUFFER_INFO);
    SetBufferInfo(ctx, 0, kBufSize);
    assert(Call(ctx, shim_sceNgs2RackCreate, sys, 0x1000, 0, kInfo, kOut) == ORBIS_NGS2_ERROR_INVALID_BUFFER_ADDRESS);
    SetBufferInfo(ctx, kRackBuf, 16);
    assert(Call(ctx, shim_sceNgs2RackCreate, sys, 0x1000, 0, kInfo, kOut) == ORBIS_NGS2_ERROR_INVALID_BUFFER_SIZE);
    SetBufferInfo(ctx, kRackBuf, kBufSize);
    assert(Call(ctx, shim_sceNgs2RackCreate, sys + 1000, 0x1000, 0, kInfo, kOut) ==
           ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE);
    printf("[+] sceNgs2RackCreate rejects invalid arguments\n");

    // sceNgs2RackCreate: this used to treat bufferInfo as an allocator and jump to hostBuffer.
    assert(Call(ctx, shim_sceNgs2RackCreate, sys, 0x1000, 0, kInfo, kOut) == 0);
    OrbisNgs2Handle rack = OutHandle(ctx);
    assert(rack != 0 && rack != sys);
    assert(Call(ctx, shim_sceNgs2RackGetInfo, rack, kSysInfo, sizeof(OrbisNgs2RackInfo)) == 0);
    const auto *rack_info = (const OrbisNgs2RackInfo *)(ctx.mem_base + kSysInfo);
    assert(rack_info->rackHandle == rack && rack_info->ownerSystemHandle == sys);
    assert(rack_info->rackId == 0x1000 && rack_info->maxVoices == 64);
    assert(Call(ctx, shim_sceNgs2SystemGetInfo, sys, kSysInfo, sizeof(OrbisNgs2SystemInfo)) == 0);
    assert(sys_info->rackCount == 1);
    printf("[+] sceNgs2RackCreate uses the caller buffer\n");

    assert(Call(ctx, shim_sceNgs2RackDestroy, rack, 0, 0) == 0);
    assert(Call(ctx, shim_sceNgs2SystemDestroy, sys, 0, 0) == 0);
    printf("[ngs2-test] all passed\n");
    free(ctx.mem_base);
    return 0;
}
