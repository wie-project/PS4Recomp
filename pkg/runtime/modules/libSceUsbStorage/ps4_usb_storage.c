// SPDX-License-Identifier: GPL-2.0-or-later
//
// libSceUsbStorage, after shadPS4 (src/core/libraries/usbd/usbstorage.cpp). One virtual USB
// storage device, id 0, backed by a host directory (ps4_vfs_usb_host_path: <data>/mnt/usb0)
// that the VFS mounts at /mnt/usb0 at boot:
//  * the device is present while that directory exists (GetDeviceList, IsExist, GetDeviceInfo,
//    which reports the host volume's capacity and free space, the shadPS4 vendor / product /
//    serial strings and exFAT).
//  * RequestMap(WSB) maps it at /mnt/usb0, or at /mnt/usb0/<subpath> (a subpath that already
//    starts with /mnt/usb is taken as is).
//  * Getdents lists the device's top directory (names starting with '.' left out) into
//    8-aligned dirent records of up to the buffer size per read; GetdentsOpen creates the
//    directory when it is missing.
//  * the attach / detach and map callbacks are only checked: shadPS4 stubs them too, since the
//    virtual device is never plugged in or out while the game runs.
#include "ps4_usb_storage.h"
#include "ps4_vfs.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define USB_TAG "[libSceUsbStorage]"

static bool usb_host_path(uint32_t device_id, char *out, size_t out_sz) {
    return ps4_vfs_usb_host_path(device_id, out, out_sz) == 0;
}

static bool usb_exists(uint32_t device_id) {
    char path[1024];
    struct stat st;
    return usb_host_path(device_id, path, sizeof(path)) && stat(path, &st) == 0;
}

int32_t sceUsbStorageInit(const void *attr) {
    (void)attr;
    return 0;
}

int32_t sceUsbStorageTerm(void) {
    return 0;
}

static bool valid_event_type(int32_t event_type) {
    return event_type == ORBIS_USB_STORAGE_EVENT_ATTACHED || event_type == ORBIS_USB_STORAGE_EVENT_DETACHED;
}

int32_t sceUsbStorageRegisterCallback(int32_t event_type, uint64_t callback, uint64_t arg) {
    if (!callback || !valid_event_type(event_type)) {
        fprintf(stderr, USB_TAG " RegisterCallback: bad callback 0x%llx or event type %d\n",
                (unsigned long long)callback, event_type);
        return ORBIS_AUTO_MOUNTER_ERROR_INVAL;
    }
    fprintf(stderr, USB_TAG " RegisterCallback(%d, 0x%llx, 0x%llx) (STUBBED)\n", event_type,
            (unsigned long long)callback, (unsigned long long)arg);
    return 0;
}

int32_t sceUsbStorageUnregisterCallback(int32_t event_type, uint64_t callback) {
    if (!callback || !valid_event_type(event_type)) {
        fprintf(stderr, USB_TAG " UnregisterCallback: bad callback 0x%llx or event type %d\n",
                (unsigned long long)callback, event_type);
        return ORBIS_AUTO_MOUNTER_ERROR_INVAL;
    }
    fprintf(stderr, USB_TAG " UnregisterCallback(%d, 0x%llx) (STUBBED)\n", event_type,
            (unsigned long long)callback);
    return 0;
}

// The four map callback (un)registrations of shadPS4: a directory and a callback are required.
static int32_t map_callback_stub(const char *what, uint32_t device_id, const char *directory, uint64_t callback) {
    if (!directory || !callback) {
        fprintf(stderr, USB_TAG " %s: directory or callback is null\n", what);
        return ORBIS_AUTO_MOUNTER_ERROR_INVAL;
    }
    fprintf(stderr, USB_TAG " %s(%u, %s, 0x%llx) (STUBBED)\n", what, device_id, directory,
            (unsigned long long)callback);
    return 0;
}

int32_t sceUsbStorageRegisterCallbackForMapAvailable(uint32_t device_id, const char *directory, uint64_t callback,
                                                     uint64_t arg) {
    (void)arg;
    return map_callback_stub("RegisterCallbackForMapAvailable", device_id, directory, callback);
}

int32_t sceUsbStorageUnregisterCallbackForMapAvailable(uint32_t device_id, const char *directory, uint64_t callback) {
    return map_callback_stub("UnregisterCallbackForMapAvailable", device_id, directory, callback);
}

