// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_AJM_H
#define PS4_AJM_H

#include "recomp_runtime.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_AT9_CONFIG_DATA_SIZE 4
#define AJM_INSTANCE_STATISTICS 0x80000

#define ORBIS_AJM_ERROR_UNKNOWN                   0x80930001
#define ORBIS_AJM_ERROR_INVALID_CONTEXT           0x80930002
#define ORBIS_AJM_ERROR_INVALID_INSTANCE          0x80930003
#define ORBIS_AJM_ERROR_INVALID_BATCH             0x80930004
#define ORBIS_AJM_ERROR_INVALID_PARAMETER         0x80930005
#define ORBIS_AJM_ERROR_OUT_OF_MEMORY             0x80930006
#define ORBIS_AJM_ERROR_OUT_OF_RESOURCES          0x80930007
#define ORBIS_AJM_ERROR_CODEC_NOT_SUPPORTED       0x80930008
#define ORBIS_AJM_ERROR_CODEC_ALREADY_REGISTERED   0x80930009
#define ORBIS_AJM_ERROR_CODEC_NOT_REGISTERED       0x8093000A
#define ORBIS_AJM_ERROR_WRONG_REVISION_FLAG       0x8093000B
#define ORBIS_AJM_ERROR_FLAG_NOT_SUPPORTED        0x8093000C
#define ORBIS_AJM_ERROR_BUSY                      0x8093000D
#define ORBIS_AJM_ERROR_BAD_PRIORITY              0x8093000E
#define ORBIS_AJM_ERROR_IN_PROGRESS               0x8093000F
#define ORBIS_AJM_ERROR_RETRY                     0x80930010
#define ORBIS_AJM_ERROR_MALFORMED_BATCH           0x80930011
#define ORBIS_AJM_ERROR_JOB_CREATION              0x80930012
#define ORBIS_AJM_ERROR_INVALID_OPCODE            0x80930013
#define ORBIS_AJM_ERROR_PRIORITY_VIOLATION        0x80930014
#define ORBIS_AJM_ERROR_BUFFER_TOO_BIG            0x80930015
#define ORBIS_AJM_ERROR_INVALID_ADDRESS           0x80930016
#define ORBIS_AJM_ERROR_CANCELLED                 0x80930017

#define ORBIS_AJM_RESULT_NOT_INITIALIZED 0x00000001
#define ORBIS_AJM_RESULT_INVALID_DATA    0x00000002
#define ORBIS_AJM_RESULT_INVALID_PARAM   0x00000004
#define ORBIS_AJM_RESULT_PARTIAL_INPUT   0x00000008
#define ORBIS_AJM_RESULT_NOT_ENOUGH_ROOM 0x00000010
#define ORBIS_AJM_RESULT_STREAM_CHANGE   0x00000020
#define ORBIS_AJM_RESULT_TOO_MANY_CHANS  0x00000040
#define ORBIS_AJM_RESULT_UNSUPP_FLAG     0x00000080
#define ORBIS_AJM_RESULT_SIDEBAND_TRUNC  0x00000100
#define ORBIS_AJM_RESULT_PRIORITY_PASSED 0x00000200
#define ORBIS_AJM_RESULT_CODEC_ERROR     0x40000000
#define ORBIS_AJM_RESULT_FATAL           0x80000000

typedef enum {
    AJM_CODEC_MP3_DEC = 0,
    AJM_CODEC_AT9_DEC = 1,
    AJM_CODEC_M4AAC_DEC = 2,
    AJM_CODEC_MAX = 23,
} OrbisAjmCodecType;

typedef enum {
    AJM_FORMAT_S16 = 0,
    AJM_FORMAT_S32 = 1,
    AJM_FORMAT_FLOAT = 2,
} OrbisAjmFormatEncoding;

typedef struct {
    int32_t error_code;
    const void *job_addr;
    uint32_t cmd_offset;
    const void *job_ra;
} OrbisAjmBatchError;

typedef struct {
    uint8_t *p_address;
    uint64_t size;
} OrbisAjmBuffer;

typedef struct {
    int32_t result;
    int32_t internal_result;
} OrbisAjmSidebandResult;

typedef struct {
    int32_t input_consumed;
    int32_t output_written;
    uint64_t total_decoded_samples;
} OrbisAjmSidebandStream;

typedef struct {
    uint32_t num_channels;
    uint32_t channel_mask;
    uint32_t sample_freq;
    OrbisAjmFormatEncoding sample_encoding;
    uint32_t bitrate;
    uint32_t reserved;
} OrbisAjmSidebandFormat;

typedef struct {
    uint32_t super_frame_size;
    uint32_t frames_in_super_frame;
    uint32_t next_frame_size;
    uint32_t frame_samples;
} OrbisAjmSidebandDecAt9CodecInfo;

// Host shim declarations for libSceAjm
void shim_sceAjmInitialize(GuestContext *ctx);
void shim_sceAjmFinalize(GuestContext *ctx);
void shim_sceAjmModuleRegister(GuestContext *ctx);
void shim_sceAjmModuleUnregister(GuestContext *ctx);
void shim_sceAjmInstanceCreate(GuestContext *ctx);
void shim_sceAjmInstanceDestroy(GuestContext *ctx);
void shim_sceAjmInstanceCodecType(GuestContext *ctx);
void shim_sceAjmInstanceExtend(GuestContext *ctx);
void shim_sceAjmInstanceSwitch(GuestContext *ctx);
void shim_sceAjmMemoryRegister(GuestContext *ctx);
void shim_sceAjmMemoryUnregister(GuestContext *ctx);
void shim_sceAjmBatchStartBuffer(GuestContext *ctx);
void shim_sceAjmBatchWait(GuestContext *ctx);
void shim_sceAjmBatchCancel(GuestContext *ctx);
void shim_sceAjmBatchErrorDump(GuestContext *ctx);
void shim_sceAjmBatchJobRunBufferRa(GuestContext *ctx);
void shim_sceAjmBatchJobControlBufferRa(GuestContext *ctx);
void shim_sceAjmBatchJobInlineBuffer(GuestContext *ctx);
void shim_sceAjmBatchJobRunSplitBufferRa(GuestContext *ctx);
void shim_sceAjmDecAt9ParseConfigData(GuestContext *ctx);
void shim_sceAjmDecMp3ParseFrame(GuestContext *ctx);
void shim_sceAjmStrError(GuestContext *ctx);

void ps4_ajm_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // PS4_AJM_H
