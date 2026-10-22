package lifter

import (
	"fmt"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

// liftFpu handles x87 FPU stack instructions for the ARM64 assembly backend. The stack values
// are worked on in d27-d29 (d0-d15 are XMM0-XMM15, which x87 code must not change).
// In the assembly runtime:
// GuestContext pointer is in global _g_current_guest_ctx.
// fpu_stack is at offset 0x2a0 (8 doubles = 64 bytes).
// fpu_top is at offset 0x2e0 (int32).
// fpu_sw is at offset 0x2e6 (uint16).
// fpu_cw is at offset 0x2e4 (uint16).
//
// Rounding: the stack holds doubles, so arithmetic results are rounded to double precision in
// the FPCR rounding mode (the one LDMXCSR derives from MXCSR). The precision control of the
// x87 control word is not modelled, and neither is its rounding control for arithmetic. The
// rounding control is applied where x87 code rounds to an integer or to single precision:
// FIST/FISTP, FRNDINT and FST/FSTP m32 (x87RoundBegin). libc's fesetround sets both control
// words alike, so the two modes only differ if guest code changes one of them alone.
//
// Tags: the tag word is not modelled. FNSTENV stores 0xffff (all empty), which is the state at
// every call boundary of the System V ABI; FLDENV ignores the stored tags.
func (l *Arm64Lifter) liftFpu(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	args := inst.Inst.Args
	defMemSz := defMemSz(inst)

	// Helper to load context pointer into a GPR (e.g. x16)
	loadCtxPtr := func(reg string) []string {
		return []string{
			fmt.Sprintf("    add  %s, x27, #0x70", reg),
			fmt.Sprintf("    ldr  %s, [x28, %s]", reg, reg),
		}
	}

	// Helper to emit fpu_push(double in d28)
	// top = (top - 1) & 7
	// stack[top] = val
	emitFpuPushD0 := func() []string {
		var code []string
		code = append(code, loadCtxPtr("x16")...)
		code = append(code,
			"    ldr  w17, [x16, #0x2e0]",      // w17 = fpu_top
			"    sub  w17, w17, #1",            // w17 = top - 1
			"    and  w17, w17, #7",            // w17 = (top - 1) & 7
			"    str  w17, [x16, #0x2e0]",      // update fpu_top
			"    add  x5, x16, #0x2a0",         // x5 = &fpu_stack[0]
			"    str  d28, [x5, w17, uxtw #3]", // fpu_stack[top] = d28
		)
		return code
	}

	// Helper to emit fpu_pop() -> result in d28
	// val = stack[top]
	// top = (top + 1) & 7
	emitFpuPopD0 := func() []string {
		var code []string
		code = append(code, loadCtxPtr("x16")...)
		code = append(code,
			"    ldr  w17, [x16, #0x2e0]",      // w17 = fpu_top
			"    add  x5, x16, #0x2a0",         // x5 = &fpu_stack[0]
			"    ldr  d28, [x5, w17, uxtw #3]", // d28 = fpu_stack[top]
			"    add  w17, w17, #1",            // w17 = top + 1
			"    and  w17, w17, #7",            // w17 = (top + 1) & 7
			"    str  w17, [x16, #0x2e0]",      // update fpu_top
		)
		return code
	}

	// Helper to read FPU ST(idx) into a target float reg (e.g. d28, d29)
	loadFpuSt := func(targetD string, idx int) []string {
		var code []string
		code = append(code, loadCtxPtr("x16")...)
		code = append(code,
			"    ldr  w17, [x16, #0x2e0]",              // w17 = fpu_top
			fmt.Sprintf("    add  w17, w17, #%d", idx), // w17 = top + idx
			"    and  w17, w17, #7",                    // w17 = (top + idx) & 7
			"    add  x5, x16, #0x2a0",                 // x5 = &fpu_stack[0]
			fmt.Sprintf("    ldr  %s, [x5, w17, uxtw #3]", targetD),
		)
		return code
	}

	// Helper to store a float reg (e.g. d28) into FPU ST(idx)
	storeFpuSt := func(srcD string, idx int) []string {
		var code []string
		code = append(code, loadCtxPtr("x16")...)
		code = append(code,
			"    ldr  w17, [x16, #0x2e0]",              // w17 = fpu_top
			fmt.Sprintf("    add  w17, w17, #%d", idx), // w17 = top + idx
			"    and  w17, w17, #7",                    // w17 = (top + idx) & 7
			"    add  x5, x16, #0x2a0",                 // x5 = &fpu_stack[0]
			fmt.Sprintf("    str  %s, [x5, w17, uxtw #3]", srcD),
		)
		return code
	}

	// Discard top of FPU stack: fpu_top = (fpu_top + 1) & 7
	emitFpuDiscard := func() []string {
		var code []string
		code = append(code, loadCtxPtr("x16")...)
		code = append(code,
			"    ldr  w17, [x16, #0x2e0]",
			"    add  w17, w17, #1",
			"    and  w17, w17, #7",
			"    str  w17, [x16, #0x2e0]",
		)
		return code
	}

	switch op {
	case x86asm.FLDZ:
		lines = append(lines, "    fmov d28, xzr")
		lines = append(lines, emitFpuPushD0()...)
		return lines, nil

	case x86asm.FLD1:
		lines = append(lines, "    fmov d28, #1.0")
		lines = append(lines, emitFpuPushD0()...)
		return lines, nil

	case x86asm.FLD:
		if reg, ok := args[0].(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
			idx := int(reg - x86asm.F0)
			lines = append(lines, loadFpuSt("d28", idx)...)
			lines = append(lines, emitFpuPushD0()...)
			return lines, nil
		}
		if mem, ok := args[0].(x86asm.Mem); ok {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			if isX87M80(inst) {
				lines = append(lines, "    add  x1, x28, x16")
				lines = append(lines, x87LoadM80()...)
			} else if defMemSz == 8 {
				lines = append(lines, "    ldr d28, [x28, x16]")
			} else {
				lines = append(lines,
					"    ldr s28, [x28, x16]",
					"    fcvt d28, s28",
				)
			}
			lines = append(lines, emitFpuPushD0()...)
			return lines, nil
		}

	case x86asm.FILD:
		if mem, ok := args[0].(x86asm.Mem); ok {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			if defMemSz == 8 {
				lines = append(lines,
					"    ldr x17, [x28, x16]",
					"    scvtf d28, x17",
				)
			} else if defMemSz == 2 {
				lines = append(lines,
					"    ldrsh w17, [x28, x16]",
					"    scvtf d28, w17",
				)
			} else {
				lines = append(lines,
					"    ldrsw x17, [x28, x16]",
					"    scvtf d28, x17",
				)
			}
			lines = append(lines, emitFpuPushD0()...)
			return lines, nil
		}

	case x86asm.FST, x86asm.FSTP:
		isPop := (op == x86asm.FSTP)
		if reg, ok := args[0].(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
			idx := int(reg - x86asm.F0)
			lines = append(lines, loadFpuSt("d28", 0)...)
			if idx != 0 {
				lines = append(lines, storeFpuSt("d28", idx)...)
			}
			if isPop {
				lines = append(lines, emitFpuDiscard()...)
			}
			return lines, nil
		}
		if mem, ok := args[0].(x86asm.Mem); ok {
			// Read ST(0) before computing the address: the stack helpers use x16 for the
			// GuestContext pointer.
			lines = append(lines, loadFpuSt("d28", 0)...)
			if isX87M80(inst) {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, "    add  x1, x28, x16")
				lines = append(lines, x87StoreM80()...)
			} else if defMemSz == 8 {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, "    str d28, [x28, x16]")
			} else {
				lines = append(lines, x87RoundBegin()...)
				lines = append(lines, "    fcvt s28, d28")
				lines = append(lines, x87RoundEnd()...)
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, "    str s28, [x28, x16]")
			}
			if isPop {
				lines = append(lines, emitFpuDiscard()...)
			}
			return lines, nil
		}

	case x86asm.FIST, x86asm.FISTP, x86asm.FISTTP:
		if mem, ok := args[0].(x86asm.Mem); ok {
			// Value first, address second (the stack helpers use x16), then the store.
			if op == x86asm.FIST {
				lines = append(lines, loadFpuSt("d28", 0)...)
			} else {
				lines = append(lines, emitFpuPopD0()...)
			}
			// FIST/FISTP round by the control word (FISTTP truncates): the value is rounded to an
			// integer in that mode first, so the conversion itself is exact.
			trunc := op == x86asm.FISTTP
			if !trunc {
				lines = append(lines, x87RoundBegin()...)
				lines = append(lines, "    frintx d28, d28")
				lines = append(lines, x87RoundEnd()...)
				trunc = true
			}
			switch defMemSz {
			case 8:
				lines = append(lines, fpToIntX86("x17", "d28", trunc)...)
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, "    str x17, [x28, x16]")
			case 2:
				lines = append(lines, fpToInt16X86("w17", "d28", trunc)...)
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, "    strh w17, [x28, x16]")
			default:
				lines = append(lines, fpToIntX86("w17", "d28", trunc)...)
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				lines = append(lines, "    str w17, [x28, x16]")
			}
			return lines, nil
		}

	case x86asm.FXCH:
		idx := 1
		if len(args) > 0 {
			if reg, ok := args[0].(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
				idx = int(reg - x86asm.F0)
			}
		}
		// Swap ST(0) and ST(idx)
		lines = append(lines, loadFpuSt("d28", 0)...)
		lines = append(lines, loadFpuSt("d29", idx)...)
		lines = append(lines, storeFpuSt("d29", 0)...)
		lines = append(lines, storeFpuSt("d28", idx)...)
		return lines, nil

	case x86asm.FCHS:
		lines = append(lines, loadFpuSt("d28", 0)...)
		lines = append(lines, "    fneg d28, d28")
		lines = append(lines, storeFpuSt("d28", 0)...)
		return lines, nil

	case x86asm.FABS:
		lines = append(lines, loadFpuSt("d28", 0)...)
		lines = append(lines, "    fabs d28, d28")
		lines = append(lines, storeFpuSt("d28", 0)...)
		return lines, nil

	case x86asm.FSQRT:
		lines = append(lines, loadFpuSt("d28", 0)...)
		lines = append(lines, "    fsqrt d28, d28")
		lines = append(lines, storeFpuSt("d28", 0)...)
		return lines, nil

	// Binary arithmetic: FADD, FADDP, FSUB, FSUBP, FSUBR, FSUBRP, FMUL, FMULP, FDIV, FDIVP, FDIVR, FDIVRP
	case x86asm.FADD, x86asm.FADDP,
		x86asm.FSUB, x86asm.FSUBP, x86asm.FSUBR, x86asm.FSUBRP,
		x86asm.FMUL, x86asm.FMULP,
		x86asm.FDIV, x86asm.FDIVP, x86asm.FDIVR, x86asm.FDIVRP:

		isPop := (op == x86asm.FADDP || op == x86asm.FSUBP || op == x86asm.FSUBRP ||
			op == x86asm.FMULP || op == x86asm.FDIVP || op == x86asm.FDIVRP)

		dstIdx := 0
		if isPop {
			dstIdx = 1
		}

		armFpOp := "fadd"
		isRev := false
		switch op {
		case x86asm.FADD, x86asm.FADDP:
			armFpOp = "fadd"
		case x86asm.FMUL, x86asm.FMULP:
			armFpOp = "fmul"
		case x86asm.FSUB, x86asm.FSUBP:
			armFpOp = "fsub"
		case x86asm.FSUBR, x86asm.FSUBRP:
			armFpOp = "fsub"
			isRev = true
		case x86asm.FDIV, x86asm.FDIVP:
			armFpOp = "fdiv"
		case x86asm.FDIVR, x86asm.FDIVRP:
			armFpOp = "fdiv"
			isRev = true
		}

		if len(args) > 0 && args[0] != nil {
			if reg0, ok0 := args[0].(x86asm.Reg); ok0 && reg0 >= x86asm.F0 && reg0 <= x86asm.F7 {
				dstIdx = int(reg0 - x86asm.F0)
				srcIdx := 0
				if len(args) > 1 && args[1] != nil {
					if reg1, ok1 := args[1].(x86asm.Reg); ok1 && reg1 >= x86asm.F0 && reg1 <= x86asm.F7 {
						srcIdx = int(reg1 - x86asm.F0)
					}
				}
				lines = append(lines, loadFpuSt("d28", dstIdx)...)
				lines = append(lines, loadFpuSt("d29", srcIdx)...)
				if isRev {
					lines = append(lines, fmt.Sprintf("    %s d28, d29, d28", armFpOp))
				} else {
					lines = append(lines, fmt.Sprintf("    %s d28, d28, d29", armFpOp))
				}
				lines = append(lines, storeFpuSt("d28", dstIdx)...)
				if isPop {
					lines = append(lines, emitFpuDiscard()...)
				}
				return lines, nil
			}

			if mem, okMem := args[0].(x86asm.Mem); okMem {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				if defMemSz == 8 {
					lines = append(lines, "    ldr d29, [x28, x16]")
				} else {
					lines = append(lines,
						"    ldr s29, [x28, x16]",
						"    fcvt d29, s29",
					)
				}
				lines = append(lines, loadFpuSt("d28", 0)...)
				if isRev {
					lines = append(lines, fmt.Sprintf("    %s d28, d29, d28", armFpOp))
				} else {
					lines = append(lines, fmt.Sprintf("    %s d28, d28, d29", armFpOp))
				}
				lines = append(lines, storeFpuSt("d28", 0)...)
				if isPop {
					lines = append(lines, emitFpuDiscard()...)
				}
				return lines, nil
			}
		}

		// Default implicit ST(1) op ST(0)
		lines = append(lines, loadFpuSt("d28", 1)...)
		lines = append(lines, loadFpuSt("d29", 0)...)
		if isRev {
			lines = append(lines, fmt.Sprintf("    %s d28, d29, d28", armFpOp))
		} else {
			lines = append(lines, fmt.Sprintf("    %s d28, d28, d29", armFpOp))
		}
		lines = append(lines, storeFpuSt("d28", 1)...)
		if isPop {
			lines = append(lines, emitFpuDiscard()...)
		}
		return lines, nil

	// Integer arithmetic: FIADD, FISUB, FIMUL, FIDIV
	case x86asm.FIADD, x86asm.FISUB, x86asm.FIMUL, x86asm.FIDIV:
		if mem, ok := args[0].(x86asm.Mem); ok {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			if defMemSz == 2 {
				lines = append(lines,
					"    ldrsh w17, [x28, x16]",
					"    scvtf d29, w17",
				)
			} else {
				lines = append(lines,
					"    ldrsw x17, [x28, x16]",
					"    scvtf d29, x17",
				)
			}
			lines = append(lines, loadFpuSt("d28", 0)...)
			switch op {
			case x86asm.FIADD:
				lines = append(lines, "    fadd d28, d28, d29")
			case x86asm.FISUB:
				lines = append(lines, "    fsub d28, d28, d29")
			case x86asm.FIMUL:
				lines = append(lines, "    fmul d28, d28, d29")
			case x86asm.FIDIV:
				lines = append(lines, "    fdiv d28, d28, d29")
			}
			lines = append(lines, storeFpuSt("d28", 0)...)
			return lines, nil
		}

	// Comparisons setting EFLAGS: FUCOMI, FUCOMIP, FCOMI, FCOMIP
	case x86asm.FUCOMI, x86asm.FUCOMIP, x86asm.FCOMI:
		idx := 1
		if len(args) > 1 && args[1] != nil {
			if reg, ok := args[1].(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
				idx = int(reg - x86asm.F0)
			}
		}
		lines = append(lines, loadFpuSt("d28", 0)...)
		lines = append(lines, loadFpuSt("d29", idx)...)
		// fcmp sets NZCV directly!
		// In ARM64:
		// EQ -> Z=1, C=1
		// LT -> C=0, Z=0
		// GT -> C=1, Z=0
		// Unordered -> V=1, C=1, Z=0
		lines = append(lines, "    fcmp d28, d29")
		lines = append(lines, l.fpCompareFlags()...)
		if op == x86asm.FUCOMIP {
			lines = append(lines, emitFpuDiscard()...)
		}
		return lines, nil

	// Conditional moves: FCMOVB, FCMOVE, FCMOVBE, FCMOVU, FCMOVNB, FCMOVNE, FCMOVNBE, FCMOVNU
	case x86asm.FCMOVB, x86asm.FCMOVE, x86asm.FCMOVBE, x86asm.FCMOVU,
		x86asm.FCMOVNB, x86asm.FCMOVNE, x86asm.FCMOVNBE, x86asm.FCMOVNU:
		// FCMOVcc ST(0), ST(i): the source is the second operand.
		idx := 1
		if len(args) > 1 {
			if reg, ok := args[1].(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
				idx = int(reg - x86asm.F0)
			}
		}
		// x86 condition encoding (see x86CondIndex): B, AE, E, NE, BE, A, P, NP.
		cc := map[x86asm.Op]int{
			x86asm.FCMOVB: 2, x86asm.FCMOVNB: 3, x86asm.FCMOVE: 4, x86asm.FCMOVNE: 5,
			x86asm.FCMOVBE: 6, x86asm.FCMOVNBE: 7, x86asm.FCMOVU: 10, x86asm.FCMOVNU: 11,
		}[op]
		pre, cond := l.condFor(cc)
		lines = append(lines, pre...)
		lines = append(lines, loadFpuSt("d28", 0)...)
		lines = append(lines, loadFpuSt("d29", idx)...)
		lines = append(lines, fmt.Sprintf("    fcsel d28, d29, d28, %s", cond))
		lines = append(lines, storeFpuSt("d28", 0)...)
		return lines, nil

	// Status and Control Word
	case x86asm.FNSTSW:
		// Store FPU status word into AX or memory
		lines = append(lines, loadCtxPtr("x16")...)
		lines = append(lines,
			"    ldrh w17, [x16, #0x2e6]", // w17 = fpu_sw
			"    ldr  w5, [x16, #0x2e0]",  // w5 = fpu_top
			"    and  w5, w5, #7",
			"    bic  w17, w17, #(7 << 11)",  // clear TOP bits
			"    orr  w17, w17, w5, lsl #11", // insert TOP bits
		)
		if reg, ok := args[0].(x86asm.Reg); ok && reg == x86asm.AX {
			lines = append(lines, "    bfi x19, x17, #0, #16") // AX is in x19
			return lines, nil
		}
		if mem, ok := args[0].(x86asm.Mem); ok {
			lines = append(lines, computeEffAddr(mem, nextPC, "x5")...)
			lines = append(lines, "    strh w17, [x28, x5]")
			return lines, nil
		}

	case x86asm.FLDCW:
		if mem, ok := args[0].(x86asm.Mem); ok {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, "    ldrh w17, [x28, x16]")
			lines = append(lines, loadCtxPtr("x5")...)
			lines = append(lines, "    strh w17, [x5, #0x2e4]")
			return lines, nil
		}

	case x86asm.FNSTCW:
		if mem, ok := args[0].(x86asm.Mem); ok {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, loadCtxPtr("x5")...)
			lines = append(lines,
				"    ldrh w17, [x5, #0x2e4]",
				"    strh w17, [x28, x16]",
			)
			return lines, nil
		}

	case x86asm.FNCLEX:
		lines = append(lines, loadCtxPtr("x16")...)
		lines = append(lines,
			"    ldrh w17, [x16, #0x2e6]",
			"    bic  w17, w17, #0x7f",
			"    strh w17, [x16, #0x2e6]",
		)
		return lines, nil

	case x86asm.FSCALE:
		lines = append(lines, loadFpuSt("d28", 0)...)
		lines = append(lines, loadFpuSt("d29", 1)...)
		lines = append(lines, x87Scale()...)
		lines = append(lines, storeFpuSt("d28", 0)...)
		return lines, nil

	case x86asm.FPREM, x86asm.FPREM1:
		lines = append(lines, loadFpuSt("d28", 0)...)
		lines = append(lines, loadFpuSt("d29", 1)...)
		lines = append(lines, x87Remainder(op == x86asm.FPREM1)...) // quotient bits in x4
		lines = append(lines, storeFpuSt("d28", 0)...)
		// C2 = 0 (complete), C0 = Q2, C3 = Q1, C1 = Q0.
		lines = append(lines, loadCtxPtr("x16")...)
		lines = append(lines,
			"    ldrh w6, [x16, #0x2e6]",
			"    mov  w2, #0x4700",
			"    bic  w6, w6, w2",
			"    ubfx w2, w4, #2, #1",
			"    orr  w6, w6, w2, lsl #8",
			"    ubfx w2, w4, #1, #1",
			"    orr  w6, w6, w2, lsl #14",
			"    and  w2, w4, #1",
			"    orr  w6, w6, w2, lsl #9",
			"    strh w6, [x16, #0x2e6]",
		)
		return lines, nil

	case x86asm.FFREE, x86asm.FFREEP:
		if op == x86asm.FFREEP {
			lines = append(lines, emitFpuDiscard()...)
		}
		return lines, nil

	case x86asm.FNSTENV:
		if mem, ok := args[0].(x86asm.Mem); ok {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, "    add  x16, x28, x16")
			lines = append(lines, loadCtxPtr("x5")...)
			// The 28-byte protected-mode environment: FCW, FSW with TOP, the tag word (not
			// modelled: all empty), and the last instruction and operand pointers (0). Then all
			// exceptions are masked, as FNSTENV does.
			lines = append(lines,
				"    ldrh w17, [x5, #0x2e4]",
				"    str  w17, [x16, #0]",
				"    orr  w17, w17, #0x3f",
				"    strh w17, [x5, #0x2e4]",
				"    ldrh w17, [x5, #0x2e6]",
				"    ldr  w6, [x5, #0x2e0]",
				"    bfi  w17, w6, #11, #3",
				"    str  w17, [x16, #4]",
				"    mov  w17, #0xffff",
				"    str  w17, [x16, #8]",
				"    stur xzr, [x16, #12]",
				"    stur xzr, [x16, #20]",
			)
			return lines, nil
		}

	case x86asm.FLDENV:
		if mem, ok := args[0].(x86asm.Mem); ok {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, "    add  x16, x28, x16")
			lines = append(lines, loadCtxPtr("x5")...)
			lines = append(lines,
				"    ldrh w17, [x16, #0]",
				"    strh w17, [x5, #0x2e4]",
				"    ldrh w17, [x16, #4]",
				"    strh w17, [x5, #0x2e6]",
				"    ubfx w17, w17, #11, #3", // TOP
				"    str  w17, [x5, #0x2e0]",
			)
			return lines, nil
		}

	case x86asm.FCOS:
		// 0 sites in TLOU and U4. A libm call here would have to keep XMM0-XMM7, the YMM upper
		// halves in v16-v31 and NZCV, which bl _cos clobbers: trap instead of lowering it wrongly.
		lines = append(lines, l.emitUnsupported(inst, "fcos (not lowered: no call site in the supported games)")...)
		return lines, nil

	case x86asm.FLDL2T:
		lines = append(lines,
			"    movz x16, #0xa371",
			"    movk x16, #0x0979, lsl #16",
			"    movk x16, #0x934f, lsl #32",
			"    movk x16, #0x400a, lsl #48",
			"    fmov d28, x16",
		)
		lines = append(lines, emitFpuPushD0()...)
		return lines, nil

	case x86asm.FRNDINT:
		lines = append(lines, loadFpuSt("d28", 0)...)
		lines = append(lines, x87RoundBegin()...)
		lines = append(lines, "    frintx d28, d28")
		lines = append(lines, x87RoundEnd()...)
		lines = append(lines, storeFpuSt("d28", 0)...)
		return lines, nil

	case x86asm.FXTRACT:
		// ST(0) = exponent, then push the significand: ST(0) = significand, ST(1) = exponent.
		lines = append(lines, loadFpuSt("d28", 0)...)
		lines = append(lines, x87Extract()...)
		lines = append(lines, storeFpuSt("d29", 0)...)
		lines = append(lines, emitFpuPushD0()...)
		return lines, nil

	case x86asm.FCOM, x86asm.FCOMP, x86asm.FCOMPP, x86asm.FICOM, x86asm.FICOMP:
		lines = append(lines, loadFpuSt("d28", 0)...)
		isInteger := (op == x86asm.FICOM || op == x86asm.FICOMP)
		if len(args) > 0 && args[0] != nil {
			if reg, ok := args[0].(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
				lines = append(lines, loadFpuSt("d29", int(reg-x86asm.F0))...)
			} else if mem, ok := args[0].(x86asm.Mem); ok {
				lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
				if isInteger {
					if defMemSz == 2 {
						lines = append(lines,
							"    ldrsh w17, [x28, x16]",
							"    scvtf d29, w17",
						)
					} else {
						lines = append(lines,
							"    ldr  w17, [x28, x16]",
							"    scvtf d29, w17",
						)
					}
				} else {
					if defMemSz == 4 {
						lines = append(lines,
							"    ldr  s29, [x28, x16]",
							"    fcvt d29, s29",
						)
					} else {
						lines = append(lines,
							"    ldr  d29, [x28, x16]",
						)
					}
				}
			}
		} else {
			lines = append(lines, loadFpuSt("d29", 1)...)
		}
		lines = append(lines, loadCtxPtr("x16")...)
		// FCOM sets C0/C2/C3 in the status word only; EFLAGS (NZCV) is kept.
		lines = append(lines,
			"    mrs  x6, nzcv",
			"    fcmp d28, d29",
			"    ldrh w17, [x16, #0x2e6]",
			"    movz w5, #0x4500",
			"    bic  w17, w17, w5",
			"    b.vs 1f",
			"    b.eq 2f",
			"    b.mi 3f",
			"    b    4f",
			"1:",
			"    orr  w17, w17, w5",
			"    b    4f",
			"2:",
			"    movz w5, #0x4000",
			"    orr  w17, w17, w5",
			"    b    4f",
			"3:",
			"    movz w5, #0x0100",
			"    orr  w17, w17, w5",
			"4:",
			"    strh w17, [x16, #0x2e6]",
			"    msr  nzcv, x6",
		)
		if op == x86asm.FCOMP || op == x86asm.FICOMP {
			lines = append(lines, emitFpuDiscard()...)
		} else if op == x86asm.FCOMPP {
			lines = append(lines, emitFpuDiscard()...)
			lines = append(lines, emitFpuDiscard()...)
		}
		return lines, nil
	}

	lines = append(lines, l.emitUnsupported(inst, fmt.Sprintf("unsupported fpu: %v", op))...)
	return lines, nil
}

