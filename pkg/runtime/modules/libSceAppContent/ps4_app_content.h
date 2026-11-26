// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_APP_CONTENT_H
#define PS4_APP_CONTENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Error codes (shadPS4 app_content_error.h)
#define ORBIS_APP_CONTENT_ERROR_NOT_INITIALIZED    ((int32_t)0x80D90001)
#define ORBIS_APP_CONTENT_ERROR_PARAMETER          ((int32_t)0x80D90002)
#define ORBIS_APP_CONTENT_ERROR_BUSY               ((int32_t)0x80D90003)
#define ORBIS_APP_CONTENT_ERROR_NOT_FOUND          ((int32_t)0x80D90005)
#define ORBIS_APP_CONTENT_ERROR_DRM_NO_ENTITLEMENT ((int32_t)0x80D90007)

// AppParam IDs
#define ORBIS_APP_CONTENT_APPPARAM_ID_SKU_FLAG             0
#define ORBIS_APP_CONTENT_APPPARAM_ID_USER_DEFINED_PARAM_1 1
#define ORBIS_APP_CONTENT_APPPARAM_ID_USER_DEFINED_PARAM_2 2
#define ORBIS_APP_CONTENT_APPPARAM_ID_USER_DEFINED_PARAM_3 3
#define ORBIS_APP_CONTENT_APPPARAM_ID_USER_DEFINED_PARAM_4 4

// SKU Flags
#define ORBIS_APP_CONTENT_APPPARAM_SKU_FLAG_TRIAL 1
#define ORBIS_APP_CONTENT_APPPARAM_SKU_FLAG_FULL  3

// Buffers / Layout limits
#define ORBIS_APP_CONTENT_MOUNTPOINT_DATA_MAXSIZE 16
#define ORBIS_NP_UNIFIED_ENTITLEMENT_LABEL_SIZE   17
#define ORBIS_APP_CONTENT_ENTITLEMENT_KEY_SIZE    16
#define ORBIS_APP_CONTENT_ENTITLEMENT_LABEL_OFFSET 20
#define ORBIS_APP_CONTENT_INFO_LIST_MAX_SIZE      2500
// Service labels: 0 is the game's own service, 1..7 the SERVICE_ID_ADDCONT_ADD_<n> services.
#define ORBIS_APP_CONTENT_SERVICE_LABEL_MAX       7

#define ORBIS_APP_CONTENT_TEMPORARY_DATA_OPTION_NONE   0
#define ORBIS_APP_CONTENT_TEMPORARY_DATA_OPTION_FORMAT (1 << 0)

// SDK version from which a second sceAppContentInitialize fails with BUSY (shadPS4 FW_150).
#define ORBIS_APP_CONTENT_SDK_FW_150 0x1500000u

typedef enum OrbisAppContentAddcontDownloadStatus {
    ORBIS_APP_CONTENT_ADDCONT_DOWNLOAD_STATUS_NO_EXTRA_DATA     = 0,
    ORBIS_APP_CONTENT_ADDCONT_DOWNLOAD_STATUS_NO_IN_QUEUE       = 1,
    ORBIS_APP_CONTENT_ADDCONT_DOWNLOAD_STATUS_DOWNLOADING       = 2,
    ORBIS_APP_CONTENT_ADDCONT_DOWNLOAD_STATUS_DOWNLOAD_SUSPENDED = 3,
    ORBIS_APP_CONTENT_ADDCONT_DOWNLOAD_STATUS_INSTALLED         = 4,
} OrbisAppContentAddcontDownloadStatus;

typedef struct OrbisAppContentInitParam {
    char reserved[32];
} OrbisAppContentInitParam;

typedef struct OrbisAppContentBootParam {
    char reserved1[4];
    uint32_t attr;
    char reserved2[32];
} OrbisAppContentBootParam;

typedef struct OrbisAppContentMountPoint {
    char data[ORBIS_APP_CONTENT_MOUNTPOINT_DATA_MAXSIZE];
} OrbisAppContentMountPoint;

typedef struct OrbisNpUnifiedEntitlementLabel {
    char data[ORBIS_NP_UNIFIED_ENTITLEMENT_LABEL_SIZE];
    char padding[3];
} OrbisNpUnifiedEntitlementLabel;

typedef struct OrbisAppContentAddcontInfo {
    OrbisNpUnifiedEntitlementLabel entitlement_label;
    uint32_t status;
} OrbisAppContentAddcontInfo;

typedef struct OrbisAppContentGetEntitlementKey {
    uint8_t data[ORBIS_APP_CONTENT_ENTITLEMENT_KEY_SIZE];
} OrbisAppContentGetEntitlementKey;

// sdk_version is the game's compiled SDK version (sceKernelGetCompiledSdkVersion).
int32_t sceAppContentInitialize(const OrbisAppContentInitParam *initParam, OrbisAppContentBootParam *bootParam,
                                uint32_t sdk_version);
int32_t sceAppContentAppParamGetInt(int32_t paramId, int32_t *value);

int32_t sceAppContentTemporaryDataMount(OrbisAppContentMountPoint *mountPoint);
int32_t sceAppContentTemporaryDataMount2(uint32_t option, OrbisAppContentMountPoint *mountPoint);
int32_t sceAppContentTemporaryDataUnmount(const OrbisAppContentMountPoint *mountPoint);
int32_t sceAppContentTemporaryDataFormat(const OrbisAppContentMountPoint *mountPoint);
int32_t sceAppContentTemporaryDataGetAvailableSpaceKb(const OrbisAppContentMountPoint *mountPoint, uint64_t *availableSpaceKb);
int32_t sceAppContentDownloadDataGetAvailableSpaceKb(const OrbisAppContentMountPoint *mountPoint, uint64_t *availableSpaceKb);

int32_t sceAppContentGetAddcontInfo(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlementLabel, OrbisAppContentAddcontInfo *info);
int32_t sceAppContentGetAddcontInfoList(uint32_t service_label, OrbisAppContentAddcontInfo *list, uint32_t list_num, uint32_t *hit_num);
int32_t sceAppContentGetEntitlementKey(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label, OrbisAppContentGetEntitlementKey *key);
int32_t sceAppContentAddcontMount(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label, OrbisAppContentMountPoint *mount_point);
int32_t sceAppContentAddcontUnmount(const OrbisAppContentMountPoint *mount_point);

// Guest ABI shims (the functions shadPS4 stubs are PS4_HLE_STUB shims in ps4_app_content.c).
void shim_sceAppContentInitialize(GuestContext *ctx);
void shim_sceAppContentAppParamGetInt(GuestContext *ctx);
void shim_sceAppContentTemporaryDataMount(GuestContext *ctx);
void shim_sceAppContentTemporaryDataMount2(GuestContext *ctx);
void shim_sceAppContentTemporaryDataUnmount(GuestContext *ctx);
void shim_sceAppContentTemporaryDataFormat(GuestContext *ctx);
void shim_sceAppContentTemporaryDataGetAvailableSpaceKb(GuestContext *ctx);
void shim_sceAppContentDownloadDataGetAvailableSpaceKb(GuestContext *ctx);
void shim_sceAppContentGetAddcontInfo(GuestContext *ctx);
void shim_sceAppContentGetAddcontInfoList(GuestContext *ctx);
void shim_sceAppContentGetEntitlementKey(GuestContext *ctx);
void shim_sceAppContentAddcontMount(GuestContext *ctx);
void shim_sceAppContentAddcontUnmount(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_APP_CONTENT_H
