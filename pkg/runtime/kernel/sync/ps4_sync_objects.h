// Guest-visible objects behind ScePthreadMutex, ScePthreadCond and ScePthreadRwlock handles.
//
// On Orbis such a variable holds a pointer to an object in the process's memory, and game
// libraries write into it: FreeBSD libthr's struct pthread_mutex starts with a 32-byte
// struct umutex followed by m_flags (the type in the low byte, PMUTEX_FLAG_PRIVATE = 0x100),
// and inFAMOUS Second Son's libc malloc sets that flag right after scePthreadMutexInit. So
// every sync object is a zeroed SYNC_OBJ_STRIDE-byte slot of an arena in guest memory, and the
// handle is the slot's guest address. The lock state lives in the host tables of ps4_sync.c,
// indexed by slot.
#ifndef PS4_SYNC_OBJECTS_H
#define PS4_SYNC_OBJECTS_H

#include <stdint.h>
#include "recomp_runtime.h"

#define SYNC_OBJ_STRIDE 0x80ULL
#define SYNC_OBJ_SLOTS (1ULL << 20)
#define SYNC_OBJ_MUTEX_FLAGS 0x20 // libthr struct pthread_mutex: m_flags

// A zeroed object: its guest address, or 0 when the arena is full or cannot be mapped.
uint64_t sync_obj_alloc(GuestContext *ctx);
// The slot of the live object at guest address v, or -1 when v is not one.
int64_t sync_obj_slot(uint64_t v);
// The slot of v when it lies in the arena on a SYNC_OBJ_STRIDE boundary, else -1; does not check
// that the slot is live. For values sync_obj_slot already validated (sync_handle's result).
int64_t sync_obj_index(uint64_t v);
// Frees the live object at v; anything else (0, a freed slot, another value) is ignored.
void sync_obj_free(uint64_t v);
// Spreads object addresses over hash buckets (the low bits of an address are all zero).
static inline uint64_t sync_obj_hash(uint64_t v) { return v / SYNC_OBJ_STRIDE; }

#endif