// x87RoundBegin sets FPCR.RMode to the rounding control of the x87 control word (FCW bits
// 11:10: nearest, down, up, toward zero; FPCR RMode: nearest, up, down, toward zero) and keeps
// the previous FPCR in x5 for x87RoundEnd. NZCV is not touched. Scratch: x5-x7.
func x87RoundBegin() []string {
	return []string{
		"    add  x5, x27, #0x70",
		"    ldr  x5, [x28, x5]",
		"    ldrh w6, [x5, #0x2e4]",
		"    ubfx w6, w6, #10, #2",
		"    eor  w7, w6, w6, lsr #1",
		"    and  w7, w7, #1", // 1 for down and up, whose encodings swap
		"    orr  w7, w7, w7, lsl #1",
		"    eor  w6, w6, w7",
		"    mrs  x5, fpcr",
		"    mov  x7, x5",
		"    bfi  x7, x6, #22, #2",
		"    msr  fpcr, x7",
	}
}

// x87RoundEnd restores the FPCR that x87RoundBegin saved in x5.
func x87RoundEnd() []string {
	return []string{"    msr  fpcr, x5"}
}

// isX87M80 reports FLD m80 (DB /5) and FSTP m80 (DB /7). x86asm decodes them as FLD/FSTP with
// a memory operand and MemBytes 0 (the m32/m64 forms report 4 or 8).
func isX87M80(inst disasm.Instruction) bool {
	if inst.Inst.Op != x86asm.FLD && inst.Inst.Op != x86asm.FSTP {
		return false
	}
	if _, ok := inst.Inst.Args[0].(x86asm.Mem); !ok {
		return false
	}
	return inst.Inst.MemBytes == 0 && inst.Inst.Opcode>>24 == 0xdb
}

