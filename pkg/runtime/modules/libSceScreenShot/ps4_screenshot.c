#include "ps4_screenshot.h"
#include <stdbool.h>

static bool g_screenshot_enabled = true;

int32_t sceScreenShotEnable(void) {
    g_screenshot_enabled = true;
    return 0;
}

int32_t sceScreenShotDisable(void) {
    g_screenshot_enabled = false;
    return 0;
}

// Shims
void shim_sceScreenShotEnable(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceScreenShotEnable();
}

void shim_sceScreenShotDisable(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceScreenShotDisable();
}
