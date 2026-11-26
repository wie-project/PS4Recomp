// SPDX-License-Identifier: GPL-2.0-or-later

// libSceAppContent after shadPS4 src/core/libraries/app_content/app_content.cpp.
//
// Additional content (DLC) is searched per service label at initialization, as in shadPS4:
// label 0 is the game's own service (the service id of CONTENT_ID), labels 1..7 are the services
// SERVICE_ID_ADDCONT_ADD_<n> of param.sfo links. The add-on folders (shadPS4's addon install
// directory) are <app root>/../addcont/<title id>, the title id of a linked service being the
// part of its service id after "UPXXXX-" (UPXXXX-CUSAXXXXX_XX); <app root>/addcont also holds
// the game's own add-ons. Each add-on is an unpacked folder with sce_sys/param.sfo (shadPS4 also
// reads .zar archives, which the runtime has no reader for). License-only entitlements come from
// the license files of each service (libSceNpEntitlementAccess, README "Licenses").

#include "ps4_app_content.h"
#include "ps4_hle_stub.h"
#include "ps4_kernel_sys.h"
#include "ps4_np_entitlement_access.h"
#include "ps4_sfo.h"
#include "ps4_system_service.h"
#include "ps4_vfs.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <removefile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define APP_PATH_LEN 1024
#define SERVICE_ID_LEN 64

// shadPS4 ORBIS_LICENSE_INFO_ENTITLEMENT_KEY_OFFSET and ORBIS_LICENSE_DAT_ENTITLEMENT_KEY_OFFSET
// (offsetof(RIFData, encrypted_secret) + RIF_SECRET_ENTITLEMENT_KEY_OFFSET).
#define LICENSE_INFO_ENTITLEMENT_KEY_OFFSET 0x30
#define LICENSE_DAT_ENTITLEMENT_KEY_OFFSET (0x270 + 0x70)

typedef struct AddContEntry {
    uint32_t service_label;
    char entitlement_label[ORBIS_NP_UNIFIED_ENTITLEMENT_LABEL_SIZE];
    uint32_t status;
    uint8_t key[ORBIS_APP_CONTENT_ENTITLEMENT_KEY_SIZE];
    char host_dir[APP_PATH_LEN]; // empty for a license-only entitlement
} AddContEntry;

static bool s_initialized = false;
static Ps4SfoContext g_app_sfo;
static char g_title_id[32] = {0};
static char g_content_id[64] = {0};

static AddContEntry g_addcont[ORBIS_APP_CONTENT_INFO_LIST_MAX_SIZE];
static uint32_t g_addcont_count = 0;

static int app_content_mkdir_p(const char *path, mode_t mode) {
    char tmp[APP_PATH_LEN];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static bool is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Host backing directory for the per-title download data area (/download0).
// On a real PS4 /download0 is always mounted for the running application
// (it is the writable "download data" partition); games treat it as an
// existing root and only create sub-directories beneath it. Keep it out of
// the (read-only) game directory: $PS4_DOWNLOAD0_DIR, else
// ~/Library/Application Support/PS4Recomp/download0/<TITLE_ID>.
static void ensure_download0_dir(char *out_path, size_t out_sz) {
    const char *env_dir = getenv("PS4_DOWNLOAD0_DIR");
    if (env_dir && *env_dir) {
        snprintf(out_path, out_sz, "%s", env_dir);
    } else {
        const char *home = getenv("HOME");
        const char *data_dir = getenv("PS4RECOMP_DATA_DIR");
        const char *tid = g_title_id[0] ? g_title_id : "CUSA00000";
        if (data_dir && *data_dir) {
            snprintf(out_path, out_sz, "%s/download0/%s", data_dir, tid);
        } else if (home && *home) {
            snprintf(out_path, out_sz, "%s/Library/Application Support/PS4Recomp/download0/%s", home, tid);
        } else {
            snprintf(out_path, out_sz, "/tmp/PS4Recomp/download0/%s", tid);
        }
    }
    if (app_content_mkdir_p(out_path, 0755) != 0) {
        snprintf(out_path, out_sz, "/tmp/ps4_app_download0");
        mkdir(out_path, 0755);
    }
}

// The host directory of /temp0 (shadPS4 TempDataDir/<TITLE_ID>), next to /download0 in the
// user's Application Support folder: the game directory is treated as read-only.
static void temp_dir_path(char *out_path, size_t out_sz) {
    const char *home = getenv("HOME");
    const char *data_dir = getenv("PS4RECOMP_DATA_DIR");
    const char *tid = g_title_id[0] ? g_title_id : "CUSA00000";
    if (data_dir && *data_dir) {
        snprintf(out_path, out_sz, "%s/temp0/%s", data_dir, tid);
    } else if (home && *home) {
        snprintf(out_path, out_sz, "%s/Library/Application Support/PS4Recomp/temp0/%s", home, tid);
    } else {
        snprintf(out_path, out_sz, "/tmp/PS4Recomp/temp0/%s", tid);
    }
}

// Deletes the temporary data directory with its contents and creates it empty.
static void format_temp_dir(const char *temp_dir) {
    removefile_state_t state = removefile_state_alloc();
    if (removefile(temp_dir, state, REMOVEFILE_RECURSIVE) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ps4-appcontent] could not clear %s: %s\n", temp_dir, strerror(errno));
    }
    removefile_state_free(state);
}

