// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_SYSCALLS_H
#define PS4_SYSCALLS_H

#include "recomp_context.h"

#ifdef __cplusplus
extern "C" {
#endif

void shim_sceKernelUsleep(GuestContext *ctx);
void shim_write(GuestContext *ctx);
void shim_writev(GuestContext *ctx);
void shim_exit(GuestContext *ctx);
void shim_error(GuestContext *ctx);
void shim_sysconf(GuestContext *ctx);
void shim_nanosleep(GuestContext *ctx);
void shim_sched_yield(GuestContext *ctx);
void shim_mmap(GuestContext *ctx);
void shim_munmap(GuestContext *ctx);
void shim_madvise(GuestContext *ctx);
void shim_read(GuestContext *ctx);
void shim_readv(GuestContext *ctx);
void shim_sceKernelPread(GuestContext *ctx);
void shim_sceKernelPreadv(GuestContext *ctx);
void shim_fcntl(GuestContext *ctx);
void shim_open(GuestContext *ctx);
void shim_close(GuestContext *ctx);
void shim_lseek(GuestContext *ctx);
void shim_stat(GuestContext *ctx);
void shim_fstat(GuestContext *ctx);
void shim_chmod(GuestContext *ctx);
void shim_utimes(GuestContext *ctx);
void shim_unlink(GuestContext *ctx);
void shim_getdents(GuestContext *ctx);
void shim_sceKernelTriggerUserEvent(GuestContext *ctx);
void shim_sceKernelAddUserEventEdge(GuestContext *ctx);
void shim_sceKernelStopUnloadModule(GuestContext *ctx);
void shim_sceKernelGetPrtAperture(GuestContext *ctx);
void shim_ioctl(GuestContext *ctx);
void shim_poll(GuestContext *ctx);
void shim_sigaction(GuestContext *ctx);
void shim_sigprocmask(GuestContext *ctx);
void shim_raise(GuestContext *ctx);
void shim_syscall(GuestContext *ctx);
void shim___stack_chk_fail(GuestContext *ctx);
void shim_clock_gettime(GuestContext *ctx);
void shim_gettimeofday(GuestContext *ctx);
void shim_getrusage(GuestContext *ctx);
void shim_getrlimit(GuestContext *ctx);
void shim_cpuset_getaffinity(GuestContext *ctx);
void shim_memcpy(GuestContext *ctx);
void shim_memmove(GuestContext *ctx);
void shim_memset(GuestContext *ctx);
void shim_strlen(GuestContext *ctx);
void shim_strcpy(GuestContext *ctx);
void shim_strncpy(GuestContext *ctx);
void shim_strcmp(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_SYSCALLS_H
