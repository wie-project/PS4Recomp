package lifter

import (
	"fmt"
	"strings"

	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/disasm"
)

// liftMxcsr handles (V)LDMXCSR and (V)STMXCSR.
func (l *Arm64Lifter) liftMxcsr(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	mem, okMem := inst.Inst.Args[0].(x86asm.Mem)
	if !okMem {
		lines = append(lines, l.emitUnsupported(inst, "mxcsr arg not memory")...)
		return lines, nil
	}
	// GuestContext.mxcsr is at 0x2e8 (checked by the lifter-difftest-asm harness).
	isStore := (inst.Inst.Op == x86asm.STMXCSR || inst.Inst.Op == x86asm.VSTMXCSR)
	if isStore {
		lines = append(lines, l.emitLoadCtxPtr("x17")...)
		lines = append(lines, "    ldr w0, [x17, #0x2e8]")
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    str w0, [x28, x16]")
	} else {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr w0, [x28, x16]")
		lines = append(lines, l.emitLoadCtxPtr("x17")...)
		lines = append(lines, "    str w0, [x17, #0x2e8]")
		lines = append(lines, mxcsrToFpcr("w0")...)
	}
	return lines, nil
}

// mxcsrToFpcr applies the MXCSR value in register w (rounding mode and flush-to-zero) to FPCR.
// x86 RC (bits 14:13: 00 nearest, 01 down, 10 up, 11 zero) is ARM RMode (bits 23:22: 00 RN,
// 01 RP, 10 RM, 11 RZ) with the two bits swapped; MXCSR.FZ (bit 15) is FPCR.FZ (bit 24).
// Without FEAT_AFP (Apple M1) FPCR.FZ also flushes denormal inputs, so a guest that sets FTZ
// alone gets DAZ as well, and MXCSR.DAZ (bit 6) has no separate effect. TLOU and Uncharted 4
// set FTZ and DAZ together, which this matches; inFAMOUS sets FTZ alone, where results are
// already flushed and only a denormal loaded from memory differs.
// Scratch: x1, x2.
func mxcsrToFpcr(w string) []string {
	return []string{
		"    mrs x1, fpcr",
		fmt.Sprintf("    ubfx w2, %s, #14, #1", w),
		"    bfi x1, x2, #22, #1",
		fmt.Sprintf("    ubfx w2, %s, #13, #1", w),
		"    bfi x1, x2, #23, #1",
		fmt.Sprintf("    ubfx w2, %s, #15, #1", w),
		"    bfi x1, x2, #24, #1",
		"    msr fpcr, x1",
	}
}

// liftMaskmov handles VMASKMOVPS and VMASKMOVPD.
func (l *Arm64Lifter) liftMaskmov(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isDouble := (op == x86asm.VMASKMOVPD)
	_, isStore := inst.Inst.Args[0].(x86asm.Mem)

	var mem x86asm.Mem
	var maskReg, dataReg x86asm.Reg

	if isStore {
		mem, _ = inst.Inst.Args[0].(x86asm.Mem)
		maskReg, _ = inst.Inst.Args[1].(x86asm.Reg)
		dataReg, _ = inst.Inst.Args[2].(x86asm.Reg)
	} else {
		dataReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		maskReg, _ = inst.Inst.Args[1].(x86asm.Reg)
		mem, _ = inst.Inst.Args[2].(x86asm.Mem)
	}

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dataReg)
	_, _, _, mLowV, mHighV, _, _, okM := getVectorDesc(maskReg)
	if !okD || !okM {
		lines = append(lines, l.emitUnsupported(inst, "vmaskmov invalid regs")...)
		return lines, nil
	}

	lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
	lines = append(lines, "    add  x16, x28, x16")

	if isStore {
		// Store data elements to memory where mask sign bit is 1
		emitStoreLanes := func(dataV, maskV string, baseOffset int) {
			numLanes := 4
			laneSize := 4
			elemStr := "s"
			regStr := "w5"
			bit := 31
			movOp := "smov"
			if isDouble {
				numLanes = 2
				laneSize = 8
				elemStr = "d"
				regStr = "x5"
				bit = 63
				movOp = "mov"
			}
			for i := 0; i < numLanes; i++ {
				lblSkip := fmt.Sprintf("L_maskmov_s_skip_%x_%d", inst.Address, baseOffset+i)
				lines = append(lines,
					fmt.Sprintf("    %s  x17, %s.%s[%d]", movOp, maskV, elemStr, i),
					fmt.Sprintf("    tbz  x17, #%d, %s", bit, lblSkip),
					fmt.Sprintf("    mov  %s, %s.%s[%d]", regStr, dataV, elemStr, i),
					fmt.Sprintf("    str  %s, [x16, #%d]", regStr, (baseOffset+i)*laneSize),
					fmt.Sprintf("%s:", lblSkip),
				)
			}
		}
		emitStoreLanes(dLowV, mLowV, 0)
		if isYmm {
			if isDouble {
				emitStoreLanes(dHighV, mHighV, 2)
			} else {
				emitStoreLanes(dHighV, mHighV, 4)
			}
		}
	} else {
		// Load data elements from memory where mask sign bit is 1, else 0
		emitLoadLanes := func(dataV, maskV string, baseOffset int) {
			numLanes := 4
			laneSize := 4
			elemStr := "s"
			regStr := "w5"
			zeroStr := "wzr"
			bit := 31
			movOp := "smov"
			if isDouble {
				numLanes = 2
				laneSize = 8
				elemStr = "d"
				regStr = "x5"
				zeroStr = "xzr"
				bit = 63
				movOp = "mov"
			}
			for i := 0; i < numLanes; i++ {
				lblZero := fmt.Sprintf("L_maskmov_l_zero_%x_%d", inst.Address, baseOffset+i)
				lblDone := fmt.Sprintf("L_maskmov_l_done_%x_%d", inst.Address, baseOffset+i)
				lines = append(lines,
					fmt.Sprintf("    %s  x17, %s.%s[%d]", movOp, maskV, elemStr, i),
					fmt.Sprintf("    tbz  x17, #%d, %s", bit, lblZero),
					fmt.Sprintf("    ldr  %s, [x16, #%d]", regStr, (baseOffset+i)*laneSize),
					fmt.Sprintf("    mov  %s.%s[%d], %s", dataV, elemStr, i, regStr),
					fmt.Sprintf("    b    %s", lblDone),
					fmt.Sprintf("%s:", lblZero),
					fmt.Sprintf("    mov  %s.%s[%d], %s", dataV, elemStr, i, zeroStr),
					fmt.Sprintf("%s:", lblDone),
				)
			}
		}
		emitLoadLanes(dLowV, mLowV, 0)
		if isYmm {
			if isDouble {
				emitLoadLanes(dHighV, mHighV, 2)
			} else {
				emitLoadLanes(dHighV, mHighV, 4)
			}
		} else {
			lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
		}
	}
	return lines, nil
}