// shadPS4 CanAddEntitlement: room in the list and the label not yet listed for the service.
static bool can_add_entitlement(uint32_t service_label, const char *entitlement_id) {
    if (g_addcont_count >= ORBIS_APP_CONTENT_INFO_LIST_MAX_SIZE) {
        fprintf(stderr, "[ps4-appcontent] too many entitlements, ignoring %s\n", entitlement_id);
        return false;
    }
    for (uint32_t i = 0; i < g_addcont_count; i++) {
        if (g_addcont[i].service_label == service_label &&
            strncmp(g_addcont[i].entitlement_label, entitlement_id, ORBIS_NP_UNIFIED_ENTITLEMENT_LABEL_SIZE) == 0) {
            return false;
        }
    }
    return true;
}

static AddContEntry *add_entitlement(uint32_t service_label, const char *entitlement_id, uint32_t status) {
    AddContEntry *e = &g_addcont[g_addcont_count++];
    memset(e, 0, sizeof(*e));
    e->service_label = service_label;
    strncpy(e->entitlement_label, entitlement_id, sizeof(e->entitlement_label) - 1);
    e->status = status;
    return e;
}

// Reads the entitlement key of an add-on folder: sce_sys/license.info, else sce_sys/license.dat.
static bool read_entitlement_key(const char *content_dir, uint8_t *key) {
    static const struct {
        const char *file;
        off_t offset;
    } sources[] = {
        {"sce_sys/license.info", LICENSE_INFO_ENTITLEMENT_KEY_OFFSET},
        {"sce_sys/license.dat", LICENSE_DAT_ENTITLEMENT_KEY_OFFSET},
    };
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        char path[APP_PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", content_dir, sources[i].file);
        FILE *f = fopen(path, "rb");
        if (!f) {
            continue; // shadPS4 falls back to license.dat only when license.info is missing
        }
        const bool ok = fseeko(f, sources[i].offset, SEEK_SET) == 0 &&
                        fread(key, 1, ORBIS_APP_CONTENT_ENTITLEMENT_KEY_SIZE, f) == ORBIS_APP_CONTENT_ENTITLEMENT_KEY_SIZE;
        fclose(f);
        return ok;
    }
    return false;
}

