// SPDX-License-Identifier: GPL-2.0-or-later

// libSceRazorCpu: the Razor CPU profiler of the PS4 SDK. No profiler is attached on a desktop
// host, so markers, tags and captures have no effect, as in shadPS4; sceRazorCpuIsCapturing and
// sceRazorCpuFlushOccurred report false (0), and the void functions leave rax at 0.

#include "ps4_razor_cpu.h"
#include "ps4_hle_stub.h"

PS4_HLE_STUB(sceRazorCpuBeginLogicalFileAccess)
PS4_HLE_STUB(sceRazorCpuDisableFiberUserMarkers)
PS4_HLE_STUB(sceRazorCpuEndLogicalFileAccess)
PS4_HLE_STUB(sceRazorCpuFiberLogNameChange)
PS4_HLE_STUB(sceRazorCpuFiberSwitch)
PS4_HLE_STUB(sceRazorCpuFlushOccurred)
PS4_HLE_STUB(sceRazorCpuGetDataTagStorageSize)
PS4_HLE_STUB(sceRazorCpuGpuMarkerSync)
PS4_HLE_STUB(sceRazorCpuInitDataTags)
PS4_HLE_STUB(sceRazorCpuInitializeGpuMarkerContext)
PS4_HLE_STUB(sceRazorCpuInitializeInternal)
PS4_HLE_STUB(sceRazorCpuIsCapturing)
PS4_HLE_STUB(sceRazorCpuJobManagerDispatch)
PS4_HLE_STUB(sceRazorCpuJobManagerJob)
PS4_HLE_STUB(sceRazorCpuJobManagerSequence)
PS4_HLE_STUB(sceRazorCpuNamedSync)
PS4_HLE_STUB(sceRazorCpuPlotValue)
PS4_HLE_STUB(sceRazorCpuPopMarker)
PS4_HLE_STUB(sceRazorCpuPushMarker)
PS4_HLE_STUB(sceRazorCpuPushMarkerStatic)
PS4_HLE_STUB(sceRazorCpuResizeTaggedBuffer)
PS4_HLE_STUB(sceRazorCpuSetPopMarkerCallback)
PS4_HLE_STUB(sceRazorCpuSetPushMarkerCallback)
PS4_HLE_STUB(sceRazorCpuSetPushMarkerStaticCallback)
PS4_HLE_STUB(sceRazorCpuShutdownDataTags)
PS4_HLE_STUB(sceRazorCpuStartCaptureInternal)
PS4_HLE_STUB(sceRazorCpuStopCaptureInternal)
PS4_HLE_STUB(sceRazorCpuSync)
PS4_HLE_STUB(sceRazorCpuTagArray)
PS4_HLE_STUB(sceRazorCpuTagBuffer)
PS4_HLE_STUB(sceRazorCpuUnTagBuffer)
// shadPS4 registers this NID (Crha9LvwvJM) under the name sceRazorCpuWorkloadRunBegin.
PS4_HLE_STUB(sceRazorCpuWorkloadRunBeginEx)
PS4_HLE_STUB(sceRazorCpuWorkloadRunEnd)
PS4_HLE_STUB(sceRazorCpuWorkloadSubmit)
PS4_HLE_STUB(sceRazorCpuWriteBookmark)
// Not exported by shadPS4's module; an import it does not register resolves to its generic
// stub, which returns 0 as well.
PS4_HLE_STUB(sceRazorCpuShutdown)
