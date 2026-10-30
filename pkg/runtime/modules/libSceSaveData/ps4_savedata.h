#ifndef PS4_SAVEDATA_H
#define PS4_SAVEDATA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// PS4 SaveData error codes (aligned with shadPS4 and official PS4 SDK)
#define ORBIS_SAVE_DATA_ERROR_PARAMETER        ((int32_t)0x809f0000)
#define ORBIS_SAVE_DATA_ERROR_NOT_INITIALIZED  ((int32_t)0x809f0001)
#define ORBIS_SAVE_DATA_ERROR_BUSY             ((int32_t)0x809f0003)
#define ORBIS_SAVE_DATA_ERROR_NOT_MOUNTED      ((int32_t)0x809f0004)
#define ORBIS_SAVE_DATA_ERROR_EXISTS           ((int32_t)0x809f0007)
#define ORBIS_SAVE_DATA_ERROR_NOT_FOUND        ((int32_t)0x809f0008)
#define ORBIS_SAVE_DATA_ERROR_INTERNAL         ((int32_t)0x809f000f)
#define ORBIS_SAVE_DATA_ERROR_MOUNT_FULL       ((int32_t)0x809f0010)
#define ORBIS_SAVE_DATA_ERROR_NO_SPACE_FS      ((int32_t)0x809f0012)
#define ORBIS_SAVE_DATA_ERROR_MEMORY_NOT_READY ((int32_t)0x809f0022)

// Compatibility aliases
#define SD_ERR_PARAMETER     ORBIS_SAVE_DATA_ERROR_PARAMETER
#define SD_ERR_NOT_INIT      ORBIS_SAVE_DATA_ERROR_NOT_INITIALIZED
#define SD_ERR_BUSY          ORBIS_SAVE_DATA_ERROR_BUSY
#define SD_ERR_NOT_MOUNTED   ORBIS_SAVE_DATA_ERROR_NOT_MOUNTED
#define SD_ERR_EXISTS        ORBIS_SAVE_DATA_ERROR_EXISTS
#define SD_ERR_NOT_FOUND     ORBIS_SAVE_DATA_ERROR_NOT_FOUND
#define SD_ERR_INTERNAL      ORBIS_SAVE_DATA_ERROR_INTERNAL
#define SD_ERR_MOUNT_FULL    ORBIS_SAVE_DATA_ERROR_MOUNT_FULL
#define SD_ERR_NO_SPACE_FS   ORBIS_SAVE_DATA_ERROR_NO_SPACE_FS
#define SD_ERR_MEM_NOT_READY ORBIS_SAVE_DATA_ERROR_MEMORY_NOT_READY

// Mount mode flags
#define SD_MOUNT_MODE_RDONLY       0x01u
#define SD_MOUNT_MODE_RDWR         0x02u
#define SD_MOUNT_MODE_CREATE       0x04u
#define SD_MOUNT_MODE_DESTRUCT_OFF 0x08u
#define SD_MOUNT_MODE_COPY_ICON    0x10u
#define SD_MOUNT_MODE_CREATE2      0x20u

#define SD_MOUNT_STATUS_NOTHING 0u
#define SD_MOUNT_STATUS_CREATED 1u

#define SD_TITLE_ID_SIZE  16
#define SD_DIRNAME_SIZE   32
#define SD_MOUNTPT_SIZE   16
#define SD_MAX_MOUNTS     16
#define SD_BLOCK_SIZE     32768ULL
#define SD_BLOCKS_MIN     96ULL    /* 3 MiB */
#define SD_BLOCKS_MAX     32768ULL /* 1 GiB */
#define SD_DEFAULT_BLOCKS 32768ULL /* 1 GiB */
#define SD_PARAM_FILE     "sce_sys/param.bin"
#define SD_PARAM_SFO      "sce_sys/param.sfo"
#define SD_ICON_FILE      "sce_sys/icon0.png"

