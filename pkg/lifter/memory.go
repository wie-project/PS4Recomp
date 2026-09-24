package lifter

import (
	"fmt"
	"strconv"
	"strings"

	"golang.org/x/arch/x86/x86asm"
)

// MemAddrExpr returns a C uint64_t expression representing the guest virtual address of a memory operand.
func MemAddrExpr(mem x86asm.Mem, nextPC uint64) (string, error) {
	// In x86-64 ModR/M and SIB, displacements are at most 32-bit and are ALWAYS sign-extended to 64-bit.
	// x86asm can decode 32-bit negative displacements as unsigned uint32 values stored in int64.
	disp := int64(int32(mem.Disp))

	// Check RIP-relative:
	// x86asm sets Base = x86asm.RIP for legacy instructions, but leaves Base = 0, Index = 0
	// for VEX-encoded ModR/M instructions with mod=00, r/m=101 ([RIP+disp32]).
	// In x86-64 mode, an addressing operand without base and index (and without segment override) is RIP-relative.
	if mem.Base == x86asm.RIP || (mem.Segment == 0 && mem.Base == 0 && mem.Index == 0 && mem.Disp != 0) {
		targetAddr := uint64(int64(nextPC) + disp)
		return "0x" + strconv.FormatUint(targetAddr, 16) + "ULL", nil
	}

	// Fast path 1: Base only (e.g. [RAX])
	if mem.Segment == 0 && mem.Index == 0 && disp == 0 && mem.Base != 0 {
		info, ok := regMap[mem.Base]
		if !ok {
			return "", fmt.Errorf("unsupported base register: %v", mem.Base)
		}
		return "ctx->" + info.BaseReg, nil
	}

	// Fast path 2: Base + Disp (e.g. [RBP-0x28], [RSP+0x10])
	if mem.Segment == 0 && mem.Index == 0 && disp != 0 && mem.Base != 0 {
		info, ok := regMap[mem.Base]
		if !ok {
			return "", fmt.Errorf("unsupported base register: %v", mem.Base)
		}
		if disp < 0 {
			return "ctx->" + info.BaseReg + " + (uint64_t)(-0x" + strconv.FormatUint(uint64(-disp), 16) + "LL)", nil
		}
		return "ctx->" + info.BaseReg + " + 0x" + strconv.FormatUint(uint64(disp), 16) + "ULL", nil
	}

	parts := make([]string, 0, 3)

	// Segment (e.g. FS for TLS)
	if mem.Segment == x86asm.FS {
		parts = append(parts, "ctx->fs_base")
	}

	// Base register
	if mem.Base != 0 {
		info, ok := regMap[mem.Base]
		if !ok {
			return "", fmt.Errorf("unsupported base register: %v", mem.Base)
		}
		parts = append(parts, "ctx->"+info.BaseReg)
	}

	// Index register
	if mem.Index != 0 {
		info, ok := regMap[mem.Index]
		if !ok {
			return "", fmt.Errorf("unsupported index register: %v", mem.Index)
		}
		scale := mem.Scale
		if scale == 0 || scale == 1 {
			parts = append(parts, "ctx->"+info.BaseReg)
		} else {
			parts = append(parts, "(ctx->"+info.BaseReg+" * "+strconv.Itoa(int(scale))+"ULL)")
		}
	}

	// Displacement
	if disp != 0 || len(parts) == 0 {
		if disp < 0 {
			parts = append(parts, "(uint64_t)(-0x"+strconv.FormatUint(uint64(-disp), 16)+"LL)")
		} else {
			parts = append(parts, "0x"+strconv.FormatUint(uint64(disp), 16)+"ULL")
		}
	}

	return strings.Join(parts, " + "), nil
}

// MemReadExpr returns a C expression to read an integer/pointer of `size` bytes (1, 2, 4, 8) from `addrExpr`.
func MemReadExpr(addrExpr string, size int) (string, error) {
	switch size {
	case 1:
		return "MEM_U8(" + addrExpr + ")", nil
	case 2:
		return "MEM_U16(" + addrExpr + ")", nil
	case 4:
		return "MEM_U32(" + addrExpr + ")", nil
	case 8:
		return "MEM_U64(" + addrExpr + ")", nil
	default:
		return "", fmt.Errorf("unsupported memory read size: %d", size)
	}
}

// MemWriteStmt returns a C statement to write `valExpr` of `size` bytes to `addrExpr`.
func MemWriteStmt(addrExpr string, size int, valExpr string) (string, error) {
	switch size {
	case 1:
		return "MEM_U8(" + addrExpr + ") = (uint8_t)(" + valExpr + ");", nil
	case 2:
		return "MEM_U16(" + addrExpr + ") = (uint16_t)(" + valExpr + ");", nil
	case 4:
		return "MEM_U32(" + addrExpr + ") = (uint32_t)(" + valExpr + ");", nil
	case 8:
		return "MEM_U64(" + addrExpr + ") = (uint64_t)(" + valExpr + ");", nil
	default:
		return "", fmt.Errorf("unsupported memory write size: %d", size)
	}
}
