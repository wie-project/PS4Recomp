#include "ps4_trophy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "ps4_vfs.h"

#define MAX_TROPHY_CONTEXTS 16
#define MAX_TROPHY_HANDLES 16

typedef struct {
    int in_use;
    int32_t id;
    int32_t user_id;
    int32_t registered_handle;
    uint32_t service_label; // selects the trophy pack, trophyNN.trp
    uint32_t unlocked_trophies[8]; // bitmask for up to 256 trophies
} TrophyContext;

typedef struct {
    int in_use;
    int32_t id;
} TrophyHandle;

static TrophyContext g_contexts[MAX_TROPHY_CONTEXTS];
static TrophyHandle g_handles[MAX_TROPHY_HANDLES];
static pthread_mutex_t g_trophy_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_next_context_id = 1;
static int32_t g_next_handle_id = 1;
static int g_trophy_inited = 0;

int32_t sceNpTrophyInit(void *params) {
    (void)params;
    pthread_mutex_lock(&g_trophy_mutex);
    if (!g_trophy_inited) {
        memset(g_contexts, 0, sizeof(g_contexts));
        memset(g_handles, 0, sizeof(g_handles));
        g_trophy_inited = 1;
    }
    pthread_mutex_unlock(&g_trophy_mutex);
    return 0;
}

int32_t sceNpTrophyTerm(void) {
    pthread_mutex_lock(&g_trophy_mutex);
    g_trophy_inited = 0;
    pthread_mutex_unlock(&g_trophy_mutex);
    return 0;
}

int32_t sceNpTrophyCreateContext(int32_t *context, int32_t user, uint32_t unk, uint64_t unk2) {
    // unk is the NP service label (shadPS4 sceNpTrophyCreateContext); unk2 the options.
    (void)unk2;
    if (!context) {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_trophy_mutex);
    if (!g_trophy_inited) {
        g_trophy_inited = 1;
        memset(g_contexts, 0, sizeof(g_contexts));
        memset(g_handles, 0, sizeof(g_handles));
    }

    TrophyContext *slot = NULL;
    for (int i = 0; i < MAX_TROPHY_CONTEXTS; i++) {
        if (!g_contexts[i].in_use) {
            slot = &g_contexts[i];
            break;
        }
    }

    if (!slot) {
        pthread_mutex_unlock(&g_trophy_mutex);
        return -ENOMEM;
    }

    slot->in_use = 1;
    slot->id = g_next_context_id++;
    slot->user_id = user;
    slot->registered_handle = 0;
    slot->service_label = unk;
    memset(slot->unlocked_trophies, 0, sizeof(slot->unlocked_trophies));

    *context = slot->id;
    pthread_mutex_unlock(&g_trophy_mutex);
    return 0;
}

