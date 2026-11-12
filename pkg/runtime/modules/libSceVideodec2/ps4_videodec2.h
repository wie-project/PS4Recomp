// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_VIDEODEC2_H
#define PS4_VIDEODEC2_H

#include "../../core/recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void shim_sceVideodec2QueryComputeMemoryInfo(GuestContext *ctx);
void shim_sceVideodec2AllocateComputeQueue(GuestContext *ctx);
void shim_sceVideodec2ReleaseComputeQueue(GuestContext *ctx);
void shim_sceVideodec2QueryDecoderMemoryInfo(GuestContext *ctx);
void shim_sceVideodec2QueryHevcDecoderMemoryInfo(GuestContext *ctx);
void shim_sceVideodec2CreateDecoder(GuestContext *ctx);
void shim_sceVideodec2CreateHevcDecoder(GuestContext *ctx);
void shim_sceVideodec2DeleteDecoder(GuestContext *ctx);
void shim_sceVideodec2Decode(GuestContext *ctx);
void shim_sceVideodec2Flush(GuestContext *ctx);
void shim_sceVideodec2Reset(GuestContext *ctx);
void shim_sceVideodec2GetPictureInfo(GuestContext *ctx);
void shim_sceVideodec2GetAvcPictureInfo(GuestContext *ctx);
void shim_sceVideodec2GetHevcPictureInfo(GuestContext *ctx);
void shim_sceVideodec2MapDirectMemory(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_VIDEODEC2_H
