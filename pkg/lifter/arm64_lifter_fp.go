package lifter

import (
	"fmt"
	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/disasm"
)

// liftFpAlu handles the legacy SSE scalar arithmetic (ADDSS ... DIVSD). Only the low element of
// the destination changes: the result is computed in v31 and inserted, because writing an ARM
// S/D register would clear the rest of the vector register.
func (l *Arm64Lifter) liftFpAlu(inst disasm.Instruction, nextPC uint64, armOp string, isDouble bool, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	dstReg, okDst := dst.(x86asm.Reg)
	xmmDst, okXD := arm64XmmMap[dstReg]
	if !okDst || !okXD {
		lines = append(lines, "    brk #0 // unsupported fp dst")
		return lines, nil
	}
	el, lane := xmmDst.S, "s"
	if isDouble {
		el, lane = xmmDst.D, "d"
	}
	srcEl := ""
	if srcReg, okSrc := src.(x86asm.Reg); okSrc {
		if xmmSrc, okXS := arm64XmmMap[srcReg]; okXS {
			srcEl = xmmSrc.S
			if isDouble {
				srcEl = xmmSrc.D
			}
		}
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, fmt.Sprintf("    ldr %s31, [x28, x16]", lane))
		srcEl = lane + "31"
	}
	if srcEl == "" {
		lines = append(lines, "    brk #0 // unsupported fp alu")
		return lines, nil
	}
	lines = append(lines, fmt.Sprintf("    %s %s31, %s, %s", armOp, lane, el, srcEl),
		fmt.Sprintf("    mov %s.%s[0], v31.%s[0]", xmmDst.V, lane, lane))
	return lines, nil
}

// fpCompareFlags converts the NZCV of an ARM FCMP (lt 1000, eq 0110, gt 0010, unordered 0011)
// into the lifter's x86 model of (U)COMIS/FCOMI flags: Z = ZF, C = !CF (carrySub), V = PF, N = 0
// (lt 0000, eq 0110, gt 0010, unordered 0101). AXFLAG gives Z = ZF and C = !CF and clears N and
// V; V is then set from the unordered result captured before.
func (l *Arm64Lifter) fpCompareFlags() []string {
	l.cpol = carrySub
	return []string{"    cset w2, vs", "    axflag", "    rmif x2, #0, #1"}
}

// liftFpCmp handles UCOMISS, UCOMISD, COMISS, COMISD, VUCOMISS, VUCOMISD, VCOMISS, and VCOMISD.
func (l *Arm64Lifter) liftFpCmp(inst disasm.Instruction, nextPC uint64, isDouble bool, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	dstReg, okDst := dst.(x86asm.Reg)
	_, _, _, _, _, dstLowS, dstLowD, okVD := getVectorDesc(dstReg)
	if !okDst || !okVD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported fp cmp dst")...)
		return lines, nil
	}

	dstElem := dstLowS
	if isDouble {
		dstElem = dstLowD
	}

	if srcReg, okSrc := src.(x86asm.Reg); okSrc {
		if _, _, _, _, _, srcLowS, srcLowD, okVS := getVectorDesc(srcReg); okVS {
			srcElem := srcLowS
			if isDouble {
				srcElem = srcLowD
			}
			lines = append(lines, fmt.Sprintf("    fcmp %s, %s", dstElem, srcElem))
			lines = append(lines, l.fpCompareFlags()...)
			return lines, nil
		}
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		if isDouble {
			lines = append(lines, "    ldr d16, [x28, x16]")
			lines = append(lines, fmt.Sprintf("    fcmp %s, d16", dstElem))
		} else {
			lines = append(lines, "    ldr s16, [x28, x16]")
			lines = append(lines, fmt.Sprintf("    fcmp %s, s16", dstElem))
		}
		lines = append(lines, l.fpCompareFlags()...)
		return lines, nil
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported fp cmp src")...)
	return lines, nil
}

// liftCvtsi2fp handles CVTSI2SS, CVTSI2SD, VCVTSI2SS, and VCVTSI2SD.
func (l *Arm64Lifter) liftCvtsi2fp(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	_, _, _, dLowV, dHighV, dLowS, dLowD, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported cvtsi2fp dst")...)
		return lines, nil
	}

	isDouble := (inst.Inst.Op == x86asm.CVTSI2SD || inst.Inst.Op == x86asm.VCVTSI2SD)
	isAvx := (inst.Inst.Op == x86asm.VCVTSI2SS || inst.Inst.Op == x86asm.VCVTSI2SD)

	var s1LowV string
	var src2 x86asm.Arg
	if isAvx {
		if src1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg); okS1 {
			if _, _, _, sLowV, _, _, _, okDesc := getVectorDesc(src1Reg); okDesc {
				s1LowV = sLowV
			}
		}
		src2 = inst.Inst.Args[2]
	} else {
		src2 = inst.Inst.Args[1]
	}

	if srcReg, okSrc := src2.(x86asm.Reg); okSrc {
		if descSrc, okDesc := arm64RegMap[srcReg]; okDesc {
			if isDouble {
				if descSrc.Size == 4 {
					lines = append(lines, fmt.Sprintf("    scvtf d30, %s", descSrc.W))
				} else {
					lines = append(lines, fmt.Sprintf("    scvtf d30, %s", descSrc.X))
				}
			} else {
				if descSrc.Size == 4 {
					lines = append(lines, fmt.Sprintf("    scvtf s30, %s", descSrc.W))
				} else {
					lines = append(lines, fmt.Sprintf("    scvtf s30, %s", descSrc.X))
				}
			}
		} else {
			lines = append(lines, l.emitUnsupported(inst, "unsupported cvtsi2fp src reg")...)
			return lines, nil
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		// The integer size is the memory operand's (m32 or m64), independent of the FP type.
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		ireg := "w17"
		if inst.Inst.MemBytes == 8 {
			ireg = "x17"
		}
		lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", ireg))
		if isDouble {
			lines = append(lines, fmt.Sprintf("    scvtf d30, %s", ireg))
		} else {
			lines = append(lines, fmt.Sprintf("    scvtf s30, %s", ireg))
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported cvtsi2fp src")...)
		return lines, nil
	}

	if isAvx {
		if s1LowV != "" && dLowV != s1LowV {
			lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1LowV))
		}
		if isDouble {
			lines = append(lines, fmt.Sprintf("    mov %s.d[0], v30.d[0]", dLowV))
		} else {
			lines = append(lines, fmt.Sprintf("    mov %s.s[0], v30.s[0]", dLowV))
		}
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	} else {
		// Legacy SSE: only the low element changes.
		_, _ = dLowS, dLowD
		if isDouble {
			lines = append(lines, fmt.Sprintf("    mov %s.d[0], v30.d[0]", dLowV))
		} else {
			lines = append(lines, fmt.Sprintf("    mov %s.s[0], v30.s[0]", dLowV))
		}
	}
	return lines, nil
}

