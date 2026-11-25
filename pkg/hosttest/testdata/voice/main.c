// SPDX-License-Identifier: GPL-2.0-or-later

// libSceVoice: initialization, the port table and its limits, per-port settings, the port
// attributes, and data calls that move no voice data.

#include "ps4_voice.h"
#include "ps4_audioin.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

static const OrbisVoiceInitParam k_init = {1, {0}};

static int32_t create(int32_t type) {
    OrbisVoicePortParam p = {type, 0, 0, 0};
    int32_t id = -1;
    return sceVoiceCreatePort(&id, &p) == 0 ? id : -1;
}

static int test_init(void) {
    int32_t id;
    OrbisVoicePortParam pp = {ORBIS_VOICE_PORTTYPE_IN_DEVICE, 0, 0, 0};
    CHECK(sceVoiceCreatePort(&id, &pp) == ORBIS_VOICE_ERROR_NOT_INIT);
    CHECK(sceVoiceEnd() == ORBIS_VOICE_ERROR_NOT_INIT && sceVoiceStop() == ORBIS_VOICE_ERROR_NOT_INIT);
    CHECK(sceVoiceGetVolume(0, NULL) == ORBIS_VOICE_ERROR_NOT_INIT);
    CHECK(sceVoiceInit(NULL, 0) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID);
    CHECK(sceVoiceInit(&k_init, 0) == 0);
    CHECK(sceVoiceInit(&k_init, 0) == ORBIS_VOICE_ERROR_ALREADY_INIT);
    CHECK(sceVoiceInitHQ(&k_init, 0) == ORBIS_VOICE_ERROR_ALREADY_INIT);
    OrbisVoiceStartParam sp = {0, 0x10000, 0, 0, 0};
    CHECK(sceVoiceStart(NULL) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID && sceVoiceStart(&sp) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID);
    sp.memory = 0x100000;
    CHECK(sceVoiceStart(&sp) == 0 && sceVoiceStop() == 0);
    OrbisVoiceResourceInfo ri = {1, 2, 3, 4};
    CHECK(sceVoiceGetResourceInfo(NULL) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID);
    CHECK(sceVoiceGetResourceInfo(&ri) == 0 && ri.field_0 == 0 && ri.field_6 == 0);
    // End drops every port; the HQ variant initializes the same way.
    CHECK(create(ORBIS_VOICE_PORTTYPE_OUT_VOICE) == 0);
    CHECK(sceVoiceEnd() == 0 && sceVoiceInitHQ(&k_init, 0) == 0);
    CHECK(create(ORBIS_VOICE_PORTTYPE_OUT_VOICE) == 0);
    CHECK(sceVoiceVADSetVersion(1) == 0 && sceVoiceVADSetVersion(2) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID);
    printf("PASS init\n");
    return 0;
}