// shadPS4 LoadAddonFolder.
static void load_addon_folder(const char *addon_path, uint32_t service_label) {
    DIR *dir = opendir(addon_path);
    if (!dir) {
        return;
    }
    for (struct dirent *entry; (entry = readdir(dir)) != NULL;) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char content_dir[APP_PATH_LEN];
        snprintf(content_dir, sizeof(content_dir), "%s/%s", addon_path, entry->d_name);
        if (!is_dir(content_dir)) {
            continue;
        }
        char sfo_path[APP_PATH_LEN];
        snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", content_dir);
        Ps4SfoContext dlc_sfo;
        memset(&dlc_sfo, 0, sizeof(dlc_sfo));
        if (!ps4_sfo_load_file(&dlc_sfo, sfo_path)) {
            fprintf(stderr, "[ps4-appcontent] additional content %s has no valid param.sfo\n", entry->d_name);
            continue;
        }
        char category[16];
        char content_id[128];
        const bool has_category = ps4_sfo_get_string(&dlc_sfo, "CATEGORY", category, sizeof(category));
        const bool has_content_id = ps4_sfo_get_string(&dlc_sfo, "CONTENT_ID", content_id, sizeof(content_id));
        ps4_sfo_free(&dlc_sfo);

        if (has_category && strncmp(category, "ac", 2) != 0) {
            fprintf(stderr, "[ps4-appcontent] %s is not additional content\n", entry->d_name);
            continue;
        }
        // Content ids look like UPXXXX-CUSAXXXXX_XX-entitlement.
        if (!has_content_id || strlen(content_id) <= ORBIS_APP_CONTENT_ENTITLEMENT_LABEL_OFFSET) {
            fprintf(stderr, "[ps4-appcontent] %s has a missing or malformed CONTENT_ID\n", entry->d_name);
            continue;
        }
        const char *entitlement_id = content_id + ORBIS_APP_CONTENT_ENTITLEMENT_LABEL_OFFSET;
        if (!can_add_entitlement(service_label, entitlement_id)) {
            continue;
        }
        AddContEntry *e = add_entitlement(service_label, entitlement_id, ORBIS_APP_CONTENT_ADDCONT_DOWNLOAD_STATUS_INSTALLED);
        snprintf(e->host_dir, sizeof(e->host_dir), "%s", content_dir);
        if (!read_entitlement_key(content_dir, e->key)) {
            fprintf(stderr, "[ps4-appcontent] could not load entitlement key for %s\n", entitlement_id);
        }
        fprintf(stderr, "[ps4-appcontent] entitlement %s found for service label %u\n", entitlement_id, service_label);
    }
    closedir(dir);
}

static void add_license_only(const char *label, const uint8_t *key, void *arg) {
    const uint32_t service_label = *(const uint32_t *)arg;
    if (!can_add_entitlement(service_label, label)) {
        return;
    }
    AddContEntry *e = add_entitlement(service_label, label, ORBIS_APP_CONTENT_ADDCONT_DOWNLOAD_STATUS_NO_EXTRA_DATA);
    memcpy(e->key, key, sizeof(e->key));
    fprintf(stderr, "[ps4-appcontent] license-only entitlement %s found for service label %u\n", label, service_label);
}

static void load_title_ids(void) {
    if (ps4_sfo_find_and_load(&g_app_sfo)) {
        ps4_sfo_get_string(&g_app_sfo, "TITLE_ID", g_title_id, sizeof(g_title_id));
        ps4_sfo_get_string(&g_app_sfo, "CONTENT_ID", g_content_id, sizeof(g_content_id));
    }
    // A dump without param.sfo: contentid.txt in the app root.
    if (g_content_id[0] == '\0') {
        char cid_file[APP_PATH_LEN];
        snprintf(cid_file, sizeof(cid_file), "%s/contentid.txt", ps4_vfs_get_app_root());
        FILE *f = fopen(cid_file, "r");
        if (f) {
            if (fgets(g_content_id, sizeof(g_content_id), f)) {
                size_t l = strlen(g_content_id);
                while (l > 0 && (g_content_id[l - 1] == '\r' || g_content_id[l - 1] == '\n')) {
                    g_content_id[--l] = '\0';
                }
                const char *p = strstr(g_content_id, "-CUSA");
                if (p && strlen(p + 1) >= 9 && g_title_id[0] == '\0') {
                    memcpy(g_title_id, p + 1, 9);
                    g_title_id[9] = '\0';
                }
            }
            fclose(f);
        }
    }
    if (g_title_id[0] == '\0') {
        snprintf(g_title_id, sizeof(g_title_id), "CUSA00000");
    }
}

