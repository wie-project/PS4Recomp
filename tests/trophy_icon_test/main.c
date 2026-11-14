// Host test for sceNpTrophyGetGameIcon / GetGroupIcon / GetTrophyIcon on a synthetic trophy
// pack. Build and run (from the repository root):
//   clang -std=gnu11 -I pkg/runtime/core -I pkg/runtime/kernel/vfs -I pkg/runtime/modules/libSceNpTrophy \
//     tests/trophy_icon_test/main.c pkg/runtime/modules/libSceNpTrophy/ps4_trophy.c \
//     -o /tmp/trophy_icon_test && /tmp/trophy_icon_test

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps4_trophy.h"
#include "ps4_vfs.h"
#include "recomp_context.h"

// The tested functions run no guest code: an empty dispatch table makes any guest call abort.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

// VFS double: /app0 is a temporary directory.
static char g_app0[256];
int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz) {
    if (strncmp(guest_path, "/app0/", 6) != 0) return -1;
    snprintf(host_path, host_path_sz, "%s/%s", g_app0, guest_path + 6);
    return 0;
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}
static void put_be64(uint8_t *p, uint64_t v) {
    put_be32(p, (uint32_t)(v >> 32));
    put_be32(p + 4, (uint32_t)v);
}

typedef struct {
    const char *name;
    const char *data;
    uint32_t flags;
} Entry;

// Writes a TRP archive with the given entries (layout as in ps4_trophy.c).
static void write_trp(const char *path, const Entry *entries, int n) {
    uint8_t buf[4096];
    memset(buf, 0, sizeof(buf));
    uint64_t data_off = 0x60 + (uint64_t)n * 0x40;
    uint64_t at = data_off;
    for (int i = 0; i < n; i++) {
        uint8_t *e = buf + 0x60 + i * 0x40;
        strncpy((char *)e, entries[i].name, 32);
        size_t len = strlen(entries[i].data);
        put_be64(e + 0x20, at);
        put_be64(e + 0x28, len);
        put_be32(e + 0x30, entries[i].flags);
        memcpy(buf + at, entries[i].data, len);
        at += len;
    }
    put_be32(buf, 0xDCA24D00u);
    put_be32(buf + 4, 3);
    put_be64(buf + 8, at);
    put_be32(buf + 0x10, (uint32_t)n);
    put_be32(buf + 0x14, 0x40);
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(buf, 1, at, f) == at);
    fclose(f);
}

