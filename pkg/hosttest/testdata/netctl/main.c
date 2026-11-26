// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNetCtl against shadPS4's netctl.cpp / net_ctl_obj.cpp, on host memory (guest base 0, so
// guest addresses are host pointers): the state and information without and with the
// connection setting (PS4_NETWORK, shadPS4 isConnectedToNetwork), the NAT information without a
// signaling server, callback ids and errors, and sceNetCtlCheckCallback delivering the state once
// per change to callbacks that are C functions in the dispatch table.

#include "ps4_netctl.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Runtime symbols the sources link against.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];
static GuestContext g_thread_ctx;
GuestContext *recomp_get_current_ctx(void) { return &g_thread_ctx; }

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

#include "../host_guest_mem.h"
#define E(x) ((int32_t)ORBIS_NET_CTL_ERROR_##x)

void shim_sceNetCtlInit(GuestContext *ctx);
void shim_sceNetCtlTerm(GuestContext *ctx);
void shim_sceNetCtlRegisterCallback(GuestContext *ctx);
void shim_sceNetCtlCheckCallback(GuestContext *ctx);
void shim_sceNetCtlCheckCallbackForNpToolkit(GuestContext *ctx);
void shim_sceNetCtlRegisterCallbackForNpToolkit(GuestContext *ctx);
void shim_Func_D8DCB6973537A3DC(GuestContext *ctx);

static uint8_t g_stack[0x4000] __attribute__((aligned(16)));

static int32_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2) {
    GuestContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mem_base = GUEST_MEM;
    ctx.mem_size = ~0ull;
    ctx.rsp = A(g_stack + sizeof(g_stack) - 8);
    ctx.rdi = a0;
    ctx.rsi = a1;
    ctx.rdx = a2;
    ctx.rax = 99;
    shim(&ctx);
    return (int32_t)ctx.rax;
}

static void register_entry(uint64_t addr, recomp_fn_t fn) {
    const uint64_t l1 = (addr >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK;
    const uint64_t l2 = (addr >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK;
    if (!g_dispatch_l1[l1]) g_dispatch_l1[l1] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t *));
    if (!g_dispatch_l1[l1][l2]) g_dispatch_l1[l1][l2] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t));
    g_dispatch_l1[l1][l2][addr & DISPATCH_LEVEL_MASK] = fn;
}

// What the callbacks saw, in call order.
static struct {
    char kind;
    int32_t event;
    uint64_t arg;
} g_calls[16];
static int g_ncalls, g_misaligned;

#define CALLBACK(fn, k)                                            \
    static void fn(GuestContext *ctx) {                            \
        if ((ctx->rsp + 8) % 16 != 0) g_misaligned++;              \
        g_calls[g_ncalls].kind = k;                                \
        g_calls[g_ncalls].event = (int32_t)ctx->rdi;               \
        g_calls[g_ncalls].arg = ctx->rsi;                          \
        g_ncalls++;                                                \
        SHIM_RETURN();                                             \
    }
CALLBACK(cb_a, 'A')
CALLBACK(cb_b, 'B')
CALLBACK(cb_toolkit, 'T')

#define F_A 0x600000ull
#define F_B 0x600100ull
#define F_TOOLKIT 0x600200ull

