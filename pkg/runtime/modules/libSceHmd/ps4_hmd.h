// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_HMD_H
#define PS4_HMD_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_context.h"

// Error codes (shadPS4 hmd/hmd_error.h).
#define ORBIS_HMD_ERROR_ALREADY_INITIALIZED 0x81110001
#define ORBIS_HMD_ERROR_NOT_INITIALIZED 0x81110002
#define ORBIS_HMD_ERROR_HANDLE_INVALID 0x81110003
#define ORBIS_HMD_ERROR_DEVICE_DISCONNECTED 0x81110004
#define ORBIS_HMD_ERROR_WRONG_ID 0x81110005
#define ORBIS_HMD_ERROR_ALREADY_OPENED 0x81110006
#define ORBIS_HMD_ERROR_INSUFFICIENT_RESOURCE 0x81110007
#define ORBIS_HMD_ERROR_PARAMETER_NULL 0x81110008
#define ORBIS_HMD_ERROR_PARAMETER_INVALID 0x81110009
#define ORBIS_HMD_ERROR_NO_2D_DISTORTION_MAP_MEMORY 0x8111000A
#define ORBIS_HMD_ERROR_2D_DISTORTION_MAP_INITIALIZATION_ERROR 0x8111000B
#define ORBIS_HMD_ERROR_REPROJECTION_NOT_INITIALIZED 0x8111000C
#define ORBIS_HMD_ERROR_REPROJECTION_ALREADY_INITIALIZED 0x8111000D
#define ORBIS_HMD_ERROR_REPROJECTION_HMD_NOT_READY 0x8111000E
#define ORBIS_HMD_ERROR_REPROJECTION_NO_DISPLAY_BUFFER 0x8111000F
#define ORBIS_HMD_ERROR_REPROJECTION_WRONG_DISPLAY_BUFFER 0x81110010
#define ORBIS_HMD_ERROR_REPROJECTION_DISPLAY_BUFFER_BUSY 0x81110011
#define ORBIS_HMD_ERROR_REPROJECTION_RESOURCE_ALREADY_SET 0x81110012
#define ORBIS_HMD_ERROR_REPROJECTION_RESOURCE_NOT_SET 0x81110013
#define ORBIS_HMD_ERROR_REPROJECTION_THREAD_NOT_WORKING 0x81110014
#define ORBIS_HMD_ERROR_REPROJECTION_NEVER_PROCESSED 0x81110015
#define ORBIS_HMD_ERROR_UNSUPPORTED_FEATURE 0x81110016

// OrbisHmdDeviceStatus (u32)
#define ORBIS_HMD_DEVICE_STATUS_READY 0
#define ORBIS_HMD_DEVICE_STATUS_NOT_READY 1
#define ORBIS_HMD_DEVICE_STATUS_NOT_DETECTED 2
#define ORBIS_HMD_DEVICE_STATUS_NOT_READY_HMU_DISCONNECT 3

// The handle sceHmdOpen returns: positive, and different from the handles of other devices
// (libSceVrTracker tells them apart), as in shadPS4.
#define ORBIS_HMD_INTERNAL_HANDLE 0xf000000

// Firmware from which a missing headset makes the per-handle queries answer HANDLE_INVALID
// (Common::ElfInfo::FW_450).
#define ORBIS_HMD_FW_450 0x4500000u

// Reprojection and distortion memory requirements (returned, not written).
#define ORBIS_HMD_REPROJECTION_GARLIC_BUFF_ALIGN 0x100
#define ORBIS_HMD_REPROJECTION_GARLIC_BUFF_SIZE 0x100000
#define ORBIS_HMD_REPROJECTION_ONION_BUFF_ALIGN 0x100
#define ORBIS_HMD_REPROJECTION_ONION_BUFF_SIZE 0x810
#define ORBIS_HMD_DISTORTION_WORK_MEMORY_ALIGN 0x400
#define ORBIS_HMD_DISTORTION_WORK_MEMORY_SIZE 0x20000

typedef struct OrbisHmdInitializeParam {
    uint64_t reserved0; // guest pointer: initializes the distortion sub-library when set
    uint8_t reserved[8];
} OrbisHmdInitializeParam;

typedef struct OrbisHmdOpenParam {
    uint8_t reserve[32];
} OrbisHmdOpenParam;

typedef struct OrbisHmdFieldOfView {
    float tan_out;
    float tan_in;
    float tan_top;
    float tan_bottom;
} OrbisHmdFieldOfView;

typedef struct OrbisHmdDeviceInformation {
    uint32_t status; // OrbisHmdDeviceStatus
    int32_t user_id;
    uint8_t reserve0[4];
    uint32_t panel_width, panel_height;     // OrbisHmdPanelResolution
    uint16_t latency_90hz, latency_120hz;   // OrbisHmdFlipToDisplayLatency
    uint8_t hmu_mount;
    uint8_t reserve1[7];
} OrbisHmdDeviceInformation;

typedef struct OrbisHmdEyeOffset {
    float offset_x;
    float offset_y;
    float offset_z;
    uint8_t reserve[20];
} OrbisHmdEyeOffset;

_Static_assert(sizeof(OrbisHmdInitializeParam) == 16, "OrbisHmdInitializeParam");
_Static_assert(sizeof(OrbisHmdOpenParam) == 32, "OrbisHmdOpenParam");
_Static_assert(sizeof(OrbisHmdFieldOfView) == 16, "OrbisHmdFieldOfView");
_Static_assert(sizeof(OrbisHmdDeviceInformation) == 32, "OrbisHmdDeviceInformation");
_Static_assert(sizeof(OrbisHmdEyeOffset) == 32, "OrbisHmdEyeOffset");

// The library logic; guest pointers are already host pointers (NULL for a null guest address),
// `fw` is the title's compiled SDK version.
int32_t sceHmdInitialize(const OrbisHmdInitializeParam *param);
int32_t sceHmdInitialize315(const OrbisHmdInitializeParam *param);
int32_t sceHmdOpen(int32_t user_id, int32_t type, int32_t index, const OrbisHmdOpenParam *param);
int32_t sceHmdClose(int32_t handle);
int32_t sceHmdTerminate(void);
int32_t sceHmdGet2DEyeOffset(uint32_t fw, int32_t handle, OrbisHmdEyeOffset *left, OrbisHmdEyeOffset *right);
int32_t sceHmdGetAssyError(const void *data);
int32_t sceHmdGetDeviceInformation(OrbisHmdDeviceInformation *info);
int32_t sceHmdGetDeviceInformationByHandle(uint32_t fw, int32_t handle, OrbisHmdDeviceInformation *info);
int32_t sceHmdGetFieldOfView(uint32_t fw, int32_t handle, OrbisHmdFieldOfView *fov);
int32_t sceHmdGetInertialSensorData(uint32_t fw, int32_t handle);
int32_t sceHmdInternalGetDeviceStatus(uint32_t *status);
int32_t sceHmdDistortionInitialize(void);
int32_t sceHmdDistortionTerminate(void);

#endif // PS4_HMD_H
