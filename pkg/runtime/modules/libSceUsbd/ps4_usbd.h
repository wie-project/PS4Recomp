#include <stdint.h>
#include "recomp_runtime.h"

int32_t sceUsbdInit(void);
int32_t sceUsbdExit(void);
int32_t sceUsbdOpen(int32_t bus, int32_t addr);
int32_t sceUsbdClose(int32_t handle);
int32_t sceUsbdGetDeviceList(void *list);
int32_t sceUsbdFreeDeviceList(void *list, int32_t unref);

int32_t sceUsbdHandleEventsTimeout(uint32_t timeout);
int32_t sceUsbdFreeTransfer(void *transfer);
int32_t sceUsbdAllocTransfer(int32_t handle);
int32_t sceUsbdFillControlTransfer(void *transfer, int32_t handle, void *buffer, void *cb, void *userData, uint32_t timeout);
int32_t sceUsbdFillControlSetup(void *buffer, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength);
int32_t sceUsbdClaimInterface(int32_t handle, int32_t interfaceNo);
int32_t sceUsbdAttachKernelDriver(int32_t handle, int32_t interfaceNo);
int32_t sceUsbdGetConfigDescriptor(int32_t handle, uint8_t index, void *buffer, size_t size);
int32_t sceUsbdSetConfiguration(int32_t handle, int32_t config);
int32_t sceUsbdGetDeviceAddress(int32_t handle);
int32_t sceUsbdFreeConfigDescriptor(void *configDesc);
int32_t sceUsbdSubmitTransfer(void *transfer);
int32_t sceUsbdCheckConnected(int32_t handle);
int32_t sceUsbdReleaseInterface(int32_t handle, int32_t interfaceNo);
int32_t sceUsbdKernelDriverActive(int32_t handle, int32_t interfaceNo);
int32_t sceUsbdControlTransfer(int32_t handle, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, void *data, uint16_t wLength, uint32_t timeout);
int32_t sceUsbdControlTransferGetData(void *transfer, void *buffer, int32_t length);
int32_t sceUsbdGetDeviceDescriptor(int32_t handle, void *buffer);
int32_t sceUsbdFillInterruptTransfer(void *transfer, int32_t handle, uint8_t endpoint, void *buffer, int32_t length, void *cb, void *userData, uint32_t timeout);
int32_t sceUsbdGetBusNumber(int32_t handle);

int32_t sceUsbdGetMaxPacketSize(int32_t handle, uint8_t endpoint);
int32_t sceUsbdInterruptTransfer(int32_t handle, uint8_t endpoint, void *data, int32_t length, int32_t *transferred, uint32_t timeout);

void shim_sceUsbdInit(GuestContext *ctx);
void shim_sceUsbdExit(GuestContext *ctx);
void shim_sceUsbdOpen(GuestContext *ctx);
void shim_sceUsbdClose(GuestContext *ctx);
void shim_sceUsbdGetDeviceList(GuestContext *ctx);
void shim_sceUsbdFreeDeviceList(GuestContext *ctx);

void shim_sceUsbdHandleEventsTimeout(GuestContext *ctx);
void shim_sceUsbdFreeTransfer(GuestContext *ctx);
void shim_sceUsbdAllocTransfer(GuestContext *ctx);
void shim_sceUsbdFillControlTransfer(GuestContext *ctx);
void shim_sceUsbdFillControlSetup(GuestContext *ctx);
void shim_sceUsbdClaimInterface(GuestContext *ctx);
void shim_sceUsbdAttachKernelDriver(GuestContext *ctx);
void shim_sceUsbdGetConfigDescriptor(GuestContext *ctx);
void shim_sceUsbdSetConfiguration(GuestContext *ctx);
void shim_sceUsbdGetDeviceAddress(GuestContext *ctx);
void shim_sceUsbdFreeConfigDescriptor(GuestContext *ctx);
void shim_sceUsbdSubmitTransfer(GuestContext *ctx);
void shim_sceUsbdCheckConnected(GuestContext *ctx);
void shim_sceUsbdReleaseInterface(GuestContext *ctx);
void shim_sceUsbdKernelDriverActive(GuestContext *ctx);
void shim_sceUsbdControlTransfer(GuestContext *ctx);
void shim_sceUsbdControlTransferGetData(GuestContext *ctx);
void shim_sceUsbdGetDeviceDescriptor(GuestContext *ctx);
void shim_sceUsbdFillInterruptTransfer(GuestContext *ctx);
void shim_sceUsbdGetBusNumber(GuestContext *ctx);

void shim_sceUsbdGetMaxPacketSize(GuestContext *ctx);
void shim_sceUsbdInterruptTransfer(GuestContext *ctx);

//To do: find a place for those functions, they are not part of the libSceUsbd module but are used by it
// Declarations & Stubs
int32_t sceGameLiveStreamingPostSocialMessage(void);
int32_t sceNpInGameMessageDeleteHandle(void);
int32_t sceNpPartyGetState(void);
int32_t sceNpSignalingGetLocalNetInfo(void);
int32_t sceNpSnsFacebookDialogClose(void);
int32_t sceSharePlayGetCurrentConnectionInfo(void);
int32_t sceVoiceDisconnectIPortFromOPort(void);

// Corresponding GuestContext Shims (returning 0)
void shim_sceGameLiveStreamingPostSocialMessage(GuestContext *ctx);
void shim_sceNpInGameMessageDeleteHandle(GuestContext *ctx);
void shim_sceNpPartyGetState(GuestContext *ctx);
void shim_sceNpSignalingGetLocalNetInfo(GuestContext *ctx);
void shim_sceNpSnsFacebookDialogClose(GuestContext *ctx);
void shim_sceSharePlayGetCurrentConnectionInfo(GuestContext *ctx);
void shim_sceVoiceDisconnectIPortFromOPort(GuestContext *ctx);