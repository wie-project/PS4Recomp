#include "ps4_random.h"
#include <stdlib.h>
#include <errno.h>

int32_t sceRandomGetRandomNumber(void *buf, size_t size) {
    if (!buf && size > 0) {
        return -EINVAL;
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
