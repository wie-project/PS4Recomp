// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_VR_TRACKER_H
#define PS4_VR_TRACKER_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_VR_TRACKER_ERROR_NOT_INIT ((int32_t)0x81260801)
#define ORBIS_VR_TRACKER_ERROR_ALREADY_INITIALIZED ((int32_t)0x81260802)
#define ORBIS_VR_TRACKER_ERROR_DEVICE_NOT_REGISTERED ((int32_t)0x81260803)
#define ORBIS_VR_TRACKER_ERROR_DEVICE_ALREADY_REGISTERED ((int32_t)0x81260804)
#define ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID ((int32_t)0x81260806)
#define ORBIS_VR_TRACKER_ERROR_NOT_EXECUTE_GPU_SUBMIT ((int32_t)0x81260815)

#define ORBIS_VR_TRACKER_MEMORY_ALIGNMENT 0x10000u
#define ORBIS_VR_TRACKER_BASE_ONION_SIZE 0x400000u
#define ORBIS_VR_TRACKER_GARLIC_SIZE 0x3000000u
#define ORBIS_VR_TRACKER_WORK_SIZE 0x1000000u

// Compute pipes a title may use (the eighth is reserved by the system) and queues per pipe.
#define ORBIS_VR_TRACKER_NUM_COMPUTE_PIPES 7u
#define ORBIS_VR_TRACKER_NUM_QUEUES_PER_PIPE 8u

enum {
    ORBIS_VR_TRACKER_PROFILE_000 = 0,
    ORBIS_VR_TRACKER_PROFILE_100 = 100,
};

enum {
    ORBIS_VR_TRACKER_CALIBRATION_MANUAL = 0,
    ORBIS_VR_TRACKER_CALIBRATION_AUTO = 1,
};

enum {
    ORBIS_VR_TRACKER_DEVICE_HMD = 0,
    ORBIS_VR_TRACKER_DEVICE_DUALSHOCK4 = 1,
    ORBIS_VR_TRACKER_DEVICE_MOVE = 2,
    ORBIS_VR_TRACKER_DEVICE_GUN = 3,
};

typedef struct OrbisVrTrackerCalibrationSettings {
    uint32_t hmd_position;
    uint32_t pad_position;
    uint32_t move_position;
    uint32_t gun_position;
    uint32_t reserved[4];
} OrbisVrTrackerCalibrationSettings;

typedef struct OrbisVrTrackerQueryMemoryParam {
    uint32_t size;
    uint32_t profile;
    uint32_t reserved[6];
    OrbisVrTrackerCalibrationSettings calibration_settings;
} OrbisVrTrackerQueryMemoryParam;

typedef struct OrbisVrTrackerQueryMemoryResult {
    uint32_t size;
    uint32_t direct_memory_onion_size;
    uint32_t direct_memory_onion_alignment;
    uint32_t direct_memory_garlic_size;
    uint32_t direct_memory_garlic_alignment;
    uint32_t work_memory_size;
    uint32_t work_memory_alignment;
    uint32_t reserved[9];
} OrbisVrTrackerQueryMemoryResult;

// The memory pointers are guest addresses.
typedef struct OrbisVrTrackerInitParam {
    uint32_t size;
    uint32_t profile;
    uint32_t execution_mode;
    int32_t hmd_thread_priority;
    int32_t pad_thread_priority;
    int32_t move_thread_priority;
    int32_t gun_thread_priority;
    int32_t reserved;
    uint64_t cpu_mask;
    OrbisVrTrackerCalibrationSettings calibration_settings;
    uint64_t direct_memory_onion;
    uint32_t direct_memory_onion_size;
    uint32_t direct_memory_onion_alignment;
    uint64_t direct_memory_garlic;
    uint32_t direct_memory_garlic_size;
    uint32_t direct_memory_garlic_alignment;
    uint64_t work_memory;
    uint32_t work_memory_size;
    uint32_t work_memory_alignment;
    int32_t gpu_pipe_id;
    int32_t gpu_queue_id;
} OrbisVrTrackerInitParam;

typedef struct OrbisVrTrackerGpuWaitParam {
    uint32_t size;
    uint32_t reserved[7];
} OrbisVrTrackerGpuWaitParam;

typedef struct OrbisVrTrackerRecalibrateParam {
    uint32_t size;
    uint32_t device_type;
    uint32_t calibration_type;
    uint32_t reserved[5];
} OrbisVrTrackerRecalibrateParam;

_Static_assert(sizeof(OrbisVrTrackerQueryMemoryParam) == 64, "QueryMemoryParam layout");
_Static_assert(sizeof(OrbisVrTrackerQueryMemoryResult) == 64, "QueryMemoryResult layout");
_Static_assert(sizeof(OrbisVrTrackerInitParam) == 128, "InitParam layout");
_Static_assert(offsetof(OrbisVrTrackerInitParam, direct_memory_onion) == 72, "InitParam layout");
_Static_assert(sizeof(OrbisVrTrackerGpuWaitParam) == 32, "GpuWaitParam layout");
_Static_assert(sizeof(OrbisVrTrackerRecalibrateParam) == 32, "RecalibrateParam layout");

