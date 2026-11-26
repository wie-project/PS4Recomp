// libSceRudp, following shadPS4 (src/core/libraries/rudp), where every function is a stub that
// returns ORBIS_OK: there is no RUDP transport on the host, so no connection ever forms.
#include "ps4_rudp.h"
#include <stdatomic.h>
#include <stdint.h>

#ifndef ORBIS_OK
#define ORBIS_OK 0
#endif

static _Atomic int32_t g_rudp_next_ctx_id = 1;

void shim_sceRudpInit(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

void shim_sceRudpTerminate(GuestContext *ctx) {
    ctx->rax = ORBIS_OK;
    SHIM_RETURN();
}

// sceRudpCreateContext(handler, arg, ctxId*). shadPS4 returns ORBIS_OK without writing *ctxId;
// the SDK signature has the context id as an output, and games pass the id read back from it
// to every later call (Yakuza 0: MOV RDX, R14; CALL; MOV EDI, [R14]), so a successful call
// stores a distinct id there instead of leaving the caller's memory uninitialized.
void shim_sceRudpCreateContext(GuestContext *ctx) {
    if (ctx->rdx && ctx->mem_base) {
        *(int32_t *)(ctx->mem_base + ctx->rdx) = atomic_fetch_add(&g_rudp_next_ctx_id, 1);
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
    ctx->rax = ORBIS_OK;
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