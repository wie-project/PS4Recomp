// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_RAZOR_CPU_H
#define PS4_RAZOR_CPU_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void shim_sceRazorCpuBeginLogicalFileAccess(GuestContext *ctx);
void shim_sceRazorCpuDisableFiberUserMarkers(GuestContext *ctx);
void shim_sceRazorCpuEndLogicalFileAccess(GuestContext *ctx);
void shim_sceRazorCpuFiberLogNameChange(GuestContext *ctx);
void shim_sceRazorCpuFiberSwitch(GuestContext *ctx);
void shim_sceRazorCpuFlushOccurred(GuestContext *ctx);
void shim_sceRazorCpuGetDataTagStorageSize(GuestContext *ctx);
void shim_sceRazorCpuGpuMarkerSync(GuestContext *ctx);
void shim_sceRazorCpuInitDataTags(GuestContext *ctx);
void shim_sceRazorCpuInitializeGpuMarkerContext(GuestContext *ctx);
void shim_sceRazorCpuInitializeInternal(GuestContext *ctx);
void shim_sceRazorCpuIsCapturing(GuestContext *ctx);
void shim_sceRazorCpuJobManagerDispatch(GuestContext *ctx);
void shim_sceRazorCpuJobManagerJob(GuestContext *ctx);
void shim_sceRazorCpuJobManagerSequence(GuestContext *ctx);
void shim_sceRazorCpuNamedSync(GuestContext *ctx);
void shim_sceRazorCpuPlotValue(GuestContext *ctx);
void shim_sceRazorCpuPopMarker(GuestContext *ctx);
void shim_sceRazorCpuPushMarker(GuestContext *ctx);
void shim_sceRazorCpuPushMarkerStatic(GuestContext *ctx);
void shim_sceRazorCpuResizeTaggedBuffer(GuestContext *ctx);
void shim_sceRazorCpuSetPopMarkerCallback(GuestContext *ctx);
void shim_sceRazorCpuSetPushMarkerCallback(GuestContext *ctx);
void shim_sceRazorCpuSetPushMarkerStaticCallback(GuestContext *ctx);
void shim_sceRazorCpuShutdownDataTags(GuestContext *ctx);
void shim_sceRazorCpuStartCaptureInternal(GuestContext *ctx);
void shim_sceRazorCpuStopCaptureInternal(GuestContext *ctx);
void shim_sceRazorCpuSync(GuestContext *ctx);
void shim_sceRazorCpuTagArray(GuestContext *ctx);
void shim_sceRazorCpuTagBuffer(GuestContext *ctx);
void shim_sceRazorCpuUnTagBuffer(GuestContext *ctx);
void shim_sceRazorCpuWorkloadRunBeginEx(GuestContext *ctx);
void shim_sceRazorCpuWorkloadRunEnd(GuestContext *ctx);
void shim_sceRazorCpuWorkloadSubmit(GuestContext *ctx);
void shim_sceRazorCpuWriteBookmark(GuestContext *ctx);
void shim_sceRazorCpuShutdown(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_RAZOR_CPU_H
