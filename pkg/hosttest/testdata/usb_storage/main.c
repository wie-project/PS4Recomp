// SPDX-License-Identifier: GPL-2.0-or-later

// libSceUsbStorage against shadPS4's usbd/usbstorage.cpp: the virtual device 0 is present while
// its host directory (<PS4RECOMP_DATA_DIR>/mnt/usb0, mounted by ps4_vfs_init at /mnt/usb0)
// exists; device info, map paths, callback checks, and directory listing with Getdents.
#include "ps4_usb_storage.h"
#include "ps4_vfs.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            exit(1);                                                              \
        }                                                                         \
    } while (0)

#define E_INVAL ORBIS_AUTO_MOUNTER_ERROR_INVAL
#define E_NOENT ORBIS_AUTO_MOUNTER_ERROR_NOENT
#define E_NOTSUP ORBIS_AUTO_MOUNTER_ERROR_NOTSUP

static char g_data[1024];
static char g_usb[1100];

static void setup(void) {
    char tmpl[] = "/tmp/ps4-usb-storage-XXXXXX";
    CHECK(mkdtemp(tmpl) != NULL);
    snprintf(g_data, sizeof(g_data), "%s", tmpl);
    setenv("PS4RECOMP_DATA_DIR", g_data, 1);
    snprintf(g_usb, sizeof(g_usb), "%s/mnt/usb0", g_data);
}

static void remove_tree(const char *path) {
    DIR *d = opendir(path);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
                continue;
            }
            char p[2048];
            snprintf(p, sizeof(p), "%s/%s", path, de->d_name);
            struct stat st;
            if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
                remove_tree(p);
            } else {
                unlink(p);
            }
        }
        closedir(d);
    }
    rmdir(path);
}

static void touch(const char *name) {
    char p[2048];
    snprintf(p, sizeof(p), "%s/%s", g_usb, name);
    FILE *f = fopen(p, "w");
    CHECK(f != NULL);
    fclose(f);
}

static void test_device(void) {
    uint32_t ids[4] = {7, 7, 7, 7}, count = 9;
    OrbisUsbStorageDeviceInfo info;

    // No host directory: no device.
    CHECK(sceUsbStorageGetDeviceList(ids, &count) == 0 && count == 0);
    CHECK(sceUsbStorageIsExist(0) == 0);
    CHECK(sceUsbStorageGetDeviceInfo(0, &info) == E_NOENT);

    // ps4_vfs_init creates it and mounts it at /mnt/usb0.
    ps4_vfs_init(g_data, true);
    struct stat st;
    CHECK(stat(g_usb, &st) == 0 && S_ISDIR(st.st_mode));
    char resolved[2048];
    CHECK(ps4_vfs_resolve("/mnt/usb0/save.bin", resolved, sizeof(resolved)) == 0);
    char want[2048];
    snprintf(want, sizeof(want), "%s/save.bin", g_usb);
    CHECK(strcmp(resolved, want) == 0);

    CHECK(sceUsbStorageInit(NULL) == 0);
    CHECK(sceUsbStorageGetDeviceList(NULL, &count) == E_INVAL);
    CHECK(sceUsbStorageGetDeviceList(ids, NULL) == E_INVAL);
    CHECK(sceUsbStorageGetDeviceList(ids, &count) == 0 && count == 1 && ids[0] == 0);
    CHECK(sceUsbStorageIsExist(0) == 1 && sceUsbStorageIsExist(1) == 0);

    CHECK(sceUsbStorageGetDeviceInfo(0, NULL) == E_INVAL);
    CHECK(sceUsbStorageGetDeviceInfo(1, &info) == E_NOENT);
    memset(&info, 0xAA, sizeof(info));
    CHECK(sceUsbStorageGetDeviceInfo(0, &info) == 0);
    CHECK(info.capacity > 0 && info.free_space <= info.capacity);
    CHECK(info.vendor_id == 0x054c && info.product_id == 0x0268 && info.bcd_device == 0x0100);
    CHECK(strcmp(info.vendor, "shadPS4") == 0 && info.vendor_len == 7);
    CHECK(strcmp(info.product, "Virtual USB Storage") == 0 && info.product_len == 19);
    CHECK(strcmp(info.serial_number, "0123456789ABCDEF") == 0 && info.serial_number_len == 16);
    CHECK(info.additional_features == ORBIS_USB_STORAGE_FEATURE_EXFAT);
    CHECK(info.pad1[0] == 0 && info.pad1[1] == 0);
    CHECK(sceUsbStorageTerm() == 0);
    printf("PASS device\n");
}

