// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNet after shadPS4 libraries/net/net.cpp and net_kernel.cpp. See ps4_net.h.

#include "ps4_net.h"
#include "ps4_hle_stub.h"
#include "ps4_kernel_sys.h"
#include "ps4_net_core.h"
#include "ps4_net_p2p.h"
#include "ps4_net_host.h"
#include "ps4_net_resolver.h"
#include "ps4_net_translate.h"
#include "ps4_netctl.h"
#include "ps4_threading.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static inline void *G(GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

static inline uint64_t stack_arg(GuestContext *ctx, int index) {
    // [rsp] is the return address; the 7th integer argument is at [rsp + 8].
    return *(uint64_t *)(ctx->mem_base + ctx->rsp + 8 * (uint64_t)(index + 1));
}

// ---------------------------------------------------------------------------------------------
// Errors

static int32_t *net_errno_slot(GuestContext *ctx) {
    return ctx && ctx->fs_base ? (int32_t *)(ctx->mem_base + ctx->fs_base + RECOMP_TCB_NET_ERRNO_SLOT) : NULL;
}

static void store_net_errno(GuestContext *ctx, int32_t e) {
    int32_t *slot = net_errno_slot(ctx);
    if (slot) *slot = e;
}

// shadPS4 SetErrno: the net errno, the kernel errno (its kernel errno hook) and the SCE code.
static int32_t set_errno(GuestContext *ctx, int e) {
    store_net_errno(ctx, e);
    set_guest_errno(ctx, e);
    return ORBIS_NET_ERROR(e);
}

static int32_t ret(GuestContext *ctx, int64_t r) {
    return r >= 0 ? (int32_t)r : set_errno(ctx, (int)-r);
}

// shadPS4 ResolverReturn: the low byte of a failure code is the net errno.
static int32_t resolver_return(GuestContext *ctx, int32_t code) {
    if (code < 0) store_net_errno(ctx, (int32_t)((uint32_t)code & 0xff));
    return code;
}

static bool name_too_long(const char *name) {
    return name && strnlen(name, ORBIS_NET_DEBUG_NAME_LEN_MAX + 1) > ORBIS_NET_DEBUG_NAME_LEN_MAX;
}

// ---------------------------------------------------------------------------------------------
// Memory pools

#define FIRST_POOL_ID 3000
#define MAX_POOLS 31
#define MIN_POOL_SIZE (4 * 1024)

typedef struct Pool {
    bool in_use;
    int32_t size;
    int32_t users; // ENOTEMPTY on destroy
} Pool;

typedef struct ResolverPool {
    int32_t rid;
    int32_t memid;
} ResolverPool;

static pthread_mutex_t g_pools_mutex = PTHREAD_MUTEX_INITIALIZER;
static Pool g_pools[MAX_POOLS];
static ResolverPool *g_resolver_pools; // resolver id -> memory id
static int g_resolver_pool_count;
static int g_resolver_pool_cap;

// Caller holds g_pools_mutex.
static Pool *find_pool(int32_t memid) {
    if (memid < FIRST_POOL_ID || memid - FIRST_POOL_ID >= MAX_POOLS || !g_pools[memid - FIRST_POOL_ID].in_use) {
        return NULL;
    }
    return &g_pools[memid - FIRST_POOL_ID];
}

// 0 or the errno.
static int check_pool_id(int32_t memid) {
    if (memid == 0) return ORBIS_NET_EINVAL;
    pthread_mutex_lock(&g_pools_mutex);
    const bool found = find_pool(memid) != NULL;
    pthread_mutex_unlock(&g_pools_mutex);
    if (!found) {
        fprintf(stderr, "[ps4-net] unknown memory pool id %d\n", memid);
        return ORBIS_NET_EBADF;
    }
    return 0;
}

static void release_resolver_pool(int32_t rid) {
    pthread_mutex_lock(&g_pools_mutex);
    for (int i = 0; i < g_resolver_pool_count; i++) {
        if (g_resolver_pools[i].rid == rid) {
            Pool *pool = find_pool(g_resolver_pools[i].memid);
            if (pool) pool->users--;
            g_resolver_pools[i] = g_resolver_pools[--g_resolver_pool_count];
            break;
        }
    }
    pthread_mutex_unlock(&g_pools_mutex);
}

int32_t ps4_sceNetPoolCreate(GuestContext *ctx, const char *name, int32_t size, int32_t flags) {
    (void)ctx;
    if (name_too_long(name)) return ORBIS_NET_ERROR(ORBIS_NET_ENAMETOOLONG);
    if (flags != 0 || size < MIN_POOL_SIZE) return ORBIS_NET_ERROR(ORBIS_NET_EINVAL);
    pthread_mutex_lock(&g_pools_mutex);
    for (int i = 0; i < MAX_POOLS; i++) {
        if (!g_pools[i].in_use) {
            g_pools[i] = (Pool){true, size, 0};
            pthread_mutex_unlock(&g_pools_mutex);
            return FIRST_POOL_ID + i;
        }
    }
    pthread_mutex_unlock(&g_pools_mutex);
    return ORBIS_NET_ERROR(ORBIS_NET_ENFILE);
}

int32_t ps4_sceNetPoolDestroy(GuestContext *ctx, int32_t pool_id) {
    (void)ctx;
    int32_t r = 0;
    pthread_mutex_lock(&g_pools_mutex);
    Pool *pool = find_pool(pool_id);
    if (!pool) {
        r = ORBIS_NET_ERROR(ORBIS_NET_EBADF);
    } else if (pool->users > 0) {
        r = ORBIS_NET_ERROR(ORBIS_NET_ENOTEMPTY);
    } else {
        *pool = (Pool){false, 0, 0};
    }
    pthread_mutex_unlock(&g_pools_mutex);
    return r;
}

// ---------------------------------------------------------------------------------------------
// Socket options

enum {
    KIND_TCP = 1u << 0,
    KIND_UDP = 1u << 1,
    KIND_RAW = 1u << 2,
    KIND_UDP_P2P = 1u << 3,
    KIND_TCP_P2P = 1u << 4,
    ALL_KINDS = KIND_TCP | KIND_UDP | KIND_RAW | KIND_UDP_P2P | KIND_TCP_P2P,
    STREAMS = KIND_TCP | KIND_TCP_P2P,
    DATAGRAMS = KIND_UDP | KIND_UDP_P2P,
};

static uint8_t kind_bit(const Ps4NetSocketInfo *info) {
    if (info->p2p) return info->type == SOCK_STREAM ? KIND_TCP_P2P : KIND_UDP_P2P;
    switch (info->type) {
    case SOCK_STREAM:
        return KIND_TCP;
    case SOCK_DGRAM:
        return KIND_UDP;
    default:
        return KIND_RAW;
    }
}

// FreeBSD FIONBIO / FIONREAD.
#define ORBIS_NET_FIONBIO 0x8004667eu
#define ORBIS_NET_FIONREAD 0x4004667fu

#define MAX_SOCKET_BUFFER (512 * 1024)
#define MAX_RAW_SEND 8192
#define MAX_PEEK_LEN (512u * 1024u)
// P2P SO_LINGER: on/off is stored under the option, the seconds under this key.
#define LINGER_SECONDS_KEY (-ORBIS_NET_SO_LINGER)

typedef struct OptionRule {
    int32_t level;
    int32_t name;
    bool get;
    bool set;
    uint8_t valid_for;
    bool others_ignored; // ignored on other kinds
    bool stored;         // no host equivalent
    bool byte_or_int;    // u8 or int
} OptionRule;

// clang-format off
static const OptionRule g_option_rules[] = {
    // level                  name                            get    set    valid_for              ignored stored byte
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_ACCEPTTIMEO,       true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_BROADCAST,         true,  true,  DATAGRAMS,             true,  false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_CONNECTTIMEO,      true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_ERROR,             true,  false, ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_ERROR_EX,          true,  false, ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_KEEPALIVE,         true,  true,  STREAMS,               false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_LINGER,            true,  true,  STREAMS,               false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_NAME,              false, true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_NBIO,              true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_ONESBCAST,         true,  true,  ALL_KINDS,             false, true,  false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_POLICY,            true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_PRIORITY,          true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_RCVBUF,            true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_RCVTIMEO,          true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_BSD_RCVTIMEO,      true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_REUSEADDR,         true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_REUSEPORT,         true,  true,  ALL_KINDS,             false, true,  false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_SNDBUF,            true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_SNDTIMEO,          true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_BSD_SNDTIMEO,      true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_TYPE,              true,  false, ALL_KINDS,             false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_USECRYPTO,         true,  true,  KIND_UDP_P2P | KIND_TCP_P2P, false, false, false},
    {ORBIS_NET_SOL_SOCKET,  ORBIS_NET_SO_USESIGNATURE,      true,  true,  KIND_UDP_P2P | KIND_TCP_P2P, false, false, false},
    {ORBIS_NET_IPPROTO_IP,  ORBIS_NET_IP_ADD_MEMBERSHIP,    false, true,  KIND_UDP | KIND_RAW,   false, false, false},
    {ORBIS_NET_IPPROTO_IP,  ORBIS_NET_IP_DROP_MEMBERSHIP,   false, true,  KIND_UDP | KIND_RAW,   false, false, false},
    {ORBIS_NET_IPPROTO_IP,  ORBIS_NET_IP_DONTFRAG,          true,  true,  DATAGRAMS | KIND_RAW,  false, true,  false},
    {ORBIS_NET_IPPROTO_IP,  ORBIS_NET_IP_HDRINCL,           true,  true,  KIND_RAW,              false, false, false},
    {ORBIS_NET_IPPROTO_IP,  ORBIS_NET_IP_MAXTTL,            true,  false, ALL_KINDS & ~KIND_RAW, false, true,  false},
    {ORBIS_NET_IPPROTO_IP,  ORBIS_NET_IP_MULTICAST_IF,      true,  true,  KIND_UDP,              false, false, false},
    {ORBIS_NET_IPPROTO_IP,  ORBIS_NET_IP_MULTICAST_LOOP,    true,  true,  KIND_UDP | KIND_RAW,   false, false, true},
    {ORBIS_NET_IPPROTO_IP,  ORBIS_NET_IP_MULTICAST_TTL,     true,  true,  KIND_UDP | KIND_RAW,   false, false, true},
    {ORBIS_NET_IPPROTO_IP,  ORBIS_NET_IP_TOS,               true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_IPPROTO_IP,  ORBIS_NET_IP_TTL,               true,  true,  ALL_KINDS,             false, false, false},
    {ORBIS_NET_IPPROTO_IP,  ORBIS_NET_IP_TTLCHK,            true,  true,  ALL_KINDS & ~KIND_RAW, false, true,  false},
    {ORBIS_NET_IPPROTO_TCP, ORBIS_NET_TCP_MAXSEG,           true,  true,  STREAMS,               false, false, false},
    {ORBIS_NET_IPPROTO_TCP, ORBIS_NET_TCP_MSS_TO_ADVERTISE, true,  true,  STREAMS,               false, true,  false},
    {ORBIS_NET_IPPROTO_TCP, ORBIS_NET_TCP_NODELAY,          true,  true,  STREAMS,               false, false, false},
    {ORBIS_NET_IPPROTO_TCP, ORBIS_NET_TCP_KEEPLISTEN,       true,  true,  KIND_TCP,              false, true,  false},
    {ORBIS_NET_IPPROTO_UDP, ORBIS_NET_UDP_SND_ON_SUSPEND,   true,  true,  KIND_UDP,              false, true,  false},
};
// clang-format on

static const OptionRule *find_option_rule(int32_t level, int32_t name) {
    for (size_t i = 0; i < sizeof(g_option_rules) / sizeof(g_option_rules[0]); i++) {
        if (g_option_rules[i].level == level && g_option_rules[i].name == name) return &g_option_rules[i];
    }
    return NULL;
}

static int32_t stored_option(int32_t s, int32_t level, int32_t name, const Ps4NetSocketInfo *info) {
    Ps4NetSocketAttributes attributes;
    int32_t value;
    if (ps4_net_socket_get_attributes(s, &attributes) == 0 && ps4_net_attr_find(&attributes, level, name, &value)) {
        return value;
    }
    if (level == ORBIS_NET_IPPROTO_TCP && name == ORBIS_NET_TCP_NODELAY) return info->p2p ? 1 : 0; // on for STREAM_P2P
    if (level == ORBIS_NET_IPPROTO_TCP && name == ORBIS_NET_TCP_MAXSEG && info->p2p) return ps4_p2p_tcp_default_config.mss;
    if (level == ORBIS_NET_IPPROTO_IP && name == ORBIS_NET_IP_TTL) return 64; // shadPS4: the FreeBSD default
    if (level == ORBIS_NET_IPPROTO_IP && (name == ORBIS_NET_IP_MULTICAST_TTL || name == ORBIS_NET_IP_MULTICAST_LOOP)) {
        return 1;
    }
    return 0;
}

typedef struct StoreArg {
    int32_t level;
    int32_t name;
    int32_t value;
} StoreArg;

static void store_update(Ps4NetSocketAttributes *a, void *arg) {
    const StoreArg *s = (const StoreArg *)arg;
    ps4_net_attr_store(a, s->level, s->name, s->value);
}

static int32_t store_option(GuestContext *ctx, int32_t s, int32_t level, int32_t name, int32_t value) {
    StoreArg arg = {level, name, value};
    return ret(ctx, ps4_net_socket_update_attributes(s, store_update, &arg));
}

typedef struct LingerArg {
    int32_t onoff;
    int32_t seconds;
} LingerArg;

// P2P SO_LINGER: on/off under the option, the seconds under LINGER_SECONDS_KEY.
static void linger_update(Ps4NetSocketAttributes *a, void *arg) {
    const LingerArg *l = (const LingerArg *)arg;
    ps4_net_attr_store(a, ORBIS_NET_SOL_SOCKET, ORBIS_NET_SO_LINGER, l->onoff);
    ps4_net_attr_store(a, ORBIS_NET_SOL_SOCKET, LINGER_SECONDS_KEY, l->seconds);
}

// FreeBSD timeval on the PS4: two 8-byte fields.
typedef struct OrbisTimeval {
    int64_t tv_sec;
    int64_t tv_usec;
} OrbisTimeval;

static bool read_timeval(const void *optval, uint32_t optlen, int64_t *us) {
    if (!optval || optlen < sizeof(OrbisTimeval)) return false;
    OrbisTimeval tv;
    memcpy(&tv, optval, sizeof(tv));
    if (tv.tv_sec < 0 || tv.tv_usec < 0 || tv.tv_usec > 999999) return false;
    *us = tv.tv_sec * 1000000 + tv.tv_usec;
    return true;
}