// The add-on folder of a service: <app root>/../addcont/<title id>.
static bool service_addon_path(uint32_t service_label, const char *service_id, char *out, size_t out_sz) {
    const char *app_root = ps4_vfs_get_app_root();
    if (!app_root || !*app_root) {
        return false;
    }
    if (service_label == 0) {
        snprintf(out, out_sz, "%s/../addcont/%s", app_root, g_title_id);
        return true;
    }
    if (strlen(service_id) >= 16) {
        snprintf(out, out_sz, "%s/../addcont/%.9s", app_root, service_id + 7);
        return true;
    }
    return false;
}

static void load_service(uint32_t service_label, const char *service_id) {
    char path[APP_PATH_LEN];
    if (service_addon_path(service_label, service_id, path, sizeof(path)) && is_dir(path)) {
        load_addon_folder(path, service_label);
    }
    if (ps4_rif_license_only_entitlements(service_id, add_license_only, &service_label) > 0) {
        fprintf(stderr, "[ps4-appcontent] checked license %s for additional entitlements\n", service_id);
    }
}

int32_t sceAppContentInitialize(const OrbisAppContentInitParam *initParam, OrbisAppContentBootParam *bootParam,
                                uint32_t sdk_version) {
    (void)initParam;
    (void)bootParam;
    if (sdk_version >= ORBIS_APP_CONTENT_SDK_FW_150 && s_initialized) {
        return ORBIS_APP_CONTENT_ERROR_BUSY;
    }
    s_initialized = true;
    if (g_title_id[0] == '\0') {
        load_title_ids();
    }

    g_addcont_count = 0;
    // The game's own add-ons in <app root>/addcont.
    const char *app_root = ps4_vfs_get_app_root();
    if (app_root && *app_root) {
        char own[APP_PATH_LEN];
        snprintf(own, sizeof(own), "%s/addcont", app_root);
        load_addon_folder(own, 0);
    }
    if (g_content_id[0] != '\0') {
        char service_id[SERVICE_ID_LEN];
        snprintf(service_id, sizeof(service_id), "%s", g_content_id);
        char *dash = strrchr(service_id, '-');
        if (dash) {
            *dash = '\0';
        }
        load_service(0, service_id);
    }
    for (uint32_t label = 1; label <= ORBIS_APP_CONTENT_SERVICE_LABEL_MAX; label++) {
        char key[32], service_id[SERVICE_ID_LEN];
        snprintf(key, sizeof(key), "SERVICE_ID_ADDCONT_ADD_%u", label);
        if (ps4_sfo_get_string(&g_app_sfo, key, service_id, sizeof(service_id)) && service_id[0]) {
            fprintf(stderr, "[ps4-appcontent] linked additional content service %s at slot %u\n", service_id, label);
            load_service(label, service_id);
        }
    }

    if (g_addcont_count > 0) {
        // userId 0 and np_service_label 0, as shadPS4 sends it.
        static OrbisSystemServiceEvent event;
        memset(&event, 0, sizeof(event));
        event.event_type = ORBIS_SYSTEM_SERVICE_EVENT_ENTITLEMENT_UPDATE;
        ps4_system_service_push_event(&event);
    }

    // /download0 is always present for an application (shadPS4 mounts it at boot).
    char dl_dir[APP_PATH_LEN];
    ensure_download0_dir(dl_dir, sizeof(dl_dir));
    ps4_vfs_mount("/download0", dl_dir);
    return 0;
}

/* USER_DEFINED_PARAM_<n> lookup: env override PS4_APPPARAM_USER<n>, then the
 * app's param.sfo, then a small table of known values for titles whose dumps
 * commonly ship a wrong param.sfo (e.g. a DLC's sfo in place of the game's).
 *
 * The Last of Us Remastered (all SKUs CUSA00552..CUSA00560) reads
 * USER_DEFINED_PARAM_1 at boot and formats it as "CUSA%05d" to select its SKU
 * record (region + allowed-language mask). With 0 it falls back to the US SKU
 * CUSA00552, whose mask only allows English/French, so the system language
 * (e.g. Russian on the EU disc CUSA00557) is ignored. */
