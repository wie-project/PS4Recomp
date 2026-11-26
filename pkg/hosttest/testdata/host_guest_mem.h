// SPDX-License-Identifier: GPL-2.0-or-later

// Guest memory for host tests that hand the shims host objects (stack variables, globals,
// string literals) instead of a mapped guest region.
//
// The runtime turns a guest address into a host pointer as mem_base + address. A test cannot
// use mem_base = NULL with host pointers as addresses: adding a non-zero offset to NULL is
// undefined behaviour, and clang optimises on it (it folds "mem_base + address == NULL" to
// "mem_base == NULL", so every pointer argument reads as NULL).
//
// Instead mem_base is GUEST_MEM, the Mach-O header of the test binary. On macOS it is the lowest
// address of the program image, and the image's data and literals, the heap and the stacks all
// lie above it, so the guest address of a host object is its distance from GUEST_MEM. NULL stays
// guest address 0.

#ifndef HOST_GUEST_MEM_H
#define HOST_GUEST_MEM_H

#include <mach-o/ldsyms.h>
#include <stdint.h>

#define GUEST_MEM ((uint8_t *)&_mh_execute_header)

// The guest address of the host object at address p (0 for NULL). It takes the address as an
// integer, so handing the shims an out-parameter does not read as passing it to a const pointer.
static inline uint64_t guest_addr(uintptr_t p) {
    return p ? (uint64_t)(p - (uintptr_t)GUEST_MEM) : 0;
}

// The host object at a guest address (NULL for 0).
static inline void *host_ptr(uint64_t addr) {
    return addr ? (void *)(GUEST_MEM + addr) : NULL;
}

#define A(p) guest_addr((uintptr_t)(p))

#endif