// fpToIntX86 converts the float/double in register src (s<n>/d<n>) to a signed integer in dst
// (w/x register) with x86 semantics: CVTT* truncate, the others round by the current mode
// (FPCR, kept in sync with MXCSR.RC by LDMXCSR); NaN and out-of-range values give the integer
// indefinite value (0x80000000 / 0x8000000000000000) where ARM FCVTZS would saturate or give 0.
// The fix-ups do not touch NZCV (x86 conversions preserve the flags). Scratch: x1, x2, v31.
func fpToIntX86(dst, src string, trunc bool) []string {
	var out []string
	is64 := dst[0] == 'x'
	isDouble := src[0] == 'd'
	if !trunc {
		out = append(out, fmt.Sprintf("    frintx %c31, %s", src[0], src))
		src = fmt.Sprintf("%c31", src[0])
	}
	w := "w" + dst[1:]
	// Ordered mask (all ones unless NaN) in x2.
	mask := func() {
		out = append(out, fmt.Sprintf("    fcmeq %c30, %s, %s", src[0], src, src))
		if isDouble {
			out = append(out, "    fmov x2, d30")
		} else {
			out = append(out, "    fmov w2, s30", "    sxtw x2, w2")
		}
	}
	if is64 || !isDouble {
		// Only the positive saturation needs fixing (negative saturation is the indefinite
		// value): MAX + 1 wraps to MIN. MAX is not representable in the source type here.
		out = append(out, fmt.Sprintf("    fcvtzs %s, %s", dst, src))
		mask()
		if is64 {
			out = append(out, fmt.Sprintf("    eor x1, %s, #0x7fffffffffffffff", dst), "    clz x1, x1",
				fmt.Sprintf("    add %s, %s, x1, lsr #6", dst, dst),
				"    mvn x2, x2", "    and x2, x2, #0x8000000000000000", fmt.Sprintf("    orr %s, %s, x2", dst, dst))
		} else {
			out = append(out, fmt.Sprintf("    eor w1, %s, #0x7fffffff", dst), "    clz w1, w1",
				fmt.Sprintf("    add %s, %s, w1, lsr #5", dst, dst),
				"    mvn w2, w2", "    and w2, w2, #0x80000000", fmt.Sprintf("    orr %s, %s, w2", dst, dst))
		}
		return out
	}
	// double -> int32: convert to 64 bits and check that the value fits.
	x := "x" + dst[1:]
	out = append(out, fmt.Sprintf("    fcvtzs %s, %s", x, src))
	mask()
	out = append(out,
		fmt.Sprintf("    sxtw x1, %s", w),
		fmt.Sprintf("    eor x1, x1, %s", x), // 0 iff it fits
		"    clz x1, x1",
		"    lsr x1, x1, #6",
		"    sub x1, x1, #1", // 0 iff it fits, else all ones
		"    orn x1, x1, x2", // ... or NaN
		fmt.Sprintf("    bic %s, %s, w1", w, w),
		"    and w1, w1, #0x80000000",
		fmt.Sprintf("    orr %s, %s, w1", w, w))
	return out
}

// liftCvtfp2si handles CVTSS2SI, CVTSD2SI, CVTTSS2SI, CVTTSD2SI, VCVTSS2SI, VCVTSD2SI, VCVTTSS2SI, VCVTTSD2SI.
func (l *Arm64Lifter) liftCvtfp2si(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	descDst, okDesc := arm64RegMap[dstReg]
	isDouble := (op == x86asm.CVTSD2SI || op == x86asm.CVTTSD2SI || op == x86asm.VCVTSD2SI || op == x86asm.VCVTTSD2SI)
	trunc := (op == x86asm.CVTTSS2SI || op == x86asm.CVTTSD2SI || op == x86asm.VCVTTSS2SI || op == x86asm.VCVTTSD2SI)
	if !okDst || !okDesc {
		lines = append(lines, l.emitUnsupported(inst, "unsupported cvtfp2si dst")...)
		return lines, nil
	}
	dst := descDst.W
	if descDst.Size == 8 {
		dst = descDst.X
	}
	src := ""
	if srcReg, okSrc := inst.Inst.Args[1].(x86asm.Reg); okSrc {
		if _, _, _, _, _, sLowS, sLowD, okS := getVectorDesc(srcReg); okS {
			src = sLowS
			if isDouble {
				src = sLowD
			}
		}
	} else if mem, okMem := inst.Inst.Args[1].(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		src = "s29"
		if isDouble {
			src = "d29"
		}
		lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", src))
	}
	if src == "" {
		lines = append(lines, l.emitUnsupported(inst, "unsupported cvtfp2si src")...)
		return lines, nil
	}
	lines = append(lines, fpToIntX86(dst, src, trunc)...)
	return lines, nil
}

