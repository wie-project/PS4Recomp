#ifndef PS4_SOCIAL_SCREEN_H
#define PS4_SOCIAL_SCREEN_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t sceSocialScreenInitialize(void);
int32_t sceSocialScreenTerminate(void);
int32_t sceSocialScreenSetMode(int32_t mode);
int32_t sceSocialScreenInitializeSeparateModeParameter(void *param);
int32_t sceSocialScreenOpenSeparateMode(void *param);
int32_t sceSocialScreenCloseSeparateMode(void);
int32_t sceSocialScreenConfigureSeparateMode(void *param);

void shim_sceSocialScreenInitialize(GuestContext *ctx);
void shim_sceSocialScreenTerminate(GuestContext *ctx);
void shim_sceSocialScreenSetMode(GuestContext *ctx);
void shim_sceSocialScreenInitializeSeparateModeParameter(GuestContext *ctx);
void shim_sceSocialScreenOpenSeparateMode(GuestContext *ctx);
void shim_sceSocialScreenCloseSeparateMode(GuestContext *ctx);
void shim_sceSocialScreenConfigureSeparateMode(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SOCIAL_SCREEN_H
