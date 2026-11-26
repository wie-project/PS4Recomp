// SPDX-License-Identifier: GPL-2.0-or-later

// libSceAppContent against shadPS4's app_content.cpp on host memory (guest base 0, so guest
// addresses are host pointers), over a game directory, add-on folders and license files the
// cases write: the initialization checks, add-on content per service label with its entitlement
// keys, license-only entitlements, the EntitlementUpdate system event, mounting, and temporary
// data. The VFS is replaced by the hooks below, which record the mounts.

#include "ps4_app_content.h"
#include "ps4_np_entitlement_access.h"
#include "ps4_system_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

#include "../host_guest_mem.h"

// ---------------------------------------------------------------------------------------------
// Hooks: the VFS, the compiled SDK version and the system language.

static char g_root[512], g_app[600];
static char g_mount_guest[16][32], g_mount_host[16][1024];
static int g_mounts;
static char g_unmounted[32];
static uint32_t g_sdk_version = 0x4508101;

const char *ps4_vfs_get_app_root(void) { return g_app; }

int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz) {
    (void)guest_path;
    (void)host_path;
    (void)host_path_sz;
    return -1;
}

int ps4_vfs_mount(const char *guest_prefix, const char *host_path) {
    if (g_mounts < 16) {
        snprintf(g_mount_guest[g_mounts], sizeof(g_mount_guest[0]), "%s", guest_prefix);
        snprintf(g_mount_host[g_mounts], sizeof(g_mount_host[0]), "%s", host_path);
        g_mounts++;
    }
    return 0;
}

int ps4_vfs_unmount(const char *guest_prefix) {
    snprintf(g_unmounted, sizeof(g_unmounted), "%s", guest_prefix);
    return 0;
}

uint32_t ps4_compiled_sdk_version(const GuestContext *ctx) {
    (void)ctx;
    return g_sdk_version;
}

int ps4_host_choose_language(void) { return 1; }

// The host directory last mounted at guest, or NULL.
static const char *mounted(const char *guest) {
    for (int i = g_mounts - 1; i >= 0; i--) {
        if (strcmp(g_mount_guest[i], guest) == 0) return g_mount_host[i];
    }
    return NULL;
}

static int32_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    GuestContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mem_base = GUEST_MEM;
    ctx.rdi = a0;
    ctx.rsi = a1;
    ctx.rdx = a2;
    ctx.rcx = a3;
    ctx.rax = 99;
    shim(&ctx);
    return (int32_t)ctx.rax;
}

// ---------------------------------------------------------------------------------------------
// Files the cases write.

static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v) {
    put_le16(p, (uint16_t)v);
    put_le16(p + 2, (uint16_t)(v >> 16));
}

