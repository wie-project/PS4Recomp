// SPDX-License-Identifier: GPL-2.0-or-later

// Guest network objects: shadPS4 core/net/guest_net.cpp without the P2P transport.
//
// Objects are reference counted. The table holds one reference per id; a blocking call holds
// another while it waits, so closing an object wakes its waiters and the host descriptor is
// closed when the last of them has returned. Until then the id stays reserved
// (ps4_net_id_reserved), so the kernel does not close a descriptor that is still in use.

#include "ps4_net_core.h"
#include "ps4_net_host.h"
#include "ps4_net_types.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Blocking calls cut short by abort or close fail with EINTR.
#define ABORTED_ERROR ORBIS_NET_EINTR
// Max bytes per host send() on a stream socket.
#define MAX_STREAM_SEND_CHUNK (64 * 1024)

enum { SIDE_RECV = 0, SIDE_SEND = 1 };

typedef struct NetObject NetObject;
struct NetObject {
    Ps4NetObjectKind kind;
    atomic_int refs;
    uint32_t generation; // tells reused ids apart in epoll tags
    void (*destroy)(NetObject *);
};

static atomic_uint g_next_generation = 1;

static void obj_retain(NetObject *o) {
    atomic_fetch_add_explicit(&o->refs, 1, memory_order_relaxed);
}

static void obj_release(NetObject *o) {
    if (o && atomic_fetch_sub_explicit(&o->refs, 1, memory_order_acq_rel) == 1) o->destroy(o);
}

// A blocking call in progress, on the caller's stack.
typedef struct NetWaiter NetWaiter;
struct NetWaiter {
    const Ps4NetWake *wake;
    int side;
    bool aborted; // owner's wait mutex
    NetWaiter *next;
};

typedef struct NetEpoll NetEpoll;

// The epolls an object is registered with (each holds a reference).
typedef struct EpollList {
    NetEpoll **items;
    int count;
    int cap;
} EpollList;

typedef struct NetSocket {
    NetObject hdr;
    int native;
    int type;
    atomic_bool nonblocking;
    atomic_bool reuse_addr;
    atomic_bool reuse_port;
    _Atomic int64_t rcv_timeout_us; // 0 = no timeout
    _Atomic int64_t snd_timeout_us;
    _Atomic int64_t connect_timeout_us;
    _Atomic int64_t accept_timeout_us;
    atomic_bool connected; // also set while connecting
    atomic_bool listening;

    pthread_mutex_t attr_mutex;
    Ps4NetSocketAttributes attributes;
    uint8_t (*pending_memberships)[8];
    int pending_count;
    int pending_cap;

    pthread_mutex_t wait_mutex;
    NetWaiter *waiters;
    bool pending_abort[2]; // preserved aborts, by side
    bool send_again;       // SND_PRESERVATION_AGAIN
    bool closing;

    // Lock order: NetEpoll.mutex first.
    pthread_mutex_t reg_mutex;
    bool closed;
    EpollList epolls;
} NetSocket;

typedef struct NetReg {
    int32_t id;
    uint32_t generation;
    uint64_t data;
    uint32_t events;
    int native; // the descriptor registered with the kqueue
    bool external;
    bool armed;
    bool reported;
} NetReg;

typedef struct HupEntry {
    int32_t id;
    uint32_t generation;
} HupEntry;

struct NetEpoll {
    NetObject hdr;
    Ps4NetHostEpoll host;
    pthread_mutex_t mutex;
    NetReg *regs;
    int reg_count;
    int reg_cap;
    NetWaiter *waiters;
    int flagged_waiters;
    bool pending_abort;
    bool destroyed;
    HupEntry *hups;
    int hup_count;
    int hup_cap;
};

typedef struct NetExternal {
    NetObject hdr;
    Ps4NetWake done; // readable once signalled; the id is its read end
    atomic_bool signaled;
    atomic_bool hangup;
    pthread_mutex_t reg_mutex;
    bool closed;
    EpollList epolls;
} NetExternal;

// ---------------------------------------------------------------------------------------------
// Id table

typedef struct NetEntry {
    NetObject *obj;
    bool counted;
    bool owns_fd;  // a duplicate id: its own host descriptor, closed with the entry
    bool reserved; // removed, the descriptor still open until the object is destroyed
} NetEntry;

static pthread_mutex_t g_table_mutex = PTHREAD_MUTEX_INITIALIZER;
static NetEntry *g_entries;
static int g_entry_cap;
static int g_counted;
// Slots holding an object or a reserved id. The kernel asks about every descriptor it reads,
// writes or closes; with no net object at all that answer needs no lock.
static atomic_int g_used_slots;

static bool slot_used(const NetEntry *e) {
    return e->obj || e->reserved;
}

// Replaces a slot, keeping g_used_slots in step. Caller holds g_table_mutex.
static void slot_set(int32_t id, NetEntry entry) {
    const int delta = (slot_used(&entry) ? 1 : 0) - (slot_used(&g_entries[id]) ? 1 : 0);
    g_entries[id] = entry;
    if (delta) atomic_fetch_add_explicit(&g_used_slots, delta, memory_order_relaxed);
}

static bool table_grow(int id) {
    if (id < g_entry_cap) return true;
    int cap = g_entry_cap ? g_entry_cap : 256;
    while (cap <= id) cap *= 2;
    NetEntry *grown = (NetEntry *)realloc(g_entries, (size_t)cap * sizeof(NetEntry));
    if (!grown) return false;
    memset(grown + g_entry_cap, 0, (size_t)(cap - g_entry_cap) * sizeof(NetEntry));
    g_entries = grown;
    g_entry_cap = cap;
    return true;
}

// Takes over the caller's reference on success.
static int64_t table_insert(int32_t id, NetObject *obj, bool counted, bool owns_fd) {
    pthread_mutex_lock(&g_table_mutex);
    if (counted && g_counted >= PS4_NET_MAX_SOCKETS) {
        pthread_mutex_unlock(&g_table_mutex);
        return -ORBIS_NET_EMFILE;
    }
    if (id < 0 || !table_grow(id) || g_entries[id].obj) {
        pthread_mutex_unlock(&g_table_mutex);
        return -ORBIS_NET_ENFILE;
    }
    slot_set(id, (NetEntry){obj, counted, owns_fd, false});
    g_counted += counted ? 1 : 0;
    pthread_mutex_unlock(&g_table_mutex);
    return id;
}

// A new reference to the object of the given kind, or NULL.
static NetObject *table_get(int32_t id, Ps4NetObjectKind kind) {
    NetObject *obj = NULL;
    pthread_mutex_lock(&g_table_mutex);
    if (id >= 0 && id < g_entry_cap && g_entries[id].obj && g_entries[id].obj->kind == kind) {
        obj = g_entries[id].obj;
        obj_retain(obj);
    }
    pthread_mutex_unlock(&g_table_mutex);
    return obj;
}

// Removes the id and returns the table's reference, or NULL.
static NetObject *table_remove(int32_t id, Ps4NetObjectKind kind) {
    NetObject *obj = NULL;
    bool owns_fd = false;
    pthread_mutex_lock(&g_table_mutex);
    if (id >= 0 && id < g_entry_cap && g_entries[id].obj && g_entries[id].obj->kind == kind) {
        obj = g_entries[id].obj;
        owns_fd = g_entries[id].owns_fd;
        g_counted -= g_entries[id].counted ? 1 : 0;
        // The object's own descriptor stays open until it is destroyed.
        slot_set(id, (NetEntry){NULL, false, false, !owns_fd});
    }
    pthread_mutex_unlock(&g_table_mutex);
    if (owns_fd) close(id);
    return obj;
}

// Called by an object's destroy right before it closes its descriptor.
static void table_unreserve(int32_t id) {
    pthread_mutex_lock(&g_table_mutex);
    if (id >= 0 && id < g_entry_cap && !g_entries[id].obj) slot_set(id, (NetEntry){NULL, false, false, false});
    pthread_mutex_unlock(&g_table_mutex);
}

