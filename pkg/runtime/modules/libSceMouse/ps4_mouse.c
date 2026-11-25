// libSceMouse, following shadPS4 (src/core/libraries/mouse): two mouse ports, each with a
// 64-entry ring of input reports; handles are the port index.

#include "ps4_mouse.h"
#include "ps4_hle_stub.h"
#include "ps4_user_service.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define MOUSE_PORTS 2

uint64_t sceKernelGetProcessTime(void);

typedef struct MouseRing {
    OrbisMouseData data[ORBIS_MOUSE_MAX_DATA_NUM];
    uint32_t begin;
    uint32_t size;
} MouseRing;

static pthread_mutex_t g_mouse_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_lib_init = false;
static int32_t g_handles[MOUSE_PORTS] = {-1, -1};
static MouseRing g_rings[MOUSE_PORTS];
// The last report of each port: motion, button and wheel reports are deltas on top of it.
static OrbisMouseData g_current[MOUSE_PORTS] = {{.connected = true}, {.connected = true}};
static atomic_bool g_capture_request;

static int g_host_enabled = -1;

bool ps4_mouse_host_enabled(void) {
    if (g_host_enabled < 0) {
        const char *e = getenv("PS4_MOUSE");
        g_host_enabled = (e && e[0] == '1') ? 1 : 0;
    }
    return g_host_enabled == 1;
}

// Overwrites the oldest report when full, like shadPS4's RingBufferQueue.
static void ring_push_locked(MouseRing *r, const OrbisMouseData *d) {
    const uint32_t idx = (r->begin + r->size) % ORBIS_MOUSE_MAX_DATA_NUM;
    r->data[idx] = *d;
    if (r->size < ORBIS_MOUSE_MAX_DATA_NUM) {
        r->size++;
    } else {
        r->begin = (r->begin + 1) % ORBIS_MOUSE_MAX_DATA_NUM;
    }
}

static bool ring_pop_locked(MouseRing *r, OrbisMouseData *out) {
    if (r->size == 0) {
        return false;
    }
    *out = r->data[r->begin];
    r->begin = (r->begin + 1) % ORBIS_MOUSE_MAX_DATA_NUM;
    r->size--;
    return true;
}

static int port_of_handle_locked(int32_t handle) {
    if (handle < 0 || handle >= MOUSE_PORTS || g_handles[handle] != handle) {
        return -1;
    }
    return handle;
}

int32_t sceMouseInit(void) {
    pthread_mutex_lock(&g_mouse_mutex);
    g_lib_init = true;
    pthread_mutex_unlock(&g_mouse_mutex);
    if (ps4_mouse_host_enabled()) {
        atomic_store(&g_capture_request, true);
    }
    return 0;
}

int32_t sceMouseOpen(int32_t userId, int32_t type, int32_t index, const OrbisMouseOpenParam *pParam) {
    if (userId != ORBIS_USER_SERVICE_INITIAL_USER_ID || type != 0 || index < 0 || index >= MOUSE_PORTS) {
        return ORBIS_MOUSE_ERROR_INVALID_ARG;
    }
    const bool merged = pParam && (pParam->behaviorFlag & ORBIS_MOUSE_OPEN_PARAM_MERGED);
    if (merged && index != 0) {
        // Only one mouse can be opened in merged mode.
        return ORBIS_MOUSE_ERROR_ALREADY_OPENED;
    }

    pthread_mutex_lock(&g_mouse_mutex);
    if (g_handles[index] != -1) {
        pthread_mutex_unlock(&g_mouse_mutex);
        return ORBIS_MOUSE_ERROR_ALREADY_OPENED;
    }
    g_handles[index] = index;
    pthread_mutex_unlock(&g_mouse_mutex);
    return index;
}

int32_t sceMouseClose(int32_t handle) {
    pthread_mutex_lock(&g_mouse_mutex);
    const int port = port_of_handle_locked(handle);
    if (port < 0) {
        pthread_mutex_unlock(&g_mouse_mutex);
        return ORBIS_MOUSE_ERROR_INVALID_HANDLE;
    }
    g_handles[port] = -1;
    pthread_mutex_unlock(&g_mouse_mutex);
    return 0;
}

