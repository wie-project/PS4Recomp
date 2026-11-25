// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_random.h"
#include "ps4_shareplay.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                    \
        }                                                                \
    } while (0)

static int test_random(void) {
    uint8_t buf[ORBIS_RANDOM_MAX_SIZE + 1];
    CHECK(sceRandomGetRandomNumber(buf, ORBIS_RANDOM_MAX_SIZE + 1) == ORBIS_RANDOM_ERROR_INVALID);
    CHECK(sceRandomGetRandomNumber(NULL, 1) == ORBIS_RANDOM_ERROR_INVALID);
    CHECK(sceRandomGetRandomNumber(NULL, 0) == 0);
    // 64 random bytes are all zero with probability 2^-512.
    memset(buf, 0, sizeof(buf));
    CHECK(sceRandomGetRandomNumber(buf, ORBIS_RANDOM_MAX_SIZE) == 0);
    int nonzero = 0;
    for (int i = 0; i < ORBIS_RANDOM_MAX_SIZE; i++) nonzero |= buf[i];
    CHECK(nonzero != 0);
    CHECK(buf[ORBIS_RANDOM_MAX_SIZE] == 0);
    printf("PASS random\n");
    return 0;
}

static int test_shareplay_info(void) {
    CHECK(sceSharePlayGetCurrentConnectionInfo(NULL) == ORBIS_SHARE_PLAY_ERROR_INVALID_ARGUMENT);
    CHECK(sceSharePlayGetCurrentConnectionInfoA(NULL) == ORBIS_SHARE_PLAY_ERROR_INVALID_ARGUMENT);

    OrbisSharePlayConnectionInfo info;
    memset(&info, 0xFF, sizeof(info));
    CHECK(sceSharePlayGetCurrentConnectionInfo(&info) == 0);
    CHECK(info.status == ORBIS_SHARE_PLAY_CONNECTION_STATUS_DORMANT);
    CHECK(info.hostUserId == 0 && info.visitorUserId == 0 && info.hostOnlineId[0] == 0);

    OrbisSharePlayConnectionInfoA infoA;
    memset(&infoA, 0xFF, sizeof(infoA));
    CHECK(sceSharePlayGetCurrentConnectionInfoA(&infoA) == 0);
    const OrbisSharePlayConnectionInfoA zero = {0};
    CHECK(memcmp(&infoA, &zero, sizeof(infoA)) == 0);
    printf("PASS shareplay-info\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "random") == 0) return test_random();
    if (strcmp(argv[1], "shareplay-info") == 0) return test_shareplay_info();
    return 2;
}