bool ps4_net_id_reserved(int32_t id) {
    if (atomic_load_explicit(&g_used_slots, memory_order_relaxed) == 0) return false;
    pthread_mutex_lock(&g_table_mutex);
    const bool reserved = id >= 0 && id < g_entry_cap && g_entries[id].reserved;
    pthread_mutex_unlock(&g_table_mutex);
    return reserved;
}

Ps4NetObjectKind ps4_net_object_kind(int32_t id) {
    Ps4NetObjectKind kind = PS4_NET_OBJ_NONE;
    if (atomic_load_explicit(&g_used_slots, memory_order_relaxed) == 0) return kind;
    pthread_mutex_lock(&g_table_mutex);
    if (id >= 0 && id < g_entry_cap && g_entries[id].obj) kind = g_entries[id].obj->kind;
    pthread_mutex_unlock(&g_table_mutex);
    return kind;
}

int ps4_net_list_sockets(int32_t *out, int cap) {
    int n = 0;
    pthread_mutex_lock(&g_table_mutex);
    for (int id = 0; id < g_entry_cap; id++) {
        if (g_entries[id].obj && g_entries[id].obj->kind == PS4_NET_OBJ_SOCKET) {
            if (n < cap) out[n] = id;
            n++;
        }
    }
    pthread_mutex_unlock(&g_table_mutex);
    return n;
}

int64_t ps4_net_duplicate_object(int32_t old_id, int32_t new_id) {
    pthread_mutex_lock(&g_table_mutex);
    if (old_id < 0 || old_id >= g_entry_cap || !g_entries[old_id].obj || new_id < 0 || !table_grow(new_id) ||
        g_entries[new_id].obj) {
        pthread_mutex_unlock(&g_table_mutex);
        return -ORBIS_NET_EBADF;
    }
    NetObject *obj = g_entries[old_id].obj;
    obj_retain(obj);
    slot_set(new_id, (NetEntry){obj, g_entries[old_id].counted, true, false});
    g_counted += g_entries[new_id].counted ? 1 : 0;
    pthread_mutex_unlock(&g_table_mutex);
    return new_id;
}

// ---------------------------------------------------------------------------------------------
// Per-thread wake handle: abort and close signal it to interrupt a blocking call.

static pthread_key_t g_wake_key;
static pthread_once_t g_wake_once = PTHREAD_ONCE_INIT;

static void wake_free(void *p) {
    Ps4NetWake *w = (Ps4NetWake *)p;
    ps4_net_wake_destroy(w);
    free(w);
}

static void wake_key_init(void) {
    pthread_key_create(&g_wake_key, wake_free);
}

static const Ps4NetWake *thread_wake(void) {
    pthread_once(&g_wake_once, wake_key_init);
    Ps4NetWake *w = (Ps4NetWake *)pthread_getspecific(g_wake_key);
    if (w) return w;
    w = (Ps4NetWake *)malloc(sizeof(*w));
    if (!w) return NULL;
    if (!ps4_net_wake_init(w)) {
        free(w);
        return NULL;
    }
    pthread_setspecific(g_wake_key, w);
    return w;
}

// ---------------------------------------------------------------------------------------------
// Epoll lists

static bool epoll_list_add(EpollList *l, NetEpoll *ep) {
    if (l->count == l->cap) {
        const int cap = l->cap ? l->cap * 2 : 4;
        NetEpoll **items = (NetEpoll **)realloc(l->items, (size_t)cap * sizeof(*items));
        if (!items) return false;
        l->items = items;
        l->cap = cap;
    }
    obj_retain(&ep->hdr);
    l->items[l->count++] = ep;
    return true;
}

// Drops ep from the list; returns whether it was there.
static bool epoll_list_remove(EpollList *l, NetEpoll *ep) {
    for (int i = 0; i < l->count; i++) {
        if (l->items[i] == ep) {
            l->items[i] = l->items[--l->count];
            obj_release(&ep->hdr);
            return true;
        }
    }
    return false;
}

// Moves the list out (the caller releases each entry).
static EpollList epoll_list_take(EpollList *l) {
    EpollList taken = *l;
    memset(l, 0, sizeof(*l));
    return taken;
}

static void epoll_list_release(EpollList *l) {
    for (int i = 0; i < l->count; i++) obj_release(&l->items[i]->hdr);
    free(l->items);
    memset(l, 0, sizeof(*l));
}

static uint64_t make_tag(int32_t id, uint32_t generation) {
    return ((uint64_t)generation << 32) | (uint32_t)id;
}

static NetReg *epoll_find(NetEpoll *ep, int32_t id) {
    for (int i = 0; i < ep->reg_count; i++) {
        if (ep->regs[i].id == id) return &ep->regs[i];
    }
    return NULL;
}

static bool epoll_add_reg(NetEpoll *ep, const NetReg *reg) {
    if (ep->reg_count == ep->reg_cap) {
        const int cap = ep->reg_cap ? ep->reg_cap * 2 : 8;
        NetReg *regs = (NetReg *)realloc(ep->regs, (size_t)cap * sizeof(NetReg));
        if (!regs) return false;
        ep->regs = regs;
        ep->reg_cap = cap;
    }
    ep->regs[ep->reg_count++] = *reg;
    return true;
}

static void epoll_erase_reg(NetEpoll *ep, NetReg *reg) {
    *reg = ep->regs[--ep->reg_count];
}

// ---------------------------------------------------------------------------------------------
// Objects

static void socket_destroy(NetObject *o) {
    NetSocket *s = (NetSocket *)o;
    if (s->type == SOCK_STREAM) ps4_net_host_prepare_close(s->native, !atomic_load(&s->nonblocking));
    epoll_list_release(&s->epolls);
    free(s->pending_memberships);
    pthread_mutex_destroy(&s->attr_mutex);
    pthread_mutex_destroy(&s->wait_mutex);
    pthread_mutex_destroy(&s->reg_mutex);
    table_unreserve(s->native);
    close(s->native);
    free(s);
}

static NetSocket *socket_new(int native, int type) {
    NetSocket *s = (NetSocket *)calloc(1, sizeof(NetSocket));
    if (!s) return NULL;
    s->hdr.kind = PS4_NET_OBJ_SOCKET;
    atomic_init(&s->hdr.refs, 1);
    s->hdr.generation = atomic_fetch_add(&g_next_generation, 1);
    s->hdr.destroy = socket_destroy;
    s->native = native;
    s->type = type;
    s->attributes.priority = 16; // shadPS4's value until set
    pthread_mutex_init(&s->attr_mutex, NULL);
    pthread_mutex_init(&s->wait_mutex, NULL);
    pthread_mutex_init(&s->reg_mutex, NULL);
    return s;
}

// Frees a socket that never made it into the table.
static void socket_discard(NetSocket *s) {
    close(s->native);
    pthread_mutex_destroy(&s->attr_mutex);
    pthread_mutex_destroy(&s->wait_mutex);
    pthread_mutex_destroy(&s->reg_mutex);
    free(s);
}

static int64_t socket_insert(NetSocket *s) {
    const int64_t r = table_insert(s->native, &s->hdr, true, false);
    if (r < 0) socket_discard(s);
    return r;
}

static NetSocket *get_socket(int32_t id) {
    return (NetSocket *)table_get(id, PS4_NET_OBJ_SOCKET);
}

static void epoll_destroy_obj(NetObject *o) {
    NetEpoll *ep = (NetEpoll *)o;
    free(ep->regs);
    free(ep->hups);
    pthread_mutex_destroy(&ep->mutex);
    table_unreserve(ep->host.kq);
    ps4_net_host_epoll_destroy(&ep->host);
    free(ep);
}

static NetEpoll *get_epoll(int32_t id) {
    return (NetEpoll *)table_get(id, PS4_NET_OBJ_EPOLL);
}

static void external_destroy(NetObject *o) {
    NetExternal *x = (NetExternal *)o;
    epoll_list_release(&x->epolls);
    pthread_mutex_destroy(&x->reg_mutex);
    table_unreserve(x->done.read_fd);
    ps4_net_wake_destroy(&x->done);
    free(x);
}

