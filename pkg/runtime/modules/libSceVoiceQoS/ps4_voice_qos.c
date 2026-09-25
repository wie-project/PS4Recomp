#include "ps4_voice_qos.h"
#include <stdio.h>

#define VOICE_QOS_LOG(fmt, ...) printf("[libSceVoiceQoS] " fmt "\n", ##__VA_ARGS__)

static int g_voice_qos_initialized = 0;

int32_t sceVoiceQoSInit(void) {
    VOICE_QOS_LOG("sceVoiceQoSInit");
    g_voice_qos_initialized = 1;
    return 0;
}

int32_t sceVoiceQoSInitHQ(void) {
    VOICE_QOS_LOG("sceVoiceQoSInitHQ");
    g_voice_qos_initialized = 1;
    return 0;
}

int32_t sceVoiceQoSEnd(void) {
    VOICE_QOS_LOG("sceVoiceQoSEnd");
    g_voice_qos_initialized = 0;
    return 0;
}

int32_t sceVoiceQoSConnect(void) {
    VOICE_QOS_LOG("sceVoiceQoSConnect");
    return 0;
}

int32_t sceVoiceQoSDisconnect(void) {
    VOICE_QOS_LOG("sceVoiceQoSDisconnect");
    return 0;
}

int32_t sceVoiceQoSGetStatus(void) {
    return g_voice_qos_initialized;
}

void shim_sceVoiceQoSInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVoiceQoSInit();
}

void shim_sceVoiceQoSInitHQ(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVoiceQoSInitHQ();
}

void shim_sceVoiceQoSEnd(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVoiceQoSEnd();
}

void shim_sceVoiceQoSConnect(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVoiceQoSConnect();
}

void shim_sceVoiceQoSDisconnect(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVoiceQoSDisconnect();
}

void shim_sceVoiceQoSGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVoiceQoSGetStatus();
}
