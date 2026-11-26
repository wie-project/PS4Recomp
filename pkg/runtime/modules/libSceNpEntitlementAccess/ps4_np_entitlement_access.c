// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpEntitlementAccess after shadPS4 (np/np_entitlement_access.cpp). Entitlements come from
// the license files the player copied from a console into the licenses directory: the SKU flag
// and the add-on content entitlement keys are read from the RIFs there. Consuming entitlements
// and the asynchronous info queries need the PlayStation Network; as in shadPS4 their requests
// only get an id and every poll reports status 0 with no data.
//
// Candidate license files for the running title (shadPS4 GetCandidateServiceIds): the service
// id of CONTENT_ID (the part before its last '-'), CONTENT_ID itself, then every <name>.idx in
// the licenses directory whose name contains TITLE_ID.

#include "ps4_np_entitlement_access.h"
#include "ps4_sfo.h"
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NAME_MAX_LEN 256
#define PATH_LEN 1024

static atomic_bool g_init;
static atomic_uint g_next_request_id = 1;

// param.sfo values the queries use, read once.
static pthread_once_t g_sfo_once = PTHREAD_ONCE_INIT;
static char g_content_id[64];
static char g_title_id[16];
static char g_category[8];
static int32_t g_app_type;

static void load_sfo(void) {
    Ps4SfoContext sfo;
    memset(&sfo, 0, sizeof(sfo));
    if (!ps4_sfo_find_and_load(&sfo)) return;
    if (!ps4_sfo_get_string(&sfo, "CONTENT_ID", g_content_id, sizeof(g_content_id))) g_content_id[0] = '\0';
    if (!ps4_sfo_get_string(&sfo, "TITLE_ID", g_title_id, sizeof(g_title_id))) g_title_id[0] = '\0';
    if (!ps4_sfo_get_string(&sfo, "CATEGORY", g_category, sizeof(g_category))) g_category[0] = '\0';
    if (!ps4_sfo_get_int(&sfo, "APP_TYPE", &g_app_type)) g_app_type = 0;
    ps4_sfo_free(&sfo);
}

void ps4_np_entitlement_licenses_dir(char *out, size_t out_size) {
    const char *data_dir = getenv("PS4RECOMP_DATA_DIR");
    const char *home = getenv("HOME");
    if (data_dir && *data_dir) {
        snprintf(out, out_size, "%s/licenses", data_dir);
    } else if (home && *home) {
        snprintf(out, out_size, "%s/Library/Application Support/PS4Recomp/licenses", home);
    } else {
        snprintf(out, out_size, "/tmp/PS4Recomp/licenses");
    }
}

static bool is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

static bool read_at(int fd, void *buf, size_t size, uint64_t offset) {
    return pread(fd, buf, size, (off_t)offset) == (ssize_t)size;
}

// The SKU flag field holds a little-endian u16; some license dumps store it big endian. A
// byte-swapped value below 0x100 is taken as the real flag (shadPS4 RIF::GetSkuFlag).
static uint16_t sku_flag_of(const uint8_t *rif) {
    const uint16_t le = le16(rif + RIF_SKU_FLAG_OFFSET);
    const uint16_t be = (uint16_t)(le >> 8 | le << 8);
    return (be != 0 && be < 0x100) ? be : le;
}

// An opened RIDX index with its RIFA container (shadPS4 RIF::Open).
typedef struct RifContainer {
    int ridx;
    int rifa;
    uint32_t entry_count;
} RifContainer;

static void rif_close(RifContainer *c) {
    if (c->ridx >= 0) close(c->ridx);
    if (c->rifa >= 0) close(c->rifa);
}

static bool rif_open(RifContainer *c, const char *idx_path, const char *rif_path) {
    c->ridx = open(idx_path, O_RDONLY);
    c->rifa = open(rif_path, O_RDONLY);
    uint8_t ridx_header[RIDX_HEADER_SIZE];
    uint8_t rifa_magic[4];
    if (c->ridx < 0 || c->rifa < 0 || !read_at(c->ridx, ridx_header, sizeof(ridx_header), 0) ||
        be32(ridx_header) != RIDX_MAGIC || ridx_header[8] != RIDX_VERSION ||
        !read_at(c->rifa, rifa_magic, sizeof(rifa_magic), 0) || be32(rifa_magic) != RIFA_MAGIC) {
        rif_close(c);
        return false;
    }
    // Entries past the end of the index file are not read (shadPS4 would read past its buffer).
    struct stat st;
    const uint64_t in_file = fstat(c->ridx, &st) == 0 && st.st_size > RIDX_HEADER_SIZE
                                 ? (uint64_t)(st.st_size - RIDX_HEADER_SIZE) / RIDX_ENTRY_SIZE
                                 : 0;
    const uint32_t declared = le32(ridx_header + 4);
    c->entry_count = declared < in_file ? declared : (uint32_t)in_file;
    return true;
}

