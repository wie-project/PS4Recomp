package lifter

import (
	"fmt"

	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/disasm"
)

// Helper to load context pointer into a GPR (e.g. x16).
func (l *Arm64Lifter) emitLoadCtxPtr(reg string) []string {
	return []string{
		fmt.Sprintf("    add  %s, x27, #0x70", reg),
		fmt.Sprintf("    ldr  %s, [x28, %s]", reg, reg),
	}
}

// mmxOffset returns the byte offset of mmx[idx] within GuestContext.
func mmxOffset(idx int) int {
	return 0x2f0 + idx*8
}

// liftMmxMovdMovq handles MOVD and MOVQ when at least one operand is an MMX register (M0-M7).
func (l *Arm64Lifter) liftMmxMovdMovq(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, bool, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]
	is64 := (inst.Inst.Op == x86asm.MOVQ || inst.Inst.Op == x86asm.VMOVQ)

	dstReg, okDstReg := dst.(x86asm.Reg)
	srcReg, okSrcReg := src.(x86asm.Reg)

	// Case 1: MMX <- MMX
	if okDstReg && isMmx(dstReg) && okSrcReg && isMmx(srcReg) {
		lines = append(lines, l.emitLoadCtxPtr("x5")...)
		lines = append(lines,
			fmt.Sprintf("    ldr x16, [x5, #%d]", mmxOffset(mmxIdx(srcReg))),
			fmt.Sprintf("    str x16, [x5, #%d]", mmxOffset(mmxIdx(dstReg))),
		)
		return lines, true, nil
	}

	// Case 2: MMX <- Mem
	if okDstReg && isMmx(dstReg) {
		if mem, okMem := src.(x86asm.Mem); okMem {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, l.emitLoadCtxPtr("x5")...)
			if is64 {
				lines = append(lines,
					"    ldr x17, [x28, x16]",
					fmt.Sprintf("    str x17, [x5, #%d]", mmxOffset(mmxIdx(dstReg))),
				)
			} else {
				lines = append(lines,
					"    ldr w17, [x28, x16]",
					"    uxtw x17, w17",
					fmt.Sprintf("    str x17, [x5, #%d]", mmxOffset(mmxIdx(dstReg))),
				)
			}
			return lines, true, nil
		}
	}

	// Case 3: Mem <- MMX
	if mem, okMem := dst.(x86asm.Mem); okMem && okSrcReg && isMmx(srcReg) {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, l.emitLoadCtxPtr("x5")...)
		if is64 {
			lines = append(lines,
				fmt.Sprintf("    ldr x17, [x5, #%d]", mmxOffset(mmxIdx(srcReg))),
				"    str x17, [x28, x16]",
			)
		} else {
			lines = append(lines,
				fmt.Sprintf("    ldr w17, [x5, #%d]", mmxOffset(mmxIdx(srcReg))),
				"    str w17, [x28, x16]",
			)
		}
		return lines, true, nil
	}

	// Case 4: MMX <- GPR
	if okDstReg && isMmx(dstReg) && okSrcReg {
		if descSrc, okDesc := arm64RegMap[srcReg]; okDesc {
			lines = append(lines, l.emitLoadCtxPtr("x5")...)
			if is64 {
				lines = append(lines, fmt.Sprintf("    str %s, [x5, #%d]", descSrc.X, mmxOffset(mmxIdx(dstReg))))
			} else {
				lines = append(lines,
					fmt.Sprintf("    uxtw x16, %s", descSrc.W),
					fmt.Sprintf("    str x16, [x5, #%d]", mmxOffset(mmxIdx(dstReg))),
				)
			}
			return lines, true, nil
		}
	}

	// Case 5: GPR <- MMX
	if okDstReg && okSrcReg && isMmx(srcReg) {
		if descDst, okDesc := arm64RegMap[dstReg]; okDesc {
			lines = append(lines, l.emitLoadCtxPtr("x5")...)
			if is64 {
				lines = append(lines, fmt.Sprintf("    ldr %s, [x5, #%d]", descDst.X, mmxOffset(mmxIdx(srcReg))))
			} else {
				lines = append(lines, fmt.Sprintf("    ldr %s, [x5, #%d]", descDst.W, mmxOffset(mmxIdx(srcReg))))
			}
			return lines, true, nil
		}
	}

	// Case 6: MMX <- XMM
	if okDstReg && isMmx(dstReg) && okSrcReg {
		if _, _, _, _, _, _, sLowD, okS := getVectorDesc(srcReg); okS {
			lines = append(lines, l.emitLoadCtxPtr("x5")...)
			lines = append(lines,
				fmt.Sprintf("    fmov x16, %s", sLowD),
				fmt.Sprintf("    str x16, [x5, #%d]", mmxOffset(mmxIdx(dstReg))),
			)
			return lines, true, nil
		}
	}

	// Case 7: XMM <- MMX
	if okDstReg && okSrcReg && isMmx(srcReg) {
		if _, _, _, dLowV, _, _, dLowD, okD := getVectorDesc(dstReg); okD {
			lines = append(lines, l.emitLoadCtxPtr("x5")...)
			lines = append(lines,
				fmt.Sprintf("    ldr %s, [x5, #%d]", dLowD, mmxOffset(mmxIdx(srcReg))),
				fmt.Sprintf("    mov %s.d[1], xzr", dLowV),
			)
			return lines, true, nil
		}
	}

	return lines, false, nil
}