// x87LoadM80 converts the 80-bit extended value at host address x1 to the double in d28 (the
// x87 stack holds doubles). For a normal double result the 64-bit significand is rounded to 53
// bits by UCVTF, i.e. in the FPCR mode derived from the guest MXCSR; a denormal result is
// rounded to nearest even. The x87 control word's rounding control is not applied here (on x86
// the load is exact and the rounding happens at a later store). Exponents beyond the double
// range give +-inf, small ones a denormal or +-0.
// Unsupported encodings (unnormals, pseudo-infinities and pseudo-NaNs: integer bit clear with a
// nonzero exponent) load the real indefinite, as the invalid-operand response of the x87.
// NaN payloads keep their top 51 fraction bits and are made quiet. NZCV is preserved. Scratch: x0, x2-x4, x6, x7.
func x87LoadM80() []string {
	return []string{
		"    mrs  x7, nzcv",
		"    ldr  x2, [x1]",        // significand
		"    ldrh w3, [x1, #8]",    // sign and exponent
		"    and  w4, w3, #0x7fff", // biased exponent (bias 16383)
		"    and  x3, x3, #0x8000",
		"    lsl  x3, x3, #48", // sign in bit 63
		"    mov  w6, #0x7fff",
		"    cmp  w4, w6",
		"    b.eq 3f",
		"    cbz  w4, 1f",
		"    tbz  x2, #63, 4f", // unnormal
		"    b    2f",
		"1:",
		"    cbz  x2, 5f", // +-0
		"    mov  w4, #1", // denormals and pseudo-denormals use exponent 1
		"2:",
		"    clz  x6, x2",
		"    lsl  x2, x2, x6",
		"    mov  x0, x2",     // normalized significand, for the denormal path
		"    sub  w4, w4, w6", // biased exponent of 1.f after normalization
		"    ucvtf d28, x2",   // 2^63 <= value <= 2^64: double exponent field 1086 (1087)
		"    fmov x2, d28",
		"    mov  w6, #16446", // 16383 + 63
		"    sub  w4, w4, w6",
		"    sxtw x4, w4", // value = ucvtf result * 2^x4
		"    add  x6, x4, #1086",
		"    cmp  x6, #2047",
		"    b.ge 6f",
		"    cmp  x6, #0",
		"    b.le 7f",
		"    add  x2, x2, x4, lsl #52",
		"    b    8f",
		"7:", // denormal result: significand x0 >> s, rounded to nearest even
		"    mov  x6, #-1074",
		"    sub  x6, x6, x4", // s = -1074 - x4 >= 12
		"    cmp  x6, #64",
		"    b.gt 5f", // below half the smallest denormal: +-0
		"    b.eq 10f",
		"    lsr  x2, x0, x6",
		"    neg  x4, x6",
		"    lsl  x6, x0, x4",  // the bits shifted out, left-aligned
		"    tbz  x6, #63, 8f", // below half: truncate
		"    lsl  x6, x6, #1",
		"    cbnz x6, 11f",    // above half
		"    tbz  x2, #0, 8f", // tie: to even
		"11:",
		"    add  x2, x2, #1", // may carry into the smallest normal: still the right bits
		"    b    8f",
		"10:", // s = 64: half the smallest denormal or more
		"    lsl  x6, x0, #1",
		"    cmp  x6, #0",
		"    cset x2, ne",
		"    b    8f",
		"3:",
		"    tbz  x2, #63, 4f", // pseudo-infinity / pseudo-NaN
		"    lsl  x6, x2, #1",
		"    cbz  x6, 6f", // infinity
		"    lsr  x6, x6, #12",
		"    orr  x2, x6, #0x7ff8000000000000", // quiet, as the x87 delivers a signaling NaN
		"    b    8f",
		"6:",
		"    mov  x2, #0x7ff0000000000000",
		"    b    8f",
		"4:",
		"    mov  x2, #0xfff8000000000000", // real indefinite
		"    mov  x3, #0",
		"    b    8f",
		"5:",
		"    mov  x2, #0",
		"8:",
		"    orr  x2, x2, x3",
		"    fmov d28, x2",
		"    msr  nzcv, x7",
	}
}

