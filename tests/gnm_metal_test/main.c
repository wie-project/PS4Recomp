// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Native Apple Metal GNM Command Processor and Hardware State Test.
 */

#include "recomp_runtime.h"
#include "ps4_gnmdriver.h"
#include "ps4_gnm_metal.h"
#include "ps4_videoout.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("[gnm-test] Initializing recomp runtime...\n");
    GuestContext *ctx = recomp_init_runtime(0x20000000, NULL, 0, "gnm_metal_test");
    assert(ctx != NULL);
    assert(ctx->mem_base != NULL);

    // 1. Tessellation factor ring buffer query & 64KB alignment
    printf("[gnm-test] Testing tessellation ring buffer allocation...\n");
    ps4_gnmdriver_init(ctx);
    uint64_t ring_addr = sceGnmGetTheTessellationFactorRingBufferBaseAddress();
    assert(ring_addr != 0);
    assert((ring_addr & 0xFFFF) == 0); // 64KB aligned
    printf("           Tessellation ring buffer VAddr: 0x%llx (OK)\n", (unsigned long long)ring_addr);

    // 2. Hardware clock frequency query
    printf("[gnm-test] Testing GPU clock frequency query...\n");
    uint32_t clock_mhz = sceGnmGetGpuCoreClockFrequency();
    assert(clock_mhz == 800);
    printf("           GPU Core Clock: %u MHz (OK)\n", clock_mhz);

    // 3. Hardware state initialization packets
    printf("[gnm-test] Testing PM4 hardware state init packets...\n");
    uint64_t cmdbuf_vaddr = recomp_vm_alloc(ctx, 0x10000);
    assert(cmdbuf_vaddr != 0);
    uint32_t *cmdbuf = (uint32_t *)(ctx->mem_base + cmdbuf_vaddr);

    int32_t written = sceGnmDrawInitDefaultHardwareState350(cmdbuf, 0x10000 / 4);
    assert(written > 0);
    // Verify IT_CLEAR_STATE preamble
    assert(cmdbuf[0] == 0xc0001200u);
    printf("           Generated %d DWORDs of Liverpool init sequence (OK)\n", written);

    // 4. PM4 draw packet encoding
    printf("[gnm-test] Testing PM4 DrawIndexAuto packet encoding...\n");
    uint32_t *draw_buf = cmdbuf + written;
    int32_t draw_res = sceGnmDrawIndexAuto(draw_buf, 7, 36, 0);
    assert(draw_res == ORBIS_GNM_OK);
    assert(((draw_buf[0] >> 8) & 0xFF) == PM4_IT_DRAW_INDEX_AUTO);
    assert(draw_buf[1] == 36);
    printf("           Encoded IT_DRAW_INDEX_AUTO packet (OK)\n");

    // 5. Metal Command Processor Initialization & PM4 submission
    printf("[gnm-test] Testing Metal Command Processor dispatch...\n");
    int metal_init = ps4_gnm_metal_init(ctx);
    assert(metal_init == 0);

    uint64_t dcb_addrs[1] = { cmdbuf_vaddr };
    uint32_t dcb_sizes[1] = { (uint32_t)((written + 7) * 4) };

    int32_t submit_res = sceGnmSubmitCommandBuffers(ctx, 1, dcb_addrs, dcb_sizes, NULL, NULL);
    assert(submit_res == ORBIS_GNM_OK);
    printf("           Submitted DCB packet stream to Metal Command Processor (OK)\n");

    // 6. SubmitDone & Synchronization
    printf("[gnm-test] Testing Metal synchronization and GPU wait idle...\n");
    ps4_gnm_metal_wait_gpu_idle();
    int32_t done_res = sceGnmSubmitDone();
    assert(done_res == ORBIS_GNM_OK);
    printf("           SubmitDone & GPU idle sync completed (OK)\n");

    // 7. VideoOut Submit & Flip Integration
    printf("[gnm-test] Testing SubmitAndFlip integration...\n");
    int32_t vo_handle = sceVideoOutOpen(0, 0, 0, NULL);
    assert(vo_handle >= 0);

    uint64_t fb_vaddr = recomp_vm_alloc(ctx, 1920 * 1080 * 4);
    assert(fb_vaddr != 0);
    void *fb_ptrs[1] = { (void *)fb_vaddr };

    OrbisVideoOutBufferAttribute attr;
    memset(&attr, 0, sizeof(attr));
    attr.width = 1920;
    attr.height = 1080;
    attr.pixelPitch = 1920;
    int32_t reg_res = sceVideoOutRegisterBuffers(vo_handle, 0, fb_ptrs, 1, &attr);
    assert(reg_res == 0);

    int32_t flip_res = sceGnmSubmitAndFlipCommandBuffers(ctx, 1, dcb_addrs, dcb_sizes, NULL, NULL,
                                                         (uint32_t)vo_handle, 0, 0, 100);
    assert(flip_res == ORBIS_GNM_OK);
    printf("           SubmitAndFlip executed successfully (OK)\n");

    sceVideoOutClose(vo_handle);
    recomp_free_runtime(ctx);
    printf("\n>>> ALL GNM METAL TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