static int32_t write_timeval(GuestContext *ctx, int64_t microseconds, void *optval, uint32_t *optlen) {
    if (*optlen < sizeof(OrbisTimeval)) return set_errno(ctx, ORBIS_NET_EINVAL);
    const OrbisTimeval tv = {microseconds / 1000000, microseconds % 1000000};
    memcpy(optval, &tv, sizeof(tv));
    *optlen = sizeof(tv);
    return 0;
}

static int32_t write_int(GuestContext *ctx, int64_t value, void *optval, uint32_t *optlen) {
    if (*optlen < sizeof(int32_t)) return set_errno(ctx, ORBIS_NET_EINVAL);
    const int32_t v = (int32_t)(value < INT32_MIN ? INT32_MIN : value > INT32_MAX ? INT32_MAX : value);
    memcpy(optval, &v, sizeof(v));
    *optlen = sizeof(v);
    return 0;
}

// The PS4 spells IP_DONTFRAG as an option of its own; macOS has IP_DONTFRAG as well.
static int set_host_dont_fragment(int32_t s, bool enable) {
    const int value = enable ? 1 : 0;
    return (int)-ps4_net_socket_set_host_option(s, IPPROTO_IP, IP_DONTFRAG, &value, sizeof(value));
}

static bool is_bound(int32_t s) {
    struct sockaddr_storage local;
    socklen_t len = sizeof(local);
    memset(&local, 0, sizeof(local));
    if (ps4_net_socket_getname(s, (struct sockaddr *)&local, &len) != 0) return false;
    if (local.ss_family == AF_INET) return ((const struct sockaddr_in *)&local)->sin_port != 0;
    if (local.ss_family == AF_INET6) return ((const struct sockaddr_in6 *)&local)->sin6_port != 0;
    return true;
}

static int32_t set_membership(GuestContext *ctx, int32_t s, bool add, const Ps4NetHostOption *option,
                              const void *optval, uint32_t optlen) {
    uint8_t request[8];
    if (!optval || optlen < sizeof(request)) return set_errno(ctx, ORBIS_NET_EINVAL);
    memcpy(request, optval, sizeof(request));
    if (!add && ps4_net_socket_drop_pending_membership(s, request) > 0) return 0;
    const int64_t r = ps4_net_socket_set_host_option(s, option->level, option->name, request, sizeof(request));
    if (add && r == -ORBIS_NET_EINVAL && !is_bound(s)) {
        // Joins refused on an unbound socket are retried after bind.
        return ret(ctx, ps4_net_socket_add_pending_membership(s, request));
    }
    return ret(ctx, r);
}

static int32_t set_send_on_suspend(GuestContext *ctx, int32_t s, const Ps4NetSocketInfo *info, const void *optval,
                                   uint32_t optlen) {
    if (!optval || optlen < sizeof(OrbisNetUdpSndOnSuspend)) return set_errno(ctx, ORBIS_NET_EINVAL);
    OrbisNetUdpSndOnSuspend value;
    memcpy(&value, optval, sizeof(value));
    if (value.onoff != 0) {
        if (!value.addr || !value.data || value.datalen == 0 || value.datalen > ORBIS_NET_UDP_SND_ON_SUSPEND_DATALEN_MAX) {
            return set_errno(ctx, ORBIS_NET_EINVAL);
        }
        if (info->connected) return set_errno(ctx, ORBIS_NET_EPROCUNAVAIL); // not allowed after connect
    }
    return store_option(ctx, s, ORBIS_NET_IPPROTO_UDP, ORBIS_NET_UDP_SND_ON_SUSPEND, value.onoff != 0 ? 1 : 0);
}

typedef struct NameArg {
    const char *text;
    size_t length;
} NameArg;

static void name_update(Ps4NetSocketAttributes *a, void *arg) {
    const NameArg *n = (const NameArg *)arg;
    memset(a->name, 0, sizeof(a->name));
    if (n->length) memcpy(a->name, n->text, n->length);
}

typedef struct IntFieldArg {
    int which; // 0 snd_buf, 1 rcv_buf, 2 policy, 3 priority
    int32_t value;
} IntFieldArg;

static void int_field_update(Ps4NetSocketAttributes *a, void *arg) {
    const IntFieldArg *f = (const IntFieldArg *)arg;
    switch (f->which) {
    case 0:
        a->snd_buf = f->value;
        break;
    case 1:
        a->rcv_buf = f->value;
        break;
    case 2:
        a->policy = f->value;
        break;
    default:
        a->priority = f->value;
        break;
    }
}

int32_t ps4_sceNetSetsockopt(GuestContext *ctx, int32_t s, int32_t level, int32_t optname, const void *optval,
                             uint32_t optlen) {
    if (!optval && optlen != 0) return set_errno(ctx, ORBIS_NET_EFAULT);
    Ps4NetSocketInfo info;
    const int64_t r = ps4_net_socket_get_info(s, &info);
    if (r < 0) return ret(ctx, r);
    const OptionRule *rule = find_option_rule(level, optname);
    if (!rule || !rule->set) return set_errno(ctx, ORBIS_NET_ENOPROTOOPT);

    // Multicast TTL/loop also accept a single byte.
    int32_t value = 0;
    bool has_int = false;
    if (optval && optlen >= sizeof(int32_t)) {
        memcpy(&value, optval, sizeof(value));
        has_int = true;
    } else if (optval && optlen == 1 && rule->byte_or_int) {
        value = *(const uint8_t *)optval;
        has_int = true;
    }

    if (!(rule->valid_for & kind_bit(&info))) {
        if (!rule->others_ignored) return set_errno(ctx, ORBIS_NET_EPROCUNAVAIL);
        return has_int ? store_option(ctx, s, level, optname, value) : 0;
    }

    if (level == ORBIS_NET_SOL_SOCKET) {
        switch (optname) {
        case ORBIS_NET_SO_NBIO:
            return has_int ? ret(ctx, ps4_net_socket_set_nonblocking(s, value != 0)) : set_errno(ctx, ORBIS_NET_EINVAL);
        case ORBIS_NET_SO_RCVTIMEO:
        case ORBIS_NET_SO_SNDTIMEO:
        case ORBIS_NET_SO_CONNECTTIMEO:
        case ORBIS_NET_SO_ACCEPTTIMEO: {
            if (!has_int) return set_errno(ctx, ORBIS_NET_EINVAL);
            // Microseconds, <= 0 means no timeout.
            const int64_t timeout = value > 0 ? value : 0;
            switch (optname) {
            case ORBIS_NET_SO_RCVTIMEO:
                return ret(ctx, ps4_net_socket_set_recv_timeout(s, timeout));
            case ORBIS_NET_SO_SNDTIMEO:
                return ret(ctx, ps4_net_socket_set_send_timeout(s, timeout));
            case ORBIS_NET_SO_CONNECTTIMEO:
                return ret(ctx, ps4_net_socket_set_connect_timeout(s, timeout));
            default:
                return ret(ctx, ps4_net_socket_set_accept_timeout(s, timeout));
            }
        }
        case ORBIS_NET_SO_BSD_RCVTIMEO:
        case ORBIS_NET_SO_BSD_SNDTIMEO: {
            int64_t timeout;
            if (!read_timeval(optval, optlen, &timeout)) return set_errno(ctx, ORBIS_NET_EINVAL);
            return ret(ctx, optname == ORBIS_NET_SO_BSD_RCVTIMEO ? ps4_net_socket_set_recv_timeout(s, timeout)
                                                                 : ps4_net_socket_set_send_timeout(s, timeout));
        }
        case ORBIS_NET_SO_REUSEADDR:
            return has_int ? ret(ctx, ps4_net_socket_set_reuse_addr(s, value != 0)) : set_errno(ctx, ORBIS_NET_EINVAL);
        case ORBIS_NET_SO_REUSEPORT:
            if (!has_int) return set_errno(ctx, ORBIS_NET_EINVAL);
            if (!info.p2p) ps4_net_socket_set_reuse_port(s, value != 0);
            return store_option(ctx, s, level, optname, value);
        case ORBIS_NET_SO_USECRYPTO:
        case ORBIS_NET_SO_USESIGNATURE: {
            if (!has_int) return set_errno(ctx, ORBIS_NET_EINVAL);
            bool crypto = false;
            bool signature = false;
            ps4_net_p2p_socket_get_protection(s, &crypto, &signature);
            if (optname == ORBIS_NET_SO_USECRYPTO) {
                crypto = value != 0;
            } else {
                signature = value != 0;
            }
            return ret(ctx, ps4_net_p2p_socket_set_protection(s, crypto, signature));
        }
        case ORBIS_NET_SO_SNDBUF:
        case ORBIS_NET_SO_RCVBUF: {
            if (!has_int || value <= 0) return set_errno(ctx, ORBIS_NET_EINVAL);
            if (value > MAX_SOCKET_BUFFER) value = MAX_SOCKET_BUFFER;
            IntFieldArg arg = {optname == ORBIS_NET_SO_SNDBUF ? 0 : 1, value};
            ps4_net_socket_update_attributes(s, int_field_update, &arg);
            if (info.p2p || kind_bit(&info) == KIND_RAW) return 0; // meaningless for RAW
            break;
        }
        case ORBIS_NET_SO_POLICY: {
            if (!has_int || (value != ORBIS_NET_SOCK_POLICY_NA &&
                             (value < ORBIS_NET_SOCK_POLICY_NUM_MIN || value > ORBIS_NET_SOCK_POLICY_NUM_MAX))) {
                return set_errno(ctx, ORBIS_NET_EINVAL);
            }
            IntFieldArg arg = {2, value};
            return ret(ctx, ps4_net_socket_update_attributes(s, int_field_update, &arg));
        }
        case ORBIS_NET_SO_PRIORITY: {
            if (!has_int || value < ORBIS_NET_SOCK_PRIORITY_NUM_USER_MIN || value > ORBIS_NET_SOCK_PRIORITY_NUM_USER_MAX) {
                return set_errno(ctx, ORBIS_NET_EINVAL);
            }
            IntFieldArg arg = {3, value};
            return ret(ctx, ps4_net_socket_update_attributes(s, int_field_update, &arg));
        }
        case ORBIS_NET_SO_NAME: {
            const char *text = (const char *)optval;
            const size_t length = text ? strnlen(text, optlen) : 0;
            if (length > ORBIS_NET_DEBUG_NAME_LEN_MAX) return set_errno(ctx, ORBIS_NET_ENAMETOOLONG);
            NameArg arg = {text, length};
            return ret(ctx, ps4_net_socket_update_attributes(s, name_update, &arg));
        }
        case ORBIS_NET_SO_ONESBCAST:
            // As shadPS4: 255.255.255.255 is not turned into the interface broadcast address.
            return has_int ? store_option(ctx, s, level, optname, value) : set_errno(ctx, ORBIS_NET_EINVAL);
        default:
            break;
        }
    }
    if (level == ORBIS_NET_IPPROTO_UDP && optname == ORBIS_NET_UDP_SND_ON_SUSPEND) {
        return set_send_on_suspend(ctx, s, &info, optval, optlen);
    }
    if (level == ORBIS_NET_IPPROTO_TCP && optname == ORBIS_NET_TCP_MSS_TO_ADVERTISE) {
        // 0 means the interface MSS.
        if (!has_int || (value != 0 && value < 216)) return set_errno(ctx, ORBIS_NET_EINVAL);
    }

    if (level == ORBIS_NET_SOL_SOCKET && optname == ORBIS_NET_SO_LINGER && info.p2p) {
        // Stored whole so getsockopt can return it (as shadPS4, not applied on a P2P close).
        if (optlen < sizeof(OrbisNetLinger)) return set_errno(ctx, ORBIS_NET_EINVAL);
        OrbisNetLinger guest;
        memcpy(&guest, optval, sizeof(guest));
        LingerArg arg = {guest.l_onoff, guest.l_linger};
        return ret(ctx, ps4_net_socket_update_attributes(s, linger_update, &arg));
    }
    Ps4NetHostOption option;
    const bool has_option = ps4_net_to_host_option(level, optname, &option);
    if (info.p2p || !has_option || rule->stored) {
        if (!has_int) return set_errno(ctx, ORBIS_NET_EINVAL);
        if (!info.p2p && level == ORBIS_NET_IPPROTO_IP && optname == ORBIS_NET_IP_DONTFRAG) {
            const int e = set_host_dont_fragment(s, value != 0);
            if (e != 0) return set_errno(ctx, e);
        }
        return store_option(ctx, s, level, optname, value);
    }
    switch (option.value) {
    case PS4_NET_OPT_INT: {
        if (!has_int) return set_errno(ctx, ORBIS_NET_EINVAL);
        const int host_value = value;
        return ret(ctx, ps4_net_socket_set_host_option(s, option.level, option.name, &host_value, sizeof(host_value)));
    }
    case PS4_NET_OPT_LINGER: {
        if (optlen < sizeof(OrbisNetLinger)) return set_errno(ctx, ORBIS_NET_EINVAL);
        OrbisNetLinger guest;
        memcpy(&guest, optval, sizeof(guest));
        const struct linger host = {guest.l_onoff, guest.l_linger};
        return ret(ctx, ps4_net_socket_set_host_option(s, option.level, option.name, &host, sizeof(host)));
    }
    case PS4_NET_OPT_RAW:
        if (level == ORBIS_NET_IPPROTO_IP &&
            (optname == ORBIS_NET_IP_ADD_MEMBERSHIP || optname == ORBIS_NET_IP_DROP_MEMBERSHIP)) {
            return set_membership(ctx, s, optname == ORBIS_NET_IP_ADD_MEMBERSHIP, &option, optval, optlen);
        }
        return ret(ctx, ps4_net_socket_set_host_option(s, option.level, option.name, optval, (socklen_t)optlen));
    }
    return set_errno(ctx, ORBIS_NET_ENOPROTOOPT);
}

