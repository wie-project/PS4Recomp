package lifter

import (
	"fmt"
	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/disasm"
)

// liftVectorMove128 handles MOVAPS, MOVUPS, MOVDQA, MOVDQU.
func (l *Arm64Lifter) liftVectorMove128(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		if xmmDst, okXD := arm64XmmMap[dstReg]; okXD {
			if srcReg, okSrc := src.(x86asm.Reg); okSrc {
				if xmmSrc, okXS := arm64XmmMap[srcReg]; okXS {
					lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", xmmDst.V, xmmSrc.V))
					return lines, nil
				}
			}
			if mem, okMem := src.(x86asm.Mem); okMem {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", xmmDst.Q))
				return lines, nil
			}
		}
	}

	if mem, okMem := dst.(x86asm.Mem); okMem {
		if srcReg, okSrc := src.(x86asm.Reg); okSrc {
			if xmmSrc, okXS := arm64XmmMap[srcReg]; okXS {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, fmt.Sprintf("    str %s, [x28, x16]", xmmSrc.Q))
				return lines, nil
			}
		}
	}

	lines = append(lines, "    brk #0 // unsupported vector move")
	return lines, nil
}

// liftVectorMove32 handles MOVSS.
func (l *Arm64Lifter) liftVectorMove32(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		if xmmDst, okXD := arm64XmmMap[dstReg]; okXD {
			if srcReg, okSrc := src.(x86asm.Reg); okSrc {
				if xmmSrc, okXS := arm64XmmMap[srcReg]; okXS {
					lines = append(lines, fmt.Sprintf("    fmov %s, %s", xmmDst.S, xmmSrc.S))
					return lines, nil
				}
			}
			if mem, okMem := src.(x86asm.Mem); okMem {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", xmmDst.S))
				return lines, nil
			}
		}
	}

	if mem, okMem := dst.(x86asm.Mem); okMem {
		if srcReg, okSrc := src.(x86asm.Reg); okSrc {
			if xmmSrc, okXS := arm64XmmMap[srcReg]; okXS {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, fmt.Sprintf("    str %s, [x28, x16]", xmmSrc.S))
				return lines, nil
			}
		}
	}

	lines = append(lines, "    brk #0 // unsupported movss")
	return lines, nil
}

// liftVectorMove64 handles MOVSD.
func (l *Arm64Lifter) liftVectorMove64(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		if xmmDst, okXD := arm64XmmMap[dstReg]; okXD {
			if srcReg, okSrc := src.(x86asm.Reg); okSrc {
				if xmmSrc, okXS := arm64XmmMap[srcReg]; okXS {
					lines = append(lines, fmt.Sprintf("    fmov %s, %s", xmmDst.D, xmmSrc.D))
					return lines, nil
				}
			}
			if mem, okMem := src.(x86asm.Mem); okMem {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", xmmDst.D))
				return lines, nil
			}
		}
	}

	if mem, okMem := dst.(x86asm.Mem); okMem {
		if srcReg, okSrc := src.(x86asm.Reg); okSrc {
			if xmmSrc, okXS := arm64XmmMap[srcReg]; okXS {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, fmt.Sprintf("    str %s, [x28, x16]", xmmSrc.D))
				return lines, nil
			}
		}
	}

	lines = append(lines, "    brk #0 // unsupported movsd")
	return lines, nil
}

// liftMovdMovq handles MOVD, MOVQ, VMOVD, and VMOVQ between GPR/memory and XMM/YMM.
func (l *Arm64Lifter) liftMovdMovq(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	var handled bool
	var err error
	lines, handled, err = l.liftMmxMovdMovq(inst, nextPC, lines)
	if err != nil {
		return lines, err
	}
	if handled {
		return lines, nil
	}

	is64 := (inst.Inst.Op == x86asm.MOVQ || inst.Inst.Op == x86asm.VMOVQ)
	isAvx := (inst.Inst.Op == x86asm.VMOVD || inst.Inst.Op == x86asm.VMOVQ)

	// Case 1: Mem <- XMM
	if mem, okMem := dst.(x86asm.Mem); okMem {
		if srcReg, okSrc := src.(x86asm.Reg); okSrc {
			if _, _, _, sLowV, _, _, sLowD, okS := getVectorDesc(srcReg); okS {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				if is64 {
					lines = append(lines, fmt.Sprintf("    str %s, [x28, x16]", sLowD))
				} else {
					lines = append(lines, fmt.Sprintf("    str s%s, [x28, x16]", sLowV[1:]))
				}
				return lines, nil
			}
		}
	}

	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		// Case 2: XMM <- XMM (MOVQ / VMOVQ)
		if is64 {
			if _, _, _, dLowV, dHighV, _, dLowD, okD := getVectorDesc(dstReg); okD {
				if srcReg, okSrc := src.(x86asm.Reg); okSrc {
					if _, _, _, _, _, _, sLowD, okS := getVectorDesc(srcReg); okS {
						lines = append(lines, fmt.Sprintf("    fmov %s, %s", dLowD, sLowD))
						lines = append(lines, fmt.Sprintf("    mov %s.d[1], xzr", dLowV))
						if isAvx {
							lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
						}
						return lines, nil
					}
				}
			}
		}

		// Case 3: XMM <- GPR / Mem
		if _, _, _, dLowV, dHighV, _, dLowD, okD := getVectorDesc(dstReg); okD {
			if srcReg, okSrc := src.(x86asm.Reg); okSrc {
				if descSrc, okDesc := arm64RegMap[srcReg]; okDesc {
					if is64 {
						lines = append(lines, fmt.Sprintf("    fmov %s, %s", dLowD, descSrc.X))
						lines = append(lines, fmt.Sprintf("    mov %s.d[1], xzr", dLowV))
					} else {
						lines = append(lines, fmt.Sprintf("    fmov s%s, %s", dLowV[1:], descSrc.W))
						lines = append(lines, fmt.Sprintf("    mov %s.s[1], wzr", dLowV))
						lines = append(lines, fmt.Sprintf("    mov %s.d[1], xzr", dLowV))
					}
					if isAvx {
						lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
					}
					return lines, nil
				}
			}
			if mem, okMem := src.(x86asm.Mem); okMem {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				if is64 {
					lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", dLowD))
					lines = append(lines, fmt.Sprintf("    mov %s.d[1], xzr", dLowV))
				} else {
					lines = append(lines, fmt.Sprintf("    ldr s%s, [x28, x16]", dLowV[1:]))
					lines = append(lines, fmt.Sprintf("    mov %s.s[1], wzr", dLowV))
					lines = append(lines, fmt.Sprintf("    mov %s.d[1], xzr", dLowV))
				}
				if isAvx {
					lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
				}
				return lines, nil
			}
		}

		// Case 4: GPR <- XMM
		if descDst, okDesc := arm64RegMap[dstReg]; okDesc {
			if srcReg, okSrc := src.(x86asm.Reg); okSrc {
				if _, _, _, sLowV, _, _, sLowD, okS := getVectorDesc(srcReg); okS {
					if is64 {
						lines = append(lines, fmt.Sprintf("    fmov %s, %s", descDst.X, sLowD))
					} else {
						lines = append(lines, fmt.Sprintf("    fmov %s, s%s", descDst.W, sLowV[1:]))
					}
					return lines, nil
				}
			}
		}
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported movd/movq")...)
	return lines, nil
}

// liftVectorLogic handles XORPS, ANDPS, ORPS, PXOR, PAND, POR, PANDN.
func (l *Arm64Lifter) liftVectorLogic(inst disasm.Instruction, armOp string, lines []string) ([]string, error) {
	nextPC := inst.Address + uint64(inst.Inst.Len)

	lines, handled, err := l.liftMmxLogic(inst, armOp, nextPC, lines)
	if err != nil {
		return lines, err
	}
	if handled {
		return lines, nil
	}

	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	dstReg, okDst := dst.(x86asm.Reg)
	xmmDst, okXD := arm64XmmMap[dstReg]

	if okDst && okXD {
		if srcReg, okSrc := src.(x86asm.Reg); okSrc {
			if xmmSrc, okXS := arm64XmmMap[srcReg]; okXS {
				if armOp == "eor" && dstReg == srcReg {
					lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", xmmDst.V))
					return lines, nil
				}
				lines = append(lines, fmt.Sprintf("    %s %s.16b, %s.16b, %s.16b", armOp, xmmDst.V, xmmDst.V, xmmSrc.V))
				return lines, nil
			}
		} else if mem, okMem := src.(x86asm.Mem); okMem {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, "    ldr q16, [x28, x16]")
			lines = append(lines, fmt.Sprintf("    %s %s.16b, %s.16b, v16.16b", armOp, xmmDst.V, xmmDst.V))
			return lines, nil
		}
	}

	lines = append(lines, fmt.Sprintf("    brk #0 // unsupported vector %s", armOp))
	return lines, nil
}

// liftShuf handles PSHUFD, VPSHUFD, SHUFPS, VSHUFPS, SHUFPD, and VSHUFPD.
func (l *Arm64Lifter) liftShuf(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported shuf dst")...)
		return lines, nil
	}

	if op == x86asm.PSHUFD || op == x86asm.VPSHUFD {
		src := inst.Inst.Args[1]
		imm, okImm := inst.Inst.Args[2].(x86asm.Imm)
		if !okImm {
			lines = append(lines, l.emitUnsupported(inst, "unsupported pshufd imm")...)
			return lines, nil
		}

		sLowV := ""
		sHighV := ""
		if srcReg, okSrc := src.(x86asm.Reg); okSrc {
			if _, _, _, rLowV, rHighV, _, _, okS := getVectorDesc(srcReg); okS {
				sLowV = rLowV
				sHighV = rHighV
			}
		} else if mem, okMem := src.(x86asm.Mem); okMem {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, "    ldr q31, [x28, x16]")
			sLowV = "v31"
			if isYmm {
				lines = append(lines, "    add x16, x16, #16")
				lines = append(lines, "    ldr q30, [x28, x16]")
				sHighV = "v30"
			}
		}
		if sLowV == "" {
			lines = append(lines, l.emitUnsupported(inst, "unsupported pshufd src")...)
			return lines, nil
		}

		immVal := uint8(imm)
		if immVal == 0x00 {
			lines = append(lines, fmt.Sprintf("    dup %s.4s, %s.s[0]", dLowV, sLowV))
			if isYmm {
				lines = append(lines, fmt.Sprintf("    dup %s.4s, %s.s[0]", dHighV, sHighV))
			} else if op == x86asm.VPSHUFD {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			}
			return lines, nil
		} else if immVal == 0x55 {
			lines = append(lines, fmt.Sprintf("    dup %s.4s, %s.s[1]", dLowV, sLowV))
			if isYmm {
				lines = append(lines, fmt.Sprintf("    dup %s.4s, %s.s[1]", dHighV, sHighV))
			} else if op == x86asm.VPSHUFD {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			}
			return lines, nil
		} else if immVal == 0xaa {
			lines = append(lines, fmt.Sprintf("    dup %s.4s, %s.s[2]", dLowV, sLowV))
			if isYmm {
				lines = append(lines, fmt.Sprintf("    dup %s.4s, %s.s[2]", dHighV, sHighV))
			} else if op == x86asm.VPSHUFD {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			}
			return lines, nil
		} else if immVal == 0xff {
			lines = append(lines, fmt.Sprintf("    dup %s.4s, %s.s[3]", dLowV, sLowV))
			if isYmm {
				lines = append(lines, fmt.Sprintf("    dup %s.4s, %s.s[3]", dHighV, sHighV))
			} else if op == x86asm.VPSHUFD {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			}
			return lines, nil
		}

		var maskBytes [16]byte
		for lane := 0; lane < 4; lane++ {
			srcLane := (immVal >> (lane * 2)) & 3
			for b := 0; b < 4; b++ {
				maskBytes[lane*4+b] = srcLane*4 + byte(b)
			}
		}
		var low64, high64 uint64
		for i := 0; i < 8; i++ {
			low64 |= uint64(maskBytes[i]) << (i * 8)
			high64 |= uint64(maskBytes[8+i]) << (i * 8)
		}

		lines = append(lines, loadImm64("x16", low64)...)
		lines = append(lines, loadImm64("x17", high64)...)
		lines = append(lines, "    fmov d29, x16")
		lines = append(lines, "    mov v29.d[1], x17")
		lines = append(lines, fmt.Sprintf("    tbl %s.16b, {%s.16b}, v29.16b", dLowV, sLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    tbl %s.16b, {%s.16b}, v29.16b", dHighV, sHighV))
		} else if op == x86asm.VPSHUFD {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		return lines, nil
	}

	if op == x86asm.SHUFPS || op == x86asm.VSHUFPS {
		isAvx := (op == x86asm.VSHUFPS)
		var s1LowV, s1HighV string
		var src2 x86asm.Arg
		var imm x86asm.Arg
		if isAvx {
			if s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg); okS1 {
				_, _, _, rLow, rHigh, _, _, _ := getVectorDesc(s1Reg)
				s1LowV = rLow
				s1HighV = rHigh
			}
			src2 = inst.Inst.Args[2]
			imm = inst.Inst.Args[3]
		} else {
			s1LowV = dLowV
			s1HighV = dHighV
			src2 = inst.Inst.Args[1]
			imm = inst.Inst.Args[2]
		}

		immVal := uint8(imm.(x86asm.Imm))
		s2LowV := "v31"
		s2HighV := "v30"
		if s2Reg, okS2 := src2.(x86asm.Reg); okS2 {
			_, _, _, rLow, rHigh, _, _, _ := getVectorDesc(s2Reg)
			s2LowV = rLow
			s2HighV = rHigh
		} else if mem, okMem := src2.(x86asm.Mem); okMem {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, "    ldr q31, [x28, x16]")
			if isYmm {
				lines = append(lines, "    add x16, x16, #16")
				lines = append(lines, "    ldr q30, [x28, x16]")
			}
		}

		// Build the result in v28: the destination may alias either source.
		shuf := func(d, a, b string) {
			lines = append(lines, fmt.Sprintf("    mov v28.s[0], %s.s[%d]", a, immVal&3),
				fmt.Sprintf("    mov v28.s[1], %s.s[%d]", a, (immVal>>2)&3),
				fmt.Sprintf("    mov v28.s[2], %s.s[%d]", b, (immVal>>4)&3),
				fmt.Sprintf("    mov v28.s[3], %s.s[%d]", b, (immVal>>6)&3),
				fmt.Sprintf("    mov %s.16b, v28.16b", d))
		}
		shuf(dLowV, s1LowV, s2LowV)
		if isYmm {
			shuf(dHighV, s1HighV, s2HighV)
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		return lines, nil
	}

	if op == x86asm.SHUFPD || op == x86asm.VSHUFPD {
		isAvx := (op == x86asm.VSHUFPD)
		var s1LowV, s1HighV string
		var src2 x86asm.Arg
		var imm x86asm.Arg
		if isAvx {
			if s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg); okS1 {
				_, _, _, rLow, rHigh, _, _, _ := getVectorDesc(s1Reg)
				s1LowV = rLow
				s1HighV = rHigh
			}
			src2 = inst.Inst.Args[2]
			imm = inst.Inst.Args[3]
		} else {
			s1LowV = dLowV
			s1HighV = dHighV
			src2 = inst.Inst.Args[1]
			imm = inst.Inst.Args[2]
		}

		immVal := uint8(imm.(x86asm.Imm))
		s2LowV := "v31"
		s2HighV := "v30"
		if s2Reg, okS2 := src2.(x86asm.Reg); okS2 {
			_, _, _, rLow, rHigh, _, _, _ := getVectorDesc(s2Reg)
			s2LowV = rLow
			s2HighV = rHigh
		} else if mem, okMem := src2.(x86asm.Mem); okMem {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, "    ldr q31, [x28, x16]")
			if isYmm {
				lines = append(lines, "    add x16, x16, #16")
				lines = append(lines, "    ldr q30, [x28, x16]")
			}
		}

		// Build the result in v28: the destination may alias either source.
		lines = append(lines, fmt.Sprintf("    mov v28.d[0], %s.d[%d]", s1LowV, immVal&1),
			fmt.Sprintf("    mov v28.d[1], %s.d[%d]", s2LowV, (immVal>>1)&1),
			fmt.Sprintf("    mov %s.16b, v28.16b", dLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    mov v28.d[0], %s.d[%d]", s1HighV, (immVal>>2)&1),
				fmt.Sprintf("    mov v28.d[1], %s.d[%d]", s2HighV, (immVal>>3)&1),
				fmt.Sprintf("    mov %s.16b, v28.16b", dHighV))
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		return lines, nil
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported shuf op")...)
	return lines, nil
}