// x87StoreM80 stores the double in d28 as an 80-bit extended value at host address x1 (exact:
// every double is representable; a signaling NaN is stored quiet). NZCV is preserved. Scratch: x0, x2-x4, x6, x7.
func x87StoreM80() []string {
	return []string{
		"    mrs  x7, nzcv",
		"    fmov x2, d28",
		"    lsr  x3, x2, #63",
		"    lsl  x3, x3, #15",      // sign in bit 15 of the exponent word
		"    ubfx x4, x2, #52, #11", // double exponent field
		"    and  x6, x2, #0xfffffffffffff",
		"    cbz  x4, 1f",
		"    mov  x0, #0x7ff",
		"    cmp  x4, x0",
		"    b.eq 2f",
		"    mov  x0, #15360", // 16383 - 1023
		"    add  x4, x4, x0",
		"    lsl  x6, x6, #11",
		"    orr  x6, x6, #0x8000000000000000",
		"    b    4f",
		"1:",
		"    cbz  x6, 4f", // +-0
		"    clz  x0, x6", // denormal: normalize the fraction
		"    lsl  x6, x6, x0",
		"    mov  x4, #15372", // 16383 - 1074 + 63
		"    sub  x4, x4, x0",
		"    b    4f",
		"2:",
		"    mov  x4, #0x7fff", // infinity or NaN
		"    cbz  x6, 3f",
		"    orr  x6, x6, #0x0008000000000000", // NaN: quiet (x87 registers never hold an SNaN loaded from m64)
		"3:",
		"    lsl  x6, x6, #11",
		"    orr  x6, x6, #0x8000000000000000",
		"4:",
		"    orr  x4, x4, x3",
		"    str  x6, [x1]",
		"    strh w4, [x1, #8]",
		"    msr  nzcv, x7",
	}
}