// liftFpConvert handles CVTSS2SD, CVTSD2SS, VCVTSS2SD, and VCVTSD2SS.
func (l *Arm64Lifter) liftFpConvert(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	_, _, _, dLowV, dHighV, dLowS, dLowD, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported fp convert dst")...)
		return lines, nil
	}

	isSS2SD := (inst.Inst.Op == x86asm.CVTSS2SD || inst.Inst.Op == x86asm.VCVTSS2SD)
	isAvx := (inst.Inst.Op == x86asm.VCVTSS2SD || inst.Inst.Op == x86asm.VCVTSD2SS)

	var s1LowV string
	var src2 x86asm.Arg
	if isAvx {
		if src1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg); okS1 {
			if _, _, _, sLowV, _, _, _, okDesc := getVectorDesc(src1Reg); okDesc {
				s1LowV = sLowV
			}
		}
		src2 = inst.Inst.Args[2]
	} else {
		src2 = inst.Inst.Args[1]
	}

	if !isAvx {
		// Legacy SSE: convert into v30 and insert the low element; the rest is kept.
		_, _ = dLowS, dLowD
		cvt := ""
		if srcReg, okSrc := src2.(x86asm.Reg); okSrc {
			if _, _, _, _, _, sLowS, sLowD, okXS := getVectorDesc(srcReg); okXS {
				if isSS2SD {
					cvt = fmt.Sprintf("    fcvt d30, %s", sLowS)
				} else {
					cvt = fmt.Sprintf("    fcvt s30, %s", sLowD)
				}
			}
		} else if mem, okMem := src2.(x86asm.Mem); okMem {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			if isSS2SD {
				lines = append(lines, "    ldr s31, [x28, x16]")
				cvt = "    fcvt d30, s31"
			} else {
				lines = append(lines, "    ldr d31, [x28, x16]")
				cvt = "    fcvt s30, d31"
			}
		}
		if cvt == "" {
			lines = append(lines, l.emitUnsupported(inst, "unsupported fp convert operand")...)
			return lines, nil
		}
		lines = append(lines, cvt)
		if isSS2SD {
			lines = append(lines, fmt.Sprintf("    mov %s.d[0], v30.d[0]", dLowV))
		} else {
			lines = append(lines, fmt.Sprintf("    mov %s.s[0], v30.s[0]", dLowV))
		}
		return lines, nil
	}

	// AVX path
	if srcReg, okSrc := src2.(x86asm.Reg); okSrc {
		if _, _, _, _, _, sLowS, sLowD, okXS := getVectorDesc(srcReg); okXS {
			if isSS2SD {
				lines = append(lines, fmt.Sprintf("    fcvt d30, %s", sLowS))
			} else {
				lines = append(lines, fmt.Sprintf("    fcvt s30, %s", sLowD))
			}
		} else {
			lines = append(lines, l.emitUnsupported(inst, "unsupported fp convert src reg")...)
			return lines, nil
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		if isSS2SD {
			lines = append(lines, "    ldr s31, [x28, x16]")
			lines = append(lines, "    fcvt d30, s31")
		} else {
			lines = append(lines, "    ldr d31, [x28, x16]")
			lines = append(lines, "    fcvt s30, d31")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported fp convert operand")...)
		return lines, nil
	}

	if s1LowV != "" && dLowV != s1LowV {
		lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1LowV))
	}
	if isSS2SD {
		lines = append(lines, fmt.Sprintf("    mov %s.d[0], v30.d[0]", dLowV))
	} else {
		lines = append(lines, fmt.Sprintf("    mov %s.s[0], v30.s[0]", dLowV))
	}
	lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	return lines, nil
}

// liftAvxScalarMove handles VMOVSS and VMOVSD.
func (l *Arm64Lifter) liftAvxScalarMove(inst disasm.Instruction, nextPC uint64, isDouble bool, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	// Mem <- Reg
	if mem, okMem := dst.(x86asm.Mem); okMem {
		if srcReg, okSrc := src.(x86asm.Reg); okSrc {
			if _, _, _, _, _, sLowS, sLowD, okS := getVectorDesc(srcReg); okS {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				if isDouble {
					lines = append(lines, fmt.Sprintf("    str %s, [x28, x16]", sLowD))
				} else {
					lines = append(lines, fmt.Sprintf("    str %s, [x28, x16]", sLowS))
				}
				return lines, nil
			}
		}
	}

	// Reg <- Mem (load scalar and zero upper bits)
	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		if _, _, _, dLowV, dHighV, dLowS, dLowD, okD := getVectorDesc(dstReg); okD {
			if mem, okMem := src.(x86asm.Mem); okMem {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dLowV))
				if isDouble {
					lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", dLowD))
				} else {
					lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", dLowS))
				}
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
				return lines, nil
			}
			// Reg <- Reg, Reg (3 operands: dst = [src1[1..3], src2[0]])
			if len(inst.Inst.Args) >= 3 && inst.Inst.Args[2] != nil {
				src1 := inst.Inst.Args[1]
				src2 := inst.Inst.Args[2]
				if s1Reg, ok1 := src1.(x86asm.Reg); ok1 {
					if s2Reg, ok2 := src2.(x86asm.Reg); ok2 {
						if _, _, _, s1LowV, _, _, _, okS1 := getVectorDesc(s1Reg); okS1 {
							if _, _, _, s2LowV, _, _, _, okS2 := getVectorDesc(s2Reg); okS2 {
								if isDouble {
									lines = append(lines, fmt.Sprintf("    mov v30.16b, %s.16b", s2LowV))
									lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1LowV))
									lines = append(lines, fmt.Sprintf("    mov %s.d[0], v30.d[0]", dLowV))
								} else {
									lines = append(lines, fmt.Sprintf("    mov v30.16b, %s.16b", s2LowV))
									lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1LowV))
									lines = append(lines, fmt.Sprintf("    mov %s.s[0], v30.s[0]", dLowV))
								}
								lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
								return lines, nil
							}
						}
					}
				}
			}
			// Reg <- Reg (2 operands: copy low element)
			if srcReg, okSrc := src.(x86asm.Reg); okSrc {
				if _, _, _, sLowV, _, _, _, okS := getVectorDesc(srcReg); okS {
					lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, sLowV))
					lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
					return lines, nil
				}
			}
		}
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported avx scalar move")...)
	return lines, nil
}