int32_t ps4_sceNetGetsockopt(GuestContext *ctx, int32_t s, int32_t level, int32_t optname, void *optval,
                             uint32_t *optlen) {
    if (!optval || !optlen) return set_errno(ctx, ORBIS_NET_EFAULT);
    Ps4NetSocketInfo info;
    const int64_t r = ps4_net_socket_get_info(s, &info);
    if (r < 0) return ret(ctx, r);
    const OptionRule *rule = find_option_rule(level, optname);
    if (!rule || !rule->get) return set_errno(ctx, ORBIS_NET_ENOPROTOOPT);
    if (!(rule->valid_for & kind_bit(&info))) {
        if (!rule->others_ignored) return set_errno(ctx, ORBIS_NET_EPROCUNAVAIL);
        return write_int(ctx, stored_option(s, level, optname, &info), optval, optlen);
    }
    Ps4NetSocketAttributes attributes;
    ps4_net_socket_get_attributes(s, &attributes);

    if (level == ORBIS_NET_SOL_SOCKET) {
        switch (optname) {
        case ORBIS_NET_SO_NBIO:
            return write_int(ctx, info.nonblocking ? 1 : 0, optval, optlen);
        case ORBIS_NET_SO_RCVTIMEO:
            return write_int(ctx, info.rcv_timeout_us, optval, optlen);
        case ORBIS_NET_SO_SNDTIMEO:
            return write_int(ctx, info.snd_timeout_us, optval, optlen);
        case ORBIS_NET_SO_CONNECTTIMEO:
            return write_int(ctx, info.connect_timeout_us, optval, optlen);
        case ORBIS_NET_SO_ACCEPTTIMEO:
            return write_int(ctx, info.accept_timeout_us, optval, optlen);
        case ORBIS_NET_SO_BSD_RCVTIMEO:
        case ORBIS_NET_SO_BSD_SNDTIMEO:
            return write_timeval(ctx, optname == ORBIS_NET_SO_BSD_RCVTIMEO ? info.rcv_timeout_us : info.snd_timeout_us,
                                 optval, optlen);
        case ORBIS_NET_SO_REUSEADDR:
            return write_int(ctx, info.reuse_addr ? 1 : 0, optval, optlen);
        case ORBIS_NET_SO_TYPE:
            return write_int(ctx, ps4_net_to_orbis_socket_type(info.type, info.p2p), optval, optlen);
        case ORBIS_NET_SO_ERROR:
            return write_int(ctx, info.pending_error, optval, optlen);
        case ORBIS_NET_SO_ERROR_EX:
            // The full SCE code. Reading either option clears the error.
            return write_int(ctx, info.pending_error == 0 ? 0 : ORBIS_NET_ERROR(info.pending_error), optval, optlen);
        case ORBIS_NET_SO_SNDBUF:
            // The PS4 value (Linux would report double what was set).
            return write_int(ctx, attributes.snd_buf ? attributes.snd_buf : ps4_net_default_send_buffer(info.type), optval,
                             optlen);
        case ORBIS_NET_SO_RCVBUF:
            return write_int(ctx, attributes.rcv_buf ? attributes.rcv_buf : ps4_net_default_receive_buffer(info.type),
                             optval, optlen);
        case ORBIS_NET_SO_POLICY:
            return write_int(ctx, attributes.policy, optval, optlen);
        case ORBIS_NET_SO_PRIORITY:
            return write_int(ctx, attributes.priority, optval, optlen);
        case ORBIS_NET_SO_USECRYPTO:
        case ORBIS_NET_SO_USESIGNATURE: {
            bool crypto = false;
            bool signature = false;
            ps4_net_p2p_socket_get_protection(s, &crypto, &signature);
            return write_int(ctx, (optname == ORBIS_NET_SO_USECRYPTO ? crypto : signature) ? 1 : 0, optval, optlen);
        }
        default:
            break;
        }
    }

    if (level == ORBIS_NET_IPPROTO_UDP && optname == ORBIS_NET_UDP_SND_ON_SUSPEND) {
        // Only onoff is stored.
        if (*optlen < sizeof(OrbisNetUdpSndOnSuspend)) return set_errno(ctx, ORBIS_NET_EINVAL);
        OrbisNetUdpSndOnSuspend value;
        memset(&value, 0, sizeof(value));
        value.onoff = stored_option(s, level, optname, &info);
        memcpy(optval, &value, sizeof(value));
        *optlen = sizeof(value);
        return 0;
    }
    if (level == ORBIS_NET_SOL_SOCKET && optname == ORBIS_NET_SO_LINGER && info.p2p) {
        if (*optlen < sizeof(OrbisNetLinger)) return set_errno(ctx, ORBIS_NET_EINVAL);
        const OrbisNetLinger guest = {stored_option(s, level, optname, &info),
                                      stored_option(s, level, LINGER_SECONDS_KEY, &info)};
        memcpy(optval, &guest, sizeof(guest));
        *optlen = sizeof(guest);
        return 0;
    }
    Ps4NetHostOption option;
    const bool has_option = ps4_net_to_host_option(level, optname, &option);
    if (info.p2p || !has_option || rule->stored) {
        const int32_t value = stored_option(s, level, optname, &info);
        if (rule->byte_or_int && *optlen == 1) {
            *(uint8_t *)optval = (uint8_t)value;
            return 0;
        }
        return write_int(ctx, value, optval, optlen);
    }
    switch (option.value) {
    case PS4_NET_OPT_INT: {
        int value = 0;
        socklen_t len = sizeof(value);
        const int64_t g = ps4_net_socket_get_host_option(s, option.level, option.name, &value, &len);
        if (g < 0) return ret(ctx, g);
        if (rule->byte_or_int && *optlen == 1) {
            *(uint8_t *)optval = (uint8_t)value;
            return 0;
        }
        return write_int(ctx, value, optval, optlen);
    }
    case PS4_NET_OPT_LINGER: {
        if (*optlen < sizeof(OrbisNetLinger)) return set_errno(ctx, ORBIS_NET_EINVAL);
        struct linger host = {0, 0};
        socklen_t len = sizeof(host);
        const int64_t g = ps4_net_socket_get_host_option(s, option.level, option.name, &host, &len);
        if (g < 0) return ret(ctx, g);
        const OrbisNetLinger guest = {host.l_onoff, host.l_linger};
        memcpy(optval, &guest, sizeof(guest));
        *optlen = sizeof(guest);
        return 0;
    }
    case PS4_NET_OPT_RAW: {
        socklen_t len = (socklen_t)*optlen;
        const int64_t g = ps4_net_socket_get_host_option(s, option.level, option.name, optval, &len);
        if (g == 0) *optlen = (uint32_t)len;
        return ret(ctx, g);
    }
    }
    return set_errno(ctx, ORBIS_NET_ENOPROTOOPT);
}

// ---------------------------------------------------------------------------------------------
// Sockets

int32_t ps4_sceNetSocket(GuestContext *ctx, const char *name, int32_t family, int32_t type, int32_t protocol) {
    Ps4NetHostSocketKind kind;
    const int e = ps4_net_to_host_socket_kind(family, type, protocol, &kind);
    if (e != 0) return set_errno(ctx, e);
    // Names over 31 characters are ENAMETOOLONG, not truncated.
    if (name_too_long(name)) return set_errno(ctx, ORBIS_NET_ENAMETOOLONG);
    if (kind.p2p && !ps4_net_p2p_ensure_transport()) return set_errno(ctx, ORBIS_NET_EADDRINUSE); // the P2P port is taken
    const int64_t r = kind.p2p ? ps4_net_p2p_socket_create(kind.type == SOCK_STREAM)
                               : ps4_net_socket_create(kind.family, kind.type, kind.protocol);
    if (r >= 0 && name) {
        NameArg arg = {name, strnlen(name, ORBIS_NET_DEBUG_NAME_LEN_MAX)};
        ps4_net_socket_update_attributes((int32_t)r, name_update, &arg);
    }
    return ret(ctx, r);
}

int32_t ps4_sceNetSocketClose(GuestContext *ctx, int32_t s) {
    return ret(ctx, ps4_net_socket_close(s));
}

int32_t ps4_sceNetSocketAbort(GuestContext *ctx, int32_t s, int32_t flags) {
    return ret(ctx, ps4_net_socket_abort(s, (uint32_t)flags));
}

// ---------------------------------------------------------------------------------------------
// P2P addresses (shadPS4 net.cpp)

static bool p2p_stream(const Ps4NetSocketInfo *info) {
    return info->type == SOCK_STREAM;
}

static int check_p2p_bind_address(const OrbisNetSockaddr *addr, bool stream) {
    OrbisNetSockaddrIn in;
    memcpy(&in, addr, sizeof(in)); // the caller checked the length
    if ((ntohl(in.sin_addr) & 0xf0000000u) == 0xe0000000u) return ORBIS_NET_EADDRNOTAVAIL;
    if (!stream) return in.sin_port == 0 ? ORBIS_NET_EADDRNOTAVAIL : 0;
    // shadPS4's TODO: the PS4 only accepts 3658; the configured port is accepted too, for tests
    // and setups that change it.
    const uint16_t udp_port = ntohs(in.sin_vport);
    const bool ok = udp_port == 0 || udp_port == PS4_NET_DEFAULT_P2P_UDP_PORT || udp_port == ps4_net_p2p_bound_port() ||
                    udp_port == ps4_net_p2p_configured_port();
    return ok ? 0 : ORBIS_NET_EINVAL;
}

static bool is_complete_p2p_address(const OrbisNetSockaddr *addr, bool stream) {
    OrbisNetSockaddrIn in;
    memcpy(&in, addr, sizeof(in));
    return in.sin_addr != 0 && in.sin_port != 0 && (stream || in.sin_vport != 0);
}

static bool is_p2p_bound(int32_t s) {
    uint16_t udp_port = 0;
    uint16_t vport = 0;
    return ps4_net_p2p_socket_getname(s, &udp_port, &vport) == 0 && vport != 0;
}

static void p2p_port_bound_update(Ps4NetSocketAttributes *a, void *arg) {
    (void)arg;
    a->p2p_tcp_port_bound = true;
}

static int32_t p2p_bind(GuestContext *ctx, int32_t s, const Ps4NetSocketInfo *info, const OrbisNetSockaddr *addr,
                        uint32_t addrlen) {
    const bool stream = p2p_stream(info);
    Ps4P2PEndpoint udp;
    uint16_t port = 0;
    int e = ps4_net_to_p2p_address(addr, addrlen, stream, &udp, &port);
    if (e != 0) return set_errno(ctx, e);
    // sa_len is not checked: games leave it 0 (shadPS4 saw it binding DGRAM_P2P during boot).
    e = check_p2p_bind_address(addr, stream);
    if (e != 0) return set_errno(ctx, e);
    // P2P sockets share the transport: no UDP port is bound here.
    const bool reserved = stream ? ps4_net_is_reserved_port(port) : ps4_net_is_reserved_p2p_vport(port);
    if (port != 0 && reserved) return set_errno(ctx, ORBIS_NET_EACCES);
    const int64_t r = ps4_net_p2p_socket_bind(s, port);
    // A listener port: connecting from it is EPROTO.
    if (r == 0 && stream && port != 0) ps4_net_socket_update_attributes(s, p2p_port_bound_update, NULL);
    return ret(ctx, r);
}

static int32_t p2p_connect(GuestContext *ctx, int32_t s, const Ps4NetSocketInfo *info, const OrbisNetSockaddr *addr,
                           uint32_t addrlen) {
    const bool stream = p2p_stream(info);
    const bool bound = is_p2p_bound(s);
    if (!bound && !stream) return set_errno(ctx, ORBIS_NET_EINVAL);
    Ps4P2PEndpoint peer;
    uint16_t port = 0;
    const int e = ps4_net_to_p2p_address(addr, addrlen, stream, &peer, &port);
    if (e != 0) return set_errno(ctx, e);
    if (!is_complete_p2p_address(addr, stream)) return set_errno(ctx, ORBIS_NET_EADDRNOTAVAIL);
    if (info->listening) return set_errno(ctx, ORBIS_NET_EOPNOTSUPP);
    if (stream) {
        Ps4NetSocketAttributes attributes;
        ps4_net_socket_get_attributes(s, &attributes);
        if (attributes.p2p_tcp_port_bound) return set_errno(ctx, ORBIS_NET_EPROTO);
        if (!bound) {
            const int64_t b = ps4_net_p2p_socket_bind(s, 0); // bound implicitly
            if (b < 0) return ret(ctx, b);
        }
    } else {
        Ps4P2PEndpoint current;
        uint16_t current_vport = 0;
        if (ps4_net_p2p_socket_getpeername(s, &current, &current_vport) == 0) return set_errno(ctx, ORBIS_NET_EISCONN);
    }
    return ret(ctx, ps4_net_p2p_socket_connect(s, &peer, port));
}

int32_t ps4_sceNetBind(GuestContext *ctx, int32_t s, const OrbisNetSockaddr *addr, uint32_t addrlen) {
    Ps4NetSocketInfo info;
    const int64_t r = ps4_net_socket_get_info(s, &info);
    if (r < 0) return ret(ctx, r);
    if (info.p2p) return p2p_bind(ctx, s, &info, addr, addrlen);
    struct sockaddr_storage host;
    socklen_t host_len = 0;
    const int e = ps4_net_to_host_sockaddr(addr, addrlen, &host, &host_len);
    if (e != 0) return set_errno(ctx, e);
    uint16_t port = 0;
    if (host.ss_family == AF_INET) {
        port = ntohs(((const struct sockaddr_in *)&host)->sin_port);
    } else if (host.ss_family == AF_INET6) {
        port = ntohs(((const struct sockaddr_in6 *)&host)->sin6_port);
    }
    if (port != 0 && ps4_net_is_reserved_port(port)) return set_errno(ctx, ORBIS_NET_EACCES);
    const int64_t b = ps4_net_socket_bind(s, (const struct sockaddr *)&host, host_len);
    if (b == 0) ps4_net_socket_apply_pending_memberships(s);
    return ret(ctx, b);
}

int32_t ps4_sceNetListen(GuestContext *ctx, int32_t s, int32_t backlog) {
    Ps4NetSocketInfo info;
    const int64_t r = ps4_net_socket_get_info(s, &info);
    if (r < 0) return ret(ctx, r);
    if (info.p2p && !is_p2p_bound(s)) return set_errno(ctx, ORBIS_NET_EINVAL); // P2P listeners must be bound
    return ret(ctx, ps4_net_socket_listen(s, backlog));
}

int32_t ps4_sceNetAccept(GuestContext *ctx, int32_t s, OrbisNetSockaddr *addr, uint32_t *paddrlen) {
    if (addr && !paddrlen) return set_errno(ctx, ORBIS_NET_EFAULT);
    Ps4NetSocketInfo info;
    const int64_t i = ps4_net_socket_get_info(s, &info);
    if (i < 0) return ret(ctx, i);
    if (info.p2p) {
        Ps4P2PEndpoint peer = {0, 0};
        uint16_t peer_vport = 0;
        const int64_t r = ps4_net_p2p_socket_accept(s, &peer, &peer_vport);
        if (r >= 0 && addr) ps4_net_to_orbis_p2p_address(&peer, peer_vport, true, addr, paddrlen);
        return ret(ctx, r);
    }
    struct sockaddr_storage host;
    socklen_t host_len = sizeof(host);
    memset(&host, 0, sizeof(host));
    const int64_t r = ps4_net_socket_accept(s, (struct sockaddr *)&host, &host_len);
    if (r >= 0 && addr) ps4_net_to_orbis_sockaddr((const struct sockaddr *)&host, host_len, addr, paddrlen);
    return ret(ctx, r);
}

