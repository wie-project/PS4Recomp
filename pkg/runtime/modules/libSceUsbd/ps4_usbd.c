#include "ps4_usbd.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

//=============================================================================
// libSceUsbd C Implementation
//=============================================================================

int32_t sceUsbdInit(void) {
    return ORBIS_USBD_SUCCESS;
}

void sceUsbdExit(void) {
}

int64_t sceUsbdGetDeviceList(SceUsbdDevice ***list) {
    if (!list) {
        return (int64_t)ORBIS_USBD_ERROR_INVALID_PARAM;
    }
    *list = NULL;
    return 0; // 0 devices discovered
}

void sceUsbdFreeDeviceList(SceUsbdDevice **list, int32_t unref_devices) {
    (void)unref_devices;
    if (list) {
        free(list);
    }
}

int32_t sceUsbdOpen(SceUsbdDevice *device, SceUsbdDeviceHandle **dev_handle) {
    (void)device;
    if (dev_handle) {
        *dev_handle = NULL;
    }
    return ORBIS_USBD_ERROR_NO_DEVICE;
}

void sceUsbdClose(SceUsbdDeviceHandle *dev_handle) {
    (void)dev_handle;
}

int32_t sceUsbdCheckConnected(SceUsbdDeviceHandle *dev_handle) {
    (void)dev_handle;
    return ORBIS_USBD_ERROR_NO_DEVICE;
}

uint8_t sceUsbdGetBusNumber(SceUsbdDevice *device) {
    (void)device;
    return 0;
}

uint8_t sceUsbdGetDeviceAddress(SceUsbdDevice *device) {
    (void)device;
    return 0;
}

int32_t sceUsbdGetDeviceDescriptor(SceUsbdDevice *device, SceUsbdDeviceDescriptor *desc) {
    (void)device;
    if (!desc) {
        return ORBIS_USBD_ERROR_INVALID_PARAM;
    }
    return ORBIS_USBD_ERROR_NO_DEVICE;
}

int32_t sceUsbdGetConfigDescriptor(SceUsbdDevice *device, uint8_t config_index, SceUsbdConfigDescriptor **config) {
    (void)device;
    (void)config_index;
    if (config) {
        *config = NULL;
    }
    return ORBIS_USBD_ERROR_NO_DEVICE;
}

void sceUsbdFreeConfigDescriptor(SceUsbdConfigDescriptor *config) {
    if (config) {
        free(config);
    }
}

int32_t sceUsbdSetConfiguration(SceUsbdDeviceHandle *dev_handle, int32_t config) {
    (void)dev_handle;
    (void)config;
    return ORBIS_USBD_ERROR_NO_DEVICE;
}

int32_t sceUsbdClaimInterface(SceUsbdDeviceHandle *dev_handle, int32_t interface_number) {
    (void)dev_handle;
    (void)interface_number;
    return ORBIS_USBD_ERROR_NO_DEVICE;
}

int32_t sceUsbdReleaseInterface(SceUsbdDeviceHandle *dev_handle, int32_t interface_number) {
    (void)dev_handle;
    (void)interface_number;
    return ORBIS_USBD_ERROR_NO_DEVICE;
}

int32_t sceUsbdAttachKernelDriver(SceUsbdDeviceHandle *dev_handle, int32_t interface_number) {
    (void)dev_handle;
    (void)interface_number;
    return ORBIS_USBD_ERROR_NO_DEVICE;
}

int32_t sceUsbdKernelDriverActive(SceUsbdDeviceHandle *dev_handle, int32_t interface_number) {
    (void)dev_handle;
    (void)interface_number;
    return 0;
}

int32_t sceUsbdGetMaxPacketSize(SceUsbdDevice *device, uint8_t endpoint) {
    (void)endpoint;
    if (!device) {
        return ORBIS_USBD_ERROR_NO_DEVICE;
    }
    return 64;
}