static NetExternal *get_external(int32_t id) {
    return (NetExternal *)table_get(id, PS4_NET_OBJ_EXTERNAL);
}

int64_t ps4_net_reserve_external_id(void) {
    NetExternal *x = (NetExternal *)calloc(1, sizeof(NetExternal));
    if (!x) return -ORBIS_NET_ENOMEM;
    if (!ps4_net_wake_init(&x->done)) {
        free(x);
        return -ORBIS_NET_ENOBUFS;
    }
    x->hdr.kind = PS4_NET_OBJ_EXTERNAL;
    atomic_init(&x->hdr.refs, 1);
    x->hdr.generation = atomic_fetch_add(&g_next_generation, 1);
    x->hdr.destroy = external_destroy;
    pthread_mutex_init(&x->reg_mutex, NULL);
    const int64_t r = table_insert(x->done.read_fd, &x->hdr, true, false);
    if (r < 0) {
        ps4_net_wake_destroy(&x->done);
        pthread_mutex_destroy(&x->reg_mutex);
        free(x);
    }
    return r;
}

int64_t ps4_net_signal_external(int32_t id, bool hangup) {
    NetExternal *x = get_external(id);
    if (!x) return -ORBIS_NET_EBADF;
    atomic_store(&x->hangup, hangup);
    atomic_store(&x->signaled, true);
    ps4_net_wake_signal(&x->done);
    obj_release(&x->hdr);
    return 0;
}

int64_t ps4_net_release_external_id(int32_t id) {
    NetExternal *x = (NetExternal *)table_remove(id, PS4_NET_OBJ_EXTERNAL);
    if (!x) return -ORBIS_NET_EBADF;
    pthread_mutex_lock(&x->reg_mutex);
    x->closed = true;
    EpollList epolls = epoll_list_take(&x->epolls);
    pthread_mutex_unlock(&x->reg_mutex);
    for (int i = 0; i < epolls.count; i++) {
        NetEpoll *ep = epolls.items[i];
        pthread_mutex_lock(&ep->mutex);
        NetReg *reg = epoll_find(ep, id);
        if (reg && reg->generation == x->hdr.generation) {
            if (reg->armed) ps4_net_host_epoll_remove(&ep->host, reg->native);
            epoll_erase_reg(ep, reg);
        }
        pthread_mutex_unlock(&ep->mutex);
    }
    epoll_list_release(&epolls);
    obj_release(&x->hdr);
    return 0;
}

int64_t ps4_net_close_object(int32_t id) {
    switch (ps4_net_object_kind(id)) {
    case PS4_NET_OBJ_EPOLL:
        return ps4_net_epoll_destroy(id);
    case PS4_NET_OBJ_EXTERNAL:
        return -ORBIS_NET_EBADF; // released by its owner
    default:
        return ps4_net_socket_close(id); // EBADF for unknown ids
    }
}

// ---------------------------------------------------------------------------------------------
// Blocking

static bool consume_pending_abort(NetSocket *s, int side) {
    pthread_mutex_lock(&s->wait_mutex);
    const bool pending = s->pending_abort[side];
    s->pending_abort[side] = false;
    pthread_mutex_unlock(&s->wait_mutex);
    return pending;
}

typedef struct WaitScope {
    NetSocket *s;
    NetWaiter waiter;
    int entry_error;
    bool registered;
} WaitScope;

static void wait_scope_enter(WaitScope *w, NetSocket *s, int side) {
    memset(w, 0, sizeof(*w));
    w->s = s;
    w->waiter.wake = thread_wake();
    w->waiter.side = side;
    if (!w->waiter.wake) {
        w->entry_error = ORBIS_NET_ENOBUFS;
        return;
    }
    pthread_mutex_lock(&s->wait_mutex);
    ps4_net_wake_drain(w->waiter.wake); // a stale signal from a previous call
    if (s->closing) {
        w->entry_error = ORBIS_NET_EBADF;
    } else if (s->pending_abort[side]) {
        s->pending_abort[side] = false;
        w->entry_error = ABORTED_ERROR;
    } else {
        w->waiter.next = s->waiters;
        s->waiters = &w->waiter;
        w->registered = true;
    }
    pthread_mutex_unlock(&s->wait_mutex);
}

static void wait_scope_leave(WaitScope *w) {
    if (!w->registered) return;
    pthread_mutex_lock(&w->s->wait_mutex);
    for (NetWaiter **p = &w->s->waiters; *p; p = &(*p)->next) {
        if (*p == &w->waiter) {
            *p = w->waiter.next;
            break;
        }
    }
    pthread_mutex_unlock(&w->s->wait_mutex);
}

static bool wait_scope_interrupted(WaitScope *w) {
    pthread_mutex_lock(&w->s->wait_mutex);
    const bool aborted = w->waiter.aborted;
    pthread_mutex_unlock(&w->s->wait_mutex);
    return aborted;
}

// Called with wait_mutex held. Sets blocked[side] for each side that had a blocked call.
static void interrupt_waiters(NetSocket *s, bool blocked[2]) {
    for (NetWaiter *w = s->waiters; w; w = w->next) {
        w->aborted = true;
        ps4_net_wake_signal(w->wake);
        blocked[w->side] = true;
    }
}

typedef int64_t (*NetOp)(void *arg);

static int64_t run_blocking(NetSocket *s, int side, uint32_t interest, int64_t deadline_ns, bool blocking, NetOp op,
                            void *arg) {
    if (!blocking) {
        if (consume_pending_abort(s, side)) return -ABORTED_ERROR;
        return op(arg);
    }
    WaitScope scope;
    wait_scope_enter(&scope, s, side);
    if (scope.entry_error != 0) return -scope.entry_error;
    int64_t result;
    for (;;) {
        if (wait_scope_interrupted(&scope)) {
            result = -ABORTED_ERROR;
            break;
        }
        result = op(arg);
        if (result != -ORBIS_NET_EWOULDBLOCK) break;
        const Ps4NetWaitResult w = ps4_net_host_wait_one(s->native, interest, scope.waiter.wake, deadline_ns);
        if (w == PS4_NET_WAIT_READY || w == PS4_NET_WAIT_WOKEN) continue;
        // BSD: an expired SO_RCVTIMEO/SO_SNDTIMEO is EAGAIN, not ETIMEDOUT.
        result = w == PS4_NET_WAIT_TIMED_OUT ? -ORBIS_NET_EWOULDBLOCK : -ps4_net_host_last_error();
        break;
    }
    wait_scope_leave(&scope);
    return result;
}

static int64_t socket_deadline(_Atomic int64_t *timeout_us) {
    return ps4_net_host_deadline_from_timeout(atomic_load(timeout_us));
}

// ---------------------------------------------------------------------------------------------
// Sockets

int64_t ps4_net_socket_create(int family, int type, int protocol) {
    // FreeBSD returns EPROTONOSUPPORT for a mismatched protocol, macOS EPROTOTYPE.
    if ((family == AF_INET || family == AF_INET6) && protocol != 0 &&
        ((type == SOCK_STREAM && protocol != IPPROTO_TCP) || (type == SOCK_DGRAM && protocol != IPPROTO_UDP))) {
        return -ORBIS_NET_EPROTONOSUPPORT;
    }
    int native;
    const int e = ps4_net_host_create_socket(family, type, protocol, &native);
    if (e != 0) return -e;
    NetSocket *s = socket_new(native, type);
    if (!s) {
        close(native);
        return -ORBIS_NET_ENOMEM;
    }
    return socket_insert(s);
}

