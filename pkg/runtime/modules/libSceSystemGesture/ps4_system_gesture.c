// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_system_gesture.h"

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