static int32_t user_defined_param(int32_t paramId) {
    char key[32];
    snprintf(key, sizeof(key), "PS4_APPPARAM_USER%d", paramId);
    const char *env = getenv(key);
    if (env && *env) return (int32_t)strtol(env, NULL, 0);

    int32_t v = 0;
    snprintf(key, sizeof(key), "USER_DEFINED_PARAM_%d", paramId);
    if (ps4_sfo_get_int(&g_app_sfo, key, &v)) return v;

    // Some titles store their own numeric title id in USER_DEFINED_PARAM_1 and pick
    // their regional SKU from it (e.g. formatting it with "CUSA%05d"). Repacks whose
    // param.sfo comes from a DLC lack the key; fall back to the digits of TITLE_ID.
    if (paramId == ORBIS_APP_CONTENT_APPPARAM_ID_USER_DEFINED_PARAM_1 && strlen(g_title_id) == 9 &&
        isalpha((unsigned char)g_title_id[0]) && isdigit((unsigned char)g_title_id[4])) {
        v = (int32_t)strtol(g_title_id + 4, NULL, 10);
        fprintf(stderr, "[ps4-appcontent] USER_DEFINED_PARAM_1 missing from param.sfo; using %d from %s\n", v,
                g_title_id);
        return v;
    }
    return 0;
}

