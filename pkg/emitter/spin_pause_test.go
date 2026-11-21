package emitter

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

// The guest keeps r8-r15 in x8-x15, which AAPCS64 treats as caller-saved. A job worker's scan
// loop keeps r9-r15 live across PAUSE, so _recomp_spin_pause must hand them back unchanged even
// when the counting half (recomp_spin_pause_impl) or the sleeping half clobbers them. This links
// the real bridge against stand-ins that do exactly that.
const spinPauseHarness = `
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Data symbol the bridge's indirect-call path addresses; never read here.
uint64_t g_dispatch_l1[1] __asm__("_g_dispatch_l1");

static int g_sleep;      // what the stand-in recomp_spin_pause_impl returns
static int g_slept;

#define CLOBBER_X8_X15() __asm__ volatile("mov x8, #-1\n mov x9, #-1\n mov x10, #-1\n mov x11, #-1\n" \
  "mov x12, #-1\n mov x13, #-1\n mov x14, #-1\n mov x15, #-1\n" ::: "x8","x9","x10","x11","x12","x13","x14","x15","memory")

int recomp_spin_pause_impl(void *ctx) { (void)ctx; CLOBBER_X8_X15(); return g_sleep; }
void recomp_spin_pause_sleep(void) {
  g_slept = 1;
  CLOBBER_X8_X15();
  __asm__ volatile("movi v0.2d, #-1\n movi v9.2d, #-1\n" ::: "v0", "v9");
}

extern void recomp_spin_pause(void) __asm__("_recomp_spin_pause");

static int run(int sleep) {
  g_sleep = sleep;
  g_slept = 0;
  uint64_t tcb[32] = {0};
  uint64_t ctx[8] = {0};
  tcb[0x70 / 8] = (uint64_t)ctx;
  uint64_t out[8];
  uint64_t nzcv_in = 0x60000000, nzcv_out;
  // x27 = TCB (the stub reads the context from [x28 + x27 + 0x70]; x28 = 0 here).
  __asm__ volatile(
      "mov x27, %[tcb]\n mov x28, xzr\n"
      "mov x8, #108\n mov x9, #109\n mov x10, #110\n mov x11, #111\n"
      "mov x12, #112\n mov x13, #113\n mov x14, #114\n mov x15, #115\n"
      "msr nzcv, %[nzin]\n"
      "bl _recomp_spin_pause\n"
      "mrs %[nzout], nzcv\n"
      "stp x8, x9, [%[out], #0]\n stp x10, x11, [%[out], #16]\n"
      "stp x12, x13, [%[out], #32]\n stp x14, x15, [%[out], #48]\n"
      : [nzout] "=&r"(nzcv_out)
      : [tcb] "r"(tcb), [nzin] "r"(nzcv_in), [out] "r"(out)
      : "x0","x1","x2","x3","x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15",
        "x16","x17","x27","x28","x30","memory","cc");
  int bad = 0;
  for (int i = 0; i < 8; i++)
    if (out[i] != (uint64_t)(108 + i)) { printf("x%d = %llu, want %d\n", 8 + i, (unsigned long long)out[i], 108 + i); bad = 1; }
  if ((nzcv_out & 0xf0000000) != nzcv_in) { printf("nzcv = %llx, want %llx\n", (unsigned long long)nzcv_out, (unsigned long long)nzcv_in); bad = 1; }
  if (sleep && !g_slept) { printf("sleep half not reached\n"); bad = 1; }
  return bad;
}

int main(void) {
  int bad = run(0);
  bad |= run(1);
  puts(bad ? "FAIL" : "PASS");
  return bad;
}
`

func TestSpinPausePreservesGuestRegisters(t *testing.T) {
	if runtime.GOOS != "darwin" || runtime.GOARCH != "arm64" {
		t.Skip("the bridge is ARM64 macOS assembly")
	}
	if _, err := exec.LookPath("clang"); err != nil {
		t.Skip("clang not found")
	}
	dir := t.TempDir()
	asm, c, bin := filepath.Join(dir, "tramp.s"), filepath.Join(dir, "harness.c"), filepath.Join(dir, "harness")
	if err := os.WriteFile(asm, []byte(trampolineAsm), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(c, []byte(spinPauseHarness), 0o644); err != nil {
		t.Fatal(err)
	}
	// The rest of the bridge references runtime symbols this test never calls.
	out, err := exec.Command("clang", "-O1", "-target", "arm64-apple-darwin", c, asm,
		"-Wl,-undefined,dynamic_lookup", "-o", bin).CombinedOutput()
	if err != nil {
		t.Fatalf("building harness: %v\n%s", err, out)
	}
	if out, err := exec.Command(bin).CombinedOutput(); err != nil || !strings.Contains(string(out), "PASS") {
		t.Fatalf("harness: %v\n%q", err, out)
	}
}
