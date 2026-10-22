#ifndef PS4_HMD_H
#define PS4_HMD_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_context.h"

#define ORBIS_HMD_ERROR_ALREADY_INITIALIZED 0x81110001
#define ORBIS_HMD_ERROR_NOT_INITIALIZED     0x81110002
#define ORBIS_HMD_ERROR_HANDLE_INVALID      0x81110003
#define ORBIS_HMD_ERROR_DEVICE_DISCONNECTED 0x81110004
#define ORBIS_HMD_ERROR_WRONG_ID            0x81110005
#define ORBIS_HMD_ERROR_ALREADY_OPENED      0x81110006
#define ORBIS_HMD_ERROR_PARAMETER_NULL      0x81110008
#define ORBIS_HMD_ERROR_PARAMETER_INVALID   0x81110009

#define ORBIS_HMD_DEVICE_STATUS_NOT_DETECTED 0
#define ORBIS_HMD_DEVICE_STATUS_READY        1

typedef struct OrbisHmdInitializeParam {
    void *reserved0;
} OrbisHmdInitializeParam;

typedef struct OrbisHmdOpenParam {
    uint32_t reserved0;
} OrbisHmdOpenParam;

typedef struct OrbisHmdDeviceInformation {
    uint32_t status;
    int32_t user_id;
    uint32_t reserved[14];
} OrbisHmdDeviceInformation;

typedef struct OrbisHmdEyeOffset {
    float offset_x;
    float offset_y;
    float offset_z;
} OrbisHmdEyeOffset;

typedef struct OrbisHmdFieldOfView {
    float left;
    float right;
    float top;
    float bottom;
} OrbisHmdFieldOfView;

#endif // PS4_HMD_H