// liftPmaddupsw handles PMADDUBSW and VPMADDUBSW.
func (l *Arm64Lifter) liftPmaddupsw(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	lines, handled, err := l.liftMmxBinary(inst, nextPC, lines)
	if err != nil {
		return lines, err
	}
	if handled {
		return lines, nil
	}

	op := inst.Inst.Op
	isAvx := (op == x86asm.VPMADDUBSW)

	var dstReg, s1Reg x86asm.Reg
	var src2 x86asm.Arg
	if isAvx && len(inst.Inst.Args) >= 3 && inst.Inst.Args[2] != nil {
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg, _ = inst.Inst.Args[1].(x86asm.Reg)
		src2 = inst.Inst.Args[2]
	} else {
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg = dstReg
		src2 = inst.Inst.Args[1]
	}

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1 := getVectorDesc(s1Reg)
	if !okD || !okS1 {
		lines = append(lines, l.emitUnsupported(inst, "pmaddubsw invalid dst/s1")...)
		return lines, nil
	}

	s2Low := "v30"
	s2High := "v31"
	if s2Reg, okR := src2.(x86asm.Reg); okR {
		if _, _, _, rL, rH, _, _, okR2 := getVectorDesc(s2Reg); okR2 {
			s2Low = rL
			s2High = rH
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16", "    ldr q31, [x28, x16]")
		}
	}

	emitLanePmaddupsw := func(dLane, s1Lane, s2Lane string) {
		lines = append(lines,
			// Low 8 bytes
			fmt.Sprintf("    uxtl v20.8h, %s.8b", s1Lane),
			fmt.Sprintf("    sxtl v21.8h, %s.8b", s2Lane),
			"    mul  v22.8h, v20.8h, v21.8h",
			"    trn1 v23.4h, v22.4h, v22.4h",
			"    trn2 v24.4h, v22.4h, v22.4h",
			"    sqadd v25.4h, v23.4h, v24.4h",
			// High 8 bytes
			fmt.Sprintf("    uxtl2 v20.8h, %s.16b", s1Lane),
			fmt.Sprintf("    sxtl2 v21.8h, %s.16b", s2Lane),
			"    mul   v22.8h, v20.8h, v21.8h",
			"    trn1  v23.4h, v22.4h, v22.4h",
			"    trn2  v24.4h, v22.4h, v22.4h",
			"    sqadd v26.4h, v23.4h, v24.4h",
			// Combine into destination
			fmt.Sprintf("    mov %s.d[0], v25.d[0]", dLane),
			fmt.Sprintf("    mov %s.d[1], v26.d[0]", dLane),
		)
	}

	emitLanePmaddupsw(dLowV, s1LowV, s2Low)
	if isYmm {
		emitLanePmaddupsw(dHighV, s1HighV, s2High)
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftPmuldq handles PMULDQ and VPMULDQ.
func (l *Arm64Lifter) liftPmuldq(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isAvx := (op == x86asm.VPMULDQ)

	var dstReg, s1Reg x86asm.Reg
	var src2 x86asm.Arg
	if isAvx && len(inst.Inst.Args) >= 3 && inst.Inst.Args[2] != nil {
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg, _ = inst.Inst.Args[1].(x86asm.Reg)
		src2 = inst.Inst.Args[2]
	} else {
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg = dstReg
		src2 = inst.Inst.Args[1]
	}

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1 := getVectorDesc(s1Reg)
	if !okD || !okS1 {
		lines = append(lines, l.emitUnsupported(inst, "pmuldq invalid dst/s1")...)
		return lines, nil
	}

	s2Low := "v30"
	s2High := "v31"
	if s2Reg, okR := src2.(x86asm.Reg); okR {
		if _, _, _, rL, rH, _, _, okR2 := getVectorDesc(s2Reg); okR2 {
			s2Low = rL
			s2High = rH
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16", "    ldr q31, [x28, x16]")
		}
	}

	emitLanePmuldq := func(dLane, s1Lane, s2Lane string) {
		lines = append(lines,
			fmt.Sprintf("    uzp1 v20.4s, %s.4s, %s.4s", s1Lane, s1Lane),
			fmt.Sprintf("    uzp1 v21.4s, %s.4s, %s.4s", s2Lane, s2Lane),
			fmt.Sprintf("    smull %s.2d, v20.2s, v21.2s", dLane),
		)
	}

	emitLanePmuldq(dLowV, s1LowV, s2Low)
	if isYmm {
		emitLanePmuldq(dHighV, s1HighV, s2High)
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftPmulhw handles PMULHW and VPMULHW.
func (l *Arm64Lifter) liftPmulhw(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	lines, handled, err := l.liftMmxBinary(inst, nextPC, lines)
	if err != nil {
		return lines, err
	}
	if handled {
		return lines, nil
	}

	op := inst.Inst.Op
	isAvx := (op == x86asm.VPMULHW)

	var dstReg, s1Reg x86asm.Reg
	var src2 x86asm.Arg
	if isAvx && len(inst.Inst.Args) >= 3 && inst.Inst.Args[2] != nil {
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg, _ = inst.Inst.Args[1].(x86asm.Reg)
		src2 = inst.Inst.Args[2]
	} else {
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg = dstReg
		src2 = inst.Inst.Args[1]
	}

	isYmm, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, s1HighV, _, _, okS1 := getVectorDesc(s1Reg)
	if !okD || !okS1 {
		lines = append(lines, l.emitUnsupported(inst, "pmulhw invalid dst/s1")...)
		return lines, nil
	}

	s2Low := "v30"
	s2High := "v31"
	if s2Reg, okR := src2.(x86asm.Reg); okR {
		if _, _, _, rL, rH, _, _, okR2 := getVectorDesc(s2Reg); okR2 {
			s2Low = rL
			s2High = rH
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
		if isYmm {
			lines = append(lines, "    add x16, x16, #16", "    ldr q31, [x28, x16]")
		}
	}

	emitLanePmulhw := func(dLane, s1Lane, s2Lane string) {
		lines = append(lines,
			fmt.Sprintf("    smull  v20.4s, %s.4h, %s.4h", s1Lane, s2Lane),
			fmt.Sprintf("    smull2 v21.4s, %s.8h, %s.8h", s1Lane, s2Lane),
			fmt.Sprintf("    uzp2   %s.8h, v20.8h, v21.8h", dLane),
		)
	}

	emitLanePmulhw(dLowV, s1LowV, s2Low)
	if isYmm {
		emitLanePmulhw(dHighV, s1HighV, s2High)
	} else if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftVphminposuw handles VPHMINPOSUW.
func (l *Arm64Lifter) liftVphminposuw(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okD := inst.Inst.Args[0].(x86asm.Reg)
	src := inst.Inst.Args[1]
	_, _, _, dLowV, dHighV, _, _, okDesc := getVectorDesc(dstReg)
	if !okD || !okDesc {
		lines = append(lines, l.emitUnsupported(inst, "vphminposuw invalid dst")...)
		return lines, nil
	}

	sLow := "v30"
	if sReg, okR := src.(x86asm.Reg); okR {
		if _, _, _, rL, _, _, _, okR2 := getVectorDesc(sReg); okR2 {
			sLow = rL
		}
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
	}

	l.labelCounter++
	lines = append(lines,
		"    mrs x16, nzcv",
		"    str x16, [sp, #-16]!",
		// Find min unsigned 16-bit word
		fmt.Sprintf("    uminv h20, %s.8h", sLow),
		fmt.Sprintf("    mov   %s.h[0], v20.h[0]", dLowV),
		// Search for index
		"    umov  w17, v20.h[0]",
		"    mov   v20.h[1], wzr",
	)
	for i := 7; i >= 0; i-- {
		skipMatch := fmt.Sprintf("L_phminposuw_skip_%d_%d", l.labelCounter, i)
		lines = append(lines,
			fmt.Sprintf("    umov  w16, %s.h[%d]", sLow, i),
			"    cmp   w16, w17",
			fmt.Sprintf("    b.ne  %s", skipMatch),
			fmt.Sprintf("    mov   w16, #%d", i),
			"    mov   v20.h[1], w16",
			fmt.Sprintf("%s:", skipMatch),
		)
	}
	lines = append(lines,
		fmt.Sprintf("    mov   %s.h[1], v20.h[1]", dLowV),
		fmt.Sprintf("    mov   %s.s[1], wzr", dLowV),
		fmt.Sprintf("    mov   %s.d[1], xzr", dLowV),
		fmt.Sprintf("    movi  %s.16b, #0", dHighV),
		"    ldr x16, [sp], #16",
		"    msr nzcv, x16",
	)
	return lines, nil
}

// liftAes handles AESENC, VAESENC, AESENCLAST, VAESENCLAST, AESDEC, VAESDEC, AESDECLAST, VAESDECLAST, AESIMC, VAESIMC, AESKEYGENASSIST, VAESKEYGENASSIST.
func (l *Arm64Lifter) liftAes(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okD := inst.Inst.Args[0].(x86asm.Reg)
	_, _, _, dLowV, dHighV, _, _, okDesc := getVectorDesc(dstReg)
	if !okD || !okDesc {
		lines = append(lines, l.emitUnsupported(inst, "aes invalid dst")...)
		return lines, nil
	}

	if op == x86asm.AESIMC || op == x86asm.VAESIMC {
		src := inst.Inst.Args[1]
		sLow := "v30"
		if sReg, okR := src.(x86asm.Reg); okR {
			if _, _, _, rL, _, _, _, okR2 := getVectorDesc(sReg); okR2 {
				sLow = rL
			}
		} else if mem, okMem := src.(x86asm.Mem); okMem {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, "    ldr q30, [x28, x16]")
		}
		lines = append(lines,
			fmt.Sprintf("    aesimc %s.16b, %s.16b", dLowV, sLow),
			fmt.Sprintf("    movi   %s.16b, #0", dHighV),
		)
		return lines, nil
	}

	if op == x86asm.AESKEYGENASSIST || op == x86asm.VAESKEYGENASSIST {
		src := inst.Inst.Args[1]
		imm, _ := inst.Inst.Args[2].(x86asm.Imm)
		if sReg, okR := src.(x86asm.Reg); okR {
			if _, _, _, rL, _, _, _, okR2 := getVectorDesc(sReg); okR2 && rL != "v0" {
				lines = append(lines, fmt.Sprintf("    mov v0.16b, %s.16b", rL))
			}
		} else if mem, okMem := src.(x86asm.Mem); okMem {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, "    ldr q0, [x28, x16]")
		}
		lines = append(lines,
			"    sub  sp, sp, #96",
			"    stp  x29, x30, [sp, #80]",
			"    add  x29, sp, #80",
			"    stp  x8,  x9,  [sp, #0]",
			"    stp  x10, x11, [sp, #16]",
			"    stp  x12, x13, [sp, #32]",
			"    stp  x14, x15, [sp, #48]",
			fmt.Sprintf("    mov  w0, #%d", uint8(imm)),
			"    bl   _recomp_vaeskeygenassist",
			"    ldp  x8,  x9,  [sp, #0]",
			"    ldp  x10, x11, [sp, #16]",
			"    ldp  x12, x13, [sp, #32]",
			"    ldp  x14, x15, [sp, #48]",
			"    ldp  x29, x30, [sp, #80]",
			"    add  sp, sp, #96",
			fmt.Sprintf("    mov  %s.16b, v0.16b", dLowV),
			fmt.Sprintf("    movi %s.16b, #0", dHighV),
		)
		return lines, nil
	}

	var s1Low string
	var src2 x86asm.Arg
	if len(inst.Inst.Args) >= 3 && inst.Inst.Args[2] != nil {
		s1Reg, _ := inst.Inst.Args[1].(x86asm.Reg)
		if _, _, _, s1L, _, _, _, okS1 := getVectorDesc(s1Reg); okS1 {
			s1Low = s1L
		}
		src2 = inst.Inst.Args[2]
	} else {
		s1Low = dLowV
		src2 = inst.Inst.Args[1]
	}

	s2Low := "v30"
	if s2Reg, okR := src2.(x86asm.Reg); okR {
		if _, _, _, rL, _, _, _, okR2 := getVectorDesc(s2Reg); okR2 {
			s2Low = rL
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
	}

	lines = append(lines,
		"    movi v20.16b, #0",
		fmt.Sprintf("    mov  v21.16b, %s.16b", s1Low),
	)

	switch op {
	case x86asm.AESENC, x86asm.VAESENC:
		lines = append(lines,
			"    aese  v21.16b, v20.16b",
			"    aesmc v21.16b, v21.16b",
			fmt.Sprintf("    eor   %s.16b, v21.16b, %s.16b", dLowV, s2Low),
		)
	case x86asm.AESENCLAST, x86asm.VAESENCLAST:
		lines = append(lines,
			"    aese  v21.16b, v20.16b",
			fmt.Sprintf("    eor   %s.16b, v21.16b, %s.16b", dLowV, s2Low),
		)
	case x86asm.AESDEC, x86asm.VAESDEC:
		lines = append(lines,
			"    aesd   v21.16b, v20.16b",
			"    aesimc v21.16b, v21.16b",
			fmt.Sprintf("    eor    %s.16b, v21.16b, %s.16b", dLowV, s2Low),
		)
	case x86asm.AESDECLAST, x86asm.VAESDECLAST:
		lines = append(lines,
			"    aesd  v21.16b, v20.16b",
			fmt.Sprintf("    eor   %s.16b, v21.16b, %s.16b", dLowV, s2Low),
		)
	}

	lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	return lines, nil
}

// liftPushfq handles PUSHFQ: SF, ZF, CF (in the tracked carry sense) and OF from NZCV, IF and
// bit 1 set; AF and PF are not modeled.
func (l *Arm64Lifter) liftPushfq(inst disasm.Instruction, lines []string) ([]string, error) {
	lines = append(lines,
		"    cset w0, mi",
		"    cset w1, eq",
		fmt.Sprintf("    cset w2, %s", l.carryCond(true)),
		"    cset w3, vs",
		"    orr w0, w2, w0, lsl #7",
		"    orr w0, w0, w1, lsl #6",
		"    orr w0, w0, w3, lsl #11",
		"    mov w1, #0x202",
		"    orr w0, w0, w1",
		"    sub x26, x26, #8",
		"    str x0, [x28, x26]",
	)
	return lines, nil
}

// liftPopfq handles POPFQ: N = SF, Z = ZF, C = CF (carryAdd), V = OF.
func (l *Arm64Lifter) liftPopfq(inst disasm.Instruction, lines []string) ([]string, error) {
	lines = append(lines,
		"    ldr x0, [x28, x26]",
		"    add x26, x26, #8",
		"    rmif x0, #4, #12", // N = bit 7, Z = bit 6
		"    rmif x0, #63, #2", // C = bit 0
		"    ubfx w1, w0, #11, #1",
		"    rmif x1, #0, #1", // V = bit 11
	)
	l.cpol = carryAdd
	return lines, nil
}

// liftXlatb handles XLATB.
func (l *Arm64Lifter) liftXlatb(lines []string) ([]string, error) {
	lines = append(lines,
		"    uxtb w16, w19",
		"    add  x16, x20, x16",
		"    ldrb w16, [x28, x16]",
		"    bfi  x19, x16, #0, #8",
	)
	return lines, nil
}

// liftLoop handles LOOP, LOOPE, LOOPNE, and JRCXZ. LOOP/LOOPE/LOOPNE decrement RCX without
// changing the flags (SUB, not SUBS); LOOPE/LOOPNE also test the guest ZF (Z of NZCV). The
// targets start in the block-boundary carry sense, so C is normalized first when CF is live.
func (l *Arm64Lifter) liftLoop(inst disasm.Instruction, nextPC uint64, fn *disasm.Function, lines []string) ([]string, error) {
	op := inst.Inst.Op
	rel, okRel := inst.Inst.Args[0].(x86asm.Rel)
	if !okRel {
		lines = append(lines, l.emitUnsupported(inst, "loop arg not rel")...)
		return lines, nil
	}
	target := uint64(int64(nextPC) + int64(rel))
	if !inst.CarryDead {
		lines = append(lines, l.normalizeCarry()...)
	}
	localLbl := localLabel(fn, target)
	local := localLbl != ""
	skip := fmt.Sprintf("L_%s_skip_%x", strings.ToLower(op.String()), inst.Address)
	far := func() { lines = append(lines, l.branchTo(fn, target)...) }

	switch op {
	case x86asm.LOOP:
		lines = append(lines, "    sub x21, x21, #1")
		if local {
			lines = append(lines, fmt.Sprintf("    cbnz x21, %s", localLbl))
			return lines, nil
		}
		lines = append(lines, fmt.Sprintf("    cbz x21, %s", skip))
		far()

	case x86asm.LOOPE, x86asm.LOOPNE:
		taken, notTaken := "eq", "ne" // LOOPE: ZF = 1
		if op == x86asm.LOOPNE {
			taken, notTaken = "ne", "eq"
		}
		lines = append(lines, "    sub x21, x21, #1", fmt.Sprintf("    cbz x21, %s", skip))
		if local {
			lines = append(lines, fmt.Sprintf("    b.%s %s", taken, localLbl))
		} else {
			lines = append(lines, fmt.Sprintf("    b.%s %s", notTaken, skip))
			far()
		}

	case x86asm.JRCXZ:
		if local {
			lines = append(lines, fmt.Sprintf("    cbz x21, %s", localLbl))
			return lines, nil
		}
		lines = append(lines, fmt.Sprintf("    cbnz x21, %s", skip))
		far()
	}
	lines = append(lines, fmt.Sprintf("%s:", skip))
	return lines, nil
}

// liftFxsave handles FXSAVE.
func (l *Arm64Lifter) liftFxsave(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	mem, okMem := inst.Inst.Args[0].(x86asm.Mem)
	if !okMem {
		lines = append(lines, l.emitUnsupported(inst, "fxsave invalid mem")...)
		return lines, nil
	}
	lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
	lines = append(lines, "    add x16, x28, x16")
	lines = append(lines, l.emitLoadCtxPtr("x17")...)
	lines = append(lines,
		"    ldrh w5, [x17, #0x2e4]",
		"    strh w5, [x16, #0]",
		"    ldrh w5, [x17, #0x2e6]",
		"    strh w5, [x16, #2]",
		"    mov  w5, #0xff",
		"    strb w5, [x16, #4]",
		"    ldr  w5, [x17, #0x2e8]", // mxcsr
		"    str  w5, [x16, #24]",
		"    movz w5, #0xffff",
		"    str  w5, [x16, #28]",
		"    stp  q0, q1, [x16, #160]",
		"    stp  q2, q3, [x16, #192]",
		"    stp  q4, q5, [x16, #224]",
		"    stp  q6, q7, [x16, #256]",
		"    stp  q8, q9, [x16, #288]",
		"    stp  q10, q11, [x16, #320]",
		"    stp  q12, q13, [x16, #352]",
		"    stp  q14, q15, [x16, #384]",
	)
	return lines, nil
}

// liftFxrstor handles FXRSTOR.
func (l *Arm64Lifter) liftFxrstor(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	mem, okMem := inst.Inst.Args[0].(x86asm.Mem)
	if !okMem {
		lines = append(lines, l.emitUnsupported(inst, "fxrstor invalid mem")...)
		return lines, nil
	}
	lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
	lines = append(lines, "    add x16, x28, x16")
	lines = append(lines, l.emitLoadCtxPtr("x17")...)
	lines = append(lines,
		"    ldrh w5, [x16, #0]",
		"    strh w5, [x17, #0x2e4]",
		"    ldrh w5, [x16, #2]",
		"    strh w5, [x17, #0x2e6]",
		"    ldr  w5, [x16, #24]",
		"    str  w5, [x17, #0x2e8]", // mxcsr
		"    ldp  q0, q1, [x16, #160]",
		"    ldp  q2, q3, [x16, #192]",
		"    ldp  q4, q5, [x16, #224]",
		"    ldp  q6, q7, [x16, #256]",
		"    ldp  q8, q9, [x16, #288]",
		"    ldp  q10, q11, [x16, #320]",
		"    ldp  q12, q13, [x16, #352]",
		"    ldp  q14, q15, [x16, #384]",
	)
	lines = append(lines, "    ldr w0, [x17, #0x2e8]")
	lines = append(lines, mxcsrToFpcr("w0")...)
	return lines, nil
}

// liftVphsubd handles VPHSUBD and PHSUBD.
func (l *Arm64Lifter) liftVphsubd(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isAvx := (op == x86asm.VPHSUBD)
	var dstReg, s1Reg x86asm.Reg
	var src2 x86asm.Arg
	if isAvx && len(inst.Inst.Args) >= 3 && inst.Inst.Args[2] != nil {
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg, _ = inst.Inst.Args[1].(x86asm.Reg)
		src2 = inst.Inst.Args[2]
	} else {
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg = dstReg
		src2 = inst.Inst.Args[1]
	}

	_, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, _, _, _, okS1 := getVectorDesc(s1Reg)
	if !okD || !okS1 {
		lines = append(lines, l.emitUnsupported(inst, "vphsubd invalid regs")...)
		return lines, nil
	}

	s2Low := "v30"
	if s2Reg, okR := src2.(x86asm.Reg); okR {
		if _, _, _, rL, _, _, _, okR2 := getVectorDesc(s2Reg); okR2 {
			s2Low = rL
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
	}

	lines = append(lines,
		fmt.Sprintf("    trn1 v20.4s, %s.4s, %s.4s", s1LowV, s1LowV),
		fmt.Sprintf("    trn2 v21.4s, %s.4s, %s.4s", s1LowV, s1LowV),
		"    sub  v22.4s, v20.4s, v21.4s",
		fmt.Sprintf("    trn1 v20.4s, %s.4s, %s.4s", s2Low, s2Low),
		fmt.Sprintf("    trn2 v21.4s, %s.4s, %s.4s", s2Low, s2Low),
		"    sub  v23.4s, v20.4s, v21.4s",
		fmt.Sprintf("    mov  %s.d[0], v22.d[0]", dLowV),
		fmt.Sprintf("    mov  %s.d[1], v23.d[0]", dLowV),
	)
	if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftPclmulqdq handles PCLMULQDQ and VPCLMULQDQ.
func (l *Arm64Lifter) liftPclmulqdq(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	isAvx := (op == x86asm.VPCLMULQDQ)
	var dstReg, s1Reg x86asm.Reg
	var src2 x86asm.Arg
	var immArg x86asm.Arg
	if isAvx && len(inst.Inst.Args) >= 4 && inst.Inst.Args[3] != nil {
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg, _ = inst.Inst.Args[1].(x86asm.Reg)
		src2 = inst.Inst.Args[2]
		immArg = inst.Inst.Args[3]
	} else {
		dstReg, _ = inst.Inst.Args[0].(x86asm.Reg)
		s1Reg = dstReg
		src2 = inst.Inst.Args[1]
		immArg = inst.Inst.Args[2]
	}

	_, _, _, dLowV, dHighV, _, _, okD := getVectorDesc(dstReg)
	_, _, _, s1LowV, _, _, _, okS1 := getVectorDesc(s1Reg)
	if !okD || !okS1 {
		lines = append(lines, l.emitUnsupported(inst, "pclmulqdq invalid dst/s1")...)
		return lines, nil
	}

	immVal := 0
	if imm, okImm := immArg.(x86asm.Imm); okImm {
		immVal = int(imm)
	}

	s2Low := "v30"
	if s2Reg, okR := src2.(x86asm.Reg); okR {
		if _, _, _, rL, _, _, _, okR2 := getVectorDesc(s2Reg); okR2 {
			s2Low = rL
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines, "    ldr q30, [x28, x16]")
	}

	if (immVal & 1) == 0 {
		lines = append(lines, fmt.Sprintf("    mov v20.16b, %s.16b", s1LowV))
	} else {
		lines = append(lines, fmt.Sprintf("    dup v20.2d, %s.d[1]", s1LowV))
	}

	if (immVal & 0x10) == 0 {
		lines = append(lines, fmt.Sprintf("    mov v21.16b, %s.16b", s2Low))
	} else {
		lines = append(lines, fmt.Sprintf("    dup v21.2d, %s.d[1]", s2Low))
	}

	lines = append(lines,
		fmt.Sprintf("    pmull %s.1q, v20.1d, v21.1d", dLowV),
	)
	if isAvx {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHighV))
	}
	return lines, nil
}

// liftPcmpistri handles VPCMPISTRI and PCMPISTRI.
func (l *Arm64Lifter) liftPcmpistri(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	s1 := inst.Inst.Args[0]
	s2 := inst.Inst.Args[1]
	immArg := inst.Inst.Args[2]

	immVal := 0
	if imm, okImm := immArg.(x86asm.Imm); okImm {
		immVal = int(imm)
	}

	s1Reg, okS1 := s1.(x86asm.Reg)
	_, _, _, s1LowV, _, _, _, okS1Desc := getVectorDesc(s1Reg)
	if !okS1 || !okS1Desc {
		lines = append(lines, l.emitUnsupported(inst, "pcmpistri invalid s1")...)
		return lines, nil
	}

	s1Q := "q" + strings.TrimPrefix(s1LowV, "v")

	if mem, okMem := s2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x1")...)
		lines = append(lines, "    add  x1, x28, x1")
	}

	lines = append(lines,
		"    sub  sp, sp, #128",
		"    stp  x29, x30, [sp, #112]",
		"    add  x29, sp, #112",
		"    stp  x8,  x9,  [sp, #32]",
		"    stp  x10, x11, [sp, #48]",
		"    stp  x12, x13, [sp, #64]",
		"    stp  x14, x15, [sp, #80]",
		fmt.Sprintf("    str  %s, [sp, #0]", s1Q),
		"    add  x2, sp, #0",
	)

	if s2Reg, okS2 := s2.(x86asm.Reg); okS2 {
		if _, _, _, s2LowV, _, _, _, okS2Desc := getVectorDesc(s2Reg); okS2Desc {
			s2Q := "q" + strings.TrimPrefix(s2LowV, "v")
			lines = append(lines,
				fmt.Sprintf("    str  %s, [sp, #16]", s2Q),
				"    add  x1, sp, #16",
			)
		}
	}

	lines = append(lines, l.emitLoadCtxPtr("x0")...)
	lines = append(lines,
		fmt.Sprintf("    mov  w3, #%d", immVal&0xff),
		"    bl   _recomp_vpcmpistri",
		"    ldp  x8,  x9,  [sp, #32]",
		"    ldp  x10, x11, [sp, #48]",
		"    ldp  x12, x13, [sp, #64]",
		"    ldp  x14, x15, [sp, #80]",
	)
	lines = append(lines, l.emitLoadCtxPtr("x17")...)
	lines = append(lines,
		"    ldr  x21, [x17, #8]",
		"    ldrb w16, [x17, #0x9a]", // sf -> N (bit 31)
		"    ldrb w5, [x17, #0x99]",  // zf -> Z (bit 30)
		"    orr  w16, w5, w16, lsl #1",
		"    ldrb w5, [x17, #0x98]", // cf
		"    eor  w5, w5, #1",       // ARM64 C = NOT x86 CF (C=1 means no carry / above or equal)
		"    orr  w16, w5, w16, lsl #1",
		"    ldrb w5, [x17, #0x9b]", // of -> V (bit 28)
		"    orr  w16, w5, w16, lsl #1",
		"    lsl  w16, w16, #28",
		"    msr  nzcv, x16",
		"    ldp  x29, x30, [sp, #112]",
		"    add  sp, sp, #128",
	)
	l.cpol = carrySub
	return lines, nil
}

// liftPcmpestri handles VPCMPESTRI and PCMPESTRI.
func (l *Arm64Lifter) liftPcmpestri(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	s1 := inst.Inst.Args[0]
	s2 := inst.Inst.Args[1]
	immArg := inst.Inst.Args[2]

	immVal := 0
	if imm, okImm := immArg.(x86asm.Imm); okImm {
		immVal = int(imm)
	}

	s1Reg, okS1 := s1.(x86asm.Reg)
	_, _, _, s1LowV, _, _, _, okS1Desc := getVectorDesc(s1Reg)
	if !okS1 || !okS1Desc {
		lines = append(lines, l.emitUnsupported(inst, "pcmpestri invalid s1")...)
		return lines, nil
	}

	s1Q := "q" + strings.TrimPrefix(s1LowV, "v")

	if mem, okMem := s2.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x1")...)
		lines = append(lines, "    add  x1, x28, x1")
	}

	lines = append(lines,
		"    sub  sp, sp, #128",
		"    stp  x29, x30, [sp, #112]",
		"    add  x29, sp, #112",
		"    stp  x8,  x9,  [sp, #32]",
		"    stp  x10, x11, [sp, #48]",
		"    stp  x12, x13, [sp, #64]",
		"    stp  x14, x15, [sp, #80]",
		fmt.Sprintf("    str  %s, [sp, #0]", s1Q),
		"    add  x2, sp, #0",
	)

	if s2Reg, okS2 := s2.(x86asm.Reg); okS2 {
		if _, _, _, s2LowV, _, _, _, okS2Desc := getVectorDesc(s2Reg); okS2Desc {
			s2Q := "q" + strings.TrimPrefix(s2LowV, "v")
			lines = append(lines,
				fmt.Sprintf("    str  %s, [sp, #16]", s2Q),
				"    add  x1, sp, #16",
			)
		}
	}

	lines = append(lines, l.emitLoadCtxPtr("x0")...)
	lines = append(lines,
		"    str  x19, [x0, #0]",
		"    str  x22, [x0, #16]",
		fmt.Sprintf("    mov  w3, #%d", immVal&0xff),
		"    bl   _recomp_vpcmpe_stri",
		"    ldp  x8,  x9,  [sp, #32]",
		"    ldp  x10, x11, [sp, #48]",
		"    ldp  x12, x13, [sp, #64]",
		"    ldp  x14, x15, [sp, #80]",
	)
	lines = append(lines, l.emitLoadCtxPtr("x17")...)
	lines = append(lines,
		"    ldr  x21, [x17, #8]",
		"    ldrb w16, [x17, #0x9a]", // sf -> N (bit 31)
		"    ldrb w5, [x17, #0x99]",  // zf -> Z (bit 30)
		"    orr  w16, w5, w16, lsl #1",
		"    ldrb w5, [x17, #0x98]", // cf
		"    eor  w5, w5, #1",       // ARM64 C = NOT x86 CF (C=1 means no carry / above or equal)
		"    orr  w16, w5, w16, lsl #1",
		"    ldrb w5, [x17, #0x9b]", // of -> V (bit 28)
		"    orr  w16, w5, w16, lsl #1",
		"    lsl  w16, w16, #28",
		"    msr  nzcv, x16",
		"    ldp  x29, x30, [sp, #112]",
		"    add  sp, sp, #128",
	)
	l.cpol = carrySub
	return lines, nil
}

// ssse3Operands resolves the operands of a 128-bit integer op in its legacy (dst, src) or VEX
// (dst, src1, src2) form; a memory src2 is loaded into v28.
func (l *Arm64Lifter) ssse3Operands(inst disasm.Instruction, nextPC uint64, lines []string) (
	out []string, d, dHigh, a, b string, vex, ok bool) {
	args := inst.Inst.Args
	vex = strings.HasPrefix(inst.Inst.Op.String(), "V")
	dReg, okD := args[0].(x86asm.Reg)
	src1, src2 := args[0], args[1]
	if vex {
		src1, src2 = args[1], args[2]
	}
	s1Reg, okS1 := src1.(x86asm.Reg)
	isY, _, _, dLow, dHi, _, _, okDD := getVectorDesc(dReg)
	_, _, _, s1Low, _, _, _, okS1D := getVectorDesc(s1Reg)
	if !okD || !okS1 || !okDD || !okS1D || isY {
		return lines, "", "", "", "", vex, false
	}
	switch v := src2.(type) {
	case x86asm.Reg:
		_, _, _, s2Low, _, _, _, okS2 := getVectorDesc(v)
		if !okS2 {
			return lines, "", "", "", "", vex, false
		}
		b = s2Low
	case x86asm.Mem:
		lines = append(lines, computeEffAddr(v, nextPC, "x16")...)
		lines = append(lines, "    ldr q28, [x28, x16]")
		b = "v28"
	default:
		return lines, "", "", "", "", vex, false
	}
	return lines, dLow, dHi, s1Low, b, vex, true
}

// liftPmulhrsw handles (V)PMULHRSW: per signed word, (a * b + 0x4000) >> 15, truncated to 16
// bits (so -32768 * -32768 gives -32768; ARM SQRDMULH would saturate to 32767).
func (l *Arm64Lifter) liftPmulhrsw(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	lines, d, dHigh, a, b, vex, ok := l.ssse3Operands(inst, nextPC, lines)
	if !ok {
		lines = append(lines, l.emitUnsupported(inst, "unsupported pmulhrsw operands")...)
		return lines, nil
	}
	lines = append(lines,
		fmt.Sprintf("    smull v29.4s, %s.4h, %s.4h", a, b),
		fmt.Sprintf("    smull2 v30.4s, %s.8h, %s.8h", a, b),
		fmt.Sprintf("    rshrn %s.4h, v29.4s, #15", d),
		fmt.Sprintf("    rshrn2 %s.8h, v30.4s, #15", d))
	if vex {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHigh))
	}
	return lines, nil
}

// liftPsign handles (V)PSIGNB/W/D: each element of src1 is negated where the src2 element is
// negative, zeroed where it is zero and kept where it is positive (negation wraps, as on x86).
func (l *Arm64Lifter) liftPsign(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	lines, d, dHigh, a, b, vex, ok := l.ssse3Operands(inst, nextPC, lines)
	if !ok {
		lines = append(lines, l.emitUnsupported(inst, "unsupported psign operands")...)
		return lines, nil
	}
	arr := map[x86asm.Op]string{x86asm.PSIGNB: ".16b", x86asm.VPSIGNB: ".16b", x86asm.PSIGNW: ".8h",
		x86asm.VPSIGNW: ".8h", x86asm.PSIGND: ".4s", x86asm.VPSIGND: ".4s"}[inst.Inst.Op]
	lines = append(lines,
		fmt.Sprintf("    cmlt v29%s, %s%s, #0", arr, b, arr), // negative
		fmt.Sprintf("    cmeq v30%s, %s%s, #0", arr, b, arr), // zero
		fmt.Sprintf("    neg v31%s, %s%s", arr, a, arr),
		fmt.Sprintf("    bsl v29.16b, v31.16b, %s.16b", a),
		fmt.Sprintf("    bic %s.16b, v29.16b, v30.16b", d))
	if vex {
		lines = append(lines, fmt.Sprintf("    movi %s.16b, #0", dHigh))
	}
	return lines, nil
}
