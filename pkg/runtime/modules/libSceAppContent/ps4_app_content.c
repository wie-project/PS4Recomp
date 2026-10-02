#include "ps4_app_content.h"
#include "ps4_sfo.h"
#include "ps4_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

typedef struct {
    char entitlement_label[ORBIS_NP_UNIFIED_ENTITLEMENT_LABEL_SIZE];
    uint32_t status;
    uint8_t key[ORBIS_APP_CONTENT_ENTITLEMENT_KEY_SIZE];
    char host_dir[1024];
} Ps4AddContEntry;

static bool s_initialized = false;
static Ps4SfoContext g_app_sfo;
static char g_title_id[32] = {0};
static char g_content_id[64] = {0};

static Ps4AddContEntry g_addcont[ORBIS_APP_CONTENT_INFO_LIST_MAX_SIZE];
static uint32_t g_addcont_count = 0;

static void scan_addcont_directory(const char *addon_path) {
    DIR *dir = opendir(addon_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_entry_path[1024];
        snprintf(full_entry_path, sizeof(full_entry_path), "%s/%s", addon_path, entry->d_name);

        struct stat st;
        if (stat(full_entry_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }

        // Check for param.sfo
        char sfo_path[1024];
        snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", full_entry_path);
        if (access(sfo_path, R_OK) != 0) {
            snprintf(sfo_path, sizeof(sfo_path), "%s/param.sfo", full_entry_path);
            if (access(sfo_path, R_OK) != 0) {
                continue;
            }
        }

        Ps4SfoContext dlc_sfo;
        if (!ps4_sfo_load_file(&dlc_sfo, sfo_path)) {
            continue;
        }

        char category[16] = {0};
        char content_id[128] = {0};
        ps4_sfo_get_string(&dlc_sfo, "CATEGORY", category, sizeof(category));
        ps4_sfo_get_string(&dlc_sfo, "CONTENT_ID", content_id, sizeof(content_id));
        ps4_sfo_free(&dlc_sfo);

        if (strncmp(category, "ac", 2) != 0 && strncmp(category, "AC", 2) != 0) {
            continue;
        }

        size_t cid_len = strlen(content_id);
        if (cid_len <= ORBIS_APP_CONTENT_ENTITLEMENT_LABEL_OFFSET) {
            continue;
        }

        const char *entitlement = content_id + ORBIS_APP_CONTENT_ENTITLEMENT_LABEL_OFFSET;
        if (g_addcont_count < ORBIS_APP_CONTENT_INFO_LIST_MAX_SIZE) {
            Ps4AddContEntry *e = &g_addcont[g_addcont_count++];
            strncpy(e->entitlement_label, entitlement, sizeof(e->entitlement_label) - 1);
            e->entitlement_label[sizeof(e->entitlement_label) - 1] = '\0';
            e->status = ORBIS_APP_CONTENT_ADDCONT_DOWNLOAD_STATUS_INSTALLED;
            strncpy(e->host_dir, full_entry_path, sizeof(e->host_dir) - 1);
            e->host_dir[sizeof(e->host_dir) - 1] = '\0';
            memset(e->key, 0, sizeof(e->key));
        }
    }
    closedir(dir);
}

static void ensure_temp_dir(char *out_path, size_t out_sz) {
    const char *app_root = ps4_vfs_get_app_root();
    if (app_root && *app_root) {
        snprintf(out_path, out_sz, "%s/temp0", app_root);
        mkdir(out_path, 0755);
        if (access(out_path, W_OK) == 0) {
            return;
        }
    }
    // Fallback to /tmp
    snprintf(out_path, out_sz, "/tmp/ps4_app_temp0");
    mkdir(out_path, 0755);
}

int32_t sceAppContentInitialize(const OrbisAppContentInitParam *initParam, OrbisAppContentBootParam *bootParam) {
    (void)initParam;
    if (bootParam) {
        memset(bootParam, 0, sizeof(OrbisAppContentBootParam));
        bootParam->attr = 0;
    }

    if (s_initialized) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_BUSY;
    }

    // Try loading param.sfo
    if (ps4_sfo_find_and_load(&g_app_sfo)) {
        ps4_sfo_get_string(&g_app_sfo, "TITLE_ID", g_title_id, sizeof(g_title_id));
        ps4_sfo_get_string(&g_app_sfo, "CONTENT_ID", g_content_id, sizeof(g_content_id));
    }

    // Fallback: check contentid.txt in app root
    if (g_content_id[0] == '\0') {
        const char *app_root = ps4_vfs_get_app_root();
        char cid_file[1024];
        snprintf(cid_file, sizeof(cid_file), "%s/contentid.txt", app_root);
        FILE *f = fopen(cid_file, "r");
        if (f) {
            if (fgets(g_content_id, sizeof(g_content_id), f)) {
                size_t l = strlen(g_content_id);
                while (l > 0 && (g_content_id[l - 1] == '\r' || g_content_id[l - 1] == '\n')) {
                    g_content_id[--l] = '\0';
                }
                // Try extracting title_id: UPXXXX-CUSAXXXXX_XX-...
                const char *p = strstr(g_content_id, "-CUSA");
                if (p && strlen(p + 1) >= 9) {
                    strncpy(g_title_id, p + 1, 9);
                    g_title_id[9] = '\0';
                }
            }
            fclose(f);
        }
    }

    // Default title_id if still empty
    if (g_title_id[0] == '\0') {
        strncpy(g_title_id, "CUSA00000", sizeof(g_title_id) - 1);
    }

    // Mount /temp0 in VFS
    char temp_dir[1024];
    ensure_temp_dir(temp_dir, sizeof(temp_dir));
    ps4_vfs_mount("/temp0", temp_dir);

    // Scan for DLCs
    const char *app_root = ps4_vfs_get_app_root();
    if (app_root && *app_root) {
        char addon_dir[1024];
        snprintf(addon_dir, sizeof(addon_dir), "%s/addcont", app_root);
        scan_addcont_directory(addon_dir);

        snprintf(addon_dir, sizeof(addon_dir), "%s/../addcont/%s", app_root, g_title_id);
        scan_addcont_directory(addon_dir);
    }

    s_initialized = true;
    return 0;
}

