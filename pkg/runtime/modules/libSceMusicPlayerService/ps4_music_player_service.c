// SPDX-License-Identifier: GPL-2.0-or-later

// libSceMusicPlayerService after shadPS4 (music/music_player_service.cpp, its MusicPlayerManager):
//
// - Track lists: SetTrackList / SetTrackList2 replace the playing list (and list 0). The
//   transaction calls edit lists 0..99; EndTransaction makes list 0 the playing list. Every
//   change of the playing list bumps the version GetTrackListVersion reports.
// - Playback: a host thread decodes the current track as a whole (ps4_music_player_audio.c) and
//   feeds it in 2048-frame chunks to an output stream, keeping at most four chunks queued, so
//   the reported position is what has been played. At the end of a track it moves on by the
//   repeat and shuffle modes; the end of the list (repeat off) stops the player.
// - Track URIs: a scheme ("file://", ...) is dropped, percent escapes are decoded, and a path
//   that starts with "/<serial>/" of a USB device set with SetUsbStorageDeviceInfo is mapped to
//   "/mnt/usb<device_id>/". The result is resolved by the VFS; a path the VFS does not know is
//   tried as a host path, as shadPS4 does with its mount points.
// - Volume 0..25 scales the output (shadPS4 also applies its emulator volume slider; the runtime
//   has no master volume).
// - Initialize / Initialize2, GetPlaybackResults and GetTrackInfo are stubs in shadPS4 and
//   here. shadPS4 never posts a player event, so ReceiveEvent waits its 50 ms for one and times
//   out; the event layout it would write (+0x08 = 1, +0x20 = 1, +0x24 event id, +0x28 track
//   index) has no producer to fill it.

#include "ps4_music_player_service.h"
#include "ps4_hle_stub.h"
#include "ps4_music_player_audio.h"
#include "ps4_vfs.h"

#include <ctype.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define MPS_MAX_TRACKLISTS 100u
#define MPS_MAX_TRACK_INDEX 3999u
#define MPS_MAX_USB_DEVICES 8u
#define MPS_CHUNK_FRAMES 2048u
#define MPS_MAX_VOLUME 25
#define MPS_PATH_MAX 4096

// play_state values (shadPS4's m_play_state)
enum { MPS_STOPPED = 0, MPS_PLAYING = 1, MPS_PAUSED = 2 };

typedef struct {
    char **uris;
    uint32_t count, cap;
} MpsList;

typedef struct {
    char serial[256];
    uint32_t device_id;
} MpsUsbDevice;

// g_control_mu serializes starting and stopping the worker (m_control_mutex); g_mu guards the
// lists and the USB devices (m_mutex). g_control_mu is taken before g_mu.
static pthread_mutex_t g_control_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static MpsList g_tracks;
static MpsList g_lists[MPS_MAX_TRACKLISTS];
static bool g_list_present[MPS_MAX_TRACKLISTS];
static MpsUsbDevice g_usb[MPS_MAX_USB_DEVICES];
static uint32_t g_usb_count;

static _Atomic uint32_t g_play_state;
static _Atomic uint32_t g_current_track;
static _Atomic uint64_t g_current_time_ms;
static _Atomic uint64_t g_total_duration_ms;
static _Atomic int32_t g_volume = MPS_MAX_VOLUME;
static _Atomic uint32_t g_repeat_mode;
static _Atomic uint32_t g_shuffle_mode;
static _Atomic bool g_stop_requested;
static _Atomic bool g_pause_requested;
static _Atomic int64_t g_seek_offset_ms = -1;
static _Atomic bool g_in_transaction;
static _Atomic uint64_t g_tracklist_version = 1;

// The worker thread; g_control_mu.
static pthread_t g_worker;
static bool g_worker_joinable;

// ---------------------------------------------------------------- lists

static void *xrealloc(void *p, size_t n) {
    void *r = realloc(p, n);
    if (!r) {
        fprintf(stderr, "[MusicPlayer] out of memory\n");
        abort();
    }
    return r;
}

static char *xstrdup(const char *s) {
    const size_t n = strlen(s) + 1;
    return (char *)memcpy(xrealloc(NULL, n), s, n);
}

static void list_clear(MpsList *l) {
    for (uint32_t i = 0; i < l->count; i++) free(l->uris[i]);
    l->count = 0;
}

