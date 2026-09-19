#include "recomp_runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>

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
  if (ret < 0) {
    set_guest_errno(ctx, errno);
  }
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
    set_guest_errno(ctx, EINVAL);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  struct guest_iovec *guest_iov =
      (struct guest_iovec *)(ctx->mem_base + iov_addr);
  struct iovec host_iov[iovcnt];

  for (int i = 0; i < iovcnt; i++) {
    host_iov[i].iov_base = ctx->mem_base + guest_iov[i].iov_base;
    host_iov[i].iov_len = (size_t)guest_iov[i].iov_len;
  }

  ssize_t ret = writev(fd, host_iov, iovcnt);
  if (ret < 0) {
    set_guest_errno(ctx, errno);
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

// Exit (does not return)
void shim_exit(GuestContext *ctx) {
  int status = (int)ctx->rdi;
  exit(status);
}

// errno pointer (allocated within dynamic TLS space)
void shim_error(GuestContext *ctx) {
  ctx->rax = ctx->fs_base + 0x100ULL;
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
  uint64_t *guest_req = (uint64_t *)(ctx->mem_base + req_addr);
  req.tv_sec = (time_t)guest_req[0];
  req.tv_nsec = (long)guest_req[1];

  int ret = nanosleep(&req, rem_addr ? &rem : NULL);
  if (ret < 0) {
    set_guest_errno(ctx, errno);
  }
  if (ret != 0 && rem_addr) {
    uint64_t *guest_rem = (uint64_t *)(ctx->mem_base + rem_addr);
    guest_rem[0] = (uint64_t)rem.tv_sec;
    guest_rem[1] = (uint64_t)rem.tv_nsec;
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

// sched_yield
void shim_sched_yield(GuestContext *ctx) {
  int ret = sched_yield();
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

// mmap
void shim_mmap(GuestContext *ctx) {
  size_t len = (size_t)ctx->rsi;
  uint64_t alloc_addr = recomp_vm_alloc(ctx, len);
  if (alloc_addr == (uint64_t)-1) {
    set_guest_errno(ctx, ENOMEM);
    ctx->rax = (uint64_t)-1;
  } else {
    ctx->rax = alloc_addr;
  }
  SHIM_RETURN();
}

// munmap
void shim_munmap(GuestContext *ctx) {
  uint64_t addr = ctx->rdi;
  size_t len = (size_t)ctx->rsi;
  int ret = recomp_vm_free(ctx, addr, len);
  if (ret != 0) {
    set_guest_errno(ctx, EINVAL);
    ctx->rax = (uint64_t)-1;
  } else {
    ctx->rax = 0;
  }
  SHIM_RETURN();
}

// madvise
void shim_madvise(GuestContext *ctx) {
  uint64_t addr = ctx->rdi;
  size_t len = (size_t)ctx->rsi;
  int behav = (int)ctx->rdx;

  if (addr + len <= ctx->mem_size) {
#if defined(MADV_DONTNEED)
    int host_behav = MADV_DONTNEED;
#if defined(MADV_FREE)
    if (behav == 5) host_behav = MADV_FREE;
#endif
    madvise(ctx->mem_base + addr, len, host_behav);
#endif
  }
  ctx->rax = 0;
  SHIM_RETURN();
}

// read
void shim_read(GuestContext *ctx) {
  int fd = (int)ctx->rdi;
  uint64_t buf_addr = ctx->rsi;
  size_t count = (size_t)ctx->rdx;
  ssize_t ret = read(fd, ctx->mem_base + buf_addr, count);
  if (ret < 0) {
    set_guest_errno(ctx, errno);
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

// readv
void shim_readv(GuestContext *ctx) {
  int fd = (int)ctx->rdi;
  uint64_t iov_addr = ctx->rsi;
  int iovcnt = (int)ctx->rdx;

  if (iovcnt <= 0 || iovcnt > 1024) {
    set_guest_errno(ctx, EINVAL);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  struct guest_iovec *guest_iov =
      (struct guest_iovec *)(ctx->mem_base + iov_addr);
  struct iovec host_iov[iovcnt];

  for (int i = 0; i < iovcnt; i++) {
    host_iov[i].iov_base = ctx->mem_base + guest_iov[i].iov_base;
    host_iov[i].iov_len = (size_t)guest_iov[i].iov_len;
  }

  ssize_t ret = readv(fd, host_iov, iovcnt);
  if (ret < 0) {
    set_guest_errno(ctx, errno);
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

// fcntl
void shim_fcntl(GuestContext *ctx) {
  int fd = (int)ctx->rdi;
  int cmd = (int)ctx->rsi;
  uint64_t arg = ctx->rdx;
  int ret = fcntl(fd, cmd, arg);
  if (ret < 0) {
    set_guest_errno(ctx, errno);
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

// open
void shim_open(GuestContext *ctx) {
  uint64_t path_addr = ctx->rdi;
  int flags = (int)ctx->rsi;
  mode_t mode = (mode_t)ctx->rdx;
  const char *path = (const char *)(ctx->mem_base + path_addr);
  int ret = open(path, flags, mode);
  if (ret < 0) {
    set_guest_errno(ctx, errno);
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

// close
void shim_close(GuestContext *ctx) {
  int fd = (int)ctx->rdi;
  int ret = close(fd);
  if (ret < 0) {
    set_guest_errno(ctx, errno);
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

// lseek
void shim_lseek(GuestContext *ctx) {
  int fd = (int)ctx->rdi;
  off_t offset = (off_t)ctx->rsi;
  int whence = (int)ctx->rdx;
  off_t ret = lseek(fd, offset, whence);
  if (ret < 0) {
    set_guest_errno(ctx, errno);
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

// Exact 128-byte layout for OpenOrbis / PS4 FreeBSD struct stat
struct ps4_stat {
  uint32_t st_dev;
  uint32_t st_ino;
  uint32_t st_mode;
  uint32_t st_nlink;
  uint32_t st_uid;
  uint32_t st_gid;
  uint32_t st_rdev;
  uint32_t __pad0;
  int64_t  st_atime_sec;
  int64_t  st_atime_nsec;
  int64_t  st_mtime_sec;
  int64_t  st_mtime_nsec;
  int64_t  st_ctime_sec;
  int64_t  st_ctime_nsec;
  int64_t  st_size;
  int64_t  st_blocks;
  uint32_t st_blksize;
  uint32_t st_flags;
  uint32_t st_gen;
  uint32_t __pad1;
  int64_t  st_birthtim_sec;
  int64_t  st_birthtim_nsec;
};

// fstat
void shim_fstat(GuestContext *ctx) {
  int fd = (int)ctx->rdi;
  uint64_t statbuf_addr = ctx->rsi;
  struct stat st;
  int ret = fstat(fd, &st);
  if (ret == 0) {
    struct ps4_stat *gst = (struct ps4_stat *)(ctx->mem_base + statbuf_addr);
    memset(gst, 0, sizeof(struct ps4_stat));
    gst->st_dev = (uint32_t)st.st_dev;
    gst->st_ino = (uint32_t)st.st_ino;
    gst->st_mode = (uint32_t)st.st_mode;
    gst->st_nlink = (uint32_t)st.st_nlink;
    gst->st_uid = (uint32_t)st.st_uid;
    gst->st_gid = (uint32_t)st.st_gid;
    gst->st_rdev = (uint32_t)st.st_rdev;
#if defined(__APPLE__)
    gst->st_atime_sec = (int64_t)st.st_atimespec.tv_sec;
    gst->st_atime_nsec = (int64_t)st.st_atimespec.tv_nsec;
    gst->st_mtime_sec = (int64_t)st.st_mtimespec.tv_sec;
    gst->st_mtime_nsec = (int64_t)st.st_mtimespec.tv_nsec;
    gst->st_ctime_sec = (int64_t)st.st_ctimespec.tv_sec;
    gst->st_ctime_nsec = (int64_t)st.st_ctimespec.tv_nsec;
    gst->st_birthtim_sec = (int64_t)st.st_birthtimespec.tv_sec;
    gst->st_birthtim_nsec = (int64_t)st.st_birthtimespec.tv_nsec;
#else
    gst->st_atime_sec = (int64_t)st.st_atim.tv_sec;
    gst->st_atime_nsec = (int64_t)st.st_atim.tv_nsec;
    gst->st_mtime_sec = (int64_t)st.st_mtim.tv_sec;
    gst->st_mtime_nsec = (int64_t)st.st_mtim.tv_nsec;
    gst->st_ctime_sec = (int64_t)st.st_ctim.tv_sec;
    gst->st_ctime_nsec = (int64_t)st.st_ctim.tv_nsec;
#endif
    gst->st_size = (int64_t)st.st_size;
    gst->st_blocks = (int64_t)st.st_blocks;
    gst->st_blksize = (uint32_t)st.st_blksize;
    gst->st_flags = (uint32_t)st.st_flags;
    gst->st_gen = (uint32_t)st.st_gen;
    ctx->rax = 0;
  } else {
    set_guest_errno(ctx, errno);
    ctx->rax = (uint64_t)-1;
  }
  SHIM_RETURN();
}

// ioctl
void shim_ioctl(GuestContext *ctx) {
  int fd = (int)ctx->rdi;
  unsigned long request = (unsigned long)ctx->rsi;
  uint64_t arg = ctx->rdx;

  void *host_arg = arg ? (void *)(ctx->mem_base + arg) : NULL;
  int ret = ioctl(fd, request, host_arg);
  if (ret < 0) {
    set_guest_errno(ctx, errno);
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

// poll
void shim_poll(GuestContext *ctx) {
  uint64_t fds_addr = ctx->rdi;
  nfds_t nfds = (nfds_t)ctx->rsi;
  int timeout = (int)ctx->rdx;

  if (nfds > 1024) {
    set_guest_errno(ctx, EINVAL);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }

  struct pollfd *guest_fds = (struct pollfd *)(ctx->mem_base + fds_addr);
  struct pollfd host_fds[nfds ? nfds : 1];
  for (nfds_t i = 0; i < nfds; i++) {
    host_fds[i].fd = guest_fds[i].fd;
    host_fds[i].events = guest_fds[i].events;
    host_fds[i].revents = 0;
  }

  int ret = poll(host_fds, nfds, timeout);
  if (ret >= 0) {
    for (nfds_t i = 0; i < nfds; i++) {
      guest_fds[i].revents = host_fds[i].revents;
    }
  } else {
    set_guest_errno(ctx, errno);
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

// sigaction
void shim_sigaction(GuestContext *ctx) {
  ctx->rax = 0;
  SHIM_RETURN();
}

// sigprocmask
void shim_sigprocmask(GuestContext *ctx) {
  int how = (int)ctx->rdi;
  uint64_t set_addr = ctx->rsi;
  uint64_t oldset_addr = ctx->rdx;

  sigset_t host_set, host_oldset;
  sigset_t *p_set = NULL;
  sigset_t *p_old = oldset_addr ? &host_oldset : NULL;

  if (set_addr) {
    uint32_t *guest_bits = (uint32_t *)(ctx->mem_base + set_addr);
    host_set = (sigset_t)guest_bits[0];
    p_set = &host_set;
  }

  int ret = sigprocmask(how, p_set, p_old);
  if (ret == 0) {
    if (oldset_addr) {
      uint32_t *guest_old = (uint32_t *)(ctx->mem_base + oldset_addr);
      guest_old[0] = (uint32_t)host_oldset;
      guest_old[1] = 0;
      guest_old[2] = 0;
      guest_old[3] = 0;
    }
  } else {
    set_guest_errno(ctx, errno);
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

// raise
void shim_raise(GuestContext *ctx) {
  int sig = (int)ctx->rdi;
  int ret = raise(sig);
  if (ret < 0) {
    set_guest_errno(ctx, errno);
  }
  ctx->rax = (uint64_t)ret;
  SHIM_RETURN();
}

// syscall
void shim_syscall(GuestContext *ctx) {
  int num = (int)ctx->rdi;
  switch (num) {
  case 1: { // SYS_exit
    int status = (int)ctx->rsi;
    exit(status);
    break;
  }
  case 3: { // SYS_read
    int fd = (int)ctx->rsi;
    uint64_t buf = ctx->rdx;
    size_t count = (size_t)ctx->rcx;
    ssize_t ret = read(fd, ctx->mem_base + buf, count);
    if (ret < 0) set_guest_errno(ctx, errno);
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
  }
  case 4: { // SYS_write
    int fd = (int)ctx->rsi;
    uint64_t buf = ctx->rdx;
    size_t count = (size_t)ctx->rcx;
    ssize_t ret = write(fd, ctx->mem_base + buf, count);
    if (ret < 0) set_guest_errno(ctx, errno);
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
  }
  case 5: { // SYS_open
    uint64_t path = ctx->rsi;
    int flags = (int)ctx->rdx;
    mode_t mode = (mode_t)ctx->rcx;
    int ret = open((const char *)(ctx->mem_base + path), flags, mode);
    if (ret < 0) set_guest_errno(ctx, errno);
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
  }
  case 6: { // SYS_close
    int fd = (int)ctx->rsi;
    int ret = close(fd);
    if (ret < 0) set_guest_errno(ctx, errno);
    ctx->rax = (uint64_t)ret;
    SHIM_RETURN();
  }
  case 20: // SYS_getpid
    ctx->rax = ctx->thread_id ? ctx->thread_id : 1;
    SHIM_RETURN();
  case 24: // SYS_getuid
  case 25: // SYS_geteuid
  case 47: // SYS_getgid
    ctx->rax = 1;
    SHIM_RETURN();
  case 73: { // SYS_munmap
    uint64_t addr = ctx->rsi;
    size_t len = (size_t)ctx->rdx;
    int ret = recomp_vm_free(ctx, addr, len);
    if (ret != 0) {
      set_guest_errno(ctx, EINVAL);
      ctx->rax = (uint64_t)-1;
    } else {
      ctx->rax = 0;
    }
    SHIM_RETURN();
  }
  case 432: // SYS_thr_self
    ctx->rax = ctx->thread_id ? ctx->thread_id : 1;
    SHIM_RETURN();
  case 477: { // SYS_mmap
    size_t len = (size_t)ctx->rdx;
    uint64_t alloc_addr = recomp_vm_alloc(ctx, len);
    if (alloc_addr == (uint64_t)-1) {
      set_guest_errno(ctx, ENOMEM);
      ctx->rax = (uint64_t)-1;
    } else {
      ctx->rax = alloc_addr;
    }
    SHIM_RETURN();
  }
  default:
    // Honest unsupported syscall: return -1 with ENOSYS
    set_guest_errno(ctx, ENOSYS);
    ctx->rax = (uint64_t)-1;
    SHIM_RETURN();
  }
}
