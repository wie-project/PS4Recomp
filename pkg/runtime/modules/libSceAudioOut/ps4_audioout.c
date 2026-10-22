// libSceAudioOut HLE.
//
// Guest ports (sceAudioOutOpen) are converted to float stereo, scaled by the
// per-channel port volume (sceAudioOutSetVolume) and queued into a per-port
// ring buffer. A single host output (AudioToolbox default-output AudioUnit,
// float32 stereo @ 48 kHz; CoreAudio converts to the device format/rate) pulls
// from every ring buffer and mixes them in its render callback.
// sceAudioOutOutput/sceAudioOutOutputs block until the ring has room for the
// next grain, so the guest's audio thread is paced by the real device clock
// (like the PS4, where an output call blocks until the previous grain played).
//
// Port parameter / format / volume ABI details follow shadPS4's
// src/core/libraries/audio/audioout.{h,cpp} (Copyright 2024-2026 shadPS4
// Emulator Project, GPL-2.0-or-later).
//
// Other runtime modules (libSceAudio3d) use the ports through the host API in
// ps4_audioout_internal.h; the guest shims below are wrappers over it.
//
// Debug env vars:
//   PS4_AUDIO_LOG=1         log volume changes and per-port / final-mix
//                           RMS + peak every ~2 s.
//   PS4_AUDIO_DUMP=<path>   write the final mixed host output to a WAV file
//                           (float32 stereo 48 kHz).
//   PS4_AUDIO_MUTE=1        don't start the host output device (time-paced).
//   PS4_AUDIO_NULL=1        headless output: instead of the host device, a
//                           clock thread pulls and mixes 512 frames every
//                           10.67 ms, so pacing, mixing, stats and
//                           PS4_AUDIO_DUMP work without playing anything.
//   PS4_AUDIO_PADSPK=<gain> gain for PADSPK (controller speaker) ports mixed
//                           into the main output (default 0.5).

#include "ps4_audioout.h"
#include "ps4_audioout_internal.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <mach/mach_time.h>
#endif

#define MAX_AUDIO_PORTS 25
#define AUDIO_RATE 48000
#define VOLUME_0DB 32768

// Error codes: see ps4_audioout_internal.h.
#define ORBIS_AUDIO_OUT_ERROR_NOT_OPENED PS4_AUDIO_OUT_ERROR_NOT_OPENED
#define ORBIS_AUDIO_OUT_ERROR_INVALID_PORT PS4_AUDIO_OUT_ERROR_INVALID_PORT
#define ORBIS_AUDIO_OUT_ERROR_INVALID_POINTER PS4_AUDIO_OUT_ERROR_INVALID_POINTER
#define ORBIS_AUDIO_OUT_ERROR_PORT_FULL PS4_AUDIO_OUT_ERROR_PORT_FULL
#define ORBIS_AUDIO_OUT_ERROR_INVALID_SIZE PS4_AUDIO_OUT_ERROR_INVALID_SIZE
#define ORBIS_AUDIO_OUT_ERROR_INVALID_FORMAT PS4_AUDIO_OUT_ERROR_INVALID_FORMAT

#define RET_ERR(ctx, code) ((ctx)->rax = (uint64_t)(int64_t)(int32_t)(uint32_t)(code))

enum {
  PORT_MAIN = 0,
  PORT_BGM = 1,
  PORT_VOICE = 2,
  PORT_PERSONAL = 3,
  PORT_PADSPK = 4,
  PORT_AUX = 127,
};

typedef struct {
  int in_use;
  int32_t handle;
  int32_t type;
  uint32_t length; // grain, frames per output call
  uint32_t freq;
  uint32_t param;
  uint32_t format;
  int is_float;
  uint32_t sample_size;
  uint32_t channels;
  uint32_t bytes_per_frame;
  float vol[8];    // per-channel gain (0..1)
  float port_gain; // extra gain for this port type (PADSPK mix level)

  // Ring buffer of interleaved float stereo frames.
  float *ring;
  uint32_t ring_cap; // frames
  uint32_t ring_read;
  uint32_t ring_fill;
  uint32_t ring_limit; // writers block while fill + length > limit

  uint64_t next_sync_time_us; // fallback pacing when no host device
  uint64_t outputs;
  uint64_t underruns;
  // stats (guest input after volume, before mixing)
  double stat_sq;
  float stat_peak;
  uint64_t stat_frames;
  uint64_t stat_nonzero_bufs;
  double stat_ch_sq[8]; // raw guest channel energy (before volume)
} Ps4AudioPort;

static Ps4AudioPort g_ports[MAX_AUDIO_PORTS];
static pthread_mutex_t g_audio_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_audio_cond = PTHREAD_COND_INITIALIZER;
static bool g_audio_inited = false;
static int g_log = -1;
static float g_padspk_gain = 0.5f;

