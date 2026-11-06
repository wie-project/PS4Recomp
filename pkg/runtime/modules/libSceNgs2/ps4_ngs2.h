// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PS4_NGS2_H
#define PS4_NGS2_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GuestContext GuestContext;

#define ORBIS_NGS2_SYSTEM_NAME_LENGTH 16
#define ORBIS_NGS2_RACK_NAME_LENGTH 16

typedef uint64_t OrbisNgs2Handle;

typedef struct OrbisNgs2ContextBufferInfo {
    uint64_t hostBuffer;
    uint64_t hostBufferSize;
    uint64_t reserved[5];
    uint64_t userData;
} OrbisNgs2ContextBufferInfo;

typedef struct OrbisNgs2SystemOption {
    uint64_t size;
    char name[ORBIS_NGS2_SYSTEM_NAME_LENGTH];
    uint32_t flags;
    uint32_t maxGrainSamples;
    uint32_t numGrainSamples;
    uint32_t sampleRate;
    uint32_t aReserved[6];
} OrbisNgs2SystemOption;

typedef struct OrbisNgs2BufferAllocator {
    uint64_t allocHandler;
    uint64_t freeHandler;
    uint64_t userData;
} OrbisNgs2BufferAllocator;

typedef struct OrbisNgs2SystemInfo {
    char name[ORBIS_NGS2_SYSTEM_NAME_LENGTH];
    OrbisNgs2Handle systemHandle;
    OrbisNgs2ContextBufferInfo bufferInfo;
    uint32_t uid;
    uint32_t minGrainSamples;
    uint32_t maxGrainSamples;
    uint32_t stateFlags;
    uint32_t rackCount;
    float lastRenderRatio;
    int64_t lastRenderTick;
    int64_t renderCount;
    uint32_t sampleRate;
    uint32_t numGrainSamples;
} OrbisNgs2SystemInfo;

typedef struct OrbisNgs2RackInfo {
    char name[ORBIS_NGS2_RACK_NAME_LENGTH];
    OrbisNgs2Handle rackHandle;
    OrbisNgs2ContextBufferInfo bufferInfo;
    OrbisNgs2Handle ownerSystemHandle;
    uint32_t type;
    uint32_t rackId;
    uint32_t uid;
    uint32_t minGrainSamples;
    uint32_t maxGrainSamples;
    uint32_t maxVoices;
    uint32_t maxChannelWorks;
    uint32_t maxInputs;
    uint32_t maxMatrices;
    uint32_t maxPorts;
    uint32_t stateFlags;
    float lastProcessRatio;
    uint64_t lastProcessTick;
    uint64_t renderCount;
    uint32_t activeVoiceCount;
    uint32_t activeChannelWorkCount;
} OrbisNgs2RackInfo;

typedef struct OrbisNgs2RackOption {
    uint64_t size;
    char name[ORBIS_NGS2_RACK_NAME_LENGTH];
    uint32_t flags;
    uint32_t maxGrainSamples;
    uint32_t maxVoices;
    uint32_t maxInputDelayBlocks;
    uint32_t maxMatrices;
    uint32_t maxPorts;
    uint32_t aReserved[20];
} OrbisNgs2RackOption;

typedef struct OrbisNgs2RenderBufferInfo {
    uint64_t buffer;
    uint64_t bufferSize;
    uint32_t waveformType;
    uint32_t numChannels;
} OrbisNgs2RenderBufferInfo;

typedef struct OrbisNgs2VoiceParamHeader {
    uint16_t size;
    int16_t next;
    uint32_t id;
} OrbisNgs2VoiceParamHeader;

typedef struct OrbisNgs2VoiceState {
    uint32_t stateFlags;
} OrbisNgs2VoiceState;

typedef struct OrbisNgs2WaveformFormat {
    uint32_t waveformType;
    uint32_t numChannels;
    uint32_t sampleRate;
    uint32_t configData;
    uint32_t frameOffset;
    uint32_t frameMargin;
} OrbisNgs2WaveformFormat;

typedef struct OrbisNgs2WaveformBlock {
    uint32_t dataOffset;
    uint32_t dataSize;
    uint32_t numRepeats;
    uint32_t numSkipSamples;
    uint32_t numSamples;
    uint32_t reserved;
    uint64_t userData;
} OrbisNgs2WaveformBlock;

typedef struct OrbisNgs2WaveformInfo {
    OrbisNgs2WaveformFormat format;
    uint32_t dataOffset;
    uint32_t dataSize;
    uint32_t loopBeginPosition;
    uint32_t loopEndPosition;
    uint32_t numSamples;
    uint32_t audioUnitSize;
    uint32_t numAudioUnitSamples;
    uint32_t numAudioUnitPerFrame;
    uint32_t audioFrameSize;
    uint32_t numAudioFrameSamples;
    uint32_t numDelaySamples;
    uint32_t numBlocks;
    OrbisNgs2WaveformBlock aBlock[4];
} OrbisNgs2WaveformInfo;

