// SPDX-License-Identifier: GPL-2.0-or-later
//
// libSceUsbStorage, after shadPS4 (src/core/libraries/usbd/usbstorage.{h,cpp}).
#ifndef PS4_USB_STORAGE_H
#define PS4_USB_STORAGE_H

#include "recomp_runtime.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// libSceUsbStorage / auto_mounter error codes (module 0x80f4)
#define ORBIS_AUTO_MOUNTER_ERROR_INVAL ((int32_t)0x80f40002)
#define ORBIS_AUTO_MOUNTER_ERROR_NOENT ((int32_t)0x80f40004)
#define ORBIS_AUTO_MOUNTER_ERROR_NOTSUP ((int32_t)0x80f40005)

// OrbisUsbStorageDeviceInfo.additional_features
#define ORBIS_USB_STORAGE_FEATURE_READ_ONLY (1ull << 4)
#define ORBIS_USB_STORAGE_FEATURE_FAT (1ull << 6)
#define ORBIS_USB_STORAGE_FEATURE_EXFAT (1ull << 7)

// Callback event types of sceUsbStorageRegisterCallback.
#define ORBIS_USB_STORAGE_EVENT_ATTACHED 1
#define ORBIS_USB_STORAGE_EVENT_DETACHED 2

// The guest directory a device is mapped at, and the size of the buffers holding it.
#define ORBIS_USB_STORAGE_MOUNT_PATH_SIZE 128
// The buffer size sceUsbStorageGetdentsOpen recommends.
#define ORBIS_USB_STORAGE_GETDENTS_BUFSIZE 0x8000u

typedef struct OrbisUsbStorageDeviceInfo {
    uint64_t capacity;            // +0x00
    uint64_t free_space;          // +0x08
    uint16_t vendor_id;           // +0x10
    uint16_t product_id;          // +0x12
    uint16_t bcd_device;          // +0x14
    char vendor[256];             // +0x16
    uint8_t pad1[2];              // +0x116
    uint64_t vendor_len;          // +0x118
    char product[256];            // +0x120
    uint64_t product_len;         // +0x220
    char serial_number[256];      // +0x228
    uint64_t serial_number_len;   // +0x328
    uint64_t additional_features; // +0x330
} OrbisUsbStorageDeviceInfo;
_Static_assert(offsetof(OrbisUsbStorageDeviceInfo, vendor) == 0x16, "vendor");
_Static_assert(offsetof(OrbisUsbStorageDeviceInfo, vendor_len) == 0x118, "vendor_len");
_Static_assert(offsetof(OrbisUsbStorageDeviceInfo, product) == 0x120, "product");
_Static_assert(offsetof(OrbisUsbStorageDeviceInfo, product_len) == 0x220, "product_len");
_Static_assert(offsetof(OrbisUsbStorageDeviceInfo, serial_number) == 0x228, "serial_number");
_Static_assert(offsetof(OrbisUsbStorageDeviceInfo, serial_number_len) == 0x328, "serial_number_len");
_Static_assert(offsetof(OrbisUsbStorageDeviceInfo, additional_features) == 0x330, "additional_features");
_Static_assert(sizeof(OrbisUsbStorageDeviceInfo) == 0x338, "OrbisUsbStorageDeviceInfo");

// shadPS4 names the first field d_fileno, which <dirent.h> on macOS defines as a macro.
typedef struct __attribute__((packed)) OrbisUsbStorageDirent {
    uint32_t file_no;
    uint16_t d_reclen;
    uint8_t d_type;
    uint8_t d_namlen;
    char d_name[256];
} OrbisUsbStorageDirent;
_Static_assert(offsetof(OrbisUsbStorageDirent, file_no) == 0, "file_no");
_Static_assert(offsetof(OrbisUsbStorageDirent, d_reclen) == 4, "d_reclen");
_Static_assert(offsetof(OrbisUsbStorageDirent, d_type) == 6, "d_type");
_Static_assert(offsetof(OrbisUsbStorageDirent, d_namlen) == 7, "d_namlen");
_Static_assert(offsetof(OrbisUsbStorageDirent, d_name) == 8, "d_name");

// Host-side API (host pointers; callbacks and arguments are guest addresses, only checked).
int32_t sceUsbStorageInit(const void *attr);
int32_t sceUsbStorageTerm(void);
int32_t sceUsbStorageRegisterCallback(int32_t event_type, uint64_t callback, uint64_t arg);
int32_t sceUsbStorageUnregisterCallback(int32_t event_type, uint64_t callback);
int32_t sceUsbStorageRegisterCallbackForMapAvailable(uint32_t device_id, const char *directory, uint64_t callback,
                                                     uint64_t arg);
int32_t sceUsbStorageUnregisterCallbackForMapAvailable(uint32_t device_id, const char *directory, uint64_t callback);
int32_t sceUsbStorageRegisterCallbackForMapUnavailable(uint32_t device_id, const char *directory, uint64_t callback,
                                                       uint64_t arg);
int32_t sceUsbStorageUnregisterCallbackForMapUnavailable(uint32_t device_id, const char *directory,
                                                         uint64_t callback);
int32_t sceUsbStorageGetDeviceList(uint32_t *device_ids, uint32_t *count);
int32_t sceUsbStorageGetDeviceInfo(uint32_t device_id, OrbisUsbStorageDeviceInfo *info);
int32_t sceUsbStorageIsExist(uint32_t device_id);
int32_t sceUsbStorageRequestMapWSB(uint32_t device_id, const char *subpath, int32_t flags, int32_t unk,
                                   char *out_mount_path, int32_t *out_status);
int32_t sceUsbStorageRequestMap(uint32_t device_id, const char *subpath, int32_t flags, int32_t unk,
                                char *out_mount_path, int32_t *out_status);
int32_t sceUsbStorageRequestUnmap(uint32_t device_id, const char *directory);
int32_t sceUsbStorageGetdentsOpen(uint32_t device_id, int32_t *out_fd, uint64_t *out_bufsize);
int32_t sceUsbStorageGetdentsRead(int32_t fd, void *buffer, uint64_t bufsize, uint64_t *out_bytes_read);
int32_t sceUsbStorageGetdentsClose(int32_t fd);

void shim_sceUsbStorageInit(GuestContext *ctx);
void shim_sceUsbStorageTerm(GuestContext *ctx);
void shim_sceUsbStorageRegisterCallback(GuestContext *ctx);
void shim_sceUsbStorageUnregisterCallback(GuestContext *ctx);
void shim_sceUsbStorageRegisterCallbackForMapAvailable(GuestContext *ctx);
void shim_sceUsbStorageUnregisterCallbackForMapAvailable(GuestContext *ctx);
void shim_sceUsbStorageRegisterCallbackForMapUnavailable(GuestContext *ctx);
void shim_sceUsbStorageUnregisterCallbackForMapUnavailable(GuestContext *ctx);
void shim_sceUsbStorageGetDeviceList(GuestContext *ctx);
void shim_sceUsbStorageGetDeviceInfo(GuestContext *ctx);
void shim_sceUsbStorageIsExist(GuestContext *ctx);
void shim_sceUsbStorageRequestMap(GuestContext *ctx);
void shim_sceUsbStorageRequestMapWSB(GuestContext *ctx);
void shim_sceUsbStorageRequestUnmap(GuestContext *ctx);
void shim_sceUsbStorageGetdentsOpen(GuestContext *ctx);
void shim_sceUsbStorageGetdentsRead(GuestContext *ctx);
void shim_sceUsbStorageGetdentsClose(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
