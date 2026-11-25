// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_playgo.h"
#include "ps4_system_service.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define COMMON_DIALOG_STATUS_NONE 0
#define COMMON_DIALOG_STATUS_INITIALIZED 1
#define COMMON_DIALOG_STATUS_RUNNING 2
#define COMMON_DIALOG_STATUS_FINISHED 3

#define COMMON_DIALOG_ERROR_NOT_INITIALIZED ((int32_t)0x80B80003)
#define COMMON_DIALOG_ERROR_ALREADY_INITIALIZED ((int32_t)0x80B80004)
#define COMMON_DIALOG_ERROR_NOT_FINISHED ((int32_t)0x80B80005)
#define COMMON_DIALOG_ERROR_ARG_NULL ((int32_t)0x80B8000D)

// The game's files: /app0/sce_sys/playgo-chunk.dat is PLAYGO_FILE when that is set.
int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz) {
    const char *file = getenv("PLAYGO_FILE");
    if (!file || strcmp(guest_path, "/app0/sce_sys/playgo-chunk.dat") != 0) return -1;
    snprintf(host_path, host_path_sz, "%s", file);
    return 0;
}

// The system language setting (libSceSystemService): 2 is Spanish.
int32_t sceSystemServiceParamGetInt(int32_t paramId, int32_t *value) {
    *value = paramId == ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG ? 2 : 0;
    return 0;
}

static void put16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }

