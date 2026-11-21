// Host test for libSceAjm instance ids: DRIVECLUB's FMOD creates 192 ATRAC9 instances at startup and
// shuts its audio output down when one fails. Build and run (from the repository root, with the
// libatrac9 and minimp3 submodules checked out and libatrac9 built):
//   clang -std=c11 -I pkg/runtime/core -I pkg/runtime/modules/libSceAjm \
//     -I 3rdparty/libatrac9/C/src -I 3rdparty/minimp3 -Wl,-undefined,dynamic_lookup \
//     tests/ajm_test/main.c pkg/runtime/modules/libSceAjm/ps4_ajm.c 3rdparty/libatrac9/libatrac9.a \
//     -o /tmp/ajm_test && /tmp/ajm_test

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps4_ajm.h"

#define CHECK(cond)                                                                                \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      fprintf(stderr, "[ajm-test] FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
      abort();                                                                                     \
    }                                                                                              \
  } while (0)

void shim_sceAjmInitialize(GuestContext *ctx);
void shim_sceAjmModuleRegister(GuestContext *ctx);
void shim_sceAjmInstanceCreate(GuestContext *ctx);
void shim_sceAjmInstanceDestroy(GuestContext *ctx);
void shim_sceAjmFinalize(GuestContext *ctx);

enum { kCodecAt9 = 1, kInstances = 192 };

static uint8_t g_mem[4096];
static GuestContext g_ctx;

// Calls a shim with up to four integer arguments; returns rax.
static uint32_t call(void (*fn)(GuestContext *), uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  g_ctx.rdi = a, g_ctx.rsi = b, g_ctx.rdx = c, g_ctx.rcx = d;
  fn(&g_ctx);
  return (uint32_t)g_ctx.rax;
}

int main(void) {
  g_ctx.mem_base = g_mem;
  g_ctx.mem_size = sizeof g_mem;
  uint32_t *out = (uint32_t *)(g_mem + 16); // guest address 16

  CHECK(call(shim_sceAjmInitialize, 0, 16, 0, 0) == 0);
  const uint32_t context = *out;
  CHECK(call(shim_sceAjmModuleRegister, context, kCodecAt9, 0, 0) == 0);

  static uint32_t ids[kInstances];
  const uint64_t flags = (2u << 3) | (2u << 7); // 2 channels, float
  for (int i = 0; i < kInstances; i++) {
    CHECK(call(shim_sceAjmInstanceCreate, context, kCodecAt9, flags, 16) == 0);
    ids[i] = *out;
    CHECK(ids[i] >> 14 == kCodecAt9);
    CHECK((ids[i] & 0x3FFF) != 0);
    for (int k = 0; k < i; k++) CHECK(ids[k] != ids[i]);
  }

  // A destroyed slot is reused; an unknown or destroyed id is rejected.
  CHECK(call(shim_sceAjmInstanceDestroy, context, ids[100], 0, 0) == 0);
  CHECK(call(shim_sceAjmInstanceDestroy, context, ids[100], 0, 0) == ORBIS_AJM_ERROR_INVALID_INSTANCE);
  CHECK(call(shim_sceAjmInstanceCreate, context, kCodecAt9, flags, 16) == 0);
  CHECK(*out == ids[100]);
  CHECK(call(shim_sceAjmInstanceDestroy, context, 0x3FFF | (kCodecAt9 << 14), 0, 0) ==
        ORBIS_AJM_ERROR_INVALID_INSTANCE);

  for (int i = 0; i < kInstances; i++) CHECK(call(shim_sceAjmInstanceDestroy, context, ids[i], 0, 0) == 0);
  CHECK(call(shim_sceAjmFinalize, context, 0, 0, 0) == 0);
  printf("[ajm-test] ok: %d instances\n", kInstances);
  return 0;
}
