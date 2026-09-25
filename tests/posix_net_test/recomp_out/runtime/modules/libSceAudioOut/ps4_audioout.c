#include "ps4_audioout.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <mach/mach_time.h>
#endif

#define MAX_AUDIO_PORTS 16

typedef struct {
  int in_use;
  int32_t handle;
  uint32_t length;
  uint32_t freq;
  uint32_t param;
  uint32_t bytes_per_frame;
  pthread_mutex_t lock;
  uint64_t next_sync_time_us;
#ifdef __APPLE__
  AudioQueueRef queue;
  bool queue_started;
#endif
} Ps4AudioPort;

static Ps4AudioPort g_audio_ports[MAX_AUDIO_PORTS];
static pthread_mutex_t g_audio_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_audio_inited = false;

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

#ifdef __APPLE__
static void recomp_audio_queue_callback(void *userData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer) {
  (void)userData;
  AudioQueueFreeBuffer(inAQ, inBuffer);
}
#endif

void shim_sceAudioOutInit(GuestContext *ctx) {
  pthread_mutex_lock(&g_audio_lock);
  g_audio_inited = true;
  pthread_mutex_unlock(&g_audio_lock);
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceAudioOutOpen(GuestContext *ctx) {
  // rdi = userId, rsi = type, rdx = index, rcx = len, r8 = freq, r9 = param
  uint32_t len = (uint32_t)ctx->rcx;
  uint32_t freq = (uint32_t)ctx->r8;
  uint32_t param = (uint32_t)ctx->r9;

  if (freq == 0) {
    freq = 48000;
  }
  if (len == 0) {
    len = 256;
  }

  pthread_mutex_lock(&g_audio_lock);
  int slot = -1;
  for (int i = 1; i < MAX_AUDIO_PORTS; i++) {
    if (!g_audio_ports[i].in_use) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    pthread_mutex_unlock(&g_audio_lock);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  Ps4AudioPort *port = &g_audio_ports[slot];
  memset(port, 0, sizeof(*port));
  port->in_use = 1;
  port->handle = slot;
  port->length = len;
  port->freq = freq;
  port->param = param;
  port->bytes_per_frame = 4; // default S16 stereo (2 channels * 2 bytes)
  pthread_mutex_init(&port->lock, NULL);

#ifdef __APPLE__
  AudioStreamBasicDescription asbd;
  memset(&asbd, 0, sizeof(asbd));
  asbd.mSampleRate = (Float64)freq;
  asbd.mFormatID = kAudioFormatLinearPCM;
  asbd.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
  asbd.mChannelsPerFrame = 2;
  asbd.mBitsPerChannel = 16;
  asbd.mBytesPerFrame = 4;
  asbd.mFramesPerPacket = 1;
  asbd.mBytesPerPacket = 4;

  OSStatus status = AudioQueueNewOutput(&asbd, recomp_audio_queue_callback, port, NULL, NULL, 0, &port->queue);
  if (status == noErr) {
    AudioQueueStart(port->queue, NULL);
    port->queue_started = true;
  } else {
    port->queue = NULL;
    port->queue_started = false;
  }
#endif

  pthread_mutex_unlock(&g_audio_lock);
  ctx->rax = (uint64_t)slot;
  SHIM_RETURN();
}

void shim_sceAudioOutOutput(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  uint64_t pcm_ptr_addr = ctx->rsi;

  if (handle <= 0 || handle >= MAX_AUDIO_PORTS) {
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  Ps4AudioPort *port = &g_audio_ports[handle];
  if (!port->in_use) {
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  if (pcm_ptr_addr == 0) {
    // Synchronization request: wait for buffer completion / pace audio playback
    uint64_t frame_dur_us = ((uint64_t)port->length * 1000000ULL) / (port->freq ? port->freq : 48000);
    uint64_t now = get_time_micros();
    if (port->next_sync_time_us == 0 || port->next_sync_time_us < now) {
      port->next_sync_time_us = now + frame_dur_us;
    } else {
      if (port->next_sync_time_us > now) {
        uint64_t diff = port->next_sync_time_us - now;
        if (diff > 50000) {
          diff = 50000;
        }
        usleep((useconds_t)diff);
      }
      port->next_sync_time_us += frame_dur_us;
    }
    ctx->rax = 0;
    SHIM_RETURN();
  }

#ifdef __APPLE__
  if (port->queue && port->queue_started) {
    uint32_t byte_count = port->length * port->bytes_per_frame;
    AudioQueueBufferRef buffer = NULL;
    OSStatus status = AudioQueueAllocateBuffer(port->queue, byte_count, &buffer);
    if (status == noErr && buffer != NULL) {
      const void *src = ctx->mem_base + pcm_ptr_addr;
      memcpy(buffer->mAudioData, src, byte_count);
      buffer->mAudioDataByteSize = byte_count;
      AudioQueueEnqueueBuffer(port->queue, buffer, 0, NULL);
    }
  }
#endif

  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceAudioOutClose(GuestContext *ctx) {
  int32_t handle = (int32_t)ctx->rdi;
  if (handle <= 0 || handle >= MAX_AUDIO_PORTS) {
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  pthread_mutex_lock(&g_audio_lock);
  Ps4AudioPort *port = &g_audio_ports[handle];
  if (port->in_use) {
#ifdef __APPLE__
    if (port->queue) {
      AudioQueueStop(port->queue, true);
      AudioQueueDispose(port->queue, true);
      port->queue = NULL;
    }
#endif
    pthread_mutex_destroy(&port->lock);
    port->in_use = 0;
  }
  pthread_mutex_unlock(&g_audio_lock);

  ctx->rax = 0;
  SHIM_RETURN();
}
