#include "ps4_netctl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define MAX_NETCTL_CALLBACKS 8

static inline void *guest_to_host(const GuestContext *ctx, uint64_t addr) {
    if (!addr) return NULL;
    if (ctx && ctx->mem_base && addr < (1ULL << 39)) {
        return (void *)(ctx->mem_base + addr);
    }
    return (void *)addr;
}

typedef struct CallbackSlot {
    OrbisNetCtlCallback func;
    void *arg;
    bool active;
} CallbackSlot;

typedef struct ToolkitCallbackSlot {
    OrbisNetCtlCallbackForNpToolkit func;
    void *arg;
    bool active;
} ToolkitCallbackSlot;

static pthread_mutex_t g_netctl_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_netctl_initialized = false;
static CallbackSlot g_callbacks[MAX_NETCTL_CALLBACKS];
static ToolkitCallbackSlot g_toolkit_callbacks[MAX_NETCTL_CALLBACKS];

static void query_local_network(char *ip_out, char *netmask_out, uint8_t *mac_out) {
    if (ip_out) strcpy(ip_out, "127.0.0.1");
    if (netmask_out) strcpy(netmask_out, "255.255.255.0");
    if (mac_out) {
        mac_out[0] = 0x00; mac_out[1] = 0xD9; mac_out[2] = 0xD1;
        mac_out[3] = 0x12; mac_out[4] = 0x34; mac_out[5] = 0x56;
    }

    struct ifaddrs *ifap = NULL;
    if (getifaddrs(&ifap) != 0 || !ifap) {
        return;
    }

    // Pass 1: Look for active non-loopback IPv4 interface
    char active_if_name[IFNAMSIZ] = {0};
    for (struct ifaddrs *ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if ((ifa->ifa_flags & IFF_LOOPBACK) || !(ifa->ifa_flags & IFF_UP)) continue;

        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            if (ip_out) {
                inet_ntop(AF_INET, &sa->sin_addr, ip_out, ORBIS_NET_CTL_IPV4_ADDR_STR_LEN);
            }
            if (ifa->ifa_netmask && netmask_out) {
                struct sockaddr_in *nm = (struct sockaddr_in *)ifa->ifa_netmask;
                inet_ntop(AF_INET, &nm->sin_addr, netmask_out, ORBIS_NET_CTL_IPV4_ADDR_STR_LEN);
            }
            strncpy(active_if_name, ifa->ifa_name, sizeof(active_if_name) - 1);
            break;
        }
    }

    // Pass 2: Look for MAC address of the active interface
    if (mac_out && active_if_name[0] != '\0') {
        for (struct ifaddrs *ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) continue;
            if (ifa->ifa_addr->sa_family == AF_LINK && strcmp(ifa->ifa_name, active_if_name) == 0) {
                struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
                if (sdl->sdl_alen >= ORBIS_NET_ETHER_ADDR_LEN) {
                    memcpy(mac_out, LLADDR(sdl), ORBIS_NET_ETHER_ADDR_LEN);
                    break;
                }
            }
        }
    }

    freeifaddrs(ifap);
}

int32_t sceNetCtlInit(void) {
    pthread_mutex_lock(&g_netctl_mutex);
    g_netctl_initialized = true;
    pthread_mutex_unlock(&g_netctl_mutex);
    return ORBIS_OK;
}

int32_t sceNetCtlTerm(void) {
    pthread_mutex_lock(&g_netctl_mutex);
    g_netctl_initialized = false;
    memset(g_callbacks, 0, sizeof(g_callbacks));
    memset(g_toolkit_callbacks, 0, sizeof(g_toolkit_callbacks));
    pthread_mutex_unlock(&g_netctl_mutex);
    return ORBIS_OK;
}

int32_t sceNetCtlGetState(int32_t *state) {
    if (!state) {
        return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_ADDR;
    }
    *state = ORBIS_NET_CTL_STATE_IPOBTAINED;
    return ORBIS_OK;
}

