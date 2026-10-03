// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4 GNM Metal Graphics Backend for PS4Recomp.
 * Translates AMD GCN Liverpool PM4 command streams directly to Apple Metal API.
 */

#ifndef PS4_GNM_METAL_H
#define PS4_GNM_METAL_H

#include "recomp_runtime.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the Metal Command Processor subsystem
int ps4_gnm_metal_init(GuestContext *ctx);

// Process a batch of PM4 Draw Command Buffers (DCB) and Constant Command Buffers (CCB)
int32_t ps4_gnm_metal_process_command_buffers(GuestContext *ctx, uint32_t count,
                                             const uint64_t *dcb_gpu_addrs, const uint32_t *dcb_sizes_in_bytes,
                                             const uint64_t *ccb_gpu_addrs, const uint32_t *ccb_sizes_in_bytes);

// Synchronize / wait for GPU to become idle
void ps4_gnm_metal_wait_gpu_idle(void);

// Shut down Metal Command Processor
void ps4_gnm_metal_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // PS4_GNM_METAL_H