// liftAvxScalarAlu handles 3-operand scalar float ALU (VADDSS, VMULSS, VSUBSS, VDIVSS, VADDSD, VMULSD, VSUBSD, VDIVSD).
func (l *Arm64Lifter) liftAvxScalarAlu(inst disasm.Instruction, nextPC uint64, armOp string, isDouble bool, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	src1Reg, okSrc1 := inst.Inst.Args[1].(x86asm.Reg)
	src2 := inst.Inst.Args[2]

	_, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, _, s1LowS, s1LowD, okS1 := getVectorDesc(src1Reg)
	if !okDst || !okSrc1 || !okD || !okS1 {
		lines = append(lines, l.emitUnsupported(inst, "unsupported avx scalar alu dst/src1")...)
		return lines, nil
	}

	s2S := "s30"
	s2D := "d30"
	if s2Reg, okSrc2 := src2.(x86asm.Reg); okSrc2 {
		if _, _, _, _, _, rS, rD, okS2 := getVectorDesc(s2Reg); okS2 {
			s2S = rS
			s2D = rD
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		if isDouble {
			lines = append(lines, "    ldr d30, [x28, x16]")
		} else {
			lines = append(lines, "    ldr s30, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported avx scalar alu src2")...)
		return lines, nil
	}

	// 1. Perform ALU into temporary d30/s30 to avoid clobbering src2 if dstReg == src2
	if isDouble {
		lines = append(lines, fmt.Sprintf("    %s d30, %s, %s", armOp, s1LowD, s2D))
	} else {
		lines = append(lines, fmt.Sprintf("    %s s30, %s, %s", armOp, s1LowS, s2S))
	}

	// 2. Copy lanes 1..3 from src1 into dst
	lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1LowV))

	// 3. Insert computed scalar into lane 0 of dst
	if isDouble {
		lines = append(lines, fmt.Sprintf("    mov %s.d[0], v30.d[0]", dLowV))
	} else {
		lines = append(lines, fmt.Sprintf("    mov %s.s[0], v30.s[0]", dLowV))
	}
	lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	return lines, nil
}

