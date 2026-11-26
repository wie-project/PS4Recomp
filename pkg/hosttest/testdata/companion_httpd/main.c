// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_companion_httpd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_companion_lifecycle(void) {
    OrbisCompanionHttpdEvent ev;

    // Before init
    assert(sceCompanionHttpdTerminate() == ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED);
    assert(sceCompanionHttpdStart() == ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED);
    assert(sceCompanionHttpdStop() == ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED);
    assert(sceCompanionHttpdGetEvent(&ev) == ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED);
    assert(sceCompanionHttpdRegisterRequestCallback2(NULL, NULL) == ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED);
    assert(sceCompanionHttpdUnregisterRequestCallback() == ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED);

    // Initialize
    assert(sceCompanionHttpdInitialize2(NULL) == 0);
    assert(sceCompanionHttpdInitialize2(NULL) == ORBIS_COMPANION_HTTPD_ERROR_ALREADY_INITIALIZED);

    // Start & Stop
    assert(sceCompanionHttpdStop() == ORBIS_COMPANION_HTTPD_ERROR_NOT_STARTED);
    assert(sceCompanionHttpdStart() == 0);
    assert(sceCompanionHttpdStart() == ORBIS_COMPANION_HTTPD_ERROR_ALREADY_STARTED);

    // GetEvent
    assert(sceCompanionHttpdGetEvent(NULL) == ORBIS_COMPANION_HTTPD_ERROR_INVALID_PARAM);
    memset(&ev, 0xAA, sizeof(ev));
    int32_t rc = sceCompanionHttpdGetEvent(&ev);
    assert(rc == ORBIS_COMPANION_HTTPD_ERROR_NO_EVENT);
    assert(ev.event == ORBIS_COMPANION_HTTPD_EVENT_DISCONNECT);

    // Register / Unregister callback
    int dummy_cb = 42;
    int dummy_data = 100;
    assert(sceCompanionHttpdRegisterRequestCallback2(&dummy_cb, &dummy_data) == 0);
    assert(sceCompanionHttpdUnregisterRequestCallback() == 0);

    // Stop & Terminate
    assert(sceCompanionHttpdStop() == 0);
    assert(sceCompanionHttpdTerminate() == 0);
    assert(sceCompanionHttpdTerminate() == ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED);

    printf("PASS companion-lifecycle\n");
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *name = argv[1];
    if (!strcmp(name, "companion-lifecycle")) test_companion_lifecycle();
    else {
        fprintf(stderr, "Unknown test: %s\n", name);
        return 2;
    }
    return 0;
}
