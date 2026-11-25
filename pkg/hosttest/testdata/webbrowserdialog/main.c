// SPDX-License-Identifier: GPL-2.0-or-later

// libSceWebBrowserDialog: its lifecycle on top of the CommonDialog system (system
// initialization, the dialog holding the system, BUSY for a second dialog) and the result.

#include "ps4_common_dialog.h"
#include "ps4_web_browser_dialog.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

static int test_lifecycle(void) {
    CHECK(sceWebBrowserDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_NOT_SYSTEM_INITIALIZED);
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(sceWebBrowserDialogTerminate() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceWebBrowserDialogInitialize() == 0 && sceCommonDialogIsUsed());
    CHECK(sceWebBrowserDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED);
    CHECK(sceWebBrowserDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_INITIALIZED);
    CHECK(sceWebBrowserDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);

    OrbisWebBrowserDialogParam p;
    memset(&p, 0, sizeof(p));
    p.userData = (void *)0x1234;
    OrbisWebBrowserDialogResult r;
    CHECK(sceWebBrowserDialogOpen(NULL) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);
    CHECK(sceWebBrowserDialogOpen(&p) == 0 && sceWebBrowserDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_RUNNING);
    CHECK(sceWebBrowserDialogOpen(&p) == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE);
    CHECK(sceWebBrowserDialogGetResult(&r) == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE);
    CHECK(sceWebBrowserDialogUpdateStatus() == ORBIS_COMMON_DIALOG_STATUS_FINISHED);
    CHECK(sceWebBrowserDialogGetResult(&r) == 0 && r.result == ORBIS_COMMON_DIALOG_RESULT_OK && r.userData == (void *)0x1234);
    // Reopened and closed by the game.
    CHECK(sceWebBrowserDialogOpen(&p) == 0 && sceWebBrowserDialogClose() == 0);
    CHECK(sceWebBrowserDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_FINISHED);
    CHECK(sceWebBrowserDialogTerminate() == 0 && !sceCommonDialogIsUsed());
    CHECK(sceWebBrowserDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_NONE);
    printf("PASS lifecycle\n");
    return 0;
}

static int test_busy(void) {
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(ps4_common_dialog_acquire()); // another dialog holds the system
    CHECK(sceWebBrowserDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_BUSY);
    CHECK(sceWebBrowserDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_NONE);
    ps4_common_dialog_release();
    CHECK(sceWebBrowserDialogInitialize() == 0);
    printf("PASS busy\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "lifecycle") == 0) return test_lifecycle();
    if (strcmp(argv[1], "busy") == 0) return test_busy();
    return 2;
}
