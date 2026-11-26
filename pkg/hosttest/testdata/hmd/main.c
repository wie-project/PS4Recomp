// SPDX-License-Identifier: GPL-2.0-or-later

// libSceHmd against shadPS4's behaviour on a host without a PlayStation VR headset: the
// initialize / open / close / terminate state machine, the queries before and from firmware
// 4.50, and the distortion and reprojection memory requirements (return values).

#include "ps4_hmd.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

#include "../host_guest_mem.h"
#define E(code) ((int32_t)(code))

void shim_sceHmdInitialize(GuestContext *ctx);
void shim_sceHmdOpen(GuestContext *ctx);
void shim_sceHmdGetFieldOfView(GuestContext *ctx);
void shim_sceHmdGetDeviceInformation(GuestContext *ctx);
void shim_sceHmdTerminate(GuestContext *ctx);
void shim_sceHmdReprojectionQueryGarlicBuffSize(GuestContext *ctx);
void shim_sceHmdReprojectionQueryGarlicBuffAlign(GuestContext *ctx);
void shim_sceHmdReprojectionQueryOnionBuffSize(GuestContext *ctx);
void shim_sceHmdReprojectionQueryOnionBuffAlign(GuestContext *ctx);
void shim_sceHmdDistortionGetWorkMemorySize(GuestContext *ctx);
void shim_sceHmdDistortionGetWorkMemoryAlign(GuestContext *ctx);
void shim_sceHmdDistortionGetCompoundEyeCorrectionCommand(GuestContext *ctx);
void shim_Func_B26430EA74FC3DC0(GuestContext *ctx);
void shim_Func_9A276E739E54EEAF(GuestContext *ctx);
void shim_sceHmdInternalMmapIsConnect(GuestContext *ctx);
void shim_sceHmdReprojectionStart2dVr(GuestContext *ctx);

// The compiled SDK version the shims read (libkernel's in the runtime).
static uint32_t g_sdk = 0x4000000;
uint32_t ps4_compiled_sdk_version(const GuestContext *ctx) {
    (void)ctx;
    return g_sdk;
}

static int32_t rc_of(const GuestContext *ctx) {
    return (int32_t)ctx->rax;
}