// liftMmxLogic handles PXOR, PAND, POR, PANDN when dst is an MMX register.
func (l *Arm64Lifter) liftMmxLogic(inst disasm.Instruction, armOp string, nextPC uint64, lines []string) ([]string, bool, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	if !okDst || !isMmx(dstReg) {
		return lines, false, nil
	}

	src := inst.Inst.Args[1]
	srcReg, okSrc := src.(x86asm.Reg)

	// Fast path: PXOR M, M -> zero out
	if okSrc && isMmx(srcReg) && dstReg == srcReg && armOp == "eor" {
		lines = append(lines, l.emitLoadCtxPtr("x5")...)
		lines = append(lines, fmt.Sprintf("    str xzr, [x5, #%d]", mmxOffset(mmxIdx(dstReg))))
		return lines, true, nil
	}

	lines = append(lines, l.emitLoadCtxPtr("x5")...)
	lines = append(lines, fmt.Sprintf("    ldr d16, [x5, #%d]", mmxOffset(mmxIdx(dstReg))))

	if okSrc && isMmx(srcReg) {
		lines = append(lines, fmt.Sprintf("    ldr d17, [x5, #%d]", mmxOffset(mmxIdx(srcReg))))
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr d17, [x28, x16]")
	} else {
		return lines, false, nil
	}

	switch armOp {
	case "eor":
		lines = append(lines, "    eor v16.8b, v16.8b, v17.8b")
	case "and":
		lines = append(lines, "    and v16.8b, v16.8b, v17.8b")
	case "orr":
		lines = append(lines, "    orr v16.8b, v16.8b, v17.8b")
	case "bic":
		// PANDN dst, src: dst = (~dst) & src.
		// ARM64 bic Rd, Rn, Rm computes Rd = Rn & ~Rm. So bic v16, v17, v16 computes src & ~dst.
		lines = append(lines, "    bic v16.8b, v17.8b, v16.8b")
	}

	lines = append(lines, fmt.Sprintf("    str d16, [x5, #%d]", mmxOffset(mmxIdx(dstReg))))
	return lines, true, nil
}