// liftPshufw handles PSHUFLW, VPSHUFLW, PSHUFHW, and VPSHUFHW.
func (l *Arm64Lifter) liftPshufw(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isAvx := (op == x86asm.VPSHUFLW || op == x86asm.VPSHUFHW)
	isHigh := (op == x86asm.PSHUFHW || op == x86asm.VPSHUFHW)

	var dstArg, srcArg, immArg x86asm.Arg
	if isAvx {
		dstArg = inst.Inst.Args[0]
		srcArg = inst.Inst.Args[1]
		immArg = inst.Inst.Args[2]
	} else {
		dstArg = inst.Inst.Args[0]
		srcArg = inst.Inst.Args[1]
		immArg = inst.Inst.Args[2]
	}

	dstReg, okDst := dstArg.(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pshufw dst")...)
		return lines, nil
	}

	imm, okImm := immArg.(x86asm.Imm)
	if !okImm {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pshufw imm")...)
		return lines, nil
	}

	sLowV := "v30"
	sHighV := "v31"
	if srcReg, okSrc := srcArg.(x86asm.Reg); okSrc {
		if _, _, _, rL, rH, _, _, okS := getVectorDesc(srcReg); okS {
			sLowV = rL
			sHighV = rH
		}
	} else if mem, okMem := srcArg.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16", "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pshufw src")...)
		return lines, nil
	}

	immVal := uint8(imm)
	var maskBytes [16]byte
	if !isHigh {
		// PSHUFLW: shuffle words 0..3 (bytes 0..7)
		for lane := 0; lane < 4; lane++ {
			srcLane := (immVal >> (lane * 2)) & 3
			maskBytes[lane*2] = srcLane * 2
			maskBytes[lane*2+1] = srcLane*2 + 1
		}
		for i := 8; i < 16; i++ {
			maskBytes[i] = byte(i)
		}
	} else {
		// PSHUFHW: shuffle words 4..7 (bytes 8..15)
		for i := 0; i < 8; i++ {
			maskBytes[i] = byte(i)
		}
		for lane := 0; lane < 4; lane++ {
			srcLane := (immVal >> (lane * 2)) & 3
			maskBytes[8+lane*2] = 8 + srcLane*2
			maskBytes[8+lane*2+1] = 8 + srcLane*2 + 1
		}
	}

	var low64, high64 uint64
	for i := 0; i < 8; i++ {
		low64 |= uint64(maskBytes[i]) << (i * 8)
		high64 |= uint64(maskBytes[8+i]) << (i * 8)
	}

	lines = append(lines, loadImm64("x16", low64)...)
	lines = append(lines, loadImm64("x17", high64)...)
	lines = append(lines, "    fmov d29, x16")
	lines = append(lines, "    mov v29.d[1], x17")
	lines = append(lines, fmt.Sprintf("    tbl %s.16b, {%s.16b}, v29.16b", dLowV, sLowV))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    tbl %s.16b, {%s.16b}, v29.16b", dHighV, sHighV))
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftVectorBinary handles 128-bit SIMD binary operations.
func (l *Arm64Lifter) liftVectorBinary(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	lines, handled, err := l.liftMmxBinary(inst, nextPC, lines)
	if err != nil {
		return lines, err
	}
	if handled {
		return lines, nil
	}

	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	xmmDst, okXD := arm64XmmMap[dstReg]
	if !okDst || !okXD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported simd binary dst")...)
		return lines, nil
	}

	src := inst.Inst.Args[1]
	srcRegStr := ""
	if srcReg, okSrc := src.(x86asm.Reg); okSrc {
		if xmmSrc, okXS := arm64XmmMap[srcReg]; okXS {
			srcRegStr = xmmSrc.V
		}
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q31, [x28, x16]")
		srcRegStr = "v31"
	}
	if srcRegStr == "" {
		lines = append(lines, l.emitUnsupported(inst, "unsupported simd binary src")...)
		return lines, nil
	}

	switch inst.Inst.Op {
	case x86asm.PADDB:
		lines = append(lines, fmt.Sprintf("    add %s.16b, %s.16b, %s.16b", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PADDW:
		lines = append(lines, fmt.Sprintf("    add %s.8h, %s.8h, %s.8h", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PADDD:
		lines = append(lines, fmt.Sprintf("    add %s.4s, %s.4s, %s.4s", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PADDQ:
		lines = append(lines, fmt.Sprintf("    add %s.2d, %s.2d, %s.2d", xmmDst.V, xmmDst.V, srcRegStr))

	case x86asm.PSUBB:
		lines = append(lines, fmt.Sprintf("    sub %s.16b, %s.16b, %s.16b", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PSUBW:
		lines = append(lines, fmt.Sprintf("    sub %s.8h, %s.8h, %s.8h", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PSUBD:
		lines = append(lines, fmt.Sprintf("    sub %s.4s, %s.4s, %s.4s", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PSUBQ:
		lines = append(lines, fmt.Sprintf("    sub %s.2d, %s.2d, %s.2d", xmmDst.V, xmmDst.V, srcRegStr))

	case x86asm.PCMPEQB:
		lines = append(lines, fmt.Sprintf("    cmeq %s.16b, %s.16b, %s.16b", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PCMPEQW:
		lines = append(lines, fmt.Sprintf("    cmeq %s.8h, %s.8h, %s.8h", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PCMPEQD:
		lines = append(lines, fmt.Sprintf("    cmeq %s.4s, %s.4s, %s.4s", xmmDst.V, xmmDst.V, srcRegStr))

	case x86asm.PMULLD:
		lines = append(lines, fmt.Sprintf("    mul %s.4s, %s.4s, %s.4s", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PMULLW:
		lines = append(lines, fmt.Sprintf("    mul %s.8h, %s.8h, %s.8h", xmmDst.V, xmmDst.V, srcRegStr))

	case x86asm.PMAXUB:
		lines = append(lines, fmt.Sprintf("    umax %s.16b, %s.16b, %s.16b", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PMINUB:
		lines = append(lines, fmt.Sprintf("    umin %s.16b, %s.16b, %s.16b", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PMAXSB:
		lines = append(lines, fmt.Sprintf("    smax %s.16b, %s.16b, %s.16b", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PMINSB:
		lines = append(lines, fmt.Sprintf("    smin %s.16b, %s.16b, %s.16b", xmmDst.V, xmmDst.V, srcRegStr))

	case x86asm.PMAXUW:
		lines = append(lines, fmt.Sprintf("    umax %s.8h, %s.8h, %s.8h", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PMINUW:
		lines = append(lines, fmt.Sprintf("    umin %s.8h, %s.8h, %s.8h", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PMAXSW:
		lines = append(lines, fmt.Sprintf("    smax %s.8h, %s.8h, %s.8h", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PMINSW:
		lines = append(lines, fmt.Sprintf("    smin %s.8h, %s.8h, %s.8h", xmmDst.V, xmmDst.V, srcRegStr))

	case x86asm.PMAXUD:
		lines = append(lines, fmt.Sprintf("    umax %s.4s, %s.4s, %s.4s", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PMINUD:
		lines = append(lines, fmt.Sprintf("    umin %s.4s, %s.4s, %s.4s", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PMAXSD:
		lines = append(lines, fmt.Sprintf("    smax %s.4s, %s.4s, %s.4s", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PMINSD:
		lines = append(lines, fmt.Sprintf("    smin %s.4s, %s.4s, %s.4s", xmmDst.V, xmmDst.V, srcRegStr))

	case x86asm.PADDSB:
		lines = append(lines, fmt.Sprintf("    sqadd %s.16b, %s.16b, %s.16b", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PADDSW:
		lines = append(lines, fmt.Sprintf("    sqadd %s.8h, %s.8h, %s.8h", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PADDUSB:
		lines = append(lines, fmt.Sprintf("    uqadd %s.16b, %s.16b, %s.16b", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PSUBSB:
		lines = append(lines, fmt.Sprintf("    sqsub %s.16b, %s.16b, %s.16b", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PSUBUSB:
		lines = append(lines, fmt.Sprintf("    uqsub %s.16b, %s.16b, %s.16b", xmmDst.V, xmmDst.V, srcRegStr))

	case x86asm.PCMPGTB:
		lines = append(lines, fmt.Sprintf("    cmgt %s.16b, %s.16b, %s.16b", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PCMPGTW:
		lines = append(lines, fmt.Sprintf("    cmgt %s.8h, %s.8h, %s.8h", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PCMPGTD:
		lines = append(lines, fmt.Sprintf("    cmgt %s.4s, %s.4s, %s.4s", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PCMPGTQ:
		lines = append(lines, fmt.Sprintf("    cmgt %s.2d, %s.2d, %s.2d", xmmDst.V, xmmDst.V, srcRegStr))
	case x86asm.PMULUDQ:
		lines = append(lines, fmt.Sprintf("    umull %s.2d, %s.2s, %s.2s", xmmDst.V, xmmDst.V, srcRegStr))
	default:
		lines = append(lines, l.emitUnsupported(inst, "unsupported simd binary op")...)
	}
	return lines, nil
}

// liftVectorByteShift handles PSRLDQ, PSLLDQ, VPSRLDQ, and VPSLLDQ instructions.
func (l *Arm64Lifter) liftVectorByteShift(inst disasm.Instruction, isLeft bool, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isAvx := (op == x86asm.VPSLLDQ || op == x86asm.VPSRLDQ)

	var dstArg, srcArg, immArg x86asm.Arg
	if isAvx {
		dstArg = inst.Inst.Args[0]
		srcArg = inst.Inst.Args[1]
		immArg = inst.Inst.Args[2]
	} else {
		dstArg = inst.Inst.Args[0]
		srcArg = inst.Inst.Args[0]
		immArg = inst.Inst.Args[1]
	}

	dstReg, okDst := dstArg.(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported byte shift dst")...)
		return lines, nil
	}

	imm, okImm := immArg.(x86asm.Imm)
	if !okImm {
		lines = append(lines, l.emitUnsupported(inst, "unsupported byte shift imm")...)
		return lines, nil
	}

	sLowV := dLowV
	sHighV := dHighV
	if isAvx {
		if srcReg, okSrc := srcArg.(x86asm.Reg); okSrc {
			if _, _, _, sL, sH, _, _, okS := getVectorDesc(srcReg); okS {
				sLowV = sL
				sHighV = sH
			}
		}
	}

	shift := int(imm)
	if shift >= 16 {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		return lines, nil
	}
	if shift <= 0 {
		if isAvx && dLowV != sLowV {
			lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, sLowV))
			if isYmm {
				lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dHighV, sHighV))
			} else {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			}
		}
		return lines, nil
	}

	lines = append(lines, "    movi v31.16b, #0")
	if isLeft {
		lines = append(lines, fmt.Sprintf("    ext %s.16b, v31.16b, %s.16b, #%d", dLowV, sLowV, 16-shift))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    ext %s.16b, v31.16b, %s.16b, #%d", dHighV, sHighV, 16-shift))
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
	} else {
		lines = append(lines, fmt.Sprintf("    ext %s.16b, %s.16b, v31.16b, #%d", dLowV, sLowV, shift))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    ext %s.16b, %s.16b, v31.16b, #%d", dHighV, sHighV, shift))
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
	}
	return lines, nil
}

// liftVectorShift handles packed integer shifts (PSLLW, PSLLD, PSLLQ, PSRLW, PSRLD, PSRLQ, PSRAW, PSRAD).
func (l *Arm64Lifter) liftVectorShift(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	lines, handled, err := l.liftMmxShift(inst, nextPC, lines)
	if err != nil {
		return lines, err
	}
	if handled {
		return lines, nil
	}

	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	xmmDst, okXD := arm64XmmMap[dstReg]
	if !okDst || !okXD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported simd shift dst")...)
		return lines, nil
	}

	src := inst.Inst.Args[1]
	imm, okImm := src.(x86asm.Imm)
	if okImm {
		shift := int64(imm)
		switch inst.Inst.Op {
		case x86asm.PSLLW:
			if shift >= 16 {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", xmmDst.V))
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    shl %s.8h, %s.8h, #%d", xmmDst.V, xmmDst.V, shift))
			}
		case x86asm.PSLLD:
			if shift >= 32 {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", xmmDst.V))
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    shl %s.4s, %s.4s, #%d", xmmDst.V, xmmDst.V, shift))
			}
		case x86asm.PSLLQ:
			if shift >= 64 {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", xmmDst.V))
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    shl %s.2d, %s.2d, #%d", xmmDst.V, xmmDst.V, shift))
			}
		case x86asm.PSRLW:
			if shift >= 16 {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", xmmDst.V))
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    ushr %s.8h, %s.8h, #%d", xmmDst.V, xmmDst.V, shift))
			}
		case x86asm.PSRLD:
			if shift >= 32 {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", xmmDst.V))
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    ushr %s.4s, %s.4s, #%d", xmmDst.V, xmmDst.V, shift))
			}
		case x86asm.PSRLQ:
			if shift >= 64 {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", xmmDst.V))
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    ushr %s.2d, %s.2d, #%d", xmmDst.V, xmmDst.V, shift))
			}
		case x86asm.PSRAW:
			if shift >= 16 {
				lines = append(lines, fmt.Sprintf("    sshr %s.8h, %s.8h, #16", xmmDst.V, xmmDst.V))
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    sshr %s.8h, %s.8h, #%d", xmmDst.V, xmmDst.V, shift))
			}
		case x86asm.PSRAD:
			if shift >= 32 {
				lines = append(lines, fmt.Sprintf("    sshr %s.4s, %s.4s, #32", xmmDst.V, xmmDst.V))
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    sshr %s.4s, %s.4s, #%d", xmmDst.V, xmmDst.V, shift))
			}
		default:
			lines = append(lines, l.emitUnsupported(inst, "unsupported simd shift op")...)
		}
		return lines, nil
	}

	// Non-immediate shift count: lower 64 bits of XMM or m64/m128
	if sReg, okSReg := src.(x86asm.Reg); okSReg {
		if _, _, _, _, _, _, sLowD, okS := getVectorDesc(sReg); okS {
			lines = append(lines, fmt.Sprintf("    fmov x16, %s", sLowD))
		} else {
			lines = append(lines, l.emitUnsupported(inst, "unsupported shift src reg")...)
			return lines, nil
		}
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr x16, [x28, x16]")
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported non-imm simd shift")...)
		return lines, nil
	}

	// x16 holds 64-bit shift count
	lines = append(lines,
		"    mrs x17, nzcv",
		"    str x17, [sp, #-16]!",
	)

	l.labelCounter++
	skipLabel := fmt.Sprintf("L_vec_shift_skip_%d", l.labelCounter)
	zeroLabel := fmt.Sprintf("L_vec_shift_zero_%d", l.labelCounter)

	op := inst.Inst.Op
	limit := 64
	if op == x86asm.PSLLW || op == x86asm.PSRLW || op == x86asm.PSRAW {
		limit = 16
	} else if op == x86asm.PSLLD || op == x86asm.PSRLD || op == x86asm.PSRAD {
		limit = 32
	}

	if op == x86asm.PSRAW || op == x86asm.PSRAD {
		// Arithmetic shift: if count >= limit, clamp to limit-1
		lines = append(lines,
			fmt.Sprintf("    cmp x16, #%d", limit),
			fmt.Sprintf("    mov x17, #%d", limit-1),
			"    csel x16, x17, x16, hs",
			"    neg x16, x16", // negative count for right shift with sshl
		)
		if limit == 16 {
			lines = append(lines,
				"    dup v31.8h, w16",
				fmt.Sprintf("    sshl %s.8h, %s.8h, v31.8h", xmmDst.V, xmmDst.V),
			)
		} else {
			lines = append(lines,
				"    dup v31.4s, w16",
				fmt.Sprintf("    sshl %s.4s, %s.4s, v31.4s", xmmDst.V, xmmDst.V),
			)
		}
	} else {
		// Logical shift: if count >= limit, result is 0
		lines = append(lines,
			fmt.Sprintf("    cmp x16, #%d", limit),
			fmt.Sprintf("    b.hs %s", zeroLabel),
		)
		isLeft := (op == x86asm.PSLLW || op == x86asm.PSLLD || op == x86asm.PSLLQ)
		if isLeft {
			switch limit {
			case 16:
				lines = append(lines,
					"    dup v31.8h, w16",
					fmt.Sprintf("    ushl %s.8h, %s.8h, v31.8h", xmmDst.V, xmmDst.V),
				)
			case 32:
				lines = append(lines,
					"    dup v31.4s, w16",
					fmt.Sprintf("    ushl %s.4s, %s.4s, v31.4s", xmmDst.V, xmmDst.V),
				)
			case 64:
				lines = append(lines,
					"    dup v31.2d, x16",
					fmt.Sprintf("    ushl %s.2d, %s.2d, v31.2d", xmmDst.V, xmmDst.V),
				)
			}
		} else {
			lines = append(lines, "    neg x16, x16")
			switch limit {
			case 16:
				lines = append(lines,
					"    dup v31.8h, w16",
					fmt.Sprintf("    ushl %s.8h, %s.8h, v31.8h", xmmDst.V, xmmDst.V),
				)
			case 32:
				lines = append(lines,
					"    dup v31.4s, w16",
					fmt.Sprintf("    ushl %s.4s, %s.4s, v31.4s", xmmDst.V, xmmDst.V),
				)
			case 64:
				lines = append(lines,
					"    dup v31.2d, x16",
					fmt.Sprintf("    ushl %s.2d, %s.2d, v31.2d", xmmDst.V, xmmDst.V),
				)
			}
		}
		lines = append(lines,
			fmt.Sprintf("    b %s", skipLabel),
			fmt.Sprintf("%s:", zeroLabel),
			fmt.Sprintf("    movi %s.16b, #0", xmmDst.V),
			fmt.Sprintf("%s:", skipLabel),
		)
	}

	lines = append(lines,
		"    ldr x17, [sp], #16",
		"    msr nzcv, x17",
	)

	return lines, nil
}

// liftAvxMove handles VMOVUPS, VMOVUPD, VMOVAPS, VMOVAPD, VMOVDQU, VMOVDQA.
func (l *Arm64Lifter) liftAvxMove(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	// Reg <- Mem
	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		if isYmm, dLowQ, dHighQ, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg); okD {
			if mem, okMem := src.(x86asm.Mem); okMem {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", dLowQ))
				if isYmm {
					lines = append(lines, "    add x16, x16, #16")
					lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", dHighQ))
				} else {
					lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
				}
				return lines, nil
			}
			if srcReg, okSrc := src.(x86asm.Reg); okSrc {
				if _, _, _, sLowV, sHighV, _, _, okS := getVectorDesc(srcReg); okS {
					lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, sLowV))
					if isYmm {
						lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dHighV, sHighV))
					} else {
						lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
					}
					return lines, nil
				}
			}
		}
	}

	// Mem <- Reg
	if mem, okMem := dst.(x86asm.Mem); okMem {
		if srcReg, okSrc := src.(x86asm.Reg); okSrc {
			if isYmm, sLowQ, sHighQ, _, _, _, _, okS := getVectorDesc(srcReg); okS {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, fmt.Sprintf("    str %s, [x28, x16]", sLowQ))
				if isYmm {
					lines = append(lines, "    add x16, x16, #16")
					lines = append(lines, fmt.Sprintf("    str %s, [x28, x16]", sHighQ))
				}
				return lines, nil
			}
		}
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported avx move")...)
	return lines, nil
}

// liftVzeroupper zeroes the upper 128 bits of all YMM registers (v16-v31).
func (l *Arm64Lifter) liftVzeroupper(lines []string) []string {
	for i := 16; i <= 31; i++ {
		lines = append(lines, fmt.Sprintf("    movi v%d.16b, #0", i))
	}
	return lines
}

// liftVbroadcastss handles VBROADCASTSS.
func (l *Arm64Lifter) liftVbroadcastss(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vbroadcastss dst")...)
		return lines, nil
	}

	src := inst.Inst.Args[1]
	if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    add x16, x28, x16")
		lines = append(lines, fmt.Sprintf("    ld1r {%s.4s}, [x16]", dLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dHighV, dLowV))
		} else {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		return lines, nil
	}
	if srcReg, okSrc := src.(x86asm.Reg); okSrc {
		if _, _, _, sLowV, _, _, _, okS := getVectorDesc(srcReg); okS {
			lines = append(lines, fmt.Sprintf("    dup %s.4s, %s.s[0]", dLowV, sLowV))
			if isYmm {
				lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dHighV, dLowV))
			} else {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			}
			return lines, nil
		}
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported vbroadcastss src")...)
	return lines, nil
}

// liftAvx3Op handles 3-operand and 2-operand packed operations (float, int, or logic).
func (l *Arm64Lifter) liftAvx3Op(inst disasm.Instruction, nextPC uint64, armOp string, isInt32 bool, lines []string) ([]string, error) {
	var dstReg, src1Reg x86asm.Reg
	var src2 x86asm.Arg
	var okDst, okSrc1 bool

	isVex := false
	if len(inst.Inst.Args) >= 3 && inst.Inst.Args[2] != nil {
		isVex = true
		dstReg, okDst = inst.Inst.Args[0].(x86asm.Reg)
		src1Reg, okSrc1 = inst.Inst.Args[1].(x86asm.Reg)
		src2 = inst.Inst.Args[2]
	} else {
		dstReg, okDst = inst.Inst.Args[0].(x86asm.Reg)
		src1Reg = dstReg
		okSrc1 = okDst
		src2 = inst.Inst.Args[1]
	}

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1 := getVectorDesc(src1Reg)
	if !okDst || !okSrc1 || !okD || !okS1 {
		lines = append(lines, l.emitUnsupported(inst, "unsupported packed op dst/src1")...)
		return lines, nil
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okSrc2 := src2.(x86asm.Reg); okSrc2 {
		if _, _, _, rLowV, rHighV, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
			s2LowV = rLowV
			s2HighV = rHighV
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16")
			lines = append(lines, "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported packed op src2")...)
		return lines, nil
	}

	laneSuffix := ".4s"
	if armOp == "eor" || armOp == "and" || armOp == "orr" {
		laneSuffix = ".16b"
	}
	_ = isInt32

	if armOp == "fmax" || armOp == "fmin" {
		lines = append(lines, x86MinMax(armOp == "fmax", ".4s", dLowV, s1LowV, s2LowV)...)
		if isYmm {
			lines = append(lines, x86MinMax(armOp == "fmax", ".4s", dHighV, s1HighV, s2HighV)...)
		} else if isVex {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		return lines, nil
	}

	lines = append(lines, fmt.Sprintf("    %s %s%s, %s%s, %s%s", armOp, dLowV, laneSuffix, s1LowV, laneSuffix, s2LowV, laneSuffix))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    %s %s%s, %s%s, %s%s", armOp, dHighV, laneSuffix, s1HighV, laneSuffix, s2HighV, laneSuffix))
	} else if isVex {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// x86MinMax emits x86 MAXPS/MINPS-style lane selection: max(a, b) = a > b ? a : b and
// min(a, b) = a < b ? a : b, so the second operand is returned when either is NaN or both are
// zero (ARM FMAX/FMIN would return NaN or order the zeros). v29 is the mask scratch register.
func x86MinMax(isMax bool, arr, d, a, b string) []string {
	cmp := fmt.Sprintf("    fcmgt v29%s, %s%s, %s%s", arr, a, arr, b, arr)
	if !isMax {
		cmp = fmt.Sprintf("    fcmgt v29%s, %s%s, %s%s", arr, b, arr, a, arr)
	}
	return []string{cmp,
		fmt.Sprintf("    bsl v29.16b, %s.16b, %s.16b", a, b),
		fmt.Sprintf("    mov %s.16b, v29.16b", d)}
}

// liftMovddup handles MOVDDUP and VMOVDDUP.
func (l *Arm64Lifter) liftMovddup(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, dLowD, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported movddup dst")...)
		return lines, nil
	}
	isAvx := (inst.Inst.Op == x86asm.VMOVDDUP)
	src := inst.Inst.Args[1]

	if srcReg, okSrc := src.(x86asm.Reg); okSrc {
		if _, _, _, sLowV, sHighV, _, _, okS := getVectorDesc(srcReg); okS {
			lines = append(lines, fmt.Sprintf("    dup %s.2d, %s.d[0]", dLowV, sLowV))
			if isYmm {
				lines = append(lines, fmt.Sprintf("    dup %s.2d, %s.d[0]", dHighV, sHighV))
			} else if isAvx {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			}
			return lines, nil
		}
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", dLowD))
		lines = append(lines, fmt.Sprintf("    dup %s.2d, %s.d[0]", dLowV, dLowV))
		if isYmm {
			lines = append(lines, "    add x16, x16, #16")
			lines = append(lines, "    ldr d31, [x28, x16]")
			lines = append(lines, fmt.Sprintf("    dup %s.2d, v31.d[0]", dHighV))
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		return lines, nil
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported movddup src")...)
	return lines, nil
}

// liftMovlpsMovhps handles MOVLPS, VMOVLPS, MOVHPS, VMOVHPS, MOVLHPS, VMOVLHPS, MOVHLPS, VMOVHLPS.
func (l *Arm64Lifter) liftMovlpsMovhps(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dst := inst.Inst.Args[0]

	// Store form: (V)MOVLPS / (V)MOVHPS / (V)MOVLPD / (V)MOVHPD mem, srcXmm
	if mem, okMem := dst.(x86asm.Mem); okMem {
		srcReg, okSrc := inst.Inst.Args[1].(x86asm.Reg)
		if _, _, _, sLowV, _, _, sLowD, okS := getVectorDesc(srcReg); okSrc && okS {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			if op == x86asm.MOVLPS || op == x86asm.VMOVLPS || op == x86asm.MOVLPD || op == x86asm.VMOVLPD {
				lines = append(lines, fmt.Sprintf("    str %s, [x28, x16]", sLowD))
			} else {
				lines = append(lines, fmt.Sprintf("    mov d31, %s.d[1]", sLowV))
				lines = append(lines, "    str d31, [x28, x16]")
			}
			return lines, nil
		}
	}

	// Load form: dst is Reg
	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		_, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
		if !okD {
			lines = append(lines, l.emitUnsupported(inst, "unsupported movlps/hps dst")...)
			return lines, nil
		}

		// 3-operand AVX form
		if op == x86asm.VMOVLPS || op == x86asm.VMOVHPS || op == x86asm.VMOVLHPS || op == x86asm.VMOVHLPS ||
			op == x86asm.VMOVLPD || op == x86asm.VMOVHPD {
			s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg)
			_, _, _, s1LowV, _, _, _, okDesc := getVectorDesc(s1Reg)
			if !okS1 || !okDesc {
				lines = append(lines, l.emitUnsupported(inst, "unsupported avx movlps/hps src1")...)
				return lines, nil
			}
			src2 := inst.Inst.Args[2]
			if op == x86asm.VMOVLHPS || op == x86asm.VMOVHLPS {
				// One ZIP reads both sources before writing: the destination may alias src2.
				if s2Reg, okS2 := src2.(x86asm.Reg); okS2 {
					if _, _, _, s2LowV, _, _, _, okDesc2 := getVectorDesc(s2Reg); okDesc2 {
						if op == x86asm.VMOVLHPS { // [s1.lo, s2.lo]
							lines = append(lines, fmt.Sprintf("    zip1 %s.2d, %s.2d, %s.2d", dLowV, s1LowV, s2LowV))
						} else { // [s2.hi, s1.hi]
							lines = append(lines, fmt.Sprintf("    zip2 %s.2d, %s.2d, %s.2d", dLowV, s2LowV, s1LowV))
						}
						lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
						return lines, nil
					}
				}
			} else if mem, okMem := src2.(x86asm.Mem); okMem {
				// VMOVLPS/VMOVHPS/VMOVLPD/VMOVHPD dst, src1, m64
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, "    ldr d31, [x28, x16]")
				if dLowV != s1LowV {
					lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1LowV))
				}
				lane := 0
				if op == x86asm.VMOVHPS || op == x86asm.VMOVHPD {
					lane = 1
				}
				lines = append(lines, fmt.Sprintf("    mov %s.d[%d], v31.d[0]", dLowV, lane))
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
				return lines, nil
			}
		}

		// 2-operand SSE form
		src := inst.Inst.Args[1]
		if op == x86asm.MOVLPS || op == x86asm.MOVLPD {
			if mem, okMem := src.(x86asm.Mem); okMem {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, "    ldr d31, [x28, x16]")
				lines = append(lines, fmt.Sprintf("    mov %s.d[0], v31.d[0]", dLowV))
				return lines, nil
			}
		} else if op == x86asm.MOVHPS || op == x86asm.MOVHPD {
			if mem, okMem := src.(x86asm.Mem); okMem {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, "    ldr d31, [x28, x16]")
				lines = append(lines, fmt.Sprintf("    mov %s.d[1], v31.d[0]", dLowV))
				return lines, nil
			}
		} else if op == x86asm.MOVLHPS {
			if sReg, okS := src.(x86asm.Reg); okS {
				if _, _, _, sLowV, _, _, _, okDesc := getVectorDesc(sReg); okDesc {
					lines = append(lines, fmt.Sprintf("    mov %s.d[1], %s.d[0]", dLowV, sLowV))
					return lines, nil
				}
			}
		} else if op == x86asm.MOVHLPS {
			if sReg, okS := src.(x86asm.Reg); okS {
				if _, _, _, sLowV, _, _, _, okDesc := getVectorDesc(sReg); okDesc {
					lines = append(lines, fmt.Sprintf("    mov %s.d[0], %s.d[1]", dLowV, sLowV))
					return lines, nil
				}
			}
		}
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported movlps/movhps op")...)
	return lines, nil
}

// liftAvxIntAlu handles VPADD*, VPSUB*, and VPCMPEQ* (B, W, D, Q).
func (l *Arm64Lifter) liftAvxIntAlu(inst disasm.Instruction, nextPC uint64, armOp string, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg)
	src2 := inst.Inst.Args[2]

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported avx int alu dst/src1")...)
		return lines, nil
	}

	lane := ".4s"
	switch op {
	case x86asm.VPADDB, x86asm.VPSUBB, x86asm.VPCMPEQB,
		x86asm.VPMAXUB, x86asm.VPMINUB, x86asm.VPMAXSB, x86asm.VPMINSB, x86asm.VPCMPGTB,
		x86asm.VPADDSB, x86asm.VPSUBSB, x86asm.VPADDUSB, x86asm.VPSUBUSB:
		lane = ".16b"
	case x86asm.VPADDW, x86asm.VPSUBW, x86asm.VPCMPEQW,
		x86asm.VPMULLW, x86asm.VPMAXUW, x86asm.VPMINUW, x86asm.VPMAXSW, x86asm.VPMINSW, x86asm.VPCMPGTW,
		x86asm.VPADDSW, x86asm.VPSUBSW, x86asm.VPADDUSW, x86asm.VPSUBUSW:
		lane = ".8h"
	case x86asm.VPADDD, x86asm.VPSUBD, x86asm.VPCMPEQD,
		x86asm.VPMULLD, x86asm.VPMAXUD, x86asm.VPMINUD, x86asm.VPMAXSD, x86asm.VPMINSD, x86asm.VPCMPGTD:
		lane = ".4s"
	case x86asm.VPADDQ, x86asm.VPSUBQ, x86asm.VPCMPEQQ, x86asm.VPCMPGTQ:
		lane = ".2d"
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okSrc2 := src2.(x86asm.Reg); okSrc2 {
		if _, _, _, rLowV, rHighV, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
			s2LowV = rLowV
			s2HighV = rHighV
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16")
			lines = append(lines, "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported avx int alu src2")...)
		return lines, nil
	}

	lines = append(lines, fmt.Sprintf("    %s %s%s, %s%s, %s%s", armOp, dLowV, lane, s1LowV, lane, s2LowV, lane))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    %s %s%s, %s%s, %s%s", armOp, dHighV, lane, s1HighV, lane, s2HighV, lane))
	} else {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftAvxPandn handles PANDN and VPANDN (dst = (~src1) & src2).
func (l *Arm64Lifter) liftAvxPandn(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	var dstReg, s1Reg x86asm.Reg
	var src2 x86asm.Arg
	isAvx := (inst.Inst.Op == x86asm.VPANDN)
	if isAvx {
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg, _ = inst.Inst.Args[1].(x86asm.Reg)
		src2 = inst.Inst.Args[2]
	} else {
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg = dstReg
		src2 = inst.Inst.Args[1]
	}

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vpandn dst/src1")...)
		return lines, nil
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okSrc2 := src2.(x86asm.Reg); okSrc2 {
		if _, _, _, rLowV, rHighV, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
			s2LowV = rLowV
			s2HighV = rHighV
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16")
			lines = append(lines, "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vpandn src2")...)
		return lines, nil
	}

	// ARM64 bic Rd, Rn, Rm computes Rn & ~Rm
	lines = append(lines, fmt.Sprintf("    bic %s.16b, %s.16b, %s.16b", dLowV, s2LowV, s1LowV))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    bic %s.16b, %s.16b, %s.16b", dHighV, s2HighV, s1HighV))
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftAvxUnpck handles (V)PUNPCK* and (V)UNPCK*.
func (l *Arm64Lifter) liftAvxUnpck(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	lines, handled, err := l.liftMmxUnpck(inst, nextPC, lines)
	if err != nil {
		return lines, err
	}
	if handled {
		return lines, nil
	}

	op := inst.Inst.Op
	var dstReg, s1Reg x86asm.Reg
	var src2 x86asm.Arg
	isAvx := false
	if len(inst.Inst.Args) >= 3 && inst.Inst.Args[2] != nil {
		isAvx = true
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg, _ = inst.Inst.Args[1].(x86asm.Reg)
		src2 = inst.Inst.Args[2]
	} else {
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg = dstReg
		src2 = inst.Inst.Args[1]
	}

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vpunpck dst/src1")...)
		return lines, nil
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okSrc2 := src2.(x86asm.Reg); okSrc2 {
		if _, _, _, rLowV, rHighV, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
			s2LowV = rLowV
			s2HighV = rHighV
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16")
			lines = append(lines, "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vpunpck src2")...)
		return lines, nil
	}

	var zipOp string
	var lane string

	switch op {
	case x86asm.PUNPCKLBW, x86asm.VPUNPCKLBW:
		zipOp = "zip1"
		lane = "16b"
	case x86asm.PUNPCKHBW, x86asm.VPUNPCKHBW:
		zipOp = "zip2"
		lane = "16b"
	case x86asm.PUNPCKLWD, x86asm.VPUNPCKLWD:
		zipOp = "zip1"
		lane = "8h"
	case x86asm.PUNPCKHWD, x86asm.VPUNPCKHWD:
		zipOp = "zip2"
		lane = "8h"
	case x86asm.PUNPCKLDQ, x86asm.VPUNPCKLDQ, x86asm.UNPCKLPS, x86asm.VUNPCKLPS:
		zipOp = "zip1"
		lane = "4s"
	case x86asm.PUNPCKHDQ, x86asm.VPUNPCKHDQ, x86asm.UNPCKHPS, x86asm.VUNPCKHPS:
		zipOp = "zip2"
		lane = "4s"
	case x86asm.PUNPCKLQDQ, x86asm.VPUNPCKLQDQ, x86asm.UNPCKLPD, x86asm.VUNPCKLPD:
		zipOp = "zip1"
		lane = "2d"
	case x86asm.PUNPCKHQDQ, x86asm.VPUNPCKHQDQ, x86asm.UNPCKHPD, x86asm.VUNPCKHPD:
		zipOp = "zip2"
		lane = "2d"
	default:
		lines = append(lines, l.emitUnsupported(inst, "unsupported vpunpck op")...)
		return lines, nil
	}

	lines = append(lines, fmt.Sprintf("    %s %s.%s, %s.%s, %s.%s", zipOp, dLowV, lane, s1LowV, lane, s2LowV, lane))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    %s %s.%s, %s.%s, %s.%s", zipOp, dHighV, lane, s1HighV, lane, s2HighV, lane))
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftAvxPmuludq handles VPMULUDQ.
func (l *Arm64Lifter) liftAvxPmuludq(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg)
	src2 := inst.Inst.Args[2]

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vpmuludq dst/src1")...)
		return lines, nil
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okSrc2 := src2.(x86asm.Reg); okSrc2 {
		if _, _, _, rLowV, rHighV, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
			s2LowV = rLowV
			s2HighV = rHighV
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16")
			lines = append(lines, "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vpmuludq src2")...)
		return lines, nil
	}

	lines = append(lines, fmt.Sprintf("    uzp1 v28.4s, %s.4s, %s.4s", s1LowV, s1LowV))
	lines = append(lines, fmt.Sprintf("    uzp1 v29.4s, %s.4s, %s.4s", s2LowV, s2LowV))
	lines = append(lines, fmt.Sprintf("    umull %s.2d, v28.2s, v29.2s", dLowV))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    uzp1 v28.4s, %s.4s, %s.4s", s1HighV, s1HighV))
		lines = append(lines, fmt.Sprintf("    uzp1 v29.4s, %s.4s, %s.4s", s2HighV, s2HighV))
		lines = append(lines, fmt.Sprintf("    umull %s.2d, v28.2s, v29.2s", dHighV))
	} else {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftBlend handles BLENDPS, VBLENDPS, BLENDPD, VBLENDPD, PBLENDW, VPBLENDW, PBLENDD, VPBLENDD,
// as well as variable blends BLENDVPS, VBLENDVPS, BLENDVPD, VBLENDVPD, PBLENDVB, VPBLENDVB.
func (l *Arm64Lifter) liftBlend(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op

	isVarBlend := (op == x86asm.BLENDVPS || op == x86asm.VBLENDVPS ||
		op == x86asm.BLENDVPD || op == x86asm.VBLENDVPD ||
		op == x86asm.PBLENDVB || op == x86asm.VPBLENDVB)

	isAvx := (op == x86asm.VBLENDPS || op == x86asm.VBLENDPD ||
		op == x86asm.VPBLENDW || op == x86asm.VPBLENDD ||
		op == x86asm.VBLENDVPS || op == x86asm.VBLENDVPD ||
		op == x86asm.VPBLENDVB)

	var dstArg, s1Arg, s2Arg, maskOrImmArg x86asm.Arg

	if isAvx {
		// 4 args: dst, src1, src2, imm/mask
		if len(inst.Inst.Args) < 4 || inst.Inst.Args[0] == nil || inst.Inst.Args[1] == nil || inst.Inst.Args[2] == nil || inst.Inst.Args[3] == nil {
			lines = append(lines, l.emitUnsupported(inst, "unsupported avx blend args")...)
			return lines, nil
		}
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[1]
		s2Arg = inst.Inst.Args[2]
		maskOrImmArg = inst.Inst.Args[3]
	} else {
		// SSE:
		if isVarBlend {
			// 2 explicit args: dst, src2 (mask is implicitly X0)
			if len(inst.Inst.Args) < 2 || inst.Inst.Args[0] == nil || inst.Inst.Args[1] == nil {
				lines = append(lines, l.emitUnsupported(inst, "unsupported sse var blend args")...)
				return lines, nil
			}
			dstArg = inst.Inst.Args[0]
			s1Arg = inst.Inst.Args[0]
			s2Arg = inst.Inst.Args[1]
			maskOrImmArg = x86asm.X0
		} else {
			// 3 args: dst, src2, imm
			if len(inst.Inst.Args) < 3 || inst.Inst.Args[0] == nil || inst.Inst.Args[1] == nil || inst.Inst.Args[2] == nil {
				lines = append(lines, l.emitUnsupported(inst, "unsupported sse blend args")...)
				return lines, nil
			}
			dstArg = inst.Inst.Args[0]
			s1Arg = inst.Inst.Args[0]
			s2Arg = inst.Inst.Args[1]
			maskOrImmArg = inst.Inst.Args[2]
		}
	}

	dstReg, okDst := dstArg.(x86asm.Reg)
	s1Reg, okS1 := s1Arg.(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported blend dst/src1")...)
		return lines, nil
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okSrc2 := s2Arg.(x86asm.Reg); okSrc2 {
		if _, _, _, rLowV, rHighV, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
			s2LowV = rLowV
			s2HighV = rHighV
		}
	} else if mem, okMem := s2Arg.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16")
			lines = append(lines, "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported blend src2")...)
		return lines, nil
	}

	if isVarBlend {
		maskReg, okMask := maskOrImmArg.(x86asm.Reg)
		_, _, _, mLowV, mHighV, _, _, okMD := getVectorDesc(maskReg)
		if !okMask || !okMD {
			lines = append(lines, l.emitUnsupported(inst, "unsupported blend mask reg")...)
			return lines, nil
		}

		var cmpSuffix string
		switch op {
		case x86asm.BLENDVPS, x86asm.VBLENDVPS:
			cmpSuffix = "4s"
		case x86asm.BLENDVPD, x86asm.VBLENDVPD:
			cmpSuffix = "2d"
		case x86asm.PBLENDVB, x86asm.VPBLENDVB:
			cmpSuffix = "16b"
		}

		lines = append(lines, fmt.Sprintf("    cmlt v28.%s, %s.%s, #0", cmpSuffix, mLowV, cmpSuffix))
		lines = append(lines, fmt.Sprintf("    bsl v28.16b, %s.16b, %s.16b", s2LowV, s1LowV))
		lines = append(lines, fmt.Sprintf("    mov %s.16b, v28.16b", dLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    cmlt v28.%s, %s.%s, #0", cmpSuffix, mHighV, cmpSuffix))
			lines = append(lines, fmt.Sprintf("    bsl v28.16b, %s.16b, %s.16b", s2HighV, s1HighV))
			lines = append(lines, fmt.Sprintf("    mov %s.16b, v28.16b", dHighV))
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		return lines, nil
	}

	imm, okImm := maskOrImmArg.(x86asm.Imm)
	if !okImm {
		lines = append(lines, l.emitUnsupported(inst, "unsupported blend imm")...)
		return lines, nil
	}
	immVal := uint8(imm)

	var lowMask0, lowMask1, highMask0, highMask1 uint64

	switch op {
	case x86asm.PBLENDW, x86asm.VPBLENDW:
		for i := 0; i < 4; i++ {
			if (immVal & (1 << i)) != 0 {
				lowMask0 |= (uint64(0xffff) << (i * 16))
			}
		}
		for i := 0; i < 4; i++ {
			if (immVal & (1 << (4 + i))) != 0 {
				lowMask1 |= (uint64(0xffff) << (i * 16))
			}
		}
		highMask0 = lowMask0
		highMask1 = lowMask1

	case x86asm.BLENDPS, x86asm.VBLENDPS, x86asm.VPBLENDD:
		if (immVal & 1) != 0 {
			lowMask0 |= 0x00000000ffffffff
		}
		if (immVal & 2) != 0 {
			lowMask0 |= 0xffffffff00000000
		}
		if (immVal & 4) != 0 {
			lowMask1 |= 0x00000000ffffffff
		}
		if (immVal & 8) != 0 {
			lowMask1 |= 0xffffffff00000000
		}

		if (immVal & 16) != 0 {
			highMask0 |= 0x00000000ffffffff
		}
		if (immVal & 32) != 0 {
			highMask0 |= 0xffffffff00000000
		}
		if (immVal & 64) != 0 {
			highMask1 |= 0x00000000ffffffff
		}
		if (immVal & 128) != 0 {
			highMask1 |= 0xffffffff00000000
		}

	case x86asm.BLENDPD, x86asm.VBLENDPD:
		if (immVal & 1) != 0 {
			lowMask0 = 0xffffffffffffffff
		}
		if (immVal & 2) != 0 {
			lowMask1 = 0xffffffffffffffff
		}
		if (immVal & 4) != 0 {
			highMask0 = 0xffffffffffffffff
		}
		if (immVal & 8) != 0 {
			highMask1 = 0xffffffffffffffff
		}
	}

	lines = append(lines, loadImm64("x16", lowMask0)...)
	lines = append(lines, loadImm64("x17", lowMask1)...)
	lines = append(lines, "    fmov d28, x16")
	lines = append(lines, "    mov v28.d[1], x17")
	lines = append(lines, fmt.Sprintf("    bsl v28.16b, %s.16b, %s.16b", s2LowV, s1LowV))
	lines = append(lines, fmt.Sprintf("    mov %s.16b, v28.16b", dLowV))

	if isYmm {
		lines = append(lines, loadImm64("x16", highMask0)...)
		lines = append(lines, loadImm64("x17", highMask1)...)
		lines = append(lines, "    fmov d28, x16")
		lines = append(lines, "    mov v28.d[1], x17")
		lines = append(lines, fmt.Sprintf("    bsl v28.16b, %s.16b, %s.16b", s2HighV, s1HighV))
		lines = append(lines, fmt.Sprintf("    mov %s.16b, v28.16b", dHighV))
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}

	return lines, nil
}

// liftVectorExtend handles (V)PMOVZX... and (V)PMOVSX... zero/sign extensions.
func (l *Arm64Lifter) liftVectorExtend(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pmov extend dst")...)
		return lines, nil
	}

	isAvx := (op == x86asm.VPMOVZXBW || op == x86asm.VPMOVZXBD || op == x86asm.VPMOVZXBQ ||
		op == x86asm.VPMOVZXWD || op == x86asm.VPMOVZXWQ || op == x86asm.VPMOVZXDQ ||
		op == x86asm.VPMOVSXBW || op == x86asm.VPMOVSXBD || op == x86asm.VPMOVSXBQ ||
		op == x86asm.VPMOVSXWD || op == x86asm.VPMOVSXWQ || op == x86asm.VPMOVSXDQ)

	isSigned := (op == x86asm.PMOVSXBW || op == x86asm.VPMOVSXBW ||
		op == x86asm.PMOVSXBD || op == x86asm.VPMOVSXBD ||
		op == x86asm.PMOVSXBQ || op == x86asm.VPMOVSXBQ ||
		op == x86asm.PMOVSXWD || op == x86asm.VPMOVSXWD ||
		op == x86asm.PMOVSXWQ || op == x86asm.VPMOVSXWQ ||
		op == x86asm.PMOVSXDQ || op == x86asm.VPMOVSXDQ)

	extOp := "uxtl"
	extOp2 := "uxtl2"
	if isSigned {
		extOp = "sxtl"
		extOp2 = "sxtl2"
	}

	isBW := (op == x86asm.PMOVZXBW || op == x86asm.VPMOVZXBW || op == x86asm.PMOVSXBW || op == x86asm.VPMOVSXBW)
	isBD := (op == x86asm.PMOVZXBD || op == x86asm.VPMOVZXBD || op == x86asm.PMOVSXBD || op == x86asm.VPMOVSXBD)
	isBQ := (op == x86asm.PMOVZXBQ || op == x86asm.VPMOVZXBQ || op == x86asm.PMOVSXBQ || op == x86asm.VPMOVSXBQ)
	isWD := (op == x86asm.PMOVZXWD || op == x86asm.VPMOVZXWD || op == x86asm.PMOVSXWD || op == x86asm.VPMOVSXWD)
	isWQ := (op == x86asm.PMOVZXWQ || op == x86asm.VPMOVZXWQ || op == x86asm.PMOVSXWQ || op == x86asm.VPMOVSXWQ)
	isDQ := (op == x86asm.PMOVZXDQ || op == x86asm.VPMOVZXDQ || op == x86asm.PMOVSXDQ || op == x86asm.VPMOVSXDQ)

	srcBytes := 8
	if isBW || isWD || isDQ {
		if isYmm {
			srcBytes = 16
		} else {
			srcBytes = 8
		}
	} else if isBD || isWQ {
		if isYmm {
			srcBytes = 8
		} else {
			srcBytes = 4
		}
	} else if isBQ {
		if isYmm {
			srcBytes = 4
		} else {
			srcBytes = 2
		}
	}

	src := inst.Inst.Args[1]
	sLowV := "v30"
	if srcReg, okSrc := src.(x86asm.Reg); okSrc {
		if _, _, _, sLow, _, _, _, okS := getVectorDesc(srcReg); okS {
			sLowV = sLow
		}
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		switch srcBytes {
		case 16:
			lines = append(lines, "    ldr q30, [x28, x16]")
		case 8:
			lines = append(lines, "    ldr d30, [x28, x16]")
		case 4:
			lines = append(lines, "    ldr s30, [x28, x16]")
		default: // 2
			lines = append(lines, "    ldrh w17, [x28, x16]", "    fmov s30, w17")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pmov extend src")...)
		return lines, nil
	}

	switch {
	case isBW:
		lines = append(lines, fmt.Sprintf("    %s %s.8h, %s.8b", extOp, dLowV, sLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    %s %s.8h, %s.16b", extOp2, dHighV, sLowV))
		}
	case isWD:
		lines = append(lines, fmt.Sprintf("    %s %s.4s, %s.4h", extOp, dLowV, sLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    %s %s.4s, %s.8h", extOp2, dHighV, sLowV))
		}
	case isDQ:
		lines = append(lines, fmt.Sprintf("    %s %s.2d, %s.2s", extOp, dLowV, sLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    %s %s.2d, %s.4s", extOp2, dHighV, sLowV))
		}
	case isBD:
		lines = append(lines, fmt.Sprintf("    %s v31.8h, %s.8b", extOp, sLowV))
		lines = append(lines, fmt.Sprintf("    %s %s.4s, v31.4h", extOp, dLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    %s %s.4s, v31.8h", extOp2, dHighV))
		}
	case isWQ:
		lines = append(lines, fmt.Sprintf("    %s v31.4s, %s.4h", extOp, sLowV))
		lines = append(lines, fmt.Sprintf("    %s %s.2d, v31.2s", extOp, dLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    %s %s.2d, v31.4s", extOp2, dHighV))
		}
	case isBQ:
		lines = append(lines,
			fmt.Sprintf("    %s v31.8h, %s.8b", extOp, sLowV),
			fmt.Sprintf("    %s v31.4s, v31.4h", extOp),
			fmt.Sprintf("    %s %s.2d, v31.2s", extOp, dLowV),
		)
		if isYmm {
			lines = append(lines, fmt.Sprintf("    %s %s.2d, v31.4s", extOp2, dHighV))
		}
	}

	if isAvx && !isYmm {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftPtestVtest handles PTEST, VPTEST, VTESTPS, VTESTPD, TESTPS, TESTPD.
func (l *Arm64Lifter) liftPtestVtest(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	dstReg, okDst := dst.(x86asm.Reg)
	isYmmDst, _, _, dLowV, dHighV, _, _, okDstDesc := getVectorDesc(dstReg)
	if !okDst || !okDstDesc {
		lines = append(lines, l.emitUnsupported(inst, "unsupported ptest dst")...)
		return lines, nil
	}

	var sLowV, sHighV string
	if srcReg, okSrc := src.(x86asm.Reg); okSrc {
		_, _, _, sL, sH, _, _, okSrcDesc := getVectorDesc(srcReg)
		if !okSrcDesc {
			lines = append(lines, l.emitUnsupported(inst, "unsupported ptest src reg")...)
			return lines, nil
		}
		sLowV = sL
		sHighV = sH
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		sLowV = "v30"
		if isYmmDst {
			lines = append(lines, "    add x17, x16, #16", "    ldr q31, [x28, x17]")
			sHighV = "v31"
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported ptest src")...)
		return lines, nil
	}

	lines = append(lines, fmt.Sprintf("    and v16.16b, %s.16b, %s.16b", sLowV, dLowV))
	lines = append(lines, fmt.Sprintf("    bic v17.16b, %s.16b, %s.16b", sLowV, dLowV))

	if isYmmDst {
		lines = append(lines, fmt.Sprintf("    and v18.16b, %s.16b, %s.16b", sHighV, dHighV))
		lines = append(lines, fmt.Sprintf("    bic v19.16b, %s.16b, %s.16b", sHighV, dHighV))
		lines = append(lines, "    orr v16.16b, v16.16b, v18.16b")
		lines = append(lines, "    orr v17.16b, v17.16b, v19.16b")
	}

	isTestPS := (op == x86asm.VTESTPS)
	isTestPD := (op == x86asm.VTESTPD)

	if isTestPS {
		lines = append(lines, loadImm64("x5", 0x8000000080000000)...)
		lines = append(lines,
			"    mov x16, v16.d[0]", "    mov x0, v16.d[1]", "    orr x16, x16, x0", "    and x16, x16, x5",
			"    mov x17, v17.d[0]", "    mov x0, v17.d[1]", "    orr x17, x17, x0", "    and x17, x17, x5",
		)
	} else if isTestPD {
		lines = append(lines, loadImm64("x5", 0x8000000000000000)...)
		lines = append(lines,
			"    mov x16, v16.d[0]", "    mov x0, v16.d[1]", "    orr x16, x16, x0", "    and x16, x16, x5",
			"    mov x17, v17.d[0]", "    mov x0, v17.d[1]", "    orr x17, x17, x0", "    and x17, x17, x5",
		)
	} else {
		lines = append(lines,
			"    mov x16, v16.d[0]", "    mov x5, v16.d[1]", "    orr x16, x16, x5",
			"    mov x17, v17.d[0]", "    mov x5, v17.d[1]", "    orr x17, x17, x5",
		)
	}

	// Set ARM64 NZCV condition flags:
	// ZF: x16 == 0 -> Z = 1 (bit 30)
	// CF: in x86 CF = (x17 == 0) ? 1 : 0 -> on ARM64 C = NOT CF = (x17 != 0) ? 1 : 0 (bit 29)
	lines = append(lines,
		"    cmp x16, #0",
		"    cset w16, eq",
		"    cmp x17, #0",
		"    cset w17, ne",
		"    add w16, w17, w16, lsl #1",
		"    lsl w16, w16, #29",
		"    msr nzcv, x16",
	)
	l.cpol = carrySub
	return lines, nil
}

// liftPack handles (V)PACKSSDW, (V)PACKSSWB, (V)PACKUSDW, (V)PACKUSWB.
func (l *Arm64Lifter) liftPack(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	lines, handled, err := l.liftMmxPack(inst, nextPC, lines)
	if err != nil {
		return lines, err
	}
	if handled {
		return lines, nil
	}

	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pack dst")...)
		return lines, nil
	}

	isAvx := (op == x86asm.VPACKSSDW || op == x86asm.VPACKSSWB || op == x86asm.VPACKUSDW || op == x86asm.VPACKUSWB)

	var s1LowV, s1HighV string
	var src2 x86asm.Arg
	if isAvx {
		s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg)
		if _, _, _, s1L, s1H, _, _, okS1D := getVectorDesc(s1Reg); okS1 && okS1D {
			s1LowV = s1L
			s1HighV = s1H
		}
		src2 = inst.Inst.Args[2]
	} else {
		s1LowV = dLowV
		s1HighV = dHighV
		src2 = inst.Inst.Args[1]
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okSrc2 := src2.(x86asm.Reg); okSrc2 {
		if _, _, _, rLowV, rHighV, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
			s2LowV = rLowV
			s2HighV = rHighV
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x17, x16, #16", "    ldr q31, [x28, x17]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pack src2")...)
		return lines, nil
	}

	switch op {
	case x86asm.PACKSSDW, x86asm.VPACKSSDW:
		lines = append(lines,
			fmt.Sprintf("    sqxtn v16.4h, %s.4s", s1LowV),
			fmt.Sprintf("    sqxtn2 v16.8h, %s.4s", s2LowV),
			fmt.Sprintf("    mov %s.16b, v16.16b", dLowV),
		)
		if isYmm {
			lines = append(lines,
				fmt.Sprintf("    sqxtn v17.4h, %s.4s", s1HighV),
				fmt.Sprintf("    sqxtn2 v17.8h, %s.4s", s2HighV),
				fmt.Sprintf("    mov %s.16b, v17.16b", dHighV),
			)
		}
	case x86asm.PACKSSWB, x86asm.VPACKSSWB:
		lines = append(lines,
			fmt.Sprintf("    sqxtn v16.8b, %s.8h", s1LowV),
			fmt.Sprintf("    sqxtn2 v16.16b, %s.8h", s2LowV),
			fmt.Sprintf("    mov %s.16b, v16.16b", dLowV),
		)
		if isYmm {
			lines = append(lines,
				fmt.Sprintf("    sqxtn v17.8b, %s.8h", s1HighV),
				fmt.Sprintf("    sqxtn2 v17.16b, %s.8h", s2HighV),
				fmt.Sprintf("    mov %s.16b, v17.16b", dHighV),
			)
		}
	case x86asm.PACKUSDW, x86asm.VPACKUSDW:
		lines = append(lines,
			fmt.Sprintf("    sqxtun v16.4h, %s.4s", s1LowV),
			fmt.Sprintf("    sqxtun2 v16.8h, %s.4s", s2LowV),
			fmt.Sprintf("    mov %s.16b, v16.16b", dLowV),
		)
		if isYmm {
			lines = append(lines,
				fmt.Sprintf("    sqxtun v17.4h, %s.4s", s1HighV),
				fmt.Sprintf("    sqxtun2 v17.8h, %s.4s", s2HighV),
				fmt.Sprintf("    mov %s.16b, v17.16b", dHighV),
			)
		}
	case x86asm.PACKUSWB, x86asm.VPACKUSWB:
		lines = append(lines,
			fmt.Sprintf("    sqxtun v16.8b, %s.8h", s1LowV),
			fmt.Sprintf("    sqxtun2 v16.16b, %s.8h", s2LowV),
			fmt.Sprintf("    mov %s.16b, v16.16b", dLowV),
		)
		if isYmm {
			lines = append(lines,
				fmt.Sprintf("    sqxtun v17.8b, %s.8h", s1HighV),
				fmt.Sprintf("    sqxtun2 v17.16b, %s.8h", s2HighV),
				fmt.Sprintf("    mov %s.16b, v17.16b", dHighV),
			)
		}
	}

	if isAvx && !isYmm {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftRcp handles (V)RCPPS and (V)RCPSS.
func (l *Arm64Lifter) liftRcp(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported rcp dst")...)
		return lines, nil
	}

	isScalar := (op == x86asm.RCPSS || op == x86asm.VRCPSS)
	isAvx := (op == x86asm.VRCPPS || op == x86asm.VRCPSS)

	var src2 x86asm.Arg
	if isAvx && isScalar {
		src2 = inst.Inst.Args[2]
	} else if isAvx {
		src2 = inst.Inst.Args[1]
	} else {
		src2 = inst.Inst.Args[1]
	}

	sLowV := "v30"
	sHighV := "v31"
	if s2Reg, okSrc2 := src2.(x86asm.Reg); okSrc2 {
		if _, _, _, rLowV, rHighV, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
			sLowV = rLowV
			sHighV = rHighV
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		if isScalar {
			lines = append(lines, "    ldr s30, [x28, x16]")
		} else {
			lines = append(lines, "    ldr q30, [x28, x16]")
			if isYmm {
				lines = append(lines, "    add x17, x16, #16", "    ldr q31, [x28, x17]")
			}
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported rcp src")...)
		return lines, nil
	}

	if isScalar {
		if isAvx {
			if s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg); okS1 {
				if _, _, _, s1Low, _, _, _, okS1D := getVectorDesc(s1Reg); okS1D && dLowV != s1Low {
					lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1Low))
				}
			}
		}
		lines = append(lines, x86Recip(false, "v16", sLowV)...)
		lines = append(lines, fmt.Sprintf("    mov %s.s[0], v16.s[0]", dLowV))
		if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
	} else {
		lines = append(lines, x86Recip(false, dLowV, sLowV)...)
		if isYmm {
			lines = append(lines, x86Recip(false, dHighV, sHighV)...)
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
	}
	return lines, nil
}

// flushDenormals4s writes v (four floats) to d with denormals replaced by zero of the same
// sign: all bits but the sign are cleared where the exponent field is zero. Scratch: v25, v26.
func flushDenormals4s(d, v string) []string {
	return []string{
		fmt.Sprintf("    shl v25.4s, %s.4s, #1", v),
		"    movi v26.4s, #0x1, lsl #24",
		"    cmhi v25.4s, v26.4s, v25.4s",
		"    movi v26.4s, #0x80, lsl #24",
		"    orn v25.16b, v26.16b, v25.16b",
		fmt.Sprintf("    and %s.16b, %s.16b, v25.16b", d, v),
	}
}

// x86Recip emits RCPPS/RSQRTPS (rsqrt) on four floats of vector register a into d with the
// x86 behaviour: denormal inputs count as zero (sign kept), denormal results (RCPPS of huge
// values) are flushed to zero, and the precision meets x86's bound (relative error <=
// 1.5 * 2^-12): the ARM estimate (about 8 bits) is refined by one Newton-Raphson step
// (FRECPS/FRSQRTS, which give 2.0 / 1.5 for 0 * inf, so zero and infinite inputs stay exact).
// Scalar forms use lane 0. v24-v27 are scratch; d may alias a.
func x86Recip(rsqrt bool, d, a string) []string {
	out := flushDenormals4s("v24", a)
	if rsqrt {
		return append(out,
			"    frsqrte v27.4s, v24.4s",
			"    fmul v26.4s, v27.4s, v27.4s",
			"    frsqrts v26.4s, v26.4s, v24.4s",             // (3 - a * x0^2) / 2
			fmt.Sprintf("    fmul %s.4s, v27.4s, v26.4s", d)) // never denormal
	}
	out = append(out,
		"    frecpe v27.4s, v24.4s",
		"    frecps v26.4s, v24.4s, v27.4s", // 2 - a * x0
		"    fmul v27.4s, v27.4s, v26.4s")
	return append(out, flushDenormals4s(d, "v27")...)
}

// liftAvxShift handles packed integer shifts by immediate (VPSLLQ, VPSRLQ, VPSLLD, VPSRLD, VPSRAD, VPSLLW, VPSRLW, VPSRAW).
func (l *Arm64Lifter) liftAvxShift(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg)
	src2 := inst.Inst.Args[2]

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vpshl/shr dst/src1")...)
		return lines, nil
	}

	if imm, okImm := src2.(x86asm.Imm); okImm {
		shift := uint64(imm)
		if shift == 0 {
			lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1LowV))
			if isYmm {
				lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dHighV, s1HighV))
			} else {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			}
			return lines, nil
		}

		switch op {
		case x86asm.VPSLLQ:
			if shift >= 64 {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dLowV))
				if isYmm {
					lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
				}
			} else {
				lines = append(lines, fmt.Sprintf("    shl %s.2d, %s.2d, #%d", dLowV, s1LowV, shift))
				if isYmm {
					lines = append(lines, fmt.Sprintf("    shl %s.2d, %s.2d, #%d", dHighV, s1HighV, shift))
				}
			}
		case x86asm.VPSRLQ:
			if shift >= 64 {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dLowV))
				if isYmm {
					lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
				}
			} else {
				lines = append(lines, fmt.Sprintf("    ushr %s.2d, %s.2d, #%d", dLowV, s1LowV, shift))
				if isYmm {
					lines = append(lines, fmt.Sprintf("    ushr %s.2d, %s.2d, #%d", dHighV, s1HighV, shift))
				}
			}
		case x86asm.VPSLLD:
			if shift >= 32 {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dLowV))
				if isYmm {
					lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
				}
			} else {
				lines = append(lines, fmt.Sprintf("    shl %s.4s, %s.4s, #%d", dLowV, s1LowV, shift))
				if isYmm {
					lines = append(lines, fmt.Sprintf("    shl %s.4s, %s.4s, #%d", dHighV, s1HighV, shift))
				}
			}
		case x86asm.VPSRLD:
			if shift >= 32 {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dLowV))
				if isYmm {
					lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
				}
			} else {
				lines = append(lines, fmt.Sprintf("    ushr %s.4s, %s.4s, #%d", dLowV, s1LowV, shift))
				if isYmm {
					lines = append(lines, fmt.Sprintf("    ushr %s.4s, %s.4s, #%d", dHighV, s1HighV, shift))
				}
			}
		case x86asm.VPSRAD:
			sVal := shift
			if sVal >= 32 {
				sVal = 31
			}
			lines = append(lines, fmt.Sprintf("    sshr %s.4s, %s.4s, #%d", dLowV, s1LowV, sVal))
			if isYmm {
				lines = append(lines, fmt.Sprintf("    sshr %s.4s, %s.4s, #%d", dHighV, s1HighV, sVal))
			}
		case x86asm.VPSLLW:
			if shift >= 16 {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dLowV))
				if isYmm {
					lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
				}
			} else {
				lines = append(lines, fmt.Sprintf("    shl %s.8h, %s.8h, #%d", dLowV, s1LowV, shift))
				if isYmm {
					lines = append(lines, fmt.Sprintf("    shl %s.8h, %s.8h, #%d", dHighV, s1HighV, shift))
				}
			}
		case x86asm.VPSRLW:
			if shift >= 16 {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dLowV))
				if isYmm {
					lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
				}
			} else {
				lines = append(lines, fmt.Sprintf("    ushr %s.8h, %s.8h, #%d", dLowV, s1LowV, shift))
				if isYmm {
					lines = append(lines, fmt.Sprintf("    ushr %s.8h, %s.8h, #%d", dHighV, s1HighV, shift))
				}
			}
		case x86asm.VPSRAW:
			sVal := shift
			if sVal >= 16 {
				sVal = 15
			}
			lines = append(lines, fmt.Sprintf("    sshr %s.8h, %s.8h, #%d", dLowV, s1LowV, sVal))
			if isYmm {
				lines = append(lines, fmt.Sprintf("    sshr %s.8h, %s.8h, #%d", dHighV, s1HighV, sVal))
			}
		default:
			lines = append(lines, l.emitUnsupported(inst, "unsupported vpshl/shr op")...)
			return lines, nil
		}

		if !isYmm {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		return lines, nil
	}

	// Non-immediate shift count for AVX: lower 64-bit of src2 (XMM or m64/m128)
	if s2Reg, okS2 := src2.(x86asm.Reg); okS2 {
		if _, _, _, _, _, _, s2LowD, okS := getVectorDesc(s2Reg); okS {
			lines = append(lines, fmt.Sprintf("    fmov x16, %s", s2LowD))
		} else {
			lines = append(lines, l.emitUnsupported(inst, "unsupported vpshl/shr src2 reg")...)
			return lines, nil
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr x16, [x28, x16]")
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vpshl/shr non-imm src2")...)
		return lines, nil
	}

	lines = append(lines,
		"    mrs x17, nzcv",
		"    str x17, [sp, #-16]!",
	)

	l.labelCounter++
	skipLabel := fmt.Sprintf("L_vshift_skip_%d", l.labelCounter)
	zeroLabel := fmt.Sprintf("L_vshift_zero_%d", l.labelCounter)

	limit := 64
	if op == x86asm.VPSLLW || op == x86asm.VPSRLW || op == x86asm.VPSRAW {
		limit = 16
	} else if op == x86asm.VPSLLD || op == x86asm.VPSRLD || op == x86asm.VPSRAD {
		limit = 32
	}

	if op == x86asm.VPSRAW || op == x86asm.VPSRAD {
		// Arithmetic shift: if count >= limit, clamp to limit-1
		lines = append(lines,
			fmt.Sprintf("    cmp x16, #%d", limit),
			fmt.Sprintf("    mov x17, #%d", limit-1),
			"    csel x16, x17, x16, hs",
			"    neg x16, x16", // negative count for right shift with sshl
		)
		if limit == 16 {
			lines = append(lines,
				"    dup v31.8h, w16",
				fmt.Sprintf("    sshl %s.8h, %s.8h, v31.8h", dLowV, s1LowV),
			)
			if isYmm {
				lines = append(lines, fmt.Sprintf("    sshl %s.8h, %s.8h, v31.8h", dHighV, s1HighV))
			}
		} else {
			lines = append(lines,
				"    dup v31.4s, w16",
				fmt.Sprintf("    sshl %s.4s, %s.4s, v31.4s", dLowV, s1LowV),
			)
			if isYmm {
				lines = append(lines, fmt.Sprintf("    sshl %s.4s, %s.4s, v31.4s", dHighV, s1HighV))
			}
		}
	} else {
		// Logical shift: if count >= limit, result is 0
		lines = append(lines,
			fmt.Sprintf("    cmp x16, #%d", limit),
			fmt.Sprintf("    b.hs %s", zeroLabel),
		)
		isLeft := (op == x86asm.VPSLLW || op == x86asm.VPSLLD || op == x86asm.VPSLLQ)
		if isLeft {
			switch limit {
			case 16:
				lines = append(lines,
					"    dup v31.8h, w16",
					fmt.Sprintf("    ushl %s.8h, %s.8h, v31.8h", dLowV, s1LowV),
				)
				if isYmm {
					lines = append(lines, fmt.Sprintf("    ushl %s.8h, %s.8h, v31.8h", dHighV, s1HighV))
				}
			case 32:
				lines = append(lines,
					"    dup v31.4s, w16",
					fmt.Sprintf("    ushl %s.4s, %s.4s, v31.4s", dLowV, s1LowV),
				)
				if isYmm {
					lines = append(lines, fmt.Sprintf("    ushl %s.4s, %s.4s, v31.4s", dHighV, s1HighV))
				}
			case 64:
				lines = append(lines,
					"    dup v31.2d, x16",
					fmt.Sprintf("    ushl %s.2d, %s.2d, v31.2d", dLowV, s1LowV),
				)
				if isYmm {
					lines = append(lines, fmt.Sprintf("    ushl %s.2d, %s.2d, v31.2d", dHighV, s1HighV))
				}
			}
		} else {
			lines = append(lines, "    neg x16, x16")
			switch limit {
			case 16:
				lines = append(lines,
					"    dup v31.8h, w16",
					fmt.Sprintf("    ushl %s.8h, %s.8h, v31.8h", dLowV, s1LowV),
				)
				if isYmm {
					lines = append(lines, fmt.Sprintf("    ushl %s.8h, %s.8h, v31.8h", dHighV, s1HighV))
				}
			case 32:
				lines = append(lines,
					"    dup v31.4s, w16",
					fmt.Sprintf("    ushl %s.4s, %s.4s, v31.4s", dLowV, s1LowV),
				)
				if isYmm {
					lines = append(lines, fmt.Sprintf("    ushl %s.4s, %s.4s, v31.4s", dHighV, s1HighV))
				}
			case 64:
				lines = append(lines,
					"    dup v31.2d, x16",
					fmt.Sprintf("    ushl %s.2d, %s.2d, v31.2d", dLowV, s1LowV),
				)
				if isYmm {
					lines = append(lines, fmt.Sprintf("    ushl %s.2d, %s.2d, v31.2d", dHighV, s1HighV))
				}
			}
		}
		lines = append(lines,
			fmt.Sprintf("    b %s", skipLabel),
			fmt.Sprintf("%s:", zeroLabel),
			fmt.Sprintf("    movi %s.16b, #0", dLowV),
		)
		if isYmm {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		lines = append(lines, fmt.Sprintf("%s:", skipLabel))
	}

	if !isYmm {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	lines = append(lines,
		"    ldr x17, [sp], #16",
		"    msr nzcv, x17",
	)
	return lines, nil
}

// liftVbroadcastf128 handles VBROADCASTF128 dst_ymm, m128.
func (l *Arm64Lifter) liftVbroadcastf128(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	isYmm, dLowQ, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD || !isYmm {
		lines = append(lines, l.emitUnsupported(inst, "vbroadcastf128 requires YMM destination")...)
		return lines, nil
	}

	src := inst.Inst.Args[1]
	if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", dLowQ))
		lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dHighV, dLowV))
		return lines, nil
	}
	if srcReg, okSrc := src.(x86asm.Reg); okSrc {
		if _, _, _, sLowV, _, _, _, okS := getVectorDesc(srcReg); okS {
			lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, sLowV))
			lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dHighV, sLowV))
			return lines, nil
		}
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported vbroadcastf128 src")...)
	return lines, nil
}

// liftVbroadcastsd handles VBROADCASTSD dst_xmm/ymm, m64/xmm.
func (l *Arm64Lifter) liftVbroadcastsd(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, dLowD, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vbroadcastsd dst")...)
		return lines, nil
	}

	src := inst.Inst.Args[1]
	if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", dLowD))
		lines = append(lines, fmt.Sprintf("    dup %s.2d, %s.d[0]", dLowV, dLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dHighV, dLowV))
		} else {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		return lines, nil
	}
	if srcReg, okSrc := src.(x86asm.Reg); okSrc {
		if _, _, _, sLowV, _, _, _, okS := getVectorDesc(srcReg); okS {
			lines = append(lines, fmt.Sprintf("    dup %s.2d, %s.d[0]", dLowV, sLowV))
			if isYmm {
				lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dHighV, dLowV))
			} else {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			}
			return lines, nil
		}
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported vbroadcastsd src")...)
	return lines, nil
}

// liftVextractf128 handles VEXTRACTF128 and VEXTRACTI128 dst_xmm/m128, src_ymm, imm8.
func (l *Arm64Lifter) liftVextractf128(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]
	immArg, okImm := inst.Inst.Args[2].(x86asm.Imm)
	if !okImm {
		lines = append(lines, l.emitUnsupported(inst, "vextractf128 missing imm")...)
		return lines, nil
	}
	srcReg, okSrc := src.(x86asm.Reg)
	_, sLowQ, sHighQ, sLowV, sHighV, _, _, okS := getVectorDesc(srcReg)
	if !okSrc || !okS {
		lines = append(lines, l.emitUnsupported(inst, "vextractf128 invalid src")...)
		return lines, nil
	}

	sel := uint8(immArg) & 1
	srcQ := sLowQ
	srcV := sLowV
	if sel == 1 {
		srcQ = sHighQ
		srcV = sHighV
	}

	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		if _, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg); okD {
			lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, srcV))
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			return lines, nil
		}
	}
	if mem, okMem := dst.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, fmt.Sprintf("    str %s, [x28, x16]", srcQ))
		return lines, nil
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported vextractf128 dst")...)
	return lines, nil
}

// liftVinsertf128 handles VINSERTF128 and VINSERTI128 dst_ymm, src1_ymm, src2_xmm/m128, imm8.
func (l *Arm64Lifter) liftVinsertf128(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	src1Reg, okSrc1 := inst.Inst.Args[1].(x86asm.Reg)
	src2 := inst.Inst.Args[2]
	immArg, okImm := inst.Inst.Args[3].(x86asm.Imm)

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1 := getVectorDesc(src1Reg)
	if !okDst || !okSrc1 || !okImm || !okD || !okS1 || !isYmm {
		lines = append(lines, l.emitUnsupported(inst, "vinsertf128 invalid operands")...)
		return lines, nil
	}

	sel := uint8(immArg) & 1

	// Load src2 into temporary v16 to completely avoid any register aliasing issues
	if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q16, [x28, x16]")
	} else if src2Reg, okSrc2 := src2.(x86asm.Reg); okSrc2 {
		if _, _, _, s2LowV, _, _, _, okS2 := getVectorDesc(src2Reg); okS2 {
			lines = append(lines, fmt.Sprintf("    mov v16.16b, %s.16b", s2LowV))
		} else {
			lines = append(lines, l.emitUnsupported(inst, "unsupported vinsertf128 src2 reg")...)
			return lines, nil
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vinsertf128 src2")...)
		return lines, nil
	}

	if sel == 0 {
		lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dHighV, s1HighV))
		lines = append(lines, fmt.Sprintf("    mov %s.16b, v16.16b", dLowV))
	} else {
		lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1LowV))
		lines = append(lines, fmt.Sprintf("    mov %s.16b, v16.16b", dHighV))
	}

	return lines, nil
}

// liftExtractps handles EXTRACTPS and VEXTRACTPS dst_r32/m32, src_xmm, imm8.
func (l *Arm64Lifter) liftExtractps(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]
	immArg, okImm := inst.Inst.Args[2].(x86asm.Imm)
	if !okImm {
		lines = append(lines, l.emitUnsupported(inst, "extractps missing imm")...)
		return lines, nil
	}
	srcReg, okSrc := src.(x86asm.Reg)
	_, _, _, sLowV, _, _, _, okS := getVectorDesc(srcReg)
	if !okSrc || !okS {
		lines = append(lines, l.emitUnsupported(inst, "extractps invalid src")...)
		return lines, nil
	}

	sel := uint8(immArg) & 3
	lines = append(lines, fmt.Sprintf("    umov w16, %s.s[%d]", sLowV, sel))

	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		storeCode, okStore := writeRegFrom(dstReg, "x16", "w16")
		if okStore {
			lines = append(lines, storeCode...)
			return lines, nil
		}
	}
	if mem, okMem := dst.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x17")...)
		lines = append(lines, "    str w16, [x28, x17]")
		return lines, nil
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported extractps dst")...)
	return lines, nil
}

// liftInsertps handles INSERTPS and VINSERTPS dst_xmm, [src1_xmm], src2_xmm/m32, imm8.
func (l *Arm64Lifter) liftInsertps(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	var dstReg, src1Reg x86asm.Reg
	var src2 x86asm.Arg
	var immArg x86asm.Imm
	var okImm bool

	isVex := inst.Inst.Op == x86asm.VINSERTPS
	if isVex {
		var okDst, okSrc1 bool
		dstReg, okDst = inst.Inst.Args[0].(x86asm.Reg)
		src1Reg, okSrc1 = inst.Inst.Args[1].(x86asm.Reg)
		src2 = inst.Inst.Args[2]
		immArg, okImm = inst.Inst.Args[3].(x86asm.Imm)
		if !okDst || !okSrc1 || !okImm {
			lines = append(lines, l.emitUnsupported(inst, "vinsertps invalid operands")...)
			return lines, nil
		}
	} else {
		var okDst bool
		dstReg, okDst = inst.Inst.Args[0].(x86asm.Reg)
		src1Reg = dstReg
		src2 = inst.Inst.Args[1]
		immArg, okImm = inst.Inst.Args[2].(x86asm.Imm)
		if !okDst || !okImm {
			lines = append(lines, l.emitUnsupported(inst, "insertps invalid operands")...)
			return lines, nil
		}
	}

	_, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, _, _, _, okS1 := getVectorDesc(src1Reg)
	if !okD || !okS1 {
		lines = append(lines, l.emitUnsupported(inst, "insertps vector reg not found")...)
		return lines, nil
	}

	imm8 := uint8(immArg)
	countS := (imm8 >> 6) & 3
	countD := (imm8 >> 4) & 3
	zmask := imm8 & 0xF

	// Load source element into temporary scalar v16.s[0]
	if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr s16, [x28, x16]")
	} else if src2Reg, okSrc2 := src2.(x86asm.Reg); okSrc2 {
		if _, _, _, s2LowV, _, _, _, okS2 := getVectorDesc(src2Reg); okS2 {
			lines = append(lines, fmt.Sprintf("    mov v16.s[0], %s.s[%d]", s2LowV, countS))
		} else {
			lines = append(lines, l.emitUnsupported(inst, "insertps invalid src2 reg")...)
			return lines, nil
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "insertps invalid src2")...)
		return lines, nil
	}

	// Copy base vector src1 to dst if different
	if dLowV != s1LowV {
		lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1LowV))
	}

	// Insert selected element
	lines = append(lines, fmt.Sprintf("    mov %s.s[%d], v16.s[0]", dLowV, countD))

	// Zero masked elements
	for i := 0; i < 4; i++ {
		if zmask&(1<<i) != 0 {
			lines = append(lines, fmt.Sprintf("    mov %s.s[%d], wzr", dLowV, i))
		}
	}

	if isVex {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}

	return lines, nil
}