static void list_insert(MpsList *l, uint32_t index, const char *uri) {
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->uris = (char **)xrealloc(l->uris, (size_t)l->cap * sizeof(char *));
    }
    if (index > l->count) index = l->count;
    memmove(l->uris + index + 1, l->uris + index, (size_t)(l->count - index) * sizeof(char *));
    l->uris[index] = xstrdup(uri);
    l->count++;
}

static void list_remove(MpsList *l, uint32_t index) {
    free(l->uris[index]);
    memmove(l->uris + index, l->uris + index + 1, (size_t)(l->count - index - 1) * sizeof(char *));
    l->count--;
}

static void list_copy(MpsList *dst, const MpsList *src) {
    list_clear(dst);
    for (uint32_t i = 0; i < src->count; i++) list_insert(dst, i, src->uris[i]);
}

static uint32_t track_count(void) {
    pthread_mutex_lock(&g_mu);
    const uint32_t n = g_tracks.count;
    pthread_mutex_unlock(&g_mu);
    return n;
}

static void sleep_ms(uint32_t ms) {
    struct timespec ts = {.tv_sec = ms / 1000u, .tv_nsec = (long)(ms % 1000u) * 1000000L};
    nanosleep(&ts, NULL);
}

// ---------------------------------------------------------------- track paths

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

// The guest path of a track URI: scheme dropped, a leading '/', percent escapes decoded, and a
// USB serial prefix mapped to its /mnt/usb mount. False when it does not fit out.
static bool track_guest_path(const char *uri, char *out, size_t out_size) {
    const char *scheme = strstr(uri, "://");
    const char *p = scheme ? scheme + 3 : uri;
    char decoded[MPS_PATH_MAX];
    size_t n = 0;
    if (*p != '/') decoded[n++] = '/';
    const size_t len = strlen(p);
    for (size_t i = 0; i < len; i++) {
        if (n + 1 >= sizeof(decoded)) return false;
        if (p[i] == '%' && i + 2 < len && isxdigit((unsigned char)p[i + 1]) &&
            isxdigit((unsigned char)p[i + 2])) {
            decoded[n++] = (char)(hex_value(p[i + 1]) * 16 + hex_value(p[i + 2]));
            i += 2;
            continue;
        }
        decoded[n++] = p[i];
    }
    decoded[n] = '\0';

    pthread_mutex_lock(&g_mu);
    for (uint32_t i = 0; i < g_usb_count; i++) {
        const size_t sl = strlen(g_usb[i].serial);
        if (decoded[0] == '/' && strncmp(decoded + 1, g_usb[i].serial, sl) == 0 &&
            decoded[1 + sl] == '/') {
            const int w = snprintf(out, out_size, "/mnt/usb%u/%s", g_usb[i].device_id,
                                   decoded + 2 + sl);
            pthread_mutex_unlock(&g_mu);
            return w >= 0 && (size_t)w < out_size;
        }
    }
    pthread_mutex_unlock(&g_mu);
    if (n >= out_size) return false;
    memcpy(out, decoded, n + 1);
    return true;
}

static bool path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// ---------------------------------------------------------------- playback worker

// Plays one decoded track from start_ms until it ends or the player is stopped. False when no
// output stream can be opened.
static bool play_pcm(const Ps4MpsPcm *pcm, uint64_t start_ms, uint64_t total_ms) {
    Ps4MpsStream *stream = ps4_mps_stream_open(pcm->channels, pcm->hz);
    if (!stream) return false;
    ps4_mps_stream_set_paused(stream, false);

    const size_t ch = pcm->channels;
    size_t offset = 0;
    if (start_ms > 0 && total_ms > 0) {
        offset = (size_t)(start_ms * pcm->hz / 1000u) * ch;
        if (offset > pcm->samples) offset = pcm->samples;
    }
    const size_t chunk_samples = MPS_CHUNK_FRAMES * ch;
    const size_t max_queued = chunk_samples * sizeof(int16_t) * 4;

    while (!atomic_load(&g_stop_requested)) {
        const int64_t seek = atomic_exchange(&g_seek_offset_ms, -1);
        if (seek >= 0) {
            offset = (size_t)((uint64_t)seek * pcm->hz / 1000u) * ch;
            if (offset > pcm->samples) offset = pcm->samples;
            atomic_store(&g_current_time_ms, (uint64_t)seek);
            ps4_mps_stream_clear(stream);
        }
        if (atomic_load(&g_pause_requested)) {
            ps4_mps_stream_set_paused(stream, true);
            sleep_ms(20);
            continue;
        }
        ps4_mps_stream_set_paused(stream, false);
        ps4_mps_stream_set_gain(stream, (float)atomic_load(&g_volume) / (float)MPS_MAX_VOLUME);

        const size_t queued = ps4_mps_stream_queued_bytes(stream);
        if (offset < pcm->samples) {
            if (queued < max_queued) {
                const size_t left = pcm->samples - offset;
                const size_t n = chunk_samples < left ? chunk_samples : left;
                if (ps4_mps_stream_put(stream, pcm->buffer + offset, n)) offset += n;
            }
        } else if (queued == 0) {
            break; // everything has been played
        }
        const size_t queued_samples = queued / sizeof(int16_t);
        const size_t played = offset > queued_samples ? offset - queued_samples : 0;
        atomic_store(&g_current_time_ms, (uint64_t)(played / ch) * 1000u / pcm->hz);
        sleep_ms(10);
    }
    ps4_mps_stream_destroy(stream);
    return true;
}