int64_t ps4_net_socket_create_pair(int family, int type, int protocol, int32_t ids[2]) {
    int natives[2];
    const int e = ps4_net_host_create_socket_pair(family, type, protocol, natives);
    if (e != 0) return -e;
    NetSocket *a = socket_new(natives[0], type);
    NetSocket *b = socket_new(natives[1], type);
    if (!a || !b) {
        if (a) socket_discard(a); else close(natives[0]);
        if (b) socket_discard(b); else close(natives[1]);
        return -ORBIS_NET_ENOMEM;
    }
    const int64_t ra = socket_insert(a);
    if (ra < 0) {
        socket_discard(b);
        return ra;
    }
    const int64_t rb = socket_insert(b);
    if (rb < 0) {
        ps4_net_socket_close((int32_t)ra);
        return rb;
    }
    ids[0] = (int32_t)ra;
    ids[1] = (int32_t)rb;
    return 0;
}

int64_t ps4_net_socket_bind(int32_t id, const struct sockaddr *addr, socklen_t len) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    int e = ps4_net_host_prepare_bind(s->native, atomic_load(&s->reuse_addr), atomic_load(&s->reuse_port));
    if (e == 0) e = bind(s->native, addr, len) == 0 ? 0 : ps4_net_host_last_error();
    obj_release(&s->hdr);
    return -e;
}

int64_t ps4_net_socket_listen(int32_t id, int backlog) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    const int e = listen(s->native, backlog) == 0 ? 0 : ps4_net_host_last_error();
    if (e == 0) atomic_store(&s->listening, true);
    obj_release(&s->hdr);
    return -e;
}

static int64_t socket_connect(NetSocket *s, const struct sockaddr *addr, socklen_t len) {
    if (atomic_load(&s->listening)) return -ORBIS_NET_EOPNOTSUPP;
    if (atomic_load(&s->nonblocking)) {
        if (consume_pending_abort(s, SIDE_SEND)) return -ABORTED_ERROR;
        const int e = ps4_net_host_connect(s->native, addr, len);
        if (e == 0 || e == ORBIS_NET_EINPROGRESS) atomic_store(&s->connected, true);
        return -e;
    }
    // Register first so a preserved abort stops it before it starts.
    WaitScope scope;
    wait_scope_enter(&scope, s, SIDE_SEND);
    if (scope.entry_error != 0) return -scope.entry_error;
    int64_t result;
    const int started = ps4_net_host_connect(s->native, addr, len);
    if (started == 0 || started == ORBIS_NET_EINPROGRESS) atomic_store(&s->connected, true);
    if (started != ORBIS_NET_EINPROGRESS) {
        result = -started;
    } else {
        // Poll in 500 ms slices (as shadPS4). On SO_CONNECTTIMEO expiry return EWOULDBLOCK and
        // leave the connect running.
        const int64_t deadline = socket_deadline(&s->connect_timeout_us);
        for (;;) {
            if (wait_scope_interrupted(&scope)) {
                result = -ABORTED_ERROR;
                break;
            }
            const int64_t now = ps4_net_host_now_ns();
            int64_t slice = now + 500LL * 1000000LL;
            if (deadline < slice) {
                if (now >= deadline) {
                    result = -ORBIS_NET_EWOULDBLOCK;
                    break;
                }
                slice = deadline;
            }
            const Ps4NetWaitResult w = ps4_net_host_wait_one(s->native, PS4_NET_WRITABLE, scope.waiter.wake, slice);
            if (w == PS4_NET_WAIT_FAILED) {
                result = -ps4_net_host_last_error();
                break;
            }
            if (w == PS4_NET_WAIT_WOKEN) continue;
            const int pending = ps4_net_host_pending_error(s->native);
            if (pending != 0) {
                result = -pending;
                break;
            }
            if (w == PS4_NET_WAIT_READY) {
                result = 0;
                break;
            }
        }
    }
    wait_scope_leave(&scope);
    return result;
}

int64_t ps4_net_socket_connect(int32_t id, const struct sockaddr *addr, socklen_t len) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    const int64_t r = socket_connect(s, addr, len);
    obj_release(&s->hdr);
    return r;
}

typedef struct AcceptOp {
    NetSocket *s;
    struct sockaddr *addr;
    socklen_t *len;
    int client;
} AcceptOp;

static int64_t accept_op(void *arg) {
    AcceptOp *a = (AcceptOp *)arg;
    const int e = ps4_net_host_accept(a->s->native, a->addr, a->len, &a->client);
    return -e;
}

int64_t ps4_net_socket_accept(int32_t id, struct sockaddr *addr, socklen_t *len) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    AcceptOp op = {s, addr, len, -1};
    int64_t r = run_blocking(s, SIDE_RECV, PS4_NET_READABLE, socket_deadline(&s->accept_timeout_us),
                             !atomic_load(&s->nonblocking), accept_op, &op);
    if (r == 0) {
        NetSocket *c = socket_new(op.client, s->type);
        if (!c) {
            close(op.client);
            r = -ORBIS_NET_ENOMEM;
        } else {
            atomic_store(&c->nonblocking, atomic_load(&s->nonblocking));
            atomic_store(&c->reuse_addr, atomic_load(&s->reuse_addr));
            atomic_store(&c->reuse_port, atomic_load(&s->reuse_port));
            atomic_store(&c->rcv_timeout_us, atomic_load(&s->rcv_timeout_us));
            atomic_store(&c->snd_timeout_us, atomic_load(&s->snd_timeout_us));
            atomic_store(&c->connect_timeout_us, atomic_load(&s->connect_timeout_us));
            atomic_store(&c->accept_timeout_us, atomic_load(&s->accept_timeout_us));
            atomic_store(&c->connected, true);
            pthread_mutex_lock(&s->attr_mutex);
            c->attributes = s->attributes;
            pthread_mutex_unlock(&s->attr_mutex);
            r = socket_insert(c);
        }
    }
    obj_release(&s->hdr);
    return r;
}

static bool is_limited_broadcast(const struct sockaddr *addr, socklen_t len) {
    if (!addr || addr->sa_family != AF_INET || len < (socklen_t)sizeof(struct sockaddr_in)) return false;
    return ((const struct sockaddr_in *)addr)->sin_addr.s_addr == htonl(INADDR_BROADCAST);
}

static bool broadcast_allowed(int s) {
    int on = 0;
    socklen_t len = sizeof(on);
    return getsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, &len) == 0 && on != 0;
}

typedef struct SendOp {
    NetSocket *s;
    const uint8_t *bytes;
    size_t len;
    size_t sent;
    int flags;
    const struct sockaddr *addr;
    socklen_t addr_len;
} SendOp;

static int64_t send_op(void *arg) {
    SendOp *o = (SendOp *)arg;
    size_t chunk = o->len - o->sent;
    if (chunk > MAX_STREAM_SEND_CHUNK) chunk = MAX_STREAM_SEND_CHUNK;
    int64_t n = 0;
    const int e = ps4_net_host_sendto(o->s->native, o->bytes + o->sent, chunk, o->flags, o->addr, o->addr_len, &n);
    return e ? -e : n;
}

int64_t ps4_net_socket_sendto(int32_t id, const void *buf, size_t len, int host_flags, bool dontwait,
                              const struct sockaddr *addr, socklen_t addr_len) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    int64_t result;
    if (s->type != SOCK_STREAM) {
        // UDP/RAW sends never block on the PS4: a full buffer silently drops the datagram.
        if (consume_pending_abort(s, SIDE_SEND)) {
            result = -ABORTED_ERROR;
        } else if (is_limited_broadcast(addr, addr_len) && !broadcast_allowed(s->native)) {
            // FreeBSD gives EACCES without SO_BROADCAST; macOS would say EHOSTUNREACH.
            result = -ORBIS_NET_EACCES;
        } else {
            int64_t n = 0;
            const int e = ps4_net_host_sendto(s->native, buf, len, host_flags, addr, addr_len, &n);
            if (e == ORBIS_NET_EWOULDBLOCK || e == ORBIS_NET_ENOBUFS) {
                result = (int64_t)len;
            } else if (e == ORBIS_NET_ENOTCONN && !addr) {
                result = -ORBIS_NET_EDESTADDRREQ; // as BSD
            } else {
                result = e ? -e : n;
            }
        }
        obj_release(&s->hdr);
        return result;
    }
    // Streams: blocking sends go out whole.
    const bool blocking = !dontwait && !atomic_load(&s->nonblocking);
    const int64_t deadline = socket_deadline(&s->snd_timeout_us);
    SendOp op = {s, (const uint8_t *)buf, len, 0, host_flags, addr, addr_len};
    for (;;) {
        const int64_t r = run_blocking(s, SIDE_SEND, PS4_NET_WRITABLE, deadline, blocking, send_op, &op);
        if (r < 0) {
            if (r == -ABORTED_ERROR) {
                pthread_mutex_lock(&s->wait_mutex);
                if (s->send_again && op.sent > 0) s->pending_abort[SIDE_SEND] = true;
                s->send_again = false;
                pthread_mutex_unlock(&s->wait_mutex);
            }
            result = op.sent > 0 ? (int64_t)op.sent : r;
            break;
        }
        op.sent += (size_t)r;
        if (!blocking || op.sent >= len) {
            result = (int64_t)op.sent;
            break;
        }
    }
    obj_release(&s->hdr);
    return result;
}

