// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_CAMERA_H
#define PS4_CAMERA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// libSceCamera errors (shadPS4 camera/camera_error.h).
#define ORBIS_CAMERA_ERROR_PARAM ((int32_t)0x802E0000)
#define ORBIS_CAMERA_ERROR_ALREADY_INIT ((int32_t)0x802E0001)
#define ORBIS_CAMERA_ERROR_NOT_INIT ((int32_t)0x802E0002)
#define ORBIS_CAMERA_ERROR_ALREADY_OPEN ((int32_t)0x802E0003)
#define ORBIS_CAMERA_ERROR_NOT_OPEN ((int32_t)0x802E0004)
#define ORBIS_CAMERA_ERROR_ALREADY_START ((int32_t)0x802E0005)
#define ORBIS_CAMERA_ERROR_NOT_START ((int32_t)0x802E0006)
#define ORBIS_CAMERA_ERROR_FORMAT_UNKNOWN ((int32_t)0x802E0007)
#define ORBIS_CAMERA_ERROR_RESOLUTION_UNKNOWN ((int32_t)0x802E0008)
#define ORBIS_CAMERA_ERROR_BAD_FRAMERATE ((int32_t)0x802E0009)
#define ORBIS_CAMERA_ERROR_TIMEOUT ((int32_t)0x802E000A)
#define ORBIS_CAMERA_ERROR_ATTRIBUTE_UNKNOWN ((int32_t)0x802E000B)
#define ORBIS_CAMERA_ERROR_BUSY ((int32_t)0x802E000C)
#define ORBIS_CAMERA_ERROR_UNKNOWN_CONFIG ((int32_t)0x802E000D)
#define ORBIS_CAMERA_ERROR_ALREADY_READ ((int32_t)0x802E000F)
#define ORBIS_CAMERA_ERROR_NOT_CONNECTED ((int32_t)0x802E0010)
#define ORBIS_CAMERA_ERROR_NOT_SUPPORTED ((int32_t)0x802E0011)
#define ORBIS_CAMERA_ERROR_INVALID_CONFIG ((int32_t)0x802E0013)
#define ORBIS_CAMERA_ERROR_MAX_HANDLE ((int32_t)0x802E0014)
#define ORBIS_CAMERA_ERROR_FATAL ((int32_t)0x802E00FF)

#define ORBIS_CAMERA_MAX_DEVICE_NUM 2
#define ORBIS_CAMERA_MAX_FORMAT_LEVEL_NUM 4

// The frame the library delivers (shadPS4 c_width x c_height).
#define CAMERA_FRAME_WIDTH 1280
#define CAMERA_FRAME_HEIGHT 800

// OrbisCameraChannel
#define ORBIS_CAMERA_CHANNEL_0 1
#define ORBIS_CAMERA_CHANNEL_1 2
#define ORBIS_CAMERA_CHANNEL_BOTH 3

// OrbisCameraBaseFormat / OrbisCameraScaleFormat
#define ORBIS_CAMERA_FORMAT_YUV422 0x0
#define ORBIS_CAMERA_FORMAT_RAW16 0x1
#define ORBIS_CAMERA_FORMAT_RAW8 0x2
#define ORBIS_CAMERA_FORMAT_NO_USE 0x10
#define ORBIS_CAMERA_SCALE_FORMAT_YUV422 0x0
#define ORBIS_CAMERA_SCALE_FORMAT_Y16 0x3
#define ORBIS_CAMERA_SCALE_FORMAT_Y8 0x4

// OrbisCameraResolution / OrbisCameraFramerate (defaults only)
#define ORBIS_CAMERA_RESOLUTION_1280X800 0x0
#define ORBIS_CAMERA_FRAMERATE_60 60

// OrbisCameraConfigType
#define ORBIS_CAMERA_CONFIG_TYPE1 0x01
#define ORBIS_CAMERA_CONFIG_TYPE2 0x02
#define ORBIS_CAMERA_CONFIG_TYPE3 0x03
#define ORBIS_CAMERA_CONFIG_TYPE4 0x04
#define ORBIS_CAMERA_CONFIG_TYPE5 0x05
#define ORBIS_CAMERA_CONFIG_EXTENTION 0x10

// OrbisCameraAecAgcTarget bounds
#define ORBIS_CAMERA_ATTRIBUTE_AECAGC_TARGET_DEF 0x00
#define ORBIS_CAMERA_ATTRIBUTE_AECAGC_TARGET_2_0 0x20

