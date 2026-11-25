// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_system_gesture.h"
#include "ps4_hle_stub.h"

void shim_sceSystemGestureOpen(GuestContext *ctx) {
  ctx->rax = 0; // ORBIS_OK
  SHIM_RETURN();
}

void shim_sceSystemGestureClose(GuestContext *ctx) {
  ctx->rax = 0; // ORBIS_OK
  SHIM_RETURN();
}

void shim_sceSystemGestureCreateTouchRecognizer(GuestContext *ctx) {
  ctx->rax = 0; // ORBIS_OK
  SHIM_RETURN();
}

void shim_sceSystemGestureRemoveTouchRecognizer(GuestContext *ctx) {
  ctx->rax = 0; // ORBIS_OK
  SHIM_RETURN();
}

void shim_sceSystemGestureAppendTouchRecognizer(GuestContext *ctx) {
  ctx->rax = 0; // ORBIS_OK
  SHIM_RETURN();
}

void shim_sceSystemGestureInitializePrimitiveTouchRecognizer(GuestContext *ctx) {
  ctx->rax = 0; // ORBIS_OK
  SHIM_RETURN();
}

void shim_sceSystemGestureFinalizePrimitiveTouchRecognizer(GuestContext *ctx) {
  ctx->rax = 0; // ORBIS_OK
  SHIM_RETURN();
}

void shim_sceSystemGestureUpdatePrimitiveTouchRecognizer(GuestContext *ctx) {
  ctx->rax = 0; // ORBIS_OK
  SHIM_RETURN();
}

void shim_sceSystemGestureUpdateAllTouchRecognizer(GuestContext *ctx) {
  ctx->rax = 0; // ORBIS_OK
  SHIM_RETURN();
}

void shim_sceSystemGestureGetTouchEventByIndex(GuestContext *ctx) {
  ctx->rax = 0; // ORBIS_OK
  SHIM_RETURN();
}

void shim_sceSystemGestureGetTouchEventsCount(GuestContext *ctx) {
  ctx->rax = 0; // 0 events
  SHIM_RETURN();
}

// Functions that shadPS4 stubs as well (src/core/libraries, libSceSystemGesture).
PS4_HLE_STUB(sceSystemGestureGetPrimitiveTouchEventByIndex)
PS4_HLE_STUB(sceSystemGestureGetPrimitiveTouchEventByPrimitiveID)
PS4_HLE_STUB(sceSystemGestureGetPrimitiveTouchEvents)
PS4_HLE_STUB(sceSystemGestureGetPrimitiveTouchEventsCount)
PS4_HLE_STUB(sceSystemGestureGetTouchEventByEventID)
PS4_HLE_STUB(sceSystemGestureGetTouchEvents)
PS4_HLE_STUB(sceSystemGestureGetTouchRecognizerInformation)
PS4_HLE_STUB(sceSystemGestureResetPrimitiveTouchRecognizer)
PS4_HLE_STUB(sceSystemGestureResetTouchRecognizer)
PS4_HLE_STUB(sceSystemGestureUpdateTouchRecognizer)
PS4_HLE_STUB(sceSystemGestureUpdateTouchRecognizerRectangle)
