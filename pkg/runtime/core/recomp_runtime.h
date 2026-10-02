// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RECOMP_RUNTIME_H
#define RECOMP_RUNTIME_H

#include "recomp_context.h"

#ifdef __cplusplus
extern "C" {
#endif

// Runtime initialization and memory layout
GuestContext *recomp_init_runtime(size_t guest_mem_sz, const uint8_t *elf_image, size_t image_size, const char *prog_name);
GuestContext *recomp_init_runtime_file(const char *image_filename, size_t requested_mem_sz, const char *prog_name);
void recomp_init_process_param(GuestContext *ctx, uint64_t proc_param_addr);
void recomp_setup_entry_args(GuestContext *ctx, int argc, char **argv, uint64_t entry_addr);
void recomp_free_runtime(GuestContext *ctx);
GuestContext *recomp_create_thread_context(GuestContext *parent, uint64_t stack_size);
uint64_t recomp_vm_alloc(GuestContext *ctx, size_t size);
uint64_t recomp_vm_alloc_named(GuestContext *ctx, size_t size, int prot, int flags, const char *name);
uint64_t recomp_vm_alloc_named_aligned(GuestContext *ctx, size_t size, size_t alignment, int prot, int flags, const char *name);
uint64_t recomp_vm_alloc_fixed(GuestContext *ctx, uint64_t desired_addr, size_t size, int prot, int flags, const char *name);
int recomp_vm_free(GuestContext *ctx, uint64_t addr, size_t size);
GuestVMExtent *recomp_vm_find(GuestContext *ctx, uint64_t addr);

// Unresolved Symbol Diagnostics & Core Stubs
typedef struct RecompUnresolvedSym {
    uint64_t addr;
    char *nid;
    char *sym_name;
    char *lib_name;
    struct RecompUnresolvedSym *next;
} RecompUnresolvedSym;

void recomp_register_unresolved(uint64_t addr, const char *nid, const char *sym_name, const char *lib_name);
const RecompUnresolvedSym *recomp_lookup_unresolved(uint64_t addr);
void recomp_unimplemented_shim(GuestContext *ctx, const char *shim_name);
void shim_unresolved_stub(GuestContext *ctx);

// Main thread initialization
void recomp_init_main_thread(GuestContext *ctx);

// Subsystem teardown and lifecycle
void recomp_free_thread_context(GuestContext *ctx);
void ps4_direct_mem_destroy(void);
void ps4_event_flag_destroy(void);
void ps4_sync_destroy(void);
void ps4_videoout_destroy(void);
void ps4_equeue_destroy(void);
void ps4_metal_screen_destroy(void);
void ps4_keyboard_destroy(void);
void ps4_vfs_destroy(void);
void ps4_aio_destroy(void);

// SIMD String helpers
void recomp_vpcmpistri(GuestContext *ctx, const void *src2_ptr, const void *src1_ptr, uint8_t imm8);

// AES-NI and PCLMULQDQ helpers
xmm_reg_t recomp_pclmulqdq(uint64_t a, uint64_t b);
xmm_reg_t recomp_vaesenc(xmm_reg_t s1, xmm_reg_t s2);
xmm_reg_t recomp_vaesenclast(xmm_reg_t s1, xmm_reg_t s2);
xmm_reg_t recomp_vaesdec(xmm_reg_t s1, xmm_reg_t s2);
xmm_reg_t recomp_vaesdeclast(xmm_reg_t s1, xmm_reg_t s2);
xmm_reg_t recomp_vaesimc(xmm_reg_t s);
xmm_reg_t recomp_vaeskeygenassist(xmm_reg_t s, uint8_t rcon);

#ifdef __cplusplus
}
#endif

#endif // RECOMP_RUNTIME_H