// liftRound handles ROUNDSD, ROUNDSS, VROUNDSD, VROUNDSS, ROUNDPS, ROUNDPD, VROUNDPS, and VROUNDPD.
func (l *Arm64Lifter) liftRound(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, dLowS, dLowD, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported round dst")...)
		return lines, nil
	}

	isScalar := (op == x86asm.ROUNDSD || op == x86asm.VROUNDSD || op == x86asm.ROUNDSS || op == x86asm.VROUNDSS)
	isDouble := (op == x86asm.ROUNDSD || op == x86asm.VROUNDSD || op == x86asm.ROUNDPD || op == x86asm.VROUNDPD)
	isAvx := (op == x86asm.VROUNDSD || op == x86asm.VROUNDSS || op == x86asm.VROUNDPD || op == x86asm.VROUNDPS)

	var immVal uint8
	if isScalar {
		var s1LowV string
		var src2 x86asm.Arg
		var imm x86asm.Arg
		if isAvx {
			if s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg); okS1 {
				if _, _, _, sLow, _, _, _, okDesc := getVectorDesc(s1Reg); okDesc {
					s1LowV = sLow
				}
			}
			src2 = inst.Inst.Args[2]
			imm = inst.Inst.Args[3]
		} else {
			src2 = inst.Inst.Args[1]
			imm = inst.Inst.Args[2]
		}

		immVal = uint8(imm.(x86asm.Imm))
		frintOp := "frintn"
		switch immVal & 3 {
		case 1:
			frintOp = "frintm" // floor
		case 2:
			frintOp = "frintp" // ceil
		case 3:
			frintOp = "frintz" // trunc
		}

		if srcReg, okSrc := src2.(x86asm.Reg); okSrc {
			if _, _, _, _, _, sLowS, sLowD, okDesc := getVectorDesc(srcReg); okDesc {
				if isDouble {
					lines = append(lines, fmt.Sprintf("    %s d30, %s", frintOp, sLowD))
				} else {
					lines = append(lines, fmt.Sprintf("    %s s30, %s", frintOp, sLowS))
				}
			}
		} else if mem, okMem := src2.(x86asm.Mem); okMem {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			if isDouble {
				lines = append(lines, "    ldr d30, [x28, x16]")
				lines = append(lines, fmt.Sprintf("    %s d30, d30", frintOp))
			} else {
				lines = append(lines, "    ldr s30, [x28, x16]")
				lines = append(lines, fmt.Sprintf("    %s s30, s30", frintOp))
			}
		} else {
			lines = append(lines, l.emitUnsupported(inst, "unsupported round src")...)
			return lines, nil
		}

		if isAvx {
			if s1LowV != "" && dLowV != s1LowV {
				lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1LowV))
			}
			if isDouble {
				lines = append(lines, fmt.Sprintf("    mov %s.d[0], v30.d[0]", dLowV))
			} else {
				lines = append(lines, fmt.Sprintf("    mov %s.s[0], v30.s[0]", dLowV))
			}
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		} else {
			if isDouble {
				lines = append(lines, fmt.Sprintf("    fmov %s, d30", dLowD))
			} else {
				lines = append(lines, fmt.Sprintf("    fmov %s, s30", dLowS))
			}
		}
		return lines, nil
	}

	// Vector packed ROUNDPS / VROUNDPS / ROUNDPD / VROUNDPD
	src2 := inst.Inst.Args[1]
	imm := inst.Inst.Args[2]
	immVal = uint8(imm.(x86asm.Imm))
	frintOp := "frintn"
	switch immVal & 3 {
	case 1:
		frintOp = "frintm" // floor
	case 2:
		frintOp = "frintp" // ceil
	case 3:
		frintOp = "frintz" // trunc
	}

	lane := ".4s"
	if isDouble {
		lane = ".2d"
	}

	s2LowV := "v30"
	s2HighV := "v31"
	if srcReg, okSrc := src2.(x86asm.Reg); okSrc {
		if _, _, _, rLowV, rHighV, _, _, okDesc := getVectorDesc(srcReg); okDesc {
			s2LowV = rLowV
			s2HighV = rHighV
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16", "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported packed round src")...)
		return lines, nil
	}

	lines = append(lines, fmt.Sprintf("    %s %s%s, %s%s", frintOp, dLowV, lane, s2LowV, lane))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    %s %s%s, %s%s", frintOp, dHighV, lane, s2HighV, lane))
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftVhsub handles HSUBPS, VHSUBPS, HSUBPD, and VHSUBPD.
func (l *Arm64Lifter) liftVhsub(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vhsub dst")...)
		return lines, nil
	}

	isDouble := (op == x86asm.HSUBPD || op == x86asm.VHSUBPD)
	isAvx := (op == x86asm.VHSUBPS || op == x86asm.VHSUBPD)

	var s1LowV, s1HighV string
	var src2 x86asm.Arg
	if isAvx {
		if s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg); okS1 {
			_, _, _, sLow, sHigh, _, _, _ := getVectorDesc(s1Reg)
			s1LowV = sLow
			s1HighV = sHigh
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
			lines = append(lines, "    add x16, x16, #16", "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vhsub src2")...)
		return lines, nil
	}

	lane := ".4s"
	if isDouble {
		lane = ".2d"
	}

	lines = append(lines,
		fmt.Sprintf("    uzp1 v28%s, %s%s, %s%s", lane, s1LowV, lane, s2LowV, lane),
		fmt.Sprintf("    uzp2 v29%s, %s%s, %s%s", lane, s1LowV, lane, s2LowV, lane),
		fmt.Sprintf("    fsub %s%s, v28%s, v29%s", dLowV, lane, lane, lane),
	)
	if isYmm {
		lines = append(lines,
			fmt.Sprintf("    uzp1 v28%s, %s%s, %s%s", lane, s1HighV, lane, s2HighV, lane),
			fmt.Sprintf("    uzp2 v29%s, %s%s, %s%s", lane, s1HighV, lane, s2HighV, lane),
			fmt.Sprintf("    fsub %s%s, v28%s, v29%s", dHighV, lane, lane, lane),
		)
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftVphadd handles PHADDD, VPHADDD, PHADDW, and VPHADDW.
func (l *Arm64Lifter) liftVphadd(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vphadd dst")...)
		return lines, nil
	}

	isWord := (op == x86asm.PHADDW || op == x86asm.VPHADDW)
	isAvx := (op == x86asm.VPHADDD || op == x86asm.VPHADDW)

	var s1LowV, s1HighV string
	var src2 x86asm.Arg
	if isAvx {
		if s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg); okS1 {
			_, _, _, sLow, sHigh, _, _, _ := getVectorDesc(s1Reg)
			s1LowV = sLow
			s1HighV = sHigh
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
			lines = append(lines, "    add x16, x16, #16", "    ldr q31, [x28, x16]")
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vphadd src2")...)
		return lines, nil
	}

	lane := ".4s"
	if isWord {
		lane = ".8h"
	}

	lines = append(lines, fmt.Sprintf("    addp %s%s, %s%s, %s%s", dLowV, lane, s1LowV, lane, s2LowV, lane))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    addp %s%s, %s%s, %s%s", dHighV, lane, s1HighV, lane, s2HighV, lane))
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftAvxMinMax handles VMAXSD, VMINSD, VMAXSS, and VMINSS.
func (l *Arm64Lifter) liftAvxMinMax(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg)
	_, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, _, s1LowS, s1LowD, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported avx min/max dst/src1")...)
		return lines, nil
	}

	isDouble := (op == x86asm.VMAXSD || op == x86asm.VMINSD)
	isMax := (op == x86asm.VMAXSD || op == x86asm.VMAXSS)
	armOp := "fmin"
	if isMax {
		armOp = "fmax"
	}

	// x86 semantics: max(a, b) = a > b ? a : b, min(a, b) = a < b ? a : b (b when either is
	// NaN or both are zero). The selected element ends up in lane 0 of v30.
	_ = armOp
	_, _ = s1LowS, s1LowD
	src2 := inst.Inst.Args[2]
	s2V := ""
	if srcReg, okSrc := src2.(x86asm.Reg); okSrc {
		if _, _, _, r2LowV, _, _, _, okS2 := getVectorDesc(srcReg); okS2 {
			s2V = r2LowV
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		if isDouble {
			lines = append(lines, "    ldr d31, [x28, x16]")
		} else {
			lines = append(lines, "    ldr s31, [x28, x16]")
		}
		s2V = "v31"
	}
	if s2V == "" {
		lines = append(lines, l.emitUnsupported(inst, "unsupported avx min/max src2")...)
		return lines, nil
	}
	el := "s"
	if isDouble {
		el = "d"
	}
	a, b := s1LowV, s2V
	if !isMax {
		a, b = b, a
	}
	lines = append(lines, fmt.Sprintf("    fcmgt %s30, %s%s, %s%s", el, el, a[1:], el, b[1:]),
		fmt.Sprintf("    bsl v30.16b, %s.16b, %s.16b", s1LowV, s2V))

	if dLowV != s1LowV {
		lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1LowV))
	}
	if isDouble {
		lines = append(lines, fmt.Sprintf("    mov %s.d[0], v30.d[0]", dLowV))
	} else {
		lines = append(lines, fmt.Sprintf("    mov %s.s[0], v30.s[0]", dLowV))
	}
	lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	return lines, nil
}

