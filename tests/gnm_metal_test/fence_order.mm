// SPDX-License-Identifier: GPL-2.0-or-later
// Real runtime/PM4 ordering regression; run with scripts/test-gnm-fence-order.sh.
#import <Metal/Metal.h>
#include "recomp_runtime.h"
#include "ps4_gnm_draw.h"
#include "ps4_gnm_metal.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
static void Check(bool ok, const char* message) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); std::abort(); }
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    @autoreleasepool {
        const unsigned op = (unsigned)strtoul(argv[1], nullptr, 16), bits = atoi(argv[2]);
        if (bits != 32 && bits != 64) return 2;
        GuestContext* ctx = recomp_init_runtime(0x20000000, nullptr, 0, "fence_order");
        Check(ctx && ps4draw::Init(ctx), "real runtime and Metal backend initialized");
        Check(ps4_gnm_metal_init(ctx) == 0, "command processor initialized");
        const uint64_t memory = recomp_vm_alloc(ctx, 0x10000);
        Check(memory != 0, "guest allocation available");
        const uint64_t label = memory + 4096, destination = memory + 8192;
        memset(ctx->mem_base + label, 0, 16);
        memset(ctx->mem_base + destination, 0, 4096);
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLSharedEvent> gate = [device newSharedEvent];
        Check(gate != nil, "GPU gate created");
        ps4draw::DebugGpuWait((__bridge void*)gate, 1);
        Check(ps4draw::GpuDma(false, true, 0, 0x5a5a5a5a, false, destination, 4096), "real GPU fill queued");
        uint32_t* packet = (uint32_t*)(ctx->mem_base + memory);
        const uint32_t lo = 0x12345678, hi = 0x9abcdef0;
        unsigned payload;
        if (op == 0x47) {
            payload = 5;
            packet[1] = 0x14; packet[2] = (uint32_t)label;
            packet[3] = (uint32_t)(label >> 32) | ((bits == 64 ? 2u : 1u) << 29);
            packet[4] = lo; packet[5] = hi;
        } else if (op == 0x49) {
            payload = 6;
            packet[1] = 0x14; packet[2] = (bits == 64 ? 2u : 1u) << 29;
            packet[3] = (uint32_t)label; packet[4] = (uint32_t)(label >> 32);
            packet[5] = lo; packet[6] = hi;
        } else if (op == 0x48) {
            payload = bits == 64 ? 5 : 4;
            packet[1] = 0x2f; packet[2] = (uint32_t)label;
            packet[3] = (uint32_t)(label >> 32) | (2u << 29);
            packet[4] = lo; packet[5] = hi;
        } else return 2;
        packet[0] = 0xc0000000u | ((payload - 1) << 16) | (op << 8);
        const uint32_t size = (payload + 1) * 4;
        Check(ps4_gnm_metal_process_command_buffers(ctx, 1, &memory, &size, nullptr, nullptr) == 0,
              "PM4 completion packet processed");
        ps4draw::Flush(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        Check(__atomic_load_n((uint64_t*)(ctx->mem_base + label), __ATOMIC_ACQUIRE) == 0,
              "completion label remains hidden while preceding GPU work is gated");
        Check(*(uint32_t*)(ctx->mem_base + destination) == 0, "GPU work has not bypassed the gate");
        gate.signaledValue = 1;
        ps4draw::Flush(true);
        const uint64_t expected = lo | (bits == 64 ? (uint64_t)hi << 32 : 0);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (__atomic_load_n((uint64_t*)(ctx->mem_base + label), __ATOMIC_ACQUIRE) != expected &&
               std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        Check(__atomic_load_n((uint64_t*)(ctx->mem_base + label), __ATOMIC_ACQUIRE) == expected,
              "completion label published after GPU completion");
        for (unsigned i = 0; i < 4096; ++i)
            Check(ctx->mem_base[destination + i] == 0x5a, "GPU fill visible before completion label");
        printf("PASS packet=%02x bits=%u GPU fill and completion order\n", op, bits);
        // Backend teardown precedes guest-memory teardown at normal process exit.
    }
}