int32_t sceUsbStorageRegisterCallbackForMapUnavailable(uint32_t device_id, const char *directory, uint64_t callback,
                                                       uint64_t arg) {
    (void)arg;
    return map_callback_stub("RegisterCallbackForMapUnavailable", device_id, directory, callback);
}

int32_t sceUsbStorageUnregisterCallbackForMapUnavailable(uint32_t device_id, const char *directory,
                                                         uint64_t callback) {
    return map_callback_stub("UnregisterCallbackForMapUnavailable", device_id, directory, callback);
}

int32_t sceUsbStorageGetDeviceList(uint32_t *device_ids, uint32_t *count) {
    if (!device_ids || !count) {
        return ORBIS_AUTO_MOUNTER_ERROR_INVAL;
    }
    if (!usb_exists(0)) {
        *count = 0;
        return 0;
    }
    device_ids[0] = 0;
    *count = 1;
    return 0;
}

int32_t sceUsbStorageGetDeviceInfo(uint32_t device_id, OrbisUsbStorageDeviceInfo *info) {
    if (!info) {
        return ORBIS_AUTO_MOUNTER_ERROR_INVAL;
    }
    if (device_id != 0) {
        return ORBIS_AUTO_MOUNTER_ERROR_NOENT;
    }
    char path[1024];
    struct stat st;
    if (!usb_host_path(device_id, path, sizeof(path)) || stat(path, &st) != 0) {
        fprintf(stderr, USB_TAG " GetDeviceInfo: device %u mount path does not exist\n", device_id);
        return ORBIS_AUTO_MOUNTER_ERROR_NOENT;
    }
    // std::filesystem::space: capacity f_blocks, free f_bfree, in f_frsize units.
    struct statvfs vfs;
    if (statvfs(path, &vfs) != 0) {
        fprintf(stderr, USB_TAG " GetDeviceInfo: could not get free space of %s: %s\n", path, strerror(errno));
        return ORBIS_AUTO_MOUNTER_ERROR_NOENT;
    }
    memset(info, 0, sizeof(*info));
    info->capacity = (uint64_t)vfs.f_blocks * vfs.f_frsize;
    info->free_space = (uint64_t)vfs.f_bfree * vfs.f_frsize;
    info->vendor_id = 0x054c;
    info->product_id = 0x0268;
    info->bcd_device = 0x0100;
    info->vendor_len = strlcpy(info->vendor, "shadPS4", sizeof(info->vendor));
    info->product_len = strlcpy(info->product, "Virtual USB Storage", sizeof(info->product));
    info->serial_number_len = strlcpy(info->serial_number, "0123456789ABCDEF", sizeof(info->serial_number));
    info->additional_features = ORBIS_USB_STORAGE_FEATURE_EXFAT;
    return 0;
}

int32_t sceUsbStorageIsExist(uint32_t device_id) {
    return device_id == 0 && usb_exists(device_id) ? 1 : 0;
}

int32_t sceUsbStorageRequestMapWSB(uint32_t device_id, const char *subpath, int32_t flags, int32_t unk,
                                   char *out_mount_path, int32_t *out_status) {
    (void)flags;
    (void)unk;
    if (device_id != 0) {
        return ORBIS_AUTO_MOUNTER_ERROR_NOENT;
    }
    char mount_path[1024];
    snprintf(mount_path, sizeof(mount_path), "/mnt/usb%u", device_id);
    if (subpath && subpath[0] != '\0') {
        if (strncmp(subpath, "/mnt/usb", 8) == 0) {
            snprintf(mount_path, sizeof(mount_path), "%s", subpath);
        } else {
            const size_t len = strlen(mount_path);
            snprintf(mount_path + len, sizeof(mount_path) - len, "%s%s", subpath[0] == '/' ? "" : "/", subpath);
        }
    }
    if (out_mount_path) {
        strlcpy(out_mount_path, mount_path, ORBIS_USB_STORAGE_MOUNT_PATH_SIZE);
    }
    if (out_status) {
        *out_status = 0;
    }
    return 0;
}

int32_t sceUsbStorageRequestMap(uint32_t device_id, const char *subpath, int32_t flags, int32_t unk,
                                char *out_mount_path, int32_t *out_status) {
    return sceUsbStorageRequestMapWSB(device_id, subpath, flags, unk, out_mount_path, out_status);
}

int32_t sceUsbStorageRequestUnmap(uint32_t device_id, const char *directory) {
    (void)device_id;
    return directory ? 0 : ORBIS_AUTO_MOUNTER_ERROR_INVAL;
}

// ---------------------------------------------------------------------------------------------
// Getdents

