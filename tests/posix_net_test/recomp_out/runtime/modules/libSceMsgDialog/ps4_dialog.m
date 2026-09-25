#import <Cocoa/Cocoa.h>
#include "ps4_dialog.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t g_dialog_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_dialog_inited = false;
static OrbisCommonDialogStatus g_dialog_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
static OrbisMsgDialogMode g_dialog_mode = ORBIS_MSG_DIALOG_MODE_USER_MSG;
static OrbisMsgDialogButtonType g_dialog_button_type = ORBIS_MSG_DIALOG_BUTTON_TYPE_OK;
static char g_dialog_msg[2048] = {0};
static char g_dialog_btn1[128] = {0};
static char g_dialog_btn2[128] = {0};
static OrbisMsgDialogResult g_dialog_result = {0};

int32_t sceCommonDialogInitialize(void) {
  pthread_mutex_lock(&g_dialog_mutex);
  g_dialog_inited = true;
  g_dialog_status = ORBIS_COMMON_DIALOG_STATUS_INITIALIZED;
  pthread_mutex_unlock(&g_dialog_mutex);
  return 0;
}

bool sceCommonDialogIsUsed(void) {
  pthread_mutex_lock(&g_dialog_mutex);
  bool used = (g_dialog_status == ORBIS_COMMON_DIALOG_STATUS_RUNNING);
  pthread_mutex_unlock(&g_dialog_mutex);
  return used;
}

int32_t sceMsgDialogInitialize(void) {
  pthread_mutex_lock(&g_dialog_mutex);
  g_dialog_inited = true;
  g_dialog_status = ORBIS_COMMON_DIALOG_STATUS_INITIALIZED;
  memset(&g_dialog_result, 0, sizeof(g_dialog_result));
  pthread_mutex_unlock(&g_dialog_mutex);
  return 0;
}

int32_t sceMsgDialogOpen(const OrbisMsgDialogParam *param) {
  if (!param) return -1;

  pthread_mutex_lock(&g_dialog_mutex);
  g_dialog_status = ORBIS_COMMON_DIALOG_STATUS_RUNNING;
  g_dialog_mode = param->mode;
  memset(&g_dialog_result, 0, sizeof(g_dialog_result));
  g_dialog_result.mode = param->mode;
  pthread_mutex_unlock(&g_dialog_mutex);
  return 0;
}

OrbisCommonDialogStatus sceMsgDialogGetStatus(void) {
  pthread_mutex_lock(&g_dialog_mutex);
  OrbisCommonDialogStatus status = g_dialog_status;
  pthread_mutex_unlock(&g_dialog_mutex);
  return status;
}

