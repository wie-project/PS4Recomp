#ifndef PS4_NET_H
#define PS4_NET_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// libSceNet Shims
void shim_sceNetInit(GuestContext *ctx);
void shim_sceNetTerm(GuestContext *ctx);
void shim_sceNetErrnoLoc(GuestContext *ctx);

void shim_sceNetSocket(GuestContext *ctx);
void shim_sceNetSocketClose(GuestContext *ctx);
void shim_sceNetBind(GuestContext *ctx);
void shim_sceNetListen(GuestContext *ctx);
void shim_sceNetAccept(GuestContext *ctx);
void shim_sceNetConnect(GuestContext *ctx);
void shim_sceNetSend(GuestContext *ctx);
void shim_sceNetRecv(GuestContext *ctx);
void shim_sceNetSendto(GuestContext *ctx);
void shim_sceNetRecvfrom(GuestContext *ctx);
void shim_sceNetSetsockopt(GuestContext *ctx);
void shim_sceNetGetsockopt(GuestContext *ctx);
void shim_sceNetGetsockname(GuestContext *ctx);
void shim_sceNetGetpeername(GuestContext *ctx);
void shim_sceNetShutdown(GuestContext *ctx);

void shim_sceNetHtons(GuestContext *ctx);
void shim_sceNetHtonl(GuestContext *ctx);
void shim_sceNetHtonll(GuestContext *ctx);
void shim_sceNetNtohs(GuestContext *ctx);
void shim_sceNetNtohl(GuestContext *ctx);
void shim_sceNetNtohll(GuestContext *ctx);
void shim_sceNetInetPton(GuestContext *ctx);
void shim_sceNetInetNtop(GuestContext *ctx);

void shim_sceNetPoolCreate(GuestContext *ctx);
void shim_sceNetPoolDestroy(GuestContext *ctx);

void shim_sceNetResolverCreate(GuestContext *ctx);
void shim_sceNetResolverDestroy(GuestContext *ctx);
void shim_sceNetResolverStartNtoa(GuestContext *ctx);
void shim_sceNetResolverStartAton(GuestContext *ctx);
void shim_sceNetResolverStartNtoaMultipleRecords(GuestContext *ctx);

void shim_sceNetEpollCreate(GuestContext *ctx);
void shim_sceNetEpollDestroy(GuestContext *ctx);
void shim_sceNetEpollControl(GuestContext *ctx);
void shim_sceNetEpollWait(GuestContext *ctx);

void shim_sceNetGetMacAddress(GuestContext *ctx);
void shim_sceNetGetSockInfo(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_NET_H
