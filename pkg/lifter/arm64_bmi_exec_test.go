package lifter

import (
	"testing"

	"golang.org/x/arch/x86/x86asm"
)

func TestExecMULXAliases(t *testing.T) {
	// MULX reads the implicit RDX multiplier before either destination is written.
	// In Intel syntax the first destination receives the high half, the second the low half.
	runExecCases(t, []execCase{
		{
			name:  "mulx64 low destination aliases implicit multiplier",
			code:  []string{"c4e2ebf6c1"}, // mulx rax, rdx, rcx
			regs:  map[x86asm.Reg]uint64{x86asm.RDX: 0xfedcba9876543210, x86asm.RCX: 0x123456789abcdef0},
			flags: fCF | fZF | fOF,
		},
		{
			name:  "mulx64 high destination aliases implicit multiplier",
			code:  []string{"c4e2fbf6d1"}, // mulx rdx, rax, rcx
			regs:  map[x86asm.Reg]uint64{x86asm.RDX: 0xfedcba9876543210, x86asm.RCX: 0x123456789abcdef0},
			flags: fSF | fOF,
		},
		{
			name:  "mulx64 both destinations alias implicit multiplier",
			code:  []string{"c4e2ebf6d1"}, // mulx rdx, rdx, rcx (high half wins)
			regs:  map[x86asm.Reg]uint64{x86asm.RDX: 0xfedcba9876543210, x86asm.RCX: 0x123456789abcdef0},
			flags: fCF | fSF,
		},
		{
			name:  "mulx64 source and both destinations alias multiplier",
			code:  []string{"c4e2ebf6d2"}, // mulx rdx, rdx, rdx
			regs:  map[x86asm.Reg]uint64{x86asm.RDX: 0xfedcba9876543210},
			flags: fCF | fZF | fSF | fOF,
		},
		{
			name:  "mulx64 source aliases low destination",
			code:  []string{"c4e2ebf6c2"}, // mulx rax, rdx, rdx
			regs:  map[x86asm.Reg]uint64{x86asm.RDX: 0xfedcba9876543210},
			flags: fOF,
		},
		{
			name:  "mulx64 memory base aliases low destination",
			code:  []string{"c4e2ebf602"}, // mulx rax, rdx, [rdx]
			regs:  map[x86asm.Reg]uint64{x86asm.RDX: execMemBase + 0x100},
			mem:   map[int][]byte{0x100: le64(0xfedcba9876543210)},
			flags: fCF | fOF,
		},
		{
			name:  "mulx32 both destinations alias multiplier",
			code:  []string{"c4e26bf6d1"}, // mulx edx, edx, ecx
			regs:  map[x86asm.Reg]uint64{x86asm.RDX: 0xfedcba9876543210, x86asm.RCX: 0x123456789abcdef0},
			flags: fCF | fSF,
		},
		{
			name:  "mulx32 low destination aliases implicit multiplier",
			code:  []string{"c4e26bf6c1"}, // mulx eax, edx, ecx
			regs:  map[x86asm.Reg]uint64{x86asm.RDX: 0xfedcba9876543210, x86asm.RCX: 0x123456789abcdef0},
			flags: fCF | fZF,
		},
	})
}
