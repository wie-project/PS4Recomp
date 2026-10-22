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

#define ORBIS_PLAYGO_ERROR_UNKNOWN             ((int32_t)0x80B20001)
#define ORBIS_PLAYGO_ERROR_FATAL               ((int32_t)0x80B20002)
#define ORBIS_PLAYGO_ERROR_NO_MEMORY           ((int32_t)0x80B20003)
#define ORBIS_PLAYGO_ERROR_INVALID_ARGUMENT    ((int32_t)0x80B20004)
#define ORBIS_PLAYGO_ERROR_NOT_INITIALIZED     ((int32_t)0x80B20005)
#define ORBIS_PLAYGO_ERROR_ALREADY_INITIALIZED ((int32_t)0x80B20006)
#define ORBIS_PLAYGO_ERROR_ALREADY_STARTED     ((int32_t)0x80B20007)
#define ORBIS_PLAYGO_ERROR_NOT_STARTED         ((int32_t)0x80B20008)
#define ORBIS_PLAYGO_ERROR_BAD_HANDLE          ((int32_t)0x80B20009)
#define ORBIS_PLAYGO_ERROR_BAD_POINTER         ((int32_t)0x80B2000A)
#define ORBIS_PLAYGO_ERROR_BAD_SIZE            ((int32_t)0x80B2000B)
#define ORBIS_PLAYGO_ERROR_BAD_CHUNK_ID        ((int32_t)0x80B2000C)
#define ORBIS_PLAYGO_ERROR_BAD_SPEED           ((int32_t)0x80B2000D)
#define ORBIS_PLAYGO_ERROR_NOT_SUPPORT_PLAYGO  ((int32_t)0x80B2000E)
#define ORBIS_PLAYGO_ERROR_EPERM               ((int32_t)0x80B2000F)
#define ORBIS_PLAYGO_ERROR_BAD_LOCUS           ((int32_t)0x80B20010)
#define ORBIS_PLAYGO_ERROR_NEED_DATA_DISC      ((int32_t)0x80B20011)

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
int32_t scePlayGoGetChunkId(OrbisPlayGoHandle handle, OrbisPlayGoChunkId *outChunkIdList, uint32_t numberOfEntries, uint32_t *outEntries);

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
void shim_scePlayGoGetChunkId(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_PLAYGO_H