typedef struct RecvOp {
    NetSocket *s;
    uint8_t *bytes;
    size_t len;
    size_t got;
    int flags;
    struct sockaddr *addr;
    socklen_t *addr_len;
} RecvOp;

static int64_t recv_op(void *arg) {
    RecvOp *o = (RecvOp *)arg;
    int64_t n = 0;
    const int e = ps4_net_host_recvfrom(o->s->native, o->bytes + o->got, o->len - o->got, o->flags,
                                        o->got == 0 ? o->addr : NULL, o->got == 0 ? o->addr_len : NULL, &n);
    return e ? -e : n;
}

int64_t ps4_net_socket_recvfrom(int32_t id, void *buf, size_t len, int host_flags, bool dontwait,
                                struct sockaddr *addr, socklen_t *addr_len) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    const bool blocking = !dontwait && !atomic_load(&s->nonblocking);
    const bool waitall = (host_flags & MSG_WAITALL) != 0 && blocking && s->type == SOCK_STREAM;
    const int64_t deadline = socket_deadline(&s->rcv_timeout_us);
    RecvOp op = {s, (uint8_t *)buf, len, 0, host_flags & ~MSG_WAITALL, addr, addr_len};
    int64_t result;
    if (!waitall) {
        result = run_blocking(s, SIDE_RECV, PS4_NET_READABLE, deadline, blocking, recv_op, &op);
    } else {
        result = 0;
        while (op.got < len) {
            const int64_t r = run_blocking(s, SIDE_RECV, PS4_NET_READABLE, deadline, blocking, recv_op, &op);
            if (r < 0) {
                result = op.got > 0 ? (int64_t)op.got : r;
                break;
            }
            if (r == 0) break; // FIN
            op.got += (size_t)r;
        }
        if (result >= 0) result = (int64_t)op.got;
    }
    obj_release(&s->hdr);
    return result;
}

int64_t ps4_net_socket_shutdown(int32_t id, int how) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    const int e = shutdown(s->native, how) == 0 ? 0 : ps4_net_host_last_error();
    obj_release(&s->hdr);
    return -e;
}

int64_t ps4_net_socket_getname(int32_t id, struct sockaddr *addr, socklen_t *len) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    const int e = getsockname(s->native, addr, len) == 0 ? 0 : ps4_net_host_last_error();
    obj_release(&s->hdr);
    return -e;
}

int64_t ps4_net_socket_getpeername(int32_t id, struct sockaddr *addr, socklen_t *len) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    const int e = getpeername(s->native, addr, len) == 0 ? 0 : ps4_net_host_last_error();
    obj_release(&s->hdr);
    return -e;
}

int64_t ps4_net_socket_set_host_option(int32_t id, int level, int name, const void *value, socklen_t len) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    const int e = setsockopt(s->native, level, name, value, len) == 0 ? 0 : ps4_net_host_last_error();
    obj_release(&s->hdr);
    return -e;
}

int64_t ps4_net_socket_get_host_option(int32_t id, int level, int name, void *value, socklen_t *len) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    const int e = getsockopt(s->native, level, name, value, len) == 0 ? 0 : ps4_net_host_last_error();
    obj_release(&s->hdr);
    return -e;
}

int64_t ps4_net_socket_get_info(int32_t id, Ps4NetSocketInfo *info) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    info->type = s->type;
    info->p2p = false;
    info->nonblocking = atomic_load(&s->nonblocking);
    info->reuse_addr = atomic_load(&s->reuse_addr);
    info->rcv_timeout_us = atomic_load(&s->rcv_timeout_us);
    info->snd_timeout_us = atomic_load(&s->snd_timeout_us);
    info->connect_timeout_us = atomic_load(&s->connect_timeout_us);
    info->accept_timeout_us = atomic_load(&s->accept_timeout_us);
    info->connected = atomic_load(&s->connected);
    info->listening = atomic_load(&s->listening);
    info->pending_error = ps4_net_host_pending_error(s->native);
    obj_release(&s->hdr);
    return 0;
}

int64_t ps4_net_socket_get_attributes(int32_t id, Ps4NetSocketAttributes *out) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    pthread_mutex_lock(&s->attr_mutex);
    *out = s->attributes;
    pthread_mutex_unlock(&s->attr_mutex);
    obj_release(&s->hdr);
    return 0;
}

int64_t ps4_net_socket_update_attributes(int32_t id, void (*update)(Ps4NetSocketAttributes *, void *), void *arg) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    pthread_mutex_lock(&s->attr_mutex);
    update(&s->attributes, arg);
    pthread_mutex_unlock(&s->attr_mutex);
    obj_release(&s->hdr);
    return 0;
}

bool ps4_net_attr_find(const Ps4NetSocketAttributes *a, int32_t level, int32_t name, int32_t *value) {
    for (int i = 0; i < a->stored_count; i++) {
        if (a->stored[i].level == level && a->stored[i].name == name) {
            *value = a->stored[i].value;
            return true;
        }
    }
    return false;
}

void ps4_net_attr_store(Ps4NetSocketAttributes *a, int32_t level, int32_t name, int32_t value) {
    for (int i = 0; i < a->stored_count; i++) {
        if (a->stored[i].level == level && a->stored[i].name == name) {
            a->stored[i].value = value;
            return;
        }
    }
    if (a->stored_count < PS4_NET_MAX_STORED_OPTIONS) {
        a->stored[a->stored_count++] = (Ps4NetStoredOption){level, name, value};
    }
}

int64_t ps4_net_socket_add_pending_membership(int32_t id, const uint8_t request[8]) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    int64_t r = 0;
    pthread_mutex_lock(&s->attr_mutex);
    if (s->pending_count == s->pending_cap) {
        const int cap = s->pending_cap ? s->pending_cap * 2 : 4;
        uint8_t(*grown)[8] = (uint8_t(*)[8])realloc(s->pending_memberships, (size_t)cap * 8);
        if (grown) {
            s->pending_memberships = grown;
            s->pending_cap = cap;
        }
    }
    if (s->pending_count < s->pending_cap) {
        memcpy(s->pending_memberships[s->pending_count++], request, 8);
    } else {
        r = -ORBIS_NET_ENOMEM;
    }
    pthread_mutex_unlock(&s->attr_mutex);
    obj_release(&s->hdr);
    return r;
}

int64_t ps4_net_socket_drop_pending_membership(int32_t id, const uint8_t request[8]) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    int64_t removed = 0;
    pthread_mutex_lock(&s->attr_mutex);
    for (int i = 0; i < s->pending_count;) {
        if (memcmp(s->pending_memberships[i], request, 8) == 0) {
            memmove(s->pending_memberships[i], s->pending_memberships[i + 1], (size_t)(s->pending_count - i - 1) * 8);
            s->pending_count--;
            removed = 1;
        } else {
            i++;
        }
    }
    pthread_mutex_unlock(&s->attr_mutex);
    obj_release(&s->hdr);
    return removed;
}

