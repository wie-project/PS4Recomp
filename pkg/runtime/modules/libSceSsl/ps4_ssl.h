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
int32_t sceSslGetMemoryPoolStats(void *currentStat);
int32_t sceSslGetNameEntryCount(void *certName, uint32_t *count);
int32_t sceSslGetNameEntryInfo(void *certName, uint32_t index, void *entryInfo);
int32_t sceSslGetIssuerName(void *cert, void **issuerName);
int32_t sceSslGetSubjectName(void *cert, void **subjectName);
int32_t sceSslGetSerialNumber(void *cert, void *serialNumber);
int32_t sceSslGetPem(void *cert, char *pemBuf, size_t *pemLen);
int32_t sceSslFreeSslCertName(void *certName);
int32_t sceSslFreeCaCerts(void *caCerts);

// Shims
void shim_sceSslInit(GuestContext *ctx);
void shim_sceSslTerm(GuestContext *ctx);
void shim_sceSslGetCaCerts(GuestContext *ctx);
void shim_sceSslGetMemoryPoolStats(GuestContext *ctx);
void shim_sceSslGetNameEntryCount(GuestContext *ctx);
void shim_sceSslGetNameEntryInfo(GuestContext *ctx);
void shim_sceSslGetIssuerName(GuestContext *ctx);
void shim_sceSslGetSubjectName(GuestContext *ctx);
void shim_sceSslGetSerialNumber(GuestContext *ctx);
void shim_sceSslGetPem(GuestContext *ctx);
void shim_sceSslFreeSslCertName(GuestContext *ctx);
void shim_sceSslFreeCaCerts(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SSL_H
