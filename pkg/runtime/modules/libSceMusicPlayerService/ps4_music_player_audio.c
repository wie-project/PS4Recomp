// SPDX-License-Identifier: GPL-2.0-or-later

// Decoding and output for libSceMusicPlayerService (see ps4_music_player_audio.h).

#include "ps4_music_player_audio.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---------------------------------------------------------------- decoding

// The extensions shadPS4's PlaybackWorker hands to a decoder (dr_flac, minimp3, FFmpeg). They
// are compared exactly, as shadPS4 does: ".Mp3" is not decoded.
static bool decodable_extension(const char *path) {
    static const char *const k_exts[] = {".flac", ".FLAC", ".mp3", ".MP3", ".m4a", ".M4A",
                                         ".aac",  ".AAC",  ".mp4", ".MP4"};
    const char *slash = strrchr(path, '/');
    const char *dot = strrchr(slash ? slash : path, '.');
    if (!dot) return false;
    for (size_t i = 0; i < sizeof(k_exts) / sizeof(k_exts[0]); i++) {
        if (strcmp(dot, k_exts[i]) == 0) return true;
    }
    return false;
}

bool ps4_mps_decode_file(const char *host_path, Ps4MpsPcm *out) {
    memset(out, 0, sizeof(*out));
    if (!decodable_extension(host_path)) return false;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, (const UInt8 *)host_path, (CFIndex)strlen(host_path), false);
    if (!url) return false;
    ExtAudioFileRef file = NULL;
    OSStatus st = ExtAudioFileOpenURL(url, &file);
    CFRelease(url);
    if (st != noErr) return false;

    bool ok = false;
    int16_t *buf = NULL;
    AudioStreamBasicDescription src;
    UInt32 size = sizeof(src);
    st = ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &size, &src);
    const uint32_t channels = src.mChannelsPerFrame;
    const uint32_t hz = (uint32_t)src.mSampleRate;
    if (st != noErr || channels == 0 || hz == 0) goto done;

    // Signed 16-bit interleaved at the file's own rate and channel count, as the decoders of
    // shadPS4 deliver it (its FFmpeg path keeps the codec's layout and rate too).
    AudioStreamBasicDescription pcm;
    memset(&pcm, 0, sizeof(pcm));
    pcm.mSampleRate = src.mSampleRate;
    pcm.mFormatID = kAudioFormatLinearPCM;
    pcm.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    pcm.mChannelsPerFrame = channels;
    pcm.mBitsPerChannel = 16;
    pcm.mBytesPerFrame = 2 * channels;
    pcm.mFramesPerPacket = 1;
    pcm.mBytesPerPacket = 2 * channels;
    if (ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof(pcm), &pcm) !=
        noErr) {
        goto done;
    }

    // The length is exact for most formats and an estimate for some (MP3): it only sizes the
    // first allocation.
    SInt64 frames_hint = 0;
    size = sizeof(frames_hint);
    if (ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileLengthFrames, &size,
                                &frames_hint) != noErr ||
        frames_hint <= 0) {
        frames_hint = (SInt64)hz;
    }
    size_t cap_frames = (size_t)frames_hint + 4096;
    size_t frames = 0;
    buf = (int16_t *)malloc(cap_frames * channels * sizeof(int16_t));
    if (!buf) goto done;
    for (;;) {
        if (cap_frames - frames < 4096) {
            cap_frames *= 2;
            int16_t *grown = (int16_t *)realloc(buf, cap_frames * channels * sizeof(int16_t));
            if (!grown) goto done;
            buf = grown;
        }
        AudioBufferList list;
        list.mNumberBuffers = 1;
        list.mBuffers[0].mNumberChannels = channels;
        list.mBuffers[0].mDataByteSize = (UInt32)(4096 * channels * sizeof(int16_t));
        list.mBuffers[0].mData = buf + frames * channels;
        UInt32 n = 4096;
        if (ExtAudioFileRead(file, &n, &list) != noErr) goto done;
        if (n == 0) break;
        frames += n;
    }
    if (frames == 0) goto done;
    out->buffer = buf;
    out->samples = frames * channels;
    out->channels = channels;
    out->hz = hz;
    buf = NULL;
    ok = true;
done:
    free(buf);
    ExtAudioFileDispose(file);
    return ok;
}

// ---------------------------------------------------------------- output stream

// The player keeps at most five 2048-frame chunks queued; eight leave headroom.
#define MPS_RING_FRAMES (8u * 2048u)

struct Ps4MpsStream {
    pthread_mutex_t mu;
    int16_t *ring;
    size_t cap; // samples
    size_t rd, fill;
    uint32_t channels, hz;
    float gain;
    bool paused;
    AudioComponentInstance unit; // NULL: PS4_AUDIO_NULL, played by the host clock
    uint64_t clock_us;           // PS4_AUDIO_NULL: playback accounted up to this time
};

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static bool audio_null(void) {
    const char *e = getenv("PS4_AUDIO_NULL");
    return e && e[0] && strcmp(e, "0") != 0;
}

// Lock held. Removes up to n samples from the front of the ring.
static void ring_drop(Ps4MpsStream *s, size_t n) {
    if (n > s->fill) n = s->fill;
    s->rd = (s->rd + n) % s->cap;
    s->fill -= n;
}

// Lock held, PS4_AUDIO_NULL only: plays what the host clock says has been played since the last
// call. Whole frames only; the remainder stays in clock_us.
static void clock_advance(Ps4MpsStream *s) {
    const uint64_t now = now_us();
    if (s->paused || s->fill == 0) {
        s->clock_us = now;
        return;
    }
    const uint64_t frames = (now - s->clock_us) * s->hz / 1000000u;
    const uint64_t queued_frames = s->fill / s->channels;
    if (frames >= queued_frames) {
        ring_drop(s, s->fill);
        s->clock_us = now;
        return;
    }
    ring_drop(s, (size_t)frames * s->channels);
    s->clock_us += frames * 1000000u / s->hz;
}