int32_t sceAppContentAppParamGetInt(int32_t paramId, int32_t *value) {
    if (!value) {
        return ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    switch (paramId) {
    case ORBIS_APP_CONTENT_APPPARAM_ID_SKU_FLAG:
        *value = ORBIS_APP_CONTENT_APPPARAM_SKU_FLAG_FULL;
        return 0;
    case ORBIS_APP_CONTENT_APPPARAM_ID_USER_DEFINED_PARAM_1:
    case ORBIS_APP_CONTENT_APPPARAM_ID_USER_DEFINED_PARAM_2:
    case ORBIS_APP_CONTENT_APPPARAM_ID_USER_DEFINED_PARAM_3:
    case ORBIS_APP_CONTENT_APPPARAM_ID_USER_DEFINED_PARAM_4:
        *value = user_defined_param(paramId);
        return 0;
    default:
        return ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
}

int32_t sceAppContentTemporaryDataMount2(uint32_t option, OrbisAppContentMountPoint *mountPoint) {
    if (!s_initialized) {
        return ORBIS_APP_CONTENT_ERROR_NOT_INITIALIZED;
    }
    if (!mountPoint) {
        return ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    char temp_dir[APP_PATH_LEN];
    temp_dir_path(temp_dir, sizeof(temp_dir));
    if (option == ORBIS_APP_CONTENT_TEMPORARY_DATA_OPTION_FORMAT) {
        format_temp_dir(temp_dir);
    }
    if (app_content_mkdir_p(temp_dir, 0755) != 0) {
        fprintf(stderr, "[ps4-appcontent] could not create %s: %s\n", temp_dir, strerror(errno));
    }
    snprintf(mountPoint->data, sizeof(mountPoint->data), "/temp0");
    ps4_vfs_mount("/temp0", temp_dir);
    return 0;
}

// shadPS4: sceAppContentTemporaryDataMount formats the temporary data.
int32_t sceAppContentTemporaryDataMount(OrbisAppContentMountPoint *mountPoint) {
    return sceAppContentTemporaryDataMount2(ORBIS_APP_CONTENT_TEMPORARY_DATA_OPTION_FORMAT, mountPoint);
}

int32_t sceAppContentTemporaryDataUnmount(const OrbisAppContentMountPoint *mountPoint) {
    if (!s_initialized) {
        return ORBIS_APP_CONTENT_ERROR_NOT_INITIALIZED;
    }
    if (!mountPoint) {
        return ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    char mp[ORBIS_APP_CONTENT_MOUNTPOINT_DATA_MAXSIZE + 1];
    snprintf(mp, sizeof(mp), "%.*s", (int)strnlen(mountPoint->data, sizeof(mountPoint->data)), mountPoint->data);
    ps4_vfs_unmount(mp);
    return 0;
}

// shadPS4 stubs this call. Formatting empties the temporary data, as the console does.
int32_t sceAppContentTemporaryDataFormat(const OrbisAppContentMountPoint *mountPoint) {
    (void)mountPoint;
    char temp_dir[APP_PATH_LEN];
    temp_dir_path(temp_dir, sizeof(temp_dir));
    format_temp_dir(temp_dir);
    app_content_mkdir_p(temp_dir, 0755);
    return 0;
}

// The free space of the host volume behind a data area (shadPS4 answers a fixed 1 GiB).
static int32_t available_space_kb(const char *dir, uint64_t *availableSpaceKb) {
    if (!availableSpaceKb) {
        return ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    struct statvfs st;
    if (statvfs(dir, &st) == 0) {
        *availableSpaceKb = ((uint64_t)st.f_bavail * (uint64_t)st.f_frsize) / 1024;
    } else {
        *availableSpaceKb = 1048576; // shadPS4's value
    }
    return 0;
}

int32_t sceAppContentTemporaryDataGetAvailableSpaceKb(const OrbisAppContentMountPoint *mountPoint, uint64_t *availableSpaceKb) {
    (void)mountPoint;
    char temp_dir[APP_PATH_LEN];
    temp_dir_path(temp_dir, sizeof(temp_dir));
    app_content_mkdir_p(temp_dir, 0755);
    return available_space_kb(temp_dir, availableSpaceKb);
}

int32_t sceAppContentDownloadDataGetAvailableSpaceKb(const OrbisAppContentMountPoint *mountPoint, uint64_t *availableSpaceKb) {
    (void)mountPoint;
    char dl_dir[APP_PATH_LEN];
    ensure_download0_dir(dl_dir, sizeof(dl_dir));
    return available_space_kb(dl_dir, availableSpaceKb);
}

// The index of the add-on of service_label with this entitlement label, or -1.
static int32_t find_addcont(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *label) {
    for (uint32_t i = 0; i < g_addcont_count; i++) {
        if (g_addcont[i].service_label == service_label &&
            strncmp(label->data, g_addcont[i].entitlement_label, ORBIS_NP_UNIFIED_ENTITLEMENT_LABEL_SIZE - 1) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t sceAppContentGetAddcontInfo(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlementLabel, OrbisAppContentAddcontInfo *info) {
    if (!s_initialized) {
        return ORBIS_APP_CONTENT_ERROR_NOT_INITIALIZED;
    }
    if (service_label > ORBIS_APP_CONTENT_SERVICE_LABEL_MAX || !entitlementLabel || !info) {
        return ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    const int32_t i = find_addcont(service_label, entitlementLabel);
    if (i < 0) {
        return ORBIS_APP_CONTENT_ERROR_DRM_NO_ENTITLEMENT;
    }
    strncpy(info->entitlement_label.data, g_addcont[i].entitlement_label, ORBIS_NP_UNIFIED_ENTITLEMENT_LABEL_SIZE);
    info->status = g_addcont[i].status;
    return 0;
}

int32_t sceAppContentGetAddcontInfoList(uint32_t service_label, OrbisAppContentAddcontInfo *list, uint32_t list_num, uint32_t *hit_num) {
    if (!s_initialized) {
        return ORBIS_APP_CONTENT_ERROR_NOT_INITIALIZED;
    }
    if (service_label > ORBIS_APP_CONTENT_SERVICE_LABEL_MAX) {
        return ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    uint32_t matching = 0;
    for (uint32_t i = 0; i < g_addcont_count; i++) {
        matching += g_addcont[i].service_label == service_label;
    }
    if (list_num == 0 || !list) {
        if (!hit_num) {
            return ORBIS_APP_CONTENT_ERROR_PARAMETER;
        }
        *hit_num = matching;
        return 0;
    }
    const uint32_t to_list = matching < list_num ? matching : list_num;
    uint32_t written = 0;
    for (uint32_t i = 0; i < g_addcont_count && written < to_list; i++) {
        if (g_addcont[i].service_label == service_label) {
            strncpy(list[written].entitlement_label.data, g_addcont[i].entitlement_label,
                    ORBIS_NP_UNIFIED_ENTITLEMENT_LABEL_SIZE);
            list[written].status = g_addcont[i].status;
            written++;
        }
    }
    if (hit_num) {
        *hit_num = to_list;
    }
    return 0;
}

int32_t sceAppContentGetEntitlementKey(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label, OrbisAppContentGetEntitlementKey *key) {
    if (!s_initialized) {
        return ORBIS_APP_CONTENT_ERROR_NOT_INITIALIZED;
    }
    if (service_label > ORBIS_APP_CONTENT_SERVICE_LABEL_MAX || !entitlement_label || !key) {
        return ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    const int32_t i = find_addcont(service_label, entitlement_label);
    if (i < 0) {
        return ORBIS_APP_CONTENT_ERROR_DRM_NO_ENTITLEMENT;
    }
    memcpy(key->data, g_addcont[i].key, sizeof(key->data));
    return 0;
}

int32_t sceAppContentAddcontMount(uint32_t service_label, const OrbisNpUnifiedEntitlementLabel *entitlement_label, OrbisAppContentMountPoint *mount_point) {
    if (!s_initialized) {
        return ORBIS_APP_CONTENT_ERROR_NOT_INITIALIZED;
    }
    if (service_label > ORBIS_APP_CONTENT_SERVICE_LABEL_MAX || !entitlement_label || !mount_point) {
        return ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    const int32_t i = find_addcont(service_label, entitlement_label);
    if (i < 0) {
        return ORBIS_APP_CONTENT_ERROR_NOT_FOUND;
    }
    snprintf(mount_point->data, sizeof(mount_point->data), "/addcont%d", i);
    if (g_addcont[i].host_dir[0] == '\0') {
        // A license-only entitlement has no folder to mount; shadPS4 stops here
        // (UNREACHABLE_MSG "Folder for loaded entitlement label ... doesn't exist").
        fprintf(stderr, "[ps4-appcontent] entitlement %s has no content folder to mount\n",
                g_addcont[i].entitlement_label);
        return ORBIS_APP_CONTENT_ERROR_NOT_FOUND;
    }
    ps4_vfs_mount(mount_point->data, g_addcont[i].host_dir);
    return 0;
}

int32_t sceAppContentAddcontUnmount(const OrbisAppContentMountPoint *mount_point) {
    if (!s_initialized) {
        return ORBIS_APP_CONTENT_ERROR_NOT_INITIALIZED;
    }
    if (!mount_point) {
        return ORBIS_APP_CONTENT_ERROR_PARAMETER;
    }
    char mp[ORBIS_APP_CONTENT_MOUNTPOINT_DATA_MAXSIZE + 1];
    snprintf(mp, sizeof(mp), "%.*s", (int)strnlen(mount_point->data, sizeof(mount_point->data)), mount_point->data);
    ps4_vfs_unmount(mp);
    return 0;
}

// Guest ABI shims

static void *guest_ptr(const GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

#define RET(v)                                  \
    do {                                        \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(v); \
        SHIM_RETURN();                          \
    } while (0)

void shim_sceAppContentInitialize(GuestContext *ctx) {
    RET(sceAppContentInitialize(guest_ptr(ctx, ctx->rdi), guest_ptr(ctx, ctx->rsi), ps4_compiled_sdk_version(ctx)));
}

void shim_sceAppContentAppParamGetInt(GuestContext *ctx) {
    RET(sceAppContentAppParamGetInt((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi)));
}

void shim_sceAppContentTemporaryDataMount(GuestContext *ctx) {
    RET(sceAppContentTemporaryDataMount(guest_ptr(ctx, ctx->rdi)));
}

void shim_sceAppContentTemporaryDataMount2(GuestContext *ctx) {
    RET(sceAppContentTemporaryDataMount2((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi)));
}

void shim_sceAppContentTemporaryDataUnmount(GuestContext *ctx) {
    RET(sceAppContentTemporaryDataUnmount(guest_ptr(ctx, ctx->rdi)));
}

void shim_sceAppContentTemporaryDataFormat(GuestContext *ctx) {
    RET(sceAppContentTemporaryDataFormat(guest_ptr(ctx, ctx->rdi)));
}

void shim_sceAppContentTemporaryDataGetAvailableSpaceKb(GuestContext *ctx) {
    RET(sceAppContentTemporaryDataGetAvailableSpaceKb(guest_ptr(ctx, ctx->rdi), guest_ptr(ctx, ctx->rsi)));
}

void shim_sceAppContentDownloadDataGetAvailableSpaceKb(GuestContext *ctx) {
    RET(sceAppContentDownloadDataGetAvailableSpaceKb(guest_ptr(ctx, ctx->rdi), guest_ptr(ctx, ctx->rsi)));
}

void shim_sceAppContentGetAddcontInfo(GuestContext *ctx) {
    RET(sceAppContentGetAddcontInfo((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), guest_ptr(ctx, ctx->rdx)));
}

void shim_sceAppContentGetAddcontInfoList(GuestContext *ctx) {
    RET(sceAppContentGetAddcontInfoList((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), (uint32_t)ctx->rdx,
                                        guest_ptr(ctx, ctx->rcx)));
}

void shim_sceAppContentGetEntitlementKey(GuestContext *ctx) {
    RET(sceAppContentGetEntitlementKey((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), guest_ptr(ctx, ctx->rdx)));
}

void shim_sceAppContentAddcontMount(GuestContext *ctx) {
    RET(sceAppContentAddcontMount((uint32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi), guest_ptr(ctx, ctx->rdx)));
}

void shim_sceAppContentAddcontUnmount(GuestContext *ctx) {
    RET(sceAppContentAddcontUnmount(guest_ptr(ctx, ctx->rdi)));
}

// Functions shadPS4 stubs as well (app_content.cpp). _Z5dummyv is shared with libSceScreenShot.
PS4_HLE_STUB(sceAppContentAddcontDelete)
PS4_HLE_STUB(sceAppContentAddcontEnqueueDownload)
PS4_HLE_STUB(sceAppContentAddcontEnqueueDownloadSp)
PS4_HLE_STUB(sceAppContentAddcontShrink)
PS4_HLE_STUB(sceAppContentAppParamGetString)
PS4_HLE_STUB(sceAppContentDownload0Expand)
PS4_HLE_STUB(sceAppContentDownload0Shrink)
PS4_HLE_STUB(sceAppContentDownload1Expand)
PS4_HLE_STUB(sceAppContentDownload1Shrink)
PS4_HLE_STUB(sceAppContentDownloadDataFormat)
PS4_HLE_STUB(sceAppContentGetAddcontDownloadProgress)
PS4_HLE_STUB(sceAppContentGetRegion)
PS4_HLE_STUB(sceAppContentRequestPatchInstall)
PS4_HLE_STUB(sceAppContentSmallSharedDataFormat)
PS4_HLE_STUB(sceAppContentSmallSharedDataGetAvailableSpaceKb)
PS4_HLE_STUB(sceAppContentSmallSharedDataMount)
PS4_HLE_STUB(sceAppContentSmallSharedDataUnmount)
PS4_HLE_STUB(sceAppContentGetPftFlag)
PS4_HLE_STUB(Func_C59A36FF8D7C59DA)
PS4_HLE_STUB(sceAppContentAddcontEnqueueDownloadByEntitlementId)
PS4_HLE_STUB(sceAppContentAddcontMountByEntitlementId)
PS4_HLE_STUB(sceAppContentGetAddcontInfoByEntitlementId)
PS4_HLE_STUB(sceAppContentGetAddcontInfoListByIroTag)
PS4_HLE_STUB(sceAppContentGetDownloadedStoreCountry)