// liftAvxDoubleAlu handles VADDPD, VSUBPD, VMULPD, and VDIVPD.
func (l *Arm64Lifter) liftAvxDoubleAlu(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	armOp := "fadd"
	switch inst.Inst.Op {
	case x86asm.SUBPD, x86asm.VSUBPD:
		armOp = "fsub"
	case x86asm.MULPD, x86asm.VMULPD:
		armOp = "fmul"
	case x86asm.DIVPD, x86asm.VDIVPD:
		armOp = "fdiv"
	case x86asm.MAXPD, x86asm.VMAXPD:
		armOp = "fmax"
	case x86asm.MINPD, x86asm.VMINPD:
		armOp = "fmin"
	}

	var dstReg, s1Reg x86asm.Reg
	var src2 x86asm.Arg
	var okDst, okS1 bool
	isVex := false

	if len(inst.Inst.Args) >= 3 && inst.Inst.Args[2] != nil {
		isVex = true
		dstReg, okDst = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg, okS1 = inst.Inst.Args[1].(x86asm.Reg)
		src2 = inst.Inst.Args[2]
	} else {
		dstReg, okDst = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg = dstReg
		okS1 = okDst
		src2 = inst.Inst.Args[1]
	}

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported avx double alu dst/src1")...)
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
		lines = append(lines, l.emitUnsupported(inst, "unsupported avx double alu src2")...)
		return lines, nil
	}

	if armOp == "fmax" || armOp == "fmin" {
		lines = append(lines, x86MinMax(armOp == "fmax", ".2d", dLowV, s1LowV, s2LowV)...)
		if isYmm {
			lines = append(lines, x86MinMax(armOp == "fmax", ".2d", dHighV, s1HighV, s2HighV)...)
		} else if isVex {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		return lines, nil
	}
	lines = append(lines, fmt.Sprintf("    %s %s.2d, %s.2d, %s.2d", armOp, dLowV, s1LowV, s2LowV))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    %s %s.2d, %s.2d, %s.2d", armOp, dHighV, s1HighV, s2HighV))
	} else if isVex {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftVhadd handles VHADDPD and VHADDPS.
func (l *Arm64Lifter) liftVhadd(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg)
	src2 := inst.Inst.Args[2]

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1D := getVectorDesc(s1Reg)
	if !okDst || !okS1 || !okD || !okS1D {
		lines = append(lines, l.emitUnsupported(inst, "unsupported vhadd dst/src1")...)
		return lines, nil
	}

	lane := ".2d"
	if op == x86asm.VHADDPS {
		lane = ".4s"
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
		lines = append(lines, l.emitUnsupported(inst, "unsupported vhadd src2")...)
		return lines, nil
	}

	lines = append(lines, fmt.Sprintf("    faddp %s%s, %s%s, %s%s", dLowV, lane, s1LowV, lane, s2LowV, lane))
	if isYmm {
		lines = append(lines, fmt.Sprintf("    faddp %s%s, %s%s, %s%s", dHighV, lane, s1HighV, lane, s2HighV, lane))
	} else {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftSqrt handles scalar and packed square root operations.
func (l *Arm64Lifter) liftSqrt(inst disasm.Instruction, isDouble, isScalar, isRsqrt bool, nextPC uint64, lines []string) ([]string, error) {
	var dstReg, src1Reg x86asm.Reg
	var src2 x86asm.Arg
	isVex := false

	if isScalar {
		if len(inst.Inst.Args) >= 3 && inst.Inst.Args[2] != nil {
			isVex = true
			dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
			src1Reg, _ = inst.Inst.Args[1].(x86asm.Reg)
			src2 = inst.Inst.Args[2]
		} else {
			dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
			src1Reg = dstReg
			src2 = inst.Inst.Args[1]
		}

		_, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
		_, _, _, s1LowV, _, _, _, okS1 := getVectorDesc(src1Reg)
		if !okD || !okS1 {
			lines = append(lines, l.emitUnsupported(inst, "sqrt scalar invalid dst")...)
			return lines, nil
		}

		// Read src2 into v16 scalar
		if mem, okMem := src2.(x86asm.Mem); okMem {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			if isDouble {
				lines = append(lines, "    ldr d16, [x28, x16]")
			} else {
				lines = append(lines, "    ldr s16, [x28, x16]")
			}
		} else if srcReg, okSrc := src2.(x86asm.Reg); okSrc {
			if _, _, _, s2LowV, _, _, _, okS2 := getVectorDesc(srcReg); okS2 {
				if isDouble {
					lines = append(lines, fmt.Sprintf("    mov v16.d[0], %s.d[0]", s2LowV))
				} else {
					lines = append(lines, fmt.Sprintf("    mov v16.s[0], %s.s[0]", s2LowV))
				}
			} else {
				lines = append(lines, l.emitUnsupported(inst, "sqrt invalid src2 reg")...)
				return lines, nil
			}
		}

		// Perform sqrt
		if isDouble {
			lines = append(lines, "    fsqrt d16, d16")
		} else if isRsqrt {
			lines = append(lines, x86Recip(true, "v16", "v16")...)
		} else {
			lines = append(lines, "    fsqrt s16, s16")
		}

		// Copy s1 to dst if different
		if dLowV != s1LowV {
			lines = append(lines, fmt.Sprintf("    mov %s.16b, %s.16b", dLowV, s1LowV))
		}

		if isDouble {
			lines = append(lines, fmt.Sprintf("    mov %s.d[0], v16.d[0]", dLowV))
		} else {
			lines = append(lines, fmt.Sprintf("    mov %s.s[0], v16.s[0]", dLowV))
		}

		if isVex {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
		return lines, nil
	}

	// Packed SQRT
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	src2 = inst.Inst.Args[1]
	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "packed sqrt invalid dst")...)
		return lines, nil
	}

	var s2Low, s2High string
	if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q16, [x28, x16]")
		s2Low = "v16"
		if isYmm {
			lines = append(lines, "    add x17, x16, #16")
			lines = append(lines, "    ldr q17, [x28, x17]")
			s2High = "v17"
		}
	} else if srcReg, okSrc := src2.(x86asm.Reg); okSrc {
		if _, _, _, sLowV, sHighV, _, _, okS := getVectorDesc(srcReg); okS {
			s2Low = sLowV
			s2High = sHighV
		} else {
			lines = append(lines, l.emitUnsupported(inst, "packed sqrt invalid src reg")...)
			return lines, nil
		}
	}

	op := "fsqrt"
	if isRsqrt {
		op = "frsqrte"
	}

	suffix := ".4s"
	if isDouble {
		suffix = ".2d"
	}

	if isRsqrt {
		lines = append(lines, x86Recip(true, dLowV, s2Low)...)
	} else {
		lines = append(lines, fmt.Sprintf("    %s %s%s, %s%s", op, dLowV, suffix, s2Low, suffix))
	}
	if isYmm {
		if isRsqrt {
			lines = append(lines, x86Recip(true, dHighV, s2High)...)
		} else {
			lines = append(lines, fmt.Sprintf("    %s %s%s, %s%s", op, dHighV, suffix, s2High, suffix))
		}
	} else {
		op := inst.Inst.Op
		if op == x86asm.VSQRTPS || op == x86asm.VSQRTPD || op == x86asm.VRSQRTPS {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
	}

	return lines, nil
}