// Error codes
#define ORBIS_NGS2_ERROR_FAIL 0x804A0001
#define ORBIS_NGS2_ERROR_INVALID_MAX_GRAIN_SAMPLES 0x804A0050
#define ORBIS_NGS2_ERROR_INVALID_NUM_GRAIN_SAMPLES 0x804A0051
#define ORBIS_NGS2_ERROR_INVALID_NUM_CHANNELS 0x804A0052
#define ORBIS_NGS2_ERROR_INVALID_OUT_ADDRESS 0x804A0053
#define ORBIS_NGS2_ERROR_INVALID_OUT_SIZE 0x804A0054
#define ORBIS_NGS2_ERROR_INVALID_OPTION_ADDRESS 0x804A0080
#define ORBIS_NGS2_ERROR_INVALID_OPTION_SIZE 0x804A0081
#define ORBIS_NGS2_ERROR_INVALID_OPTION_FLAG 0x804A0082
#define ORBIS_NGS2_ERROR_INVALID_MAX_MATRICES 0x804A0100
#define ORBIS_NGS2_ERROR_INVALID_MAX_PORTS 0x804A0101
#define ORBIS_NGS2_ERROR_INVALID_MAX_INPUT_DELAY_BLOCKS 0x804A0102
#define ORBIS_NGS2_ERROR_INVALID_MAX_VOICES 0x804A0103
#define ORBIS_NGS2_ERROR_INVALID_MAX_CHANNELS 0x804A0104
#define ORBIS_NGS2_ERROR_INVALID_MAX_CHANNEL_WORKS 0x804A0105
#define ORBIS_NGS2_ERROR_INVALID_MAX_WAVEFORM_BLOCKS 0x804A0106
#define ORBIS_NGS2_ERROR_INVALID_MAX_ENVELOPE_POINTS 0x804A0107
#define ORBIS_NGS2_ERROR_INVALID_MAX_FILTERS 0x804A0108
#define ORBIS_NGS2_ERROR_INVALID_MAX_CODEC_CACHES 0x804A0109
#define ORBIS_NGS2_ERROR_INVALID_MAX_ATRAC9_DECODERS 0x804A010A
#define ORBIS_NGS2_ERROR_INVALID_MAX_ATRAC9_CHANNEL_WORKS 0x804A010B
#define ORBIS_NGS2_ERROR_INVALID_MAX_INPUTS 0x804A010C
#define ORBIS_NGS2_ERROR_INVALID_HANDLE 0x804A0200
#define ORBIS_NGS2_ERROR_INVALID_SAMPLE_RATE 0x804A0201
#define ORBIS_NGS2_ERROR_INVALID_BUFFER_INFO 0x804A0206
#define ORBIS_NGS2_ERROR_INVALID_BUFFER_ADDRESS 0x804A0207
#define ORBIS_NGS2_ERROR_INVALID_BUFFER_SIZE 0x804A0209
#define ORBIS_NGS2_ERROR_INVALID_BUFFER_ALLOCATOR 0x804A020A
#define ORBIS_NGS2_ERROR_EMPTY_BUFFER 0x804A020C
#define ORBIS_NGS2_ERROR_INVALID_SYSTEM_HANDLE 0x804A0230
#define ORBIS_NGS2_ERROR_INVALID_RACK_ID 0x804A0260
#define ORBIS_NGS2_ERROR_INVALID_RACK_HANDLE 0x804A0261
#define ORBIS_NGS2_ERROR_INVALID_VOICE_HANDLE 0x804A0300
#define ORBIS_NGS2_ERROR_INVALID_VOICE_INDEX 0x804A0302

// Shims
void shim_sceNgs2SystemCreateWithAllocator(GuestContext *ctx);
void shim_sceNgs2SystemDestroy(GuestContext *ctx);
void shim_sceNgs2SystemSetGrainSamples(GuestContext *ctx);
void shim_sceNgs2SystemRender(GuestContext *ctx);
void shim_sceNgs2RackCreateWithAllocator(GuestContext *ctx);
void shim_sceNgs2RackDestroy(GuestContext *ctx);
void shim_sceNgs2RackGetVoiceHandle(GuestContext *ctx);
void shim_sceNgs2VoiceControl(GuestContext *ctx);
void shim_sceNgs2VoiceGetState(GuestContext *ctx);
void shim_sceNgs2VoiceGetStateFlags(GuestContext *ctx);
void shim_sceNgs2ParseWaveformData(GuestContext *ctx);
void shim_sceNgs2CalcWaveformBlock(GuestContext *ctx);

void shim_sceNgs2SystemCreate(GuestContext *ctx);
void shim_sceNgs2RackCreate(GuestContext *ctx);
void shim_sceNgs2SystemQueryBufferSize(GuestContext *ctx);
void shim_sceNgs2RackQueryBufferSize(GuestContext *ctx);
void shim_sceNgs2SystemGetInfo(GuestContext *ctx);
void shim_sceNgs2RackGetInfo(GuestContext *ctx);
void shim_sceNgs2SystemLock(GuestContext *ctx);
void shim_sceNgs2SystemUnlock(GuestContext *ctx);
void shim_sceNgs2RackLock(GuestContext *ctx);
void shim_sceNgs2RackUnlock(GuestContext *ctx);
void shim_sceNgs2VoiceGetOwner(GuestContext *ctx);
void shim_sceNgs2SystemSetUserData(GuestContext *ctx);
void shim_sceNgs2SystemGetUserData(GuestContext *ctx);
void shim_sceNgs2RackSetUserData(GuestContext *ctx);
void shim_sceNgs2RackGetUserData(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_NGS2_H