// liftMmxBinary handles packed SIMD binary operations on MMX registers.
func (l *Arm64Lifter) liftMmxBinary(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, bool, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	if !okDst || !isMmx(dstReg) {
		return lines, false, nil
	}

	src := inst.Inst.Args[1]
	lines = append(lines, l.emitLoadCtxPtr("x5")...)
	lines = append(lines, fmt.Sprintf("    ldr d16, [x5, #%d]", mmxOffset(mmxIdx(dstReg))))

	if srcReg, okSrc := src.(x86asm.Reg); okSrc && isMmx(srcReg) {
		lines = append(lines, fmt.Sprintf("    ldr d17, [x5, #%d]", mmxOffset(mmxIdx(srcReg))))
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr d17, [x28, x16]")
	} else {
		return lines, false, nil
	}

	switch inst.Inst.Op {
	case x86asm.PADDB:
		lines = append(lines, "    add v16.8b, v16.8b, v17.8b")
	case x86asm.PADDW:
		lines = append(lines, "    add v16.4h, v16.4h, v17.4h")
	case x86asm.PADDD:
		lines = append(lines, "    add v16.2s, v16.2s, v17.2s")
	case x86asm.PADDQ:
		lines = append(lines, "    add d16, d16, d17")
	case x86asm.PADDSB:
		lines = append(lines, "    sqadd v16.8b, v16.8b, v17.8b")
	case x86asm.PADDSW:
		lines = append(lines, "    sqadd v16.4h, v16.4h, v17.4h")
	case x86asm.PADDUSB:
		lines = append(lines, "    uqadd v16.8b, v16.8b, v17.8b")
	case x86asm.PADDUSW:
		lines = append(lines, "    uqadd v16.4h, v16.4h, v17.4h")
	case x86asm.PSUBB:
		lines = append(lines, "    sub v16.8b, v16.8b, v17.8b")
	case x86asm.PSUBW:
		lines = append(lines, "    sub v16.4h, v16.4h, v17.4h")
	case x86asm.PSUBD:
		lines = append(lines, "    sub v16.2s, v16.2s, v17.2s")
	case x86asm.PSUBQ:
		lines = append(lines, "    sub d16, d16, d17")
	case x86asm.PSUBSB:
		lines = append(lines, "    sqsub v16.8b, v16.8b, v17.8b")
	case x86asm.PSUBSW:
		lines = append(lines, "    sqsub v16.4h, v16.4h, v17.4h")
	case x86asm.PSUBUSB:
		lines = append(lines, "    uqsub v16.8b, v16.8b, v17.8b")
	case x86asm.PSUBUSW:
		lines = append(lines, "    uqsub v16.4h, v16.4h, v17.4h")
	case x86asm.PMULLW:
		lines = append(lines, "    mul v16.4h, v16.4h, v17.4h")
	case x86asm.PMULHW:
		lines = append(lines,
			"    smull v18.4s, v16.4h, v17.4h",
			"    uzp2 v16.4h, v18.4h, v18.4h",
		)
	case x86asm.PMADDUBSW:
		lines = append(lines,
			"    uxtl v20.8h, v16.8b",
			"    sxtl v21.8h, v17.8b",
			"    mul  v22.8h, v20.8h, v21.8h",
			"    trn1 v23.4h, v22.4h, v22.4h",
			"    trn2 v24.4h, v22.4h, v22.4h",
			"    sqadd v16.4h, v23.4h, v24.4h",
		)
	case x86asm.PMULHUW:
		lines = append(lines,
			"    umull v18.4s, v16.4h, v17.4h",
			"    uzp2 v16.4h, v18.4h, v18.4h",
		)
	case x86asm.PMAXSW:
		lines = append(lines, "    smax v16.4h, v16.4h, v17.4h")
	case x86asm.PMAXUB:
		lines = append(lines, "    umax v16.8b, v16.8b, v17.8b")
	case x86asm.PMINSW:
		lines = append(lines, "    smin v16.4h, v16.4h, v17.4h")
	case x86asm.PMINUB:
		lines = append(lines, "    umin v16.8b, v16.8b, v17.8b")
	case x86asm.PCMPEQB:
		lines = append(lines, "    cmeq v16.8b, v16.8b, v17.8b")
	case x86asm.PCMPEQW:
		lines = append(lines, "    cmeq v16.4h, v16.4h, v17.4h")
	case x86asm.PCMPEQD:
		lines = append(lines, "    cmeq v16.2s, v16.2s, v17.2s")
	case x86asm.PCMPGTB:
		lines = append(lines, "    cmgt v16.8b, v16.8b, v17.8b")
	case x86asm.PCMPGTW:
		lines = append(lines, "    cmgt v16.4h, v16.4h, v17.4h")
	case x86asm.PCMPGTD:
		lines = append(lines, "    cmgt v16.2s, v16.2s, v17.2s")
	case x86asm.PAVGB:
		lines = append(lines, "    urhadd v16.8b, v16.8b, v17.8b")
	case x86asm.PAVGW:
		lines = append(lines, "    urhadd v16.4h, v16.4h, v17.4h")
	default:
		return lines, false, nil
	}

	lines = append(lines, fmt.Sprintf("    str d16, [x5, #%d]", mmxOffset(mmxIdx(dstReg))))
	return lines, true, nil
}

