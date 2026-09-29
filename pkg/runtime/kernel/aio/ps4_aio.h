// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_AIO_H
#define PS4_AIO_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_context.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ORBIS_KERNEL_AIO_STATE_SUBMITTED = 1,
    ORBIS_KERNEL_AIO_STATE_PROCESSING = 2,
    ORBIS_KERNEL_AIO_STATE_COMPLETED = 3,
    ORBIS_KERNEL_AIO_STATE_ABORTED = 4
};

typedef int32_t OrbisKernelAioSubmitId;

// Guest-visible result struct (16 bytes)
typedef struct OrbisKernelAioResult {
    int64_t returnValue;
    uint32_t state;
    uint32_t reserved;
} OrbisKernelAioResult;

// Guest-visible read/write command request struct (40 bytes)
typedef struct OrbisKernelAioRWRequest {
    int64_t offset;
    int64_t nbyte;
    uint64_t buf;      // Guest VA to buffer
    uint64_t result;   // Guest VA to OrbisKernelAioResult
    int32_t fd;
    int32_t reserved;
} OrbisKernelAioRWRequest;

// Subsystem lifecycle
void ps4_aio_init(void);
void ps4_aio_destroy(void);

// Host shims
void shim_sceKernelAioInitializeImpl(GuestContext *ctx);
void shim_sceKernelAioInitializeParam(GuestContext *ctx);
void shim_sceKernelAioSetParam(GuestContext *ctx);

void shim_sceKernelAioSubmitReadCommands(GuestContext *ctx);
void shim_sceKernelAioSubmitReadCommandsMultiple(GuestContext *ctx);
void shim_sceKernelAioSubmitWriteCommands(GuestContext *ctx);
void shim_sceKernelAioSubmitWriteCommandsMultiple(GuestContext *ctx);

void shim_sceKernelAioPollRequest(GuestContext *ctx);
void shim_sceKernelAioPollRequests(GuestContext *ctx);

void shim_sceKernelAioCancelRequest(GuestContext *ctx);
void shim_sceKernelAioCancelRequests(GuestContext *ctx);

void shim_sceKernelAioDeleteRequest(GuestContext *ctx);
void shim_sceKernelAioDeleteRequests(GuestContext *ctx);

void shim_sceKernelAioWaitRequest(GuestContext *ctx);
void shim_sceKernelAioWaitRequests(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_AIO_H
