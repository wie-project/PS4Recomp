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

	// x87 FPU ST(0)-ST(7)
	x86asm.F0: {BaseReg: "FPU_ST(0)", Size: 8},
	x86asm.F1: {BaseReg: "FPU_ST(1)", Size: 8},
	x86asm.F2: {BaseReg: "FPU_ST(2)", Size: 8},
	x86asm.F3: {BaseReg: "FPU_ST(3)", Size: 8},
	x86asm.F4: {BaseReg: "FPU_ST(4)", Size: 8},
	x86asm.F5: {BaseReg: "FPU_ST(5)", Size: 8},
	x86asm.F6: {BaseReg: "FPU_ST(6)", Size: 8},
	x86asm.F7: {BaseReg: "FPU_ST(7)", Size: 8},

	// MMX (M0-M7)
	x86asm.M0: {BaseReg: "mmx[0]", Size: 8},
	x86asm.M1: {BaseReg: "mmx[1]", Size: 8},
	x86asm.M2: {BaseReg: "mmx[2]", Size: 8},
	x86asm.M3: {BaseReg: "mmx[3]", Size: 8},
	x86asm.M4: {BaseReg: "mmx[4]", Size: 8},
	x86asm.M5: {BaseReg: "mmx[5]", Size: 8},
	x86asm.M6: {BaseReg: "mmx[6]", Size: 8},
	x86asm.M7: {BaseReg: "mmx[7]", Size: 8},

}

type regReadEntry struct {
	expr string
	size int
	ok   bool
}

type regWriteEntry struct {
	prefix string
	suffix string
	ok     bool
}

var (
	regReadTable  [256]regReadEntry
	regWriteTable [256]regWriteEntry
)

func isMmx(reg x86asm.Reg) bool {
	return reg >= x86asm.M0 && reg <= x86asm.M7
}

func mmxIdx(reg x86asm.Reg) int {
	return int(reg - x86asm.M0)
}

func init() {
	for r := x86asm.F0; r <= x86asm.F7; r++ {
		idx := int(r - x86asm.F0)
		regReadTable[r] = regReadEntry{
			expr: fmt.Sprintf("FPU_ST(%d)", idx),
			size: 8,
			ok:   true,
		}
		regWriteTable[r] = regWriteEntry{
			prefix: fmt.Sprintf("FPU_ST(%d) = (double)(", idx),
			suffix: ");",
			ok:     true,
		}
	}

	// Segment register read/write special handling in x86-64 flat mode:
	// FS: reads ctx->fs_base, writes ctx->fs_base
	// GS: reads ctx->gs_base, writes ctx->gs_base
	// CS, DS, ES, SS: in x86-64 flat mode, reads 0, writes ignored
	regReadTable[x86asm.FS] = regReadEntry{expr: "((uint16_t)ctx->fs_base)", size: 2, ok: true}
	regWriteTable[x86asm.FS] = regWriteEntry{prefix: "ctx->fs_base = (uint64_t)(uint16_t)(", suffix: ");", ok: true}

	regReadTable[x86asm.GS] = regReadEntry{expr: "((uint16_t)ctx->gs_base)", size: 2, ok: true}
	regWriteTable[x86asm.GS] = regWriteEntry{prefix: "ctx->gs_base = (uint64_t)(uint16_t)(", suffix: ");", ok: true}

	for _, seg := range []x86asm.Reg{x86asm.CS, x86asm.DS, x86asm.ES, x86asm.SS} {
		regReadTable[seg] = regReadEntry{expr: "((uint16_t)0)", size: 2, ok: true}
		regWriteTable[seg] = regWriteEntry{prefix: "/* ignored seg write: */ (void)(", suffix: ");", ok: true}
	}

	for reg, info := range regMap {
		var readExpr string
		var readOk bool
		var writePrefix, writeSuffix string
		var writeOk bool

		switch info.Size {
		case 16:
			readExpr = "ctx->" + info.BaseReg
			readOk = true
			writePrefix = "ctx->" + info.BaseReg + " = "
			writeSuffix = ";"
			writeOk = true
		case 8:
			readExpr = "ctx->" + info.BaseReg
			readOk = true
			writePrefix = "ctx->" + info.BaseReg + " = (uint64_t)("
			writeSuffix = ");"
			writeOk = true
		case 4:
			readExpr = "((uint32_t)ctx->" + info.BaseReg + ")"
			readOk = true
			writePrefix = "ctx->" + info.BaseReg + " = (uint64_t)(uint32_t)("
			writeSuffix = ");"
			writeOk = true
		case 2:
			readExpr = "((uint16_t)ctx->" + info.BaseReg + ")"
			readOk = true
			writePrefix = "ctx->" + info.BaseReg + " = (ctx->" + info.BaseReg + " & ~0xffffULL) | ((uint64_t)(uint16_t)("
			writeSuffix = "));"
			writeOk = true
		case 1:
			readOk = true
			writeOk = true
			if info.High8 {
				readExpr = "((uint8_t)((ctx->" + info.BaseReg + " >> 8) & 0xff))"
				writePrefix = "ctx->" + info.BaseReg + " = (ctx->" + info.BaseReg + " & ~0xff00ULL) | (((uint64_t)(uint8_t)("
				writeSuffix = ")) << 8);"
			} else {
				readExpr = "((uint8_t)(ctx->" + info.BaseReg + " & 0xff))"
				writePrefix = "ctx->" + info.BaseReg + " = (ctx->" + info.BaseReg + " & ~0xffULL) | ((uint64_t)(uint8_t)("
				writeSuffix = "));"
			}
		}

		if readOk {
			regReadTable[reg] = regReadEntry{
				expr: readExpr,
				size: info.Size,
				ok:   true,
			}
		}
		if writeOk {
			regWriteTable[reg] = regWriteEntry{
				prefix: writePrefix,
				suffix: writeSuffix,
				ok:     true,
			}
		}
	}
}

// GetRegReadExpr returns a C expression to read the value of an x86 register.
func GetRegReadExpr(reg x86asm.Reg) (string, int, error) {
	entry := &regReadTable[reg]
	if !entry.ok {
		return "", 0, fmt.Errorf("unsupported register: %v", reg)
	}
	return entry.expr, entry.size, nil
}

// GetRegWriteStmt returns a C statement to write a value into an x86 register.
// x86-64 Rule: 32-bit register writes ZERO-EXTEND to the full 64-bit register.
func GetRegWriteStmt(reg x86asm.Reg, valExpr string) (string, error) {
	entry := &regWriteTable[reg]
	if !entry.ok {
		return "", fmt.Errorf("unsupported register: %v", reg)
	}
	return entry.prefix + valExpr + entry.suffix, nil
}