static int case_disconnected(void) {
    CHECK(!ps4_netctl_connected());
    CHECK(call(shim_sceNetCtlInit, 0, 0, 0) == 0);
    int32_t state = -1;
    CHECK(sceNetCtlGetState(&state) == 0 && state == ORBIS_NET_CTL_STATE_DISCONNECTED);
    OrbisNetCtlInfo info;
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_DEVICE, &info) == E(NOT_CONNECTED));
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) == E(NOT_CONNECTED));
    int32_t err = 7;
    CHECK(sceNetCtlGetResult(ORBIS_NET_CTL_EVENT_TYPE_DISCONNECTED, NULL) == E(INVALID_ADDR));
    CHECK(sceNetCtlGetResult(ORBIS_NET_CTL_EVENT_TYPE_DISCONNECTED, &err) == 0 && err == 0);

    // The NAT information does not depend on the connection: no STUN result without a server.
    OrbisNetCtlNatInfo nat = {8, 5, 5, 0};
    CHECK(sceNetCtlGetNatInfo(NULL) == E(INVALID_ADDR));
    CHECK(sceNetCtlGetNatInfo(&nat) == E(INVALID_SIZE));
    nat.size = sizeof(nat);
    CHECK(sceNetCtlGetNatInfo(&nat) == 0);
    CHECK(nat.stun_status == 0 && nat.nat_type == 0 && nat.mapped_addr != 0);

    // Callbacks: the event is DISCONNECTED, delivered once.
    register_entry(F_A, cb_a);
    int32_t cid = -1;
    CHECK(call(shim_sceNetCtlRegisterCallback, F_A, 11, A(&cid)) == 0 && cid == 0);
    CHECK(call(shim_sceNetCtlCheckCallback, 0, 0, 0) == 0);
    CHECK(g_ncalls == 1 && g_calls[0].kind == 'A' && g_calls[0].event == ORBIS_NET_CTL_EVENT_TYPE_DISCONNECTED &&
          g_calls[0].arg == 11);
    CHECK(call(shim_sceNetCtlCheckCallback, 0, 0, 0) == 0 && g_ncalls == 1);
    CHECK(g_misaligned == 0);
    CHECK(call(shim_sceNetCtlTerm, 0, 0, 0) == 0);
    return 0;
}

