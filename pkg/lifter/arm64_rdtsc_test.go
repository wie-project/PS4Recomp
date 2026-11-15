package lifter

import (
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"strconv"
	"strings"
	"testing"
)

// RDTSC must count in guest TSC units: sceKernelGetTscFrequency reports RECOMP_TSC_FREQ and
// sceKernelReadTsc scales to it, so a raw host counter makes guest timing wrong (TLOU: fades and
// the boot flow never advanced, black screen). CNTFRQ_EL0 is not a constant to rely on — 24 MHz
// when that bug was found, 1 GHz on macOS 27.3 — so the lowering scales by the frequency it reads.
func TestRdtscMatchesRuntimeTsc(t *testing.T) {
	hdr, err := os.ReadFile("../runtime/core/recomp_context.h")
	if err != nil {
		t.Fatal(err)
	}
	m := regexp.MustCompile(`#define RECOMP_TSC_FREQ (\d+)ULL`).FindSubmatch(hdr)
	if m == nil {
		t.Fatal("RECOMP_TSC_FREQ not found in recomp_context.h")
	}
	if f, _ := strconv.ParseUint(string(m[1]), 10, 64); f != guestTscFreq {
		t.Fatalf("guestTscFreq %d, RECOMP_TSC_FREQ %d", guestTscFreq, f)
	}
	l := liftBlock(t, "0f31")[0] // rdtsc
	requireInOrder(t, "rdtsc", l,
		"mrs x16, cntvct_el0", "mrs x17, cntfrq_el0",
		"udiv x0, x16, x17", "msub x1, x0, x17, x16",
		"movz x2, #0x3880, lsl #0", "movk x2, #0x7ef5, lsl #16",
		"mul x0, x0, x2", "mul x1, x1, x2", "udiv x1, x1, x17", "add x16, x0, x1",
		"mov w19, w16", "lsr x16, x16, #32", "mov w22, w16")
	forbid(t, "rdtsc", l, "msr nzcv...", "subs ...", "adds ...", "cmp ...")

	if runtime.GOOS != "darwin" || runtime.GOARCH != "arm64" {
		t.Skip("execution part needs an arm64 Mac")
	}
	if _, err := exec.LookPath("clang"); err != nil {
		t.Skip("clang not found")
	}
	dir := t.TempDir()
	var asm strings.Builder
	asm.WriteString(".text\n.globl _lifted_rdtsc\n.p2align 2\n_lifted_rdtsc:\n")
	asm.WriteString("    stp x19, x22, [sp, #-16]!\n")
	for _, ln := range l {
		asm.WriteString(ln + "\n")
	}
	asm.WriteString("    mov w0, w19\n    orr x0, x0, x22, lsl #32\n    ldp x19, x22, [sp], #16\n    ret\n")
	driver := `
#include <stdint.h>
#include <stdio.h>
#include "recomp_context.h"
uint64_t lifted_rdtsc(void);
int main(void) {
    for (int i = 0; i < 100000; i++) {
        uint64_t a = recomp_rdtsc(), b = lifted_rdtsc(), c = recomp_rdtsc();
        if (b < a || b > c) {
            printf("lifted %llu not within runtime [%llu, %llu]\n", (unsigned long long)b,
                   (unsigned long long)a, (unsigned long long)c);
            return 1;
        }
    }
    return 0;
}
`
	if err := os.WriteFile(filepath.Join(dir, "rdtsc.s"), []byte(asm.String()), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "main.c"), []byte(driver), 0o644); err != nil {
		t.Fatal(err)
	}
	core, _ := filepath.Abs("../runtime/core")
	bin := filepath.Join(dir, "rdtsc")
	if o, err := exec.Command("clang", "-O1", "-I", core, "-o", bin, filepath.Join(dir, "main.c"),
		filepath.Join(dir, "rdtsc.s")).CombinedOutput(); err != nil {
		t.Fatalf("clang: %v\n%s", err, o)
	}
	if o, err := exec.Command(bin).CombinedOutput(); err != nil {
		t.Fatalf("%v: %s", err, o)
	}
}

