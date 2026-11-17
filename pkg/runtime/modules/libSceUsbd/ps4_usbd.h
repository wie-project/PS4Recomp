#ifndef PS4_USBD_H
#define PS4_USBD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Error Codes
#define ORBIS_USBD_SUCCESS 0
#define ORBIS_USBD_ERROR_IO ((int32_t)0x80240001)
#define ORBIS_USBD_ERROR_INVALID_PARAM ((int32_t)0x80240002)
#define ORBIS_USBD_ERROR_ACCESS ((int32_t)0x80240003)
#define ORBIS_USBD_ERROR_NO_DEVICE ((int32_t)0x80240004)
#define ORBIS_USBD_ERROR_NOT_FOUND ((int32_t)0x80240005)
#define ORBIS_USBD_ERROR_BUSY ((int32_t)0x80240006)
#define ORBIS_USBD_ERROR_TIMEOUT ((int32_t)0x80240007)
#define ORBIS_USBD_ERROR_OVERFLOW ((int32_t)0x80240008)
#define ORBIS_USBD_ERROR_PIPE ((int32_t)0x80240009)
#define ORBIS_USBD_ERROR_INTERRUPTED ((int32_t)0x8024000A)
#define ORBIS_USBD_ERROR_NO_MEM ((int32_t)0x8024000B)
#define ORBIS_USBD_ERROR_NOT_SUPPORTED ((int32_t)0x8024000C)
#define ORBIS_USBD_ERROR_OTHER ((int32_t)0x802400FF)

// Constants
#define LIBUSB_CONTROL_SETUP_SIZE 8

enum libusb_transfer_type {
    LIBUSB_TRANSFER_TYPE_CONTROL = 0U,
    LIBUSB_TRANSFER_TYPE_ISOCHRONOUS = 1U,
    LIBUSB_TRANSFER_TYPE_BULK = 2U,
    LIBUSB_TRANSFER_TYPE_INTERRUPT = 3U,
    LIBUSB_TRANSFER_TYPE_BULK_STREAM = 4U,
};

enum libusb_transfer_status {
    LIBUSB_TRANSFER_COMPLETED = 0,
    LIBUSB_TRANSFER_ERROR = 1,
    LIBUSB_TRANSFER_TIMED_OUT = 2,
    LIBUSB_TRANSFER_CANCELLED = 3,
    LIBUSB_TRANSFER_STALL = 4,
    LIBUSB_TRANSFER_NO_DEVICE = 5,
    LIBUSB_TRANSFER_OVERFLOW = 6,
};

// Types & Descriptors
typedef struct SceUsbdDevice SceUsbdDevice;
typedef struct SceUsbdDeviceHandle SceUsbdDeviceHandle;

typedef struct __attribute__((packed)) SceUsbdDeviceDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} SceUsbdDeviceDescriptor;

typedef struct __attribute__((packed)) SceUsbdEndpointDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
    uint8_t  bRefresh;
    uint8_t  bSynchAddress;
    const uint8_t *extra;
    int32_t  extra_length;
} SceUsbdEndpointDescriptor;

typedef struct __attribute__((packed)) SceUsbdInterfaceDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
    const SceUsbdEndpointDescriptor *endpoint;
    const uint8_t *extra;
    int32_t  extra_length;
} SceUsbdInterfaceDescriptor;

typedef struct SceUsbdInterface {
    const SceUsbdInterfaceDescriptor *altsetting;
    int32_t num_altsetting;
} SceUsbdInterface;

typedef struct SceUsbdConfigDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  MaxPower;
    const SceUsbdInterface *interface;
    const uint8_t *extra;
    int32_t  extra_length;
} SceUsbdConfigDescriptor;

typedef struct __attribute__((packed)) SceUsbdControlSetup {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} SceUsbdControlSetup;

typedef struct SceUsbdIsoPacketDescriptor {
    uint32_t length;
    uint32_t actual_length;
    int32_t  status;
} SceUsbdIsoPacketDescriptor;