// liftMmxShift handles packed shifts on MMX registers.
func (l *Arm64Lifter) liftMmxShift(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, bool, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	if !okDst || !isMmx(dstReg) {
		return lines, false, nil
	}

	lines = append(lines, l.emitLoadCtxPtr("x5")...)
	lines = append(lines, fmt.Sprintf("    ldr d16, [x5, #%d]", mmxOffset(mmxIdx(dstReg))))

	src := inst.Inst.Args[1]
	if imm, okImm := src.(x86asm.Imm); okImm {
		shift := int64(imm)
		switch inst.Inst.Op {
		case x86asm.PSLLW:
			if shift >= 16 {
				lines = append(lines, "    movi v16.8b, #0")
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    shl v16.4h, v16.4h, #%d", shift))
			}
		case x86asm.PSLLD:
			if shift >= 32 {
				lines = append(lines, "    movi v16.8b, #0")
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    shl v16.2s, v16.2s, #%d", shift))
			}
		case x86asm.PSLLQ:
			if shift >= 64 {
				lines = append(lines, "    movi v16.8b, #0")
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    shl d16, d16, #%d", shift))
			}
		case x86asm.PSRLW:
			if shift >= 16 {
				lines = append(lines, "    movi v16.8b, #0")
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    ushr v16.4h, v16.4h, #%d", shift))
			}
		case x86asm.PSRLD:
			if shift >= 32 {
				lines = append(lines, "    movi v16.8b, #0")
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    ushr v16.2s, v16.2s, #%d", shift))
			}
		case x86asm.PSRLQ:
			if shift >= 64 {
				lines = append(lines, "    movi v16.8b, #0")
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    ushr d16, d16, #%d", shift))
			}
		case x86asm.PSRAW:
			if shift >= 16 {
				lines = append(lines, "    sshr v16.4h, v16.4h, #15")
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    sshr v16.4h, v16.4h, #%d", shift))
			}
		case x86asm.PSRAD:
			if shift >= 32 {
				lines = append(lines, "    sshr v16.2s, v16.2s, #31")
			} else if shift > 0 {
				lines = append(lines, fmt.Sprintf("    sshr v16.2s, v16.2s, #%d", shift))
			}
		}
	} else {
		// Shift by register / memory count
		if sReg, okSR := src.(x86asm.Reg); okSR && isMmx(sReg) {
			lines = append(lines, fmt.Sprintf("    ldr x16, [x5, #%d]", mmxOffset(mmxIdx(sReg))))
		} else if sReg, okSR := src.(x86asm.Reg); okSR {
			if _, _, _, _, _, _, sLowD, okS := getVectorDesc(sReg); okS {
				lines = append(lines, fmt.Sprintf("    fmov x16, %s", sLowD))
			}
		} else if mem, okMem := src.(x86asm.Mem); okMem {
			lines = append(lines, computeEffAddr(mem, nextPC, "x17")...)
			lines = append(lines, "    ldr x16, [x28, x17]")
		}
		// Clamp and shift
		lines = append(lines,
			"    mrs x17, nzcv",
			"    str x17, [sp, #-16]!",
		)
		switch inst.Inst.Op {
		case x86asm.PSLLW:
			lines = append(lines,
				"    cmp x16, #16",
				"    csel x16, x16, xzr, lo",
				"    fmov d17, x16",
				"    dup v17.4h, v17.h[0]",
				"    ushl v16.4h, v16.4h, v17.4h",
			)
		case x86asm.PSLLD:
			lines = append(lines,
				"    cmp x16, #32",
				"    csel x16, x16, xzr, lo",
				"    fmov d17, x16",
				"    dup v17.2s, v17.s[0]",
				"    ushl v16.2s, v16.2s, v17.2s",
			)
		case x86asm.PSLLQ:
			lines = append(lines,
				"    cmp x16, #64",
				"    csel x16, x16, xzr, lo",
				"    fmov d17, x16",
				"    ushl d16, d16, d17",
			)
		case x86asm.PSRLW:
			lines = append(lines,
				"    cmp x16, #16",
				"    csel x16, x16, xzr, lo",
				"    neg x16, x16",
				"    fmov d17, x16",
				"    dup v17.4h, v17.h[0]",
				"    ushl v16.4h, v16.4h, v17.4h",
			)
		case x86asm.PSRLD:
			lines = append(lines,
				"    cmp x16, #32",
				"    csel x16, x16, xzr, lo",
				"    neg x16, x16",
				"    fmov d17, x16",
				"    dup v17.2s, v17.s[0]",
				"    ushl v16.2s, v16.2s, v17.2s",
			)
		case x86asm.PSRLQ:
			lines = append(lines,
				"    cmp x16, #64",
				"    csel x16, x16, xzr, lo",
				"    neg x16, x16",
				"    fmov d17, x16",
				"    ushl d16, d16, d17",
			)
		case x86asm.PSRAW:
			lines = append(lines,
				"    mov x17, #15",
				"    cmp x16, #15",
				"    csel x16, x16, x17, lo",
				"    neg x16, x16",
				"    fmov d17, x16",
				"    dup v17.4h, v17.h[0]",
				"    sshl v16.4h, v16.4h, v17.4h",
			)
		case x86asm.PSRAD:
			lines = append(lines,
				"    mov x17, #31",
				"    cmp x16, #31",
				"    csel x16, x16, x17, lo",
				"    neg x16, x16",
				"    fmov d17, x16",
				"    dup v17.2s, v17.s[0]",
				"    sshl v16.2s, v16.2s, v17.2s",
			)
		}
		lines = append(lines,
			"    ldr x17, [sp], #16",
			"    msr nzcv, x17",
		)
	}

	lines = append(lines, fmt.Sprintf("    str d16, [x5, #%d]", mmxOffset(mmxIdx(dstReg))))
	return lines, true, nil
}

