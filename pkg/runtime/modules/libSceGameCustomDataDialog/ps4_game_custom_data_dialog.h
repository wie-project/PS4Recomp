#ifndef PS4_GAME_CUSTOM_DATA_DIALOG_H
#define PS4_GAME_CUSTOM_DATA_DIALOG_H
#include <stdint.h>
#include "recomp_runtime.h"

// Common dialog status values and error codes (shadPS4 system/commondialog.h).
#define GCDD_STATUS_NONE 0
#define GCDD_STATUS_INITIALIZED 1
#define GCDD_STATUS_RUNNING 2
#define GCDD_STATUS_FINISHED 3
#define GCDD_ERROR_NOT_INITIALIZED ((int32_t)0x80B80003)
#define GCDD_ERROR_ALREADY_INITIALIZED ((int32_t)0x80B80004)
#define GCDD_ERROR_NOT_FINISHED ((int32_t)0x80B80005)
#define GCDD_ERROR_PARAM_INVALID ((int32_t)0x80B8000A)
#define GCDD_ERROR_ARG_NULL ((int32_t)0x80B8000D)

// SceGameCustomDataDialogResult: callbackArg, errorCode, result, sentOnlineIds, reserved[32].
#define GCDD_RESULT_RESERVED_OFFSET 0x18
#define GCDD_RESULT_RESERVED_SIZE 32

int32_t ps4_game_custom_data_dialog_initialize(void);
int32_t ps4_game_custom_data_dialog_terminate(void);
int32_t ps4_game_custom_data_dialog_update_status(void);
int32_t ps4_game_custom_data_dialog_get_result(const uint8_t *result);

#endif
