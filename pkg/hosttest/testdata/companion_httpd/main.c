// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_http.h"
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

static void test_http_options(void) {
    // Test resolve timeout
    assert(sceHttpSetResolveTimeOut(0, 5000000) == (int32_t)ORBIS_HTTP_ERROR_INVALID_ID);
    assert(sceHttpSetResolveTimeOut(-1, 5000000) == (int32_t)ORBIS_HTTP_ERROR_INVALID_ID);
    assert(sceHttpSetResolveTimeOut(9999, 5000000) == (int32_t)ORBIS_HTTP_ERROR_INVALID_ID);

    // Test cookie enabled
    assert(sceHttpSetCookieEnabled(0, 1) == (int32_t)ORBIS_HTTP_ERROR_INVALID_ID);
    assert(sceHttpSetCookieEnabled(-1, 1) == (int32_t)ORBIS_HTTP_ERROR_INVALID_ID);
    assert(sceHttpSetCookieEnabled(9999, 1) == (int32_t)ORBIS_HTTP_ERROR_INVALID_ID);

    // Initialize an HTTP context
    int32_t ctx_id = sceHttpInit(1, 1, 1024 * 1024);
    assert(ctx_id > 0);

    // Now valid IDs succeed
    assert(sceHttpSetResolveTimeOut(ctx_id, 5000000) == 0);
    assert(sceHttpSetCookieEnabled(ctx_id, 1) == 0);

    // Test CA list
    OrbisHttpsCaList list;
    memset(&list, 0x55, sizeof(list));
    assert(sceHttpsGetCaList(0, &list) == (int32_t)ORBIS_HTTP_ERROR_INVALID_ID);
    assert(sceHttpsGetCaList(ctx_id, NULL) == (int32_t)ORBIS_HTTP_ERROR_INVALID_VALUE);
    assert(sceHttpsGetCaList(9999, &list) == (int32_t)ORBIS_HTTP_ERROR_INVALID_ID);

    assert(sceHttpsGetCaList(ctx_id, &list) == 0);
    assert(list.certs == NULL);
    assert(list.certsNum == 0);

    assert(sceHttpTerm(ctx_id) == 0);
    printf("PASS http-options\n");
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *name = argv[1];
    if (!strcmp(name, "companion-lifecycle")) test_companion_lifecycle();
    else if (!strcmp(name, "http-options")) test_http_options();
    else {
        fprintf(stderr, "Unknown test: %s\n", name);
        return 2;
    }
    return 0;
}