// Reads entry i: its entitlement label and its RIF. False for an entry shadPS4 skips (a RIF
// size other than 0x400, a RIF outside the container or without the RIF magic) and past the
// end of the index.
static bool rif_entry(const RifContainer *c, uint32_t i, char label[17], uint8_t rif[RIF_SIZE]) {
    uint8_t e[RIDX_ENTRY_SIZE];
    if (!read_at(c->ridx, e, sizeof(e), RIDX_HEADER_SIZE + (uint64_t)i * RIDX_ENTRY_SIZE)) return false;
    if (le64(e + 0x18) != RIF_SIZE || !read_at(c->rifa, rif, RIF_SIZE, le64(e + 0x10)) || be32(rif) != RIF_MAGIC) {
        return false;
    }
    memcpy(label, e, 16);
    label[16] = '\0';
    return true;
}

// shadPS4 keeps the entitlements in a map keyed by label: of entries sharing a label, the first
// valid one counts.
static bool rif_label_taken(const RifContainer *c, uint32_t i, const char *label) {
    char other[17];
    uint8_t rif[RIF_SIZE];
    for (uint32_t j = 0; j < i; j++) {
        if (rif_entry(c, j, other, rif) && strcmp(other, label) == 0) return true;
    }
    return false;
}

static bool rif_sku_flag(const RifContainer *c, uint32_t *flag) {
    char label[17];
    uint8_t rif[RIF_SIZE];
    for (uint32_t i = 0; i < c->entry_count; i++) {
        if (!rif_entry(c, i, label, rif) || rif_label_taken(c, i, label)) continue;
        const uint16_t type = le16(rif + RIF_CONTENT_TYPE_OFFSET);
        if (type != 0x1A00 && type != 0x001A) continue;
        const uint16_t f = sku_flag_of(rif);
        if (f != 0) {
            *flag = f;
            return true;
        }
    }
    return false;
}

static bool rif_entitlement_key(const RifContainer *c, const char *wanted, uint8_t *key) {
    char label[17];
    uint8_t rif[RIF_SIZE];
    for (uint32_t i = 0; i < c->entry_count; i++) {
        if (rif_entry(c, i, label, rif) && strcmp(label, wanted) == 0) {
            memcpy(key, rif + RIF_ENTITLEMENT_KEY_OFFSET, NP_ENTITLEMENT_KEY_SIZE);
            return true;
        }
    }
    return false;
}

// A query over the license files of one service id; true stops the search.
typedef bool (*ServiceVisitor)(const char *dir, const char *service_id, void *arg);

// Splits "<name>.<ext>" like std::filesystem::path stem/extension.
static bool has_idx_extension(const char *name, char stem[NAME_MAX_LEN]) {
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name || strcmp(dot, ".idx") != 0) return false;
    const size_t n = (size_t)(dot - name);
    if (n >= NAME_MAX_LEN) return false;
    memcpy(stem, name, n);
    stem[n] = '\0';
    return true;
}

static bool visit_candidates(const char *dir, ServiceVisitor visit, void *arg) {
    pthread_once(&g_sfo_once, load_sfo);
    if (g_content_id[0]) {
        const char *dash = strrchr(g_content_id, '-');
        if (dash) {
            char service[sizeof(g_content_id)];
            const size_t n = (size_t)(dash - g_content_id);
            memcpy(service, g_content_id, n);
            service[n] = '\0';
            if (visit(dir, service, arg)) return true;
        }
        if (visit(dir, g_content_id, arg)) return true;
    }
    if (!g_title_id[0]) return false;
    DIR *d = opendir(dir);
    if (!d) return false;
    bool found = false;
    char stem[NAME_MAX_LEN];
    for (struct dirent *e; !found && (e = readdir(d)) != NULL;) {
        if (strstr(e->d_name, g_title_id) && has_idx_extension(e->d_name, stem)) found = visit(dir, stem, arg);
    }
    closedir(d);
    return found;
}

