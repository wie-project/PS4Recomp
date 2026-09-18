package lifter_test

import (
	"testing"

	"golang.org/x/arch/x86/x86asm"
)

func TestRegTypes(t *testing.T) {
	regs := []x86asm.Reg{
		x86asm.RAX, x86asm.EAX, x86asm.AX, x86asm.AL, x86asm.AH,
		x86asm.R8, x86asm.R8L, x86asm.R8W, x86asm.R8B,
		x86asm.X0, x86asm.RIP,
	}
	for _, r := range regs {
		t.Logf("Reg %v: string=%q", r, r.String())
	}
}
