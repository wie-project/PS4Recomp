// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNet resolvers: shadPS4 libraries/net/net_resolver.cpp. IPv4 only, at most 10 records,
// through the host's getaddrinfo/getnameinfo.

#include "ps4_net_resolver.h"
#include "ps4_net_core.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

// Each kind has its own preserved abort.
enum { LOOKUP_NTOA = 0, LOOKUP_ATON = 1 };

typedef struct Resolver Resolver;
struct Resolver {
    int32_t id;
    atomic_int refs;
    char name[ORBIS_NET_DEBUG_NAME_LEN_MAX + 1];

    pthread_mutex_t mutex; // guards everything below
    bool running;
    bool destroyed;
    uint64_t lookup; // bumped to orphan a running asynchronous lookup
    int32_t status;
    bool pending_abort[2]; // indexed by lookup kind
    Resolver *next;
};

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static Resolver *g_resolvers;
static bool (*g_is_online)(void);

static void resolver_release(Resolver *r) {
    if (atomic_fetch_sub_explicit(&r->refs, 1, memory_order_acq_rel) == 1) {
        pthread_mutex_destroy(&r->mutex);
        free(r);
    }
}

static Resolver *find(int32_t id) {
    pthread_mutex_lock(&g_mutex);
    Resolver *r = g_resolvers;
    while (r && r->id != id) r = r->next;
    if (r) atomic_fetch_add_explicit(&r->refs, 1, memory_order_relaxed);
    pthread_mutex_unlock(&g_mutex);
    return r;
}

static bool is_online(void) {
    pthread_mutex_lock(&g_mutex);
    bool (*check)(void) = g_is_online;
    pthread_mutex_unlock(&g_mutex);
    return !check || check();
}

void ps4_net_resolver_set_online_check(bool (*check)(void)) {
    pthread_mutex_lock(&g_mutex);
    g_is_online = check;
    pthread_mutex_unlock(&g_mutex);
}

int32_t ps4_net_resolver_create(const char *name) {
    const int64_t r = ps4_net_reserve_external_id();
    if (r < 0) return (int32_t)r;
    Resolver *resolver = (Resolver *)calloc(1, sizeof(Resolver));
    if (!resolver) {
        ps4_net_release_external_id((int32_t)r);
        return -ORBIS_NET_ENOMEM;
    }
    resolver->id = (int32_t)r;
    atomic_init(&resolver->refs, 1);
    if (name) strncpy(resolver->name, name, sizeof(resolver->name) - 1);
    pthread_mutex_init(&resolver->mutex, NULL);
    pthread_mutex_lock(&g_mutex);
    resolver->next = g_resolvers;
    g_resolvers = resolver;
    pthread_mutex_unlock(&g_mutex);
    return resolver->id;
}

int ps4_net_resolver_destroy(int32_t id) {
    pthread_mutex_lock(&g_mutex);
    Resolver **p = &g_resolvers;
    while (*p && (*p)->id != id) p = &(*p)->next;
    Resolver *resolver = *p;
    if (resolver) *p = resolver->next;
    pthread_mutex_unlock(&g_mutex);
    if (!resolver) return ORBIS_NET_EBADF;
    pthread_mutex_lock(&resolver->mutex);
    resolver->destroyed = true; // keeps a running lookup from writing guest memory
    pthread_mutex_unlock(&resolver->mutex);
    ps4_net_release_external_id(id);
    resolver_release(resolver);
    return 0;
}

int ps4_net_resolver_abort(int32_t id, uint32_t flags) {
    Resolver *r = find(id);
    if (!r) return ORBIS_NET_EBADF;
    pthread_mutex_lock(&r->mutex);
    if (r->running) {
        r->lookup++; // orphan the running thread
        r->running = false;
        r->status = ORBIS_NET_ERROR(ORBIS_NET_EINTR);
        ps4_net_signal_external(id, true);
    } else {
        if (flags & ORBIS_NET_RESOLVER_ABORT_FLAG_NTOA_PRESERVATION) r->pending_abort[LOOKUP_NTOA] = true;
        if (flags & ORBIS_NET_RESOLVER_ABORT_FLAG_ATON_PRESERVATION) r->pending_abort[LOOKUP_ATON] = true;
    }
    pthread_mutex_unlock(&r->mutex);
    resolver_release(r);
    return 0;
}

// A lookup's work and its result.
typedef struct Lookup {
    Resolver *resolver;
    int kind;
    uint64_t lookup;
    // Ntoa
    char hostname[ORBIS_NET_RESOLVER_HOSTNAME_LEN_MAX + 1];
    bool allow_literal;
    OrbisNetInAddr *out_addr;
    OrbisNetResolverInfo *out_info;
    uint32_t addrs[10];
    int addr_count;
    // Aton
    uint32_t address;
    char *out_hostname;
    int32_t out_len;
    char name[1025]; // NI_MAXHOST
} Lookup;

