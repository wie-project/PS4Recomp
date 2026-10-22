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

// RDTSC returned the raw host counter (24 MHz on Apple silicon) while sceKernelGetTscFrequency
// reports RECOMP_TSC_FREQ (2.13 GHz) and sceKernelReadTsc scales to it: guest code timing with
// RDTSC saw time run ~89 times slower (TLOU: fades and the boot flow never advanced, black
// screen). The lowering must count in the same units as the runtime's recomp_rdtsc().
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