// fpToInt16X86 converts the double in src (d<n>) to int16 in dst (w register) with x86 FIST
// semantics: NaN and values outside [-32768, 32767] give the integer indefinite 0x8000. FISTTP
// truncates; FIST/FISTP round by the current mode. NZCV is not touched. Scratch: x1, x2, v30,
// v31.
func fpToInt16X86(dst, src string, trunc bool) []string {
	var out []string
	if !trunc {
		out = append(out, fmt.Sprintf("    frintx d31, %s", src))
		src = "d31"
	}
	x := "x" + dst[1:]
	out = append(out,
		fmt.Sprintf("    fcvtzs %s, %s", x, src),
		fmt.Sprintf("    fcmeq d30, %s, %s", src, src),
		"    fmov x2, d30", // all ones unless NaN
		fmt.Sprintf("    sxth x1, %s", dst),
		fmt.Sprintf("    eor x1, x1, %s", x), // 0 iff it fits in 16 bits
		"    clz x1, x1",
		"    lsr x1, x1, #6",
		"    sub x1, x1, #1", // 0 iff it fits, else all ones
		"    orn x1, x1, x2", // ... or NaN
		fmt.Sprintf("    bic %s, %s, w1", dst, dst),
		"    and w1, w1, #0x8000",
		fmt.Sprintf("    orr %s, %s, w1", dst, dst))
	return out
}