// liftMmxUnpck handles PUNPCK* when dst is an MMX register.
func (l *Arm64Lifter) liftMmxUnpck(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, bool, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	if !okDst || !isMmx(dstReg) {
		return lines, false, nil
	}

	src2 := inst.Inst.Args[1]
	lines = append(lines, l.emitLoadCtxPtr("x5")...)
	lines = append(lines, fmt.Sprintf("    ldr d16, [x5, #%d]", mmxOffset(mmxIdx(dstReg))))

	if sReg, okS := src2.(x86asm.Reg); okS && isMmx(sReg) {
		lines = append(lines, fmt.Sprintf("    ldr d17, [x5, #%d]", mmxOffset(mmxIdx(sReg))))
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr d17, [x28, x16]")
	} else {
		return lines, false, nil
	}

	switch inst.Inst.Op {
	case x86asm.PUNPCKLBW:
		lines = append(lines, "    zip1 v16.8b, v16.8b, v17.8b")
	case x86asm.PUNPCKLWD:
		lines = append(lines, "    zip1 v16.4h, v16.4h, v17.4h")
	case x86asm.PUNPCKLDQ:
		lines = append(lines, "    zip1 v16.2s, v16.2s, v17.2s")
	case x86asm.PUNPCKHBW:
		lines = append(lines, "    zip2 v16.8b, v16.8b, v17.8b")
	case x86asm.PUNPCKHWD:
		lines = append(lines, "    zip2 v16.4h, v16.4h, v17.4h")
	case x86asm.PUNPCKHDQ:
		lines = append(lines, "    zip2 v16.2s, v16.2s, v17.2s")
	default:
		return lines, false, nil
	}

	lines = append(lines, fmt.Sprintf("    str d16, [x5, #%d]", mmxOffset(mmxIdx(dstReg))))
	return lines, true, nil
}

// liftMmxPack handles PACKSSWB, PACKSSDW, PACKUSWB when dst is an MMX register.
func (l *Arm64Lifter) liftMmxPack(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, bool, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	if !okDst || !isMmx(dstReg) {
		return lines, false, nil
	}

	src := inst.Inst.Args[1]
	lines = append(lines, l.emitLoadCtxPtr("x5")...)
	lines = append(lines, fmt.Sprintf("    ldr d16, [x5, #%d]", mmxOffset(mmxIdx(dstReg))))

	if sReg, okS := src.(x86asm.Reg); okS && isMmx(sReg) {
		lines = append(lines, fmt.Sprintf("    ldr d17, [x5, #%d]", mmxOffset(mmxIdx(sReg))))
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr d17, [x28, x16]")
	} else {
		return lines, false, nil
	}

	switch inst.Inst.Op {
	case x86asm.PACKSSWB:
		lines = append(lines,
			"    mov v18.d[0], v16.d[0]",
			"    mov v18.d[1], v17.d[0]",
			"    sqxtn v16.8b, v18.8h",
		)
	case x86asm.PACKUSWB:
		lines = append(lines,
			"    mov v18.d[0], v16.d[0]",
			"    mov v18.d[1], v17.d[0]",
			"    sqxtun v16.8b, v18.8h",
		)
	case x86asm.PACKSSDW:
		lines = append(lines,
			"    mov v18.d[0], v16.d[0]",
			"    mov v18.d[1], v17.d[0]",
			"    sqxtn v16.4h, v18.4s",
		)
	default:
		return lines, false, nil
	}

	lines = append(lines, fmt.Sprintf("    str d16, [x5, #%d]", mmxOffset(mmxIdx(dstReg))))
	return lines, true, nil
}
