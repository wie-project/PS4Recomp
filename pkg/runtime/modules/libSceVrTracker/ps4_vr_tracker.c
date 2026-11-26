// SPDX-License-Identifier: GPL-2.0-or-later

// libSceVrTracker after shadPS4: the parameter checks, memory requirements and device registry
// are complete; tracking itself needs a PlayStation VR headset and camera, which a desktop host
// does not have, so the GPU path reports that no submit happened (as in shadPS4).

#include "ps4_vr_tracker.h"
#include "ps4_direct_mem.h"
#include "ps4_hle_stub.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

uint64_t sceKernelGetProcessTime(void);

static pthread_mutex_t g_vr_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_vr_initialized;
static int32_t g_hmd_handle = -1;
static int32_t g_pad_handle = -1;
static int32_t g_move_handle = -1;
static int32_t g_gun_handle = -1;

static bool calibration_invalid(const OrbisVrTrackerCalibrationSettings *s) {
    // The HMD has no automatic calibration.
    return s->pad_position > ORBIS_VR_TRACKER_CALIBRATION_AUTO ||
           s->hmd_position > ORBIS_VR_TRACKER_CALIBRATION_MANUAL ||
           s->move_position > ORBIS_VR_TRACKER_CALIBRATION_AUTO ||
           s->gun_position > ORBIS_VR_TRACKER_CALIBRATION_AUTO;
}

// Automatic Move calibration doubles the onion memory.
static uint32_t required_onion_size(const OrbisVrTrackerCalibrationSettings *s) {
    return s->move_position == ORBIS_VR_TRACKER_CALIBRATION_AUTO
               ? ORBIS_VR_TRACKER_BASE_ONION_SIZE * 2
               : ORBIS_VR_TRACKER_BASE_ONION_SIZE;
}

int32_t sceVrTrackerQueryMemory(const OrbisVrTrackerQueryMemoryParam *param,
                                OrbisVrTrackerQueryMemoryResult *result) {
    if (!param || !result || param->size != sizeof(OrbisVrTrackerQueryMemoryParam) ||
        (param->profile != ORBIS_VR_TRACKER_PROFILE_000 &&
         param->profile != ORBIS_VR_TRACKER_PROFILE_100) ||
        calibration_invalid(&param->calibration_settings)) {
        return ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID;
    }
    result->direct_memory_onion_size = required_onion_size(&param->calibration_settings);
    result->direct_memory_onion_alignment = ORBIS_VR_TRACKER_MEMORY_ALIGNMENT;
    result->direct_memory_garlic_size = ORBIS_VR_TRACKER_GARLIC_SIZE;
    result->direct_memory_garlic_alignment = ORBIS_VR_TRACKER_MEMORY_ALIGNMENT;
    result->work_memory_size = ORBIS_VR_TRACKER_WORK_SIZE;
    result->work_memory_alignment = ORBIS_VR_TRACKER_MEMORY_ALIGNMENT;
    return 0;
}

static void require_mapping(GuestContext *ctx, uint64_t addr, uint32_t size, const char *what) {
    OrbisKernelVirtualQueryInfo info;
    const int rc = sceKernelVirtualQuery(ctx, (const void *)(uintptr_t)addr, 0, &info, sizeof(info));
    if (rc != 0 || (uint64_t)(uintptr_t)info.end - addr < size) {
        fprintf(stderr, "[ps4-vrtracker] sceVrTrackerInit: insufficient %s memory provided "
                        "(0x%llx, 0x%x bytes)\n",
                what, (unsigned long long)addr, size);
        abort();
    }
}