typedef struct UsbDirentItem {
    char *name;
    bool is_directory;
} UsbDirentItem;

typedef struct UsbGetdentsState {
    struct UsbGetdentsState *next;
    int32_t fd;
    UsbDirentItem *items;
    size_t count;
    size_t current_index;
} UsbGetdentsState;

static pthread_mutex_t g_getdents_mutex = PTHREAD_MUTEX_INITIALIZER;
static UsbGetdentsState *g_getdents;
static int32_t g_next_fd = 1;

static void free_state(UsbGetdentsState *s) {
    for (size_t i = 0; i < s->count; i++) {
        free(s->items[i].name);
    }
    free(s->items);
    free(s);
}

// Reads the top directory of the device into s (names starting with '.' left out).
static void list_directory(const char *dir_path, UsbGetdentsState *s) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return;
    }
    size_t cap = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '\0' || de->d_name[0] == '.') {
            continue;
        }
        bool is_dir = de->d_type == DT_DIR;
        if (de->d_type == DT_UNKNOWN || de->d_type == DT_LNK) {
            char full[2048];
            struct stat st;
            snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);
            is_dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        }
        if (s->count == cap) {
            const size_t ncap = cap ? cap * 2 : 16;
            UsbDirentItem *items = realloc(s->items, ncap * sizeof(*items));
            if (!items) {
                break;
            }
            s->items = items;
            cap = ncap;
        }
        char *name = strdup(de->d_name);
        if (!name) {
            break;
        }
        s->items[s->count++] = (UsbDirentItem){name, is_dir};
    }
    closedir(dir);
}

int32_t sceUsbStorageGetdentsOpen(uint32_t device_id, int32_t *out_fd, uint64_t *out_bufsize) {
    if (!out_fd || !out_bufsize) {
        return ORBIS_AUTO_MOUNTER_ERROR_INVAL;
    }
    if (device_id != 0) {
        return ORBIS_AUTO_MOUNTER_ERROR_NOENT;
    }
    char path[1024];
    UsbGetdentsState *s = calloc(1, sizeof(*s));
    if (!s) {
        return ORBIS_AUTO_MOUNTER_ERROR_INVAL;
    }
    if (usb_host_path(device_id, path, sizeof(path))) {
        if (ps4_vfs_host_mkdir_p(path) != 0) {
            fprintf(stderr, USB_TAG " GetdentsOpen: could not create %s: %s\n", path, strerror(errno));
        }
        list_directory(path, s);
    }
    pthread_mutex_lock(&g_getdents_mutex);
    s->fd = g_next_fd++;
    s->next = g_getdents;
    g_getdents = s;
    pthread_mutex_unlock(&g_getdents_mutex);
    *out_fd = s->fd;
    *out_bufsize = ORBIS_USB_STORAGE_GETDENTS_BUFSIZE;
    return 0;
}

int32_t sceUsbStorageGetdentsRead(int32_t fd, void *buffer, uint64_t bufsize, uint64_t *out_bytes_read) {
    if (!buffer || !out_bytes_read) {
        return ORBIS_AUTO_MOUNTER_ERROR_INVAL;
    }
    pthread_mutex_lock(&g_getdents_mutex);
    UsbGetdentsState *s = g_getdents;
    while (s && s->fd != fd) {
        s = s->next;
    }
    if (!s) {
        pthread_mutex_unlock(&g_getdents_mutex);
        *out_bytes_read = 0;
        return ORBIS_AUTO_MOUNTER_ERROR_NOTSUP;
    }
    uint64_t written = 0;
    while (s->current_index < s->count) {
        const UsbDirentItem *item = &s->items[s->current_index];
        const size_t name_len = strlen(item->name);
        const uint16_t namlen = (uint16_t)(name_len < 255 ? name_len : 255);
        const uint16_t reclen = (uint16_t)((8u + namlen + 1u + 7u) & ~7u);
        if (written + reclen > bufsize) {
            break;
        }
        OrbisUsbStorageDirent *entry = (OrbisUsbStorageDirent *)((uint8_t *)buffer + written);
        memset(entry, 0, reclen);
        entry->file_no = (uint32_t)(s->current_index + 1);
        entry->d_reclen = reclen;
        entry->d_type = item->is_directory ? DT_DIR : DT_REG;
        entry->d_namlen = (uint8_t)namlen;
        memcpy(entry->d_name, item->name, namlen);
        written += reclen;
        s->current_index++;
    }
    pthread_mutex_unlock(&g_getdents_mutex);
    *out_bytes_read = written;
    return 0;
}