// host output state
static bool g_host_tried = false;
static bool g_host_running = false;
static uint64_t g_render_calls = 0;
static uint64_t g_render_frames = 0;
static uint64_t g_last_render_us = 0;
static double g_mix_sq = 0.0;
static float g_mix_peak = 0.0f;
static uint64_t g_mix_frames = 0;
static uint64_t g_last_stat_us = 0;

// WAV dump
static FILE *g_dump = NULL;
static uint64_t g_dump_frames = 0;
static uint64_t g_dump_frames_hdr = 0;

#ifdef __APPLE__
static AudioComponentInstance g_unit = NULL;
#endif

static uint64_t get_time_micros(void) {
#ifdef __APPLE__
  static mach_timebase_info_data_t timebase;
  if (timebase.denom == 0) {
    mach_timebase_info(&timebase);
  }
  uint64_t t = mach_absolute_time();
  return (t * timebase.numer) / (timebase.denom * 1000ULL);
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
#endif
}

static bool audio_log_enabled(void) {
  if (g_log < 0) {
    const char *e = getenv("PS4_AUDIO_LOG");
    g_log = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
  }
  return g_log == 1;
}

// ---------------------------------------------------------------- WAV dump

static void wav_write_header(FILE *f, uint64_t frames) {
  uint32_t data_bytes = (uint32_t)(frames * 8ULL);
  uint8_t h[44];
  uint32_t v;
  uint16_t s;
  memcpy(h, "RIFF", 4);
  v = 36 + data_bytes;
  memcpy(h + 4, &v, 4);
  memcpy(h + 8, "WAVEfmt ", 8);
  v = 16;
  memcpy(h + 16, &v, 4);
  s = 3; // IEEE float
  memcpy(h + 20, &s, 2);
  s = 2;
  memcpy(h + 22, &s, 2);
  v = AUDIO_RATE;
  memcpy(h + 24, &v, 4);
  v = AUDIO_RATE * 8;
  memcpy(h + 28, &v, 4);
  s = 8;
  memcpy(h + 32, &s, 2);
  s = 32;
  memcpy(h + 34, &s, 2);
  memcpy(h + 36, "data", 4);
  memcpy(h + 40, &data_bytes, 4);
  long pos = ftell(f);
  fseek(f, 0, SEEK_SET);
  fwrite(h, 1, 44, f);
  if (pos > 44) {
    fseek(f, pos, SEEK_SET);
  }
  fflush(f);
}

static void dump_open(void) {
  const char *p = getenv("PS4_AUDIO_DUMP");
  if (!p || !*p) {
    return;
  }
  g_dump = fopen(p, "wb");
  if (g_dump) {
    wav_write_header(g_dump, 0);
    fprintf(stderr, "[AudioOut] dumping final mix to %s\n", p);
  }
}

static void dump_write(const float *stereo, uint32_t frames) {
  if (!g_dump) {
    return;
  }
  fwrite(stereo, 8, frames, g_dump);
  g_dump_frames += frames;
  if (g_dump_frames - g_dump_frames_hdr >= AUDIO_RATE) {
    g_dump_frames_hdr = g_dump_frames;
    wav_write_header(g_dump, g_dump_frames);
  }
}

// ---------------------------------------------------------------- stats

// Called with g_audio_lock held.
static void audio_stats_maybe_log(void) {
  if (!audio_log_enabled()) {
    return;
  }
  uint64_t now = get_time_micros();
  if (g_last_stat_us == 0) {
    g_last_stat_us = now;
    return;
  }
  if (now - g_last_stat_us < 2000000ULL) {
    return;
  }
  g_last_stat_us = now;
  for (int i = 1; i < MAX_AUDIO_PORTS; i++) {
    Ps4AudioPort *p = &g_ports[i];
    if (!p->in_use) {
      continue;
    }
    double rms = p->stat_frames ? sqrt(p->stat_sq / (double)(p->stat_frames * 2)) : 0.0;
    fprintf(stderr,
            "[AudioOut] port %d type=%d fmt=%u ch=%u len=%u: outputs=%llu nonzero_bufs=%llu "
            "in_rms=%.5f in_peak=%.5f vol=[%.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f] fill=%u/%u "
            "underruns=%llu\n",
            i, p->type, p->format, p->channels, p->length, (unsigned long long)p->outputs,
            (unsigned long long)p->stat_nonzero_bufs, rms, p->stat_peak, p->vol[0], p->vol[1],
            p->vol[2], p->vol[3], p->vol[4], p->vol[5], p->vol[6], p->vol[7], p->ring_fill,
            p->ring_cap, (unsigned long long)p->underruns);
    if (p->channels == 8 && p->stat_frames) {
      double n = (double)p->stat_frames;
      fprintf(stderr,
              "[AudioOut] port %d raw ch rms: FL=%.5f FR=%.5f FC=%.5f LFE=%.5f c4=%.5f c5=%.5f "
              "c6=%.5f c7=%.5f\n",
              i, sqrt(p->stat_ch_sq[0] / n), sqrt(p->stat_ch_sq[1] / n), sqrt(p->stat_ch_sq[2] / n),
              sqrt(p->stat_ch_sq[3] / n), sqrt(p->stat_ch_sq[4] / n), sqrt(p->stat_ch_sq[5] / n),
              sqrt(p->stat_ch_sq[6] / n), sqrt(p->stat_ch_sq[7] / n));
    }
    memset(p->stat_ch_sq, 0, sizeof(p->stat_ch_sq));
    p->stat_sq = 0.0;
    p->stat_peak = 0.0f;
    p->stat_frames = 0;
    p->stat_nonzero_bufs = 0;
  }
  double mrms = g_mix_frames ? sqrt(g_mix_sq / (double)(g_mix_frames * 2)) : 0.0;
  fprintf(stderr,
          "[AudioOut] host: running=%d render_calls=%llu render_frames=%llu mix_rms=%.5f "
          "mix_peak=%.5f dump_frames=%llu\n",
          g_host_running ? 1 : 0, (unsigned long long)g_render_calls,
          (unsigned long long)g_render_frames, mrms, g_mix_peak,
          (unsigned long long)g_dump_frames);
  g_mix_sq = 0.0;
  g_mix_peak = 0.0f;
  g_mix_frames = 0;
}