void ps4_net_socket_apply_pending_memberships(int32_t id) {
    NetSocket *s = get_socket(id);
    if (!s) return;
    pthread_mutex_lock(&s->attr_mutex);
    uint8_t(*pending)[8] = s->pending_memberships;
    const int count = s->pending_count;
    s->pending_memberships = NULL;
    s->pending_count = s->pending_cap = 0;
    pthread_mutex_unlock(&s->attr_mutex);
    for (int i = 0; i < count; i++) {
        if (setsockopt(s->native, IPPROTO_IP, IP_ADD_MEMBERSHIP, pending[i], 8) != 0) {
            fprintf(stderr, "[ps4-net] multicast join after bind failed: %d\n", ps4_net_host_last_error());
        }
    }
    free(pending);
    obj_release(&s->hdr);
}

int64_t ps4_net_socket_bytes_readable(int32_t id) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    int64_t n = 0;
    const int e = ps4_net_host_bytes_readable(s->native, &n);
    obj_release(&s->hdr);
    return e ? -e : n;
}

#define SOCKET_SETTER(fn, field, type, expr)          \
    int64_t fn(int32_t id, type value) {               \
        NetSocket *s = get_socket(id);                 \
        if (!s) return -ORBIS_NET_EBADF;               \
        atomic_store(&s->field, (expr));               \
        obj_release(&s->hdr);                          \
        return 0;                                      \
    }

// The host socket is always non-blocking; only the guest's mode changes.
SOCKET_SETTER(ps4_net_socket_set_nonblocking, nonblocking, bool, value)
SOCKET_SETTER(ps4_net_socket_set_reuse_addr, reuse_addr, bool, value)
SOCKET_SETTER(ps4_net_socket_set_reuse_port, reuse_port, bool, value)
SOCKET_SETTER(ps4_net_socket_set_recv_timeout, rcv_timeout_us, int64_t, value)
SOCKET_SETTER(ps4_net_socket_set_send_timeout, snd_timeout_us, int64_t, value)
SOCKET_SETTER(ps4_net_socket_set_connect_timeout, connect_timeout_us, int64_t, value > 0 ? value : 0)
SOCKET_SETTER(ps4_net_socket_set_accept_timeout, accept_timeout_us, int64_t, value > 0 ? value : 0)

static bool hup_add(NetEpoll *ep, int32_t id, uint32_t generation) {
    if (ep->hup_count == ep->hup_cap) {
        const int cap = ep->hup_cap ? ep->hup_cap * 2 : 4;
        HupEntry *grown = (HupEntry *)realloc(ep->hups, (size_t)cap * sizeof(HupEntry));
        if (!grown) return false;
        ep->hups = grown;
        ep->hup_cap = cap;
    }
    ep->hups[ep->hup_count++] = (HupEntry){id, generation};
    return true;
}

int64_t ps4_net_socket_abort(int32_t id, uint32_t flags) {
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    bool hangup = false;
    // An epoll wait watching this socket reports EPOLLHUP for it.
    pthread_mutex_lock(&s->reg_mutex);
    EpollList epolls = {NULL, 0, 0};
    for (int i = 0; i < s->epolls.count; i++) epoll_list_add(&epolls, s->epolls.items[i]);
    pthread_mutex_unlock(&s->reg_mutex);
    for (int i = 0; i < epolls.count; i++) {
        NetEpoll *ep = epolls.items[i];
        pthread_mutex_lock(&ep->mutex);
        const NetReg *reg = epoll_find(ep, id);
        if (!ep->destroyed && ep->waiters && reg && reg->generation == s->hdr.generation &&
            hup_add(ep, id, s->hdr.generation)) {
            ps4_net_wake_signal(&ep->host.wake);
            hangup = true;
        }
        pthread_mutex_unlock(&ep->mutex);
    }
    epoll_list_release(&epolls);

    // Abort doesn't touch queued data.
    pthread_mutex_lock(&s->wait_mutex);
    bool blocked[2] = {false, false};
    interrupt_waiters(s, blocked);
    bool preserved = false;
    const bool send_again = (flags & PS4_NET_ABORT_PRESERVE_SEND_AGAIN) != 0;
    if ((flags & PS4_NET_ABORT_PRESERVE_RECV) && !blocked[SIDE_RECV]) {
        s->pending_abort[SIDE_RECV] = true;
        preserved = true;
    }
    if ((flags & PS4_NET_ABORT_PRESERVE_SEND) || send_again) {
        if (!blocked[SIDE_SEND]) {
            s->pending_abort[SIDE_SEND] = true;
            preserved = true;
        } else if (send_again) {
            s->send_again = true; // resolved by the interrupted send
        }
    }
    pthread_mutex_unlock(&s->wait_mutex);
    obj_release(&s->hdr);
    // Nothing to abort: ENOTBLK (PS4 behaviour).
    if (!blocked[0] && !blocked[1] && !preserved && !hangup) return -ORBIS_NET_ENOTBLK;
    return 0;
}

