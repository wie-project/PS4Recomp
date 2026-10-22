#ifndef PS4_AUDIODEC_H
#define PS4_AUDIODEC_H

// libSceAudiodec HLE (MPEG-4 AAC on AudioToolbox). The API contract is documented at the top of
// ps4_audiodec.c; tests/host/audiodec_test.c drives the guest shims on a fake guest memory.

#include "recomp_runtime.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS4_AUDIODEC_TYPE_M4AAC 3u

#define PS4_AUDIODEC_ERROR_API_FAIL ((int32_t)0x807F0000)
#define PS4_AUDIODEC_ERROR_INVALID_TYPE ((int32_t)0x807F0001)
#define PS4_AUDIODEC_ERROR_INVALID_INIT_PARAM ((int32_t)0x807F0003)
#define PS4_AUDIODEC_ERROR_NOT_INITIALIZED ((int32_t)0x807F0006)
#define PS4_AUDIODEC_ERROR_ALL_HANDLES_IN_USE ((int32_t)0x807F0008)
#define PS4_AUDIODEC_ERROR_INVALID_PTR ((int32_t)0x807F0009)
#define PS4_AUDIODEC_ERROR_INVALID_HANDLE ((int32_t)0x807F000A)
#define PS4_AUDIODEC_ERROR_CH_SHORTAGE ((int32_t)0x807F000C)
#define PS4_AUDIODEC_ERROR_INVALID_WORD_LENGTH ((int32_t)0x807F000D)
#define PS4_AUDIODEC_ERROR_UNSUPPORTED ((int32_t)0x807F000E)
#define PS4_AUDIODEC_ERROR_INVALID_SIZE ((int32_t)0x807F000F)

void shim_sceAudiodecInitLibrary(GuestContext *ctx);
void shim_sceAudiodecTermLibrary(GuestContext *ctx);
void shim_sceAudiodecCreateDecoder(GuestContext *ctx);
void shim_sceAudiodecDecode(GuestContext *ctx);
void shim_sceAudiodecDeleteDecoder(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