// The following x87 helpers work on d28 = ST(0) and d29 = ST(1), use x0-x7 and d30/d31 as
// scratch, and preserve NZCV (x86 FSCALE/FPREM/FXTRACT do not change EFLAGS). They use numeric
// local labels so that fixUpperHalves can still save the YMM upper halves around them.

// x87Scale emits FSCALE: d28 = d28 * 2^trunc(d29), rounded once to double (musl scalbn's
// stepping keeps the subnormal range correctly rounded). ST(1) = +inf gives d28 * inf, -inf
// gives d28 * 0 (0 * inf and inf * 0 are the invalid cases).
func x87Scale() []string {
	return []string{
		"    mrs  x7, nzcv",
		"    fcmp d29, d29",
		"    b.vs 8f",
		"    fabs d30, d29",
		"    fmov x6, d30",
		"    movz x2, #0x7ff0, lsl #48",
		"    cmp  x6, x2",
		"    b.ne 1f",
		"    movi d31, #0",
		"    fcmp d29, #0.0",
		"    fcsel d31, d30, d31, gt",
		"    fmul d28, d28, d31",
		"    b    7f",
		"1:",
		"    frintz d31, d29",
		"    fcvtzs x2, d31",
		"    mov  x6, #2200", // beyond +-2200 every finite non-zero double overflows or underflows
		"    cmp  x2, x6",
		"    csel x2, x6, x2, gt",
		"    mov  x6, #-2200",
		"    cmp  x2, x6",
		"    csel x2, x6, x2, lt",
		"    mov  x6, #1023",
		"    movz x3, #0x7fe0, lsl #48", // 2^1023
		"    fmov d31, x3",
		"    cmp  x2, x6",
		"    b.le 2f",
		"    fmul d28, d28, d31",
		"    sub  x2, x2, #1023",
		"    cmp  x2, x6",
		"    b.le 2f",
		"    fmul d28, d28, d31",
		"    sub  x2, x2, #1023",
		"    cmp  x2, x6",
		"    csel x2, x6, x2, gt",
		"2:",
		"    mov  x6, #-1022",
		"    movz x3, #0x0360, lsl #48", // 2^-969 = 2^-1022 * 2^53
		"    fmov d31, x3",
		"    cmp  x2, x6",
		"    b.ge 3f",
		"    fmul d28, d28, d31",
		"    add  x2, x2, #969",
		"    cmp  x2, x6",
		"    b.ge 3f",
		"    fmul d28, d28, d31",
		"    add  x2, x2, #969",
		"    cmp  x2, x6",
		"    csel x2, x6, x2, lt",
		"3:",
		"    add  x2, x2, #1023",
		"    lsl  x2, x2, #52",
		"    fmov d31, x2",
		"    fmul d28, d28, d31",
		"    b    7f",
		"8:",
		"    fadd d28, d28, d29", // NaN operand
		"7:",
		"    msr  nzcv, x7",
	}
}

