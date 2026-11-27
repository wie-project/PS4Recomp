// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_MUSIC_PLAYER_AUDIO_H
#define PS4_MUSIC_PLAYER_AUDIO_H

// Host side of libSceMusicPlayerService: decoding a music file and the output stream it plays
// through. shadPS4 decodes with dr_flac, minimp3 and FFmpeg and plays through an SDL audio
// stream; here AudioToolbox does both (ExtAudioFile, a default-output AudioUnit).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A whole decoded file: interleaved signed 16-bit samples.
typedef struct {
    int16_t *buffer; // malloc'ed
    size_t samples;  // channel samples (frames * channels)
    uint32_t channels;
    uint32_t hz;
} Ps4MpsPcm;

// Decodes the file at host_path when its extension is one shadPS4 decodes (.flac, .mp3, .m4a,
// .aac, .mp4, lower or upper case). Returns false for any other file, a decoding failure or an
// empty file.
bool ps4_mps_decode_file(const char *host_path, Ps4MpsPcm *out);

// An output stream with the parts of an SDL audio stream the player uses: data is queued with
// put and played at the stream's rate; queued_bytes is what has not been played yet.
//
// PS4_AUDIO_NULL (the runtime's headless audio switch, see libSceAudioOut) gives a stream that
// opens no device and plays at the rate of the host clock, so the player keeps its timing.
typedef struct Ps4MpsStream Ps4MpsStream;

// NULL when the host device cannot be opened.
Ps4MpsStream *ps4_mps_stream_open(uint32_t channels, uint32_t hz);
void ps4_mps_stream_destroy(Ps4MpsStream *s);
// Queues samples (interleaved, the stream's channel count). Returns false when the queue has no
// room for them (the player keeps the queue short, so that is a caller error).
bool ps4_mps_stream_put(Ps4MpsStream *s, const int16_t *samples, size_t count);
size_t ps4_mps_stream_queued_bytes(Ps4MpsStream *s);
// Drops everything queued (SDL_ClearAudioStream).
void ps4_mps_stream_clear(Ps4MpsStream *s);
// Pauses or resumes the device; paused, nothing is played and the queue stays.
void ps4_mps_stream_set_paused(Ps4MpsStream *s, bool paused);
// Linear gain applied when the samples are played.
void ps4_mps_stream_set_gain(Ps4MpsStream *s, float gain);

#ifdef __cplusplus
}
#endif

#endif // PS4_MUSIC_PLAYER_AUDIO_H