static bool sku_from_service(const char *dir, const char *service_id, void *arg) {
    uint32_t *flag = arg;
    char idx_path[PATH_LEN], rif_path[PATH_LEN];
    snprintf(idx_path, sizeof(idx_path), "%s/%s.idx", dir, service_id);
    snprintf(rif_path, sizeof(rif_path), "%s/%s.rif", dir, service_id);
    if (exists(idx_path) && exists(rif_path)) {
        RifContainer c;
        if (rif_open(&c, idx_path, rif_path)) {
            const bool found = rif_sku_flag(&c, flag);
            rif_close(&c);
            if (found) {
                fprintf(stderr, "[ps4-np-entitlement] SKU flag 0x%x from license container %s\n", *flag, service_id);
                return true;
            }
        }
    }
    // A lone RIF.
    const int fd = open(rif_path, O_RDONLY);
    if (fd < 0) return false;
    uint8_t rif[RIF_SIZE];
    const bool ok = read_at(fd, rif, sizeof(rif), 0) && be32(rif) == RIF_MAGIC;
    close(fd);
    if (!ok) return false;
    const uint16_t f = sku_flag_of(rif);
    if (f == 0) return false;
    *flag = f;
    fprintf(stderr, "[ps4-np-entitlement] SKU flag 0x%x from single RIF %s\n", f, service_id);
    return true;
}

// shadPS4 QueryLicenseSkuFlag: a game application (APP_TYPE 2 or CATEGORY "gd") has flag 0.
static bool query_sku_flag(uint32_t *flag) {
    pthread_once(&g_sfo_once, load_sfo);
    if (g_app_type == 2 || strcmp(g_category, "gd") == 0) {
        *flag = 0;
        return true;
    }
    char dir[PATH_LEN];
    ps4_np_entitlement_licenses_dir(dir, sizeof(dir));
    return is_dir(dir) && visit_candidates(dir, sku_from_service, flag);
}

typedef struct KeyQuery {
    const char *label;
    uint8_t *key;
} KeyQuery;

static bool key_from_container(const char *idx_path, const char *rif_path, const KeyQuery *q) {
    RifContainer c;
    if (!rif_open(&c, idx_path, rif_path)) return false;
    const bool found = rif_entitlement_key(&c, q->label, q->key);
    rif_close(&c);
    return found;
}

static bool key_from_service(const char *dir, const char *service_id, void *arg) {
    char idx_path[PATH_LEN], rif_path[PATH_LEN];
    snprintf(idx_path, sizeof(idx_path), "%s/%s.idx", dir, service_id);
    snprintf(rif_path, sizeof(rif_path), "%s/%s.rif", dir, service_id);
    return exists(idx_path) && exists(rif_path) && key_from_container(idx_path, rif_path, arg);
}

// shadPS4 QueryLicenseEntitlementKey: the candidates first, then every container in the
// licenses directory.
static bool query_entitlement_key(const char *label, uint8_t *key) {
    char dir[PATH_LEN];
    ps4_np_entitlement_licenses_dir(dir, sizeof(dir));
    if (!is_dir(dir)) return false;
    KeyQuery q = {label, key};
    if (visit_candidates(dir, key_from_service, &q)) return true;
    DIR *d = opendir(dir);
    if (!d) return false;
    bool found = false;
    char stem[NAME_MAX_LEN];
    for (struct dirent *e; !found && (e = readdir(d)) != NULL;) {
        if (has_idx_extension(e->d_name, stem)) found = key_from_service(dir, stem, &q);
    }
    closedir(d);
    if (found) fprintf(stderr, "[ps4-np-entitlement] entitlement key for %s found\n", label);
    return found;
}

int32_t sceNpEntitlementAccessInitialize(const void *param, const void *reserved) {
    (void)param;
    (void)reserved;
    return atomic_exchange(&g_init, true) ? ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_ALREADY_INITIALIZED : 0;
}

int32_t sceNpEntitlementAccessGetSkuFlag(uint32_t *flag) {
    if (!atomic_load(&g_init)) return ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED;
    if (!flag) return ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT;
    uint32_t f;
    if (!query_sku_flag(&f)) {
        fprintf(stderr, "[ps4-np-entitlement] no license found for the SKU flag\n");
        return ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_FOUND;
    }
    *flag = f;
    return 0;
}

int32_t sceNpEntitlementAccessGetAddcontEntitlementInfoList(int32_t user_id, const char *service_label,
                                                            uint32_t limit, void *out_list) {
    (void)user_id;
    (void)service_label;
    (void)limit;
    if (!atomic_load(&g_init)) return ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED;
    return out_list ? 0 : ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT;
}

