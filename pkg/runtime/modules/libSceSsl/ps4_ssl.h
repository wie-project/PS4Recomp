// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_SSL_H
#define PS4_SSL_H

#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_SSL_ERROR_INVALID_ARGUMENT ((int32_t)0x8095F007)

// OrbisSslData as the guest sees it: a guest pointer to the bytes and their length.
typedef struct OrbisSslData {
    uint64_t ptr;
    uint64_t size;
} OrbisSslData;

// OrbisSslCaCerts: a guest pointer to an array of num OrbisSslData, and the pool pointer.
typedef struct OrbisSslCaCerts {
    uint64_t certs;
    uint64_t num;
    uint64_t pool;
} OrbisSslCaCerts;

// The one certificate sceSslGetCaCerts reports, as in shadPS4 (ssl2.cpp): a dummy entry whose
// data is this string with its terminator.
#define PS4_SSL_DUMMY_CERT "dummy"

int32_t ps4_ssl_init(void);
int32_t ps4_ssl_get_ca_certs(GuestContext *ctx, OrbisSslCaCerts *certs);
int32_t ps4_ssl_free_ca_certs(GuestContext *ctx, OrbisSslCaCerts *certs);

void shim_sceSslInit(GuestContext *ctx);
void shim_sceSslGetCaCerts(GuestContext *ctx);
void shim_sceSslFreeCaCerts(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SSL_H
