// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_zlib.h"
#include "../../core/recomp_context.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <pthread.h>
#include <queue>
#include <thread>
#include <unordered_map>
#include <zlib.h>

namespace {

struct InflateTask {
    uint64_t request_id = 0;
    const void *src = nullptr;
    uint32_t src_length = 0;
    void *dst = nullptr;
    uint32_t dst_length = 0;
};

struct InflateResult {
    uint32_t length = 0;
    int32_t status = 0;
};

std::mutex g_mutex;
std::condition_variable g_task_cv;
std::condition_variable g_done_cv;
std::thread g_worker;
bool g_stop = false;
bool g_initialized = false;
std::queue<InflateTask> g_tasks;
std::queue<uint64_t> g_done;
std::unordered_map<uint64_t, InflateResult> g_results;
uint64_t g_next_request_id = 1;

void *guest_ptr(const GuestContext *ctx, uint64_t addr, size_t size) {
    if (!ctx || !ctx->mem_base || addr == 0) {
        return nullptr;
    }
    const uintptr_t base = (uintptr_t)ctx->mem_base;
    if (addr >= base && addr < base + ctx->mem_size) {
        const size_t offset = (size_t)(addr - base);
        if (size <= ctx->mem_size - offset) {
            return (void *)(uintptr_t)addr;
        }
        return nullptr;
    }
    if (addr < ctx->mem_size && size <= ctx->mem_size - (size_t)addr) {
        return (void *)(ctx->mem_base + addr);
    }
    return nullptr;
}

void zlib_task_thread(void) {
#ifdef __APPLE__
    pthread_setname_np("ps4:ZlibTaskThread");
#endif
    while (true) {
        InflateTask task;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_task_cv.wait(lock, [] { return g_stop || !g_tasks.empty(); });
            if (g_stop && g_tasks.empty()) {
                break;
            }
            task = g_tasks.front();
            g_tasks.pop();
        }

        uLongf decompressed_length = (uLongf)task.dst_length;
        const int ret = uncompress(static_cast<Bytef *>(task.dst), &decompressed_length,
                                   static_cast<const Bytef *>(task.src), (uLong)task.src_length);

        const int32_t status = (ret == Z_OK)        ? 0
                             : (ret == Z_BUF_ERROR) ? ORBIS_ZLIB_ERROR_NOSPACE
                                                    : ORBIS_ZLIB_ERROR_FATAL;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_results[task.request_id] = InflateResult{
                .length = static_cast<uint32_t>(decompressed_length),
                .status = status,
            };
            g_done.push(task.request_id);
        }
        g_done_cv.notify_one();
    }
}

} // namespace

extern "C" {

int32_t sceZlibInitialize(const void *buffer, uint32_t length) {
    (void)buffer;
    (void)length;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) {
        return ORBIS_ZLIB_ERROR_ALREADY_INITIALIZED;
    }

    g_tasks = std::queue<InflateTask>();
    g_done = std::queue<uint64_t>();
    g_results.clear();
    g_next_request_id = 1;
    g_stop = false;
    g_initialized = true;
    g_worker = std::thread(zlib_task_thread);
    return 0;
}

int32_t sceZlibInflate(const void *src, uint32_t src_len, void *dst, uint32_t dst_len,
                       uint64_t *request_id) {
    constexpr uint32_t kMaxDstLen = 64u << 10;
    constexpr uint32_t kDstAlign = 2u << 10;

    if (!src || !src_len || !dst || !dst_len || !request_id ||
        dst_len > kMaxDstLen || (dst_len % kDstAlign) != 0) {
        return ORBIS_ZLIB_ERROR_INVALID;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        return ORBIS_ZLIB_ERROR_NOT_INITIALIZED;
    }

    const uint64_t req_id = g_next_request_id++;
    *request_id = req_id;
    g_tasks.push(InflateTask{
        .request_id = req_id,
        .src = src,
        .src_length = src_len,
        .dst = dst,
        .dst_length = dst_len,
    });
    g_task_cv.notify_one();
    return 0;
}