int32_t sceMouseRead(int32_t handle, OrbisMouseData *pData, int32_t num) {
    if (!pData || num < 1 || num > ORBIS_MOUSE_MAX_DATA_NUM) {
        return ORBIS_MOUSE_ERROR_INVALID_ARG;
    }
    pthread_mutex_lock(&g_mouse_mutex);
    const int port = port_of_handle_locked(handle);
    if (port < 0) {
        pthread_mutex_unlock(&g_mouse_mutex);
        return ORBIS_MOUSE_ERROR_INVALID_HANDLE;
    }
    if (!ps4_mouse_host_enabled()) {
        pthread_mutex_unlock(&g_mouse_mutex);
        memset(&pData[0], 0, sizeof(pData[0]));
        pData[0].connected = false;
        return 1;
    }
    int32_t i = 0;
    while (i < num && ring_pop_locked(&g_rings[port], &pData[i])) {
        i++;
    }
    pthread_mutex_unlock(&g_mouse_mutex);
    return i;
}

bool ps4_mouse_take_capture_request(void) {
    return atomic_exchange(&g_capture_request, false);
}

// Applies a host input report to port 0 (the only host pointer, and the merged port).
// Reports are dropped until the game initialized the library.
static void host_report(int32_t dx, int32_t dy, int32_t wheel, int32_t tilt, uint32_t set_buttons,
                        uint32_t clear_buttons) {
    if (!ps4_mouse_host_enabled()) {
        return;
    }
    const uint64_t now = sceKernelGetProcessTime();
    pthread_mutex_lock(&g_mouse_mutex);
    if (g_lib_init) {
        OrbisMouseData *s = &g_current[0];
        s->x_axis = dx;
        s->y_axis = dy;
        s->wheel = wheel;
        s->tilt = tilt;
        s->buttons = (s->buttons | set_buttons) & ~clear_buttons;
        s->timestamp = now;
        ring_push_locked(&g_rings[0], s);
    }
    pthread_mutex_unlock(&g_mouse_mutex);
}

void ps4_mouse_host_motion(int32_t dx, int32_t dy) {
    host_report(dx, dy, 0, 0, 0, 0);
}

void ps4_mouse_host_button(uint32_t orbis_button, bool down) {
    host_report(0, 0, 0, 0, down ? orbis_button : 0, down ? 0 : orbis_button);
}

void ps4_mouse_host_wheel(int32_t wheel, int32_t tilt) {
    host_report(0, 0, wheel, tilt, 0, 0);
}

// Shims
void shim_sceMouseInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceMouseInit();
    SHIM_RETURN();
}

void shim_sceMouseOpen(GuestContext *ctx) {
    const OrbisMouseOpenParam *param = ctx->rcx ? (const OrbisMouseOpenParam *)(ctx->mem_base + ctx->rcx) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceMouseOpen((int32_t)ctx->rdi, (int32_t)ctx->rsi, (int32_t)ctx->rdx, param);
    SHIM_RETURN();
}

void shim_sceMouseClose(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceMouseClose((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceMouseRead(GuestContext *ctx) {
    OrbisMouseData *pData = ctx->rsi ? (OrbisMouseData *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceMouseRead((int32_t)ctx->rdi, pData, (int32_t)ctx->rdx);
    SHIM_RETURN();
}

// Functions that shadPS4 stubs as well (src/core/libraries, libSceMouse).
PS4_HLE_STUB(sceMouseConnectPort)
PS4_HLE_STUB(sceMouseDebugGetDeviceId)
PS4_HLE_STUB(sceMouseDeviceOpen)
PS4_HLE_STUB(sceMouseDisconnectDevice)
PS4_HLE_STUB(sceMouseDisconnectPort)
PS4_HLE_STUB(sceMouseGetDeviceInfo)
PS4_HLE_STUB(sceMouseMbusInit)
PS4_HLE_STUB(sceMouseSetHandType)
PS4_HLE_STUB(sceMouseSetPointerSpeed)
PS4_HLE_STUB(sceMouseSetProcessPrivilege)