SceUsbdTransfer *sceUsbdAllocTransfer(int32_t iso_packets) {
    int count = (iso_packets > 0) ? iso_packets : 0;
    size_t sz = sizeof(SceUsbdTransfer) + (size_t)count * sizeof(SceUsbdIsoPacketDescriptor);
    GuestContext *ctx = recomp_get_current_ctx();
    SceUsbdTransfer *t = NULL;
    if (ctx) {
        uint64_t vaddr = recomp_vm_alloc_named(ctx, sz, 0x3, 0, "usbd_transfer");
        if (vaddr) {
            t = (SceUsbdTransfer *)vaddr;
        }
    }
    if (!t) {
        t = (SceUsbdTransfer *)calloc(1, sz);
    } else {
        memset(t, 0, sz);
    }
    if (t) {
        t->num_iso_packets = count;
    }
    return t;
}

void sceUsbdFreeTransfer(SceUsbdTransfer *transfer) {
    if (!transfer) return;
    GuestContext *ctx = recomp_get_current_ctx();
    if (ctx) {
        int count = (transfer->num_iso_packets > 0) ? transfer->num_iso_packets : 0;
        size_t sz = sizeof(SceUsbdTransfer) + (size_t)count * sizeof(SceUsbdIsoPacketDescriptor);
        recomp_vm_free(ctx, (uint64_t)transfer, sz);
    } else {
        free(transfer);
    }
}

int32_t sceUsbdSubmitTransfer(SceUsbdTransfer *transfer) {
    if (!transfer || !transfer->dev_handle) {
        return ORBIS_USBD_ERROR_NO_DEVICE;
    }
    return ORBIS_USBD_ERROR_IO;
}

void sceUsbdFillControlSetup(uint8_t *buf, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength) {
    if (!buf) return;
    SceUsbdControlSetup *setup = (SceUsbdControlSetup *)buf;
    setup->bmRequestType = bmRequestType;
    setup->bRequest = bRequest;
    setup->wValue = wValue;
    setup->wIndex = wIndex;
    setup->wLength = wLength;
}

void sceUsbdFillControlTransfer(SceUsbdTransfer *transfer, SceUsbdDeviceHandle *dev_handle, uint8_t *buffer, SceUsbdTransferCallback callback, void *user_data, uint32_t timeout) {
    if (!transfer) return;
    SceUsbdControlSetup *setup = (SceUsbdControlSetup *)buffer;
    transfer->dev_handle = dev_handle;
    transfer->endpoint = 0;
    transfer->type = LIBUSB_TRANSFER_TYPE_CONTROL;
    transfer->timeout = timeout;
    transfer->buffer = buffer;
    if (setup) {
        transfer->length = LIBUSB_CONTROL_SETUP_SIZE + (int32_t)setup->wLength;
    } else {
        transfer->length = 0;
    }
    transfer->user_data = user_data;
    transfer->callback = callback;
}

void sceUsbdFillInterruptTransfer(SceUsbdTransfer *transfer, SceUsbdDeviceHandle *dev_handle, uint8_t endpoint, uint8_t *buffer, int32_t length, SceUsbdTransferCallback callback, void *user_data, uint32_t timeout) {
    if (!transfer) return;
    transfer->dev_handle = dev_handle;
    transfer->endpoint = endpoint;
    transfer->type = LIBUSB_TRANSFER_TYPE_INTERRUPT;
    transfer->timeout = timeout;
    transfer->buffer = buffer;
    transfer->length = length;
    transfer->user_data = user_data;
    transfer->callback = callback;
}

uint8_t *sceUsbdControlTransferGetData(SceUsbdTransfer *transfer) {
    if (!transfer || !transfer->buffer) {
        return NULL;
    }
    return transfer->buffer + LIBUSB_CONTROL_SETUP_SIZE;
}

int32_t sceUsbdControlTransfer(SceUsbdDeviceHandle *dev_handle, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t *data, int32_t length, uint32_t timeout) {
    (void)dev_handle; (void)request_type; (void)bRequest; (void)wValue; (void)wIndex; (void)data; (void)length; (void)timeout;
    return ORBIS_USBD_ERROR_NO_DEVICE;
}