// OrbisCameraCalibrationDataFunctionType
#define ORBIS_CAMERA_CALIBRATION_DATA_FUNCTION_TYPE_IMAGE_RECTIFICATION 0
#define ORBIS_CAMERA_CALIBRATION_DATA_FUNCTION_TYPE_IMAGE_INVERSE_RECTIFICATION 1

// Firmware (compiled SDK) versions the checks depend on (shadPS4 Common::ElfInfo).
#define CAMERA_FW_250 0x2500000u
#define CAMERA_FW_300 0x3000000u
#define CAMERA_FW_350 0x3500000u
#define CAMERA_FW_450 0x4500000u

// Guest structures (shadPS4 camera/camera.h, camera_config_types.h). Pointer members hold guest
// addresses.
typedef struct OrbisCameraFormat {
    uint32_t formatLevel0;
    uint32_t formatLevel1;
    uint32_t formatLevel2;
    uint32_t formatLevel3;
} OrbisCameraFormat;

typedef struct OrbisCameraConfigExtention {
    OrbisCameraFormat format;
    uint32_t resolution;
    uint32_t framerate;
    uint32_t width;
    uint32_t height;
    uint32_t reserved1;
    uint64_t pBaseOption;
} OrbisCameraConfigExtention;
_Static_assert(sizeof(OrbisCameraConfigExtention) == 48, "OrbisCameraConfigExtention layout");

typedef struct OrbisCameraConfig {
    uint32_t sizeThis;
    uint32_t configType;
    OrbisCameraConfigExtention configExtention[ORBIS_CAMERA_MAX_DEVICE_NUM];
} OrbisCameraConfig;
_Static_assert(sizeof(OrbisCameraConfig) == 104, "OrbisCameraConfig layout");

typedef struct OrbisCameraDeviceInfo {
    uint32_t sizeThis;
    uint32_t infoRevision;
    uint32_t deviceRevision;
    uint32_t padding;
} OrbisCameraDeviceInfo;

typedef struct OrbisCameraStartParameter {
    uint32_t sizeThis;
    uint32_t formatLevel[ORBIS_CAMERA_MAX_DEVICE_NUM];
    uint64_t pStartOption;
} OrbisCameraStartParameter;
_Static_assert(sizeof(OrbisCameraStartParameter) == 24, "OrbisCameraStartParameter layout");

typedef struct OrbisCameraVideoSyncParameter {
    uint32_t sizeThis;
    uint32_t videoSyncMode;
    uint64_t pModeOption;
} OrbisCameraVideoSyncParameter;
_Static_assert(sizeof(OrbisCameraVideoSyncParameter) == 16, "OrbisCameraVideoSyncParameter layout");

typedef struct OrbisCameraFramePosition {
    uint32_t x;
    uint32_t y;
    uint32_t xSize;
    uint32_t ySize;
} OrbisCameraFramePosition;

typedef struct OrbisCameraAutoExposureGainTarget {
    uint32_t sizeThis;
    uint32_t target;
} OrbisCameraAutoExposureGainTarget;

typedef struct OrbisCameraExposureGain {
    uint32_t exposureControl;
    uint32_t exposure;
    uint32_t gain;
    uint32_t mode;
} OrbisCameraExposureGain;

typedef struct OrbisCameraWhiteBalance {
    uint32_t whiteBalanceControl;
    uint32_t gainRed;
    uint32_t gainBlue;
    uint32_t gainGreen;
} OrbisCameraWhiteBalance;

typedef struct OrbisCameraGamma {
    uint32_t gammaControl;
    uint32_t value;
    uint8_t reserved[16];
} OrbisCameraGamma;

typedef struct OrbisCameraMeta {
    uint32_t metaMode;
    uint32_t format[ORBIS_CAMERA_MAX_DEVICE_NUM][ORBIS_CAMERA_MAX_FORMAT_LEVEL_NUM];
    uint64_t frame[ORBIS_CAMERA_MAX_DEVICE_NUM];
    uint64_t timestamp[ORBIS_CAMERA_MAX_DEVICE_NUM];
    uint32_t deviceTimestamp[ORBIS_CAMERA_MAX_DEVICE_NUM];
    OrbisCameraExposureGain exposureGain[ORBIS_CAMERA_MAX_DEVICE_NUM];
    OrbisCameraWhiteBalance whiteBalance[ORBIS_CAMERA_MAX_DEVICE_NUM];
    OrbisCameraGamma gamma[ORBIS_CAMERA_MAX_DEVICE_NUM];
    uint32_t luminance[ORBIS_CAMERA_MAX_DEVICE_NUM];
    float acceleration_x;
    float acceleration_y;
    float acceleration_z;
    uint64_t vcounter;
    uint32_t reserved[14];
} OrbisCameraMeta;