int32_t ps4_sceNetConnect(GuestContext *ctx, int32_t s, const OrbisNetSockaddr *addr, uint32_t addrlen) {
    Ps4NetSocketInfo info;
    const int64_t r = ps4_net_socket_get_info(s, &info);
    if (r < 0) return ret(ctx, r);
    if (info.p2p) return p2p_connect(ctx, s, &info, addr, addrlen);
    struct sockaddr_storage host;
    socklen_t host_len = 0;
    const int e = ps4_net_to_host_sockaddr(addr, addrlen, &host, &host_len);
    if (e != 0) return set_errno(ctx, e);
    if (info.type == SOCK_DGRAM && stored_option(s, ORBIS_NET_IPPROTO_UDP, ORBIS_NET_UDP_SND_ON_SUSPEND, &info) != 0) {
        return set_errno(ctx, ORBIS_NET_EPROCUNAVAIL);
    }
    if (info.type == SOCK_STREAM && host.ss_family == AF_INET &&
        ((const struct sockaddr_in *)&host)->sin_addr.s_addr == ORBIS_NET_INADDR_BROADCAST) {
        return set_errno(ctx, ORBIS_NET_EACCES);
    }
    return ret(ctx, ps4_net_socket_connect(s, (const struct sockaddr *)&host, host_len));
}

int32_t ps4_sceNetSendto(GuestContext *ctx, int32_t s, const void *buf, uint64_t len, int32_t flags,
                         const OrbisNetSockaddr *addr, uint32_t addrlen) {
    if (!buf && len != 0) return set_errno(ctx, ORBIS_NET_EFAULT);
    const Ps4NetHostMsgFlags msg = ps4_net_to_host_msg_flags(flags);
    if (msg.unsupported != 0) fprintf(stderr, "[ps4-net] send: ignoring flags 0x%x\n", msg.unsupported);
    Ps4NetSocketInfo info;
    const int64_t r = ps4_net_socket_get_info(s, &info);
    if (r < 0) return ret(ctx, r);
    if (info.type != SOCK_STREAM) {
        Ps4NetSocketAttributes attributes;
        ps4_net_socket_get_attributes(s, &attributes);
        const int32_t limit = info.type == SOCK_RAW ? MAX_RAW_SEND
                              : attributes.snd_buf ? attributes.snd_buf
                                                   : ps4_net_default_send_buffer(info.type);
        if (len > (uint64_t)limit) return set_errno(ctx, ORBIS_NET_EMSGSIZE);
    }
    if (addr && info.connected) {
        if (info.type != SOCK_STREAM) return set_errno(ctx, ORBIS_NET_EISCONN);
        addr = NULL;
        addrlen = 0;
    }
    if (info.p2p) {
        Ps4P2PEndpoint to = {0, 0};
        uint16_t to_vport = 0;
        if (addr) {
            const int e = ps4_net_to_p2p_address(addr, addrlen, p2p_stream(&info), &to, &to_vport);
            if (e != 0) return set_errno(ctx, e);
            if (info.type == SOCK_DGRAM && !is_complete_p2p_address(addr, false)) {
                return set_errno(ctx, ORBIS_NET_EADDRNOTAVAIL);
            }
        }
        // A stream's protection is fixed per connection (shadPS4 ignores per-message flags there).
        return ret(ctx, ps4_net_p2p_socket_sendto(s, buf, (size_t)len, msg.dontwait, addr ? &to : NULL, to_vport,
                                                  msg.crypto, msg.signature));
    }
    struct sockaddr_storage host;
    socklen_t host_len = 0;
    if (addr) {
        const int e = ps4_net_to_host_sockaddr(addr, addrlen, &host, &host_len);
        if (e != 0) return set_errno(ctx, e);
    }
    return ret(ctx, ps4_net_socket_sendto(s, buf, (size_t)len, msg.host, msg.dontwait,
                                          addr ? (const struct sockaddr *)&host : NULL, host_len));
}

int32_t ps4_sceNetRecvfrom(GuestContext *ctx, int32_t s, void *buf, uint64_t len, int32_t flags,
                           OrbisNetSockaddr *addr, uint32_t *paddrlen) {
    const Ps4NetHostMsgFlags msg = ps4_net_to_host_msg_flags(flags);
    if (msg.peeklen && !buf) {
        // MSG_PEEKLEN with no buffer: peek into our own and return the size waiting.
        const size_t size = (size_t)(len < MAX_PEEK_LEN ? len : MAX_PEEK_LEN);
        void *scratch = malloc(size ? size : 1);
        if (!scratch) return set_errno(ctx, ORBIS_NET_ENOMEM);
        const int32_t r = ps4_sceNetRecvfrom(ctx, s, scratch, size, (flags & ~ORBIS_NET_MSG_PEEKLEN) | ORBIS_NET_MSG_PEEK,
                                             addr, paddrlen);
        free(scratch);
        return r;
    }
    if (!buf && len != 0) return set_errno(ctx, ORBIS_NET_EFAULT);
    if (!paddrlen) addr = NULL; // FreeBSD: no length, no address, not an error
    if (msg.unsupported != 0) fprintf(stderr, "[ps4-net] recv: ignoring flags 0x%x\n", msg.unsupported);
    Ps4NetSocketInfo info;
    const int64_t i = ps4_net_socket_get_info(s, &info);
    if (i < 0) return ret(ctx, i);
    if (info.p2p) {
        Ps4P2PEndpoint from = {0, 0};
        uint16_t from_vport = 0;
        const int64_t r = ps4_net_p2p_socket_recvfrom(s, buf, (size_t)len, msg.peek, msg.dontwait, addr ? &from : NULL,
                                                      addr ? &from_vport : NULL, msg.waitall);
        if (r >= 0 && addr) ps4_net_to_orbis_p2p_address(&from, from_vport, p2p_stream(&info), addr, paddrlen);
        return ret(ctx, r);
    }
    struct sockaddr_storage host;
    socklen_t host_len = sizeof(host);
    memset(&host, 0, sizeof(host));
    const int64_t r = ps4_net_socket_recvfrom(s, buf, (size_t)len, msg.host, msg.dontwait,
                                              addr ? (struct sockaddr *)&host : NULL, addr ? &host_len : NULL);
    if (r >= 0 && addr) ps4_net_to_orbis_sockaddr((const struct sockaddr *)&host, host_len, addr, paddrlen);
    return ret(ctx, r);
}

// 0 or the errno; *total is the summed iovec length.
static int gather_size(GuestContext *ctx, const OrbisNetMsghdr *msg, size_t *total) {
    if (msg->msg_iovlen > 1024) return ORBIS_NET_EMSGSIZE; // more than 1024 iovecs
    if (msg->msg_iovlen < 0 || (msg->msg_iovlen > 0 && !msg->msg_iov)) return ORBIS_NET_EINVAL;
    const OrbisNetIovec *iov = (const OrbisNetIovec *)G(ctx, msg->msg_iov);
    *total = 0;
    for (int32_t i = 0; i < msg->msg_iovlen; i++) {
        if (!iov[i].iov_base && iov[i].iov_len != 0) return ORBIS_NET_EFAULT;
        *total += (size_t)iov[i].iov_len;
        if (*total > (size_t)INT32_MAX) return ORBIS_NET_EINVAL;
    }
    return 0;
}

int32_t ps4_sceNetSendmsg(GuestContext *ctx, int32_t s, const OrbisNetMsghdr *msg, int32_t flags) {
    if (!msg) return set_errno(ctx, ORBIS_NET_EFAULT);
    if (msg->msg_control && msg->msg_controllen != 0) {
        fprintf(stderr, "[ps4-net] sendmsg: ancillary data ignored (%u bytes)\n", msg->msg_controllen);
    }
    // Gathered into one buffer so a datagram goes out whole.
    size_t total = 0;
    const int e = gather_size(ctx, msg, &total);
    if (e != 0) return set_errno(ctx, e);
    uint8_t *buffer = (uint8_t *)malloc(total ? total : 1);
    if (!buffer) return set_errno(ctx, ORBIS_NET_ENOMEM);
    const OrbisNetIovec *iov = (const OrbisNetIovec *)G(ctx, msg->msg_iov);
    size_t offset = 0;
    for (int32_t i = 0; i < msg->msg_iovlen; i++) {
        if (iov[i].iov_len) memcpy(buffer + offset, G(ctx, iov[i].iov_base), (size_t)iov[i].iov_len);
        offset += (size_t)iov[i].iov_len;
    }
    const int32_t r = ps4_sceNetSendto(ctx, s, buffer, total, flags, (const OrbisNetSockaddr *)G(ctx, msg->msg_name),
                                       msg->msg_namelen);
    free(buffer);
    return r;
}

int32_t ps4_sceNetRecvmsg(GuestContext *ctx, int32_t s, OrbisNetMsghdr *msg, int32_t flags) {
    if (!msg) return set_errno(ctx, ORBIS_NET_EFAULT);
    if ((flags & ORBIS_NET_MSG_PEEKLEN) == ORBIS_NET_MSG_PEEKLEN) return set_errno(ctx, ORBIS_NET_EINVAL);
    size_t total = 0;
    const int e = gather_size(ctx, msg, &total);
    if (e != 0) return set_errno(ctx, e);
    uint8_t *buffer = (uint8_t *)malloc(total ? total : 1);
    if (!buffer) return set_errno(ctx, ORBIS_NET_ENOMEM);
    uint32_t namelen = msg->msg_name ? msg->msg_namelen : 0;
    const int32_t r = ps4_sceNetRecvfrom(ctx, s, buffer, total, flags, (OrbisNetSockaddr *)G(ctx, msg->msg_name),
                                         msg->msg_name ? &namelen : NULL);
    if (r >= 0) {
        const OrbisNetIovec *iov = (const OrbisNetIovec *)G(ctx, msg->msg_iov);
        size_t offset = 0;
        for (int32_t i = 0; i < msg->msg_iovlen && offset < (size_t)r; i++) {
            size_t chunk = (size_t)r - offset;
            if (chunk > iov[i].iov_len) chunk = (size_t)iov[i].iov_len;
            if (chunk) memcpy(G(ctx, iov[i].iov_base), buffer + offset, chunk);
            offset += chunk;
        }
        msg->msg_namelen = namelen;
    }
    free(buffer);
    return r;
}

int32_t ps4_sceNetShutdown(GuestContext *ctx, int32_t s, int32_t how) {
    if (how < ORBIS_NET_SHUT_RD || how > ORBIS_NET_SHUT_RDWR) return set_errno(ctx, ORBIS_NET_EINVAL);
    return ret(ctx, ps4_net_socket_shutdown(s, how));
}

int32_t ps4_sceNetGetsockname(GuestContext *ctx, int32_t s, OrbisNetSockaddr *addr, uint32_t *paddrlen) {
    if (!addr || !paddrlen) return set_errno(ctx, ORBIS_NET_EFAULT);
    Ps4NetSocketInfo info;
    const int64_t i = ps4_net_socket_get_info(s, &info);
    if (i < 0) return ret(ctx, i);
    if (info.p2p) {
        uint16_t udp_port = 0;
        uint16_t vport = 0;
        const int64_t r = ps4_net_p2p_socket_getname(s, &udp_port, &vport);
        if (r == 0) {
            // As shadPS4: INADDR_ANY (its TODO: maybe the advertised NAT address).
            const Ps4P2PEndpoint local = {htonl(INADDR_ANY), htons(udp_port)};
            ps4_net_to_orbis_p2p_address(&local, vport, p2p_stream(&info), addr, paddrlen);
        }
        return ret(ctx, r);
    }
    struct sockaddr_storage host;
    socklen_t host_len = sizeof(host);
    memset(&host, 0, sizeof(host));
    const int64_t r = ps4_net_socket_getname(s, (struct sockaddr *)&host, &host_len);
    if (r == 0) ps4_net_to_orbis_sockaddr((const struct sockaddr *)&host, host_len, addr, paddrlen);
    return ret(ctx, r);
}

int32_t ps4_sceNetGetpeername(GuestContext *ctx, int32_t s, OrbisNetSockaddr *addr, uint32_t *paddrlen) {
    if (!addr || !paddrlen) return set_errno(ctx, ORBIS_NET_EFAULT);
    Ps4NetSocketInfo info;
    const int64_t i = ps4_net_socket_get_info(s, &info);
    if (i < 0) return ret(ctx, i);
    if (info.p2p) {
        Ps4P2PEndpoint peer = {0, 0};
        uint16_t vport = 0;
        const int64_t r = ps4_net_p2p_socket_getpeername(s, &peer, &vport);
        if (r == 0) ps4_net_to_orbis_p2p_address(&peer, vport, p2p_stream(&info), addr, paddrlen);
        return ret(ctx, r);
    }
    struct sockaddr_storage host;
    socklen_t host_len = sizeof(host);
    memset(&host, 0, sizeof(host));
    const int64_t r = ps4_net_socket_getpeername(s, (struct sockaddr *)&host, &host_len);
    if (r == 0) ps4_net_to_orbis_sockaddr((const struct sockaddr *)&host, host_len, addr, paddrlen);
    return ret(ctx, r);
}

int32_t ps4_sceNetIoctl(GuestContext *ctx, int32_t s, uint64_t cmd, void *data) {
    Ps4NetSocketInfo info;
    const int64_t r = ps4_net_socket_get_info(s, &info);
    if (r < 0) return ret(ctx, r);
    if (info.p2p) {
        const int e = ps4_net_p2p_key_ioctl(htons(ps4_net_p2p_bound_port()), cmd, data);
        if (e != 0) {
            fprintf(stderr, "[ps4-net] P2P ioctl 0x%llx failed: %d\n", (unsigned long long)cmd, e);
            return set_errno(ctx, e);
        }
        return 0;
    }
    switch ((uint32_t)cmd) {
    case ORBIS_NET_FIONBIO:
        if (!data) return set_errno(ctx, ORBIS_NET_EFAULT);
        return ret(ctx, ps4_net_socket_set_nonblocking(s, *(const int32_t *)data != 0));
    case ORBIS_NET_FIONREAD: {
        if (!data) return set_errno(ctx, ORBIS_NET_EFAULT);
        const int64_t n = ps4_net_socket_bytes_readable(s);
        if (n < 0) return ret(ctx, n);
        *(int32_t *)data = (int32_t)n;
        return 0;
    }
    default:
        fprintf(stderr, "[ps4-net] unsupported ioctl 0x%llx on a non-P2P socket\n", (unsigned long long)cmd);
        return set_errno(ctx, ORBIS_NET_EINVAL);
    }
}

