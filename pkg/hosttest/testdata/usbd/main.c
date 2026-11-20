// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_usbd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

GuestContext *recomp_get_current_ctx(void) {
    return NULL;
}

uint64_t recomp_vm_alloc_named(GuestContext *ctx, size_t size, int prot, int flags, const char *name) {
    (void)ctx; (void)size; (void)prot; (void)flags; (void)name;
    return 0;
}

int recomp_vm_free(GuestContext *ctx, uint64_t addr, size_t size) {
    (void)ctx; (void)addr; (void)size;
    return 0;
}

static void test_init_exit(void) {
    int rc = sceUsbdInit();
    assert(rc == ORBIS_USBD_SUCCESS);
    sceUsbdExit();
    printf("PASS init-exit\n");
}

static void test_device_list(void) {
    SceUsbdDevice **list = (SceUsbdDevice **)(uintptr_t)0x1234;
    int64_t count = sceUsbdGetDeviceList(&list);
    assert(count == 0);
    assert(list == NULL);

    int64_t err = sceUsbdGetDeviceList(NULL);
    assert(err == (int64_t)ORBIS_USBD_ERROR_INVALID_PARAM);

    sceUsbdFreeDeviceList(NULL, 0);
    printf("PASS device-list\n");
}

static void test_device_ops(void) {
    SceUsbdDeviceHandle *handle = (SceUsbdDeviceHandle *)(uintptr_t)0x1234;
    int rc = sceUsbdOpen(NULL, &handle);
    assert(rc == ORBIS_USBD_ERROR_NO_DEVICE);
    assert(handle == NULL);

    rc = sceUsbdCheckConnected(NULL);
    assert(rc == ORBIS_USBD_ERROR_NO_DEVICE);

    rc = sceUsbdClaimInterface(NULL, 0);
    assert(rc == ORBIS_USBD_ERROR_NO_DEVICE);

    rc = sceUsbdReleaseInterface(NULL, 0);
    assert(rc == ORBIS_USBD_ERROR_NO_DEVICE);

    rc = sceUsbdAttachKernelDriver(NULL, 0);
    assert(rc == ORBIS_USBD_ERROR_NO_DEVICE);

    rc = sceUsbdKernelDriverActive(NULL, 0);
    assert(rc == 0);

    rc = sceUsbdSetConfiguration(NULL, 1);
    assert(rc == ORBIS_USBD_ERROR_NO_DEVICE);

    rc = sceUsbdGetDeviceDescriptor(NULL, NULL);
    assert(rc == ORBIS_USBD_ERROR_INVALID_PARAM);

    SceUsbdDeviceDescriptor desc;
    rc = sceUsbdGetDeviceDescriptor(NULL, &desc);
    assert(rc == ORBIS_USBD_ERROR_NO_DEVICE);

    rc = sceUsbdGetMaxPacketSize(NULL, 0);
    assert(rc == ORBIS_USBD_ERROR_NO_DEVICE);

    sceUsbdClose(NULL);
    printf("PASS device-ops\n");
}

static void test_control_transfer(void) {
    SceUsbdTransfer *t = sceUsbdAllocTransfer(0);
    assert(t != NULL);
    assert(t->num_iso_packets == 0);

    uint8_t buffer[64] = {0};
    sceUsbdFillControlSetup(buffer, 0x80, 0x06, 0x0100, 0x0000, 18);
    SceUsbdControlSetup *setup = (SceUsbdControlSetup *)buffer;
    assert(setup->bmRequestType == 0x80);
    assert(setup->bRequest == 0x06);
    assert(setup->wValue == 0x0100);
    assert(setup->wIndex == 0x0000);
    assert(setup->wLength == 18);

    sceUsbdFillControlTransfer(t, NULL, buffer, NULL, (void *)(uintptr_t)0xdeadbeef, 1000);
    assert(t->dev_handle == NULL);
    assert(t->endpoint == 0);
    assert(t->type == LIBUSB_TRANSFER_TYPE_CONTROL);
    assert(t->timeout == 1000);
    assert(t->buffer == buffer);
    assert(t->length == 8 + 18);
    assert(t->user_data == (void *)(uintptr_t)0xdeadbeef);

    uint8_t *data = sceUsbdControlTransferGetData(t);
    assert(data == buffer + 8);

    int rc = sceUsbdSubmitTransfer(t);
    assert(rc == ORBIS_USBD_ERROR_NO_DEVICE);

    sceUsbdFreeTransfer(t);
    printf("PASS control-transfer\n");
}

static void test_interrupt_transfer(void) {
    SceUsbdTransfer *t = sceUsbdAllocTransfer(0);
    assert(t != NULL);

    uint8_t buf[32];
    sceUsbdFillInterruptTransfer(t, NULL, 0x81, buf, 32, NULL, (void *)(uintptr_t)0xcafebabe, 500);
    assert(t->endpoint == 0x81);
    assert(t->type == LIBUSB_TRANSFER_TYPE_INTERRUPT);
    assert(t->timeout == 500);
    assert(t->buffer == buf);
    assert(t->length == 32);
    assert(t->user_data == (void *)(uintptr_t)0xcafebabe);

    sceUsbdFreeTransfer(t);
    printf("PASS interrupt-transfer\n");
}

static void test_handle_events(void) {
    // Null timeout: sleeps default 10ms
    int rc = sceUsbdHandleEventsTimeout(NULL);
    assert(rc == ORBIS_USBD_SUCCESS);

    // Negative timeout: error
    int64_t bad_tv[2] = {-1, 0};
    rc = sceUsbdHandleEventsTimeout(bad_tv);
    assert(rc == (int)ORBIS_USBD_ERROR_INVALID_PARAM);

    // Zero timeout: clamps to 1ms without busy-spinning
    int64_t zero_tv[2] = {0, 0};
    rc = sceUsbdHandleEventsTimeout(zero_tv);
    assert(rc == ORBIS_USBD_SUCCESS);

    // 1ms timeout
    int64_t tv[2] = {0, 1000};
    rc = sceUsbdHandleEventsTimeout(tv);
    assert(rc == ORBIS_USBD_SUCCESS);

    // Shim test with guest memory translation
    _Alignas(uint64_t) uint8_t guest_mem[64] = {0};
    GuestContext ctx = {0};
    ctx.mem_base = guest_mem;
    ctx.rdi = 16; // offset in guest_mem
    int64_t *guest_tv = (int64_t *)(guest_mem + ctx.rdi);
    guest_tv[0] = 0;
    guest_tv[1] = 1000;
    shim_sceUsbdHandleEventsTimeout(&ctx);
    assert(ctx.rax == 0);
    assert(ctx.rsp == 8);

    printf("PASS handle-events\n");
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *name = argv[1];
    if (!strcmp(name, "init-exit")) test_init_exit();
    else if (!strcmp(name, "device-list")) test_device_list();
    else if (!strcmp(name, "device-ops")) test_device_ops();
    else if (!strcmp(name, "control-transfer")) test_control_transfer();
    else if (!strcmp(name, "interrupt-transfer")) test_interrupt_transfer();
    else if (!strcmp(name, "handle-events")) test_handle_events();
    else {
        fprintf(stderr, "Unknown test: %s\n", name);
        return 2;
    }
    return 0;
}