// Param types for sceSaveDataSetParam / sceSaveDataGetParam
enum {
    SD_PARAM_ALL        = 0,
    SD_PARAM_TITLE      = 1,
    SD_PARAM_SUB_TITLE  = 2,
    SD_PARAM_DETAIL     = 3,
    SD_PARAM_USER_PARAM = 4,
    SD_PARAM_MTIME      = 5,
};

// Sort keys for sceSaveDataDirNameSearch
enum {
    SD_SORT_DIRNAME     = 0,
    SD_SORT_USER_PARAM  = 1,
    SD_SORT_BLOCKS      = 2,
    SD_SORT_MTIME       = 3,
    SD_SORT_FREE_BLOCKS = 5,
};

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

// OrbisSaveDataParam (1328 bytes)
typedef struct Ps4SaveDataParam {
    char title[128];
    char subTitle[128];
    char detail[1024];
    uint32_t userParam;
    int32_t _pad0;
    int64_t mtime;
    uint8_t _reserved[32];
} Ps4SaveDataParam;

typedef Ps4SaveDataParam SdParam;

typedef struct SdTitleId {
    char data[SD_TITLE_ID_SIZE];
} SdTitleId;

typedef struct SdDirName {
    char data[SD_DIRNAME_SIZE];
} SdDirName;

typedef struct SdMountPoint {
    char data[SD_MOUNTPT_SIZE];
} SdMountPoint;

typedef struct SdFingerprint {
    char data[65];
    uint8_t _pad[15];
} SdFingerprint;

// SDK 1.x Mount struct
typedef struct SdMount {
    int32_t userId;
    int32_t _pad0;
    uint64_t titleId;      // const SdTitleId*
    uint64_t dirName;      // const SdDirName*
    uint64_t fingerprint;  // const SdFingerprint*
    uint64_t blocks;
    uint32_t mountMode;
    uint8_t _reserved[32];
} SdMount;

// SDK 2.x Mount struct
typedef struct SdMount2 {
    int32_t userId;
    int32_t _pad0;
    uint64_t dirName;      // const SdDirName*
    uint64_t blocks;
    uint32_t mountMode;
    uint8_t _reserved[32];
    int32_t _pad1;
} SdMount2;

typedef struct SdMountResult {
    SdMountPoint mountPoint;
    uint64_t requiredBlocks;
    uint32_t _unused;
    uint32_t mountStatus;
    uint8_t _reserved[28];
    int32_t _pad0;
} SdMountResult;

typedef struct SdIcon {
    uint64_t buf;
    uint64_t bufSize;
    uint64_t dataSize;
    uint8_t _reserved[32];
} SdIcon;

typedef struct SdMountInfo {
    uint64_t blocks;
    uint64_t freeBlocks;
    uint8_t _reserved[32];
} SdMountInfo;

typedef struct SdDelete {
    int32_t userId;
    int32_t _pad0;
    uint64_t titleId;
    uint64_t dirName;
    uint32_t _unused;
    uint8_t _reserved[32];
    int32_t _pad1;
} SdDelete;

typedef struct SdDirNameSearchCond {
    int32_t userId;
    int32_t _pad0;
    uint64_t titleId;
    uint64_t dirName;      // pattern, '%' = any run, '_' = any char
    uint32_t key;
    uint32_t order;
    uint8_t _reserved[32];
} SdDirNameSearchCond;

typedef struct SdSearchInfo {
    uint64_t blocks;
    uint64_t freeBlocks;
    uint8_t _reserved[32];
} SdSearchInfo;

typedef struct SdDirNameSearchResult {
    uint32_t hitNum;
    int32_t _pad0;
    uint64_t dirNames;     // SdDirName*
    uint32_t dirNamesNum;
    uint32_t setNum;       // SDK >= 1.7
    uint64_t params;       // SdParam*          (SDK >= 1.7)
    uint64_t infos;        // SdSearchInfo*     (SDK >= 2.5)
    uint8_t _reserved[12];
    int32_t _pad1;
} SdDirNameSearchResult;