// Decodes and plays the track at index; false when the player has to stop (no such track, a
// missing or undecodable file, no output device).
static bool play_track(uint32_t index, uint64_t start_ms) {
    char uri_path[MPS_PATH_MAX];
    pthread_mutex_lock(&g_mu);
    if (index >= g_tracks.count) {
        pthread_mutex_unlock(&g_mu);
        return false;
    }
    const size_t len = strlen(g_tracks.uris[index]);
    const bool fits = len < sizeof(uri_path);
    if (fits) memcpy(uri_path, g_tracks.uris[index], len + 1);
    pthread_mutex_unlock(&g_mu);
    atomic_store(&g_current_track, index);

    char guest[MPS_PATH_MAX];
    char host[MPS_PATH_MAX];
    if (!fits || !track_guest_path(uri_path, guest, sizeof(guest))) {
        fprintf(stderr, "[MusicPlayer] track %u: URI too long\n", index);
        return false;
    }
    if (ps4_vfs_resolve(guest, host, sizeof(host)) != 0 || !path_exists(host)) {
        snprintf(host, sizeof(host), "%s", guest);
    }
    if (!path_exists(host)) {
        fprintf(stderr, "[MusicPlayer] music file does not exist: %s\n", host);
        return false;
    }
    Ps4MpsPcm pcm;
    if (!ps4_mps_decode_file(host, &pcm)) {
        fprintf(stderr, "[MusicPlayer] failed to decode audio file: %s\n", host);
        return false;
    }
    const uint64_t total_ms = (uint64_t)(pcm.samples / pcm.channels) * 1000u / pcm.hz;
    atomic_store(&g_total_duration_ms, total_ms);
    const bool played = atomic_load(&g_stop_requested) || play_pcm(&pcm, start_ms, total_ms);
    free(pcm.buffer);
    return played;
}

// shadPS4 shuffle: another random track, ten tries to avoid the current one.
static uint32_t shuffle_next(uint32_t current, uint32_t count) {
    uint32_t next = current;
    for (int retry = 0; retry < 10 && next == current; ++retry) next = arc4random_uniform(count);
    return next;
}

static uint32_t g_worker_start_track;
static uint64_t g_worker_start_ms;

static void *playback_worker(void *arg) {
    (void)arg;
    uint32_t track = g_worker_start_track;
    uint64_t start_ms = g_worker_start_ms;
    while (!atomic_load(&g_stop_requested)) {
        if (!play_track(track, start_ms) || atomic_load(&g_stop_requested)) break;
        const uint32_t count = track_count();
        if (count == 0) break;
        const uint32_t repeat = atomic_load(&g_repeat_mode);
        uint32_t next = track;
        if (repeat == 2) {
            next = track; // repeat one
        } else if (atomic_load(&g_shuffle_mode) == 1) {
            if (count > 1) next = shuffle_next(track, count);
        } else if (repeat == 1) {
            next = (track + 1) % count;
        } else if (track + 1 < count) {
            next = track + 1;
        } else {
            fprintf(stderr, "[MusicPlayer] playback reached the end of the playlist\n");
            break;
        }
        track = next;
        start_ms = 0;
        atomic_store(&g_current_track, track);
        atomic_store(&g_current_time_ms, 0);
    }
    if (!atomic_load(&g_stop_requested)) {
        atomic_store(&g_play_state, MPS_STOPPED);
        atomic_store(&g_current_time_ms, 0);
    }
    return NULL;
}

