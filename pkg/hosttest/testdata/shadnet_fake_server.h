// SPDX-License-Identifier: GPL-2.0-or-later

// A local server speaking shadPS4's shadNet wire protocol, for host tests of the NP libraries:
// it answers the NP handler's ServerInfo probe and one login session (Login, GetToken,
// GetServerFeatures, SetClientVersion), then LookupOnlineId requests, until the client hangs
// up. Include it in one test program; it needs shadnet_codec.c and shadnet_pb.c.

#ifndef SHADNET_FAKE_SERVER_H
#define SHADNET_FAKE_SERVER_H

#include "shadnet_client.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct FakeShadNet {
    int listen_fd;
    uint16_t port;
    uint64_t account_id;     // LoginReply user_id
    const char *lookup_npid; // the only online id LookupOnlineId finds
    uint64_t lookup_account;
    pthread_t thread;
    volatile bool stopping;
    // Filled by the server.
    char login_npid[32];
    int sessions; // login sessions served
    int failures;
} FakeShadNet;

static void fake_put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static bool fake_read(int fd, uint8_t *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        const ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

static void fake_send(int fd, uint8_t type, uint16_t cmd, uint64_t id, const uint8_t *payload, size_t len) {
    uint8_t hdr[SHADNET_HEADER_SIZE];
    hdr[0] = type;
    hdr[1] = (uint8_t)cmd;
    hdr[2] = (uint8_t)(cmd >> 8);
    fake_put32(hdr + 3, (uint32_t)(SHADNET_HEADER_SIZE + len));
    for (int i = 0; i < 8; i++) hdr[7 + i] = (uint8_t)(id >> (8 * i));
    send(fd, hdr, sizeof(hdr), 0);
    if (len) send(fd, payload, len, 0);
}

static void fake_reply(int fd, uint16_t cmd, uint64_t id, uint8_t err, const ShadPbMessageDesc *desc, const void *msg) {
    uint8_t buf[512];
    buf[0] = err;
    size_t len = 1;
    if (desc) {
        const size_t n = shadpb_encode(desc, msg, buf + 5);
        fake_put32(buf + 1, (uint32_t)n);
        len += 4 + n;
    }
    fake_send(fd, SHADNET_PACKET_REPLY, cmd, id, buf, len);
}

// Reads one request; payload gets its body (at most cap bytes).
static bool fake_request(int fd, uint16_t *cmd, uint64_t *id, uint8_t *payload, size_t cap, size_t *len) {
    uint8_t hdr[SHADNET_HEADER_SIZE];
    if (!fake_read(fd, hdr, sizeof(hdr))) return false;
    *cmd = (uint16_t)(hdr[1] | (hdr[2] << 8));
    *id = 0;
    for (int i = 0; i < 8; i++) *id |= (uint64_t)hdr[7 + i] << (8 * i);
    const uint32_t total = (uint32_t)hdr[3] | ((uint32_t)hdr[4] << 8) | ((uint32_t)hdr[5] << 16) | ((uint32_t)hdr[6] << 24);
    if (total < SHADNET_HEADER_SIZE || total - SHADNET_HEADER_SIZE > cap) return false;
    *len = total - SHADNET_HEADER_SIZE;
    return fake_read(fd, payload, *len);
}

static bool fake_decode(const uint8_t *payload, size_t len, const ShadPbMessageDesc *desc, void *msg, ShadPbArena *a) {
    if (len < 4) return false;
    const uint32_t n = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) | ((uint32_t)payload[2] << 16) |
                       ((uint32_t)payload[3] << 24);
    return len == 4 + (size_t)n && shadpb_decode(desc, payload + 4, n, msg, a);
}

static void fake_session(FakeShadNet *s, int fd) {
    uint8_t payload[1024];
    size_t len;
    uint16_t cmd;
    uint64_t id;
    ShadPbArena arena = {0};
    // The probe hangs up after the ServerInfo packet: no request arrives.
    if (!fake_request(fd, &cmd, &id, payload, sizeof(payload), &len)) return;
    ShadPb_LoginRequest login;
    if (cmd != SHADNET_CMD_LOGIN || !fake_decode(payload, len, &shadpb_LoginRequest_desc, &login, &arena)) {
        s->failures++;
        return;
    }
    const size_t n = login.npid.len < sizeof(s->login_npid) - 1 ? login.npid.len : sizeof(s->login_npid) - 1;
    memcpy(s->login_npid, login.npid.data, n);
    s->login_npid[n] = '\0';
    ShadPb_LoginReply reply = {.user_id = s->account_id, .avatar_url = shadpb_str("http://avatar")};
    fake_reply(fd, SHADNET_CMD_LOGIN, id, 0, &shadpb_LoginReply_desc, &reply);
    s->sessions++;
    while (fake_request(fd, &cmd, &id, payload, sizeof(payload), &len)) {
        if (cmd == SHADNET_CMD_GET_TOKEN) {
            ShadPb_GetTokenReply token = {.token = shadpb_str("token")};
            fake_reply(fd, cmd, id, 0, &shadpb_GetTokenReply_desc, &token);
        } else if (cmd == SHADNET_CMD_GET_SERVER_FEATURES) {
            ShadPb_ServerFeaturesReply features = {0};
            fake_reply(fd, cmd, id, 0, &shadpb_ServerFeaturesReply_desc, &features);
        } else if (cmd == SHADNET_CMD_LOOKUP_ONLINE_ID) {
            ShadPb_LookupOnlineIdRequest req;
            if (!fake_decode(payload, len, &shadpb_LookupOnlineIdRequest_desc, &req, &arena)) {
                fake_reply(fd, cmd, id, SHADNET_ERR_MALFORMED, NULL, NULL);
            } else if (s->lookup_npid && req.npid.len == strlen(s->lookup_npid) &&
                       strncasecmp((const char *)req.npid.data, s->lookup_npid, req.npid.len) == 0) {
                ShadPb_LookupOnlineIdReply r = {.account_id = s->lookup_account, .npid = shadpb_str(s->lookup_npid)};
                fake_reply(fd, cmd, id, 0, &shadpb_LookupOnlineIdReply_desc, &r);
            } else {
                fake_reply(fd, cmd, id, SHADNET_ERR_NOT_FOUND, NULL, NULL);
            }
        }
        // SetClientVersion and other requests get no reply.
    }
    shadpb_arena_free(&arena);
}

static void *fake_main(void *arg) {
    FakeShadNet *s = arg;
    for (;;) {
        const int fd = accept(s->listen_fd, NULL, NULL);
        if (fd < 0) return NULL;
        if (s->stopping) {
            close(fd);
            return NULL;
        }
        uint8_t ver[4];
        fake_put32(ver, SHADNET_PROTOCOL_VERSION);
        fake_send(fd, SHADNET_PACKET_SERVER_INFO, 0, 0, ver, sizeof(ver));
        fake_session(s, fd);
        close(fd);
    }
}

static int fake_shadnet_start(FakeShadNet *s) {
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (bind(s->listen_fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s->listen_fd, 4) != 0) return 1;
    socklen_t len = sizeof(a);
    getsockname(s->listen_fd, (struct sockaddr *)&a, &len);
    s->port = ntohs(a.sin_port);
    return pthread_create(&s->thread, NULL, fake_main, s) != 0;
}

// Call once the clients hung up: wakes the accept loop with a connection of its own.
static void fake_shadnet_stop(FakeShadNet *s) {
    s->stopping = true;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(s->port), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    connect(fd, (struct sockaddr *)&a, sizeof(a));
    pthread_join(s->thread, NULL);
    close(fd);
    close(s->listen_fd);
}

#endif