static bool fill_sock_info(int32_t id, OrbisNetSockInfo *out) {
    Ps4NetSocketInfo info;
    if (ps4_net_socket_get_info(id, &info) != 0) return false;
    Ps4NetSocketAttributes attributes;
    if (ps4_net_socket_get_attributes(id, &attributes) != 0) return false;
    OrbisNetSockInfo record;
    memset(&record, 0, sizeof(record));
    memcpy(record.name, attributes.name, sizeof(record.name) - 1);
    record.s = id;
    record.socket_type = (int8_t)ps4_net_to_orbis_socket_type(info.type, info.p2p);
    record.policy = (int8_t)attributes.policy;
    record.priority = (int8_t)attributes.priority;
    record.flags = ORBIS_NET_SOCKINFO_F_SELF | (info.nonblocking ? ORBIS_NET_SOCKINFO_F_NONBLOCK : 0);
    record.send_buffer_size = attributes.snd_buf ? attributes.snd_buf : ps4_net_default_send_buffer(info.type);
    record.recv_buffer_size = attributes.rcv_buf ? attributes.rcv_buf : ps4_net_default_receive_buffer(info.type);
    // As shadPS4: TCP state, queues and bandwidth are not tracked. OPENED, or ESTABLISHED once
    // there is a peer.
    record.state = ORBIS_NET_SOCKINFO_STATE_OPENED;
    if (info.p2p) {
        uint16_t udp_port = 0;
        uint16_t vport = 0;
        if (ps4_net_p2p_socket_getname(id, &udp_port, &vport) == 0) {
            record.local_port = htons(udp_port);
            record.local_vport = htons(vport);
        }
        Ps4P2PEndpoint peer;
        uint16_t peer_vport = 0;
        if (ps4_net_p2p_socket_getpeername(id, &peer, &peer_vport) == 0) {
            OrbisNetSockaddrIn in;
            uint32_t in_len = sizeof(in);
            ps4_net_to_orbis_p2p_address(&peer, peer_vport, info.type == SOCK_STREAM, (OrbisNetSockaddr *)&in, &in_len);
            record.remote_adr.inaddr_addr = in.sin_addr;
            record.remote_port = in.sin_port;
            record.remote_vport = in.sin_vport;
            record.state = ORBIS_NET_SOCKINFO_STATE_ESTABLISHED;
        }
        memcpy(out, &record, sizeof(record));
        return true;
    }
    struct sockaddr_storage host;
    socklen_t len = sizeof(host);
    if (ps4_net_socket_getname(id, (struct sockaddr *)&host, &len) == 0 && host.ss_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)&host;
        record.local_adr.inaddr_addr = in->sin_addr.s_addr;
        record.local_port = in->sin_port;
    }
    len = sizeof(host);
    if (ps4_net_socket_getpeername(id, (struct sockaddr *)&host, &len) == 0 && host.ss_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)&host;
        record.remote_adr.inaddr_addr = in->sin_addr.s_addr;
        record.remote_port = in->sin_port;
        record.state = ORBIS_NET_SOCKINFO_STATE_ESTABLISHED;
    }
    const int64_t queued = ps4_net_socket_bytes_readable(id);
    if (queued >= 0) record.recv_queue_length = (int32_t)queued;
    memcpy(out, &record, sizeof(record));
    return true;
}

int32_t ps4_sceNetGetSockInfo(GuestContext *ctx, int32_t s, OrbisNetSockInfo *info, int32_t n, int32_t flags) {
    (void)flags;
    if (!info) {
        // No buffer and s < 0: the socket count.
        return s < 0 ? ps4_net_list_sockets(NULL, 0) : set_errno(ctx, ORBIS_NET_EINVAL);
    }
    if (n <= 0) return set_errno(ctx, ORBIS_NET_EINVAL);
    if (s >= 0) return fill_sock_info(s, info) ? 1 : set_errno(ctx, ORBIS_NET_EBADF);
    int32_t ids[PS4_NET_MAX_SOCKETS * 2];
    int count = ps4_net_list_sockets(ids, PS4_NET_MAX_SOCKETS * 2);
    if (count > PS4_NET_MAX_SOCKETS * 2) count = PS4_NET_MAX_SOCKETS * 2;
    int32_t written = 0;
    for (int i = 0; i < count && written < n; i++) {
        written += fill_sock_info(ids[i], &info[written]) ? 1 : 0; // closed meanwhile
    }
    return written;
}

// ---------------------------------------------------------------------------------------------
// Epolls

int32_t ps4_sceNetEpollCreate(GuestContext *ctx, const char *name, int32_t flags) {
    if (flags != 0) return set_errno(ctx, ORBIS_NET_EINVAL);
    if (name_too_long(name)) return set_errno(ctx, ORBIS_NET_ENAMETOOLONG);
    return ret(ctx, ps4_net_epoll_create());
}

int32_t ps4_sceNetEpollControl(GuestContext *ctx, int32_t eid, int32_t op, int32_t id, const OrbisNetEpollEvent *event) {
    Ps4NetEpollOp guest_op;
    switch (op) {
    case ORBIS_NET_EPOLL_CTL_ADD:
        guest_op = PS4_NET_EPOLL_ADD;
        break;
    case ORBIS_NET_EPOLL_CTL_MOD:
        guest_op = PS4_NET_EPOLL_MODIFY;
        break;
    case ORBIS_NET_EPOLL_CTL_DEL:
        guest_op = PS4_NET_EPOLL_DELETE;
        break;
    default:
        return set_errno(ctx, ORBIS_NET_EINVAL);
    }
    if ((guest_op == PS4_NET_EPOLL_DELETE) != (event == NULL)) return set_errno(ctx, ORBIS_NET_EINVAL);
    // Epolls can't be added (EPERM). Unknown ids are EBADF.
    const Ps4NetObjectKind kind = ps4_net_object_kind(id);
    if (ps4_net_object_kind(eid) != PS4_NET_OBJ_EPOLL || kind == PS4_NET_OBJ_NONE) {
        return set_errno(ctx, ORBIS_NET_EBADF);
    }
    if (kind == PS4_NET_OBJ_EPOLL) return set_errno(ctx, ORBIS_NET_EPERM);
    if (event && event->events == 0) return set_errno(ctx, ORBIS_NET_EINVAL);
    const uint32_t events =
        event ? ps4_net_to_host_epoll_events(event->events & (ORBIS_NET_EPOLLIN | ORBIS_NET_EPOLLOUT)) : 0;
    const uint64_t data = event ? event->data : 0;
    int64_t r = ps4_net_epoll_control(eid, guest_op, id, events, data);
    if (r == -ORBIS_NET_ENOENT && guest_op == PS4_NET_EPOLL_MODIFY) {
        r = ps4_net_epoll_control(eid, PS4_NET_EPOLL_ADD, id, events, data);
    }
    if (r == -ORBIS_NET_ENOENT) return set_errno(ctx, ORBIS_NET_EBADF); // DEL of an unregistered id
    return ret(ctx, r);
}

int32_t ps4_sceNetEpollWait(GuestContext *ctx, int32_t eid, OrbisNetEpollEvent *events, int32_t maxevents,
                            int32_t timeout_us) {
    if (ps4_net_object_kind(eid) != PS4_NET_OBJ_EPOLL) return set_errno(ctx, ORBIS_NET_EBADF);
    if (!events || maxevents <= 0) return set_errno(ctx, ORBIS_NET_EINVAL);
    Ps4NetGuestEpollEvent ready[256];
    const int cap = maxevents < 256 ? maxevents : 256;
    const int64_t r = ps4_net_epoll_wait(eid, ready, cap, timeout_us < 0 ? -1 : timeout_us);
    if (r < 0) return ret(ctx, r);
    for (int64_t i = 0; i < r; i++) {
        memset(&events[i], 0, sizeof(events[i]));
        events[i].events = ps4_net_to_orbis_epoll_events(ready[i].events);
        events[i].ident = (uint64_t)ready[i].ident;
        events[i].data = ready[i].data;
    }
    return (int32_t)r;
}

int32_t ps4_sceNetEpollAbort(GuestContext *ctx, int32_t eid, int32_t flags) {
    if (ps4_net_object_kind(eid) != PS4_NET_OBJ_EPOLL) return set_errno(ctx, ORBIS_NET_EBADF);
    if (((uint32_t)flags & ~PS4_NET_EPOLL_ABORT_PRESERVE) != 0) return set_errno(ctx, ORBIS_NET_EINVAL);
    return ret(ctx, ps4_net_epoll_abort(eid, (uint32_t)flags));
}

int32_t ps4_sceNetEpollDestroy(GuestContext *ctx, int32_t eid) {
    const Ps4NetObjectKind kind = ps4_net_object_kind(eid);
    if (kind != PS4_NET_OBJ_NONE && kind != PS4_NET_OBJ_EPOLL) return set_errno(ctx, ORBIS_NET_EPERM);
    return ret(ctx, ps4_net_epoll_destroy(eid));
}

// ---------------------------------------------------------------------------------------------
// Resolvers

static bool net_is_online(void) {
    return ps4_netctl_connected();
}

int32_t ps4_sceNetResolverCreate(GuestContext *ctx, const char *name, int32_t poolid, int32_t flags) {
    if (flags != 0) return set_errno(ctx, ORBIS_NET_EINVAL);
    if (name_too_long(name)) return set_errno(ctx, ORBIS_NET_ENAMETOOLONG);
    ps4_net_resolver_set_online_check(net_is_online);
    pthread_mutex_lock(&g_pools_mutex);
    Pool *pool = find_pool(poolid);
    if (!pool) {
        pthread_mutex_unlock(&g_pools_mutex);
        fprintf(stderr, "[ps4-net] unknown memory pool id %d\n", poolid);
        return set_errno(ctx, ORBIS_NET_EBADF);
    }
    const int32_t id = ps4_net_resolver_create(name);
    if (id < 0) {
        pthread_mutex_unlock(&g_pools_mutex);
        return set_errno(ctx, -id);
    }
    if (g_resolver_pool_count == g_resolver_pool_cap) {
        const int cap = g_resolver_pool_cap ? g_resolver_pool_cap * 2 : 8;
        ResolverPool *grown = (ResolverPool *)realloc(g_resolver_pools, (size_t)cap * sizeof(ResolverPool));
        if (!grown) {
            pthread_mutex_unlock(&g_pools_mutex);
            ps4_net_resolver_destroy(id);
            return set_errno(ctx, ORBIS_NET_ENOMEM);
        }
        g_resolver_pools = grown;
        g_resolver_pool_cap = cap;
    }
    // The resolver holds its pool until destroyed.
    pool->users++;
    g_resolver_pools[g_resolver_pool_count++] = (ResolverPool){id, poolid};
    pthread_mutex_unlock(&g_pools_mutex);
    return id;
}

int32_t ps4_sceNetResolverDestroy(GuestContext *ctx, int32_t rid) {
    const int e = ps4_net_resolver_destroy(rid);
    if (e != 0) return set_errno(ctx, e);
    release_resolver_pool(rid);
    return 0;
}

int32_t ps4_sceNetResolverAbort(GuestContext *ctx, int32_t rid, int32_t flags) {
    const int e = ps4_net_resolver_abort(rid, (uint32_t)flags);
    return e == 0 ? 0 : set_errno(ctx, e);
}

int32_t ps4_sceNetResolverGetError(GuestContext *ctx, int32_t rid, int32_t *status) {
    if (!status) return set_errno(ctx, ORBIS_NET_EINVAL);
    const int e = ps4_net_resolver_get_error(rid, status);
    return e == 0 ? 0 : set_errno(ctx, e);
}

int32_t ps4_sceNetResolverStartNtoa(GuestContext *ctx, int32_t rid, const char *hostname, OrbisNetInAddr *addr,
                                    int32_t flags) {
    return resolver_return(ctx, ps4_net_resolver_start_ntoa(rid, hostname, addr, NULL, (flags & ORBIS_NET_RESOLVER_ASYNC) != 0,
                                                            (flags & ORBIS_NET_RESOLVER_START_NTOA_DISABLE_IPADDRESS) != 0));
}

static int32_t start_ntoa_records(GuestContext *ctx, int32_t rid, const char *hostname, OrbisNetResolverInfo *info,
                                  int32_t flags) {
    return resolver_return(ctx, ps4_net_resolver_start_ntoa(rid, hostname, NULL, info, (flags & ORBIS_NET_RESOLVER_ASYNC) != 0,
                                                            (flags & ORBIS_NET_RESOLVER_START_NTOA_DISABLE_IPADDRESS) != 0));
}

// Flags 0x1000000 ask for AAAA records and 0x2000000 for A and AAAA. Only IPv4 is looked up,
// so AAAA-only has no records and A+AAAA returns the A records.
int32_t ps4_sceNetResolverStartNtoaMultipleRecordsEx(GuestContext *ctx, int32_t rid, const char *hostname,
                                                     OrbisNetResolverInfo *info, int32_t flags) {
    const uint32_t aaaa = 0x1000000;
    const uint32_t a_and_aaaa = 0x2000000;
    if (((uint32_t)flags & 0xfccefffeu) != 0) return resolver_return(ctx, ORBIS_NET_ERROR(ORBIS_NET_EINVAL));
    if (((uint32_t)flags & aaaa) != 0 && ((uint32_t)flags & a_and_aaaa) == 0) {
        fprintf(stderr, "[ps4-net] AAAA lookup of %s not supported\n", hostname ? hostname : "");
        return resolver_return(ctx, ORBIS_NET_ERROR_RESOLVER_ENORECORD);
    }
    return start_ntoa_records(ctx, rid, hostname, info, flags);
}

int32_t ps4_sceNetResolverStartAton(GuestContext *ctx, int32_t rid, const OrbisNetInAddr *addr, char *hostname,
                                    int32_t len, int32_t flags) {
    return resolver_return(ctx,
                           ps4_net_resolver_start_aton(rid, addr, hostname, len, (flags & ORBIS_NET_RESOLVER_ASYNC) != 0));
}

// ---------------------------------------------------------------------------------------------
// DNS servers

static pthread_mutex_t g_dns_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_dns_override[2];
static uint8_t g_dns6_override[32];