int32_t sceUsbdInterruptTransfer(SceUsbdDeviceHandle *dev_handle, uint8_t endpoint, uint8_t *data, int32_t length, int32_t *transferred, uint32_t timeout) {
    (void)dev_handle; (void)endpoint; (void)data; (void)length; (void)timeout;
    if (transferred) {
        *transferred = 0;
    }
    return ORBIS_USBD_ERROR_NO_DEVICE;
}

typedef struct SceUsbdTimeval {
    int64_t tv_sec;
    int64_t tv_usec;
} SceUsbdTimeval;

int32_t sceUsbdHandleEventsTimeout(void *tv) {
    int64_t sleep_usec = 10000; // default 10ms if tv is NULL
    if (tv != NULL) {
        const SceUsbdTimeval *otv = (const SceUsbdTimeval *)tv;
        if (otv->tv_sec < 0 || otv->tv_usec < 0) {
            return ORBIS_USBD_ERROR_INVALID_PARAM;
        }
        sleep_usec = otv->tv_sec * 1000000LL + otv->tv_usec;
        // In an event loop without physical USB devices connected,
        // a 0 or sub-millisecond timeout causes a 100% CPU busy-spin loop.
        // Sleep for at least 1ms to yield the CPU, and clamp to at most 50ms.
        if (sleep_usec <= 0) {
            sleep_usec = 1000; // 1 ms
        } else if (sleep_usec > 50000) {
            sleep_usec = 50000; // 50 ms
        }
    }
    usleep((useconds_t)sleep_usec);
    return ORBIS_USBD_SUCCESS;
}

//=============================================================================
// GuestContext Shims
//=============================================================================

void shim_sceUsbdInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUsbdInit();
    SHIM_RETURN();
}