static int32_t forward_lookup(Lookup *l) {
    char host[ORBIS_NET_RESOLVER_HOSTNAME_LEN_MAX + 1];
    strcpy(host, l->hostname);
    const size_t n = strlen(host);
    if (n > 0 && host[n - 1] == '.') host[n - 1] = '\0';
    if (strncmp(host, "localhost", 9) == 0) {
        l->addrs[l->addr_count++] = htonl(INADDR_LOOPBACK);
        return 0;
    }
    if (l->allow_literal) {
        struct in_addr literal;
        if (inet_pton(AF_INET, host, &literal) == 1) {
            l->addrs[l->addr_count++] = literal.s_addr;
            return 0;
        }
    }
    if (!is_online()) return ORBIS_NET_ERROR_RESOLVER_ENODNS;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM; // one entry per address
    struct addrinfo *result = NULL;
    const int gai = getaddrinfo(host, NULL, &hints, &result);
    if (gai != 0) {
        fprintf(stderr, "[ps4-net] lookup of %s failed: %s\n", host, gai_strerror(gai));
        switch (gai) {
        case EAI_NONAME:
#if defined(EAI_NODATA) && EAI_NODATA != EAI_NONAME
        case EAI_NODATA:
#endif
            return ORBIS_NET_ERROR_RESOLVER_ENOHOST;
        case EAI_AGAIN:
            return ORBIS_NET_ERROR_RESOLVER_ETIMEDOUT;
        case EAI_FAIL:
            return ORBIS_NET_ERROR_RESOLVER_ESERVERFAILURE;
        default:
            return ORBIS_NET_ERROR_RESOLVER_EINTERNAL;
        }
    }
    for (const struct addrinfo *ai = result; ai && l->addr_count < 10; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET || !ai->ai_addr) continue;
        const uint32_t addr = ((const struct sockaddr_in *)ai->ai_addr)->sin_addr.s_addr;
        bool seen = false;
        for (int i = 0; i < l->addr_count; i++) seen = seen || l->addrs[i] == addr;
        if (!seen) l->addrs[l->addr_count++] = addr;
    }
    freeaddrinfo(result);
    return l->addr_count == 0 ? ORBIS_NET_ERROR_RESOLVER_ENORECORD : 0;
}

static int32_t reverse_lookup(Lookup *l) {
    if (l->address == htonl(INADDR_LOOPBACK)) {
        strcpy(l->name, "localhost");
        return 0;
    }
    if (!is_online()) return ORBIS_NET_ERROR_RESOLVER_ENODNS;
    struct sockaddr_in in;
    memset(&in, 0, sizeof(in));
    in.sin_len = sizeof(in);
    in.sin_family = AF_INET;
    in.sin_addr.s_addr = l->address;
    switch (getnameinfo((const struct sockaddr *)&in, sizeof(in), l->name, sizeof(l->name), NULL, 0, NI_NAMEREQD)) {
    case 0:
        return 0;
    case EAI_NONAME:
        return ORBIS_NET_ERROR_RESOLVER_ENOHOST;
    case EAI_AGAIN:
        return ORBIS_NET_ERROR_RESOLVER_ETIMEDOUT;
    case EAI_FAIL:
        return ORBIS_NET_ERROR_RESOLVER_ESERVERFAILURE;
    default:
        return ORBIS_NET_ERROR_RESOLVER_EINTERNAL;
    }
}

static int32_t work(Lookup *l) {
    return l->kind == LOOKUP_NTOA ? forward_lookup(l) : reverse_lookup(l);
}

// Writes the result to guest memory; called with the resolver's mutex held.
static int32_t deliver(Lookup *l) {
    if (l->kind == LOOKUP_ATON) {
        const size_t size = strlen(l->name) + 1;
        if (size > (size_t)l->out_len) return ORBIS_NET_ERROR_RESOLVER_ENOSPACE;
        memcpy(l->out_hostname, l->name, size);
        return 0;
    }
    if (l->out_addr) l->out_addr->inaddr_addr = l->addrs[0];
    if (l->out_info) {
        memset(l->out_info, 0, sizeof(*l->out_info));
        for (int i = 0; i < l->addr_count; i++) {
            l->out_info->addrs[i].u.addr.inaddr_addr = l->addrs[i];
            l->out_info->addrs[i].af = ORBIS_NET_AF_INET;
        }
        l->out_info->records = (uint32_t)l->addr_count;
        l->out_info->recordsv4 = (uint32_t)l->addr_count;
    }
    return 0;
}

