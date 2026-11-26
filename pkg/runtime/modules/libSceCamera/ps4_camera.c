// SPDX-License-Identifier: GPL-2.0-or-later

// libSceCamera, ported from shadPS4 (src/core/libraries/camera/): the handle and open state,
// the parameter checks and error codes of every call, the default attributes and configs, the
// dumped calibration blobs, the YUY2 -> RAW8 / RAW16 conversion and the frame buffers in a
// named "SceCameraGpuGarlicPool" mapping. shadPS4 captures through SDL's camera API; here the
// frames come from AVFoundation (ps4_camera_capture.m). As in shadPS4, the camera is off unless
// a device is configured (PS4_CAMERA, shadPS4's camera_id); the calls that would change
// camera hardware settings report NOT_CONNECTED, as they do in shadPS4.

#include "ps4_camera.h"
#include "ps4_hle_stub.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#define ORBIS_USER_SERVICE_USER_ID_SYSTEM 0xFF
#define CAMERA_PIXELS ((size_t)CAMERA_FRAME_WIDTH * CAMERA_FRAME_HEIGHT)
#define CAMERA_POOL_SIZE 0xfd0000
#define FRAME_DATA_MAX_SIZE ((uint32_t)sizeof(OrbisCameraFrameData))

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static bool g_library_opened;
static int32_t g_handles;
static bool g_capture_open;
static OrbisCameraConfigExtention g_output_config[ORBIS_CAMERA_MAX_DEVICE_NUM];

// The frame buffers in guest memory (shadPS4 raw8_buffer1/2, raw16_buffer1/2): guest addresses
// and the host pointers behind them.
static bool g_buffers_initialized;
static uint64_t g_raw8_guest[ORBIS_CAMERA_MAX_DEVICE_NUM];
static uint64_t g_raw16_guest[ORBIS_CAMERA_MAX_DEVICE_NUM];
static uint8_t *g_raw8[ORBIS_CAMERA_MAX_DEVICE_NUM];
static uint16_t *g_raw16[ORBIS_CAMERA_MAX_DEVICE_NUM];
// The latest captured YUY2 frame, before conversion (allocated once, at the first Start).
static uint8_t *g_yuy2;

// shadPS4 camera_config_types: the two output configs of config types 1 to 5.
static const OrbisCameraConfigExtention k_config_types[5][ORBIS_CAMERA_MAX_DEVICE_NUM] = {
    {{.format = {ORBIS_CAMERA_FORMAT_YUV422, ORBIS_CAMERA_SCALE_FORMAT_Y8, ORBIS_CAMERA_SCALE_FORMAT_Y8,
                 ORBIS_CAMERA_SCALE_FORMAT_Y8},
      .framerate = ORBIS_CAMERA_FRAMERATE_60},
     {.format = {ORBIS_CAMERA_FORMAT_RAW16, ORBIS_CAMERA_SCALE_FORMAT_Y8, ORBIS_CAMERA_SCALE_FORMAT_Y8,
                 ORBIS_CAMERA_SCALE_FORMAT_Y8},
      .framerate = ORBIS_CAMERA_FRAMERATE_60}},
    {{.format = {ORBIS_CAMERA_FORMAT_YUV422, ORBIS_CAMERA_SCALE_FORMAT_YUV422, ORBIS_CAMERA_SCALE_FORMAT_YUV422,
                 ORBIS_CAMERA_SCALE_FORMAT_YUV422},
      .framerate = ORBIS_CAMERA_FRAMERATE_60},
     {.format = {ORBIS_CAMERA_FORMAT_YUV422, ORBIS_CAMERA_SCALE_FORMAT_YUV422, ORBIS_CAMERA_SCALE_FORMAT_YUV422,
                 ORBIS_CAMERA_SCALE_FORMAT_YUV422},
      .framerate = ORBIS_CAMERA_FRAMERATE_60}},
    {{.format = {ORBIS_CAMERA_FORMAT_YUV422, ORBIS_CAMERA_SCALE_FORMAT_Y8, ORBIS_CAMERA_SCALE_FORMAT_Y8,
                 ORBIS_CAMERA_SCALE_FORMAT_Y8},
      .framerate = ORBIS_CAMERA_FRAMERATE_60},
     {.format = {ORBIS_CAMERA_FORMAT_YUV422, ORBIS_CAMERA_SCALE_FORMAT_Y8, ORBIS_CAMERA_SCALE_FORMAT_Y8,
                 ORBIS_CAMERA_SCALE_FORMAT_Y8},
      .framerate = ORBIS_CAMERA_FRAMERATE_60}},
    {{.format = {ORBIS_CAMERA_FORMAT_RAW16, ORBIS_CAMERA_SCALE_FORMAT_YUV422, ORBIS_CAMERA_SCALE_FORMAT_YUV422,
                 ORBIS_CAMERA_SCALE_FORMAT_YUV422},
      .framerate = ORBIS_CAMERA_FRAMERATE_60},
     {.format = {ORBIS_CAMERA_FORMAT_RAW16, ORBIS_CAMERA_SCALE_FORMAT_YUV422, ORBIS_CAMERA_SCALE_FORMAT_YUV422,
                 ORBIS_CAMERA_SCALE_FORMAT_YUV422},
      .framerate = ORBIS_CAMERA_FRAMERATE_60}},
    {{.format = {ORBIS_CAMERA_FORMAT_YUV422, ORBIS_CAMERA_SCALE_FORMAT_YUV422, ORBIS_CAMERA_SCALE_FORMAT_YUV422,
                 ORBIS_CAMERA_SCALE_FORMAT_YUV422},
      .framerate = ORBIS_CAMERA_FRAMERATE_60},
     {.format = {ORBIS_CAMERA_FORMAT_RAW16, ORBIS_CAMERA_SCALE_FORMAT_YUV422, ORBIS_CAMERA_SCALE_FORMAT_YUV422,
                 ORBIS_CAMERA_SCALE_FORMAT_YUV422},
      .framerate = ORBIS_CAMERA_FRAMERATE_60}},
};

