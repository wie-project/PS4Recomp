package lifter

import (
	"fmt"
	"strings"

	"golang.org/x/arch/x86/x86asm"
)

// MemAddrExpr returns a C uint64_t expression representing the guest virtual address of a memory operand.
func MemAddrExpr(mem x86asm.Mem, nextPC uint64) (string, error) {
	disp := mem.Disp
	if mem.Base != 0 || mem.Index != 0 {
		// In x86-64 ModR/M and SIB, displacements are at most 32-bit and are ALWAYS sign-extended to 64-bit.
		// x86asm can decode 32-bit negative displacements as unsigned uint32 values stored in int64.
		disp = int64(int32(disp))
	}

	// Check RIP-relative
	if mem.Base == x86asm.RIP {
		targetAddr := uint64(int64(nextPC) + disp)
		return fmt.Sprintf("0x%xULL", targetAddr), nil
	}

	var parts []string

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
		parts = append(parts, fmt.Sprintf("ctx->%s", info.BaseReg))
	}

	// Index register
	if mem.Index != 0 {
		info, ok := regMap[mem.Index]
		if !ok {
			return "", fmt.Errorf("unsupported index register: %v", mem.Index)
		}
		scale := mem.Scale
		if scale == 0 || scale == 1 {
			parts = append(parts, fmt.Sprintf("ctx->%s", info.BaseReg))
		} else {
			parts = append(parts, fmt.Sprintf("(ctx->%s * %dULL)", info.BaseReg, scale))
		}
	}

	// Displacement
	if disp != 0 || len(parts) == 0 {
		if disp < 0 {
			parts = append(parts, fmt.Sprintf("(uint64_t)(-0x%xLL)", -disp))
		} else {
			parts = append(parts, fmt.Sprintf("0x%xULL", disp))
		}
	}

	return strings.Join(parts, " + "), nil
}

// MemReadExpr returns a C expression to read an integer/pointer of `size` bytes (1, 2, 4, 8) from `addrExpr`.
func MemReadExpr(addrExpr string, size int) (string, error) {
	switch size {
	case 1:
		return fmt.Sprintf("MEM_U8(%s)", addrExpr), nil
	case 2:
		return fmt.Sprintf("MEM_U16(%s)", addrExpr), nil
	case 4:
		return fmt.Sprintf("MEM_U32(%s)", addrExpr), nil
	case 8:
		return fmt.Sprintf("MEM_U64(%s)", addrExpr), nil
	default:
		return "", fmt.Errorf("unsupported memory read size: %d", size)
	}
}

// MemWriteStmt returns a C statement to write `valExpr` of `size` bytes to `addrExpr`.
func MemWriteStmt(addrExpr string, size int, valExpr string) (string, error) {
	switch size {
	case 1:
		return fmt.Sprintf("MEM_U8(%s) = (uint8_t)(%s);", addrExpr, valExpr), nil
	case 2:
		return fmt.Sprintf("MEM_U16(%s) = (uint16_t)(%s);", addrExpr, valExpr), nil
	case 4:
		return fmt.Sprintf("MEM_U32(%s) = (uint32_t)(%s);", addrExpr, valExpr), nil
	case 8:
		return fmt.Sprintf("MEM_U64(%s) = (uint64_t)(%s);", addrExpr, valExpr), nil
	default:
		return "", fmt.Errorf("unsupported memory write size: %d", size)
	}
}
