#ifndef PS4_SHAREPLAY_H
#define PS4_SHAREPLAY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// C API
int32_t sceSharePlayInitialize(void);
int32_t sceSharePlayTerminate(void);
int32_t sceSharePlaySetProhibition(int32_t prohibited);

// Shims
void shim_sceSharePlayInitialize(GuestContext *ctx);
void shim_sceSharePlayTerminate(GuestContext *ctx);
void shim_sceSharePlaySetProhibition(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SHAREPLAY_H
