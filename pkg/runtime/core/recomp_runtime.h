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
void recomp_init_tls(GuestContext *ctx, uint64_t tls_vaddr, uint64_t tls_filesz, uint64_t tls_memsz, uint64_t tls_align);
uint64_t recomp_tls_block_offset(const GuestContext *ctx);
void recomp_setup_entry_args(GuestContext *ctx, int argc, char **argv, uint64_t entry_addr);
void recomp_dispatch_external(GuestContext *ctx, uint64_t target);
// True when host pc is in recompiled guest code (the __recomp_guest section: recompiled
// functions, shim stubs, indirect-branch entry points). There the guest registers are live in
// the pinned host registers; in host code (shims, the bridge, helpers) the GuestContext holds
// what the last guest -> host transition stored. Async-signal-safe.
bool recomp_pc_in_guest_code(uint64_t pc);
// Reports guest longjmp / C++ throw (unsupported by the assembly backend) and aborts.
void shim_guest_nonlocal_jump(GuestContext *ctx);
void shim_guest_cxa_throw(GuestContext *ctx);
void recomp_free_runtime(GuestContext *ctx);
GuestContext *recomp_create_thread_context(GuestContext *parent, uint64_t stack_size);
uint64_t recomp_vm_alloc(GuestContext *ctx, size_t size);
uint64_t recomp_vm_alloc_named(GuestContext *ctx, size_t size, int prot, int flags, const char *name);
uint64_t recomp_vm_alloc_named_aligned(GuestContext *ctx, size_t size, size_t alignment, int prot, int flags, const char *name);
uint64_t recomp_vm_alloc_fixed(GuestContext *ctx, uint64_t desired_addr, size_t size, int prot, int flags, const char *name);
int recomp_vm_free(GuestContext *ctx, uint64_t addr, size_t size);
bool recomp_vm_split_extent_at(GuestContext *proc, uint64_t at);
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
void recomp_unsupported_instruction(uint64_t rip, uint64_t fn_entry, const char *inst_str);

// Main thread and TLS context helpers
void recomp_init_main_thread(GuestContext *ctx);
GuestContext *recomp_get_current_ctx(void);
void recomp_set_current_ctx(GuestContext *ctx);

// Subsystem teardown and lifecycle
void recomp_free_thread_context(GuestContext *ctx);
void ps4_direct_mem_destroy(void);
void ps4_event_flag_destroy(void);
void ps4_sync_destroy(void);
void ps4_videoout_destroy(void);
void ps4_equeue_destroy(void);
void ps4_metal_screen_destroy(void);
void ps4_host_run_main_loop(void);

// Ends the process from an exit path (window close, Cmd+Q) without running static
// destructors: game threads and Metal completion handlers still use global state.
void ps4_host_quit(int code) __attribute__((noreturn));
// Startup settings (libSceSystemService/ps4_settings.m): resolved once before the guest starts.
void ps4_host_settings_init(void);
void ps4_stall_watchdog_note_present(void); // frame stall diagnostics (PS4_STALL_WATCHDOG)
int ps4_host_setting_pro_mode(void);
void ps4_keyboard_destroy(void);
void ps4_vfs_destroy(void);
void ps4_aio_destroy(void);
void ps4_gnmdriver_destroy(void);
void ps4_ajm_destroy(void);

// SIMD String helpers: see recomp_context.h

// AES-NI and PCLMULQDQ helpers
xmm_reg_t recomp_pclmulqdq(uint64_t a, uint64_t b);
xmm_reg_t recomp_vaesenc(xmm_reg_t s1, xmm_reg_t s2);
xmm_reg_t recomp_vaesenclast(xmm_reg_t s1, xmm_reg_t s2);
xmm_reg_t recomp_vaesdec(xmm_reg_t s1, xmm_reg_t s2);
xmm_reg_t recomp_vaesdeclast(xmm_reg_t s1, xmm_reg_t s2);
xmm_reg_t recomp_vaesimc(xmm_reg_t s);

#ifdef __cplusplus
}
#endif

#endif // RECOMP_RUNTIME_H
