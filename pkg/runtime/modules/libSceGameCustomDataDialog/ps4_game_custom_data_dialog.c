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

int32_t ps4_game_custom_data_dialog_open(const void *param) {
  if (!param) return GCDD_ERROR_ARG_NULL;
  pthread_mutex_lock(&g_gcdd_mutex);
  int32_t r = 0;
  if (g_gcdd_status == GCDD_STATUS_INITIALIZED || g_gcdd_status == GCDD_STATUS_FINISHED) {
    g_gcdd_status = GCDD_STATUS_RUNNING;
  } else if (g_gcdd_status == GCDD_STATUS_NONE) {
    r = GCDD_ERROR_NOT_INITIALIZED;
  } else {
    r = GCDD_ERROR_INVALID_STATE;
  }
  pthread_mutex_unlock(&g_gcdd_mutex);
  return r;
}

int32_t ps4_game_custom_data_dialog_close(void) {
  pthread_mutex_lock(&g_gcdd_mutex);
  int32_t r = 0;
  if (g_gcdd_status == GCDD_STATUS_NONE) {
    r = GCDD_ERROR_NOT_INITIALIZED;
  } else if (g_gcdd_status != GCDD_STATUS_RUNNING) {
    r = GCDD_ERROR_NOT_RUNNING;
  } else {
    g_gcdd_status = GCDD_STATUS_FINISHED;
  }
  pthread_mutex_unlock(&g_gcdd_mutex);
  return r;
}

int32_t ps4_game_custom_data_dialog_update_status(void) {
  pthread_mutex_lock(&g_gcdd_mutex);
  if (g_gcdd_status == GCDD_STATUS_RUNNING) {
    g_gcdd_status = GCDD_STATUS_FINISHED;
  }
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
  if (st == GCDD_STATUS_NONE) return GCDD_ERROR_NOT_INITIALIZED;
  if (st != GCDD_STATUS_FINISHED) return GCDD_ERROR_NOT_FINISHED;

  *(int32_t *)(result + 0x08) = 1; // USER_CANCELED
  return 0;
}

int32_t sceGameCustomDataDialogInitialize(void) {
  return ps4_game_custom_data_dialog_initialize();
}

int32_t sceGameCustomDataDialogOpen(const void *param) {
  return ps4_game_custom_data_dialog_open(param);
}

int32_t sceGameCustomDataDialogUpdateStatus(void) {
  return ps4_game_custom_data_dialog_update_status();
}

int32_t sceGameCustomDataDialogGetResult(const void *result) {
  return ps4_game_custom_data_dialog_get_result((const uint8_t *)result);
}

int32_t sceGameCustomDataDialogClose(void) {
  return ps4_game_custom_data_dialog_close();
}

int32_t sceGameCustomDataDialogTerminate(void) {
  return ps4_game_custom_data_dialog_terminate();
}