int32_t sceVrTrackerInit(GuestContext *ctx, const OrbisVrTrackerInitParam *param) {
    pthread_mutex_lock(&g_vr_mutex);
    if (g_vr_initialized) {
        pthread_mutex_unlock(&g_vr_mutex);
        return ORBIS_VR_TRACKER_ERROR_ALREADY_INITIALIZED;
    }
    pthread_mutex_unlock(&g_vr_mutex);

    // The console reads the structure without a null check.
    const OrbisVrTrackerCalibrationSettings *cal = &param->calibration_settings;
    if (param->size != sizeof(OrbisVrTrackerInitParam) ||
        param->direct_memory_garlic == 0 ||
        param->direct_memory_garlic_alignment != ORBIS_VR_TRACKER_MEMORY_ALIGNMENT ||
        param->direct_memory_garlic_size != ORBIS_VR_TRACKER_GARLIC_SIZE ||
        param->direct_memory_onion == 0 ||
        param->direct_memory_onion_alignment != ORBIS_VR_TRACKER_MEMORY_ALIGNMENT ||
        param->direct_memory_onion_size != required_onion_size(cal) ||
        param->work_memory == 0 ||
        param->work_memory_alignment != ORBIS_VR_TRACKER_MEMORY_ALIGNMENT ||
        param->work_memory_size != ORBIS_VR_TRACKER_WORK_SIZE ||
        (uint32_t)param->gpu_pipe_id >= ORBIS_VR_TRACKER_NUM_COMPUTE_PIPES ||
        (uint32_t)param->gpu_queue_id >= ORBIS_VR_TRACKER_NUM_QUEUES_PER_PIPE ||
        calibration_invalid(cal)) {
        return ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID;
    }

    require_mapping(ctx, param->direct_memory_garlic, param->direct_memory_garlic_size, "garlic");
    require_mapping(ctx, param->direct_memory_onion, param->direct_memory_onion_size, "onion");
    require_mapping(ctx, param->work_memory, param->work_memory_size, "work");

    fprintf(stderr, "[ps4-vrtracker] PlayStation VR headsets are not supported\n");
    pthread_mutex_lock(&g_vr_mutex);
    g_vr_initialized = true;
    pthread_mutex_unlock(&g_vr_mutex);
    return 0;
}

int32_t sceVrTrackerRegisterDeviceInternal(uint32_t device_type, int32_t handle, int32_t unk0,
                                           int32_t unk1) {
    (void)unk1;
    pthread_mutex_lock(&g_vr_mutex);
    int32_t rc = 0;
    int32_t *slot = NULL;
    if (!g_vr_initialized) {
        rc = ORBIS_VR_TRACKER_ERROR_NOT_INIT;
    } else if (device_type > ORBIS_VR_TRACKER_DEVICE_GUN || unk0 > 4) {
        rc = ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID;
    } else {
        // The handle itself is not validated (no device behind it is emulated).
        switch (device_type) {
        case ORBIS_VR_TRACKER_DEVICE_HMD: slot = &g_hmd_handle; break;
        case ORBIS_VR_TRACKER_DEVICE_DUALSHOCK4: slot = &g_pad_handle; break;
        case ORBIS_VR_TRACKER_DEVICE_MOVE: slot = &g_move_handle; break;
        default: slot = &g_gun_handle; break;
        }
        if (*slot != -1) {
            rc = ORBIS_VR_TRACKER_ERROR_DEVICE_ALREADY_REGISTERED;
        } else {
            *slot = handle;
        }
    }
    pthread_mutex_unlock(&g_vr_mutex);
    return rc;
}

int32_t sceVrTrackerRegisterDevice(uint32_t device_type, int32_t handle) {
    return sceVrTrackerRegisterDeviceInternal(device_type, handle, -1, 0);
}

int32_t sceVrTrackerRegisterDevice2(uint32_t device_type, int32_t handle) {
    return sceVrTrackerRegisterDeviceInternal(device_type, handle, -1, 1);
}

static bool vr_initialized(void) {
    pthread_mutex_lock(&g_vr_mutex);
    const bool init = g_vr_initialized;
    pthread_mutex_unlock(&g_vr_mutex);
    return init;
}

