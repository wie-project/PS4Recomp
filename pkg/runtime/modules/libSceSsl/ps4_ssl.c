#include "ps4_ssl.h"
#include <pthread.h>
#include <string.h>

static pthread_mutex_t g_ssl_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_ssl_next_id = 1;

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

// Shims
void shim_sceSslInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSslInit((size_t)ctx->rdi);
}

void shim_sceSslTerm(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSslTerm((int32_t)ctx->rdi);
}

void shim_sceSslGetCaCerts(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSslGetCaCerts((int32_t)ctx->rdi, (OrbisSslCaCerts *)ctx->rsi);
}