// liftVperm2f128 handles VPERM2F128 dst_ymm, src1_ymm, src2_ymm/m256, imm8.
func (l *Arm64Lifter) liftVperm2f128(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	src1Reg, okSrc1 := inst.Inst.Args[1].(x86asm.Reg)
	src2 := inst.Inst.Args[2]
	immArg, okImm := inst.Inst.Args[3].(x86asm.Imm)

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1 := getVectorDesc(src1Reg)
	if !okDst || !okSrc1 || !okImm || !okD || !okS1 || !isYmm {
		lines = append(lines, l.emitUnsupported(inst, "vperm2f128 invalid operands")...)
		return lines, nil
	}

	imm8 := uint8(immArg)
	loSel := imm8 & 3
	loZero := (imm8 & 8) != 0
	hiSel := (imm8 >> 4) & 3
	hiZero := (imm8 & 0x80) != 0

	// Resolve s2 low and high into v16 and v17
	var s2Low, s2High string
	if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q16, [x28, x16]")
		lines = append(lines, "    add x17, x16, #16")
		lines = append(lines, "    ldr q17, [x28, x17]")
		s2Low = "v16"
		s2High = "v17"
	} else if src2Reg, okSrc2 := src2.(x86asm.Reg); okSrc2 {
		if _, _, _, s2LowV, s2HighV, _, _, okS2 := getVectorDesc(src2Reg); okS2 {
			s2Low = s2LowV
			s2High = s2HighV
		} else {
			lines = append(lines, l.emitUnsupported(inst, "vperm2f128 invalid src2 reg")...)
			return lines, nil
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "vperm2f128 invalid src2")...)
		return lines, nil
	}

	getLane := func(sel uint8) string {
		switch sel {
		case 0:
			return s1LowV
		case 1:
			return s1HighV
		case 2:
			return s2Low
		default:
			return s2High
		}
	}

	// Compute into temporary v18 and v19 to avoid any register overwriting
	if loZero {
		lines = append(lines, "    movi v18.16b, #0")
	} else {
		lines = append(lines, fmt.Sprintf("    mov v18.16b, %s.16b", getLane(loSel)))
	}

	if hiZero {
		lines = append(lines, "    movi v19.16b, #0")
	} else {
		lines = append(lines, fmt.Sprintf("    mov v19.16b, %s.16b", getLane(hiSel)))
	}

	lines = append(lines, fmt.Sprintf("    mov %s.16b, v18.16b", dLowV))
	lines = append(lines, fmt.Sprintf("    mov %s.16b, v19.16b", dHighV))
	return lines, nil
}

