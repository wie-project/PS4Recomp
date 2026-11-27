// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNetCtl after shadPS4 (network/netctl.cpp, net_ctl_obj.cpp, net/net_util.cpp).
//
// The console's connection state is shadPS4's "isConnectedToNetwork" setting, off by default:
// PS4_NETWORK=1 reports the host connected. Disconnected, sceNetCtlGetState answers DISCONNECTED
// and sceNetCtlGetInfo NOT_CONNECTED; connected, the state is IPOBTAINED and the information
// comes from the host (address of the interface that routes to the Internet, its netmask and
// hardware address, the default gateway). sceNetCtlCheckCallback reports the state to each
// registered callback once (and again when it changes), as shadPS4 does.
//
// There is no shadNet signaling server: no external address is known, so sceNetCtlGetNatInfo
// answers STUN status 0, NAT type 0 and the local address, as shadPS4 does before the server
// reported one. The functions shadPS4 leaves as stubs are stubs here too (end of file).

#include "ps4_netctl.h"
#include "ps4_hle_stub.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct NetCtlCallbackSlot {
    uint64_t func; // guest address, 0 for a free slot
    uint64_t arg;
    int32_t last_event;
} NetCtlCallbackSlot;

static pthread_mutex_t g_netctl_mutex = PTHREAD_MUTEX_INITIALIZER;
static NetCtlCallbackSlot g_callbacks[ORBIS_NET_CTL_CALLBACK_SLOTS] = {
    [0 ... ORBIS_NET_CTL_CALLBACK_SLOTS - 1] = {0, 0, -1}};
static NetCtlCallbackSlot g_toolkit_callbacks[ORBIS_NET_CTL_CALLBACK_SLOTS] = {
    [0 ... ORBIS_NET_CTL_CALLBACK_SLOTS - 1] = {0, 0, -1}};

static pthread_once_t g_connected_once = PTHREAD_ONCE_INIT;
static bool g_connected;

static void read_connected(void) {
    const char *v = getenv("PS4_NETWORK");
    g_connected = v && strcmp(v, "1") == 0;
}

bool ps4_netctl_connected(void) {
    pthread_once(&g_connected_once, read_connected);
    return g_connected;
}

// ---------------------------------------------------------------------------------------------
// Host network information (shadPS4 NetUtilInternal)

// The local address the host uses to reach the Internet (shadPS4 RetrieveIp connects a TCP
// socket to 1.1.1.1:80; a UDP socket picks the same route without sending anything).
static bool local_ip(struct in_addr *out) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(80);
    sa.sin_addr.s_addr = inet_addr("1.1.1.1");
    socklen_t len = sizeof(sa);
    const bool ok = connect(fd, (const struct sockaddr *)&sa, sizeof(sa)) == 0 &&
                    getsockname(fd, (struct sockaddr *)&sa, &len) == 0 && sa.sin_addr.s_addr != 0;
    close(fd);
    if (ok) *out = sa.sin_addr;
    return ok;
}

// The netmask and hardware address of the interface that holds `ip`.
static bool interface_of(struct in_addr ip, struct in_addr *netmask, uint8_t mac[ORBIS_NET_ETHER_ADDR_LEN]) {
    struct ifaddrs *ifap = NULL;
    if (getifaddrs(&ifap) != 0) return false;
    const char *name = NULL;
    for (struct ifaddrs *p = ifap; p; p = p->ifa_next) {
        if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET &&
            ((const struct sockaddr_in *)p->ifa_addr)->sin_addr.s_addr == ip.s_addr) {
            name = p->ifa_name;
            if (netmask && p->ifa_netmask) *netmask = ((const struct sockaddr_in *)p->ifa_netmask)->sin_addr;
            break;
        }
    }
    bool found = name != NULL;
    if (found && mac) {
        found = false;
        for (struct ifaddrs *p = ifap; p; p = p->ifa_next) {
            if (p->ifa_addr && p->ifa_addr->sa_family == AF_LINK && strcmp(p->ifa_name, name) == 0) {
                const struct sockaddr_dl *sdl = (const struct sockaddr_dl *)p->ifa_addr;
                if (sdl->sdl_alen == ORBIS_NET_ETHER_ADDR_LEN) {
                    memcpy(mac, LLADDR(sdl), ORBIS_NET_ETHER_ADDR_LEN);
                    found = true;
                }
                break;
            }
        }
    }
    freeifaddrs(ifap);
    return found;
}

