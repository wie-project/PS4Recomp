// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_disc_map.h"
#include "ps4_signin_dialog.h"
#include "ps4_vr_tracker.h"
#include "ps4_direct_mem.h"
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

// The kernel side of sceVrTrackerInit's mapping check: one guest mapping [MAP_START, MAP_END).
#define MAP_START 0x10000000ull
#define MAP_END (MAP_START + 0x6000000ull)

int sceKernelVirtualQuery(GuestContext *ctx, const void *addr, int flags,
                          OrbisKernelVirtualQueryInfo *info, size_t infoSize) {
    const uint64_t a = (uint64_t)(uintptr_t)addr;
    if (a < MAP_START || a >= MAP_END) {
        return -14; // -EFAULT
    }
    memset(info, 0, infoSize);
    info->start = (void *)(uintptr_t)MAP_START;
    info->end = (void *)(uintptr_t)MAP_END;
    return 0;
}

uint64_t sceKernelGetProcessTime(void) {
    return 123456789;
}

static OrbisVrTrackerInitParam valid_init(void) {
    OrbisVrTrackerInitParam p;
    memset(&p, 0, sizeof(p));
    p.size = sizeof(p);
    p.direct_memory_garlic = MAP_START;
    p.direct_memory_garlic_size = ORBIS_VR_TRACKER_GARLIC_SIZE;
    p.direct_memory_garlic_alignment = ORBIS_VR_TRACKER_MEMORY_ALIGNMENT;
    p.direct_memory_onion = MAP_START + ORBIS_VR_TRACKER_GARLIC_SIZE;
    p.direct_memory_onion_size = ORBIS_VR_TRACKER_BASE_ONION_SIZE;
    p.direct_memory_onion_alignment = ORBIS_VR_TRACKER_MEMORY_ALIGNMENT;
    p.work_memory = MAP_START + 0x4000000;
    p.work_memory_size = ORBIS_VR_TRACKER_WORK_SIZE;
    p.work_memory_alignment = ORBIS_VR_TRACKER_MEMORY_ALIGNMENT;
    p.gpu_pipe_id = 6;
    p.gpu_queue_id = 7;
    return p;
}

