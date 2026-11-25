#ifndef PS4_MOUSE_H
#define PS4_MOUSE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_MOUSE_ERROR_INVALID_ARG ((int32_t)0x80DF0001)
#define ORBIS_MOUSE_ERROR_INVALID_HANDLE ((int32_t)0x80DF0003)
#define ORBIS_MOUSE_ERROR_ALREADY_OPENED ((int32_t)0x80DF0004)
#define ORBIS_MOUSE_ERROR_NOT_INITIALIZED ((int32_t)0x80DF0005)
#define ORBIS_MOUSE_ERROR_FATAL ((int32_t)0x80DF00FF)

#define ORBIS_MOUSE_MAX_DATA_NUM 64

#define ORBIS_MOUSE_BUTTON_PRIMARY (1u << 0)
#define ORBIS_MOUSE_BUTTON_SECONDARY (1u << 1)
#define ORBIS_MOUSE_BUTTON_OPTIONAL (1u << 2)
#define ORBIS_MOUSE_BUTTON_OPTIONAL2 (1u << 3)
#define ORBIS_MOUSE_BUTTON_OPTIONAL3 (1u << 4)

// OrbisMouseOpenParam.behaviorFlag: one merged handle receives the input of every mouse.
#define ORBIS_MOUSE_OPEN_PARAM_MERGED 0x01

typedef struct OrbisMouseData {
    uint64_t timestamp;
    bool connected;
    uint32_t buttons;
    int32_t x_axis;
    int32_t y_axis;
    int32_t wheel;
    int32_t tilt;
    uint8_t reserve[8];
} OrbisMouseData;
_Static_assert(sizeof(OrbisMouseData) == 0x28, "OrbisMouseData layout");

typedef struct OrbisMouseOpenParam {
    uint8_t behaviorFlag;
    uint8_t reserve[7];
} OrbisMouseOpenParam;
_Static_assert(sizeof(OrbisMouseOpenParam) == 8, "OrbisMouseOpenParam layout");

// C API
int32_t sceMouseInit(void);
int32_t sceMouseOpen(int32_t userId, int32_t type, int32_t index, const OrbisMouseOpenParam *pParam);
int32_t sceMouseClose(int32_t handle);
int32_t sceMouseRead(int32_t handle, OrbisMouseData *pData, int32_t num);

// Host input. The host mouse is used as a PS4 mouse only when PS4_MOUSE=1 is set (shadPS4's
// use_mice_as_mice, off by default); otherwise reads report a disconnected mouse. The window
// event pump feeds the host pointer through these functions; macOS reports one merged pointer,
// so it drives mouse port 0.
bool ps4_mouse_host_enabled(void);
// True once after sceMouseInit enabled host input: the window must capture the pointer
// (hidden cursor, relative motion), as shadPS4 does with SDL relative mouse mode.
bool ps4_mouse_take_capture_request(void);
void ps4_mouse_host_motion(int32_t dx, int32_t dy);
void ps4_mouse_host_button(uint32_t orbis_button, bool down);
void ps4_mouse_host_wheel(int32_t wheel, int32_t tilt);

// Shims
void shim_sceMouseInit(GuestContext *ctx);
void shim_sceMouseOpen(GuestContext *ctx);
void shim_sceMouseClose(GuestContext *ctx);
void shim_sceMouseRead(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_MOUSE_H
