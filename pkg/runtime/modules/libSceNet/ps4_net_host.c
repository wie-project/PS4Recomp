// SPDX-License-Identifier: GPL-2.0-or-later

// Host side of the guest network stack: shadPS4 core/net/host_net.cpp, macOS backend.

#include "ps4_net_host.h"
#include "ps4_net_types.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

int64_t ps4_net_host_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int64_t ps4_net_host_deadline_from_timeout(int64_t timeout_us) {
    if (timeout_us <= 0) return PS4_NET_NO_DEADLINE;
    return ps4_net_host_now_ns() + timeout_us * 1000LL;
}

// Milliseconds to the deadline for poll(), rounded up so a wait does not end early.
static int timeout_ms(int64_t deadline_ns) {
    if (deadline_ns == PS4_NET_NO_DEADLINE) return -1;
    const int64_t left = deadline_ns - ps4_net_host_now_ns();
    if (left <= 0) return 0;
    const int64_t ms = (left + 999999) / 1000000;
    return ms > INT_MAX ? INT_MAX : (int)ms;
}

static bool expired(int64_t deadline_ns) {
    return deadline_ns != PS4_NET_NO_DEADLINE && ps4_net_host_now_ns() >= deadline_ns;
}

static bool set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool set_cloexec(int fd) {
    const int flags = fcntl(fd, F_GETFD, 0);
    return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

int ps4_net_host_translate(int code) {
    // macOS shares most BSD values with FreeBSD, but not all (ECANCELED is 89 on macOS, 85 on
    // FreeBSD), so map by name as shadPS4 does.
    static const struct {
        int host;
        int orbis;
    } table[] = {
        {EPERM, ORBIS_NET_EPERM},
        {ENOENT, ORBIS_NET_ENOENT},
        {EINTR, ORBIS_NET_EINTR},
        {EIO, ORBIS_NET_EIO},
        {EBADF, ORBIS_NET_EBADF},
        {ENOMEM, ORBIS_NET_ENOMEM},
        {EACCES, ORBIS_NET_EACCES},
        {EFAULT, ORBIS_NET_EFAULT},
        {EBUSY, ORBIS_NET_EBUSY},
        {EEXIST, ORBIS_NET_EEXIST},
        {ENODEV, ORBIS_NET_ENODEV},
        {EINVAL, ORBIS_NET_EINVAL},
        {ENFILE, ORBIS_NET_ENFILE},
        {EMFILE, ORBIS_NET_EMFILE},
        {ENOSPC, ORBIS_NET_ENOSPC},
        {EPIPE, ORBIS_NET_EPIPE},
        {EAGAIN, ORBIS_NET_EAGAIN},
        {EINPROGRESS, ORBIS_NET_EINPROGRESS},
        {EALREADY, ORBIS_NET_EALREADY},
        {ENOTSOCK, ORBIS_NET_ENOTSOCK},
        {EDESTADDRREQ, ORBIS_NET_EDESTADDRREQ},
        {EMSGSIZE, ORBIS_NET_EMSGSIZE},
        {EPROTOTYPE, ORBIS_NET_EPROTOTYPE},
        {ENOPROTOOPT, ORBIS_NET_ENOPROTOOPT},
        {EPROTONOSUPPORT, ORBIS_NET_EPROTONOSUPPORT},
        {ESOCKTNOSUPPORT, ORBIS_NET_ESOCKTNOSUPPORT},
        {EOPNOTSUPP, ORBIS_NET_EOPNOTSUPP},
        {ENOTSUP, ORBIS_NET_EOPNOTSUPP},
        {EPFNOSUPPORT, ORBIS_NET_EPFNOSUPPORT},
        {EAFNOSUPPORT, ORBIS_NET_EAFNOSUPPORT},
        {EADDRINUSE, ORBIS_NET_EADDRINUSE},
        {EADDRNOTAVAIL, ORBIS_NET_EADDRNOTAVAIL},
        {ENETDOWN, ORBIS_NET_ENETDOWN},
        {ENETUNREACH, ORBIS_NET_ENETUNREACH},
        {ENETRESET, ORBIS_NET_ENETRESET},
        {ECONNABORTED, ORBIS_NET_ECONNABORTED},
        {ECONNRESET, ORBIS_NET_ECONNRESET},
        {ENOBUFS, ORBIS_NET_ENOBUFS},
        {EISCONN, ORBIS_NET_EISCONN},
        {ENOTCONN, ORBIS_NET_ENOTCONN},
        {ESHUTDOWN, ORBIS_NET_EPIPE},
        {ETOOMANYREFS, ORBIS_NET_ETOOMANYREFS},
        {ETIMEDOUT, ORBIS_NET_ETIMEDOUT},
        {ECONNREFUSED, ORBIS_NET_ECONNREFUSED},
        {ELOOP, ORBIS_NET_ELOOP},
        {ENAMETOOLONG, ORBIS_NET_ENAMETOOLONG},
        {EHOSTDOWN, ORBIS_NET_EHOSTDOWN},
        {EHOSTUNREACH, ORBIS_NET_EHOSTUNREACH},
        {ENOTEMPTY, ORBIS_NET_ENOTEMPTY},
        {ECANCELED, ORBIS_NET_ECANCELED},
    };
    if (code == 0) return 0;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (table[i].host == code) return table[i].orbis;
    }
    return ORBIS_NET_EINTERNAL;
}

