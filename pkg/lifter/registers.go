package lifter

import (
	"fmt"

	"golang.org/x/arch/x86/x86asm"
)

// RegInfo holds properties of an x86 register.
type RegInfo struct {
	BaseReg string // e.g. "rax", "rbx", "r8", "xmm[0]"
	Size    int    // 1, 2, 4, 8, 16
	High8   bool   // true for AH, CH, DH, BH
}

var regMap = map[x86asm.Reg]RegInfo{
	// 64-bit
	x86asm.RAX: {BaseReg: "rax", Size: 8},
	x86asm.RCX: {BaseReg: "rcx", Size: 8},
	x86asm.RDX: {BaseReg: "rdx", Size: 8},
	x86asm.RBX: {BaseReg: "rbx", Size: 8},
	x86asm.RSP: {BaseReg: "rsp", Size: 8},
	x86asm.RBP: {BaseReg: "rbp", Size: 8},
	x86asm.RSI: {BaseReg: "rsi", Size: 8},
	x86asm.RDI: {BaseReg: "rdi", Size: 8},
	x86asm.R8:  {BaseReg: "r8", Size: 8},
	x86asm.R9:  {BaseReg: "r9", Size: 8},
	x86asm.R10: {BaseReg: "r10", Size: 8},
	x86asm.R11: {BaseReg: "r11", Size: 8},
	x86asm.R12: {BaseReg: "r12", Size: 8},
	x86asm.R13: {BaseReg: "r13", Size: 8},
	x86asm.R14: {BaseReg: "r14", Size: 8},
	x86asm.R15: {BaseReg: "r15", Size: 8},

	// 32-bit
	x86asm.EAX:  {BaseReg: "rax", Size: 4},
	x86asm.ECX:  {BaseReg: "rcx", Size: 4},
	x86asm.EDX:  {BaseReg: "rdx", Size: 4},
	x86asm.EBX:  {BaseReg: "rbx", Size: 4},
	x86asm.ESP:  {BaseReg: "rsp", Size: 4},
	x86asm.EBP:  {BaseReg: "rbp", Size: 4},
	x86asm.ESI:  {BaseReg: "rsi", Size: 4},
	x86asm.EDI:  {BaseReg: "rdi", Size: 4},
	x86asm.R8L:  {BaseReg: "r8", Size: 4},
	x86asm.R9L:  {BaseReg: "r9", Size: 4},
	x86asm.R10L: {BaseReg: "r10", Size: 4},
	x86asm.R11L: {BaseReg: "r11", Size: 4},
	x86asm.R12L: {BaseReg: "r12", Size: 4},
	x86asm.R13L: {BaseReg: "r13", Size: 4},
	x86asm.R14L: {BaseReg: "r14", Size: 4},
	x86asm.R15L: {BaseReg: "r15", Size: 4},

	// 16-bit
	x86asm.AX:   {BaseReg: "rax", Size: 2},
	x86asm.CX:   {BaseReg: "rcx", Size: 2},
	x86asm.DX:   {BaseReg: "rdx", Size: 2},
	x86asm.BX:   {BaseReg: "rbx", Size: 2},
	x86asm.SP:   {BaseReg: "rsp", Size: 2},
	x86asm.BP:   {BaseReg: "rbp", Size: 2},
	x86asm.SI:   {BaseReg: "rsi", Size: 2},
	x86asm.DI:   {BaseReg: "rdi", Size: 2},
	x86asm.R8W:  {BaseReg: "r8", Size: 2},
	x86asm.R9W:  {BaseReg: "r9", Size: 2},
	x86asm.R10W: {BaseReg: "r10", Size: 2},
	x86asm.R11W: {BaseReg: "r11", Size: 2},
	x86asm.R12W: {BaseReg: "r12", Size: 2},
	x86asm.R13W: {BaseReg: "r13", Size: 2},
	x86asm.R14W: {BaseReg: "r14", Size: 2},
	x86asm.R15W: {BaseReg: "r15", Size: 2},

	// 8-bit
	x86asm.AL:   {BaseReg: "rax", Size: 1},
	x86asm.CL:   {BaseReg: "rcx", Size: 1},
	x86asm.DL:   {BaseReg: "rdx", Size: 1},
	x86asm.BL:   {BaseReg: "rbx", Size: 1},
	x86asm.AH:   {BaseReg: "rax", Size: 1, High8: true},
	x86asm.CH:   {BaseReg: "rcx", Size: 1, High8: true},
	x86asm.DH:   {BaseReg: "rdx", Size: 1, High8: true},
	x86asm.BH:   {BaseReg: "rbx", Size: 1, High8: true},
	x86asm.SPB:  {BaseReg: "rsp", Size: 1},
	x86asm.BPB:  {BaseReg: "rbp", Size: 1},
	x86asm.SIB:  {BaseReg: "rsi", Size: 1},
	x86asm.DIB:  {BaseReg: "rdi", Size: 1},
	x86asm.R8B:  {BaseReg: "r8", Size: 1},
	x86asm.R9B:  {BaseReg: "r9", Size: 1},
	x86asm.R10B: {BaseReg: "r10", Size: 1},
	x86asm.R11B: {BaseReg: "r11", Size: 1},
	x86asm.R12B: {BaseReg: "r12", Size: 1},
	x86asm.R13B: {BaseReg: "r13", Size: 1},
	x86asm.R14B: {BaseReg: "r14", Size: 1},
	x86asm.R15B: {BaseReg: "r15", Size: 1},

	// XMM (0-15)
	x86asm.X0:  {BaseReg: "xmm[0]", Size: 16},
	x86asm.X1:  {BaseReg: "xmm[1]", Size: 16},
	x86asm.X2:  {BaseReg: "xmm[2]", Size: 16},
	x86asm.X3:  {BaseReg: "xmm[3]", Size: 16},
	x86asm.X4:  {BaseReg: "xmm[4]", Size: 16},
	x86asm.X5:  {BaseReg: "xmm[5]", Size: 16},
	x86asm.X6:  {BaseReg: "xmm[6]", Size: 16},
	x86asm.X7:  {BaseReg: "xmm[7]", Size: 16},
	x86asm.X8:  {BaseReg: "xmm[8]", Size: 16},
	x86asm.X9:  {BaseReg: "xmm[9]", Size: 16},
	x86asm.X10: {BaseReg: "xmm[10]", Size: 16},
	x86asm.X11: {BaseReg: "xmm[11]", Size: 16},
	x86asm.X12: {BaseReg: "xmm[12]", Size: 16},
	x86asm.X13: {BaseReg: "xmm[13]", Size: 16},
	x86asm.X14: {BaseReg: "xmm[14]", Size: 16},
	x86asm.X15: {BaseReg: "xmm[15]", Size: 16},
}

