#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <orbis/libkernel.h>

static OrbisPthreadMutex g_mutex;
static OrbisPthreadCond g_cond;
static OrbisKernelEventFlag g_evf;
static volatile int g_phase = 0;

static void *worker_thread(void *arg) {
    (void)arg;
    printf("[Worker] Thread started, sleeping briefly...\n");
    sceKernelUsleep(50000); // 50ms

    // 1. Signal condvar
    scePthreadMutexLock(&g_mutex);
    g_phase = 1;
    printf("[Worker] Signaling condvar...\n");
    scePthreadCondSignal(&g_cond);
    scePthreadMutexUnlock(&g_mutex);

    sceKernelUsleep(50000); // 50ms

    // 2. Set Event Flag bit 0x02
    printf("[Worker] Setting event flag bit 0x02...\n");
    int32_t rc = sceKernelSetEventFlag(g_evf, 0x02);
    if (rc != 0) {
        printf("[Worker] ERROR: sceKernelSetEventFlag failed: 0x%x\n", rc);
    }

    return NULL;
}

int main(void) {
    printf("[PS4 EventFlag & Sync Test] Starting...\n");

    // Initialize Mutex and Cond
    int rc = scePthreadMutexInit(&g_mutex, NULL, "test_mutex");
    if (rc != 0) {
        printf("ERROR: scePthreadMutexInit failed: %d\n", rc);
        return 1;
    }
    rc = scePthreadCondInit(&g_cond, NULL, "test_cond");
    if (rc != 0) {
        printf("ERROR: scePthreadCondInit failed: %d\n", rc);
        return 1;
    }

    // Initialize Event Flag (initial pattern = 0x01)
    rc = sceKernelCreateEventFlag(&g_evf, "test_evf", 0, 0x01, NULL);
    if (rc != 0) {
        printf("ERROR: sceKernelCreateEventFlag failed: 0x%x\n", rc);
        return 1;
    }
    printf("[Main] Event flag created: id=%p\n", (void *)g_evf);

    // Poll Event Flag for bit 0x01
    uint64_t resPattern = 0;
    rc = sceKernelPollEventFlag(g_evf, 0x01, ORBIS_KERNEL_EVF_WAITMODE_AND, &resPattern);
    if (rc != 0 || (resPattern & 0x01) == 0) {
        printf("ERROR: sceKernelPollEventFlag failed: rc=0x%x res=0x%llx\n", rc, (unsigned long long)resPattern);
        return 1;
    }
    printf("[Main] Poll initial bit 0x01 OK (res=0x%llx)\n", (unsigned long long)resPattern);

    // Spawn worker thread using scePthreadCreate
    OrbisPthread thread;
    rc = scePthreadCreate(&thread, NULL, worker_thread, NULL, "worker");
    if (rc != 0) {
        printf("ERROR: scePthreadCreate failed: %d\n", rc);
        return 1;
    }
    printf("[Main] Worker thread created\n");

    // Wait on Condvar
    scePthreadMutexLock(&g_mutex);
    while (g_phase == 0) {
        scePthreadCondWait(&g_cond, &g_mutex);
    }
    printf("[Main] Condvar woke up, phase=%d\n", g_phase);
    scePthreadMutexUnlock(&g_mutex);

    // Wait on Event Flag for bit 0x02
    printf("[Main] Waiting on event flag bit 0x02...\n");
    OrbisKernelUseconds timeout = 2000000; // 2 seconds
    rc = sceKernelWaitEventFlag(g_evf, 0x02, ORBIS_KERNEL_EVF_WAITMODE_AND, &resPattern, &timeout);
    if (rc != 0) {
        printf("ERROR: sceKernelWaitEventFlag failed: 0x%x\n", rc);
        return 1;
    }
    printf("[Main] Event flag bit 0x02 received! (res=0x%llx)\n", (unsigned long long)resPattern);

    // Join worker thread
    void *ret = NULL;
    scePthreadJoin(thread, &ret);
    printf("[Main] Worker thread joined\n");

    // Clean up
    sceKernelDeleteEventFlag(g_evf);
    scePthreadCondDestroy(&g_cond);
    scePthreadMutexDestroy(&g_mutex);

    printf("[SUCCESS] All PS4 Threading, Mutex, Cond, and EventFlag tests PASSED!\n");
    return 0;
}