int ps4_net_host_last_error(void) {
    return ps4_net_host_translate(errno);
}

bool ps4_net_host_configure(int s) {
    if (!set_nonblocking(s)) return false;
    set_cloexec(s);
    // Don't let a dead peer SIGPIPE the process.
    const int on = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
    return true;
}

int ps4_net_host_create_socket(int family, int type, int protocol, int *out) {
    const int s = socket(family, type, protocol);
    if (s < 0) return ps4_net_host_last_error();
    if (!ps4_net_host_configure(s)) {
        const int e = ps4_net_host_last_error();
        close(s);
        return e;
    }
    *out = s;
    return 0;
}

int ps4_net_host_create_socket_pair(int family, int type, int protocol, int out[2]) {
    int fds[2];
    if (socketpair(family, type, protocol, fds) != 0) return ps4_net_host_last_error();
    if (!ps4_net_host_configure(fds[0]) || !ps4_net_host_configure(fds[1])) {
        const int e = ps4_net_host_last_error();
        close(fds[0]);
        close(fds[1]);
        return e;
    }
    out[0] = fds[0];
    out[1] = fds[1];
    return 0;
}

int ps4_net_host_prepare_bind(int s, bool reuse_addr, bool reuse_port) {
    int on = reuse_addr ? 1 : 0;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) return ps4_net_host_last_error();
    on = reuse_port ? 1 : 0;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) != 0) return ps4_net_host_last_error();
    return 0;
}

void ps4_net_host_prepare_close(int s, bool guest_blocking) {
    // Darwin's SO_LINGER counts clock ticks; SO_LINGER_SEC is the BSD option in seconds.
    struct linger value = {0, 0};
    socklen_t len = sizeof(value);
    if (getsockopt(s, SOL_SOCKET, SO_LINGER_SEC, &value, &len) != 0 || value.l_onoff == 0 || value.l_linger <= 0) {
        return; // no linger or linger 0, the same in every mode
    }
    if (guest_blocking) {
        // A blocking guest close lingers: let close() block as BSD does.
        const int flags = fcntl(s, F_GETFL, 0);
        if (flags >= 0) fcntl(s, F_SETFL, flags & ~O_NONBLOCK);
        return;
    }
    value.l_onoff = 0;
    setsockopt(s, SOL_SOCKET, SO_LINGER_SEC, &value, sizeof(value));
}

int ps4_net_host_connect(int s, const struct sockaddr *addr, socklen_t len) {
    return connect(s, addr, len) == 0 ? 0 : ps4_net_host_last_error();
}

int ps4_net_host_pending_error(int s) {
    int value = 0;
    socklen_t len = sizeof(value);
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, &value, &len) != 0) return ps4_net_host_last_error();
    return ps4_net_host_translate(value);
}

