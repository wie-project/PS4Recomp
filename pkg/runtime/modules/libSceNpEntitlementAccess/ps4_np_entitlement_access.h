// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_NP_ENTITLEMENT_ACCESS_H
#define PS4_NP_ENTITLEMENT_ACCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "recomp_runtime.h"

// shadPS4 np/np_entitlement_access_error.h
#define ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED ((int32_t)0x817D0001)
#define ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT ((int32_t)0x817D0002)
#define ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_ALREADY_INITIALIZED ((int32_t)0x817D0003)
#define ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_OUT_OF_MEMORY ((int32_t)0x817D0004)
#define ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_FOUND ((int32_t)0x817D0005)
#define ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_SIZE ((int32_t)0x817D0006)

// Sizes of the structures the queries clear (shadPS4 writes zeroes of these sizes).
#define NP_ENTITLEMENT_ADDCONT_INFO_SIZE 0x1C
#define NP_ENTITLEMENT_POLL_INFO_SIZE 0x40
#define NP_ENTITLEMENT_KEY_SIZE 16
// "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx" and its terminator.
#define NP_ENTITLEMENT_TRANSACTION_ID_SIZE 37

// License files (core/file_format/rif.h): a RIDX index (<service id>.idx) lists the RIFs held
// in a RIFA container (<service id>.rif); a lone <service id>.rif may also be a single RIF.
#define RIF_MAGIC 0x52494600u  // "RIF\0", big endian
#define RIFA_MAGIC 0x72696661u // "rifa", big endian
#define RIDX_MAGIC 0x72696478u // "ridx", big endian
#define RIDX_VERSION 1
#define RIF_SIZE 0x400
#define RIFA_HEADER_SIZE 0x400
#define RIDX_HEADER_SIZE 0x20
#define RIDX_ENTRY_SIZE 0x30
#define RIF_CONTENT_TYPE_OFFSET 0x54
#define RIF_SKU_FLAG_OFFSET 0x56
// encrypted_secret starts at 0x270; the entitlement key is 16 bytes at offset 0x70 in it.
#define RIF_ENTITLEMENT_KEY_OFFSET (0x270 + 0x70)

// Content type of a license-only entitlement (add-on content with no data to install).
#define RIF_CONTENT_TYPE_PS4AL 0x1C00

// The licenses directory: $PS4RECOMP_DATA_DIR/licenses, else
// ~/Library/Application Support/PS4Recomp/licenses (shadPS4 user/licenses).
void ps4_np_entitlement_licenses_dir(char *out, size_t out_size);

// Calls visit for each license-only entitlement (content type RIF_CONTENT_TYPE_PS4AL) in the
// license container <service_id>.idx / <service_id>.rif of the licenses directory, with its
// label and its 16-byte entitlement key (shadPS4 RIF::GetLicenseOnlyEntitlements). Returns 0
// when the container is absent, -1 when it is invalid, 1 when it was read.
typedef void (*Ps4RifEntitlementVisitor)(const char *label, const uint8_t *key, void *arg);
int ps4_rif_license_only_entitlements(const char *service_id, Ps4RifEntitlementVisitor visit, void *arg);

int32_t sceNpEntitlementAccessInitialize(const void *param, const void *reserved);
int32_t sceNpEntitlementAccessGetSkuFlag(uint32_t *flag);
int32_t sceNpEntitlementAccessGetAddcontEntitlementInfo(int32_t user_id, const char *label, void *out_info);
int32_t sceNpEntitlementAccessGetAddcontEntitlementInfoList(int32_t user_id, const char *service_label,
                                                            uint32_t limit, void *out_list);
int32_t sceNpEntitlementAccessGetEntitlementKey(int32_t user_id, const char *label, void *out_key);
int32_t sceNpEntitlementAccessGenerateTransactionId(char *out_transaction_id);

// The remaining calls are only shims: the Request* calls hand out request ids (1, 2, ...) and
// every poll answers "status 0, nothing", as in shadPS4.
void shim_sceNpEntitlementAccessInitialize(GuestContext *ctx);
void shim_sceNpEntitlementAccessGetSkuFlag(GuestContext *ctx);
void shim_sceNpEntitlementAccessGetAddcontEntitlementInfoList(GuestContext *ctx);
void shim_sceNpEntitlementAccessGetAddcontEntitlementInfo(GuestContext *ctx);
void shim_sceNpEntitlementAccessGetEntitlementKey(GuestContext *ctx);
void shim_sceNpEntitlementAccessGenerateTransactionId(GuestContext *ctx);
void shim_sceNpEntitlementAccessRequestConsumeEntitlement(GuestContext *ctx);
void shim_sceNpEntitlementAccessRequestConsumeUnifiedEntitlement(GuestContext *ctx);
void shim_sceNpEntitlementAccessRequestConsumeServiceEntitlement(GuestContext *ctx);
void shim_sceNpEntitlementAccessPollConsumeEntitlement(GuestContext *ctx);
void shim_sceNpEntitlementAccessRequestConsumableEntitlementInfo(GuestContext *ctx);
void shim_sceNpEntitlementAccessPollConsumableEntitlementInfo(GuestContext *ctx);
void shim_sceNpEntitlementAccessRequestUnifiedEntitlementInfo(GuestContext *ctx);
void shim_sceNpEntitlementAccessPollUnifiedEntitlementInfo(GuestContext *ctx);
void shim_sceNpEntitlementAccessRequestUnifiedEntitlementInfoList(GuestContext *ctx);
void shim_sceNpEntitlementAccessPollUnifiedEntitlementInfoList(GuestContext *ctx);
void shim_sceNpEntitlementAccessRequestServiceEntitlementInfo(GuestContext *ctx);
void shim_sceNpEntitlementAccessPollServiceEntitlementInfo(GuestContext *ctx);
void shim_sceNpEntitlementAccessRequestServiceEntitlementInfoList(GuestContext *ctx);
void shim_sceNpEntitlementAccessPollServiceEntitlementInfoList(GuestContext *ctx);
void shim_sceNpEntitlementAccessDeleteRequest(GuestContext *ctx);
void shim_sceNpEntitlementAccessAbortRequest(GuestContext *ctx);

#endif // PS4_NP_ENTITLEMENT_ACCESS_H
