// SPDX-License-Identifier: GPL-2.0-or-later

// libSceMusicPlayerService against shadPS4's music_player_service.cpp: decoding (lossless FLAC
// compared sample by sample, AAC in an .m4a by length and level, extensions shadPS4 does not
// decode), track lists and transactions, the status and argument checks, and playback on the
// headless output stream (PS4_AUDIO_NULL), which plays at the rate of the host clock.

#include "ps4_music_player_audio.h"
#include "ps4_music_player_service.h"
#include "ps4_vfs.h"

#include <AudioToolbox/AudioToolbox.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../host_guest_mem.h"

recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

static char g_dir[1024];

// VFS double: /mnt/usb3/ is the test directory; nothing else is mounted.
int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz) {
    static const char k_usb[] = "/mnt/usb3/";
    if (strncmp(guest_path, k_usb, sizeof(k_usb) - 1) != 0) return -2;
    snprintf(host_path, host_path_sz, "%s/%s", g_dir, guest_path + sizeof(k_usb) - 1);
    return 0;
}

static int make_dir(void) {
    snprintf(g_dir, sizeof(g_dir), "%s/mps-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    return mkdtemp(g_dir) ? 0 : 1;
}

static int16_t sample_at(uint32_t frame, uint32_t ch, uint32_t hz) {
    return (int16_t)(12000.0 * sin(2.0 * M_PI * (440.0 + 110.0 * ch) * frame / hz));
}

// Writes frames of a sine per channel with AudioToolbox's encoder for the file type.
static bool write_file(const char *path, AudioFileTypeID type, UInt32 format, uint32_t channels,
                       uint32_t hz, uint32_t frames) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path,
                                                           (CFIndex)strlen(path), false);
    AudioStreamBasicDescription dst;
    memset(&dst, 0, sizeof(dst));
    dst.mSampleRate = hz;
    dst.mFormatID = format;
    dst.mChannelsPerFrame = channels;
    if (format == kAudioFormatFLAC) {
        dst.mFormatFlags = kAppleLosslessFormatFlag_16BitSourceData;
        dst.mFramesPerPacket = 4096;
    }
    ExtAudioFileRef f = NULL;
    OSStatus st = ExtAudioFileCreateWithURL(url, type, &dst, NULL, kAudioFileFlags_EraseFile, &f);
    CFRelease(url);
    if (st != noErr) {
        fprintf(stderr, "create %s: %d\n", path, (int)st);
        return false;
    }
    AudioStreamBasicDescription pcm = {.mSampleRate = hz,
                                       .mFormatID = kAudioFormatLinearPCM,
                                       .mFormatFlags = kAudioFormatFlagIsSignedInteger |
                                                       kAudioFormatFlagIsPacked,
                                       .mChannelsPerFrame = channels,
                                       .mBitsPerChannel = 16,
                                       .mBytesPerFrame = 2 * channels,
                                       .mFramesPerPacket = 1,
                                       .mBytesPerPacket = 2 * channels};
    bool ok = ExtAudioFileSetProperty(f, kExtAudioFileProperty_ClientDataFormat, sizeof(pcm), &pcm) ==
              noErr;
    int16_t *buf = (int16_t *)malloc((size_t)frames * channels * sizeof(int16_t));
    for (uint32_t i = 0; i < frames; i++) {
        for (uint32_t c = 0; c < channels; c++) buf[i * channels + c] = sample_at(i, c, hz);
    }
    AudioBufferList list = {.mNumberBuffers = 1};
    list.mBuffers[0].mNumberChannels = channels;
    list.mBuffers[0].mDataByteSize = frames * channels * 2;
    list.mBuffers[0].mData = buf;
    ok = ok && ExtAudioFileWrite(f, frames, &list) == noErr;
    ok = ExtAudioFileDispose(f) == noErr && ok;
    free(buf);
    return ok;
}

static void path_in_dir(char *out, size_t n, const char *name) {
    snprintf(out, n, "%s/%s", g_dir, name);
}

