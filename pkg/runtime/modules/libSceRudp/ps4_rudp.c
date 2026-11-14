#include "ps4_rudp.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

#ifndef ORBIS_OK
#define ORBIS_OK 0
#endif

static pthread_mutex_t g_rudp_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_rudp_initialized = false;
static int32_t g_rudp_next_ctx_id = 1;

int32_t sysctl(void *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    (void)name; (void)namelen; (void)oldp; (void)oldlenp; (void)newp; (void)newlen;
    return 0;
}
int __Ux86_64_setcontext(void *ucp) { (void)ucp; return 0; }

void shim_sceRudpInit(GuestContext *ctx) {
    pthread_mutex_lock(&g_rudp_mutex);
    g_rudp_initialized = true;
    pthread_mutex_unlock(&g_rudp_mutex);
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpTerminate(GuestContext *ctx) {
    pthread_mutex_lock(&g_rudp_mutex);
    g_rudp_initialized = false;
    pthread_mutex_unlock(&g_rudp_mutex);
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpCreateContext(GuestContext *ctx) {
    pthread_mutex_lock(&g_rudp_mutex);
    int32_t ctx_id = g_rudp_next_ctx_id++;
    pthread_mutex_unlock(&g_rudp_mutex);

    // As disassembled in games like Yakuza 0:
    // RDX holds pointer to output context id storage (e.g. MOV RDX, R14; CALL; MOV EDI, [R14])
    if (ctx->rdx && ctx->mem_base) {
        *(int32_t *)(ctx->mem_base + ctx->rdx) = ctx_id;
    }
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpBind(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpSetEventHandler(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpInitiate(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpEnd(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpWrite(GuestContext *ctx) {
    // Return 0 bytes written in offline mode
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceRudpRead(GuestContext *ctx) {
    // Return 0 bytes read in offline mode
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceRudpEnableInternalIOThread(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpEnableInternalIOThread2(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpAccept(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpActivate(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpFlush(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpGetContextStatus(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpGetLocalInfo(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpGetMaxSegmentSize(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpGetNumberOfPacketsToRead(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceRudpGetOption(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpGetRemoteInfo(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpGetSizeReadable(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceRudpGetSizeWritable(GuestContext *ctx) {
    ctx->rax = 0x4000;
    SHIM_RETURN();
}

void shim_sceRudpGetStatus(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpListen(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpNetFlush(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpNetReceived(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpPollCancel(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpPollControl(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpPollCreate(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpPollDestroy(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpPollWait(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpProcessEvents(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpSetMaxSegmentSize(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpSetOption(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}


void shim_sysctl(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim___Ux86_64_setcontext(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }