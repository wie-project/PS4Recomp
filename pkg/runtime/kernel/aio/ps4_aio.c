// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_aio.h"
#include "recomp_runtime.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ORBIS_KERNEL_AIO_MAX_QUEUES 512

#define ORBIS_KERNEL_ERROR_EFAULT   0x8002000E
#define ORBIS_KERNEL_ERROR_EINVAL   0x80020016
#define ORBIS_KERNEL_ERROR_ETIMEDOUT 0x8002003C

static pthread_mutex_t g_aio_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t *g_id_state = NULL;
static int32_t g_id_index = 1;

void ps4_aio_init(void) {
    pthread_mutex_lock(&g_aio_mutex);
    if (!g_id_state) {
        g_id_state = (int32_t *)calloc(ORBIS_KERNEL_AIO_MAX_QUEUES, sizeof(int32_t));
        g_id_index = 1;
    }
    pthread_mutex_unlock(&g_aio_mutex);
}

void ps4_aio_destroy(void) {
    pthread_mutex_lock(&g_aio_mutex);
    if (g_id_state) {
        free(g_id_state);
        g_id_state = NULL;
    }
    g_id_index = 1;
    pthread_mutex_unlock(&g_aio_mutex);
}

static inline void aio_ensure_init(void) {
    if (!g_id_state) {
        ps4_aio_init();
    }
}

// sceKernelAioInitializeImpl(void *p, s32 size) -> s32
void shim_sceKernelAioInitializeImpl(GuestContext *ctx) {
    aio_ensure_init();
    ctx->rax = 0;
    SHIM_RETURN();
}

// sceKernelAioInitializeParam() -> s32
void shim_sceKernelAioInitializeParam(GuestContext *ctx) {
    aio_ensure_init();
    ctx->rax = 0;
    SHIM_RETURN();
}

// sceKernelAioSetParam() -> s32
void shim_sceKernelAioSetParam(GuestContext *ctx) {
    aio_ensure_init();
    ctx->rax = 0;
    SHIM_RETURN();
}

// sceKernelAioDeleteRequest(OrbisKernelAioSubmitId id, s32 *ret) -> s32
void shim_sceKernelAioDeleteRequest(GuestContext *ctx) {
    aio_ensure_init();
    OrbisKernelAioSubmitId id = (OrbisKernelAioSubmitId)ctx->rdi;
    uint64_t ret_guest_addr = ctx->rsi;

    if (!ret_guest_addr) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_EFAULT;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_aio_mutex);
    if (id >= 0 && id < ORBIS_KERNEL_AIO_MAX_QUEUES) {
        g_id_state[id] = ORBIS_KERNEL_AIO_STATE_ABORTED;
    }
    pthread_mutex_unlock(&g_aio_mutex);

    int32_t *ret_ptr = (int32_t *)(ctx->mem_base + ret_guest_addr);
    *ret_ptr = 0;
    ctx->rax = 0;
    SHIM_RETURN();
}

// sceKernelAioDeleteRequests(OrbisKernelAioSubmitId id[], s32 num, s32 ret[]) -> s32
void shim_sceKernelAioDeleteRequests(GuestContext *ctx) {
    aio_ensure_init();
    uint64_t id_arr_addr = ctx->rdi;
    int32_t num = (int32_t)ctx->rsi;
    uint64_t ret_arr_addr = ctx->rdx;

    if (!id_arr_addr || !ret_arr_addr) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_EFAULT;
        SHIM_RETURN();
    }

    const OrbisKernelAioSubmitId *ids = (const OrbisKernelAioSubmitId *)(ctx->mem_base + id_arr_addr);
    int32_t *rets = (int32_t *)(ctx->mem_base + ret_arr_addr);

    pthread_mutex_lock(&g_aio_mutex);
    for (int32_t i = 0; i < num; i++) {
        OrbisKernelAioSubmitId id = ids[i];
        if (id >= 0 && id < ORBIS_KERNEL_AIO_MAX_QUEUES) {
            g_id_state[id] = ORBIS_KERNEL_AIO_STATE_ABORTED;
        }
        rets[i] = 0;
    }
    pthread_mutex_unlock(&g_aio_mutex);

    ctx->rax = 0;
    SHIM_RETURN();
}