// GetRegReadExpr returns a C expression to read the value of an x86 register.
func GetRegReadExpr(reg x86asm.Reg) (string, int, error) {
	info, ok := regMap[reg]
	if !ok {
		return "", 0, fmt.Errorf("unsupported register: %v", reg)
	}

	if info.Size == 16 {
		return fmt.Sprintf("ctx->%s", info.BaseReg), 16, nil
	}
	if info.Size == 8 {
		return fmt.Sprintf("ctx->%s", info.BaseReg), 8, nil
	}
	if info.Size == 4 {
		return fmt.Sprintf("((uint32_t)ctx->%s)", info.BaseReg), 4, nil
	}
	if info.Size == 2 {
		return fmt.Sprintf("((uint16_t)ctx->%s)", info.BaseReg), 2, nil
	}
	if info.Size == 1 {
		if info.High8 {
			return fmt.Sprintf("((uint8_t)((ctx->%s >> 8) & 0xff))", info.BaseReg), 1, nil
		}
		return fmt.Sprintf("((uint8_t)(ctx->%s & 0xff))", info.BaseReg), 1, nil
	}
	return "", 0, fmt.Errorf("invalid reg size for %v", reg)
}

// GetRegWriteStmt returns a C statement to write a value into an x86 register.
// x86-64 Rule: 32-bit register writes ZERO-EXTEND to the full 64-bit register.
func GetRegWriteStmt(reg x86asm.Reg, valExpr string) (string, error) {
	info, ok := regMap[reg]
	if !ok {
		return "", fmt.Errorf("unsupported register: %v", reg)
	}

	if info.Size == 16 {
		return fmt.Sprintf("ctx->%s = %s;", info.BaseReg, valExpr), nil
	}
	if info.Size == 8 {
		return fmt.Sprintf("ctx->%s = (uint64_t)(%s);", info.BaseReg, valExpr), nil
	}
	if info.Size == 4 {
		// Zero-extend 32-bit to 64-bit
		return fmt.Sprintf("ctx->%s = (uint64_t)(uint32_t)(%s);", info.BaseReg, valExpr), nil
	}
	if info.Size == 2 {
		return fmt.Sprintf("ctx->%s = (ctx->%s & ~0xffffULL) | ((uint64_t)(uint16_t)(%s));", info.BaseReg, info.BaseReg, valExpr), nil
	}
	if info.Size == 1 {
		if info.High8 {
			return fmt.Sprintf("ctx->%s = (ctx->%s & ~0xff00ULL) | (((uint64_t)(uint8_t)(%s)) << 8);", info.BaseReg, info.BaseReg, valExpr), nil
		}
		return fmt.Sprintf("ctx->%s = (ctx->%s & ~0xffULL) | ((uint64_t)(uint8_t)(%s));", info.BaseReg, info.BaseReg, valExpr), nil
	}
	return "", fmt.Errorf("invalid reg size for %v", reg)
}
