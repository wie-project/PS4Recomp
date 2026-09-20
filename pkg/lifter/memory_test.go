package lifter_test

import (
	"testing"

	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/lifter"
)

func TestMemoryAddressing(t *testing.T) {
	// RIP-relative: [RIP+0x1000] at instruction ending at 0x2000 -> target = 0x3000
	ripMem := x86asm.Mem{
		Base: x86asm.RIP,
		Disp: 0x1000,
	}
	addrExpr, err := lifter.MemAddrExpr(ripMem, 0x2000)
	if err != nil || addrExpr != "0x3000ULL" {
		t.Errorf("unexpected RIP addr: %q, %v", addrExpr, err)
	}

	// Base + Disp: [RBP - 0x28]
	baseMem := x86asm.Mem{
		Base: x86asm.RBP,
		Disp: -0x28,
	}
	addrExpr, err = lifter.MemAddrExpr(baseMem, 0x100)
	if err != nil || addrExpr != "ctx->rbp + (uint64_t)(-0x28LL)" {
		t.Errorf("unexpected base mem: %q, %v", addrExpr, err)
	}

	// FS Segment: [FS:0x0]
	fsMem := x86asm.Mem{
		Segment: x86asm.FS,
		Disp:    0,
	}
	addrExpr, err = lifter.MemAddrExpr(fsMem, 0x100)
	if err != nil || addrExpr != "ctx->fs_base" {
		t.Errorf("unexpected FS mem: %q, %v", addrExpr, err)
	}

	// VEX RIP-relative (x86asm sets Base=0, Index=0 for VEX [RIP+disp32]): [RIP+0x1000] at nextPC 0x2000 -> 0x3000
	vexRipMem := x86asm.Mem{
		Base: 0,
		Disp: 0x1000,
	}
	addrExpr, err = lifter.MemAddrExpr(vexRipMem, 0x2000)
	if err != nil || addrExpr != "0x3000ULL" {
		t.Errorf("unexpected VEX RIP addr: %q, %v", addrExpr, err)
	}

	// VEX RIP-relative negative displacement: [RIP-0x20] at nextPC 0x2000 -> 0x1fe0
	vexRipNegMem := x86asm.Mem{
		Base: 0,
		Disp: -0x20,
	}
	addrExpr, err = lifter.MemAddrExpr(vexRipNegMem, 0x2000)
	if err != nil || addrExpr != "0x1fe0ULL" {
		t.Errorf("unexpected VEX RIP negative addr: %q, %v", addrExpr, err)
	}
}