int32_t sceZlibWaitForDone(uint64_t *request_id, const uint32_t *timeout) {
    if (!request_id) {
        return ORBIS_ZLIB_ERROR_INVALID;
    }

    std::unique_lock<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        return ORBIS_ZLIB_ERROR_NOT_INITIALIZED;
    }

    const auto pred = [] { return g_stop || !g_done.empty(); };
    if (timeout) {
        if (!g_done_cv.wait_for(lock, std::chrono::milliseconds(*timeout), pred)) {
            return ORBIS_ZLIB_ERROR_TIMEDOUT;
        }
    } else {
        g_done_cv.wait(lock, pred);
    }

    if (g_done.empty()) {
        return ORBIS_ZLIB_ERROR_NOT_INITIALIZED;
    }

    *request_id = g_done.front();
    g_done.pop();
    return 0;
}

int32_t sceZlibGetResult(uint64_t request_id, uint32_t *dst_length, int32_t *status) {
    if (!dst_length || !status) {
        return ORBIS_ZLIB_ERROR_INVALID;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        return ORBIS_ZLIB_ERROR_NOT_INITIALIZED;
    }

    auto it = g_results.find(request_id);
    if (it == g_results.end()) {
        return ORBIS_ZLIB_ERROR_NOT_FOUND;
    }

    *dst_length = it->second.length;
    *status = it->second.status;
    return 0;
}

int32_t sceZlibFinalize(void) {
    std::thread worker_to_join;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_initialized) {
            return ORBIS_ZLIB_ERROR_NOT_INITIALIZED;
        }
        g_stop = true;
        g_initialized = false;
        worker_to_join = std::move(g_worker);
    }
    g_task_cv.notify_all();
    g_done_cv.notify_all();
    if (worker_to_join.joinable()) {
        worker_to_join.join();
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    g_tasks = std::queue<InflateTask>();
    g_done = std::queue<uint64_t>();
    g_results.clear();
    return 0;
}

void shim_sceZlibInitialize(GuestContext *ctx) {
    const void *buffer = guest_ptr(ctx, ctx->rdi, (size_t)(uint32_t)ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceZlibInitialize(buffer, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceZlibInflate(GuestContext *ctx) {
    const uint32_t src_len = (uint32_t)ctx->rsi;
    const uint32_t dst_len = (uint32_t)ctx->rcx;
    const void *src = ctx->rdi ? guest_ptr(ctx, ctx->rdi, src_len) : nullptr;
    void *dst = ctx->rdx ? guest_ptr(ctx, ctx->rdx, dst_len) : nullptr;
    uint64_t *request_id = ctx->r8 ? static_cast<uint64_t *>(guest_ptr(ctx, ctx->r8, sizeof(uint64_t))) : nullptr;

    int32_t rc;
    if ((ctx->rdi && !src) || (ctx->rdx && !dst) || (ctx->r8 && !request_id)) {
        rc = ORBIS_ZLIB_ERROR_FAULT;
    } else {
        rc = sceZlibInflate(src, src_len, dst, dst_len, request_id);
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceZlibWaitForDone(GuestContext *ctx) {
    uint64_t *request_id = ctx->rdi ? static_cast<uint64_t *>(guest_ptr(ctx, ctx->rdi, sizeof(uint64_t))) : nullptr;
    const uint32_t *timeout = ctx->rsi ? static_cast<const uint32_t *>(guest_ptr(ctx, ctx->rsi, sizeof(uint32_t))) : nullptr;

    int32_t rc;
    if ((ctx->rdi && !request_id) || (ctx->rsi && !timeout)) {
        rc = ORBIS_ZLIB_ERROR_FAULT;
    } else {
        rc = sceZlibWaitForDone(request_id, timeout);
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceZlibGetResult(GuestContext *ctx) {
    uint32_t *dst_length = ctx->rsi ? static_cast<uint32_t *>(guest_ptr(ctx, ctx->rsi, sizeof(uint32_t))) : nullptr;
    int32_t *status = ctx->rdx ? static_cast<int32_t *>(guest_ptr(ctx, ctx->rdx, sizeof(int32_t))) : nullptr;

    int32_t rc;
    if ((ctx->rsi && !dst_length) || (ctx->rdx && !status)) {
        rc = ORBIS_ZLIB_ERROR_FAULT;
    } else {
        rc = sceZlibGetResult(ctx->rdi, dst_length, status);
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceZlibFinalize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceZlibFinalize();
    SHIM_RETURN();
}

} // extern "C"
