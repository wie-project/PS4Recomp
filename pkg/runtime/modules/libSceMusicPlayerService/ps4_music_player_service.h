// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_MUSIC_PLAYER_SERVICE_H
#define PS4_MUSIC_PLAYER_SERVICE_H

// libSceMusicPlayerService after shadPS4 (music/music_player_service.{h,cpp}): the system music
// player a title drives for background music (BGM) from files it names (its own files or USB
// storage). Host functions take host pointers; the shims translate guest pointers.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Error codes of module 0x12f.
#define ORBIS_MUSIC_PLAYER_SERVICE_ERROR_NOT_INITIALIZED ((int32_t)0x812f0001)
#define ORBIS_MUSIC_PLAYER_SERVICE_ERROR_ALREADY_INITIALIZED ((int32_t)0x812f0002)
#define ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER ((int32_t)0x812f0004)
#define ORBIS_MUSIC_PLAYER_SERVICE_ERROR_ALREADY_IN_TRANSACTION ((int32_t)0x812f0101)
#define ORBIS_MUSIC_PLAYER_SERVICE_ERROR_NOT_IN_TRANSACTION ((int32_t)0x812f0102)
#define ORBIS_MUSIC_PLAYER_SERVICE_ERROR_TIMED_OUT ((int32_t)0x812f010a)

// Strides of the track entries of SetTrackList / SetTrackList2; each entry starts with its URI.
#define PS4_MPS_TRACK_ENTRY_SIZE 0xc2fu
#define PS4_MPS_TRACK_ENTRY2_SIZE 0xc48u

typedef struct {
    uint64_t size;              // +0x00, must be 0x48
    uint32_t error_code;        // +0x08 (0: ok)
    uint32_t play_state;        // +0x0c (0: playing, 1: paused, 2: stopped)
    uint64_t total_duration_s;  // +0x10
    uint64_t current_time_s;    // +0x18
    uint32_t tracklist_id;      // +0x20
    uint32_t track_index;       // +0x24
    uint32_t play_index;        // +0x28
    uint32_t total_track_count; // +0x2c
    uint32_t volume;            // +0x30 (0 - 25)
    uint32_t repeat_mode;       // +0x34 (0: off, 1: all, 2: one)
    uint32_t shuffle_mode;      // +0x38 (0: off, 1: on)
    uint32_t can_skip_next;     // +0x3c (0: can skip, 1: cannot)
    uint32_t can_skip_previous; // +0x40 (0: can skip, 1: cannot)
    uint32_t is_loading;        // +0x44
} OrbisMusicPlayerPlayStatusExtension;
_Static_assert(sizeof(OrbisMusicPlayerPlayStatusExtension) == 0x48, "status extension size");

typedef struct {
    uint32_t result;        // +0x00
    uint32_t play_status;   // +0x04 (0: playing, 1: paused, 2: stopped)
    uint32_t track_index;   // +0x08
    uint32_t tracklist_id;  // +0x0c
    uint64_t playback_time; // +0x10, seconds
    uint32_t shuffle;       // +0x18
    uint32_t repeat;        // +0x1c
} SceMusicPlayerServicePlayStatus;
_Static_assert(sizeof(SceMusicPlayerServicePlayStatus) == 0x20, "play status size");

typedef struct {
    uint32_t device_id;         // +0x00
    char serial_number[256];    // +0x04
    uint8_t pad0[4];            // +0x104
    uint64_t serial_number_len; // +0x108
} OrbisMusicPlayerUsbDeviceInfo;
_Static_assert(sizeof(OrbisMusicPlayerUsbDeviceInfo) == 0x110, "USB device info size");

int32_t sceMusicPlayerServiceInitialize(size_t pool_size, void *pool_addr);
int32_t sceMusicPlayerServiceInitialize2(size_t pool_size, void *pool_addr);
int32_t sceMusicPlayerServiceInitialize3(size_t pool_size, void *pool_addr, uint64_t unk1,
                                         uint64_t unk2);
int32_t sceMusicPlayerServiceTerminate(void);
int32_t sceMusicPlayerServiceSetUsbStorageDeviceInfo(const OrbisMusicPlayerUsbDeviceInfo *devices,
                                                     uint32_t count);
int32_t sceMusicPlayerServiceRemoveAllData(int32_t player_id);
int32_t sceMusicPlayerServiceSetTrackList(int32_t player_id, const void *tracks, uint32_t count);
int32_t sceMusicPlayerServiceSetTrackList2(int32_t player_id, const void *tracks, uint32_t count,
                                           uint32_t flags);
int32_t sceMusicPlayerServicePlay(int32_t player_id, uint32_t track_index, int64_t start_time_s,
                                  int32_t flags);
int32_t sceMusicPlayerServicePlayStartByTime(int32_t player_id, uint32_t track_index,
                                             int64_t start_time_s);