int32_t sceAppContentAppParamGetInt(int32_t paramId, int32_t *value) {
    if (!value) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }

    switch (paramId) {
    case ORBIS_APP_CONTENT_APPPARAM_ID_SKU_FLAG:
        *value = ORBIS_APP_CONTENT_APPPARAM_SKU_FLAG_FULL;
        return 0;
    case ORBIS_APP_CONTENT_APPPARAM_ID_USER_DEFINED_PARAM_1:
        if (!ps4_sfo_get_int(&g_app_sfo, "USER_DEFINED_PARAM_1", value)) {
            *value = 0;
        }
        return 0;
    case ORBIS_APP_CONTENT_APPPARAM_ID_USER_DEFINED_PARAM_2:
        if (!ps4_sfo_get_int(&g_app_sfo, "USER_DEFINED_PARAM_2", value)) {
            *value = 0;
        }
        return 0;
    case ORBIS_APP_CONTENT_APPPARAM_ID_USER_DEFINED_PARAM_3:
        if (!ps4_sfo_get_int(&g_app_sfo, "USER_DEFINED_PARAM_3", value)) {
            *value = 0;
        }
        return 0;
    case ORBIS_APP_CONTENT_APPPARAM_ID_USER_DEFINED_PARAM_4:
        if (!ps4_sfo_get_int(&g_app_sfo, "USER_DEFINED_PARAM_4", value)) {
            *value = 0;
        }
        return 0;
    default:
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
}

int32_t sceAppContentAppParamGetString(int32_t paramId, char *buf, size_t bufSize) {
    if (!buf || bufSize == 0) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }

    char key[64];
    if (paramId == 0) {
        strncpy(buf, g_title_id, bufSize - 1);
        buf[bufSize - 1] = '\0';
        return 0;
    } else if (paramId == 1) {
        strncpy(buf, g_content_id, bufSize - 1);
        buf[bufSize - 1] = '\0';
        return 0;
    } else {
        snprintf(key, sizeof(key), "USER_DEFINED_PARAM_%d", paramId);
        if (!ps4_sfo_get_string(&g_app_sfo, key, buf, bufSize)) {
            buf[0] = '\0';
        }
        return 0;
    }
}

int32_t sceAppContentTemporaryDataMount2(uint32_t option, OrbisAppContentMountPoint *mountPoint) {
    if (!mountPoint) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }

    char temp_dir[1024];
    ensure_temp_dir(temp_dir, sizeof(temp_dir));
    ps4_vfs_mount("/temp0", temp_dir);

    if (option & ORBIS_APP_CONTENT_TEMPORARY_DATA_OPTION_FORMAT) {
        sceAppContentTemporaryDataFormat(mountPoint);
    }

    strncpy(mountPoint->data, "/temp0", sizeof(mountPoint->data) - 1);
    mountPoint->data[sizeof(mountPoint->data) - 1] = '\0';
    return 0;
}

int32_t sceAppContentTemporaryDataMount(OrbisAppContentMountPoint *mountPoint) {
    return sceAppContentTemporaryDataMount2(ORBIS_APP_CONTENT_TEMPORARY_DATA_OPTION_NONE, mountPoint);
}

