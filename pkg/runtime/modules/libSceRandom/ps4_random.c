#include "ps4_random.h"
#include <stdlib.h>

// Requests above ORBIS_RANDOM_MAX_SIZE fail with ORBIS_RANDOM_ERROR_INVALID, as in shadPS4.
// The bytes come from the host CSPRNG: the PS4 library is a cryptographic source.
int32_t sceRandomGetRandomNumber(void *buf, size_t size) {
    if (size > ORBIS_RANDOM_MAX_SIZE || (!buf && size > 0)) {
        return ORBIS_RANDOM_ERROR_INVALID;
    }
    if (size > 0) {
        arc4random_buf(buf, size);
    }
    return 0;
}

void shim_sceRandomGetRandomNumber(GuestContext *ctx) {
    uint64_t bufGuest = ctx->rdi;
    size_t size = (size_t)ctx->rsi;
    void *buf = bufGuest ? (void *)(ctx->mem_base + bufGuest) : NULL;

    int32_t ret = sceRandomGetRandomNumber(buf, size);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}