// Host-side view of one save directory, for the save data dialog's list.
bool ps4_savedata_read_dir_info(int32_t user_id, const char *dir_name,
                                Ps4SaveDataParam *out_param, uint64_t *out_bytes);

// C API
int32_t sceSaveDataInitialize(const void *param);
int32_t sceSaveDataInitialize2(const void *param);
int32_t sceSaveDataInitialize3(const void *param);
int32_t sceSaveDataTerminate(void);

int32_t sceSaveDataMount(GuestContext *ctx, const SdMount *mount, SdMountResult *result);
int32_t sceSaveDataMount2(GuestContext *ctx, const SdMount2 *mount, SdMountResult *result);
int32_t sceSaveDataUmount(GuestContext *ctx, uint64_t mount_point_addr);
int32_t sceSaveDataUmountWithBackup(GuestContext *ctx, uint64_t mount_point_addr);
int32_t sceSaveDataSetParam(GuestContext *ctx, uint64_t mount_point_addr, uint32_t param_type,
                            uint64_t buf_addr, uint64_t buf_size);
int32_t sceSaveDataGetParam(GuestContext *ctx, uint64_t mount_point_addr, uint32_t param_type,
                            uint64_t buf_addr, uint64_t buf_size, uint64_t got_size_addr);
int32_t sceSaveDataSaveIcon(GuestContext *ctx, uint64_t mount_point_addr, const SdIcon *icon);
int32_t sceSaveDataLoadIcon(GuestContext *ctx, uint64_t mount_point_addr, SdIcon *icon);
int32_t sceSaveDataGetMountInfo(GuestContext *ctx, uint64_t mount_point_addr, SdMountInfo *info);
int32_t sceSaveDataDelete(GuestContext *ctx, const SdDelete *del);
int32_t sceSaveDataDirNameSearch(GuestContext *ctx, const SdDirNameSearchCond *cond,
                                 SdDirNameSearchResult *result);

int32_t sceSaveDataSetupSaveDataMemory(GuestContext *ctx, int32_t userId, size_t memorySize, uint64_t param_addr);
int32_t sceSaveDataGetSaveDataMemory(GuestContext *ctx, int32_t userId, uint64_t buf_addr, size_t bufSize, int64_t offset);
int32_t sceSaveDataSetSaveDataMemory(GuestContext *ctx, int32_t userId, uint64_t buf_addr, size_t bufSize, int64_t offset);

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

void shim_sceSaveDataMount(GuestContext *ctx);
void shim_sceSaveDataMount2(GuestContext *ctx);
void shim_sceSaveDataUmount(GuestContext *ctx);
void shim_sceSaveDataUmountWithBackup(GuestContext *ctx);
void shim_sceSaveDataSetParam(GuestContext *ctx);
void shim_sceSaveDataGetParam(GuestContext *ctx);
void shim_sceSaveDataSaveIcon(GuestContext *ctx);
void shim_sceSaveDataLoadIcon(GuestContext *ctx);
void shim_sceSaveDataGetMountInfo(GuestContext *ctx);
void shim_sceSaveDataDelete(GuestContext *ctx);
void shim_sceSaveDataDirNameSearch(GuestContext *ctx);

void shim_sceSaveDataSetupSaveDataMemory(GuestContext *ctx);
void shim_sceSaveDataGetSaveDataMemory(GuestContext *ctx);
void shim_sceSaveDataSetSaveDataMemory(GuestContext *ctx);

void shim_sceSaveDataSetupSaveDataMemory2(GuestContext *ctx);
void shim_sceSaveDataGetSaveDataMemory2(GuestContext *ctx);
void shim_sceSaveDataSetSaveDataMemory2(GuestContext *ctx);
void shim_sceSaveDataSyncSaveDataMemory(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SAVEDATA_H
