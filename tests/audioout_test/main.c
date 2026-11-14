// Host test for the libSceAudioOut timing counters, on the headless device (PS4_AUDIO_NULL: the
// rings are mixed every 512 frames on the 48 kHz clock, nothing is played). Build and run (from
// the repository root):
//   clang -std=c11 -I pkg/runtime/core -I pkg/runtime/modules/libSceAudioOut \
//     -framework AudioToolbox -framework CoreFoundation -Wl,-undefined,dynamic_lookup \
//     tests/audioout_test/main.c pkg/runtime/modules/libSceAudioOut/ps4_audioout.c \
//     -o /tmp/audioout_test && /tmp/audioout_test

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ps4_audioout_internal.h"

#define CHECK(cond)                                                                                \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      fprintf(stderr, "[audioout-test] FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      abort();                                                                                     \
    }                                                                                              \
  } while (0)

#define LEN 256

int main(void) {
  setenv("PS4_AUDIO_NULL", "1", 1);
  static float grain[LEN * 2];
  for (int i = 0; i < LEN * 2; i++) grain[i] = 0.25f;

  int32_t h = ps4_audioout_port_open(255, PS4_AUDIO_OUT_PORT_MAIN, 0, LEN, 48000,
                                     PS4_AUDIO_OUT_FMT_FLOAT_STEREO);
  CHECK(h > 0);
  Ps4AudioDeviceStats dev0, dev;
  Ps4AudioPortStats st0, st;
  ps4_audioout_device_stats(&dev0);
  CHECK(dev0.running);

  // A producer that keeps up: 1 s of grains, each call blocking until the ring has room.
  for (int i = 0; i < 48000 / LEN; i++) CHECK(ps4_audioout_port_output(h, grain) == LEN);
  CHECK(ps4_audioout_port_stats(h, &st0) == 0);
  ps4_audioout_device_stats(&dev0);
  CHECK(dev0.periods > 80); // ~94 periods of 512 frames in 1 s
  // The producer never comes late. The device may still starve: the headless device sleeps
  // between periods, and on a host whose timers fire late it mixes the missed periods back to
  // back, faster than a producer blocked in its output call can refill (so no underrun count is
  // checked here).
  CHECK(st0.late_outputs == 0);
  CHECK(st0.min_queued != UINT32_MAX);
  CHECK(st0.max_work_us < 20000);
  printf("[+] steady producer: %llu periods, min queued %u frames, %llu underrun frames\n",
         (unsigned long long)dev0.periods, st0.min_queued, (unsigned long long)st0.underrun_frames);

  // A producer that stalls for 100 ms: the device finds the ring short, the next grain is late
  // and the stall shows as guest work.
  usleep(100000);
  ps4_audioout_device_stats(&dev);
  CHECK(dev.starved_periods >= dev0.starved_periods + 5);
  CHECK(ps4_audioout_port_output(h, grain) == LEN);
  CHECK(ps4_audioout_port_stats(h, &st) == 0);
  CHECK(st.min_queued == 0);
  CHECK(st.late_outputs == st0.late_outputs + 1);
  CHECK(st.underrun_frames >= st0.underrun_frames + 2048);
  CHECK(st.max_work_us >= 100000);
  printf("[+] stalled producer: %llu starved periods, %llu underrun frames, guest work %.1f ms\n",
         (unsigned long long)(dev.starved_periods - dev0.starved_periods),
         (unsigned long long)(st.underrun_frames - st0.underrun_frames), st.max_work_us / 1000.0);

  CHECK(ps4_audioout_port_close(h) == 0);
  printf("[audioout-test] all passed\n");
  return 0;
}