OrbisCommonDialogStatus sceMsgDialogUpdateStatus(void) {
  pthread_mutex_lock(&g_dialog_mutex);
  if (g_dialog_status != ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
    OrbisCommonDialogStatus st = g_dialog_status;
    pthread_mutex_unlock(&g_dialog_mutex);
    return st;
  }

  // If in headless mode or non-interactive environment
  if (getenv("PS4_RECOMP_HEADLESS") != NULL || (!isatty(fileno(stdin)) && !isatty(fileno(stdout)))) {
    printf("[ps4-dialog] [HEADLESS] Message: %s\n", g_dialog_msg);
    g_dialog_result.buttonId = ORBIS_MSG_DIALOG_BUTTON_ID_YES;
    g_dialog_result.result = ORBIS_COMMON_DIALOG_RESULT_OK;
    g_dialog_result.mode = g_dialog_mode;
    g_dialog_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
    OrbisCommonDialogStatus st = g_dialog_status;
    pthread_mutex_unlock(&g_dialog_mutex);
    return st;
  }

  // Copy NSString objects before releasing lock for UI execution
  NSString *nsMsg = [NSString stringWithUTF8String:g_dialog_msg];
  NSString *nsBtn1 = [NSString stringWithUTF8String:g_dialog_btn1[0] ? g_dialog_btn1 : "Button 1"];
  NSString *nsBtn2 = [NSString stringWithUTF8String:g_dialog_btn2[0] ? g_dialog_btn2 : "Button 2"];
  OrbisMsgDialogButtonType btnType = g_dialog_button_type;
  pthread_mutex_unlock(&g_dialog_mutex);

  __block NSModalResponse resp = NSAlertFirstButtonReturn;
  void (^showAlert)(void) = ^{
    @autoreleasepool {
      [NSApplication sharedApplication];
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
      [NSApp activateIgnoringOtherApps:YES];

      NSAlert *alert = [[NSAlert alloc] init];
      [alert setMessageText:@"PlayStation®4"];
      [alert setInformativeText:nsMsg ? nsMsg : @""];
      [alert setAlertStyle:NSAlertStyleInformational];

      switch (btnType) {
      case ORBIS_MSG_DIALOG_BUTTON_TYPE_YESNO:
      case ORBIS_MSG_DIALOG_BUTTON_TYPE_YESNO_FOCUS_NO:
        [alert addButtonWithTitle:@"Yes"];
        [alert addButtonWithTitle:@"No"];
        break;
      case ORBIS_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL:
      case ORBIS_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL_FOCUS_CANCEL:
        [alert addButtonWithTitle:@"OK"];
        [alert addButtonWithTitle:@"Cancel"];
        break;
      case ORBIS_MSG_DIALOG_BUTTON_TYPE_2BUTTONS:
        [alert addButtonWithTitle:nsBtn1];
        [alert addButtonWithTitle:nsBtn2];
        break;
      case ORBIS_MSG_DIALOG_BUTTON_TYPE_OK:
      default:
        [alert addButtonWithTitle:@"OK"];
        break;
      }

      resp = [alert runModal];
    }
  };

  if ([NSThread isMainThread]) {
    showAlert();
  } else {
    dispatch_sync(dispatch_get_main_queue(), showAlert);
  }

  pthread_mutex_lock(&g_dialog_mutex);
  if (resp == NSAlertFirstButtonReturn) {
    g_dialog_result.buttonId = ORBIS_MSG_DIALOG_BUTTON_ID_YES;
    g_dialog_result.result = ORBIS_COMMON_DIALOG_RESULT_OK;
  } else if (resp == NSAlertSecondButtonReturn) {
    g_dialog_result.buttonId = ORBIS_MSG_DIALOG_BUTTON_ID_NO;
    g_dialog_result.result = (btnType == ORBIS_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL ||
                              btnType == ORBIS_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL_FOCUS_CANCEL) ?
                             ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED : ORBIS_COMMON_DIALOG_RESULT_OK;
  } else {
    g_dialog_result.buttonId = ORBIS_MSG_DIALOG_BUTTON_ID_INVALID;
    g_dialog_result.result = ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED;
  }
  g_dialog_result.mode = g_dialog_mode;
  g_dialog_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
  OrbisCommonDialogStatus st = g_dialog_status;
  pthread_mutex_unlock(&g_dialog_mutex);

  return st;
}

int32_t sceMsgDialogClose(void) {
  pthread_mutex_lock(&g_dialog_mutex);
  g_dialog_status = ORBIS_COMMON_DIALOG_STATUS_INITIALIZED;
  pthread_mutex_unlock(&g_dialog_mutex);
  return 0;
}

int32_t sceMsgDialogGetResult(OrbisMsgDialogResult *result) {
  if (!result) return -1;
  pthread_mutex_lock(&g_dialog_mutex);
  memcpy(result, &g_dialog_result, sizeof(OrbisMsgDialogResult));
  pthread_mutex_unlock(&g_dialog_mutex);
  return 0;
}

int32_t sceMsgDialogTerminate(void) {
  pthread_mutex_lock(&g_dialog_mutex);
  g_dialog_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
  g_dialog_inited = false;
  pthread_mutex_unlock(&g_dialog_mutex);
  return 0;
}

// ---------------------------------------------------------------------------
// Shims for Guest ABI
// ---------------------------------------------------------------------------

void shim_sceCommonDialogInitialize(GuestContext *ctx) {
  ctx->rax = (uint64_t)sceCommonDialogInitialize();
  SHIM_RETURN();
}