int32_t sceAppContentTemporaryDataUnmount(const OrbisAppContentMountPoint *mountPoint) {
    (void)mountPoint;
    return 0;
}

int32_t sceAppContentTemporaryDataFormat(const OrbisAppContentMountPoint *mountPoint) {
    (void)mountPoint;
    char temp_dir[1024];
    ensure_temp_dir(temp_dir, sizeof(temp_dir));

    DIR *dir = opendir(temp_dir);
    if (!dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s/%s", temp_dir, entry->d_name);
        unlink(file_path);
    }
    closedir(dir);
    return 0;
}

int32_t sceAppContentTemporaryDataGetAvailableSpaceKb(const OrbisAppContentMountPoint *mountPoint, uint64_t *availableSpaceKb) {
    (void)mountPoint;
    if (!availableSpaceKb) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }

    char temp_dir[1024];
    ensure_temp_dir(temp_dir, sizeof(temp_dir));

    struct statvfs st;
    if (statvfs(temp_dir, &st) == 0) {
        *availableSpaceKb = ((uint64_t)st.f_bavail * (uint64_t)st.f_frsize) / 1024;
    } else {
        *availableSpaceKb = 10485760ULL; // 10 GB fallback in KB
    }
    return 0;
}

int32_t sceAppContentDownloadDataGetAvailableSpaceKb(const OrbisAppContentMountPoint *mountPoint, uint64_t *availableSpaceKb) {
    (void)mountPoint;
    if (!availableSpaceKb) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }

    const char *app_root = ps4_vfs_get_app_root();
    struct statvfs st;
    if (app_root && statvfs(app_root, &st) == 0) {
        *availableSpaceKb = ((uint64_t)st.f_bavail * (uint64_t)st.f_frsize) / 1024;
    } else {
        *availableSpaceKb = 10485760ULL; // 10 GB fallback in KB
    }
    return 0;
}

int32_t sceAppContentDownloadDataFormat(const OrbisAppContentMountPoint *mountPoint) {
    (void)mountPoint;
    return 0;
}

int32_t sceAppContentGetAddcontInfo(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlementLabel, OrbisAppContentAddcontInfo *info) {
    (void)service_label;
    if (!entitlementLabel || !info) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }

    for (uint32_t i = 0; i < g_addcont_count; i++) {
        if (strncmp(entitlementLabel->data, g_addcont[i].entitlement_label, sizeof(g_addcont[i].entitlement_label)) == 0) {
            strncpy(info->entitlement_label.data, g_addcont[i].entitlement_label, sizeof(info->entitlement_label.data));
            info->status = g_addcont[i].status;
            return 0;
        }
    }
    return (int32_t)ORBIS_APP_CONTENT_ERROR_DRM_NO_ENTITLEMENT;
}

int32_t sceAppContentGetAddcontInfoList(uint32_t service_label, OrbisAppContentAddcontInfo *list, uint32_t list_num, uint32_t *hit_num) {
    (void)service_label;
    if (!list || list_num == 0) {
        if (hit_num) {
            *hit_num = g_addcont_count;
        }
        return 0;
    }

    uint32_t count = g_addcont_count < list_num ? g_addcont_count : list_num;
    for (uint32_t i = 0; i < count; i++) {
        strncpy(list[i].entitlement_label.data, g_addcont[i].entitlement_label, sizeof(list[i].entitlement_label.data));
        list[i].status = g_addcont[i].status;
    }
    if (hit_num) {
        *hit_num = count;
    }
    return 0;
}

int32_t sceAppContentGetEntitlementKey(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label, OrbisAppContentGetEntitlementKey *key) {
    (void)service_label;
    if (!entitlement_label || !key) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }

    for (uint32_t i = 0; i < g_addcont_count; i++) {
        if (strncmp(entitlement_label->data, g_addcont[i].entitlement_label, sizeof(g_addcont[i].entitlement_label)) == 0) {
            memcpy(key->data, g_addcont[i].key, sizeof(key->data));
            return 0;
        }
    }
    return (int32_t)ORBIS_APP_CONTENT_ERROR_DRM_NO_ENTITLEMENT;
}

int32_t sceAppContentAddcontMount(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label, OrbisAppContentMountPoint *mount_point) {
    (void)service_label;
    if (!entitlement_label || !mount_point) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }

    for (uint32_t i = 0; i < g_addcont_count; i++) {
        if (strncmp(entitlement_label->data, g_addcont[i].entitlement_label, sizeof(g_addcont[i].entitlement_label)) == 0) {
            snprintf(mount_point->data, sizeof(mount_point->data), "/addcont%u", i);
            ps4_vfs_mount(mount_point->data, g_addcont[i].host_dir);
            return 0;
        }
    }
    return (int32_t)ORBIS_APP_CONTENT_ERROR_NOT_FOUND;
}

