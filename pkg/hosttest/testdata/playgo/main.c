// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_playgo.h"
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

int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz) {
    (void)guest_path;
    (void)host_path;
    (void)host_path_sz;
    return -1;
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

    // Now SetToDoList (file_size == 0 -> NOT_SUPPORT_PLAYGO, or 0 if chunks loaded)
    rc = scePlayGoSetToDoList(1, &todo, 1);
    assert(rc == ORBIS_PLAYGO_ERROR_NOT_SUPPORT_PLAYGO || rc == 0);

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
    else {
        fprintf(stderr, "Unknown test: %s\n", name);
        return 2;
    }
    return 0;
}