int ps4_net_host_bytes_readable(int s, int64_t *out) {
    int n = 0;
    if (ioctl(s, FIONREAD, &n) != 0) return ps4_net_host_last_error();
    *out = n;
    return 0;
}

int ps4_net_host_accept(int s, struct sockaddr *addr, socklen_t *len, int *out) {
    for (;;) {
        const int c = accept(s, addr, len);
        if (c < 0) {
            if (errno == EINTR) continue;
            return ps4_net_host_last_error();
        }
        // Whether accept() inherits O_NONBLOCK varies by platform.
        if (!ps4_net_host_configure(c)) {
            const int e = ps4_net_host_last_error();
            close(c);
            return e;
        }
        *out = c;
        return 0;
    }
}

int ps4_net_host_sendto(int s, const void *buf, size_t len, int flags, const struct sockaddr *addr,
                        socklen_t addr_len, int64_t *out) {
    ssize_t n;
    do {
        n = addr ? sendto(s, buf, len, flags, addr, addr_len) : send(s, buf, len, flags);
    } while (n < 0 && errno == EINTR);
    if (n < 0) return ps4_net_host_last_error();
    *out = n;
    return 0;
}

int ps4_net_host_recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *addr,
                          socklen_t *addr_len, int64_t *out) {
    ssize_t n;
    do {
        n = addr ? recvfrom(s, buf, len, flags, addr, addr_len) : recv(s, buf, len, flags);
    } while (n < 0 && errno == EINTR);
    if (n < 0) return ps4_net_host_last_error();
    *out = n;
    return 0;
}

bool ps4_net_wake_init(Ps4NetWake *w) {
    int fds[2];
    w->read_fd = w->write_fd = -1;
    if (pipe(fds) != 0) return false;
    w->read_fd = fds[0];
    w->write_fd = fds[1];
    for (int i = 0; i < 2; i++) {
        if (!set_nonblocking(fds[i]) || !set_cloexec(fds[i])) {
            ps4_net_wake_destroy(w);
            return false;
        }
    }
    return true;
}

void ps4_net_wake_destroy(Ps4NetWake *w) {
    if (w->read_fd >= 0) close(w->read_fd);
    if (w->write_fd >= 0) close(w->write_fd);
    w->read_fd = w->write_fd = -1;
}

void ps4_net_wake_signal(const Ps4NetWake *w) {
    // Fails only when the pipe is full, which means it is signalled already.
    const char byte = 1;
    (void)!write(w->write_fd, &byte, 1);
}

void ps4_net_wake_drain(const Ps4NetWake *w) {
    char buf[64];
    while (read(w->read_fd, buf, sizeof(buf)) > 0) {
    }
}

Ps4NetWaitResult ps4_net_host_wait_one(int s, uint32_t interest, const Ps4NetWake *wake, int64_t deadline_ns) {
    struct pollfd fds[2];
    fds[0].fd = s;
    fds[0].events = (short)(((interest & PS4_NET_READABLE) ? POLLIN : 0) | ((interest & PS4_NET_WRITABLE) ? POLLOUT : 0));
    fds[1].fd = wake->read_fd;
    fds[1].events = POLLIN;
    for (;;) {
        fds[0].revents = fds[1].revents = 0;
        const int n = poll(fds, 2, timeout_ms(deadline_ns));
        if (n < 0) {
            if (errno == EINTR) continue;
            return PS4_NET_WAIT_FAILED;
        }
        if (fds[1].revents != 0) return PS4_NET_WAIT_WOKEN; // abort wins
        if (fds[0].revents != 0) return PS4_NET_WAIT_READY; // the retry reports any error
        if (expired(deadline_ns)) return PS4_NET_WAIT_TIMED_OUT;
    }
}

bool ps4_net_host_epoll_init(Ps4NetHostEpoll *ep) {
    ep->kq = -1;
    if (!ps4_net_wake_init(&ep->wake)) return false;
    ep->kq = kqueue();
    if (ep->kq < 0) {
        ps4_net_wake_destroy(&ep->wake);
        return false;
    }
    set_cloexec(ep->kq);
    struct kevent kev;
    EV_SET(&kev, ep->wake.read_fd, EVFILT_READ, EV_ADD, 0, 0, (void *)(uintptr_t)PS4_NET_RESERVED_TAG);
    kevent(ep->kq, &kev, 1, NULL, 0, NULL);
    return true;
}

