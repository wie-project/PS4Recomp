// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PS4_AVPLAYER_H
#define PS4_AVPLAYER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "recomp_context.h"

#ifdef __cplusplus
extern "C" {
#endif

// Error Codes
#define ORBIS_OK                                     ((int32_t)0)
#define ORBIS_AVPLAYER_ERROR_INVALID_PARAMS          ((int32_t)0x806A0001)
#define ORBIS_AVPLAYER_ERROR_OPERATION_FAILED        ((int32_t)0x806A0002)
#define ORBIS_AVPLAYER_ERROR_NO_MEMORY               ((int32_t)0x806A0003)
#define ORBIS_AVPLAYER_ERROR_NOT_SUPPORTED           ((int32_t)0x806A0004)
#define ORBIS_AVPLAYER_ERROR_WAR_FILE_NONINTERLEAVED ((int32_t)0x806A00A0)
#define ORBIS_AVPLAYER_ERROR_WAR_LOOPING_BACK        ((int32_t)0x806A00A1)
#define ORBIS_AVPLAYER_ERROR_WAR_JUMP_COMPLETE       ((int32_t)0x806A00A3)
#define ORBIS_AVPLAYER_ERROR_INFO_MARLIN_ENCRY       ((int32_t)0x806A00B0)
#define ORBIS_AVPLAYER_ERROR_INFO_PLAYREADY_ENCRY    ((int32_t)0x806A00B4)
#define ORBIS_AVPLAYER_ERROR_INFO_AES_ENCRY          ((int32_t)0x806A00B5)
#define ORBIS_AVPLAYER_ERROR_INFO_OTHER_ENCRY        ((int32_t)0x806A00BF)

typedef enum AvPlayerUriType {
    AVPLAYER_URI_TYPE_SOURCE = 0,
} AvPlayerUriType;

typedef struct AvPlayerUri {
    uint64_t name; // const char* guest ptr
    uint32_t length;
} AvPlayerUri;

typedef enum AvPlayerSourceType {
    AVPLAYER_SOURCE_TYPE_UNKNOWN = 0,
    AVPLAYER_SOURCE_TYPE_FILE_MP4 = 1,
    AVPLAYER_SOURCE_TYPE_HLS = 8,
} AvPlayerSourceType;

typedef enum AvPlayerStreamType {
    AVPLAYER_STREAM_TYPE_VIDEO = 0,
    AVPLAYER_STREAM_TYPE_AUDIO = 1,
    AVPLAYER_STREAM_TYPE_TIMED_TEXT = 2,
    AVPLAYER_STREAM_TYPE_UNKNOWN = 3,
} AvPlayerStreamType;

typedef struct AvPlayerSourceDetails {
    AvPlayerUri uri;
    uint8_t reserved1[64];
    AvPlayerSourceType source_type;
    uint8_t reserved2[44];
} AvPlayerSourceDetails;

typedef struct AvPlayerAudio {
    uint16_t channel_count;
    uint8_t reserved1[2];
    uint32_t sample_rate;
    uint32_t size;
    char language_code[4];
} AvPlayerAudio;

typedef struct AvPlayerVideo {
    uint32_t width;
    uint32_t height;
    float aspect_ratio;
    char language_code[4];
} AvPlayerVideo;

typedef struct AvPlayerTextPosition {
    uint16_t top;
    uint16_t left;
    uint16_t bottom;
    uint16_t right;
} AvPlayerTextPosition;

typedef struct AvPlayerTimedText {
    char language_code[4];
    uint16_t text_size;
    uint16_t font_size;
    AvPlayerTextPosition position;
} AvPlayerTimedText;

typedef union AvPlayerStreamDetails {
    uint8_t reserved[16];
    AvPlayerAudio audio;
    AvPlayerVideo video;
    AvPlayerTimedText subs;
} AvPlayerStreamDetails;

typedef struct AvPlayerFrameInfo {
    uint64_t p_data; // uint8_t* guest ptr
    uint8_t reserved[4];
    uint64_t timestamp;
    AvPlayerStreamDetails details;
} AvPlayerFrameInfo;

typedef struct AvPlayerStreamInfo {
    AvPlayerStreamType type;
    uint8_t reserved[4];
    AvPlayerStreamDetails details;
    uint64_t duration;
    uint64_t start_time;
} AvPlayerStreamInfo;

typedef struct AvPlayerAudioEx {
    uint16_t channel_count;
    uint8_t reserved[2];
    uint32_t sample_rate;
    uint32_t size;
    uint8_t language_code[4];
    uint8_t reserved1[64];
} AvPlayerAudioEx;

typedef struct AvPlayerVideoEx {
    uint32_t width;
    uint32_t height;
    float aspect_ratio;
    uint8_t language_code[4];
    uint32_t framerate;
    uint32_t crop_left_offset;
    uint32_t crop_right_offset;
    uint32_t crop_top_offset;
    uint32_t crop_bottom_offset;
    uint32_t pitch;
    uint8_t luma_bit_depth;
    uint8_t chroma_bit_depth;
    bool video_full_range_flag;
    uint8_t reserved1[37];
} AvPlayerVideoEx;

typedef struct AvPlayerTimedTextEx {
    uint8_t language_code[4];
    uint8_t reserved[12];
    uint8_t reserved1[64];
} AvPlayerTimedTextEx;

typedef union AvPlayerStreamDetailsEx {
    AvPlayerAudioEx audio;
    AvPlayerVideoEx video;
    AvPlayerTimedTextEx subs;
    uint8_t reserved1[80];
} AvPlayerStreamDetailsEx;

typedef struct AvPlayerFrameInfoEx {
    uint64_t p_data; // void* guest ptr
    uint8_t reserved[4];
    uint64_t timestamp;
    AvPlayerStreamDetailsEx details;
} AvPlayerFrameInfoEx;

typedef struct AvPlayerMemAllocator {
    uint64_t object_ptr;
    uint64_t allocate;
    uint64_t deallocate;
    uint64_t allocate_texture;
    uint64_t deallocate_texture;
} AvPlayerMemAllocator;

typedef struct AvPlayerFileReplacement {
    uint64_t object_ptr;
    uint64_t open;
    uint64_t close;
    uint64_t read_offset;
    uint64_t size;
} AvPlayerFileReplacement;

typedef enum AvPlayerEvents {
    AVPLAYER_EVENT_STATE_STOP = 0x01,
    AVPLAYER_EVENT_STATE_READY = 0x02,
    AVPLAYER_EVENT_STATE_PLAY = 0x03,
    AVPLAYER_EVENT_STATE_PAUSE = 0x04,
    AVPLAYER_EVENT_STATE_BUFFERING = 0x05,
    AVPLAYER_EVENT_TIMED_TEXT_DELIVERY = 0x10,
    AVPLAYER_EVENT_WARNING_ID = 0x20,
    AVPLAYER_EVENT_ENCRYPTION = 0x30,
    AVPLAYER_EVENT_DRM_ERROR = 0x40,
} AvPlayerEvents;

typedef struct AvPlayerEventReplacement {
    uint64_t object_ptr;
    uint64_t event_callback;
} AvPlayerEventReplacement;

typedef enum AvPlayerDebuglevels {
    AVPLAYER_DBG_NONE,
    AVPLAYER_DBG_INFO,
    AVPLAYER_DBG_WARNINGS,
    AVPLAYER_DBG_ALL,
} AvPlayerDebuglevels;

typedef struct AvPlayerInitData {
    AvPlayerMemAllocator memory_replacement;
    AvPlayerFileReplacement file_replacement;
    AvPlayerEventReplacement event_replacement;
    AvPlayerDebuglevels debug_level;
    uint32_t base_priority;
    int32_t num_output_video_framebuffers;
    bool auto_start;
    uint8_t reserved[3];
    uint64_t default_language; // const char* guest ptr
} AvPlayerInitData;

typedef struct AvPlayerInitDataEx {
    uint64_t this_size;
    AvPlayerMemAllocator memory_replacement;
    AvPlayerFileReplacement file_replacement;
    AvPlayerEventReplacement event_replacement;
    uint64_t default_language;
    AvPlayerDebuglevels debug_level;
    uint32_t audio_decoder_priority;
    uint32_t audio_decoder_affinity;
    uint32_t video_decoder_priority;
    uint32_t video_decoder_affinity;
    uint32_t demuxer_priority;
    uint32_t demuxer_affinity;
    uint32_t controller_priority;
    uint32_t controller_affinity;
    uint32_t http_streaming_priority;
    uint32_t http_streaming_affinity;
    uint32_t file_streaming_priority;
    uint32_t file_streaming_affinity;
    int32_t num_output_video_framebuffers;
    bool auto_start;
    uint8_t reserved[3];
} AvPlayerInitDataEx;

typedef enum AvPlayerAvSyncMode {
    AVPLAYER_AV_SYNC_MODE_DEFAULT = 0,
    AVPLAYER_AV_SYNC_MODE_NONE,
} AvPlayerAvSyncMode;

typedef struct AvPlayerDecoderInit {
    uint32_t decoder_type;
    uint8_t decoder_params[28];
} AvPlayerDecoderInit;

typedef struct AvPlayerHTTPCtx {
    uint32_t http_context_id;
    uint32_t ssl_context_id;
} AvPlayerHTTPCtx;

typedef struct AvPlayerPostInitData {
    uint32_t demux_video_buffer_size;
    AvPlayerDecoderInit video_decoder_init;
    AvPlayerDecoderInit audio_decoder_init;
    AvPlayerHTTPCtx http_context;
    uint8_t reserved[56];
} AvPlayerPostInitData;

// Host Shims
void shim_sceAvPlayerInit(GuestContext *ctx);
void shim_sceAvPlayerInitEx(GuestContext *ctx);
void shim_sceAvPlayerPostInit(GuestContext *ctx);
void shim_sceAvPlayerAddSource(GuestContext *ctx);
void shim_sceAvPlayerAddSourceEx(GuestContext *ctx);
void shim_sceAvPlayerStreamCount(GuestContext *ctx);
void shim_sceAvPlayerGetStreamInfo(GuestContext *ctx);
void shim_sceAvPlayerEnableStream(GuestContext *ctx);
void shim_sceAvPlayerDisableStream(GuestContext *ctx);
void shim_sceAvPlayerChangeStream(GuestContext *ctx);
void shim_sceAvPlayerStart(GuestContext *ctx);
void shim_sceAvPlayerStop(GuestContext *ctx);
void shim_sceAvPlayerPause(GuestContext *ctx);
void shim_sceAvPlayerResume(GuestContext *ctx);
void shim_sceAvPlayerSetLooping(GuestContext *ctx);
void shim_sceAvPlayerCurrentTime(GuestContext *ctx);
void shim_sceAvPlayerIsActive(GuestContext *ctx);
void shim_sceAvPlayerGetAudioData(GuestContext *ctx);
void shim_sceAvPlayerGetVideoData(GuestContext *ctx);
void shim_sceAvPlayerGetVideoDataEx(GuestContext *ctx);
void shim_sceAvPlayerClose(GuestContext *ctx);
void shim_sceAvPlayerSetAvSyncMode(GuestContext *ctx);
void shim_sceAvPlayerSetLogCallback(GuestContext *ctx);
void shim_sceAvPlayerJumpToTime(GuestContext *ctx);
void shim_sceAvPlayerSetTrickSpeed(GuestContext *ctx);
void shim_sceAvPlayerPrintf(GuestContext *ctx);
void shim_sceAvPlayerVprintf(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_AVPLAYER_H
