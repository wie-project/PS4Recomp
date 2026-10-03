#ifndef PS4_SAVEDATA_H
#define PS4_SAVEDATA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// PS4 SaveData error codes
#define ORBIS_SAVE_DATA_ERROR_NOT_INITIALIZED  ((int32_t)0x809f0001)
#define ORBIS_SAVE_DATA_ERROR_PARAMETER        ((int32_t)0x809f0002)
#define ORBIS_SAVE_DATA_ERROR_NOT_FOUND        ((int32_t)0x809f0004)
#define ORBIS_SAVE_DATA_ERROR_BUSY             ((int32_t)0x809f0009)
#define ORBIS_SAVE_DATA_ERROR_NO_SPACE_FS      ((int32_t)0x809f0012)
#define ORBIS_SAVE_DATA_ERROR_MEMORY_NOT_READY ((int32_t)0x809f0022)
#define ORBIS_SAVE_DATA_ERROR_INTERNAL         ((int32_t)0x809f00ff)

// SaveDataMemory structures (aligned with PS4 SysV ABI)
typedef struct OrbisSaveDataMemoryData {
    uint64_t buf;         // Guest pointer / host pointer to data
    size_t bufSize;
    int64_t offset;
    uint8_t _reserved[40];
} OrbisSaveDataMemoryData;

typedef struct OrbisSaveDataMemoryGet2 {
    int32_t userId;
    uint8_t _pad[4];
    uint64_t data;        // Guest pointer to OrbisSaveDataMemoryData
    uint64_t param;       // Guest pointer to OrbisSaveDataParam
    uint64_t icon;        // Guest pointer to OrbisSaveDataIcon
    uint32_t slotId;
    uint8_t _reserved[28];
} OrbisSaveDataMemoryGet2;

typedef struct OrbisSaveDataMemorySet2 {
    int32_t userId;
    uint8_t _pad[4];
    uint64_t data;        // Guest pointer to const OrbisSaveDataMemoryData
    uint64_t param;       // Guest pointer to const OrbisSaveDataParam
    uint64_t icon;        // Guest pointer to const OrbisSaveDataIcon
    uint32_t dataNum;
    uint32_t slotId;
    uint8_t _reserved[32];
} OrbisSaveDataMemorySet2;

typedef struct OrbisSaveDataMemorySetup2 {
    uint32_t option;
    int32_t userId;
    size_t memorySize;
    size_t iconMemorySize;
    uint64_t initParam;   // Guest pointer to const OrbisSaveDataParam
    uint64_t initIcon;    // Guest pointer to const OrbisSaveDataIcon
    uint32_t slotId;
    uint8_t _reserved[20];
} OrbisSaveDataMemorySetup2;

typedef struct OrbisSaveDataMemorySetupResult {
    size_t existedMemorySize;
    uint8_t _reserved[16];
} OrbisSaveDataMemorySetupResult;

typedef enum OrbisSaveDataMemorySyncOption {
    ORBIS_SAVE_DATA_MEMORY_SYNC_OPTION_NONE     = 0,
    ORBIS_SAVE_DATA_MEMORY_SYNC_OPTION_BLOCKING = 1,
} OrbisSaveDataMemorySyncOption;

typedef struct OrbisSaveDataMemorySync {
    int32_t userId;
    uint32_t slotId;
    uint32_t option;
    uint8_t _reserved[28];
} OrbisSaveDataMemorySync;

// C API
int32_t sceSaveDataInitialize(const void *param);
int32_t sceSaveDataInitialize2(const void *param);
int32_t sceSaveDataInitialize3(const void *param);
int32_t sceSaveDataTerminate(void);

int32_t sceSaveDataSetupSaveDataMemory2(GuestContext *ctx,
                                       const OrbisSaveDataMemorySetup2 *setupParam,
                                       OrbisSaveDataMemorySetupResult *result);

int32_t sceSaveDataGetSaveDataMemory2(GuestContext *ctx,
                                     OrbisSaveDataMemoryGet2 *getParam);

int32_t sceSaveDataSetSaveDataMemory2(GuestContext *ctx,
                                     const OrbisSaveDataMemorySet2 *setParam);

int32_t sceSaveDataSyncSaveDataMemory(GuestContext *ctx,
                                     OrbisSaveDataMemorySync *syncParam);

// Shims
void shim_sceSaveDataInitialize(GuestContext *ctx);
void shim_sceSaveDataInitialize2(GuestContext *ctx);
void shim_sceSaveDataInitialize3(GuestContext *ctx);
void shim_sceSaveDataTerminate(GuestContext *ctx);

void shim_sceSaveDataSetupSaveDataMemory2(GuestContext *ctx);
void shim_sceSaveDataGetSaveDataMemory2(GuestContext *ctx);
void shim_sceSaveDataSetSaveDataMemory2(GuestContext *ctx);
void shim_sceSaveDataSyncSaveDataMemory(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SAVEDATA_H