// ---------------------------------------------------------------- control (g_control_mu held)

static void stop_locked(void) {
    atomic_store(&g_stop_requested, true);
    atomic_store(&g_pause_requested, false);
    atomic_store(&g_play_state, MPS_STOPPED);
    atomic_store(&g_current_time_ms, 0);
    atomic_store(&g_total_duration_ms, 0);
    if (g_worker_joinable) {
        pthread_join(g_worker, NULL);
        g_worker_joinable = false;
    }
}

static void play_locked(uint32_t index, uint64_t start_ms) {
    stop_locked();
    pthread_mutex_lock(&g_mu);
    const bool valid = index < g_tracks.count;
    pthread_mutex_unlock(&g_mu);
    if (!valid) return;
    atomic_store(&g_current_track, index);
    atomic_store(&g_stop_requested, false);
    atomic_store(&g_pause_requested, false);
    atomic_store(&g_seek_offset_ms, -1);
    atomic_store(&g_current_time_ms, start_ms);
    atomic_store(&g_total_duration_ms, 0);
    atomic_store(&g_play_state, MPS_PLAYING);
    g_worker_start_track = index;
    g_worker_start_ms = start_ms;
    if (pthread_create(&g_worker, NULL, playback_worker, NULL) != 0) {
        fprintf(stderr, "[MusicPlayer] cannot create the playback thread\n");
        atomic_store(&g_play_state, MPS_STOPPED);
        return;
    }
    g_worker_joinable = true;
}

static void seek_to(uint64_t time_ms) {
    atomic_store(&g_seek_offset_ms, (int64_t)time_ms);
    atomic_store(&g_current_time_ms, time_ms);
}

static void unpause(void) {
    atomic_store(&g_pause_requested, false);
    atomic_store(&g_play_state, MPS_PLAYING);
}

static bool track_active_locked(uint32_t index) {
    return atomic_load(&g_current_track) == index && atomic_load(&g_play_state) != MPS_STOPPED &&
           g_worker_joinable && !atomic_load(&g_stop_requested);
}

static void stop_playback(void) {
    pthread_mutex_lock(&g_control_mu);
    stop_locked();
    pthread_mutex_unlock(&g_control_mu);
}

static void play_or_seek(uint32_t index, uint64_t start_ms) {
    pthread_mutex_lock(&g_control_mu);
    if (track_active_locked(index)) {
        if (start_ms > 0) seek_to(start_ms);
        unpause();
    } else {
        play_locked(index, start_ms);
    }
    pthread_mutex_unlock(&g_control_mu);
}

static uint64_t seconds_to_ms(int64_t s) {
    return s > 0 ? (uint64_t)s * 1000u : 0;
}

// ---------------------------------------------------------------- library

int32_t sceMusicPlayerServiceInitialize(size_t pool_size, void *pool_addr) {
    (void)pool_size, (void)pool_addr;
    return 0; // a stub in shadPS4
}

int32_t sceMusicPlayerServiceInitialize2(size_t pool_size, void *pool_addr) {
    (void)pool_size, (void)pool_addr;
    return 0; // a stub in shadPS4
}

int32_t sceMusicPlayerServiceInitialize3(size_t pool_size, void *pool_addr, uint64_t unk1,
                                         uint64_t unk2) {
    (void)unk1, (void)unk2;
    if (pool_size < 0x200000 || !pool_addr) return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    return 0;
}

int32_t sceMusicPlayerServiceTerminate(void) {
    stop_playback();
    return 0;
}

int32_t sceMusicPlayerServiceSetUsbStorageDeviceInfo(const OrbisMusicPlayerUsbDeviceInfo *devices,
                                                     uint32_t count) {
    if (!devices || count == 0 || count > MPS_MAX_USB_DEVICES) {
        return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    }
    int32_t r = 0;
    pthread_mutex_lock(&g_mu);
    g_usb_count = 0;
    // As shadPS4: the devices before an invalid one stay registered.
    for (uint32_t i = 0; i < count && r == 0; i++) {
        const OrbisMusicPlayerUsbDeviceInfo *d = &devices[i];
        const size_t len = strnlen(d->serial_number, sizeof(d->serial_number));
        if (d->serial_number_len > 255 || len > d->serial_number_len) {
            r = ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
            break;
        }
        uint32_t slot = 0;
        while (slot < g_usb_count && strncmp(g_usb[slot].serial, d->serial_number, 256) != 0) {
            slot++;
        }
        memcpy(g_usb[slot].serial, d->serial_number, len);
        g_usb[slot].serial[len] = '\0';
        g_usb[slot].device_id = d->device_id;
        if (slot == g_usb_count) g_usb_count++;
        fprintf(stderr, "[MusicPlayer] registered USB device_id %u with serial '%s'\n",
                d->device_id, g_usb[slot].serial);
    }
    pthread_mutex_unlock(&g_mu);
    return r;
}

