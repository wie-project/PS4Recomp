// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_VOICE_H
#define PS4_VOICE_H

#include <stdbool.h>
#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_VOICE_ERROR_NOT_INIT ((int32_t)0x804E0801)
#define ORBIS_VOICE_ERROR_ALREADY_INIT ((int32_t)0x804E0802)
#define ORBIS_VOICE_ERROR_PORT_INVALID ((int32_t)0x804E0804)
#define ORBIS_VOICE_ERROR_ARGUMENT_INVALID ((int32_t)0x804E0805)
#define ORBIS_VOICE_ERROR_TOPOLOGY ((int32_t)0x804E0807)
#define ORBIS_VOICE_ERROR_RESOURCE_INSUFFICIENT ((int32_t)0x804E0808)

#define ORBIS_VOICE_MAX_PORT 64

#define ORBIS_VOICE_PORTTYPE_IN_DEVICE 0
#define ORBIS_VOICE_PORTTYPE_IN_PCMAUDIO 1
#define ORBIS_VOICE_PORTTYPE_IN_VOICE 2
#define ORBIS_VOICE_PORTTYPE_OUT_PCMAUDIO 3
#define ORBIS_VOICE_PORTTYPE_OUT_VOICE 4
#define ORBIS_VOICE_PORTTYPE_OUT_DEVICE 5

#define ORBIS_VOICE_ATTR_1000 1000
#define ORBIS_VOICE_ATTR_1001 1001
#define ORBIS_VOICE_ATTR_1002 1002 // the microphone's silent state (an AudioIn silent state)
#define ORBIS_VOICE_ATTR_1003 1003
#define ORBIS_VOICE_ATTR_2000 2000
#define ORBIS_VOICE_ATTR_2001 2001
#define ORBIS_VOICE_ATTR_2002 2002

typedef struct OrbisVoicePortInfo {
    int32_t port_type;
    int32_t state;
    uint64_t edge; // uint32_t * (guest)
    uint32_t byte_count;
    uint32_t frame_size;
    uint16_t edge_count;
    uint16_t reserved;
} OrbisVoicePortInfo;

typedef struct OrbisVoicePortParam {
    int32_t type;
    int32_t reserved0;
    uint64_t reserved1;
    uint64_t reserved2;
} OrbisVoicePortParam;

typedef struct OrbisVoiceInitParam {
    uint64_t app_type;
    uint64_t reserved[4];
} OrbisVoiceInitParam;

typedef struct OrbisVoiceStartParam {
    uint64_t memory; // guest address
    uint32_t memory_size;
    uint32_t reserved0;
    uint64_t reserved1;
    uint64_t reserved2;
} OrbisVoiceStartParam;

typedef struct OrbisVoiceResourceInfo {
    uint16_t field_0;
    uint16_t field_2;
    uint16_t field_4;
    uint16_t field_6;
} OrbisVoiceResourceInfo;

_Static_assert(sizeof(OrbisVoicePortInfo) == 32, "OrbisVoicePortInfo layout");
_Static_assert(sizeof(OrbisVoicePortParam) == 0x18, "OrbisVoicePortParam layout");
_Static_assert(sizeof(OrbisVoiceInitParam) == 0x28, "OrbisVoiceInitParam layout");
_Static_assert(sizeof(OrbisVoiceStartParam) == 0x20, "OrbisVoiceStartParam layout");
_Static_assert(sizeof(OrbisVoiceResourceInfo) == 8, "OrbisVoiceResourceInfo layout");