static OSStatus render_cb(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts,
                          UInt32 bus, UInt32 frames, AudioBufferList *io) {
    (void)flags, (void)ts, (void)bus;
    Ps4MpsStream *s = (Ps4MpsStream *)ref;
    float *dst = (float *)io->mBuffers[0].mData;
    const size_t want = (size_t)frames * s->channels;
    pthread_mutex_lock(&s->mu);
    const float g = s->gain * (1.0f / 32768.0f);
    size_t i = 0;
    for (; i < want && s->fill > 0; i++) {
        dst[i] = (float)s->ring[s->rd] * g;
        s->rd = (s->rd + 1) % s->cap;
        s->fill--;
    }
    pthread_mutex_unlock(&s->mu);
    for (; i < want; i++) dst[i] = 0.0f;
    return noErr;
}

static AudioComponentInstance unit_open(Ps4MpsStream *s) {
    AudioComponentDescription desc;
    memset(&desc, 0, sizeof(desc));
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    AudioComponentInstance unit = NULL;
    if (!comp || AudioComponentInstanceNew(comp, &unit) != noErr) return NULL;
    // Float input: the gain is applied while converting, the unit resamples to the device.
    AudioStreamBasicDescription asbd;
    memset(&asbd, 0, sizeof(asbd));
    asbd.mSampleRate = s->hz;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    asbd.mChannelsPerFrame = s->channels;
    asbd.mBitsPerChannel = 32;
    asbd.mBytesPerFrame = 4 * s->channels;
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerPacket = 4 * s->channels;
    AURenderCallbackStruct cb = {.inputProc = render_cb, .inputProcRefCon = s};
    if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd,
                             sizeof(asbd)) != noErr ||
        AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0,
                             &cb, sizeof(cb)) != noErr ||
        AudioUnitInitialize(unit) != noErr) {
        AudioComponentInstanceDispose(unit);
        return NULL;
    }
    return unit;
}

Ps4MpsStream *ps4_mps_stream_open(uint32_t channels, uint32_t hz) {
    if (channels == 0 || hz == 0) return NULL;
    Ps4MpsStream *s = (Ps4MpsStream *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->cap = (size_t)MPS_RING_FRAMES * channels;
    s->ring = (int16_t *)malloc(s->cap * sizeof(int16_t));
    if (!s->ring) {
        free(s);
        return NULL;
    }
    pthread_mutex_init(&s->mu, NULL);
    s->channels = channels;
    s->hz = hz;
    s->gain = 1.0f;
    // SDL_OpenAudioDeviceStream opens the stream paused; the player resumes it right away.
    s->paused = true;
    s->clock_us = now_us();
    if (!audio_null()) {
        s->unit = unit_open(s);
        if (!s->unit) {
            fprintf(stderr, "[MusicPlayer] cannot open the default output device\n");
            ps4_mps_stream_destroy(s);
            return NULL;
        }
    }
    return s;
}

void ps4_mps_stream_destroy(Ps4MpsStream *s) {
    if (!s) return;
    if (s->unit) {
        AudioOutputUnitStop(s->unit); // waits for a running render callback
        AudioUnitUninitialize(s->unit);
        AudioComponentInstanceDispose(s->unit);
    }
    pthread_mutex_destroy(&s->mu);
    free(s->ring);
    free(s);
}

bool ps4_mps_stream_put(Ps4MpsStream *s, const int16_t *samples, size_t count) {
    pthread_mutex_lock(&s->mu);
    if (!s->unit) clock_advance(s);
    const bool fits = count <= s->cap - s->fill;
    if (fits) {
        size_t wr = (s->rd + s->fill) % s->cap;
        const size_t first = count < s->cap - wr ? count : s->cap - wr;
        memcpy(s->ring + wr, samples, first * sizeof(int16_t));
        memcpy(s->ring, samples + first, (count - first) * sizeof(int16_t));
        s->fill += count;
    }
    pthread_mutex_unlock(&s->mu);
    return fits;
}

size_t ps4_mps_stream_queued_bytes(Ps4MpsStream *s) {
    pthread_mutex_lock(&s->mu);
    if (!s->unit) clock_advance(s);
    const size_t bytes = s->fill * sizeof(int16_t);
    pthread_mutex_unlock(&s->mu);
    return bytes;
}

void ps4_mps_stream_clear(Ps4MpsStream *s) {
    pthread_mutex_lock(&s->mu);
    ring_drop(s, s->fill);
    s->clock_us = now_us();
    pthread_mutex_unlock(&s->mu);
}

void ps4_mps_stream_set_paused(Ps4MpsStream *s, bool paused) {
    pthread_mutex_lock(&s->mu);
    const bool change = s->paused != paused;
    if (change && !s->unit) clock_advance(s); // account the time played before a pause
    s->paused = paused;
    if (change) s->clock_us = now_us();
    pthread_mutex_unlock(&s->mu);
    // Start / Stop wait for the render callback, so they run without the lock.
    if (change && s->unit) {
        if (paused) {
            AudioOutputUnitStop(s->unit);
        } else {
            AudioOutputUnitStart(s->unit);
        }
    }
}

void ps4_mps_stream_set_gain(Ps4MpsStream *s, float gain) {
    pthread_mutex_lock(&s->mu);
    s->gain = gain;
    pthread_mutex_unlock(&s->mu);
}