int32_t sceNetCtlGetInfo(int32_t code, OrbisNetCtlInfo *info) {
    if (!info) {
        return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_ADDR;
    }

    char local_ip[ORBIS_NET_CTL_IPV4_ADDR_STR_LEN] = {0};
    char local_netmask[ORBIS_NET_CTL_IPV4_ADDR_STR_LEN] = {0};
    uint8_t local_mac[ORBIS_NET_ETHER_ADDR_LEN] = {0};

    switch (code) {
    case ORBIS_NET_CTL_INFO_DEVICE:
        info->device = ORBIS_NET_CTL_DEVICE_WIRED;
        break;
    case ORBIS_NET_CTL_INFO_ETHER_ADDR:
        query_local_network(NULL, NULL, local_mac);
        memcpy(info->ether_addr.data, local_mac, ORBIS_NET_ETHER_ADDR_LEN);
        break;
    case ORBIS_NET_CTL_INFO_MTU:
        info->mtu = 1500;
        break;
    case ORBIS_NET_CTL_INFO_LINK:
        info->link = ORBIS_NET_CTL_LINK_CONNECTED;
        break;
    case ORBIS_NET_CTL_INFO_IP_ADDRESS:
        query_local_network(local_ip, NULL, NULL);
        strncpy(info->ip_address, local_ip, sizeof(info->ip_address) - 1);
        info->ip_address[sizeof(info->ip_address) - 1] = '\0';
        break;
    case ORBIS_NET_CTL_INFO_NETMASK:
        query_local_network(NULL, local_netmask, NULL);
        strncpy(info->netmask, local_netmask, sizeof(info->netmask) - 1);
        info->netmask[sizeof(info->netmask) - 1] = '\0';
        break;
    case ORBIS_NET_CTL_INFO_DEFAULT_ROUTE: {
        query_local_network(local_ip, NULL, NULL);
        // Estimate gateway as x.x.x.1
        strncpy(info->default_route, local_ip, sizeof(info->default_route) - 1);
        char *last_dot = strrchr(info->default_route, '.');
        if (last_dot) {
            strcpy(last_dot + 1, "1");
        } else {
            strcpy(info->default_route, "192.168.1.1");
        }
        break;
    }
    case ORBIS_NET_CTL_INFO_PRIMARY_DNS:
        strcpy(info->primary_dns, "1.1.1.1");
        break;
    case ORBIS_NET_CTL_INFO_SECONDARY_DNS:
        strcpy(info->secondary_dns, "8.8.8.8");
        break;
    case ORBIS_NET_CTL_INFO_HTTP_PROXY_CONFIG:
        info->http_proxy_config = 0; // off
        break;
    case ORBIS_NET_CTL_INFO_HTTP_PROXY_SERVER:
        info->http_proxy_server[0] = '\0';
        break;
    case ORBIS_NET_CTL_INFO_HTTP_PROXY_PORT:
        info->http_proxy_port = 0;
        break;
    case ORBIS_NET_CTL_INFO_IP_CONFIG:
        info->ip_config = 1; // static / manual or DHCP obtained
        break;
    case ORBIS_NET_CTL_INFO_DHCP_HOSTNAME:
        info->dhcp_hostname[0] = '\0';
        break;
    default:
        return (int32_t)ORBIS_NET_CTL_ERROR_NOT_AVAIL;
    }

    return ORBIS_OK;
}

int32_t sceNetCtlGetNatInfo(OrbisNetCtlNatInfo *nat_info) {
    if (!nat_info) {
        return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_ADDR;
    }
    if (nat_info->size != sizeof(OrbisNetCtlNatInfo)) {
        return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_SIZE;
    }

    char local_ip[ORBIS_NET_CTL_IPV4_ADDR_STR_LEN] = {0};
    query_local_network(local_ip, NULL, NULL);

    nat_info->stun_status = 1; // Success
    nat_info->nat_type = 2;    // Type 2 (Moderate / Console standard behind router)
    nat_info->mapped_addr = (uint32_t)inet_addr(local_ip);
    return ORBIS_OK;
}

int32_t sceNetCtlRegisterCallback(OrbisNetCtlCallback func, void *arg, int32_t *cid) {
    if (!func || !cid) {
        return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_ADDR;
    }

    pthread_mutex_lock(&g_netctl_mutex);
    for (int i = 0; i < MAX_NETCTL_CALLBACKS; i++) {
        if (!g_callbacks[i].active) {
            g_callbacks[i].func = func;
            g_callbacks[i].arg = arg;
            g_callbacks[i].active = true;
            *cid = i + 1;
            pthread_mutex_unlock(&g_netctl_mutex);
            return ORBIS_OK;
        }
    }
    pthread_mutex_unlock(&g_netctl_mutex);
    return (int32_t)ORBIS_NET_CTL_ERROR_CALLBACK_MAX;
}

int32_t sceNetCtlUnregisterCallback(int32_t cid) {
    pthread_mutex_lock(&g_netctl_mutex);
    int idx = cid - 1;
    if (idx >= 0 && idx < MAX_NETCTL_CALLBACKS && g_callbacks[idx].active) {
        g_callbacks[idx].active = false;
        g_callbacks[idx].func = NULL;
        g_callbacks[idx].arg = NULL;
        pthread_mutex_unlock(&g_netctl_mutex);
        return ORBIS_OK;
    }
    pthread_mutex_unlock(&g_netctl_mutex);
    return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_ID;
}

int32_t sceNetCtlCheckCallback(void) {
    CallbackSlot copy[MAX_NETCTL_CALLBACKS];
    pthread_mutex_lock(&g_netctl_mutex);
    memcpy(copy, g_callbacks, sizeof(copy));
    pthread_mutex_unlock(&g_netctl_mutex);

    for (int i = 0; i < MAX_NETCTL_CALLBACKS; i++) {
        if (copy[i].active && copy[i].func) {
            copy[i].func(ORBIS_NET_CTL_EVENT_TYPE_IPOBTAINED, copy[i].arg);
        }
    }
    return ORBIS_OK;
}