// x87Remainder emits FPREM (truncated quotient) or FPREM1 (nearest, ties to even): d28 = the
// exact remainder of d28 / d29, by long division of the significands. The low quotient bits are
// left in x4 (for C0/C3/C1). The reduction is always complete (C2 = 0): where x87 returns a
// partial remainder (exponent difference >= 64) the result is the final one of the FPREM loop.
// Invalid operands (x infinite, y zero) give the x86 indefinite NaN.
func x87Remainder(nearest bool) []string {
	out := []string{
		"    mrs  x7, nzcv",
		"    fmov x0, d28",
		"    fmov x1, d29",
		"    mov  x17, #0",
		"    fcmp d28, d29",
		"    b.vs 8f",
		"    ubfx x2, x0, #52, #11",
		"    ubfx x3, x1, #52, #11",
		"    cmp  x2, #0x7ff",
		"    b.eq 9f", // x = +-inf
		"    lsl  x4, x1, #1",
		"    cbz  x4, 9f", // y = +-0
		"    cmp  x3, #0x7ff",
		"    b.eq 7f", // y = +-inf: x
		"    lsl  x4, x0, #1",
		"    cbz  x4, 7f", // x = +-0: x
		"    and  x4, x0, #0xfffffffffffff",
		"    cbnz x2, 1f",
		"    clz  x6, x4", // subnormal x: normalize
		"    sub  x6, x6, #11",
		"    lsl  x4, x4, x6",
		"    mov  x2, #1",
		"    sub  x2, x2, x6",
		"    b    2f",
		"1:",
		"    orr  x4, x4, #0x10000000000000",
		"2:",
		"    and  x5, x1, #0xfffffffffffff",
		"    cbnz x3, 1f",
		"    clz  x6, x5", // subnormal y: normalize
		"    sub  x6, x6, #11",
		"    lsl  x5, x5, x6",
		"    mov  x3, #1",
		"    sub  x3, x3, x6",
		"    b    2f",
		"1:",
		"    orr  x5, x5, #0x10000000000000",
		"2:",
		"    and  x0, x0, #0x8000000000000000", // sign of the result (that of x)
		"    subs x1, x2, x3",                  // exponent difference
		"    b.lt 6f",
		"    mov  x6, x4", // r = (mx << d) mod my, one quotient bit per step
		"3:",
		"    cmp  x6, x5",
		"    b.lo 4f",
		"    sub  x6, x6, x5",
		"    orr  x17, x17, #1",
		"4:",
		"    cbz  x1, 5f",
		"    lsl  x6, x6, #1",
		"    lsl  x17, x17, #1",
		"    sub  x1, x1, #1",
		"    b    3b",
		"5:",
	}
	if nearest {
		out = append(out,
			"    lsl  x2, x6, #1", // 2r against my: round the quotient to nearest, ties to even
			"    cmp  x2, x5",
			"    b.lo 1f",
			"    b.hi 2f",
			"    tbz  x17, #0, 1f",
			"2:",
			"    sub  x6, x5, x6",
			"    add  x17, x17, #1",
			"    eor  x0, x0, #0x8000000000000000",
			"1:")
	}
	out = append(out,
		"    scvtf d28, x6", // r < 2^53: exact; r * 2^(ey - 1075) in two exact steps
		"    sub  x3, x3, #1075",
		"    asr  x2, x3, #1",
		"    sub  x3, x3, x2",
		"    add  x2, x2, #1023",
		"    lsl  x2, x2, #52",
		"    fmov d30, x2",
		"    fmul d28, d28, d30",
		"    add  x3, x3, #1023",
		"    lsl  x3, x3, #52",
		"    fmov d30, x3",
		"    fmul d28, d28, d30",
		"    fmov x6, d28",
		"    orr  x6, x6, x0",
		"    fmov d28, x6",
		"    b    7f",
		"6:") // |x| < |y| by exponent: quotient 0 ...
	if nearest {
		out = append(out,
			"    cmn  x1, #1", // ... unless d = -1 and |x| > |y| / 2: quotient 1
			"    b.ne 7f",
			"    cmp  x4, x5",
			"    b.ls 7f",
			"    fabs d30, d28",
			"    fabs d31, d29",
			"    fsub d30, d30, d31", // |x| - |y|, exact (Sterbenz)
			"    fmov x6, d30",
			"    eor  x6, x6, x0",
			"    fmov d28, x6",
			"    mov  x17, #1")
	}
	out = append(out,
		"    b    7f",
		"8:",
		"    fadd d28, d28, d29", // NaN operand
		"    b    7f",
		"9:",
		"    movz x6, #0xfff8, lsl #48", // x86 real indefinite
		"    fmov d28, x6",
		"7:",
		"    mov  x4, x17",
		"    msr  nzcv, x7")
	return out
}