int32_t sceVrTrackerGetTime(uint64_t *time) {
    if (!vr_initialized()) {
        return ORBIS_VR_TRACKER_ERROR_NOT_INIT;
    }
    if (!time) {
        return ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID;
    }
    *time = sceKernelGetProcessTime();
    return 0;
}

// Valid submit data needs a camera frame, and sceCameraGetFrameData has none.
int32_t sceVrTrackerGpuSubmit(const void *param) {
    (void)param;
    if (!vr_initialized()) {
        return ORBIS_VR_TRACKER_ERROR_NOT_INIT;
    }
    return ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID;
}

int32_t sceVrTrackerGpuWait(const OrbisVrTrackerGpuWaitParam *param) {
    if (!vr_initialized()) {
        return ORBIS_VR_TRACKER_ERROR_NOT_INIT;
    }
    if (!param || param->size != sizeof(OrbisVrTrackerGpuWaitParam)) {
        return ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID;
    }
    return ORBIS_VR_TRACKER_ERROR_NOT_EXECUTE_GPU_SUBMIT;
}

int32_t sceVrTrackerGpuWaitAndCpuProcess(void) {
    if (!vr_initialized()) {
        return ORBIS_VR_TRACKER_ERROR_NOT_INIT;
    }
    return ORBIS_VR_TRACKER_ERROR_NOT_EXECUTE_GPU_SUBMIT;
}

