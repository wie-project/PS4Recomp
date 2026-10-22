//go:build darwin && arm64

package emitter

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/lifter"
)

// overlapDriver calls the recompiled _fn_0x40 with guest RDI and RAX set and returns guest RAX.
// Guest memory is a host buffer (x28); guest RSP (x26) points at a fake return address.
const overlapDriver = `
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
uint64_t run_guest(uint8_t *mem, uint64_t rdi, uint64_t rax);
int main(int argc, char **argv) {
  static uint8_t mem[0x2000];
  uint64_t rdi = strtoull(argv[1], 0, 0);
  printf("%llx\n", (unsigned long long)run_guest(mem, rdi, 5));
  return 0;
}
`

const overlapThunk = `
.text
.p2align 2
.globl _run_guest
_run_guest:
    stp x29, x30, [sp, #-96]!
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    stp x27, x28, [sp, #80]
    mov x28, x0
    mov x24, x1
    mov x19, x2
    mov x26, #0x1000
    bl _fn_0x40
    mov x0, x19
    ldp x27, x28, [sp, #80]
    ldp x25, x26, [sp, #64]
    ldp x23, x24, [sp, #48]
    ldp x21, x22, [sp, #32]
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #96
    ret
`

// A branch into the middle of an instruction makes the decoder lay out two overlapping
// instruction streams; the recompiled code must still continue each instruction at its own
// successor. Guest function (see disasm overlapImage):
//
//	0x40: test edi, edi ; 0x42: je 0x47 ; 0x44: mov eax, 0xc0ff2211 ; 0x47: inc eax ; 0x49: ret
//
// RDI = 0 takes the je into the mov's immediate (RAX 5 -> 6); RDI = 1 runs the mov and must
// return 0xc0ff2211, not fall into the inc that follows it in address order.
func TestOverlappingInstructionsRun(t *testing.T) {
	if _, err := exec.LookPath("clang"); err != nil {
		t.Skip("clang not found")
	}
	img := make([]byte, 0x100)
	copy(img[0x40:], []byte{0x85, 0xff, 0x74, 0x03, 0xb8, 0x11, 0x22, 0xff, 0xc0, 0xc3})
	loaded := &elfloader.LoadedELF{
		MemoryImage:  img,
		ExecRanges:   []elfloader.AddrRange{{Start: 0, End: uint64(len(img))}},
		SymbolByAddr: map[uint64]elfloader.Symbol{},
		SymbolByName: map[string]elfloader.Symbol{},
	}
	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}
	em := NewAsmEmitter(loaded, d, lifter.NewArm64Lifter(map[uint64]bool{0x40: true}))
	dir := t.TempDir()
	code := filepath.Join(dir, "code_000.s")
	if err := em.emitSingleAsmChunk(code, 0, []uint64{0x40}); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "driver.c"), []byte(overlapDriver), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "thunk.s"), []byte(overlapThunk), 0o644); err != nil {
		t.Fatal(err)
	}
	bin := filepath.Join(dir, "overlap")
	out, err := exec.Command("clang", "-target", "arm64-apple-darwin", "-o", bin, filepath.Join(dir, "driver.c"),
		filepath.Join(dir, "thunk.s"), code).CombinedOutput()
	if err != nil {
		t.Fatalf("clang: %v\n%s", err, out)
	}
	for _, c := range []struct{ rdi, want string }{{"0", "6"}, {"1", "c0ff2211"}} {
		out, err := exec.Command(bin, c.rdi).CombinedOutput()
		if err != nil {
			t.Fatalf("rdi=%s: %v\n%s", c.rdi, err, out)
		}
		if got := strings.TrimSpace(string(out)); got != c.want {
			src, _ := os.ReadFile(code)
			t.Errorf("rdi=%s: rax = %s, want %s\n%s", c.rdi, got, c.want, src)
		}
	}
}
