#ifndef PS4_PAD_H
#define PS4_PAD_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#define ORBIS_PAD_BUTTON_L3        0x0002
#define ORBIS_PAD_BUTTON_R3        0x0004
#define ORBIS_PAD_BUTTON_OPTIONS   0x0008
#define ORBIS_PAD_BUTTON_UP        0x0010
#define ORBIS_PAD_BUTTON_RIGHT     0x0020
#define ORBIS_PAD_BUTTON_DOWN      0x0040
#define ORBIS_PAD_BUTTON_LEFT      0x0080
#define ORBIS_PAD_BUTTON_L2        0x0100
#define ORBIS_PAD_BUTTON_R2        0x0200
#define ORBIS_PAD_BUTTON_L1        0x0400
#define ORBIS_PAD_BUTTON_R1        0x0800
#define ORBIS_PAD_BUTTON_TRIANGLE  0x1000
#define ORBIS_PAD_BUTTON_CIRCLE    0x2000
#define ORBIS_PAD_BUTTON_CROSS     0x4000
#define ORBIS_PAD_BUTTON_SQUARE    0x8000
#define ORBIS_PAD_BUTTON_TOUCH_PAD 0x100000

#define ORBIS_PAD_MAX_TOUCH_NUM 2

typedef struct vec_float3 {
    float x;
    float y;
    float z;
} vec_float3;

typedef struct vec_float4 {
    float x;
    float y;
    float z;
    float w;
} vec_float4;

typedef struct stick {
    uint8_t x;
    uint8_t y;
} stick;

typedef struct analog {
    uint8_t l2;
    uint8_t r2;
} analog;

typedef struct OrbisPadTouch {
    uint16_t x, y;
    uint8_t finger;
    uint8_t pad[3];
} OrbisPadTouch;

typedef struct OrbisPadTouchData {
    uint8_t fingers;
    uint8_t pad1[3];
    uint32_t pad2;
    OrbisPadTouch touch[ORBIS_PAD_MAX_TOUCH_NUM];
} OrbisPadTouchData;

typedef struct OrbisPadData {
    uint32_t buttons;
    stick leftStick;
    stick rightStick;
    analog analogButtons;
    uint16_t padding;
    vec_float4 quat;
    vec_float3 vel;
    vec_float3 acell;
    OrbisPadTouchData touch;
    uint8_t connected;
    uint64_t timestamp;
    uint8_t ext[16];
    uint8_t count;
    uint8_t unknown[15];
} OrbisPadData;

#ifdef __cplusplus
extern "C" {
#endif

int32_t scePadInit(void);
int32_t scePadOpen(int32_t userID, int32_t type, int32_t index, void *param);
int32_t scePadClose(int32_t handle);
int32_t scePadReadState(int32_t handle, OrbisPadData *data);
int32_t scePadRead(int32_t handle, OrbisPadData *data, int32_t count);
int32_t scePadGetHandle(int32_t userID, int32_t type, int32_t index);

// Host input helper (called by window event pump)
void ps4_pad_handle_key(unsigned short keyCode, int isDown);

// Guest ABI shims
void shim_scePadInit(GuestContext *ctx);
void shim_scePadOpen(GuestContext *ctx);
void shim_scePadClose(GuestContext *ctx);
void shim_scePadReadState(GuestContext *ctx);
void shim_scePadRead(GuestContext *ctx);
void shim_scePadGetHandle(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_PAD_H
