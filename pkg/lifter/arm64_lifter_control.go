package lifter

import (
	"fmt"
	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/disasm"
)

// liftSetcc handles SETcc.
func (l *Arm64Lifter) liftSetcc(inst disasm.Instruction, nextPC uint64, cond string, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	lines = append(lines, fmt.Sprintf("    cset w16, %s", cond))

	if reg, ok := dst.(x86asm.Reg); ok {
		storeCode, okStore := writeRegFrom(reg, "x16", "w16")
		if okStore {
			lines = append(lines, storeCode...)
			return lines, nil
		}
	}

	if mem, ok := dst.(x86asm.Mem); ok {
		lines = append(lines, writeMemFrom(mem, nextPC, 1, "x16", "w16")...)
		return lines, nil
	}

	lines = append(lines, "    brk #0 // unsupported setcc")
	return lines, nil
}

// liftCmovcc handles CMOVcc.
func (l *Arm64Lifter) liftCmovcc(inst disasm.Instruction, nextPC uint64, cond string, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	src := inst.Inst.Args[1]

	descDst, okDescDst := arm64RegMap[dstReg]
	if !okDst || !okDescDst {
		lines = append(lines, "    brk #0 // unsupported cmov dst")
		return lines, nil
	}

	if descDst.Size == 2 {
		// 16-bit: only the low 16 bits of the destination may change.
		s16 := ""
		if srcReg, okSrc := src.(x86asm.Reg); okSrc {
			if descSrc, okDescSrc := arm64RegMap[srcReg]; okDescSrc {
				s16 = descSrc.W
			}
		} else if mem, okMem := src.(x86asm.Mem); okMem {
			lines = append(lines, readMemTo(mem, nextPC, 2, "x16", "w16")...)
			s16 = "w16"
		}
		if s16 == "" {
			lines = append(lines, "    brk #0 // unsupported cmov")
			return lines, nil
		}
		lines = append(lines, fmt.Sprintf("    csel w16, %s, %s, %s", s16, descDst.W, cond),
			fmt.Sprintf("    bfi %s, x16, #0, #16", descDst.X))
		return lines, nil
	}

	if srcReg, okSrc := src.(x86asm.Reg); okSrc {
		if descSrc, okDescSrc := arm64RegMap[srcReg]; okDescSrc {
			if descDst.Size == 4 {
				lines = append(lines, fmt.Sprintf("    csel %s, %s, %s, %s", descDst.W, descSrc.W, descDst.W, cond))
			} else {
				lines = append(lines, fmt.Sprintf("    csel %s, %s, %s, %s", descDst.X, descSrc.X, descDst.X, cond))
			}
			return lines, nil
		}
	}

	if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, readMemTo(mem, nextPC, descDst.Size, "x16", "w16")...)
		if descDst.Size == 4 {
			lines = append(lines, fmt.Sprintf("    csel %s, w16, %s, %s", descDst.W, descDst.W, cond))
		} else {
			lines = append(lines, fmt.Sprintf("    csel %s, x16, %s, %s", descDst.X, descDst.X, cond))
		}
		return lines, nil
	}

	lines = append(lines, "    brk #0 // unsupported cmov")
	return lines, nil
}