// ---------------------------------------------------------------- mixing

// Mix `frames` stereo frames from all port rings into out (interleaved).
// Called with g_audio_lock held.
static void audio_mix(float *out, uint32_t frames) {
  memset(out, 0, (size_t)frames * 2 * sizeof(float));
  for (int i = 1; i < MAX_AUDIO_PORTS; i++) {
    Ps4AudioPort *p = &g_ports[i];
    if (!p->in_use || !p->ring) {
      continue;
    }
    uint32_t n = p->ring_fill < frames ? p->ring_fill : frames;
    if (n < frames && p->outputs > 0) {
      p->underruns++;
    }
    uint32_t r = p->ring_read;
    for (uint32_t f = 0; f < n; f++) {
      out[f * 2 + 0] += p->ring[r * 2 + 0];
      out[f * 2 + 1] += p->ring[r * 2 + 1];
      if (++r == p->ring_cap) r = 0;
    }
    p->ring_read = r;
    p->ring_fill -= n;
  }
  for (uint32_t f = 0; f < frames * 2; f++) {
    float v = out[f];
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    out[f] = v;
    float a = fabsf(v);
    if (a > g_mix_peak) g_mix_peak = a;
    g_mix_sq += (double)v * v;
  }
  g_mix_frames += frames;
}

#ifdef __APPLE__
static OSStatus audio_render_cb(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
                                const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
                                UInt32 inNumberFrames, AudioBufferList *ioData) {
  (void)inRefCon;
  (void)ioActionFlags;
  (void)inTimeStamp;
  (void)inBusNumber;
  float *out = (float *)ioData->mBuffers[0].mData;
  uint32_t frames = inNumberFrames;
  uint32_t cap = ioData->mBuffers[0].mDataByteSize / 8;
  if (frames > cap) frames = cap;
  pthread_mutex_lock(&g_audio_lock);
  audio_mix(out, frames);
  g_render_calls++;
  g_render_frames += frames;
  g_last_render_us = get_time_micros();
  dump_write(out, frames);
  pthread_cond_broadcast(&g_audio_cond);
  pthread_mutex_unlock(&g_audio_lock);
  return noErr;
}
#endif

// Headless output device (PS4_AUDIO_NULL): consumes and mixes the port rings
// at the device rate (48 kHz, 512-frame periods) without playing anything.
#define NULL_DEVICE_FRAMES 512
static void *audio_null_device_main(void *arg) {
  (void)arg;
  static float buf[NULL_DEVICE_FRAMES * 2];
  uint64_t start = get_time_micros();
  uint64_t frames = 0;
  for (;;) {
    pthread_mutex_lock(&g_audio_lock);
    audio_mix(buf, NULL_DEVICE_FRAMES);
    g_render_calls++;
    g_render_frames += NULL_DEVICE_FRAMES;
    g_last_render_us = get_time_micros();
    dump_write(buf, NULL_DEVICE_FRAMES);
    pthread_cond_broadcast(&g_audio_cond);
    pthread_mutex_unlock(&g_audio_lock);
    frames += NULL_DEVICE_FRAMES;
    uint64_t due = start + frames * 1000000ULL / AUDIO_RATE;
    uint64_t now = get_time_micros();
    if (due > now) {
      usleep((useconds_t)(due - now));
    } else if (now - due > 200000ULL) {
      // The process was stalled for a long time: resynchronise instead of
      // consuming the backlog in a burst (a real device drops it too).
      start = now;
      frames = 0;
    }
  }
  return NULL;
}

