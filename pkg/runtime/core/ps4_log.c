// Asynchronous stdout/stderr.
//
// The generated main() makes stdout and stderr unbuffered, so every printf from a guest or
// renderer thread is a synchronous write() to the log file or pipe. A slow sink (disk under
// memory pressure, a stalled reader) then blocks the calling thread for hundreds of
// milliseconds, for instance the thread that submits command buffers.
//
// When stdout/stderr are not terminals, each is replaced by a pipe that a drain thread copies to
// the original file, so writers only block when the 64 KB pipe is full. Order within a stream
// is kept; when fd 1 and fd 2 are the same file they share one pipe, so their relative order is
// kept too. The guest's own write() on fd 1/2 goes to the same pipe.
//
// Output must survive abort(), crashes and exit: ps4_log_drain_sync() points fd 1/2 back at the
// original files (dup2 closes the last write end of each pipe), the drain threads empty their
// pipes, see EOF and finish. It only uses async-signal-safe calls, so crash handlers and a
// SIGABRT handler call it first. PS4_SYNC_LOG=1 keeps direct writes.
#include "ps4_log.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LOG_MAX_STREAMS 2
#define LOG_CHUNK 65536

typedef struct {
  int rd;
  int nfds;
  int fd[2];   // the standard descriptors this pipe stands in for
  int orig[2]; // their original targets
  atomic_int done;
} LogStream;

static LogStream g_stream[LOG_MAX_STREAMS];
static int g_nstreams;
static struct sigaction g_old_abort;

static void *log_drain_thread(void *arg) {
  LogStream *st = (LogStream *)arg;
  // Highest class: the thread is idle almost all the time, and a writer whose pipe fills up waits
  // for it, so it must not queue behind the job workers (user-initiated).
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  pthread_setname_np("ps4-log");
  char *buf = malloc(LOG_CHUNK);
  int sink_ok = buf != NULL;
  for (;;) {
    ssize_t n = buf ? read(st->rd, buf, LOG_CHUNK) : 0;
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break; // EOF: every write end is closed
    // A dead sink loses the output but must keep reading so writers never block.
    for (ssize_t off = 0; sink_ok && off < n;) {
      ssize_t w = write(st->orig[0], buf + off, (size_t)(n - off));
      if (w < 0 && errno == EINTR) continue;
      if (w <= 0) { sink_ok = 0; break; }
      off += w;
    }
  }
  free(buf);
  // Whatever ended the loop (EOF, read error, no buffer): writers must never block on an unread
  // pipe, so point the standard descriptors back at the originals first.
  for (int j = 0; j < st->nfds; j++) dup2(st->orig[j], st->fd[j]);
  atomic_store(&st->done, 1);
  return NULL;
}

// Note: on timeout this returns while a drain thread may still be inside a slow write().
void ps4_log_drain_sync(double timeout_s) {
  if (!g_nstreams) return;
  for (int i = 0; i < g_nstreams; i++)
    for (int j = 0; j < g_stream[i].nfds; j++) dup2(g_stream[i].orig[j], g_stream[i].fd[j]);
  struct timespec t0, t;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (;;) {
    int pending = 0;
    for (int i = 0; i < g_nstreams; i++)
      if (!atomic_load(&g_stream[i].done)) pending = 1;
    if (!pending) return;
    clock_gettime(CLOCK_MONOTONIC, &t);
    if ((t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) * 1e-9 > timeout_s) return;
    nanosleep(&(struct timespec){0, 1000000}, NULL);
  }
}

static void log_abort_handler(int sig, siginfo_t *si, void *uc) {
  ps4_log_drain_sync(1.0);
  const struct sigaction *old = &g_old_abort;
  if (old->sa_flags & SA_SIGINFO) {
    if (old->sa_sigaction) old->sa_sigaction(sig, si, uc);
  } else if (old->sa_handler != SIG_DFL && old->sa_handler != SIG_IGN) {
    old->sa_handler(sig);
  }
  signal(SIGABRT, SIG_DFL);
  raise(SIGABRT);
}

static void log_atexit(void) {
  fflush(NULL);
  ps4_log_drain_sync(1.0);
}

static int same_file(int a, int b) {
  struct stat sa, sb;
  return fstat(a, &sa) == 0 && fstat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static int redirectable(int fd) { return fcntl(fd, F_GETFD) >= 0 && !isatty(fd); }

static void set_cloexec(int fd) { fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC); }

// Replaces `nfds` descriptors (fds[]) by one pipe. Returns 1 on success.
static int redirect(const int *fds, int nfds) {
  LogStream *st = &g_stream[g_nstreams];
  int p[2];
  if (pipe(p) != 0) return 0;
  set_cloexec(p[0]);
  set_cloexec(p[1]);
  int ok = 1;
  for (int j = 0; j < nfds; j++) {
    st->fd[j] = fds[j];
    st->orig[j] = dup(fds[j]);
    if (st->orig[j] < 0) { ok = 0; nfds = j; break; }
    set_cloexec(st->orig[j]);
  }
  st->nfds = nfds;
  st->rd = p[0];
  atomic_store(&st->done, 0);
  pthread_t th;
  if (ok) {
    g_nstreams++; // visible to the sync before the descriptors change
    if (pthread_create(&th, NULL, log_drain_thread, st) != 0) { g_nstreams--; ok = 0; }
  }
  if (!ok) {
    for (int j = 0; j < nfds; j++) close(st->orig[j]);
    close(p[0]);
    close(p[1]);
    return 0;
  }
  pthread_detach(th);
  for (int j = 0; j < nfds; j++) dup2(p[1], fds[j]);
  close(p[1]);
  return 1;
}

__attribute__((constructor)) static void ps4_log_init(void) {
  const char *sync = getenv("PS4_SYNC_LOG");
  if (sync && sync[0] == '1') return;
  int r1 = redirectable(1), r2 = redirectable(2);
  if (r1 && r2 && same_file(1, 2)) {
    const int both[2] = {1, 2};
    redirect(both, 2);
  } else {
    if (r1) { const int f = 1; redirect(&f, 1); }
    if (r2) { const int f = 2; redirect(&f, 1); }
  }
  if (!g_nstreams) return;
#ifdef PS4_LOG_TEST
  // Test-only: lets the host test show that it detects a missing SIGABRT handler.
  if (getenv("PS4_LOG_TEST_NO_ABORT_HANDLER")) { atexit(log_atexit); return; }
#endif
  struct sigaction sa = {0};
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = log_abort_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGABRT, &sa, &g_old_abort);
  atexit(log_atexit);
}