int main(void) {
    snprintf(g_app0, sizeof(g_app0), "%s/trophy_icon_test_XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    assert(mkdtemp(g_app0));
    char dir[512], trp[600], bad[600];
    snprintf(dir, sizeof(dir), "%s/sce_sys", g_app0);
    assert(mkdir(dir, 0700) == 0);
    snprintf(dir, sizeof(dir), "%s/sce_sys/trophy", g_app0);
    assert(mkdir(dir, 0700) == 0);
    snprintf(trp, sizeof(trp), "%s/trophy00.trp", dir);
    const Entry entries[] = {
        {"TROPCONF.ESFM", "encrypted", 3}, {"ICON0.PNG", "game-icon", 0}, {"GR001.PNG", "group-1", 0},
        {"TROP000.PNG", "trophy-0", 0},    {"TROP001.PNG", "trophy-1", 0},
    };
    write_trp(trp, entries, 5);
    // Service label 1 selects trophy01.trp, which is not a TRP archive.
    snprintf(bad, sizeof(bad), "%s/trophy01.trp", dir);
    FILE *f = fopen(bad, "wb");
    assert(f && fwrite("not a trophy pack, padded to more than 0x60 bytes ......................................"
                       "..........", 1, 100, f) == 100);
    fclose(f);

    int32_t ctx, handle, ctx_bad, ctx_none;
    assert(sceNpTrophyInit(NULL) == 0);
    assert(sceNpTrophyCreateContext(&ctx, 1, 0, 0) == 0);
    assert(sceNpTrophyCreateContext(&ctx_bad, 1, 1, 0) == 0);
    assert(sceNpTrophyCreateContext(&ctx_none, 1, 2, 0) == 0);
    assert(sceNpTrophyCreateHandle(&handle) == 0);

    char buf[64];
    uint64_t size = 0;
    // Argument, context, handle and registration checks.
    assert(sceNpTrophyGetGameIcon(ctx, handle, NULL, NULL) == ORBIS_NP_TROPHY_ERROR_INVALID_ARGUMENT);
    assert(sceNpTrophyGetGameIcon(ctx + 100, handle, NULL, &size) == ORBIS_NP_TROPHY_ERROR_INVALID_CONTEXT);
    assert(sceNpTrophyGetGameIcon(ctx, handle + 100, NULL, &size) == ORBIS_NP_TROPHY_ERROR_INVALID_HANDLE);
    assert(sceNpTrophyGetGameIcon(ctx, handle, NULL, &size) == ORBIS_NP_TROPHY_ERROR_NOT_REGISTERED);
    assert(sceNpTrophyRegisterContext(ctx, handle, 0) == 0);
    assert(sceNpTrophyRegisterContext(ctx_bad, handle, 0) == 0);
    assert(sceNpTrophyRegisterContext(ctx_none, handle, 0) == 0);

    // Game icon: size query, short buffer, copy.
    assert(sceNpTrophyGetGameIcon(ctx, handle, NULL, &size) == 0 && size == 9);
    size = 4;
    assert(sceNpTrophyGetGameIcon(ctx, handle, buf, &size) == ORBIS_NP_TROPHY_ERROR_INSUFFICIENT_BUFFER);
    assert(size == 9);
    memset(buf, 0, sizeof(buf));
    size = sizeof(buf);
    assert(sceNpTrophyGetGameIcon(ctx, handle, buf, &size) == 0);
    assert(size == 9 && memcmp(buf, "game-icon", 9) == 0 && buf[9] == 0);

    // The service label selects the pack: a malformed one, and a missing one.
    size = sizeof(buf);
    assert(sceNpTrophyGetGameIcon(ctx_bad, handle, buf, &size) == ORBIS_NP_TROPHY_ERROR_INVALID_TRP_FILE_FORMAT);
    assert(sceNpTrophyGetGameIcon(ctx_none, handle, buf, &size) == ORBIS_NP_TROPHY_ERROR_ICON_FILE_NOT_FOUND);

    // Group icons: GRnnn.PNG; the base game group and groups without an icon are invalid.
    size = sizeof(buf);
    memset(buf, 0, sizeof(buf));
    assert(sceNpTrophyGetGroupIcon(ctx, handle, 1, buf, &size) == 0);
    assert(size == 7 && memcmp(buf, "group-1", 7) == 0);
    assert(sceNpTrophyGetGroupIcon(ctx, handle, -1, buf, &size) == ORBIS_NP_TROPHY_ERROR_INVALID_GROUP_ID);
    assert(sceNpTrophyGetGroupIcon(ctx, handle, 2, buf, &size) == ORBIS_NP_TROPHY_ERROR_INVALID_GROUP_ID);

    // Trophy icons: only for unlocked trophies; ids outside the pack are invalid.
    size = sizeof(buf);
    assert(sceNpTrophyGetTrophyIcon(ctx, handle, 0, buf, &size) == ORBIS_NP_TROPHY_ERROR_TROPHY_NOT_UNLOCKED);
    assert(sceNpTrophyGetTrophyIcon(ctx, handle, 5, buf, &size) == ORBIS_NP_TROPHY_ERROR_INVALID_TROPHY_ID);
    assert(sceNpTrophyGetTrophyIcon(ctx, handle, -1, buf, &size) == ORBIS_NP_TROPHY_ERROR_INVALID_TROPHY_ID);
    assert(sceNpTrophyGetTrophyIcon(ctx, handle, ORBIS_NP_TROPHY_NUM_MAX, buf, &size) ==
           ORBIS_NP_TROPHY_ERROR_INVALID_TROPHY_ID);
    int32_t plat = -1;
    assert(sceNpTrophyUnlockTrophy(ctx, handle, 1, &plat) == 0);
    memset(buf, 0, sizeof(buf));
    size = sizeof(buf);
    assert(sceNpTrophyGetTrophyIcon(ctx, handle, 1, buf, &size) == 0);
    assert(size == 8 && memcmp(buf, "trophy-1", 8) == 0);
    assert(sceNpTrophyGetTrophyIcon(ctx, handle, 1, NULL, &size) == 0 && size == 8);
    // Unlock state is per context.
    int32_t ctx2;
    assert(sceNpTrophyCreateContext(&ctx2, 2, 0, 0) == 0);
    assert(sceNpTrophyRegisterContext(ctx2, handle, 0) == 0);
    assert(sceNpTrophyGetTrophyIcon(ctx2, handle, 1, NULL, &size) == ORBIS_NP_TROPHY_ERROR_TROPHY_NOT_UNLOCKED);
    // Without a trophy pack every icon is ICON_FILE_NOT_FOUND.
    assert(sceNpTrophyGetGroupIcon(ctx_none, handle, 1, NULL, &size) == ORBIS_NP_TROPHY_ERROR_ICON_FILE_NOT_FOUND);
    assert(sceNpTrophyGetTrophyIcon(ctx_none, handle, 1, NULL, &size) == ORBIS_NP_TROPHY_ERROR_ICON_FILE_NOT_FOUND);

    unlink(bad);
    unlink(trp);
    rmdir(dir);
    snprintf(dir, sizeof(dir), "%s/sce_sys", g_app0);
    rmdir(dir);
    rmdir(g_app0);
    printf("[trophy-icon-test] PASSED\n");
    return 0;
}