int32_t sceMusicPlayerServiceStop(void);
int32_t sceMusicPlayerServicePause(void);
int32_t sceMusicPlayerServiceUnpause(void);
int32_t sceMusicPlayerServiceNextTrack(void);
int32_t sceMusicPlayerServicePreviousTrack(void);
int32_t sceMusicPlayerServiceSeek(int32_t player_id, uint32_t track_index, int64_t time_s);
int32_t sceMusicPlayerServiceSetAudioVolume(int32_t volume);
int32_t sceMusicPlayerServiceGetAudioVolume(int32_t *volume, int32_t *mute);
int32_t sceMusicPlayerServiceSetShuffle(uint32_t mode);
int32_t sceMusicPlayerServiceSetRepeatMode(uint32_t mode);
int32_t sceMusicPlayerServiceGetPlayStatusExtension(OrbisMusicPlayerPlayStatusExtension *status);
int32_t sceMusicPlayerServiceGetCurrentPlayStatus(SceMusicPlayerServicePlayStatus *status);
int32_t sceMusicPlayerServiceReceiveEvent(void *buffer);
int32_t sceMusicPlayerServiceBeginTransaction(int32_t a);
int32_t sceMusicPlayerServiceEndTransaction(int32_t a);
int32_t sceMusicPlayerServiceCreateTrackList(uint32_t tracklist_id);
int32_t sceMusicPlayerServiceRemoveTrackList(uint32_t tracklist_id);
int32_t sceMusicPlayerServiceInsertTrackEntry(uint32_t tracklist_id, uint32_t track_index,
                                              const void *track_entry);
int32_t sceMusicPlayerServiceRemoveTrackEntry(uint32_t tracklist_id, uint32_t track_index);
int32_t sceMusicPlayerServiceGetTrackListVersion(int32_t player_id, uint64_t *version);

// shadPS4 IsBgmPlaying: the player is playing or paused (sceSystemServiceIsBgmPlaying).
bool ps4_music_player_is_bgm_playing(void);

void shim_sceMusicPlayerServiceInitialize(GuestContext *ctx);
void shim_sceMusicPlayerServiceInitialize2(GuestContext *ctx);
void shim_sceMusicPlayerServiceInitialize3(GuestContext *ctx);
void shim_sceMusicPlayerServiceTerminate(GuestContext *ctx);
void shim_sceMusicPlayerServiceSetUsbStorageDeviceInfo(GuestContext *ctx);
void shim_sceMusicPlayerServiceRemoveAllData(GuestContext *ctx);
void shim_sceMusicPlayerServiceSetTrackList(GuestContext *ctx);
void shim_sceMusicPlayerServiceSetTrackList2(GuestContext *ctx);
void shim_sceMusicPlayerServicePlay(GuestContext *ctx);
void shim_sceMusicPlayerServicePlayStartByTime(GuestContext *ctx);
void shim_sceMusicPlayerServiceStop(GuestContext *ctx);
void shim_sceMusicPlayerServicePause(GuestContext *ctx);
void shim_sceMusicPlayerServiceUnpause(GuestContext *ctx);
void shim_sceMusicPlayerServiceNextTrack(GuestContext *ctx);
void shim_sceMusicPlayerServicePreviousTrack(GuestContext *ctx);
void shim_sceMusicPlayerServiceSeek(GuestContext *ctx);
void shim_sceMusicPlayerServiceSetAudioVolume(GuestContext *ctx);
void shim_sceMusicPlayerServiceGetAudioVolume(GuestContext *ctx);
void shim_sceMusicPlayerServiceSetShuffle(GuestContext *ctx);
void shim_sceMusicPlayerServiceSetRepeatMode(GuestContext *ctx);
void shim_sceMusicPlayerServiceGetPlayStatusExtension(GuestContext *ctx);
void shim_sceMusicPlayerServiceGetCurrentPlayStatus(GuestContext *ctx);
void shim_sceMusicPlayerServiceReceiveEvent(GuestContext *ctx);
void shim_sceMusicPlayerServiceGetPlaybackResults(GuestContext *ctx);
void shim_sceMusicPlayerServiceGetTrackInfo(GuestContext *ctx);
void shim_sceMusicPlayerServiceBeginTransaction(GuestContext *ctx);
void shim_sceMusicPlayerServiceEndTransaction(GuestContext *ctx);
void shim_sceMusicPlayerServiceCreateTrackList(GuestContext *ctx);
void shim_sceMusicPlayerServiceRemoveTrackList(GuestContext *ctx);
void shim_sceMusicPlayerServiceInsertTrackEntry(GuestContext *ctx);
void shim_sceMusicPlayerServiceRemoveTrackEntry(GuestContext *ctx);
void shim_sceMusicPlayerServiceGetTrackListVersion(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_MUSIC_PLAYER_SERVICE_H