// x87Extract emits FXTRACT on d28: d29 = the unbiased exponent, d28 = the significand (sign
// kept, exponent 0). Subnormal doubles are normalized first (they are normal in the 80-bit
// format). +-0 gives exponent -inf, +-inf gives +inf, a NaN gives NaN in both.
func x87Extract() []string {
	return []string{
		"    mrs  x7, nzcv",
		"    fmov x0, d28",
		"    ubfx x2, x0, #52, #11",
		"    and  x3, x0, #0xfffffffffffff",
		"    cmp  x2, #0x7ff",
		"    b.eq 5f",
		"    cbnz x2, 2f",
		"    cbz  x3, 4f",
		"    clz  x6, x3",
		"    sub  x6, x6, #11",
		"    lsl  x3, x3, x6",
		"    and  x3, x3, #0xfffffffffffff",
		"    mov  x2, #1",
		"    sub  x2, x2, x6",
		"2:",
		"    sub  x2, x2, #1023",
		"    scvtf d29, x2",
		"    and  x0, x0, #0x8000000000000000",
		"    orr  x0, x0, x3",
		"    orr  x0, x0, #0x3ff0000000000000",
		"    fmov d28, x0",
		"    b    7f",
		"4:",
		"    movz x6, #0xfff0, lsl #48",
		"    fmov d29, x6",
		"    b    7f",
		"5:",
		"    cbnz x3, 6f",
		"    fabs d29, d28",
		"    b    7f",
		"6:",
		"    fmov d29, d28",
		"7:",
		"    msr  nzcv, x7",
	}
}