// packedFpToInt32X86 converts four floats (s, a v register) to int32 in d with x86 CVT(T)PS2DQ
// semantics: NaN and out-of-range lanes give 0x80000000 (ARM FCVTZS saturates or gives 0).
// The non-truncating form rounds by the current mode (FRINTX). Scratch: v29, v30, v31.
func packedFpToInt32X86(d, s string, trunc bool) []string {
	out := []string{fmt.Sprintf("    fcmeq v30.4s, %s.4s, %s.4s", s, s)} // ordered lanes, before d is written
	src := s
	if !trunc {
		out = append(out, fmt.Sprintf("    frintx v31.4s, %s.4s", s))
		src = "v31"
	}
	return append(out,
		fmt.Sprintf("    fcvtzs %s.4s, %s.4s", d, src),
		"    mvni v31.4s, #0x80, lsl #24", // 0x7fffffff
		fmt.Sprintf("    cmeq v29.4s, %s.4s, v31.4s", d),
		"    orn v29.16b, v29.16b, v30.16b", // saturated high or NaN
		"    movi v31.4s, #0x80, lsl #24",   // 0x80000000
		fmt.Sprintf("    bit %s.16b, v31.16b, v29.16b", d))
}

// cvtps2phRounding returns the code around the FCVTNs of VCVTPS2PH that applies its imm8
// rounding control: bits 1:0 (nearest, down, up, toward zero) unless bit 2 selects MXCSR.RC,
// which FPCR already holds. FPCR is only written when its mode differs; the old value stays in
// x5. NZCV is not touched. Scratch: x5-x7.
func cvtps2phRounding(inst disasm.Instruction) (begin, end []string) {
	imm, ok := inst.Inst.Args[2].(x86asm.Imm)
	if !ok || imm&4 != 0 {
		return nil, nil
	}
	rm := [4]int{0, 2, 1, 3}[imm&3] // FPCR RMode: nearest, up, down, toward zero
	begin = []string{"    mrs  x5, fpcr", "    ubfx x6, x5, #22, #2"}
	if rm != 0 {
		begin = append(begin, fmt.Sprintf("    eor  x6, x6, #%d", rm))
	}
	begin = append(begin,
		"    cbz  x6, 1f", // already in that mode
		fmt.Sprintf("    mov  x6, #%d", rm),
		"    mov  x7, x5",
		"    bfi  x7, x6, #22, #2",
		"    msr  fpcr, x7",
		"    mov  x6, #1",
		"1:")
	end = []string{"    cbz  x6, 2f", "    msr  fpcr, x5", "2:"}
	return begin, end
}

