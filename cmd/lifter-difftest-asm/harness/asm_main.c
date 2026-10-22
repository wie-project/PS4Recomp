// arm64 side: executes the Arm64Lifter's assembly translation with mem_base (x28) = 0.
// Each test function loads the pinned guest registers, v0-v31 and NZCV from a DtState, runs the
// lifted instruction and stores the state back (see writeAsm in ../main.go).
#include "harness.h"
#include "recomp_context.h"
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

/* GuestContext offsets the lifter hard-codes (pkg/lifter: liftMxcsr, mmxOffset, pcmpistri). */
_Static_assert(offsetof(GuestContext, mxcsr) == 0x2e8, "mxcsr offset used by the lifter");
_Static_assert(offsetof(GuestContext, mmx) == 0x2f0, "mmx offset used by the lifter");
_Static_assert(offsetof(GuestContext, fpu_stack) == 0x2a0, "fpu_stack offset used by the lifter");
_Static_assert(offsetof(GuestContext, fpu_top) == 0x2e0, "fpu_top offset used by the lifter");
_Static_assert(offsetof(GuestContext, fpu_cw) == 0x2e4, "fpu_cw offset used by the lifter");
_Static_assert(offsetof(GuestContext, fpu_sw) == 0x2e6, "fpu_sw offset used by the lifter");
_Static_assert(offsetof(GuestContext, zf) == 0x99, "zf offset used by the lifter");
_Static_assert(offsetof(GuestContext, sf) == 0x9a, "sf offset used by the lifter");

extern const uint64_t lt_count;
extern void (*const lt_table[])(DtState *, uint64_t fs_base);
extern const uint64_t lt_regsets[][17];
static sigjmp_buf jb;
static void on_sig(int s) { (void)s; siglongjmp(jb, 1); }

int main(int argc, char **argv) {
    int special = argc > 2 && atoi(argv[2]);
    uint8_t *scr = mmap((void *)DT_SCRATCH_BASE, DT_SCRATCH_SIZE, PROT_READ | PROT_WRITE,
                        MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
    if (scr != (uint8_t *)DT_SCRATCH_BASE) { perror("mmap"); return 1; }
    FILE *out = fopen(argv[1], "wb");
    if (!out) { perror("fopen"); return 1; }
    /* The handler runs on its own stack: a test that corrupts the stack pointer must not take
       the process down (or, under Rosetta 2, make it grow without bound). */
    static uint8_t altstack[1 << 16];
    stack_t ss = {.ss_sp = altstack, .ss_size = sizeof(altstack), .ss_flags = 0};
    sigaltstack(&ss, NULL);
    struct sigaction sa = {0};
    sa.sa_handler = on_sig;
    sa.sa_flags = SA_NODEFER | SA_ONSTACK;
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGTRAP, &sa, NULL);
    static GuestContext ctx;
    for (unsigned t = 0; t < lt_count; t++) {
        for (unsigned k = 0; k < DT_TRIALS; k++) {
            DtResult res;
            memset(&res, 0, sizeof(res));
            memset(scr, 0, DT_SCRATCH_SIZE);
            dt_make_state(&res.st, scr, t, k, special, lt_regsets[t]);
            /* Fake thread control block: lowerings that need the GuestContext load it from
               [x28 + x27 + 0x70]. */
            memset(&ctx, 0, sizeof(ctx));
            ctx.mxcsr = res.st.mxcsr;
            ctx.fpu_cw = res.st.fcw;
            ctx.fpu_top = (int)res.st.ftop;
            for (int i = 0; i < 8; i++) ctx.fpu_stack[(res.st.ftop + i) & 7] = res.st.st[i];
            GuestContext *cp = &ctx;
            memcpy(scr + (DT_FAKE_TCB - DT_SCRATCH_BASE) + 0x70, &cp, sizeof(cp));
            static volatile DtState st;
            memcpy((void *)&st, &res.st, sizeof(st));
            if (sigsetjmp(jb, 1) == 0) {
                lt_table[t]((DtState *)&st, DT_FAKE_TCB);
                memcpy(&res.st, (void *)&st, sizeof(st));
                res.st.mxcsr = ctx.mxcsr;
                /* x87: the lifter keeps TOP and the eight registers in the GuestContext and
                   inserts TOP into the status word only when it is stored (FNSTSW). */
                unsigned top = (unsigned)ctx.fpu_top & 7;
                res.st.ftop = top;
                for (int i = 0; i < 8; i++) res.st.st[i] = ctx.fpu_stack[(top + i) & 7];
                res.st.fcw = ctx.fpu_cw;
                res.st.fsw = (uint16_t)((ctx.fpu_sw & ~0x3800) | (top << 11));
                res.st.ftag = 0xff;
            } else {
                res.status = 2;
            }
            memcpy(res.mem, scr + (DT_MEM_TARGET - DT_SCRATCH_BASE) - 32, DT_MEM_WINDOW);
            fwrite(&res, sizeof(res), 1, out);
        }
    }
    fclose(out);
    return 0;
}
