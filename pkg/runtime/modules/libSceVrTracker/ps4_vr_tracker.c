#include "ps4_vr_tracker.h"
#include <string.h>
#include <time.h>

static int g_vr_tracker_initialized = 0;
static int32_t g_registered_devices[8];
static int g_registered_count = 0;

int32_t sceVrTrackerQueryMemory(void *param, OrbisVrTrackerMemoryResult *result) {
    (void)param;
    if (!result) {
        return (int32_t)0x80ED0001; // ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID
    }
    result->direct_memory_onion_size = 0x400000;
    result->direct_memory_onion_alignment = 0x10000;
    result->direct_memory_garlic_size = 0x3000000;
    result->direct_memory_garlic_alignment = 0x10000;
    result->work_memory_size = 0x1000000;
    result->work_memory_alignment = 0x10000;
    return 0;
}

int32_t sceVrTrackerInit(const void *param) {
    (void)param;
    g_vr_tracker_initialized = 1;
    return 0;
}

int32_t sceVrTrackerTerm(void) {
    g_vr_tracker_initialized = 0;
    g_registered_count = 0;
    return 0;
}

int32_t sceVrTrackerRegisterDevice(int32_t deviceType, int32_t handle) {
    (void)deviceType;
    if (g_registered_count < 8) {
        g_registered_devices[g_registered_count++] = handle;
    }
    return 0;
}

int32_t sceVrTrackerRegisterDevice2(int32_t deviceType, int32_t handle) {
    return sceVrTrackerRegisterDevice(deviceType, handle);
}

int32_t sceVrTrackerUnregisterDevice(int32_t handle) {
    for (int i = 0; i < g_registered_count; i++) {
        if (g_registered_devices[i] == handle) {
            g_registered_devices[i] = -1;
            return 0;
        }
    }
    return 0;
}

int32_t sceVrTrackerUpdateMotionSensorData(const void *param) {
    (void)param;
    return 0;
}

int32_t sceVrTrackerGetResult(const void *param, void *result) {
    (void)param;
    (void)result;
    return 0;
}

int32_t sceVrTrackerRecalibrate(const void *param) {
    (void)param;
    return 0;
}

int32_t sceVrTrackerCpuProcess(const void *param) {
    (void)param;
    return 0;
}

int32_t sceVrTrackerGpuSubmit(const void *param) {
    (void)param;
    return 0;
}

int32_t sceVrTrackerGpuWait(const void *param) {
    (void)param;
    return 0;
}

int32_t sceVrTrackerGetTime(uint64_t *out_time) {
    if (!out_time) {
        return (int32_t)0x80ED0001;
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *out_time = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
    return 0;
}

int32_t sceVrTrackerNotifyEndOfCpuProcess(void) {
    return 0;
}

int32_t sceVrTrackerResetOrientationRelative(int32_t deviceType, int32_t handle) {
    (void)deviceType;
    (void)handle;
    return 0;
}

int32_t sceVrTrackerGetPlayAreaWarningInfo(void *info) {
    if (info) {
        memset(info, 0, 32);
    }
    return 0;
}

void shim_sceVrTrackerQueryMemory(GuestContext *ctx) {
    void *param = ctx->rdi ? (void *)(ctx->mem_base + ctx->rdi) : NULL;
    OrbisVrTrackerMemoryResult *result = ctx->rsi ? (OrbisVrTrackerMemoryResult *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerQueryMemory(param, result);
    SHIM_RETURN();
}

void shim_sceVrTrackerInit(GuestContext *ctx) {
    const void *param = ctx->rdi ? (const void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerInit(param);
    SHIM_RETURN();
}

void shim_sceVrTrackerTerm(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerTerm();
    SHIM_RETURN();
}

void shim_sceVrTrackerRegisterDevice(GuestContext *ctx) {
    int32_t deviceType = (int32_t)ctx->rdi;
    int32_t handle = (int32_t)ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerRegisterDevice(deviceType, handle);
    SHIM_RETURN();
}

void shim_sceVrTrackerRegisterDevice2(GuestContext *ctx) {
    int32_t deviceType = (int32_t)ctx->rdi;
    int32_t handle = (int32_t)ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerRegisterDevice2(deviceType, handle);
    SHIM_RETURN();
}

void shim_sceVrTrackerUnregisterDevice(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerUnregisterDevice(handle);
    SHIM_RETURN();
}

void shim_sceVrTrackerUpdateMotionSensorData(GuestContext *ctx) {
    const void *param = ctx->rdi ? (const void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerUpdateMotionSensorData(param);
    SHIM_RETURN();
}

void shim_sceVrTrackerGetResult(GuestContext *ctx) {
    const void *param = ctx->rdi ? (const void *)(ctx->mem_base + ctx->rdi) : NULL;
    void *result = ctx->rsi ? (void *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerGetResult(param, result);
    SHIM_RETURN();
}

void shim_sceVrTrackerRecalibrate(GuestContext *ctx) {
    const void *param = ctx->rdi ? (const void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerRecalibrate(param);
    SHIM_RETURN();
}

void shim_sceVrTrackerCpuProcess(GuestContext *ctx) {
    const void *param = ctx->rdi ? (const void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerCpuProcess(param);
    SHIM_RETURN();
}

void shim_sceVrTrackerGpuSubmit(GuestContext *ctx) {
    const void *param = ctx->rdi ? (const void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerGpuSubmit(param);
    SHIM_RETURN();
}

void shim_sceVrTrackerGpuWait(GuestContext *ctx) {
    const void *param = ctx->rdi ? (const void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerGpuWait(param);
    SHIM_RETURN();
}

void shim_sceVrTrackerGetTime(GuestContext *ctx) {
    uint64_t *out_time = ctx->rdi ? (uint64_t *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerGetTime(out_time);
    SHIM_RETURN();
}

void shim_sceVrTrackerNotifyEndOfCpuProcess(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerNotifyEndOfCpuProcess();
    SHIM_RETURN();
}

void shim_sceVrTrackerResetOrientationRelative(GuestContext *ctx) {
    int32_t deviceType = (int32_t)ctx->rdi;
    int32_t handle = (int32_t)ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerResetOrientationRelative(deviceType, handle);
    SHIM_RETURN();
}

void shim_sceVrTrackerGetPlayAreaWarningInfo(GuestContext *ctx) {
    void *info = ctx->rdi ? (void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerGetPlayAreaWarningInfo(info);
    SHIM_RETURN();
}