static bool is_zero(const uint8_t *bytes, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (bytes[i]) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Kernel integration (shadPS4 net_kernel.cpp)

bool ps4_net_kernel_owns(int32_t fd) {
    return ps4_net_object_kind(fd) != PS4_NET_OBJ_NONE || ps4_net_id_reserved(fd);
}

// shadPS4 Posix(): a failure copies the net errno to the kernel errno and returns -1.
static int64_t posix_result(GuestContext *ctx, int64_t result) {
    if (result < 0) {
        const int32_t *slot = net_errno_slot(ctx);
        set_guest_errno(ctx, slot ? *slot : ORBIS_NET_EINTERNAL);
        return -1;
    }
    return result;
}

static int64_t posix_error(GuestContext *ctx, int e) {
    set_guest_errno(ctx, e);
    return -1;
}

int32_t ps4_net_kernel_close(GuestContext *ctx, int32_t fd) {
    const Ps4NetObjectKind kind = ps4_net_object_kind(fd);
    if (kind == PS4_NET_OBJ_NONE) return (int32_t)posix_error(ctx, ORBIS_NET_EBADF);
    if (kind == PS4_NET_OBJ_EXTERNAL) {
        const int e = ps4_net_resolver_destroy(fd);
        if (e != 0) return (int32_t)posix_error(ctx, e);
        release_resolver_pool(fd);
        return 0;
    }
    const int64_t r = ps4_net_close_object(fd);
    return r == 0 ? 0 : (int32_t)posix_error(ctx, (int)-r);
}

int64_t ps4_net_kernel_read(GuestContext *ctx, int32_t fd, void *buf, uint64_t nbytes) {
    if (ps4_net_object_kind(fd) != PS4_NET_OBJ_SOCKET) return posix_error(ctx, ORBIS_NET_EBADF);
    int32_t *slot = net_errno_slot(ctx);
    const int32_t saved = slot ? *slot : 0;
    const int64_t r = posix_result(ctx, ps4_sceNetRecvfrom(ctx, fd, buf, nbytes, 0, NULL, NULL));
    if (slot) *slot = saved;
    return r;
}

int64_t ps4_net_kernel_write(GuestContext *ctx, int32_t fd, const void *buf, uint64_t nbytes) {
    if (ps4_net_object_kind(fd) != PS4_NET_OBJ_SOCKET) return posix_error(ctx, ORBIS_NET_EBADF);
    int32_t *slot = net_errno_slot(ctx);
    const int32_t saved = slot ? *slot : 0;
    const int64_t r = posix_result(ctx, ps4_sceNetSendto(ctx, fd, buf, nbytes, 0, NULL, 0));
    if (slot) *slot = saved;
    return r;
}

int32_t ps4_net_kernel_fcntl(GuestContext *ctx, int32_t fd, int32_t cmd, int64_t arg) {
    // FreeBSD F_GETFL / F_SETFL and O_RDWR / O_NONBLOCK.
    const int32_t getfl = 3;
    const int32_t setfl = 4;
    const int32_t o_rdwr = 0x0002;
    const int32_t o_nonblock = 0x0004;
    if (ps4_net_object_kind(fd) != PS4_NET_OBJ_SOCKET) return (int32_t)posix_error(ctx, ORBIS_NET_EBADF);
    if (cmd == getfl) {
        Ps4NetSocketInfo info;
        const int64_t r = ps4_net_socket_get_info(fd, &info);
        if (r < 0) return (int32_t)posix_error(ctx, (int)-r);
        return o_rdwr | (info.nonblocking ? o_nonblock : 0);
    }
    if (cmd == setfl) {
        const int64_t r = ps4_net_socket_set_nonblocking(fd, (arg & o_nonblock) != 0);
        return r < 0 ? (int32_t)posix_error(ctx, (int)-r) : 0;
    }
    return (int32_t)posix_error(ctx, ORBIS_NET_EINVAL);
}

int32_t ps4_net_kernel_duplicate(GuestContext *ctx, int32_t old_fd, int32_t new_fd) {
    const int64_t r = ps4_net_duplicate_object(old_fd, new_fd);
    return r < 0 ? (int32_t)posix_error(ctx, (int)-r) : 0;
}

// ---------------------------------------------------------------------------------------------
// Shims

#define A0 ctx->rdi
#define A1 ctx->rsi
#define A2 ctx->rdx
#define A3 ctx->rcx
#define A4 ctx->r8
#define A5 ctx->r9

#define RETURN32(expr)                       \
    do {                                     \
        ctx->rax = (uint64_t)(int64_t)(expr); \
        SHIM_RETURN();                       \
    } while (0)

void shim_sceNetInit(GuestContext *ctx) {
    ps4_net_resolver_set_online_check(net_is_online);
    RETURN32(0);
}

void shim_sceNetTerm(GuestContext *ctx) {
    RETURN32(0);
}

void shim_sceNetErrnoLoc(GuestContext *ctx) {
    ctx->rax = ctx->fs_base ? ctx->fs_base + RECOMP_TCB_NET_ERRNO_SLOT : 0;
    SHIM_RETURN();
}

void shim_sceNetSocket(GuestContext *ctx) {
    RETURN32(ps4_sceNetSocket(ctx, (const char *)G(ctx, A0), (int32_t)A1, (int32_t)A2, (int32_t)A3));
}

void shim_sceNetSocketClose(GuestContext *ctx) {
    RETURN32(ps4_sceNetSocketClose(ctx, (int32_t)A0));
}

void shim_sceNetSocketAbort(GuestContext *ctx) {
    RETURN32(ps4_sceNetSocketAbort(ctx, (int32_t)A0, (int32_t)A1));
}

void shim_sceNetBind(GuestContext *ctx) {
    RETURN32(ps4_sceNetBind(ctx, (int32_t)A0, (const OrbisNetSockaddr *)G(ctx, A1), (uint32_t)A2));
}

void shim_sceNetListen(GuestContext *ctx) {
    RETURN32(ps4_sceNetListen(ctx, (int32_t)A0, (int32_t)A1));
}

void shim_sceNetAccept(GuestContext *ctx) {
    RETURN32(ps4_sceNetAccept(ctx, (int32_t)A0, (OrbisNetSockaddr *)G(ctx, A1), (uint32_t *)G(ctx, A2)));
}

void shim_sceNetConnect(GuestContext *ctx) {
    RETURN32(ps4_sceNetConnect(ctx, (int32_t)A0, (const OrbisNetSockaddr *)G(ctx, A1), (uint32_t)A2));
}

void shim_sceNetSend(GuestContext *ctx) {
    RETURN32(ps4_sceNetSendto(ctx, (int32_t)A0, G(ctx, A1), A2, (int32_t)A3, NULL, 0));
}

void shim_sceNetSendto(GuestContext *ctx) {
    RETURN32(ps4_sceNetSendto(ctx, (int32_t)A0, G(ctx, A1), A2, (int32_t)A3, (const OrbisNetSockaddr *)G(ctx, A4),
                              (uint32_t)A5));
}

void shim_sceNetSendmsg(GuestContext *ctx) {
    RETURN32(ps4_sceNetSendmsg(ctx, (int32_t)A0, (const OrbisNetMsghdr *)G(ctx, A1), (int32_t)A2));
}

void shim_sceNetRecv(GuestContext *ctx) {
    RETURN32(ps4_sceNetRecvfrom(ctx, (int32_t)A0, G(ctx, A1), A2, (int32_t)A3, NULL, NULL));
}

void shim_sceNetRecvfrom(GuestContext *ctx) {
    RETURN32(ps4_sceNetRecvfrom(ctx, (int32_t)A0, G(ctx, A1), A2, (int32_t)A3, (OrbisNetSockaddr *)G(ctx, A4),
                                (uint32_t *)G(ctx, A5)));
}

void shim_sceNetRecvmsg(GuestContext *ctx) {
    RETURN32(ps4_sceNetRecvmsg(ctx, (int32_t)A0, (OrbisNetMsghdr *)G(ctx, A1), (int32_t)A2));
}

void shim_sceNetSetsockopt(GuestContext *ctx) {
    RETURN32(ps4_sceNetSetsockopt(ctx, (int32_t)A0, (int32_t)A1, (int32_t)A2, G(ctx, A3), (uint32_t)A4));
}

void shim_sceNetGetsockopt(GuestContext *ctx) {
    RETURN32(ps4_sceNetGetsockopt(ctx, (int32_t)A0, (int32_t)A1, (int32_t)A2, G(ctx, A3), (uint32_t *)G(ctx, A4)));
}

void shim_sceNetGetsockname(GuestContext *ctx) {
    RETURN32(ps4_sceNetGetsockname(ctx, (int32_t)A0, (OrbisNetSockaddr *)G(ctx, A1), (uint32_t *)G(ctx, A2)));
}

void shim_sceNetGetpeername(GuestContext *ctx) {
    RETURN32(ps4_sceNetGetpeername(ctx, (int32_t)A0, (OrbisNetSockaddr *)G(ctx, A1), (uint32_t *)G(ctx, A2)));
}

void shim_sceNetShutdown(GuestContext *ctx) {
    RETURN32(ps4_sceNetShutdown(ctx, (int32_t)A0, (int32_t)A1));
}

void shim_sceNetIoctl(GuestContext *ctx) {
    RETURN32(ps4_sceNetIoctl(ctx, (int32_t)A0, A1, G(ctx, A2)));
}

void shim_sceNetGetSockInfo(GuestContext *ctx) {
    RETURN32(ps4_sceNetGetSockInfo(ctx, (int32_t)A0, (OrbisNetSockInfo *)G(ctx, A1), (int32_t)A2, (int32_t)A3));
}

void shim_sceNetHtons(GuestContext *ctx) {
    ctx->rax = (uint64_t)__builtin_bswap16((uint16_t)A0);
    SHIM_RETURN();
}

void shim_sceNetHtonl(GuestContext *ctx) {
    ctx->rax = (uint64_t)__builtin_bswap32((uint32_t)A0);
    SHIM_RETURN();
}

void shim_sceNetHtonll(GuestContext *ctx) {
    ctx->rax = __builtin_bswap64(A0);
    SHIM_RETURN();
}

void shim_sceNetNtohs(GuestContext *ctx) {
    shim_sceNetHtons(ctx);
}

void shim_sceNetNtohl(GuestContext *ctx) {
    shim_sceNetHtonl(ctx);
}

void shim_sceNetNtohll(GuestContext *ctx) {
    shim_sceNetHtonll(ctx);
}

void shim_sceNetInetPton(GuestContext *ctx) {
    const char *src = (const char *)G(ctx, A1);
    void *dst = G(ctx, A2);
    if (!src || !dst) RETURN32(set_errno(ctx, ORBIS_NET_EFAULT));
    const int family = ps4_net_to_host_family((int32_t)A0);
    if (family != AF_INET && family != AF_INET6) RETURN32(set_errno(ctx, ORBIS_NET_EAFNOSUPPORT));
    RETURN32(inet_pton(family, src, dst)); // 1 on success, 0 for malformed text
}

void shim_sceNetInetPtonEx(GuestContext *ctx) {
    // As shadPS4: the flags are unknown (IPv6 scope?), IPv4 needs none.
    if ((int32_t)A3 != 0) fprintf(stderr, "[ps4-net] sceNetInetPtonEx: ignoring flags 0x%x\n", (unsigned)A3);
    shim_sceNetInetPton(ctx);
}

void shim_sceNetInetNtop(GuestContext *ctx) {
    const void *src = G(ctx, A1);
    char *dst = (char *)G(ctx, A2);
    if (!src || !dst) {
        store_net_errno(ctx, ORBIS_NET_EFAULT);
        RETURN32(0);
    }
    const int family = ps4_net_to_host_family((int32_t)A0);
    if (family != AF_INET && family != AF_INET6) {
        store_net_errno(ctx, ORBIS_NET_EAFNOSUPPORT);
        ctx->rax = 0;
        SHIM_RETURN();
    }
    if (!inet_ntop(family, src, dst, (socklen_t)(uint32_t)A3)) {
        store_net_errno(ctx, ORBIS_NET_ENOSPC);
        ctx->rax = 0;
        SHIM_RETURN();
    }
    ctx->rax = A2;
    SHIM_RETURN();
}

void shim_sceNetEtherNtostr(GuestContext *ctx) {
    const OrbisNetEtherAddr *n = (const OrbisNetEtherAddr *)G(ctx, A0);
    char *str = (char *)G(ctx, A1);
    if (!n || !str) RETURN32(set_errno(ctx, ORBIS_NET_EFAULT));
    if (A2 < ORBIS_NET_ETHER_ADDRSTRLEN) RETURN32(set_errno(ctx, ORBIS_NET_ENOSPC));
    snprintf(str, (size_t)A2, "%02x:%02x:%02x:%02x:%02x:%02x", n->data[0], n->data[1], n->data[2], n->data[3],
             n->data[4], n->data[5]);
    RETURN32(0);
}

void shim_sceNetEtherStrton(GuestContext *ctx) {
    const char *str = (const char *)G(ctx, A0);
    OrbisNetEtherAddr *n = (OrbisNetEtherAddr *)G(ctx, A1);
    if (!str || !n) RETURN32(set_errno(ctx, ORBIS_NET_EFAULT));
    OrbisNetEtherAddr out;
    const char *p = str;
    for (int i = 0; i < 6; i++) {
        int value = 0;
        int digits = 0;
        for (; digits < 2 && isxdigit((unsigned char)*p); ++digits, ++p) {
            const char c = (char)tolower((unsigned char)*p);
            value = value * 16 + (c <= '9' ? c - '0' : c - 'a' + 10);
        }
        if (digits == 0 || (i < 5 && *p != ':' && *p != '-') || (i == 5 && *p != '\0')) {
            RETURN32(set_errno(ctx, ORBIS_NET_EINVAL));
        }
        out.data[i] = (uint8_t)value;
        if (i < 5) ++p;
    }
    *n = out;
    RETURN32(0);
}

void shim_sceNetPoolCreate(GuestContext *ctx) {
    RETURN32(ps4_sceNetPoolCreate(ctx, (const char *)G(ctx, A0), (int32_t)A1, (int32_t)A2));
}

void shim_sceNetPoolDestroy(GuestContext *ctx) {
    RETURN32(ps4_sceNetPoolDestroy(ctx, (int32_t)A0));
}

void shim_sceNetGetMemoryPoolStats(GuestContext *ctx) {
    OrbisNetMemoryPoolStats *stat = (OrbisNetMemoryPoolStats *)G(ctx, A1);
    pthread_mutex_lock(&g_pools_mutex);
    const Pool *pool = find_pool((int32_t)A0);
    const int32_t size = pool ? pool->size : 0;
    pthread_mutex_unlock(&g_pools_mutex);
    if (!pool) RETURN32(set_errno(ctx, ORBIS_NET_EBADF));
    if (!stat) RETURN32(set_errno(ctx, ORBIS_NET_EINVAL));
    memset(stat, 0, sizeof(*stat));
    stat->pool_size = (uint64_t)size;
    RETURN32(0);
}

void shim_sceNetEpollCreate(GuestContext *ctx) {
    RETURN32(ps4_sceNetEpollCreate(ctx, (const char *)G(ctx, A0), (int32_t)A1));
}

void shim_sceNetEpollControl(GuestContext *ctx) {
    RETURN32(ps4_sceNetEpollControl(ctx, (int32_t)A0, (int32_t)A1, (int32_t)A2, (const OrbisNetEpollEvent *)G(ctx, A3)));
}

void shim_sceNetEpollWait(GuestContext *ctx) {
    RETURN32(ps4_sceNetEpollWait(ctx, (int32_t)A0, (OrbisNetEpollEvent *)G(ctx, A1), (int32_t)A2, (int32_t)A3));
}

void shim_sceNetEpollAbort(GuestContext *ctx) {
    RETURN32(ps4_sceNetEpollAbort(ctx, (int32_t)A0, (int32_t)A1));
}

void shim_sceNetEpollDestroy(GuestContext *ctx) {
    RETURN32(ps4_sceNetEpollDestroy(ctx, (int32_t)A0));
}

void shim_sceNetResolverCreate(GuestContext *ctx) {
    RETURN32(ps4_sceNetResolverCreate(ctx, (const char *)G(ctx, A0), (int32_t)A1, (int32_t)A2));
}

void shim_sceNetResolverDestroy(GuestContext *ctx) {
    RETURN32(ps4_sceNetResolverDestroy(ctx, (int32_t)A0));
}

void shim_sceNetResolverAbort(GuestContext *ctx) {
    RETURN32(ps4_sceNetResolverAbort(ctx, (int32_t)A0, (int32_t)A1));
}

void shim_sceNetResolverGetError(GuestContext *ctx) {
    RETURN32(ps4_sceNetResolverGetError(ctx, (int32_t)A0, (int32_t *)G(ctx, A1)));
}

// (rid, hostname, addr, timeout, retry, flags)
void shim_sceNetResolverStartNtoa(GuestContext *ctx) {
    RETURN32(ps4_sceNetResolverStartNtoa(ctx, (int32_t)A0, (const char *)G(ctx, A1), (OrbisNetInAddr *)G(ctx, A2),
                                         (int32_t)A5));
}

// (rid, hostname, info, timeout, retry, flags)
void shim_sceNetResolverStartNtoaMultipleRecords(GuestContext *ctx) {
    RETURN32(start_ntoa_records(ctx, (int32_t)A0, (const char *)G(ctx, A1), (OrbisNetResolverInfo *)G(ctx, A2),
                                (int32_t)A5));
}

void shim_sceNetResolverStartNtoaMultipleRecordsEx(GuestContext *ctx) {
    RETURN32(ps4_sceNetResolverStartNtoaMultipleRecordsEx(ctx, (int32_t)A0, (const char *)G(ctx, A1),
                                                          (OrbisNetResolverInfo *)G(ctx, A2), (int32_t)A5));
}

// (rid, addr, hostname, len, timeout, retry, flags): flags is the 7th argument, on the stack.
void shim_sceNetResolverStartAton(GuestContext *ctx) {
    RETURN32(ps4_sceNetResolverStartAton(ctx, (int32_t)A0, (const OrbisNetInAddr *)G(ctx, A1), (char *)G(ctx, A2),
                                         (int32_t)A3, (int32_t)stack_arg(ctx, 0)));
}

// Not implemented in shadPS4 either: fail rather than report success with nothing written.
#define RESOLVER_NOT_SUPPORTED(name)                                                       \
    void shim_##name(GuestContext *ctx) {                                                  \
        fprintf(stderr, "[ps4-net] %s: not supported (as in shadPS4)\n", #name);           \
        RETURN32(resolver_return(ctx, ORBIS_NET_ERROR_RESOLVER_ENOSUPPORT));               \
    }
RESOLVER_NOT_SUPPORTED(sceNetResolverConnect)
RESOLVER_NOT_SUPPORTED(sceNetResolverConnectAbort)
RESOLVER_NOT_SUPPORTED(sceNetResolverConnectCreate)
RESOLVER_NOT_SUPPORTED(sceNetResolverConnectDestroy)
RESOLVER_NOT_SUPPORTED(sceNetResolverStartAton6)
RESOLVER_NOT_SUPPORTED(sceNetResolverStartNtoa6)

// Returns the number of servers, 0 to 2.
void shim_sceNetGetDnsInfo(GuestContext *ctx) {
    uint32_t *info = (uint32_t *)G(ctx, A0);
    if (!info || (int32_t)A1 != 0) RETURN32(set_errno(ctx, ORBIS_NET_EINVAL));
    pthread_mutex_lock(&g_dns_mutex);
    uint32_t servers[2] = {g_dns_override[0], g_dns_override[1]};
    pthread_mutex_unlock(&g_dns_mutex);
    if (servers[0] == 0 && ps4_netctl_connected()) {
        // What sceNetCtlGetInfo reports while online (1.1.1.1).
        servers[0] = servers[1] = htonl(0x01010101u);
    }
    info[0] = servers[0];
    info[1] = servers[1];
    RETURN32(servers[0] == 0 ? 0 : servers[1] == 0 ? 1 : 2);
}

void shim_sceNetGetDns6Info(GuestContext *ctx) {
    uint8_t *info = (uint8_t *)G(ctx, A0);
    if (!info || (int32_t)A1 != 0) RETURN32(set_errno(ctx, ORBIS_NET_EINVAL));
    uint8_t servers[32];
    pthread_mutex_lock(&g_dns_mutex);
    memcpy(servers, g_dns6_override, sizeof(servers)); // no IPv6 servers otherwise
    pthread_mutex_unlock(&g_dns_mutex);
    memcpy(info, servers, sizeof(servers));
    RETURN32(is_zero(servers, 16) ? 0 : is_zero(servers + 16, 16) ? 1 : 2);
}

// A null info clears the override.
void shim_sceNetSetDnsInfo(GuestContext *ctx) {
    const uint32_t *info = (const uint32_t *)G(ctx, A0);
    if ((int32_t)A1 != 0 || (info && info[0] == 0)) RETURN32(set_errno(ctx, ORBIS_NET_EINVAL));
    pthread_mutex_lock(&g_dns_mutex);
    g_dns_override[0] = info ? info[0] : 0;
    g_dns_override[1] = info ? info[1] : 0;
    pthread_mutex_unlock(&g_dns_mutex);
    RETURN32(0);
}

void shim_sceNetSetDns6Info(GuestContext *ctx) {
    const uint8_t *info = (const uint8_t *)G(ctx, A0);
    if ((int32_t)A1 != 0 || (info && is_zero(info, 16))) RETURN32(set_errno(ctx, ORBIS_NET_EINVAL));
    pthread_mutex_lock(&g_dns_mutex);
    memset(g_dns6_override, 0, sizeof(g_dns6_override));
    if (info) memcpy(g_dns6_override, info, sizeof(g_dns6_override));
    pthread_mutex_unlock(&g_dns_mutex);
    RETURN32(0);
}

// The interface names, in guest memory once (sceNetGetIfName returns a pointer to one).
static const char *const g_if_names[] = {"lo0", "eth0", "eth1", "dbg0", "wlan0", "wlan1", "gbe0", "bt0", "phone0", "pppoe0"};
#define IF_NAME_COUNT (sizeof(g_if_names) / sizeof(g_if_names[0]))
#define IF_NAME_SLOT 8
static pthread_mutex_t g_if_names_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_if_names_guest;

void shim_sceNetGetIfName(GuestContext *ctx) {
    const uint32_t index = (uint32_t)A0;
    pthread_mutex_lock(&g_if_names_mutex);
    if (!g_if_names_guest) {
        const uint64_t g = recomp_vm_alloc_named(ctx, 0x1000, PROT_READ | PROT_WRITE, 0, "SceNetIfNames");
        if (g && g != (uint64_t)-1) {
            for (size_t i = 0; i <= IF_NAME_COUNT; i++) {
                // The last slot is the empty string returned for a bad index.
                char *slot = (char *)G(ctx, g + i * IF_NAME_SLOT);
                memset(slot, 0, IF_NAME_SLOT);
                if (i < IF_NAME_COUNT) memcpy(slot, g_if_names[i], strlen(g_if_names[i]));
            }
            g_if_names_guest = g;
        }
    }
    const uint64_t names = g_if_names_guest;
    pthread_mutex_unlock(&g_if_names_mutex);
    if (index >= IF_NAME_COUNT) store_net_errno(ctx, ORBIS_NET_EINVAL);
    ctx->rax = names ? names + (index >= IF_NAME_COUNT ? IF_NAME_COUNT : index) * IF_NAME_SLOT : 0;
    SHIM_RETURN();
}

void shim_sceNetGetMacAddress(GuestContext *ctx) {
    OrbisNetEtherAddr *addr = (OrbisNetEtherAddr *)G(ctx, A0);
    if (!addr) RETURN32(set_errno(ctx, ORBIS_NET_EINVAL));
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    if (!ps4_netctl_host_ether_addr(mac)) fprintf(stderr, "[ps4-net] host MAC address not available\n");
    memcpy(addr->data, mac, sizeof(mac));
    RETURN32(0);
}

void shim_sceNetGetRandom(GuestContext *ctx) {
    uint32_t *out = (uint32_t *)G(ctx, A0);
    if (!out) RETURN32(set_errno(ctx, ORBIS_NET_EINVAL));
    *out = arc4random();
    RETURN32(0);
}

void shim_sceNetGetStatisticsInfo(GuestContext *ctx) {
    OrbisNetStatisticsInfo *info = (OrbisNetStatisticsInfo *)G(ctx, A0);
    if (!info) RETURN32(set_errno(ctx, ORBIS_NET_EFAULT));
    // As shadPS4: no host equivalent; plenty free and nothing queued so titles don't throttle.
    const int32_t plenty = 16 * 1024 * 1024;
    *info = (OrbisNetStatisticsInfo){plenty, plenty, 0, 0, plenty, plenty};
    RETURN32(0);
}

// Process time in microseconds (sceKernelGetProcessTime). No return value; a null pointer is
// ignored.
void shim_sceNetGetSystemTime(GuestContext *ctx) {
    uint64_t *out = (uint64_t *)G(ctx, A0);
    if (out) *out = sceKernelGetProcessTime();
    SHIM_RETURN();
}

// Guest memory (shadPS4 malloc/calloc in the shared address space): a header holds the size
// for sceNetMemoryFree.
#define NET_MEMORY_HEADER 16

void shim_sceNetMemoryAllocate(GuestContext *ctx) {
    const int64_t size = (int64_t)A0;
    const int32_t flags = (int32_t)A1;
    if (size <= 0) RETURN32(0);
    const size_t total = (size_t)size + NET_MEMORY_HEADER;
    const uint64_t g = recomp_vm_alloc_named(ctx, total, PROT_READ | PROT_WRITE, 0, "SceNetMemory");
    if (!g || g == (uint64_t)-1) {
        ctx->rax = 0;
        SHIM_RETURN();
    }
    *(uint64_t *)G(ctx, g) = total;
    const int32_t zero_fill = 2;
    if (flags & zero_fill) memset(G(ctx, g + NET_MEMORY_HEADER), 0, (size_t)size);
    ctx->rax = g + NET_MEMORY_HEADER;
    SHIM_RETURN();
}

void shim_sceNetMemoryFree(GuestContext *ctx) {
    if (A0 >= NET_MEMORY_HEADER) {
        const uint64_t g = A0 - NET_MEMORY_HEADER;
        recomp_vm_free(ctx, g, (size_t) * (uint64_t *)G(ctx, g));
    }
    SHIM_RETURN();
}

// shadPS4 ThreadReturn: a kernel error 0x8002xxxx with a POSIX errno in its low byte.
static int32_t thread_return(GuestContext *ctx, int32_t code) {
    if (code < 0) {
        store_net_errno(ctx, ((uint32_t)code & 0xff00) == 0x100 ? (int32_t)((uint32_t)code & 0xff) : 0xcd);
    }
    return code;
}

typedef struct NetThreadStart {
    uint64_t entry;
    uint64_t arg;
    uint32_t flags;
} NetThreadStart;

// shadPS4 NetThreadEntry: runs param->entry(param->arg); with flag 1 the thread then exits
// through pthread_exit, which is what returning from the start routine does as well.
static void net_thread_entry(GuestContext *child, void *p) {
    NetThreadStart start = *(NetThreadStart *)p;
    free(p);
    recomp_shim_call_guest(child, start.entry, 0, start.arg, 0, 0, 0);
}

void shim_sceNetThreadCreate(GuestContext *ctx) {
    const OrbisNetThreadParam *param = (const OrbisNetThreadParam *)G(ctx, A1);
    if (!param) RETURN32(set_errno(ctx, ORBIS_NET_EINVAL));
    NetThreadStart *start = (NetThreadStart *)malloc(sizeof(NetThreadStart));
    if (!start) RETURN32(thread_return(ctx, ORBIS_KERNEL_ERROR_UNKNOWN + ENOMEM));
    *start = (NetThreadStart){param->entry, param->arg, param->flags};
    const char *name = (const char *)G(ctx, A2);
    const int e = ps4_create_host_entry_thread(ctx, A0, NULL, net_thread_entry, start, name ? name : "SceNet");
    if (e != 0) {
        free(start);
        fprintf(stderr, "[ps4-net] thread '%s' not created: %d\n", name ? name : "", e);
        RETURN32(thread_return(ctx, ORBIS_KERNEL_ERROR_UNKNOWN + e));
    }
    RETURN32(0);
}

void shim_sceNetThreadExit(GuestContext *ctx) {
    ctx->rdi = 0;
    shim_scePthreadExit(ctx);
}

void shim_sceNetThreadJoin(GuestContext *ctx) {
    const int e = ps4_join_guest_thread(A0, NULL);
    RETURN32(e == 0 ? 0 : thread_return(ctx, ORBIS_KERNEL_ERROR_UNKNOWN + e));
}

void shim_sceNetUsleep(GuestContext *ctx) {
    const int32_t microseconds = (int32_t)A0;
    if (microseconds < 0) RETURN32(set_errno(ctx, ORBIS_NET_EINVAL));
    usleep((useconds_t)microseconds);
    RETURN32(0);
}

void shim_sceNetEmulationGet(GuestContext *ctx) {
    fprintf(stderr, "[ps4-net] network emulation is not available on retail units\n");
    RETURN32(set_errno(ctx, ORBIS_NET_EOPNOTSUPP));
}

void shim_sceNetEmulationSet(GuestContext *ctx) {
    fprintf(stderr, "[ps4-net] network emulation is not available on retail units\n");
    RETURN32(set_errno(ctx, ORBIS_NET_EOPNOTSUPP));
}

// Stubs in shadPS4 that check the memory pool id first.
#define POOL_CHECKED_STUB(name)                                                     \
    void shim_##name(GuestContext *ctx) {                                           \
        const int e = check_pool_id((int32_t)A0);                                   \
        if (e != 0) RETURN32(set_errno(ctx, e));                                    \
        fprintf(stderr, "[ps4-stub] %s -> 0 (stubbed, as in shadPS4)\n", #name);    \
        RETURN32(0);                                                                \
    }
POOL_CHECKED_STUB(sceNetShowIfconfigWithMemory)
POOL_CHECKED_STUB(sceNetShowNetstatWithMemory)
POOL_CHECKED_STUB(sceNetShowPolicyWithMemory)
POOL_CHECKED_STUB(sceNetShowRoute6WithMemory)
POOL_CHECKED_STUB(sceNetShowRouteWithMemory)

// Stubs in shadPS4 as well.
PS4_HLE_STUB(sceNetAddrConfig6GetInfo)
PS4_HLE_STUB(sceNetAddrConfig6Start)
PS4_HLE_STUB(sceNetAddrConfig6Stop)
PS4_HLE_STUB(sceNetAllocateAllRouteInfo)
PS4_HLE_STUB(sceNetBandwidthControlGetDataTraffic)
PS4_HLE_STUB(sceNetBandwidthControlGetDefaultParam)
PS4_HLE_STUB(sceNetBandwidthControlGetIfParam)
PS4_HLE_STUB(sceNetBandwidthControlGetPolicy)
PS4_HLE_STUB(sceNetBandwidthControlSetDefaultParam)
PS4_HLE_STUB(sceNetBandwidthControlSetIfParam)
PS4_HLE_STUB(sceNetBandwidthControlSetPolicy)
PS4_HLE_STUB(sceNetClearDnsCache)
PS4_HLE_STUB(sceNetConfigAddArp)
PS4_HLE_STUB(sceNetConfigAddArpWithInterface)
PS4_HLE_STUB(sceNetConfigAddIfaddr)
PS4_HLE_STUB(sceNetConfigAddMRoute)
PS4_HLE_STUB(sceNetConfigAddRoute)
PS4_HLE_STUB(sceNetConfigAddRoute6)
PS4_HLE_STUB(sceNetConfigAddRouteWithInterface)
PS4_HLE_STUB(sceNetConfigCleanUpAllInterfaces)
PS4_HLE_STUB(sceNetConfigDelArp)
PS4_HLE_STUB(sceNetConfigDelArpWithInterface)
PS4_HLE_STUB(sceNetConfigDelDefaultRoute)
PS4_HLE_STUB(sceNetConfigDelDefaultRoute6)
PS4_HLE_STUB(sceNetConfigDelIfaddr)
PS4_HLE_STUB(sceNetConfigDelIfaddr6)
PS4_HLE_STUB(sceNetConfigDelMRoute)
PS4_HLE_STUB(sceNetConfigDelRoute)
PS4_HLE_STUB(sceNetConfigDelRoute6)
PS4_HLE_STUB(sceNetConfigDownInterface)
PS4_HLE_STUB(sceNetConfigEtherGetLinkMode)
PS4_HLE_STUB(sceNetConfigEtherPostPlugInOutEvent)
PS4_HLE_STUB(sceNetConfigEtherSetLinkMode)
PS4_HLE_STUB(sceNetConfigFlushRoute)
PS4_HLE_STUB(sceNetConfigGetDefaultRoute)
PS4_HLE_STUB(sceNetConfigGetDefaultRoute6)
PS4_HLE_STUB(sceNetConfigGetIfaddr)
PS4_HLE_STUB(sceNetConfigGetIfaddr6)
PS4_HLE_STUB(sceNetConfigRoutingShowRoutingConfig)
PS4_HLE_STUB(sceNetConfigRoutingShowtCtlVar)
PS4_HLE_STUB(sceNetConfigRoutingStart)
PS4_HLE_STUB(sceNetConfigRoutingStop)
PS4_HLE_STUB(sceNetConfigSetDefaultRoute)
PS4_HLE_STUB(sceNetConfigSetDefaultRoute6)
PS4_HLE_STUB(sceNetConfigSetDefaultScope)
PS4_HLE_STUB(sceNetConfigSetIfaddr)
PS4_HLE_STUB(sceNetConfigSetIfaddr6)
PS4_HLE_STUB(sceNetConfigSetIfaddr6WithFlags)
PS4_HLE_STUB(sceNetConfigSetIfFlags)
PS4_HLE_STUB(sceNetConfigSetIfLinkLocalAddr6)
PS4_HLE_STUB(sceNetConfigSetIfmtu)
PS4_HLE_STUB(sceNetConfigUnsetIfFlags)
PS4_HLE_STUB(sceNetConfigUpInterface)
PS4_HLE_STUB(sceNetConfigUpInterfaceWithFlags)
PS4_HLE_STUB(sceNetConfigWlanAdhocClearWakeOnWlan)
PS4_HLE_STUB(sceNetConfigWlanAdhocCreate)
PS4_HLE_STUB(sceNetConfigWlanAdhocGetWakeOnWlanInfo)
PS4_HLE_STUB(sceNetConfigWlanAdhocJoin)
PS4_HLE_STUB(sceNetConfigWlanAdhocLeave)
PS4_HLE_STUB(sceNetConfigWlanAdhocPspEmuClearWakeOnWlan)
PS4_HLE_STUB(sceNetConfigWlanAdhocPspEmuGetWakeOnWlanInfo)
PS4_HLE_STUB(sceNetConfigWlanAdhocPspEmuSetWakeOnWlan)
PS4_HLE_STUB(sceNetConfigWlanAdhocScanJoin)
PS4_HLE_STUB(sceNetConfigWlanAdhocSetExtInfoElement)
PS4_HLE_STUB(sceNetConfigWlanAdhocSetWakeOnWlan)
PS4_HLE_STUB(sceNetConfigWlanApStart)
PS4_HLE_STUB(sceNetConfigWlanApStop)
PS4_HLE_STUB(sceNetConfigWlanBackgroundScanQuery)
PS4_HLE_STUB(sceNetConfigWlanBackgroundScanStart)
PS4_HLE_STUB(sceNetConfigWlanBackgroundScanStop)
PS4_HLE_STUB(sceNetConfigWlanDiagGetDeviceInfo)
PS4_HLE_STUB(sceNetConfigWlanDiagSetAntenna)
PS4_HLE_STUB(sceNetConfigWlanDiagSetTxFixedRate)
PS4_HLE_STUB(sceNetConfigWlanGetDeviceConfig)
PS4_HLE_STUB(sceNetConfigWlanInfraGetRssiInfo)
PS4_HLE_STUB(sceNetConfigWlanInfraLeave)
PS4_HLE_STUB(sceNetConfigWlanInfraScanJoin)
PS4_HLE_STUB(sceNetConfigWlanScan)
PS4_HLE_STUB(sceNetConfigWlanSetDeviceConfig)
PS4_HLE_STUB(sceNetControl)
PS4_HLE_STUB(sceNetDhcpdStart)
PS4_HLE_STUB(sceNetDhcpdStop)
PS4_HLE_STUB(sceNetDhcpGetAutoipInfo)
PS4_HLE_STUB(sceNetDhcpGetInfo)
PS4_HLE_STUB(sceNetDhcpGetInfoEx)
PS4_HLE_STUB(sceNetDhcpStart)
PS4_HLE_STUB(sceNetDhcpStop)
PS4_HLE_STUB(sceNetDumpAbort)
PS4_HLE_STUB(sceNetDumpCreate)
PS4_HLE_STUB(sceNetDumpDestroy)
PS4_HLE_STUB(sceNetDumpRead)
PS4_HLE_STUB(sceNetDuplicateIpStart)
PS4_HLE_STUB(sceNetDuplicateIpStop)
PS4_HLE_STUB(sceNetEventCallbackCreate)
PS4_HLE_STUB(sceNetEventCallbackDestroy)
PS4_HLE_STUB(sceNetEventCallbackGetError)
PS4_HLE_STUB(sceNetEventCallbackWaitCB)
PS4_HLE_STUB(sceNetFreeAllRouteInfo)
PS4_HLE_STUB(sceNetGetArpInfo)
PS4_HLE_STUB(sceNetGetIfList)
PS4_HLE_STUB(sceNetGetIfListOnce)
PS4_HLE_STUB(sceNetGetIfnameNumList)
PS4_HLE_STUB(sceNetGetNameToIndex)
PS4_HLE_STUB(sceNetGetRouteInfo)
PS4_HLE_STUB(sceNetGetSockInfo6)
PS4_HLE_STUB(sceNetGetStatisticsInfoInternal)
PS4_HLE_STUB(sceNetInetNtopWithScopeId)
PS4_HLE_STUB(sceNetInetPtonWithScopeId)
PS4_HLE_STUB(sceNetInfoDumpStart)
PS4_HLE_STUB(sceNetInfoDumpStop)
PS4_HLE_STUB(sceNetInitParam)
PS4_HLE_STUB(sceNetPppoeStart)
PS4_HLE_STUB(sceNetPppoeStop)
PS4_HLE_STUB(sceNetSetDns6InfoToKernel)
PS4_HLE_STUB(sceNetSetDnsInfoToKernel)
PS4_HLE_STUB(sceNetShowIfconfig)
PS4_HLE_STUB(sceNetShowIfconfigForBuffer)
PS4_HLE_STUB(sceNetShowNetstat)
PS4_HLE_STUB(sceNetShowNetstatEx)
PS4_HLE_STUB(sceNetShowNetstatExForBuffer)
PS4_HLE_STUB(sceNetShowNetstatForBuffer)
PS4_HLE_STUB(sceNetShowPolicy)
PS4_HLE_STUB(sceNetShowRoute)
PS4_HLE_STUB(sceNetShowRoute6)
PS4_HLE_STUB(sceNetShowRoute6ForBuffer)
PS4_HLE_STUB(sceNetShowRouteForBuffer)
PS4_HLE_STUB(sceNetSyncCreate)
PS4_HLE_STUB(sceNetSyncDestroy)
PS4_HLE_STUB(sceNetSyncGet)
PS4_HLE_STUB(sceNetSyncSignal)
PS4_HLE_STUB(sceNetSyncWait)
PS4_HLE_STUB(sceNetSysctl)
PS4_HLE_STUB(Func_0E707A589F751C68)

// ---------------------------------------------------------------------------------------------
// libkernel / libScePosix socket calls (shadPS4 net_kernel.cpp sys_*): POSIX results with the
// net errno left as it was.

#define SYS_CALL(expr)                                          \
    do {                                                        \
        int32_t *slot_ = net_errno_slot(ctx);                   \
        const int32_t saved_ = slot_ ? *slot_ : 0;              \
        const int64_t r_ = posix_result(ctx, (expr));           \
        if (slot_) *slot_ = saved_;                             \
        ctx->rax = (uint64_t)r_;                                \
        SHIM_RETURN();                                          \
    } while (0)

void shim_socket(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetSocket(ctx, NULL, (int32_t)A0, (int32_t)A1, (int32_t)A2));
}

