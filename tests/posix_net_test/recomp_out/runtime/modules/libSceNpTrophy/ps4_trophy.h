#ifndef PS4_TROPHY_H
#define PS4_TROPHY_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Trophy Management APIs
int32_t sceNpTrophyInit(void *params);
int32_t sceNpTrophyTerm(void);
int32_t sceNpTrophyCreateContext(int32_t *context, int32_t user, uint32_t unk, uint64_t unk2);
int32_t sceNpTrophyDestroyContext(int32_t context);
int32_t sceNpTrophyCreateHandle(int32_t *handle);
int32_t sceNpTrophyDestroyHandle(int32_t handle);
int32_t sceNpTrophyRegisterContext(int32_t context, int32_t handle, uint64_t unk);
int32_t sceNpTrophyUnlockTrophy(int32_t context, int32_t handle, int32_t trophyId, int32_t *platId);
int32_t sceNpTrophyShowTrophyList(int32_t context, int32_t handle);

// Guest ABI Shims
void shim_sceNpTrophyInit(GuestContext *ctx);
void shim_sceNpTrophyTerm(GuestContext *ctx);
void shim_sceNpTrophyCreateContext(GuestContext *ctx);
void shim_sceNpTrophyDestroyContext(GuestContext *ctx);
void shim_sceNpTrophyCreateHandle(GuestContext *ctx);
void shim_sceNpTrophyDestroyHandle(GuestContext *ctx);
void shim_sceNpTrophyRegisterContext(GuestContext *ctx);
void shim_sceNpTrophyUnlockTrophy(GuestContext *ctx);
void shim_sceNpTrophyShowTrophyList(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_TROPHY_H
