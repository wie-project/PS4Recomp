// SPDX-License-Identifier: GPL-2.0-or-later

// libSceSsl against shadPS4's ssl2.cpp, through the shims on host memory: increasing context
// ids from sceSslInit, the dummy certificate of sceSslGetCaCerts in guest memory and its release
// by sceSslFreeCaCerts, and the stubs returning 0.

#include "ps4_ssl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../host_guest_mem.h"

// Stub shims, defined by PS4_HLE_STUB in ps4_ssl.c.
void shim_sceSslTerm(GuestContext *ctx);
void shim_sceSslGetPem(GuestContext *ctx);
void shim_sceSslGetNameEntryCount(GuestContext *ctx);
void shim_sceSslConnect(GuestContext *ctx);
void shim_CA_MGMT_freeKeyBlob(GuestContext *ctx);
void shim_Func_28F8791A771D39C7(GuestContext *ctx);

// Runtime symbols the sources link against.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

// Guest memory blocks the module allocates and frees, as the runtime's VM would hand them out.
static uint64_t g_alloc_addr, g_alloc_size, g_freed_addr, g_freed_size;
static int g_frees;

uint64_t recomp_vm_alloc(GuestContext *ctx, size_t size) {
    (void)ctx;
    void *p = calloc(1, size);
    if (!p) return (uint64_t)-1;
    g_alloc_addr = A(p);
    g_alloc_size = size;
    return g_alloc_addr;
}

int recomp_vm_free(GuestContext *ctx, uint64_t addr, size_t size) {
    (void)ctx;
    g_freed_addr = addr;
    g_freed_size = size;
    g_frees++;
    free(host_ptr(addr));
    return 0;
}

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

static int32_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1) {
    GuestContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mem_base = GUEST_MEM;
    ctx.mem_size = ~0ull;
    ctx.rdi = a0;
    ctx.rsi = a1;
    ctx.rax = 99;
    shim(&ctx);
    return (int32_t)ctx.rax;
}

static int case_init(void) {
    // A counter that starts at 0 and is incremented before use.
    CHECK(call(shim_sceSslInit, 0x10000, 0) == 1);
    CHECK(call(shim_sceSslInit, 0, 0) == 2);
    CHECK(call(shim_sceSslInit, 0x40000, 0) == 3);
    printf("PASS init\n");
    return 0;
}

static int case_ca_certs(void) {
    CHECK(call(shim_sceSslGetCaCerts, 1, 0) == ORBIS_SSL_ERROR_INVALID_ARGUMENT);
    CHECK(call(shim_sceSslFreeCaCerts, 1, 0) == ORBIS_SSL_ERROR_INVALID_ARGUMENT);

    OrbisSslCaCerts certs;
    memset(&certs, 0xAB, sizeof(certs));
    CHECK(call(shim_sceSslGetCaCerts, 1, A(&certs)) == 0);
    CHECK(certs.num == 1);
    CHECK(certs.pool == 0);
    CHECK(certs.certs == g_alloc_addr);
    const OrbisSslData *entry = host_ptr(certs.certs);
    CHECK(entry->size == sizeof(PS4_SSL_DUMMY_CERT));
    CHECK(memcmp(host_ptr(entry->ptr), PS4_SSL_DUMMY_CERT, sizeof(PS4_SSL_DUMMY_CERT)) == 0);
    const uint64_t block = certs.certs;
    const uint64_t size = g_alloc_size;

    // The structure is left as it is; the block is released once.
    CHECK(call(shim_sceSslFreeCaCerts, 1, A(&certs)) == 0);
    CHECK(g_frees == 1 && g_freed_addr == block && g_freed_size == size);
    CHECK(certs.certs == block && certs.num == 1);
    CHECK(call(shim_sceSslFreeCaCerts, 1, A(&certs)) == 0);
    CHECK(g_frees == 1);

    // No array, or one the library did not allocate: nothing to release.
    OrbisSslCaCerts empty = {0, 0, 0};
    CHECK(call(shim_sceSslFreeCaCerts, 1, A(&empty)) == 0);
    uint8_t foreign[32];
    OrbisSslCaCerts stray = {A(foreign), 1, 0};
    CHECK(call(shim_sceSslFreeCaCerts, 1, A(&stray)) == 0);
    CHECK(g_frees == 1);

    // Every call allocates a new block.
    OrbisSslCaCerts a, b;
    CHECK(call(shim_sceSslGetCaCerts, 1, A(&a)) == 0);
    CHECK(call(shim_sceSslGetCaCerts, 1, A(&b)) == 0);
    CHECK(a.certs != b.certs);
    CHECK(call(shim_sceSslFreeCaCerts, 1, A(&a)) == 0 && g_freed_addr == a.certs);
    CHECK(call(shim_sceSslFreeCaCerts, 1, A(&b)) == 0 && g_freed_addr == b.certs);
    CHECK(g_frees == 3);
    printf("PASS ca-certs\n");
    return 0;
}

static int case_stubs(void) {
    // shadPS4 stubs: 0, arguments untouched.
    uint64_t len = 7;
    CHECK(call(shim_sceSslTerm, 1, 0) == 0);
    CHECK(call(shim_sceSslGetPem, 0, A(&len)) == 0);
    CHECK(len == 7);
    CHECK(call(shim_sceSslGetNameEntryCount, 0, A(&len)) == 0);
    CHECK(len == 7);
    CHECK(call(shim_sceSslConnect, 1, 2) == 0);
    CHECK(call(shim_CA_MGMT_freeKeyBlob, 0, 0) == 0);
    CHECK(call(shim_Func_28F8791A771D39C7, 0, 0) == 0);
    printf("PASS stubs\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "init") == 0) return case_init();
    if (strcmp(argv[1], "ca-certs") == 0) return case_ca_certs();
    if (strcmp(argv[1], "stubs") == 0) return case_stubs();
    return 2;
}