static void *lookup_thread(void *arg) {
    Lookup *l = (Lookup *)arg;
    Resolver *r = l->resolver;
    int32_t status = work(l);
    pthread_mutex_lock(&r->mutex);
    // Stale when destroyed or aborted since: the id may have been reused.
    if (!r->destroyed && r->lookup == l->lookup) {
        if (status == 0) status = deliver(l);
        r->status = status;
        r->running = false;
        ps4_net_signal_external(r->id, false);
    }
    pthread_mutex_unlock(&r->mutex);
    resolver_release(r);
    free(l);
    return NULL;
}

// Takes ownership of l.
static int32_t run_lookup(Resolver *r, Lookup *l, bool async) {
    pthread_mutex_lock(&r->mutex);
    if (r->running) {
        pthread_mutex_unlock(&r->mutex);
        free(l);
        return ORBIS_NET_ERROR_RESOLVER_EBUSY;
    }
    if (r->pending_abort[l->kind]) {
        r->pending_abort[l->kind] = false;
        r->status = ORBIS_NET_ERROR(ORBIS_NET_EINTR);
        pthread_mutex_unlock(&r->mutex);
        free(l);
        return ORBIS_NET_ERROR(ORBIS_NET_EINTR);
    }
    if (async) {
        r->running = true;
        r->status = 0;
        l->lookup = ++r->lookup;
    }
    pthread_mutex_unlock(&r->mutex);
    if (!async) {
        int32_t status = work(l);
        pthread_mutex_lock(&r->mutex);
        if (status == 0) status = deliver(l);
        r->status = status;
        pthread_mutex_unlock(&r->mutex);
        free(l);
        return status;
    }
    l->resolver = r;
    atomic_fetch_add_explicit(&r->refs, 1, memory_order_relaxed);
    pthread_t thread;
    if (pthread_create(&thread, NULL, lookup_thread, l) != 0) {
        pthread_mutex_lock(&r->mutex);
        r->running = false;
        r->status = ORBIS_NET_ERROR_RESOLVER_EINTERNAL;
        pthread_mutex_unlock(&r->mutex);
        resolver_release(r);
        free(l);
        return ORBIS_NET_ERROR_RESOLVER_EINTERNAL;
    }
    pthread_detach(thread);
    return 0;
}

int32_t ps4_net_resolver_start_ntoa(int32_t id, const char *hostname, OrbisNetInAddr *addr, OrbisNetResolverInfo *info,
                                    bool async, bool disable_ipaddress) {
    Resolver *r = find(id);
    if (!r) return ORBIS_NET_ERROR(ORBIS_NET_EBADF);
    int32_t result = ORBIS_NET_ERROR(ORBIS_NET_EINVAL);
    const size_t length = hostname ? strnlen(hostname, ORBIS_NET_RESOLVER_HOSTNAME_LEN_MAX + 1) : 0;
    if (hostname && (addr || info) && length > 0 && length <= ORBIS_NET_RESOLVER_HOSTNAME_LEN_MAX) {
        Lookup *l = (Lookup *)calloc(1, sizeof(Lookup));
        if (!l) {
            result = ORBIS_NET_ERROR(ORBIS_NET_ENOMEM);
        } else {
            l->kind = LOOKUP_NTOA;
            memcpy(l->hostname, hostname, length);
            l->allow_literal = !disable_ipaddress;
            l->out_addr = addr;
            l->out_info = info;
            result = run_lookup(r, l, async);
        }
    }
    resolver_release(r);
    return result;
}

int32_t ps4_net_resolver_start_aton(int32_t id, const OrbisNetInAddr *addr, char *hostname, int32_t len, bool async) {
    Resolver *r = find(id);
    if (!r) return ORBIS_NET_ERROR(ORBIS_NET_EBADF);
    int32_t result = ORBIS_NET_ERROR(ORBIS_NET_EINVAL);
    if (addr && hostname && len > 0) {
        Lookup *l = (Lookup *)calloc(1, sizeof(Lookup));
        if (!l) {
            result = ORBIS_NET_ERROR(ORBIS_NET_ENOMEM);
        } else {
            l->kind = LOOKUP_ATON;
            l->address = addr->inaddr_addr;
            l->out_hostname = hostname;
            l->out_len = len;
            result = run_lookup(r, l, async);
        }
    }
    resolver_release(r);
    return result;
}

int ps4_net_resolver_get_error(int32_t id, int32_t *status) {
    Resolver *r = find(id);
    if (!r) return ORBIS_NET_EBADF;
    pthread_mutex_lock(&r->mutex);
    *status = r->status;
    pthread_mutex_unlock(&r->mutex);
    resolver_release(r);
    return 0;
}
