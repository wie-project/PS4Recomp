#ifndef PS4_PLAYGO_H
#define PS4_PLAYGO_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

typedef uint32_t OrbisPlayGoHandle;
typedef uint16_t OrbisPlayGoChunkId;
typedef int64_t OrbisPlayGoEta;
typedef uint64_t OrbisPlayGoLanguageMask;

typedef enum OrbisPlayGoLocus {
    ORBIS_PLAYGO_LOCUS_NOT_DOWNLOADED = 0,
    ORBIS_PLAYGO_LOCUS_LOCAL_SLOW = 2,
    ORBIS_PLAYGO_LOCUS_LOCAL_FAST = 3,
} OrbisPlayGoLocus;

typedef enum OrbisPlayGoInstallSpeed {
    ORBIS_PLAYGO_INSTALL_SPEED_SUSPENDED = 0,
    ORBIS_PLAYGO_INSTALL_SPEED_TRICKLE = 1,
    ORBIS_PLAYGO_INSTALL_SPEED_FULL = 2,
} OrbisPlayGoInstallSpeed;

typedef struct OrbisPlayGoInitParams {
    const void *bufAddr;
    uint32_t bufSize;
    uint32_t reserved;
} OrbisPlayGoInitParams;

typedef struct OrbisPlayGoToDo {
    OrbisPlayGoChunkId chunkId;
    int8_t locus;
    int8_t reserved;
} OrbisPlayGoToDo;

typedef struct OrbisPlayGoProgress {
    uint64_t progressSize;
    uint64_t totalSize;
} OrbisPlayGoProgress;

#ifdef __cplusplus
extern "C" {
#endif

int32_t scePlayGoInitialize(const OrbisPlayGoInitParams *params);
int32_t scePlayGoTerminate(void);
int32_t scePlayGoOpen(OrbisPlayGoHandle *handle, const void *params);
int32_t scePlayGoClose(OrbisPlayGoHandle handle);
int32_t scePlayGoGetProgress(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count, OrbisPlayGoProgress *outProgress);
int32_t scePlayGoGetLocus(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count, int8_t *outLocus);
int32_t scePlayGoGetLanguageMask(OrbisPlayGoHandle handle, OrbisPlayGoLanguageMask *outMask);
int32_t scePlayGoSetLanguageMask(OrbisPlayGoHandle handle, OrbisPlayGoLanguageMask mask);
int32_t scePlayGoGetInstallSpeed(OrbisPlayGoHandle handle, int32_t *outSpeed);
int32_t scePlayGoSetInstallSpeed(OrbisPlayGoHandle handle, int32_t speed);
int32_t scePlayGoGetToDoList(OrbisPlayGoHandle handle, OrbisPlayGoToDo *outToDoList, uint32_t maxEntries, uint32_t *outNumEntries);
int32_t scePlayGoGetEta(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count, OrbisPlayGoEta *outEta);
int32_t scePlayGoPrefetch(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count);

// Guest ABI shims
void shim_scePlayGoInitialize(GuestContext *ctx);
void shim_scePlayGoTerminate(GuestContext *ctx);
void shim_scePlayGoOpen(GuestContext *ctx);
void shim_scePlayGoClose(GuestContext *ctx);
void shim_scePlayGoGetProgress(GuestContext *ctx);
void shim_scePlayGoGetLocus(GuestContext *ctx);
void shim_scePlayGoGetLanguageMask(GuestContext *ctx);
void shim_scePlayGoSetLanguageMask(GuestContext *ctx);
void shim_scePlayGoGetInstallSpeed(GuestContext *ctx);
void shim_scePlayGoSetInstallSpeed(GuestContext *ctx);
void shim_scePlayGoGetToDoList(GuestContext *ctx);
void shim_scePlayGoGetEta(GuestContext *ctx);
void shim_scePlayGoPrefetch(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_PLAYGO_H
