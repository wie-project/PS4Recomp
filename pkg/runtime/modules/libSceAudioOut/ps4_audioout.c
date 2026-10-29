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
//   PS4_AUDIO_LOG=1         log volume changes and, every ~2 s, per-port /
//                           final-mix RMS + peak and per-port timing: outputs
//                           in the window against the grains the window should
//                           have had, the window's longest gap between two
//                           grains, late grains (the ring had run empty), the
//                           frames of silence the device filled in and the
//                           longest wait for the mixer lock. A gap over two
//                           grains gets its own line with the time it ended.
//   PS4_AUDIO_DUMP=<path>   write the final mixed host output to a WAV file
//                           (float32 stereo 48 kHz). The `frame 0 at mono` line
//                           gives the CLOCK_MONOTONIC time of its first frame;
//                           frame n reaches the device n / 48000 s later. On
//                           CoreAudio that time is the first buffer's host
//                           time, which excludes the device's own latency and
//                           safety offset: it is when a frame reaches the
//                           device, not when it is heard. The header is
//                           rewritten every second of audio and at exit. A
//                           writer thread does the file I/O (never under the
//                           mixer lock or on the device's thread); the host
//                           stats line gives its longest write (dump_io_max_ms).
//   PS4_AUDIO_MUTE=1        don't start the host output device (time-paced).
//   PS4_AUDIO_NULL=1        headless output: instead of the host device, a
//                           clock thread pulls and mixes 512 frames every
//                           10.67 ms, so pacing, mixing, stats and
//                           PS4_AUDIO_DUMP work without playing anything.
//   PS4_AUDIO_PADSPK=<gain> a fixed gain for PADSPK (controller speaker) ports,
//                           which are mixed into the main output (the host has
//                           no controller speaker). Unset, each port is mixed
//                           at its mix level: -9 dB until the game sets one
//                           with sceAudioOutSetMixLevelPadSpk.

#include "ps4_audioout.h"
#include "ps4_audioout_internal.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
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
#define ORBIS_AUDIO_OUT_ERROR_INVALID_PORT_TYPE PS4_AUDIO_OUT_ERROR_INVALID_PORT_TYPE
#define ORBIS_AUDIO_OUT_ERROR_INVALID_MIXLEVEL PS4_AUDIO_OUT_ERROR_INVALID_MIXLEVEL

#define RET_ERR(ctx, code) ((ctx)->rax = (uint64_t)(int64_t)(int32_t)(uint32_t)(code))

enum {
  PORT_MAIN = 0,
  PORT_BGM = 1,
  PORT_VOICE = 2,
  PORT_PERSONAL = 3,
  PORT_PADSPK = 4,
  PORT_AUX = 127,
};

#define PORT_GAP_LOG 8

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
  float port_gain; // extra gain for this port type (PADSPK: its mix level)

  // Ring buffer of interleaved float stereo frames.
  float *ring;
  uint32_t ring_cap; // frames
  uint32_t ring_read;
  uint32_t ring_fill;
  uint32_t ring_limit; // writers block while fill + length > limit

  uint64_t next_sync_time_us; // fallback pacing when no host device
  uint64_t outputs;
  uint64_t underruns;
  // timing (get_time_micros clock); see Ps4AudioPortStats
  uint64_t underrun_frames;
  uint64_t late_outputs;
  uint64_t last_output_us;
  uint64_t max_gap_us;
  uint64_t win_max_gap_us; // longest gap in the current stats window
  uint64_t win_outputs;    // outputs in the current stats window
  uint64_t win_start_us;
  uint64_t win_max_lock_us; // longest wait for the mixer lock at an output call in the window
  // Gaps over two grains since the last stats line (PS4_AUDIO_LOG), printed with it.
  struct {
    uint64_t gap_us, end_us, lock_us;
  } gaps[PORT_GAP_LOG];
  uint32_t gap_n;
  uint64_t gap_unlisted; // gaps beyond PORT_GAP_LOG in the window
  uint64_t first_sound_us;
  uint64_t first_sound_play_us;
  uint64_t first_sound_outputs;
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
static pthread_mutex_t g_open_lock = PTHREAD_MUTEX_INITIALIZER; // port opens (their log buffer)
static bool g_audio_inited = false;
static int g_log = -1;
static float g_padspk_override = -1.0f; // PS4_AUDIO_PADSPK; < 0: the ports' mix levels

// host output state
static bool g_host_tried = false;
static bool g_host_running = false;
static uint64_t g_render_calls = 0;
static uint64_t g_render_frames = 0;
static uint64_t g_last_render_us = 0;
// Device time at which the last mixed period has been handed on (headless: its clock slot ends;
// CoreAudio: the buffer's host time plus its length, without the device's own latency): frames
// queued in a ring now reach the device from there on.
static uint64_t g_dev_end_us = 0;
static double g_mix_sq = 0.0;
static float g_mix_peak = 0.0f;
static uint64_t g_mix_frames = 0;
static uint64_t g_last_stat_us = 0;