void shim_socketex(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetSocket(ctx, (const char *)G(ctx, A0), (int32_t)A1, (int32_t)A2, (int32_t)A3));
}

void shim_connect(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetConnect(ctx, (int32_t)A0, (const OrbisNetSockaddr *)G(ctx, A1), (uint32_t)A2));
}

void shim_bind(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetBind(ctx, (int32_t)A0, (const OrbisNetSockaddr *)G(ctx, A1), (uint32_t)A2));
}

void shim_listen(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetListen(ctx, (int32_t)A0, (int32_t)A1));
}

void shim_accept(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetAccept(ctx, (int32_t)A0, (OrbisNetSockaddr *)G(ctx, A1), (uint32_t *)G(ctx, A2)));
}

void shim_send(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetSendto(ctx, (int32_t)A0, G(ctx, A1), A2, (int32_t)A3, NULL, 0));
}

void shim_sendto(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetSendto(ctx, (int32_t)A0, G(ctx, A1), A2, (int32_t)A3, (const OrbisNetSockaddr *)G(ctx, A4),
                              (uint32_t)A5));
}

void shim_sendmsg(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetSendmsg(ctx, (int32_t)A0, (const OrbisNetMsghdr *)G(ctx, A1), (int32_t)A2));
}

void shim_recv(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetRecvfrom(ctx, (int32_t)A0, G(ctx, A1), A2, (int32_t)A3, NULL, NULL));
}

