// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_equeue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_create_delete(void) {
    OrbisKernelEqueue eq = 0;
    int rc = sceKernelCreateEqueue(&eq, "test_eq");
    assert(rc == 0);
    assert(eq > 0);
    rc = sceKernelDeleteEqueue(eq);
    assert(rc == 0);
    printf("PASS create-delete\n");
}

static void test_timer_event(void) {
    OrbisKernelEqueue eq = 0;
    int rc = sceKernelCreateEqueue(&eq, "test_timer");
    assert(rc == 0);

    // 10ms timer
    rc = sceKernelAddTimerEvent(eq, 42, 10000, (void *)(uintptr_t)0xdeadbeef);
    assert(rc == 0);

    OrbisKernelEvent ev = {0};
    int count = 0;
    uint32_t timeout_us = 200000; // 200ms
    rc = sceKernelWaitEqueue(eq, &ev, 1, &count, &timeout_us);
    assert(rc == 0);
    assert(count == 1);
    assert(ev.ident == 42);
    assert(ev.filter == ORBIS_KERNEL_EVFILT_TIMER);
    assert(ev.udata == (void *)(uintptr_t)0xdeadbeef);

    sceKernelDeleteEqueue(eq);
    printf("PASS timer-event\n");
}

static void test_hrtimer_event(void) {
    OrbisKernelEqueue eq = 0;
    int rc = sceKernelCreateEqueue(&eq, "test_hrtimer");
    assert(rc == 0);

    // 10ms high-resolution timer
    SceKernelTimespec ts = {.tv_sec = 0, .tv_nsec = 10000000};
    rc = sceKernelAddHRTimerEvent(eq, 99, &ts, (void *)(uintptr_t)0xcafebabe);
    assert(rc == 0);

    OrbisKernelEvent ev = {0};
    int count = 0;
    uint32_t timeout_us = 200000;
    rc = sceKernelWaitEqueue(eq, &ev, 1, &count, &timeout_us);
    assert(rc == 0);
    assert(count == 1);
    assert(ev.ident == 99);
    assert(ev.filter == ORBIS_KERNEL_EVFILT_HRTIMER);
    assert(ev.udata == (void *)(uintptr_t)0xcafebabe);

    sceKernelDeleteEqueue(eq);
    printf("PASS hrtimer-event\n");
}

static void test_delete_timer(void) {
    OrbisKernelEqueue eq = 0;
    int rc = sceKernelCreateEqueue(&eq, "test_del");
    assert(rc == 0);

    rc = sceKernelAddTimerEvent(eq, 77, 20000, NULL);
    assert(rc == 0);

    rc = sceKernelDeleteTimerEvent(eq, 77);
    assert(rc == 0);

    // Non-existent delete should return ENOENT
    rc = sceKernelDeleteTimerEvent(eq, 77);
    assert(rc == (int)ORBIS_KERNEL_ERROR_ENOENT);

    OrbisKernelEvent ev = {0};
    int count = 0;
    uint32_t timeout_us = 30000; // 30ms
    rc = sceKernelWaitEqueue(eq, &ev, 1, &count, &timeout_us);
    assert(rc == (int)ORBIS_KERNEL_ERROR_ETIMEDOUT);
    assert(count == 0);

    sceKernelDeleteEqueue(eq);
    printf("PASS delete-timer\n");
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    if (strcmp(argv[1], "create-delete") == 0) {
        test_create_delete();
    } else if (strcmp(argv[1], "timer-event") == 0) {
        test_timer_event();
    } else if (strcmp(argv[1], "hrtimer-event") == 0) {
        test_hrtimer_event();
    } else if (strcmp(argv[1], "delete-timer") == 0) {
        test_delete_timer();
    } else {
        return 1;
    }
    return 0;
}