int32_t sceNetCtlRegisterCallbackForNpToolkit(OrbisNetCtlCallbackForNpToolkit func, void *arg, int32_t *cid) {
    if (!func || !cid) {
        return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_ADDR;
    }

    pthread_mutex_lock(&g_netctl_mutex);
    for (int i = 0; i < MAX_NETCTL_CALLBACKS; i++) {
        if (!g_toolkit_callbacks[i].active) {
            g_toolkit_callbacks[i].func = func;
            g_toolkit_callbacks[i].arg = arg;
            g_toolkit_callbacks[i].active = true;
            *cid = i + 1;
            pthread_mutex_unlock(&g_netctl_mutex);
            return ORBIS_OK;
        }
    }
    pthread_mutex_unlock(&g_netctl_mutex);
    return (int32_t)ORBIS_NET_CTL_ERROR_CALLBACK_MAX;
}

int32_t sceNetCtlUnregisterCallbackForNpToolkit(int32_t cid) {
    pthread_mutex_lock(&g_netctl_mutex);
    int idx = cid - 1;
    if (idx >= 0 && idx < MAX_NETCTL_CALLBACKS && g_toolkit_callbacks[idx].active) {
        g_toolkit_callbacks[idx].active = false;
        g_toolkit_callbacks[idx].func = NULL;
        g_toolkit_callbacks[idx].arg = NULL;
        pthread_mutex_unlock(&g_netctl_mutex);
        return ORBIS_OK;
    }
    pthread_mutex_unlock(&g_netctl_mutex);
    return (int32_t)ORBIS_NET_CTL_ERROR_INVALID_ID;
}

int32_t sceNetCtlCheckCallbackForNpToolkit(void) {
    ToolkitCallbackSlot copy[MAX_NETCTL_CALLBACKS];
    pthread_mutex_lock(&g_netctl_mutex);
    memcpy(copy, g_toolkit_callbacks, sizeof(copy));
    pthread_mutex_unlock(&g_netctl_mutex);

    for (int i = 0; i < MAX_NETCTL_CALLBACKS; i++) {
        if (copy[i].active && copy[i].func) {
            copy[i].func(ORBIS_NET_CTL_EVENT_TYPE_IPOBTAINED, copy[i].arg);
        }
    }
    return ORBIS_OK;
}

// Shims
void shim_sceNetCtlInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNetCtlInit();
    SHIM_RETURN();
}

void shim_sceNetCtlTerm(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNetCtlTerm();
    SHIM_RETURN();
}

void shim_sceNetCtlGetState(GuestContext *ctx) {
    int32_t *state = (int32_t *)guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)sceNetCtlGetState(state);
    SHIM_RETURN();
}

void shim_sceNetCtlGetInfo(GuestContext *ctx) {
    int32_t code = (int32_t)ctx->rdi;
    OrbisNetCtlInfo *info = (OrbisNetCtlInfo *)guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)sceNetCtlGetInfo(code, info);
    SHIM_RETURN();
}

void shim_sceNetCtlGetNatInfo(GuestContext *ctx) {
    OrbisNetCtlNatInfo *nat_info = (OrbisNetCtlNatInfo *)guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)sceNetCtlGetNatInfo(nat_info);
    SHIM_RETURN();
}

void shim_sceNetCtlRegisterCallback(GuestContext *ctx) {
    OrbisNetCtlCallback func = (OrbisNetCtlCallback)guest_to_host(ctx, ctx->rdi);
    void *arg = (void *)guest_to_host(ctx, ctx->rsi);
    int32_t *cid = (int32_t *)guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)sceNetCtlRegisterCallback(func, arg, cid);
    SHIM_RETURN();
}

void shim_sceNetCtlUnregisterCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNetCtlUnregisterCallback((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceNetCtlCheckCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNetCtlCheckCallback();
    SHIM_RETURN();
}

void shim_sceNetCtlRegisterCallbackForNpToolkit(GuestContext *ctx) {
    OrbisNetCtlCallbackForNpToolkit func = (OrbisNetCtlCallbackForNpToolkit)guest_to_host(ctx, ctx->rdi);
    void *arg = (void *)guest_to_host(ctx, ctx->rsi);
    int32_t *cid = (int32_t *)guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)sceNetCtlRegisterCallbackForNpToolkit(func, arg, cid);
    SHIM_RETURN();
}

void shim_sceNetCtlUnregisterCallbackForNpToolkit(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNetCtlUnregisterCallbackForNpToolkit((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceNetCtlCheckCallbackForNpToolkit(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNetCtlCheckCallbackForNpToolkit();
    SHIM_RETURN();
}