// sceKernelAioPollRequest(OrbisKernelAioSubmitId id, s32 *state) -> s32
void shim_sceKernelAioPollRequest(GuestContext *ctx) {
    aio_ensure_init();
    OrbisKernelAioSubmitId id = (OrbisKernelAioSubmitId)ctx->rdi;
    uint64_t state_guest_addr = ctx->rsi;

    if (!state_guest_addr) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_EFAULT;
        SHIM_RETURN();
    }

    int32_t current_state = ORBIS_KERNEL_AIO_STATE_COMPLETED;
    pthread_mutex_lock(&g_aio_mutex);
    if (id >= 0 && id < ORBIS_KERNEL_AIO_MAX_QUEUES) {
        current_state = g_id_state[id];
    }
    pthread_mutex_unlock(&g_aio_mutex);

    int32_t *state_ptr = (int32_t *)(ctx->mem_base + state_guest_addr);
    *state_ptr = current_state;
    ctx->rax = 0;
    SHIM_RETURN();
}

// sceKernelAioPollRequests(OrbisKernelAioSubmitId id[], s32 num, s32 state[]) -> s32
void shim_sceKernelAioPollRequests(GuestContext *ctx) {
    aio_ensure_init();
    uint64_t id_arr_addr = ctx->rdi;
    int32_t num = (int32_t)ctx->rsi;
    uint64_t state_arr_addr = ctx->rdx;

    if (!id_arr_addr || !state_arr_addr) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_EFAULT;
        SHIM_RETURN();
    }

    const OrbisKernelAioSubmitId *ids = (const OrbisKernelAioSubmitId *)(ctx->mem_base + id_arr_addr);
    int32_t *states = (int32_t *)(ctx->mem_base + state_arr_addr);

    pthread_mutex_lock(&g_aio_mutex);
    for (int32_t i = 0; i < num; i++) {
        OrbisKernelAioSubmitId id = ids[i];
        if (id >= 0 && id < ORBIS_KERNEL_AIO_MAX_QUEUES) {
            states[i] = g_id_state[id];
        } else {
            states[i] = ORBIS_KERNEL_AIO_STATE_COMPLETED;
        }
    }
    pthread_mutex_unlock(&g_aio_mutex);

    ctx->rax = 0;
    SHIM_RETURN();
}

// sceKernelAioCancelRequest(OrbisKernelAioSubmitId id, s32 *state) -> s32
void shim_sceKernelAioCancelRequest(GuestContext *ctx) {
    aio_ensure_init();
    OrbisKernelAioSubmitId id = (OrbisKernelAioSubmitId)ctx->rdi;
    uint64_t state_guest_addr = ctx->rsi;

    if (!state_guest_addr) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_EFAULT;
        SHIM_RETURN();
    }

    int32_t res_state;
    pthread_mutex_lock(&g_aio_mutex);
    if (id > 0 && id < ORBIS_KERNEL_AIO_MAX_QUEUES) {
        g_id_state[id] = ORBIS_KERNEL_AIO_STATE_ABORTED;
        res_state = ORBIS_KERNEL_AIO_STATE_ABORTED;
    } else {
        res_state = ORBIS_KERNEL_AIO_STATE_PROCESSING;
    }
    pthread_mutex_unlock(&g_aio_mutex);

    int32_t *state_ptr = (int32_t *)(ctx->mem_base + state_guest_addr);
    *state_ptr = res_state;
    ctx->rax = 0;
    SHIM_RETURN();
}

// sceKernelAioCancelRequests(OrbisKernelAioSubmitId id[], s32 num, s32 state[]) -> s32
void shim_sceKernelAioCancelRequests(GuestContext *ctx) {
    aio_ensure_init();
    uint64_t id_arr_addr = ctx->rdi;
    int32_t num = (int32_t)ctx->rsi;
    uint64_t state_arr_addr = ctx->rdx;

    if (!id_arr_addr || !state_arr_addr) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_EFAULT;
        SHIM_RETURN();
    }

    const OrbisKernelAioSubmitId *ids = (const OrbisKernelAioSubmitId *)(ctx->mem_base + id_arr_addr);
    int32_t *states = (int32_t *)(ctx->mem_base + state_arr_addr);

    pthread_mutex_lock(&g_aio_mutex);
    for (int32_t i = 0; i < num; i++) {
        OrbisKernelAioSubmitId id = ids[i];
        if (id > 0 && id < ORBIS_KERNEL_AIO_MAX_QUEUES) {
            g_id_state[id] = ORBIS_KERNEL_AIO_STATE_ABORTED;
            states[i] = ORBIS_KERNEL_AIO_STATE_ABORTED;
        } else {
            states[i] = ORBIS_KERNEL_AIO_STATE_PROCESSING;
        }
    }
    pthread_mutex_unlock(&g_aio_mutex);

    ctx->rax = 0;
    SHIM_RETURN();
}