int32_t sceVrTrackerRecalibrate(const OrbisVrTrackerRecalibrateParam *param) {
    pthread_mutex_lock(&g_vr_mutex);
    int32_t rc = 0;
    if (!g_vr_initialized) {
        rc = ORBIS_VR_TRACKER_ERROR_NOT_INIT;
    } else if (!param || param->size != sizeof(OrbisVrTrackerRecalibrateParam) ||
               param->device_type > ORBIS_VR_TRACKER_DEVICE_GUN) {
        rc = ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID;
    } else {
        switch (param->device_type) {
        case ORBIS_VR_TRACKER_DEVICE_HMD:
            // No headset is ever connected.
            rc = ORBIS_VR_TRACKER_ERROR_DEVICE_NOT_REGISTERED;
            break;
        case ORBIS_VR_TRACKER_DEVICE_DUALSHOCK4:
            rc = g_pad_handle == -1 ? ORBIS_VR_TRACKER_ERROR_DEVICE_NOT_REGISTERED : 0;
            break;
        case ORBIS_VR_TRACKER_DEVICE_MOVE:
            rc = g_move_handle == -1 ? ORBIS_VR_TRACKER_ERROR_DEVICE_NOT_REGISTERED : 0;
            break;
        default:
            rc = g_gun_handle == -1 ? ORBIS_VR_TRACKER_ERROR_DEVICE_NOT_REGISTERED : 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_vr_mutex);
    return rc;
}

int32_t sceVrTrackerSetDurationUntilStatusNotTracking(uint32_t device_type,
                                                      uint32_t duration_camera_frames) {
    (void)duration_camera_frames;
    if (!vr_initialized()) {
        return ORBIS_VR_TRACKER_ERROR_NOT_INIT;
    }
    if (device_type > ORBIS_VR_TRACKER_DEVICE_GUN) {
        return ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID;
    }
    return 0;
}

int32_t sceVrTrackerUnregisterDevice(int32_t handle) {
    pthread_mutex_lock(&g_vr_mutex);
    int32_t rc = 0;
    if (!g_vr_initialized) {
        rc = ORBIS_VR_TRACKER_ERROR_NOT_INIT;
    } else if (handle < 0) {
        rc = ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID;
    } else if (handle == g_hmd_handle) {
        g_hmd_handle = -1;
    } else if (handle == g_pad_handle) {
        g_pad_handle = -1;
    } else if (handle == g_move_handle) {
        g_move_handle = -1;
    } else if (handle == g_gun_handle) {
        g_gun_handle = -1;
    } else {
        rc = ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID;
    }
    pthread_mutex_unlock(&g_vr_mutex);
    return rc;
}

// Registered devices stay registered across Term and Init, as in shadPS4.
int32_t sceVrTrackerTerm(void) {
    pthread_mutex_lock(&g_vr_mutex);
    int32_t rc = 0;
    if (!g_vr_initialized) {
        rc = ORBIS_VR_TRACKER_ERROR_NOT_INIT;
    } else {
        g_vr_initialized = false;
    }
    pthread_mutex_unlock(&g_vr_mutex);
    return rc;
}

static void *guest_ptr(GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

void shim_sceVrTrackerQueryMemory(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerQueryMemory(guest_ptr(ctx, ctx->rdi),
                                                           guest_ptr(ctx, ctx->rsi));
    SHIM_RETURN();
}

void shim_sceVrTrackerInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerInit(ctx, guest_ptr(ctx, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceVrTrackerTerm(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerTerm();
    SHIM_RETURN();
}

void shim_sceVrTrackerRegisterDevice(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerRegisterDevice((uint32_t)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceVrTrackerRegisterDevice2(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerRegisterDevice2((uint32_t)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceVrTrackerRegisterDeviceInternal(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerRegisterDeviceInternal(
        (uint32_t)ctx->rdi, (int32_t)ctx->rsi, (int32_t)ctx->rdx, (int32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceVrTrackerUnregisterDevice(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerUnregisterDevice((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceVrTrackerGetTime(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerGetTime(guest_ptr(ctx, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceVrTrackerGpuSubmit(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerGpuSubmit(guest_ptr(ctx, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceVrTrackerGpuWait(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerGpuWait(guest_ptr(ctx, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceVrTrackerGpuWaitAndCpuProcess(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerGpuWaitAndCpuProcess();
    SHIM_RETURN();
}

void shim_sceVrTrackerRecalibrate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerRecalibrate(guest_ptr(ctx, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceVrTrackerSetDurationUntilStatusNotTracking(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceVrTrackerSetDurationUntilStatusNotTracking(
        (uint32_t)ctx->rdi, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

PS4_HLE_STUB(sceVrTrackerCpuProcess)
PS4_HLE_STUB(sceVrTrackerGetPlayAreaWarningInfo)
PS4_HLE_STUB(sceVrTrackerGetResult)
PS4_HLE_STUB(sceVrTrackerNotifyEndOfCpuProcess)
PS4_HLE_STUB(sceVrTrackerResetAll)
PS4_HLE_STUB(sceVrTrackerResetOrientationRelative)
PS4_HLE_STUB(sceVrTrackerSaveInternalBuffers)
PS4_HLE_STUB(sceVrTrackerSetExtendedMode)
PS4_HLE_STUB(sceVrTrackerSetLEDBrightness)
PS4_HLE_STUB(sceVrTrackerSetRestingMode)
PS4_HLE_STUB(sceVrTrackerUpdateMotionSensorData)
PS4_HLE_STUB(sceVrTrackerSetDeviceRejection)
PS4_HLE_STUB(sceVrTrackerCpuPopMarker)
PS4_HLE_STUB(sceVrTrackerCpuPushMarker)
PS4_HLE_STUB(sceVrTrackerGetLiveCaptureId)
PS4_HLE_STUB(sceVrTrackerStartLiveCapture)
PS4_HLE_STUB(sceVrTrackerStopLiveCapture)
PS4_HLE_STUB(Func_0FA4C949F8D3024E)
PS4_HLE_STUB(Func_285C6AFC09C42F7E)
PS4_HLE_STUB(Func_9A6CDB2103664F8A)
PS4_HLE_STUB(Func_B4D26B7D8B18DF06)
PS4_HLE_STUB(Func_1119B0BE399F37E7)
PS4_HLE_STUB(Func_4928B43816BC440D)
PS4_HLE_STUB(Func_863EF32EFCB0FA9C)
PS4_HLE_STUB(Func_E6E726CBC85C48F9)
PS4_HLE_STUB(Func_F6407E46C66DF383)
