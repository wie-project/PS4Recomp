package lifter_test

import (
	"testing"

	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/lifter"
)

func TestRegisters(t *testing.T) {
	// Test 64-bit RAX write
	stmt, err := lifter.GetRegWriteStmt(x86asm.RAX, "123")
	if err != nil || stmt != "ctx->rax = (uint64_t)(123);" {
		t.Errorf("unexpected RAX stmt: %q, %v", stmt, err)
	}

	// Test 32-bit EAX write (must zero-extend)
	stmt, err = lifter.GetRegWriteStmt(x86asm.EAX, "123")
	if err != nil || stmt != "ctx->rax = (uint64_t)(uint32_t)(123);" {
		t.Errorf("unexpected EAX stmt: %q, %v", stmt, err)
	}

	// Test 8-bit AL write
	stmt, err = lifter.GetRegWriteStmt(x86asm.AL, "0x55")
	if err != nil || stmt != "ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x55));" {
		t.Errorf("unexpected AL stmt: %q, %v", stmt, err)
	}

	// Test 8-bit AH write
	stmt, err = lifter.GetRegWriteStmt(x86asm.AH, "0x55")
	if err != nil || stmt != "ctx->rax = (ctx->rax & ~0xff00ULL) | (((uint64_t)(uint8_t)(0x55)) << 8);" {
		t.Errorf("unexpected AH stmt: %q, %v", stmt, err)
	}

	// Test reading EDX
	expr, sz, err := lifter.GetRegReadExpr(x86asm.EDX)
	if err != nil || expr != "((uint32_t)ctx->rdx)" || sz != 4 {
		t.Errorf("unexpected EDX read: %q, size=%d, %v", expr, sz, err)
	}
}