static void test_map(void) {
    char path[ORBIS_USB_STORAGE_MOUNT_PATH_SIZE];
    int32_t status = -1;
    CHECK(sceUsbStorageRequestMap(1, NULL, 0, 0, path, &status) == E_NOENT);
    CHECK(sceUsbStorageRequestMap(0, NULL, 0, 0, path, &status) == 0);
    CHECK(strcmp(path, "/mnt/usb0") == 0 && status == 0);
    CHECK(sceUsbStorageRequestMapWSB(0, "saves", 0, 0, path, NULL) == 0 && strcmp(path, "/mnt/usb0/saves") == 0);
    CHECK(sceUsbStorageRequestMapWSB(0, "/saves", 0, 0, path, NULL) == 0 && strcmp(path, "/mnt/usb0/saves") == 0);
    CHECK(sceUsbStorageRequestMap(0, "/mnt/usb0/x", 0, 0, path, NULL) == 0 && strcmp(path, "/mnt/usb0/x") == 0);
    CHECK(sceUsbStorageRequestMap(0, "", 0, 0, NULL, NULL) == 0);
    // The mapped path is cut at 127 characters.
    char longsub[300];
    memset(longsub, 'a', sizeof(longsub) - 1);
    longsub[sizeof(longsub) - 1] = '\0';
    CHECK(sceUsbStorageRequestMap(0, longsub, 0, 0, path, NULL) == 0 && strlen(path) == 127);
    CHECK(sceUsbStorageRequestUnmap(0, NULL) == E_INVAL);
    CHECK(sceUsbStorageRequestUnmap(0, "/mnt/usb0") == 0);

    // Callbacks: only checked.
    CHECK(sceUsbStorageRegisterCallback(1, 0, 0) == E_INVAL);
    CHECK(sceUsbStorageRegisterCallback(3, 0x1000, 0) == E_INVAL);
    CHECK(sceUsbStorageRegisterCallback(2, 0x1000, 0) == 0);
    CHECK(sceUsbStorageUnregisterCallback(0, 0x1000) == E_INVAL);
    CHECK(sceUsbStorageUnregisterCallback(1, 0x1000) == 0);
    CHECK(sceUsbStorageRegisterCallbackForMapAvailable(0, NULL, 0x1000, 0) == E_INVAL);
    CHECK(sceUsbStorageRegisterCallbackForMapAvailable(0, "/mnt/usb0", 0, 0) == E_INVAL);
    CHECK(sceUsbStorageRegisterCallbackForMapAvailable(0, "/mnt/usb0", 0x1000, 0) == 0);
    CHECK(sceUsbStorageUnregisterCallbackForMapAvailable(0, "/mnt/usb0", 0x1000) == 0);
    CHECK(sceUsbStorageRegisterCallbackForMapUnavailable(0, "/mnt/usb0", 0x1000, 0) == 0);
    CHECK(sceUsbStorageUnregisterCallbackForMapUnavailable(0, NULL, 0x1000) == E_INVAL);
    printf("PASS map\n");
}