// liftVpermil handles VPERMILPS and VPERMILPD dst_xmm/ymm, src_xmm/ymm/mem, imm8/ctrl.
func (l *Arm64Lifter) liftVpermil(inst disasm.Instruction, nextPC uint64, isDouble bool, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "vpermil invalid dst")...)
		return lines, nil
	}

	var src, ctrlArg x86asm.Arg
	hasImm := false
	var imm8 uint8

	if len(inst.Inst.Args) >= 3 && inst.Inst.Args[2] != nil {
		src = inst.Inst.Args[1]
		if immArg, okImm := inst.Inst.Args[2].(x86asm.Imm); okImm {
			hasImm = true
			imm8 = uint8(immArg)
		} else {
			ctrlArg = inst.Inst.Args[2]
		}
	} else if len(inst.Inst.Args) >= 2 && inst.Inst.Args[1] != nil {
		if immArg, okImm := inst.Inst.Args[1].(x86asm.Imm); okImm {
			hasImm = true
			imm8 = uint8(immArg)
			src = inst.Inst.Args[0]
		} else {
			src = inst.Inst.Args[0]
			ctrlArg = inst.Inst.Args[1]
		}
	}

	// Load source into v16 (and v17 if YMM) or use register
	var sLow, sHigh string
	if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q16, [x28, x16]")
		sLow = "v16"
		if isYmm {
			lines = append(lines, "    add x17, x16, #16")
			lines = append(lines, "    ldr q17, [x28, x17]")
			sHigh = "v17"
		}
	} else if srcReg, okSrc := src.(x86asm.Reg); okSrc {
		if _, _, _, sLowV, sHighV, _, _, okS := getVectorDesc(srcReg); okS {
			sLow = sLowV
			sHigh = sHighV
		} else {
			lines = append(lines, l.emitUnsupported(inst, "vpermil invalid src reg")...)
			return lines, nil
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "vpermil invalid src")...)
		return lines, nil
	}

	if hasImm {
		if isDouble {
			// VPERMILPD: 2 double elements per 128-bit lane
			sel0 := imm8 & 1
			sel1 := (imm8 >> 1) & 1

			lines = append(lines, fmt.Sprintf("    mov v18.d[0], %s.d[%d]", sLow, sel0))
			lines = append(lines, fmt.Sprintf("    mov v18.d[1], %s.d[%d]", sLow, sel1))
			lines = append(lines, fmt.Sprintf("    mov %s.16b, v18.16b", dLowV))

			if isYmm {
				lines = append(lines, fmt.Sprintf("    mov v19.d[0], %s.d[%d]", sHigh, sel0))
				lines = append(lines, fmt.Sprintf("    mov v19.d[1], %s.d[%d]", sHigh, sel1))
				lines = append(lines, fmt.Sprintf("    mov %s.16b, v19.16b", dHighV))
			} else {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			}
		} else {
			// VPERMILPS: 4 float elements per 128-bit lane
			sel0 := imm8 & 3
			sel1 := (imm8 >> 2) & 3
			sel2 := (imm8 >> 4) & 3
			sel3 := (imm8 >> 6) & 3

			lines = append(lines, fmt.Sprintf("    mov v18.s[0], %s.s[%d]", sLow, sel0))
			lines = append(lines, fmt.Sprintf("    mov v18.s[1], %s.s[%d]", sLow, sel1))
			lines = append(lines, fmt.Sprintf("    mov v18.s[2], %s.s[%d]", sLow, sel2))
			lines = append(lines, fmt.Sprintf("    mov v18.s[3], %s.s[%d]", sLow, sel3))
			lines = append(lines, fmt.Sprintf("    mov %s.16b, v18.16b", dLowV))

			if isYmm {
				lines = append(lines, fmt.Sprintf("    mov v19.s[0], %s.s[%d]", sHigh, sel0))
				lines = append(lines, fmt.Sprintf("    mov v19.s[1], %s.s[%d]", sHigh, sel1))
				lines = append(lines, fmt.Sprintf("    mov v19.s[2], %s.s[%d]", sHigh, sel2))
				lines = append(lines, fmt.Sprintf("    mov v19.s[3], %s.s[%d]", sHigh, sel3))
				lines = append(lines, fmt.Sprintf("    mov %s.16b, v19.16b", dHighV))
			} else {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			}
		}
		return lines, nil
	}

	// Non-immediate control operand
	cLow := "v20"
	cHigh := "v21"
	if ctrlMem, okCM := ctrlArg.(x86asm.Mem); okCM {
		lines = append(lines, computeEffAddr(ctrlMem, nextPC, "x17")...)
		lines = append(lines, "    ldr q20, [x28, x17]")
		if isYmm {
			lines = append(lines, "    add x17, x17, #16", "    ldr q21, [x28, x17]")
		}
	} else if ctrlReg, okCR := ctrlArg.(x86asm.Reg); okCR {
		if _, _, _, crLow, crHigh, _, _, okDesc := getVectorDesc(ctrlReg); okDesc {
			cLow = crLow
			cHigh = crHigh
		} else {
			lines = append(lines, l.emitUnsupported(inst, "vpermil invalid ctrl reg")...)
			return lines, nil
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "vpermil invalid ctrl")...)
		return lines, nil
	}

	if !isDouble {
		// VPERMILPS: 32-bit floats
		emitLanePermPS := func(dstLane, srcLane, ctrlLane string) {
			lines = append(lines,
				"    movi v22.4s, #3",
				fmt.Sprintf("    and v23.16b, %s.16b, v22.16b", ctrlLane),
				"    shl v23.4s, v23.4s, #2",
				"    mov w16, #0x0101",
				"    movk w16, #0x0101, lsl #16",
				"    dup v22.4s, w16",
				"    mul v23.4s, v23.4s, v22.4s",
				"    mov w16, #0x0100",
				"    movk w16, #0x0302, lsl #16",
				"    dup v22.4s, w16",
				"    add v23.16b, v23.16b, v22.16b",
				fmt.Sprintf("    tbl %s.16b, {%s.16b}, v23.16b", dstLane, srcLane),
			)
		}
		emitLanePermPS(dLowV, sLow, cLow)
		if isYmm {
			emitLanePermPS(dHighV, sHigh, cHigh)
		} else {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
	} else {
		// VPERMILPD: 64-bit doubles
		emitLanePermPD := func(dstLane, srcLane, ctrlLane string) {
			lines = append(lines,
				fmt.Sprintf("    ushr v23.2d, %s.2d, #1", ctrlLane),
				"    movi v22.2d, #1",
				"    and v23.16b, v23.16b, v22.16b",
				"    shl v23.2d, v23.2d, #3",
				"    cmeq v22.2d, v23.2d, #0",
			)
			lines = append(lines, loadImm64("x16", 0x0808080808080808)...)
			lines = append(lines,
				"    dup v23.2d, x16",
				"    bic v23.16b, v23.16b, v22.16b",
			)
			lines = append(lines, loadImm64("x16", 0x0706050403020100)...)
			lines = append(lines,
				"    dup v22.2d, x16",
				"    add v23.16b, v23.16b, v22.16b",
				fmt.Sprintf("    tbl %s.16b, {%s.16b}, v23.16b", dstLane, srcLane),
			)
		}
		emitLanePermPD(dLowV, sLow, cLow)
		if isYmm {
			emitLanePermPD(dHighV, sHigh, cHigh)
		} else {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
	}
	return lines, nil
}

// liftPinsr handles PINSRB, PINSRW, PINSRD, PINSRQ, VPINSRB, VPINSRW, VPINSRD, VPINSRQ.
func (l *Arm64Lifter) liftPinsr(inst disasm.Instruction, elemBytes int, nextPC uint64, lines []string) ([]string, error) {
	var dstReg, src1Reg x86asm.Reg
	var src2 x86asm.Arg
	var immArg x86asm.Imm
	var okImm bool

	isVex := inst.Inst.Op == x86asm.VPINSRB || inst.Inst.Op == x86asm.VPINSRW || inst.Inst.Op == x86asm.VPINSRD || inst.Inst.Op == x86asm.VPINSRQ
	if isVex && len(inst.Inst.Args) >= 4 {
		var okDst, okSrc1 bool
		dstReg, okDst = inst.Inst.Args[0].(x86asm.Reg)
		src1Reg, okSrc1 = inst.Inst.Args[1].(x86asm.Reg)
		src2 = inst.Inst.Args[2]
		immArg, okImm = inst.Inst.Args[3].(x86asm.Imm)
		if !okDst || !okSrc1 || !okImm {
			lines = append(lines, l.emitUnsupported(inst, "vpinsr invalid operands")...)
			return lines, nil
		}
	} else {
		var okDst bool
		dstReg, okDst = inst.Inst.Args[0].(x86asm.Reg)
		src1Reg = dstReg
		src2 = inst.Inst.Args[1]
		immArg, okImm = inst.Inst.Args[2].(x86asm.Imm)
		if !okDst || !okImm {
			lines = append(lines, l.emitUnsupported(inst, "pinsr invalid operands")...)
			return lines, nil
		}
	}

	_, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, _, _, _, okS1 := getVectorDesc(src1Reg)
	if !okD || !okS1 {
		lines = append(lines, l.emitUnsupported(inst, "pinsr vector reg not found")...)
		return lines, nil
	}

	laneMask := (16 / elemBytes) - 1
	lane := int(uint8(immArg)) & laneMask

	// Load/read src2 value into x16 / w16
	if reg, okReg := src2.(x86asm.Reg); okReg {
		readCode, okRead := readRegTo(reg, "x16", "w16")
		if !okRead {
			lines = append(lines, l.emitUnsupported(inst, "pinsr unhandled src reg")...)
			return lines, nil
		}
		lines = append(lines, readCode...)
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, readMemTo(mem, nextPC, elemBytes, "x16", "w16")...)
	} else {
		lines = append(lines, l.emitUnsupported(inst, "pinsr unhandled src")...)
		return lines, nil
	}

	// Copy base src1 into dst if different
	if dLowV != s1LowV {
		lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1LowV))
	}

	// Insert element into vector lane
	switch elemBytes {
	case 1:
		lines = append(lines, fmt.Sprintf("    mov %s.b[%d], w16", dLowV, lane))
	case 2:
		lines = append(lines, fmt.Sprintf("    mov %s.h[%d], w16", dLowV, lane))
	case 4:
		lines = append(lines, fmt.Sprintf("    mov %s.s[%d], w16", dLowV, lane))
	case 8:
		lines = append(lines, fmt.Sprintf("    mov %s.d[%d], x16", dLowV, lane))
	}

	if isVex {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}

	return lines, nil
}

