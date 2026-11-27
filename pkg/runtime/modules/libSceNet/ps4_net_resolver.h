// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNet resolvers, after shadPS4 (libraries/net/net_resolver.h). A resolver's id is an
// external object (ps4_net_core.h), so a guest can wait for an asynchronous lookup in an epoll.
//
// Functions returning int32_t give 0 or an ORBIS_NET_ERROR_* code; those returning int give 0 or
// a guest errno.

#ifndef PS4_NET_RESOLVER_H
#define PS4_NET_RESOLVER_H

#include "ps4_net_types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The new id, or -errno.
int32_t ps4_net_resolver_create(const char *name);
int ps4_net_resolver_destroy(int32_t id);
int ps4_net_resolver_abort(int32_t id, uint32_t flags);

// Exactly one of addr and info is set. Both point into guest memory, written when the lookup
// finishes (on a worker thread for an asynchronous one).
int32_t ps4_net_resolver_start_ntoa(int32_t id, const char *hostname, OrbisNetInAddr *addr, OrbisNetResolverInfo *info,
                                    bool async, bool disable_ipaddress);
int32_t ps4_net_resolver_start_aton(int32_t id, const OrbisNetInAddr *addr, char *hostname, int32_t len, bool async);
int ps4_net_resolver_get_error(int32_t id, int32_t *status);

// Whether lookups may use DNS (shadPS4's is_online hook: the network connection setting).
void ps4_net_resolver_set_online_check(bool (*is_online)(void));

#ifdef __cplusplus
}
#endif

#endif // PS4_NET_RESOLVER_H