// The IPv4 default gateway, asked from the routing socket (shadPS4 RetrieveDefaultGateway on
// macOS: RTM_GET for 0.0.0.0/0, the RTA_GATEWAY address of the answer).
static bool default_gateway(struct in_addr *out) {
    const int fd = socket(PF_ROUTE, SOCK_RAW, AF_INET);
    if (fd < 0) return false;
    const struct timeval timeout = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct {
        struct rt_msghdr h;
        char space[512];
    } m;
    memset(&m, 0, sizeof(m));
    struct sockaddr_in *dst = (struct sockaddr_in *)(void *)m.space;
    dst[0].sin_len = dst[1].sin_len = sizeof(struct sockaddr_in);
    dst[0].sin_family = dst[1].sin_family = AF_INET; // destination and netmask 0.0.0.0
    static int seq;
    const int my_seq = __atomic_add_fetch(&seq, 1, __ATOMIC_RELAXED);
    const pid_t pid = getpid();
    m.h.rtm_msglen = sizeof(m.h) + 2 * sizeof(struct sockaddr_in);
    m.h.rtm_version = RTM_VERSION;
    m.h.rtm_type = RTM_GET;
    m.h.rtm_addrs = RTA_DST | RTA_NETMASK;
    m.h.rtm_flags = RTF_UP | RTF_GATEWAY;
    m.h.rtm_pid = pid;
    m.h.rtm_seq = my_seq;
    if (write(fd, &m, m.h.rtm_msglen) < 0) {
        close(fd);
        return false;
    }
    ssize_t n;
    do {
        n = read(fd, &m, sizeof(m));
    } while (n > 0 && (m.h.rtm_pid != pid || m.h.rtm_seq != my_seq));
    close(fd);
    if (n < (ssize_t)sizeof(m.h) || m.h.rtm_errno != 0) return false;

    const char *p = m.space;
    const char *end = (const char *)&m + n;
    for (int bit = 0; bit < RTAX_MAX && p < end; bit++) {
        if (!(m.h.rtm_addrs & (1 << bit))) continue;
        const struct sockaddr *sa = (const struct sockaddr *)(const void *)p;
        if (bit == RTAX_GATEWAY) {
            if (sa->sa_family != AF_INET || p + sizeof(struct sockaddr_in) > end) return false;
            *out = ((const struct sockaddr_in *)(const void *)sa)->sin_addr;
            return true;
        }
        // sockaddrs in a routing message are padded to 4 bytes
        p += sa->sa_len ? (size_t)((sa->sa_len + 3) & ~3) : sizeof(uint32_t);
    }
    return false;
}

static void put_addr(char dst[ORBIS_NET_CTL_IPV4_ADDR_STR_LEN], struct in_addr a) {
    inet_ntop(AF_INET, &a, dst, ORBIS_NET_CTL_IPV4_ADDR_STR_LEN);
}

// ---------------------------------------------------------------------------------------------
// Library logic

int32_t sceNetCtlGetState(int32_t *state) {
    // shadPS4 writes without a check; a null pointer is answered as the other queries do.
    if (!state) return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_ADDR;
    *state = ps4_netctl_connected() ? ORBIS_NET_CTL_STATE_IPOBTAINED : ORBIS_NET_CTL_STATE_DISCONNECTED;
    return ORBIS_OK;
}

// The error code of the last connection event of the type: never a failed one.
int32_t sceNetCtlGetResult(int32_t eventType, int32_t *errorCode) {
    (void)eventType;
    if (!errorCode) return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_ADDR;
    *errorCode = 0;
    return ORBIS_OK;
}

bool ps4_netctl_host_ether_addr(uint8_t mac[6]) {
    struct in_addr ip;
    return local_ip(&ip) && interface_of(ip, NULL, mac);
}

