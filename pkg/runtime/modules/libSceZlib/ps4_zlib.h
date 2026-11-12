// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_ZLIB_H
#define PS4_ZLIB_H

#include <stdint.h>
#include "../../core/recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_ZLIB_ERROR_NOT_FOUND           ((int32_t)0x81120002)
#define ORBIS_ZLIB_ERROR_BUSY                ((int32_t)0x8112000B)
#define ORBIS_ZLIB_ERROR_FAULT               ((int32_t)0x8112000E)
#define ORBIS_ZLIB_ERROR_INVALID             ((int32_t)0x81120016)
#define ORBIS_ZLIB_ERROR_NOSPACE             ((int32_t)0x8112001C)
#define ORBIS_ZLIB_ERROR_NOT_SUPPORTED       ((int32_t)0x81120025)
#define ORBIS_ZLIB_ERROR_TIMEDOUT            ((int32_t)0x81120027)
#define ORBIS_ZLIB_ERROR_NOT_INITIALIZED     ((int32_t)0x81120032)
#define ORBIS_ZLIB_ERROR_ALREADY_INITIALIZED ((int32_t)0x81120033)
#define ORBIS_ZLIB_ERROR_FATAL               ((int32_t)0x811200FF)

// C API
int32_t sceZlibInitialize(const void *buffer, uint32_t length);
int32_t sceZlibInflate(const void *src, uint32_t src_len, void *dst, uint32_t dst_len,
                       uint64_t *request_id);
int32_t sceZlibWaitForDone(uint64_t *request_id, const uint32_t *timeout);
int32_t sceZlibGetResult(uint64_t request_id, uint32_t *dst_length, int32_t *status);
int32_t sceZlibFinalize(void);

// Shims
void shim_sceZlibInitialize(GuestContext *ctx);
void shim_sceZlibInflate(GuestContext *ctx);
void shim_sceZlibWaitForDone(GuestContext *ctx);
void shim_sceZlibGetResult(GuestContext *ctx);
void shim_sceZlibFinalize(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_ZLIB_H
