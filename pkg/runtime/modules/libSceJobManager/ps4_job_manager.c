#include "ps4_job_manager.h"
#include <stdio.h>
#include <string.h>

static uint64_t g_job_factory_addr = 0;
static uint64_t g_job_vtable_addr = 0;
static uint64_t g_job_stub_fn_addr = 0;

static void shim_job_factory_noop(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

static void ensure_job_factory(GuestContext *ctx, uint64_t base_mem) {
    if (g_job_factory_addr != 0) return;
    if (base_mem != 0 && ctx->mem_base && base_mem + 1024 <= ctx->mem_size) {
        g_job_factory_addr = base_mem;
        g_job_vtable_addr = base_mem + 64;
    } else {
        uint64_t alloc = recomp_vm_alloc(ctx, 4096);
        if (alloc == (uint64_t)-1) return;
        g_job_factory_addr = alloc;
        g_job_vtable_addr = alloc + 64;
    }
    g_job_stub_fn_addr = g_job_vtable_addr + 512;
    recomp_register_fn(g_job_stub_fn_addr, (recomp_fn_t)shim_job_factory_noop);

    uint64_t *vtable = (uint64_t *)(ctx->mem_base + g_job_vtable_addr);
    for (int i = 0; i < 32; i++) {
        vtable[i] = g_job_stub_fn_addr;
    }
    uint64_t *obj = (uint64_t *)(ctx->mem_base + g_job_factory_addr);
    obj[0] = g_job_vtable_addr;
}

void shim__ZN3sce3Job10JobManagerC1Ev(GuestContext *ctx) {
    uint64_t this_ptr = ctx->rdi;
    printf("[ps4-job] JobManager::JobManager(this=0x%llx)\n", (unsigned long long)this_ptr);
    if (this_ptr && ctx->mem_base && this_ptr + 128 <= ctx->mem_size) {
        memset(ctx->mem_base + this_ptr, 0, 128);
    }
    ctx->rax = this_ptr;
    SHIM_RETURN();
}

void shim__ZN3sce3Job10JobManagerC2Ev(GuestContext *ctx) {
    shim__ZN3sce3Job10JobManagerC1Ev(ctx);
}

void shim__ZN3sce3Job10JobManagerD1Ev(GuestContext *ctx) {
    uint64_t this_ptr = ctx->rdi;
    printf("[ps4-job] JobManager::~JobManager(this=0x%llx)\n", (unsigned long long)this_ptr);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__ZN3sce3Job10JobManagerD2Ev(GuestContext *ctx) {
    shim__ZN3sce3Job10JobManagerD1Ev(ctx);
}

void shim__ZN3sce3Job10JobManager27calculateRequiredMemorySizeEPKNS1_21MemorySizeQueryParamsE(GuestContext *ctx) {
    uint64_t params_addr = ctx->rdi;
    uint64_t req = 2ULL * 1024 * 1024; // 2 MB baseline
    if (params_addr && ctx->mem_base && params_addr + sizeof(uint32_t) <= ctx->mem_size) {
        uint32_t count = *(uint32_t *)(ctx->mem_base + params_addr);
        if (count > 0 && count < 0x100000) {
            uint64_t calculated = (uint64_t)count * 1024ULL + 0x100000ULL;
            if (calculated > req) req = calculated;
        }
    }
    req = (req + 0xffffULL) & ~0xffffULL; // 64KB align
    printf("[ps4-job] JobManager::calculateRequiredMemorySize: params=0x%llx -> %llu bytes (%.2f MB)\n",
           (unsigned long long)params_addr, (unsigned long long)req, (double)req / (1024.0 * 1024.0));
    ctx->rax = req;
    SHIM_RETURN();
}

void shim__ZN3sce3Job10JobManager10initializeEPvmjjjmPKNS1_12ConfigParamsE(GuestContext *ctx) {
    uint64_t this_ptr = ctx->rdi;
    uint64_t mem_ptr = ctx->rsi;
    uint64_t mem_size = ctx->rdx;
    uint32_t num_workers = (uint32_t)ctx->rcx;
    uint32_t max_jobs = (uint32_t)ctx->r8;
    uint32_t max_sequences = (uint32_t)ctx->r9;
    uint64_t rsp = ctx->rsp;
    uint64_t shared_mem_size = 0;
    uint64_t config_ptr = 0;
    if (rsp && ctx->mem_base && rsp + 16 <= ctx->mem_size) {
        shared_mem_size = *(uint64_t *)(ctx->mem_base + rsp + 8);
        config_ptr = *(uint64_t *)(ctx->mem_base + rsp + 16);
    }
    printf("[ps4-job] JobManager::initialize: this=0x%llx mem=0x%llx sz=%llu workers=%u jobs=%u seqs=%u sharedSz=%llu cfg=0x%llx\n",
           (unsigned long long)this_ptr, (unsigned long long)mem_ptr,
           (unsigned long long)mem_size, num_workers, max_jobs, max_sequences,
           (unsigned long long)shared_mem_size, (unsigned long long)config_ptr);

    ensure_job_factory(ctx, mem_ptr);

    if (this_ptr && ctx->mem_base && this_ptr + sizeof(uint64_t) <= ctx->mem_size) {
        *(uint64_t *)(ctx->mem_base + this_ptr) = g_job_factory_addr;
    }
    ctx->rax = 0; // ORBIS_OK
    SHIM_RETURN();
}

void shim__ZN3sce3Job10JobManager24setPersistentThreadCountEj(GuestContext *ctx) {
    uint64_t this_ptr = ctx->rdi;
    uint32_t count = (uint32_t)ctx->rsi;
    printf("[ps4-job] JobManager::setPersistentThreadCount: this=0x%llx count=%u\n",
           (unsigned long long)this_ptr, count);
    ctx->rax = 0; // ORBIS_OK
    SHIM_RETURN();
}

void shim__ZN3sce3Job10JobManager27getSequenceFactoryInterfaceENS1_8PriorityE(GuestContext *ctx) {
    uint64_t this_ptr = ctx->rdi;
    uint32_t priority = (uint32_t)ctx->rsi;
    printf("[ps4-job] JobManager::getSequenceFactoryInterface: this=0x%llx priority=%u\n",
           (unsigned long long)this_ptr, priority);

    ensure_job_factory(ctx, 0);

    ctx->rax = g_job_factory_addr ? g_job_factory_addr : this_ptr;
    SHIM_RETURN();
}

void shim__ZN3sce3Job10JobManager8shutdownEv(GuestContext *ctx) {
    uint64_t this_ptr = ctx->rdi;
    printf("[ps4-job] JobManager::shutdown: this=0x%llx\n", (unsigned long long)this_ptr);
    ctx->rax = 0; // ORBIS_OK
    SHIM_RETURN();
}