// Called with g_audio_lock held.
static void audio_host_start(void) {
  if (g_host_tried) {
    return;
  }
  g_host_tried = true;
  const char *pg = getenv("PS4_AUDIO_PADSPK");
  if (pg && *pg) {
    g_padspk_gain = (float)atof(pg);
  }
  dump_open();
  const char *mute = getenv("PS4_AUDIO_MUTE");
  if (mute && *mute && strcmp(mute, "0") != 0) {
    fprintf(stderr, "[AudioOut] PS4_AUDIO_MUTE set; host output disabled\n");
    return;
  }
  const char *null_dev = getenv("PS4_AUDIO_NULL");
  if (null_dev && *null_dev && strcmp(null_dev, "0") != 0) {
    pthread_t t;
    if (pthread_create(&t, NULL, audio_null_device_main, NULL) == 0) {
      pthread_detach(t);
      g_host_running = true;
      g_last_render_us = get_time_micros();
      fprintf(stderr, "[AudioOut] PS4_AUDIO_NULL set; headless output (clock-paced mix, "
                      "nothing played)\n");
    } else {
      fprintf(stderr, "[AudioOut] PS4_AUDIO_NULL: thread creation failed; host output disabled\n");
    }
    return;
  }
#ifdef __APPLE__
  AudioComponentDescription desc;
  memset(&desc, 0, sizeof(desc));
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_DefaultOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  AudioComponent comp = AudioComponentFindNext(NULL, &desc);
  if (!comp) {
    fprintf(stderr, "[AudioOut] no default output AudioComponent\n");
    return;
  }
  OSStatus st = AudioComponentInstanceNew(comp, &g_unit);
  if (st != noErr) {
    fprintf(stderr, "[AudioOut] AudioComponentInstanceNew failed: %d\n", (int)st);
    g_unit = NULL;
    return;
  }
  AudioStreamBasicDescription asbd;
  memset(&asbd, 0, sizeof(asbd));
  asbd.mSampleRate = AUDIO_RATE;
  asbd.mFormatID = kAudioFormatLinearPCM;
  asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  asbd.mChannelsPerFrame = 2;
  asbd.mBitsPerChannel = 32;
  asbd.mBytesPerFrame = 8;
  asbd.mFramesPerPacket = 1;
  asbd.mBytesPerPacket = 8;
  st = AudioUnitSetProperty(g_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                            &asbd, sizeof(asbd));
  if (st != noErr) {
    fprintf(stderr, "[AudioOut] set stream format failed: %d\n", (int)st);
  }
  AURenderCallbackStruct cb;
  cb.inputProc = audio_render_cb;
  cb.inputProcRefCon = NULL;
  st = AudioUnitSetProperty(g_unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
                            0, &cb, sizeof(cb));
  if (st != noErr) {
    fprintf(stderr, "[AudioOut] set render callback failed: %d\n", (int)st);
  }
  st = AudioUnitInitialize(g_unit);
  if (st != noErr) {
    fprintf(stderr, "[AudioOut] AudioUnitInitialize failed: %d\n", (int)st);
    AudioComponentInstanceDispose(g_unit);
    g_unit = NULL;
    return;
  }
  // AudioOutputUnitStart does not invoke the render callback synchronously, so
  // it is safe to call with g_audio_lock held.
  st = AudioOutputUnitStart(g_unit);
  if (st != noErr) {
    fprintf(stderr, "[AudioOut] AudioOutputUnitStart failed: %d\n", (int)st);
    AudioUnitUninitialize(g_unit);
    AudioComponentInstanceDispose(g_unit);
    g_unit = NULL;
    return;
  }
  g_host_running = true;
  g_last_render_us = get_time_micros();
  fprintf(stderr, "[AudioOut] host output started (default device, float32 stereo 48 kHz)\n");
#endif
}

// ---------------------------------------------------------------- formats

static int audio_format_info(uint32_t fmt, int *is_float, uint32_t *ssize, uint32_t *ch) {
  switch (fmt) {
  case 0: *is_float = 0; *ssize = 2; *ch = 1; return 0; // S16 mono
  case 1: *is_float = 0; *ssize = 2; *ch = 2; return 0; // S16 stereo
  case 2: *is_float = 0; *ssize = 2; *ch = 8; return 0; // S16 8ch
  case 3: *is_float = 1; *ssize = 4; *ch = 1; return 0; // float mono
  case 4: *is_float = 1; *ssize = 4; *ch = 2; return 0; // float stereo
  case 5: *is_float = 1; *ssize = 4; *ch = 8; return 0; // float 8ch
  case 6: *is_float = 0; *ssize = 2; *ch = 8; return 0; // S16 8ch STD
  case 7: *is_float = 1; *ssize = 4; *ch = 8; return 0; // float 8ch STD
  default: return -1;
  }
}