int32_t sceNetCtlGetInfo(int32_t code, OrbisNetCtlInfo *info) {
    if (!ps4_netctl_connected()) return (int32_t)ORBIS_NET_CTL_ERROR_NOT_CONNECTED;
    // shadPS4 writes without a check
    if (!info) return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_ADDR;
    struct in_addr ip, a;
    switch (code) {
    case ORBIS_NET_CTL_INFO_DEVICE:
        info->device = ORBIS_NET_CTL_DEVICE_WIRED;
        break;
    case ORBIS_NET_CTL_INFO_ETHER_ADDR: {
        uint8_t mac[ORBIS_NET_ETHER_ADDR_LEN];
        if (ps4_netctl_host_ether_addr(mac)) {
            memcpy(info->ether_addr.data, mac, ORBIS_NET_ETHER_ADDR_LEN);
        } else {
            fprintf(stderr, "[ps4-netctl] hardware address: failed to retrieve\n");
        }
        break;
    }
    case ORBIS_NET_CTL_INFO_MTU:
        info->mtu = 1500;
        break;
    case ORBIS_NET_CTL_INFO_LINK:
        info->link = ORBIS_NET_CTL_LINK_CONNECTED;
        break;
    case ORBIS_NET_CTL_INFO_IP_ADDRESS:
        strcpy(info->ip_address, "127.0.0.1"); // when the address cannot be found
        if (local_ip(&ip)) {
            put_addr(info->ip_address, ip);
        } else {
            fprintf(stderr, "[ps4-netctl] local ip: failed to retrieve\n");
        }
        break;
    case ORBIS_NET_CTL_INFO_NETMASK:
        if (local_ip(&ip) && interface_of(ip, &a, NULL)) {
            put_addr(info->netmask, a);
        } else {
            fprintf(stderr, "[ps4-netctl] netmask: failed to retrieve\n");
        }
        break;
    case ORBIS_NET_CTL_INFO_DEFAULT_ROUTE:
        if (default_gateway(&a)) {
            put_addr(info->default_route, a);
        } else {
            fprintf(stderr, "[ps4-netctl] default gateway: failed to retrieve\n");
        }
        break;
    case ORBIS_NET_CTL_INFO_HTTP_PROXY_CONFIG:
        info->http_proxy_config = 0; // off
        break;
    case ORBIS_NET_CTL_INFO_PRIMARY_DNS:
        strcpy(info->primary_dns, "1.1.1.1");
        break;
    case ORBIS_NET_CTL_INFO_SECONDARY_DNS:
        strcpy(info->secondary_dns, "1.1.1.1");
        break;
    case ORBIS_NET_CTL_INFO_HTTP_PROXY_SERVER:
        info->http_proxy_server[0] = '\0';
        break;
    case ORBIS_NET_CTL_INFO_HTTP_PROXY_PORT:
        info->http_proxy_port = 0;
        break;
    case ORBIS_NET_CTL_INFO_IP_CONFIG:
        info->ip_config = 1; // static
        break;
    case ORBIS_NET_CTL_INFO_DHCP_HOSTNAME:
        break; // none, nothing written
    default:
        fprintf(stderr, "[ps4-netctl] sceNetCtlGetInfo: %d unsupported code\n", code);
        break;
    }
    return ORBIS_OK;
}

int32_t sceNetCtlGetNatInfo(OrbisNetCtlNatInfo *nat_info) {
    if (!nat_info) return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_ADDR;
    if (nat_info->size != sizeof(OrbisNetCtlNatInfo)) return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_SIZE;
    // No signaling server reported an external address: STUN failed, the local address.
    nat_info->nat_type = 0;
    nat_info->stun_status = 0;
    struct in_addr ip;
    nat_info->mapped_addr = local_ip(&ip) ? ip.s_addr : inet_addr("127.0.0.1");
    return ORBIS_OK;
}