int32_t sceUsbStorageGetdentsClose(int32_t fd) {
    pthread_mutex_lock(&g_getdents_mutex);
    UsbGetdentsState *found = NULL;
    for (UsbGetdentsState **link = &g_getdents; *link; link = &(*link)->next) {
        if ((*link)->fd == fd) {
            found = *link;
            *link = found->next;
            break;
        }
    }
    pthread_mutex_unlock(&g_getdents_mutex);
    if (found) {
        free_state(found);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Guest shims

static inline void *guest_ptr(GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

#define SHIM_RESULT(expr)                       \
    do {                                        \
        ctx->rax = (uint64_t)(int64_t)(expr);   \
        SHIM_RETURN();                          \
    } while (0)

// int sceUsbStorageInit(const SceUsbStorageInitParam *attr)
void shim_sceUsbStorageInit(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageInit(guest_ptr(ctx, ctx->rdi)));
}

void shim_sceUsbStorageTerm(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageTerm());
}

// int sceUsbStorageRegisterCallback(int event_type, SceUsbStorageCallback cb, void *arg)
void shim_sceUsbStorageRegisterCallback(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageRegisterCallback((int32_t)ctx->rdi, ctx->rsi, ctx->rdx));
}

void shim_sceUsbStorageUnregisterCallback(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageUnregisterCallback((int32_t)ctx->rdi, ctx->rsi));
}

// int sceUsbStorageRegisterCallbackForMapAvailable(uint32_t id, const char *dir, cb, void *arg)
void shim_sceUsbStorageRegisterCallbackForMapAvailable(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageRegisterCallbackForMapAvailable((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi),
                                                             ctx->rdx, ctx->rcx));
}

void shim_sceUsbStorageUnregisterCallbackForMapAvailable(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageUnregisterCallbackForMapAvailable((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi),
                                                               ctx->rdx));
}

void shim_sceUsbStorageRegisterCallbackForMapUnavailable(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageRegisterCallbackForMapUnavailable((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi),
                                                               ctx->rdx, ctx->rcx));
}

void shim_sceUsbStorageUnregisterCallbackForMapUnavailable(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageUnregisterCallbackForMapUnavailable((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi),
                                                                 ctx->rdx));
}

// int sceUsbStorageGetDeviceList(uint32_t *device_ids, uint32_t *count)
void shim_sceUsbStorageGetDeviceList(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageGetDeviceList(guest_ptr(ctx, ctx->rdi), guest_ptr(ctx, ctx->rsi)));
}

// int sceUsbStorageGetDeviceInfo(uint32_t device_id, SceUsbStorageDeviceInfo *info)
void shim_sceUsbStorageGetDeviceInfo(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageGetDeviceInfo((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi)));
}

void shim_sceUsbStorageIsExist(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageIsExist((uint32_t)ctx->rdi));
}

// int sceUsbStorageRequestMap(uint32_t id, const char *subpath, int flags, int, char *out_path,
//                             int *out_status)
void shim_sceUsbStorageRequestMap(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageRequestMap((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), (int32_t)ctx->rdx,
                                        (int32_t)ctx->rcx, guest_ptr(ctx, ctx->r8), guest_ptr(ctx, ctx->r9)));
}

void shim_sceUsbStorageRequestMapWSB(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageRequestMapWSB((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), (int32_t)ctx->rdx,
                                           (int32_t)ctx->rcx, guest_ptr(ctx, ctx->r8), guest_ptr(ctx, ctx->r9)));
}

// int sceUsbStorageRequestUnmap(uint32_t id, const char *directory)
void shim_sceUsbStorageRequestUnmap(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageRequestUnmap((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi)));
}

// int sceUsbStorageGetdentsOpen(uint32_t id, int *out_fd, size_t *out_bufsize)
void shim_sceUsbStorageGetdentsOpen(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageGetdentsOpen((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), guest_ptr(ctx, ctx->rdx)));
}

// int sceUsbStorageGetdentsRead(int fd, void *buf, size_t bufsize, size_t *out_bytes)
void shim_sceUsbStorageGetdentsRead(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageGetdentsRead((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), ctx->rdx,
                                          guest_ptr(ctx, ctx->rcx)));
}

void shim_sceUsbStorageGetdentsClose(GuestContext *ctx) {
    SHIM_RESULT(sceUsbStorageGetdentsClose((int32_t)ctx->rdi));
}