static inline float audio_sample(const uint8_t *src, int is_float, uint32_t idx) {
  if (is_float) {
    float v;
    memcpy(&v, src + (size_t)idx * 4, 4);
    if (!(v == v)) v = 0.0f; // NaN guard
    return v;
  }
  int16_t s;
  memcpy(&s, src + (size_t)idx * 2, 2);
  return (float)s * (1.0f / 32768.0f);
}

// Convert one grain of guest PCM into stereo float (with volume applied).
// 8ch input is FL FR FC LFE + two surround pairs (pair order differs between
// the normal and "STD" layouts, but both pairs are L,R so the downmix is the
// same). LFE is dropped.
static void audio_convert(Ps4AudioPort *p, const uint8_t *src, float *dst, uint32_t len) {
  const float g = p->port_gain;
  const float k = 0.7071f;
  int nonzero = 0;
  for (uint32_t f = 0; f < len; f++) {
    float l, r;
    if (p->channels == 1) {
      float m = audio_sample(src, p->is_float, f) * p->vol[0];
      l = m;
      r = m;
    } else if (p->channels == 2) {
      l = audio_sample(src, p->is_float, f * 2 + 0) * p->vol[0];
      r = audio_sample(src, p->is_float, f * 2 + 1) * p->vol[1];
    } else {
      float c[8];
      for (int ch = 0; ch < 8; ch++) {
        float raw = audio_sample(src, p->is_float, f * 8 + ch);
        p->stat_ch_sq[ch] += (double)raw * raw;
        c[ch] = raw * p->vol[ch];
      }
      l = c[0] + k * c[2] + k * c[4] + k * c[6];
      r = c[1] + k * c[2] + k * c[5] + k * c[7];
    }
    l *= g;
    r *= g;
    dst[f * 2 + 0] = l;
    dst[f * 2 + 1] = r;
    if (l != 0.0f || r != 0.0f) nonzero = 1;
    float a = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
    if (a > p->stat_peak) p->stat_peak = a;
    p->stat_sq += (double)l * l + (double)r * r;
  }
  p->stat_frames += len;
  if (nonzero) p->stat_nonzero_bufs++;
}

// ---------------------------------------------------------------- ports

static Ps4AudioPort *audio_port_get(int32_t handle) {
  if (handle <= 0 || handle >= MAX_AUDIO_PORTS || !g_ports[handle].in_use) {
    return NULL;
  }
  return &g_ports[handle];
}

static void timespec_after_us(struct timespec *ts, uint64_t us) {
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  uint64_t ns = (uint64_t)now.tv_nsec + (us % 1000000ULL) * 1000ULL;
  ts->tv_sec = now.tv_sec + (time_t)(us / 1000000ULL) + (time_t)(ns / 1000000000ULL);
  ts->tv_nsec = (long)(ns % 1000000000ULL);
}

// Fallback pacing when no host device is running. Called without the lock.
static void audio_port_pace_time(uint32_t length, uint64_t *next_sync) {
  uint64_t frame_dur_us = ((uint64_t)length * 1000000ULL) / AUDIO_RATE;
  uint64_t now = get_time_micros();
  if (*next_sync == 0 || *next_sync < now) {
    *next_sync = now + frame_dur_us;
  } else {
    uint64_t diff = *next_sync - now;
    if (diff > 50000) diff = 50000;
    usleep((useconds_t)diff);
    *next_sync += frame_dur_us;
  }
}

// Wait until the port ring has room for one more grain. Lock held on
// entry/exit. Returns false if the port was closed meanwhile.
static bool audio_port_wait_room(Ps4AudioPort *p, int32_t handle) {
  while (p->in_use && p->handle == handle && p->ring_fill + p->length > p->ring_limit) {
    if (!g_host_running) {
      break;
    }
    // If the device stopped calling us (e.g. it was unplugged), don't hang
    // the guest forever: drop the oldest audio.
    uint64_t now = get_time_micros();
    if (now - g_last_render_us > 250000ULL) {
      uint32_t drop = p->length;
      if (drop > p->ring_fill) drop = p->ring_fill;
      p->ring_read = (p->ring_read + drop) % p->ring_cap;
      p->ring_fill -= drop;
      break;
    }
    struct timespec ts;
    timespec_after_us(&ts, 20000);
    pthread_cond_timedwait(&g_audio_cond, &g_audio_lock, &ts);
  }
  return p->in_use && p->handle == handle;
}