static int32_t register_callback(NetCtlCallbackSlot *slots, uint64_t func, uint64_t arg, int32_t *cid) {
    if (!func || !cid) return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_ADDR;
    pthread_mutex_lock(&g_netctl_mutex);
    for (int32_t i = 0; i < ORBIS_NET_CTL_CALLBACK_SLOTS; i++) {
        if (slots[i].func == 0) {
            slots[i] = (NetCtlCallbackSlot){func, arg, -1};
            pthread_mutex_unlock(&g_netctl_mutex);
            *cid = i;
            return ORBIS_OK;
        }
    }
    pthread_mutex_unlock(&g_netctl_mutex);
    return (int32_t)ORBIS_NET_CTL_ERROR_CALLBACK_MAX;
}

static int32_t unregister_callback(NetCtlCallbackSlot *slots, int32_t cid) {
    if (cid < 0 || cid >= ORBIS_NET_CTL_CALLBACK_SLOTS) return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_ID;
    pthread_mutex_lock(&g_netctl_mutex);
    int32_t rc = ORBIS_OK;
    if (slots[cid].func == 0) {
        rc = (int32_t)ORBIS_NET_CTL_ERROR_ID_NOT_FOUND;
    } else {
        slots[cid] = (NetCtlCallbackSlot){0, 0, -1};
    }
    pthread_mutex_unlock(&g_netctl_mutex);
    return rc;
}

int32_t sceNetCtlRegisterCallback(uint64_t func, uint64_t arg, int32_t *cid) {
    return register_callback(g_callbacks, func, arg, cid);
}

int32_t sceNetCtlUnregisterCallback(int32_t cid) {
    return unregister_callback(g_callbacks, cid);
}

int32_t sceNetCtlRegisterCallbackForNpToolkit(uint64_t func, uint64_t arg, int32_t *cid) {
    return register_callback(g_toolkit_callbacks, func, arg, cid);
}

int32_t sceNetCtlUnregisterCallbackForNpToolkit(int32_t cid) {
    return unregister_callback(g_toolkit_callbacks, cid);
}

// Calls `callback(event, arg)` for every registered callback that has not seen the current
// event yet (shadPS4 NetCtlInternal::CheckCallback). The callbacks run on the calling thread,
// outside the lock, so they may register and unregister callbacks.
static void check_callbacks(GuestContext *ctx, NetCtlCallbackSlot *slots) {
    const int32_t event =
        ps4_netctl_connected() ? ORBIS_NET_CTL_EVENT_TYPE_IPOBTAINED : ORBIS_NET_CTL_EVENT_TYPE_DISCONNECTED;
    NetCtlCallbackSlot due[ORBIS_NET_CTL_CALLBACK_SLOTS];
    int n = 0;
    pthread_mutex_lock(&g_netctl_mutex);
    for (int i = 0; i < ORBIS_NET_CTL_CALLBACK_SLOTS; i++) {
        if (slots[i].func != 0 && slots[i].last_event != event) {
            slots[i].last_event = event;
            due[n++] = slots[i];
        }
    }
    pthread_mutex_unlock(&g_netctl_mutex);
    for (int i = 0; i < n; i++) {
        recomp_shim_call_guest(ctx, due[i].func, 0, (uint64_t)(int64_t)event, due[i].arg, 0, 0);
    }
}

int32_t sceNetCtlCheckCallback(GuestContext *ctx) {
    check_callbacks(ctx, g_callbacks);
    return ORBIS_OK;
}

int32_t sceNetCtlCheckCallbackForNpToolkit(GuestContext *ctx) {
    check_callbacks(ctx, g_toolkit_callbacks);
    return ORBIS_OK;
}

// ---------------------------------------------------------------------------------------------
// Shims

