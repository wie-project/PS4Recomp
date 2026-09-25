#include "ps4_playgo.h"
#include <errno.h>

static const OrbisPlayGoHandle kValidPlayGoHandle = 1;

int32_t scePlayGoInitialize(const OrbisPlayGoInitParams *params) {
    (void)params;
    return 0;
}

int32_t scePlayGoTerminate(void) {
    return 0;
}

int32_t scePlayGoOpen(OrbisPlayGoHandle *handle, const void *params) {
    (void)params;
    if (!handle) {
        return -EINVAL;
    }
    *handle = kValidPlayGoHandle;
    return 0;
}

int32_t scePlayGoClose(OrbisPlayGoHandle handle) {
    (void)handle;
    return 0;
}

int32_t scePlayGoGetProgress(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count, OrbisPlayGoProgress *outProgress) {
    (void)handle;
    (void)chunkIds;
    if (!outProgress) {
        return -EINVAL;
    }
    for (uint32_t i = 0; i < count; i++) {
        outProgress[i].progressSize = 100000000ULL;
        outProgress[i].totalSize = 100000000ULL;
    }
    return 0;
}

int32_t scePlayGoGetLocus(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count, int8_t *outLocus) {
    (void)handle;
    (void)chunkIds;
    if (!outLocus) {
        return -EINVAL;
    }
    for (uint32_t i = 0; i < count; i++) {
        outLocus[i] = (int8_t)ORBIS_PLAYGO_LOCUS_LOCAL_FAST;
    }
    return 0;
}

int32_t scePlayGoGetLanguageMask(OrbisPlayGoHandle handle, OrbisPlayGoLanguageMask *outMask) {
    (void)handle;
    if (!outMask) {
        return -EINVAL;
    }
    *outMask = 0xFFFFFFFFFFFFFFFFULL;
    return 0;
}

int32_t scePlayGoSetLanguageMask(OrbisPlayGoHandle handle, OrbisPlayGoLanguageMask mask) {
    (void)handle;
    (void)mask;
    return 0;
}

int32_t scePlayGoGetInstallSpeed(OrbisPlayGoHandle handle, int32_t *outSpeed) {
    (void)handle;
    if (!outSpeed) {
        return -EINVAL;
    }
    *outSpeed = (int32_t)ORBIS_PLAYGO_INSTALL_SPEED_FULL;
    return 0;
}

int32_t scePlayGoSetInstallSpeed(OrbisPlayGoHandle handle, int32_t speed) {
    (void)handle;
    (void)speed;
    return 0;
}

int32_t scePlayGoGetToDoList(OrbisPlayGoHandle handle, OrbisPlayGoToDo *outToDoList, uint32_t maxEntries, uint32_t *outNumEntries) {
    (void)handle;
    (void)outToDoList;
    (void)maxEntries;
    if (!outNumEntries) {
        return -EINVAL;
    }
    *outNumEntries = 0;
    return 0;
}

int32_t scePlayGoGetEta(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count, OrbisPlayGoEta *outEta) {
    (void)handle;
    (void)chunkIds;
    if (!outEta) {
        return -EINVAL;
    }
    for (uint32_t i = 0; i < count; i++) {
        outEta[i] = 0;
    }
    return 0;
}

int32_t scePlayGoPrefetch(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count) {
    (void)handle;
    (void)chunkIds;
    (void)count;
    return 0;
}

