package emitter

// trampolineAsm is the ARM64 bridge between the host C runtime and recompiled guest code
// (trampoline_arm64.s). The guest-state contract is documented at _recomp_enter_guest.
const trampolineAsm = `// ARM64 assembly bridge between the host C runtime and recompiled guest code.
.text
.p2align 2

// Stores the 16 guest GPRs to the GuestContext at \c (rax, rcx, rdx, rbx, rsp, rbp, rsi,
// rdi, r8-r15 at offsets 0x00-0x78).
.macro SAVE_GPRS c
    stp  x19, x21, [\c, #0x00]   // rax, rcx
    stp  x22, x20, [\c, #0x10]   // rdx, rbx
    stp  x26, x25, [\c, #0x20]   // rsp, rbp
    stp  x23, x24, [\c, #0x30]   // rsi, rdi
    stp  x8,  x9,  [\c, #0x40]
    stp  x10, x11, [\c, #0x50]
    stp  x12, x13, [\c, #0x60]
    stp  x14, x15, [\c, #0x70]
.endm

// xmm0-7 (the System V vector argument and return registers) to and from ctx->xmm[0-7].
.macro SAVE_XMM0_7 c
    stp  q0, q1, [\c, #0xa0]
    stp  q2, q3, [\c, #0xc0]
    stp  q4, q5, [\c, #0xe0]
    stp  q6, q7, [\c, #0x100]
.endm
.macro LOAD_XMM0_7 c
    ldp  q0, q1, [\c, #0xa0]
    ldp  q2, q3, [\c, #0xc0]
    ldp  q4, q5, [\c, #0xe0]
    ldp  q6, q7, [\c, #0x100]
.endm

// Code with the guest registers live goes to __recomp_guest, host code to __text: a signal
// handler tells them apart by PC (recomp_pc_in_guest_code). Every instruction in __recomp_guest
// runs with the pinned registers holding the guest state; everywhere else ctx holds it (or the
// live registers equal ctx).
.macro GUEST_SECTION
.section __TEXT,__recomp_guest,regular,pure_instructions
.p2align 2
.endm
.macro HOST_SECTION
.text
.p2align 2
.endm

// Points x30 at label \l (in another section than the caller).
.macro RETURN_TO l
    adrp x30, \l@PAGE
    add  x30, x30, \l@PAGEOFF
.endm

// Loads the 16 guest GPRs from the GuestContext at \c.
.macro LOAD_GPRS c
    ldp  x19, x21, [\c, #0x00]
    ldp  x22, x20, [\c, #0x10]
    ldp  x26, x25, [\c, #0x20]
    ldp  x23, x24, [\c, #0x30]
    ldp  x8,  x9,  [\c, #0x40]
    ldp  x10, x11, [\c, #0x50]
    ldp  x12, x13, [\c, #0x60]
    ldp  x14, x15, [\c, #0x70]
.endm

// ---- Guest-state contract ------------------------------------------------------------
//
// While recompiled guest code runs, the x86 state is pinned in host registers:
//   rax x19, rbx x20, rcx x21, rdx x22, rsi x23, rdi x24, rbp x25, rsp x26, r8-r15 x8-x15,
//   FS base x27, guest memory base x28; xmm0-15 (low 128 bits) v0-v15, the upper halves
//   of ymm0-15 v16-v31; the flags in NZCV; MXCSR.RC/FZ in FPCR (RMode, FZ).
// x0-x7, x16 and x17 are scratch, x18 is never touched, x29 is not maintained (guest code
// builds no frame records, so a frame-pointer walk from guest code finds only the host frames
// below recomp_enter_guest; the stall profiler records just the PC there). Each active guest
// function keeps its host return address (x30) in a 16-byte slot on the host stack.
// Code that runs in this state (recompiled functions, shim stubs, the indirect-branch entry
// points below) is in the __TEXT,__recomp_guest section: a host PC there means the pinned
// registers are the guest registers; anywhere else the GuestContext holds what the last
// guest -> host transition stored (recomp_pc_in_guest_code; signal handlers rely on it).
// The rest of the guest state (x87 stack, MMX, MXCSR) lives in the GuestContext of the
// execution context that runs, found through the TCB slot [x28 + x27 + 0x70]. The runtime
// keeps that slot pointing at the context that runs on the guest thread: the thread's own
// GuestContext, or the fiber's while a fiber runs (ps4_fiber.c).
//
// Host -> guest, recomp_enter_guest(ctx, fn): saves the host callee-saved x19-x30 and d8-d15
// (guest code overwrites them: they are the low halves of xmm8-15) and the host FPCR, loads the
// 16 GPRs, FS base, memory base and xmm0-7 from ctx, sets FPCR RMode/FZ from ctx->mxcsr,
// calls fn, and on return stores the 16 GPRs and xmm0-7 back to ctx (a callback's vector
// arguments and result) and restores the host FPCR.
//
// Guest -> host, _recomp_call_shim (x0 = shim, x1 = its guest address; the shim stub branches
// here in place of a guest function): ctx = the TCB slot. Stores the shim's guest address to
// ctx->rip (guest code itself never writes ctx->rip) and the 16 GPRs and xmm0-7 to ctx,
// calls shim(ctx), then reloads from that same ctx (kept in the bridge frame, not looked up
// again) all 16 GPRs, xmm0-7, the FS base and the memory base. Host code runs with
// round to nearest, FZ as the guest set it; the bridge keeps the guest FPCR in its frame and
// restores it on return. A nested callback that changes MXCSR leaves the outer guest's FPCR
// as it was: MXCSR control is callee-saved in System V, so the callback restores it. A shim reads its arguments and returns its results
// through ctx (rax/rdx, xmm0/xmm1) and may set any GPR (rsp included). xmm8-15, the ymm
// upper halves (ctx->ymmh is never written) and the flags are not synced: System V makes
// them caller-saved, so guest code does not expect them to survive a call, and no shim
// reads them.
//
// A shim that switches context (sceFiberRun/Switch/ReturnToThread) switches host stacks
// inside the shim. The bridge frame of a call lives on the host stack of the context that
// made the call, so when that context is resumed, its own frame finishes the call: it
// reloads the guest registers that context stored and its FPCR, and the FS base from
// ctx->fs_base, which the fiber runtime updates when a fiber resumes on another guest
// thread. The runtime must point the TCB slot of a guest thread at the GuestContext of the
// endpoint it switches to, before the switch.
//
// Not supported: guest non-local transfers (longjmp, C++ unwinding) RET to another frame,
// but each guest RET returns through its own host frame; libc's longjmp and __cxa_throw
// are shims that report and abort (shim_guest_nonlocal_jump, shim_guest_cxa_throw).
// Helpers the lifter calls inside one instruction (recomp_vpcmpistri, recomp_vpcmpe_stri,
// recomp_vaeskeygenassist below) keep every vector register.
// --------------------------------------------------------------------------------------

// void recomp_enter_guest(GuestContext *ctx, uint64_t target_fn)
// x0 = ctx, x1 = target_fn
.globl _recomp_enter_guest
// Frame: [sp] x29, x30; [sp+16] x19-x28; [sp+96] d8-d15; [sp+160] ctx, host FPCR.
_recomp_enter_guest:
    stp  x29, x30, [sp, #-176]!
    mov  x29, sp
    stp  x19, x20, [sp, #16]
    stp  x21, x22, [sp, #32]
    stp  x23, x24, [sp, #48]
    stp  x25, x26, [sp, #64]
    stp  x27, x28, [sp, #80]
    stp  d8,  d9,  [sp, #96]
    stp  d10, d11, [sp, #112]
    stp  d12, d13, [sp, #128]
    stp  d14, d15, [sp, #144]
    str  x0,       [sp, #160]

    // Guest FPCR: the host's with RMode and FZ from ctx->mxcsr (as the lifter's LDMXCSR maps
    // them: x86 RC bits 14:13 swapped into RMode bits 23:22, MXCSR.FZ into FPCR.FZ).
    mrs  x2, fpcr
    str  x2, [sp, #168]
    ldr  w3, [x0, #0x2e8]        // mxcsr
    and  x4, x2, #0xfffffffffe3fffff
    ubfx w5, w3, #14, #1
    bfi  x4, x5, #22, #1
    ubfx w5, w3, #13, #1
    bfi  x4, x5, #23, #1
    ubfx w5, w3, #15, #1
    bfi  x4, x5, #24, #1
    cmp  x4, x2
    b.eq 1f
    msr  fpcr, x4
1:
    LOAD_GPRS x0
    LOAD_XMM0_7 x0
    ldr  x27, [x0, #0x88]        // fs_base
    ldr  x28, [x0, #0x330]       // mem_base

    // The guest function returns to Lenter_guest_ret (guest section: its registers are live
    // there until they are stored).
    RETURN_TO Lenter_guest_ret
    br   x1

GUEST_SECTION
Lenter_guest_ret:
    ldr  x0, [sp, #160]
    SAVE_GPRS x0
    SAVE_XMM0_7 x0
    b    Lenter_guest_tail

HOST_SECTION
Lenter_guest_tail:
    ldr  x2, [sp, #168]          // host FPCR
    mrs  x3, fpcr
    cmp  x2, x3
    b.eq 2f
    msr  fpcr, x2
2:
    ldp  x19, x20, [sp, #16]
    ldp  x21, x22, [sp, #32]
    ldp  x23, x24, [sp, #48]
    ldp  x25, x26, [sp, #64]
    ldp  x27, x28, [sp, #80]
    ldp  d8,  d9,  [sp, #96]
    ldp  d10, d11, [sp, #112]
    ldp  d12, d13, [sp, #128]
    ldp  d14, d15, [sp, #144]
    ldp  x29, x30, [sp], #176
    ret

// _recomp_call_shim: calls a host C shim from guest code (see the contract above).
// x0 = shim, x1 = guest address of the shimmed function (stored to ctx->rip).
// Frame: [sp] x29, x30; [sp+16] shim, ctx; [sp+32] guest FPCR. The frame record makes host
// backtraces from inside a shim reach the guest function that called it (x30).
// The entry, up to the stores of the guest registers, is in the guest section.
GUEST_SECTION
.globl _recomp_call_shim
_recomp_call_shim:
    stp  x29, x30, [sp, #-48]!
    mov  x29, sp
    add  x2, x27, #0x70
    ldr  x2, [x28, x2]           // ctx: the TCB slot
    stp  x0, x2, [sp, #16]
    str  x1, [x2, #0x80]         // rip
    SAVE_GPRS x2
    SAVE_XMM0_7 x2
    b    Lcall_shim_host

HOST_SECTION
Lcall_shim_host:
    // Host code runs with round to nearest. FZ stays as the guest set it (flush to zero only
    // changes denormal results of host float math), so a guest with the default rounding mode
    // - TLOU's threads run with FTZ|DAZ - costs no FPCR write (msr fpcr serialises: ~12 ns).
    mrs  x3, fpcr
    str  x3, [sp, #32]
    tst  x3, #0xc00000           // RMode
    b.eq 1f
    and  x4, x3, #0xffffffffff3fffff
    msr  fpcr, x4
1:

    mov  x0, x2
    ldr  x3, [sp, #16]
    blr  x3

    // The ctx this call stored into. A context switch inside the shim may have changed the
    // TCB slot, and x27 may name another guest thread's TCB by now.
    ldr  x1, [sp, #24]
    // The guest FPCR of this call (the frame is the caller's). A context switch inside the
    // shim may have run endpoints with another FZ on this host thread, so compare.
    ldr  x2, [sp, #32]
    mrs  x3, fpcr
    cmp  x2, x3
    b.eq 2f
    msr  fpcr, x2
2:
    LOAD_GPRS x1
    LOAD_XMM0_7 x1
    ldr  x27, [x1, #0x88]        // fs_base
    ldr  x28, [x1, #0x330]       // mem_base
    ldp  x29, x30, [sp], #48
    ret

// ---- Helpers called in the middle of a guest instruction ----------------------------
// The lifter calls these with bl and keeps x8-x15 itself; every other guest register the
// instruction does not write must survive, but C code clobbers v0-v7, v16-v31, the upper
// halves of v8-v15 and NZCV. These entry points keep all 32 vector registers (and
// _recomp_vaeskeygenassist NZCV, which the lifter does not set after it). Their guest-section
// part stores the GPRs to ctx (the TCB slot), so ctx holds the guest registers while the C
// implementation runs; the host part reloads r8-r15 before returning, so every GPR is live
// again when execution is back in guest code.

// ctx (the TCB slot) into x16, then the 16 GPRs to it.
.macro HELPER_SAVE_GPRS
    add  x16, x27, #0x70
    ldr  x16, [x28, x16]
    SAVE_GPRS x16
.endm
.macro HELPER_LOAD_R8_R15
    add  x16, x27, #0x70
    ldr  x16, [x28, x16]
    ldp  x8,  x9,  [x16, #0x40]
    ldp  x10, x11, [x16, #0x50]
    ldp  x12, x13, [x16, #0x60]
    ldp  x14, x15, [x16, #0x70]
.endm

.macro SAVE_Q0_31
    sub  sp, sp, #512
    stp  q0,  q1,  [sp, #0]
    stp  q2,  q3,  [sp, #32]
    stp  q4,  q5,  [sp, #64]
    stp  q6,  q7,  [sp, #96]
    stp  q8,  q9,  [sp, #128]
    stp  q10, q11, [sp, #160]
    stp  q12, q13, [sp, #192]
    stp  q14, q15, [sp, #224]
    stp  q16, q17, [sp, #256]
    stp  q18, q19, [sp, #288]
    stp  q20, q21, [sp, #320]
    stp  q22, q23, [sp, #352]
    stp  q24, q25, [sp, #384]
    stp  q26, q27, [sp, #416]
    stp  q28, q29, [sp, #448]
    stp  q30, q31, [sp, #480]
.endm
.macro LOAD_Q0_31
    ldp  q0,  q1,  [sp, #0]
    ldp  q2,  q3,  [sp, #32]
    ldp  q4,  q5,  [sp, #64]
    ldp  q6,  q7,  [sp, #96]
    ldp  q8,  q9,  [sp, #128]
    ldp  q10, q11, [sp, #160]
    ldp  q12, q13, [sp, #192]
    ldp  q14, q15, [sp, #224]
    ldp  q16, q17, [sp, #256]
    ldp  q18, q19, [sp, #288]
    ldp  q20, q21, [sp, #320]
    ldp  q22, q23, [sp, #352]
    ldp  q24, q25, [sp, #384]
    ldp  q26, q27, [sp, #416]
    ldp  q28, q29, [sp, #448]
    ldp  q30, q31, [sp, #480]
    add  sp, sp, #512
.endm

// (V)PCMPISTRI / (V)PCMPESTRI: C signature (ctx, src2, src1, imm8); results in ctx (rcx,
// flags), which the lifter reads back.
GUEST_SECTION
.globl _recomp_vpcmpistri
_recomp_vpcmpistri:
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    HELPER_SAVE_GPRS
    b    Lvpcmpistri_host
.globl _recomp_vpcmpe_stri
_recomp_vpcmpe_stri:
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    HELPER_SAVE_GPRS
    b    Lvpcmpe_stri_host

HOST_SECTION
Lvpcmpistri_host:
    SAVE_Q0_31
    bl   _recomp_vpcmpistri_impl
    LOAD_Q0_31
    HELPER_LOAD_R8_R15
    ldp  x29, x30, [sp], #16
    ret
Lvpcmpe_stri_host:
    SAVE_Q0_31
    bl   _recomp_vpcmpe_stri_impl
    LOAD_Q0_31
    HELPER_LOAD_R8_R15
    ldp  x29, x30, [sp], #16
    ret

// (V)AESKEYGENASSIST as the lifter calls it: v0 = source, w0 = round constant; result in v0.
// recomp_vaeskeygenassist_impl(xmm_reg_t s, uint8_t rcon) takes s in x0:x1 and rcon in w2
// and returns in x0:x1 (AAPCS64, a 16-byte non-HFA union).
GUEST_SECTION
.globl _recomp_vaeskeygenassist
_recomp_vaeskeygenassist:
    stp  x29, x30, [sp, #-32]!
    mov  x29, sp
    mrs  x16, nzcv
    str  x16, [sp, #16]
    HELPER_SAVE_GPRS
    b    Lvaeskeygenassist_host

HOST_SECTION
Lvaeskeygenassist_host:
    SAVE_Q0_31
    and  w2, w0, #0xff
    fmov x0, d0
    mov  x1, v0.d[1]
    bl   _recomp_vaeskeygenassist_impl
    LOAD_Q0_31
    fmov d0, x0
    mov  v0.d[1], x1
    HELPER_LOAD_R8_R15
    ldr  x16, [sp, #16]
    msr  nzcv, x16
    ldp  x29, x30, [sp], #32
    ret

// Slow path of _recomp_indirect_call/_recomp_indirect_jump: the target is not in the
// dispatch table; recomp_dispatch_external reports it. x0 = guest target RIP.
.macro DISPATCH_EXTERNAL
    stp  x29, x30, [sp, #-32]!
    mov  x29, sp
    add  x2, x27, #0x70
    ldr  x2, [x28, x2]
    stp  x0, x2, [sp, #16]
    cbz  x2, 98f
    SAVE_GPRS x2
98:
    ldr  x1, [sp, #16]
    mov  x0, x2
    bl   _recomp_dispatch_external
    ldr  x0, [sp, #24]
    cbz  x0, 97f
    LOAD_GPRS x0
    ldr  x27, [x0, #0x88]        // fs_base
    ldr  x28, [x0, #0x330]       // mem_base
97:
    ldp  x29, x30, [sp], #32
    ret
.endm

// Three-level dispatch table lookup of the guest RIP in x0; branches to the recompiled
// function, or falls through to 99 when the target is unknown.
.macro DISPATCH_LOOKUP
    adrp x16, _g_dispatch_l1@PAGE
    add  x16, x16, _g_dispatch_l1@PAGEOFF
    lsr  x17, x0, #32
    and  x17, x17, #0xffff
    ldr  x16, [x16, x17, lsl #3]
    cbz  x16, 99f
    lsr  x17, x0, #16
    and  x17, x17, #0xffff
    ldr  x16, [x16, x17, lsl #3]
    cbz  x16, 99f
    and  x17, x0, #0xffff
    ldr  x16, [x16, x17, lsl #3]
    cbz  x16, 99f
    br   x16
.endm

// The indirect-branch entry points run with the guest registers live: guest code section.
GUEST_SECTION

// _recomp_indirect_call: calls a guest function via dispatch. x0 = guest target RIP.
.globl _recomp_indirect_call
_recomp_indirect_call:
    DISPATCH_LOOKUP
99:
    DISPATCH_EXTERNAL

// _recomp_indirect_jump: jumps to a guest function via dispatch. x0 = guest target RIP.
.globl _recomp_indirect_jump
_recomp_indirect_jump:
    DISPATCH_LOOKUP
99:
    DISPATCH_EXTERNAL
`
