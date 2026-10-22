package lifter

import (
	"fmt"
	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/disasm"
)

// liftBmi handles BMI1 and BMI2 instructions:
// BLSI, BLSR, BLSMSK, BZHI, SHLX, SHRX, SARX, RORX, MULX.
func (l *Arm64Lifter) liftBmi(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	args := inst.Inst.Args

	switch op {
	case x86asm.BLSI, x86asm.BLSR, x86asm.BLSMSK, x86asm.BZHI:
		// SF/ZF from the result (BLSMSK: ZF = 0, its result is never 0), OF = 0, and
		//   BLSI: CF = (src != 0); BLSR, BLSMSK: CF = (src == 0); BZHI: CF = (index >= size).
		// ANDS/TST set N, Z and clear V; CF is then inserted into C (carryAdd).
		dstReg, okDst := args[0].(x86asm.Reg)
		descDst, okDD := arm64RegMap[dstReg]
		if !okDst || !okDD {
			lines = append(lines, l.emitUnsupported(inst, "unsupported bmi dst")...)
			return lines, nil
		}
		sz := descDst.Size
		r := "w"
		bits := 32
		dst := descDst.W
		if sz == 8 {
			r, bits, dst = "x", 64, descDst.X
		}
		if op == x86asm.BZHI {
			idxReg, okIdx := args[2].(x86asm.Reg)
			descIdx, okIdxD := arm64RegMap[idxReg]
			if !okIdx || !okIdxD {
				lines = append(lines, l.emitUnsupported(inst, "unsupported bzhi index")...)
				return lines, nil
			}
			lines = append(lines, fmt.Sprintf("    and w3, %s, #0xff", descIdx.W)) // before dst is written
		}
		// Load src into x0 / w0
		if sReg, okS := args[1].(x86asm.Reg); okS {
			rc, _ := readRegTo(sReg, "x0", "w0")
			lines = append(lines, rc...)
		} else if mem, okMem := args[1].(x86asm.Mem); okMem {
			lines = append(lines, readMemTo(mem, nextPC, sz, "x0", "w0")...)
		} else {
			lines = append(lines, l.emitUnsupported(inst, "unsupported bmi src")...)
			return lines, nil
		}
		flags := !inst.SkipFlags
		switch op {
		case x86asm.BLSI:
			if flags {
				lines = append(lines, fmt.Sprintf("    cmp %s0, #0", r), "    cset w2, ne")
			}
			lines = append(lines, fmt.Sprintf("    neg %s1, %s0", r, r), fmt.Sprintf("    and %s1, %s0, %s1", r, r, r))
		case x86asm.BLSR, x86asm.BLSMSK:
			if flags {
				lines = append(lines, fmt.Sprintf("    cmp %s0, #0", r), "    cset w2, eq")
			}
			arith := "and"
			if op == x86asm.BLSMSK {
				arith = "eor"
			}
			lines = append(lines, fmt.Sprintf("    sub %s1, %s0, #1", r, r), fmt.Sprintf("    %s %s1, %s1, %s0", arith, r, r, r))
		case x86asm.BZHI:
			lines = append(lines,
				fmt.Sprintf("    mov %s4, #1", r),
				fmt.Sprintf("    lsl %s4, %s4, %s3", r, r, r),
				fmt.Sprintf("    sub %s4, %s4, #1", r, r),
				fmt.Sprintf("    cmp w3, #%d", bits),
				fmt.Sprintf("    csinv %s4, %s4, %szr, lo", r, r, r),
				"    cset w2, hs",
				fmt.Sprintf("    and %s1, %s0, %s4", r, r, r))
		}
		if flags {
			lines = append(lines, fmt.Sprintf("    tst %s1, %s1", r, r), "    rmif x2, #63, #2")
			l.cpol = carryAdd
		}
		lines = append(lines, fmt.Sprintf("    mov %s, %s1", dst, r))
		return lines, nil

	case x86asm.SHLX, x86asm.SHRX, x86asm.SARX:
		// SHLX/SHRX/SARX dst, src, cnt (does not modify flags!)
		dstReg, okDst := args[0].(x86asm.Reg)
		cntReg, okCnt := args[2].(x86asm.Reg)
		descDst, okDD := arm64RegMap[dstReg]
		descCnt, okCD := arm64RegMap[cntReg]
		if !okDst || !okCnt || !okDD || !okCD {
			lines = append(lines, l.emitUnsupported(inst, "unsupported bmi shift operands")...)
			return lines, nil
		}

		sz := descDst.Size
		if sz == 0 {
			sz = 4
		}

		// Load src into x16 / w16
		if sReg, okS := args[1].(x86asm.Reg); okS {
			rc, _ := readRegTo(sReg, "x16", "w16")
			lines = append(lines, rc...)
		} else if mem, okMem := args[1].(x86asm.Mem); okMem {
			lines = append(lines, readMemTo(mem, nextPC, sz, "x16", "w16")...)
		} else {
			lines = append(lines, l.emitUnsupported(inst, "unsupported bmi shift src")...)
			return lines, nil
		}

		shiftOp := "lsl"
		switch op {
		case x86asm.SHRX:
			shiftOp = "lsr"
		case x86asm.SARX:
			shiftOp = "asr"
		}

		if sz == 8 {
			lines = append(lines,
				fmt.Sprintf("    and x17, %s, #63", descCnt.X),
				fmt.Sprintf("    %s %s, x16, x17", shiftOp, descDst.X),
			)
		} else {
			lines = append(lines,
				fmt.Sprintf("    and w17, %s, #31", descCnt.W),
				fmt.Sprintf("    %s %s, w16, w17", shiftOp, descDst.W),
			)
		}
		return lines, nil

	case x86asm.RORX:
		// RORX dst, src, imm8 (does not modify flags!)
		dstReg, okDst := args[0].(x86asm.Reg)
		imm, okImm := args[2].(x86asm.Imm)
		descDst, okDD := arm64RegMap[dstReg]
		if !okDst || !okImm || !okDD {
			lines = append(lines, l.emitUnsupported(inst, "unsupported rorx operands")...)
			return lines, nil
		}

		sz := descDst.Size
		if sz == 0 {
			sz = 4
		}

		if sReg, okS := args[1].(x86asm.Reg); okS {
			rc, _ := readRegTo(sReg, "x16", "w16")
			lines = append(lines, rc...)
		} else if mem, okMem := args[1].(x86asm.Mem); okMem {
			lines = append(lines, readMemTo(mem, nextPC, sz, "x16", "w16")...)
		} else {
			lines = append(lines, l.emitUnsupported(inst, "unsupported rorx src")...)
			return lines, nil
		}

		rotVal := uint64(imm)
		if sz == 8 {
			rotVal &= 63
			if rotVal == 0 {
				lines = append(lines, fmt.Sprintf("    mov %s, x16", descDst.X))
			} else {
				lines = append(lines, fmt.Sprintf("    ror %s, x16, #%d", descDst.X, rotVal))
			}
		} else {
			rotVal &= 31
			if rotVal == 0 {
				lines = append(lines, fmt.Sprintf("    mov %s, w16", descDst.W))
			} else {
				lines = append(lines, fmt.Sprintf("    ror %s, w16, #%d", descDst.W, rotVal))
			}
		}
		return lines, nil

	case x86asm.MULX:
		// MULX dst_hi, dst_lo, src (unsigned multiply RDX by src without flags)
		dstHiReg, okHi := args[0].(x86asm.Reg)
		dstLoReg, okLo := args[1].(x86asm.Reg)
		descHi, okHiD := arm64RegMap[dstHiReg]
		descLo, okLoD := arm64RegMap[dstLoReg]
		if !okHi || !okLo || !okHiD || !okLoD {
			lines = append(lines, l.emitUnsupported(inst, "unsupported mulx dst")...)
			return lines, nil
		}

		sz := descHi.Size
		if sz == 0 {
			sz = 4
		}

		if sReg, okS := args[2].(x86asm.Reg); okS {
			rc, _ := readRegTo(sReg, "x16", "w16")
			lines = append(lines, rc...)
		} else if mem, okMem := args[2].(x86asm.Mem); okMem {
			lines = append(lines, readMemTo(mem, nextPC, sz, "x16", "w16")...)
		} else {
			lines = append(lines, l.emitUnsupported(inst, "unsupported mulx src")...)
			return lines, nil
		}

		if sz == 8 {
			// RDX is x22
			lines = append(lines,
				fmt.Sprintf("    mul %s, x22, x16", descLo.X),
				fmt.Sprintf("    umulh %s, x22, x16", descHi.X),
			)
		} else {
			lines = append(lines,
				"    umull x17, w22, w16",
				fmt.Sprintf("    mov %s, w17", descLo.W),
				fmt.Sprintf("    lsr %s, x17, #32", descHi.X),
			)
		}
		return lines, nil
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported bmi op")...)
	return lines, nil
}
