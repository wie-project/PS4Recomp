#ifndef PS4_RANDOM_H
#define PS4_RANDOM_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_RANDOM_ERROR_INVALID ((int32_t)0x817C0016)
#define ORBIS_RANDOM_ERROR_OUT_OF_RESOURCES ((int32_t)0x817C001C)
#define ORBIS_RANDOM_ERROR_FATAL ((int32_t)0x817C00FF)

// Largest request sceRandomGetRandomNumber accepts, in bytes.
#define ORBIS_RANDOM_MAX_SIZE 64

int32_t sceRandomGetRandomNumber(void *buf, size_t size);
void shim_sceRandomGetRandomNumber(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_RANDOM_H
