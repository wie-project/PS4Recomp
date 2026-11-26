// SPDX-License-Identifier: GPL-2.0-or-later

// libSceCamera against shadPS4 (src/core/libraries/camera/): handles and open state, the
// parameter checks and defaults of the attribute and config calls, calibration data, and frame
// delivery with the YUY2 -> RAW16 / RAW8 conversion. The AVFoundation backend is replaced by a
// fake device that delivers a known frame; guest memory is a host buffer.

#include "ps4_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

#define SDK_NEW 0x5050001u
#define SDK_OLD 0x2000000u
#define PIXELS ((size_t)CAMERA_FRAME_WIDTH * CAMERA_FRAME_HEIGHT)

// ---------------------------------------------------------------------------------------------
// Guest memory and the runtime functions the library uses

#define GUEST_MEM_SIZE (32u << 20)
static uint8_t *g_mem;
static uint64_t g_next = 0x10000;
static GuestContext g_ctx;
static char g_alloc_name[64];

uint64_t recomp_vm_alloc_named(GuestContext *ctx, size_t size, int prot, int flags, const char *name) {
    (void)ctx;
    (void)prot;
    (void)flags;
    snprintf(g_alloc_name, sizeof(g_alloc_name), "%s", name);
    const uint64_t at = g_next;
    g_next += (size + 0xfff) & ~(uint64_t)0xfff;
    return g_next <= GUEST_MEM_SIZE ? at : (uint64_t)-1;
}

uint32_t ps4_compiled_sdk_version(const GuestContext *ctx) {
    (void)ctx;
    return SDK_NEW;
}

// ---------------------------------------------------------------------------------------------
// Fake capture device

static int32_t g_device_count = 1;
static int g_opened_index = -1, g_open_calls, g_close_calls;
static int g_frames_pending; // frames the device will deliver

int32_t ps4_camera_capture_device_count(void) {
    return g_device_count;
}

bool ps4_camera_capture_open(int32_t index) {
    g_open_calls++;
    g_opened_index = index;
    return true;
}

// A frame where every pair is Y0=100 U=90 Y1=200 V=160.
bool ps4_camera_capture_acquire(uint8_t *dst) {
    if (g_frames_pending == 0) return false;
    g_frames_pending--;
    for (size_t i = 0; i < PIXELS / 2; i++) {
        dst[i * 4 + 0] = 100;
        dst[i * 4 + 1] = 90;
        dst[i * 4 + 2] = 200;
        dst[i * 4 + 3] = 160;
    }
    return true;
}

void ps4_camera_capture_close(void) {
    g_close_calls++;
}

// shadPS4 YuvToRgb, for the expected mosaic values.
static uint8_t clamp(int v) {
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}
static void rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    const int c = y - 16, d = u - 128, e = v - 128;
    *r = clamp((298 * c + 409 * e + 128) >> 8);
    *g = clamp((298 * c - 100 * d - 208 * e + 128) >> 8);
    *b = clamp((298 * c + 516 * d + 128) >> 8);
}

static int32_t open_camera(void) {
    return sceCameraOpen(&g_ctx, 0xFF, 0, 0, NULL);
}

static OrbisCameraStartParameter start_param(uint32_t l0, uint32_t l1) {
    OrbisCameraStartParameter p = {sizeof(p), {l0, l1}, 0};
    return p;
}

static OrbisCameraConfig config_of(uint32_t type) {
    OrbisCameraConfig c;
    memset(&c, 0, sizeof(c));
    c.sizeThis = sizeof(c);
    c.configType = type;
    return c;
}

// ---------------------------------------------------------------------------------------------
// Cases