int32_t sceVoiceInit(const OrbisVoiceInitParam *param, int32_t container);
int32_t sceVoiceInitHQ(const OrbisVoiceInitParam *param, int32_t container);
int32_t sceVoiceEnd(void);
int32_t sceVoiceStart(const OrbisVoiceStartParam *param);
int32_t sceVoiceStop(void);
int32_t sceVoiceCreatePort(int32_t *port_id, const OrbisVoicePortParam *param);
int32_t sceVoiceUpdatePort(uint32_t port_id, const OrbisVoicePortParam *param);
int32_t sceVoiceDeletePort(uint32_t port_id);
int32_t sceVoiceConnectIPortToOPort(uint32_t in_port_id, uint32_t out_port_id);
int32_t sceVoiceDisconnectIPortFromOPort(uint32_t in_port_id, uint32_t out_port_id);
int32_t sceVoiceGetPortInfo(uint32_t port_id, OrbisVoicePortInfo *info);
int32_t sceVoiceGetPortAttr(uint32_t port_id, int32_t attr_id, void *value, uint32_t size);
int32_t sceVoiceGetResourceInfo(OrbisVoiceResourceInfo *info);
int32_t sceVoiceGetBitRate(uint32_t port_id, uint32_t *bitrate);
int32_t sceVoiceSetBitRate(uint32_t port_id, uint32_t bitrate);
int32_t sceVoiceGetMuteFlag(uint32_t port_id, bool *mute);
int32_t sceVoiceSetMuteFlag(uint32_t port_id, bool mute);
int32_t sceVoiceSetMuteFlagAll(bool mute);
int32_t sceVoiceGetVolume(uint32_t port_id, float *volume);
int32_t sceVoiceSetVolume(uint32_t port_id, float volume);
int32_t sceVoicePausePort(uint32_t port_id);
int32_t sceVoicePausePortAll(void);
int32_t sceVoiceResumePort(uint32_t port_id);
int32_t sceVoiceResumePortAll(void);
int32_t sceVoiceResetPort(uint32_t port_id);
int32_t sceVoiceReadFromOPort(uint32_t port_id, void *data, uint32_t *size);
int32_t sceVoiceWriteToIPort(uint32_t port_id, const void *data, uint32_t *size, int16_t frame_gaps);
int32_t sceVoiceVADSetVersion(int32_t version);

void shim_sceVoiceInit(GuestContext *ctx);
void shim_sceVoiceInitHQ(GuestContext *ctx);
void shim_sceVoiceEnd(GuestContext *ctx);
void shim_sceVoiceStart(GuestContext *ctx);
void shim_sceVoiceStop(GuestContext *ctx);
void shim_sceVoiceCreatePort(GuestContext *ctx);
void shim_sceVoiceUpdatePort(GuestContext *ctx);
void shim_sceVoiceDeletePort(GuestContext *ctx);
void shim_sceVoiceConnectIPortToOPort(GuestContext *ctx);
void shim_sceVoiceDisconnectIPortFromOPort(GuestContext *ctx);
void shim_sceVoiceGetPortInfo(GuestContext *ctx);
void shim_sceVoiceGetPortAttr(GuestContext *ctx);
void shim_sceVoiceGetResourceInfo(GuestContext *ctx);
void shim_sceVoiceGetBitRate(GuestContext *ctx);
void shim_sceVoiceSetBitRate(GuestContext *ctx);
void shim_sceVoiceGetMuteFlag(GuestContext *ctx);
void shim_sceVoiceSetMuteFlag(GuestContext *ctx);
void shim_sceVoiceSetMuteFlagAll(GuestContext *ctx);
void shim_sceVoiceGetVolume(GuestContext *ctx);
void shim_sceVoiceSetVolume(GuestContext *ctx);
void shim_sceVoicePausePort(GuestContext *ctx);
void shim_sceVoicePausePortAll(GuestContext *ctx);
void shim_sceVoiceResumePort(GuestContext *ctx);
void shim_sceVoiceResumePortAll(GuestContext *ctx);
void shim_sceVoiceResetPort(GuestContext *ctx);
void shim_sceVoiceReadFromOPort(GuestContext *ctx);
void shim_sceVoiceWriteToIPort(GuestContext *ctx);
void shim_sceVoiceVADSetVersion(GuestContext *ctx);
void shim_sceVoiceVADAdjustment(GuestContext *ctx);
void shim_sceVoiceSetThreadsParams(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_VOICE_H