// Lock held. Converts and queues one grain (host memory) into the port ring.
static void audio_port_push(Ps4AudioPort *p, const void *pcm) {
  static float tmp[2048 * 2];
  if (!p->ring) {
    return;
  }
  uint32_t len = p->length > 2048 ? 2048 : p->length;
  audio_convert(p, (const uint8_t *)pcm, tmp, len);
  p->outputs++;
  if (!g_host_running) {
    return; // nobody consumes the ring
  }
  if (p->ring_fill + len > p->ring_cap) {
    uint32_t drop = p->ring_fill + len - p->ring_cap;
    p->ring_read = (p->ring_read + drop) % p->ring_cap;
    p->ring_fill -= drop;
  }
  uint32_t w = (p->ring_read + p->ring_fill) % p->ring_cap;
  for (uint32_t f = 0; f < len; f++) {
    p->ring[w * 2 + 0] = tmp[f * 2 + 0];
    p->ring[w * 2 + 1] = tmp[f * 2 + 1];
    if (++w == p->ring_cap) w = 0;
  }
  p->ring_fill += len;
}

void shim_sceAudioOutInit(GuestContext *ctx) {
  pthread_mutex_lock(&g_audio_lock);
  g_audio_inited = true;
  pthread_mutex_unlock(&g_audio_lock);
  ctx->rax = 0;
  SHIM_RETURN();
}

int32_t ps4_audioout_port_open(int32_t user, int32_t type, int32_t index, uint32_t len,
                               uint32_t freq, uint32_t param) {
  uint32_t fmt = param & 0xFF;
  int is_float = 0;
  uint32_t ssize = 2, ch = 2;
  if (audio_format_info(fmt, &is_float, &ssize, &ch) != 0) {
    fprintf(stderr, "[AudioOut] sceAudioOutOpen: invalid format param=0x%x\n", param);
    return ORBIS_AUDIO_OUT_ERROR_INVALID_FORMAT;
  }
  if (len == 0 || len > 2048) {
    fprintf(stderr, "[AudioOut] sceAudioOutOpen: invalid len=%u\n", len);
    return ORBIS_AUDIO_OUT_ERROR_INVALID_SIZE;
  }
  if (freq == 0) {
    freq = AUDIO_RATE;
  }

  pthread_mutex_lock(&g_audio_lock);
  audio_host_start();
  int slot = -1;
  for (int i = 1; i < MAX_AUDIO_PORTS; i++) {
    if (!g_ports[i].in_use) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    pthread_mutex_unlock(&g_audio_lock);
    return ORBIS_AUDIO_OUT_ERROR_PORT_FULL;
  }

  Ps4AudioPort *port = &g_ports[slot];
  memset(port, 0, sizeof(*port));
  port->in_use = 1;
  port->handle = slot;
  port->type = type;
  port->length = len;
  port->freq = freq;
  port->param = param;
  port->format = fmt;
  port->is_float = is_float;
  port->sample_size = ssize;
  port->channels = ch;
  port->bytes_per_frame = ssize * ch;
  for (int i = 0; i < 8; i++) port->vol[i] = 1.0f;
  port->port_gain = (type == PORT_PADSPK) ? g_padspk_gain : 1.0f;
  if (type == PORT_AUX) port->port_gain = 0.0f;
  // Allow one grain playing + one queued, plus slack for the device's
  // callback size (typically 512 frames at 48 kHz).
  port->ring_limit = 2 * len + 1024;
  port->ring_cap = port->ring_limit + len;
  port->ring = (float *)calloc((size_t)port->ring_cap * 2, sizeof(float));

  fprintf(stderr,
          "[AudioOut] open: user=%d type=%d index=%d len=%u freq=%u param=0x%x (fmt=%u %s %uch) "
          "-> handle %d\n",
          user, type, index, len, freq, param, fmt, is_float ? "float" : "s16", ch, slot);
  if (freq != AUDIO_RATE) {
    fprintf(stderr, "[AudioOut] warning: port freq %u != 48000 (played as 48 kHz)\n", freq);
  }

  pthread_mutex_unlock(&g_audio_lock);
  return slot;
}

void shim_sceAudioOutOpen(GuestContext *ctx) {
  // rdi = userId, rsi = type, rdx = index, rcx = len, r8 = freq, r9 = param
  int32_t r = ps4_audioout_port_open((int32_t)ctx->rdi, (int32_t)ctx->rsi, (int32_t)ctx->rdx,
                                     (uint32_t)ctx->rcx, (uint32_t)ctx->r8, (uint32_t)ctx->r9);
  ctx->rax = (uint64_t)(int64_t)r;
  SHIM_RETURN();
}