int32_t sceAppContentAddcontUnmount(const OrbisAppContentMountPoint *mount_point) {
    if (mount_point) {
        ps4_vfs_unmount(mount_point->data);
    }
    return 0;
}

int32_t sceAppContentAddcontDelete(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label) {
    (void)service_label;
    (void)entitlement_label;
    return 0;
}

int32_t sceAppContentAddcontEnqueueDownload(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label) {
    (void)service_label;
    (void)entitlement_label;
    return 0;
}

int32_t sceAppContentAddcontEnqueueDownloadSp(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label) {
    (void)service_label;
    (void)entitlement_label;
    return 0;
}

int32_t sceAppContentAddcontShrink(void) {
    return 0;
}

int32_t sceAppContentDownload0Expand(void) {
    return 0;
}

int32_t sceAppContentDownload0Shrink(void) {
    return 0;
}

int32_t sceAppContentDownload1Expand(void) {
    return 0;
}

int32_t sceAppContentDownload1Shrink(void) {
    return 0;
}

int32_t sceAppContentGetAddcontDownloadProgress(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label, void *progress) {
    (void)service_label;
    (void)entitlement_label;
    (void)progress;
    return 0;
}

int32_t sceAppContentGetRegion(void) {
    if (strncmp(g_title_id, "UP", 2) == 0) return 1; // America
    if (strncmp(g_title_id, "EP", 2) == 0) return 2; // Europe
    if (strncmp(g_title_id, "JP", 2) == 0) return 0; // Japan
    return 1;
}

int32_t sceAppContentRequestPatchInstall(void) {
    return 0;
}

int32_t sceAppContentSmallSharedDataFormat(void) {
    return 0;
}

int32_t sceAppContentSmallSharedDataGetAvailableSpaceKb(uint64_t *availableSpaceKb) {
    if (!availableSpaceKb) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    *availableSpaceKb = 1048576ULL; // 1 GB in KB
    return 0;
}

int32_t sceAppContentSmallSharedDataMount(OrbisAppContentMountPoint *mountPoint) {
    if (!mountPoint) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    strncpy(mountPoint->data, "/small_shared", sizeof(mountPoint->data) - 1);
    mountPoint->data[sizeof(mountPoint->data) - 1] = '\0';
    return 0;
}

int32_t sceAppContentSmallSharedDataUnmount(const OrbisAppContentMountPoint *mountPoint) {
    (void)mountPoint;
    return 0;
}

int32_t sceAppContentGetPftFlag(void) {
    return 0;
}

int32_t Func_C59A36FF8D7C59DA(void) {
    return 0;
}

int32_t sceAppContentAddcontEnqueueDownloadByEntitlementId(const char *entitlementId) {
    (void)entitlementId;
    return 0;
}

int32_t sceAppContentAddcontMountByEntitlementId(const char *entitlementId, OrbisAppContentMountPoint *mount_point) {
    if (!entitlementId || !mount_point) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    for (uint32_t i = 0; i < g_addcont_count; i++) {
        if (strcmp(entitlementId, g_addcont[i].entitlement_label) == 0) {
            snprintf(mount_point->data, sizeof(mount_point->data), "/addcont%u", i);
            ps4_vfs_mount(mount_point->data, g_addcont[i].host_dir);
            return 0;
        }
    }
    return (int32_t)ORBIS_APP_CONTENT_ERROR_NOT_FOUND;
}

int32_t sceAppContentGetAddcontInfoByEntitlementId(const char *entitlementId, OrbisAppContentAddcontInfo *info) {
    if (!entitlementId || !info) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    for (uint32_t i = 0; i < g_addcont_count; i++) {
        if (strcmp(entitlementId, g_addcont[i].entitlement_label) == 0) {
            strncpy(info->entitlement_label.data, g_addcont[i].entitlement_label, sizeof(info->entitlement_label.data));
            info->status = g_addcont[i].status;
            return 0;
        }
    }
    return (int32_t)ORBIS_APP_CONTENT_ERROR_DRM_NO_ENTITLEMENT;
}

int32_t sceAppContentGetAddcontInfoListByIroTag(void) {
    return 0;
}