static int test_ports(void) {
    CHECK(sceVoiceInit(&k_init, 0) == 0);
    int32_t id;
    OrbisVoicePortParam pp = {6, 0, 0, 0};
    CHECK(sceVoiceCreatePort(&id, &pp) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID); // no type 6
    CHECK(sceVoiceCreatePort(NULL, &pp) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID);
    for (int i = 0; i < ORBIS_VOICE_MAX_PORT; i++) CHECK(create(i % 6) == i);
    pp.type = 0;
    CHECK(sceVoiceCreatePort(&id, &pp) == ORBIS_VOICE_ERROR_RESOURCE_INSUFFICIENT);
    CHECK(sceVoiceDeletePort(64) == ORBIS_VOICE_ERROR_PORT_INVALID);
    CHECK(sceVoiceDeletePort(7) == 0 && sceVoiceDeletePort(7) == ORBIS_VOICE_ERROR_PORT_INVALID);
    CHECK(create(ORBIS_VOICE_PORTTYPE_IN_VOICE) == 7); // the lowest free slot

    CHECK(sceVoiceConnectIPortToOPort(2, 3) == 0 && sceVoiceDisconnectIPortFromOPort(2, 3) == 0);
    CHECK(sceVoiceConnectIPortToOPort(2, 64) == ORBIS_VOICE_ERROR_PORT_INVALID);
    CHECK(sceVoiceDeletePort(3) == 0 && sceVoiceConnectIPortToOPort(2, 3) == ORBIS_VOICE_ERROR_TOPOLOGY);

    OrbisVoicePortInfo info;
    memset(&info, 0xff, sizeof(info));
    CHECK(sceVoiceGetPortInfo(4, &info) == 0 && info.port_type == 4 && info.byte_count == 0 && info.frame_size == 1 &&
          info.edge_count == 0);
    CHECK(sceVoiceGetPortInfo(3, &info) == ORBIS_VOICE_ERROR_TOPOLOGY);
    CHECK(sceVoiceGetPortInfo(4, NULL) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID);
    CHECK(sceVoiceUpdatePort(4, NULL) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID && sceVoiceUpdatePort(4, &pp) == 0);
    CHECK(sceVoiceUpdatePort(3, &pp) == ORBIS_VOICE_ERROR_PORT_INVALID);
    CHECK(sceVoicePausePort(4) == 0 && sceVoiceResumePort(4) == 0 && sceVoicePausePort(3) == ORBIS_VOICE_ERROR_PORT_INVALID);
    CHECK(sceVoicePausePortAll() == 0 && sceVoiceResumePortAll() == 0);
    printf("PASS ports\n");
    return 0;
}

static int test_settings(void) {
    CHECK(sceVoiceInit(&k_init, 0) == 0);
    const int32_t a = create(ORBIS_VOICE_PORTTYPE_IN_PCMAUDIO), b = create(ORBIS_VOICE_PORTTYPE_OUT_PCMAUDIO);
    float v = 0;
    uint32_t rate = 0;
    bool mute = true;
    CHECK(sceVoiceGetVolume(a, &v) == 0 && v == 1.0f);
    CHECK(sceVoiceGetBitRate(a, &rate) == 0 && rate == 48000);
    CHECK(sceVoiceGetMuteFlag(a, &mute) == 0 && !mute);
    CHECK(sceVoiceSetVolume(a, 0.25f) == 0 && sceVoiceGetVolume(a, &v) == 0 && v == 0.25f);
    CHECK(sceVoiceSetVolume(a, NAN) == 0 && sceVoiceGetVolume(a, &v) == 0 && v == 0.0f);
    CHECK(sceVoiceSetBitRate(a, 16000) == 0 && sceVoiceGetBitRate(a, &rate) == 0 && rate == 16000);
    CHECK(sceVoiceSetMuteFlag(a, true) == 0 && sceVoiceGetMuteFlag(a, &mute) == 0 && mute);
    CHECK(sceVoiceSetMuteFlagAll(true) == 0 && sceVoiceGetMuteFlag(b, &mute) == 0 && mute);
    CHECK(sceVoiceGetBitRate(a, NULL) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID);
    CHECK(sceVoiceGetVolume(9, &v) == ORBIS_VOICE_ERROR_PORT_INVALID);
    // Reset puts the settings back and keeps the port type.
    CHECK(sceVoiceResetPort(a) == 0 && sceVoiceGetVolume(a, &v) == 0 && v == 1.0f);
    CHECK(sceVoiceGetMuteFlag(a, &mute) == 0 && !mute && sceVoiceGetBitRate(a, &rate) == 0 && rate == 48000);
    OrbisVoicePortInfo info;
    CHECK(sceVoiceGetPortInfo(a, &info) == 0 && info.port_type == ORBIS_VOICE_PORTTYPE_IN_PCMAUDIO);
    CHECK(sceVoiceResetPort(9) == ORBIS_VOICE_ERROR_PORT_INVALID);
    printf("PASS settings\n");
    return 0;
}

