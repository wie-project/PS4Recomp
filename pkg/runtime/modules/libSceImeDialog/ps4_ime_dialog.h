#ifndef PS4_IME_DIALOG_H
#define PS4_IME_DIALOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum OrbisImeDialogStatus {
    ORBIS_IME_DIALOG_STATUS_NONE     = 0,
    ORBIS_IME_DIALOG_STATUS_RUNNING  = 1,
    ORBIS_IME_DIALOG_STATUS_FINISHED = 2
} OrbisImeDialogStatus;

typedef enum OrbisImeDialogEndStatus {
    ORBIS_IME_DIALOG_END_STATUS_OK            = 0,
    ORBIS_IME_DIALOG_END_STATUS_USER_CANCELED = 1,
    ORBIS_IME_DIALOG_END_STATUS_ABORTED       = 2
} OrbisImeDialogEndStatus;

typedef enum OrbisImeType {
    ORBIS_IME_TYPE_DEFAULT     = 0,
    ORBIS_IME_TYPE_BASIC_LATIN = 1,
    ORBIS_IME_TYPE_URL         = 2,
    ORBIS_IME_TYPE_MAIL        = 3,
    ORBIS_IME_TYPE_NUMBER      = 4
} OrbisImeType;

typedef enum OrbisImeOption {
    ORBIS_IME_OPTION_DEFAULT                 = 0,
    ORBIS_IME_OPTION_MULTILINE               = 1 << 0,
    ORBIS_IME_OPTION_NO_AUTO_CAPITALIZATION  = 1 << 1,
    ORBIS_IME_OPTION_PASSWORD                = 1 << 2,
    ORBIS_IME_OPTION_LANGUAGES_FORCED        = 1 << 3,
    ORBIS_IME_OPTION_EXT_KEYBOARD            = 1 << 4,
    ORBIS_IME_OPTION_NO_LEARNING             = 1 << 5,
    ORBIS_IME_OPTION_FIXED_POSITION          = 1 << 6,
    ORBIS_IME_OPTION_DISABLE_COPY_PASTE      = 1 << 7,
    ORBIS_IME_OPTION_DISABLE_RESUME          = 1 << 8,
    ORBIS_IME_OPTION_DISABLE_AUTO_SPACE      = 1 << 9,
    ORBIS_IME_OPTION_DISABLE_POSITION_ADJUST = 1 << 11,
    ORBIS_IME_OPTION_EXPANDED_PREEDIT_BUFFER = 1 << 12,
    ORBIS_IME_OPTION_USE_OVER_2K_COORDINATES = 1 << 14
} OrbisImeOption;

typedef struct OrbisImeDialogResult {
    OrbisImeDialogEndStatus endstatus;
    int8_t reserved[12];
} OrbisImeDialogResult;

typedef struct OrbisImeDialogParam {
    int32_t userId;
    uint32_t type;
    uint64_t supported_languages;
    int32_t enter_label;
    int32_t input_method;
    void *filter;
    uint32_t option;
    uint32_t max_text_length;
    uint16_t *input_text_buffer;
    float posx;
    float posy;
    int32_t horizontal_alignment;
    int32_t vertical_alignment;
    const uint16_t *placeholder;
    const uint16_t *title;
    int8_t reserved[16];
} OrbisImeDialogParam;

typedef struct OrbisImeParamExtended {
    uint32_t option;
    uint8_t color_base[4];
    uint8_t color_line[4];
    uint8_t color_text_field[4];
    uint8_t color_preedit[4];
    uint8_t color_button_default[4];
    uint8_t color_button_function[4];
    uint8_t color_button_symbol[4];
    uint8_t color_text[4];
    uint8_t color_special[4];
    uint32_t priority;
    char *additional_dictionary_path;
    void *ext_keyboard_filter;
    uint32_t disable_device;
    uint32_t ext_keyboard_mode;
    int8_t reserved[60];
} OrbisImeParamExtended;

// C API
int32_t sceImeDialogInit(OrbisImeDialogParam *param, OrbisImeParamExtended *extended);
OrbisImeDialogStatus sceImeDialogGetStatus(void);
int32_t sceImeDialogGetResult(OrbisImeDialogResult *result);
int32_t sceImeDialogAbort(void);
int32_t sceImeDialogTerm(void);
int32_t sceImeDialogGetPanelSizeExtended(const OrbisImeDialogParam *param,
                                        const OrbisImeParamExtended *extended,
                                        uint32_t *width, uint32_t *height);

// Shims
void shim_sceImeDialogInit(GuestContext *ctx);
void shim_sceImeDialogGetStatus(GuestContext *ctx);
void shim_sceImeDialogGetResult(GuestContext *ctx);
void shim_sceImeDialogAbort(GuestContext *ctx);
void shim_sceImeDialogTerm(GuestContext *ctx);
void shim_sceImeDialogGetPanelSizeExtended(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_IME_DIALOG_H
