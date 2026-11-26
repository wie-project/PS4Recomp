// SPDX-License-Identifier: GPL-2.0-or-later
//
// libSceCoredump. Registration follows shadPS4 (kernel/coredump/coredump.cpp): one handler per
// process, ORBIS_COREDUMP_ERROR_ALREADY_REGISTERED for a second one, ORBIS_COREDUMP_ERROR_PARAM
// for a null handler or a stack of 512 MiB or more, ORBIS_COREDUMP_ERROR_NOT_REGISTERED when
// unregistering without a handler. Like shadPS4, the handler is stored and never run: the
// console starts it on its own thread when the process crashes, and the host writes no dump.
//
// The other functions are not registered by shadPS4; Uncharted 4 imports them. They are only
// meaningful while a dump is being written, which never happens here:
//  * sceCoredumpWriteUserData, sceCoredumpAttachMemoryRegion(AsUserFile) and
//    sceCoredumpSetUserDataType add data to the dump being written: there is none, SCE_OK.
//  * sceCoredumpGetStopInfoGpu reads the GPU state at the time of the crash and is only valid
//    inside the handler: ORBIS_COREDUMP_ERROR_NOT_IN_COREDUMP_HANDLER, what the console answers
//    outside a dump.
#include "ps4_coredump.h"

#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t g_coredump_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_coredump_handler;
static uint64_t g_coredump_common;

int32_t sceCoredumpRegisterCoredumpHandler(uint64_t handler, uint64_t stack_size, uint64_t common) {
    pthread_mutex_lock(&g_coredump_lock);
    int32_t rc = 0;
    if (g_coredump_handler) {
        rc = ORBIS_COREDUMP_ERROR_ALREADY_REGISTERED;
    } else if (!handler || stack_size >= ORBIS_COREDUMP_MAX_STACK_SIZE) {
        rc = ORBIS_COREDUMP_ERROR_PARAM;
    } else {
        g_coredump_handler = handler;
        g_coredump_common = common;
    }
    pthread_mutex_unlock(&g_coredump_lock);
    if (rc != 0) {
        fprintf(stderr, "[libSceCoredump] RegisterCoredumpHandler(0x%llx, stack 0x%llx) -> 0x%08x\n",
                (unsigned long long)handler, (unsigned long long)stack_size, (unsigned)rc);
    }
    return rc;
}

int32_t sceCoredumpUnregisterCoredumpHandler(void) {
    pthread_mutex_lock(&g_coredump_lock);
    int32_t rc = 0;
    if (!g_coredump_handler) {
        rc = ORBIS_COREDUMP_ERROR_NOT_REGISTERED;
    } else {
        g_coredump_handler = 0;
    }
    pthread_mutex_unlock(&g_coredump_lock);
    return rc;
}

uint64_t ps4_coredump_handler(void) {
    pthread_mutex_lock(&g_coredump_lock);
    const uint64_t h = g_coredump_handler;
    pthread_mutex_unlock(&g_coredump_lock);
    return h;
}

uint64_t ps4_coredump_common(void) {
    pthread_mutex_lock(&g_coredump_lock);
    const uint64_t c = g_coredump_handler ? g_coredump_common : 0;
    pthread_mutex_unlock(&g_coredump_lock);
    return c;
}

// int sceCoredumpRegisterCoredumpHandler(SceCoredumpHandler handler, size_t stackSize, void *common)
void shim_sceCoredumpRegisterCoredumpHandler(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCoredumpRegisterCoredumpHandler(ctx->rdi, ctx->rsi, ctx->rdx);
    SHIM_RETURN();
}

// int sceCoredumpUnregisterCoredumpHandler(void)
void shim_sceCoredumpUnregisterCoredumpHandler(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCoredumpUnregisterCoredumpHandler();
    SHIM_RETURN();
}

void shim_sceCoredumpWriteUserData(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceCoredumpAttachMemoryRegionAsUserFile(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceCoredumpAttachMemoryRegion(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceCoredumpSetUserDataType(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceCoredumpGetStopInfoGpu(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ORBIS_COREDUMP_ERROR_NOT_IN_COREDUMP_HANDLER;
    SHIM_RETURN();
}