// liftPextr handles PEXTRB, PEXTRW, PEXTRD, PEXTRQ, VPEXTRB, VPEXTRW, VPEXTRD, VPEXTRQ.
func (l *Arm64Lifter) liftPextr(inst disasm.Instruction, elemBytes int, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]
	immArg, okImm := inst.Inst.Args[2].(x86asm.Imm)
	if !okImm {
		lines = append(lines, l.emitUnsupported(inst, "pextr missing imm")...)
		return lines, nil
	}

	srcReg, okSrc := src.(x86asm.Reg)
	_, _, _, sLowV, _, _, _, okS := getVectorDesc(srcReg)
	if !okSrc || !okS {
		lines = append(lines, l.emitUnsupported(inst, "pextr invalid src reg")...)
		return lines, nil
	}

	laneMask := (16 / elemBytes) - 1
	lane := int(uint8(immArg)) & laneMask

	// Extract element into w16 / x16
	switch elemBytes {
	case 1:
		lines = append(lines, fmt.Sprintf("    umov w16, %s.b[%d]", sLowV, lane))
	case 2:
		lines = append(lines, fmt.Sprintf("    umov w16, %s.h[%d]", sLowV, lane))
	case 4:
		lines = append(lines, fmt.Sprintf("    umov w16, %s.s[%d]", sLowV, lane))
	case 8:
		lines = append(lines, fmt.Sprintf("    umov x16, %s.d[%d]", sLowV, lane))
	}

	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		storeCode, okStore := writeRegFrom(dstReg, "x16", "w16")
		if !okStore {
			lines = append(lines, l.emitUnsupported(inst, "pextr unhandled dst reg")...)
			return lines, nil
		}
		lines = append(lines, storeCode...)
		return lines, nil
	}

	if mem, okMem := dst.(x86asm.Mem); okMem {
		lines = append(lines, writeMemFrom(mem, nextPC, elemBytes, "x16", "w16")...)
		return lines, nil
	}

	lines = append(lines, l.emitUnsupported(inst, "pextr unhandled dst")...)
	return lines, nil
}

