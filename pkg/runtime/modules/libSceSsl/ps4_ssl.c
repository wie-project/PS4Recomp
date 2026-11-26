// SPDX-License-Identifier: GPL-2.0-or-later

// libSceSsl, following shadPS4. shadPS4 registers the "libSceSsl" library from
// src/core/libraries/network/ssl2.cpp (module libSceSsl2.sprx); its ssl.cpp is not loaded
// (libs.cpp keeps it commented out behind a title workaround), so ssl2.cpp is the reference.
//
// ssl2.cpp has logic in three functions: sceSslInit hands out increasing context ids, and
// sceSslGetCaCerts / sceSslFreeCaCerts report and release one dummy certificate. Every other
// function is a stub there ("(STUBBED) called", returns ORBIS_OK) and is a stub here.
#include "ps4_ssl.h"
#include "ps4_hle_stub.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Guest blocks sceSslGetCaCerts allocated: the OrbisSslData array (certs) and the block size.
// sceSslFreeCaCerts releases only blocks found here, so a stray pointer is never unmapped.
typedef struct {
    uint64_t certs;
    size_t size;
} SslCertBlock;

static pthread_mutex_t g_ssl_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_ssl_last_id;
static SslCertBlock *g_ssl_blocks;
static size_t g_ssl_block_count;
static size_t g_ssl_block_cap;

int32_t ps4_ssl_init(void) {
    // shadPS4: "return a value >1" from a counter that starts at 0, so the first id is 1.
    pthread_mutex_lock(&g_ssl_mutex);
    const int32_t id = ++g_ssl_last_id;
    pthread_mutex_unlock(&g_ssl_mutex);
    return id;
}

int32_t ps4_ssl_get_ca_certs(GuestContext *ctx, OrbisSslCaCerts *certs) {
    if (!certs) {
        return ORBIS_SSL_ERROR_INVALID_ARGUMENT;
    }
    // One guest block holds the OrbisSslData entry followed by its data, "dummy" with the
    // terminator (shadPS4 mallocs strlen + 1 bytes and records that length).
    const uint64_t data_len = sizeof(PS4_SSL_DUMMY_CERT);
    const size_t size = sizeof(OrbisSslData) + (size_t)data_len;
    pthread_mutex_lock(&g_ssl_mutex);
    if (g_ssl_block_count == g_ssl_block_cap) {
        const size_t cap = g_ssl_block_cap ? g_ssl_block_cap * 2 : 4;
        SslCertBlock *grown = realloc(g_ssl_blocks, cap * sizeof(*grown));
        if (!grown) {
            pthread_mutex_unlock(&g_ssl_mutex);
            fprintf(stderr, "[libSceSsl] sceSslGetCaCerts: out of host memory\n");
            abort(); // shadPS4's new / malloc have no failure path either
        }
        g_ssl_blocks = grown;
        g_ssl_block_cap = cap;
    }
    const uint64_t block = recomp_vm_alloc(ctx, size);
    if (block == 0 || block == (uint64_t)-1) {
        pthread_mutex_unlock(&g_ssl_mutex);
        fprintf(stderr, "[libSceSsl] sceSslGetCaCerts: out of guest memory\n");
        abort();
    }
    g_ssl_blocks[g_ssl_block_count++] = (SslCertBlock){block, size};
    pthread_mutex_unlock(&g_ssl_mutex);

    uint8_t *host = ctx->mem_base + block;
    const OrbisSslData entry = {block + sizeof(OrbisSslData), data_len};
    memcpy(host, &entry, sizeof(entry));
    memcpy(host + sizeof(entry), PS4_SSL_DUMMY_CERT, (size_t)data_len);
    certs->certs = block;
    certs->num = 1;
    certs->pool = 0;
    return 0;
}

int32_t ps4_ssl_free_ca_certs(GuestContext *ctx, OrbisSslCaCerts *certs) {
    if (!certs) {
        return ORBIS_SSL_ERROR_INVALID_ARGUMENT;
    }
    // shadPS4 frees every entry's data and then the array, and leaves the structure as it is.
    // Both live in the one block sceSslGetCaCerts allocated.
    if (certs->certs != 0) {
        size_t size = 0;
        pthread_mutex_lock(&g_ssl_mutex);
        for (size_t i = 0; i < g_ssl_block_count; i++) {
            if (g_ssl_blocks[i].certs == certs->certs) {
                size = g_ssl_blocks[i].size;
                g_ssl_blocks[i] = g_ssl_blocks[--g_ssl_block_count];
                break;
            }
        }
        pthread_mutex_unlock(&g_ssl_mutex);
        if (size != 0) {
            recomp_vm_free(ctx, certs->certs, size);
        }
    }
    return 0;
}

