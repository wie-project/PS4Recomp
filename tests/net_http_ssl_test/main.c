#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "modules/libSceNetCtl/ps4_netctl.h"
#include "modules/libSceNet/ps4_net.h"
#include "modules/libSceHttp/ps4_http.h"
#include "modules/libSceSsl/ps4_ssl.h"

static int g_toolkit_cb_count = 0;
static void test_toolkit_callback(int32_t eventType, void *arg) {
    (void)arg;
    if (eventType == ORBIS_NET_CTL_EVENT_TYPE_IPOBTAINED) {
        g_toolkit_cb_count++;
    }
}

int main(void) {
    printf("[TEST] Starting Pack 4 (Net, NetCtl, Http, Ssl) verification...\n");

    // 1. NetCtl Test
    assert(sceNetCtlInit() == ORBIS_OK);

    int32_t state = 0;
    assert(sceNetCtlGetState(&state) == ORBIS_OK);
    assert(state == ORBIS_NET_CTL_STATE_IPOBTAINED);

    OrbisNetCtlInfo info;
    assert(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_DEVICE, &info) == ORBIS_OK);
    assert(info.device == ORBIS_NET_CTL_DEVICE_WIRED || info.device == ORBIS_NET_CTL_DEVICE_WIRELESS);

    assert(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_MTU, &info) == ORBIS_OK);
    assert(info.mtu == 1500);

    assert(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_LINK, &info) == ORBIS_OK);
    assert(info.link == ORBIS_NET_CTL_LINK_CONNECTED);

    assert(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) == ORBIS_OK);
    printf("[TEST] IP address: %s\n", info.ip_address);
    assert(strlen(info.ip_address) > 0);

    assert(sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_PRIMARY_DNS, &info) == ORBIS_OK);
    assert(strcmp(info.primary_dns, "1.1.1.1") == 0);

    OrbisNetCtlNatInfo nat_info;
    memset(&nat_info, 0, sizeof(nat_info));
    nat_info.size = sizeof(OrbisNetCtlNatInfo);
    assert(sceNetCtlGetNatInfo(&nat_info) == ORBIS_OK);
    assert(nat_info.stun_status == 1);
    assert(nat_info.nat_type == 2);
    printf("[TEST] NAT type: %d, STUN status: %d, Mapped addr: 0x%08x\n",
           nat_info.nat_type, nat_info.stun_status, nat_info.mapped_addr);

    int32_t cid = 0;
    assert(sceNetCtlRegisterCallbackForNpToolkit(test_toolkit_callback, NULL, &cid) == ORBIS_OK);
    assert(cid > 0);
    assert(sceNetCtlCheckCallbackForNpToolkit() == ORBIS_OK);
    assert(g_toolkit_cb_count == 1);
    assert(sceNetCtlUnregisterCallbackForNpToolkit(cid) == ORBIS_OK);

    // 2. Net & Epoll & Resolver Test
    GuestContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.rdi = 1; // epfd
    ctx.rsi = 0;
    shim_sceNetEpollAbort(&ctx);
    assert(ctx.rax == ORBIS_OK);

    int32_t resolver_status = -1;
    ctx.rdi = 1; // resolver id
    ctx.rsi = (uint64_t)&resolver_status;
    shim_sceNetResolverGetError(&ctx);
    assert(ctx.rax == ORBIS_OK);
    assert(resolver_status == 0);

    shim_sceNetGetMemoryPoolStats(&ctx);
    assert(ctx.rax == ORBIS_OK);

    // 3. HTTP Test
    int32_t httpCtxId = sceHttpInit(1, 1, 1024 * 1024);
    assert(httpCtxId > 0);

    int32_t tmplId = sceHttpCreateTemplate(httpCtxId, "SilksongClient/1.0", 1, 0);
    assert(tmplId > 0);

    int32_t connId = sceHttpCreateConnection(tmplId, "api.teamcherry.com", "https", 443, 1);
    assert(connId > 0);

    int32_t reqId = sceHttpCreateRequest2(connId, "GET", "/v1/check", 0);
    assert(reqId > 0);

    assert(sceHttpSetConnectTimeOut(reqId, 5000000) == ORBIS_OK);
    assert(sceHttpSetSendTimeOut(reqId, 5000000) == ORBIS_OK);
    assert(sceHttpSetRecvTimeOut(reqId, 5000000) == ORBIS_OK);
    assert(sceHttpSetRequestContentLength(reqId, 0) == ORBIS_OK);
    assert(sceHttpSetChunkedTransferEnabled(reqId, 0) == ORBIS_OK);
    assert(sceHttpSetAuthEnabled(reqId, 1) == ORBIS_OK);
    assert(sceHttpsEnableOption(reqId, 0x01) == ORBIS_OK);
    assert(sceHttpsDisableOption(reqId, 0x01) == ORBIS_OK);
    assert(sceHttpsSetSslCallback(reqId, NULL, NULL) == ORBIS_OK);
    assert(sceHttpSetRedirectCallback(reqId, NULL, NULL) == ORBIS_OK);
    assert(sceHttpCookieFlush(httpCtxId) == ORBIS_OK);

    // Test sceHttpUriEscape
    const char *raw_str = "param=hello world&symbol=%foo/bar_1.0~test";
    uint64_t req_len = 0;
    assert(sceHttpUriEscape(NULL, &req_len, 0, raw_str) == ORBIS_OK);
    assert(req_len > strlen(raw_str));

    char escaped_buf[256] = {0};
    assert(sceHttpUriEscape(escaped_buf, &req_len, sizeof(escaped_buf), raw_str) == ORBIS_OK);
    printf("[TEST] Escaped URI: %s\n", escaped_buf);
    assert(strstr(escaped_buf, "hello%20world") != NULL);
    assert(strstr(escaped_buf, "%25foo") != NULL);
    assert(strstr(escaped_buf, "%2Fbar") != NULL);
    assert(strstr(escaped_buf, "_1.0~test") != NULL);

    // 4. SSL Test
    int32_t sslCtxId = sceSslInit(65536);
    assert(sslCtxId > 0);

    assert(sceSslGetMemoryPoolStats(NULL) == ORBIS_OK);
    uint32_t cert_count = 999;
    assert(sceSslGetNameEntryCount(NULL, &cert_count) == ORBIS_OK);
    assert(cert_count == 0);
    assert(sceSslGetNameEntryInfo(NULL, 0, NULL) == ORBIS_OK);
    assert(sceSslGetIssuerName(NULL, NULL) == ORBIS_OK);
    assert(sceSslGetSubjectName(NULL, NULL) == ORBIS_OK);
    assert(sceSslGetSerialNumber(NULL, NULL) == ORBIS_OK);
    size_t pem_len = 999;
    assert(sceSslGetPem(NULL, NULL, &pem_len) == ORBIS_OK);
    assert(pem_len == 0);
    assert(sceSslFreeSslCertName(NULL) == ORBIS_OK);
    assert(sceSslFreeCaCerts(NULL) == ORBIS_OK);
    assert(sceSslTerm(sslCtxId) == ORBIS_OK);

    printf("[TEST] Pack 4 verification successfully PASSED!\n");
    return 0;
}