int32_t ps4_audioout_port_output(int32_t handle, const void *pcm) {
  pthread_mutex_lock(&g_audio_lock);
  Ps4AudioPort *port = audio_port_get(handle);
  if (!port) {
    pthread_mutex_unlock(&g_audio_lock);
    return ORBIS_AUDIO_OUT_ERROR_INVALID_PORT;
  }
  uint32_t len = port->length;
  if (!audio_port_wait_room(port, handle)) {
    pthread_mutex_unlock(&g_audio_lock);
    return ORBIS_AUDIO_OUT_ERROR_NOT_OPENED;
  }
  if (pcm) {
    audio_port_push(port, pcm);
  }
  bool host = g_host_running;
  audio_stats_maybe_log();
  uint64_t next = port->next_sync_time_us;
  pthread_mutex_unlock(&g_audio_lock);
  if (!host) {
    audio_port_pace_time(len, &next);
    pthread_mutex_lock(&g_audio_lock);
    if (port->in_use && port->handle == handle) port->next_sync_time_us = next;
    pthread_mutex_unlock(&g_audio_lock);
  }
  return pcm ? (int32_t)len : 0;
}

int32_t ps4_audioout_port_writable(int32_t handle) {
  pthread_mutex_lock(&g_audio_lock);
  Ps4AudioPort *p = audio_port_get(handle);
  if (!p) {
    pthread_mutex_unlock(&g_audio_lock);
    return ORBIS_AUDIO_OUT_ERROR_INVALID_PORT;
  }
  int32_t n;
  if (g_host_running) {
    if (get_time_micros() - g_last_render_us > 250000ULL) {
      n = 1; // stalled device: audio_port_wait_room drops instead of blocking
    } else {
      n = p->ring_fill + p->length <= p->ring_limit
              ? (int32_t)((p->ring_limit - p->ring_fill) / p->length)
              : 0;
    }
  } else {
    // Wall-time pacing: an output blocks while the previous grain's slot has
    // not elapsed yet (see audio_port_pace_time).
    n = (p->next_sync_time_us == 0 || p->next_sync_time_us <= get_time_micros()) ? 1 : 0;
  }
  pthread_mutex_unlock(&g_audio_lock);
  return n;
}

void shim_sceAudioOutOutput(GuestContext *ctx) {
  uint64_t pcm_addr = ctx->rsi;
  int32_t r = ps4_audioout_port_output((int32_t)ctx->rdi,
                                       pcm_addr ? (const void *)(ctx->mem_base + pcm_addr) : NULL);
  ctx->rax = r < 0 ? (uint64_t)(int64_t)r : (uint64_t)(uint32_t)r;
  SHIM_RETURN();
}

// sceAudioOutOutputs(SceAudioOutOutputParam *param, uint32_t num), where each
// param is { int32_t handle; const void *ptr; } (16 bytes). Waits until all
// ports can take a grain, then submits all of them together.
void shim_sceAudioOutOutputs(GuestContext *ctx) {
  uint64_t param_addr = ctx->rdi;
  uint32_t num = (uint32_t)ctx->rsi;
  if (param_addr == 0) {
    RET_ERR(ctx, ORBIS_AUDIO_OUT_ERROR_INVALID_POINTER);
    SHIM_RETURN();
  }
  if (num == 0 || num > MAX_AUDIO_PORTS) {
    RET_ERR(ctx, ORBIS_AUDIO_OUT_ERROR_PORT_FULL);
    SHIM_RETURN();
  }
  pthread_mutex_lock(&g_audio_lock);
  uint32_t len = 0;
  for (uint32_t i = 0; i < num; i++) {
    int32_t handle = *(int32_t *)(ctx->mem_base + param_addr + i * 16ULL);
    Ps4AudioPort *port = audio_port_get(handle);
    if (!port) {
      pthread_mutex_unlock(&g_audio_lock);
      RET_ERR(ctx, ORBIS_AUDIO_OUT_ERROR_INVALID_PORT);
      SHIM_RETURN();
    }
    if (i == 0) len = port->length;
  }
  for (uint32_t i = 0; i < num; i++) {
    int32_t handle = *(int32_t *)(ctx->mem_base + param_addr + i * 16ULL);
    Ps4AudioPort *port = audio_port_get(handle);
    if (!port || !audio_port_wait_room(port, handle)) {
      pthread_mutex_unlock(&g_audio_lock);
      RET_ERR(ctx, ORBIS_AUDIO_OUT_ERROR_NOT_OPENED);
      SHIM_RETURN();
    }
  }
  for (uint32_t i = 0; i < num; i++) {
    int32_t handle = *(int32_t *)(ctx->mem_base + param_addr + i * 16ULL);
    uint64_t pcm = *(uint64_t *)(ctx->mem_base + param_addr + i * 16ULL + 8);
    Ps4AudioPort *port = audio_port_get(handle);
    if (port && pcm) {
      audio_port_push(port, ctx->mem_base + pcm);
    }
  }
  bool host = g_host_running;
  audio_stats_maybe_log();
  int32_t h0 = *(int32_t *)(ctx->mem_base + param_addr);
  Ps4AudioPort *first = audio_port_get(h0);
  uint64_t next = first ? first->next_sync_time_us : 0;
  pthread_mutex_unlock(&g_audio_lock);
  if (!host && first) {
    audio_port_pace_time(len, &next);
    pthread_mutex_lock(&g_audio_lock);
    if (first->in_use && first->handle == h0) first->next_sync_time_us = next;
    pthread_mutex_unlock(&g_audio_lock);
  }
  ctx->rax = (uint64_t)len;
  SHIM_RETURN();
}

