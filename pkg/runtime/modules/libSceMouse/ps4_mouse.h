#ifndef PS4_MOUSE_H
#define PS4_MOUSE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OrbisMouseData {
    uint64_t timestamp;
    uint8_t connected;
    uint8_t pad[3];
    uint32_t buttons;
    int32_t x_axis;
    int32_t y_axis;
    int32_t wheel;
    int32_t tilt;
    uint8_t reserve[8];
} OrbisMouseData;

// C API
int32_t sceMouseInit(void);
int32_t sceMouseOpen(int32_t userId, int32_t type, int32_t index, const void *pParam);
int32_t sceMouseClose(int32_t handle);
int32_t sceMouseRead(int32_t handle, OrbisMouseData *pData, int32_t num);

// Shims
void shim_sceMouseInit(GuestContext *ctx);
void shim_sceMouseOpen(GuestContext *ctx);
void shim_sceMouseClose(GuestContext *ctx);
void shim_sceMouseRead(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_MOUSE_H
