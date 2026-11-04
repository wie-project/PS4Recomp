// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_razor_cpu.h"
#include <stdio.h>

void shim_sceRazorCpuInit(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceRazorCpuShutdown(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceRazorCpuPushMarker(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceRazorCpuPushMarkerStatic(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceRazorCpuPopMarker(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceRazorCpuNamedSync(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceRazorCpuPlotValue(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceRazorCpuSync(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

void shim_sceRazorCpuIsCapturing(GuestContext *ctx) {
  ctx->rax = 0; // false
  SHIM_RETURN();
}