static int test_vr_query(void) {
    OrbisVrTrackerQueryMemoryParam q;
    memset(&q, 0, sizeof(q));
    q.size = sizeof(q);
    OrbisVrTrackerQueryMemoryResult r;
    memset(&r, 0xAB, sizeof(r));
    CHECK(sceVrTrackerQueryMemory(&q, &r) == 0);
    CHECK(r.direct_memory_onion_size == 0x400000 && r.direct_memory_garlic_size == 0x3000000);
    CHECK(r.work_memory_size == 0x1000000 && r.work_memory_alignment == 0x10000);
    CHECK(r.direct_memory_onion_alignment == 0x10000 && r.direct_memory_garlic_alignment == 0x10000);
    CHECK(r.size == 0xABABABABu); // not written

    q.calibration_settings.move_position = ORBIS_VR_TRACKER_CALIBRATION_AUTO;
    q.profile = ORBIS_VR_TRACKER_PROFILE_100;
    CHECK(sceVrTrackerQueryMemory(&q, &r) == 0);
    CHECK(r.direct_memory_onion_size == 0x800000);

    q.calibration_settings.hmd_position = ORBIS_VR_TRACKER_CALIBRATION_AUTO;
    CHECK(sceVrTrackerQueryMemory(&q, &r) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    q.calibration_settings.hmd_position = 0;
    q.profile = 50;
    CHECK(sceVrTrackerQueryMemory(&q, &r) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    q.profile = 0;
    q.size = 63;
    CHECK(sceVrTrackerQueryMemory(&q, &r) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    CHECK(sceVrTrackerQueryMemory(NULL, &r) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    printf("PASS vr-query\n");
    return 0;
}

static int test_vr_lifecycle(void) {
    GuestContext ctx = {0};
    uint64_t t = 0;
    CHECK(sceVrTrackerGetTime(&t) == ORBIS_VR_TRACKER_ERROR_NOT_INIT);
    CHECK(sceVrTrackerRegisterDevice(ORBIS_VR_TRACKER_DEVICE_DUALSHOCK4, 5) ==
          ORBIS_VR_TRACKER_ERROR_NOT_INIT);
    CHECK(sceVrTrackerTerm() == ORBIS_VR_TRACKER_ERROR_NOT_INIT);

    OrbisVrTrackerInitParam p = valid_init();
    p.gpu_pipe_id = 7;
    CHECK(sceVrTrackerInit(&ctx, &p) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    p = valid_init();
    p.gpu_queue_id = -1;
    CHECK(sceVrTrackerInit(&ctx, &p) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    p = valid_init();
    p.direct_memory_onion_size = 0x800000; // only with automatic Move calibration
    CHECK(sceVrTrackerInit(&ctx, &p) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    p.calibration_settings.move_position = ORBIS_VR_TRACKER_CALIBRATION_AUTO;
    CHECK(sceVrTrackerInit(&ctx, &p) == 0);
    CHECK(sceVrTrackerInit(&ctx, &p) == ORBIS_VR_TRACKER_ERROR_ALREADY_INITIALIZED);

    CHECK(sceVrTrackerGetTime(NULL) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    CHECK(sceVrTrackerGetTime(&t) == 0 && t == 123456789);

    CHECK(sceVrTrackerRegisterDevice(4, 5) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    CHECK(sceVrTrackerRegisterDeviceInternal(ORBIS_VR_TRACKER_DEVICE_MOVE, 9, 5, 0) ==
          ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    CHECK(sceVrTrackerRegisterDevice(ORBIS_VR_TRACKER_DEVICE_DUALSHOCK4, 5) == 0);
    CHECK(sceVrTrackerRegisterDevice2(ORBIS_VR_TRACKER_DEVICE_DUALSHOCK4, 6) ==
          ORBIS_VR_TRACKER_ERROR_DEVICE_ALREADY_REGISTERED);
    CHECK(sceVrTrackerRegisterDevice2(ORBIS_VR_TRACKER_DEVICE_MOVE, 7) == 0);

    OrbisVrTrackerRecalibrateParam rc = {.size = sizeof(rc)};
    rc.device_type = ORBIS_VR_TRACKER_DEVICE_HMD;
    CHECK(sceVrTrackerRecalibrate(&rc) == ORBIS_VR_TRACKER_ERROR_DEVICE_NOT_REGISTERED);
    rc.device_type = ORBIS_VR_TRACKER_DEVICE_DUALSHOCK4;
    CHECK(sceVrTrackerRecalibrate(&rc) == 0);
    rc.device_type = ORBIS_VR_TRACKER_DEVICE_GUN;
    CHECK(sceVrTrackerRecalibrate(&rc) == ORBIS_VR_TRACKER_ERROR_DEVICE_NOT_REGISTERED);
    rc.size = 0;
    CHECK(sceVrTrackerRecalibrate(&rc) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);

    OrbisVrTrackerGpuWaitParam w = {.size = sizeof(w)};
    CHECK(sceVrTrackerGpuWait(&w) == ORBIS_VR_TRACKER_ERROR_NOT_EXECUTE_GPU_SUBMIT);
    w.size = 1;
    CHECK(sceVrTrackerGpuWait(&w) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    CHECK(sceVrTrackerGpuSubmit(NULL) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    CHECK(sceVrTrackerGpuWaitAndCpuProcess() == ORBIS_VR_TRACKER_ERROR_NOT_EXECUTE_GPU_SUBMIT);
    CHECK(sceVrTrackerSetDurationUntilStatusNotTracking(3, 10) == 0);
    CHECK(sceVrTrackerSetDurationUntilStatusNotTracking(4, 10) ==
          ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);

    CHECK(sceVrTrackerUnregisterDevice(-1) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    CHECK(sceVrTrackerUnregisterDevice(42) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);
    CHECK(sceVrTrackerUnregisterDevice(5) == 0);
    CHECK(sceVrTrackerUnregisterDevice(5) == ORBIS_VR_TRACKER_ERROR_ARGUMENT_INVALID);

    CHECK(sceVrTrackerTerm() == 0);
    CHECK(sceVrTrackerGpuWaitAndCpuProcess() == ORBIS_VR_TRACKER_ERROR_NOT_INIT);
    // Registrations outlive Term: the Move handle is still taken after a new Init.
    CHECK(sceVrTrackerInit(&ctx, &p) == 0);
    CHECK(sceVrTrackerRegisterDevice(ORBIS_VR_TRACKER_DEVICE_MOVE, 8) ==
          ORBIS_VR_TRACKER_ERROR_DEVICE_ALREADY_REGISTERED);
    CHECK(sceVrTrackerTerm() == 0);
    printf("PASS vr-lifecycle\n");
    return 0;
}

// A range that is not mapped for its whole size stops the process (the console faults on it
// later). The Go test expects this case to abort with the message.
static int test_vr_bad_mapping(void) {
    GuestContext ctx = {0};
    OrbisVrTrackerInitParam p = valid_init();
    p.work_memory = MAP_END - 0x1000; // mapped, but 4 KiB short of work_memory_size
    sceVrTrackerInit(&ctx, &p);
    printf("sceVrTrackerInit returned\n");
    return 0;
}

static int test_signin(void) {
    OrbisSigninDialogResult r;
    CHECK(sceSigninDialogGetStatus() == ORBIS_SIGNIN_DIALOG_STATUS_NONE);
    CHECK(sceSigninDialogGetResult(&r) == ORBIS_SIGNIN_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceSigninDialogOpen() == 0);
    CHECK(sceSigninDialogUpdateStatus() == ORBIS_SIGNIN_DIALOG_STATUS_NONE);
    CHECK(sceSigninDialogInitialize() == 0);
    CHECK(sceSigninDialogUpdateStatus() == ORBIS_SIGNIN_DIALOG_STATUS_FINISHED);
    CHECK(sceSigninDialogGetResult(NULL) == ORBIS_SIGNIN_DIALOG_ERROR_PARAM_INVALID);
    memset(&r, 0x5A, sizeof(r));
    CHECK(sceSigninDialogGetResult(&r) == 0);
    CHECK(r.result == ORBIS_SIGNIN_DIALOG_RESULT_OK && r.reserved[0] == 0x5A5A5A5A);
    CHECK(sceSigninDialogClose() == 0);
    CHECK(sceSigninDialogGetStatus() == ORBIS_SIGNIN_DIALOG_STATUS_NONE);
    CHECK(sceSigninDialogGetResult(&r) == 0);
    CHECK(sceSigninDialogOpen() == 0);
    CHECK(sceSigninDialogGetStatus() == ORBIS_SIGNIN_DIALOG_STATUS_FINISHED);
    CHECK(sceSigninDialogTerminate() == 0);
    CHECK(sceSigninDialogGetStatus() == ORBIS_SIGNIN_DIALOG_STATUS_NONE);
    CHECK(sceSigninDialogGetResult(&r) == ORBIS_SIGNIN_DIALOG_ERROR_NOT_INITIALIZED);
    printf("PASS signin\n");
    return 0;
}

static int test_disc_map(void) {
    uint8_t *mem = calloc(1, 0x1000);
    GuestContext ctx = {0};
    ctx.mem_base = mem;
    ctx.mem_size = 0x1000;
    const uint64_t want = (uint64_t)(int64_t)ORBIS_DISC_MAP_ERROR_NO_BITMAP_INFO;

    ctx.rsi = 0x100;
    ctx.rdx = 0x104;
    shim_sceDiscMapGetPackageSize(&ctx);
    CHECK(ctx.rax == want);
    shim_sceDiscMapIsRequestOnHDD(&ctx);
    CHECK(ctx.rax == want);
    shim_Func_8A828CAEE7EDD5E9(&ctx);
    CHECK(ctx.rax == want);
    shim_Func_E7EBCE96E92F91F8(&ctx);
    CHECK(ctx.rax == want);

    memset(mem, 0xFF, 0x1000);
    ctx.rcx = 0x200;
    ctx.r8 = 0x204;
    ctx.r9 = 0x208;
    shim_Func_7C980FFB0AA27E7A(&ctx);
    CHECK(ctx.rax == 0);
    const uint8_t zero[12] = {0};
    CHECK(memcmp(mem + 0x200, zero, sizeof(zero)) == 0 && mem[0x20C] == 0xFF);
    free(mem);
    printf("PASS disc-map\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "vr-query") == 0) return test_vr_query();
    if (strcmp(argv[1], "vr-lifecycle") == 0) return test_vr_lifecycle();
    if (strcmp(argv[1], "vr-bad-mapping") == 0) return test_vr_bad_mapping();
    if (strcmp(argv[1], "signin") == 0) return test_signin();
    if (strcmp(argv[1], "disc-map") == 0) return test_disc_map();
    return 2;
}