// sceKernelAioWaitRequest(OrbisKernelAioSubmitId id, s32 *state, u32 *usec) -> s32
void shim_sceKernelAioWaitRequest(GuestContext *ctx) {
    aio_ensure_init();
    OrbisKernelAioSubmitId id = (OrbisKernelAioSubmitId)ctx->rdi;
    uint64_t state_guest_addr = ctx->rsi;
    uint64_t usec_guest_addr = ctx->rdx;

    if (!state_guest_addr) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_EFAULT;
        SHIM_RETURN();
    }

    uint32_t max_usec = 0;
    if (usec_guest_addr) {
        max_usec = *(const uint32_t *)(ctx->mem_base + usec_guest_addr);
    }

    uint32_t timer = 0;
    int32_t timed_out = 0;
    int32_t cur_state;

    while (1) {
        pthread_mutex_lock(&g_aio_mutex);
        cur_state = (id >= 0 && id < ORBIS_KERNEL_AIO_MAX_QUEUES) ? g_id_state[id] : ORBIS_KERNEL_AIO_STATE_COMPLETED;
        pthread_mutex_unlock(&g_aio_mutex);

        if (cur_state != ORBIS_KERNEL_AIO_STATE_PROCESSING && cur_state != ORBIS_KERNEL_AIO_STATE_SUBMITTED) {
            break;
        }
        usleep(10);
        timer += 10;
        if (max_usec > 0 && timer > max_usec) {
            timed_out = 1;
            break;
        }
    }

    int32_t *state_ptr = (int32_t *)(ctx->mem_base + state_guest_addr);
    *state_ptr = cur_state;

    if (timed_out) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_ETIMEDOUT;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

// sceKernelAioWaitRequests(OrbisKernelAioSubmitId id[], s32 num, s32 state[], u32 mode, u32 *usec) -> s32
void shim_sceKernelAioWaitRequests(GuestContext *ctx) {
    aio_ensure_init();
    uint64_t id_arr_addr = ctx->rdi;
    int32_t num = (int32_t)ctx->rsi;
    uint64_t state_arr_addr = ctx->rdx;
    uint32_t mode = (uint32_t)ctx->rcx;
    uint64_t usec_guest_addr = ctx->r8;

    if (!id_arr_addr || !state_arr_addr) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_EFAULT;
        SHIM_RETURN();
    }

    uint32_t max_usec = 0;
    if (usec_guest_addr) {
        max_usec = *(const uint32_t *)(ctx->mem_base + usec_guest_addr);
    }

    const OrbisKernelAioSubmitId *ids = (const OrbisKernelAioSubmitId *)(ctx->mem_base + id_arr_addr);
    int32_t *states = (int32_t *)(ctx->mem_base + state_arr_addr);

    uint32_t timer = 0;
    int32_t timed_out = 0;
    int32_t completion = 0;

    for (int32_t i = 0; i < num; i++) {
        OrbisKernelAioSubmitId id = ids[i];
        if (!completion && !timed_out) {
            while (1) {
                pthread_mutex_lock(&g_aio_mutex);
                int32_t st = (id >= 0 && id < ORBIS_KERNEL_AIO_MAX_QUEUES) ? g_id_state[id] : ORBIS_KERNEL_AIO_STATE_COMPLETED;
                pthread_mutex_unlock(&g_aio_mutex);

                if (st != ORBIS_KERNEL_AIO_STATE_PROCESSING && st != ORBIS_KERNEL_AIO_STATE_SUBMITTED) {
                    break;
                }
                usleep(10);
                timer += 10;
                if (max_usec > 0 && timer > max_usec) {
                    timed_out = 1;
                    break;
                }
            }
        }

        pthread_mutex_lock(&g_aio_mutex);
        int32_t final_st = (id >= 0 && id < ORBIS_KERNEL_AIO_MAX_QUEUES) ? g_id_state[id] : ORBIS_KERNEL_AIO_STATE_COMPLETED;
        pthread_mutex_unlock(&g_aio_mutex);

        if (mode == 0x02 && final_st == ORBIS_KERNEL_AIO_STATE_COMPLETED) {
            completion = 1;
        }
        states[i] = final_st;
    }

    if (timed_out) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_ETIMEDOUT;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