static OrbisSslCaCerts *ssl_ca_certs_arg(GuestContext *ctx, uint64_t addr) {
    return (addr && addr + sizeof(OrbisSslCaCerts) <= ctx->mem_size) ? (OrbisSslCaCerts *)(ctx->mem_base + addr)
                                                                      : NULL;
}

// int sceSslInit(size_t poolSize)
void shim_sceSslInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ps4_ssl_init();
    SHIM_RETURN();
}

// int sceSslGetCaCerts(int sslCtxId, OrbisSslCaCerts *certs)
void shim_sceSslGetCaCerts(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ps4_ssl_get_ca_certs(ctx, ssl_ca_certs_arg(ctx, ctx->rsi));
    SHIM_RETURN();
}

// int sceSslFreeCaCerts(int sslCtxId, OrbisSslCaCerts *certs)
void shim_sceSslFreeCaCerts(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ps4_ssl_free_ca_certs(ctx, ssl_ca_certs_arg(ctx, ctx->rsi));
    SHIM_RETURN();
}

// Stubs in shadPS4's ssl2.cpp, in its registration order.
PS4_HLE_STUB(CA_MGMT_extractKeyBlobEx)
PS4_HLE_STUB(CA_MGMT_extractPublicKeyInfo)
PS4_HLE_STUB(CA_MGMT_freeKeyBlob)
PS4_HLE_STUB(CRYPTO_initAsymmetricKey)
PS4_HLE_STUB(CRYPTO_uninitAsymmetricKey)
PS4_HLE_STUB(RSA_verifySignature)
PS4_HLE_STUB(sceSslCheckRecvPending)
PS4_HLE_STUB(sceSslClose)
PS4_HLE_STUB(sceSslConnect)
PS4_HLE_STUB(sceSslCreateConnection)
PS4_HLE_STUB(sceSslCreateSslConnection)
PS4_HLE_STUB(sceSslDeleteConnection)
PS4_HLE_STUB(sceSslDeleteSslConnection)
PS4_HLE_STUB(sceSslDisableOption)
PS4_HLE_STUB(sceSslDisableOptionInternal)
PS4_HLE_STUB(sceSslDisableOptionInternalInsecure)
PS4_HLE_STUB(sceSslDisableVerifyOption)
PS4_HLE_STUB(sceSslEnableOption)
PS4_HLE_STUB(sceSslEnableOptionInternal)
PS4_HLE_STUB(sceSslEnableVerifyOption)
PS4_HLE_STUB(sceSslFreeCaList)
PS4_HLE_STUB(sceSslFreeSslCertName)
PS4_HLE_STUB(sceSslGetAlpnSelected)
PS4_HLE_STUB(sceSslGetCaList)
PS4_HLE_STUB(sceSslGetFingerprint)
PS4_HLE_STUB(sceSslGetIssuerName)
PS4_HLE_STUB(sceSslGetMemoryPoolStats)
PS4_HLE_STUB(sceSslGetNameEntryCount)
PS4_HLE_STUB(sceSslGetNameEntryInfo)
PS4_HLE_STUB(sceSslGetNanoSSLModuleId)
PS4_HLE_STUB(sceSslGetNotAfter)
PS4_HLE_STUB(sceSslGetNotBefore)
PS4_HLE_STUB(sceSslGetPeerCert)
PS4_HLE_STUB(sceSslGetPem)
PS4_HLE_STUB(sceSslGetSerialNumber)
PS4_HLE_STUB(sceSslGetSslError)
PS4_HLE_STUB(sceSslGetSubjectName)
PS4_HLE_STUB(sceSslLoadCert)
PS4_HLE_STUB(sceSslLoadRootCACert)
PS4_HLE_STUB(sceSslRead)
PS4_HLE_STUB(sceSslRecv)
PS4_HLE_STUB(sceSslReuseConnection)
PS4_HLE_STUB(sceSslSend)
PS4_HLE_STUB(sceSslSetAlpn)
PS4_HLE_STUB(sceSslSetMinSslVersion)
PS4_HLE_STUB(sceSslSetSslVersion)
PS4_HLE_STUB(sceSslSetVerifyCallback)
PS4_HLE_STUB(sceSslTerm)
PS4_HLE_STUB(sceSslUnloadCert)
PS4_HLE_STUB(sceSslWrite)
PS4_HLE_STUB(VLONG_freeVlongQueue)
PS4_HLE_STUB(Func_22E76E60BC0587D7)
PS4_HLE_STUB(Func_28F8791A771D39C7)
