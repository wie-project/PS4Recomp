// SPDX-License-Identifier: GPL-2.0-or-later

// libSceHmd (with its distortion and reprojection parts) after shadPS4 (hmd/hmd.cpp,
// hmd_distortion.cpp, hmd_reprojection.cpp). PlayStation VR headsets are not supported: the
// library initializes and opens a handle, and every query reports what the console reports
// without a headset. The functions shadPS4 leaves as stubs are stubs here too (end of file).

#include "ps4_hmd.h"
#include "ps4_hle_stub.h"
#include "recomp_runtime.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

uint32_t ps4_compiled_sdk_version(const GuestContext *ctx);

#define USER_ID_INVALID (-1)
#define USER_ID_SYSTEM 0xFF

static bool g_initialized;
static bool g_distortion_initialized;
static int32_t g_internal_handle;
static int32_t g_user_id = USER_ID_INVALID;

// ---------------------------------------------------------------------------------------------
// Library logic

int32_t sceHmdDistortionInitialize(void) {
    g_distortion_initialized = true;
    return 0;
}

// Internal (not exported): terminates the distortion sub-library.
int32_t sceHmdDistortionTerminate(void) {
    if (!g_distortion_initialized) {
        return (int32_t)ORBIS_HMD_ERROR_NOT_INITIALIZED;
    }
    g_distortion_initialized = false;
    return 0;
}

static int32_t hmd_initialize(const OrbisHmdInitializeParam *param, bool distortion) {
    if (g_initialized) {
        return (int32_t)ORBIS_HMD_ERROR_ALREADY_INITIALIZED;
    }
    if (!param) {
        return (int32_t)ORBIS_HMD_ERROR_PARAMETER_NULL;
    }
    fprintf(stderr, "[ps4-hmd] PlayStation VR headsets are not supported\n");
    if (distortion && param->reserved0 != 0) {
        sceHmdDistortionInitialize();
    }
    g_initialized = true;
    return 0;
}

int32_t sceHmdInitialize(const OrbisHmdInitializeParam *param) {
    return hmd_initialize(param, true);
}

int32_t sceHmdInitialize315(const OrbisHmdInitializeParam *param) {
    return hmd_initialize(param, false);
}

int32_t sceHmdOpen(int32_t user_id, int32_t type, int32_t index, const OrbisHmdOpenParam *param) {
    if (!g_initialized) {
        return (int32_t)ORBIS_HMD_ERROR_NOT_INITIALIZED;
    }
    if (type != 0 || index != 0 || param) {
        return (int32_t)ORBIS_HMD_ERROR_PARAMETER_INVALID;
    }
    if (g_internal_handle != 0) {
        return (int32_t)ORBIS_HMD_ERROR_ALREADY_OPENED;
    }
    if (user_id == USER_ID_INVALID || user_id == USER_ID_SYSTEM) {
        return (int32_t)ORBIS_HMD_ERROR_PARAMETER_INVALID;
    }
    g_user_id = user_id;
    g_internal_handle = ORBIS_HMD_INTERNAL_HANDLE;
    return g_internal_handle;
}

int32_t sceHmdClose(int32_t handle) {
    if (!g_initialized) {
        return (int32_t)ORBIS_HMD_ERROR_NOT_INITIALIZED;
    }
    if (handle != g_internal_handle) {
        return (int32_t)ORBIS_HMD_ERROR_HANDLE_INVALID;
    }
    g_internal_handle = 0;
    g_user_id = USER_ID_INVALID;
    return 0;
}

int32_t sceHmdTerminate(void) {
    if (!g_initialized) {
        return (int32_t)ORBIS_HMD_ERROR_NOT_INITIALIZED;
    }
    sceHmdDistortionTerminate();
    g_initialized = false;
    return 0;
}

// From firmware 4.50 the library's own checks fail without a headset, and the per-handle
// queries answer HANDLE_INVALID instead of DEVICE_DISCONNECTED.
static bool headset_check_fails(uint32_t fw) {
    return fw >= ORBIS_HMD_FW_450;
}

