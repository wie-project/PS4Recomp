#ifndef PS4_RANDOM_H
#define PS4_RANDOM_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t sceRandomGetRandomNumber(void *buf, size_t size);
void shim_sceRandomGetRandomNumber(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_RANDOM_H
