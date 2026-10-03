#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ps4_json2.h"

uint64_t recomp_vm_alloc(GuestContext *ctx, size_t size) {
    (void)ctx;
    static uint64_t bump = 0x80000;
    uint64_t ret = bump;
    bump += (size + 4095) & ~4095ULL;
    return ret;
}

int main(void) {
    printf("[ps4-json-test] Running sce::Json tests...\n");

    // Initialize mock GuestContext
    GuestContext ctx = {0};
    ctx.mem_size = 10 * 1024 * 1024;
    ctx.mem_base = (uint8_t *)calloc(1, ctx.mem_size);
    assert(ctx.mem_base != NULL);

    // 1. Test String
    uint64_t g_str = 0x10000;
    ctx.rdi = g_str;
    ctx.rsi = 0x10100;
    strcpy((char *)(ctx.mem_base + ctx.rsi), "HelloSilksong");
    shim__ZN3sce4Json6StringC1EPKc(&ctx);

    ctx.rdi = g_str;
    shim__ZNK3sce4Json6String6lengthEv(&ctx);
    assert(ctx.rax == 13);
    printf("[+] String::length() = %llu\n", (unsigned long long)ctx.rax);

    // 2. Test Value constructors & getters
    uint64_t g_val_int = 0x20000;
    ctx.rdi = g_val_int;
    ctx.rsi = 42;
    shim__ZN3sce4Json5ValueC1El(&ctx);

    ctx.rdi = g_val_int;
    shim__ZNK3sce4Json5Value7getTypeEv(&ctx);
    assert(ctx.rax == SCE_JSON_TYPE_INT);

    ctx.rdi = g_val_int;
    shim__ZNK3sce4Json5Value10getIntegerEv(&ctx);
    assert(ctx.rax == 42);
    printf("[+] Value(int64) = %lld\n", (long long)ctx.rax);

    uint64_t g_val_real = 0x21000;
    ctx.rdi = g_val_real;
    ctx.xmm[0].f64[0] = 3.14159;
    shim__ZN3sce4Json5ValueC1Ed(&ctx);

    ctx.rdi = g_val_real;
    shim__ZNK3sce4Json5Value7getRealEv(&ctx);
    assert(ctx.xmm[0].f64[0] > 3.14 && ctx.xmm[0].f64[0] < 3.15);
    printf("[+] Value(double) = %g\n", ctx.xmm[0].f64[0]);

    uint64_t g_val_bool = 0x22000;
    ctx.rdi = g_val_bool;
    ctx.rsi = 1;
    shim__ZN3sce4Json5ValueC1Eb(&ctx);

    ctx.rdi = g_val_bool;
    shim__ZNK3sce4Json5Value10getBooleanEv(&ctx);
    assert(ctx.rax == 1);
    printf("[+] Value(bool) = %llu\n", (unsigned long long)ctx.rax);

    // 3. Test Array & push_back
    uint64_t g_val_arr = 0x23000;
    ctx.rdi = g_val_arr;
    shim__ZN3sce4Json5ValueC1Ev(&ctx);

    ctx.rdi = g_val_arr;
    shim__ZN3sce4Json5Value10referArrayEv(&ctx);
    uint64_t g_arr = ctx.rax;

    // Push int
    ctx.rdi = g_arr;
    ctx.rsi = g_val_int;
    shim__ZN3sce4Json5Array9push_backERKNS0_5ValueE(&ctx);

    // Push real
    ctx.rdi = g_arr;
    ctx.rsi = g_val_real;
    shim__ZN3sce4Json5Array9push_backERKNS0_5ValueE(&ctx);

    ctx.rdi = g_arr;
    shim__ZNK3sce4Json5Array4sizeEv(&ctx);
    assert(ctx.rax == 2);
    printf("[+] Array::size() = %llu\n", (unsigned long long)ctx.rax);

    // Array indexing
    ctx.rdi = g_val_arr;
    ctx.rsi = 0;
    shim__ZNK3sce4Json5ValueixEm(&ctx);
    uint64_t item0_addr = ctx.rax;
    ctx.rdi = item0_addr;
    shim__ZNK3sce4Json5Value10getIntegerEv(&ctx);
    assert(ctx.rax == 42);
    printf("[+] Array[0] value = %lld\n", (long long)ctx.rax);

    // 4. Test Serialization
    uint64_t g_out_str = 0x30000;
    ctx.rdi = g_out_str;
    shim__ZN3sce4Json6StringC1Ev(&ctx);

    ctx.rdi = g_val_arr;
    ctx.rsi = g_out_str;
    shim__ZN3sce4Json5Value9serializeERNS0_6StringE(&ctx);

    ctx.rdi = g_out_str;
    shim__ZNK3sce4Json6String5c_strEv(&ctx);
    const char *serialized = (const char *)(ctx.mem_base + ctx.rax);
    assert(serialized != NULL);
    printf("[+] Serialized JSON Array: %s\n", serialized);
    assert(strstr(serialized, "42") != NULL);

    // 5. Test DiscMap Func_7C980FFB0AA27E7A
    uint64_t g_flags = 0x40000;
    uint64_t g_ret1 = 0x40008;
    uint64_t g_ret2 = 0x40010;
    ctx.rcx = g_flags;
    ctx.r8 = g_ret1;
    ctx.r9 = g_ret2;
    shim_Func_7C980FFB0AA27E7A(&ctx);
    assert(ctx.rax == 0);
    assert(*(int *)(ctx.mem_base + g_flags) == 0);
    assert(*(int *)(ctx.mem_base + g_ret1) == 0);
    assert(*(int *)(ctx.mem_base + g_ret2) == 0);
    printf("[+] Func_7C980FFB0AA27E7A DiscMap: PASSED\n");

    free(ctx.mem_base);
    printf("[ps4-json-test] ALL TESTS PASSED!\n");
    return 0;
}
