#ifndef PS4_APP_CONTENT_H
#define PS4_APP_CONTENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Error codes
#define ORBIS_APP_CONTENT_ERROR_PARAMETER          0x80D90002
#define ORBIS_APP_CONTENT_ERROR_BUSY               0x80D90003
#define ORBIS_APP_CONTENT_ERROR_NOT_FOUND          0x80D90005
#define ORBIS_APP_CONTENT_ERROR_DRM_NO_ENTITLEMENT 0x80D90007

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

#define ORBIS_APP_CONTENT_TEMPORARY_DATA_OPTION_NONE   0
#define ORBIS_APP_CONTENT_TEMPORARY_DATA_OPTION_FORMAT (1 << 0)

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
    char data[ORBIS_APP_CONTENT_ENTITLEMENT_KEY_SIZE];
} OrbisAppContentGetEntitlementKey;

// Primary API functions
int32_t sceAppContentInitialize(const OrbisAppContentInitParam *initParam, OrbisAppContentBootParam *bootParam);
int32_t sceAppContentAppParamGetInt(int32_t paramId, int32_t *value);
int32_t sceAppContentAppParamGetString(int32_t paramId, char *buf, size_t bufSize);

int32_t sceAppContentTemporaryDataMount(OrbisAppContentMountPoint *mountPoint);
int32_t sceAppContentTemporaryDataMount2(uint32_t option, OrbisAppContentMountPoint *mountPoint);
int32_t sceAppContentTemporaryDataUnmount(const OrbisAppContentMountPoint *mountPoint);
int32_t sceAppContentTemporaryDataFormat(const OrbisAppContentMountPoint *mountPoint);
int32_t sceAppContentTemporaryDataGetAvailableSpaceKb(const OrbisAppContentMountPoint *mountPoint, uint64_t *availableSpaceKb);

int32_t sceAppContentDownloadDataGetAvailableSpaceKb(const OrbisAppContentMountPoint *mountPoint, uint64_t *availableSpaceKb);
int32_t sceAppContentDownloadDataFormat(const OrbisAppContentMountPoint *mountPoint);

int32_t sceAppContentGetAddcontInfo(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlementLabel, OrbisAppContentAddcontInfo *info);
int32_t sceAppContentGetAddcontInfoList(uint32_t service_label, OrbisAppContentAddcontInfo *list, uint32_t list_num, uint32_t *hit_num);
int32_t sceAppContentGetEntitlementKey(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label, OrbisAppContentGetEntitlementKey *key);

int32_t sceAppContentAddcontMount(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label, OrbisAppContentMountPoint *mount_point);
int32_t sceAppContentAddcontUnmount(const OrbisAppContentMountPoint *mount_point);
int32_t sceAppContentAddcontDelete(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label);
int32_t sceAppContentAddcontEnqueueDownload(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label);
int32_t sceAppContentAddcontEnqueueDownloadSp(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label);
int32_t sceAppContentAddcontShrink(void);

int32_t sceAppContentDownload0Expand(void);
int32_t sceAppContentDownload0Shrink(void);
int32_t sceAppContentDownload1Expand(void);
int32_t sceAppContentDownload1Shrink(void);

int32_t sceAppContentGetAddcontDownloadProgress(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label, void *progress);
int32_t sceAppContentGetRegion(void);
int32_t sceAppContentRequestPatchInstall(void);

int32_t sceAppContentSmallSharedDataFormat(void);
int32_t sceAppContentSmallSharedDataGetAvailableSpaceKb(uint64_t *availableSpaceKb);
int32_t sceAppContentSmallSharedDataMount(OrbisAppContentMountPoint *mountPoint);
int32_t sceAppContentSmallSharedDataUnmount(const OrbisAppContentMountPoint *mountPoint);

int32_t sceAppContentGetPftFlag(void);
int32_t Func_C59A36FF8D7C59DA(void);

int32_t sceAppContentAddcontEnqueueDownloadByEntitlementId(const char *entitlementId);
int32_t sceAppContentAddcontMountByEntitlementId(const char *entitlementId, OrbisAppContentMountPoint *mount_point);
int32_t sceAppContentGetAddcontInfoByEntitlementId(const char *entitlementId, OrbisAppContentAddcontInfo *info);
int32_t sceAppContentGetAddcontInfoListByIroTag(void);
int32_t sceAppContentGetDownloadedStoreCountry(char *countryCode);

// Guest ABI shims
void shim_sceAppContentInitialize(GuestContext *ctx);
void shim_sceAppContentAppParamGetInt(GuestContext *ctx);
void shim_sceAppContentAppParamGetString(GuestContext *ctx);

void shim_sceAppContentTemporaryDataMount(GuestContext *ctx);
void shim_sceAppContentTemporaryDataMount2(GuestContext *ctx);
void shim_sceAppContentTemporaryDataUnmount(GuestContext *ctx);
void shim_sceAppContentTemporaryDataFormat(GuestContext *ctx);
void shim_sceAppContentTemporaryDataGetAvailableSpaceKb(GuestContext *ctx);

void shim_sceAppContentDownloadDataGetAvailableSpaceKb(GuestContext *ctx);
void shim_sceAppContentDownloadDataFormat(GuestContext *ctx);

void shim_sceAppContentGetAddcontInfo(GuestContext *ctx);
void shim_sceAppContentGetAddcontInfoList(GuestContext *ctx);
void shim_sceAppContentGetEntitlementKey(GuestContext *ctx);

void shim_sceAppContentAddcontMount(GuestContext *ctx);
void shim_sceAppContentAddcontUnmount(GuestContext *ctx);
void shim_sceAppContentAddcontDelete(GuestContext *ctx);
void shim_sceAppContentAddcontEnqueueDownload(GuestContext *ctx);
void shim_sceAppContentAddcontEnqueueDownloadSp(GuestContext *ctx);
void shim_sceAppContentAddcontShrink(GuestContext *ctx);

void shim_sceAppContentDownload0Expand(GuestContext *ctx);
void shim_sceAppContentDownload0Shrink(GuestContext *ctx);
void shim_sceAppContentDownload1Expand(GuestContext *ctx);
void shim_sceAppContentDownload1Shrink(GuestContext *ctx);

void shim_sceAppContentGetAddcontDownloadProgress(GuestContext *ctx);
void shim_sceAppContentGetRegion(GuestContext *ctx);
void shim_sceAppContentRequestPatchInstall(GuestContext *ctx);

void shim_sceAppContentSmallSharedDataFormat(GuestContext *ctx);
void shim_sceAppContentSmallSharedDataGetAvailableSpaceKb(GuestContext *ctx);
void shim_sceAppContentSmallSharedDataMount(GuestContext *ctx);
void shim_sceAppContentSmallSharedDataUnmount(GuestContext *ctx);

void shim_sceAppContentGetPftFlag(GuestContext *ctx);
void shim_Func_C59A36FF8D7C59DA(GuestContext *ctx);

void shim_sceAppContentAddcontEnqueueDownloadByEntitlementId(GuestContext *ctx);
void shim_sceAppContentAddcontMountByEntitlementId(GuestContext *ctx);
void shim_sceAppContentGetAddcontInfoByEntitlementId(GuestContext *ctx);
void shim_sceAppContentGetAddcontInfoListByIroTag(GuestContext *ctx);
void shim_sceAppContentGetDownloadedStoreCountry(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_APP_CONTENT_H