typedef struct OrbisCameraFrameData {
    uint32_t sizeThis;
    uint32_t readMode;
    OrbisCameraFramePosition framePosition[ORBIS_CAMERA_MAX_DEVICE_NUM][ORBIS_CAMERA_MAX_FORMAT_LEVEL_NUM];
    uint64_t pFramePointerList[ORBIS_CAMERA_MAX_DEVICE_NUM][ORBIS_CAMERA_MAX_FORMAT_LEVEL_NUM];
    uint32_t frameSize[ORBIS_CAMERA_MAX_DEVICE_NUM][ORBIS_CAMERA_MAX_FORMAT_LEVEL_NUM];
    uint32_t status[ORBIS_CAMERA_MAX_DEVICE_NUM];
    OrbisCameraMeta meta;
    // Present from the SDK that made the structure 584 bytes; older games pass 520.
    uint64_t pFramePointerListGarlic[ORBIS_CAMERA_MAX_DEVICE_NUM][ORBIS_CAMERA_MAX_FORMAT_LEVEL_NUM];
} OrbisCameraFrameData;
_Static_assert(sizeof(OrbisCameraFrameData) == 584, "OrbisCameraFrameData layout");

typedef struct OrbisCameraAttribute {
    uint32_t sizeThis;
    int32_t channel;
    OrbisCameraFramePosition framePosition;
    OrbisCameraExposureGain exposureGain;
    OrbisCameraWhiteBalance whiteBalance;
    OrbisCameraGamma gamma;
    uint32_t saturation;
    uint32_t contrast;
    uint32_t sharpness;
    int32_t hue;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
    uint32_t reserved4;
} OrbisCameraAttribute;
_Static_assert(sizeof(OrbisCameraAttribute) == 112, "OrbisCameraAttribute layout");

typedef struct OrbisCameraGetCalibrationDataParameter {
    uint32_t size;
    uint32_t format_type;
    uint32_t function_type;
} OrbisCameraGetCalibrationDataParameter;

// Size of the calibration blob sceCameraGetCalibData writes.
#define CAMERA_CALIB_DATA_SIZE 256

// The camera the library uses: PS4_CAMERA=<index> picks a macOS video capture device (0 is the
// first one); unset or -1 means no camera, shadPS4's default (camera_id -1).
int32_t ps4_camera_configured_id(void);

// The capture backend (ps4_camera_capture.m), shadPS4's SDL camera on AVFoundation. Frames are
// YUY2 (Y0 U Y1 V), CAMERA_FRAME_WIDTH x CAMERA_FRAME_HEIGHT, rows packed.
int32_t ps4_camera_capture_device_count(void);
// Opens and starts device `index`; false when it cannot be opened.
bool ps4_camera_capture_open(int32_t index);
// Copies a frame that arrived since the previous call into dst (CAMERA_FRAME_WIDTH *
// CAMERA_FRAME_HEIGHT * 2 bytes); false when there is none.
bool ps4_camera_capture_acquire(uint8_t *dst);
void ps4_camera_capture_close(void);

// The library over host pointers (shadPS4 camera.cpp, camera_audio.cpp, camera_calibration.cpp,
// camera_config.cpp, camera_device.cpp). sdk is the game's compiled SDK version. Open places
// the frame buffers in guest memory through ctx; GetFrameData returns guest addresses.
int32_t sceCameraOpen(GuestContext *ctx, int32_t user_id, int32_t type, int32_t index, const void *param);
int32_t sceCameraClose(int32_t handle);
int32_t sceCameraCloseByHandle(int32_t handle);
int32_t sceCameraIsAttached(int32_t index);
int32_t sceCameraStart(uint32_t sdk, int32_t handle, const OrbisCameraStartParameter *param);
int32_t sceCameraStartByHandle(int32_t handle, const OrbisCameraStartParameter *param);
int32_t sceCameraStop(int32_t handle);
int32_t sceCameraGetFrameData(int32_t handle, OrbisCameraFrameData *frame_data);
int32_t sceCameraIsValidFrameData(int32_t handle, const OrbisCameraFrameData *frame_data);
int32_t sceCameraGetConfig(int32_t handle, OrbisCameraConfig *config);
int32_t sceCameraSetConfig(uint32_t sdk, int32_t handle, const OrbisCameraConfig *config);
int32_t sceCameraSetConfigInternal(int32_t handle, const OrbisCameraConfig *config);
int32_t sceCameraGetDeviceConfig(int32_t handle, OrbisCameraConfig *config);
int32_t sceCameraGetDeviceConfigWithoutHandle(OrbisCameraConfig *config);
int32_t sceCameraGetDeviceInfo(int32_t reserved, const OrbisCameraDeviceInfo *device_info);
int32_t sceCameraGetAttribute(int32_t handle, OrbisCameraAttribute *attribute);
int32_t sceCameraSetAttribute(int32_t handle, const OrbisCameraAttribute *attribute);
int32_t sceCameraGetAutoExposureGain(uint32_t sdk, int32_t handle, int32_t channel, uint32_t *enable,
                                     OrbisCameraAutoExposureGainTarget *option);
