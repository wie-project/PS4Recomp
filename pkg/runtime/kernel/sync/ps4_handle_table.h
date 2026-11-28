// SPDX-License-Identifier: GPL-2.0-or-later
//
// Handle tables of the kernel synchronization objects (POSIX and Orbis semaphores, event flags):
// the guest holds a small integer id, the runtime a refcounted host object. A thread that looks
// an object up holds a reference until it is done, so deleting an object another thread waits
// on frees it only when that thread has left (shadPS4 frees it at once).
#ifndef PS4_HANDLE_TABLE_H
#define PS4_HANDLE_TABLE_H

#include <os/lock.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Ps4HandleObject {
    int32_t refs; // guarded by the table lock
    void (*destroy)(struct Ps4HandleObject *obj);
} Ps4HandleObject;

typedef struct Ps4HandleTable {
    os_unfair_lock lock;
    uint32_t max;  // ids are 1..max
    uint32_t hint; // where the next free-slot search starts
    Ps4HandleObject **slots;
} Ps4HandleTable;

#define PS4_HANDLE_TABLE_INIT(max_ids) {OS_UNFAIR_LOCK_INIT, (max_ids), 0, NULL}

// Stores obj (whose destroy is set) with one reference held by the table; 0 when the table is
// full or cannot be allocated.
uint32_t ps4_handle_insert(Ps4HandleTable *t, Ps4HandleObject *obj);
// The object of id with a new reference, or NULL.
Ps4HandleObject *ps4_handle_get(Ps4HandleTable *t, uint32_t id);
// Takes id out of the table and hands its table reference to the caller; NULL for an unknown id.
Ps4HandleObject *ps4_handle_remove(Ps4HandleTable *t, uint32_t id);
// Drops a reference; the last one destroys the object.
void ps4_handle_put(Ps4HandleTable *t, Ps4HandleObject *obj);

// CLOCK_MONOTONIC in nanoseconds.
static inline uint64_t ps4_mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// Waits on cond until deadline (ps4_mono_ns time) at the latest: false once it has passed.
// macOS condition variables cannot use CLOCK_MONOTONIC, so this waits relative to now.
static inline bool ps4_cond_wait_until(pthread_cond_t *cond, pthread_mutex_t *mutex, uint64_t deadline) {
    const uint64_t now = ps4_mono_ns();
    if (now >= deadline) {
        return false;
    }
    const uint64_t rel = deadline - now;
    const struct timespec ts = {.tv_sec = (time_t)(rel / 1000000000ull), .tv_nsec = (long)(rel % 1000000000ull)};
    pthread_cond_timedwait_relative_np(cond, mutex, &ts);
    return ps4_mono_ns() < deadline;
}

#ifdef __cplusplus
}
#endif

#endif