// Guest ABI shims
void shim_scePlayGoInitialize(GuestContext *ctx) {
    uint64_t paramsGuest = ctx->rdi;
    const OrbisPlayGoInitParams *params = paramsGuest ? (const OrbisPlayGoInitParams *)(ctx->mem_base + paramsGuest) : NULL;
    int32_t ret = scePlayGoInitialize(params);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoTerminate(GuestContext *ctx) {
    int32_t ret = scePlayGoTerminate();
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoOpen(GuestContext *ctx) {
    uint64_t handleGuest = ctx->rdi;
    uint64_t paramsGuest = ctx->rsi;
    OrbisPlayGoHandle *handle = handleGuest ? (OrbisPlayGoHandle *)(ctx->mem_base + handleGuest) : NULL;
    const void *params = paramsGuest ? (const void *)(ctx->mem_base + paramsGuest) : NULL;

    int32_t ret = scePlayGoOpen(handle, params);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoClose(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    int32_t ret = scePlayGoClose(handle);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoGetProgress(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t chunkIdsGuest = ctx->rsi;
    uint32_t count = (uint32_t)ctx->rdx;
    uint64_t outProgressGuest = ctx->rcx;

    const OrbisPlayGoChunkId *chunkIds = chunkIdsGuest ? (const OrbisPlayGoChunkId *)(ctx->mem_base + chunkIdsGuest) : NULL;
    OrbisPlayGoProgress *outProgress = outProgressGuest ? (OrbisPlayGoProgress *)(ctx->mem_base + outProgressGuest) : NULL;

    int32_t ret = scePlayGoGetProgress(handle, chunkIds, count, outProgress);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoGetLocus(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t chunkIdsGuest = ctx->rsi;
    uint32_t count = (uint32_t)ctx->rdx;
    uint64_t outLocusGuest = ctx->rcx;

    const OrbisPlayGoChunkId *chunkIds = chunkIdsGuest ? (const OrbisPlayGoChunkId *)(ctx->mem_base + chunkIdsGuest) : NULL;
    int8_t *outLocus = outLocusGuest ? (int8_t *)(ctx->mem_base + outLocusGuest) : NULL;

    int32_t ret = scePlayGoGetLocus(handle, chunkIds, count, outLocus);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoGetLanguageMask(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t outMaskGuest = ctx->rsi;
    OrbisPlayGoLanguageMask *outMask = outMaskGuest ? (OrbisPlayGoLanguageMask *)(ctx->mem_base + outMaskGuest) : NULL;

    int32_t ret = scePlayGoGetLanguageMask(handle, outMask);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoSetLanguageMask(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    OrbisPlayGoLanguageMask mask = (OrbisPlayGoLanguageMask)ctx->rsi;

    int32_t ret = scePlayGoSetLanguageMask(handle, mask);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoGetInstallSpeed(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t outSpeedGuest = ctx->rsi;
    int32_t *outSpeed = outSpeedGuest ? (int32_t *)(ctx->mem_base + outSpeedGuest) : NULL;

    int32_t ret = scePlayGoGetInstallSpeed(handle, outSpeed);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoSetInstallSpeed(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    int32_t speed = (int32_t)ctx->rsi;

    int32_t ret = scePlayGoSetInstallSpeed(handle, speed);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoGetToDoList(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t outToDoGuest = ctx->rsi;
    uint32_t maxEntries = (uint32_t)ctx->rdx;
    uint64_t outNumGuest = ctx->rcx;

    OrbisPlayGoToDo *outToDo = outToDoGuest ? (OrbisPlayGoToDo *)(ctx->mem_base + outToDoGuest) : NULL;
    uint32_t *outNum = outNumGuest ? (uint32_t *)(ctx->mem_base + outNumGuest) : NULL;

    int32_t ret = scePlayGoGetToDoList(handle, outToDo, maxEntries, outNum);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoGetEta(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t chunkIdsGuest = ctx->rsi;
    uint32_t count = (uint32_t)ctx->rdx;
    uint64_t outEtaGuest = ctx->rcx;

    const OrbisPlayGoChunkId *chunkIds = chunkIdsGuest ? (const OrbisPlayGoChunkId *)(ctx->mem_base + chunkIdsGuest) : NULL;
    OrbisPlayGoEta *outEta = outEtaGuest ? (OrbisPlayGoEta *)(ctx->mem_base + outEtaGuest) : NULL;

    int32_t ret = scePlayGoGetEta(handle, chunkIds, count, outEta);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoPrefetch(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t chunkIdsGuest = ctx->rsi;
    uint32_t count = (uint32_t)ctx->rdx;

    const OrbisPlayGoChunkId *chunkIds = chunkIdsGuest ? (const OrbisPlayGoChunkId *)(ctx->mem_base + chunkIdsGuest) : NULL;

    int32_t ret = scePlayGoPrefetch(handle, chunkIds, count);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}