// TestRdtscLiftedArithmetic runs the emitted instruction text itself — with the counter and
// frequency reads replaced by arguments — against the 128-bit quotient recomp_rdtsc() returns.
//
// The lowering computes q*F + (r*F)/frq with q, r = cntvct divmod frq, which equals that quotient
// while q*F stays in 64 bits: q = cntvct/frq is seconds of uptime and q*F overflows past about
// 274 years, tested here up to the largest cntvct that still fits. Counter frequencies from 1 Hz
// to 4 GHz cover the values Apple systems report and the general form.
func TestRdtscLiftedArithmetic(t *testing.T) {
	l := liftBlock(t, "0f31")[0] // rdtsc
	if runtime.GOOS != "darwin" || runtime.GOARCH != "arm64" {
		t.Skip("execution part needs an arm64 Mac")
	}
	if _, err := exec.LookPath("clang"); err != nil {
		t.Skip("clang not found")
	}
	var asm strings.Builder
	asm.WriteString(".text\n.globl _rdtsc_probe\n.p2align 2\n_rdtsc_probe:\n")
	asm.WriteString("    stp x19, x22, [sp, #-16]!\n    mov x16, x0\n    mov x17, x1\n")
	for _, ln := range l {
		if strings.HasPrefix(code(ln), "mrs ") {
			continue
		}
		asm.WriteString(ln + "\n")
	}
	asm.WriteString("    mov w0, w19\n    orr x0, x0, x22, lsl #32\n    ldp x19, x22, [sp], #16\n    ret\n")
	driver := `
#include <stdint.h>
#include <stdio.h>
#include "recomp_context.h"
uint64_t rdtsc_probe(uint64_t val, uint64_t frq);
static int check(uint64_t val, uint64_t frq) {
    uint64_t got = rdtsc_probe(val, frq);
    uint64_t want = (uint64_t)(((__uint128_t)val * RECOMP_TSC_FREQ) / frq);
    if (got != want) {
        printf("cntvct %llu, cntfrq %llu: lifted %llu, runtime %llu\n",
               (unsigned long long)val, (unsigned long long)frq,
               (unsigned long long)got, (unsigned long long)want);
        return 0;
    }
    return 1;
}
int main(void) {
    static const uint64_t frqs[] = {1000000000ULL, 24000000ULL, 19200000ULL,
                                    24000001ULL, 4000000000ULL, 3ULL, 1ULL};
    // Largest q = cntvct/frq that still keeps q * RECOMP_TSC_FREQ inside 64 bits.
    const uint64_t qmax = (uint64_t)((((__uint128_t)1 << 64) - 1) / RECOMP_TSC_FREQ);
    for (unsigned f = 0; f < sizeof frqs / sizeof *frqs; f++) {
        uint64_t frq = frqs[f];
        uint64_t vals[11];
        unsigned n = 0;
        vals[n++] = 0;
        vals[n++] = 1;
        vals[n++] = 2;
        vals[n++] = frq - 1;
        vals[n++] = frq;
        vals[n++] = frq + 1;
        vals[n++] = 2 * frq + 7;
        vals[n++] = 0xffffffffULL;
        vals[n++] = 1000000000000ULL;
        vals[n++] = 86400000000000ULL;
        __uint128_t edge = (__uint128_t)qmax * frq;
        if (edge <= (__uint128_t)UINT64_MAX) {
            vals[n++] = (uint64_t)edge;
        }
        for (unsigned i = 0; i < n; i++) {
            if (!check(vals[i], frq)) {
                return 1;
            }
        }
    }
    return 0;
}
`
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "probe.s"), []byte(asm.String()), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "main.c"), []byte(driver), 0o644); err != nil {
		t.Fatal(err)
	}
	core, _ := filepath.Abs("../runtime/core")
	bin := filepath.Join(dir, "probe")
	if o, err := exec.Command("clang", "-O1", "-I", core, "-o", bin, filepath.Join(dir, "main.c"),
		filepath.Join(dir, "probe.s")).CombinedOutput(); err != nil {
		t.Fatalf("clang: %v\n%s", err, o)
	}
	if o, err := exec.Command(bin).CombinedOutput(); err != nil {
		t.Fatalf("%v: %s", err, o)
	}
}

func TestRdtscpLifting(t *testing.T) {
	// 0f 01 f9 = rdtscp
	l := liftBlock(t, "0f01f9")[0]
	forbid(t, "rdtscp", l, "msr nzcv...", "subs ...", "adds ...", "cmp ...")

	foundTsc := false
	foundAux := false
	for _, ln := range l {
		if strings.Contains(ln, "cntvct_el0") {
			foundTsc = true
		}
		if strings.Contains(ln, "mov w21, wzr") {
			foundAux = true
		}
	}
	if !foundTsc || !foundAux {
		t.Fatalf("expected cntvct_el0 and mov w21, wzr in lifted rdtscp, got:\n%s", strings.Join(l, "\n"))
	}
}