void ps4_net_host_epoll_destroy(Ps4NetHostEpoll *ep) {
    if (ep->kq >= 0) close(ep->kq);
    ep->kq = -1;
    ps4_net_wake_destroy(&ep->wake);
}

int ps4_net_host_epoll_set(Ps4NetHostEpoll *ep, int s, uint64_t tag, uint32_t events) {
    const uint16_t arm = EV_ADD | EV_ENABLE | EV_RECEIPT;
    const uint16_t disarm = EV_DELETE | EV_RECEIPT;
    // With neither EvIn nor EvOut epoll would still report ERR/HUP, kqueue reports nothing (as
    // in shadPS4).
    struct kevent changes[2];
    struct kevent results[2];
    EV_SET(&changes[0], s, EVFILT_READ, (events & PS4_NET_EV_IN) ? arm : disarm, 0, 0, (void *)(uintptr_t)tag);
    EV_SET(&changes[1], s, EVFILT_WRITE, (events & PS4_NET_EV_OUT) ? arm : disarm, 0, 0, (void *)(uintptr_t)tag);
    const int n = kevent(ep->kq, changes, 2, results, 2, NULL);
    if (n < 0) return ps4_net_host_last_error();
    for (int i = 0; i < n; i++) {
        // EV_RECEIPT gives an EV_ERROR entry per change, data 0 on success. ENOENT is deleting a
        // filter that was never added.
        if ((results[i].flags & EV_ERROR) && results[i].data != 0 && results[i].data != ENOENT) {
            return ps4_net_host_translate((int)results[i].data);
        }
    }
    return 0;
}

int ps4_net_host_epoll_remove(Ps4NetHostEpoll *ep, int s) {
    return ps4_net_host_epoll_set(ep, s, 0, 0);
}

int ps4_net_host_epoll_wait(Ps4NetHostEpoll *ep, Ps4NetHostEvent *out, int cap, int64_t deadline_ns, int *count,
                            bool *woken) {
    struct kevent buf[PS4_NET_HOST_EPOLL_MAX_EVENTS];
    if (cap > PS4_NET_HOST_EPOLL_MAX_EVENTS) cap = PS4_NET_HOST_EPOLL_MAX_EVENTS;
    *count = 0;
    *woken = false;
    if (cap <= 0) return ORBIS_NET_EINVAL;
    for (;;) {
        struct timespec ts;
        struct timespec *pts = NULL;
        if (deadline_ns != PS4_NET_NO_DEADLINE) {
            int64_t left = deadline_ns - ps4_net_host_now_ns();
            if (left < 0) left = 0;
            ts.tv_sec = (time_t)(left / 1000000000LL);
            ts.tv_nsec = (long)(left % 1000000000LL);
            pts = &ts;
        }
        const int n = kevent(ep->kq, NULL, 0, buf, cap, pts);
        if (n < 0) {
            if (errno == EINTR) continue;
            return ps4_net_host_last_error();
        }
        for (int i = 0; i < n; i++) {
            const uint64_t tag = (uint64_t)(uintptr_t)buf[i].udata;
            if (tag == PS4_NET_RESERVED_TAG) {
                *woken = true;
                continue;
            }
            uint32_t ev = buf[i].filter == EVFILT_READ ? PS4_NET_EV_IN : PS4_NET_EV_OUT;
            if (buf[i].flags & EV_EOF) {
                ev |= PS4_NET_EV_HUP;
                if (buf[i].fflags != 0) ev |= PS4_NET_EV_ERR; // fflags holds the socket error
            }
            // kqueue reports read and write separately, merge them.
            bool merged = false;
            for (int j = 0; j < *count; j++) {
                if (out[j].tag == tag) {
                    out[j].events |= ev;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                out[*count].tag = tag;
                out[*count].events = ev;
                (*count)++;
            }
        }
        if (n > 0 || expired(deadline_ns)) return 0;
    }
}
