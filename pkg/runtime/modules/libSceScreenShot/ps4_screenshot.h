#ifndef PS4_SCREENSHOT_H
#define PS4_SCREENSHOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// C API
int32_t sceScreenShotEnable(void);
int32_t sceScreenShotDisable(void);
int32_t sceScreenShotSetOverlayImage(const char *filePath, int32_t posX, int32_t posY);
int32_t sceScreenShotSetOverlayImageWithOrigin(const char *filePath, int32_t posX, int32_t posY, int32_t origin);

// Shims
void shim_sceScreenShotEnable(GuestContext *ctx);
void shim_sceScreenShotDisable(GuestContext *ctx);
void shim_sceScreenShotSetOverlayImage(GuestContext *ctx);
void shim_sceScreenShotSetOverlayImageWithOrigin(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SCREENSHOT_H
