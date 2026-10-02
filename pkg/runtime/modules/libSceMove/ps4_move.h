#ifndef PS4_MOVE_H
#define PS4_MOVE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_MOVE_ERROR_NO_CONTROLLER_CONNECTED 1
#define ORBIS_MOVE_ERROR_NOT_INIT 0x80EE0001
#define ORBIS_MOVE_ERROR_ALREADY_INIT 0x80EE0002
#define ORBIS_MOVE_ERROR_INVALID_ARG 0x80EE0003
#define ORBIS_MOVE_ERROR_INVALID_HANDLE 0x80EE0004

typedef struct OrbisMoveDeviceInfo {
    float sphere_radius;
    float accelerometer_offset[3];
} OrbisMoveDeviceInfo;

typedef struct OrbisMoveButtonData {
    uint16_t button_data;
    uint16_t trigger_data;
} OrbisMoveButtonData;

typedef struct OrbisMoveExtensionPortData {
    uint16_t status;
    uint16_t digital0;
    uint16_t digital1;
    uint16_t analog_right_x;
    uint16_t analog_right_y;
    uint16_t analog_left_x;
    uint16_t analog_left_y;
    unsigned char custom[5];
} OrbisMoveExtensionPortData;

typedef struct OrbisMoveData {
    float accelerometer[3];
    float gyro[3];
    OrbisMoveButtonData button_data;
    OrbisMoveExtensionPortData extension_data;
    int64_t timestamp;
    int32_t count;
    float temperature;
} OrbisMoveData;

int32_t sceMoveInit(void);
int32_t sceMoveOpen(int32_t userId, int32_t type, int32_t index);
int32_t sceMoveClose(int32_t handle);
int32_t sceMoveTerm(void);
int32_t sceMoveGetDeviceInfo(int32_t handle, OrbisMoveDeviceInfo *info);
int32_t sceMoveReadStateLatest(int32_t handle, OrbisMoveData *data);
int32_t sceMoveReadStateRecent(int32_t handle, int64_t timestamp, OrbisMoveData *data, int32_t *out_count);
int32_t sceMoveGetExtensionPortInfo(int32_t handle, void *data);
int32_t sceMoveSetVibration(int32_t handle, uint8_t intensity);
int32_t sceMoveSetLightSphere(int32_t handle, uint8_t red, uint8_t green, uint8_t blue);
int32_t sceMoveResetLightSphere(int32_t handle);

void shim_sceMoveInit(GuestContext *ctx);
void shim_sceMoveOpen(GuestContext *ctx);
void shim_sceMoveClose(GuestContext *ctx);
void shim_sceMoveTerm(GuestContext *ctx);
void shim_sceMoveGetDeviceInfo(GuestContext *ctx);
void shim_sceMoveReadStateLatest(GuestContext *ctx);
void shim_sceMoveReadStateRecent(GuestContext *ctx);
void shim_sceMoveGetExtensionPortInfo(GuestContext *ctx);
void shim_sceMoveSetVibration(GuestContext *ctx);
void shim_sceMoveSetLightSphere(GuestContext *ctx);
void shim_sceMoveResetLightSphere(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_MOVE_H