int32_t ps4_audioout_port_close(int32_t handle) {
  if (handle <= 0 || handle >= MAX_AUDIO_PORTS) {
    return ORBIS_AUDIO_OUT_ERROR_INVALID_PORT;
  }
  pthread_mutex_lock(&g_audio_lock);
  Ps4AudioPort *port = &g_ports[handle];
  if (port->in_use) {
    free(port->ring);
    port->ring = NULL;
    port->in_use = 0;
    fprintf(stderr, "[AudioOut] close handle %d\n", handle);
  }
  pthread_cond_broadcast(&g_audio_cond);
  pthread_mutex_unlock(&g_audio_lock);
  return 0;
}

void shim_sceAudioOutClose(GuestContext *ctx) {
  int32_t r = ps4_audioout_port_close((int32_t)ctx->rdi);
  ctx->rax = (uint64_t)(int64_t)r;
  SHIM_RETURN();
}

// OrbisAudioOutPortState (32 bytes; layout per shadPS4 audioout.h)
typedef struct {
  uint16_t output;
  uint8_t channel;
  uint8_t reserved8_1;
  int16_t volume;
  uint16_t rerouteCounter;
  uint64_t flag;
  uint64_t reserved64[2];
} OrbisAudioOutPortState;

void shim_sceAudioOutGetPortState(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  uint64_t state_gaddr = ctx->rsi;

  if (state_gaddr == 0) {
    RET_ERR(ctx, ORBIS_AUDIO_OUT_ERROR_INVALID_POINTER);
    SHIM_RETURN();
  }

  pthread_mutex_lock(&g_audio_lock);
  Ps4AudioPort *port = audio_port_get(handle);
  if (!port) {
    pthread_mutex_unlock(&g_audio_lock);
    RET_ERR(ctx, ORBIS_AUDIO_OUT_ERROR_INVALID_PORT);
    SHIM_RETURN();
  }

  OrbisAudioOutPortState *state = (OrbisAudioOutPortState *)(ctx->mem_base + state_gaddr);
  memset(state, 0, sizeof(*state));
  state->volume = -1;
  switch (port->type) {
  case PORT_MAIN:
  case PORT_BGM:
  case 126:
    state->output = 0x01; // CONNECTED_PRIMARY
    state->channel = port->channels > 2 ? 2 : (uint8_t)port->channels;
    break;
  case PORT_VOICE:
  case PORT_PERSONAL:
    state->output = 0x40; // CONNECTED_HEADPHONE
    state->channel = 1;
    break;
  case PORT_PADSPK:
    state->output = 0x04; // CONNECTED_TERTIARY
    state->channel = 1;
    state->volume = 127;
    break;
  case PORT_AUX:
    state->output = 0x80; // CONNECTED_EXTERNAL
    break;
  default:
    break;
  }
  pthread_mutex_unlock(&g_audio_lock);

  ctx->rax = 0;
  SHIM_RETURN();
}

// sceAudioOutSetVolume(handle, flag, const int32_t *vol): vol[i] is the
// volume (0..32768 = 0 dB) of channel i for each bit i set in flag.
void shim_sceAudioOutSetVolume(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  uint32_t flag = (uint32_t)ctx->rsi;
  uint64_t vol_addr = ctx->rdx;
  if (vol_addr == 0) {
    RET_ERR(ctx, ORBIS_AUDIO_OUT_ERROR_INVALID_POINTER);
    SHIM_RETURN();
  }
  pthread_mutex_lock(&g_audio_lock);
  Ps4AudioPort *port = audio_port_get(handle);
  if (!port) {
    pthread_mutex_unlock(&g_audio_lock);
    RET_ERR(ctx, ORBIS_AUDIO_OUT_ERROR_INVALID_PORT);
    SHIM_RETURN();
  }
  const int32_t *vol = (const int32_t *)(ctx->mem_base + vol_addr);
  int32_t raw[8];
  for (int i = 0; i < 8; i++) {
    raw[i] = -1;
    if (flag & (1u << i)) {
      int32_t v = vol[i];
      raw[i] = v;
      if (v < 0) v = 0;
      if (v > VOLUME_0DB) v = VOLUME_0DB;
      port->vol[i] = (float)v / (float)VOLUME_0DB;
    }
  }
  if (audio_log_enabled()) {
    fprintf(stderr, "[AudioOut] SetVolume handle=%d flag=0x%x vol=[%d %d %d %d %d %d %d %d]\n",
            handle, flag, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7]);
  }
  pthread_mutex_unlock(&g_audio_lock);
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceAudioOutSetMixLevelPadSpk(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}