static int case_decode(void) {
    CHECK(make_dir() == 0);
    char path[1200];

    // FLAC is lossless: every sample comes back.
    path_in_dir(path, sizeof(path), "tone.flac");
    CHECK(write_file(path, kAudioFileFLACType, kAudioFormatFLAC, 2, 48000, 48000));
    Ps4MpsPcm pcm;
    CHECK(ps4_mps_decode_file(path, &pcm));
    CHECK(pcm.channels == 2 && pcm.hz == 48000 && pcm.samples == 96000);
    for (uint32_t i = 0; i < 48000; i++) {
        CHECK(pcm.buffer[i * 2] == sample_at(i, 0, 48000));
        CHECK(pcm.buffer[i * 2 + 1] == sample_at(i, 1, 48000));
    }
    free(pcm.buffer);

    // AAC in MPEG-4 (the FFmpeg path of shadPS4): the length without the encoder priming and a
    // level close to the source's.
    path_in_dir(path, sizeof(path), "tone.m4a");
    CHECK(write_file(path, kAudioFileM4AType, kAudioFormatMPEG4AAC, 1, 44100, 44100));
    CHECK(ps4_mps_decode_file(path, &pcm));
    CHECK(pcm.channels == 1 && pcm.hz == 44100);
    CHECK(pcm.samples >= 44100 && pcm.samples <= 44100 + 2048);
    double sq = 0.0;
    for (size_t i = 4096; i < 40000; i++) sq += (double)pcm.buffer[i] * pcm.buffer[i];
    const double rms = sqrt(sq / (40000 - 4096));
    CHECK(fabs(rms - 12000.0 / sqrt(2.0)) < 600.0);
    free(pcm.buffer);

    // shadPS4 picks the decoder by the exact extension: others are not decoded.
    char other[1200];
    path_in_dir(other, sizeof(other), "other.Flac");
    path_in_dir(path, sizeof(path), "tone.flac");
    CHECK(link(path, other) == 0);
    CHECK(!ps4_mps_decode_file(other, &pcm));
    path_in_dir(other, sizeof(other), "tone.wav");
    CHECK(link(path, other) == 0);
    CHECK(!ps4_mps_decode_file(other, &pcm));
    path_in_dir(other, sizeof(other), "missing.flac");
    CHECK(!ps4_mps_decode_file(other, &pcm));
    printf("PASS decode\n");
    return 0;
}

// Track entries of SetTrackList: the URI at the start of each 0xc2f-byte entry.
static char g_entries[4][PS4_MPS_TRACK_ENTRY2_SIZE];

static uint32_t track_total(void) {
    OrbisMusicPlayerPlayStatusExtension st = {.size = sizeof(st)};
    return sceMusicPlayerServiceGetPlayStatusExtension(&st) == 0 ? st.total_track_count : 9999;
}