// liftPmovmskb handles PMOVMSKB and VPMOVMSKB dst_r32, src_xmm/ymm.
func (l *Arm64Lifter) liftPmovmskb(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	srcReg, okSrc := inst.Inst.Args[1].(x86asm.Reg)
	if !okDst || !okSrc {
		lines = append(lines, l.emitUnsupported(inst, "pmovmskb invalid operands")...)
		return lines, nil
	}

	isYmm, _, _, sLowV, sHighV, _, _, okS := getVectorDesc(srcReg)
	if !okS {
		lines = append(lines, l.emitUnsupported(inst, "pmovmskb invalid src reg")...)
		return lines, nil
	}

	// Load weights 0x8040201008040201 into v17.2d
	lines = append(lines,
		"    mov x16, #0x0201",
		"    movk x16, #0x0804, lsl #16",
		"    movk x16, #0x2010, lsl #32",
		"    movk x16, #0x8040, lsl #48",
		"    dup v17.2d, x16",
		fmt.Sprintf("    sshr v16.16b, %s.16b, #7", sLowV),
		"    and v16.16b, v16.16b, v17.16b",
		"    addp v16.16b, v16.16b, v16.16b",
		"    addp v16.16b, v16.16b, v16.16b",
		"    addp v16.16b, v16.16b, v16.16b",
		"    umov w16, v16.h[0]",
	)

	if isYmm {
		lines = append(lines,
			fmt.Sprintf("    sshr v18.16b, %s.16b, #7", sHighV),
			"    and v18.16b, v18.16b, v17.16b",
			"    addp v18.16b, v18.16b, v18.16b",
			"    addp v18.16b, v18.16b, v18.16b",
			"    addp v18.16b, v18.16b, v18.16b",
			"    umov w17, v18.h[0]",
			"    orr w16, w16, w17, lsl #16",
		)
	}

	storeCode, okStore := writeRegFrom(dstReg, "x16", "w16")
	if !okStore {
		lines = append(lines, l.emitUnsupported(inst, "pmovmskb unhandled dst reg")...)
		return lines, nil
	}
	lines = append(lines, storeCode...)
	return lines, nil
}