int32_t sceCameraSetAutoExposureGain(uint32_t sdk, int32_t handle, int32_t channel, uint32_t enable,
                                     const OrbisCameraAutoExposureGainTarget *option);
int32_t sceCameraGetAutoWhiteBalance(int32_t handle, int32_t channel, uint32_t *enable, const void *option);
int32_t sceCameraSetAutoWhiteBalance(int32_t handle, int32_t channel, uint32_t enable, const void *option);
int32_t sceCameraGetContrast(int32_t handle, int32_t channel, uint32_t *contrast, const void *option);
int32_t sceCameraSetContrast(int32_t handle, int32_t channel, uint32_t contrast, const void *option);
int32_t sceCameraGetDefectivePixelCancellation(int32_t handle, int32_t channel, uint32_t *enable,
                                               const void *option);
int32_t sceCameraSetDefectivePixelCancellation(int32_t handle, int32_t channel, uint32_t enable,
                                               const void *option);
int32_t sceCameraSetDefectivePixelCancellationInternal(int32_t handle, int32_t channel, uint32_t enable,
                                                       const void *option);
int32_t sceCameraGetExposureGain(int32_t handle, int32_t channel, OrbisCameraExposureGain *exposure_gain,
                                 const void *option);
int32_t sceCameraSetExposureGain(int32_t handle, int32_t channel, const OrbisCameraExposureGain *exposure_gain,
                                 const void *option);
int32_t sceCameraGetGamma(int32_t handle, int32_t channel, OrbisCameraGamma *gamma, const void *option);
int32_t sceCameraSetGamma(int32_t handle, int32_t channel, const OrbisCameraGamma *gamma, const void *option);
int32_t sceCameraGetHue(int32_t handle, int32_t channel, int32_t *hue, const void *option);
int32_t sceCameraSetHue(int32_t handle, int32_t channel, int32_t hue, const void *option);
int32_t sceCameraGetLensCorrection(int32_t handle, int32_t channel, uint32_t *enable, const void *option);
int32_t sceCameraSetLensCorrection(int32_t handle, int32_t channel, uint32_t enable, const void *option);
int32_t sceCameraSetLensCorrectionInternal(int32_t handle, int32_t channel, uint32_t enable, const void *option);
int32_t sceCameraGetSaturation(int32_t handle, int32_t channel, uint32_t *saturation, const void *option);
int32_t sceCameraSetSaturation(int32_t handle, int32_t channel, uint32_t saturation, const void *option);
int32_t sceCameraGetSharpness(int32_t handle, int32_t channel, uint32_t *sharpness, const void *option);
int32_t sceCameraSetSharpness(uint32_t sdk, int32_t handle, int32_t channel, uint32_t sharpness,
                              const void *option);
int32_t sceCameraGetWhiteBalance(int32_t handle, int32_t channel, OrbisCameraWhiteBalance *white_balance,
                                 const void *option);
int32_t sceCameraSetWhiteBalance(int32_t handle, int32_t channel, const OrbisCameraWhiteBalance *white_balance,
                                 const void *option);
int32_t sceCameraGetMmapConnectedCount(uint32_t *count);
int32_t sceCameraGetProductInfo(void *product_info);
int32_t sceCameraGetRegistryInfo(void *registry_info);
int32_t sceCameraSetDebugStop(uint32_t debug_stop_enable);
int32_t sceCameraSetVideoSync(int32_t handle, const OrbisCameraVideoSyncParameter *video_sync);
int32_t sceCameraGetCalibData(int32_t id, void *calib_data);
int32_t sceCameraGetCalibDataFromDevice(void);
int32_t sceCameraGetCalibrationData(const OrbisCameraGetCalibrationDataParameter *param, void *calibration_data);
int32_t sceCameraInitializeRegistryCalibData(void);
int32_t sceCameraAudioOpen(void);
int32_t sceCameraAudioGetData(void *data);

#ifdef __cplusplus
}
#endif

#endif // PS4_CAMERA_H