static int test_open_close(void) {
    CHECK(sceCameraIsAttached(1) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraIsAttached(0) == 0); // no PS4_CAMERA: disconnected
    CHECK(sceCameraClose(1) == ORBIS_CAMERA_ERROR_NOT_OPEN);
    CHECK(sceCameraOpen(&g_ctx, 1, 0, 0, NULL) == ORBIS_CAMERA_ERROR_PARAM); // only the system user
    CHECK(sceCameraOpen(&g_ctx, 0xFF, 1, 0, NULL) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraOpen(&g_ctx, 0xFF, 0, 1, NULL) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(open_camera() == 1);
    CHECK(strcmp(g_alloc_name, "SceCameraGpuGarlicPool") == 0);
    const uint64_t after_pool = g_next;
    CHECK(open_camera() == 2);
    CHECK(g_next == after_pool); // the pool is mapped once
    CHECK(sceCameraClose(0) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraCloseByHandle(1) == 0);
    CHECK(sceCameraStop(2) == 0); // one handle left: still open
    CHECK(sceCameraClose(2) == 0);
    CHECK(g_close_calls == 0); // never started
    CHECK(sceCameraStop(2) == ORBIS_CAMERA_ERROR_NOT_OPEN);
    CHECK(sceCameraClose(2) == ORBIS_CAMERA_ERROR_NOT_OPEN);
    CHECK(open_camera() == 1); // the handle count starts over
    printf("PASS open-close\n");
    return 0;
}

static int test_not_open(void) {
    uint32_t v;
    OrbisCameraConfig c = config_of(1);
    OrbisCameraDeviceInfo info = {sizeof(info), 1, 0, 0};
    OrbisCameraVideoSyncParameter vs = {sizeof(vs), 0, 0};
    OrbisCameraStartParameter sp = start_param(1, 1);
    CHECK(sceCameraGetConfig(1, &c) == ORBIS_CAMERA_ERROR_NOT_OPEN);
    CHECK(sceCameraGetContrast(1, ORBIS_CAMERA_CHANNEL_0, &v, NULL) == ORBIS_CAMERA_ERROR_NOT_OPEN);
    CHECK(sceCameraGetMmapConnectedCount(NULL) == ORBIS_CAMERA_ERROR_NOT_OPEN); // open state first
    CHECK(sceCameraGetProductInfo(NULL) == ORBIS_CAMERA_ERROR_PARAM);          // argument first
    CHECK(sceCameraGetProductInfo(&v) == ORBIS_CAMERA_ERROR_NOT_INIT);
    CHECK(sceCameraGetRegistryInfo(&v) == ORBIS_CAMERA_ERROR_NOT_INIT);
    CHECK(sceCameraGetDeviceInfo(0, &info) == ORBIS_CAMERA_ERROR_NOT_INIT);
    CHECK(sceCameraInitializeRegistryCalibData() == ORBIS_CAMERA_ERROR_NOT_INIT);
    CHECK(sceCameraSetVideoSync(1, &vs) == ORBIS_CAMERA_ERROR_NOT_OPEN);
    CHECK(sceCameraStart(SDK_NEW, 1, &sp) == ORBIS_CAMERA_ERROR_NOT_OPEN);
    CHECK(sceCameraStartByHandle(1, &sp) == ORBIS_CAMERA_ERROR_NOT_OPEN);
    OrbisCameraFrameData fd;
    memset(&fd, 0, sizeof(fd));
    fd.sizeThis = sizeof(fd);
    CHECK(sceCameraGetFrameData(1, &fd) == ORBIS_CAMERA_ERROR_NOT_OPEN);
    CHECK(fd.status[0] == 0xFFFFFFFFu && fd.status[1] == 0xFFFFFFFFu);
    CHECK(sceCameraIsValidFrameData(1, &fd) == ORBIS_CAMERA_ERROR_NOT_OPEN);
    printf("PASS not-open\n");
    return 0;
}

static int test_attributes(void) {
    CHECK(open_camera() == 1);
    OrbisCameraAttribute a;
    memset(&a, 0xAA, sizeof(a));
    a.sizeThis = sizeof(a);
    a.channel = ORBIS_CAMERA_CHANNEL_BOTH; // getters take one channel
    CHECK(sceCameraGetAttribute(1, &a) == ORBIS_CAMERA_ERROR_PARAM);
    a.channel = ORBIS_CAMERA_CHANNEL_1;
    CHECK(sceCameraGetAttribute(1, &a) == 0);
    CHECK(a.channel == ORBIS_CAMERA_CHANNEL_1 && a.exposureGain.exposure == 83 && a.exposureGain.gain == 100);
    CHECK(a.whiteBalance.gainRed == 768 && a.whiteBalance.gainBlue == 768 && a.whiteBalance.gainGreen == 512);
    CHECK(a.gamma.value == 4 && a.saturation == 64 && a.contrast == 32 && a.sharpness == 1 && a.hue == 1);
    CHECK(a.framePosition.x == 0 && a.reserved4 == 0);
    a.channel = ORBIS_CAMERA_CHANNEL_BOTH; // setters take both
    CHECK(sceCameraSetAttribute(1, &a) == ORBIS_CAMERA_ERROR_NOT_CONNECTED);
    a.channel = 4;
    CHECK(sceCameraSetAttribute(1, &a) == ORBIS_CAMERA_ERROR_PARAM);

    uint32_t v = 99;
    int32_t hue = 0;
    uint8_t opt[8] = {0};
    CHECK(sceCameraGetContrast(1, ORBIS_CAMERA_CHANNEL_0, &v, NULL) == 0 && v == 32);
    CHECK(sceCameraGetContrast(1, ORBIS_CAMERA_CHANNEL_0, &v, opt) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraGetContrast(0, ORBIS_CAMERA_CHANNEL_0, &v, NULL) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraGetContrast(1, 0, &v, NULL) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraGetSaturation(1, ORBIS_CAMERA_CHANNEL_1, &v, NULL) == 0 && v == 64);
    CHECK(sceCameraGetSharpness(1, ORBIS_CAMERA_CHANNEL_1, &v, NULL) == 0 && v == 1);
    CHECK(sceCameraGetHue(1, ORBIS_CAMERA_CHANNEL_1, &hue, NULL) == 0 && hue == 1);
    v = 7;
    CHECK(sceCameraGetLensCorrection(1, ORBIS_CAMERA_CHANNEL_1, &v, NULL) == 0 && v == 0);
    CHECK(sceCameraGetAutoWhiteBalance(1, ORBIS_CAMERA_CHANNEL_0, NULL, NULL) == ORBIS_CAMERA_ERROR_PARAM);

    OrbisCameraExposureGain eg;
    CHECK(sceCameraGetExposureGain(1, ORBIS_CAMERA_CHANNEL_0, &eg, NULL) == 0);
    CHECK(eg.exposureControl == 0 && eg.exposure == 20 && eg.gain == 100 && eg.mode == 0);
    CHECK(sceCameraSetExposureGain(1, ORBIS_CAMERA_CHANNEL_0, &eg, NULL) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraSetExposureGain(1, ORBIS_CAMERA_CHANNEL_0, NULL, NULL) == ORBIS_CAMERA_ERROR_NOT_CONNECTED);
    OrbisCameraGamma gm;
    memset(&gm, 0xAA, sizeof(gm));
    CHECK(sceCameraGetGamma(1, ORBIS_CAMERA_CHANNEL_0, &gm, NULL) == 0 && gm.value == 4 && gm.reserved[15] == 0);
    CHECK(sceCameraSetGamma(1, ORBIS_CAMERA_CHANNEL_0, &gm, NULL) == ORBIS_CAMERA_ERROR_PARAM);
    OrbisCameraWhiteBalance wb;
    CHECK(sceCameraGetWhiteBalance(1, ORBIS_CAMERA_CHANNEL_0, &wb, NULL) == 0 && wb.gainGreen == 512);
    CHECK(sceCameraSetWhiteBalance(1, ORBIS_CAMERA_CHANNEL_BOTH, NULL, NULL) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraSetWhiteBalance(1, ORBIS_CAMERA_CHANNEL_BOTH, &wb, NULL) == ORBIS_CAMERA_ERROR_NOT_CONNECTED);

    CHECK(sceCameraSetLensCorrection(1, ORBIS_CAMERA_CHANNEL_0, 2, NULL) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraSetLensCorrectionInternal(1, ORBIS_CAMERA_CHANNEL_0, 2, NULL) == ORBIS_CAMERA_ERROR_NOT_CONNECTED);
    CHECK(sceCameraSetDefectivePixelCancellation(1, ORBIS_CAMERA_CHANNEL_0, 2, NULL) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraSetDefectivePixelCancellationInternal(1, ORBIS_CAMERA_CHANNEL_0, 3, NULL) ==
          ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraSetSharpness(SDK_NEW, 1, ORBIS_CAMERA_CHANNEL_0, 11, NULL) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraSetSharpness(SDK_OLD, 1, ORBIS_CAMERA_CHANNEL_0, 11, NULL) == ORBIS_CAMERA_ERROR_NOT_CONNECTED);

    // Auto exposure: the target option needs SDK 3.00 and an even target up to 2.0.
    OrbisCameraAutoExposureGainTarget t = {sizeof(t), 0x10};
    CHECK(sceCameraGetAutoExposureGain(SDK_OLD, 1, ORBIS_CAMERA_CHANNEL_0, &v, &t) == ORBIS_CAMERA_ERROR_PARAM);
    v = 5;
    CHECK(sceCameraGetAutoExposureGain(SDK_NEW, 1, ORBIS_CAMERA_CHANNEL_0, &v, &t) == 0);
    CHECK(v == 0 && t.sizeThis == 0 && t.target == 0);
    t.sizeThis = sizeof(t);
    t.target = 0x11;
    CHECK(sceCameraSetAutoExposureGain(SDK_NEW, 1, ORBIS_CAMERA_CHANNEL_0, 1, &t) == ORBIS_CAMERA_ERROR_PARAM);
    t.target = 0x22;
    CHECK(sceCameraSetAutoExposureGain(SDK_NEW, 1, ORBIS_CAMERA_CHANNEL_0, 1, &t) == ORBIS_CAMERA_ERROR_PARAM);
    t.target = 0x20;
    CHECK(sceCameraSetAutoExposureGain(SDK_NEW, 1, ORBIS_CAMERA_CHANNEL_0, 1, &t) ==
          ORBIS_CAMERA_ERROR_NOT_CONNECTED);

    CHECK(sceCameraGetMmapConnectedCount(NULL) == ORBIS_CAMERA_ERROR_PARAM);
    v = 3;
    CHECK(sceCameraGetMmapConnectedCount(&v) == 0 && v == 0);
    CHECK(sceCameraSetDebugStop(2) == ORBIS_CAMERA_ERROR_PARAM && sceCameraSetDebugStop(1) == 0);
    OrbisCameraVideoSyncParameter vs = {sizeof(vs), 2, 0};
    CHECK(sceCameraSetVideoSync(1, &vs) == ORBIS_CAMERA_ERROR_PARAM);
    vs.videoSyncMode = 1;
    CHECK(sceCameraSetVideoSync(1, &vs) == 0);
    vs.pModeOption = 0x1000;
    CHECK(sceCameraSetVideoSync(1, &vs) == ORBIS_CAMERA_ERROR_PARAM);
    OrbisCameraDeviceInfo info = {sizeof(info), 2, 0, 0};
    CHECK(sceCameraGetDeviceInfo(0, &info) == ORBIS_CAMERA_ERROR_PARAM);
    info.infoRevision = 1;
    CHECK(sceCameraGetDeviceInfo(0, &info) == ORBIS_CAMERA_ERROR_NOT_CONNECTED);
    CHECK(sceCameraInitializeRegistryCalibData() == 0);
    printf("PASS attributes\n");
    return 0;
}

static int test_config(void) {
    CHECK(open_camera() == 1);
    OrbisCameraConfig c = config_of(9);
    c.sizeThis = 8;
    CHECK(sceCameraGetConfig(1, &c) == ORBIS_CAMERA_ERROR_PARAM);
    c = config_of(9);
    CHECK(sceCameraGetConfig(1, &c) == 0);
    CHECK(c.configType == ORBIS_CAMERA_CONFIG_TYPE1);
    for (int i = 0; i < 2; i++) {
        CHECK(c.configExtention[i].format.formatLevel0 == ORBIS_CAMERA_FORMAT_YUV422);
        CHECK(c.configExtention[i].format.formatLevel3 == ORBIS_CAMERA_SCALE_FORMAT_Y8);
        CHECK(c.configExtention[i].resolution == 0 && c.configExtention[i].framerate == 60);
    }
    memset(&c, 0x55, sizeof(c));
    c.sizeThis = sizeof(c);
    CHECK(sceCameraGetDeviceConfig(1, &c) == 0 && c.sizeThis == sizeof(c) && c.configType == 0);
    memset(&c, 0x55, sizeof(c));
    c.sizeThis = sizeof(c);
    CHECK(sceCameraGetDeviceConfigWithoutHandle(&c) == 0 && c.sizeThis == 0 && c.configType == 0);
    c = config_of(1);
    CHECK(sceCameraSetConfig(SDK_NEW, 1, &c) == ORBIS_CAMERA_ERROR_NOT_CONNECTED); // no camera
    CHECK(sceCameraSetConfigInternal(1, &c) == ORBIS_CAMERA_ERROR_NOT_CONNECTED);
    printf("PASS config\n");
    return 0;
}

static int test_calibration(void) {
    uint8_t blob[CAMERA_CALIB_DATA_SIZE];
    memset(blob, 0xEE, sizeof(blob));
    CHECK(sceCameraGetCalibData(1, NULL) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraGetCalibData(3, blob) == 0 && blob[0] == 0xEE); // other ids: nothing written
    CHECK(sceCameraGetCalibData(1, blob) == 0);
    CHECK(blob[0] == 0x00 && blob[1] == 0x02 && blob[2] == 0x0b && blob[255] == 0x00 && blob[248] == 0x7f);
    CHECK(sceCameraGetCalibData(2, blob) == 0 && blob[0] == 0x09 && blob[2] == 0xff && blob[255] == 0);
    OrbisCameraGetCalibrationDataParameter p = {sizeof(p), 0, 2};
    uint8_t out[16];
    CHECK(sceCameraGetCalibrationData(&p, out) == ORBIS_CAMERA_ERROR_PARAM);
    p.function_type = 1;
    CHECK(sceCameraGetCalibrationData(&p, NULL) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraGetCalibrationData(&p, out) == ORBIS_CAMERA_ERROR_NOT_CONNECTED);
    CHECK(sceCameraGetCalibDataFromDevice() == ORBIS_CAMERA_ERROR_NOT_CONNECTED);
    CHECK(sceCameraAudioOpen() == ORBIS_CAMERA_ERROR_FATAL);
    CHECK(sceCameraAudioGetData(NULL) == ORBIS_CAMERA_ERROR_PARAM && sceCameraAudioGetData(out) == 0);
    printf("PASS calibration\n");
    return 0;
}

// PS4_CAMERA=0: the fake device delivers frames.
static int test_frames(void) {
    CHECK(sceCameraIsAttached(0) == 1);
    CHECK(open_camera() == 1);
    OrbisCameraStartParameter sp = start_param(0, 0);
    CHECK(sceCameraStart(SDK_NEW, 1, &sp) == ORBIS_CAMERA_ERROR_FORMAT_UNKNOWN);
    sp = start_param(0x10, 1);
    CHECK(sceCameraStart(SDK_NEW, 1, &sp) == ORBIS_CAMERA_ERROR_FORMAT_UNKNOWN);
    sp.sizeThis = 16;
    CHECK(sceCameraStart(SDK_NEW, 1, &sp) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraStartByHandle(1, &sp) == ORBIS_CAMERA_ERROR_PARAM);
    sp = start_param(0, 0);
    CHECK(g_open_calls == 0);

    // Before Start no device is open.
    OrbisCameraFrameData fd;
    memset(&fd, 0, sizeof(fd));
    fd.sizeThis = sizeof(fd);
    CHECK(sceCameraGetFrameData(1, &fd) == ORBIS_CAMERA_ERROR_NOT_OPEN);

    g_frames_pending = 1; // consumed by the warm-up
    CHECK(sceCameraStart(SDK_OLD, 1, &sp) == 0); // formats are checked from SDK 2.50
    CHECK(g_open_calls == 1 && g_opened_index == 0 && g_frames_pending == 0);

    // Config type 1: device 0 YUV422, device 1 RAW16.
    OrbisCameraConfig c = config_of(ORBIS_CAMERA_CONFIG_TYPE1);
    CHECK(sceCameraSetConfig(SDK_NEW, 1, &c) == 0);
    g_frames_pending = 1;
    fd.sizeThis = 520; // a game built before the Garlic list
    CHECK(sceCameraGetFrameData(1, &fd) == 0);
    CHECK(fd.status[0] == 1 && fd.status[1] == 1);
    CHECK(sceCameraIsValidFrameData(1, &fd) == 1);
    CHECK(fd.pFramePointerListGarlic[0][0] == 0);
    CHECK(fd.meta.format[0][0] == ORBIS_CAMERA_FORMAT_YUV422 && fd.meta.format[1][0] == ORBIS_CAMERA_FORMAT_RAW16);
    CHECK(fd.frameSize[0][0] == PIXELS * 2 && fd.frameSize[1][0] == PIXELS * 2);
    const uint8_t *yuv = g_mem + fd.pFramePointerList[0][0];
    CHECK(yuv[0] == 100 && yuv[1] == 90 && yuv[2] == 200 && yuv[3] == 160 && yuv[PIXELS * 2 - 1] == 160);
    const uint16_t *raw16 = (const uint16_t *)(g_mem + fd.pFramePointerList[1][0]);
    uint8_t r0, g0, b0, r1, g1, b1;
    rgb(100, 90, 160, &r0, &g0, &b0);
    rgb(200, 90, 160, &r1, &g1, &b1);
    CHECK(raw16[0] == b0 << 4 && raw16[1] == g1 << 4);
    CHECK(raw16[CAMERA_FRAME_WIDTH] == g0 << 4 && raw16[CAMERA_FRAME_WIDTH + 1] == r1 << 4);
    const uint64_t yuv_buf = fd.pFramePointerList[0][0], raw16_buf = fd.pFramePointerList[1][0];
    CHECK(yuv_buf != raw16_buf);

    // No new frame: status -1, same buffers, OK.
    fd.sizeThis = sizeof(fd);
    CHECK(sceCameraGetFrameData(1, &fd) == 0);
    CHECK(fd.status[0] == 0xFFFFFFFFu && fd.status[1] == 0xFFFFFFFFu);
    CHECK(sceCameraIsValidFrameData(1, &fd) == 0);
    CHECK(fd.pFramePointerList[0][0] == yuv_buf && fd.pFramePointerList[1][0] == raw16_buf);
    CHECK(fd.pFramePointerListGarlic[0][0] == 0); // written with a new frame only

    // An extension config with RAW8 on device 0; the 584-byte structure gets the Garlic list.
    c = config_of(ORBIS_CAMERA_CONFIG_EXTENTION);
    c.configExtention[0].format.formatLevel0 = ORBIS_CAMERA_FORMAT_RAW8;
    c.configExtention[1].format.formatLevel0 = ORBIS_CAMERA_FORMAT_YUV422;
    CHECK(sceCameraSetConfig(SDK_NEW, 1, &c) == 0);
    g_frames_pending = 1;
    CHECK(sceCameraGetFrameData(1, &fd) == 0);
    const uint8_t *raw8 = g_mem + fd.pFramePointerList[0][0];
    CHECK(raw8[0] == b0 && raw8[1] == g1 && raw8[CAMERA_FRAME_WIDTH] == g0 && raw8[CAMERA_FRAME_WIDTH + 1] == r1);
    CHECK(fd.frameSize[0][0] == PIXELS && fd.meta.format[0][0] == ORBIS_CAMERA_FORMAT_RAW8);
    CHECK(fd.pFramePointerListGarlic[0][0] == fd.pFramePointerList[0][0]);
    CHECK(fd.pFramePointerListGarlic[1][0] == fd.pFramePointerList[1][0]);

    fd.sizeThis = sizeof(fd) + 1;
    CHECK(sceCameraGetFrameData(1, &fd) == ORBIS_CAMERA_ERROR_PARAM);
    CHECK(sceCameraIsValidFrameData(1, &fd) == ORBIS_CAMERA_ERROR_PARAM);

    // Config type 5 needs SDK 4.50; unknown types are rejected.
    c = config_of(ORBIS_CAMERA_CONFIG_TYPE5);
    CHECK(sceCameraSetConfig(SDK_OLD, 1, &c) == ORBIS_CAMERA_ERROR_UNKNOWN_CONFIG);
    CHECK(sceCameraSetConfig(SDK_NEW, 1, &c) == 0);
    c = config_of(6);
    CHECK(sceCameraSetConfig(SDK_NEW, 1, &c) == ORBIS_CAMERA_ERROR_PARAM);

    // Hardware settings: exposure needs a connected camera only.
    CHECK(sceCameraSetExposureGain(1, ORBIS_CAMERA_CHANNEL_0, NULL, NULL) == 0);
    OrbisCameraGetCalibrationDataParameter cp = {sizeof(cp), 0, 0};
    uint8_t out[16];
    CHECK(sceCameraGetCalibrationData(&cp, out) == 0);

    // A second Start reopens the device; Close stops it.
    sp = start_param(1, 1);
    CHECK(sceCameraStart(SDK_NEW, 1, &sp) == 0);
    CHECK(g_open_calls == 2 && g_close_calls == 1);
    CHECK(sceCameraClose(1) == 0);
    CHECK(g_close_calls == 2);
    fd.sizeThis = sizeof(fd);
    CHECK(sceCameraGetFrameData(1, &fd) == ORBIS_CAMERA_ERROR_NOT_OPEN);
    printf("PASS frames\n");
    return 0;
}

// PS4_CAMERA=0 without a device, and PS4_CAMERA naming a device that does not exist.
static int test_no_device(void) {
    CHECK(open_camera() == 1);
    OrbisCameraStartParameter sp = start_param(1, 1);
    g_device_count = 0;
    CHECK(sceCameraStart(SDK_NEW, 1, &sp) == ORBIS_CAMERA_ERROR_NOT_CONNECTED);
    CHECK(g_open_calls == 0);
    printf("PASS no-device\n");
    return 0;
}

static int test_missing_device(void) {
    CHECK(open_camera() == 1);
    OrbisCameraStartParameter sp = start_param(1, 1);
    CHECK(sceCameraStart(SDK_NEW, 1, &sp) == ORBIS_CAMERA_ERROR_FATAL);
    CHECK(g_open_calls == 0);
    printf("PASS missing-device\n");
    return 0;
}

// A base format shadPS4 does not handle stops the process (UNREACHABLE).
static int test_bad_format(void) {
    CHECK(open_camera() == 1);
    OrbisCameraStartParameter sp = start_param(1, 1);
    CHECK(sceCameraStart(SDK_NEW, 1, &sp) == 0);
    OrbisCameraConfig c = config_of(ORBIS_CAMERA_CONFIG_EXTENTION);
    c.configExtention[0].format.formatLevel0 = ORBIS_CAMERA_FORMAT_NO_USE;
    CHECK(sceCameraSetConfig(SDK_NEW, 1, &c) == 0);
    OrbisCameraFrameData fd;
    memset(&fd, 0, sizeof(fd));
    fd.sizeThis = sizeof(fd);
    sceCameraGetFrameData(1, &fd);
    printf("sceCameraGetFrameData returned\n");
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    g_mem = calloc(1, GUEST_MEM_SIZE);
    if (!g_mem) return 2;
    g_ctx.mem_base = g_mem;
    const char *c = argv[1];
    if (strcmp(c, "open-close") == 0) return test_open_close();
    if (strcmp(c, "not-open") == 0) return test_not_open();
    if (strcmp(c, "attributes") == 0) return test_attributes();
    if (strcmp(c, "config") == 0) return test_config();
    if (strcmp(c, "calibration") == 0) return test_calibration();
    setenv("PS4_CAMERA", "0", 1);
    if (strcmp(c, "frames") == 0) return test_frames();
    if (strcmp(c, "no-device") == 0) return test_no_device();
    if (strcmp(c, "bad-format") == 0) return test_bad_format();
    setenv("PS4_CAMERA", "3", 1);
    if (strcmp(c, "missing-device") == 0) return test_missing_device();
    return 2;
}