static void *guest_ptr(const GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

#define RET(v)                                                                                     \
    do {                                                                                           \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(v);                                                \
        SHIM_RETURN();                                                                             \
    } while (0)

void shim_sceNetCtlGetState(GuestContext *ctx) {
    RET(sceNetCtlGetState((int32_t *)guest_ptr(ctx, ctx->rdi)));
}

void shim_sceNetCtlGetResult(GuestContext *ctx) {
    RET(sceNetCtlGetResult((int32_t)ctx->rdi, (int32_t *)guest_ptr(ctx, ctx->rsi)));
}

void shim_sceNetCtlGetInfo(GuestContext *ctx) {
    RET(sceNetCtlGetInfo((int32_t)ctx->rdi, (OrbisNetCtlInfo *)guest_ptr(ctx, ctx->rsi)));
}

void shim_sceNetCtlGetNatInfo(GuestContext *ctx) {
    RET(sceNetCtlGetNatInfo((OrbisNetCtlNatInfo *)guest_ptr(ctx, ctx->rdi)));
}

void shim_sceNetCtlRegisterCallback(GuestContext *ctx) {
    RET(sceNetCtlRegisterCallback(ctx->rdi, ctx->rsi, (int32_t *)guest_ptr(ctx, ctx->rdx)));
}

void shim_sceNetCtlUnregisterCallback(GuestContext *ctx) {
    RET(sceNetCtlUnregisterCallback((int32_t)ctx->rdi));
}

void shim_sceNetCtlCheckCallback(GuestContext *ctx) {
    RET(sceNetCtlCheckCallback(ctx));
}

void shim_sceNetCtlRegisterCallbackForNpToolkit(GuestContext *ctx) {
    RET(sceNetCtlRegisterCallbackForNpToolkit(ctx->rdi, ctx->rsi, (int32_t *)guest_ptr(ctx, ctx->rdx)));
}

void shim_sceNetCtlUnregisterCallbackForNpToolkit(GuestContext *ctx) {
    RET(sceNetCtlUnregisterCallbackForNpToolkit((int32_t)ctx->rdi));
}

void shim_sceNetCtlCheckCallbackForNpToolkit(GuestContext *ctx) {
    RET(sceNetCtlCheckCallbackForNpToolkit(ctx));
}

// ---------------------------------------------------------------------------------------------
// Stubs in shadPS4 ("(STUBBED) called", SCE_OK)

PS4_HLE_STUB(sceNetBweCheckCallbackIpcInt)
PS4_HLE_STUB(sceNetBweClearEventIpcInt)
PS4_HLE_STUB(sceNetBweFinishInternetConnectionTestIpcInt)
PS4_HLE_STUB(sceNetBweGetInfoIpcInt)
PS4_HLE_STUB(sceNetBweRegisterCallbackIpcInt)
PS4_HLE_STUB(sceNetBweSetInternetConnectionTestResultIpcInt)
PS4_HLE_STUB(sceNetBweStartInternetConnectionTestBandwidthTestIpcInt)
PS4_HLE_STUB(sceNetBweStartInternetConnectionTestIpcInt)
PS4_HLE_STUB(sceNetBweUnregisterCallbackIpcInt)
PS4_HLE_STUB(sceNetCtlGetInfoV6)
PS4_HLE_STUB(sceNetCtlGetResultV6)
PS4_HLE_STUB(sceNetCtlGetStateV6)
PS4_HLE_STUB(sceNetCtlRegisterCallbackV6)
PS4_HLE_STUB(sceNetCtlUnregisterCallbackV6)
PS4_HLE_STUB(sceNetCtlCheckCallbackForLibIpcInt)
PS4_HLE_STUB(sceNetCtlClearEventForLibIpcInt)
PS4_HLE_STUB(sceNetCtlClearEventIpcInt)
PS4_HLE_STUB(sceNetCtlConnectConfIpcInt)
PS4_HLE_STUB(sceNetCtlConnectIpcInt)
PS4_HLE_STUB(sceNetCtlConnectWithRetryIpcInt)
PS4_HLE_STUB(sceNetCtlDisableBandwidthManagementIpcInt)
PS4_HLE_STUB(sceNetCtlDisconnectIpcInt)
PS4_HLE_STUB(sceNetCtlEnableBandwidthManagementIpcInt)
PS4_HLE_STUB(sceNetCtlGetBandwidthInfoIpcInt)
PS4_HLE_STUB(sceNetCtlGetEtherLinkMode)
PS4_HLE_STUB(sceNetCtlGetIfStat)
PS4_HLE_STUB(sceNetCtlGetInfoIpcInt)
PS4_HLE_STUB(sceNetCtlGetInfoV6IpcInt)
PS4_HLE_STUB(sceNetCtlGetNatInfoIpcInt)
PS4_HLE_STUB(sceNetCtlGetNetEvConfigInfoIpcInt)
PS4_HLE_STUB(sceNetCtlGetResultIpcInt)
PS4_HLE_STUB(sceNetCtlGetResultV6IpcInt)
PS4_HLE_STUB(sceNetCtlGetScanInfoBssidForSsidListScanIpcInt)
PS4_HLE_STUB(sceNetCtlGetScanInfoBssidIpcInt)
PS4_HLE_STUB(sceNetCtlGetScanInfoByBssidIpcInt)
PS4_HLE_STUB(sceNetCtlGetScanInfoForSsidListScanIpcInt)
PS4_HLE_STUB(sceNetCtlGetScanInfoForSsidScanIpcInt)
PS4_HLE_STUB(sceNetCtlGetState2IpcInt)
PS4_HLE_STUB(sceNetCtlGetStateIpcInt)
PS4_HLE_STUB(sceNetCtlGetStateV6IpcInt)
PS4_HLE_STUB(sceNetCtlGetWifiType)
PS4_HLE_STUB(sceNetCtlInit)
PS4_HLE_STUB(sceNetCtlIsBandwidthManagementEnabledIpcInt)
PS4_HLE_STUB(sceNetCtlRegisterCallbackForLibIpcInt)
PS4_HLE_STUB(sceNetCtlRegisterCallbackIpcInt)
PS4_HLE_STUB(sceNetCtlRegisterCallbackV6IpcInt)
PS4_HLE_STUB(sceNetCtlScanIpcInt)
PS4_HLE_STUB(sceNetCtlSetErrorNotificationEnabledIpcInt)
PS4_HLE_STUB(sceNetCtlSetStunWithPaddingFlagIpcInt)
PS4_HLE_STUB(sceNetCtlTerm)
PS4_HLE_STUB(sceNetCtlUnregisterCallbackForLibIpcInt)
PS4_HLE_STUB(sceNetCtlUnregisterCallbackIpcInt)
PS4_HLE_STUB(sceNetCtlUnregisterCallbackV6IpcInt)
PS4_HLE_STUB(sceNetCtlUnsetStunWithPaddingFlagIpcInt)
PS4_HLE_STUB(Func_D8DCB6973537A3DC)
PS4_HLE_STUB(sceNetCtlClearEventForNpToolkit)
PS4_HLE_STUB(sceNetCtlApCheckCallback)
PS4_HLE_STUB(sceNetCtlApClearEvent)
PS4_HLE_STUB(sceNetCtlApGetConnectInfo)
PS4_HLE_STUB(sceNetCtlApGetInfo)
PS4_HLE_STUB(sceNetCtlApGetResult)
PS4_HLE_STUB(sceNetCtlApGetState)
PS4_HLE_STUB(sceNetCtlApInit)
PS4_HLE_STUB(sceNetCtlApRegisterCallback)
PS4_HLE_STUB(sceNetCtlApStop)
PS4_HLE_STUB(sceNetCtlApTerm)
PS4_HLE_STUB(sceNetCtlApUnregisterCallback)
PS4_HLE_STUB(sceNetCtlApAppInitWpaKey)
PS4_HLE_STUB(sceNetCtlApAppInitWpaKeyForQa)
PS4_HLE_STUB(sceNetCtlApAppStartWithRetry)
PS4_HLE_STUB(sceNetCtlApAppStartWithRetryPid)
PS4_HLE_STUB(sceNetCtlApRestart)
PS4_HLE_STUB(sceNetCtlApRpCheckCallback)
PS4_HLE_STUB(sceNetCtlApRpClearEvent)
PS4_HLE_STUB(sceNetCtlApRpGetInfo)
PS4_HLE_STUB(sceNetCtlApRpGetResult)
PS4_HLE_STUB(sceNetCtlApRpGetState)
PS4_HLE_STUB(sceNetCtlApRpRegisterCallback)
PS4_HLE_STUB(sceNetCtlApRpStart)
PS4_HLE_STUB(sceNetCtlApRpStartConf)
PS4_HLE_STUB(sceNetCtlApRpStartWithRetry)
PS4_HLE_STUB(sceNetCtlApRpStop)
PS4_HLE_STUB(sceNetCtlApRpUnregisterCallback)