static int case_connected(void) {
    CHECK(setenv("PS4_NETWORK", "1", 1) == 0);
    CHECK(ps4_netctl_connected());
    int32_t state = -1;
    CHECK(sceNetCtlGetState(NULL) == E(INVALID_ADDR));
    CHECK(sceNetCtlGetState(&state) == 0 && state == ORBIS_NET_CTL_STATE_IPOBTAINED);

    OrbisNetCtlInfo info;
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_DEVICE, NULL) == E(INVALID_ADDR));
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_DEVICE, &info) == 0 && info.device == ORBIS_NET_CTL_DEVICE_WIRED);
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_MTU, &info) == 0 && info.mtu == 1500);
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_LINK, &info) == 0 && info.link == ORBIS_NET_CTL_LINK_CONNECTED);
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_PRIMARY_DNS, &info) == 0 && strcmp(info.primary_dns, "1.1.1.1") == 0);
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_SECONDARY_DNS, &info) == 0 &&
          strcmp(info.secondary_dns, "1.1.1.1") == 0);
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_CONFIG, &info) == 0 && info.ip_config == 1);
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_HTTP_PROXY_CONFIG, &info) == 0 && info.http_proxy_config == 0);
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_HTTP_PROXY_PORT, &info) == 0 && info.http_proxy_port == 0);
    memset(&info, 'x', sizeof(info));
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_HTTP_PROXY_SERVER, &info) == 0 && info.http_proxy_server[0] == 0);
    // No hostname and unsupported codes: SCE_OK, nothing written.
    memset(&info, 'x', sizeof(info));
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_DHCP_HOSTNAME, &info) == 0 && info.dhcp_hostname[0] == 'x');
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_SSID, &info) == 0 && info.ssid[0] == 'x');
    CHECK(sceNetCtlGetInfo(99, &info) == 0 && info.ssid[0] == 'x');

    // The address is a dotted quad (127.0.0.1 when the host has no route).
    struct in_addr a;
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) == 0);
    CHECK(inet_pton(AF_INET, info.ip_address, &a) == 1);
    const bool online = strcmp(info.ip_address, "127.0.0.1") != 0;
    // With a route: the netmask, gateway and hardware address of that interface.
    memset(&info, 0, sizeof(info));
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_NETMASK, &info) == 0);
    CHECK(!online || inet_pton(AF_INET, info.netmask, &a) == 1);
    memset(&info, 0, sizeof(info));
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_DEFAULT_ROUTE, &info) == 0);
    // a default route through an interface without a gateway (a VPN tunnel) leaves it untouched
    CHECK(info.default_route[0] == 0 || inet_pton(AF_INET, info.default_route, &a) == 1);
    memset(&info, 0, sizeof(info));
    CHECK(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_ETHER_ADDR, &info) == 0);
    printf("online=%d\n", online);

    // Callback ids are slot indices; the event is IPOBTAINED.
    register_entry(F_A, cb_a);
    register_entry(F_B, cb_b);
    register_entry(F_TOOLKIT, cb_toolkit);
    int32_t cid = -1;
    CHECK(sceNetCtlRegisterCallback(0, 1, &cid) == E(INVALID_ADDR));
    CHECK(sceNetCtlRegisterCallback(F_A, 1, NULL) == E(INVALID_ADDR));
    CHECK(call(shim_sceNetCtlRegisterCallback, F_A, 21, A(&cid)) == 0 && cid == 0);
    CHECK(call(shim_sceNetCtlRegisterCallback, F_B, 22, A(&cid)) == 0 && cid == 1);
    CHECK(call(shim_sceNetCtlCheckCallback, 0, 0, 0) == 0);
    CHECK(g_ncalls == 2);
    CHECK(g_calls[0].kind == 'A' && g_calls[0].event == ORBIS_NET_CTL_EVENT_TYPE_IPOBTAINED && g_calls[0].arg == 21);
    CHECK(g_calls[1].kind == 'B' && g_calls[1].event == ORBIS_NET_CTL_EVENT_TYPE_IPOBTAINED && g_calls[1].arg == 22);
    CHECK(call(shim_sceNetCtlCheckCallback, 0, 0, 0) == 0 && g_ncalls == 2); // already seen

    CHECK(sceNetCtlUnregisterCallback(-1) == E(INVALID_ID));
    CHECK(sceNetCtlUnregisterCallback(ORBIS_NET_CTL_CALLBACK_SLOTS) == E(INVALID_ID));
    CHECK(sceNetCtlUnregisterCallback(5) == E(ID_NOT_FOUND));
    CHECK(sceNetCtlUnregisterCallback(0) == 0);
    CHECK(sceNetCtlUnregisterCallback(0) == E(ID_NOT_FOUND));
    // A new callback in the freed slot sees the state again.
    CHECK(call(shim_sceNetCtlRegisterCallback, F_A, 23, A(&cid)) == 0 && cid == 0);
    CHECK(call(shim_sceNetCtlCheckCallback, 0, 0, 0) == 0 && g_ncalls == 3 && g_calls[2].arg == 23);
    for (int32_t i = 2; i < ORBIS_NET_CTL_CALLBACK_SLOTS; i++) {
        CHECK(sceNetCtlRegisterCallback(F_B, 0, &cid) == 0 && cid == i);
    }
    CHECK(sceNetCtlRegisterCallback(F_B, 0, &cid) == E(CALLBACK_MAX));

    // The NP Toolkit callbacks are separate.
    CHECK(call(shim_sceNetCtlRegisterCallbackForNpToolkit, F_TOOLKIT, 31, A(&cid)) == 0 && cid == 0);
    g_ncalls = 0;
    CHECK(call(shim_sceNetCtlCheckCallbackForNpToolkit, 0, 0, 0) == 0);
    CHECK(g_ncalls == 1 && g_calls[0].kind == 'T' && g_calls[0].event == ORBIS_NET_CTL_EVENT_TYPE_IPOBTAINED &&
          g_calls[0].arg == 31);
    CHECK(sceNetCtlUnregisterCallbackForNpToolkit(1) == E(ID_NOT_FOUND));
    CHECK(sceNetCtlUnregisterCallbackForNpToolkit(0) == 0);
    CHECK(g_misaligned == 0);

    // A function shadPS4 leaves as a stub.
    CHECK(call(shim_Func_D8DCB6973537A3DC, 0, 0, 0) == 0);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const char *c = argv[1];
    int rc;
    unsetenv("PS4_NETWORK");
    if (strcmp(c, "disconnected") == 0) {
        rc = case_disconnected();
    } else if (strcmp(c, "connected") == 0) {
        rc = case_connected();
    } else {
        return 2;
    }
    if (rc == 0) printf("PASS %s\n", c);
    return rc;
}