static int test_lifecycle(void) {
    OrbisHmdInitializeParam init;
    memset(&init, 0, sizeof(init));
    OrbisHmdDeviceInformation info;

    CHECK(sceHmdOpen(1, 0, 0, NULL) == E(ORBIS_HMD_ERROR_NOT_INITIALIZED));
    CHECK(sceHmdClose(0) == E(ORBIS_HMD_ERROR_NOT_INITIALIZED));
    CHECK(sceHmdTerminate() == E(ORBIS_HMD_ERROR_NOT_INITIALIZED));
    CHECK(sceHmdGetDeviceInformation(&info) == E(ORBIS_HMD_ERROR_NOT_INITIALIZED));
    CHECK(sceHmdGetDeviceInformation(NULL) == E(ORBIS_HMD_ERROR_PARAMETER_NULL)); // checked first
    CHECK(sceHmdGetAssyError(&info) == E(ORBIS_HMD_ERROR_NOT_INITIALIZED));
    CHECK(sceHmdInitialize(NULL) == E(ORBIS_HMD_ERROR_PARAMETER_NULL));

    CHECK(sceHmdInitialize(&init) == 0);
    CHECK(sceHmdInitialize(&init) == E(ORBIS_HMD_ERROR_ALREADY_INITIALIZED));
    CHECK(sceHmdInitialize315(&init) == E(ORBIS_HMD_ERROR_ALREADY_INITIALIZED));

    // No headset: the device is not detected and has no user before open.
    memset(&info, 0xAB, sizeof(info));
    CHECK(sceHmdGetDeviceInformation(&info) == 0);
    CHECK(info.status == ORBIS_HMD_DEVICE_STATUS_NOT_DETECTED);
    CHECK(info.user_id == -1);
    CHECK(info.panel_width == 0 && info.hmu_mount == 0 && info.reserve1[6] == 0);
    CHECK(sceHmdGetAssyError(&info) == E(ORBIS_HMD_ERROR_DEVICE_DISCONNECTED));
    CHECK(sceHmdGetAssyError(NULL) == E(ORBIS_HMD_ERROR_PARAMETER_NULL));

    OrbisHmdOpenParam op;
    memset(&op, 0, sizeof(op)); // a non-NULL parameter is rejected whatever it holds
    CHECK(sceHmdOpen(1, 1, 0, NULL) == E(ORBIS_HMD_ERROR_PARAMETER_INVALID));
    CHECK(sceHmdOpen(1, 0, 1, NULL) == E(ORBIS_HMD_ERROR_PARAMETER_INVALID));
    CHECK(sceHmdOpen(1, 0, 0, &op) == E(ORBIS_HMD_ERROR_PARAMETER_INVALID));
    CHECK(sceHmdOpen(-1, 0, 0, NULL) == E(ORBIS_HMD_ERROR_PARAMETER_INVALID));
    CHECK(sceHmdOpen(0xFF, 0, 0, NULL) == E(ORBIS_HMD_ERROR_PARAMETER_INVALID));
    const int32_t h = sceHmdOpen(0x1001, 0, 0, NULL);
    CHECK(h == ORBIS_HMD_INTERNAL_HANDLE);
    CHECK(sceHmdOpen(0x1001, 0, 0, NULL) == E(ORBIS_HMD_ERROR_ALREADY_OPENED));
    CHECK(sceHmdGetDeviceInformation(&info) == 0);
    CHECK(info.user_id == 0x1001 && info.status == ORBIS_HMD_DEVICE_STATUS_NOT_DETECTED);

    CHECK(sceHmdClose(h + 1) == E(ORBIS_HMD_ERROR_HANDLE_INVALID));
    CHECK(sceHmdClose(h) == 0);
    CHECK(sceHmdClose(h) == E(ORBIS_HMD_ERROR_HANDLE_INVALID));
    CHECK(sceHmdGetDeviceInformation(&info) == 0 && info.user_id == -1);
    CHECK(sceHmdTerminate() == 0);
    CHECK(sceHmdTerminate() == E(ORBIS_HMD_ERROR_NOT_INITIALIZED));

    // The 3.15 entry point does not start the distortion library; sceHmdInitialize does when the
    // reserved pointer is set, and sceHmdTerminate stops it.
    CHECK(sceHmdInitialize315(&init) == 0);
    CHECK(sceHmdTerminate() == 0);
    CHECK(sceHmdDistortionTerminate() == E(ORBIS_HMD_ERROR_NOT_INITIALIZED));
    init.reserved0 = 0x1000;
    CHECK(sceHmdInitialize(&init) == 0);
    CHECK(sceHmdTerminate() == 0);
    CHECK(sceHmdDistortionTerminate() == E(ORBIS_HMD_ERROR_NOT_INITIALIZED));
    CHECK(sceHmdDistortionInitialize() == 0);
    CHECK(sceHmdDistortionTerminate() == 0);

    uint32_t status = 0;
    CHECK(sceHmdInternalGetDeviceStatus(NULL) == E(ORBIS_HMD_ERROR_PARAMETER_NULL));
    CHECK(sceHmdInternalGetDeviceStatus(&status) == 0 && status == ORBIS_HMD_DEVICE_STATUS_NOT_DETECTED);
    return 0;
}

static int test_queries(void) {
    OrbisHmdInitializeParam init;
    memset(&init, 0, sizeof(init));
    OrbisHmdEyeOffset left, right;
    OrbisHmdFieldOfView fov;
    OrbisHmdDeviceInformation info;
    const uint32_t fw400 = 0x4000000, fw450 = ORBIS_HMD_FW_450;

    CHECK(sceHmdGet2DEyeOffset(fw400, 0, &left, &right) == E(ORBIS_HMD_ERROR_NOT_INITIALIZED));
    CHECK(sceHmdGetInertialSensorData(fw400, 0) == E(ORBIS_HMD_ERROR_NOT_INITIALIZED));
    CHECK(sceHmdInitialize(&init) == 0);
    const int32_t h = sceHmdOpen(1, 0, 0, NULL);
    CHECK(h == ORBIS_HMD_INTERNAL_HANDLE);

    // Before 4.50: default eye offsets; the reserved bytes are left alone.
    memset(&left, 0xAB, sizeof(left));
    memset(&right, 0xAB, sizeof(right));
    CHECK(sceHmdGet2DEyeOffset(fw400, h, &left, NULL) == E(ORBIS_HMD_ERROR_PARAMETER_NULL));
    CHECK(sceHmdGet2DEyeOffset(fw400, h + 1, &left, &right) == E(ORBIS_HMD_ERROR_HANDLE_INVALID));
    CHECK(sceHmdGet2DEyeOffset(fw400, h, &left, &right) == 0);
    CHECK(left.offset_x == -0.0315f && left.offset_y == 0.f && left.offset_z == 0.f);
    CHECK(right.offset_x == 0.0315f && right.offset_y == 0.f && right.offset_z == 0.f);
    CHECK(left.reserve[0] == 0xAB && right.reserve[19] == 0xAB);
    CHECK(sceHmdGetDeviceInformationByHandle(fw400, h, NULL) == E(ORBIS_HMD_ERROR_PARAMETER_NULL));
    CHECK(sceHmdGetDeviceInformationByHandle(fw400, h, &info) == 0);
    CHECK(info.status == ORBIS_HMD_DEVICE_STATUS_NOT_DETECTED && info.user_id == 1);
    CHECK(sceHmdGetInertialSensorData(fw400, h) == E(ORBIS_HMD_ERROR_DEVICE_DISCONNECTED));
    // The field of view fails in the library's own checks without a headset.
    CHECK(sceHmdGetFieldOfView(fw400, h, NULL) == E(ORBIS_HMD_ERROR_PARAMETER_NULL));
    CHECK(sceHmdGetFieldOfView(fw400, h, &fov) == E(ORBIS_HMD_ERROR_HANDLE_INVALID));

    // From 4.50 the per-handle queries answer HANDLE_INVALID, before the pointer checks.
    CHECK(sceHmdGet2DEyeOffset(fw450, h, NULL, NULL) == E(ORBIS_HMD_ERROR_HANDLE_INVALID));
    CHECK(sceHmdGetDeviceInformationByHandle(fw450, h, NULL) == E(ORBIS_HMD_ERROR_HANDLE_INVALID));
    CHECK(sceHmdGetInertialSensorData(fw450, h) == E(ORBIS_HMD_ERROR_HANDLE_INVALID));
    CHECK(sceHmdGetFieldOfView(fw450, h, &fov) == E(ORBIS_HMD_ERROR_HANDLE_INVALID));

    CHECK(sceHmdClose(h) == 0);
    CHECK(sceHmdTerminate() == 0);
    return 0;
}

