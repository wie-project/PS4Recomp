// Guest functions for the guest-state boundary tests (boundary_test.go), written the way the
// assembly emitter lays recompiled code out: guest registers pinned (rax x19, rbx x20, rcx x21,
// rdx x22, rsi x23, rdi x24, rbp x25, rsp x26, r8-r15 x8-x15, FS x27, memory x28), one 16-byte
// host stack slot per active function for x30, x86 calls push a return address on the guest
// stack. The shim stubs (_stub_*) are generated with the emitter's writeShimStub.
// Addresses are the guest data layout of harness.c.
.section __TEXT,__recomp_guest,regular,pure_instructions
.p2align 2

.set D, 0x1000000
.set FIBER_A, D + 0x000
.set FIBER_B, D + 0x100
.set ARGRET1, D + 0x300
.set ARGRET2, D + 0x308
.set ARGRUN_A, D + 0x310
.set ARGRUN_A2, D + 0x318
.set ARGRUN_B, D + 0x320
.set ARGRUN_A3, D + 0x328
.set RES, D + 0x1000            // result block n at RES + n * 0x100 (harness.c: res())
.set RES_M1, RES + 0x000
.set RES_A1, RES + 0x100
.set RES_A2, RES + 0x200
.set RES_B1, RES + 0x300
.set RES_M2, RES + 0x400
.set RES_A_ARGS, RES + 0x500
.set RES_B_ARGS, RES + 0x600
.set RES_A_PRE, RES + 0x700
.set RES_B_PRE, RES + 0x800
.set RES_M1_PRE, RES + 0x900
.set RES_M2_PRE, RES + 0xa00
.set RES_EXC_PRE, RES + 0xb00
.set RES_EXC, RES + 0xc00
.set RES_EXC_HANDLER, RES + 0xd00
.set RES_GPRS_PRE, RES + 0xe00
.set RES_GPRS, RES + 0xf00
.set RES_XMM, RES + 0x1000
.set RES_FPCR, RES + 0x1100
.set RES_FP_M1, RES + 0x1200    // FPCR and ctx->mxcsr after each fiber hand-over (STORE_FP)
.set RES_FP_A1, RES + 0x1210
.set RES_FP_A2, RES + 0x1220
.set RES_FP_B1, RES + 0x1230
.set RES_FP_M2, RES + 0x1240
.set THREAD_HANDLE, D + 0x400
.set THREAD_RET, D + 0x408
.set TLS_INDEX, D + 0x500        // tls_index {module 1, offset 8}
.set ADDR_THREAD_ENTRY, 0x400a00
.set RES_THR_PRE, RES + 0x1300
.set RES_THR_MAIN, RES + 0x1400
.set RES_THR_MAIN_FP, RES + 0x1500
.set RES_THR_CHILD, RES + 0x1600

.macro LI reg, val
    movz \reg, #((\val) & 0xffff)
    movk \reg, #(((\val) >> 16) & 0xffff), lsl #16
.endm