// liftMovmskps handles MOVMSKPS and VMOVMSKPS dst_r32, src_xmm/ymm.
func (l *Arm64Lifter) liftMovmskps(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	srcReg, okSrc := inst.Inst.Args[1].(x86asm.Reg)
	if !okDst || !okSrc {
		lines = append(lines, l.emitUnsupported(inst, "movmskps invalid operands")...)
		return lines, nil
	}

	isYmm, _, _, sLowV, sHighV, _, _, okS := getVectorDesc(srcReg)
	if !okS {
		lines = append(lines, l.emitUnsupported(inst, "movmskps invalid src reg")...)
		return lines, nil
	}

	lines = append(lines,
		fmt.Sprintf("    umov w16, %s.s[0]", sLowV),
		"    lsr w16, w16, #31",
		fmt.Sprintf("    umov w17, %s.s[1]", sLowV),
		"    ubfx w17, w17, #31, #1",
		"    orr w16, w16, w17, lsl #1",
		fmt.Sprintf("    umov w17, %s.s[2]", sLowV),
		"    ubfx w17, w17, #31, #1",
		"    orr w16, w16, w17, lsl #2",
		fmt.Sprintf("    umov w17, %s.s[3]", sLowV),
		"    ubfx w17, w17, #31, #1",
		"    orr w16, w16, w17, lsl #3",
	)

	if isYmm {
		lines = append(lines,
			fmt.Sprintf("    umov w17, %s.s[0]", sHighV),
			"    ubfx w17, w17, #31, #1",
			"    orr w16, w16, w17, lsl #4",
			fmt.Sprintf("    umov w17, %s.s[1]", sHighV),
			"    ubfx w17, w17, #31, #1",
			"    orr w16, w16, w17, lsl #5",
			fmt.Sprintf("    umov w17, %s.s[2]", sHighV),
			"    ubfx w17, w17, #31, #1",
			"    orr w16, w16, w17, lsl #6",
			fmt.Sprintf("    umov w17, %s.s[3]", sHighV),
			"    ubfx w17, w17, #31, #1",
			"    orr w16, w16, w17, lsl #7",
		)
	}

	storeCode, okStore := writeRegFrom(dstReg, "x16", "w16")
	if !okStore {
		lines = append(lines, l.emitUnsupported(inst, "movmskps unhandled dst reg")...)
		return lines, nil
	}
	lines = append(lines, storeCode...)
	return lines, nil
}

// liftMovmskpd handles MOVMSKPD and VMOVMSKPD dst_r32, src_xmm/ymm.
func (l *Arm64Lifter) liftMovmskpd(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	srcReg, okSrc := inst.Inst.Args[1].(x86asm.Reg)
	if !okDst || !okSrc {
		lines = append(lines, l.emitUnsupported(inst, "movmskpd invalid operands")...)
		return lines, nil
	}

	isYmm, _, _, sLowV, sHighV, _, _, okS := getVectorDesc(srcReg)
	if !okS {
		lines = append(lines, l.emitUnsupported(inst, "movmskpd invalid src reg")...)
		return lines, nil
	}

	lines = append(lines,
		fmt.Sprintf("    umov x16, %s.d[0]", sLowV),
		"    lsr x16, x16, #63",
		fmt.Sprintf("    umov x17, %s.d[1]", sLowV),
		"    ubfx x17, x17, #63, #1",
		"    orr w16, w16, w17, lsl #1",
	)

	if isYmm {
		lines = append(lines,
			fmt.Sprintf("    umov x17, %s.d[0]", sHighV),
			"    ubfx x17, x17, #63, #1",
			"    orr w16, w16, w17, lsl #2",
			fmt.Sprintf("    umov x17, %s.d[1]", sHighV),
			"    ubfx x17, x17, #63, #1",
			"    orr w16, w16, w17, lsl #3",
		)
	}

	storeCode, okStore := writeRegFrom(dstReg, "x16", "w16")
	if !okStore {
		lines = append(lines, l.emitUnsupported(inst, "movmskpd unhandled dst reg")...)
		return lines, nil
	}
	lines = append(lines, storeCode...)
	return lines, nil
}

// liftDpps handles DPPS and VDPPS dst_xmm/ymm, [src1_xmm/ymm], src2_xmm/ymm/m128/m256, imm8.
func (l *Arm64Lifter) liftDpps(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	var dstReg, src1Reg x86asm.Reg
	var src2 x86asm.Arg
	var immArg x86asm.Imm
	var okImm bool

	isVex := inst.Inst.Op == x86asm.VDPPS
	if isVex {
		var okDst, okSrc1 bool
		dstReg, okDst = inst.Inst.Args[0].(x86asm.Reg)
		src1Reg, okSrc1 = inst.Inst.Args[1].(x86asm.Reg)
		src2 = inst.Inst.Args[2]
		immArg, okImm = inst.Inst.Args[3].(x86asm.Imm)
		if !okDst || !okSrc1 || !okImm {
			lines = append(lines, l.emitUnsupported(inst, "vdpps invalid operands")...)
			return lines, nil
		}
	} else {
		var okDst bool
		dstReg, okDst = inst.Inst.Args[0].(x86asm.Reg)
		src1Reg = dstReg
		src2 = inst.Inst.Args[1]
		immArg, okImm = inst.Inst.Args[2].(x86asm.Imm)
		if !okDst || !okImm {
			lines = append(lines, l.emitUnsupported(inst, "dpps invalid operands")...)
			return lines, nil
		}
	}

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1 := getVectorDesc(src1Reg)
	if !okD || !okS1 {
		lines = append(lines, l.emitUnsupported(inst, "dpps vector reg not found")...)
		return lines, nil
	}

	imm8 := uint8(immArg)

	// Resolve s2 into v20 (low) and v21 (high if YMM)
	var s2Low, s2High string
	if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q20, [x28, x16]")
		s2Low = "v20"
		if isYmm {
			lines = append(lines, "    add x17, x16, #16")
			lines = append(lines, "    ldr q21, [x28, x17]")
			s2High = "v21"
		}
	} else if src2Reg, okSrc2 := src2.(x86asm.Reg); okSrc2 {
		if _, _, _, s2LowV, s2HighV, _, _, okS2 := getVectorDesc(src2Reg); okS2 {
			s2Low = s2LowV
			s2High = s2HighV
		} else {
			lines = append(lines, l.emitUnsupported(inst, "dpps invalid src2 reg")...)
			return lines, nil
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "dpps invalid src2")...)
		return lines, nil
	}

	computeLane := func(s1V, s2V, targetV string) {
		lines = append(lines, fmt.Sprintf("    fmul v16.4s, %s.4s, %s.4s", s1V, s2V))
		// Zero disabled input lanes (bits 4..7)
		for i := 0; i < 4; i++ {
			if imm8&(1<<(4+i)) == 0 {
				lines = append(lines, fmt.Sprintf("    mov v16.s[%d], wzr", i))
			}
		}
		// Sum remaining elements
		lines = append(lines,
			"    faddp v17.4s, v16.4s, v16.4s",
			"    faddp s17, v17.2s",
			fmt.Sprintf("    movi %s.16b, #0", targetV),
		)
		// Store to enabled output lanes (bits 0..3)
		for i := 0; i < 4; i++ {
			if imm8&(1<<i) != 0 {
				lines = append(lines, fmt.Sprintf("    mov %s.s[%d], v17.s[0]", targetV, i))
			}
		}
	}

	computeLane(s1LowV, s2Low, dLowV)
	if isYmm {
		computeLane(s1HighV, s2High, dHighV)
	} else if isVex {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}

	return lines, nil
}

// liftMovsdup handles MOVSHDUP, VMOVSHDUP, MOVSLDUP, VMOVSLDUP.
func (l *Arm64Lifter) liftMovsdup(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	src := inst.Inst.Args[1]

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported movsdup dst")...)
		return lines, nil
	}

	isAvx := (op == x86asm.VMOVSHDUP || op == x86asm.VMOVSLDUP)
	isHigh := (op == x86asm.MOVSHDUP || op == x86asm.VMOVSHDUP)

	sLowV := "v30"
	sHighV := "v31"
	if sReg, okSrc := src.(x86asm.Reg); okSrc {
		if _, _, _, rLowV, rHighV, _, _, okS := getVectorDesc(sReg); okS {
			sLowV = rLowV
			sHighV = rHighV
		}
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16")
			lines = append(lines, "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported movsdup src")...)
		return lines, nil
	}

	trnOp := "trn1"
	if isHigh {
		trnOp = "trn2"
	}

	lines = append(lines, fmt.Sprintf("    %s %s.4s, %s.4s, %s.4s", trnOp, dLowV, sLowV, sLowV))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    %s %s.4s, %s.4s, %s.4s", trnOp, dHighV, sHighV, sHighV))
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftPshufb handles PSHUFB and VPSHUFB.
func (l *Arm64Lifter) liftPshufb(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isAvx := (op == x86asm.VPSHUFB)

	var dstArg, s1Arg, s2Arg x86asm.Arg
	if isAvx {
		if len(inst.Inst.Args) < 3 || inst.Inst.Args[0] == nil || inst.Inst.Args[1] == nil || inst.Inst.Args[2] == nil {
			lines = append(lines, l.emitUnsupported(inst, "unsupported vpshufb args")...)
			return lines, nil
		}
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[1]
		s2Arg = inst.Inst.Args[2]
	} else {
		if len(inst.Inst.Args) < 2 || inst.Inst.Args[0] == nil || inst.Inst.Args[1] == nil {
			lines = append(lines, l.emitUnsupported(inst, "unsupported pshufb args")...)
			return lines, nil
		}
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[0]
		s2Arg = inst.Inst.Args[1]
	}

	dstReg, okDst := dstArg.(x86asm.Reg)
	s1Reg, okS1 := s1Arg.(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pshufb dst/src1")...)
		return lines, nil
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okSrc2 := s2Arg.(x86asm.Reg); okSrc2 {
		if _, _, _, rLowV, rHighV, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
			s2LowV = rLowV
			s2HighV = rHighV
		}
	} else if mem, okMem := s2Arg.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16")
			lines = append(lines, "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pshufb src2")...)
		return lines, nil
	}

	lines = append(lines,
		"    movi v28.16b, #0x8f",
		fmt.Sprintf("    and v29.16b, %s.16b, v28.16b", s2LowV),
		fmt.Sprintf("    tbl %s.16b, {%s.16b}, v29.16b", dLowV, s1LowV),
	)
	if isYmm {
		lines = append(lines,
			fmt.Sprintf("    and v29.16b, %s.16b, v28.16b", s2HighV),
			fmt.Sprintf("    tbl %s.16b, {%s.16b}, v29.16b", dHighV, s1HighV),
		)
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftVandn handles VANDNPS and VANDNPD.
func (l *Arm64Lifter) liftVandn(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg)
	src2 := inst.Inst.Args[2]

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vandn dst/src1")...)
		return lines, nil
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okSrc2 := src2.(x86asm.Reg); okSrc2 {
		if _, _, _, rLowV, rHighV, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
			s2LowV = rLowV
			s2HighV = rHighV
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16")
			lines = append(lines, "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vandn src2")...)
		return lines, nil
	}

	// dst = (~s1) & s2 -> bic dst, s2, s1
	lines = append(lines, fmt.Sprintf("    bic %s.16b, %s.16b, %s.16b", dLowV, s2LowV, s1LowV))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    bic %s.16b, %s.16b, %s.16b", dHighV, s2HighV, s1HighV))
	} else {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftFpVectorCmp handles CMPPS, VCMPPS, CMPSS, VCMPSS, CMPPD, VCMPPD, CMPSD, VCMPSD.
func (l *Arm64Lifter) liftFpVectorCmp(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isAvx := (op == x86asm.VCMPPS || op == x86asm.VCMPSS || op == x86asm.VCMPPD || op == x86asm.VCMPSD)
	isDouble := (op == x86asm.CMPPD || op == x86asm.VCMPPD || op == x86asm.CMPSD || op == x86asm.VCMPSD)
	isScalar := (op == x86asm.CMPSS || op == x86asm.VCMPSS || op == x86asm.CMPSD || op == x86asm.VCMPSD)

	var dstArg, s1Arg, s2Arg, immArg x86asm.Arg
	if isAvx {
		if len(inst.Inst.Args) < 4 || inst.Inst.Args[0] == nil || inst.Inst.Args[1] == nil || inst.Inst.Args[2] == nil || inst.Inst.Args[3] == nil {
			lines = append(lines, l.emitUnsupported(inst, "unsupported avx vcmp args")...)
			return lines, nil
		}
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[1]
		s2Arg = inst.Inst.Args[2]
		immArg = inst.Inst.Args[3]
	} else {
		if len(inst.Inst.Args) < 3 || inst.Inst.Args[0] == nil || inst.Inst.Args[1] == nil || inst.Inst.Args[2] == nil {
			lines = append(lines, l.emitUnsupported(inst, "unsupported sse cmp args")...)
			return lines, nil
		}
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[0]
		s2Arg = inst.Inst.Args[1]
		immArg = inst.Inst.Args[2]
	}

	dstReg, okDst := dstArg.(x86asm.Reg)
	s1Reg, okS1 := s1Arg.(x86asm.Reg)
	imm, okImm := immArg.(x86asm.Imm)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okImm || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vcmp operands")...)
		return lines, nil
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okSrc2 := s2Arg.(x86asm.Reg); okSrc2 {
		if _, _, _, rLowV, rHighV, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
			s2LowV = rLowV
			s2HighV = rHighV
		}
	} else if mem, okMem := s2Arg.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		if isScalar {
			if isDouble {
				lines = append(lines, "    ldr d30, [x28, x16]")
			} else {
				lines = append(lines, "    ldr s30, [x28, x16]")
			}
		} else {
			lines = append(lines, "    ldr q30, [x28, x16]")
			if isYmm {
				lines = append(lines, "    add x16, x16, #16")
				lines = append(lines, "    ldr q31, [x28, x16]")
			}
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vcmp src2")...)
		return lines, nil
	}

	pred := uint8(imm) & 7
	lane := "4s"
	if isDouble {
		lane = "2d"
	}

	emitCompare := func(targetV, op1V, op2V string) {
		switch pred {
		case 0: // EQ
			lines = append(lines, fmt.Sprintf("    fcmeq %s.%s, %s.%s, %s.%s", targetV, lane, op1V, lane, op2V, lane))
		case 1: // LT: op1 < op2  ==  op2 > op1
			lines = append(lines, fmt.Sprintf("    fcmgt %s.%s, %s.%s, %s.%s", targetV, lane, op2V, lane, op1V, lane))
		case 2: // LE: op1 <= op2 ==  op2 >= op1
			lines = append(lines, fmt.Sprintf("    fcmge %s.%s, %s.%s, %s.%s", targetV, lane, op2V, lane, op1V, lane))
		case 3: // UNORD: isnan(op1) || isnan(op2)
			lines = append(lines,
				fmt.Sprintf("    fcmeq v28.%s, %s.%s, %s.%s", lane, op1V, lane, op1V, lane),
				fmt.Sprintf("    fcmeq v29.%s, %s.%s, %s.%s", lane, op2V, lane, op2V, lane),
				"    and v28.16b, v28.16b, v29.16b",
				fmt.Sprintf("    mvn %s.16b, v28.16b", targetV),
			)
		case 4: // NEQ
			lines = append(lines,
				fmt.Sprintf("    fcmeq %s.%s, %s.%s, %s.%s", targetV, lane, op1V, lane, op2V, lane),
				fmt.Sprintf("    mvn %s.16b, %s.16b", targetV, targetV),
			)
		case 5: // NLT: !(op1 < op2) == !(op2 > op1)
			lines = append(lines,
				fmt.Sprintf("    fcmgt %s.%s, %s.%s, %s.%s", targetV, lane, op2V, lane, op1V, lane),
				fmt.Sprintf("    mvn %s.16b, %s.16b", targetV, targetV),
			)
		case 6: // NLE: !(op1 <= op2) == !(op2 >= op1)
			lines = append(lines,
				fmt.Sprintf("    fcmge %s.%s, %s.%s, %s.%s", targetV, lane, op2V, lane, op1V, lane),
				fmt.Sprintf("    mvn %s.16b, %s.16b", targetV, targetV),
			)
		case 7: // ORD: !isnan(op1) && !isnan(op2)
			lines = append(lines,
				fmt.Sprintf("    fcmeq v28.%s, %s.%s, %s.%s", lane, op1V, lane, op1V, lane),
				fmt.Sprintf("    fcmeq v29.%s, %s.%s, %s.%s", lane, op2V, lane, op2V, lane),
				fmt.Sprintf("    and %s.16b, v28.16b, v29.16b", targetV),
			)
		}
	}

	if isScalar {
		emitCompare("v28", s1LowV, s2LowV)
		if isDouble {
			lines = append(lines, fmt.Sprintf("    mov %s.d[0], v28.d[0]", dLowV))
			if isAvx {
				lines = append(lines, fmt.Sprintf("    mov %s.d[1], %s.d[1]", dLowV, s1LowV))
			}
		} else {
			lines = append(lines, fmt.Sprintf("    mov %s.s[0], v28.s[0]", dLowV))
			if isAvx {
				lines = append(lines,
					fmt.Sprintf("    mov %s.s[1], %s.s[1]", dLowV, s1LowV),
					fmt.Sprintf("    mov %s.d[1], %s.d[1]", dLowV, s1LowV),
				)
			}
		}
		if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
	} else {
		emitCompare(dLowV, s1LowV, s2LowV)
		if isYmm {
			emitCompare(dHighV, s1HighV, s2HighV)
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
	}

	return lines, nil
}

// liftPalignr handles PALIGNR and VPALIGNR.
func (l *Arm64Lifter) liftPalignr(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isAvx := (op == x86asm.VPALIGNR)

	var dstArg, s1Arg, s2Arg, immArg x86asm.Arg
	if isAvx {
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[1]
		s2Arg = inst.Inst.Args[2]
		immArg = inst.Inst.Args[3]
	} else {
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[0]
		s2Arg = inst.Inst.Args[1]
		immArg = inst.Inst.Args[2]
	}

	dstReg, okDst := dstArg.(x86asm.Reg)
	s1Reg, okS1 := s1Arg.(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported palignr dst/src1")...)
		return lines, nil
	}

	imm, okImm := immArg.(x86asm.Imm)
	if !okImm {
		lines = append(lines, l.emitUnsupported(inst, "unsupported palignr imm")...)
		return lines, nil
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okSrc2 := s2Arg.(x86asm.Reg); okSrc2 {
		if _, _, _, rL, rH, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
			s2LowV = rL
			s2HighV = rH
		}
	} else if mem, okMem := s2Arg.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16", "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported palignr src2")...)
		return lines, nil
	}

	shift := int(imm)
	if shift >= 32 {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		return lines, nil
	}

	lines = append(lines, "    movi v29.16b, #0")
	if shift < 16 {
		lines = append(lines, fmt.Sprintf("    ext %s.16b, %s.16b, %s.16b, #%d", dLowV, s2LowV, s1LowV, shift))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    ext %s.16b, %s.16b, %s.16b, #%d", dHighV, s2HighV, s1HighV, shift))
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
	} else {
		lines = append(lines, fmt.Sprintf("    ext %s.16b, %s.16b, v29.16b, #%d", dLowV, s1LowV, shift-16))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    ext %s.16b, %s.16b, v29.16b, #%d", dHighV, s1HighV, shift-16))
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
	}
	return lines, nil
}