// Helper to execute read request
static void execute_read_request(GuestContext *ctx, const OrbisKernelAioRWRequest *req) {
    if (!req) return;
    int fd = req->fd;
    void *buf = ctx->mem_base + req->buf;
    size_t nbyte = (size_t)req->nbyte;
    off_t offset = (off_t)req->offset;

    ssize_t ret = pread(fd, buf, nbyte, offset);

    if (req->result) {
        OrbisKernelAioResult *res = (OrbisKernelAioResult *)(ctx->mem_base + req->result);
        if (ret < 0) {
            res->state = ORBIS_KERNEL_AIO_STATE_ABORTED;
            res->returnValue = (int64_t)-errno;
        } else {
            res->state = ORBIS_KERNEL_AIO_STATE_COMPLETED;
            res->returnValue = (int64_t)ret;
        }
    }
}

// Helper to execute write request
static void execute_write_request(GuestContext *ctx, const OrbisKernelAioRWRequest *req) {
    if (!req) return;
    int fd = req->fd;
    const void *buf = ctx->mem_base + req->buf;
    size_t nbyte = (size_t)req->nbyte;
    off_t offset = (off_t)req->offset;

    ssize_t ret = pwrite(fd, buf, nbyte, offset);

    if (req->result) {
        OrbisKernelAioResult *res = (OrbisKernelAioResult *)(ctx->mem_base + req->result);
        if (ret < 0) {
            res->state = ORBIS_KERNEL_AIO_STATE_ABORTED;
            res->returnValue = (int64_t)-errno;
        } else {
            res->state = ORBIS_KERNEL_AIO_STATE_COMPLETED;
            res->returnValue = (int64_t)ret;
        }
    }
}

// sceKernelAioSubmitReadCommands(OrbisKernelAioRWRequest req[], s32 size, s32 prio, OrbisKernelAioSubmitId *id) -> s32
void shim_sceKernelAioSubmitReadCommands(GuestContext *ctx) {
    aio_ensure_init();
    uint64_t req_arr_addr = ctx->rdi;
    int32_t size = (int32_t)ctx->rsi;
    // prio = ctx->rdx
    uint64_t id_ptr_addr = ctx->rcx;

    if (!req_arr_addr || !id_ptr_addr) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_EFAULT;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_aio_mutex);
    int32_t curr_id = g_id_index;
    g_id_state[curr_id] = ORBIS_KERNEL_AIO_STATE_PROCESSING;

    g_id_index = (g_id_index + 1) % ORBIS_KERNEL_AIO_MAX_QUEUES;
    if (g_id_index == 0) g_id_index = 1;
    pthread_mutex_unlock(&g_aio_mutex);

    const OrbisKernelAioRWRequest *requests = (const OrbisKernelAioRWRequest *)(ctx->mem_base + req_arr_addr);
    for (int32_t i = 0; i < size; i++) {
        execute_read_request(ctx, &requests[i]);
    }

    pthread_mutex_lock(&g_aio_mutex);
    g_id_state[curr_id] = ORBIS_KERNEL_AIO_STATE_COMPLETED;
    pthread_mutex_unlock(&g_aio_mutex);

    OrbisKernelAioSubmitId *out_id = (OrbisKernelAioSubmitId *)(ctx->mem_base + id_ptr_addr);
    *out_id = curr_id;

    ctx->rax = 0;
    SHIM_RETURN();
}

// sceKernelAioSubmitReadCommandsMultiple(OrbisKernelAioRWRequest req[], s32 size, s32 prio, OrbisKernelAioSubmitId id[]) -> s32
void shim_sceKernelAioSubmitReadCommandsMultiple(GuestContext *ctx) {
    aio_ensure_init();
    uint64_t req_arr_addr = ctx->rdi;
    int32_t size = (int32_t)ctx->rsi;
    // prio = ctx->rdx
    uint64_t id_arr_addr = ctx->rcx;

    if (!req_arr_addr || !id_arr_addr) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_EFAULT;
        SHIM_RETURN();
    }

    const OrbisKernelAioRWRequest *requests = (const OrbisKernelAioRWRequest *)(ctx->mem_base + req_arr_addr);
    OrbisKernelAioSubmitId *ids = (OrbisKernelAioSubmitId *)(ctx->mem_base + id_arr_addr);

    for (int32_t i = 0; i < size; i++) {
        pthread_mutex_lock(&g_aio_mutex);
        int32_t curr_id = g_id_index;
        g_id_state[curr_id] = ORBIS_KERNEL_AIO_STATE_PROCESSING;

        g_id_index = (g_id_index + 1) % ORBIS_KERNEL_AIO_MAX_QUEUES;
        if (g_id_index == 0) g_id_index = 1;
        pthread_mutex_unlock(&g_aio_mutex);

        execute_read_request(ctx, &requests[i]);

        pthread_mutex_lock(&g_aio_mutex);
        g_id_state[curr_id] = ORBIS_KERNEL_AIO_STATE_COMPLETED;
        pthread_mutex_unlock(&g_aio_mutex);

        ids[i] = curr_id;
    }

    ctx->rax = 0;
    SHIM_RETURN();
}

