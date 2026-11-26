#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "modules/libSceNet/ps4_net.h"
#include "modules/libSceHttp/ps4_http.h"
#include "modules/libSceSsl/ps4_ssl.h"

int main(void) {
    printf("[TEST] Starting Pack 4 (Net, Http, Ssl) verification...\n");

    // libSceNetCtl is covered by pkg/hosttest (TestNetCtl).

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