// WAV dump. The device (the headless clock thread or the CoreAudio render callback) copies each
// mixed period into a ring; a writer thread moves the ring to the file. No file I/O runs under
// g_audio_lock or on the device's thread: a write can stall for tens of milliseconds (in p3_a1,
// 39-263 ms about every 30 s), and a stall under the lock stopped the device and every output
// call with it, which showed as gaps in the guest's grains and as underruns.
#define DUMP_RING_FRAMES (4 * AUDIO_RATE) // 4 s of stereo frames
static FILE *g_dump = NULL;               // the writer's; under g_dump_io_lock
static float *g_dump_ring = NULL;         // DUMP_RING_FRAMES stereo frames, set once at open
static _Atomic bool g_dump_on = false;    // the device may put frames
static _Atomic uint64_t g_dump_put = 0;   // frames put into the ring (by the device)
static _Atomic uint64_t g_dump_taken = 0; // frames the writer has written to the file
static uint64_t g_dump_frames = 0;        // frames handed to the dump (g_audio_lock)
static uint64_t g_dump_lost = 0;          // of them, frames that found the ring full (g_audio_lock)
static bool g_dump_lost_logged = false;
static uint64_t g_dump_frames_hdr = 0;    // frames named by the last header (the writer)
static uint64_t g_dump_t0_us = 0; // device time of the dump's frame 0 (get_time_micros clock)
static bool g_dump_t0_logged = false;
static char g_dump_path[1024];
static _Atomic uint64_t g_dump_io_max_us = 0; // longest write + flush in the stats window
static _Atomic int g_dump_errno = 0;          // the first write error; the dump stops there
static bool g_dump_err_logged = false;
static pthread_mutex_t g_dump_io_lock = PTHREAD_MUTEX_INITIALIZER; // the FILE
static pthread_cond_t g_dump_wake = PTHREAD_COND_INITIALIZER;     // frames were put

#ifdef __APPLE__
static AudioComponentInstance g_unit = NULL;
#endif

#ifdef __APPLE__
// Mach absolute time (also CoreAudio's host time) in microseconds.
static uint64_t mach_to_micros(uint64_t t) {
  static mach_timebase_info_data_t timebase;
  if (timebase.denom == 0) {
    mach_timebase_info(&timebase);
  }
  return (t * timebase.numer) / (timebase.denom * 1000ULL);
}
#endif

static uint64_t get_time_micros(void) {
#ifdef __APPLE__
  return mach_to_micros(mach_absolute_time());
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
#endif
}

// A get_time_micros() time as CLOCK_MONOTONIC microseconds, the clock other traces print
// (`mono`). On macOS, mach absolute time stops while the machine sleeps and CLOCK_MONOTONIC does
// not, so the offset between them is read at each conversion. Used only for log lines and the
// stats query, never per grain.
static uint64_t mono_micros_of(uint64_t us) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  int64_t mono = (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
  return us ? (uint64_t)((int64_t)us + (mono - (int64_t)get_time_micros())) : 0;
}

static double mono_seconds_of(uint64_t us) {
  return (double)mono_micros_of(us) / 1e6;
}

static bool audio_log_enabled(void) {
  if (g_log < 0) {
    const char *e = getenv("PS4_AUDIO_LOG");
    g_log = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
  }
  return g_log == 1;
}

// Log lines built while g_audio_lock is held and printed after it is released, so no output
// call or device period waits behind stderr. No allocation: callers own the buffer.
typedef struct {
  char text[16384];
  size_t len;
  bool truncated;
} AudioLogBuf;

