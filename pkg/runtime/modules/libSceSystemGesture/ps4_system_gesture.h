// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_SYSTEM_GESTURE_H
#define PS4_SYSTEM_GESTURE_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void shim_sceSystemGestureOpen(GuestContext *ctx);
void shim_sceSystemGestureClose(GuestContext *ctx);
void shim_sceSystemGestureCreateTouchRecognizer(GuestContext *ctx);
void shim_sceSystemGestureRemoveTouchRecognizer(GuestContext *ctx);
void shim_sceSystemGestureAppendTouchRecognizer(GuestContext *ctx);
void shim_sceSystemGestureInitializePrimitiveTouchRecognizer(GuestContext *ctx);
void shim_sceSystemGestureFinalizePrimitiveTouchRecognizer(GuestContext *ctx);
void shim_sceSystemGestureUpdatePrimitiveTouchRecognizer(GuestContext *ctx);
void shim_sceSystemGestureUpdateAllTouchRecognizer(GuestContext *ctx);
void shim_sceSystemGestureGetTouchEventByIndex(GuestContext *ctx);
void shim_sceSystemGestureGetTouchEventsCount(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SYSTEM_GESTURE_H