static int test_shims(void) {
    GuestContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mem_base = GUEST_MEM;
    OrbisHmdInitializeParam init;
    memset(&init, 0, sizeof(init));
    ctx.rdi = A(&init);
    shim_sceHmdInitialize(&ctx);
    CHECK(rc_of(&ctx) == 0);
    ctx.rdi = 1;
    ctx.rsi = ctx.rdx = ctx.rcx = 0;
    shim_sceHmdOpen(&ctx);
    CHECK(rc_of(&ctx) == ORBIS_HMD_INTERNAL_HANDLE);

    // The firmware comes from the title's compiled SDK version.
    OrbisHmdFieldOfView fov;
    ctx.rdi = ORBIS_HMD_INTERNAL_HANDLE;
    ctx.rsi = A(&fov);
    g_sdk = 0x4500001;
    shim_sceHmdGetFieldOfView(&ctx);
    CHECK(rc_of(&ctx) == E(ORBIS_HMD_ERROR_HANDLE_INVALID));

    // A 32-byte information block: nothing past it is written.
    unsigned char buf[40];
    memset(buf, 0xCD, sizeof(buf));
    ctx.rdi = A(buf);
    shim_sceHmdGetDeviceInformation(&ctx);
    CHECK(rc_of(&ctx) == 0);
    CHECK(buf[0] == ORBIS_HMD_DEVICE_STATUS_NOT_DETECTED && buf[31] == 0 && buf[32] == 0xCD);

    // Memory requirements are return values.
    struct {
        void (*fn)(GuestContext *);
        int32_t want;
    } values[] = {
        {shim_sceHmdReprojectionQueryGarlicBuffSize, 0x100000},
        {shim_sceHmdReprojectionQueryGarlicBuffAlign, 0x100},
        {shim_sceHmdReprojectionQueryOnionBuffSize, 0x810},
        {shim_sceHmdReprojectionQueryOnionBuffAlign, 0x100},
        {shim_sceHmdDistortionGetWorkMemorySize, 0x20000},
        {shim_sceHmdDistortionGetWorkMemoryAlign, 0x400},
        {shim_sceHmdDistortionGetCompoundEyeCorrectionCommand, E(ORBIS_HMD_ERROR_PARAMETER_INVALID)},
        {shim_Func_B26430EA74FC3DC0, E(ORBIS_HMD_ERROR_PARAMETER_INVALID)},
        {shim_Func_9A276E739E54EEAF, 0},
        {shim_sceHmdInternalMmapIsConnect, 0},
        {shim_sceHmdReprojectionStart2dVr, 0},
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        ctx.rax = 99;
        ctx.rdi = 0;
        values[i].fn(&ctx);
        CHECK(rc_of(&ctx) == values[i].want);
    }
    shim_sceHmdTerminate(&ctx);
    CHECK(rc_of(&ctx) == 0);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const char *c = argv[1];
    int rc;
    if (strcmp(c, "state-machine") == 0) {
        rc = test_lifecycle();
    } else if (strcmp(c, "queries") == 0) {
        rc = test_queries();
    } else if (strcmp(c, "shims") == 0) {
        rc = test_shims();
    } else {
        return 2;
    }
    if (rc == 0) printf("PASS %s\n", c);
    return rc;
}
