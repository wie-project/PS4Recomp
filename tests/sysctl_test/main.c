// Host test for the sysctl shim (kern.arandom, hw.ncpu, hw.pagesize) and the page size that
// sysctl, sysconf(_SC_PAGESIZE) and getpagesize report. Build and run (from the
// repository root):
//   clang -std=c11 -I pkg/runtime/core -I pkg/runtime/kernel/syscalls tests/sysctl_test/main.c \
//     pkg/runtime/kernel/syscalls/ps4_sysctl.c -o /tmp/sysctl_test && /tmp/sysctl_test

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ps4_sysctl.h"
#include "recomp_context.h"

// The tested shim runs no guest code: an empty dispatch table makes any guest call abort.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

enum {
    kStack = 0x10000, // guest stack top
    kTls = 0x20000,   // fs_base; errno lives at fs_base + 0x100
    kName = 0x21000,  // MIB
    kLen = 0x22000,   // size_t oldlen
    kOld = 0x23000,   // old value buffer
    kNew = 0x24000,   // new value buffer
    kMem = 0x30000,
};

static int32_t call(GuestContext *ctx, uint64_t namelen, uint64_t old, uint64_t oldlen, uint64_t newp,
                    uint64_t newlen) {
    ctx->rsp = kStack - 8;
    ctx->rdi = kName;
    ctx->rsi = namelen;
    ctx->rdx = old;
    ctx->rcx = oldlen;
    ctx->r8 = newp;
    ctx->r9 = newlen;
    shim_sysctl(ctx);
    assert(ctx->rsp == kStack);
    return (int32_t)ctx->rax;
}

static int guest_errno(GuestContext *ctx) { return *(int *)(ctx->mem_base + kTls + 0x100); }

static void set_mib(GuestContext *ctx, int32_t a, int32_t b) {
    int32_t *mib = (int32_t *)(ctx->mem_base + kName);
    mib[0] = a;
    mib[1] = b;
}

int main(void) {
    GuestContext ctx = {0};
    ctx.mem_size = kMem;
    ctx.mem_base = calloc(1, ctx.mem_size);
    assert(ctx.mem_base);
    ctx.fs_base = kTls;
    uint64_t *len = (uint64_t *)(ctx.mem_base + kLen);
    uint8_t *old = ctx.mem_base + kOld;

    // hw.pagesize: the PS4 16 KiB page, as an int.
    set_mib(&ctx, PS4_CTL_HW, PS4_HW_PAGESIZE);
    *len = 4;
    assert(call(&ctx, 2, kOld, kLen, 0, 0) == 0);
    assert(*len == 4 && *(int32_t *)old == PS4_PAGE_SIZE);

    // A NULL old buffer reports the size only.
    *len = 0;
    assert(call(&ctx, 2, 0, kLen, 0, 0) == 0);
    assert(*len == 4);

    // A short buffer gets the bytes that fit and ENOMEM (FreeBSD sysctl_old_user).
    memset(old, 0xAA, 8);
    *len = 2;
    assert(call(&ctx, 2, kOld, kLen, 0, 0) == -1);
    assert(guest_errno(&ctx) == ENOMEM);
    assert(*len == 2 && old[0] == 0x00 && old[1] == 0x40 && old[2] == 0xAA);

    // Read-only OID: a new value is refused with EPERM before anything is read.
    *len = 4;
    assert(call(&ctx, 2, kOld, kLen, kNew, 4) == -1);
    assert(guest_errno(&ctx) == EPERM);

    // hw.ncpu: the host online CPU count, as the sysconf shim reports it.
    set_mib(&ctx, PS4_CTL_HW, PS4_HW_NCPU);
    *len = 4;
    assert(call(&ctx, 2, kOld, kLen, 0, 0) == 0);
    assert(*len == 4 && *(int32_t *)old == (int32_t)sysconf(_SC_NPROCESSORS_ONLN));

    // kern.arandom: random bytes, capped at 256 per call.
    set_mib(&ctx, PS4_CTL_KERN, PS4_KERN_ARND);
    memset(old, 0, 512);
    *len = 512;
    assert(call(&ctx, 2, kOld, kLen, 0, 0) == 0);
    assert(*len == 256);
    int nonzero = 0;
    for (int i = 0; i < 256; i++) nonzero += old[i] != 0;
    assert(nonzero > 200);
    for (int i = 256; i < 512; i++) assert(old[i] == 0);
    uint8_t first[32];
    memcpy(first, old, sizeof(first));
    *len = 32;
    assert(call(&ctx, 2, kOld, kLen, 0, 0) == 0);
    assert(*len == 32 && memcmp(first, old, sizeof(first)) != 0);

    // Name length outside [2, CTL_MAXNAME] is EINVAL.
    assert(call(&ctx, 1, kOld, kLen, 0, 0) == -1);
    assert(guest_errno(&ctx) == EINVAL);
    assert(call(&ctx, 25, kOld, kLen, 0, 0) == -1);
    assert(guest_errno(&ctx) == EINVAL);

    // A guest pointer past the end of guest memory is EFAULT.
    ctx.rsp = kStack - 8;
    ctx.rdi = kName;
    ctx.rsi = 2;
    ctx.rdx = kMem - 2;
    ctx.rcx = kLen;
    ctx.r8 = 0;
    ctx.r9 = 0;
    *len = 4;
    shim_sysctl(&ctx);
    assert((int32_t)ctx.rax == -1 && guest_errno(&ctx) == EFAULT);

    // Unmodelled OIDs are reported to the caller of ps4_sysctl, which stops on them.
    const int32_t osrel[2] = {PS4_CTL_KERN, 24}; // kern.osreldate
    uint64_t hlen = 4;
    int32_t hv;
    assert(ps4_sysctl(osrel, 2, &hv, &hlen, NULL, 0) == PS4_SYSCTL_UNSUPPORTED);
    const int32_t deep[3] = {PS4_CTL_HW, PS4_HW_NCPU, 0};
    assert(ps4_sysctl(deep, 3, &hv, &hlen, NULL, 0) == PS4_SYSCTL_UNSUPPORTED);

    // One page size on every path: the PS4 16 KiB page, whatever the host's is.
    assert(PS4_PAGE_SIZE == 16384);
    ctx.rsp = kStack - 8;
    shim_getpagesize(&ctx);
    assert(ctx.rsp == kStack && ctx.rax == PS4_PAGE_SIZE);
    ctx.rsp = kStack - 8;
    ctx.rdi = PS4_SC_PAGESIZE;
    shim_sysconf(&ctx);
    assert(ctx.rsp == kStack && ctx.rax == PS4_PAGE_SIZE);
    set_mib(&ctx, PS4_CTL_HW, PS4_HW_PAGESIZE);
    *len = 4;
    assert(call(&ctx, 2, kOld, kLen, 0, 0) == 0 && *(int32_t *)old == PS4_PAGE_SIZE);

    free(ctx.mem_base);
    printf("[sysctl-test] PASSED\n");
    return 0;
}