int32_t sceAppContentGetDownloadedStoreCountry(char *countryCode) {
    if (!countryCode) {
        return (int32_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    if (strncmp(g_title_id, "EP", 2) == 0) {
        strncpy(countryCode, "GB", 3);
    } else {
        strncpy(countryCode, "US", 3);
    }
    return 0;
}

// Guest ABI shims

void shim_sceAppContentInitialize(GuestContext *ctx) {
    uint64_t initParamGuest = ctx->rdi;
    uint64_t bootParamGuest = ctx->rsi;
    const OrbisAppContentInitParam *initParam = NULL;
    OrbisAppContentBootParam *bootParam = NULL;

    if (initParamGuest != 0) {
        if (!ctx->mem_base || initParamGuest + sizeof(OrbisAppContentInitParam) > ctx->mem_size) {
            ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
            SHIM_RETURN();
        }
        initParam = (const OrbisAppContentInitParam *)(ctx->mem_base + initParamGuest);
    }
    if (bootParamGuest != 0) {
        if (!ctx->mem_base || bootParamGuest + sizeof(OrbisAppContentBootParam) > ctx->mem_size) {
            ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
            SHIM_RETURN();
        }
        bootParam = (OrbisAppContentBootParam *)(ctx->mem_base + bootParamGuest);
    }

    int32_t ret = sceAppContentInitialize(initParam, bootParam);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentAppParamGetInt(GuestContext *ctx) {
    int32_t paramId = (int32_t)ctx->rdi;
    uint64_t valGuestPtr = ctx->rsi;
    if (valGuestPtr == 0) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    if (!ctx->mem_base || valGuestPtr + sizeof(int32_t) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    int32_t val = 0;
    int32_t ret = sceAppContentAppParamGetInt(paramId, &val);
    if (ret == 0) {
        *(int32_t *)(ctx->mem_base + valGuestPtr) = val;
    }
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentAppParamGetString(GuestContext *ctx) {
    int32_t paramId = (int32_t)ctx->rdi;
    uint64_t bufGuestPtr = ctx->rsi;
    size_t bufSize = (size_t)ctx->rdx;
    if (bufGuestPtr == 0 || bufSize == 0) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    if (!ctx->mem_base || bufGuestPtr + bufSize > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    char *buf = (char *)(ctx->mem_base + bufGuestPtr);
    int32_t ret = sceAppContentAppParamGetString(paramId, buf, bufSize);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentTemporaryDataMount(GuestContext *ctx) {
    uint64_t mountPointGuest = ctx->rdi;
    if (mountPointGuest == 0) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    if (!ctx->mem_base || mountPointGuest + sizeof(OrbisAppContentMountPoint) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    OrbisAppContentMountPoint *mountPoint = (OrbisAppContentMountPoint *)(ctx->mem_base + mountPointGuest);
    int32_t ret = sceAppContentTemporaryDataMount(mountPoint);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentTemporaryDataMount2(GuestContext *ctx) {
    uint32_t option = (uint32_t)ctx->rdi;
    uint64_t mountPointGuest = ctx->rsi;
    if (mountPointGuest == 0) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    if (!ctx->mem_base || mountPointGuest + sizeof(OrbisAppContentMountPoint) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    OrbisAppContentMountPoint *mountPoint = (OrbisAppContentMountPoint *)(ctx->mem_base + mountPointGuest);
    int32_t ret = sceAppContentTemporaryDataMount2(option, mountPoint);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentTemporaryDataUnmount(GuestContext *ctx) {
    uint64_t mountPointGuest = ctx->rdi;
    const OrbisAppContentMountPoint *mountPoint = NULL;
    if (mountPointGuest != 0 && ctx->mem_base && mountPointGuest + sizeof(OrbisAppContentMountPoint) <= ctx->mem_size) {
        mountPoint = (const OrbisAppContentMountPoint *)(ctx->mem_base + mountPointGuest);
    }
    int32_t ret = sceAppContentTemporaryDataUnmount(mountPoint);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentTemporaryDataFormat(GuestContext *ctx) {
    uint64_t mountPointGuest = ctx->rdi;
    const OrbisAppContentMountPoint *mountPoint = NULL;
    if (mountPointGuest != 0 && ctx->mem_base && mountPointGuest + sizeof(OrbisAppContentMountPoint) <= ctx->mem_size) {
        mountPoint = (const OrbisAppContentMountPoint *)(ctx->mem_base + mountPointGuest);
    }
    int32_t ret = sceAppContentTemporaryDataFormat(mountPoint);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentTemporaryDataGetAvailableSpaceKb(GuestContext *ctx) {
    uint64_t mountPointGuest = ctx->rdi;
    uint64_t spaceGuest = ctx->rsi;
    if (spaceGuest == 0) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    if (!ctx->mem_base || spaceGuest + sizeof(uint64_t) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    const OrbisAppContentMountPoint *mountPoint = NULL;
    if (mountPointGuest != 0 && ctx->mem_base && mountPointGuest + sizeof(OrbisAppContentMountPoint) <= ctx->mem_size) {
        mountPoint = (const OrbisAppContentMountPoint *)(ctx->mem_base + mountPointGuest);
    }
    uint64_t avail = 0;
    int32_t ret = sceAppContentTemporaryDataGetAvailableSpaceKb(mountPoint, &avail);
    if (ret == 0) {
        *(uint64_t *)(ctx->mem_base + spaceGuest) = avail;
    }
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentDownloadDataGetAvailableSpaceKb(GuestContext *ctx) {
    uint64_t mountPointGuest = ctx->rdi;
    uint64_t spaceGuest = ctx->rsi;
    if (spaceGuest == 0) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    if (!ctx->mem_base || spaceGuest + sizeof(uint64_t) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    const OrbisAppContentMountPoint *mountPoint = NULL;
    if (mountPointGuest != 0 && ctx->mem_base && mountPointGuest + sizeof(OrbisAppContentMountPoint) <= ctx->mem_size) {
        mountPoint = (const OrbisAppContentMountPoint *)(ctx->mem_base + mountPointGuest);
    }
    uint64_t avail = 0;
    int32_t ret = sceAppContentDownloadDataGetAvailableSpaceKb(mountPoint, &avail);
    if (ret == 0) {
        *(uint64_t *)(ctx->mem_base + spaceGuest) = avail;
    }
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentDownloadDataFormat(GuestContext *ctx) {
    uint64_t mountPointGuest = ctx->rdi;
    const OrbisAppContentMountPoint *mountPoint = NULL;
    if (mountPointGuest != 0 && ctx->mem_base && mountPointGuest + sizeof(OrbisAppContentMountPoint) <= ctx->mem_size) {
        mountPoint = (const OrbisAppContentMountPoint *)(ctx->mem_base + mountPointGuest);
    }
    int32_t ret = sceAppContentDownloadDataFormat(mountPoint);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentGetAddcontInfo(GuestContext *ctx) {
    uint32_t service_label = (uint32_t)ctx->rdi;
    uint64_t labelGuest = ctx->rsi;
    uint64_t infoGuest = ctx->rdx;
    if (labelGuest == 0 || infoGuest == 0) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    if (!ctx->mem_base || labelGuest + sizeof(OrbisNpUnifiedEntitlementLabel) > ctx->mem_size ||
        infoGuest + sizeof(OrbisAppContentAddcontInfo) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    const OrbisNpUnifiedEntitlementLabel *label = (const OrbisNpUnifiedEntitlementLabel *)(ctx->mem_base + labelGuest);
    OrbisAppContentAddcontInfo *info = (OrbisAppContentAddcontInfo *)(ctx->mem_base + infoGuest);
    int32_t ret = sceAppContentGetAddcontInfo(service_label, label, info);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentGetAddcontInfoList(GuestContext *ctx) {
    uint32_t service_label = (uint32_t)ctx->rdi;
    uint64_t listGuest = ctx->rsi;
    uint32_t list_num = (uint32_t)ctx->rdx;
    uint64_t hitNumGuest = ctx->rcx;

    OrbisAppContentAddcontInfo *list = NULL;
    if (listGuest != 0 && list_num > 0) {
        if (!ctx->mem_base || listGuest + (uint64_t)list_num * sizeof(OrbisAppContentAddcontInfo) > ctx->mem_size) {
            ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
            SHIM_RETURN();
        }
        list = (OrbisAppContentAddcontInfo *)(ctx->mem_base + listGuest);
    }
    uint32_t *hit_num = NULL;
    if (hitNumGuest != 0) {
        if (!ctx->mem_base || hitNumGuest + sizeof(uint32_t) > ctx->mem_size) {
            ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
            SHIM_RETURN();
        }
        hit_num = (uint32_t *)(ctx->mem_base + hitNumGuest);
    }

    int32_t ret = sceAppContentGetAddcontInfoList(service_label, list, list_num, hit_num);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentGetEntitlementKey(GuestContext *ctx) {
    uint32_t service_label = (uint32_t)ctx->rdi;
    uint64_t labelGuest = ctx->rsi;
    uint64_t keyGuest = ctx->rdx;
    if (labelGuest == 0 || keyGuest == 0) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    if (!ctx->mem_base || labelGuest + sizeof(OrbisNpUnifiedEntitlementLabel) > ctx->mem_size ||
        keyGuest + sizeof(OrbisAppContentGetEntitlementKey) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    const OrbisNpUnifiedEntitlementLabel *label = (const OrbisNpUnifiedEntitlementLabel *)(ctx->mem_base + labelGuest);
    OrbisAppContentGetEntitlementKey *key = (OrbisAppContentGetEntitlementKey *)(ctx->mem_base + keyGuest);
    int32_t ret = sceAppContentGetEntitlementKey(service_label, label, key);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentAddcontMount(GuestContext *ctx) {
    uint32_t service_label = (uint32_t)ctx->rdi;
    uint64_t labelGuest = ctx->rsi;
    uint64_t mountPointGuest = ctx->rdx;
    if (labelGuest == 0 || mountPointGuest == 0) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    if (!ctx->mem_base || labelGuest + sizeof(OrbisNpUnifiedEntitlementLabel) > ctx->mem_size ||
        mountPointGuest + sizeof(OrbisAppContentMountPoint) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    const OrbisNpUnifiedEntitlementLabel *label = (const OrbisNpUnifiedEntitlementLabel *)(ctx->mem_base + labelGuest);
    OrbisAppContentMountPoint *mount_point = (OrbisAppContentMountPoint *)(ctx->mem_base + mountPointGuest);
    int32_t ret = sceAppContentAddcontMount(service_label, label, mount_point);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentAddcontUnmount(GuestContext *ctx) {
    uint64_t mountPointGuest = ctx->rdi;
    const OrbisAppContentMountPoint *mount_point = NULL;
    if (mountPointGuest != 0 && ctx->mem_base && mountPointGuest + sizeof(OrbisAppContentMountPoint) <= ctx->mem_size) {
        mount_point = (const OrbisAppContentMountPoint *)(ctx->mem_base + mountPointGuest);
    }
    int32_t ret = sceAppContentAddcontUnmount(mount_point);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentAddcontDelete(GuestContext *ctx) {
    uint32_t service_label = (uint32_t)ctx->rdi;
    uint64_t labelGuest = ctx->rsi;
    const OrbisNpUnifiedEntitlementLabel *label = NULL;
    if (labelGuest != 0 && ctx->mem_base && labelGuest + sizeof(OrbisNpUnifiedEntitlementLabel) <= ctx->mem_size) {
        label = (const OrbisNpUnifiedEntitlementLabel *)(ctx->mem_base + labelGuest);
    }
    int32_t ret = sceAppContentAddcontDelete(service_label, label);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentAddcontEnqueueDownload(GuestContext *ctx) {
    uint32_t service_label = (uint32_t)ctx->rdi;
    uint64_t labelGuest = ctx->rsi;
    const OrbisNpUnifiedEntitlementLabel *label = NULL;
    if (labelGuest != 0 && ctx->mem_base && labelGuest + sizeof(OrbisNpUnifiedEntitlementLabel) <= ctx->mem_size) {
        label = (const OrbisNpUnifiedEntitlementLabel *)(ctx->mem_base + labelGuest);
    }
    int32_t ret = sceAppContentAddcontEnqueueDownload(service_label, label);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentAddcontEnqueueDownloadSp(GuestContext *ctx) {
    uint32_t service_label = (uint32_t)ctx->rdi;
    uint64_t labelGuest = ctx->rsi;
    const OrbisNpUnifiedEntitlementLabel *label = NULL;
    if (labelGuest != 0 && ctx->mem_base && labelGuest + sizeof(OrbisNpUnifiedEntitlementLabel) <= ctx->mem_size) {
        label = (const OrbisNpUnifiedEntitlementLabel *)(ctx->mem_base + labelGuest);
    }
    int32_t ret = sceAppContentAddcontEnqueueDownloadSp(service_label, label);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentAddcontShrink(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAppContentAddcontShrink();
    SHIM_RETURN();
}

void shim_sceAppContentDownload0Expand(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAppContentDownload0Expand();
    SHIM_RETURN();
}

void shim_sceAppContentDownload0Shrink(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAppContentDownload0Shrink();
    SHIM_RETURN();
}

void shim_sceAppContentDownload1Expand(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAppContentDownload1Expand();
    SHIM_RETURN();
}

void shim_sceAppContentDownload1Shrink(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAppContentDownload1Shrink();
    SHIM_RETURN();
}

void shim_sceAppContentGetAddcontDownloadProgress(GuestContext *ctx) {
    uint32_t service_label = (uint32_t)ctx->rdi;
    uint64_t labelGuest = ctx->rsi;
    uint64_t progGuest = ctx->rdx;
    const OrbisNpUnifiedEntitlementLabel *label = NULL;
    void *prog = NULL;
    if (labelGuest != 0 && ctx->mem_base && labelGuest + sizeof(OrbisNpUnifiedEntitlementLabel) <= ctx->mem_size) {
        label = (const OrbisNpUnifiedEntitlementLabel *)(ctx->mem_base + labelGuest);
    }
    if (progGuest != 0 && ctx->mem_base && progGuest <= ctx->mem_size) {
        prog = (void *)(ctx->mem_base + progGuest);
    }
    int32_t ret = sceAppContentGetAddcontDownloadProgress(service_label, label, prog);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentGetRegion(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAppContentGetRegion();
    SHIM_RETURN();
}

void shim_sceAppContentRequestPatchInstall(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAppContentRequestPatchInstall();
    SHIM_RETURN();
}

void shim_sceAppContentSmallSharedDataFormat(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAppContentSmallSharedDataFormat();
    SHIM_RETURN();
}

void shim_sceAppContentSmallSharedDataGetAvailableSpaceKb(GuestContext *ctx) {
    uint64_t spaceGuest = ctx->rdi;
    if (spaceGuest == 0 || !ctx->mem_base || spaceGuest + sizeof(uint64_t) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    uint64_t avail = 0;
    int32_t ret = sceAppContentSmallSharedDataGetAvailableSpaceKb(&avail);
    if (ret == 0) {
        *(uint64_t *)(ctx->mem_base + spaceGuest) = avail;
    }
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentSmallSharedDataMount(GuestContext *ctx) {
    uint64_t mountPointGuest = ctx->rdi;
    if (mountPointGuest == 0 || !ctx->mem_base || mountPointGuest + sizeof(OrbisAppContentMountPoint) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    OrbisAppContentMountPoint *mountPoint = (OrbisAppContentMountPoint *)(ctx->mem_base + mountPointGuest);
    int32_t ret = sceAppContentSmallSharedDataMount(mountPoint);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentSmallSharedDataUnmount(GuestContext *ctx) {
    uint64_t mountPointGuest = ctx->rdi;
    const OrbisAppContentMountPoint *mountPoint = NULL;
    if (mountPointGuest != 0 && ctx->mem_base && mountPointGuest + sizeof(OrbisAppContentMountPoint) <= ctx->mem_size) {
        mountPoint = (const OrbisAppContentMountPoint *)(ctx->mem_base + mountPointGuest);
    }
    int32_t ret = sceAppContentSmallSharedDataUnmount(mountPoint);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentGetPftFlag(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAppContentGetPftFlag();
    SHIM_RETURN();
}

void shim_Func_C59A36FF8D7C59DA(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)Func_C59A36FF8D7C59DA();
    SHIM_RETURN();
}

void shim_sceAppContentAddcontEnqueueDownloadByEntitlementId(GuestContext *ctx) {
    uint64_t idGuest = ctx->rdi;
    const char *idStr = NULL;
    if (idGuest != 0 && ctx->mem_base && idGuest < ctx->mem_size) {
        idStr = (const char *)(ctx->mem_base + idGuest);
    }
    int32_t ret = sceAppContentAddcontEnqueueDownloadByEntitlementId(idStr);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentAddcontMountByEntitlementId(GuestContext *ctx) {
    uint64_t idGuest = ctx->rdi;
    uint64_t mountPointGuest = ctx->rsi;
    if (idGuest == 0 || mountPointGuest == 0) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    if (!ctx->mem_base || idGuest >= ctx->mem_size || mountPointGuest + sizeof(OrbisAppContentMountPoint) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    const char *idStr = (const char *)(ctx->mem_base + idGuest);
    OrbisAppContentMountPoint *mountPoint = (OrbisAppContentMountPoint *)(ctx->mem_base + mountPointGuest);
    int32_t ret = sceAppContentAddcontMountByEntitlementId(idStr, mountPoint);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentGetAddcontInfoByEntitlementId(GuestContext *ctx) {
    uint64_t idGuest = ctx->rdi;
    uint64_t infoGuest = ctx->rsi;
    if (idGuest == 0 || infoGuest == 0) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    if (!ctx->mem_base || idGuest >= ctx->mem_size || infoGuest + sizeof(OrbisAppContentAddcontInfo) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    const char *idStr = (const char *)(ctx->mem_base + idGuest);
    OrbisAppContentAddcontInfo *info = (OrbisAppContentAddcontInfo *)(ctx->mem_base + infoGuest);
    int32_t ret = sceAppContentGetAddcontInfoByEntitlementId(idStr, info);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceAppContentGetAddcontInfoListByIroTag(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAppContentGetAddcontInfoListByIroTag();
    SHIM_RETURN();
}

void shim_sceAppContentGetDownloadedStoreCountry(GuestContext *ctx) {
    uint64_t countryGuest = ctx->rdi;
    if (countryGuest == 0 || !ctx->mem_base || countryGuest + 3 > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_APP_CONTENT_ERROR_PARAMETER;
        SHIM_RETURN();
    }
    char *countryCode = (char *)(ctx->mem_base + countryGuest);
    int32_t ret = sceAppContentGetDownloadedStoreCountry(countryCode);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}