int32_t sceMusicPlayerServiceRemoveAllData(int32_t player_id) {
    (void)player_id;
    stop_playback();
    pthread_mutex_lock(&g_mu);
    list_clear(&g_tracks);
    for (uint32_t i = 0; i < MPS_MAX_TRACKLISTS; i++) {
        list_clear(&g_lists[i]);
        g_list_present[i] = false;
    }
    atomic_fetch_add(&g_tracklist_version, 1);
    pthread_mutex_unlock(&g_mu);
    return 0;
}

static int32_t set_track_list(const void *tracks, uint32_t count, size_t stride) {
    if (!tracks || count == 0) return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    const uint8_t *base = (const uint8_t *)tracks;
    MpsList list = {0};
    for (uint32_t i = 0; i < count; i++) {
        list_insert(&list, i, (const char *)(base + (size_t)i * stride));
    }
    pthread_mutex_lock(&g_mu);
    list_clear(&g_tracks);
    free(g_tracks.uris);
    g_tracks = list;
    list_copy(&g_lists[0], &g_tracks);
    g_list_present[0] = true;
    atomic_fetch_add(&g_tracklist_version, 1);
    pthread_mutex_unlock(&g_mu);
    return 0;
}

int32_t sceMusicPlayerServiceSetTrackList(int32_t player_id, const void *tracks, uint32_t count) {
    (void)player_id;
    return set_track_list(tracks, count, PS4_MPS_TRACK_ENTRY_SIZE);
}

int32_t sceMusicPlayerServiceSetTrackList2(int32_t player_id, const void *tracks, uint32_t count,
                                           uint32_t flags) {
    (void)player_id, (void)flags;
    return set_track_list(tracks, count, PS4_MPS_TRACK_ENTRY2_SIZE);
}

int32_t sceMusicPlayerServicePlay(int32_t player_id, uint32_t track_index, int64_t start_time_s,
                                  int32_t flags) {
    (void)player_id, (void)flags;
    play_or_seek(track_index, seconds_to_ms(start_time_s));
    return 0;
}

int32_t sceMusicPlayerServicePlayStartByTime(int32_t player_id, uint32_t track_index,
                                             int64_t start_time_s) {
    (void)player_id;
    play_or_seek(track_index, seconds_to_ms(start_time_s));
    return 0;
}

int32_t sceMusicPlayerServiceStop(void) {
    stop_playback();
    return 0;
}

int32_t sceMusicPlayerServicePause(void) {
    atomic_store(&g_pause_requested, true);
    atomic_store(&g_play_state, MPS_PAUSED);
    return 0;
}

int32_t sceMusicPlayerServiceUnpause(void) {
    unpause();
    return 0;
}

int32_t sceMusicPlayerServiceNextTrack(void) {
    pthread_mutex_lock(&g_control_mu);
    const uint32_t count = track_count();
    if (count > 0) {
        // shadPS4 also has a shuffle retry here, but it only runs while the next index equals
        // the current one, which (cur + 1) % count never does with more than one track: the
        // next track is always the following one.
        play_locked((atomic_load(&g_current_track) + 1) % count, 0);
    }
    pthread_mutex_unlock(&g_control_mu);
    return 0;
}

int32_t sceMusicPlayerServicePreviousTrack(void) {
    pthread_mutex_lock(&g_control_mu);
    if (track_count() > 0) {
        const uint32_t cur = atomic_load(&g_current_track);
        play_locked(cur > 0 ? cur - 1 : 0, 0);
    }
    pthread_mutex_unlock(&g_control_mu);
    return 0;
}