static void logbuf_add(AudioLogBuf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void logbuf_add(AudioLogBuf *b, const char *fmt, ...) {
  if (b->truncated) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(b->text + b->len, sizeof b->text - b->len, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= sizeof b->text - b->len) {
    b->truncated = true; // the cut line is dropped
    b->text[b->len] = 0;
    return;
  }
  b->len += (size_t)n;
}

// Without g_audio_lock: prints and empties the buffer.
static void logbuf_flush(AudioLogBuf *b) {
  if (b->len) {
    fwrite(b->text, 1, b->len, stderr);
  }
  if (b->truncated) {
    fputs("[AudioOut] (log lines cut: the stats buffer is full)\n", stderr);
  }
  b->len = 0;
  b->truncated = false;
}

// ---------------------------------------------------------------- WAV dump

// Returns false on a write error (errno set).
static bool wav_write_header(FILE *f, uint64_t frames) {
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
  bool ok = fseek(f, 0, SEEK_SET) == 0 && fwrite(h, 1, 44, f) == 44;
  if (pos > 44) {
    ok = fseek(f, pos, SEEK_SET) == 0 && ok;
  }
  return fflush(f) == 0 && ok;
}

// Records the first write error (from the writer or the exit handler); the dump stops there and
// dump_log_pending reports it once.
static void dump_fail(int err) {
  int none = 0;
  atomic_compare_exchange_strong(&g_dump_errno, &none, err ? err : EIO);
}

static void dump_note_io(uint64_t us) {
  uint64_t cur = atomic_load_explicit(&g_dump_io_max_us, memory_order_relaxed);
  while (us > cur &&
         !atomic_compare_exchange_weak_explicit(&g_dump_io_max_us, &cur, us, memory_order_relaxed,
                                                memory_order_relaxed)) {
  }
}

// g_dump_io_lock held, g_dump open: writes every frame put so far, then the header (once per
// second of audio) or a flush, so the file is current after each batch.
static void dump_drain_locked(void) {
  uint64_t put = atomic_load_explicit(&g_dump_put, memory_order_acquire);
  uint64_t taken = atomic_load_explicit(&g_dump_taken, memory_order_relaxed);
  if (taken == put || atomic_load(&g_dump_errno) != 0) {
    return;
  }
  uint64_t io0 = get_time_micros();
  while (taken < put) {
    uint64_t at = taken % DUMP_RING_FRAMES;
    size_t n = (size_t)(put - taken);
    if (n > DUMP_RING_FRAMES - at) n = (size_t)(DUMP_RING_FRAMES - at);
    if (fwrite(g_dump_ring + at * 2, 8, n, g_dump) != n) {
      dump_fail(errno);
      return;
    }
    taken += n;
    atomic_store_explicit(&g_dump_taken, taken, memory_order_release); // the ring space is free
  }
  bool ok;
  if (taken - g_dump_frames_hdr >= AUDIO_RATE) {
    g_dump_frames_hdr = taken;
    ok = wav_write_header(g_dump, taken); // flushes
  } else {
    ok = fflush(g_dump) == 0;
  }
  if (!ok) {
    dump_fail(errno);
  }
  dump_note_io(get_time_micros() - io0);
}

static void timespec_after_us(struct timespec *ts, uint64_t us);

// The writer thread: waits for frames (woken by the device, or every 20 ms) and drains them.
// Ends when the exit handler closes the file or a write fails.
static void *dump_writer_main(void *arg) {
  (void)arg;
  pthread_mutex_lock(&g_dump_io_lock);
  while (g_dump && atomic_load(&g_dump_errno) == 0) {
    if (atomic_load_explicit(&g_dump_put, memory_order_acquire) ==
        atomic_load_explicit(&g_dump_taken, memory_order_relaxed)) {
      struct timespec ts;
      timespec_after_us(&ts, 20000);
      pthread_cond_timedwait(&g_dump_wake, &g_dump_io_lock, &ts);
      continue;
    }
    dump_drain_locked();
  }
  pthread_mutex_unlock(&g_dump_io_lock);
  return NULL;
}

// At exit: the frames still in the ring, then the final header, so a run that ends normally
// leaves a WAV whose header names every frame (the header is otherwise rewritten once per second
// of audio). The file lock is tried for up to 2 s: exit must not hang on a write that does not
// return.
static void dump_finish(void) {
  atomic_store(&g_dump_on, false);
  bool locked = false;
  // Only the writer takes this lock, for one batch; a stalled write has been seen to take 263 ms.
  for (int i = 0; i < 2000 && !locked; i++) {
    locked = pthread_mutex_trylock(&g_dump_io_lock) == 0;
    if (!locked) usleep(1000);
  }
  if (!locked) {
    fprintf(stderr,
            "[AudioOut] PS4_AUDIO_DUMP: %s keeps its last header (a write is still running at "
            "exit)\n",
            g_dump_path);
    return;
  }
  if (g_dump) {
    dump_drain_locked();
    uint64_t taken = atomic_load_explicit(&g_dump_taken, memory_order_relaxed);
    if (atomic_load(&g_dump_errno) == 0 && !wav_write_header(g_dump, taken)) {
      dump_fail(errno);
    }
    fclose(g_dump);
    g_dump = NULL;
  }
  pthread_cond_signal(&g_dump_wake);
  pthread_mutex_unlock(&g_dump_io_lock);
}

// Called with g_audio_lock held, once, at the first port open (from audio_host_start); its
// lines go to `b`, printed by the caller after the unlock.
static void dump_open(AudioLogBuf *b) {
  const char *p = getenv("PS4_AUDIO_DUMP");
  if (!p || !*p) {
    return;
  }
  snprintf(g_dump_path, sizeof g_dump_path, "%s", p);
  g_dump_ring = (float *)malloc((size_t)DUMP_RING_FRAMES * 2 * sizeof(float));
  FILE *f = g_dump_ring ? fopen(p, "wb") : NULL;
  if (!f) {
    int e = g_dump_ring ? errno : ENOMEM;
    logbuf_add(b, "[AudioOut] PS4_AUDIO_DUMP: cannot open %s: %s (errno %d); no dump\n", p,
               strerror(e), e);
    free(g_dump_ring);
    g_dump_ring = NULL;
    return;
  }
  if (!wav_write_header(f, 0)) {
    dump_fail(errno);
  }
  pthread_mutex_lock(&g_dump_io_lock);
  g_dump = f;
  pthread_mutex_unlock(&g_dump_io_lock);
  pthread_t t;
  if (pthread_create(&t, NULL, dump_writer_main, NULL) != 0) {
    logbuf_add(b, "[AudioOut] PS4_AUDIO_DUMP: writer thread creation failed; no dump\n");
    pthread_mutex_lock(&g_dump_io_lock);
    fclose(g_dump);
    g_dump = NULL;
    pthread_mutex_unlock(&g_dump_io_lock);
    return;
  }
  pthread_detach(t);
  atexit(dump_finish);
  atomic_store(&g_dump_on, true);
  logbuf_add(b, "[AudioOut] dumping final mix to %s\n", p);
}

// Called with g_audio_lock held, from a thread that prints `b` after the unlock (never the render
// callback): adds the device time of the dump's first frame once the device has set it, a write
// error once, and once that the writer fell behind.
static void dump_log_pending(AudioLogBuf *b) {
  int err = atomic_load(&g_dump_errno);
  if (err != 0 && !g_dump_err_logged) {
    g_dump_err_logged = true;
    logbuf_add(b, "[AudioOut] PS4_AUDIO_DUMP: writing %s failed after %llu frames: %s (errno %d); "
                  "the dump stops there\n",
               g_dump_path, (unsigned long long)atomic_load(&g_dump_taken), strerror(err), err);
  }
  if (g_dump_lost != 0 && !g_dump_lost_logged) {
    g_dump_lost_logged = true;
    logbuf_add(b,
               "[AudioOut] PS4_AUDIO_DUMP: the writer fell %.1f s behind; %llu frames lost after "
               "frame %llu, the dump is off the clock from there\n",
               (double)DUMP_RING_FRAMES / AUDIO_RATE, (unsigned long long)g_dump_lost,
               (unsigned long long)(g_dump_frames - g_dump_lost));
  }
  if (!g_dump_ring || g_dump_t0_logged || g_dump_t0_us == 0) {
    return;
  }
  g_dump_t0_logged = true;
  logbuf_add(b, "[AudioOut] dump frame 0 at mono %.6f (48000 frames per second from there)\n",
             mono_seconds_of(g_dump_t0_us));
}

// The device: one mixed period, t0 the device time of its first frame. g_audio_lock held. Only
// copies into the ring (no I/O, no allocation), so it is safe on the render callback.
static void dump_write(const float *stereo, uint32_t frames, uint64_t t0) {
  if (!atomic_load_explicit(&g_dump_on, memory_order_relaxed) ||
      atomic_load_explicit(&g_dump_errno, memory_order_relaxed) != 0) {
    return;
  }
  if (g_dump_frames == 0 && g_dump_t0_us == 0) {
    g_dump_t0_us = t0;
  }
  uint64_t put = atomic_load_explicit(&g_dump_put, memory_order_relaxed);
  uint64_t taken = atomic_load_explicit(&g_dump_taken, memory_order_acquire);
  uint64_t room = DUMP_RING_FRAMES - (put - taken);
  uint32_t n = frames <= room ? frames : (uint32_t)room;
  uint64_t at = put % DUMP_RING_FRAMES;
  uint32_t first = n <= DUMP_RING_FRAMES - at ? n : (uint32_t)(DUMP_RING_FRAMES - at);
  memcpy(g_dump_ring + at * 2, stereo, (size_t)first * 8);
  memcpy(g_dump_ring, stereo + (size_t)first * 2, (size_t)(n - first) * 8);
  atomic_store_explicit(&g_dump_put, put + n, memory_order_release);
  g_dump_frames += frames;
  g_dump_lost += frames - n;
  pthread_cond_signal(&g_dump_wake);
}

// ---------------------------------------------------------------- stats

// Lock held: the port's recorded gaps over two grains go to `b`, and the record is emptied.
static void audio_port_gap_lines(Ps4AudioPort *p, AudioLogBuf *b) {
  for (uint32_t g = 0; g < p->gap_n; g++) {
    logbuf_add(b, "[AudioOut] port %d gap %.1f ms ending at mono %.3f (mixer lock wait %.1f ms)\n",
               p->handle, (double)p->gaps[g].gap_us / 1000.0, mono_seconds_of(p->gaps[g].end_us),
               (double)p->gaps[g].lock_us / 1000.0);
  }
  if (p->gap_unlisted) {
    logbuf_add(b, "[AudioOut] port %d: %llu more gaps over two grains in the window\n", p->handle,
               (unsigned long long)p->gap_unlisted);
  }
  p->gap_n = 0;
  p->gap_unlisted = 0;
}

// Called with g_audio_lock held; the caller prints `b` after the unlock. Every ~2 s: the stats
// lines of each port (with its gaps over two grains since the last line) and of the host mix.
static void audio_stats_maybe_log(AudioLogBuf *b) {
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
    // The grains the window should have had at the port's rate.
    double expected = (double)(now - p->win_start_us) / 1e6 * (double)p->freq / (double)p->length;
    logbuf_add(b,
               "[AudioOut] port %d type=%d fmt=%u ch=%u len=%u: nonzero_bufs=%llu in_rms=%.5f "
               "in_peak=%.5f vol=[%.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f] fill=%u/%u "
               "outputs=%llu (window %llu, expected %.1f) max_gap_ms=%.1f late=%llu underruns=%llu "
               "underrun_frames=%llu mono=%.3f lock_wait_max_ms=%.1f\n",
               i, p->type, p->format, p->channels, p->length,
               (unsigned long long)p->stat_nonzero_bufs,
               rms, p->stat_peak, p->vol[0], p->vol[1], p->vol[2], p->vol[3], p->vol[4], p->vol[5],
               p->vol[6], p->vol[7], p->ring_fill, p->ring_cap, (unsigned long long)p->outputs,
               (unsigned long long)p->win_outputs, expected, (double)p->win_max_gap_us / 1000.0,
               (unsigned long long)p->late_outputs, (unsigned long long)p->underruns,
               (unsigned long long)p->underrun_frames, mono_seconds_of(now),
               (double)p->win_max_lock_us / 1000.0);
    audio_port_gap_lines(p, b);
    p->win_outputs = 0;
    p->win_max_gap_us = 0;
    p->win_max_lock_us = 0;
    p->win_start_us = now;
    if (p->channels == 8 && p->stat_frames) {
      double n = (double)p->stat_frames;
      logbuf_add(b,
                 "[AudioOut] port %d raw ch rms: FL=%.5f FR=%.5f FC=%.5f LFE=%.5f c4=%.5f c5=%.5f "
                 "c6=%.5f c7=%.5f\n",
                 i, sqrt(p->stat_ch_sq[0] / n), sqrt(p->stat_ch_sq[1] / n),
                 sqrt(p->stat_ch_sq[2] / n),
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
  logbuf_add(b,
             "[AudioOut] host: running=%d render_calls=%llu render_frames=%llu mix_rms=%.5f "
             "mix_peak=%.5f dump_frames=%llu dump_io_max_ms=%.1f\n",
             g_host_running ? 1 : 0, (unsigned long long)g_render_calls,
             (unsigned long long)g_render_frames, mrms, g_mix_peak,
             (unsigned long long)g_dump_frames,
             (double)atomic_exchange_explicit(&g_dump_io_max_us, 0, memory_order_relaxed) / 1000.0);
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
      p->underrun_frames += frames - n;
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
  // The host time CoreAudio gives a buffer is when its first frame is played.
  uint64_t t0 = (inTimeStamp && (inTimeStamp->mFlags & kAudioTimeStampHostTimeValid))
                    ? mach_to_micros(inTimeStamp->mHostTime)
                    : g_last_render_us;
  g_dev_end_us = t0 + (uint64_t)frames * 1000000ULL / AUDIO_RATE;
  dump_write(out, frames, t0);
  pthread_cond_broadcast(&g_audio_cond);
  pthread_mutex_unlock(&g_audio_lock);
  return noErr;
}
#endif

// Headless output device (PS4_AUDIO_NULL): consumes and mixes the port rings
// at the device rate (48 kHz, 512-frame periods) without playing anything.
// Period k is mixed at start + k * 512 / 48000 s, so dump frame n is the device
// time start + n / 48000 s. The dump's writer thread flushes after each batch it
// writes, so a killed run keeps everything up to a period or two before the kill.
#define NULL_DEVICE_FRAMES 512
static uint64_t g_null_start_us;
static void *audio_null_device_main(void *arg) {
  (void)arg;
  static float buf[NULL_DEVICE_FRAMES * 2];
  static AudioLogBuf log; // this thread's
  static const float silence[NULL_DEVICE_FRAMES * 2];
  const uint64_t start = g_null_start_us;
  uint64_t frames = 0;
  for (;;) {
    pthread_mutex_lock(&g_audio_lock);
    audio_mix(buf, NULL_DEVICE_FRAMES);
    g_render_calls++;
    g_render_frames += NULL_DEVICE_FRAMES;
    g_last_render_us = get_time_micros();
    uint64_t t0 = start + frames * 1000000ULL / AUDIO_RATE;
    g_dev_end_us = t0 + NULL_DEVICE_FRAMES * 1000000ULL / AUDIO_RATE;
    dump_write(buf, NULL_DEVICE_FRAMES, t0);
    pthread_cond_broadcast(&g_audio_cond);
    pthread_mutex_unlock(&g_audio_lock);
    frames += NULL_DEVICE_FRAMES;
    uint64_t due = start + frames * 1000000ULL / AUDIO_RATE;
    uint64_t now = get_time_micros();
    if (due > now) {
      usleep((useconds_t)(due - now));
    } else if (now - due > 200000ULL) {
      // The process was stalled for a long time: skip the missed periods
      // instead of consuming the backlog in a burst (a real device drops it
      // too). The dump gets their silence, so it stays on the clock.
      uint64_t skip = (now - due) * AUDIO_RATE / 1000000ULL / NULL_DEVICE_FRAMES;
      pthread_mutex_lock(&g_audio_lock);
      for (uint64_t k = 0; k < skip; k++) {
        dump_write(silence, NULL_DEVICE_FRAMES, 0);
      }
      pthread_mutex_unlock(&g_audio_lock);
      frames += skip * NULL_DEVICE_FRAMES;
    }
    pthread_mutex_lock(&g_audio_lock);
    dump_log_pending(&log);
    pthread_mutex_unlock(&g_audio_lock);
    logbuf_flush(&log);
  }
  return NULL;
}

// Called with g_audio_lock held, at each port open (it acts once); its lines go to `b`, printed by
// the caller after the unlock.
static void audio_host_start(AudioLogBuf *b) {
  if (g_host_tried) {
    return;
  }
  g_host_tried = true;
  const char *pg = getenv("PS4_AUDIO_PADSPK");
  if (pg && *pg) {
    g_padspk_override = (float)atof(pg);
    if (g_padspk_override < 0.0f) g_padspk_override = 0.0f;
  }
  dump_open(b);
  const char *mute = getenv("PS4_AUDIO_MUTE");
  if (mute && *mute && strcmp(mute, "0") != 0) {
    logbuf_add(b, "[AudioOut] PS4_AUDIO_MUTE set; host output disabled\n");
    return;
  }
  const char *null_dev = getenv("PS4_AUDIO_NULL");
  if (null_dev && *null_dev && strcmp(null_dev, "0") != 0) {
    pthread_t t;
    g_null_start_us = get_time_micros();
    if (pthread_create(&t, NULL, audio_null_device_main, NULL) == 0) {
      pthread_detach(t);
      g_host_running = true;
      g_last_render_us = get_time_micros();
      g_dump_t0_us = g_null_start_us; // the device's first period is due at once
      dump_log_pending(b);
      logbuf_add(b, "[AudioOut] PS4_AUDIO_NULL set; headless output (clock-paced mix, "
                    "nothing played)\n");
    } else {
      logbuf_add(b, "[AudioOut] PS4_AUDIO_NULL: thread creation failed; host output disabled\n");
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
    logbuf_add(b, "[AudioOut] no default output AudioComponent\n");
    return;
  }
  OSStatus st = AudioComponentInstanceNew(comp, &g_unit);
  if (st != noErr) {
    logbuf_add(b, "[AudioOut] AudioComponentInstanceNew failed: %d\n", (int)st);
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
    logbuf_add(b, "[AudioOut] set stream format failed: %d\n", (int)st);
  }
  AURenderCallbackStruct cb;
  cb.inputProc = audio_render_cb;
  cb.inputProcRefCon = NULL;
  st = AudioUnitSetProperty(g_unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
                            0, &cb, sizeof(cb));
  if (st != noErr) {
    logbuf_add(b, "[AudioOut] set render callback failed: %d\n", (int)st);
  }
  st = AudioUnitInitialize(g_unit);
  if (st != noErr) {
    logbuf_add(b, "[AudioOut] AudioUnitInitialize failed: %d\n", (int)st);
    AudioComponentInstanceDispose(g_unit);
    g_unit = NULL;
    return;
  }
  // AudioOutputUnitStart does not invoke the render callback synchronously, so
  // it is safe to call with g_audio_lock held.
  st = AudioOutputUnitStart(g_unit);
  if (st != noErr) {
    logbuf_add(b, "[AudioOut] AudioOutputUnitStart failed: %d\n", (int)st);
    AudioUnitUninitialize(g_unit);
    AudioComponentInstanceDispose(g_unit);
    g_unit = NULL;
    return;
  }
  g_host_running = true;
  g_last_render_us = get_time_micros();
  logbuf_add(b, "[AudioOut] host output started (default device, float32 stereo 48 kHz)\n");
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
// same). LFE is dropped. Returns whether any converted sample is non-zero.
static int audio_convert(Ps4AudioPort *p, const uint8_t *src, float *dst, uint32_t len) {
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
  return nonzero;
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

// Lock held. The guest called for a grain for the port at `now` and got the mixer lock at
// `locked` (before any wait for room): records the gap since the previous call, the lock wait,
// and a late grain, one that found the ring already drained by the device (so silence was
// played in between). No device consumes the rings under PS4_AUDIO_MUTE, so no grain is late
// there. Under PS4_AUDIO_LOG a gap longer than two grains is recorded for the next stats line,
// with its end on the CLOCK_MONOTONIC timeline and the part of it spent waiting for the lock.
static void audio_port_arrive(Ps4AudioPort *p, uint64_t now, uint64_t locked) {
  uint64_t lock_wait = locked > now ? locked - now : 0;
  if (lock_wait > p->win_max_lock_us) p->win_max_lock_us = lock_wait;
  if (p->last_output_us != 0) {
    uint64_t gap = now - p->last_output_us;
    if (gap > p->max_gap_us) p->max_gap_us = gap;
    if (gap > p->win_max_gap_us) p->win_max_gap_us = gap;
    if (gap * (uint64_t)p->freq > 2ULL * p->length * 1000000ULL && audio_log_enabled()) {
      if (p->gap_n < PORT_GAP_LOG) {
        p->gaps[p->gap_n].gap_us = gap;
        p->gaps[p->gap_n].end_us = now;
        p->gaps[p->gap_n].lock_us = lock_wait;
        p->gap_n++;
      } else {
        p->gap_unlisted++;
      }
    }
  }
  p->last_output_us = now;
  if (p->outputs > 0 && g_host_running && p->ring_fill == 0) {
    p->late_outputs++;
  }
}

// Lock held. Converts and queues one grain (host memory) into the port ring; a line goes to `b`.
static void audio_port_push(Ps4AudioPort *p, const void *pcm, AudioLogBuf *b) {
  static float tmp[2048 * 2];
  if (!p->ring) {
    return;
  }
  uint32_t len = p->length > 2048 ? 2048 : p->length;
  int nonzero = audio_convert(p, (const uint8_t *)pcm, tmp, len);
  if (nonzero && p->first_sound_us == 0) {
    // Its first frame plays after the period the device is playing and the
    // frames queued ahead of it.
    uint64_t now = get_time_micros();
    uint64_t from = (g_host_running && g_dev_end_us > now) ? g_dev_end_us : now;
    uint32_t ahead = g_host_running ? p->ring_fill : 0;
    p->first_sound_us = now;
    p->first_sound_play_us = from + (uint64_t)ahead * 1000000ULL / AUDIO_RATE;
    p->first_sound_outputs = p->outputs;
    logbuf_add(b, "[AudioOut] port %d first non-silent grain at mono %.6f (after %llu outputs), "
                  "plays at mono %.6f\n",
               p->handle, mono_seconds_of(p->first_sound_us), (unsigned long long)p->outputs,
               mono_seconds_of(p->first_sound_play_us));
  }
  p->outputs++;
  p->win_outputs++;
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

  static AudioLogBuf log; // filled and printed under g_open_lock (opens are rare)
  pthread_mutex_lock(&g_open_lock);
  pthread_mutex_lock(&g_audio_lock);
  audio_host_start(&log);
  int slot = -1;
  for (int i = 1; i < MAX_AUDIO_PORTS; i++) {
    if (!g_ports[i].in_use) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    pthread_mutex_unlock(&g_audio_lock);
    logbuf_flush(&log);
    pthread_mutex_unlock(&g_open_lock);
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
  port->port_gain = 1.0f;
  if (type == PORT_PADSPK) {
    port->port_gain = g_padspk_override >= 0.0f
                          ? g_padspk_override
                          : (float)PS4_AUDIO_OUT_MIXLEVEL_PADSPK_DEFAULT / (float)VOLUME_0DB;
  }
  if (type == PORT_AUX) port->port_gain = 0.0f;
  // Allow one grain playing + one queued, plus slack for the device's
  // callback size (typically 512 frames at 48 kHz).
  port->ring_limit = 2 * len + 1024;
  port->ring_cap = port->ring_limit + len;
  port->ring = (float *)calloc((size_t)port->ring_cap * 2, sizeof(float));
  port->win_start_us = get_time_micros();

  logbuf_add(&log,
             "[AudioOut] open: user=%d type=%d index=%d len=%u freq=%u param=0x%x (fmt=%u %s %uch) "
             "-> handle %d\n",
             user, type, index, len, freq, param, fmt, is_float ? "float" : "s16", ch, slot);
  if (freq != AUDIO_RATE) {
    logbuf_add(&log, "[AudioOut] warning: port freq %u != 48000 (played as 48 kHz)\n", freq);
  }

  pthread_mutex_unlock(&g_audio_lock);
  logbuf_flush(&log);
  pthread_mutex_unlock(&g_open_lock);
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
  AudioLogBuf log;
  log.len = 0;
  log.truncated = false;
  uint64_t called = get_time_micros();
  pthread_mutex_lock(&g_audio_lock);
  uint64_t locked = get_time_micros();
  Ps4AudioPort *port = audio_port_get(handle);
  if (!port) {
    pthread_mutex_unlock(&g_audio_lock);
    return ORBIS_AUDIO_OUT_ERROR_INVALID_PORT;
  }
  uint32_t len = port->length;
  if (pcm) {
    audio_port_arrive(port, called, locked);
  }
  if (!audio_port_wait_room(port, handle)) {
    pthread_mutex_unlock(&g_audio_lock);
    return ORBIS_AUDIO_OUT_ERROR_NOT_OPENED;
  }
  if (pcm) {
    audio_port_push(port, pcm, &log);
  }
  bool host = g_host_running;
  dump_log_pending(&log);
  audio_stats_maybe_log(&log);
  uint64_t next = port->next_sync_time_us;
  pthread_mutex_unlock(&g_audio_lock);
  logbuf_flush(&log);
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
  AudioLogBuf log;
  log.len = 0;
  log.truncated = false;
  uint64_t called = get_time_micros();
  pthread_mutex_lock(&g_audio_lock);
  uint64_t locked = get_time_micros();
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
    uint64_t pcm = *(uint64_t *)(ctx->mem_base + param_addr + i * 16ULL + 8);
    if (pcm) {
      audio_port_arrive(audio_port_get(handle), called, locked);
    }
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
      audio_port_push(port, ctx->mem_base + pcm, &log);
    }
  }
  bool host = g_host_running;
  dump_log_pending(&log);
  audio_stats_maybe_log(&log);
  int32_t h0 = *(int32_t *)(ctx->mem_base + param_addr);
  Ps4AudioPort *first = audio_port_get(h0);
  uint64_t next = first ? first->next_sync_time_us : 0;
  pthread_mutex_unlock(&g_audio_lock);
  logbuf_flush(&log);
  if (!host && first) {
    audio_port_pace_time(len, &next);
    pthread_mutex_lock(&g_audio_lock);
    if (first->in_use && first->handle == h0) first->next_sync_time_us = next;
    pthread_mutex_unlock(&g_audio_lock);
  }
  ctx->rax = (uint64_t)len;
  SHIM_RETURN();
}

int32_t ps4_audioout_port_stats(int32_t handle, Ps4AudioPortStats *out) {
  if (!out) {
    return ORBIS_AUDIO_OUT_ERROR_INVALID_POINTER;
  }
  pthread_mutex_lock(&g_audio_lock);
  Ps4AudioPort *p = audio_port_get(handle);
  if (!p) {
    pthread_mutex_unlock(&g_audio_lock);
    return ORBIS_AUDIO_OUT_ERROR_INVALID_PORT;
  }
  out->outputs = p->outputs;
  out->underruns = p->underruns;
  out->underrun_frames = p->underrun_frames;
  out->late_outputs = p->late_outputs;
  out->max_gap_us = p->max_gap_us;
  out->last_output_us = mono_micros_of(p->last_output_us);
  out->first_sound_us = mono_micros_of(p->first_sound_us);
  out->first_sound_play_us = mono_micros_of(p->first_sound_play_us);
  out->first_sound_outputs = p->first_sound_outputs;
  out->ring_frames = p->ring_cap;
  pthread_mutex_unlock(&g_audio_lock);
  return 0;
}

int32_t ps4_audioout_port_close(int32_t handle) {
  if (handle <= 0 || handle >= MAX_AUDIO_PORTS) {
    return ORBIS_AUDIO_OUT_ERROR_INVALID_PORT;
  }
  AudioLogBuf log;
  log.len = 0;
  log.truncated = false;
  pthread_mutex_lock(&g_audio_lock);
  Ps4AudioPort *port = &g_ports[handle];
  bool closed = port->in_use != 0;
  if (closed) {
    audio_port_gap_lines(port, &log); // the gaps since the last stats line
    free(port->ring);
    port->ring = NULL;
    port->in_use = 0;
  }
  pthread_cond_broadcast(&g_audio_cond);
  pthread_mutex_unlock(&g_audio_lock);
  logbuf_flush(&log);
  if (closed) {
    fprintf(stderr, "[AudioOut] close handle %d\n", handle);
  }
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
  pthread_mutex_unlock(&g_audio_lock);
  if (audio_log_enabled()) {
    fprintf(stderr, "[AudioOut] SetVolume handle=%d flag=0x%x vol=[%d %d %d %d %d %d %d %d]\n",
            handle, flag, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7]);
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

// A handle that is not open is INVALID_PORT here, as in every call of this module (shadPS4 tells
// a closed port, NOT_OPENED, from an out-of-range one).
int32_t ps4_audioout_port_set_mix_level_padspk(int32_t handle, int32_t level) {
  pthread_mutex_lock(&g_audio_lock);
  Ps4AudioPort *port = audio_port_get(handle);
  int32_t r = 0;
  if (!port) {
    r = ORBIS_AUDIO_OUT_ERROR_INVALID_PORT;
  } else if (port->type != PORT_PADSPK) {
    r = ORBIS_AUDIO_OUT_ERROR_INVALID_PORT_TYPE;
  } else if (level > PS4_AUDIO_OUT_MIXLEVEL_PADSPK_0DB) {
    r = ORBIS_AUDIO_OUT_ERROR_INVALID_MIXLEVEL;
  } else if (g_padspk_override < 0.0f) {
    port->port_gain = (float)(level < 0 ? 0 : level) / (float)VOLUME_0DB;
  }
  bool overridden = g_padspk_override >= 0.0f;
  pthread_mutex_unlock(&g_audio_lock);
  if (audio_log_enabled()) {
    fprintf(stderr, "[AudioOut] SetMixLevelPadSpk handle=%d level=%d -> 0x%x%s\n", handle, level,
            (unsigned)r, overridden ? " (PS4_AUDIO_PADSPK overrides it)" : "");
  }
  return r;
}

// sceAudioOutSetMixLevelPadSpk(handle, mixLevel)
void shim_sceAudioOutSetMixLevelPadSpk(GuestContext *ctx) {
  int32_t r = ps4_audioout_port_set_mix_level_padspk((int32_t)ctx->rdi, (int32_t)ctx->rsi);
  ctx->rax = (uint64_t)(int64_t)r;
  SHIM_RETURN();
}
