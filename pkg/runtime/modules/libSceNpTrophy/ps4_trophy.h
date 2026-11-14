#ifndef PS4_TROPHY_H
#define PS4_TROPHY_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// libSceNpTrophy error codes (shadPS4 np_error.h).
#define ORBIS_NP_TROPHY_ERROR_INVALID_ARGUMENT ((int32_t)0x80551604)
#define ORBIS_NP_TROPHY_ERROR_INSUFFICIENT_BUFFER ((int32_t)0x80551605)
#define ORBIS_NP_TROPHY_ERROR_INVALID_HANDLE ((int32_t)0x80551608)
#define ORBIS_NP_TROPHY_ERROR_INVALID_CONTEXT ((int32_t)0x80551609)
#define ORBIS_NP_TROPHY_ERROR_INVALID_TROPHY_ID ((int32_t)0x8055160A)
#define ORBIS_NP_TROPHY_ERROR_INVALID_GROUP_ID ((int32_t)0x8055160B)
#define ORBIS_NP_TROPHY_ERROR_NOT_REGISTERED ((int32_t)0x8055160F)
#define ORBIS_NP_TROPHY_ERROR_ICON_FILE_NOT_FOUND ((int32_t)0x80551614)
#define ORBIS_NP_TROPHY_ERROR_INVALID_TRP_FILE_FORMAT ((int32_t)0x80551616)
#define ORBIS_NP_TROPHY_ERROR_TROPHY_NOT_UNLOCKED ((int32_t)0x8055161A)
#define ORBIS_NP_TROPHY_NUM_MAX 128

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
// Icons from the trophy pack: buffer NULL asks for the size (see ps4_trophy.c).
int32_t sceNpTrophyGetGameIcon(int32_t context, int32_t handle, void *buffer, uint64_t *size);
int32_t sceNpTrophyGetGroupIcon(int32_t context, int32_t handle, int32_t group_id, void *buffer,
                                uint64_t *size);
int32_t sceNpTrophyGetTrophyIcon(int32_t context, int32_t handle, int32_t trophy_id, void *buffer,
                                 uint64_t *size);

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
void shim_sceNpTrophyGetGameInfo(GuestContext *ctx);
void shim_sceNpTrophyGetTrophyInfo(GuestContext *ctx);
void shim_sceNpTrophyGetGameIcon(GuestContext *ctx);
void shim_sceNpTrophyGetGroupIcon(GuestContext *ctx);
void shim_sceNpTrophyGetTrophyIcon(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_TROPHY_H