int32_t sceHmdGet2DEyeOffset(uint32_t fw, int32_t handle, OrbisHmdEyeOffset *left, OrbisHmdEyeOffset *right) {
    if (!g_initialized) {
        return (int32_t)ORBIS_HMD_ERROR_NOT_INITIALIZED;
    }
    if (handle != g_internal_handle || headset_check_fails(fw)) {
        return (int32_t)ORBIS_HMD_ERROR_HANDLE_INVALID;
    }
    if (!left || !right) {
        return (int32_t)ORBIS_HMD_ERROR_PARAMETER_NULL;
    }
    // The default offsets.
    left->offset_x = -0.0315f;
    left->offset_y = 0.f;
    left->offset_z = 0.f;
    right->offset_x = 0.0315f;
    right->offset_y = 0.f;
    right->offset_z = 0.f;
    return 0;
}

int32_t sceHmdGetAssyError(const void *data) {
    if (!data) {
        return (int32_t)ORBIS_HMD_ERROR_PARAMETER_NULL;
    }
    if (!g_initialized) {
        return (int32_t)ORBIS_HMD_ERROR_NOT_INITIALIZED;
    }
    return (int32_t)ORBIS_HMD_ERROR_DEVICE_DISCONNECTED;
}

static void fill_device_information(OrbisHmdDeviceInformation *info) {
    memset(info, 0, sizeof(*info));
    info->status = ORBIS_HMD_DEVICE_STATUS_NOT_DETECTED;
    info->user_id = g_user_id;
}

int32_t sceHmdGetDeviceInformation(OrbisHmdDeviceInformation *info) {
    if (!info) {
        return (int32_t)ORBIS_HMD_ERROR_PARAMETER_NULL;
    }
    if (!g_initialized) {
        return (int32_t)ORBIS_HMD_ERROR_NOT_INITIALIZED;
    }
    fill_device_information(info);
    return 0;
}

int32_t sceHmdGetDeviceInformationByHandle(uint32_t fw, int32_t handle, OrbisHmdDeviceInformation *info) {
    if (handle != g_internal_handle || headset_check_fails(fw)) {
        return (int32_t)ORBIS_HMD_ERROR_HANDLE_INVALID;
    }
    if (!info) {
        return (int32_t)ORBIS_HMD_ERROR_PARAMETER_NULL;
    }
    if (!g_initialized) {
        return (int32_t)ORBIS_HMD_ERROR_NOT_INITIALIZED;
    }
    fill_device_information(info);
    return 0;
}

int32_t sceHmdGetFieldOfView(uint32_t fw, int32_t handle, OrbisHmdFieldOfView *fov) {
    if (!fov) {
        return (int32_t)ORBIS_HMD_ERROR_PARAMETER_NULL;
    }
    if (handle != g_internal_handle || headset_check_fails(fw)) {
        return (int32_t)ORBIS_HMD_ERROR_HANDLE_INVALID;
    }
    if (!g_initialized) {
        return (int32_t)ORBIS_HMD_ERROR_NOT_INITIALIZED;
    }
    // With a headset the console answers tan_out 1.20743, tan_in 1.181346, tan_top and
    // tan_bottom 1.262872; without one the library's internal checks fail.
    return (int32_t)ORBIS_HMD_ERROR_HANDLE_INVALID;
}

// sceHmdGetInertialSensorData and its unnamed twin (Func_69383B2B4E3AEABF).
int32_t sceHmdGetInertialSensorData(uint32_t fw, int32_t handle) {
    if (!g_initialized) {
        return (int32_t)ORBIS_HMD_ERROR_NOT_INITIALIZED;
    }
    if (handle != g_internal_handle || headset_check_fails(fw)) {
        return (int32_t)ORBIS_HMD_ERROR_HANDLE_INVALID;
    }
    return (int32_t)ORBIS_HMD_ERROR_DEVICE_DISCONNECTED;
}

