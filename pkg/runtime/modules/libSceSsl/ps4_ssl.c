#include "ps4_ssl.h"
#include <pthread.h>
#include <string.h>

static pthread_mutex_t g_ssl_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_ssl_next_id = 1;

static inline void *guest_to_host(const GuestContext *ctx, uint64_t addr) {
    if (!addr) return NULL;
    if (ctx && ctx->mem_base && addr < (1ULL << 39)) {
        return (void *)(ctx->mem_base + addr);
    }
    return (void *)addr;
}

int32_t sceSslInit(size_t poolSize) {
    (void)poolSize;
    pthread_mutex_lock(&g_ssl_mutex);
    int32_t id = g_ssl_next_id++;
    pthread_mutex_unlock(&g_ssl_mutex);
    return id;
}

int32_t sceSslTerm(int32_t sslCtxId) {
    (void)sslCtxId;
    return 0;
}

int32_t sceSslGetCaCerts(int32_t sslCtxId, OrbisSslCaCerts *certs) {
    (void)sslCtxId;
    if (certs) {
        certs->certs = NULL;
        certs->num = 0;
        certs->pool = NULL;
    }
    return 0;
}

int32_t sceSslGetMemoryPoolStats(void *currentStat) {
    (void)currentStat;
    return 0;
}

int32_t sceSslGetNameEntryCount(void *certName, uint32_t *count) {
    (void)certName;
    if (count) {
        *count = 0;
    }
    return 0;
}

int32_t sceSslGetNameEntryInfo(void *certName, uint32_t index, void *entryInfo) {
    (void)certName;
    (void)index;
    (void)entryInfo;
    return 0;
}

int32_t sceSslGetIssuerName(void *cert, void **issuerName) {
    (void)cert;
    if (issuerName) {
        *issuerName = NULL;
    }
    return 0;
}

int32_t sceSslGetSubjectName(void *cert, void **subjectName) {
    (void)cert;
    if (subjectName) {
        *subjectName = NULL;
    }
    return 0;
}

int32_t sceSslGetSerialNumber(void *cert, void *serialNumber) {
    (void)cert;
    (void)serialNumber;
    return 0;
}

int32_t sceSslGetPem(void *cert, char *pemBuf, size_t *pemLen) {
    (void)cert;
    (void)pemBuf;
    if (pemLen) {
        *pemLen = 0;
    }
    return 0;
}

int32_t sceSslFreeSslCertName(void *certName) {
    (void)certName;
    return 0;
}

int32_t sceSslFreeCaCerts(void *caCerts) {
    (void)caCerts;
    return 0;
}

// Shims
void shim_sceSslInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSslInit((size_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceSslTerm(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSslTerm((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceSslGetCaCerts(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSslGetCaCerts((int32_t)ctx->rdi, (OrbisSslCaCerts *)guest_to_host(ctx, ctx->rsi));
    SHIM_RETURN();
}

void shim_sceSslGetMemoryPoolStats(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSslGetMemoryPoolStats(guest_to_host(ctx, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceSslGetNameEntryCount(GuestContext *ctx) {
    void *certName = guest_to_host(ctx, ctx->rdi);
    uint32_t *count = (uint32_t *)guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)sceSslGetNameEntryCount(certName, count);
    SHIM_RETURN();
}

void shim_sceSslGetNameEntryInfo(GuestContext *ctx) {
    void *certName = guest_to_host(ctx, ctx->rdi);
    uint32_t index = (uint32_t)ctx->rsi;
    void *entryInfo = guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)sceSslGetNameEntryInfo(certName, index, entryInfo);
    SHIM_RETURN();
}

void shim_sceSslGetIssuerName(GuestContext *ctx) {
    void *cert = guest_to_host(ctx, ctx->rdi);
    void **issuerName = (void **)guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)sceSslGetIssuerName(cert, issuerName);
    SHIM_RETURN();
}

void shim_sceSslGetSubjectName(GuestContext *ctx) {
    void *cert = guest_to_host(ctx, ctx->rdi);
    void **subjectName = (void **)guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)sceSslGetSubjectName(cert, subjectName);
    SHIM_RETURN();
}

void shim_sceSslGetSerialNumber(GuestContext *ctx) {
    void *cert = guest_to_host(ctx, ctx->rdi);
    void *serialNumber = guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)sceSslGetSerialNumber(cert, serialNumber);
    SHIM_RETURN();
}

void shim_sceSslGetPem(GuestContext *ctx) {
    void *cert = guest_to_host(ctx, ctx->rdi);
    char *pemBuf = (char *)guest_to_host(ctx, ctx->rsi);
    size_t *pemLen = (size_t *)guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)sceSslGetPem(cert, pemBuf, pemLen);
    SHIM_RETURN();
}

void shim_sceSslFreeSslCertName(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSslFreeSslCertName(guest_to_host(ctx, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceSslFreeCaCerts(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSslFreeCaCerts(guest_to_host(ctx, ctx->rdi));
    SHIM_RETURN();
}