int32_t sceNpTrophyDestroyContext(int32_t context) {
    pthread_mutex_lock(&g_trophy_mutex);
    for (int i = 0; i < MAX_TROPHY_CONTEXTS; i++) {
        if (g_contexts[i].in_use && g_contexts[i].id == context) {
            g_contexts[i].in_use = 0;
            pthread_mutex_unlock(&g_trophy_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_trophy_mutex);
    return -EINVAL;
}

int32_t sceNpTrophyCreateHandle(int32_t *handle) {
    if (!handle) {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_trophy_mutex);
    if (!g_trophy_inited) {
        g_trophy_inited = 1;
        memset(g_contexts, 0, sizeof(g_contexts));
        memset(g_handles, 0, sizeof(g_handles));
    }

    TrophyHandle *slot = NULL;
    for (int i = 0; i < MAX_TROPHY_HANDLES; i++) {
        if (!g_handles[i].in_use) {
            slot = &g_handles[i];
            break;
        }
    }

    if (!slot) {
        pthread_mutex_unlock(&g_trophy_mutex);
        return -ENOMEM;
    }

    slot->in_use = 1;
    slot->id = g_next_handle_id++;

    *handle = slot->id;
    pthread_mutex_unlock(&g_trophy_mutex);
    return 0;
}

int32_t sceNpTrophyDestroyHandle(int32_t handle) {
    pthread_mutex_lock(&g_trophy_mutex);
    for (int i = 0; i < MAX_TROPHY_HANDLES; i++) {
        if (g_handles[i].in_use && g_handles[i].id == handle) {
            g_handles[i].in_use = 0;
            pthread_mutex_unlock(&g_trophy_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_trophy_mutex);
    return -EINVAL;
}

int32_t sceNpTrophyRegisterContext(int32_t context, int32_t handle, uint64_t unk) {
    (void)unk;
    pthread_mutex_lock(&g_trophy_mutex);

    int context_found = 0;
    int handle_found = 0;
    TrophyContext *ctx_ptr = NULL;

    for (int i = 0; i < MAX_TROPHY_CONTEXTS; i++) {
        if (g_contexts[i].in_use && g_contexts[i].id == context) {
            context_found = 1;
            ctx_ptr = &g_contexts[i];
            break;
        }
    }

    for (int i = 0; i < MAX_TROPHY_HANDLES; i++) {
        if (g_handles[i].in_use && g_handles[i].id == handle) {
            handle_found = 1;
            break;
        }
    }

    if (!context_found || !handle_found) {
        pthread_mutex_unlock(&g_trophy_mutex);
        return -EINVAL;
    }

    ctx_ptr->registered_handle = handle;
    pthread_mutex_unlock(&g_trophy_mutex);
    return 0;
}

int32_t sceNpTrophyUnlockTrophy(int32_t context, int32_t handle, int32_t trophyId, int32_t *platId) {
    pthread_mutex_lock(&g_trophy_mutex);

    TrophyContext *ctx_ptr = NULL;
    for (int i = 0; i < MAX_TROPHY_CONTEXTS; i++) {
        if (g_contexts[i].in_use && g_contexts[i].id == context) {
            ctx_ptr = &g_contexts[i];
            break;
        }
    }

    if (!ctx_ptr) {
        pthread_mutex_unlock(&g_trophy_mutex);
        return -EINVAL;
    }

    (void)handle;

    if (trophyId >= 0 && trophyId < 256) {
        ctx_ptr->unlocked_trophies[trophyId / 32] |= (1U << (trophyId % 32));
    }

    if (platId) {
        *platId = 0;
    }

    printf("\n==================================================\n");
    printf("🏆 [PS4 TROPHY UNLOCKED] Context: %d, Trophy ID: %d\n", context, trophyId);
    printf("==================================================\n\n");
    fflush(stdout);

    pthread_mutex_unlock(&g_trophy_mutex);
    return 0;
}

int32_t sceNpTrophyShowTrophyList(int32_t context, int32_t handle) {
    (void)context;
    (void)handle;
    printf("[ps4-recomp] sceNpTrophyShowTrophyList called\n");
    return 0;
}

//=============================================================================
// Trophy icons, read from the game's trophy pack /app0/sce_sys/trophy/trophyNN.trp (NN = the
// context's service label). A TRP file is a big-endian archive: a 0x60-byte header (magic
// 0xDCA24D00, version, file size, entry count, entry size 0x40, ...) followed by entries of
// {name[32], offset u64, size u64, flags u32, pad[12]}. The PNG icons (ICON0.PNG, GRnnn.PNG,
// TROPnnn.PNG) are stored in the clear; the trophy configuration (TROPCONF.ESFM, TROP*.ESFM)
// is encrypted with the console's trophy key, so group and trophy texts are not available
// here and sceNpTrophyGetGroupInfo is not provided.
//=============================================================================

#define TRP_MAGIC 0xDCA24D00u
#define TRP_HEADER_SIZE 0x60
#define TRP_ENTRY_SIZE 0x40

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

// trp_find result for a pack without the requested entry.
#define TRP_NO_ENTRY 1

// Finds the entry name in trophyNN.trp. Returns 0 with fd open on the file and the entry's
// offset and size, TRP_NO_ENTRY when the pack has no such entry, ICON_FILE_NOT_FOUND when
// there is no pack, or INVALID_TRP_FILE_FORMAT for a malformed pack.
static int32_t trp_find(uint32_t service_label, const char *name, int *fd, uint64_t *off, uint64_t *size) {
    char guest[64], host[1024];
    snprintf(guest, sizeof(guest), "/app0/sce_sys/trophy/trophy%02u.trp", service_label);
    if (ps4_vfs_resolve(guest, host, sizeof(host)) != 0) return ORBIS_NP_TROPHY_ERROR_ICON_FILE_NOT_FOUND;
    int f = open(host, O_RDONLY);
    if (f < 0) return ORBIS_NP_TROPHY_ERROR_ICON_FILE_NOT_FOUND;
    uint8_t hdr[TRP_HEADER_SIZE];
    if (pread(f, hdr, sizeof(hdr), 0) != (ssize_t)sizeof(hdr) || be32(hdr) != TRP_MAGIC ||
        be32(hdr + 0x14) != TRP_ENTRY_SIZE) {
        close(f);
        return ORBIS_NP_TROPHY_ERROR_INVALID_TRP_FILE_FORMAT;
    }
    const uint64_t file_size = be64(hdr + 0x08);
    const uint32_t count = be32(hdr + 0x10);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t e[TRP_ENTRY_SIZE];
        if (pread(f, e, sizeof(e), TRP_HEADER_SIZE + (off_t)i * TRP_ENTRY_SIZE) != (ssize_t)sizeof(e)) break;
        if (strncmp((const char *)e, name, 32) != 0) continue;
        uint64_t o = be64(e + 0x20), n = be64(e + 0x28);
        if (o > file_size || n > file_size - o) break;
        *fd = f;
        *off = o;
        *size = n;
        return 0;
    }
    close(f);
    return TRP_NO_ENTRY;
}

// Lock held. Context and handle checks shared by the icon queries (shadPS4 np_trophy.cpp order).
static int32_t trophy_check_registered(int32_t context, int32_t handle, TrophyContext **out) {
    TrophyContext *c = NULL;
    for (int i = 0; i < MAX_TROPHY_CONTEXTS; i++) {
        if (g_contexts[i].in_use && g_contexts[i].id == context) c = &g_contexts[i];
    }
    if (!c) return ORBIS_NP_TROPHY_ERROR_INVALID_CONTEXT;
    bool handle_ok = false;
    for (int i = 0; i < MAX_TROPHY_HANDLES; i++) {
        handle_ok |= g_handles[i].in_use && g_handles[i].id == handle;
    }
    if (!handle_ok) return ORBIS_NP_TROPHY_ERROR_INVALID_HANDLE;
    if (c->registered_handle == 0) return ORBIS_NP_TROPHY_ERROR_NOT_REGISTERED;
    *out = c;
    return 0;
}

// Copies icon entry name of the context's pack. buffer NULL: *size gets the icon size. Otherwise
// *size is the buffer size: a smaller buffer is INSUFFICIENT_BUFFER, else the icon is copied
// and *size set to its size. missing is the error for a pack without the entry.
static int32_t trophy_copy_icon(uint32_t service_label, const char *name, int32_t missing,
                                void *buffer, uint64_t *size) {
    int fd;
    uint64_t off, n;
    int32_t r = trp_find(service_label, name, &fd, &off, &n);
    if (r == TRP_NO_ENTRY) return missing;
    if (r != 0) return r;
    if (buffer) {
        if (*size < n) {
            r = ORBIS_NP_TROPHY_ERROR_INSUFFICIENT_BUFFER;
        } else if (pread(fd, buffer, (size_t)n, (off_t)off) != (ssize_t)n) {
            r = ORBIS_NP_TROPHY_ERROR_INVALID_TRP_FILE_FORMAT;
        }
    }
    close(fd);
    *size = n;
    return r;
}

int32_t sceNpTrophyGetGameIcon(int32_t context, int32_t handle, void *buffer, uint64_t *size) {
    if (!size) return ORBIS_NP_TROPHY_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&g_trophy_mutex);
    TrophyContext *c = NULL;
    int32_t r = trophy_check_registered(context, handle, &c);
    uint32_t label = c ? c->service_label : 0;
    pthread_mutex_unlock(&g_trophy_mutex);
    if (r != 0) return r;
    return trophy_copy_icon(label, "ICON0.PNG", ORBIS_NP_TROPHY_ERROR_ICON_FILE_NOT_FOUND, buffer, size);
}

int32_t sceNpTrophyGetGroupIcon(int32_t context, int32_t handle, int32_t group_id, void *buffer,
                                uint64_t *size) {
    if (!size) return ORBIS_NP_TROPHY_ERROR_INVALID_ARGUMENT;
    // Only numbered groups have a GRnnn.PNG icon; the base game group (-1) has none.
    if (group_id < 0 || group_id > 999) return ORBIS_NP_TROPHY_ERROR_INVALID_GROUP_ID;
    pthread_mutex_lock(&g_trophy_mutex);
    TrophyContext *c = NULL;
    int32_t r = trophy_check_registered(context, handle, &c);
    uint32_t label = c ? c->service_label : 0;
    pthread_mutex_unlock(&g_trophy_mutex);
    if (r != 0) return r;
    char name[16];
    snprintf(name, sizeof(name), "GR%03d.PNG", group_id);
    return trophy_copy_icon(label, name, ORBIS_NP_TROPHY_ERROR_INVALID_GROUP_ID, buffer, size);
}

// Icons of trophies not yet unlocked are not handed out (shadPS4 sceNpTrophyGetTrophyIcon).
// The unlock state is the context's: trophies unlocked with sceNpTrophyUnlockTrophy.
int32_t sceNpTrophyGetTrophyIcon(int32_t context, int32_t handle, int32_t trophy_id, void *buffer,
                                 uint64_t *size) {
    if (!size) return ORBIS_NP_TROPHY_ERROR_INVALID_ARGUMENT;
    if (trophy_id < 0 || trophy_id >= ORBIS_NP_TROPHY_NUM_MAX) return ORBIS_NP_TROPHY_ERROR_INVALID_TROPHY_ID;
    pthread_mutex_lock(&g_trophy_mutex);
    TrophyContext *c = NULL;
    int32_t r = trophy_check_registered(context, handle, &c);
    uint32_t label = c ? c->service_label : 0;
    bool unlocked = c && (c->unlocked_trophies[trophy_id / 32] >> (trophy_id % 32)) & 1u;
    pthread_mutex_unlock(&g_trophy_mutex);
    if (r != 0) return r;
    char name[16];
    snprintf(name, sizeof(name), "TROP%03d.PNG", trophy_id);
    if (!unlocked) {
        // An id the pack does not have is INVALID_TROPHY_ID even while locked.
        uint64_t probe = 0;
        r = trophy_copy_icon(label, name, ORBIS_NP_TROPHY_ERROR_INVALID_TROPHY_ID, NULL, &probe);
        return r != 0 ? r : ORBIS_NP_TROPHY_ERROR_TROPHY_NOT_UNLOCKED;
    }
    return trophy_copy_icon(label, name, ORBIS_NP_TROPHY_ERROR_INVALID_TROPHY_ID, buffer, size);
}

// Guest ABI Shims
void shim_sceNpTrophyInit(GuestContext *ctx) {
    uint64_t paramsGuest = ctx->rdi;
    void *params = paramsGuest ? (void *)(ctx->mem_base + paramsGuest) : NULL;
    int32_t ret = sceNpTrophyInit(params);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyTerm(GuestContext *ctx) {
    int32_t ret = sceNpTrophyTerm();
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyCreateContext(GuestContext *ctx) {
    uint64_t contextGuest = ctx->rdi;
    int32_t user = (int32_t)ctx->rsi;
    uint32_t unk = (uint32_t)ctx->rdx;
    uint64_t unk2 = ctx->rcx;

    int32_t *context = contextGuest ? (int32_t *)(ctx->mem_base + contextGuest) : NULL;
    int32_t ret = sceNpTrophyCreateContext(context, user, unk, unk2);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyDestroyContext(GuestContext *ctx) {
    int32_t context = (int32_t)ctx->rdi;
    int32_t ret = sceNpTrophyDestroyContext(context);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyCreateHandle(GuestContext *ctx) {
    uint64_t handleGuest = ctx->rdi;
    int32_t *handle = handleGuest ? (int32_t *)(ctx->mem_base + handleGuest) : NULL;
    int32_t ret = sceNpTrophyCreateHandle(handle);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyDestroyHandle(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    int32_t ret = sceNpTrophyDestroyHandle(handle);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyRegisterContext(GuestContext *ctx) {
    int32_t context = (int32_t)ctx->rdi;
    int32_t handle = (int32_t)ctx->rsi;
    uint64_t unk = ctx->rdx;
    int32_t ret = sceNpTrophyRegisterContext(context, handle, unk);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyUnlockTrophy(GuestContext *ctx) {
    int32_t context = (int32_t)ctx->rdi;
    int32_t handle = (int32_t)ctx->rsi;
    int32_t trophyId = (int32_t)ctx->rdx;
    uint64_t platIdGuest = ctx->rcx;
    int32_t *platId = platIdGuest ? (int32_t *)(ctx->mem_base + platIdGuest) : NULL;

    int32_t ret = sceNpTrophyUnlockTrophy(context, handle, trophyId, platId);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyShowTrophyList(GuestContext *ctx) {
    int32_t context = (int32_t)ctx->rdi;
    int32_t handle = (int32_t)ctx->rsi;
    int32_t ret = sceNpTrophyShowTrophyList(context, handle);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

// sceNpTrophyGetTrophyUnlockState(context, handle, SceNpTrophyFlagArray *flags, uint32_t *count)
// No trophies are tracked on the host: report every trophy as locked.
void shim_sceNpTrophyGetTrophyUnlockState(GuestContext *ctx) {
    uint64_t flags_addr = ctx->rdx;
    uint64_t count_addr = ctx->rcx;
    if (flags_addr) {
        memset(ctx->mem_base + flags_addr, 0, 16); // 128-bit flag array
    }
    if (count_addr) {
        *(uint32_t *)(ctx->mem_base + count_addr) = 0;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpTrophyGetGameInfo(GuestContext *ctx) {
    uint64_t details_addr = ctx->rdx;
    uint64_t data_addr = ctx->rcx;
    if (details_addr) {
        size_t sz = *(size_t *)(ctx->mem_base + details_addr);
        if (sz > sizeof(size_t)) {
            memset(ctx->mem_base + details_addr + sizeof(size_t), 0, sz - sizeof(size_t));
        }
    }
    if (data_addr) {
        size_t sz = *(size_t *)(ctx->mem_base + data_addr);
        if (sz > sizeof(size_t)) {
            memset(ctx->mem_base + data_addr + sizeof(size_t), 0, sz - sizeof(size_t));
        }
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpTrophyGetTrophyInfo(GuestContext *ctx) {
    int32_t trophy_id = (int32_t)ctx->rdx;
    uint64_t details_addr = ctx->rcx;
    uint64_t data_addr = ctx->r8;
    if (details_addr) {
        size_t sz = *(size_t *)(ctx->mem_base + details_addr);
        if (sz > sizeof(size_t)) {
            memset(ctx->mem_base + details_addr + sizeof(size_t), 0, sz - sizeof(size_t));
            *(int32_t *)(ctx->mem_base + details_addr + sizeof(size_t)) = trophy_id;
        }
    }
    if (data_addr) {
        size_t sz = *(size_t *)(ctx->mem_base + data_addr);
        if (sz > sizeof(size_t)) {
            memset(ctx->mem_base + data_addr + sizeof(size_t), 0, sz - sizeof(size_t));
            *(int32_t *)(ctx->mem_base + data_addr + sizeof(size_t)) = trophy_id;
        }
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceNpTrophyAbortHandle(GuestContext *ctx) {
    (void)ctx->rdi;
    ctx->rax = 0;
    SHIM_RETURN();
}

// Guest buffer for an icon: NULL when absent; *fault when it does not fit in guest memory.
static void *trophy_icon_buffer(GuestContext *ctx, uint64_t addr, uint64_t size, bool *fault) {
    if (!addr) return NULL;
    if (addr + size < addr || addr + size > ctx->mem_size) {
        *fault = true;
        return NULL;
    }
    return ctx->mem_base + addr;
}

static uint64_t *trophy_size_ptr(GuestContext *ctx, uint64_t addr) {
    return (addr && addr + sizeof(uint64_t) <= ctx->mem_size) ? (uint64_t *)(ctx->mem_base + addr) : NULL;
}

// int sceNpTrophyGetGameIcon(SceNpTrophyContext context, SceNpTrophyHandle handle, void *buffer,
//                            size_t *size)
void shim_sceNpTrophyGetGameIcon(GuestContext *ctx) {
    uint64_t *size = trophy_size_ptr(ctx, ctx->rcx);
    bool fault = false;
    void *buf = trophy_icon_buffer(ctx, ctx->rdx, size ? *size : 0, &fault);
    int32_t r = fault ? ORBIS_NP_TROPHY_ERROR_INVALID_ARGUMENT
                      : sceNpTrophyGetGameIcon((int32_t)ctx->rdi, (int32_t)ctx->rsi, buf, size);
    ctx->rax = (uint64_t)(int64_t)r;
    SHIM_RETURN();
}

// int sceNpTrophyGetGroupIcon(SceNpTrophyContext context, SceNpTrophyHandle handle,
//                             SceNpTrophyGroupId groupId, void *buffer, size_t *size)
void shim_sceNpTrophyGetGroupIcon(GuestContext *ctx) {
    uint64_t *size = trophy_size_ptr(ctx, ctx->r8);
    bool fault = false;
    void *buf = trophy_icon_buffer(ctx, ctx->rcx, size ? *size : 0, &fault);
    int32_t r = fault ? ORBIS_NP_TROPHY_ERROR_INVALID_ARGUMENT
                      : sceNpTrophyGetGroupIcon((int32_t)ctx->rdi, (int32_t)ctx->rsi, (int32_t)ctx->rdx, buf, size);
    ctx->rax = (uint64_t)(int64_t)r;
    SHIM_RETURN();
}

// int sceNpTrophyGetTrophyIcon(SceNpTrophyContext context, SceNpTrophyHandle handle,
//                              SceNpTrophyId trophyId, void *buffer, size_t *size)
void shim_sceNpTrophyGetTrophyIcon(GuestContext *ctx) {
    uint64_t *size = trophy_size_ptr(ctx, ctx->r8);
    bool fault = false;
    void *buf = trophy_icon_buffer(ctx, ctx->rcx, size ? *size : 0, &fault);
    int32_t r = fault ? ORBIS_NP_TROPHY_ERROR_INVALID_ARGUMENT
                      : sceNpTrophyGetTrophyIcon((int32_t)ctx->rdi, (int32_t)ctx->rsi, (int32_t)ctx->rdx, buf, size);
    ctx->rax = (uint64_t)(int64_t)r;
    SHIM_RETURN();
}