int32_t sceHmdInternalGetDeviceStatus(uint32_t *status) {
    if (!status) {
        return (int32_t)ORBIS_HMD_ERROR_PARAMETER_NULL;
    }
    *status = ORBIS_HMD_DEVICE_STATUS_NOT_DETECTED;
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Shims

static void *guest_ptr(const GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

#define RET(v)                                                                                     \
    do {                                                                                           \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(v);                                                \
        SHIM_RETURN();                                                                             \
    } while (0)

void shim_sceHmdInitialize(GuestContext *ctx) {
    RET(sceHmdInitialize((const OrbisHmdInitializeParam *)guest_ptr(ctx, ctx->rdi)));
}

void shim_sceHmdInitialize315(GuestContext *ctx) {
    RET(sceHmdInitialize315((const OrbisHmdInitializeParam *)guest_ptr(ctx, ctx->rdi)));
}

void shim_sceHmdOpen(GuestContext *ctx) {
    RET(sceHmdOpen((int32_t)ctx->rdi, (int32_t)ctx->rsi, (int32_t)ctx->rdx,
                   (const OrbisHmdOpenParam *)guest_ptr(ctx, ctx->rcx)));
}

void shim_sceHmdClose(GuestContext *ctx) {
    RET(sceHmdClose((int32_t)ctx->rdi));
}

void shim_sceHmdTerminate(GuestContext *ctx) {
    RET(sceHmdTerminate());
}

void shim_sceHmdGet2DEyeOffset(GuestContext *ctx) {
    RET(sceHmdGet2DEyeOffset(ps4_compiled_sdk_version(ctx), (int32_t)ctx->rdi,
                             (OrbisHmdEyeOffset *)guest_ptr(ctx, ctx->rsi),
                             (OrbisHmdEyeOffset *)guest_ptr(ctx, ctx->rdx)));
}

void shim_sceHmdGetAssyError(GuestContext *ctx) {
    RET(sceHmdGetAssyError(guest_ptr(ctx, ctx->rdi)));
}

void shim_sceHmdGetDeviceInformation(GuestContext *ctx) {
    RET(sceHmdGetDeviceInformation((OrbisHmdDeviceInformation *)guest_ptr(ctx, ctx->rdi)));
}

void shim_sceHmdGetDeviceInformationByHandle(GuestContext *ctx) {
    RET(sceHmdGetDeviceInformationByHandle(ps4_compiled_sdk_version(ctx), (int32_t)ctx->rdi,
                                           (OrbisHmdDeviceInformation *)guest_ptr(ctx, ctx->rsi)));
}

void shim_sceHmdGetFieldOfView(GuestContext *ctx) {
    RET(sceHmdGetFieldOfView(ps4_compiled_sdk_version(ctx), (int32_t)ctx->rdi,
                             (OrbisHmdFieldOfView *)guest_ptr(ctx, ctx->rsi)));
}

// s32 sceHmdGetInertialSensorData(s32 handle, void *data, s32 unk): the data is not written.
void shim_sceHmdGetInertialSensorData(GuestContext *ctx) {
    RET(sceHmdGetInertialSensorData(ps4_compiled_sdk_version(ctx), (int32_t)ctx->rdi));
}

void shim_Func_69383B2B4E3AEABF(GuestContext *ctx) {
    RET(sceHmdGetInertialSensorData(ps4_compiled_sdk_version(ctx), (int32_t)ctx->rdi));
}

void shim_sceHmdInternalGetDeviceStatus(GuestContext *ctx) {
    RET(sceHmdInternalGetDeviceStatus((uint32_t *)guest_ptr(ctx, ctx->rdi)));
}

// 0: no headset connected.
void shim_sceHmdInternalMmapIsConnect(GuestContext *ctx) {
    RET(0);
}

// Functions the console implements as stubs: SCE_OK, nothing logged.
void shim_Func_9A276E739E54EEAF(GuestContext *ctx) {
    RET(0);
}

void shim_Func_9E501994E289CBE7(GuestContext *ctx) {
    RET(0);
}

void shim_Func_ADCCC25CB876FDBE(GuestContext *ctx) {
    RET(0);
}

// Distortion

// s32 sceHmdDistortionInitialize(void *reserved)
void shim_sceHmdDistortionInitialize(GuestContext *ctx) {
    RET(sceHmdDistortionInitialize());
}

// Stubs on the console that reject every call.
void shim_sceHmdDistortionGetCompoundEyeCorrectionCommand(GuestContext *ctx) {
    RET(ORBIS_HMD_ERROR_PARAMETER_INVALID);
}

void shim_Func_B26430EA74FC3DC0(GuestContext *ctx) {
    RET(ORBIS_HMD_ERROR_PARAMETER_INVALID);
}

void shim_sceHmdDistortionGetWorkMemoryAlign(GuestContext *ctx) {
    RET(ORBIS_HMD_DISTORTION_WORK_MEMORY_ALIGN);
}

void shim_sceHmdDistortionGetWorkMemorySize(GuestContext *ctx) {
    RET(ORBIS_HMD_DISTORTION_WORK_MEMORY_SIZE);
}

// Reprojection: the memory requirements are the return values.

void shim_sceHmdReprojectionQueryGarlicBuffAlign(GuestContext *ctx) {
    RET(ORBIS_HMD_REPROJECTION_GARLIC_BUFF_ALIGN);
}

void shim_sceHmdReprojectionQueryGarlicBuffSize(GuestContext *ctx) {
    RET(ORBIS_HMD_REPROJECTION_GARLIC_BUFF_SIZE);
}

void shim_sceHmdReprojectionQueryOnionBuffAlign(GuestContext *ctx) {
    RET(ORBIS_HMD_REPROJECTION_ONION_BUFF_ALIGN);
}

void shim_sceHmdReprojectionQueryOnionBuffSize(GuestContext *ctx) {
    RET(ORBIS_HMD_REPROJECTION_ONION_BUFF_SIZE);
}

// ---------------------------------------------------------------------------------------------
// Stubs in shadPS4 ("(STUBBED) called", SCE_OK)

PS4_HLE_STUB(sceHmdInternal3dAudioClose)
PS4_HLE_STUB(sceHmdInternal3dAudioOpen)
PS4_HLE_STUB(sceHmdInternal3dAudioSendData)
PS4_HLE_STUB(sceHmdInternalAnotherScreenClose)
PS4_HLE_STUB(sceHmdInternalAnotherScreenGetAudioStatus)
PS4_HLE_STUB(sceHmdInternalAnotherScreenGetFadeState)
PS4_HLE_STUB(sceHmdInternalAnotherScreenGetVideoStatus)
PS4_HLE_STUB(sceHmdInternalAnotherScreenOpen)
PS4_HLE_STUB(sceHmdInternalAnotherScreenSendAudio)
PS4_HLE_STUB(sceHmdInternalAnotherScreenSendVideo)
PS4_HLE_STUB(sceHmdInternalAnotherScreenSetFadeAndSwitch)
PS4_HLE_STUB(sceHmdInternalBindDeviceWithUserId)
PS4_HLE_STUB(sceHmdInternalCheckDeviceModelMk3)
PS4_HLE_STUB(sceHmdInternalCheckS3dPassModeAvailable)
PS4_HLE_STUB(sceHmdInternalCrashReportCancel)
PS4_HLE_STUB(sceHmdInternalCrashReportClose)
PS4_HLE_STUB(sceHmdInternalCrashReportOpen)
PS4_HLE_STUB(sceHmdInternalCrashReportReadData)
PS4_HLE_STUB(sceHmdInternalCrashReportReadDataSize)
PS4_HLE_STUB(sceHmdInternalCreateSharedMemory)
PS4_HLE_STUB(sceHmdInternalDfuCheckAfterPvt)
PS4_HLE_STUB(sceHmdInternalDfuCheckPartialUpdateAvailable)
PS4_HLE_STUB(sceHmdInternalDfuClose)
PS4_HLE_STUB(sceHmdInternalDfuGetStatus)
PS4_HLE_STUB(sceHmdInternalDfuOpen)
PS4_HLE_STUB(sceHmdInternalDfuReset)
PS4_HLE_STUB(sceHmdInternalDfuSend)
PS4_HLE_STUB(sceHmdInternalDfuSendSize)
PS4_HLE_STUB(sceHmdInternalDfuSetMode)
PS4_HLE_STUB(sceHmdInternalDfuStart)
PS4_HLE_STUB(sceHmdInternalEventInitialize)
PS4_HLE_STUB(sceHmdInternalGetBrightness)
PS4_HLE_STUB(sceHmdInternalGetCrashDumpInfo)
PS4_HLE_STUB(sceHmdInternalGetDebugMode)
PS4_HLE_STUB(sceHmdInternalGetDebugSocialScreenMode)
PS4_HLE_STUB(sceHmdInternalGetDebugTextMode)
PS4_HLE_STUB(sceHmdInternalGetDefaultLedData)
PS4_HLE_STUB(sceHmdInternalGetDemoMode)
PS4_HLE_STUB(sceHmdInternalGetDeviceInformation)
PS4_HLE_STUB(sceHmdInternalGetDeviceInformationByHandle)
PS4_HLE_STUB(sceHmdInternalGetEyeStatus)
PS4_HLE_STUB(sceHmdInternalGetHmuOpticalParam)
PS4_HLE_STUB(sceHmdInternalGetHmuPowerStatusForDebug)
PS4_HLE_STUB(sceHmdInternalGetHmuSerialNumber)
PS4_HLE_STUB(sceHmdInternalGetIPD)
PS4_HLE_STUB(sceHmdInternalGetIpdSettingEnableForSystemService)
PS4_HLE_STUB(sceHmdInternalGetPuBuildNumber)
PS4_HLE_STUB(sceHmdInternalGetPuPositionParam)
PS4_HLE_STUB(sceHmdInternalGetPuRevision)
PS4_HLE_STUB(sceHmdInternalGetPUSerialNumber)
PS4_HLE_STUB(sceHmdInternalGetPUVersion)
PS4_HLE_STUB(sceHmdInternalGetRequiredPUPVersion)
PS4_HLE_STUB(sceHmdInternalGetStatusReport)
PS4_HLE_STUB(sceHmdInternalGetTv4kCapability)
PS4_HLE_STUB(sceHmdInternalGetVirtualDisplayDepth)
PS4_HLE_STUB(sceHmdInternalGetVirtualDisplayHeight)
PS4_HLE_STUB(sceHmdInternalGetVirtualDisplaySize)
PS4_HLE_STUB(sceHmdInternalGetVr2dData)
PS4_HLE_STUB(sceHmdInternalIsCommonDlgMiniAppVr2d)
PS4_HLE_STUB(sceHmdInternalIsCommonDlgVr2d)
PS4_HLE_STUB(sceHmdInternalIsGameVr2d)
PS4_HLE_STUB(sceHmdInternalIsMiniAppVr2d)
PS4_HLE_STUB(sceHmdInternalMapSharedMemory)
PS4_HLE_STUB(sceHmdInternalMirroringModeSetAspect)
PS4_HLE_STUB(sceHmdInternalMirroringModeSetAspectDebug)
PS4_HLE_STUB(sceHmdInternalMmapGetCount)
PS4_HLE_STUB(sceHmdInternalMmapGetModeId)
PS4_HLE_STUB(sceHmdInternalMmapGetSensorCalibrationData)
PS4_HLE_STUB(sceHmdInternalPushVr2dData)
PS4_HLE_STUB(sceHmdInternalRegisterEventCallback)
PS4_HLE_STUB(sceHmdInternalResetInertialSensor)
PS4_HLE_STUB(sceHmdInternalResetLedForVrTracker)
PS4_HLE_STUB(sceHmdInternalResetLedForVsh)
PS4_HLE_STUB(sceHmdInternalSeparateModeClose)
PS4_HLE_STUB(sceHmdInternalSeparateModeGetAudioStatus)
PS4_HLE_STUB(sceHmdInternalSeparateModeGetVideoStatus)
PS4_HLE_STUB(sceHmdInternalSeparateModeOpen)
PS4_HLE_STUB(sceHmdInternalSeparateModeSendAudio)
PS4_HLE_STUB(sceHmdInternalSeparateModeSendVideo)
PS4_HLE_STUB(sceHmdInternalSetBrightness)
PS4_HLE_STUB(sceHmdInternalSetCrashReportCommand)
PS4_HLE_STUB(sceHmdInternalSetDebugGpo)
PS4_HLE_STUB(sceHmdInternalSetDebugMode)
PS4_HLE_STUB(sceHmdInternalSetDebugSocialScreenMode)
PS4_HLE_STUB(sceHmdInternalSetDebugTextMode)
PS4_HLE_STUB(sceHmdInternalSetDefaultLedData)
PS4_HLE_STUB(sceHmdInternalSetDemoMode)
PS4_HLE_STUB(sceHmdInternalSetDeviceConnection)
PS4_HLE_STUB(sceHmdInternalSetForcedCrash)
PS4_HLE_STUB(sceHmdInternalSetHmuPowerControl)
PS4_HLE_STUB(sceHmdInternalSetHmuPowerControlForDebug)
PS4_HLE_STUB(sceHmdInternalSetIPD)
PS4_HLE_STUB(sceHmdInternalSetIpdSettingEnableForSystemService)
PS4_HLE_STUB(sceHmdInternalSetLedOn)
PS4_HLE_STUB(sceHmdInternalSetM2LedBrightness)
PS4_HLE_STUB(sceHmdInternalSetM2LedOn)
PS4_HLE_STUB(sceHmdInternalSetPortConnection)
PS4_HLE_STUB(sceHmdInternalSetPortStatus)
PS4_HLE_STUB(sceHmdInternalSetS3dPassMode)
PS4_HLE_STUB(sceHmdInternalSetSidetone)
PS4_HLE_STUB(sceHmdInternalSetUserType)
PS4_HLE_STUB(sceHmdInternalSetVirtualDisplayDepth)
PS4_HLE_STUB(sceHmdInternalSetVirtualDisplayHeight)
PS4_HLE_STUB(sceHmdInternalSetVirtualDisplaySize)
PS4_HLE_STUB(sceHmdInternalSetVRMode)
PS4_HLE_STUB(sceHmdInternalSocialScreenGetFadeState)
PS4_HLE_STUB(sceHmdInternalSocialScreenSetFadeAndSwitch)
PS4_HLE_STUB(sceHmdInternalSocialScreenSetOutput)
PS4_HLE_STUB(Func_202D0D1A687FCD2F)
PS4_HLE_STUB(Func_358DBF818A3D8A12)
PS4_HLE_STUB(Func_5CCBADA76FE8F40E)
PS4_HLE_STUB(Func_63D403167DC08CF0)
PS4_HLE_STUB(Func_791560C32F4F6D68)
PS4_HLE_STUB(Func_7C955961EA85B6D3)
PS4_HLE_STUB(Func_9952277839236BA7)
PS4_HLE_STUB(Func_A31F4DA8B3BD2E12)
PS4_HLE_STUB(Func_A92D7C23AC364993)
PS4_HLE_STUB(Func_B16652641FE69F0E)
PS4_HLE_STUB(Func_FC193BD653F2AF2E)
PS4_HLE_STUB(Func_FF2E0E53015FE231)
PS4_HLE_STUB(sceHmdDistortionGet2dVrCommand)
PS4_HLE_STUB(sceHmdDistortionGetCorrectionCommand)
PS4_HLE_STUB(sceHmdDistortionGetWideNearCorrectionCommand)
PS4_HLE_STUB(sceHmdDistortionSetOutputMinColor)
PS4_HLE_STUB(sceHmdGetDistortionParams)
PS4_HLE_STUB(Func_B614F290B67FB59B)
PS4_HLE_STUB(sceHmdReprojectionStartMultilayer)
PS4_HLE_STUB(sceHmdReprojectionAddDisplayBuffer)
PS4_HLE_STUB(sceHmdReprojectionClearUserEventEnd)
PS4_HLE_STUB(sceHmdReprojectionClearUserEventStart)
PS4_HLE_STUB(sceHmdReprojectionDebugGetLastInfo)
PS4_HLE_STUB(sceHmdReprojectionDebugGetLastInfoMultilayer)
PS4_HLE_STUB(sceHmdReprojectionFinalize)
PS4_HLE_STUB(sceHmdReprojectionFinalizeCapture)
PS4_HLE_STUB(sceHmdReprojectionInitialize)
PS4_HLE_STUB(sceHmdReprojectionInitializeCapture)
PS4_HLE_STUB(sceHmdReprojectionSetCallback)
PS4_HLE_STUB(sceHmdReprojectionSetDisplayBuffers)
PS4_HLE_STUB(sceHmdReprojectionSetOutputMinColor)
PS4_HLE_STUB(sceHmdReprojectionSetUserEventEnd)
PS4_HLE_STUB(sceHmdReprojectionSetUserEventStart)
PS4_HLE_STUB(sceHmdReprojectionStart)
PS4_HLE_STUB(sceHmdReprojectionStart2dVr)
PS4_HLE_STUB(sceHmdReprojectionStartCapture)
PS4_HLE_STUB(sceHmdReprojectionStartLiveCapture)
PS4_HLE_STUB(sceHmdReprojectionStartMultilayer2)
PS4_HLE_STUB(sceHmdReprojectionStartWideNear)
PS4_HLE_STUB(sceHmdReprojectionStartWideNearWithOverlay)
PS4_HLE_STUB(sceHmdReprojectionStartWithOverlay)
PS4_HLE_STUB(sceHmdReprojectionStop)
PS4_HLE_STUB(sceHmdReprojectionStopCapture)
PS4_HLE_STUB(sceHmdReprojectionStopLiveCapture)
PS4_HLE_STUB(sceHmdReprojectionUnsetCallback)
PS4_HLE_STUB(sceHmdReprojectionUnsetDisplayBuffers)
PS4_HLE_STUB(Func_A31A0320D80EAD99)
PS4_HLE_STUB(Func_B9A6FA0735EC7E49)
