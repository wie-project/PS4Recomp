// SPDX-License-Identifier: GPL-2.0-or-later
//
// libSceCoredump, after shadPS4 (src/core/libraries/kernel/coredump/coredump.{h,cpp},
// coredump_error.h).
#ifndef PS4_COREDUMP_H
#define PS4_COREDUMP_H

#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_COREDUMP_ERROR_PARAM                   ((int32_t)0x81180000)
#define ORBIS_COREDUMP_ERROR_NOT_REGISTERED          ((int32_t)0x81180001)
#define ORBIS_COREDUMP_ERROR_ALREADY_REGISTERED      ((int32_t)0x81180002)
#define ORBIS_COREDUMP_ERROR_NOT_IN_COREDUMP_HANDLER ((int32_t)0x81180003)
#define ORBIS_COREDUMP_ERROR_THREAD_CREATE           ((int32_t)0x81180004)
#define ORBIS_COREDUMP_ERROR_STOP_INFO_UNAVAILABLE   ((int32_t)0x81180005)

// Largest handler stack size sceCoredumpRegisterCoredumpHandler accepts (exclusive).
#define ORBIS_COREDUMP_MAX_STACK_SIZE 0x20000000ULL

// handler and common are guest addresses. The handler is only recorded: the console runs it on
// a thread of its own when the process crashes, and no coredump is ever produced on the host.
int32_t sceCoredumpRegisterCoredumpHandler(uint64_t handler, uint64_t stack_size, uint64_t common);
int32_t sceCoredumpUnregisterCoredumpHandler(void);

// The registered handler and its argument (guest addresses), 0 when none is registered.
uint64_t ps4_coredump_handler(void);
uint64_t ps4_coredump_common(void);

void shim_sceCoredumpRegisterCoredumpHandler(GuestContext *ctx);
void shim_sceCoredumpUnregisterCoredumpHandler(GuestContext *ctx);
void shim_sceCoredumpWriteUserData(GuestContext *ctx);
void shim_sceCoredumpAttachMemoryRegionAsUserFile(GuestContext *ctx);
void shim_sceCoredumpAttachMemoryRegion(GuestContext *ctx);
void shim_sceCoredumpSetUserDataType(GuestContext *ctx);
void shim_sceCoredumpGetStopInfoGpu(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_COREDUMP_H
