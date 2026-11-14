// libSceGameCustomDataDialog: the system dialog that sends game custom data (UGC) to other
// PSN users. The common dialog state machine follows shadPS4's game_custom_data_dialog.cpp.
// sceGameCustomDataDialogOpen is not provided: the dialog is a PSN feature with no host UI,
// and there is no evidence for what the console's dialog does for a signed-out user. Without
// Open the dialog never runs, so GetResult can only report that nothing finished.
#include "ps4_game_custom_data_dialog.h"
#include <pthread.h>

static pthread_mutex_t g_gcdd_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_gcdd_status = GCDD_STATUS_NONE;

int32_t ps4_game_custom_data_dialog_initialize(void) {
  pthread_mutex_lock(&g_gcdd_mutex);
  int32_t r = GCDD_ERROR_ALREADY_INITIALIZED;
  if (g_gcdd_status == GCDD_STATUS_NONE) {
    g_gcdd_status = GCDD_STATUS_INITIALIZED;
    r = 0;
  }
  pthread_mutex_unlock(&g_gcdd_mutex);
  return r;
}

int32_t ps4_game_custom_data_dialog_terminate(void) {
  pthread_mutex_lock(&g_gcdd_mutex);
  int32_t r = GCDD_ERROR_NOT_INITIALIZED;
  if (g_gcdd_status != GCDD_STATUS_NONE) {
    g_gcdd_status = GCDD_STATUS_NONE;
    r = 0;
  }
  pthread_mutex_unlock(&g_gcdd_mutex);
  return r;
}

int32_t ps4_game_custom_data_dialog_update_status(void) {
  pthread_mutex_lock(&g_gcdd_mutex);
  int32_t st = g_gcdd_status;
  pthread_mutex_unlock(&g_gcdd_mutex);
  return st;
}

int32_t ps4_game_custom_data_dialog_get_result(const uint8_t *result) {
  if (!result) return GCDD_ERROR_ARG_NULL;
  for (int i = 0; i < GCDD_RESULT_RESERVED_SIZE; i++) {
    if (result[GCDD_RESULT_RESERVED_OFFSET + i]) return GCDD_ERROR_PARAM_INVALID;
  }
  pthread_mutex_lock(&g_gcdd_mutex);
  int32_t st = g_gcdd_status;
  pthread_mutex_unlock(&g_gcdd_mutex);
  // FINISHED needs a dialog that ran, which Open (not provided) would start.
  return st == GCDD_STATUS_NONE ? GCDD_ERROR_NOT_INITIALIZED : GCDD_ERROR_NOT_FINISHED;
}

// int sceGameCustomDataDialogInitialize(void)
void shim_sceGameCustomDataDialogInitialize(GuestContext *ctx) {
  ctx->rax = (uint64_t)(int64_t)ps4_game_custom_data_dialog_initialize();
  SHIM_RETURN();
}

// int sceGameCustomDataDialogTerminate(void)
void shim_sceGameCustomDataDialogTerminate(GuestContext *ctx) {
  ctx->rax = (uint64_t)(int64_t)ps4_game_custom_data_dialog_terminate();
  SHIM_RETURN();
}

// SceCommonDialogStatus sceGameCustomDataDialogUpdateStatus(void)
void shim_sceGameCustomDataDialogUpdateStatus(GuestContext *ctx) {
  ctx->rax = (uint64_t)(int64_t)ps4_game_custom_data_dialog_update_status();
  SHIM_RETURN();
}

// int sceGameCustomDataDialogGetResult(SceGameCustomDataDialogResult *result)
void shim_sceGameCustomDataDialogGetResult(GuestContext *ctx) {
  const uint8_t *result = NULL;
  if (ctx->rdi && ctx->rdi + GCDD_RESULT_RESERVED_OFFSET + GCDD_RESULT_RESERVED_SIZE <= ctx->mem_size) {
    result = ctx->mem_base + ctx->rdi;
  }
  ctx->rax = (uint64_t)(int64_t)ps4_game_custom_data_dialog_get_result(result);
  SHIM_RETURN();
}
