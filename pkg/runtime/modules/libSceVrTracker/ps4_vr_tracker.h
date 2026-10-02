#ifndef PS4_VR_TRACKER_H
#define PS4_VR_TRACKER_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OrbisVrTrackerMemoryResult {
    uint32_t direct_memory_onion_size;
    uint32_t direct_memory_onion_alignment;
    uint32_t direct_memory_garlic_size;
    uint32_t direct_memory_garlic_alignment;
    uint32_t work_memory_size;
    uint32_t work_memory_alignment;
} OrbisVrTrackerMemoryResult;

int32_t sceVrTrackerQueryMemory(void *param, OrbisVrTrackerMemoryResult *result);
int32_t sceVrTrackerInit(const void *param);
int32_t sceVrTrackerTerm(void);
int32_t sceVrTrackerRegisterDevice(int32_t deviceType, int32_t handle);
int32_t sceVrTrackerRegisterDevice2(int32_t deviceType, int32_t handle);
int32_t sceVrTrackerUnregisterDevice(int32_t handle);
int32_t sceVrTrackerUpdateMotionSensorData(const void *param);
int32_t sceVrTrackerGetResult(const void *param, void *result);
int32_t sceVrTrackerRecalibrate(const void *param);
int32_t sceVrTrackerCpuProcess(const void *param);
int32_t sceVrTrackerGpuSubmit(const void *param);
int32_t sceVrTrackerGpuWait(const void *param);
int32_t sceVrTrackerGetTime(uint64_t *time);
int32_t sceVrTrackerNotifyEndOfCpuProcess(void);
int32_t sceVrTrackerResetOrientationRelative(int32_t deviceType, int32_t handle);
int32_t sceVrTrackerGetPlayAreaWarningInfo(void *info);

void shim_sceVrTrackerQueryMemory(GuestContext *ctx);
void shim_sceVrTrackerInit(GuestContext *ctx);
void shim_sceVrTrackerTerm(GuestContext *ctx);
void shim_sceVrTrackerRegisterDevice(GuestContext *ctx);
void shim_sceVrTrackerRegisterDevice2(GuestContext *ctx);
void shim_sceVrTrackerUnregisterDevice(GuestContext *ctx);
void shim_sceVrTrackerUpdateMotionSensorData(GuestContext *ctx);
void shim_sceVrTrackerGetResult(GuestContext *ctx);
void shim_sceVrTrackerRecalibrate(GuestContext *ctx);
void shim_sceVrTrackerCpuProcess(GuestContext *ctx);
void shim_sceVrTrackerGpuSubmit(GuestContext *ctx);
void shim_sceVrTrackerGpuWait(GuestContext *ctx);
void shim_sceVrTrackerGetTime(GuestContext *ctx);
void shim_sceVrTrackerNotifyEndOfCpuProcess(GuestContext *ctx);
void shim_sceVrTrackerResetOrientationRelative(GuestContext *ctx);
void shim_sceVrTrackerGetPlayAreaWarningInfo(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_VR_TRACKER_H