.macro GUEST_FN name
.globl \name
.p2align 2
\name:
    str  x30, [sp, #-16]!
.endm

// x86 RET as the lifter emits it.
.macro GUEST_RET
    ldr  x16, [x28, x26]
    add  x26, x26, #8
    ldr  x30, [sp], #16
    ret
.endm

// x86 CALL of a shim: push a return address, branch to the shim stub.
.macro GUEST_CALL target
    sub  x26, x26, #8
    str  xzr, [x28, x26]
    bl   \target
.endm

// Distinct values in the GPRs a call must not change (System V callee-saved: rbx, rbp,
// r12-r15) and in r8-r11; harness.c check_pattern() knows the layout.
.macro SET_PATTERN p
    movz x20, #(\p + 0x01)
    movz x25, #(\p + 0x02)
    movz x8,  #(\p + 0x08)
    movz x9,  #(\p + 0x09)
    movz x10, #(\p + 0x0a)
    movz x11, #(\p + 0x0b)
    movz x12, #(\p + 0x0c)
    movz x13, #(\p + 0x0d)
    movz x14, #(\p + 0x0e)
    movz x15, #(\p + 0x0f)
.endm

// LDMXCSR as the lifter lowers it: ctx->mxcsr (through the TCB slot) and FPCR RMode/FZ.
.macro SET_MXCSR val
    LI   x0, \val
    add  x17, x27, #0x70
    ldr  x17, [x28, x17]
    str  w0, [x17, #0x2e8]
    mrs  x1, fpcr
    ubfx w2, w0, #14, #1
    bfi  x1, x2, #22, #1
    ubfx w2, w0, #13, #1
    bfi  x1, x2, #23, #1
    ubfx w2, w0, #15, #1
    bfi  x1, x2, #24, #1
    msr  fpcr, x1
.endm

// FPCR and the ctx->mxcsr STMXCSR would read (through the TCB slot) to \addr.
.macro STORE_FP addr
    LI   x16, \addr
    add  x16, x28, x16
    mrs  x0, fpcr
    add  x17, x27, #0x70
    ldr  x17, [x28, x17]
    ldr  w1, [x17, #0x2e8]
    stp  x0, x1, [x16]
.endm

// Result block: rax rbx rcx rdx rsi rdi rbp rsp r8-r15 fs mem (18 words).
.macro STORE_REGS addr
    LI   x16, \addr
    add  x16, x28, x16
    stp  x19, x20, [x16, #0]
    stp  x21, x22, [x16, #16]
    stp  x23, x24, [x16, #32]
    stp  x25, x26, [x16, #48]
    stp  x8,  x9,  [x16, #64]
    stp  x10, x11, [x16, #80]
    stp  x12, x13, [x16, #96]
    stp  x14, x15, [x16, #112]
    stp  x27, x28, [x16, #128]
.endm

// ---- Fibers: thread 1 runs A; A switches to B; B calls a shim and switches back to A; A
// returns to thread 1. Thread 2 runs A again (A resumes on another guest thread), A switches
// to B (B resumes on thread 2), B returns to thread 2.

GUEST_FN _guest_fiber_main1
    SET_PATTERN 0x1100
    SET_MXCSR 0x7f80                // round toward zero
    STORE_REGS RES_M1_PRE
    LI   x24, FIBER_A
    LI   x23, 0x5150
    LI   x22, ARGRET1
    GUEST_CALL _stub_sceFiberRun
    STORE_REGS RES_M1
    STORE_FP RES_FP_M1
    SET_MXCSR 0x1f80
    GUEST_RET

GUEST_FN _guest_fiber_main2
    SET_PATTERN 0x2200
    STORE_REGS RES_M2_PRE
    LI   x24, FIBER_A
    LI   x23, 0x5250
    LI   x22, ARGRET2
    GUEST_CALL _stub_sceFiberRun
    STORE_REGS RES_M2
    STORE_FP RES_FP_M2
    GUEST_RET

GUEST_FN _guest_fiber_a
    STORE_REGS RES_A_ARGS
    SET_PATTERN 0xa000
    SET_MXCSR 0x9f80                // flush to zero, round to nearest: B and thread 2 have FZ 0
    STORE_REGS RES_A_PRE
    LI   x24, FIBER_B
    LI   x23, 0x5151
    LI   x22, ARGRUN_A
    GUEST_CALL _stub_sceFiberSwitch
    STORE_REGS RES_A1
    STORE_FP RES_FP_A1
    LI   x24, 0x77
    LI   x23, ARGRUN_A2
    GUEST_CALL _stub_sceFiberReturnToThread
    STORE_REGS RES_A2
    STORE_FP RES_FP_A2
    LI   x24, FIBER_B
    LI   x23, 0x5252
    LI   x22, ARGRUN_A3
    GUEST_CALL _stub_sceFiberSwitch
    brk  #1

GUEST_FN _guest_fiber_b
    STORE_REGS RES_B_ARGS
    SET_PATTERN 0xb000
    SET_MXCSR 0x5f80                // round up
    STORE_REGS RES_B_PRE
    GUEST_CALL _stub_test_noop
    LI   x24, FIBER_A
    LI   x23, 0x5353
    LI   x22, ARGRUN_B
    GUEST_CALL _stub_sceFiberSwitch
    STORE_REGS RES_B1
    STORE_FP RES_FP_B1
    LI   x24, 0x88
    movz x23, #0
    GUEST_CALL _stub_sceFiberReturnToThread
    brk  #2

// ---- Guest exception handler (SIGUSR1) delivered while the thread is inside a shim.

GUEST_FN _guest_exc_main
    SET_PATTERN 0xe000
    STORE_REGS RES_EXC_PRE
    GUEST_CALL _stub_test_wait_signal
    STORE_REGS RES_EXC
    GUEST_RET

// void handler(int signum, void *ucontext)
GUEST_FN _guest_exc_handler
    STORE_REGS RES_EXC_HANDLER
    GUEST_RET

// ---- Guest exception handler that changes thread state: TLS key 5 = 0x7777
// (pthread_setspecific), a new key with destructor 0x7abc (pthread_key_create, key number to
// EXC_KEY_OUT) and the heap pointer (shim_test_heap_bump). harness.c test_exception_state
// checks that all of it survives the return from the handler.

.set EXC_KEY_OUT, D + 0x6f0

GUEST_FN _guest_exc_state_handler
    movz x24, #5
    movz x23, #0x7777
    GUEST_CALL _stub_pthread_setspecific
    LI   x24, EXC_KEY_OUT
    movz x23, #0x7abc
    GUEST_CALL _stub_pthread_key_create
    GUEST_CALL _stub_test_heap_bump
    movz x24, #30                   // the signal number raise_exception_in_wait waits for
    STORE_REGS RES_EXC_HANDLER
    GUEST_RET

// ---- A shim that sets every GPR (as a longjmp-style shim does).

GUEST_FN _guest_gprs
    SET_PATTERN 0xc000
    movz x19, #0xc019
    movz x21, #0xc021
    movz x22, #0xc022
    movz x23, #0xc023
    movz x24, #0xc024
    STORE_REGS RES_GPRS_PRE
    GUEST_CALL _stub_test_set_gprs
    STORE_REGS RES_GPRS
    GUEST_RET

// ---- Vector arguments and results: xmm0-7 hold distinct bytes; the shim reads them, calls a
// guest callback with a float argument and returns a double.

GUEST_FN _guest_xmm
    movi v0.16b, #0x01
    movi v1.16b, #0x12
    movi v2.16b, #0x23
    movi v3.16b, #0x34
    movi v4.16b, #0x45
    movi v5.16b, #0x56
    movi v6.16b, #0x67
    movi v7.16b, #0x78
    GUEST_CALL _stub_test_xmm
    LI   x16, RES_XMM
    add  x16, x28, x16
    stp  q0, q1, [x16, #0]
    stp  q2, q3, [x16, #32]
    stp  q4, q5, [x16, #64]
    stp  q6, q7, [x16, #96]
    GUEST_RET

// float callback(float x) { return x + x; }
GUEST_FN _guest_xmm_callback
    fadd s0, s0, s0
    GUEST_RET

// ---- FPCR: guest code runs with the FPCR its MXCSR asks for, host code with the default.

GUEST_FN _guest_fpcr
    LI   x16, RES_FPCR
    add  x16, x28, x16
    mrs  x0, fpcr
    str  x0, [x16, #0]              // at entry: from ctx->mxcsr
    SET_MXCSR 0xbf80                // round down, flush to zero
    GUEST_CALL _stub_test_fpcr
    LI   x16, RES_FPCR
    add  x16, x28, x16
    mrs  x0, fpcr
    str  x0, [x16, #8]              // after the shim
    GUEST_RET

// ---- Thread start: pthread_create from guest code, the new thread's FS base, TCB, TCB slot,
// FPCR and static TLS (PT_TLS template, variant II, read FS-relative and via __tls_get_addr).

GUEST_FN _guest_thread_main
    SET_PATTERN 0x7000
    SET_MXCSR 0x3f80                // round down: a new thread starts with the default
    STORE_REGS RES_THR_PRE
    LI   x24, THREAD_HANDLE
    movz x23, #0
    LI   x22, ADDR_THREAD_ENTRY
    movz x21, #0x1234
    GUEST_CALL _stub_pthread_create
    LI   x16, THREAD_HANDLE
    add  x16, x28, x16
    ldr  x24, [x16]
    LI   x23, THREAD_RET
    GUEST_CALL _stub_pthread_join
    STORE_REGS RES_THR_MAIN
    STORE_FP RES_THR_MAIN_FP
    SET_MXCSR 0x1f80
    GUEST_RET

// void *entry(void *arg): records its thread state, returns arg + 1.
// Child block: [0] FS base, [1] fs:[0], [2] TCB slot, [3] FPCR, [4] .tdata word 0 at
// FS - 32, [5] .tbss word at FS - 16, [6] arg, [8] __tls_get_addr({1, 8}), [9] its word.
GUEST_FN _guest_thread_entry
    LI   x16, RES_THR_CHILD
    add  x16, x28, x16
    mrs  x0, fpcr
    ldr  x1, [x28, x27]
    add  x2, x27, #0x70
    ldr  x2, [x28, x2]
    stp  x27, x1, [x16, #0]
    stp  x2, x0, [x16, #16]
    sub  x3, x27, #32               // recomp_tls_block_offset: memsz 32, align 16
    ldr  x4, [x28, x3]
    add  x3, x3, #16
    ldr  x5, [x28, x3]
    stp  x4, x5, [x16, #32]
    movz x6, #0x7777
    str  x6, [x28, x3]              // this thread's .tbss word only
    stp  x24, xzr, [x16, #48]
    LI   x24, TLS_INDEX
    GUEST_CALL _stub___tls_get_addr
    LI   x16, RES_THR_CHILD
    add  x16, x28, x16
    ldr  x4, [x28, x19]
    stp  x19, x4, [x16, #64]
    GUEST_CALL _stub_test_record_ctx
    LI   x16, RES_THR_CHILD
    add  x16, x28, x16
    ldr  x19, [x16, #48]
    add  x19, x19, #1
    GUEST_RET

// ---- A guest thread busy in guest code, for the stall profiler: spins until the flag is set.
.set SPIN_FLAG, D + 0x600

GUEST_FN _guest_spin
    LI   x16, SPIN_FLAG
1:  ldr  x0, [x28, x16]
    cbz  x0, 1b
    GUEST_RET

// ---- A fault in guest code (a load 256 GB past the guest memory base).

GUEST_FN _guest_crash
    SET_PATTERN 0xd000
    movz x16, #0x40, lsl #32
    ldr  x0, [x28, x16]
    GUEST_RET

// ---- Exception handler that checks the interrupted state (shim_test_check_uc).

// void handler(int signum, void *ucontext)
GUEST_FN _guest_uc_check
    GUEST_CALL _stub_test_check_uc
    GUEST_RET

// ---- Exceptions while a fiber runs: thread 1 runs fiber A, which waits in a shim.

GUEST_FN _guest_exc_fiber_main
    SET_PATTERN 0x1500
    STORE_REGS RES_M1_PRE
    LI   x24, FIBER_A
    movz x23, #0
    LI   x22, ARGRET1
    GUEST_CALL _stub_sceFiberRun
    STORE_REGS RES_M1
    GUEST_RET

GUEST_FN _guest_exc_fiber_a
    SET_PATTERN 0xe500
    STORE_REGS RES_A_PRE
    GUEST_CALL _stub_test_wait_signal
    STORE_REGS RES_A1
    movz x24, #0
    movz x23, #0
    GUEST_CALL _stub_sceFiberReturnToThread
    brk  #3

// ---- Shim call cost: 10M calls of a shim that does nothing (harness.c bench-shim).

GUEST_FN _guest_shim_bench
    LI   x20, 10000000
1:  GUEST_CALL _stub_test_noop
    subs x20, x20, #1
    b.ne 1b
    GUEST_RET

// ---- longjmp(env, 1) from guest code.

GUEST_FN _guest_longjmp
    LI   x24, D + 0x700
    movz x23, #1
    GUEST_CALL _stub_longjmp
    GUEST_RET

// ---- A shim sees the guest address of its stub in ctx->rip.

GUEST_FN _guest_rip
    GUEST_CALL _stub_test_rip
    GUEST_RET

// ---- recomp_enter_guest is called from C: it must keep the AAPCS64 callee-saved d8-d15,
// which guest code overwrites (they are the low halves of xmm8-15).

GUEST_FN _guest_clobber_fp
    movi v8.16b, #0xff
    movi v9.16b, #0xff
    movi v10.16b, #0xff
    movi v11.16b, #0xff
    movi v12.16b, #0xff
    movi v13.16b, #0xff
    movi v14.16b, #0xff
    movi v15.16b, #0xff
    GUEST_RET

// Host code. uint64_t host_enter_guest_d8_d15(GuestContext *ctx, uint64_t fn): calls
// recomp_enter_guest(ctx, fn) with d8-d15 = 8.0 .. 15.0; returns the mask of those changed.
.text
.p2align 2
.globl _host_enter_guest_d8_d15
_host_enter_guest_d8_d15:
    stp  x29, x30, [sp, #-80]!
    mov  x29, sp
    stp  d8, d9, [sp, #16]
    stp  d10, d11, [sp, #32]
    stp  d12, d13, [sp, #48]
    stp  d14, d15, [sp, #64]
    fmov d8, #8.0
    fmov d9, #9.0
    fmov d10, #10.0
    fmov d11, #11.0
    fmov d12, #12.0
    fmov d13, #13.0
    fmov d14, #14.0
    fmov d15, #15.0
    bl   _recomp_enter_guest
    mov  x0, #0
.macro CHECK_D n, val
    fmov d16, #\val
    fcmp d\n, d16
    cset x1, ne
    orr  x0, x0, x1, lsl #\n
.endm
    CHECK_D 8, 8.0
    CHECK_D 9, 9.0
    CHECK_D 10, 10.0
    CHECK_D 11, 11.0
    CHECK_D 12, 12.0
    CHECK_D 13, 13.0
    CHECK_D 14, 14.0
    CHECK_D 15, 15.0
    ldp  d8, d9, [sp, #16]
    ldp  d10, d11, [sp, #32]
    ldp  d12, d13, [sp, #48]
    ldp  d14, d15, [sp, #64]
    ldp  x29, x30, [sp], #80
    ret