static int test_attrs(void) {
    CHECK(sceVoiceInit(&k_init, 0) == 0);
    const int32_t mic = create(ORBIS_VOICE_PORTTYPE_IN_DEVICE), spk = create(ORBIS_VOICE_PORTTYPE_OUT_DEVICE);
    uint8_t b = 0xff;
    uint16_t h = 0xffff;
    uint32_t w = 0xffffffff;
    CHECK(sceVoiceGetPortAttr(mic, ORBIS_VOICE_ATTR_1000, &b, 1) == 0 && b == 0);
    CHECK(sceVoiceGetPortAttr(mic, ORBIS_VOICE_ATTR_1002, &w, 4) == 0 && w == ORBIS_AUDIO_IN_SILENT_STATE_DEVICE_NONE);
    CHECK(sceVoiceGetPortAttr(mic, ORBIS_VOICE_ATTR_2001, &w, 4) == 0 && w == 0);
    CHECK(sceVoiceGetPortAttr(spk, ORBIS_VOICE_ATTR_1003, &h, 2) == 0 && h == 0);
    CHECK(sceVoiceGetPortAttr(mic, ORBIS_VOICE_ATTR_1002, &w, 2) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID); // size
    CHECK(sceVoiceGetPortAttr(spk, ORBIS_VOICE_ATTR_1000, &b, 1) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID); // type
    CHECK(sceVoiceGetPortAttr(mic, 999, &b, 1) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID);
    CHECK(sceVoiceGetPortAttr(mic, ORBIS_VOICE_ATTR_1000, NULL, 1) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID);
    CHECK(sceVoiceGetPortAttr(5, ORBIS_VOICE_ATTR_1000, &b, 1) == ORBIS_VOICE_ERROR_PORT_INVALID);
    printf("PASS attrs\n");
    return 0;
}

static int test_data(void) {
    CHECK(sceVoiceInit(&k_init, 0) == 0);
    const int32_t in_pcm = create(ORBIS_VOICE_PORTTYPE_IN_PCMAUDIO), in_dev = create(ORBIS_VOICE_PORTTYPE_IN_DEVICE),
                  out_voice = create(ORBIS_VOICE_PORTTYPE_OUT_VOICE), out_dev = create(ORBIS_VOICE_PORTTYPE_OUT_DEVICE);
    uint8_t buf[64];
    uint32_t size = sizeof(buf);
    CHECK(sceVoiceWriteToIPort(in_pcm, buf, &size, 0) == 0 && size == sizeof(buf)); // taken whole
    CHECK(sceVoiceWriteToIPort(in_dev, buf, &size, 0) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID && size == 0);
    size = 8;
    CHECK(sceVoiceWriteToIPort(40, buf, &size, 0) == ORBIS_VOICE_ERROR_TOPOLOGY && size == 0);
    CHECK(sceVoiceWriteToIPort(in_pcm, NULL, &size, 0) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID);
    size = sizeof(buf);
    CHECK(sceVoiceReadFromOPort(out_voice, buf, &size) == 0 && size == 0); // nothing arrives
    size = 8;
    CHECK(sceVoiceReadFromOPort(out_dev, buf, &size) == ORBIS_VOICE_ERROR_ARGUMENT_INVALID && size == 0);
    CHECK(sceVoiceReadFromOPort(in_pcm, buf, &size) == ORBIS_VOICE_ERROR_TOPOLOGY);
    CHECK(sceVoiceReadFromOPort(40, buf, &size) == ORBIS_VOICE_ERROR_TOPOLOGY);
    CHECK(sceVoiceReadFromOPort(64, buf, &size) == ORBIS_VOICE_ERROR_PORT_INVALID);
    printf("PASS data\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const char *c = argv[1];
    if (strcmp(c, "init") == 0) return test_init();
    if (strcmp(c, "ports") == 0) return test_ports();
    if (strcmp(c, "settings") == 0) return test_settings();
    if (strcmp(c, "attrs") == 0) return test_attrs();
    if (strcmp(c, "data") == 0) return test_data();
    return 2;
}
