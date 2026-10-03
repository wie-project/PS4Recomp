#ifndef PS4_NETCTL_H
#define PS4_NETCTL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ORBIS_OK
#define ORBIS_OK 0
#endif

// Error codes
#define ORBIS_NET_CTL_ERROR_CALLBACK_MAX        0x80412103
#define ORBIS_NET_CTL_ERROR_ID_NOT_FOUND        0x80412104
#define ORBIS_NET_CTL_ERROR_INVALID_ID          0x80412105
#define ORBIS_NET_CTL_ERROR_INVALID_ADDR        0x80412107
#define ORBIS_NET_CTL_ERROR_NOT_CONNECTED       0x80412108
#define ORBIS_NET_CTL_ERROR_NOT_AVAIL           0x80412109
#define ORBIS_NET_CTL_ERROR_NETWORK_DISABLED    0x8041210D
#define ORBIS_NET_CTL_ERROR_DISCONNECT_REQ      0x8041210E
#define ORBIS_NET_CTL_ERROR_INVALID_SIZE        0x80412111
#define ORBIS_NET_CTL_ERROR_ETHERNET_PLUGOUT    0x80412115
#define ORBIS_NET_CTL_ERROR_WIFI_DEAUTHED       0x80412116
#define ORBIS_NET_CTL_ERROR_WIFI_BEACON_LOST    0x80412117

// State codes
#define ORBIS_NET_CTL_STATE_DISCONNECTED        0
#define ORBIS_NET_CTL_STATE_CONNECTING          1
#define ORBIS_NET_CTL_STATE_IPOBTAINING         2
#define ORBIS_NET_CTL_STATE_IPOBTAINED          3

// Event types
#define ORBIS_NET_CTL_EVENT_TYPE_DISCONNECTED   1
#define ORBIS_NET_CTL_EVENT_TYPE_DISCONNECT_REQ_FINISHED 2
#define ORBIS_NET_CTL_EVENT_TYPE_IPOBTAINED     3

// Device types
#define ORBIS_NET_CTL_DEVICE_WIRED              0
#define ORBIS_NET_CTL_DEVICE_WIRELESS           1

// Link status
#define ORBIS_NET_CTL_LINK_DISCONNECTED         0
#define ORBIS_NET_CTL_LINK_CONNECTED            1

// Info codes
#define ORBIS_NET_CTL_INFO_DEVICE               1
#define ORBIS_NET_CTL_INFO_ETHER_ADDR           2
#define ORBIS_NET_CTL_INFO_MTU                  3
#define ORBIS_NET_CTL_INFO_LINK                 4
#define ORBIS_NET_CTL_INFO_BSSID                5
#define ORBIS_NET_CTL_INFO_SSID                 6
#define ORBIS_NET_CTL_INFO_WIFI_SECURITY        7
#define ORBIS_NET_CTL_INFO_RSSI_DBM             8
#define ORBIS_NET_CTL_INFO_RSSI_PERCENTAGE      9
#define ORBIS_NET_CTL_INFO_CHANNEL              10
#define ORBIS_NET_CTL_INFO_IP_CONFIG            11
#define ORBIS_NET_CTL_INFO_DHCP_HOSTNAME        12
#define ORBIS_NET_CTL_INFO_PPPOE_AUTH_NAME      13
#define ORBIS_NET_CTL_INFO_IP_ADDRESS           14
#define ORBIS_NET_CTL_INFO_NETMASK              15
#define ORBIS_NET_CTL_INFO_DEFAULT_ROUTE        16
#define ORBIS_NET_CTL_INFO_PRIMARY_DNS          17
#define ORBIS_NET_CTL_INFO_SECONDARY_DNS        18
#define ORBIS_NET_CTL_INFO_HTTP_PROXY_CONFIG    19
#define ORBIS_NET_CTL_INFO_HTTP_PROXY_SERVER    20
#define ORBIS_NET_CTL_INFO_HTTP_PROXY_PORT      21