// shadPS4 sceCameraGetCalibData: calibration data dumped from a console camera, for ids 1 and 2.
static const uint64_t k_calib_data1[32] = {
    0x000000000b0b0200, 0x00000000bc128639, 0x000000003de79766, 0x00000000be6e2869, 0x3ca0ce1f3e30521c,
    0x4452d55e3d0160b6, 0x3cf7fdaa4453abf1, 0x000000003ce3f40f, 0x000000003cdbcaa6, 0x000000003d635f4e,
    0x00000000be299439, 0xbb98ddc23e1159b4, 0x44523b6c3c87d83c, 0x3c8d1c46445308a2, 0xbbb83fe03c9bcc40,
    0xbab0ed65b8fe93b0, 0xba677004bb598091, 0x3c0eb61e3b49d173, 0xba1bac9f3c67af0b, 0x00000000b9d0e64d,
    0x0000000000000000, 0x0000000000000000, 0xb97653e400000000, 0x00000000ba95460e, 0x0000000000000000,
    0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
    0xbab5330000000000, 0x000001013b07a47f,
};
static const uint64_t k_calib_data2[32] = {
    0x002d000000ff0009, 0x002d0ff70ffd0f14, 0x002d0efe00010008, 0x002f0ffe0f020008,
    0x002f000800060105, 0x002e00fc0001000a,
};
_Static_assert(sizeof(k_calib_data1) == CAMERA_CALIB_DATA_SIZE, "calibration blob size");

static int32_t g_configured_id;
static pthread_once_t g_configured_once = PTHREAD_ONCE_INIT;

static void read_configured_id(void) {
    g_configured_id = -1;
    const char *e = getenv("PS4_CAMERA");
    if (!e || !*e) return;
    char *end;
    const long id = strtol(e, &end, 10);
    if (*end || id < -1 || id > 255) {
        fprintf(stderr, "[ps4-camera] PS4_CAMERA=%s is not a device index; no camera\n", e);
        return;
    }
    g_configured_id = (int32_t)id;
}

int32_t ps4_camera_configured_id(void) {
    pthread_once(&g_configured_once, read_configured_id);
    return g_configured_id;
}

static bool camera_connected(void) {
    return ps4_camera_configured_id() != -1;
}

// shadPS4 aborts (UNREACHABLE / ASSERT) where the library meets a state it does not handle.
static void fatal(const char *what) {
    fprintf(stderr, "[ps4-camera] %s\n", what);
    abort();
}

static bool getter_channel_valid(int32_t channel) {
    return channel >= ORBIS_CAMERA_CHANNEL_0 && channel < ORBIS_CAMERA_CHANNEL_BOTH;
}

static bool setter_channel_valid(int32_t channel) {
    return channel >= ORBIS_CAMERA_CHANNEL_0 && channel <= ORBIS_CAMERA_CHANNEL_BOTH;
}

static bool library_opened(void) {
    pthread_mutex_lock(&g_mu);
    const bool opened = g_library_opened;
    pthread_mutex_unlock(&g_mu);
    return opened;
}

// ---------------------------------------------------------------------------------------------
// Pixel conversion (shadPS4 camera_helpers.cpp)

static int32_t size_of_base_format(uint32_t f) {
    switch (f) {
    case ORBIS_CAMERA_FORMAT_YUV422:
    case ORBIS_CAMERA_FORMAT_RAW16:
        return 2;
    case ORBIS_CAMERA_FORMAT_RAW8:
        return 1;
    default:
        return 0;
    }
}

static inline uint8_t clamp_u8(int v) {
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static inline void yuv_to_rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    const int c = (int)y - 16;
    const int d = (int)u - 128;
    const int e = (int)v - 128;
    *r = clamp_u8((298 * c + 409 * e + 128) >> 8);
    *g = clamp_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
    *b = clamp_u8((298 * c + 516 * d + 128) >> 8);
}

