#ifndef PS4_RUDP_H
#define PS4_RUDP_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t sysctl(void *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
int __Ux86_64_setcontext(void *ucp);

void shim_sceRudpInit(GuestContext *ctx);
void shim_sceRudpTerminate(GuestContext *ctx);
void shim_sceRudpCreateContext(GuestContext *ctx);
void shim_sceRudpBind(GuestContext *ctx);
void shim_sceRudpSetEventHandler(GuestContext *ctx);
void shim_sceRudpInitiate(GuestContext *ctx);
void shim_sceRudpEnd(GuestContext *ctx);
void shim_sceRudpWrite(GuestContext *ctx);
void shim_sceRudpRead(GuestContext *ctx);
void shim_sceRudpEnableInternalIOThread(GuestContext *ctx);
void shim_sceRudpEnableInternalIOThread2(GuestContext *ctx);
void shim_sceRudpAccept(GuestContext *ctx);
void shim_sceRudpActivate(GuestContext *ctx);
void shim_sceRudpFlush(GuestContext *ctx);
void shim_sceRudpGetContextStatus(GuestContext *ctx);
void shim_sceRudpGetLocalInfo(GuestContext *ctx);
void shim_sceRudpGetMaxSegmentSize(GuestContext *ctx);
void shim_sceRudpGetNumberOfPacketsToRead(GuestContext *ctx);
void shim_sceRudpGetOption(GuestContext *ctx);
void shim_sceRudpGetRemoteInfo(GuestContext *ctx);
void shim_sceRudpGetSizeReadable(GuestContext *ctx);
void shim_sceRudpGetSizeWritable(GuestContext *ctx);
void shim_sceRudpGetStatus(GuestContext *ctx);
void shim_sceRudpListen(GuestContext *ctx);
void shim_sceRudpNetFlush(GuestContext *ctx);
void shim_sceRudpNetReceived(GuestContext *ctx);
void shim_sceRudpPollCancel(GuestContext *ctx);
void shim_sceRudpPollControl(GuestContext *ctx);
void shim_sceRudpPollCreate(GuestContext *ctx);
void shim_sceRudpPollDestroy(GuestContext *ctx);
void shim_sceRudpPollWait(GuestContext *ctx);
void shim_sceRudpProcessEvents(GuestContext *ctx);
void shim_sceRudpSetMaxSegmentSize(GuestContext *ctx);
void shim_sceRudpSetOption(GuestContext *ctx);

void shim_sysctl(GuestContext *ctx);
void shim___Ux86_64_setcontext(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_RUDP_H