int32_t sceNpEntitlementAccessGetAddcontEntitlementInfo(int32_t user_id, const char *label, void *out_info) {
    (void)user_id;
    if (!atomic_load(&g_init)) return ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED;
    if (!label || !out_info) return ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT;
    uint8_t key[NP_ENTITLEMENT_KEY_SIZE];
    if (!query_entitlement_key(label, key)) {
        fprintf(stderr, "[ps4-np-entitlement] add-on content entitlement %s not found\n", label);
        return ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_FOUND;
    }
    memset(out_info, 0, NP_ENTITLEMENT_ADDCONT_INFO_SIZE);
    return 0;
}

int32_t sceNpEntitlementAccessGetEntitlementKey(int32_t user_id, const char *label, void *out_key) {
    (void)user_id;
    if (!atomic_load(&g_init)) return ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED;
    if (!label || !out_key) return ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT;
    if (query_entitlement_key(label, out_key)) return 0;
    fprintf(stderr, "[ps4-np-entitlement] entitlement %s not found in licenses\n", label);
    return ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_FOUND;
}

// A random UUID version 4.
int32_t sceNpEntitlementAccessGenerateTransactionId(char *out) {
    if (!atomic_load(&g_init)) return ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED;
    if (!out) return ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT;
    uint64_t part[2];
    arc4random_buf(part, sizeof(part));
    snprintf(out, NP_ENTITLEMENT_TRANSACTION_ID_SIZE, "%08x-%04x-4%03x-%04x-%012llx", (uint32_t)(part[0] >> 32),
             (uint32_t)((part[0] >> 16) & 0xFFFF), (uint32_t)(part[0] & 0x0FFF),
             (uint32_t)(((part[1] >> 48) & 0x3FFF) | 0x8000), (unsigned long long)(part[1] & 0xFFFFFFFFFFFFull));
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Shims

static void *gptr(const GuestContext *ctx, uint64_t addr) {
    return addr ? ctx->mem_base + addr : NULL;
}

static void ea_return(GuestContext *ctx, int32_t rc) {
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

// A Request* call: the request id goes to *request_id when the required pointers are given.
static void request(GuestContext *ctx, bool args_ok, uint64_t request_id) {
    if (!atomic_load(&g_init)) {
        ea_return(ctx, ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED);
        return;
    }
    if (!args_ok || !request_id) {
        ea_return(ctx, ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT);
        return;
    }
    *(uint32_t *)gptr(ctx, request_id) = atomic_fetch_add(&g_next_request_id, 1);
    ea_return(ctx, 0);
}

static void store32(const GuestContext *ctx, uint64_t addr, uint32_t v) {
    if (addr) *(uint32_t *)gptr(ctx, addr) = v;
}

// A Poll*Info call (request_id, int *out_status, void *out_info): status 0, info cleared.
static void poll_info(GuestContext *ctx) {
    if (!atomic_load(&g_init)) {
        ea_return(ctx, ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED);
        return;
    }
    store32(ctx, ctx->rsi, 0);
    if (ctx->rdx) memset(gptr(ctx, ctx->rdx), 0, NP_ENTITLEMENT_POLL_INFO_SIZE);
    ea_return(ctx, 0);
}

// A Poll*InfoList call (request_id, int *out_status, void *out_list, u32 limit, u32 *out_count,
// u32 *out_total_count, const void *opt): status 0, no entries.
static void poll_list(GuestContext *ctx) {
    if (!atomic_load(&g_init)) {
        ea_return(ctx, ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED);
        return;
    }
    store32(ctx, ctx->rsi, 0);
    store32(ctx, ctx->r8, 0);
    store32(ctx, ctx->r9, 0);
    ea_return(ctx, 0);
}

// DeleteRequest / AbortRequest: no request is ever in flight.
static void request_done(GuestContext *ctx) {
    ea_return(ctx, atomic_load(&g_init) ? 0 : ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED);
}

void shim_sceNpEntitlementAccessInitialize(GuestContext *ctx) {
    ea_return(ctx, sceNpEntitlementAccessInitialize(gptr(ctx, ctx->rdi), gptr(ctx, ctx->rsi)));
}

void shim_sceNpEntitlementAccessGetSkuFlag(GuestContext *ctx) {
    ea_return(ctx, sceNpEntitlementAccessGetSkuFlag(gptr(ctx, ctx->rdi)));
}

void shim_sceNpEntitlementAccessGetAddcontEntitlementInfoList(GuestContext *ctx) {
    ea_return(ctx, sceNpEntitlementAccessGetAddcontEntitlementInfoList((int32_t)ctx->rdi, gptr(ctx, ctx->rsi),
                                                                       (uint32_t)ctx->rdx, gptr(ctx, ctx->rcx)));
}

void shim_sceNpEntitlementAccessGetAddcontEntitlementInfo(GuestContext *ctx) {
    ea_return(ctx, sceNpEntitlementAccessGetAddcontEntitlementInfo((int32_t)ctx->rdi, gptr(ctx, ctx->rsi),
                                                                   gptr(ctx, ctx->rdx)));
}

void shim_sceNpEntitlementAccessGetEntitlementKey(GuestContext *ctx) {
    ea_return(ctx,
              sceNpEntitlementAccessGetEntitlementKey((int32_t)ctx->rdi, gptr(ctx, ctx->rsi), gptr(ctx, ctx->rdx)));
}

void shim_sceNpEntitlementAccessGenerateTransactionId(GuestContext *ctx) {
    ea_return(ctx, sceNpEntitlementAccessGenerateTransactionId(gptr(ctx, ctx->rdi)));
}

// (user_id, type, label, transaction_id, use_count, u32 *request_id)
void shim_sceNpEntitlementAccessRequestConsumeEntitlement(GuestContext *ctx) {
    request(ctx, ctx->rdx && ctx->rcx, ctx->r9);
}

void shim_sceNpEntitlementAccessRequestConsumeUnifiedEntitlement(GuestContext *ctx) {
    request(ctx, ctx->rdx && ctx->rcx, ctx->r9);
}

// (user_id, type, service_label, transaction_id, use_count, u32 *request_id)
void shim_sceNpEntitlementAccessRequestConsumeServiceEntitlement(GuestContext *ctx) {
    request(ctx, ctx->rdx && ctx->rcx, ctx->r9);
}

// (request_id, int *out_status, u32 *out_remaining_count)
void shim_sceNpEntitlementAccessPollConsumeEntitlement(GuestContext *ctx) {
    if (!atomic_load(&g_init)) {
        ea_return(ctx, ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED);
        return;
    }
    store32(ctx, ctx->rsi, 0);
    store32(ctx, ctx->rdx, 0);
    ea_return(ctx, 0);
}

// (user_id, type, label, u32 *request_id)
void shim_sceNpEntitlementAccessRequestConsumableEntitlementInfo(GuestContext *ctx) {
    request(ctx, ctx->rdx != 0, ctx->rcx);
}

void shim_sceNpEntitlementAccessPollConsumableEntitlementInfo(GuestContext *ctx) {
    poll_info(ctx);
}

void shim_sceNpEntitlementAccessRequestUnifiedEntitlementInfo(GuestContext *ctx) {
    request(ctx, ctx->rdx != 0, ctx->rcx);
}

void shim_sceNpEntitlementAccessPollUnifiedEntitlementInfo(GuestContext *ctx) {
    poll_info(ctx);
}

// (user_id, type, const char **labels, u32 num_labels, const void *opt, u32 *request_id)
void shim_sceNpEntitlementAccessRequestUnifiedEntitlementInfoList(GuestContext *ctx) {
    request(ctx, true, ctx->r9);
}

void shim_sceNpEntitlementAccessPollUnifiedEntitlementInfoList(GuestContext *ctx) {
    poll_list(ctx);
}

// (user_id, type, service_label, u32 *request_id)
void shim_sceNpEntitlementAccessRequestServiceEntitlementInfo(GuestContext *ctx) {
    request(ctx, ctx->rdx != 0, ctx->rcx);
}

void shim_sceNpEntitlementAccessPollServiceEntitlementInfo(GuestContext *ctx) {
    poll_info(ctx);
}

void shim_sceNpEntitlementAccessRequestServiceEntitlementInfoList(GuestContext *ctx) {
    request(ctx, true, ctx->r9);
}

void shim_sceNpEntitlementAccessPollServiceEntitlementInfoList(GuestContext *ctx) {
    poll_list(ctx);
}

void shim_sceNpEntitlementAccessDeleteRequest(GuestContext *ctx) {
    request_done(ctx);
}

void shim_sceNpEntitlementAccessAbortRequest(GuestContext *ctx) {
    request_done(ctx);
}