int32_t sceMusicPlayerServiceSeek(int32_t player_id, uint32_t track_index, int64_t time_s) {
    (void)player_id;
    const uint64_t ms = seconds_to_ms(time_s);
    pthread_mutex_lock(&g_control_mu);
    if (track_active_locked(track_index)) {
        seek_to(ms);
    } else {
        play_locked(track_index, ms);
    }
    pthread_mutex_unlock(&g_control_mu);
    return 0;
}

int32_t sceMusicPlayerServiceSetAudioVolume(int32_t volume) {
    if (volume < 0 || volume > MPS_MAX_VOLUME) {
        return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    }
    atomic_store(&g_volume, volume);
    return 0;
}

int32_t sceMusicPlayerServiceGetAudioVolume(int32_t *volume, int32_t *mute) {
    if (volume) *volume = atomic_load(&g_volume);
    if (mute) *mute = 0;
    return 0;
}

int32_t sceMusicPlayerServiceSetShuffle(uint32_t mode) {
    if (mode > 1) return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    atomic_store(&g_shuffle_mode, mode);
    return 0;
}

int32_t sceMusicPlayerServiceSetRepeatMode(uint32_t mode) {
    if (mode > 2) return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    atomic_store(&g_repeat_mode, mode);
    return 0;
}

// The guest's play status: 0 playing, 1 paused, 2 stopped.
static uint32_t guest_play_state(void) {
    switch (atomic_load(&g_play_state)) {
    case MPS_PLAYING: return 0;
    case MPS_PAUSED: return 1;
    default: return 2;
    }
}

int32_t sceMusicPlayerServiceGetPlayStatusExtension(OrbisMusicPlayerPlayStatusExtension *status) {
    if (!status || status->size != sizeof(*status)) {
        return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    }
    const uint32_t count = track_count();
    const uint32_t cur = atomic_load(&g_current_track);
    const uint32_t repeat = atomic_load(&g_repeat_mode);
    const uint32_t shuffle = atomic_load(&g_shuffle_mode);
    status->error_code = 0;
    status->play_state = guest_play_state();
    status->total_duration_s = atomic_load(&g_total_duration_ms) / 1000u;
    status->current_time_s = atomic_load(&g_current_time_ms) / 1000u;
    status->tracklist_id = 0;
    status->track_index = cur;
    status->play_index = cur;
    status->total_track_count = count;
    status->volume = (uint32_t)atomic_load(&g_volume);
    status->repeat_mode = repeat;
    status->shuffle_mode = shuffle;
    const bool wraps = repeat != 0 || shuffle != 0;
    const bool can_next = count > 0 && (cur + 1 < count || wraps);
    const bool can_prev = count > 0 && (cur > 0 || wraps);
    // 0 = skip allowed, 1 = skip disabled
    status->can_skip_next = can_next ? 0 : 1;
    status->can_skip_previous = can_prev ? 0 : 1;
    status->is_loading = 0;
    return 0;
}

int32_t sceMusicPlayerServiceGetCurrentPlayStatus(SceMusicPlayerServicePlayStatus *status) {
    if (!status) return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    status->result = 0;
    status->play_status = guest_play_state();
    status->track_index = atomic_load(&g_current_track);
    status->tracklist_id = 0;
    status->playback_time = atomic_load(&g_current_time_ms) / 1000u;
    status->shuffle = atomic_load(&g_shuffle_mode);
    status->repeat = atomic_load(&g_repeat_mode);
    return 0;
}

int32_t sceMusicPlayerServiceReceiveEvent(void *buffer) {
    if (!buffer) return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    sleep_ms(50); // the wait for an event no part of the player posts (see the top)
    return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_TIMED_OUT;
}

int32_t sceMusicPlayerServiceBeginTransaction(int32_t a) {
    (void)a;
    pthread_mutex_lock(&g_mu);
    const bool was = atomic_exchange(&g_in_transaction, true);
    pthread_mutex_unlock(&g_mu);
    return was ? ORBIS_MUSIC_PLAYER_SERVICE_ERROR_ALREADY_IN_TRANSACTION : 0;
}

int32_t sceMusicPlayerServiceEndTransaction(int32_t a) {
    (void)a;
    pthread_mutex_lock(&g_mu);
    if (!atomic_load(&g_in_transaction)) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_NOT_IN_TRANSACTION;
    }
    atomic_store(&g_in_transaction, false);
    atomic_fetch_add(&g_tracklist_version, 1);
    if (g_list_present[0]) list_copy(&g_tracks, &g_lists[0]);
    pthread_mutex_unlock(&g_mu);
    return 0;
}