static int case_lists(void) {
    uint64_t v0 = 0, v = 0;
    CHECK(sceMusicPlayerServiceGetTrackListVersion(0, NULL) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceGetTrackListVersion(0, &v0) == 0 && v0 == 1);

    CHECK(sceMusicPlayerServiceSetTrackList(0, NULL, 2) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceSetTrackList(0, g_entries, 0) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    // SetTrackList entries are 0xc2f bytes apart, SetTrackList2 ones 0xc48.
    char *flat = (char *)g_entries;
    strcpy(flat, "file:///a.flac");
    strcpy(flat + PS4_MPS_TRACK_ENTRY_SIZE, "file:///b.flac");
    strcpy(flat + 2 * PS4_MPS_TRACK_ENTRY_SIZE, "file:///c.flac");
    CHECK(sceMusicPlayerServiceSetTrackList(0, g_entries, 3) == 0);
    CHECK(track_total() == 3);
    CHECK(sceMusicPlayerServiceGetTrackListVersion(0, &v) == 0 && v == v0 + 1);
    CHECK(sceMusicPlayerServiceSetTrackList2(0, g_entries, 2, 0) == 0);
    CHECK(track_total() == 2);

    // Transactions.
    CHECK(sceMusicPlayerServiceEndTransaction(0) == ORBIS_MUSIC_PLAYER_SERVICE_ERROR_NOT_IN_TRANSACTION);
    CHECK(sceMusicPlayerServiceCreateTrackList(1) == ORBIS_MUSIC_PLAYER_SERVICE_ERROR_NOT_IN_TRANSACTION);
    CHECK(sceMusicPlayerServiceInsertTrackEntry(0, 0, "x") ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_NOT_IN_TRANSACTION);
    CHECK(sceMusicPlayerServiceRemoveTrackEntry(0, 0) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_NOT_IN_TRANSACTION);
    CHECK(sceMusicPlayerServiceRemoveTrackList(0) == ORBIS_MUSIC_PLAYER_SERVICE_ERROR_NOT_IN_TRANSACTION);
    // Argument checks come before the transaction check.
    CHECK(sceMusicPlayerServiceCreateTrackList(100) == ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceInsertTrackEntry(0, 4000, "x") ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceInsertTrackEntry(0, 0, NULL) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceRemoveTrackEntry(100, 0) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);

    CHECK(sceMusicPlayerServiceBeginTransaction(0) == 0);
    CHECK(sceMusicPlayerServiceBeginTransaction(0) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_ALREADY_IN_TRANSACTION);
    // List 0 holds the two tracks of SetTrackList2; edits show only after EndTransaction.
    CHECK(sceMusicPlayerServiceInsertTrackEntry(0, 0, "file:///first.flac") == 0);
    CHECK(sceMusicPlayerServiceInsertTrackEntry(0, 3999, "file:///last.flac") == 0); // appended
    CHECK(sceMusicPlayerServiceRemoveTrackEntry(0, 1) == 0);    // a.flac
    CHECK(sceMusicPlayerServiceRemoveTrackEntry(0, 3000) == 0); // past the end: nothing
    CHECK(sceMusicPlayerServiceRemoveTrackEntry(7, 0) == 0);    // no such list: nothing
    CHECK(sceMusicPlayerServiceCreateTrackList(5) == 0);
    CHECK(sceMusicPlayerServiceInsertTrackEntry(5, 0, "file:///other.flac") == 0);
    CHECK(track_total() == 2);
    CHECK(sceMusicPlayerServiceGetTrackListVersion(0, &v0) == 0);
    CHECK(sceMusicPlayerServiceEndTransaction(0) == 0);
    CHECK(track_total() == 3); // first, b, last
    CHECK(sceMusicPlayerServiceGetTrackListVersion(0, &v) == 0 && v == v0 + 1);

    // Removing list 0 in a transaction empties the playing list at once.
    CHECK(sceMusicPlayerServiceBeginTransaction(0) == 0);
    CHECK(sceMusicPlayerServiceRemoveTrackList(0) == 0);
    CHECK(track_total() == 0);
    CHECK(sceMusicPlayerServiceEndTransaction(0) == 0);
    CHECK(track_total() == 0); // no list 0 any more: the playing list stays as it is

    // RemoveAllData drops every list.
    CHECK(sceMusicPlayerServiceSetTrackList(0, g_entries, 3) == 0);
    CHECK(sceMusicPlayerServiceGetTrackListVersion(0, &v0) == 0);
    CHECK(sceMusicPlayerServiceRemoveAllData(0) == 0);
    CHECK(track_total() == 0);
    CHECK(sceMusicPlayerServiceGetTrackListVersion(0, &v) == 0 && v == v0 + 1);
    CHECK(sceMusicPlayerServiceBeginTransaction(0) == 0);
    CHECK(sceMusicPlayerServiceEndTransaction(0) == 0);
    CHECK(track_total() == 0);
    printf("PASS lists\n");
    return 0;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int case_status(void) {
    // Initialize3 needs a pool of at least 2 MiB; the other two accept anything (stubs).
    static uint8_t pool[16];
    CHECK(sceMusicPlayerServiceInitialize(0, NULL) == 0);
    CHECK(sceMusicPlayerServiceInitialize2(0, NULL) == 0);
    CHECK(sceMusicPlayerServiceInitialize3(0x1fffff, pool, 0, 0) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceInitialize3(0x200000, NULL, 0, 0) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceInitialize3(0x200000, pool, 0, 0) == 0);

    int32_t vol = -1, mute = -1;
    CHECK(sceMusicPlayerServiceGetAudioVolume(&vol, &mute) == 0 && vol == 25 && mute == 0);
    CHECK(sceMusicPlayerServiceGetAudioVolume(NULL, NULL) == 0);
    CHECK(sceMusicPlayerServiceSetAudioVolume(26) == ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceSetAudioVolume(-1) == ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceSetAudioVolume(10) == 0);
    CHECK(sceMusicPlayerServiceSetShuffle(2) == ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceSetRepeatMode(3) == ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);

    OrbisMusicPlayerPlayStatusExtension ext = {.size = 0x40};
    CHECK(sceMusicPlayerServiceGetPlayStatusExtension(&ext) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceGetPlayStatusExtension(NULL) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceGetCurrentPlayStatus(NULL) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);

    // Skip flags at the list boundaries: 1 = cannot skip.
    char *flat = (char *)g_entries;
    strcpy(flat, "file:///a.flac");
    strcpy(flat + PS4_MPS_TRACK_ENTRY_SIZE, "file:///b.flac");
    CHECK(sceMusicPlayerServiceSetTrackList(0, g_entries, 2) == 0);
    ext.size = sizeof(ext);
    CHECK(sceMusicPlayerServiceGetPlayStatusExtension(&ext) == 0);
    CHECK(ext.play_state == 2 && ext.volume == 10 && ext.total_track_count == 2);
    CHECK(ext.track_index == 0 && ext.can_skip_next == 0 && ext.can_skip_previous == 1);
    CHECK(sceMusicPlayerServiceSetRepeatMode(1) == 0);
    CHECK(sceMusicPlayerServiceGetPlayStatusExtension(&ext) == 0);
    CHECK(ext.repeat_mode == 1 && ext.can_skip_next == 0 && ext.can_skip_previous == 0);
    CHECK(sceMusicPlayerServiceSetRepeatMode(0) == 0);
    CHECK(sceMusicPlayerServiceSetShuffle(1) == 0);
    SceMusicPlayerServicePlayStatus st;
    memset(&st, 0xff, sizeof(st));
    CHECK(sceMusicPlayerServiceGetCurrentPlayStatus(&st) == 0);
    CHECK(st.result == 0 && st.play_status == 2 && st.shuffle == 1 && st.repeat == 0);
    CHECK(sceMusicPlayerServiceSetShuffle(0) == 0);
    CHECK(!ps4_music_player_is_bgm_playing());

    // No event is ever posted: ReceiveEvent waits 50 ms and times out.
    uint8_t event[0x40];
    CHECK(sceMusicPlayerServiceReceiveEvent(NULL) == ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    const uint64_t t0 = now_ms();
    CHECK(sceMusicPlayerServiceReceiveEvent(event) == ORBIS_MUSIC_PLAYER_SERVICE_ERROR_TIMED_OUT);
    CHECK(now_ms() - t0 >= 45);

    // The shims read and write guest memory.
    GuestContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mem_base = GUEST_MEM;
    ctx.mem_size = ~0ull;
    vol = -1;
    ctx.rdi = A(&vol);
    shim_sceMusicPlayerServiceGetAudioVolume(&ctx);
    CHECK(ctx.rax == 0 && vol == 10);
    ctx.rdi = A(&ext);
    ctx.rax = 99;
    shim_sceMusicPlayerServiceGetPlayStatusExtension(&ctx);
    CHECK(ctx.rax == 0 && ext.volume == 10);
    ctx.rax = 99;
    shim_sceMusicPlayerServiceGetTrackInfo(&ctx); // a stub in shadPS4
    CHECK(ctx.rax == 0);
    printf("PASS status\n");
    return 0;
}

// Polls the play status until pred holds or timeout_ms passes.
static bool wait_for(bool (*pred)(const OrbisMusicPlayerPlayStatusExtension *), uint32_t timeout_ms) {
    const uint64_t end = now_ms() + timeout_ms;
    OrbisMusicPlayerPlayStatusExtension st = {.size = sizeof(st)};
    do {
        if (sceMusicPlayerServiceGetPlayStatusExtension(&st) == 0 && pred(&st)) return true;
        usleep(10000);
    } while (now_ms() < end);
    return false;
}

static bool on_track1(const OrbisMusicPlayerPlayStatusExtension *s) {
    return s->play_state == 0 && s->track_index == 1;
}
static bool stopped(const OrbisMusicPlayerPlayStatusExtension *s) { return s->play_state == 2; }
static bool at_one_second(const OrbisMusicPlayerPlayStatusExtension *s) {
    return s->current_time_s == 1;
}

static OrbisMusicPlayerPlayStatusExtension status_now(void) {
    OrbisMusicPlayerPlayStatusExtension st = {.size = sizeof(st)};
    sceMusicPlayerServiceGetPlayStatusExtension(&st);
    return st;
}

static int case_playback(void) {
    setenv("PS4_AUDIO_NULL", "1", 1);
    CHECK(make_dir() == 0);
    char path[1200];
    // Two 1.5 s tracks: one reached through a USB serial and an escaped name, one by host path.
    path_in_dir(path, sizeof(path), "track 0.flac");
    CHECK(write_file(path, kAudioFileFLACType, kAudioFormatFLAC, 2, 48000, 72000));
    path_in_dir(path, sizeof(path), "track1.m4a");
    CHECK(write_file(path, kAudioFileM4AType, kAudioFormatMPEG4AAC, 2, 44100, 66150));

    OrbisMusicPlayerUsbDeviceInfo usb[2];
    memset(usb, 0, sizeof(usb));
    usb[0].device_id = 3;
    strcpy(usb[0].serial_number, "SERIAL123");
    usb[0].serial_number_len = 9;
    usb[1].device_id = 4;
    strcpy(usb[1].serial_number, "TOOLONG");
    usb[1].serial_number_len = 3; // shorter than the string: rejected, the first device stays
    CHECK(sceMusicPlayerServiceSetUsbStorageDeviceInfo(usb, 0) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceSetUsbStorageDeviceInfo(usb, 9) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);
    CHECK(sceMusicPlayerServiceSetUsbStorageDeviceInfo(usb, 2) ==
          ORBIS_MUSIC_PLAYER_SERVICE_ERROR_INVALID_PARAMETER);

    char *flat = (char *)g_entries;
    strcpy(flat, "file:///SERIAL123/track%200.flac");
    snprintf(flat + PS4_MPS_TRACK_ENTRY2_SIZE, PS4_MPS_TRACK_ENTRY2_SIZE, "file://%s", path);
    strcpy(flat + 2 * PS4_MPS_TRACK_ENTRY2_SIZE, "file:///SERIAL123/missing.flac");
    CHECK(sceMusicPlayerServiceSetTrackList2(0, g_entries, 3, 0) == 0);

    // Track 0 plays, then the player moves on to track 1 by itself.
    CHECK(sceMusicPlayerServicePlay(0, 0, 0, 0) == 0);
    usleep(300000);
    OrbisMusicPlayerPlayStatusExtension st = status_now();
    CHECK(st.play_state == 0 && st.track_index == 0 && st.total_duration_s == 1);
    CHECK(ps4_music_player_is_bgm_playing());
    CHECK(wait_for(on_track1, 3000));
    // Track 2 does not exist: the player stops at it.
    CHECK(wait_for(stopped, 4000));
    CHECK(!ps4_music_player_is_bgm_playing());
    CHECK(status_now().track_index == 2);

    // Pause holds the position; Unpause carries on.
    CHECK(sceMusicPlayerServicePlay(0, 0, 0, 0) == 0);
    usleep(200000);
    CHECK(sceMusicPlayerServicePause() == 0);
    usleep(1500000); // longer than the track: paused, it does not end
    st = status_now();
    CHECK(st.play_state == 1 && st.track_index == 0 && st.current_time_s == 0);
    CHECK(ps4_music_player_is_bgm_playing());
    CHECK(sceMusicPlayerServiceUnpause() == 0);
    CHECK(status_now().play_state == 0);

    // Seek within the playing track; Play on the playing track with a start time seeks too.
    CHECK(sceMusicPlayerServiceSeek(0, 0, 1) == 0);
    CHECK(wait_for(at_one_second, 300));
    CHECK(status_now().track_index == 0);
    CHECK(sceMusicPlayerServiceStop() == 0);
    st = status_now();
    CHECK(st.play_state == 2 && st.current_time_s == 0 && st.total_duration_s == 0);

    // Repeat one: track 1 plays again instead of moving on.
    CHECK(sceMusicPlayerServiceSetRepeatMode(2) == 0);
    CHECK(sceMusicPlayerServicePlay(0, 1, 0, 0) == 0);
    usleep(2200000);
    st = status_now();
    CHECK(st.play_state == 0 && st.track_index == 1);
    CHECK(sceMusicPlayerServiceSetRepeatMode(0) == 0);

    // Previous / Next restart at the neighbouring track; Play of an index past the list stops.
    CHECK(sceMusicPlayerServicePreviousTrack() == 0);
    usleep(100000);
    CHECK(status_now().track_index == 0 && status_now().play_state == 0);
    CHECK(sceMusicPlayerServicePreviousTrack() == 0); // stays at 0
    CHECK(status_now().track_index == 0);
    CHECK(sceMusicPlayerServiceNextTrack() == 0);
    usleep(100000);
    CHECK(status_now().track_index == 1 && status_now().play_state == 0);
    CHECK(sceMusicPlayerServicePlay(0, 7, 0, 0) == 0);
    CHECK(status_now().play_state == 2);

    // Start time past the end of the track: nothing is left to play, so it moves on at once.
    CHECK(sceMusicPlayerServicePlayStartByTime(0, 0, 5) == 0);
    CHECK(wait_for(on_track1, 1000));
    CHECK(sceMusicPlayerServiceTerminate() == 0);
    CHECK(status_now().play_state == 2);
    printf("PASS playback\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "decode") == 0) return case_decode();
    if (strcmp(argv[1], "lists") == 0) return case_lists();
    if (strcmp(argv[1], "status") == 0) return case_status();
    if (strcmp(argv[1], "playback") == 0) return case_playback();
    return 2;
}