int64_t ps4_net_socket_close(int32_t id) {
    NetSocket *s = (NetSocket *)table_remove(id, PS4_NET_OBJ_SOCKET);
    if (!s) return -ORBIS_NET_EBADF;
    if (id != s->native) {
        // A duplicate id: the object lives on under its other ids.
        obj_release(&s->hdr);
        return 0;
    }
    // Wake blocked threads (BSD would leave a recv blocked across close; shadPS4 wakes it).
    pthread_mutex_lock(&s->wait_mutex);
    s->closing = true;
    bool blocked[2] = {false, false};
    interrupt_waiters(s, blocked);
    pthread_mutex_unlock(&s->wait_mutex);

    pthread_mutex_lock(&s->reg_mutex);
    s->closed = true; // blocks later epoll adds
    EpollList epolls = epoll_list_take(&s->epolls);
    pthread_mutex_unlock(&s->reg_mutex);
    for (int i = 0; i < epolls.count; i++) {
        NetEpoll *ep = epolls.items[i];
        pthread_mutex_lock(&ep->mutex);
        NetReg *reg = epoll_find(ep, id);
        if (reg && reg->generation == s->hdr.generation) {
            ps4_net_host_epoll_remove(&ep->host, reg->native);
            epoll_erase_reg(ep, reg);
        }
        pthread_mutex_unlock(&ep->mutex);
    }
    epoll_list_release(&epolls);
    obj_release(&s->hdr);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Epolls

int64_t ps4_net_epoll_create(void) {
    NetEpoll *ep = (NetEpoll *)calloc(1, sizeof(NetEpoll));
    if (!ep) return -ORBIS_NET_ENOMEM;
    if (!ps4_net_host_epoll_init(&ep->host)) {
        free(ep);
        return -ORBIS_NET_ENOBUFS;
    }
    ep->hdr.kind = PS4_NET_OBJ_EPOLL;
    atomic_init(&ep->hdr.refs, 1);
    ep->hdr.generation = atomic_fetch_add(&g_next_generation, 1);
    ep->hdr.destroy = epoll_destroy_obj;
    pthread_mutex_init(&ep->mutex, NULL);
    const int64_t r = table_insert(ep->host.kq, &ep->hdr, false, false);
    if (r < 0) {
        pthread_mutex_destroy(&ep->mutex);
        ps4_net_host_epoll_destroy(&ep->host);
        free(ep);
    }
    return r;
}

static int64_t external_epoll_control(NetEpoll *ep, int32_t id, NetExternal *x, Ps4NetEpollOp op, uint32_t events,
                                      uint64_t data) {
    pthread_mutex_lock(&ep->mutex);
    pthread_mutex_lock(&x->reg_mutex);
    int64_t r = 0;
    NetReg *reg = epoll_find(ep, id);
    if (x->closed) {
        r = -ORBIS_NET_EBADF;
    } else if (op == PS4_NET_EPOLL_ADD) {
        const bool arm = (events & PS4_NET_EV_IN) != 0;
        NetReg added = {id, x->hdr.generation, data, events, x->done.read_fd, true, arm, false};
        if (reg) {
            r = -ORBIS_NET_EEXIST;
        } else if (arm && (r = -ps4_net_host_epoll_set(&ep->host, x->done.read_fd, make_tag(id, x->hdr.generation),
                                                       PS4_NET_EV_IN)) != 0) {
        } else if (!epoll_add_reg(ep, &added) || !epoll_list_add(&x->epolls, ep)) {
            r = -ORBIS_NET_ENOMEM;
        }
    } else if (op == PS4_NET_EPOLL_MODIFY) {
        if (!reg) {
            r = -ORBIS_NET_ENOENT;
        } else {
            const bool want = (events & PS4_NET_EV_IN) != 0 && !reg->reported;
            if (want && !reg->armed) {
                r = -ps4_net_host_epoll_set(&ep->host, x->done.read_fd, make_tag(id, x->hdr.generation), PS4_NET_EV_IN);
                if (r == 0) reg->armed = true;
            } else if (!want && reg->armed) {
                ps4_net_host_epoll_remove(&ep->host, x->done.read_fd);
                reg->armed = false;
            }
            if (r == 0) {
                reg->data = data;
                reg->events = events;
            }
        }
    } else {
        if (!reg) {
            r = -ORBIS_NET_ENOENT;
        } else {
            if (reg->armed) ps4_net_host_epoll_remove(&ep->host, x->done.read_fd);
            epoll_erase_reg(ep, reg);
            epoll_list_remove(&x->epolls, ep);
        }
    }
    pthread_mutex_unlock(&x->reg_mutex);
    pthread_mutex_unlock(&ep->mutex);
    return r;
}

// Also used by select, with an epoll that has no id.
static int64_t epoll_control_on(NetEpoll *ep, Ps4NetEpollOp op, int32_t id, uint32_t events, uint64_t data) {
    NetExternal *x = get_external(id);
    if (x) {
        const int64_t r = external_epoll_control(ep, id, x, op, events, data);
        obj_release(&x->hdr);
        return r;
    }
    NetSocket *s = get_socket(id);
    if (!s) return -ORBIS_NET_EBADF;
    pthread_mutex_lock(&ep->mutex);
    pthread_mutex_lock(&s->reg_mutex);
    int64_t r = 0;
    NetReg *reg = epoll_find(ep, id);
    if (s->closed) {
        r = -ORBIS_NET_EBADF;
    } else if (op == PS4_NET_EPOLL_ADD) {
        if (reg) {
            r = -ORBIS_NET_EEXIST;
        } else {
            r = -ps4_net_host_epoll_set(&ep->host, s->native, make_tag(id, s->hdr.generation), events);
            const NetReg added = {id, s->hdr.generation, data, events, s->native, false, false, false};
            if (r == 0 && (!epoll_add_reg(ep, &added) || !epoll_list_add(&s->epolls, ep))) r = -ORBIS_NET_ENOMEM;
        }
    } else if (op == PS4_NET_EPOLL_MODIFY) {
        if (!reg) {
            r = -ORBIS_NET_ENOENT;
        } else {
            r = -ps4_net_host_epoll_set(&ep->host, s->native, make_tag(id, s->hdr.generation), events);
            if (r == 0) {
                reg->data = data;
                reg->events = events;
            }
        }
    } else {
        if (!reg) {
            r = -ORBIS_NET_ENOENT;
        } else {
            r = -ps4_net_host_epoll_remove(&ep->host, s->native);
            epoll_erase_reg(ep, reg);
            epoll_list_remove(&s->epolls, ep);
        }
    }
    pthread_mutex_unlock(&s->reg_mutex);
    pthread_mutex_unlock(&ep->mutex);
    obj_release(&s->hdr);
    return r;
}

int64_t ps4_net_epoll_control(int32_t eid, Ps4NetEpollOp op, int32_t id, uint32_t events, uint64_t data) {
    NetEpoll *ep = get_epoll(eid);
    if (!ep) return -ORBIS_NET_EBADF;
    const int64_t r = epoll_control_on(ep, op, id, events, data);
    obj_release(&ep->hdr);
    return r;
}

static bool epoll_wake_idle(const NetEpoll *ep) {
    return ep->flagged_waiters == 0 && ep->hup_count == 0 && !ep->destroyed;
}

// Called with ep->mutex held: adds the pending hangups to out[0..n), returns the new count.
static int add_hangups(NetEpoll *ep, Ps4NetGuestEpollEvent *out, int n, int cap) {
    int kept = 0;
    for (int i = 0; i < ep->hup_count; i++) {
        const HupEntry h = ep->hups[i];
        const NetReg *reg = epoll_find(ep, h.id);
        if (!reg || reg->generation != h.generation) continue; // gone since
        int existing = -1;
        for (int j = 0; j < n; j++) {
            if (out[j].ident == h.id) {
                existing = j;
                break;
            }
        }
        if (existing >= 0) {
            out[existing].events |= PS4_NET_EV_HUP;
        } else if (n < cap) {
            out[n++] = (Ps4NetGuestEpollEvent){PS4_NET_EV_HUP, h.id, reg->data};
        } else {
            ep->hups[kept++] = h; // no room, keep for the next wait
        }
    }
    ep->hup_count = kept;
    if (epoll_wake_idle(ep)) ps4_net_wake_drain(&ep->host.wake);
    return n;
}

static int64_t epoll_wait_on(NetEpoll *ep, Ps4NetGuestEpollEvent *out, int cap, int64_t timeout_us) {
    if (cap <= 0) return -ORBIS_NET_EINVAL;
    const int64_t deadline = timeout_us < 0 ? PS4_NET_NO_DEADLINE : ps4_net_host_now_ns() + timeout_us * 1000LL;
    Ps4NetHostEvent host_out[PS4_NET_HOST_EPOLL_MAX_EVENTS];
    const int host_cap = cap < PS4_NET_HOST_EPOLL_MAX_EVENTS ? cap : PS4_NET_HOST_EPOLL_MAX_EVENTS;

    // The wake handle is drained once every interrupted wait has left.
    NetWaiter me = {NULL, 0, false, NULL};
    pthread_mutex_lock(&ep->mutex);
    if (ep->destroyed) {
        pthread_mutex_unlock(&ep->mutex);
        return -ORBIS_NET_EBADF;
    }
    if (ep->pending_abort) {
        ep->pending_abort = false;
        pthread_mutex_unlock(&ep->mutex);
        return -ABORTED_ERROR;
    }
    me.next = ep->waiters;
    ep->waiters = &me;
    pthread_mutex_unlock(&ep->mutex);

    int64_t result;
    for (;;) {
        pthread_mutex_lock(&ep->mutex);
        if (me.aborted) {
            pthread_mutex_unlock(&ep->mutex);
            result = -ABORTED_ERROR;
            break;
        }
        if (ep->hup_count > 0) {
            const int n = add_hangups(ep, out, 0, cap);
            if (n > 0) {
                pthread_mutex_unlock(&ep->mutex);
                result = n;
                break;
            }
        }
        pthread_mutex_unlock(&ep->mutex);
        int count = 0;
        bool woken = false;
        const int e = ps4_net_host_epoll_wait(&ep->host, host_out, host_cap, deadline, &count, &woken);
        if (e != 0) {
            result = -e;
            break;
        }
        int n = 0;
        pthread_mutex_lock(&ep->mutex);
        for (int i = 0; i < count; i++) {
            const int32_t id = (int32_t)(host_out[i].tag & 0x7fffffffu);
            const uint32_t generation = (uint32_t)(host_out[i].tag >> 32);
            NetReg *reg = epoll_find(ep, id);
            if (!reg || reg->generation != generation) continue; // removed since the host wait
            uint32_t events = host_out[i].events;
            if (reg->external) {
                // Reported once; the flag stays set.
                NetExternal *x = get_external(id);
                if (!x || x->hdr.generation != generation || !reg->armed) {
                    if (x) obj_release(&x->hdr);
                    continue;
                }
                ps4_net_host_epoll_remove(&ep->host, reg->native);
                reg->armed = false;
                reg->reported = true;
                // EPOLLIN | EPOLLDESCID, plus EPOLLHUP if the lookup was aborted.
                events = PS4_NET_EV_IN | PS4_NET_EV_DESCID | (atomic_load(&x->hangup) ? PS4_NET_EV_HUP : 0u);
                obj_release(&x->hdr);
            } else if (events & (PS4_NET_EV_ERR | PS4_NET_EV_HUP)) {
                // An error or hangup is reported as the registered directions.
                events = (events & ~(uint32_t)(PS4_NET_EV_ERR | PS4_NET_EV_HUP)) |
                         (reg->events & (PS4_NET_EV_IN | PS4_NET_EV_OUT));
            }
            if (events == 0) continue;
            int existing = -1;
            for (int j = 0; j < n; j++) {
                if (out[j].ident == id) {
                    existing = j;
                    break;
                }
            }
            if (existing >= 0) {
                out[existing].events |= events;
            } else {
                out[n++] = (Ps4NetGuestEpollEvent){events, id, reg->data};
            }
        }
        if (ep->hup_count > 0) n = add_hangups(ep, out, n, cap);
        pthread_mutex_unlock(&ep->mutex);
        if (n > 0) {
            result = n;
            break;
        }
        if (count == 0 && !woken) {
            result = 0; // timed out
            break;
        }
    }

    pthread_mutex_lock(&ep->mutex);
    for (NetWaiter **p = &ep->waiters; *p; p = &(*p)->next) {
        if (*p == &me) {
            *p = me.next;
            break;
        }
    }
    if (me.aborted && --ep->flagged_waiters == 0 && epoll_wake_idle(ep)) ps4_net_wake_drain(&ep->host.wake);
    pthread_mutex_unlock(&ep->mutex);
    return result;
}

int64_t ps4_net_epoll_wait(int32_t eid, Ps4NetGuestEpollEvent *out, int cap, int64_t timeout_us) {
    NetEpoll *ep = get_epoll(eid);
    if (!ep) return -ORBIS_NET_EBADF;
    const int64_t r = epoll_wait_on(ep, out, cap, timeout_us);
    obj_release(&ep->hdr);
    return r;
}

// Called with ep->mutex held.
static bool interrupt_epoll_waiters(NetEpoll *ep) {
    for (NetWaiter *w = ep->waiters; w; w = w->next) {
        if (!w->aborted) {
            w->aborted = true;
            ep->flagged_waiters++;
        }
    }
    if (!ep->waiters) return false;
    ps4_net_wake_signal(&ep->host.wake);
    return true;
}

int64_t ps4_net_epoll_abort(int32_t eid, uint32_t flags) {
    NetEpoll *ep = get_epoll(eid);
    if (!ep) return -ORBIS_NET_EBADF;
    int64_t r = 0;
    pthread_mutex_lock(&ep->mutex);
    if (!interrupt_epoll_waiters(ep)) {
        if (flags & PS4_NET_EPOLL_ABORT_PRESERVE) {
            ep->pending_abort = true;
        } else {
            r = -ORBIS_NET_ENOTBLK;
        }
    }
    pthread_mutex_unlock(&ep->mutex);
    obj_release(&ep->hdr);
    return r;
}

// Drops the epoll from the epoll lists of the objects registered with it. ep->mutex held.
static void epoll_detach_all(NetEpoll *ep) {
    for (int i = 0; i < ep->reg_count; i++) {
        const NetReg *reg = &ep->regs[i];
        NetObject *o = table_get(reg->id, reg->external ? PS4_NET_OBJ_EXTERNAL : PS4_NET_OBJ_SOCKET);
        if (!o || o->generation != reg->generation) {
            if (o) obj_release(o);
            continue;
        }
        if (reg->external) {
            NetExternal *x = (NetExternal *)o;
            pthread_mutex_lock(&x->reg_mutex);
            epoll_list_remove(&x->epolls, ep);
            pthread_mutex_unlock(&x->reg_mutex);
        } else {
            NetSocket *s = (NetSocket *)o;
            pthread_mutex_lock(&s->reg_mutex);
            epoll_list_remove(&s->epolls, ep);
            pthread_mutex_unlock(&s->reg_mutex);
        }
        obj_release(o);
    }
    ep->reg_count = 0;
}

int64_t ps4_net_epoll_destroy(int32_t eid) {
    NetEpoll *ep = (NetEpoll *)table_remove(eid, PS4_NET_OBJ_EPOLL);
    if (!ep) return -ORBIS_NET_EBADF;
    // Waiters hold a reference, so the kqueue is closed after the last one returns.
    pthread_mutex_lock(&ep->mutex);
    ep->destroyed = true;
    interrupt_epoll_waiters(ep);
    epoll_detach_all(ep);
    pthread_mutex_unlock(&ep->mutex);
    obj_release(&ep->hdr);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Select

int64_t ps4_net_socket_select(Ps4NetSelectEntry *entries, int count, int64_t timeout_us) {
    for (int i = 0; i < count; i++) entries[i].ready = 0;
    if (count <= 0) return 0;
    // A private epoll with no id.
    NetEpoll *ep = (NetEpoll *)calloc(1, sizeof(NetEpoll));
    if (!ep) return -ORBIS_NET_ENOMEM;
    if (!ps4_net_host_epoll_init(&ep->host)) {
        free(ep);
        return -ORBIS_NET_ENOBUFS;
    }
    ep->hdr.kind = PS4_NET_OBJ_EPOLL;
    atomic_init(&ep->hdr.refs, 1);
    ep->hdr.generation = atomic_fetch_add(&g_next_generation, 1);
    ep->hdr.destroy = epoll_destroy_obj;
    pthread_mutex_init(&ep->mutex, NULL);
    // epoll_destroy_obj unreserves the kqueue descriptor; it is not in the table, so that is a
    // no-op for an unused slot.

    int64_t result = 0;
    int registered = 0;
    Ps4NetGuestEpollEvent *out = (Ps4NetGuestEpollEvent *)malloc((size_t)count * sizeof(*out));
    if (!out) {
        result = -ORBIS_NET_ENOMEM;
        goto done;
    }
    for (int i = 0; i < count && result == 0; i++) {
        if (ps4_net_object_kind(entries[i].id) != PS4_NET_OBJ_SOCKET) {
            result = -ORBIS_NET_EBADF;
            break;
        }
        // Merge duplicate entries into the first one.
        bool seen = false;
        for (int j = 0; j < i; j++) seen = seen || entries[j].id == entries[i].id;
        if (seen) continue;
        uint32_t interest = 0;
        for (int j = i; j < count; j++) {
            if (entries[j].id == entries[i].id) interest |= entries[j].interest;
        }
        result = epoll_control_on(ep, PS4_NET_EPOLL_ADD, entries[i].id, interest, (uint64_t)i);
        if (result == 0) registered++;
    }
    int64_t wait = timeout_us;
    while (result == 0 && registered > 0) {
        const int64_t r = epoll_wait_on(ep, out, registered, wait);
        if (r < 0) {
            result = r;
            break;
        }
        if (r == 0) break;
        for (int64_t k = 0; k < r; k++) {
            // As on BSD, an error or hangup makes the socket readable and writable.
            uint32_t ready = out[k].events;
            if (ready & (PS4_NET_EV_ERR | PS4_NET_EV_HUP)) ready |= PS4_NET_EV_IN | PS4_NET_EV_OUT;
            for (int i = 0; i < count; i++) {
                if (entries[i].id == out[k].ident) entries[i].ready = ready & entries[i].interest;
            }
            if (epoll_control_on(ep, PS4_NET_EPOLL_DELETE, out[k].ident, 0, 0) == 0) registered--;
        }
        wait = 0;
    }

done:
    pthread_mutex_lock(&ep->mutex);
    epoll_detach_all(ep);
    pthread_mutex_unlock(&ep->mutex);
    free(out);
    obj_release(&ep->hdr);
    if (result < 0) return result;
    int64_t ready_count = 0;
    for (int i = 0; i < count; i++) ready_count += __builtin_popcount(entries[i].ready);
    return ready_count;
}