int32_t sceMusicPlayerServiceCreateTrackList(uint32_t tracklist_id) {
    if (tracklist_id >= MPS_MAX_TRACKLISTS) return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    pthread_mutex_lock(&g_mu);
    int32_t r = ORBIS_MUSIC_PLAYER_SERVICE_ERROR_NOT_IN_TRANSACTION;
    if (atomic_load(&g_in_transaction)) {
        list_clear(&g_lists[tracklist_id]);
        g_list_present[tracklist_id] = true;
        r = 0;
    }
    pthread_mutex_unlock(&g_mu);
    return r;
}

int32_t sceMusicPlayerServiceRemoveTrackList(uint32_t tracklist_id) {
    if (tracklist_id >= MPS_MAX_TRACKLISTS) return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    pthread_mutex_lock(&g_mu);
    int32_t r = ORBIS_MUSIC_PLAYER_SERVICE_ERROR_NOT_IN_TRANSACTION;
    if (atomic_load(&g_in_transaction)) {
        list_clear(&g_lists[tracklist_id]);
        g_list_present[tracklist_id] = false;
        if (tracklist_id == 0) list_clear(&g_tracks);
        r = 0;
    }
    pthread_mutex_unlock(&g_mu);
    return r;
}

int32_t sceMusicPlayerServiceInsertTrackEntry(uint32_t tracklist_id, uint32_t track_index,
                                              const void *track_entry) {
    if (!track_entry) return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    if (tracklist_id >= MPS_MAX_TRACKLISTS || track_index > MPS_MAX_TRACK_INDEX) {
        return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    }
    pthread_mutex_lock(&g_mu);
    int32_t r = ORBIS_MUSIC_PLAYER_SERVICE_ERROR_NOT_IN_TRANSACTION;
    if (atomic_load(&g_in_transaction)) {
        // An index past the end appends; a missing list is created (operator[] in shadPS4).
        list_insert(&g_lists[tracklist_id], track_index, (const char *)track_entry);
        g_list_present[tracklist_id] = true;
        r = 0;
    }
    pthread_mutex_unlock(&g_mu);
    return r;
}

int32_t sceMusicPlayerServiceRemoveTrackEntry(uint32_t tracklist_id, uint32_t track_index) {
    if (tracklist_id >= MPS_MAX_TRACKLISTS || track_index > MPS_MAX_TRACK_INDEX) {
        return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    }
    pthread_mutex_lock(&g_mu);
    int32_t r = ORBIS_MUSIC_PLAYER_SERVICE_ERROR_NOT_IN_TRANSACTION;
    if (atomic_load(&g_in_transaction)) {
        if (g_list_present[tracklist_id] && track_index < g_lists[tracklist_id].count) {
            list_remove(&g_lists[tracklist_id], track_index);
        }
        r = 0;
    }
    pthread_mutex_unlock(&g_mu);
    return r;
}

int32_t sceMusicPlayerServiceGetTrackListVersion(int32_t player_id, uint64_t *version) {
    (void)player_id;
    if (!version) return ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER;
    *version = atomic_load(&g_tracklist_version);
    return 0;
}

bool ps4_music_player_is_bgm_playing(void) {
    return atomic_load(&g_play_state) != MPS_STOPPED;
}

// ---------------------------------------------------------------- guest shims

#define MPS_GPTR(ctx, a) ((a) ? (void *)((ctx)->mem_base + (a)) : NULL)

static void mps_return(GuestContext *ctx, int32_t rc) {
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceMusicPlayerServiceInitialize(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceInitialize((size_t)ctx->rdi, MPS_GPTR(ctx, ctx->rsi)));
}

void shim_sceMusicPlayerServiceInitialize2(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceInitialize2((size_t)ctx->rdi, MPS_GPTR(ctx, ctx->rsi)));
}

void shim_sceMusicPlayerServiceInitialize3(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceInitialize3((size_t)ctx->rdi, MPS_GPTR(ctx, ctx->rsi),
                                                     ctx->rdx, ctx->rcx));
}

void shim_sceMusicPlayerServiceTerminate(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceTerminate());
}

void shim_sceMusicPlayerServiceSetUsbStorageDeviceInfo(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceSetUsbStorageDeviceInfo(
                        (const OrbisMusicPlayerUsbDeviceInfo *)MPS_GPTR(ctx, ctx->rdi),
                        (uint32_t)ctx->rsi));
}