void shim_sceCommonDialogIsUsed(GuestContext *ctx) {
  ctx->rax = (uint64_t)(sceCommonDialogIsUsed() ? 1 : 0);
  SHIM_RETURN();
}

void shim_sceMsgDialogInitialize(GuestContext *ctx) {
  ctx->rax = (uint64_t)sceMsgDialogInitialize();
  SHIM_RETURN();
}

void shim_sceMsgDialogOpen(GuestContext *ctx) {
  uint64_t param_addr = ctx->rdi;
  if (param_addr == 0) {
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  const OrbisMsgDialogParam *param = (const OrbisMsgDialogParam *)(ctx->mem_base + param_addr);
  pthread_mutex_lock(&g_dialog_mutex);
  g_dialog_msg[0] = '\0';
  g_dialog_btn1[0] = '\0';
  g_dialog_btn2[0] = '\0';
  g_dialog_button_type = ORBIS_MSG_DIALOG_BUTTON_TYPE_OK;

  if (param->mode == ORBIS_MSG_DIALOG_MODE_USER_MSG && param->userMsgParam) {
    uint64_t uParamAddr = (uint64_t)param->userMsgParam;
    const OrbisMsgDialogUserMessageParam *uParam = (const OrbisMsgDialogUserMessageParam *)(ctx->mem_base + uParamAddr);
    g_dialog_button_type = uParam->buttonType;
    if (uParam->msg) {
      uint64_t msgAddr = (uint64_t)uParam->msg;
      const char *srcMsg = (const char *)(ctx->mem_base + msgAddr);
      strncpy(g_dialog_msg, srcMsg, sizeof(g_dialog_msg) - 1);
      g_dialog_msg[sizeof(g_dialog_msg) - 1] = '\0';
    }
    if (uParam->buttonType == ORBIS_MSG_DIALOG_BUTTON_TYPE_2BUTTONS && uParam->buttonsParam) {
      uint64_t bParamAddr = (uint64_t)uParam->buttonsParam;
      const OrbisMsgDialogButtonsParam *bParam = (const OrbisMsgDialogButtonsParam *)(ctx->mem_base + bParamAddr);
      if (bParam->msg1) {
        strncpy(g_dialog_btn1, (const char *)(ctx->mem_base + (uint64_t)bParam->msg1), sizeof(g_dialog_btn1) - 1);
        g_dialog_btn1[sizeof(g_dialog_btn1) - 1] = '\0';
      }
      if (bParam->msg2) {
        strncpy(g_dialog_btn2, (const char *)(ctx->mem_base + (uint64_t)bParam->msg2), sizeof(g_dialog_btn2) - 1);
        g_dialog_btn2[sizeof(g_dialog_btn2) - 1] = '\0';
      }
    }
  }
  pthread_mutex_unlock(&g_dialog_mutex);

  ctx->rax = (uint64_t)sceMsgDialogOpen(param);
  SHIM_RETURN();
}

void shim_sceMsgDialogUpdateStatus(GuestContext *ctx) {
  ctx->rax = (uint64_t)sceMsgDialogUpdateStatus();
  SHIM_RETURN();
}

void shim_sceMsgDialogGetStatus(GuestContext *ctx) {
  ctx->rax = (uint64_t)sceMsgDialogGetStatus();
  SHIM_RETURN();
}

void shim_sceMsgDialogClose(GuestContext *ctx) {
  ctx->rax = (uint64_t)sceMsgDialogClose();
  SHIM_RETURN();
}

void shim_sceMsgDialogGetResult(GuestContext *ctx) {
  uint64_t res_addr = ctx->rdi;
  if (res_addr == 0) {
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  OrbisMsgDialogResult *res = (OrbisMsgDialogResult *)(ctx->mem_base + res_addr);
  ctx->rax = (uint64_t)sceMsgDialogGetResult(res);
  SHIM_RETURN();
}

void shim_sceMsgDialogTerminate(GuestContext *ctx) {
  ctx->rax = (uint64_t)sceMsgDialogTerminate();
  SHIM_RETURN();
}

void shim_sceMsgDialogProgressBarInc(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceMsgDialogProgressBarSetMsg(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceMsgDialogProgressBarSetValue(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}
