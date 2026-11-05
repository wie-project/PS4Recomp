#include "ps4_screenshot.h"
#include <stdbool.h>
#include <string.h>

static bool g_screenshot_enabled = true;

int32_t sceScreenShotEnable(void) {
    g_screenshot_enabled = true;
    return 0;
}

int32_t sceScreenShotDisable(void) {
    g_screenshot_enabled = false;
    return 0;
}

typedef struct OrbisScreenShotOverlay {
    char filePath[1024];
    int32_t posX;
    int32_t posY;
    int32_t origin;
    bool hasOverlay;
} OrbisScreenShotOverlay;

static OrbisScreenShotOverlay g_screenshot_overlay;

int32_t sceScreenShotSetOverlayImage(const char *filePath, int32_t posX, int32_t posY) {
    return sceScreenShotSetOverlayImageWithOrigin(filePath, posX, posY, 0);
}

int32_t sceScreenShotSetOverlayImageWithOrigin(const char *filePath, int32_t posX, int32_t posY, int32_t origin) {
    if (!filePath || filePath[0] == '\0') {
        g_screenshot_overlay.hasOverlay = false;
        g_screenshot_overlay.filePath[0] = '\0';
        return 0;
    }
    strncpy(g_screenshot_overlay.filePath, filePath, sizeof(g_screenshot_overlay.filePath) - 1);
    g_screenshot_overlay.filePath[sizeof(g_screenshot_overlay.filePath) - 1] = '\0';
    g_screenshot_overlay.posX = posX;
    g_screenshot_overlay.posY = posY;
    g_screenshot_overlay.origin = origin;
    g_screenshot_overlay.hasOverlay = true;
    return 0;
}

// Shims
void shim_sceScreenShotEnable(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceScreenShotEnable();
    SHIM_RETURN();
}

void shim_sceScreenShotDisable(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceScreenShotDisable();
    SHIM_RETURN();
}

void shim_sceScreenShotSetOverlayImage(GuestContext *ctx) {
    const char *filePath = ctx->rdi ? (const char *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)sceScreenShotSetOverlayImage(filePath, (int32_t)ctx->rsi, (int32_t)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceScreenShotSetOverlayImageWithOrigin(GuestContext *ctx) {
    const char *filePath = ctx->rdi ? (const char *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)sceScreenShotSetOverlayImageWithOrigin(filePath, (int32_t)ctx->rsi, (int32_t)ctx->rdx, (int32_t)ctx->rcx);
    SHIM_RETURN();
}