// sceKernelAioSubmitWriteCommands(OrbisKernelAioRWRequest req[], s32 size, s32 prio, OrbisKernelAioSubmitId *id) -> s32
void shim_sceKernelAioSubmitWriteCommands(GuestContext *ctx) {
    aio_ensure_init();
    uint64_t req_arr_addr = ctx->rdi;
    int32_t size = (int32_t)ctx->rsi;
    // prio = ctx->rdx
    uint64_t id_ptr_addr = ctx->rcx;

    if (!req_arr_addr || !id_ptr_addr) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_EFAULT;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_aio_mutex);
    int32_t curr_id = g_id_index;
    g_id_state[curr_id] = ORBIS_KERNEL_AIO_STATE_PROCESSING;

    g_id_index = (g_id_index + 1) % ORBIS_KERNEL_AIO_MAX_QUEUES;
    if (g_id_index == 0) g_id_index = 1;
    pthread_mutex_unlock(&g_aio_mutex);

    const OrbisKernelAioRWRequest *requests = (const OrbisKernelAioRWRequest *)(ctx->mem_base + req_arr_addr);
    for (int32_t i = 0; i < size; i++) {
        execute_write_request(ctx, &requests[i]);
    }

    pthread_mutex_lock(&g_aio_mutex);
    g_id_state[curr_id] = ORBIS_KERNEL_AIO_STATE_COMPLETED;
    pthread_mutex_unlock(&g_aio_mutex);

    OrbisKernelAioSubmitId *out_id = (OrbisKernelAioSubmitId *)(ctx->mem_base + id_ptr_addr);
    *out_id = curr_id;

    ctx->rax = 0;
    SHIM_RETURN();
}

// sceKernelAioSubmitWriteCommandsMultiple(OrbisKernelAioRWRequest req[], s32 size, s32 prio, OrbisKernelAioSubmitId id[]) -> s32
void shim_sceKernelAioSubmitWriteCommandsMultiple(GuestContext *ctx) {
    aio_ensure_init();
    uint64_t req_arr_addr = ctx->rdi;
    int32_t size = (int32_t)ctx->rsi;
    // prio = ctx->rdx
    uint64_t id_arr_addr = ctx->rcx;

    if (!req_arr_addr || !id_arr_addr) {
        ctx->rax = (uint64_t)ORBIS_KERNEL_ERROR_EFAULT;
        SHIM_RETURN();
    }

    const OrbisKernelAioRWRequest *requests = (const OrbisKernelAioRWRequest *)(ctx->mem_base + req_arr_addr);
    OrbisKernelAioSubmitId *ids = (OrbisKernelAioSubmitId *)(ctx->mem_base + id_arr_addr);

    for (int32_t i = 0; i < size; i++) {
        pthread_mutex_lock(&g_aio_mutex);
        int32_t curr_id = g_id_index;
        g_id_state[curr_id] = ORBIS_KERNEL_AIO_STATE_PROCESSING;

        g_id_index = (g_id_index + 1) % ORBIS_KERNEL_AIO_MAX_QUEUES;
        if (g_id_index == 0) g_id_index = 1;
        pthread_mutex_unlock(&g_aio_mutex);

        execute_write_request(ctx, &requests[i]);

        pthread_mutex_lock(&g_aio_mutex);
        g_id_state[curr_id] = ORBIS_KERNEL_AIO_STATE_COMPLETED;
        pthread_mutex_unlock(&g_aio_mutex);

        ids[i] = curr_id;
    }

    ctx->rax = 0;
    SHIM_RETURN();
}