void shim_sceMusicPlayerServiceRemoveAllData(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceRemoveAllData((int32_t)ctx->rdi));
}

void shim_sceMusicPlayerServiceSetTrackList(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceSetTrackList((int32_t)ctx->rdi, MPS_GPTR(ctx, ctx->rsi),
                                                      (uint32_t)ctx->rdx));
}

void shim_sceMusicPlayerServiceSetTrackList2(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceSetTrackList2((int32_t)ctx->rdi, MPS_GPTR(ctx, ctx->rsi),
                                                       (uint32_t)ctx->rdx, (uint32_t)ctx->rcx));
}

void shim_sceMusicPlayerServicePlay(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServicePlay((int32_t)ctx->rdi, (uint32_t)ctx->rsi,
                                              (int64_t)ctx->rdx, (int32_t)ctx->rcx));
}

void shim_sceMusicPlayerServicePlayStartByTime(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServicePlayStartByTime((int32_t)ctx->rdi, (uint32_t)ctx->rsi,
                                                         (int64_t)ctx->rdx));
}

void shim_sceMusicPlayerServiceStop(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceStop());
}

void shim_sceMusicPlayerServicePause(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServicePause());
}

void shim_sceMusicPlayerServiceUnpause(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceUnpause());
}

void shim_sceMusicPlayerServiceNextTrack(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceNextTrack());
}

void shim_sceMusicPlayerServicePreviousTrack(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServicePreviousTrack());
}

void shim_sceMusicPlayerServiceSeek(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceSeek((int32_t)ctx->rdi, (uint32_t)ctx->rsi,
                                              (int64_t)ctx->rdx));
}

void shim_sceMusicPlayerServiceSetAudioVolume(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceSetAudioVolume((int32_t)ctx->rdi));
}

void shim_sceMusicPlayerServiceGetAudioVolume(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceGetAudioVolume((int32_t *)MPS_GPTR(ctx, ctx->rdi),
                                                        (int32_t *)MPS_GPTR(ctx, ctx->rsi)));
}

void shim_sceMusicPlayerServiceSetShuffle(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceSetShuffle((uint32_t)ctx->rdi));
}

void shim_sceMusicPlayerServiceSetRepeatMode(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceSetRepeatMode((uint32_t)ctx->rdi));
}

void shim_sceMusicPlayerServiceGetPlayStatusExtension(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceGetPlayStatusExtension(
                        (OrbisMusicPlayerPlayStatusExtension *)MPS_GPTR(ctx, ctx->rdi)));
}

void shim_sceMusicPlayerServiceGetCurrentPlayStatus(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceGetCurrentPlayStatus(
                        (SceMusicPlayerServicePlayStatus *)MPS_GPTR(ctx, ctx->rdi)));
}

void shim_sceMusicPlayerServiceReceiveEvent(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceReceiveEvent(MPS_GPTR(ctx, ctx->rdi)));
}

PS4_HLE_STUB(sceMusicPlayerServiceGetPlaybackResults)
PS4_HLE_STUB(sceMusicPlayerServiceGetTrackInfo)

void shim_sceMusicPlayerServiceBeginTransaction(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceBeginTransaction((int32_t)ctx->rdi));
}

void shim_sceMusicPlayerServiceEndTransaction(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceEndTransaction((int32_t)ctx->rdi));
}

void shim_sceMusicPlayerServiceCreateTrackList(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceCreateTrackList((uint32_t)ctx->rdi));
}

void shim_sceMusicPlayerServiceRemoveTrackList(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceRemoveTrackList((uint32_t)ctx->rdi));
}

void shim_sceMusicPlayerServiceInsertTrackEntry(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceInsertTrackEntry((uint32_t)ctx->rdi, (uint32_t)ctx->rsi,
                                                          MPS_GPTR(ctx, ctx->rdx)));
}

void shim_sceMusicPlayerServiceRemoveTrackEntry(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceRemoveTrackEntry((uint32_t)ctx->rdi, (uint32_t)ctx->rsi));
}

void shim_sceMusicPlayerServiceGetTrackListVersion(GuestContext *ctx) {
    mps_return(ctx, sceMusicPlayerServiceGetTrackListVersion((int32_t)ctx->rdi,
                                                             (uint64_t *)MPS_GPTR(ctx, ctx->rsi)));
}