// A playgo-chunk.dat with 3 chunks over 4 mchunks, laid out as the console's tools write it:
// a 0x100-byte header, then 32-byte chunk attributes, the mchunk index lists, labels and
// 16-byte mchunk attributes.
static const char *write_playgo_file(void) {
    static uint8_t f[0x400];
    memset(f, 0, sizeof(f));
    put32(f + 0x00, 0x6F676C70); // 'plgo'
    put16(f + 0x0a, 3);          // chunk_count
    put16(f + 0x0c, 4);          // mchunk_count
    put32(f + 0x10, sizeof(f));  // file_size
    const uint32_t attrs = 0x100, mchunks = 0x160, labels = 0x180, mattrs = 0x1a0;
    put32(f + 0xc0, attrs);   put32(f + 0xc4, 3 * 32);
    put32(f + 0xc8, mchunks); put32(f + 0xcc, 12);
    put32(f + 0xd0, labels);  put32(f + 0xd4, 16);
    put32(f + 0xd8, mattrs);  put32(f + 0xdc, 4 * 16);
    // chunk i: req_locus at +2, mchunk_count at +14, language_mask at +16, mchunks_offset at +24.
    const uint16_t counts[3] = {1, 2, 3}, offsets[3] = {0, 2, 6};
    for (int i = 0; i < 3; i++) {
        uint8_t *a = f + attrs + i * 32;
        a[2] = 3;
        put16(a + 14, counts[i]);
        put64(a + 16, 0x8000000000000000ull >> i);
        put32(a + 24, offsets[i]);
    }
    const uint16_t ids[6] = {0, 1, 2, 1, 2, 3};
    for (int i = 0; i < 6; i++) put16(f + mchunks + i * 2, ids[i]);
    // mchunk sizes 0x1000, 0x2000, 0x3000, 0x4000; the top 16 bits are not part of the size.
    for (int i = 0; i < 4; i++) put64(f + mattrs + i * 16 + 8, 0xABCD000000000000ull | (uint64_t)(i + 1) * 0x1000);

    static char path[256];
    snprintf(path, sizeof(path), "%s/playgo-chunk.dat", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    FILE *out = fopen(path, "wb");
    assert(out && fwrite(f, 1, sizeof(f), out) == sizeof(f));
    fclose(out);
    return path;
}

static void test_chunk_file(void) {
    setenv("PLAYGO_FILE", write_playgo_file(), 1);
    OrbisPlayGoInitParams params = { .bufAddr = (void *)(uintptr_t)0x1000, .bufSize = 0x200000, .reserved = 0 };
    assert(scePlayGoInitialize(&params) == 0);
    OrbisPlayGoHandle h = 0;
    assert(scePlayGoOpen(&h, NULL) == 0 && h == 1);

    uint32_t n = 0;
    OrbisPlayGoChunkId ids[8];
    assert(scePlayGoGetChunkId(h, NULL, 0, &n) == 0 && n == 3);
    assert(scePlayGoGetChunkId(h, ids, 8, &n) == 0 && n == 3 && ids[2] == 2);

    // Chunk sizes are the sums of their mchunks (48-bit sizes).
    OrbisPlayGoProgress p;
    OrbisPlayGoChunkId one = 1, all[3] = {0, 1, 2}, bad[2] = {0, 3};
    assert(scePlayGoGetProgress(h, &one, 1, &p) == 0 && p.totalSize == 0x5000 && p.progressSize == 0x5000);
    assert(scePlayGoGetProgress(h, all, 3, &p) == 0 && p.totalSize == 0x1000 + 0x5000 + 0x9000);
    assert(scePlayGoGetProgress(h, bad, 2, &p) == ORBIS_PLAYGO_ERROR_BAD_CHUNK_ID);

    int8_t loci[2] = {-1, -1};
    assert(scePlayGoGetLocus(h, all, 2, loci) == 0 && loci[0] == ORBIS_PLAYGO_LOCUS_LOCAL_FAST);
    assert(scePlayGoGetLocus(h, bad, 2, loci) == ORBIS_PLAYGO_ERROR_BAD_CHUNK_ID &&
           loci[1] == ORBIS_PLAYGO_LOCUS_NOT_DOWNLOADED);

    // One ETA for the whole request: only *outEta is written.
    OrbisPlayGoEta eta[2] = {-1, -1};
    assert(scePlayGoGetEta(h, all, 3, eta) == 0 && eta[0] == 0 && eta[1] == -1);

    // The language mask starts as the system language (bit 63 - lang).
    OrbisPlayGoLanguageMask mask = 0;
    assert(scePlayGoGetLanguageMask(h, &mask) == 0 && mask == 1ull << 61);
    assert(scePlayGoSetLanguageMask(h, 3) == 0 && scePlayGoGetLanguageMask(h, &mask) == 0 && mask == 3);

    int32_t speed = -1;
    assert(scePlayGoGetInstallSpeed(h, &speed) == 0 && speed == ORBIS_PLAYGO_INSTALL_SPEED_FULL);
    assert(scePlayGoSetInstallSpeed(h, 3) == ORBIS_PLAYGO_ERROR_INVALID_ARGUMENT);
    assert(scePlayGoSetInstallSpeed(h, ORBIS_PLAYGO_INSTALL_SPEED_SUSPENDED) == 0);
    assert(scePlayGoGetInstallSpeed(h, &speed) == 0 && speed == ORBIS_PLAYGO_INSTALL_SPEED_SUSPENDED);

    assert(scePlayGoPrefetch(h, all, 3, ORBIS_PLAYGO_LOCUS_LOCAL_SLOW) == 0);
    assert(scePlayGoPrefetch(h, all, 3, 1) == ORBIS_PLAYGO_ERROR_BAD_LOCUS);

    OrbisPlayGoToDo todo[2];
    assert(scePlayGoGetToDoList(h, todo, 2, &n) == 0 && n == 0);
    assert(scePlayGoGetToDoList(h, NULL, 2, &n) == ORBIS_PLAYGO_ERROR_BAD_POINTER);
    assert(scePlayGoGetToDoList(h, todo, 0, &n) == ORBIS_PLAYGO_ERROR_BAD_SIZE);

    assert(scePlayGoTerminate() == 0 && scePlayGoTerminate() == ORBIS_PLAYGO_ERROR_NOT_INITIALIZED);
    unlink(getenv("PLAYGO_FILE"));
    printf("PASS chunk-file\n");
}

static void test_no_playgo(void) {
    OrbisPlayGoInitParams params = { .bufAddr = (void *)(uintptr_t)0x1000, .bufSize = 0x200000, .reserved = 0 };
    assert(scePlayGoInitialize(&params) == 0);
    assert(scePlayGoInitialize(&params) == ORBIS_PLAYGO_ERROR_ALREADY_INITIALIZED);
    OrbisPlayGoHandle h = 0;
    assert(scePlayGoOpen(&h, NULL) == ORBIS_PLAYGO_ERROR_NOT_SUPPORT_PLAYGO);
    uint32_t n = 9;
    assert(scePlayGoGetChunkId(1, NULL, 0, &n) == 0 && n == 0);
    OrbisPlayGoChunkId one = 0;
    OrbisPlayGoProgress p;
    assert(scePlayGoGetProgress(1, &one, 1, &p) == ORBIS_PLAYGO_ERROR_BAD_CHUNK_ID);
    int8_t locus;
    assert(scePlayGoGetLocus(1, &one, 1, &locus) == ORBIS_PLAYGO_ERROR_NOT_SUPPORT_PLAYGO);
    params.bufSize = 0x1000;
    assert(scePlayGoTerminate() == 0 && scePlayGoInitialize(&params) == ORBIS_PLAYGO_ERROR_BAD_SIZE);
    printf("PASS no-playgo\n");
}

static void test_todo_list(void) {
    OrbisPlayGoToDo todo = { .chunkId = 1, .locus = 0, .reserved = 0 };

    // Bad handle
    int32_t rc = scePlayGoSetToDoList(999, &todo, 1);
    assert(rc == ORBIS_PLAYGO_ERROR_BAD_HANDLE);

    // Bad pointer
    rc = scePlayGoSetToDoList(1, NULL, 1);
    assert(rc == ORBIS_PLAYGO_ERROR_BAD_POINTER);

    // Bad size
    rc = scePlayGoSetToDoList(1, &todo, 0);
    assert(rc == ORBIS_PLAYGO_ERROR_BAD_SIZE);

    // Uninitialized
    rc = scePlayGoSetToDoList(1, &todo, 1);
    assert(rc == ORBIS_PLAYGO_ERROR_NOT_INITIALIZED);

    // Initialize playgo
    OrbisPlayGoInitParams params = { .bufAddr = (void *)(uintptr_t)0x1000, .bufSize = 0x200000, .reserved = 0 };
    rc = scePlayGoInitialize(&params);
    assert(rc == 0);

    // Every chunk is installed: there is nothing to schedule.
    rc = scePlayGoSetToDoList(1, &todo, 1);
    assert(rc == 0);

    scePlayGoTerminate();
    printf("PASS todo-list\n");
}

static void test_dialog_lifecycle(void) {
    int32_t res[8] = {0};

    // Before init
    assert(scePlayGoDialogUpdateStatus() == COMMON_DIALOG_STATUS_NONE);
    assert(scePlayGoDialogTerminate() == COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    assert(scePlayGoDialogGetResult(res) == COMMON_DIALOG_ERROR_NOT_INITIALIZED);

    // Initialize
    assert(scePlayGoDialogInitialize() == 0);
    assert(scePlayGoDialogInitialize() == COMMON_DIALOG_ERROR_ALREADY_INITIALIZED);
    assert(scePlayGoDialogUpdateStatus() == COMMON_DIALOG_STATUS_INITIALIZED);

    // Open with NULL param
    assert(scePlayGoDialogOpen(NULL) == COMMON_DIALOG_ERROR_ARG_NULL);

    // Open with valid dummy param
    int dummy = 1;
    assert(scePlayGoDialogOpen(&dummy) == 0);

    // UpdateStatus transitions RUNNING -> FINISHED in headless environment
    assert(scePlayGoDialogUpdateStatus() == COMMON_DIALOG_STATUS_FINISHED);

    // GetResult
    assert(scePlayGoDialogGetResult(NULL) == COMMON_DIALOG_ERROR_ARG_NULL);
    assert(scePlayGoDialogGetResult(res) == 0);
    assert(res[1] == 3); // Result = 3 (allowed to proceed)

    // Terminate
    assert(scePlayGoDialogTerminate() == 0);
    assert(scePlayGoDialogUpdateStatus() == COMMON_DIALOG_STATUS_NONE);

    printf("PASS dialog-lifecycle\n");
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *name = argv[1];
    if (!strcmp(name, "todo-list")) test_todo_list();
    else if (!strcmp(name, "dialog-lifecycle")) test_dialog_lifecycle();
    else if (!strcmp(name, "chunk-file")) test_chunk_file();
    else if (!strcmp(name, "no-playgo")) test_no_playgo();
    else {
        fprintf(stderr, "Unknown test: %s\n", name);
        return 2;
    }
    return 0;
}