// liftCvtdq handles conversions between float, double, int, and half-precision.
func (l *Arm64Lifter) liftCvtdq(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]
	if len(inst.Inst.Args) >= 3 && inst.Inst.Args[2] != nil && op != x86asm.VCVTPS2PH {
		src = inst.Inst.Args[2]
	}

	isAvx := (op == x86asm.VCVTDQ2PS || op == x86asm.VCVTTPS2DQ || op == x86asm.VCVTPS2DQ || op == x86asm.VCVTPD2PS ||
		op == x86asm.VCVTPS2PD || op == x86asm.VCVTTPD2DQ || op == x86asm.VCVTDQ2PD || op == x86asm.VCVTPH2PS || op == x86asm.VCVTPS2PH)

	// Special handling for VCVTPS2PH mem destination
	if op == x86asm.VCVTPS2PH {
		if memDst, okMem := dst.(x86asm.Mem); okMem {
			sReg, okSrc := src.(x86asm.Reg)
			if isY, _, _, sL, sH, _, _, okS := getVectorDesc(sReg); okSrc && okS {
				lines = append(lines, computeEffAddr(memDst, nextPC, "x16")...)
				begin, end := cvtps2phRounding(inst)
				lines = append(lines, begin...)
				if isY {
					lines = append(lines,
						fmt.Sprintf("    fcvtn v28.4h, %s.4s", sL),
						fmt.Sprintf("    fcvtn2 v28.8h, %s.4s", sH),
					)
					lines = append(lines, end...)
					lines = append(lines, "    str q28, [x28, x16]")
				} else {
					lines = append(lines, fmt.Sprintf("    fcvtn v28.4h, %s.4s", sL))
					lines = append(lines, end...)
					lines = append(lines, "    str d28, [x28, x16]")
				}
				return lines, nil
			}
		}
	}

	dstReg, okDst := dst.(x86asm.Reg)
	dstYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported cvtdq dst")...)
		return lines, nil
	}

	srcYmm := false
	sLowV := "v30"
	sHighV := "v31"
	if sReg, okSrc := src.(x86asm.Reg); okSrc {
		if ymm, _, _, rLowV, rHighV, _, _, okS := getVectorDesc(sReg); okS {
			srcYmm = ymm
			sLowV = rLowV
			sHighV = rHighV
		}
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		mem64 := (!dstYmm && (op == x86asm.CVTPS2PD || op == x86asm.VCVTPS2PD ||
			op == x86asm.CVTDQ2PD || op == x86asm.VCVTDQ2PD || op == x86asm.VCVTPH2PS))
		if mem64 {
			lines = append(lines, "    ldr d30, [x28, x16]")
		} else {
			lines = append(lines, "    ldr q30, [x28, x16]")
			if op == x86asm.CVTTPD2DQ || op == x86asm.VCVTTPD2DQ || op == x86asm.CVTPD2PS || op == x86asm.VCVTPD2PS {
				// If AVX instruction operating on YMM memory (256-bit to 128-bit)
				// For memory operands, x86asm doesn't have YMM register for src, but we check if dst is YMM or prefix is 256-bit
				// But CVTTPD2DQ destination is XMM! We check if operand has 256-bit attribute or inst is 256-bit
			}
		}
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported cvtdq src")...)
		return lines, nil
	}

	isYmm := dstYmm || srcYmm

	switch op {
	case x86asm.CVTDQ2PS, x86asm.VCVTDQ2PS:
		lines = append(lines, fmt.Sprintf("    scvtf %s.4s, %s.4s", dLowV, sLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    scvtf %s.4s, %s.4s", dHighV, sHighV))
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}

	case x86asm.CVTTPS2DQ, x86asm.VCVTTPS2DQ, x86asm.CVTPS2DQ, x86asm.VCVTPS2DQ:
		trunc := op == x86asm.CVTTPS2DQ || op == x86asm.VCVTTPS2DQ
		lines = append(lines, packedFpToInt32X86(dLowV, sLowV, trunc)...)
		if isYmm {
			lines = append(lines, packedFpToInt32X86(dHighV, sHighV, trunc)...)
		} else if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}

	case x86asm.CVTPD2PS, x86asm.VCVTPD2PS:
		lines = append(lines, fmt.Sprintf("    fcvtn %s.2s, %s.2d", dLowV, sLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    fcvtn2 %s.4s, %s.2d", dLowV, sHighV))
		} else {
			lines = append(lines, fmt.Sprintf("    mov %s.d[1], xzr", dLowV))
		}
		if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}

	case x86asm.CVTPS2PD, x86asm.VCVTPS2PD:
		// The upper half first: the destination may alias the source.
		if isYmm {
			lines = append(lines, fmt.Sprintf("    fcvtl2 %s.2d, %s.4s", dHighV, sLowV))
		}
		lines = append(lines, fmt.Sprintf("    fcvtl %s.2d, %s.2s", dLowV, sLowV))
		if !isYmm && isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}

	case x86asm.CVTTPD2DQ, x86asm.VCVTTPD2DQ:
		// Per double: truncate to int64 and narrow; a lane that does not fit int32 or is NaN
		// gives 0x80000000 (scalar fix-up per lane, see fpToIntX86).
		lanes := []string{sLowV + ".d[0]", sLowV + ".d[1]"}
		if isYmm {
			lanes = append(lanes, sHighV+".d[0]", sHighV+".d[1]")
		}
		for k, ln := range lanes {
			lines = append(lines, fmt.Sprintf("    mov d29, %s", ln))
			lines = append(lines, fpToIntX86("w3", "d29", true)...)
			lines = append(lines, fmt.Sprintf("    mov v28.s[%d], w3", k))
		}
		if !isYmm {
			lines = append(lines, "    mov v28.d[1], xzr")
		}
		lines = append(lines, fmt.Sprintf("    mov %s.16b, v28.16b", dLowV))
		if isAvx {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}

	case x86asm.CVTDQ2PD, x86asm.VCVTDQ2PD:
		// int32 -> double is exact: sign-extend to int64 and convert (going through float32
		// would round). Both halves are extended before the destination (may alias) is written.
		lines = append(lines, fmt.Sprintf("    sxtl v28.2d, %s.2s", sLowV))
		if isYmm {
			lines = append(lines,
				fmt.Sprintf("    sxtl2 v29.2d, %s.4s", sLowV),
				fmt.Sprintf("    scvtf %s.2d, v28.2d", dLowV),
				fmt.Sprintf("    scvtf %s.2d, v29.2d", dHighV),
			)
		} else {
			lines = append(lines, fmt.Sprintf("    scvtf %s.2d, v28.2d", dLowV))
			if isAvx {
				lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
			}
		}

	case x86asm.VCVTPH2PS:
		// The upper half first: the destination may alias the source.
		if isYmm {
			lines = append(lines, fmt.Sprintf("    fcvtl2 %s.4s, %s.8h", dHighV, sLowV))
		}
		lines = append(lines, fmt.Sprintf("    fcvtl %s.4s, %s.4h", dLowV, sLowV))
		if !isYmm {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}

	case x86asm.VCVTPS2PH:
		begin, end := cvtps2phRounding(inst)
		lines = append(lines, begin...)
		lines = append(lines, fmt.Sprintf("    fcvtn %s.4h, %s.4s", dLowV, sLowV))
		if isYmm {
			lines = append(lines, fmt.Sprintf("    fcvtn2 %s.8h, %s.4s", dLowV, sHighV))
		}
		lines = append(lines, end...)
		if !isYmm {
			lines = append(lines, fmt.Sprintf("    mov %s.d[1], xzr", dLowV))
		}
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}

	return lines, nil
}