static void test_getdents(void) {
    int32_t fd = -1;
    uint64_t bufsize = 0;
    CHECK(sceUsbStorageGetdentsOpen(0, NULL, &bufsize) == E_INVAL);
    CHECK(sceUsbStorageGetdentsOpen(1, &fd, &bufsize) == E_NOENT);
    // GetdentsOpen creates a missing directory.
    CHECK(sceUsbStorageGetdentsOpen(0, &fd, &bufsize) == 0 && bufsize == 0x8000);
    struct stat st;
    CHECK(stat(g_usb, &st) == 0);
    uint8_t buf[0x8000];
    uint64_t got = 99;
    CHECK(sceUsbStorageGetdentsRead(fd, buf, sizeof(buf), &got) == 0 && got == 0);
    CHECK(sceUsbStorageGetdentsClose(fd) == 0);

    touch("a.sav");
    touch("abcdefghij.dat");
    touch(".hidden");
    char sub[2048];
    snprintf(sub, sizeof(sub), "%s/dir", g_usb);
    CHECK(mkdir(sub, 0755) == 0);

    CHECK(sceUsbStorageGetdentsOpen(0, &fd, &bufsize) == 0);
    CHECK(sceUsbStorageGetdentsRead(fd, NULL, sizeof(buf), &got) == E_INVAL);
    CHECK(sceUsbStorageGetdentsRead(fd, buf, sizeof(buf), NULL) == E_INVAL);
    // A buffer too small for the first record returns nothing.
    CHECK(sceUsbStorageGetdentsRead(fd, buf, 8, &got) == 0 && got == 0);
    CHECK(sceUsbStorageGetdentsRead(fd, buf, sizeof(buf), &got) == 0);
    int seen_a = 0, seen_long = 0, seen_dir = 0, entries = 0;
    for (uint64_t off = 0; off < got; entries++) {
        const OrbisUsbStorageDirent *de = (const OrbisUsbStorageDirent *)(buf + off);
        CHECK(de->d_reclen % 8 == 0 && de->d_reclen == ((8 + de->d_namlen + 1 + 7) & ~7));
        CHECK(de->file_no == (uint32_t)entries + 1);
        CHECK(de->d_name[de->d_namlen] == '\0' && de->d_name[0] != '.');
        if (strcmp(de->d_name, "a.sav") == 0) {
            seen_a = de->d_type == DT_REG && de->d_reclen == 16;
        } else if (strcmp(de->d_name, "abcdefghij.dat") == 0) {
            seen_long = de->d_type == DT_REG && de->d_reclen == 24;
        } else if (strcmp(de->d_name, "dir") == 0) {
            seen_dir = de->d_type == DT_DIR;
        }
        off += de->d_reclen;
    }
    CHECK(entries == 3 && seen_a && seen_long && seen_dir);
    // Read past the end: nothing more.
    CHECK(sceUsbStorageGetdentsRead(fd, buf, sizeof(buf), &got) == 0 && got == 0);
    CHECK(sceUsbStorageGetdentsClose(fd) == 0);
    got = 5;
    CHECK(sceUsbStorageGetdentsRead(fd, buf, sizeof(buf), &got) == E_NOTSUP && got == 0);

    // Reads stop at record boundaries: a 24-byte buffer holds one record of 16 or 24 bytes.
    CHECK(sceUsbStorageGetdentsOpen(0, &fd, &bufsize) == 0);
    int records = 0;
    for (;;) {
        CHECK(sceUsbStorageGetdentsRead(fd, buf, 24, &got) == 0);
        if (got == 0) {
            break;
        }
        CHECK(got == ((const OrbisUsbStorageDirent *)buf)->d_reclen);
        records++;
    }
    CHECK(records == 3);
    CHECK(sceUsbStorageGetdentsClose(fd) == 0);
    printf("PASS getdents\n");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    setup();
    int rc = 0;
    if (strcmp(argv[1], "device") == 0) {
        test_device();
    } else if (strcmp(argv[1], "map") == 0) {
        test_map();
    } else if (strcmp(argv[1], "getdents") == 0) {
        test_getdents();
    } else {
        fprintf(stderr, "unknown case %s\n", argv[1]);
        rc = 2;
    }
    remove_tree(g_data);
    return rc;
}