static void put_le64(uint8_t *p, uint64_t v) {
    put_le32(p, (uint32_t)v);
    put_le32(p + 4, (uint32_t)(v >> 32));
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int mkdirs(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || access(tmp, F_OK) == 0 ? 0 : -1;
}

static int write_file(const char *path, const void *data, size_t size) {
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    *strrchr(dir, '/') = '\0';
    if (mkdirs(dir) != 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    const size_t n = fwrite(data, 1, size, f);
    return fclose(f) == 0 && n == size ? 0 : -1;
}

// <dir>/sce_sys/param.sfo with string values; a NULL key ends the list.
static int write_sfo(const char *dir, const char *const *kv) {
    static uint8_t sfo[2048];
    memset(sfo, 0, sizeof(sfo));
    uint32_t n = 0;
    while (kv[2 * n]) n++;
    const uint32_t key_table = 20 + n * 16, data_table = key_table + 256;
    sfo[1] = 'P';
    sfo[2] = 'S';
    sfo[3] = 'F';
    put_le32(sfo + 4, 0x101);
    put_le32(sfo + 8, key_table);
    put_le32(sfo + 12, data_table);
    put_le32(sfo + 16, n);
    uint32_t key_off = 0, data_off = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *e = sfo + 20 + i * 16;
        const uint32_t len = (uint32_t)strlen(kv[2 * i + 1]) + 1;
        put_le16(e, (uint16_t)key_off);
        put_le16(e + 2, 0x0204);
        put_le32(e + 4, len);
        put_le32(e + 8, 64);
        put_le32(e + 12, data_off);
        memcpy(sfo + key_table + key_off, kv[2 * i], strlen(kv[2 * i]) + 1);
        memcpy(sfo + data_table + data_off, kv[2 * i + 1], len);
        key_off += (uint32_t)strlen(kv[2 * i]) + 1;
        data_off += 64;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/sce_sys/param.sfo", dir);
    return write_file(path, sfo, data_table + data_off);
}

// A license file whose 16 key bytes at offset are seed, seed + 1, ...
static int write_license(const char *dir, const char *name, long offset, uint8_t seed) {
    static uint8_t buf[0x400];
    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 16; i++) buf[offset + i] = (uint8_t)(seed + i);
    char path[1024];
    snprintf(path, sizeof(path), "%s/sce_sys/%s", dir, name);
    return write_file(path, buf, sizeof(buf));
}

// <licenses>/<service>.idx and .rif: one license-only entitlement and one game license.
static int write_licenses(const char *service, const char *ps4al_label, uint8_t seed) {
    static uint8_t ridx[RIDX_HEADER_SIZE + 2 * RIDX_ENTRY_SIZE];
    static uint8_t rifa[RIFA_HEADER_SIZE + 2 * RIF_SIZE];
    memset(ridx, 0, sizeof(ridx));
    memset(rifa, 0, sizeof(rifa));
    put_be32(ridx, RIDX_MAGIC);
    put_le32(ridx + 4, 2);
    ridx[8] = RIDX_VERSION;
    put_be32(rifa, RIFA_MAGIC);
    const char *labels[2] = {ps4al_label, "GAMELICENSE00000"};
    const uint16_t types[2] = {RIF_CONTENT_TYPE_PS4AL, 0x1A00};
    for (int i = 0; i < 2; i++) {
        uint8_t *e = ridx + RIDX_HEADER_SIZE + i * RIDX_ENTRY_SIZE;
        strncpy((char *)e, labels[i], 16);
        const uint64_t off = RIFA_HEADER_SIZE + (uint64_t)i * RIF_SIZE;
        put_le64(e + 0x10, off);
        put_le64(e + 0x18, RIF_SIZE);
        uint8_t *rif = rifa + off;
        put_be32(rif, RIF_MAGIC);
        put_le16(rif + RIF_CONTENT_TYPE_OFFSET, types[i]);
        for (int k = 0; k < 16; k++) rif[RIF_ENTITLEMENT_KEY_OFFSET + k] = (uint8_t)(seed + k);
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/licenses/%s.idx", g_root, service);
    if (write_file(path, ridx, sizeof(ridx)) != 0) return -1;
    snprintf(path, sizeof(path), "%s/licenses/%s.rif", g_root, service);
    return write_file(path, rifa, sizeof(rifa));
}

#define CID(label) "UP0001-CUSA01234_00-" label

// The game in <root>/game; its add-ons in <root>/game/addcont and <root>/addcont/CUSA01234, the
// linked service UP0002-CUSA05678_00 in <root>/addcont/CUSA05678.
static int setup_tree(void) {
    const char *tmp = getenv("TMPDIR");
    snprintf(g_root, sizeof(g_root), "%s/ps4-appcontent-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(g_root)) return -1;
    setenv("PS4RECOMP_DATA_DIR", g_root, 1);
    snprintf(g_app, sizeof(g_app), "%s/game", g_root);
    char d[1024];
    const char *game[] = {"TITLE_ID", "CUSA01234", "CONTENT_ID", CID("GAMEDATA00000001"), "CATEGORY", "gd",
                          "SERVICE_ID_ADDCONT_ADD_1", "UP0002-CUSA05678_00", "SERVICE_ID_ADDCONT_ADD_2", "",
                          NULL};
    if (write_sfo(g_app, game) != 0) return -1;

    snprintf(d, sizeof(d), "%s/addcont/DLC_A", g_app);
    const char *a[] = {"CATEGORY", "ac", "CONTENT_ID", CID("DLCAAAAAAAAAAAA1"), NULL};
    if (write_sfo(d, a) != 0 || write_license(d, "license.info", 0x30, 0x10) != 0) return -1;

    snprintf(d, sizeof(d), "%s/addcont/CUSA01234/DLC_A_COPY", g_root);
    if (write_sfo(d, a) != 0) return -1;
    snprintf(d, sizeof(d), "%s/addcont/CUSA01234/DLC_B", g_root);
    const char *b[] = {"CONTENT_ID", CID("DLCBBBBBBBBBBBB2"), NULL}; // no CATEGORY: accepted
    if (write_sfo(d, b) != 0 || write_license(d, "license.dat", 0x2E0, 0x20) != 0) return -1;
    snprintf(d, sizeof(d), "%s/addcont/CUSA01234/GAME", g_root);
    const char *g[] = {"CATEGORY", "gd", "CONTENT_ID", CID("NOTANADDON000000"), NULL};
    if (write_sfo(d, g) != 0) return -1;
    snprintf(d, sizeof(d), "%s/addcont/CUSA01234/SHORT", g_root);
    const char *s[] = {"CATEGORY", "ac", "CONTENT_ID", "UP0001-CUSA01234_00", NULL};
    if (write_sfo(d, s) != 0) return -1;
    snprintf(d, sizeof(d), "%s/addcont/CUSA01234/EMPTY", g_root);
    if (mkdirs(d) != 0) return -1;

    snprintf(d, sizeof(d), "%s/addcont/CUSA05678/DLC_C", g_root);
    const char *c[] = {"CATEGORY", "ac", "CONTENT_ID", "UP0002-CUSA05678_00-DLCCCCCCCCCCCCC3", NULL};
    if (write_sfo(d, c) != 0) return -1;

    return write_licenses("UP0001-CUSA01234_00", "LICONLY000000004", 0x40);
}

static void remove_tree(void) {
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_root);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", g_root);
}

static OrbisNpUnifiedEntitlementLabel label_of(const char *s) {
    OrbisNpUnifiedEntitlementLabel l;
    memset(&l, 0, sizeof(l));
    snprintf(l.data, sizeof(l.data), "%s", s);
    return l;
}

static int key_is(const OrbisAppContentGetEntitlementKey *k, uint8_t seed) {
    for (int i = 0; i < 16; i++) {
        if (k->data[i] != (uint8_t)(seed + i)) return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Cases

static int case_not_initialized(void) {
    OrbisNpUnifiedEntitlementLabel l = label_of("DLCAAAAAAAAAAAA1");
    OrbisAppContentAddcontInfo info;
    OrbisAppContentGetEntitlementKey key;
    OrbisAppContentMountPoint mp;
    uint32_t hits = 0;
    const int32_t E = ORBIS_APP_CONTENT_ERROR_NOT_INITIALIZED;
    CHECK(call(shim_sceAppContentGetAddcontInfo, 0, A(&l), A(&info), 0) == E);
    CHECK(call(shim_sceAppContentGetAddcontInfoList, 0, 0, 0, A(&hits)) == E);
    CHECK(call(shim_sceAppContentGetEntitlementKey, 0, A(&l), A(&key), 0) == E);
    CHECK(call(shim_sceAppContentAddcontMount, 0, A(&l), A(&mp), 0) == E);
    CHECK(call(shim_sceAppContentAddcontUnmount, A(&mp), 0, 0, 0) == E);
    CHECK(call(shim_sceAppContentTemporaryDataMount2, 0, A(&mp), 0, 0) == E);
    CHECK(call(shim_sceAppContentTemporaryDataUnmount, A(&mp), 0, 0, 0) == E);
    // The parameters do not need the library.
    int32_t v = 0;
    CHECK(call(shim_sceAppContentAppParamGetInt, ORBIS_APP_CONTENT_APPPARAM_ID_SKU_FLAG, A(&v), 0, 0) == 0);
    CHECK(v == ORBIS_APP_CONTENT_APPPARAM_SKU_FLAG_FULL);
    CHECK(call(shim_sceAppContentAppParamGetInt, 5, A(&v), 0, 0) == ORBIS_APP_CONTENT_ERROR_PARAMETER);
    CHECK(call(shim_sceAppContentAppParamGetInt, 0, 0, 0, 0) == ORBIS_APP_CONTENT_ERROR_PARAMETER);
    printf("PASS not-initialized\n");
    return 0;
}

static int case_addcont(void) {
    CHECK(setup_tree() == 0);
    OrbisSystemServiceStatus st;
    static OrbisSystemServiceEvent ev;

    CHECK(call(shim_sceAppContentInitialize, 0, 0, 0, 0) == 0);
    CHECK(strcmp(mounted("/download0"), "") != 0);
    // One EntitlementUpdate event (user 0, service label 0).
    CHECK(sceSystemServiceGetStatus(&st) == 0 && st.event_num == 1);
    memset(&ev, 0xff, sizeof(ev));
    CHECK(sceSystemServiceReceiveEvent(&ev) == 0);
    CHECK(ev.event_type == ORBIS_SYSTEM_SERVICE_EVENT_ENTITLEMENT_UPDATE);
    CHECK(ev.data[0] == 0 && ev.data[4] == 0);
    CHECK(sceSystemServiceReceiveEvent(&ev) == (int32_t)ORBIS_SYSTEM_SERVICE_ERROR_NO_EVENT);

    // A second initialization: BUSY from SDK 1.50, a reload before.
    CHECK(call(shim_sceAppContentInitialize, 0, 0, 0, 0) == ORBIS_APP_CONTENT_ERROR_BUSY);
    g_sdk_version = 0x1000000;
    CHECK(call(shim_sceAppContentInitialize, 0, 0, 0, 0) == 0);
    CHECK(sceSystemServiceReceiveEvent(&ev) == 0); // a second EntitlementUpdate

    // Service label 0: DLC_A (app root), DLC_B (no CATEGORY), the license-only entitlement.
    uint32_t hits = 99;
    CHECK(call(shim_sceAppContentGetAddcontInfoList, 0, 0, 0, A(&hits)) == 0 && hits == 3);
    CHECK(call(shim_sceAppContentGetAddcontInfoList, 0, 0, 0, 0) == ORBIS_APP_CONTENT_ERROR_PARAMETER);
    CHECK(call(shim_sceAppContentGetAddcontInfoList, 8, 0, 0, A(&hits)) == ORBIS_APP_CONTENT_ERROR_PARAMETER);
    CHECK(call(shim_sceAppContentGetAddcontInfoList, 1, 0, 0, A(&hits)) == 0 && hits == 1);
    CHECK(call(shim_sceAppContentGetAddcontInfoList, 2, 0, 0, A(&hits)) == 0 && hits == 0);
    OrbisAppContentAddcontInfo list[4];
    memset(list, 0, sizeof(list));
    CHECK(call(shim_sceAppContentGetAddcontInfoList, 0, A(list), 2, A(&hits)) == 0 && hits == 2);
    CHECK(strcmp(list[0].entitlement_label.data, "DLCAAAAAAAAAAAA1") == 0);
    CHECK(list[0].status == ORBIS_APP_CONTENT_ADDCONT_DOWNLOAD_STATUS_INSTALLED);
    CHECK(call(shim_sceAppContentGetAddcontInfoList, 0, A(list), 4, 0) == 0);
    int found_b = 0, found_lic = 0;
    for (int i = 0; i < 3; i++) {
        if (strcmp(list[i].entitlement_label.data, "DLCBBBBBBBBBBBB2") == 0) {
            found_b = list[i].status == ORBIS_APP_CONTENT_ADDCONT_DOWNLOAD_STATUS_INSTALLED;
        }
        if (strcmp(list[i].entitlement_label.data, "LICONLY000000004") == 0) {
            found_lic = list[i].status == ORBIS_APP_CONTENT_ADDCONT_DOWNLOAD_STATUS_NO_EXTRA_DATA;
        }
    }
    CHECK(found_b && found_lic);

    // Info and keys: license.info, license.dat, the license container.
    OrbisNpUnifiedEntitlementLabel la = label_of("DLCAAAAAAAAAAAA1"), lb = label_of("DLCBBBBBBBBBBBB2"),
                                   lc = label_of("DLCCCCCCCCCCCCC3"), ll = label_of("LICONLY000000004"),
                                   lx = label_of("UNKNOWN000000000");
    OrbisAppContentAddcontInfo info;
    OrbisAppContentGetEntitlementKey key;
    CHECK(call(shim_sceAppContentGetAddcontInfo, 0, A(&lb), A(&info), 0) == 0);
    CHECK(strcmp(info.entitlement_label.data, "DLCBBBBBBBBBBBB2") == 0);
    CHECK(call(shim_sceAppContentGetAddcontInfo, 0, A(&lc), A(&info), 0) == ORBIS_APP_CONTENT_ERROR_DRM_NO_ENTITLEMENT);
    CHECK(call(shim_sceAppContentGetAddcontInfo, 1, A(&lc), A(&info), 0) == 0);
    CHECK(call(shim_sceAppContentGetAddcontInfo, 8, A(&lc), A(&info), 0) == ORBIS_APP_CONTENT_ERROR_PARAMETER);
    CHECK(call(shim_sceAppContentGetAddcontInfo, 0, 0, A(&info), 0) == ORBIS_APP_CONTENT_ERROR_PARAMETER);
    CHECK(call(shim_sceAppContentGetEntitlementKey, 0, A(&la), A(&key), 0) == 0 && key_is(&key, 0x10));
    CHECK(call(shim_sceAppContentGetEntitlementKey, 0, A(&lb), A(&key), 0) == 0 && key_is(&key, 0x20));
    CHECK(call(shim_sceAppContentGetEntitlementKey, 0, A(&ll), A(&key), 0) == 0 && key_is(&key, 0x40));
    CHECK(call(shim_sceAppContentGetEntitlementKey, 0, A(&lx), A(&key), 0) == ORBIS_APP_CONTENT_ERROR_DRM_NO_ENTITLEMENT);
    CHECK(call(shim_sceAppContentGetEntitlementKey, 0, A(&la), 0, 0) == ORBIS_APP_CONTENT_ERROR_PARAMETER);

    // Mounting: /addcont<index>, the add-on's folder.
    OrbisAppContentMountPoint mp;
    char want[1100];
    CHECK(call(shim_sceAppContentAddcontMount, 0, A(&la), A(&mp), 0) == 0);
    CHECK(strcmp(mp.data, "/addcont0") == 0);
    snprintf(want, sizeof(want), "%s/addcont/DLC_A", g_app);
    CHECK(strcmp(mounted("/addcont0"), want) == 0);
    CHECK(call(shim_sceAppContentAddcontMount, 1, A(&lc), A(&mp), 0) == 0);
    CHECK(strcmp(mp.data, "/addcont3") == 0);
    snprintf(want, sizeof(want), "%s/game/../addcont/CUSA05678/DLC_C", g_root);
    CHECK(strcmp(mounted("/addcont3"), want) == 0);
    CHECK(call(shim_sceAppContentAddcontMount, 0, A(&lc), A(&mp), 0) == ORBIS_APP_CONTENT_ERROR_NOT_FOUND);
    CHECK(call(shim_sceAppContentAddcontMount, 0, A(&ll), A(&mp), 0) == ORBIS_APP_CONTENT_ERROR_NOT_FOUND);
    CHECK(call(shim_sceAppContentAddcontMount, 8, A(&la), A(&mp), 0) == ORBIS_APP_CONTENT_ERROR_PARAMETER);
    snprintf(mp.data, sizeof(mp.data), "/addcont3");
    CHECK(call(shim_sceAppContentAddcontUnmount, A(&mp), 0, 0, 0) == 0 && strcmp(g_unmounted, "/addcont3") == 0);
    CHECK(call(shim_sceAppContentAddcontUnmount, 0, 0, 0, 0) == ORBIS_APP_CONTENT_ERROR_PARAMETER);
    remove_tree();
    printf("PASS addcont\n");
    return 0;
}

static int case_temp_data(void) {
    CHECK(setup_tree() == 0);
    // No add-ons and no event without add-on folders or licenses.
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s/addcont' '%s/game/addcont' '%s/licenses'", g_root, g_root, g_root);
    CHECK(system(cmd) == 0);
    CHECK(call(shim_sceAppContentInitialize, 0, 0, 0, 0) == 0);
    OrbisSystemServiceStatus st;
    CHECK(sceSystemServiceGetStatus(&st) == 0 && st.event_num == 0);

    OrbisAppContentMountPoint mp;
    char file[1100];
    CHECK(call(shim_sceAppContentTemporaryDataMount2, 0, 0, 0, 0) == ORBIS_APP_CONTENT_ERROR_PARAMETER);
    CHECK(call(shim_sceAppContentTemporaryDataMount2, ORBIS_APP_CONTENT_TEMPORARY_DATA_OPTION_NONE, A(&mp), 0, 0) == 0);
    CHECK(strcmp(mp.data, "/temp0") == 0);
    const char *dir = mounted("/temp0");
    CHECK(dir != NULL);
    snprintf(file, sizeof(file), "%s/sub/kept", dir);
    CHECK(write_file(file, "x", 1) == 0);
    // NONE keeps the data, sceAppContentTemporaryDataMount formats it.
    CHECK(call(shim_sceAppContentTemporaryDataMount2, ORBIS_APP_CONTENT_TEMPORARY_DATA_OPTION_NONE, A(&mp), 0, 0) == 0);
    CHECK(access(file, F_OK) == 0);
    CHECK(call(shim_sceAppContentTemporaryDataMount, A(&mp), 0, 0, 0) == 0);
    CHECK(access(file, F_OK) != 0 && access(mounted("/temp0"), F_OK) == 0);
    CHECK(write_file(file, "x", 1) == 0);
    CHECK(call(shim_sceAppContentTemporaryDataFormat, A(&mp), 0, 0, 0) == 0);
    CHECK(access(file, F_OK) != 0);

    uint64_t kb = 0;
    CHECK(call(shim_sceAppContentTemporaryDataGetAvailableSpaceKb, A(&mp), A(&kb), 0, 0) == 0 && kb > 0);
    CHECK(call(shim_sceAppContentTemporaryDataGetAvailableSpaceKb, A(&mp), 0, 0, 0) == ORBIS_APP_CONTENT_ERROR_PARAMETER);
    kb = 0;
    CHECK(call(shim_sceAppContentDownloadDataGetAvailableSpaceKb, 0, A(&kb), 0, 0) == 0 && kb > 0);

    CHECK(call(shim_sceAppContentTemporaryDataUnmount, 0, 0, 0, 0) == ORBIS_APP_CONTENT_ERROR_PARAMETER);
    CHECK(call(shim_sceAppContentTemporaryDataUnmount, A(&mp), 0, 0, 0) == 0 && strcmp(g_unmounted, "/temp0") == 0);
    remove_tree();
    printf("PASS temp-data\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "not-initialized") == 0) return case_not_initialized();
    if (strcmp(argv[1], "addcont") == 0) return case_addcont();
    if (strcmp(argv[1], "temp-data") == 0) return case_temp_data();
    return 2;
}
