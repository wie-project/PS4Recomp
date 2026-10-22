// x86-64 side (run under Rosetta 2): executes the raw instructions.
#include "harness.h"
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

/* x87 state around a test (AT&T syntax). dt_x87_load(st, k, full): FNINIT, k x FINCSTP, then
   push st[7] (if full), st[6] .. st[0], so ST(i) = st[i] and TOP = (k - 7 - full) & 7, and clear the exception flags that
   loading special values raised (the lifter does not model x87 exception flags). dt_x87_save(fx): FXSAVE into the
   16-byte aligned 512-byte area, then FNINIT. The C code between them and the test stub uses no
   x87 instructions. */
__asm__(".text\n"
        ".p2align 4\n_dt_x87_load:\n"
        "  fninit\n"
        "1:\n  testl %esi, %esi\n  jz 2f\n  fincstp\n  decl %esi\n  jmp 1b\n"
        "2:\n  testl %edx, %edx\n  jz 3f\n  fldl 56(%rdi)\n"
        "3:\n  fldl 48(%rdi)\n  fldl 40(%rdi)\n  fldl 32(%rdi)\n  fldl 24(%rdi)\n"
        "  fldl 16(%rdi)\n  fldl 8(%rdi)\n  fldl 0(%rdi)\n  fnclex\n  ret\n"
        ".p2align 4\n_dt_x87_save:\n  fxsave (%rdi)\n  fninit\n  ret\n");
void dt_x87_load(const double *st, unsigned k, int full);
void dt_x87_save(void *fx);

/* Unpacks an FXSAVE image into DtState: ST(i) as doubles where the tag says valid. */
static void dt_x87_unpack(const uint8_t *fx, DtState *st) {
    uint16_t fcw, fsw;
    memcpy(&fcw, fx, 2);
    memcpy(&fsw, fx + 2, 2);
    unsigned top = (fsw >> 11) & 7, ftw = fx[4];
    st->fcw = fcw;
    st->fsw = fsw;
    st->ftop = top;
    st->ftag = 0;
    for (int i = 0; i < 8; i++) {
        st->st[i] = 0;
        if (ftw >> ((top + i) & 7) & 1) {
            long double v;
            memcpy(&v, fx + 32 + 16 * i, 10);
            st->st[i] = (double)v;
            st->ftag |= 1u << i;
        }
    }
}

extern const uint64_t nt_count;
extern void (*const nt_table[])(DtState *);
extern const uint64_t nt_regsets[][17];
static sigjmp_buf jb;
static volatile int sig_hit;
static void on_sig(int s) { sig_hit = s; siglongjmp(jb, 1); }

int main(int argc, char **argv) {
    int special = argc > 2 && atoi(argv[2]);
    uint8_t *scr = mmap((void *)DT_SCRATCH_BASE, DT_SCRATCH_SIZE, PROT_READ | PROT_WRITE,
                        MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
    if (scr != (uint8_t *)DT_SCRATCH_BASE) { perror("mmap"); return 1; }
    FILE *out = fopen(argv[1], "wb");
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
    sigaction(SIGFPE, &sa, NULL);
    for (unsigned t = 0; t < nt_count; t++) {
        for (unsigned k = 0; k < DT_TRIALS; k++) {
            DtResult res;
            memset(&res, 0, sizeof(res));
            memset(scr, 0, DT_SCRATCH_SIZE);
            dt_make_state(&res.st, scr, t, k, special, nt_regsets[t]);
            sig_hit = 0;
            static uint8_t fx[512] __attribute__((aligned(16)));
            if (sigsetjmp(jb, 1) == 0) {
                int full = res.st.ftag == 0xff;
                dt_x87_load(res.st.st, (res.st.ftop + 7 + full) & 7, full);
                nt_table[t](&res.st);
                dt_x87_save(fx);
                dt_x87_unpack(fx, &res.st);
            } else {
                res.status = sig_hit == SIGILL ? 1 : 2;
            }
            memcpy(res.mem, scr + (DT_MEM_TARGET - DT_SCRATCH_BASE) - 32, DT_MEM_WINDOW);
            fwrite(&res, sizeof(res), 1, out);
        }
    }
    fclose(out);
    return 0;
}