// A Bayer mosaic (B G / G R) from YUY2: even rows take B of the first and G of the second pixel
// of each pair, odd rows G and R. RAW16 holds the 8-bit values shifted to 12 bits.
static void convert_yuy2_to_raw(const uint8_t *src, uint8_t *dst8, uint16_t *dst16) {
    for (int y = 0; y < CAMERA_FRAME_HEIGHT; ++y) {
        const uint8_t *row = src + (size_t)y * CAMERA_FRAME_WIDTH * 2;
        const size_t out = (size_t)y * CAMERA_FRAME_WIDTH;
        const bool even_row = (y & 1) == 0;
        for (int x = 0; x < CAMERA_FRAME_WIDTH; x += 2) {
            uint8_t r0, g0, b0, r1, g1, b1;
            yuv_to_rgb(row[x * 2 + 0], row[x * 2 + 1], row[x * 2 + 3], &r0, &g0, &b0);
            yuv_to_rgb(row[x * 2 + 2], row[x * 2 + 1], row[x * 2 + 3], &r1, &g1, &b1);
            const uint8_t first = even_row ? b0 : g0;
            const uint8_t second = even_row ? g1 : r1;
            if (dst8) {
                dst8[out + x] = first;
                dst8[out + x + 1] = second;
            } else {
                dst16[out + x] = (uint16_t)(first << 4);
                dst16[out + x + 1] = (uint16_t)(second << 4);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Open, close, start, stop (shadPS4 camera.cpp)

// Maps the frame buffer pool once (shadPS4 maps 0xfd0000 bytes of system flexible memory named
// SceCameraGpuGarlicPool and carves the four buffers from it).
static void init_buffers_locked(GuestContext *ctx) {
    if (g_buffers_initialized) return;
    const uint64_t pool = recomp_vm_alloc_named(ctx, CAMERA_POOL_SIZE, PROT_READ | PROT_WRITE, 0, "SceCameraGpuGarlicPool");
    if (pool == 0 || pool == (uint64_t)-1) fatal("cannot map SceCameraGpuGarlicPool");
    uint64_t at = pool;
    for (int i = 0; i < ORBIS_CAMERA_MAX_DEVICE_NUM; i++) {
        g_raw8_guest[i] = at;
        at += CAMERA_PIXELS;
    }
    for (int i = 0; i < ORBIS_CAMERA_MAX_DEVICE_NUM; i++) {
        g_raw16_guest[i] = at;
        at += CAMERA_PIXELS * sizeof(uint16_t);
    }
    _Static_assert(CAMERA_PIXELS * 6 <= CAMERA_POOL_SIZE, "camera buffers exceed the pool");
    for (int i = 0; i < ORBIS_CAMERA_MAX_DEVICE_NUM; i++) {
        g_raw8[i] = ctx->mem_base + g_raw8_guest[i];
        g_raw16[i] = (uint16_t *)(ctx->mem_base + g_raw16_guest[i]);
    }
    g_buffers_initialized = true;
}

int32_t sceCameraOpen(GuestContext *ctx, int32_t user_id, int32_t type, int32_t index, const void *param) {
    (void)param;
    if (user_id != ORBIS_USER_SERVICE_USER_ID_SYSTEM || type != 0 || index != 0) return ORBIS_CAMERA_ERROR_PARAM;
    pthread_mutex_lock(&g_mu);
    init_buffers_locked(ctx);
    g_library_opened = true;
    const int32_t handle = ++g_handles;
    pthread_mutex_unlock(&g_mu);
    return handle;
}

static void close_capture_locked(void) {
    if (!g_capture_open) return;
    ps4_camera_capture_close();
    g_capture_open = false;
}

// Drops a handle; the library closes with the last one.
static int32_t release_handle(int32_t handle, bool close_capture) {
    if (handle < 1) return ORBIS_CAMERA_ERROR_PARAM;
    pthread_mutex_lock(&g_mu);
    if (!g_library_opened) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_CAMERA_ERROR_NOT_OPEN;
    }
    if (--g_handles == 0) g_library_opened = false;
    if (close_capture) close_capture_locked();
    pthread_mutex_unlock(&g_mu);
    return 0;
}

int32_t sceCameraClose(int32_t handle) {
    return release_handle(handle, true);
}

int32_t sceCameraCloseByHandle(int32_t handle) {
    return release_handle(handle, false);
}

int32_t sceCameraIsAttached(int32_t index) {
    if (index != 0) return ORBIS_CAMERA_ERROR_PARAM;
    return camera_connected() ? 1 : 0; // 0 = disconnected, 1 = connected
}

static bool start_param_valid(int32_t handle, const OrbisCameraStartParameter *param) {
    return handle >= 1 && param && param->sizeThis == sizeof(OrbisCameraStartParameter);
}

int32_t sceCameraStart(uint32_t sdk, int32_t handle, const OrbisCameraStartParameter *param) {
    if (!start_param_valid(handle, param)) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    if (sdk >= CAMERA_FW_250 && (param->formatLevel[0] > 0xf || param->formatLevel[1] > 0xf ||
                                 (param->formatLevel[0] | param->formatLevel[1]) == 0)) {
        return ORBIS_CAMERA_ERROR_FORMAT_UNKNOWN;
    }
    if (param->formatLevel[0] > 1 || param->formatLevel[1] > 1) {
        fprintf(stderr, "[ps4-camera] downscaled image retrieval is not supported (format levels %u, %u)\n",
                param->formatLevel[0], param->formatLevel[1]);
    }
    const int32_t count = ps4_camera_capture_device_count();
    if (count == 0) {
        fprintf(stderr, "[ps4-camera] no camera devices connected\n");
        return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
    }
    const int32_t id = ps4_camera_configured_id();
    pthread_mutex_lock(&g_mu);
    if (!g_yuy2) g_yuy2 = malloc(CAMERA_PIXELS * 2);
    close_capture_locked(); // a second Start reopens the device
    // shadPS4 opens devices[camera_id] without a range check; a device that cannot be opened is
    // FATAL there, and so is an index that names none here.
    g_capture_open = g_yuy2 && id >= 0 && id < count && ps4_camera_capture_open(id);
    const bool opened = g_capture_open;
    pthread_mutex_unlock(&g_mu);
    if (!opened) {
        fprintf(stderr, "[ps4-camera] failed to open camera %d of %d\n", id, count);
        return ORBIS_CAMERA_ERROR_FATAL;
    }
    // "Warm up" the device as shadPS4 does: wait briefly for its first frame.
    const struct timespec ts = {0, 10};
    for (int i = 0; i < 1000; i++) {
        pthread_mutex_lock(&g_mu);
        const bool got = ps4_camera_capture_acquire(g_yuy2);
        pthread_mutex_unlock(&g_mu);
        if (got) break;
        nanosleep(&ts, NULL);
    }
    return 0;
}

int32_t sceCameraStartByHandle(int32_t handle, const OrbisCameraStartParameter *param) {
    if (!start_param_valid(handle, param)) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    return 0;
}

int32_t sceCameraStop(int32_t handle) {
    if (handle < 1) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Frames

// The output buffer of one device for its base format (shadPS4 GetFrameData); NULL-free since
// the buffers exist from the first Open.
static uint64_t output_buffer_locked(int dev, bool convert) {
    switch (g_output_config[dev].format.formatLevel0) {
    case ORBIS_CAMERA_FORMAT_YUV422:
        if (convert) memcpy(g_raw16[dev], g_yuy2, CAMERA_PIXELS * sizeof(uint16_t));
        return g_raw16_guest[dev];
    case ORBIS_CAMERA_FORMAT_RAW16:
        if (convert) convert_yuy2_to_raw(g_yuy2, NULL, g_raw16[dev]);
        return g_raw16_guest[dev];
    case ORBIS_CAMERA_FORMAT_RAW8:
        if (convert) convert_yuy2_to_raw(g_yuy2, g_raw8[dev], NULL);
        return g_raw8_guest[dev];
    default:
        fatal("sceCameraGetFrameData: unhandled base format");
        return 0;
    }
}

int32_t sceCameraGetFrameData(int32_t handle, OrbisCameraFrameData *fd) {
    if (!fd) return ORBIS_CAMERA_ERROR_PARAM;
    fd->status[0] = (uint32_t)-1;
    fd->status[1] = (uint32_t)-1;
    if (handle < 1 || fd->sizeThis > FRAME_DATA_MAX_SIZE) return ORBIS_CAMERA_ERROR_PARAM;
    pthread_mutex_lock(&g_mu);
    if (!g_library_opened || !g_capture_open) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_CAMERA_ERROR_NOT_OPEN;
    }
    if (!camera_connected()) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
    }
    const bool frame = ps4_camera_capture_acquire(g_yuy2);
    fd->status[0] = frame ? 1 : (uint32_t)-1;
    fd->status[1] = frame ? 1 : (uint32_t)-1;
    for (int dev = 0; dev < ORBIS_CAMERA_MAX_DEVICE_NUM; dev++) {
        fd->pFramePointerList[dev][0] = output_buffer_locked(dev, frame);
    }
    if (!frame) {
        // No new frame: the previous one stays in the buffers.
        const int32_t rc = fd->pFramePointerList[0][0] == 0 ? ORBIS_CAMERA_ERROR_BUSY : 0;
        pthread_mutex_unlock(&g_mu);
        return rc;
    }
    for (int dev = 0; dev < ORBIS_CAMERA_MAX_DEVICE_NUM; dev++) {
        const uint32_t format = g_output_config[dev].format.formatLevel0;
        fd->meta.format[dev][0] = format;
        fd->frameSize[dev][0] = (uint32_t)(CAMERA_PIXELS * (size_t)size_of_base_format(format));
    }
    // Games built before the structure grew to 584 bytes pass the shorter one: the original
    // library checks the size, not the SDK version, before writing the Garlic list.
    if (fd->sizeThis == FRAME_DATA_MAX_SIZE) {
        fd->pFramePointerListGarlic[0][0] = fd->pFramePointerList[0][0];
        fd->pFramePointerListGarlic[1][0] = fd->pFramePointerList[1][0];
    }
    pthread_mutex_unlock(&g_mu);
    return 0;
}

int32_t sceCameraIsValidFrameData(int32_t handle, const OrbisCameraFrameData *fd) {
    if (handle < 1 || !fd || fd->sizeThis > FRAME_DATA_MAX_SIZE) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    return fd->status[0] == 1 && fd->status[1] == 1;
}

// ---------------------------------------------------------------------------------------------
// Configs (shadPS4 camera_config.cpp, camera_device.cpp)

int32_t sceCameraGetConfig(int32_t handle, OrbisCameraConfig *config) {
    if (handle < 1 || !config || config->sizeThis != sizeof(OrbisCameraConfig)) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    config->configType = ORBIS_CAMERA_CONFIG_TYPE1;
    const OrbisCameraConfigExtention def = {
        .format = {ORBIS_CAMERA_FORMAT_YUV422, ORBIS_CAMERA_SCALE_FORMAT_Y8, ORBIS_CAMERA_SCALE_FORMAT_Y8,
                   ORBIS_CAMERA_SCALE_FORMAT_Y8},
        .resolution = ORBIS_CAMERA_RESOLUTION_1280X800,
        .framerate = ORBIS_CAMERA_FRAMERATE_60,
    };
    config->configExtention[0] = def;
    config->configExtention[1] = def;
    return 0;
}

int32_t sceCameraSetConfig(uint32_t sdk, int32_t handle, const OrbisCameraConfig *config) {
    if (handle < 1 || !config || config->sizeThis != sizeof(OrbisCameraConfig)) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    if (!camera_connected()) return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
    const OrbisCameraConfigExtention *src;
    switch (config->configType) {
    case ORBIS_CAMERA_CONFIG_TYPE5:
        if (sdk < CAMERA_FW_450) return ORBIS_CAMERA_ERROR_UNKNOWN_CONFIG;
        // fallthrough
    case ORBIS_CAMERA_CONFIG_TYPE1:
    case ORBIS_CAMERA_CONFIG_TYPE2:
    case ORBIS_CAMERA_CONFIG_TYPE3:
    case ORBIS_CAMERA_CONFIG_TYPE4:
        src = k_config_types[config->configType - 1];
        break;
    case ORBIS_CAMERA_CONFIG_EXTENTION:
        src = config->configExtention;
        break;
    default:
        fprintf(stderr, "[ps4-camera] sceCameraSetConfig: invalid config type %u\n", config->configType);
        return ORBIS_CAMERA_ERROR_PARAM;
    }
    pthread_mutex_lock(&g_mu);
    g_output_config[0] = src[0];
    g_output_config[1] = src[1];
    pthread_mutex_unlock(&g_mu);
    return 0;
}

int32_t sceCameraSetConfigInternal(int32_t handle, const OrbisCameraConfig *config) {
    if (handle < 1 || !config || config->sizeThis != sizeof(OrbisCameraConfig)) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
}

int32_t sceCameraGetDeviceConfig(int32_t handle, OrbisCameraConfig *config) {
    if (handle < 1 || !config || config->sizeThis != sizeof(OrbisCameraConfig)) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    memset(config, 0, sizeof(*config));
    config->sizeThis = sizeof(OrbisCameraConfig);
    return 0;
}

int32_t sceCameraGetDeviceConfigWithoutHandle(OrbisCameraConfig *config) {
    if (!config || config->sizeThis != sizeof(OrbisCameraConfig)) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    memset(config, 0, sizeof(*config)); // sizeThis included, as in shadPS4
    return 0;
}

int32_t sceCameraGetDeviceInfo(int32_t reserved, const OrbisCameraDeviceInfo *info) {
    if (reserved != 0 || !info || info->sizeThis != sizeof(OrbisCameraDeviceInfo) || info->infoRevision != 1) {
        return ORBIS_CAMERA_ERROR_PARAM;
    }
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_INIT;
    return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
}

// ---------------------------------------------------------------------------------------------
// Attributes: the getters report the console defaults, the setters change nothing.

int32_t sceCameraGetAttribute(int32_t handle, OrbisCameraAttribute *a) {
    if (handle < 1 || !a || a->sizeThis != sizeof(OrbisCameraAttribute) || !getter_channel_valid(a->channel)) {
        return ORBIS_CAMERA_ERROR_PARAM;
    }
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    const int32_t channel = a->channel;
    memset(a, 0, sizeof(*a));
    a->sizeThis = sizeof(OrbisCameraAttribute);
    a->channel = channel;
    a->exposureGain.exposure = 83;
    a->exposureGain.gain = 100;
    a->whiteBalance.gainRed = 768;
    a->whiteBalance.gainBlue = 768;
    a->whiteBalance.gainGreen = 512;
    a->gamma.value = 4;
    a->saturation = 64;
    a->contrast = 32;
    a->sharpness = 1;
    a->hue = 1;
    return 0;
}

int32_t sceCameraSetAttribute(int32_t handle, const OrbisCameraAttribute *a) {
    if (handle < 1 || !a || a->sizeThis != sizeof(OrbisCameraAttribute) || !setter_channel_valid(a->channel)) {
        return ORBIS_CAMERA_ERROR_PARAM;
    }
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
}

int32_t sceCameraGetAutoExposureGain(uint32_t sdk, int32_t handle, int32_t channel, uint32_t *enable,
                                     OrbisCameraAutoExposureGainTarget *option) {
    if (handle < 1 || !getter_channel_valid(channel) || !enable) return ORBIS_CAMERA_ERROR_PARAM;
    if (option && (sdk < CAMERA_FW_300 || option->sizeThis != sizeof(OrbisCameraAutoExposureGainTarget))) {
        return ORBIS_CAMERA_ERROR_PARAM;
    }
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    *enable = 0;
    if (option) {
        option->sizeThis = 0;
        option->target = ORBIS_CAMERA_ATTRIBUTE_AECAGC_TARGET_DEF;
    }
    return 0;
}

int32_t sceCameraSetAutoExposureGain(uint32_t sdk, int32_t handle, int32_t channel, uint32_t enable,
                                     const OrbisCameraAutoExposureGainTarget *option) {
    (void)enable;
    if (handle < 1 || !setter_channel_valid(channel)) return ORBIS_CAMERA_ERROR_PARAM;
    if (option) {
        if (sdk < CAMERA_FW_300 || option->sizeThis != sizeof(OrbisCameraAutoExposureGainTarget)) {
            return ORBIS_CAMERA_ERROR_PARAM;
        }
        if (option->target % 2 == 1 || option->target > ORBIS_CAMERA_ATTRIBUTE_AECAGC_TARGET_2_0) {
            return ORBIS_CAMERA_ERROR_PARAM;
        }
    }
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
}

// A getter of one u32 / s32 value: PARAM checks, NOT_OPEN, then the default.
static int32_t get_value(int32_t handle, int32_t channel, void *out, const void *option, uint32_t value) {
    if (handle < 1 || !getter_channel_valid(channel) || !out || option) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    *(uint32_t *)out = value;
    return 0;
}

// A setter the camera hardware would apply: PARAM checks, NOT_OPEN, then NOT_CONNECTED.
static int32_t set_value(int32_t handle, int32_t channel, bool value_valid, const void *option) {
    if (handle < 1 || !setter_channel_valid(channel) || !value_valid || option) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
}

int32_t sceCameraGetAutoWhiteBalance(int32_t handle, int32_t channel, uint32_t *enable, const void *option) {
    return get_value(handle, channel, enable, option, 0);
}

int32_t sceCameraSetAutoWhiteBalance(int32_t handle, int32_t channel, uint32_t enable, const void *option) {
    (void)enable;
    return set_value(handle, channel, true, option);
}

int32_t sceCameraGetContrast(int32_t handle, int32_t channel, uint32_t *contrast, const void *option) {
    return get_value(handle, channel, contrast, option, 32);
}

int32_t sceCameraSetContrast(int32_t handle, int32_t channel, uint32_t contrast, const void *option) {
    (void)contrast;
    return set_value(handle, channel, true, option);
}

int32_t sceCameraGetDefectivePixelCancellation(int32_t handle, int32_t channel, uint32_t *enable,
                                               const void *option) {
    return get_value(handle, channel, enable, option, 0);
}

int32_t sceCameraSetDefectivePixelCancellation(int32_t handle, int32_t channel, uint32_t enable,
                                               const void *option) {
    return set_value(handle, channel, enable <= 1, option);
}

int32_t sceCameraSetDefectivePixelCancellationInternal(int32_t handle, int32_t channel, uint32_t enable,
                                                       const void *option) {
    return set_value(handle, channel, enable <= 2, option);
}

int32_t sceCameraGetExposureGain(int32_t handle, int32_t channel, OrbisCameraExposureGain *eg, const void *option) {
    if (handle < 1 || !getter_channel_valid(channel) || !eg || option) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    eg->exposureControl = 0;
    eg->exposure = 20;
    eg->gain = 100;
    eg->mode = 0;
    return 0;
}

// shadPS4 rejects a non-NULL exposure_gain here (and a non-NULL gamma in SetGamma).
int32_t sceCameraSetExposureGain(int32_t handle, int32_t channel, const OrbisCameraExposureGain *eg,
                                 const void *option) {
    if (handle < 1 || !setter_channel_valid(channel) || eg || option) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    if (!camera_connected()) return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
    return 0;
}

int32_t sceCameraGetGamma(int32_t handle, int32_t channel, OrbisCameraGamma *gamma, const void *option) {
    if (handle < 1 || !getter_channel_valid(channel) || !gamma || option) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    memset(gamma, 0, sizeof(*gamma));
    gamma->value = 4;
    return 0;
}

int32_t sceCameraSetGamma(int32_t handle, int32_t channel, const OrbisCameraGamma *gamma, const void *option) {
    return set_value(handle, channel, gamma == NULL, option);
}

int32_t sceCameraGetHue(int32_t handle, int32_t channel, int32_t *hue, const void *option) {
    return get_value(handle, channel, hue, option, 1);
}

int32_t sceCameraSetHue(int32_t handle, int32_t channel, int32_t hue, const void *option) {
    (void)hue;
    return set_value(handle, channel, true, option);
}

int32_t sceCameraGetLensCorrection(int32_t handle, int32_t channel, uint32_t *enable, const void *option) {
    return get_value(handle, channel, enable, option, 0);
}

int32_t sceCameraSetLensCorrection(int32_t handle, int32_t channel, uint32_t enable, const void *option) {
    return set_value(handle, channel, enable <= 1, option);
}

int32_t sceCameraSetLensCorrectionInternal(int32_t handle, int32_t channel, uint32_t enable, const void *option) {
    return set_value(handle, channel, enable <= 2, option);
}

int32_t sceCameraGetSaturation(int32_t handle, int32_t channel, uint32_t *saturation, const void *option) {
    return get_value(handle, channel, saturation, option, 64);
}

int32_t sceCameraSetSaturation(int32_t handle, int32_t channel, uint32_t saturation, const void *option) {
    (void)saturation;
    return set_value(handle, channel, true, option);
}

int32_t sceCameraGetSharpness(int32_t handle, int32_t channel, uint32_t *sharpness, const void *option) {
    return get_value(handle, channel, sharpness, option, 1);
}

int32_t sceCameraSetSharpness(uint32_t sdk, int32_t handle, int32_t channel, uint32_t sharpness,
                              const void *option) {
    if (handle < 1 || !setter_channel_valid(channel) || option) return ORBIS_CAMERA_ERROR_PARAM;
    if (sdk >= CAMERA_FW_350 && sharpness > 10) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
}

int32_t sceCameraGetWhiteBalance(int32_t handle, int32_t channel, OrbisCameraWhiteBalance *wb, const void *option) {
    if (handle < 1 || !getter_channel_valid(channel) || !wb || option) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    wb->whiteBalanceControl = 0;
    wb->gainRed = 768;
    wb->gainBlue = 768;
    wb->gainGreen = 512;
    return 0;
}

int32_t sceCameraSetWhiteBalance(int32_t handle, int32_t channel, const OrbisCameraWhiteBalance *wb,
                                 const void *option) {
    return set_value(handle, channel, wb != NULL, option);
}

// ---------------------------------------------------------------------------------------------
// Device queries, video sync, calibration, audio

int32_t sceCameraGetMmapConnectedCount(uint32_t *count) {
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    if (!count) return ORBIS_CAMERA_ERROR_PARAM;
    *count = 0;
    return 0;
}

int32_t sceCameraGetProductInfo(void *product_info) {
    if (!product_info) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_INIT;
    return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
}

int32_t sceCameraGetRegistryInfo(void *registry_info) {
    if (!registry_info) return ORBIS_CAMERA_ERROR_PARAM;
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_INIT;
    return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
}

int32_t sceCameraSetDebugStop(uint32_t debug_stop_enable) {
    return debug_stop_enable > 1 ? ORBIS_CAMERA_ERROR_PARAM : 0;
}

int32_t sceCameraSetVideoSync(int32_t handle, const OrbisCameraVideoSyncParameter *vs) {
    if (handle < 1 || !vs || vs->sizeThis != sizeof(OrbisCameraVideoSyncParameter) || vs->videoSyncMode > 1 ||
        vs->pModeOption != 0) {
        return ORBIS_CAMERA_ERROR_PARAM;
    }
    if (!library_opened()) return ORBIS_CAMERA_ERROR_NOT_OPEN;
    return 0;
}

int32_t sceCameraGetCalibData(int32_t id, void *calib_data) {
    if (!calib_data) return ORBIS_CAMERA_ERROR_PARAM;
    if (id != 1 && id != 2) return 0;
    memcpy(calib_data, id == 1 ? k_calib_data1 : k_calib_data2, CAMERA_CALIB_DATA_SIZE);
    return 0;
}

int32_t sceCameraGetCalibDataFromDevice(void) {
    return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
}

int32_t sceCameraGetCalibrationData(const OrbisCameraGetCalibrationDataParameter *param, void *calibration_data) {
    if (!param || !calibration_data || param->size != sizeof(OrbisCameraGetCalibrationDataParameter) ||
        param->format_type != 0 ||
        param->function_type > ORBIS_CAMERA_CALIBRATION_DATA_FUNCTION_TYPE_IMAGE_INVERSE_RECTIFICATION) {
        return ORBIS_CAMERA_ERROR_PARAM;
    }
    if (!camera_connected()) return ORBIS_CAMERA_ERROR_NOT_CONNECTED;
    return 0;
}

int32_t sceCameraInitializeRegistryCalibData(void) {
    return library_opened() ? 0 : ORBIS_CAMERA_ERROR_NOT_INIT;
}

// The camera microphone: the original library reports success while failing internally, except
// Open, which fails without a camera attached (shadPS4 camera_audio.cpp).
int32_t sceCameraAudioOpen(void) {
    return ORBIS_CAMERA_ERROR_FATAL;
}

int32_t sceCameraAudioGetData(void *data) {
    return data ? 0 : ORBIS_CAMERA_ERROR_PARAM;
}

// ---------------------------------------------------------------------------------------------
// Guest ABI

static void *gaddr(GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

static void ret(GuestContext *ctx, int32_t rc) {
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

#define ARG_I32(r) ((int32_t)ctx->r)
#define ARG_U32(r) ((uint32_t)ctx->r)
#define ARG_PTR(r) gaddr(ctx, ctx->r)

uint32_t ps4_compiled_sdk_version(const GuestContext *ctx);
#define SDK ps4_compiled_sdk_version(ctx)

void shim_sceCameraOpen(GuestContext *ctx) {
    ret(ctx, sceCameraOpen(ctx, ARG_I32(rdi), ARG_I32(rsi), ARG_I32(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraClose(GuestContext *ctx) {
    ret(ctx, sceCameraClose(ARG_I32(rdi)));
}
void shim_sceCameraCloseByHandle(GuestContext *ctx) {
    ret(ctx, sceCameraCloseByHandle(ARG_I32(rdi)));
}
void shim_sceCameraIsAttached(GuestContext *ctx) {
    ret(ctx, sceCameraIsAttached(ARG_I32(rdi)));
}
void shim_sceCameraStart(GuestContext *ctx) {
    ret(ctx, sceCameraStart(SDK, ARG_I32(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraStartByHandle(GuestContext *ctx) {
    ret(ctx, sceCameraStartByHandle(ARG_I32(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraStop(GuestContext *ctx) {
    ret(ctx, sceCameraStop(ARG_I32(rdi)));
}
void shim_sceCameraStopByHandle(GuestContext *ctx) {
    ret(ctx, sceCameraStop(ARG_I32(rdi)));
}
void shim_sceCameraGetFrameData(GuestContext *ctx) {
    ret(ctx, sceCameraGetFrameData(ARG_I32(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraIsValidFrameData(GuestContext *ctx) {
    ret(ctx, sceCameraIsValidFrameData(ARG_I32(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraGetConfig(GuestContext *ctx) {
    ret(ctx, sceCameraGetConfig(ARG_I32(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraSetConfig(GuestContext *ctx) {
    ret(ctx, sceCameraSetConfig(SDK, ARG_I32(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraSetConfigInternal(GuestContext *ctx) {
    ret(ctx, sceCameraSetConfigInternal(ARG_I32(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraGetDeviceConfig(GuestContext *ctx) {
    ret(ctx, sceCameraGetDeviceConfig(ARG_I32(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraGetDeviceConfigWithoutHandle(GuestContext *ctx) {
    ret(ctx, sceCameraGetDeviceConfigWithoutHandle(ARG_PTR(rdi)));
}
void shim_sceCameraGetDeviceInfo(GuestContext *ctx) {
    ret(ctx, sceCameraGetDeviceInfo(ARG_I32(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraGetAttribute(GuestContext *ctx) {
    ret(ctx, sceCameraGetAttribute(ARG_I32(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraSetAttribute(GuestContext *ctx) {
    ret(ctx, sceCameraSetAttribute(ARG_I32(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraGetAutoExposureGain(GuestContext *ctx) {
    ret(ctx, sceCameraGetAutoExposureGain(SDK, ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraSetAutoExposureGain(GuestContext *ctx) {
    ret(ctx, sceCameraSetAutoExposureGain(SDK, ARG_I32(rdi), ARG_I32(rsi), ARG_U32(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraGetAutoWhiteBalance(GuestContext *ctx) {
    ret(ctx, sceCameraGetAutoWhiteBalance(ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraSetAutoWhiteBalance(GuestContext *ctx) {
    ret(ctx, sceCameraSetAutoWhiteBalance(ARG_I32(rdi), ARG_I32(rsi), ARG_U32(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraGetContrast(GuestContext *ctx) {
    ret(ctx, sceCameraGetContrast(ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraSetContrast(GuestContext *ctx) {
    ret(ctx, sceCameraSetContrast(ARG_I32(rdi), ARG_I32(rsi), ARG_U32(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraGetDefectivePixelCancellation(GuestContext *ctx) {
    ret(ctx, sceCameraGetDefectivePixelCancellation(ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraSetDefectivePixelCancellation(GuestContext *ctx) {
    ret(ctx, sceCameraSetDefectivePixelCancellation(ARG_I32(rdi), ARG_I32(rsi), ARG_U32(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraSetDefectivePixelCancellationInternal(GuestContext *ctx) {
    ret(ctx,
        sceCameraSetDefectivePixelCancellationInternal(ARG_I32(rdi), ARG_I32(rsi), ARG_U32(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraGetExposureGain(GuestContext *ctx) {
    ret(ctx, sceCameraGetExposureGain(ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraSetExposureGain(GuestContext *ctx) {
    ret(ctx, sceCameraSetExposureGain(ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraGetGamma(GuestContext *ctx) {
    ret(ctx, sceCameraGetGamma(ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraSetGamma(GuestContext *ctx) {
    ret(ctx, sceCameraSetGamma(ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraGetHue(GuestContext *ctx) {
    ret(ctx, sceCameraGetHue(ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraSetHue(GuestContext *ctx) {
    ret(ctx, sceCameraSetHue(ARG_I32(rdi), ARG_I32(rsi), ARG_I32(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraGetLensCorrection(GuestContext *ctx) {
    ret(ctx, sceCameraGetLensCorrection(ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraSetLensCorrection(GuestContext *ctx) {
    ret(ctx, sceCameraSetLensCorrection(ARG_I32(rdi), ARG_I32(rsi), ARG_U32(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraSetLensCorrectionInternal(GuestContext *ctx) {
    ret(ctx, sceCameraSetLensCorrectionInternal(ARG_I32(rdi), ARG_I32(rsi), ARG_U32(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraGetSaturation(GuestContext *ctx) {
    ret(ctx, sceCameraGetSaturation(ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraSetSaturation(GuestContext *ctx) {
    ret(ctx, sceCameraSetSaturation(ARG_I32(rdi), ARG_I32(rsi), ARG_U32(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraGetSharpness(GuestContext *ctx) {
    ret(ctx, sceCameraGetSharpness(ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraSetSharpness(GuestContext *ctx) {
    ret(ctx, sceCameraSetSharpness(SDK, ARG_I32(rdi), ARG_I32(rsi), ARG_U32(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraGetWhiteBalance(GuestContext *ctx) {
    ret(ctx, sceCameraGetWhiteBalance(ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraSetWhiteBalance(GuestContext *ctx) {
    ret(ctx, sceCameraSetWhiteBalance(ARG_I32(rdi), ARG_I32(rsi), ARG_PTR(rdx), ARG_PTR(rcx)));
}
void shim_sceCameraGetMmapConnectedCount(GuestContext *ctx) {
    ret(ctx, sceCameraGetMmapConnectedCount(ARG_PTR(rdi)));
}
void shim_sceCameraGetProductInfo(GuestContext *ctx) {
    ret(ctx, sceCameraGetProductInfo(ARG_PTR(rdi)));
}
void shim_sceCameraGetRegistryInfo(GuestContext *ctx) {
    ret(ctx, sceCameraGetRegistryInfo(ARG_PTR(rdi)));
}
void shim_sceCameraSetDebugStop(GuestContext *ctx) {
    ret(ctx, sceCameraSetDebugStop(ARG_U32(rdi)));
}
void shim_sceCameraSetVideoSync(GuestContext *ctx) {
    ret(ctx, sceCameraSetVideoSync(ARG_I32(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraGetCalibData(GuestContext *ctx) {
    ret(ctx, sceCameraGetCalibData(ARG_I32(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraGetCalibDataFromDevice(GuestContext *ctx) {
    ret(ctx, sceCameraGetCalibDataFromDevice());
}
void shim_sceCameraGetCalibrationData(GuestContext *ctx) {
    ret(ctx, sceCameraGetCalibrationData(ARG_PTR(rdi), ARG_PTR(rsi)));
}
void shim_sceCameraInitializeRegistryCalibData(GuestContext *ctx) {
    ret(ctx, sceCameraInitializeRegistryCalibData());
}
void shim_sceCameraAudioOpen(GuestContext *ctx) {
    ret(ctx, sceCameraAudioOpen());
}
void shim_sceCameraAudioGetData(GuestContext *ctx) {
    ret(ctx, sceCameraAudioGetData(ARG_PTR(rdi)));
}
void shim_sceCameraAudioGetData2(GuestContext *ctx) {
    ret(ctx, sceCameraAudioGetData(ARG_PTR(rdi)));
}
// The original library returns OK from these while failing internally (no camera microphone).
void shim_sceCameraAudioClose(GuestContext *ctx) {
    ret(ctx, 0);
}
void shim_sceCameraAudioReset(GuestContext *ctx) {
    ret(ctx, 0);
}
// "Stubbed on real hardware" (shadPS4 camera_device.cpp / camera_config.cpp): OK, no effect.
void shim_sceCameraDeviceOpen(GuestContext *ctx) {
    ret(ctx, 0);
}
void shim_sceCameraSetAttributeInternal(GuestContext *ctx) {
    ret(ctx, 0);
}

// Stubs in shadPS4 as well.
PS4_HLE_STUB(sceCameraAccGetData)
PS4_HLE_STUB(sceCameraChangeAppModuleState)
PS4_HLE_STUB(sceCameraGetDeviceID)
PS4_HLE_STUB(sceCameraGetDeviceIDWithoutOpen)
PS4_HLE_STUB(sceCameraGetRegister)
PS4_HLE_STUB(sceCameraGetVrCaptureInfo)
PS4_HLE_STUB(sceCameraIsConfigChangeDone)
PS4_HLE_STUB(sceCameraOpenByModuleId)
PS4_HLE_STUB(sceCameraRemoveAppModuleFocus)
PS4_HLE_STUB(sceCameraSetAppModuleFocus)
PS4_HLE_STUB(sceCameraSetCalibData)
PS4_HLE_STUB(sceCameraSetForceActivate)
PS4_HLE_STUB(sceCameraSetProcessFocus)
PS4_HLE_STUB(sceCameraSetProcessFocusByHandle)
PS4_HLE_STUB(sceCameraSetRegister)
PS4_HLE_STUB(sceCameraSetTrackerMode)
PS4_HLE_STUB(sceCameraSetUacModeInternal)
PS4_HLE_STUB(sceCameraSetVideoSyncInternal)
