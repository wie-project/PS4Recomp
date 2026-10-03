#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ps4_signin_dialog.h"
#include "ps4_web_browser_dialog.h"
#include "ps4_ime.h"
#include "ps4_np.h"

int main(void) {
    printf("[ps4-dialogs-auth-test] Running Dialogs & Auth tests...\n");

    // 1. Signin Dialog Test
    int32_t ret = sceSigninDialogInitialize();
    assert(ret == 0);
    assert(sceSigninDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_INITIALIZED);

    OrbisSigninDialogParam signinParam = {0};
    signinParam.baseParam.size = sizeof(signinParam);
    signinParam.mode = 0;
    ret = sceSigninDialogOpen(&signinParam);
    assert(ret == 0);
    assert(sceSigninDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_RUNNING);

    OrbisCommonDialogStatus status = sceSigninDialogUpdateStatus();
    assert(status == ORBIS_COMMON_DIALOG_STATUS_FINISHED);

    OrbisSigninDialogResult signinResult = {0};
    ret = sceSigninDialogGetResult(&signinResult);
    assert(ret == 0);
    assert(signinResult.result == ORBIS_COMMON_DIALOG_RESULT_OK);

    ret = sceSigninDialogTerminate();
    assert(ret == 0);
    assert(sceSigninDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_NONE);
    printf("[+] Signin Dialog lifecycle: PASSED\n");

    // 2. Web Browser Dialog Test
    ret = sceWebBrowserDialogInitialize();
    assert(ret == 0);
    assert(sceWebBrowserDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_INITIALIZED);

    OrbisWebBrowserDialogParam webParam = {0};
    webParam.baseParam.size = sizeof(webParam);
    webParam.url = "https://example.com";
    ret = sceWebBrowserDialogOpen(&webParam);
    assert(ret == 0);
    assert(sceWebBrowserDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_RUNNING);

    status = sceWebBrowserDialogUpdateStatus();
    assert(status == ORBIS_COMMON_DIALOG_STATUS_FINISHED);

    OrbisWebBrowserDialogResult webResult = {0};
    ret = sceWebBrowserDialogGetResult(&webResult);
    assert(ret == 0);
    assert(webResult.result == ORBIS_COMMON_DIALOG_RESULT_OK);

    ret = sceWebBrowserDialogTerminate();
    assert(ret == 0);
    assert(sceWebBrowserDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_NONE);
    printf("[+] Web Browser Dialog lifecycle: PASSED\n");

    // 3. Ime Keyboard Test
    OrbisImeKeyboardParam imeParam = {0};
    imeParam.option = 0;
    ret = sceImeKeyboardOpen(0x10000000, &imeParam);
    assert(ret == 0);

    OrbisImeKeyboardResourceIdArray resArray = {0};
    ret = sceImeKeyboardGetResourceId(0x10000000, &resArray);
    assert(ret == ORBIS_IME_ERROR_CONNECTION_FAILED);
    assert(resArray.user_id == 0x10000000);

    ret = sceImeKeyboardClose(0x10000000);
    assert(ret == 0);

    ret = sceImeKeyboardGetResourceId(0x10000000, &resArray);
    assert(ret == ORBIS_IME_ERROR_NOT_OPENED);
    printf("[+] Ime Keyboard lifecycle: PASSED\n");

    // 4. NP State Callbacks for Toolkit Test
    ret = sceNpRegisterStateCallbackForToolkit((void*)0x1234, (void*)0x5678);
    assert(ret == 0);

    ret = sceNpCheckCallbackForLib();
    assert(ret == 0);

    ret = sceNpCheckCallback();
    assert(ret == 0);

    ret = sceNpUnregisterStateCallbackForToolkit();
    assert(ret == 0);
    printf("[+] NP State Callbacks for Toolkit: PASSED\n");

    printf("[ps4-dialogs-auth-test] ALL TESTS PASSED!\n");
    return 0;
}