int32_t sceVrTrackerQueryMemory(const OrbisVrTrackerQueryMemoryParam *param,
                                OrbisVrTrackerQueryMemoryResult *result);
// ctx resolves the guest memory ranges in param: each must lie in one mapping long enough for
// its size, or the process stops (the console faults on such a range later).
int32_t sceVrTrackerInit(GuestContext *ctx, const OrbisVrTrackerInitParam *param);
int32_t sceVrTrackerRegisterDeviceInternal(uint32_t device_type, int32_t handle, int32_t unk0,
                                           int32_t unk1);
int32_t sceVrTrackerRegisterDevice(uint32_t device_type, int32_t handle);
int32_t sceVrTrackerRegisterDevice2(uint32_t device_type, int32_t handle);
int32_t sceVrTrackerGetTime(uint64_t *time);
int32_t sceVrTrackerGpuSubmit(const void *param);
int32_t sceVrTrackerGpuWait(const OrbisVrTrackerGpuWaitParam *param);
int32_t sceVrTrackerGpuWaitAndCpuProcess(void);
int32_t sceVrTrackerRecalibrate(const OrbisVrTrackerRecalibrateParam *param);
int32_t sceVrTrackerSetDurationUntilStatusNotTracking(uint32_t device_type,
                                                      uint32_t duration_camera_frames);
int32_t sceVrTrackerUnregisterDevice(int32_t handle);
int32_t sceVrTrackerTerm(void);

void shim_sceVrTrackerQueryMemory(GuestContext *ctx);
void shim_sceVrTrackerInit(GuestContext *ctx);
void shim_sceVrTrackerTerm(GuestContext *ctx);
void shim_sceVrTrackerRegisterDevice(GuestContext *ctx);
void shim_sceVrTrackerRegisterDevice2(GuestContext *ctx);
void shim_sceVrTrackerRegisterDeviceInternal(GuestContext *ctx);
void shim_sceVrTrackerUnregisterDevice(GuestContext *ctx);
void shim_sceVrTrackerGetTime(GuestContext *ctx);
void shim_sceVrTrackerGpuSubmit(GuestContext *ctx);
void shim_sceVrTrackerGpuWait(GuestContext *ctx);
void shim_sceVrTrackerGpuWaitAndCpuProcess(GuestContext *ctx);
void shim_sceVrTrackerRecalibrate(GuestContext *ctx);
void shim_sceVrTrackerSetDurationUntilStatusNotTracking(GuestContext *ctx);

// Functions shadPS4 stubs as well.
void shim_sceVrTrackerCpuProcess(GuestContext *ctx);
void shim_sceVrTrackerGetPlayAreaWarningInfo(GuestContext *ctx);
void shim_sceVrTrackerGetResult(GuestContext *ctx);
void shim_sceVrTrackerNotifyEndOfCpuProcess(GuestContext *ctx);
void shim_sceVrTrackerResetAll(GuestContext *ctx);
void shim_sceVrTrackerResetOrientationRelative(GuestContext *ctx);
void shim_sceVrTrackerSaveInternalBuffers(GuestContext *ctx);
void shim_sceVrTrackerSetExtendedMode(GuestContext *ctx);
void shim_sceVrTrackerSetLEDBrightness(GuestContext *ctx);
void shim_sceVrTrackerSetRestingMode(GuestContext *ctx);
void shim_sceVrTrackerUpdateMotionSensorData(GuestContext *ctx);
void shim_sceVrTrackerSetDeviceRejection(GuestContext *ctx);
void shim_sceVrTrackerCpuPopMarker(GuestContext *ctx);
void shim_sceVrTrackerCpuPushMarker(GuestContext *ctx);
void shim_sceVrTrackerGetLiveCaptureId(GuestContext *ctx);
void shim_sceVrTrackerStartLiveCapture(GuestContext *ctx);
void shim_sceVrTrackerStopLiveCapture(GuestContext *ctx);
void shim_Func_0FA4C949F8D3024E(GuestContext *ctx);
void shim_Func_285C6AFC09C42F7E(GuestContext *ctx);
void shim_Func_9A6CDB2103664F8A(GuestContext *ctx);
void shim_Func_B4D26B7D8B18DF06(GuestContext *ctx);
void shim_Func_1119B0BE399F37E7(GuestContext *ctx);
void shim_Func_4928B43816BC440D(GuestContext *ctx);
void shim_Func_863EF32EFCB0FA9C(GuestContext *ctx);
void shim_Func_E6E726CBC85C48F9(GuestContext *ctx);
void shim_Func_F6407E46C66DF383(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_VR_TRACKER_H
