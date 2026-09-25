#ifndef PS4_SSL_H
#define PS4_SSL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OrbisSslCaCerts {
    void *certs;
    int32_t num;
    void *pool;
} OrbisSslCaCerts;

// C API
int32_t sceSslInit(size_t poolSize);
int32_t sceSslTerm(int32_t sslCtxId);
int32_t sceSslGetCaCerts(int32_t sslCtxId, OrbisSslCaCerts *certs);

// Shims
void shim_sceSslInit(GuestContext *ctx);
void shim_sceSslTerm(GuestContext *ctx);
void shim_sceSslGetCaCerts(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SSL_H
