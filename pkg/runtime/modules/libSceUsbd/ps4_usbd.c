#include "ps4_usbd.h"

int32_t sceUsbdInit(void) { return 0; }
int32_t sceUsbdExit(void) { return 0; }
int32_t sceUsbdOpen(int32_t bus, int32_t addr) { (void)bus; (void)addr; return -1; }
int32_t sceUsbdClose(int32_t handle) { (void)handle; return 0; }
int32_t sceUsbdGetDeviceList(void *list) { (void)list; return 0; }
int32_t sceUsbdFreeDeviceList(void *list, int32_t unref) { (void)list; (void)unref; return 0; }

int32_t sceUsbdHandleEventsTimeout(uint32_t timeout) { (void)timeout; return 0; }
int32_t sceUsbdFreeTransfer(void *transfer) { (void)transfer; return 0; }
int32_t sceUsbdAllocTransfer(int32_t handle) { (void)handle; return (int32_t)0x200001; }
int32_t sceUsbdFillControlTransfer(void *transfer, int32_t handle, void *buffer, void *cb, void *userData, uint32_t timeout) { (void)transfer; (void)handle; (void)buffer; (void)cb; (void)userData; (void)timeout; return 0; }
int32_t sceUsbdFillControlSetup(void *buffer, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength) { (void)buffer; (void)bmRequestType; (void)bRequest; (void)wValue; (void)wIndex; (void)wLength; return 0; }
int32_t sceUsbdClaimInterface(int32_t handle, int32_t interfaceNo) { (void)handle; (void)interfaceNo; return 0; }
int32_t sceUsbdAttachKernelDriver(int32_t handle, int32_t interfaceNo) { (void)handle; (void)interfaceNo; return 0; }
int32_t sceUsbdGetConfigDescriptor(int32_t handle, uint8_t index, void *buffer, size_t size) { (void)handle; (void)index; (void)buffer; (void)size; return 0; }
int32_t sceUsbdSetConfiguration(int32_t handle, int32_t config) { (void)handle; (void)config; return 0; }
int32_t sceUsbdGetDeviceAddress(int32_t handle) { (void)handle; return 1; }
int32_t sceUsbdFreeConfigDescriptor(void *configDesc) { (void)configDesc; return 0; }
int32_t sceUsbdSubmitTransfer(void *transfer) { (void)transfer; return 0; }
int32_t sceUsbdCheckConnected(int32_t handle) { (void)handle; return 1; }
int32_t sceUsbdReleaseInterface(int32_t handle, int32_t interfaceNo) { (void)handle; (void)interfaceNo; return 0; }
int32_t sceUsbdKernelDriverActive(int32_t handle, int32_t interfaceNo) { (void)handle; (void)interfaceNo; return 0; }
int32_t sceUsbdControlTransfer(int32_t handle, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, void *data, uint16_t wLength, uint32_t timeout) { (void)handle; (void)bmRequestType; (void)bRequest; (void)wValue; (void)wIndex; (void)data; (void)wLength; (void)timeout; return 0; }
int32_t sceUsbdControlTransferGetData(void *transfer, void *buffer, int32_t length) { (void)transfer; (void)buffer; (void)length; return 0; }
int32_t sceUsbdGetDeviceDescriptor(int32_t handle, void *buffer) { (void)handle; (void)buffer; return 0; }
int32_t sceUsbdFillInterruptTransfer(void *transfer, int32_t handle, uint8_t endpoint, void *buffer, int32_t length, void *cb, void *userData, uint32_t timeout) { (void)transfer; (void)handle; (void)endpoint; (void)buffer; (void)length; (void)cb; (void)userData; (void)timeout; return 0; }
int32_t sceUsbdGetBusNumber(int32_t handle) { (void)handle; return 1; }

int32_t sceUsbdGetMaxPacketSize(int32_t handle, uint8_t endpoint) { (void)handle; (void)endpoint; return 64; }
int32_t sceUsbdInterruptTransfer(int32_t handle, uint8_t endpoint, void *data, int32_t length, int32_t *transferred, uint32_t timeout) {
    (void)handle; (void)endpoint; (void)data; (void)length; (void)transferred; (void)timeout;
    return 0;
}

//To do: find a place for those functions, they are not part of the libSceUsbd module but are used by it
// Declarations & Stubs
int32_t sceGameLiveStreamingPostSocialMessage(void) { return 0; }
int32_t sceNpInGameMessageDeleteHandle(void) { return 0; }
int32_t sceNpPartyGetState(void) { return 0; }
int32_t sceNpSignalingGetLocalNetInfo(void) { return 0; }
int32_t sceNpSnsFacebookDialogClose(void) { return 0; }
int32_t sceSharePlayGetCurrentConnectionInfo(void) { return 0; }
int32_t sceVoiceDisconnectIPortFromOPort(void) { return 0; }

void shim_sceUsbdInit(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdExit(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdOpen(GuestContext *ctx) { ctx->rax = 0xFFFFFFFF; SHIM_RETURN(); }
void shim_sceUsbdClose(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdGetDeviceList(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdFreeDeviceList(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }

void shim_sceUsbdHandleEventsTimeout(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdFreeTransfer(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdAllocTransfer(GuestContext *ctx) { ctx->rax = 0x200001; SHIM_RETURN(); }
void shim_sceUsbdFillControlTransfer(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdFillControlSetup(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdClaimInterface(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdAttachKernelDriver(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdGetConfigDescriptor(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdSetConfiguration(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdGetDeviceAddress(GuestContext *ctx) { ctx->rax = 1; SHIM_RETURN(); }
void shim_sceUsbdFreeConfigDescriptor(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdSubmitTransfer(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdCheckConnected(GuestContext *ctx) { ctx->rax = 1; SHIM_RETURN(); }
void shim_sceUsbdReleaseInterface(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdKernelDriverActive(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdControlTransfer(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdControlTransferGetData(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdGetDeviceDescriptor(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdFillInterruptTransfer(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceUsbdGetBusNumber(GuestContext *ctx) { ctx->rax = 1; SHIM_RETURN(); }

void shim_sceUsbdGetMaxPacketSize(GuestContext *ctx) { ctx->rax = 64; SHIM_RETURN(); }
void shim_sceUsbdInterruptTransfer(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }

// Corresponding GuestContext Shims (returning 0)
void shim_sceGameLiveStreamingPostSocialMessage(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceNpInGameMessageDeleteHandle(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceNpPartyGetState(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceNpSignalingGetLocalNetInfo(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceNpSnsFacebookDialogClose(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceSharePlayGetCurrentConnectionInfo(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }
void shim_sceVoiceDisconnectIPortFromOPort(GuestContext *ctx) { ctx->rax = 0; SHIM_RETURN(); }