struct SceUsbdTransfer;
typedef void (*SceUsbdTransferCallback)(struct SceUsbdTransfer *transfer);

typedef struct SceUsbdTransfer {
    SceUsbdDeviceHandle *dev_handle;
    uint8_t flags;
    uint8_t endpoint;
    uint8_t type;
    uint32_t timeout;
    int32_t status;
    int32_t length;
    int32_t actual_length;
    SceUsbdTransferCallback callback;
    void *user_data;
    uint8_t *buffer;
    int32_t num_iso_packets;
    SceUsbdIsoPacketDescriptor iso_packet_desc[];
} SceUsbdTransfer;

// C API
int32_t sceUsbdInit(void);
void sceUsbdExit(void);
int64_t sceUsbdGetDeviceList(SceUsbdDevice ***list);
void sceUsbdFreeDeviceList(SceUsbdDevice **list, int32_t unref_devices);

int32_t sceUsbdOpen(SceUsbdDevice *device, SceUsbdDeviceHandle **dev_handle);
void sceUsbdClose(SceUsbdDeviceHandle *dev_handle);
int32_t sceUsbdCheckConnected(SceUsbdDeviceHandle *dev_handle);

uint8_t sceUsbdGetBusNumber(SceUsbdDevice *device);
uint8_t sceUsbdGetDeviceAddress(SceUsbdDevice *device);
int32_t sceUsbdGetDeviceDescriptor(SceUsbdDevice *device, SceUsbdDeviceDescriptor *desc);
int32_t sceUsbdGetConfigDescriptor(SceUsbdDevice *device, uint8_t config_index, SceUsbdConfigDescriptor **config);
void sceUsbdFreeConfigDescriptor(SceUsbdConfigDescriptor *config);

int32_t sceUsbdSetConfiguration(SceUsbdDeviceHandle *dev_handle, int32_t config);
int32_t sceUsbdClaimInterface(SceUsbdDeviceHandle *dev_handle, int32_t interface_number);
int32_t sceUsbdReleaseInterface(SceUsbdDeviceHandle *dev_handle, int32_t interface_number);
int32_t sceUsbdAttachKernelDriver(SceUsbdDeviceHandle *dev_handle, int32_t interface_number);
int32_t sceUsbdKernelDriverActive(SceUsbdDeviceHandle *dev_handle, int32_t interface_number);

int32_t sceUsbdGetMaxPacketSize(SceUsbdDevice *device, uint8_t endpoint);

SceUsbdTransfer *sceUsbdAllocTransfer(int32_t iso_packets);
void sceUsbdFreeTransfer(SceUsbdTransfer *transfer);
int32_t sceUsbdSubmitTransfer(SceUsbdTransfer *transfer);

void sceUsbdFillControlSetup(uint8_t *buf, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength);
void sceUsbdFillControlTransfer(SceUsbdTransfer *transfer, SceUsbdDeviceHandle *dev_handle, uint8_t *buffer, SceUsbdTransferCallback callback, void *user_data, uint32_t timeout);
void sceUsbdFillInterruptTransfer(SceUsbdTransfer *transfer, SceUsbdDeviceHandle *dev_handle, uint8_t endpoint, uint8_t *buffer, int32_t length, SceUsbdTransferCallback callback, void *user_data, uint32_t timeout);
uint8_t *sceUsbdControlTransferGetData(SceUsbdTransfer *transfer);

int32_t sceUsbdControlTransfer(SceUsbdDeviceHandle *dev_handle, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t *data, int32_t length, uint32_t timeout);
int32_t sceUsbdInterruptTransfer(SceUsbdDeviceHandle *dev_handle, uint8_t endpoint, uint8_t *data, int32_t length, int32_t *transferred, uint32_t timeout);

int32_t sceUsbdHandleEventsTimeout(void *tv);

// Shims
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

#ifdef __cplusplus
}
#endif

#endif // PS4_USBD_H