void shim_recvfrom(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetRecvfrom(ctx, (int32_t)A0, G(ctx, A1), A2, (int32_t)A3, (OrbisNetSockaddr *)G(ctx, A4),
                                (uint32_t *)G(ctx, A5)));
}

void shim_recvmsg(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetRecvmsg(ctx, (int32_t)A0, (OrbisNetMsghdr *)G(ctx, A1), (int32_t)A2));
}

void shim_setsockopt(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetSetsockopt(ctx, (int32_t)A0, (int32_t)A1, (int32_t)A2, G(ctx, A3), (uint32_t)A4));
}

void shim_getsockopt(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetGetsockopt(ctx, (int32_t)A0, (int32_t)A1, (int32_t)A2, G(ctx, A3), (uint32_t *)G(ctx, A4)));
}

void shim_getsockname(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetGetsockname(ctx, (int32_t)A0, (OrbisNetSockaddr *)G(ctx, A1), (uint32_t *)G(ctx, A2)));
}

void shim_getpeername(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetGetpeername(ctx, (int32_t)A0, (OrbisNetSockaddr *)G(ctx, A1), (uint32_t *)G(ctx, A2)));
}

void shim_shutdown(GuestContext *ctx) {
    SYS_CALL(ps4_sceNetShutdown(ctx, (int32_t)A0, (int32_t)A1));
}

void shim_socketpair(GuestContext *ctx) {
    int32_t *sv = (int32_t *)G(ctx, A3);
    if (!sv) {
        ctx->rax = (uint64_t)posix_error(ctx, ORBIS_NET_EFAULT);
        SHIM_RETURN();
    }
    if ((int32_t)A0 != ORBIS_NET_AF_UNIX) {
        ctx->rax = (uint64_t)posix_error(ctx, ORBIS_NET_EAFNOSUPPORT); // AF_UNIX only, as FreeBSD
        SHIM_RETURN();
    }
    Ps4NetHostSocketKind kind;
    const int e = ps4_net_to_host_socket_kind((int32_t)A0, (int32_t)A1, (int32_t)A2, &kind);
    if (e != 0) {
        ctx->rax = (uint64_t)posix_error(ctx, e);
        SHIM_RETURN();
    }
    int32_t ids[2];
    const int64_t r = ps4_net_socket_create_pair(kind.family, kind.type, kind.protocol, ids);
    if (r < 0) {
        ctx->rax = (uint64_t)posix_error(ctx, (int)-r);
        SHIM_RETURN();
    }
    sv[0] = ids[0];
    sv[1] = ids[1];
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_htons(GuestContext *ctx) {
    shim_sceNetHtons(ctx);
}

void shim_htonl(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)(int32_t)__builtin_bswap32((uint32_t)A0);
    SHIM_RETURN();
}

// shadPS4 registers sceNetInetPton / sceNetInetNtop for libScePosix inet_pton / inet_ntop.
void shim_inet_pton(GuestContext *ctx) {
    shim_sceNetInetPton(ctx);
}

void shim_inet_ntop(GuestContext *ctx) {
    shim_sceNetInetNtop(ctx);
}
