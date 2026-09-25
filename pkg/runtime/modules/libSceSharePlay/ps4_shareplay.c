#include "ps4_shareplay.h"
#include <stdbool.h>

static bool g_shareplay_initialized = false;
static int32_t g_shareplay_prohibited = 0;

int32_t sceSharePlayInitialize(void) {
    g_shareplay_initialized = true;
    return 0;
}

int32_t sceSharePlayTerminate(void) {
    g_shareplay_initialized = false;
    return 0;
}

int32_t sceSharePlaySetProhibition(int32_t prohibited) {
    g_shareplay_prohibited = prohibited;
    return 0;
}

// Shims
void shim_sceSharePlayInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSharePlayInitialize();
}

void shim_sceSharePlayTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSharePlayTerminate();
}

void shim_sceSharePlaySetProhibition(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSharePlaySetProhibition((int32_t)ctx->rdi);
}
