#include "recomp_runtime.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sched.h>
#include <errno.h>

// Helper to pop return address on return from shim
#define SHIM_RETURN() do { ctx->rsp += 8; return; } while (0)

// PS4 specific: sceKernelUsleep sleeps for microseconds
void shim_sceKernelUsleep(GuestContext *ctx) {
    uint32_t usec = (uint32_t)ctx->rdi;
    usleep(usec);
    ctx->rax = 0;
    SHIM_RETURN();
}

// POSIX write(fd, buf, count)
void shim_write(GuestContext *ctx) {
    int fd = (int)ctx->rdi;
    uint64_t buf_addr = ctx->rsi;
    size_t count = (size_t)ctx->rdx;

    const void *host_ptr = ctx->mem_base + buf_addr;
    ssize_t ret = write(fd, host_ptr, count);
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
}

// PS4 / FreeBSD 64-bit iovec
struct guest_iovec {
    uint64_t iov_base;
    uint64_t iov_len;
};

// POSIX writev(fd, iov, iovcnt)
void shim_writev(GuestContext *ctx) {
    int fd = (int)ctx->rdi;
    uint64_t iov_addr = ctx->rsi;
    int iovcnt = (int)ctx->rdx;

    if (iovcnt <= 0 || iovcnt > 1024) {
        ctx->rax = (uint64_t)-1;
        SHIM_RETURN();
    }

    struct guest_iovec *guest_iov = (struct guest_iovec *)(ctx->mem_base + iov_addr);
    struct iovec host_iov[iovcnt];

    for (int i = 0; i < iovcnt; i++) {
        host_iov[i].iov_base = ctx->mem_base + guest_iov[i].iov_base;
        host_iov[i].iov_len = (size_t)guest_iov[i].iov_len;
    }

    ssize_t ret = writev(fd, host_iov, iovcnt);
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
}

// Exit (does not return)
void shim_exit(GuestContext *ctx) {
    int status = (int)ctx->rdi;
    exit(status);
}

// errno pointer
void shim_error(GuestContext *ctx) {
    ctx->rax = 0x10001000ULL;
    SHIM_RETURN();
}

// sysconf
void shim_sysconf(GuestContext *ctx) {
    int name = (int)ctx->rdi;
    long val = sysconf(name);
    if (val < 0) {
        val = 4096;
    }
    ctx->rax = (uint64_t)val;
    SHIM_RETURN();
}

// nanosleep
void shim_nanosleep(GuestContext *ctx) {
    uint64_t req_addr = ctx->rdi;
    uint64_t rem_addr = ctx->rsi;

    struct timespec req, rem;
    uint64_t *guest_req = (uint64_t*)(ctx->mem_base + req_addr);
    req.tv_sec = (time_t)guest_req[0];
    req.tv_nsec = (long)guest_req[1];

    int ret = nanosleep(&req, rem_addr ? &rem : NULL);
    if (ret == 0 && rem_addr) {
        uint64_t *guest_rem = (uint64_t*)(ctx->mem_base + rem_addr);
        guest_rem[0] = (uint64_t)rem.tv_sec;
        guest_rem[1] = (uint64_t)rem.tv_nsec;
    }
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
}

// sched_yield
void shim_sched_yield(GuestContext *ctx) {
    ctx->rax = (uint64_t)sched_yield();
    SHIM_RETURN();
}

// Simple bump allocator for guest mmap
static uint64_t g_guest_heap_ptr = 0x21000000ULL; // Start heap at 528MB (after args at 0x20000000)
void shim_mmap(GuestContext *ctx) {
    uint64_t addr = ctx->rdi;
    size_t len = (size_t)ctx->rsi;
    if (addr == 0) {
        addr = g_guest_heap_ptr;
        g_guest_heap_ptr = (g_guest_heap_ptr + len + 4095) & ~4095ULL;
    }
    ctx->rax = addr;
    SHIM_RETURN();
}


void shim_munmap(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_madvise(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_open(GuestContext *ctx) {
    const char *path = (const char*)(ctx->mem_base + ctx->rdi);
    int flags = (int)ctx->rsi;
    int mode = (int)ctx->rdx;
    int ret = open(path, flags, mode);
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
}

void shim_close(GuestContext *ctx) {
    int fd = (int)ctx->rdi;
    ctx->rax = (uint64_t)close(fd);
    SHIM_RETURN();
}

void shim_lseek(GuestContext *ctx) {
    int fd = (int)ctx->rdi;
    off_t offset = (off_t)ctx->rsi;
    int whence = (int)ctx->rdx;
    ctx->rax = (uint64_t)lseek(fd, offset, whence);
    SHIM_RETURN();
}

void shim_fstat(GuestContext *ctx) {
    int fd = (int)ctx->rdi;
    uint64_t statbuf_addr = ctx->rsi;
    struct stat st;
    int ret = fstat(fd, &st);
    if (ret == 0) {
        memset(ctx->mem_base + statbuf_addr, 0, 128);
        *(uint64_t*)(ctx->mem_base + statbuf_addr + 48) = (uint64_t)st.st_size;
    }
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
}

void shim_ioctl(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_poll(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sigaction(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sigprocmask(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_raise(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// Pthreads: lightweight stub implementations for single-threaded environment
void shim_pthread_mutex_init(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_mutex_lock(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_mutex_trylock(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_mutex_unlock(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_mutex_destroy(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_mutexattr_init(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_mutexattr_settype(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_mutexattr_destroy(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_cond_init(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_cond_wait(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_cond_timedwait(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_cond_signal(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_cond_broadcast(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_cond_destroy(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_rwlock_rdlock(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_rwlock_wrlock(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_rwlock_unlock(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }

void shim_pthread_once(GuestContext *ctx) {
    uint64_t once_control_addr = ctx->rdi;
    uint64_t init_routine_addr = ctx->rsi;
    int *ctrl = (int*)(ctx->mem_base + once_control_addr);
    if (*ctrl == 0) {
        *ctrl = 1;
        ctx->rsp -= 8;
        MEM_U64(ctx->rsp) = 0xdeadbeefULL; // dummy return address
        recomp_dispatch(ctx, init_routine_addr);
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

static uint64_t g_tls_keys[64];
static uint32_t g_next_key = 1;
void shim_pthread_key_create(GuestContext *ctx) {
    uint64_t key_ptr_addr = ctx->rdi;
    uint32_t key = g_next_key++;
    *(uint32_t*)(ctx->mem_base + key_ptr_addr) = key;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_pthread_setspecific(GuestContext *ctx) {
    uint32_t key = (uint32_t)ctx->rdi;
    uint64_t val = ctx->rsi;
    if (key < 64) {
        g_tls_keys[key] = val;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_pthread_getspecific(GuestContext *ctx) {
    uint32_t key = (uint32_t)ctx->rdi;
    if (key < 64) {
        ctx->rax = g_tls_keys[key];
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim_pthread_self(GuestContext *ctx) {
    ctx->rax = 1;
    SHIM_RETURN();
}

void shim_pthread_equal(GuestContext *ctx) {
    ctx->rax = (ctx->rdi == ctx->rsi);
    SHIM_RETURN();
}

void shim_pthread_detach(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_pthread_join(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