// liftMovnt handles MOVNTPS, VMOVNTPS, MOVNTDQ, VMOVNTDQ, and VMOVNTDQA.
func (l *Arm64Lifter) liftMovnt(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	// Load: VMOVNTDQA dstReg, srcMem
	if op == x86asm.VMOVNTDQA {
		dstReg, okDst := dst.(x86asm.Reg)
		isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
		mem, okMem := src.(x86asm.Mem)
		if okDst && okD && okMem {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, fmt.Sprintf("    ldr q%s, [x28, x16]", dLowV[1:]))
			if isYmm {
				lines = append(lines, "    add x16, x16, #16", fmt.Sprintf("    ldr q%s, [x28, x16]", dHighV[1:]))
			} else {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			}
			return lines, nil
		}
	}

	// Store: (V)MOVNTPS / (V)MOVNTDQ dstMem, srcReg
	mem, okMem := dst.(x86asm.Mem)
	srcReg, okSrc := src.(x86asm.Reg)
	if !okMem || !okSrc {
		lines = append(lines, l.emitUnsupported(inst, "unsupported movnt operands")...)
		return lines, nil
	}

	isYmm, _, _, sLowV, sHighV, _, _, okS := getVectorDesc(srcReg)
	if !okS {
		lines = append(lines, l.emitUnsupported(inst, "unsupported movnt src reg")...)
		return lines, nil
	}

	lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
	lines = append(lines, fmt.Sprintf("    str q%s, [x28, x16]", sLowV[1:]))
	if isYmm {
		lines = append(lines, "    add x16, x16, #16", fmt.Sprintf("    str q%s, [x28, x16]", sHighV[1:]))
	}
	return lines, nil
}

// liftAddsub handles ADDSUBPS, VADDSUBPS, ADDSUBPD, and VADDSUBPD.
func (l *Arm64Lifter) liftAddsub(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isDouble := (op == x86asm.ADDSUBPD || op == x86asm.VADDSUBPD)
	isAvx := (op == x86asm.VADDSUBPS || op == x86asm.VADDSUBPD)

	var dstArg, s1Arg, s2Arg x86asm.Arg
	if isAvx {
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[1]
		s2Arg = inst.Inst.Args[2]
	} else {
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[0]
		s2Arg = inst.Inst.Args[1]
	}

	dstReg, okDst := dstArg.(x86asm.Reg)
	s1Reg, okS1 := s1Arg.(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported addsub dst/src1")...)
		return lines, nil
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okSrc2 := s2Arg.(x86asm.Reg); okSrc2 {
		if _, _, _, rL, rH, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
			s2LowV = rL
			s2HighV = rH
		}
	} else if mem, okMem := s2Arg.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16", "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported addsub src2")...)
		return lines, nil
	}

	lane := ".4s"
	if isDouble {
		lane = ".2d"
	}

	lines = append(lines,
		fmt.Sprintf("    fsub v28%s, %s%s, %s%s", lane, s1LowV, lane, s2LowV, lane),
		fmt.Sprintf("    fadd v29%s, %s%s, %s%s", lane, s1LowV, lane, s2LowV, lane),
		fmt.Sprintf("    mov %s.16b, v28.16b", dLowV),
	)
	if isDouble {
		lines = append(lines, fmt.Sprintf("    mov %s.d[1], v29.d[1]", dLowV))
	} else {
		lines = append(lines,
			fmt.Sprintf("    mov %s.s[1], v29.s[1]", dLowV),
			fmt.Sprintf("    mov %s.s[3], v29.s[3]", dLowV),
		)
	}

	if isYmm {
		lines = append(lines,
			fmt.Sprintf("    fsub v28%s, %s%s, %s%s", lane, s1HighV, lane, s2HighV, lane),
			fmt.Sprintf("    fadd v29%s, %s%s, %s%s", lane, s1HighV, lane, s2HighV, lane),
			fmt.Sprintf("    mov %s.16b, v28.16b", dHighV),
		)
		if isDouble {
			lines = append(lines, fmt.Sprintf("    mov %s.d[1], v29.d[1]", dHighV))
		} else {
			lines = append(lines,
				fmt.Sprintf("    mov %s.s[1], v29.s[1]", dHighV),
				fmt.Sprintf("    mov %s.s[3], v29.s[3]", dHighV),
			)
		}
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftPavg handles PAVGB, VPAVGB, PAVGW, and VPAVGW (average unsigned integers with rounding: (a + b + 1) >> 1).
func (l *Arm64Lifter) liftPavg(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isWord := (op == x86asm.PAVGW || op == x86asm.VPAVGW)
	isAvx := (op == x86asm.VPAVGB || op == x86asm.VPAVGW)

	var dstArg, s1Arg, s2Arg x86asm.Arg
	if isAvx {
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[1]
		s2Arg = inst.Inst.Args[2]
	} else {
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[0]
		s2Arg = inst.Inst.Args[1]
	}

	dstReg, okDst := dstArg.(x86asm.Reg)
	s1Reg, okS1 := s1Arg.(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pavg dst/src1")...)
		return lines, nil
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okS2 := s2Arg.(x86asm.Reg); okS2 {
		if _, _, _, rL, rH, _, _, okS2D := getVectorDesc(s2Reg); okS2D {
			s2LowV = rL
			s2HighV = rH
		}
	} else if mem, okMem := s2Arg.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16", "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pavg src2")...)
		return lines, nil
	}

	lane := ".16b"
	if isWord {
		lane = ".8h"
	}

	// ARM64 URHADD: Unsigned Rounding Halving Add: (a + b + 1) >> 1
	lines = append(lines, fmt.Sprintf("    urhadd %s%s, %s%s, %s%s", dLowV, lane, s1LowV, lane, s2LowV, lane))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    urhadd %s%s, %s%s, %s%s", dHighV, lane, s1HighV, lane, s2HighV, lane))
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftPabs handles PABSB, VPABSB, PABSW, VPABSW, PABSD, and VPABSD.
func (l *Arm64Lifter) liftPabs(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isAvx := (op == x86asm.VPABSB || op == x86asm.VPABSW || op == x86asm.VPABSD)
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	dstReg, okDst := dst.(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pabs dst")...)
		return lines, nil
	}

	sLowV := "v30"
	sHighV := "v31"
	if sReg, okS := src.(x86asm.Reg); okS {
		if _, _, _, rL, rH, _, _, okSD := getVectorDesc(sReg); okSD {
			sLowV = rL
			sHighV = rH
		}
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16", "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pabs src")...)
		return lines, nil
	}

	lane := ".16b"
	if op == x86asm.PABSW || op == x86asm.VPABSW {
		lane = ".8h"
	} else if op == x86asm.PABSD || op == x86asm.VPABSD {
		lane = ".4s"
	}

	lines = append(lines, fmt.Sprintf("    abs %s%s, %s%s", dLowV, lane, sLowV, lane))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    abs %s%s, %s%s", dHighV, lane, sHighV, lane))
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftPsadbw handles PSADBW and VPSADBW (sum of absolute differences of 8-bit unsigned integers across 64-bit halves).
func (l *Arm64Lifter) liftPsadbw(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isAvx := (op == x86asm.VPSADBW)

	var dstArg, s1Arg, s2Arg x86asm.Arg
	if isAvx {
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[1]
		s2Arg = inst.Inst.Args[2]
	} else {
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[0]
		s2Arg = inst.Inst.Args[1]
	}

	dstReg, okDst := dstArg.(x86asm.Reg)
	s1Reg, okS1 := s1Arg.(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported psadbw dst/src1")...)
		return lines, nil
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okS2 := s2Arg.(x86asm.Reg); okS2 {
		if _, _, _, rL, rH, _, _, okS2D := getVectorDesc(s2Reg); okS2D {
			s2LowV = rL
			s2HighV = rH
		}
	} else if mem, okMem := s2Arg.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16", "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported psadbw src2")...)
		return lines, nil
	}

	computeSadHalf := func(s1, s2, d string) []string {
		return []string{
			fmt.Sprintf("    uabd v28.16b, %s.16b, %s.16b", s1, s2), // absolute difference
			"    uaddlp v29.8h, v28.16b",                            // sum 8-bit pairs to 16-bit
			"    uaddlp v28.4s, v29.8h",                             // sum 16-bit pairs to 32-bit
			"    uaddlp v29.2d, v28.4s",                             // sum 32-bit pairs to 64-bit
			fmt.Sprintf("    mov %s.16b, v29.16b", d),
		}
	}

	lines = append(lines, computeSadHalf(s1LowV, s2LowV, dLowV)...)
	if isYmm {
		lines = append(lines, computeSadHalf(s1HighV, s2HighV, dHighV)...)
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftPmaddwd handles PMADDWD and VPMADDWD.
func (l *Arm64Lifter) liftPmaddwd(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isAvx := (op == x86asm.VPMADDWD)

	var dstArg, s1Arg, s2Arg x86asm.Arg
	if isAvx {
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[1]
		s2Arg = inst.Inst.Args[2]
	} else {
		dstArg = inst.Inst.Args[0]
		s1Arg = inst.Inst.Args[0]
		s2Arg = inst.Inst.Args[1]
	}

	dstReg, okDst := dstArg.(x86asm.Reg)
	s1Reg, okS1 := s1Arg.(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pmaddwd dst/src1")...)
		return lines, nil
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if s2Reg, okS2 := s2Arg.(x86asm.Reg); okS2 {
		if _, _, _, rL, rH, _, _, okS2D := getVectorDesc(s2Reg); okS2D {
			s2LowV = rL
			s2HighV = rH
		}
	} else if mem, okMem := s2Arg.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16", "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pmaddwd src2")...)
		return lines, nil
	}

	computePmadd := func(s1, s2, d string) []string {
		return []string{
			fmt.Sprintf("    smull v28.4s, %s.4h, %s.4h", s1, s2),  // low 4 words multiply
			fmt.Sprintf("    smull2 v29.4s, %s.8h, %s.8h", s1, s2), // high 4 words multiply
			"    addp v28.4s, v28.4s, v28.4s",                      // pairwise add low halves
			"    addp v29.4s, v29.4s, v29.4s",                      // pairwise add high halves
			fmt.Sprintf("    mov %s.d[0], v28.d[0]", d),
			fmt.Sprintf("    mov %s.d[1], v29.d[0]", d),
		}
	}

	lines = append(lines, computePmadd(s1LowV, s2LowV, dLowV)...)
	if isYmm {
		lines = append(lines, computePmadd(s1HighV, s2HighV, dHighV)...)
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftSSE4A lowers AMD's EXTRQ and INSERTQ (see disasm.OpEXTRQ for the operands): a bit field of
// the destination's low quadword is extracted (shifted down to bit 0, the bits above it zeroed)
// or replaced. A length of 0 means 64. Bits 127:64 of the destination are undefined in the AMD
// manual and are left as they were; these are legacy SSE instructions, so a YMM upper half is
// not written either. The flags are not affected.
//
// A field with index + length > 64 is undefined as well; the lowering keeps the bits that fit
// below bit 64 (the register forms and the immediate forms agree on this).
func (l *Arm64Lifter) liftSSE4A(inst disasm.Instruction, lines []string) ([]string, error) {
	args := inst.Inst.Args
	dst, okD := args[0].(x86asm.Reg)
	_, _, _, dV, _, _, dD, okDV := getVectorDesc(dst)
	if !okD || !okDV || dst > x86asm.X15 {
		return append(lines, l.emitUnsupported(inst, "SSE4A destination is not an XMM register")...), nil
	}
	if inst.Inst.Op == disasm.OpEXTRQ {
		if length, ok := args[1].(x86asm.Imm); ok {
			idx, w := sse4aField(length, args[2].(x86asm.Imm))
			if idx == 0 && w == 64 {
				return lines, nil // the whole quadword: nothing changes
			}
			return append(lines,
				fmt.Sprintf("    fmov x16, %s", dD),
				fmt.Sprintf("    ubfx x16, x16, #%d, #%d", idx, w),
				fmt.Sprintf("    mov %s.d[0], x16", dV),
			), nil
		}
	}
	src, okS := args[1].(x86asm.Reg)
	_, _, _, sV, _, _, sD, okSV := getVectorDesc(src)
	if !okS || !okSV || src > x86asm.X15 {
		return append(lines, l.emitUnsupported(inst, "SSE4A source is not an XMM register")...), nil
	}
	if inst.Inst.Op == disasm.OpEXTRQ {
		// Length in xmm2[5:0], index in xmm2[13:8]. LSRV takes its count mod 64, so the
		// control word needs no masking: (-length) mod 64 is 64 - length, or 0 for length 0.
		return append(lines,
			fmt.Sprintf("    fmov x17, %s", sD),
			fmt.Sprintf("    fmov x16, %s", dD),
			"    lsr x6, x17, #8",
			"    lsr x16, x16, x6",
			"    neg x7, x17",
			"    mov x6, #-1",
			"    lsr x6, x6, x7",
			"    and x16, x16, x6",
			fmt.Sprintf("    mov %s.d[0], x16", dV),
		), nil
	}
	if length, ok := args[2].(x86asm.Imm); ok {
		idx, w := sse4aField(length, args[3].(x86asm.Imm))
		return append(lines,
			fmt.Sprintf("    fmov x16, %s", sD),
			fmt.Sprintf("    fmov x17, %s", dD),
			fmt.Sprintf("    bfi x17, x16, #%d, #%d", idx, w),
			fmt.Sprintf("    mov %s.d[0], x17", dV),
		), nil
	}
	// INSERTQ xmm1, xmm2: the field is xmm2[length-1:0], the length xmm2[69:64] and the index
	// xmm2[77:72]. Both are read before the destination (which may be xmm2) is written.
	return append(lines,
		fmt.Sprintf("    mov x17, %s.d[1]", sV),
		fmt.Sprintf("    fmov x16, %s", sD),
		"    neg x7, x17",
		"    mov x6, #-1",
		"    lsr x6, x6, x7",
		"    lsr x7, x17, #8",
		"    lsl x6, x6, x7",
		"    lsl x16, x16, x7",
		"    and x16, x16, x6",
		fmt.Sprintf("    fmov x17, %s", dD),
		"    bic x17, x17, x6",
		"    orr x16, x16, x17",
		fmt.Sprintf("    mov %s.d[0], x16", dV),
	), nil
}

// sse4aField returns the bit index and the width of the field that fits below bit 64 for the
// immediate forms: bits 5:0 of each byte, a length of 0 meaning 64.
func sse4aField(length, index x86asm.Imm) (idx, w int) {
	idx = int(index & 63)
	w = int(length & 63)
	if w == 0 {
		w = 64
	}
	if idx+w > 64 {
		w = 64 - idx
	}
	return idx, w
}