void shim_sceUsbdExit(GuestContext *ctx) {
    sceUsbdExit();
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceUsbdOpen(GuestContext *ctx) {
    SceUsbdDeviceHandle **dev_handle = ctx->rsi ? (SceUsbdDeviceHandle **)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceUsbdOpen((SceUsbdDevice *)ctx->rdi, dev_handle);
    SHIM_RETURN();
}

void shim_sceUsbdClose(GuestContext *ctx) {
    sceUsbdClose((SceUsbdDeviceHandle *)ctx->rdi);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceUsbdGetDeviceList(GuestContext *ctx) {
    SceUsbdDevice ***list = ctx->rdi ? (SceUsbdDevice ***)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)sceUsbdGetDeviceList(list);
    SHIM_RETURN();
}

void shim_sceUsbdFreeDeviceList(GuestContext *ctx) {
    sceUsbdFreeDeviceList((SceUsbdDevice **)ctx->rdi, (int32_t)ctx->rsi);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceUsbdHandleEventsTimeout(GuestContext *ctx) {
    void *tv = ctx->rdi ? (void *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceUsbdHandleEventsTimeout(tv);
    SHIM_RETURN();
}

void shim_sceUsbdFreeTransfer(GuestContext *ctx) {
    sceUsbdFreeTransfer((SceUsbdTransfer *)ctx->rdi);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceUsbdAllocTransfer(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceUsbdAllocTransfer((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceUsbdFillControlTransfer(GuestContext *ctx) {
    uint8_t *buffer = ctx->rdx ? (uint8_t *)(ctx->mem_base + ctx->rdx) : NULL;
    sceUsbdFillControlTransfer((SceUsbdTransfer *)ctx->rdi, (SceUsbdDeviceHandle *)ctx->rsi, buffer, (SceUsbdTransferCallback)ctx->rcx, (void *)ctx->r8, (uint32_t)ctx->r9);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceUsbdFillControlSetup(GuestContext *ctx) {
    uint8_t *buf = ctx->rdi ? (uint8_t *)(ctx->mem_base + ctx->rdi) : NULL;
    sceUsbdFillControlSetup(buf, (uint8_t)ctx->rsi, (uint8_t)ctx->rdx, (uint16_t)ctx->rcx, (uint16_t)ctx->r8, (uint16_t)ctx->r9);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceUsbdClaimInterface(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUsbdClaimInterface((SceUsbdDeviceHandle *)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceUsbdAttachKernelDriver(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUsbdAttachKernelDriver((SceUsbdDeviceHandle *)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceUsbdGetConfigDescriptor(GuestContext *ctx) {
    SceUsbdConfigDescriptor **config = ctx->rdx ? (SceUsbdConfigDescriptor **)(ctx->mem_base + ctx->rdx) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceUsbdGetConfigDescriptor((SceUsbdDevice *)ctx->rdi, (uint8_t)ctx->rsi, config);
    SHIM_RETURN();
}

void shim_sceUsbdSetConfiguration(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUsbdSetConfiguration((SceUsbdDeviceHandle *)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceUsbdGetDeviceAddress(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceUsbdGetDeviceAddress((SceUsbdDevice *)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceUsbdFreeConfigDescriptor(GuestContext *ctx) {
    sceUsbdFreeConfigDescriptor((SceUsbdConfigDescriptor *)ctx->rdi);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceUsbdSubmitTransfer(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUsbdSubmitTransfer((SceUsbdTransfer *)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceUsbdCheckConnected(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUsbdCheckConnected((SceUsbdDeviceHandle *)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceUsbdReleaseInterface(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUsbdReleaseInterface((SceUsbdDeviceHandle *)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceUsbdKernelDriverActive(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUsbdKernelDriverActive((SceUsbdDeviceHandle *)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceUsbdControlTransfer(GuestContext *ctx) {
    int32_t length = (int32_t)*(uint64_t *)(ctx->rsp + 8);
    uint32_t timeout = (uint32_t)*(uint64_t *)(ctx->rsp + 16);
    uint8_t *data = ctx->r9 ? (uint8_t *)(ctx->mem_base + ctx->r9) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceUsbdControlTransfer((SceUsbdDeviceHandle *)ctx->rdi, (uint8_t)ctx->rsi, (uint8_t)ctx->rdx, (uint16_t)ctx->rcx, (uint16_t)ctx->r8, data, length, timeout);
    SHIM_RETURN();
}

void shim_sceUsbdControlTransferGetData(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceUsbdControlTransferGetData((SceUsbdTransfer *)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceUsbdGetDeviceDescriptor(GuestContext *ctx) {
    SceUsbdDeviceDescriptor *desc = ctx->rsi ? (SceUsbdDeviceDescriptor *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceUsbdGetDeviceDescriptor((SceUsbdDevice *)ctx->rdi, desc);
    SHIM_RETURN();
}

void shim_sceUsbdFillInterruptTransfer(GuestContext *ctx) {
    uint8_t *buffer = ctx->rcx ? (uint8_t *)(ctx->mem_base + ctx->rcx) : NULL;
    void *user_data = (void *)*(uint64_t *)(ctx->rsp + 8);
    uint32_t timeout = (uint32_t)*(uint64_t *)(ctx->rsp + 16);
    sceUsbdFillInterruptTransfer((SceUsbdTransfer *)ctx->rdi, (SceUsbdDeviceHandle *)ctx->rsi, (uint8_t)ctx->rdx, buffer, (int32_t)ctx->r8, (SceUsbdTransferCallback)ctx->r9, user_data, timeout);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceUsbdGetBusNumber(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceUsbdGetBusNumber((SceUsbdDevice *)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceUsbdGetMaxPacketSize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUsbdGetMaxPacketSize((SceUsbdDevice *)ctx->rdi, (uint8_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceUsbdInterruptTransfer(GuestContext *ctx) {
    uint8_t *data = ctx->rdx ? (uint8_t *)(ctx->mem_base + ctx->rdx) : NULL;
    int32_t *transferred = ctx->r8 ? (int32_t *)(ctx->mem_base + ctx->r8) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceUsbdInterruptTransfer((SceUsbdDeviceHandle *)ctx->rdi, (uint8_t)ctx->rsi, data, (int32_t)ctx->rcx, transferred, (uint32_t)ctx->r9);
    SHIM_RETURN();
}