#define ORBIS_NET_ETHER_ADDR_LEN 6
#define ORBIS_NET_CTL_SSID_LEN (32 + 1)
#define ORBIS_NET_CTL_HOSTNAME_LEN (255 + 1)
#define ORBIS_NET_CTL_AUTH_NAME_LEN (127 + 1)
#define ORBIS_NET_CTL_IPV4_ADDR_STR_LEN 16

typedef struct OrbisNetEtherAddr {
    uint8_t data[ORBIS_NET_ETHER_ADDR_LEN];
} OrbisNetEtherAddr;

typedef union OrbisNetCtlInfo {
    uint32_t device;
    OrbisNetEtherAddr ether_addr;
    uint32_t mtu;
    uint32_t link;
    OrbisNetEtherAddr bssid;
    char ssid[ORBIS_NET_CTL_SSID_LEN];
    uint32_t wifi_security;
    uint8_t rssi_dbm;
    uint8_t rssi_percentage;
    uint8_t channel;
    uint32_t ip_config;
    char dhcp_hostname[ORBIS_NET_CTL_HOSTNAME_LEN];
    char pppoe_auth_name[ORBIS_NET_CTL_AUTH_NAME_LEN];
    char ip_address[ORBIS_NET_CTL_IPV4_ADDR_STR_LEN];
    char netmask[ORBIS_NET_CTL_IPV4_ADDR_STR_LEN];
    char default_route[ORBIS_NET_CTL_IPV4_ADDR_STR_LEN];
    char primary_dns[ORBIS_NET_CTL_IPV4_ADDR_STR_LEN];
    char secondary_dns[ORBIS_NET_CTL_IPV4_ADDR_STR_LEN];
    uint32_t http_proxy_config;
    char http_proxy_server[ORBIS_NET_CTL_HOSTNAME_LEN];
    uint16_t http_proxy_port;
} OrbisNetCtlInfo;

typedef struct OrbisNetCtlNatInfo {
    uint32_t size;
    int32_t stun_status;
    int32_t nat_type;
    uint32_t mapped_addr;
} OrbisNetCtlNatInfo;

typedef void (*OrbisNetCtlCallback)(int32_t eventType, void *arg);
typedef void (*OrbisNetCtlCallbackForNpToolkit)(int32_t eventType, void *arg);

// C API
int32_t sceNetCtlInit(void);
int32_t sceNetCtlTerm(void);
int32_t sceNetCtlGetState(int32_t *state);
int32_t sceNetCtlGetInfo(int32_t code, OrbisNetCtlInfo *info);
int32_t sceNetCtlGetNatInfo(OrbisNetCtlNatInfo *nat_info);
int32_t sceNetCtlRegisterCallback(OrbisNetCtlCallback func, void *arg, int32_t *cid);
int32_t sceNetCtlUnregisterCallback(int32_t cid);
int32_t sceNetCtlCheckCallback(void);
int32_t sceNetCtlRegisterCallbackForNpToolkit(OrbisNetCtlCallbackForNpToolkit func, void *arg, int32_t *cid);
int32_t sceNetCtlUnregisterCallbackForNpToolkit(int32_t cid);
int32_t sceNetCtlCheckCallbackForNpToolkit(void);

// Shims
void shim_sceNetCtlInit(GuestContext *ctx);
void shim_sceNetCtlTerm(GuestContext *ctx);
void shim_sceNetCtlGetState(GuestContext *ctx);
void shim_sceNetCtlGetInfo(GuestContext *ctx);
void shim_sceNetCtlGetNatInfo(GuestContext *ctx);
void shim_sceNetCtlRegisterCallback(GuestContext *ctx);
void shim_sceNetCtlUnregisterCallback(GuestContext *ctx);
void shim_sceNetCtlCheckCallback(GuestContext *ctx);
void shim_sceNetCtlRegisterCallbackForNpToolkit(GuestContext *ctx);
void shim_sceNetCtlUnregisterCallbackForNpToolkit(GuestContext *ctx);
void shim_sceNetCtlCheckCallbackForNpToolkit(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_NETCTL_H
