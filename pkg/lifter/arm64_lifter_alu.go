package lifter

import (
	"fmt"
	"strings"

	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/disasm"
)

// liftMov handles MOV operations.
func (l *Arm64Lifter) liftMov(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	// Reg <- Reg
	if dstReg, ok := dst.(x86asm.Reg); ok {
		if srcReg, okSrc := src.(x86asm.Reg); okSrc {
			loadCode, okLoad := readRegTo(srcReg, "x16", "w16")
			storeCode, okStore := writeRegFrom(dstReg, "x16", "w16")
			if okLoad && okStore {
				descDst := arm64RegMap[dstReg]
				descSrc := arm64RegMap[srcReg]
				// Fast path: direct register move when no byte masking needed
				if !descDst.IsHigh8 && !descSrc.IsHigh8 {
					if descDst.Size == 4 && descSrc.Size == 4 {
						lines = append(lines, fmt.Sprintf("    mov %s, %s", descDst.W, descSrc.W))
						return lines, nil
					}
					if descDst.Size == 8 && descSrc.Size == 8 {
						lines = append(lines, fmt.Sprintf("    mov %s, %s", descDst.X, descSrc.X))
						return lines, nil
					}
				}
				lines = append(lines, loadCode...)
				lines = append(lines, storeCode...)
				return lines, nil
			}
		}

		// Reg <- Imm
		if imm, okImm := src.(x86asm.Imm); okImm {
			immCode, ok := loadImmToReg(dstReg, uint64(imm))
			if ok {
				lines = append(lines, immCode...)
				return lines, nil
			}
		}

		// Reg <- Mem
		if mem, okMem := src.(x86asm.Mem); okMem {
			descDst, okDst := arm64RegMap[dstReg]
			if okDst && descDst.Size >= 4 {
				// 32/64-bit: load straight into the pinned register (a 32-bit load zero-extends,
				// as an x86 32-bit write does).
				return append(lines, readMemTo(mem, nextPC, descDst.Size, descDst.X, descDst.W)...), nil
			}
			if okDst {
				lines = append(lines, readMemTo(mem, nextPC, descDst.Size, "x16", "w16")...)
				storeCode, _ := writeRegFrom(dstReg, "x16", "w16")
				lines = append(lines, storeCode...)
				return lines, nil
			}
		}
	}

	// Mem <- Reg
	if mem, okMem := dst.(x86asm.Mem); okMem {
		if srcReg, okSrc := src.(x86asm.Reg); okSrc {
			descSrc, okDesc := arm64RegMap[srcReg]
			if okDesc && !descSrc.IsHigh8 {
				// Store straight from the pinned register (STRB/STRH store its low bits).
				return append(lines, writeMemFrom(mem, nextPC, descSrc.Size, descSrc.X, descSrc.W)...), nil
			}
			if okDesc {
				loadCode, _ := readRegTo(srcReg, "x17", "w17")
				lines = append(lines, loadCode...)
				lines = append(lines, writeMemFrom(mem, nextPC, descSrc.Size, "x17", "w17")...)
				return lines, nil
			}
		}

		// Mem <- Imm
		if imm, okImm := src.(x86asm.Imm); okImm {
			sz := defMemSz(inst)
			if sz == 8 {
				lines = append(lines, loadImm64("x17", uint64(imm))...)
			} else {
				lines = append(lines, loadImm32("w17", uint32(imm))...)
			}
			lines = append(lines, writeMemFrom(mem, nextPC, sz, "x17", "w17")...)
			return lines, nil
		}
	}

	// Segment register support (CS, DS, ES, SS, FS, GS in x86-64 long mode)
	if dstReg, ok := dst.(x86asm.Reg); ok && isSegmentReg(dstReg) {
		if dstReg == x86asm.FS {
			if srcReg, okSrc := src.(x86asm.Reg); okSrc {
				loadCode, okLoad := readRegTo(srcReg, "x27", "w27")
				if okLoad {
					lines = append(lines, loadCode...)
					return lines, nil
				}
			} else if mem, okMem := src.(x86asm.Mem); okMem {
				lines = append(lines, readMemTo(mem, nextPC, 8, "x27", "w27")...)
				return lines, nil
			}
		}
		// In 64-bit flat mode, CS/DS/ES/SS writes are ignored
		if mem, okMem := src.(x86asm.Mem); okMem {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		}
		return lines, nil
	}
	if srcReg, ok := src.(x86asm.Reg); ok && isSegmentReg(srcReg) {
		if srcReg == x86asm.FS {
			if dstReg, okDst := dst.(x86asm.Reg); okDst {
				storeCode, okStore := writeRegFrom(dstReg, "x27", "w27")
				if okStore {
					lines = append(lines, storeCode...)
					return lines, nil
				}
			} else if mem, okMem := dst.(x86asm.Mem); okMem {
				lines = append(lines, writeMemFrom(mem, nextPC, 8, "x27", "w27")...)
				return lines, nil
			}
		}
		// CS, DS, ES, SS reads return 0
		if dstReg, okDst := dst.(x86asm.Reg); okDst {
			lines = append(lines, loadImm32("w16", 0)...)
			storeCode, okStore := writeRegFrom(dstReg, "x16", "w16")
			if okStore {
				lines = append(lines, storeCode...)
				return lines, nil
			}
		} else if mem, okMem := dst.(x86asm.Mem); okMem {
			lines = append(lines, writeMemFrom(mem, nextPC, 2, "xzr", "wzr")...)
			return lines, nil
		}
	}

	lines = append(lines, "    brk #0 // unsupported mov")
	return lines, nil
}

// liftMovzx handles MOVZX.
func (l *Arm64Lifter) liftMovzx(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	dstReg, okDst := dst.(x86asm.Reg)
	descDst, okDescDst := arm64RegMap[dstReg]
	if !okDst || !okDescDst {
		lines = append(lines, "    brk #0 // unsupported movzx dst")
		return lines, nil
	}

	if srcReg, okSrc := src.(x86asm.Reg); okSrc {
		descSrc, okDescSrc := arm64RegMap[srcReg]
		if okDescSrc {
			if descSrc.IsHigh8 {
				lines = append(lines, fmt.Sprintf("    ubfx %s, %s, #8, #8", descDst.W, descSrc.W))
			} else if descSrc.Size == 1 {
				lines = append(lines, fmt.Sprintf("    uxtb %s, %s", descDst.W, descSrc.W))
			} else {
				lines = append(lines, fmt.Sprintf("    uxth %s, %s", descDst.W, descSrc.W))
			}
			return lines, nil
		}
	}

	if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		sz := defMemSz(inst)
		switch sz {
		case 2:
			lines = append(lines, fmt.Sprintf("    ldrh %s, [x28, x16]", descDst.W))
		case 4:
			lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", descDst.W))
		default:
			lines = append(lines, fmt.Sprintf("    ldrb %s, [x28, x16]", descDst.W))
		}
		return lines, nil
	}

	lines = append(lines, "    brk #0 // unsupported movzx")
	return lines, nil
}

// liftMovsx handles MOVSX and MOVSXD.
func (l *Arm64Lifter) liftMovsx(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	dstReg, okDst := dst.(x86asm.Reg)
	descDst, okDescDst := arm64RegMap[dstReg]
	if !okDst || !okDescDst {
		lines = append(lines, "    brk #0 // unsupported movsx dst")
		return lines, nil
	}

	if srcReg, okSrc := src.(x86asm.Reg); okSrc {
		descSrc, okDescSrc := arm64RegMap[srcReg]
		if okDescSrc {
			if descSrc.Size == 1 {
				if descDst.Size == 8 {
					lines = append(lines, fmt.Sprintf("    sxtb %s, %s", descDst.X, descSrc.W))
				} else {
					lines = append(lines, fmt.Sprintf("    sxtb %s, %s", descDst.W, descSrc.W))
				}
			} else if descSrc.Size == 2 {
				if descDst.Size == 8 {
					lines = append(lines, fmt.Sprintf("    sxth %s, %s", descDst.X, descSrc.W))
				} else {
					lines = append(lines, fmt.Sprintf("    sxth %s, %s", descDst.W, descSrc.W))
				}
			} else if descSrc.Size == 4 {
				lines = append(lines, fmt.Sprintf("    sxtw %s, %s", descDst.X, descSrc.W))
			}
			return lines, nil
		}
	}

	if mem, okMem := src.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		sz := defMemSz(inst)
		if inst.Inst.Op == x86asm.MOVSXD || sz == 4 {
			lines = append(lines, fmt.Sprintf("    ldrsw %s, [x28, x16]", descDst.X))
		} else if sz == 2 {
			if descDst.Size == 8 {
				lines = append(lines, fmt.Sprintf("    ldrsh %s, [x28, x16]", descDst.X))
			} else {
				lines = append(lines, fmt.Sprintf("    ldrsh %s, [x28, x16]", descDst.W))
			}
		} else {
			if descDst.Size == 8 {
				lines = append(lines, fmt.Sprintf("    ldrsb %s, [x28, x16]", descDst.X))
			} else {
				lines = append(lines, fmt.Sprintf("    ldrsb %s, [x28, x16]", descDst.W))
			}
		}
		return lines, nil
	}

	lines = append(lines, "    brk #0 // unsupported movsx")
	return lines, nil
}

// liftAlu handles ADD, SUB, AND, OR, XOR.
func (l *Arm64Lifter) liftAlu(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	return l.liftArith(inst, nextPC, lines)
}

// liftCmpTest handles CMP and TEST.
func (l *Arm64Lifter) liftCmpTest(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	return l.liftArith(inst, nextPC, lines)
}

// arithOperand puts a source operand of an ALU instruction in a register: the pinned register
// itself for 32/64-bit registers, else xT/wT (8/16-bit values zero-extended; a memory operand's
// address goes through x16). An immediate add/sub operand that fits ARM's 12-bit form is
// returned as "#imm".
func arithOperand(arg x86asm.Arg, size int, nextPC uint64, xT, wT string, immOK bool) ([]string, string, bool) {
	switch v := arg.(type) {
	case x86asm.Reg:
		d, ok := arm64RegMap[v]
		if !ok {
			return nil, "", false
		}
		if size == 8 {
			return nil, d.X, true
		}
		if size == 4 {
			return nil, d.W, true
		}
		rc, _ := readRegTo(v, xT, wT)
		return rc, wT, true
	case x86asm.Imm:
		val := uint64(int64(v))
		switch size {
		case 1:
			val &= 0xff
		case 2:
			val &= 0xffff
		case 4:
			val &= 0xffffffff
		}
		if immOK && size >= 4 && val <= 4095 {
			return nil, fmt.Sprintf("#%d", val), true
		}
		if size == 8 {
			return loadImm64(xT, val), xT, true
		}
		return loadImm32(wT, uint32(val)), wT, true
	case x86asm.Mem:
		rd := readMemTo(v, nextPC, size, xT, wT)
		if size == 8 {
			return rd, xT, true
		}
		return rd, wT, true
	}
	return nil, "", false
}

// liftArith lowers ADD, SUB, AND, OR, XOR, CMP and TEST with x86 flags for every operand size.
// 8/16-bit add/sub/cmp run on top-aligned operands (value << (32 - size)) so N, Z, C and V
// come from the narrow result. The carry sense afterwards is carryAdd for ADD, carrySub for
// SUB/CMP and carryClear for the logical operations (ARM ANDS/TST clear C and V).
func (l *Arm64Lifter) liftArith(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]
	flags := !inst.SkipFlags
	compareOnly := op == x86asm.CMP || op == x86asm.TEST
	if compareOnly && !flags {
		return lines, nil // nothing observable
	}

	size := 0
	dstReg, dstIsReg := dst.(x86asm.Reg)
	dstMem, dstIsMem := dst.(x86asm.Mem)
	var desc Arm64RegDesc
	if dstIsReg {
		d, ok := arm64RegMap[dstReg]
		if !ok {
			lines = append(lines, l.emitUnsupported(inst, "unmapped alu dst")...)
			return lines, nil
		}
		desc, size = d, d.Size
	} else if dstIsMem {
		size = defMemSz(inst)
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported alu dst")...)
		return lines, nil
	}

	// a: the destination value (pinned register for 32/64-bit registers, else x17/w17; x16
	// holds a memory destination's address). b: the source (x0/w0 or the pinned register).
	a := ""
	if dstIsMem {
		lines = append(lines, readMemTo(dstMem, nextPC, size, "x17", "w17")...)
		a = "w17"
		if size == 8 {
			a = "x17"
		}
	} else if size >= 4 {
		a = desc.W
		if size == 8 {
			a = desc.X
		}
	} else {
		rc, _ := readRegTo(dstReg, "x17", "w17")
		lines = append(lines, rc...)
		a = "w17"
	}
	isAddSub := op == x86asm.ADD || op == x86asm.SUB || op == x86asm.CMP
	if _, srcIsMem := src.(x86asm.Mem); srcIsMem && dstIsMem {
		lines = append(lines, l.emitUnsupported(inst, "alu mem, mem")...)
		return lines, nil
	}
	bx, bw := "x0", "w0"
	if dstIsReg {
		bx, bw = "x16", "w16" // no address to keep
	}
	rb, b, ok := arithOperand(src, size, nextPC, bx, bw, isAddSub)
	if !ok {
		lines = append(lines, l.emitUnsupported(inst, "unsupported alu src")...)
		return lines, nil
	}
	lines = append(lines, rb...)

	armOp := map[x86asm.Op]string{x86asm.ADD: "add", x86asm.SUB: "sub", x86asm.CMP: "sub",
		x86asm.AND: "and", x86asm.TEST: "and", x86asm.OR: "orr", x86asm.XOR: "eor"}[op]
	logical := !isAddSub

	// res: where the result is computed ("" for CMP/TEST).
	res := a
	if !dstIsMem && size >= 4 {
		res = a
	} else if size < 4 {
		res = "w17"
	}
	if compareOnly {
		res = "wzr"
		if size == 8 {
			res = "xzr"
		}
	}

	if size >= 4 {
		switch {
		case !flags:
			lines = append(lines, fmt.Sprintf("    %s %s, %s, %s", armOp, res, a, b))
		case compareOnly && armOp == "and":
			lines = append(lines, fmt.Sprintf("    tst %s, %s", a, b))
		case compareOnly:
			lines = append(lines, fmt.Sprintf("    cmp %s, %s", a, b))
		case armOp == "and":
			lines = append(lines, fmt.Sprintf("    ands %s, %s, %s", res, a, b))
		case logical: // ORR/EOR have no flag-setting form
			lines = append(lines, fmt.Sprintf("    %s %s, %s, %s", armOp, res, a, b),
				fmt.Sprintf("    tst %s, %s", res, res))
		default:
			lines = append(lines, fmt.Sprintf("    %ss %s, %s, %s", armOp, res, a, b))
		}
	} else {
		sh := 32 - 8*size
		if flags {
			// Top-align both operands: the flags of the 32-bit operation are those of the
			// narrow one; the result is shifted back down.
			lines = append(lines, fmt.Sprintf("    lsl w17, %s, #%d", a, sh))
			if logical {
				if armOp == "and" {
					lines = append(lines, fmt.Sprintf("    ands w17, w17, %s, lsl #%d", b, sh))
				} else {
					lines = append(lines, fmt.Sprintf("    %s w17, w17, %s, lsl #%d", armOp, b, sh),
						"    tst w17, w17")
				}
			} else {
				lines = append(lines, fmt.Sprintf("    %ss w17, w17, %s, lsl #%d", armOp, b, sh))
			}
			if !compareOnly {
				lines = append(lines, fmt.Sprintf("    lsr w17, w17, #%d", sh))
			}
		} else {
			lines = append(lines, fmt.Sprintf("    %s w17, %s, %s", armOp, a, b))
		}
	}
	if flags {
		switch {
		case logical:
			l.cpol = carryClear
		case op == x86asm.ADD:
			l.cpol = carryAdd
		default:
			l.cpol = carrySub
		}
	}
	if compareOnly {
		return lines, nil
	}

	if dstIsMem {
		switch size {
		case 1:
			lines = append(lines, "    strb w17, [x28, x16]")
		case 2:
			lines = append(lines, "    strh w17, [x28, x16]")
		case 4:
			lines = append(lines, "    str w17, [x28, x16]")
		default:
			lines = append(lines, "    str x17, [x28, x16]")
		}
	} else if size < 4 {
		wc, _ := writeRegFrom(dstReg, "x17", "w17")
		lines = append(lines, wc...)
	}
	return lines, nil
}

// unaryOperand loads the operand of a one-operand integer instruction: the pinned register for
// 32/64-bit registers, else x17/w17 (8/16-bit values zero-extended; x16 keeps a memory
// operand's address). store writes a result register back.
func unaryOperand(inst disasm.Instruction, nextPC uint64) (load []string, val string, size int,
	store func(res string) []string, ok bool) {
	switch v := inst.Inst.Args[0].(type) {
	case x86asm.Reg:
		d, okD := arm64RegMap[v]
		if !okD {
			return nil, "", 0, nil, false
		}
		size = d.Size
		if size >= 4 {
			val = d.W
			if size == 8 {
				val = d.X
			}
			return nil, val, size, func(res string) []string {
				if res == val {
					return nil
				}
				return []string{fmt.Sprintf("    mov %s, %s", val, res)}
			}, true
		}
		load, _ = readRegTo(v, "x17", "w17")
		return load, "w17", size, func(res string) []string {
			wc, _ := writeRegFrom(v, "x"+res[1:], res)
			return wc
		}, true
	case x86asm.Mem:
		size = defMemSz(inst)
		load = readMemTo(v, nextPC, size, "x17", "w17")
		val = "w17"
		if size == 8 {
			val = "x17"
		}
		return load, val, size, func(res string) []string {
			switch size {
			case 1:
				return []string{fmt.Sprintf("    strb %s, [x28, x16]", res)}
			case 2:
				return []string{fmt.Sprintf("    strh %s, [x28, x16]", res)}
			}
			return []string{fmt.Sprintf("    str %s, [x28, x16]", res)}
		}, true
	}
	return nil, "", 0, nil, false
}

// liftIncDec handles INC and DEC. They do not change CF: 8/16-bit results set N/Z/V with
// SETF8/SETF16 (which keep C); 32/64-bit ADDS/SUBS are followed by restoring C when CF is live.
func (l *Arm64Lifter) liftIncDec(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	load, val, size, store, ok := unaryOperand(inst, nextPC)
	if !ok {
		lines = append(lines, l.emitUnsupported(inst, "unsupported inc/dec operand")...)
		return lines, nil
	}
	lines = append(lines, load...)
	arith := "add"
	if inst.Inst.Op == x86asm.DEC {
		arith = "sub"
	}
	flags := !inst.SkipFlags
	switch {
	case !flags:
		lines = append(lines, fmt.Sprintf("    %s %s, %s, #1", arith, val, val))
		lines = append(lines, store(val)...)
	case size < 4:
		ext, setf := "sxtb", "setf8"
		if size == 2 {
			ext, setf = "sxth", "setf16"
		}
		lines = append(lines,
			fmt.Sprintf("    %s w3, %s", ext, val),
			fmt.Sprintf("    %s w3, w3, #1", arith),
			fmt.Sprintf("    %s w3", setf))
		lines = append(lines, store("w3")...)
	default:
		keepC := !inst.CarryDead
		if keepC {
			lines = append(lines, "    cset w2, cs")
		}
		lines = append(lines, fmt.Sprintf("    %ss %s, %s, #1", arith, val, val))
		if keepC {
			lines = append(lines, "    rmif x2, #63, #2") // C = bit 0 of x2
		}
		lines = append(lines, store(val)...)
	}
	return lines, nil
}

// liftNegNot handles NEG and NOT. NEGS leaves C = (x == 0) = !CF (carrySub); 8/16-bit NEG runs
// on the top-aligned value so N/Z/C/V are those of the narrow result.
func (l *Arm64Lifter) liftNegNot(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	load, val, size, store, ok := unaryOperand(inst, nextPC)
	if !ok {
		lines = append(lines, l.emitUnsupported(inst, "unsupported neg/not operand")...)
		return lines, nil
	}
	lines = append(lines, load...)
	if inst.Inst.Op == x86asm.NOT {
		lines = append(lines, fmt.Sprintf("    mvn %s, %s", val, val))
		lines = append(lines, store(val)...)
		return lines, nil
	}
	if inst.SkipFlags {
		lines = append(lines, fmt.Sprintf("    neg %s, %s", val, val))
		lines = append(lines, store(val)...)
		return lines, nil
	}
	if size < 4 {
		sh := 32 - 8*size
		lines = append(lines,
			fmt.Sprintf("    lsl w3, %s, #%d", val, sh),
			"    negs w3, w3",
			fmt.Sprintf("    lsr w3, w3, #%d", sh))
		lines = append(lines, store("w3")...)
	} else {
		lines = append(lines, fmt.Sprintf("    negs %s, %s", val, val))
		lines = append(lines, store(val)...)
	}
	l.cpol = carrySub
	return lines, nil
}

// liftShift handles SHL, SHR, SAR, ROL, ROR with x86 semantics:
//   - the count is masked to 5 bits (6 for 64-bit operands); a masked count of 0 changes
//     neither the flags nor the value (a 32-bit register is still zero-extended);
//   - SHL/SHR/SAR: SF/ZF from the result, CF = last bit shifted out, OF for a count of 1
//     (SHL: MSB(result) ^ CF, SHR: MSB(source), SAR: 0);
//   - ROL/ROR change only CF (ROL: LSB(result), ROR: MSB(result)) and OF (ROL: MSB ^ CF,
//     ROR: the two top bits XORed); SF/ZF are kept.
//
// CF and OF are inserted into C and V with RMIF; SF/ZF come from CMN of the top-aligned result.
// A variable count keeps the current carry sense (the count may be 0); an immediate count
// leaves carryAdd.
func (l *Arm64Lifter) liftShift(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	load, val, size, store, ok := unaryOperand(inst, nextPC)
	if !ok {
		lines = append(lines, l.emitUnsupported(inst, fmt.Sprintf("unsupported %s operand", strings.ToLower(op.String())))...)
		return lines, nil
	}
	bits := 8 * size
	mask := 31
	if bits == 64 {
		mask = 63
	}
	immCount := -1
	switch c := inst.Inst.Args[1].(type) {
	case x86asm.Imm:
		immCount = int(c) & mask
	case x86asm.Reg:
		if c != x86asm.CL {
			lines = append(lines, l.emitUnsupported(inst, "shift count register")...)
			return lines, nil
		}
	default:
		lines = append(lines, l.emitUnsupported(inst, "shift count")...)
		return lines, nil
	}
	if immCount == 0 {
		return append(lines, zeroCountZext(inst)...), nil
	}
	rotate := op == x86asm.ROL || op == x86asm.ROR
	flags := !inst.SkipFlags
	needCO := !inst.CarryDead || !inst.OverflowDead
	if rotate && !needCO {
		flags = false
	}

	if size >= 4 && (!flags || (!needCO && !rotate)) {
		// Result straight into the operand; at most SF/ZF are needed (TST leaves C = V = 0).
		cnt := ""
		if immCount > 0 {
			r := immCount
			if rotate {
				r %= bits
				if op == x86asm.ROL {
					r = (bits - r) % bits
				}
			}
			cnt = fmt.Sprintf("#%d", r)
		} else {
			// ARM masks register counts to 5/6 bits like x86 does for 32/64-bit operands.
			cnt = val[:1] + "21"
		}
		lines = append(lines, load...)
		arm := map[x86asm.Op]string{x86asm.SHL: "lsl", x86asm.SHR: "lsr", x86asm.SAR: "asr", x86asm.ROL: "ror", x86asm.ROR: "ror"}[op]
		if immCount < 0 {
			lines = append(lines, fmt.Sprintf("    and w1, w21, #%d", mask)) // the flags test needs it
			if op == x86asm.ROL {
				lines = append(lines, fmt.Sprintf("    neg %s5, %s21", val[:1], val[:1]))
				cnt = val[:1] + "5"
			}
		}
		if !(rotate && immCount > 0 && cnt == "#0") {
			lines = append(lines, fmt.Sprintf("    %s %s, %s, %s", arm, val, val, cnt))
		}
		if flags {
			skip := ""
			if immCount < 0 {
				skip = fmt.Sprintf("L_shift_0x%x_%d", inst.Address, l.labelCounter)
				l.labelCounter++
				lines = append(lines, fmt.Sprintf("    cbz w1, %s", skip))
			}
			lines = append(lines, fmt.Sprintf("    tst %s, %s", val, val))
			if skip != "" {
				lines = append(lines, skip+":")
			}
			l.cpol = carryClear // CF is dead
		}
		lines = append(lines, store(val)...)
		return lines, nil
	}

	if immCount < 0 {
		lines = append(lines, fmt.Sprintf("    and w1, w21, #%d", mask)) // before CL can be written
	}
	lines = append(lines, load...)

	// xv: the source as a 64-bit value (zero-extended; sign-extended for SAR).
	xv := "x" + val[1:]
	switch {
	case op == x86asm.SAR && bits < 64:
		ext := map[int]string{8: "sxtb", 16: "sxth", 32: "sxtw"}[bits]
		lines = append(lines, fmt.Sprintf("    %s x4, %s", ext, val))
		xv = "x4"
	case bits == 32 && val != "w17":
		lines = append(lines, fmt.Sprintf("    mov w4, %s", val)) // pinned register: drop the upper half
		xv = "x4"
	}
	amt := func(n int) string { return fmt.Sprintf("#%d", n) }

	// Result into x0 (w0).
	switch op {
	case x86asm.SHL, x86asm.SHR, x86asm.SAR:
		arm := map[x86asm.Op]string{x86asm.SHL: "lsl", x86asm.SHR: "lsr", x86asm.SAR: "asr"}[op]
		if immCount > 0 {
			lines = append(lines, fmt.Sprintf("    %s x0, %s, %s", arm, xv, amt(immCount)))
		} else {
			lines = append(lines, fmt.Sprintf("    %s x0, %s, x1", arm, xv))
		}
	default: // ROL/ROR
		src := xv
		if bits < 32 {
			// Replicate the value over 32 bits; a 32-bit rotate then rotates the low bits.
			lines = append(lines, fmt.Sprintf("    orr w4, %s, %s, lsl #%d", val, val, bits))
			if bits == 8 {
				lines = append(lines, "    orr w4, w4, w4, lsl #16")
			}
			src = "x4"
		}
		rw := "w"
		if bits == 64 {
			rw = "x"
		}
		width := bits
		if bits < 32 {
			width = 32
		}
		if immCount > 0 {
			r := immCount % bits
			if op == x86asm.ROL {
				r = (bits - r) % bits
			}
			lines = append(lines, fmt.Sprintf("    ror %s0, %s%s, #%d", rw, rw, src[1:], r%width))
		} else {
			cnt := "1"
			if op == x86asm.ROL {
				lines = append(lines, fmt.Sprintf("    neg %s5, %s1", rw, rw))
				cnt = "5"
			}
			if bits < 32 {
				lines = append(lines, fmt.Sprintf("    and w5, w%s, #%d", cnt, bits-1))
				cnt = "5"
			}
			lines = append(lines, fmt.Sprintf("    ror %s0, %s%s, %s%s", rw, rw, src[1:], rw, cnt))
		}
	}

	if flags {
		skip := ""
		if immCount < 0 {
			skip = fmt.Sprintf("L_shift_0x%x_%d", inst.Address, l.labelCounter)
			l.labelCounter++
			lines = append(lines, fmt.Sprintf("    cbz w1, %s", skip))
		}
		setNZ := ""
		if !rotate {
			if bits == 64 {
				setNZ = "    cmn xzr, x0"
			} else {
				setNZ = fmt.Sprintf("    cmn wzr, w0, lsl #%d", 32-bits)
			}
		}
		if !needCO {
			// CF and OF are dead: only SF/ZF matter (CMN leaves C = V = 0).
			lines = append(lines, setNZ)
			l.cpol = carryClear
		} else {
			// x2 = CF, x3 = OF
			switch op {
			case x86asm.SHL:
				if bits < 64 {
					lines = append(lines,
						fmt.Sprintf("    ubfx x2, x0, #%d, #1", bits),
						"    eor x3, x0, x0, lsr #1",
						fmt.Sprintf("    ubfx x3, x3, #%d, #1", bits-1))
				} else {
					if immCount > 0 {
						lines = append(lines, fmt.Sprintf("    ubfx x2, %s, #%d, #1", xv, 64-immCount))
					} else {
						lines = append(lines, "    neg x5, x1", fmt.Sprintf("    lsr x2, %s, x5", xv), "    and x2, x2, #1")
					}
					lines = append(lines, "    eor x3, x2, x0, lsr #63")
				}
			case x86asm.SHR, x86asm.SAR:
				if immCount > 0 {
					lines = append(lines, fmt.Sprintf("    ubfx x2, %s, #%d, #1", xv, immCount-1))
				} else {
					arm := "lsr"
					if op == x86asm.SAR {
						arm = "asr"
					}
					lines = append(lines, "    sub x5, x1, #1", fmt.Sprintf("    %s x2, %s, x5", arm, xv), "    and x2, x2, #1")
				}
				if op == x86asm.SHR {
					lines = append(lines, fmt.Sprintf("    ubfx x3, %s, #%d, #1", xv, bits-1))
				} else {
					lines = append(lines, "    mov x3, #0")
				}
			case x86asm.ROL:
				lines = append(lines, "    and x2, x0, #1",
					fmt.Sprintf("    ubfx x3, x0, #%d, #1", bits-1), "    eor x3, x3, x2")
			case x86asm.ROR:
				lines = append(lines, fmt.Sprintf("    ubfx x2, x0, #%d, #1", bits-1),
					fmt.Sprintf("    ubfx x3, x0, #%d, #1", bits-2), "    eor x3, x3, x2")
			}
			outPol := carryAdd
			if immCount < 0 && l.cpol == carrySub {
				outPol = carrySub // the skipped path keeps C = !CF
			}
			if outPol == carrySub {
				lines = append(lines, "    eor x2, x2, #1")
			}
			lines = append(lines, "    orr x2, x3, x2, lsl #1")
			if setNZ != "" {
				lines = append(lines, setNZ)
			}
			lines = append(lines, "    rmif x2, #0, #3") // C = bit 1, V = bit 0
			l.cpol = outPol
		}
		if skip != "" {
			lines = append(lines, skip+":")
		}
	}

	res := "w0"
	if bits == 64 {
		res = "x0"
	}
	lines = append(lines, store(res)...)
	return lines, nil
}

// mulOverflowFlags sets x86 CF = OF = (bit 0 of w2) in C and V; SF/ZF are undefined after
// MUL/IMUL and are left as they are. The carry sense afterwards is carryAdd (C = CF).
func (l *Arm64Lifter) mulOverflowFlags(lines []string) []string {
	l.cpol = carryAdd
	return append(lines, "    orr w2, w2, w2, lsl #1", "    rmif x2, #0, #3")
}

// mulSource puts a multiplier operand in x1/w1 (a pinned register is returned as is).
func mulSource(arg x86asm.Arg, size int, nextPC uint64) ([]string, string, bool) {
	switch v := arg.(type) {
	case x86asm.Reg:
		d, ok := arm64RegMap[v]
		if !ok {
			return nil, "", false
		}
		switch size {
		case 8:
			return nil, d.X, true
		case 4:
			return nil, d.W, true
		}
		rc, _ := readRegTo(v, "x1", "w1")
		return rc, "w1", true
	case x86asm.Mem:
		if size == 8 {
			return readMemTo(v, nextPC, 8, "x1", "w1"), "x1", true
		}
		return readMemTo(v, nextPC, size, "x1", "w1"), "w1", true
	}
	return nil, "", false
}

// liftImul handles IMUL (one, two and three operands). CF = OF = 1 when the signed product does
// not fit the destination; they are computed only when one of them is live.
func (l *Arm64Lifter) liftImul(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	args := inst.Inst.Args
	flags := !inst.CarryDead || !inst.OverflowDead
	if args[1] == nil {
		// One operand: RDX:RAX (AX for 8-bit) = RAX * src, signed.
		size := defMemSz(inst)
		if r, ok := args[0].(x86asm.Reg); ok {
			d, okD := arm64RegMap[r]
			if !okD {
				lines = append(lines, l.emitUnsupported(inst, "unmapped imul src")...)
				return lines, nil
			}
			size = d.Size
		}
		ld, src, ok := mulSource(args[0], size, nextPC)
		if !ok {
			lines = append(lines, l.emitUnsupported(inst, "unsupported imul operand")...)
			return lines, nil
		}
		lines = append(lines, ld...)
		switch size {
		case 1, 2:
			ext := "sxtb"
			if size == 2 {
				ext = "sxth"
			}
			lines = append(lines, fmt.Sprintf("    %s w1, %s", ext, src), fmt.Sprintf("    %s w0, w19", ext),
				"    mul w0, w0, w1")
			if size == 1 {
				lines = append(lines, "    bfi x19, x0, #0, #16") // AX
			} else {
				lines = append(lines, "    bfi x19, x0, #0, #16", "    lsr w1, w0, #16", "    bfi x22, x1, #0, #16")
			}
			if flags {
				lines = append(lines, fmt.Sprintf("    cmp w0, w0, %s", ext), "    cset w2, ne")
				lines = l.mulOverflowFlags(lines)
			}
		case 4:
			lines = append(lines, fmt.Sprintf("    smull x0, w19, %s", src), "    mov w19, w0", "    lsr x22, x0, #32")
			if flags {
				lines = append(lines, "    cmp x0, w0, sxtw", "    cset w2, ne")
				lines = l.mulOverflowFlags(lines)
			}
		default:
			lines = append(lines, fmt.Sprintf("    mul x0, x19, %s", src), fmt.Sprintf("    smulh x22, x19, %s", src),
				"    mov x19, x0")
			if flags {
				lines = append(lines, "    cmp x22, x0, asr #63", "    cset w2, ne")
				lines = l.mulOverflowFlags(lines)
			}
		}
		return lines, nil
	}

	dstReg, okDst := args[0].(x86asm.Reg)
	d, okD := arm64RegMap[dstReg]
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported imul dst")...)
		return lines, nil
	}
	size := d.Size
	// Two operands: dst = dst * src. Three operands: dst = src * imm.
	var a, b string
	if args[2] == nil {
		ld, src, ok := mulSource(args[1], size, nextPC)
		if !ok {
			lines = append(lines, l.emitUnsupported(inst, "unsupported imul src")...)
			return lines, nil
		}
		lines = append(lines, ld...)
		b = src
		switch size {
		case 8:
			a = d.X
		case 4:
			a = d.W
		default:
			rc, _ := readRegTo(dstReg, "x0", "w0")
			lines = append(lines, rc...)
			a = "w0"
		}
	} else {
		imm, okImm := args[2].(x86asm.Imm)
		ld, src, ok := mulSource(args[1], size, nextPC)
		if !okImm || !ok {
			lines = append(lines, l.emitUnsupported(inst, "unsupported imul operands")...)
			return lines, nil
		}
		lines = append(lines, ld...)
		a = src
		// The immediate is sign-extended to the operand size.
		if size == 8 {
			lines = append(lines, loadImm64("x3", uint64(int64(imm)))...)
			b = "x3"
		} else {
			lines = append(lines, loadImm32("w3", uint32(int64(imm)))...)
			b = "w3"
		}
	}
	switch {
	case !flags && size >= 4:
		// The low half of the product does not depend on the signedness.
		if size == 8 {
			lines = append(lines, fmt.Sprintf("    mul %s, %s, %s", d.X, a, b))
		} else {
			lines = append(lines, fmt.Sprintf("    mul %s, %s, %s", d.W, a, b))
		}
	case size == 8:
		lines = append(lines, fmt.Sprintf("    mul x0, %s, %s", a, b))
		lines = append(lines, fmt.Sprintf("    smulh x2, %s, %s", a, b), "    cmp x2, x0, asr #63", "    cset w2, ne")
		lines = append(lines, fmt.Sprintf("    mov %s, x0", d.X))
	case size == 4:
		lines = append(lines, fmt.Sprintf("    smull x0, %s, %s", a, b))
		lines = append(lines, "    cmp x0, w0, sxtw", "    cset w2, ne")
		lines = append(lines, fmt.Sprintf("    mov %s, w0", d.W))
	default: // 16-bit
		lines = append(lines, fmt.Sprintf("    sxth w4, %s", a), fmt.Sprintf("    sxth w5, %s", b), "    mul w0, w4, w5")
		if flags {
			lines = append(lines, "    cmp w0, w0, sxth", "    cset w2, ne")
		}
		wc, _ := writeRegFrom(dstReg, "x0", "w0")
		lines = append(lines, wc...)
	}
	if flags {
		lines = l.mulOverflowFlags(lines)
	}
	return lines, nil
}

// liftMul handles unsigned MUL: RDX:RAX (AX for 8-bit) = RAX * src; CF = OF = (high half != 0).
func (l *Arm64Lifter) liftMul(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	flags := !inst.CarryDead || !inst.OverflowDead
	size := defMemSz(inst)
	if r, ok := inst.Inst.Args[0].(x86asm.Reg); ok {
		d, okD := arm64RegMap[r]
		if !okD {
			lines = append(lines, l.emitUnsupported(inst, "unmapped mul src")...)
			return lines, nil
		}
		size = d.Size
	}
	ld, src, ok := mulSource(inst.Inst.Args[0], size, nextPC)
	if !ok {
		lines = append(lines, l.emitUnsupported(inst, "unsupported mul operand")...)
		return lines, nil
	}
	lines = append(lines, ld...)
	switch size {
	case 1:
		lines = append(lines, "    and w0, w19, #0xff", fmt.Sprintf("    mul w0, w0, %s", src), "    bfi x19, x0, #0, #16")
		if flags {
			lines = append(lines, "    ubfx w2, w0, #8, #8")
		}
	case 2:
		lines = append(lines, "    and w0, w19, #0xffff", fmt.Sprintf("    mul w0, w0, %s", src),
			"    bfi x19, x0, #0, #16", "    lsr w2, w0, #16", "    bfi x22, x2, #0, #16")
	case 4:
		lines = append(lines, fmt.Sprintf("    umull x0, w19, %s", src), "    mov w19, w0", "    lsr x22, x0, #32")
		if flags {
			lines = append(lines, "    mov x2, x22")
		}
	default:
		lines = append(lines, fmt.Sprintf("    mul x0, x19, %s", src), fmt.Sprintf("    umulh x22, x19, %s", src),
			"    mov x19, x0")
		if flags {
			lines = append(lines, "    mov x2, x22")
		}
	}
	if flags {
		lines = append(lines, "    cmp x2, #0", "    cset w2, ne")
		lines = l.mulOverflowFlags(lines)
	}
	return lines, nil
}

// liftDiv handles IDIV and DIV.
func (l *Arm64Lifter) liftDiv(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	isSigned := inst.Inst.Op == x86asm.IDIV
	divOp := "udiv"
	if isSigned {
		divOp = "sdiv"
	}

	if reg, ok := inst.Inst.Args[0].(x86asm.Reg); ok {
		if desc, okDesc := arm64RegMap[reg]; okDesc {
			switch desc.Size {
			case 1:
				rc, _ := readRegTo(reg, "x17", "w17")
				lines = append(lines, rc...)
				if isSigned {
					lines = append(lines, "    sxth w16, w19")
					lines = append(lines, "    sxtb w17, w17")
				} else {
					lines = append(lines, "    uxth w16, w19")
				}
				lines = append(lines, fmt.Sprintf("    %s w0, w16, w17", divOp))
				lines = append(lines, "    msub w1, w0, w17, w16")
				lines = append(lines, "    bfi x19, x0, #0, #8")
				lines = append(lines, "    bfi x19, x1, #8, #8")
			case 2:
				rc, _ := readRegTo(reg, "x17", "w17")
				lines = append(lines, rc...)
				lines = append(lines, "    and w16, w19, #0xffff")
				lines = append(lines, "    bfi w16, w22, #16, #16")
				if isSigned {
					lines = append(lines, "    sxth w17, w17")
				}
				lines = append(lines, fmt.Sprintf("    %s w0, w16, w17", divOp))
				lines = append(lines, "    msub w1, w0, w17, w16")
				lines = append(lines, "    bfi x19, x0, #0, #16")
				lines = append(lines, "    bfi x22, x1, #0, #16")
			case 4:
				lines = append(lines, fmt.Sprintf("    %s w16, w19, %s", divOp, desc.W))
				lines = append(lines, fmt.Sprintf("    msub w22, w16, %s, w19", desc.W))
				lines = append(lines, "    mov w19, w16")
			default: // 8
				lines = append(lines, fmt.Sprintf("    %s x16, x19, %s", divOp, desc.X))
				lines = append(lines, fmt.Sprintf("    msub x22, x16, %s, x19", desc.X))
				lines = append(lines, "    mov x19, x16")
			}
			return lines, nil
		}
	} else if mem, ok := inst.Inst.Args[0].(x86asm.Mem); ok {
		sz := defMemSz(inst)
		lines = append(lines, readMemTo(mem, nextPC, sz, "x17", "w17")...)
		switch sz {
		case 1:
			if isSigned {
				lines = append(lines, "    sxth w16, w19")
				lines = append(lines, "    sxtb w17, w17")
			} else {
				lines = append(lines, "    uxth w16, w19")
			}
			lines = append(lines, fmt.Sprintf("    %s w0, w16, w17", divOp))
			lines = append(lines, "    msub w1, w0, w17, w16")
			lines = append(lines, "    bfi x19, x0, #0, #8")
			lines = append(lines, "    bfi x19, x1, #8, #8")
		case 2:
			lines = append(lines, "    and w16, w19, #0xffff")
			lines = append(lines, "    bfi w16, w22, #16, #16")
			if isSigned {
				lines = append(lines, "    sxth w17, w17")
			}
			lines = append(lines, fmt.Sprintf("    %s w0, w16, w17", divOp))
			lines = append(lines, "    msub w1, w0, w17, w16")
			lines = append(lines, "    bfi x19, x0, #0, #16")
			lines = append(lines, "    bfi x22, x1, #0, #16")
		case 4:
			lines = append(lines, fmt.Sprintf("    %s w16, w19, w17", divOp))
			lines = append(lines, "    msub w22, w16, w17, w19")
			lines = append(lines, "    mov w19, w16")
		default: // 8
			lines = append(lines, fmt.Sprintf("    %s x16, x19, x17", divOp))
			lines = append(lines, "    msub x22, x16, x17, x19")
			lines = append(lines, "    mov x19, x16")
		}
		return lines, nil
	}
	lines = append(lines, "    brk #0 // unsupported div")
	return lines, nil
}

// liftXadd handles XADD: TEMP = DEST + SRC; SRC = DEST; DEST = TEMP, with ADD flags (8/16-bit
// from top-aligned operands; carryAdd). The memory form is atomic (liftAtomicXadd).
func (l *Arm64Lifter) liftXadd(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	srcReg, okSrc := inst.Inst.Args[1].(x86asm.Reg)
	srcDesc, okSrcDesc := arm64RegMap[srcReg]
	if !okSrc || !okSrcDesc {
		lines = append(lines, l.emitUnsupported(inst, "unsupported xadd src")...)
		return lines, nil
	}
	sz := srcDesc.Size
	r := "w"
	if sz == 8 {
		r = "x"
	}
	if mem, okMem := dst.(x86asm.Mem); okMem {
		return l.liftAtomicXadd(inst, mem, srcReg, nextPC, lines), nil
	}
	rc, _ := readRegTo(srcReg, "x2", "w2")
	lines = append(lines, rc...)
	flags := func() {
		if inst.SkipFlags {
			return
		}
		if sz < 4 {
			sh := 32 - 8*sz
			lines = append(lines, fmt.Sprintf("    lsl w3, w17, #%d", sh), fmt.Sprintf("    adds w3, w3, w2, lsl #%d", sh))
		} else {
			lines = append(lines, fmt.Sprintf("    adds %s3, %s17, %s2", r, r, r))
		}
		l.cpol = carryAdd
	}
	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		if _, ok := arm64RegMap[dstReg]; ok {
			rd, _ := readRegTo(dstReg, "x17", "w17")
			lines = append(lines, rd...)
			lines = append(lines, fmt.Sprintf("    add %s0, %s17, %s2", r, r, r))
			flags()
			ws, _ := writeRegFrom(srcReg, "x17", "w17")
			wd, _ := writeRegFrom(dstReg, "x0", "w0")
			lines = append(lines, ws...)
			lines = append(lines, wd...)
			return lines, nil
		}
	}
	lines = append(lines, l.emitUnsupported(inst, "unsupported xadd")...)
	return lines, nil
}

// liftCmpxchg handles CMPXCHG. The memory form is atomic (liftAtomicCmpxchg).
func (l *Arm64Lifter) liftCmpxchg(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]
	sz := defMemSz(inst)

	srcReg, okSrc := src.(x86asm.Reg)
	if !okSrc {
		lines = append(lines, "    brk #0 // unsupported cmpxchg src")
		return lines, nil
	}
	srcDesc, okSrcDesc := arm64RegMap[srcReg]
	if !okSrcDesc {
		lines = append(lines, "    brk #0 // unmapped cmpxchg src")
		return lines, nil
	}
	if srcDesc.Size > 0 {
		sz = srcDesc.Size
	}

	if mem, okMem := dst.(x86asm.Mem); okMem {
		return l.liftAtomicCmpxchg(mem, srcReg, nextPC, lines), nil
	}

	l.cpol = carrySub // flags of CMP accumulator, destination
	l.labelCounter++
	failLbl := fmt.Sprintf("L_cmpxchg_fail_%d", l.labelCounter)
	endLbl := fmt.Sprintf("L_cmpxchg_end_%d", l.labelCounter)

	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		if dstDesc, okDstDesc := arm64RegMap[dstReg]; okDstDesc {
			switch sz {
			case 1:
				lines = append(lines, "    lsl w0, w19, #24")
				lines = append(lines, fmt.Sprintf("    cmp w0, %s, lsl #24", dstDesc.W))
				lines = append(lines, fmt.Sprintf("    b.ne %s", failLbl))
				lines = append(lines, fmt.Sprintf("    bfi %s, %s, #0, #8", dstDesc.X, srcDesc.X))
				lines = append(lines, fmt.Sprintf("    b %s", endLbl))
				lines = append(lines, fmt.Sprintf("%s:", failLbl))
				lines = append(lines, fmt.Sprintf("    bfi x19, %s, #0, #8", dstDesc.X))
				lines = append(lines, fmt.Sprintf("%s:", endLbl))
			case 2:
				lines = append(lines, "    lsl w0, w19, #16")
				lines = append(lines, fmt.Sprintf("    cmp w0, %s, lsl #16", dstDesc.W))
				lines = append(lines, fmt.Sprintf("    b.ne %s", failLbl))
				lines = append(lines, fmt.Sprintf("    bfi %s, %s, #0, #16", dstDesc.X, srcDesc.X))
				lines = append(lines, fmt.Sprintf("    b %s", endLbl))
				lines = append(lines, fmt.Sprintf("%s:", failLbl))
				lines = append(lines, fmt.Sprintf("    bfi x19, %s, #0, #16", dstDesc.X))
				lines = append(lines, fmt.Sprintf("%s:", endLbl))
			case 4:
				lines = append(lines, fmt.Sprintf("    cmp w19, %s", dstDesc.W))
				lines = append(lines, fmt.Sprintf("    b.ne %s", failLbl))
				lines = append(lines, fmt.Sprintf("    mov %s, %s", dstDesc.W, srcDesc.W))
				lines = append(lines, fmt.Sprintf("    b %s", endLbl))
				lines = append(lines, fmt.Sprintf("%s:", failLbl))
				lines = append(lines, fmt.Sprintf("    mov w19, %s", dstDesc.W))
				lines = append(lines, fmt.Sprintf("%s:", endLbl))
			default:
				lines = append(lines, fmt.Sprintf("    cmp x19, %s", dstDesc.X))
				lines = append(lines, fmt.Sprintf("    b.ne %s", failLbl))
				lines = append(lines, fmt.Sprintf("    mov %s, %s", dstDesc.X, srcDesc.X))
				lines = append(lines, fmt.Sprintf("    b %s", endLbl))
				lines = append(lines, fmt.Sprintf("%s:", failLbl))
				lines = append(lines, fmt.Sprintf("    mov x19, %s", dstDesc.X))
				lines = append(lines, fmt.Sprintf("%s:", endLbl))
			}
			return lines, nil
		}
	}

	lines = append(lines, "    brk #0 // unsupported cmpxchg")
	return lines, nil
}

// liftXchg handles XCHG. The memory form is atomic (implicit LOCK, liftAtomicXchg); 8/16-bit
// registers (including AH-DH) exchange only their own bits.
func (l *Arm64Lifter) liftXchg(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	// Reg <-> Reg
	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		if srcReg, okSrc := src.(x86asm.Reg); okSrc {
			dDesc, ok1 := arm64RegMap[dstReg]
			sDesc, ok2 := arm64RegMap[srcReg]
			if !ok1 || !ok2 {
				lines = append(lines, l.emitUnsupported(inst, "unmapped xchg register")...)
				return lines, nil
			}
			switch dDesc.Size {
			case 8:
				lines = append(lines, fmt.Sprintf("    mov x16, %s", dDesc.X),
					fmt.Sprintf("    mov %s, %s", dDesc.X, sDesc.X), fmt.Sprintf("    mov %s, x16", sDesc.X))
			case 4:
				lines = append(lines, fmt.Sprintf("    mov w16, %s", dDesc.W),
					fmt.Sprintf("    mov %s, %s", dDesc.W, sDesc.W), fmt.Sprintf("    mov %s, w16", sDesc.W))
			default:
				r1, _ := readRegTo(dstReg, "x16", "w16")
				r2, _ := readRegTo(srcReg, "x17", "w17")
				w1, _ := writeRegFrom(dstReg, "x17", "w17")
				w2, _ := writeRegFrom(srcReg, "x16", "w16")
				lines = append(lines, r1...)
				lines = append(lines, r2...)
				lines = append(lines, w1...)
				lines = append(lines, w2...)
			}
			return lines, nil
		}
	}

	// Mem <-> Reg (x86asm puts the memory operand first or second)
	mem, okMem := dst.(x86asm.Mem)
	regArg := src
	if !okMem {
		mem, okMem = src.(x86asm.Mem)
		regArg = dst
	}
	reg, okReg := regArg.(x86asm.Reg)
	if okMem && okReg {
		if _, ok := arm64RegMap[reg]; !ok {
			lines = append(lines, l.emitUnsupported(inst, "unmapped xchg register")...)
			return lines, nil
		}
		return l.liftAtomicXchg(mem, reg, nextPC, lines), nil
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported xchg")...)
	return lines, nil
}

// liftAdcSbb handles ADC and SBB.
//
// 32/64-bit: ADCS needs C = CF and SBCS needs C = !CF, so C is inverted first when the tracked
// sense differs; the result sense is then carryAdd (ADC) or carrySub (SBB).
// 8/16-bit: ADCS/SBCS cannot add the carry at the bottom of top-aligned operands, so the
// result and N/Z/V come from sign-extended operands (SETF8/SETF16, which keep C) and CF from
// the zero-extended sum, inserted into C with RMIF.
func (l *Arm64Lifter) liftAdcSbb(inst disasm.Instruction, nextPC uint64, isSbb bool, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]
	flags := !inst.SkipFlags

	size := 0
	dstReg, dstIsReg := dst.(x86asm.Reg)
	dstMem, dstIsMem := dst.(x86asm.Mem)
	if dstIsReg {
		d, ok := arm64RegMap[dstReg]
		if !ok {
			lines = append(lines, l.emitUnsupported(inst, "unmapped adc/sbb dst")...)
			return lines, nil
		}
		size = d.Size
	} else if dstIsMem {
		size = defMemSz(inst)
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported adc/sbb operand")...)
		return lines, nil
	}

	// a: destination value in x17/w17 (zero-extended for 8/16-bit); x16 keeps a memory
	// destination's address. b: source value in x0/w0 (zero-extended for 8/16-bit).
	if dstIsMem {
		lines = append(lines, readMemTo(dstMem, nextPC, size, "x17", "w17")...)
	} else if size < 4 {
		rc, _ := readRegTo(dstReg, "x17", "w17")
		lines = append(lines, rc...)
	}
	bReg := "" // a 32/64-bit source used in place (pinned register or zero register)
	switch v := src.(type) {
	case x86asm.Imm:
		val := uint64(int64(v))
		switch size {
		case 1:
			val &= 0xff
		case 2:
			val &= 0xffff
		case 4:
			val &= 0xffffffff
		}
		if val == 0 && size >= 4 {
			bReg = "zr"
		} else {
			lines = append(lines, loadImm64("x0", val)...)
		}
	case x86asm.Reg:
		if d, ok := arm64RegMap[v]; ok && size >= 4 {
			bReg = d.X[1:]
			break
		}
		rc, ok := readRegTo(v, "x0", "w0")
		if !ok {
			lines = append(lines, l.emitUnsupported(inst, "unmapped adc/sbb src")...)
			return lines, nil
		}
		lines = append(lines, rc...)
	case x86asm.Mem:
		if dstIsMem {
			lines = append(lines, l.emitUnsupported(inst, "adc/sbb mem, mem")...)
			return lines, nil
		}
		lines = append(lines, readMemTo(v, nextPC, size, "x0", "w0")...)
	default:
		lines = append(lines, l.emitUnsupported(inst, "unsupported adc/sbb source")...)
		return lines, nil
	}

	res := "x17"
	if size <= 4 {
		res = "w17"
	}
	if dstIsReg && size >= 4 {
		// Operate on the pinned register directly.
		res = arm64RegMap[dstReg].X
		if size == 4 {
			res = arm64RegMap[dstReg].W
		}
	}
	if size >= 4 && dstIsReg && inst.SkipFlags {
		// Flags dead: the common idioms need no carry inversion.
		d := arm64RegMap[dstReg]
		r := d.X
		if size == 4 {
			r = d.W
		}
		if !isSbb && bReg == "zr" {
			lines = append(lines, fmt.Sprintf("    cinc %s, %s, %s", r, r, l.carryCond(true))) // adc r, 0
			return lines, nil
		}
		if s, isReg := src.(x86asm.Reg); isSbb && isReg && arm64RegMap[s].X == d.X {
			lines = append(lines, fmt.Sprintf("    csetm %s, %s", r, l.carryCond(true))) // sbb r, r = -CF
			return lines, nil
		}
	}
	if size >= 4 {
		if bReg == "" {
			bReg = "0"
		}
		b := "x" + bReg
		if size == 4 {
			b = "w" + bReg
		}
		op := "adc"
		if isSbb {
			op = "sbc"
			if l.cpol != carrySub {
				lines = append(lines, l.normalizeCarry()...)
			}
		} else if l.cpol == carrySub {
			lines = append(lines, "    cfinv")
			l.cpol = carryAdd
		} else {
			l.cpol = carryAdd // carryClear: C = 0 = CF
		}
		if flags {
			op += "s"
		}
		lines = append(lines, fmt.Sprintf("    %s %s, %s, %s", op, res, res, b))
	} else {
		bits := 8 * size
		ext := "sxtb"
		setf := "setf8"
		if size == 2 {
			ext, setf = "sxth", "setf16"
		}
		lines = append(lines, fmt.Sprintf("    cset w2, %s", l.carryCond(true)))
		arith := "add"
		if isSbb {
			arith = "sub"
		}
		if flags {
			lines = append(lines,
				fmt.Sprintf("    %s w3, w17", ext),
				fmt.Sprintf("    %s w4, w0", ext),
				fmt.Sprintf("    %s w3, w3, w4", arith),
				fmt.Sprintf("    %s w3, w3, w2", arith),
				fmt.Sprintf("    %s w3", setf), // N, Z, V of the narrow result; C kept
				fmt.Sprintf("    %s w4, w17, w0", arith),
				fmt.Sprintf("    %s w4, w4, w2", arith),
			)
			if isSbb {
				// borrow = sign of the zero-extended difference; C = !borrow
				lines = append(lines, "    lsr w4, w4, #31", "    eor w4, w4, #1")
				l.cpol = carrySub
			} else {
				lines = append(lines, fmt.Sprintf("    ubfx w4, w4, #%d, #1", bits))
				l.cpol = carryAdd
			}
			lines = append(lines, "    rmif x4, #63, #2") // C = bit 0 of x4
		} else {
			lines = append(lines,
				fmt.Sprintf("    %s w3, w17, w0", arith),
				fmt.Sprintf("    %s w3, w3, w2", arith),
			)
		}
		res = "w3"
	}

	resX := "x" + res[1:]
	if dstIsReg && size >= 4 {
		return lines, nil
	}
	if dstIsMem {
		switch size {
		case 1:
			lines = append(lines, fmt.Sprintf("    strb %s, [x28, x16]", res))
		case 2:
			lines = append(lines, fmt.Sprintf("    strh %s, [x28, x16]", res))
		case 4:
			lines = append(lines, fmt.Sprintf("    str %s, [x28, x16]", res))
		default:
			lines = append(lines, fmt.Sprintf("    str %s, [x28, x16]", resX))
		}
	} else {
		wc, _ := writeRegFrom(dstReg, resX, "w"+res[1:])
		lines = append(lines, wc...)
	}
	return lines, nil
}

// liftBt handles BT, BTS, BTR, BTC. Only CF changes (ZF is kept, the others are undefined), so
// the tested bit is inserted into C with RMIF (carryAdd) and nothing else in NZCV is touched.
// With a register bit offset and a memory operand the offset is signed and selects the operand
// at addr + (offset >> log2(bits)) * size, as on x86.
func (l *Arm64Lifter) liftBt(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	size := 0
	v := "" // the operand value
	var store func() []string
	switch d := dst.(type) {
	case x86asm.Reg:
		desc, ok := arm64RegMap[d]
		if !ok {
			lines = append(lines, l.emitUnsupported(inst, "unmapped bt dst")...)
			return lines, nil
		}
		size = desc.Size
		switch size {
		case 8:
			v = desc.X
		case 4:
			v = desc.W
		default:
			rc, _ := readRegTo(d, "x17", "w17")
			lines = append(lines, rc...)
			v = "w17"
			store = func() []string { wc, _ := writeRegFrom(d, "x17", "w17"); return wc }
		}
	case x86asm.Mem:
		size = defMemSz(inst)
		lines = append(lines, computeEffAddr(d, nextPC, "x16")...)
		v = "w17"
		if size == 8 {
			v = "x17"
		}
		store = func() []string {
			st := map[int]string{1: "strb", 2: "strh", 4: "str", 8: "str"}[size]
			return []string{fmt.Sprintf("    %s %s, [x28, x16]", st, v)}
		}
	default:
		lines = append(lines, l.emitUnsupported(inst, "unsupported bt operand")...)
		return lines, nil
	}
	bits := 8 * size
	log2 := map[int]int{16: 4, 32: 5, 64: 6}[bits]
	x := "x" + v[1:]

	imm, isImm := src.(x86asm.Imm)
	k := 0
	if isImm {
		k = int(uint64(imm) % uint64(bits))
	} else {
		r, ok := src.(x86asm.Reg)
		desc, okD := arm64RegMap[r]
		if !ok || !okD {
			lines = append(lines, l.emitUnsupported(inst, "unsupported bt bit offset")...)
			return lines, nil
		}
		if _, isMem := dst.(x86asm.Mem); isMem {
			// Signed bit offset: move the address by whole operands, keep the bit within one.
			switch desc.Size {
			case 8:
				lines = append(lines, fmt.Sprintf("    mov x4, %s", desc.X))
			case 4:
				lines = append(lines, fmt.Sprintf("    sxtw x4, %s", desc.W))
			default:
				lines = append(lines, fmt.Sprintf("    sxth x4, %s", desc.W))
			}
			lines = append(lines, fmt.Sprintf("    asr x5, x4, #%d", log2),
				fmt.Sprintf("    add x16, x16, x5, lsl #%d", log2-3),
				fmt.Sprintf("    and x1, x4, #%d", bits-1))
		} else {
			lines = append(lines, fmt.Sprintf("    and w1, %s, #%d", desc.W, bits-1))
		}
	}
	if _, isMem := dst.(x86asm.Mem); isMem {
		ld := map[int]string{1: "ldrb", 2: "ldrh", 4: "ldr", 8: "ldr"}[size]
		lines = append(lines, fmt.Sprintf("    %s %s, [x28, x16]", ld, v))
	}

	if !inst.CarryDead {
		if isImm {
			lines = append(lines, fmt.Sprintf("    ubfx x2, %s, #%d, #1", x, k))
		} else {
			lines = append(lines, fmt.Sprintf("    lsr x2, %s, x1", x))
		}
		lines = append(lines, "    rmif x2, #63, #2") // C = tested bit (bit 0 of x2)
		l.cpol = carryAdd
	}
	if op == x86asm.BT {
		return lines, nil
	}
	arm := map[x86asm.Op]string{x86asm.BTS: "orr", x86asm.BTR: "bic", x86asm.BTC: "eor"}[op]
	reg32 := size <= 4
	if isImm {
		if arm == "bic" {
			if reg32 {
				lines = append(lines, fmt.Sprintf("    and %s, %s, #0x%x", v, v, ^(uint32(1)<<k)))
			} else {
				lines = append(lines, fmt.Sprintf("    and %s, %s, #0x%x", v, v, ^(uint64(1)<<k)))
			}
		} else {
			lines = append(lines, fmt.Sprintf("    %s %s, %s, #0x%x", arm, v, v, uint64(1)<<k))
		}
	} else {
		lines = append(lines, "    mov x3, #1", "    lsl x3, x3, x1", fmt.Sprintf("    %s %s, %s, %s", arm, x, x, "x3"))
		if size == 4 && v != "w17" {
			lines = append(lines, fmt.Sprintf("    mov %s, %s", v, v)) // 32-bit write: clear the upper half
		}
	}
	if store != nil {
		lines = append(lines, store()...)
	}
	return lines, nil
}

// liftMovbe handles MOVBE instruction (move with byte swap).
func (l *Arm64Lifter) liftMovbe(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	src := inst.Inst.Args[1]

	// Reg <- Mem (load and swap)
	if dstReg, okDst := dst.(x86asm.Reg); okDst {
		if mem, okMem := src.(x86asm.Mem); okMem {
			descDst, okDesc := arm64RegMap[dstReg]
			if okDesc {
				lines = append(lines, readMemTo(mem, nextPC, descDst.Size, "x16", "w16")...)
				switch descDst.Size {
				case 2:
					lines = append(lines, "    rev16 w16, w16")
				case 4:
					lines = append(lines, "    rev w16, w16")
				default: // 8
					lines = append(lines, "    rev x16, x16")
				}
				storeCode, _ := writeRegFrom(dstReg, "x16", "w16")
				lines = append(lines, storeCode...)
				return lines, nil
			}
		}
	}

	// Mem <- Reg (swap and store)
	if mem, okMem := dst.(x86asm.Mem); okMem {
		if srcReg, okSrc := src.(x86asm.Reg); okSrc {
			descSrc, okDesc := arm64RegMap[srcReg]
			if okDesc {
				loadCode, _ := readRegTo(srcReg, "x17", "w17")
				lines = append(lines, loadCode...)
				switch descSrc.Size {
				case 2:
					lines = append(lines, "    rev16 w17, w17")
				case 4:
					lines = append(lines, "    rev w17, w17")
				default: // 8
					lines = append(lines, "    rev x17, x17")
				}
				lines = append(lines, writeMemFrom(mem, nextPC, descSrc.Size, "x17", "w17")...)
				return lines, nil
			}
		}
	}

	lines = append(lines, l.emitUnsupported(inst, "unsupported movbe operands")...)
	return lines, nil
}

// guestTscFreq is the TSC frequency the guest sees: RECOMP_TSC_FREQ in
// pkg/runtime/core/recomp_context.h, which sceKernelGetTscFrequency reports and
// sceKernelReadTsc counts in (TestRdtscMatchesRuntimeTsc checks that they agree).
const guestTscFreq = 2130000000

// liftRdtsc handles RDTSC (Read Time-Stamp Counter): the host counter scaled to guestTscFreq,
// floor(cntvct * F / cntfrq) as recomp_rdtsc computes it, without a 128-bit product:
// q*F + (r*F)/cntfrq with q, r = cntvct divmod cntfrq (r*F < cntfrq*F fits in 64 bits).
// cntfrq_el0 is never 0 on Apple silicon (24 MHz, set by the firmware), so the divisions are
// safe. RDTSCP (which would also write ECX = TSC_AUX) has no site in TLOU or U4 and is not lifted.
// Flags are not touched. Scratch: x0-x2, x16, x17.
func (l *Arm64Lifter) liftRdtsc(lines []string) []string {
	lines = append(lines, "    mrs x16, cntvct_el0")
	lines = append(lines, "    mrs x17, cntfrq_el0")
	lines = append(lines, "    udiv x0, x16, x17")
	lines = append(lines, "    msub x1, x0, x17, x16")
	lines = append(lines, loadImm64("x2", guestTscFreq)...)
	lines = append(lines, "    mul x0, x0, x2")
	lines = append(lines, "    mul x1, x1, x2")
	lines = append(lines, "    udiv x1, x1, x17")
	lines = append(lines, "    add x16, x0, x1")
	lines = append(lines, "    mov w19, w16")
	lines = append(lines, "    lsr x16, x16, #32")
	lines = append(lines, "    mov w22, w16")
	return lines
}

// bitSource loads the source of a bit-count instruction into x0/w0, zero-extended.
func bitSource(arg x86asm.Arg, size int, nextPC uint64) ([]string, bool) {
	switch v := arg.(type) {
	case x86asm.Reg:
		rc, ok := readRegTo(v, "x0", "w0")
		return rc, ok
	case x86asm.Mem:
		return readMemTo(v, nextPC, size, "x0", "w0"), true
	}
	return nil, false
}

// liftPopcnt handles POPCNT with a scalar bit count (no vector scratch register). ZF = (src ==
// 0); CF, OF, SF, PF are cleared: TST of the count gives exactly that (carryClear).
func (l *Arm64Lifter) liftPopcnt(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	descDst, okDD := arm64RegMap[dstReg]
	if !okDst || !okDD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported popcnt dst")...)
		return lines, nil
	}
	sz := descDst.Size
	ld, ok := bitSource(inst.Inst.Args[1], sz, nextPC)
	if !ok {
		lines = append(lines, l.emitUnsupported(inst, "unsupported popcnt src")...)
		return lines, nil
	}
	lines = append(lines, ld...)
	r := "x"
	if sz < 8 {
		r = "w"
	}
	lines = append(lines,
		fmt.Sprintf("    lsr %s1, %s0, #1", r, r),
		fmt.Sprintf("    and %s1, %s1, #0x55555555%s", r, r, map[string]string{"x": "55555555", "w": ""}[r]),
		fmt.Sprintf("    sub %s0, %s0, %s1", r, r, r),
		fmt.Sprintf("    and %s1, %s0, #0x33333333%s", r, r, map[string]string{"x": "33333333", "w": ""}[r]),
		fmt.Sprintf("    lsr %s0, %s0, #2", r, r),
		fmt.Sprintf("    and %s0, %s0, #0x33333333%s", r, r, map[string]string{"x": "33333333", "w": ""}[r]),
		fmt.Sprintf("    add %s0, %s0, %s1", r, r, r),
		fmt.Sprintf("    add %s0, %s0, %s0, lsr #4", r, r, r),
		fmt.Sprintf("    and %s0, %s0, #0x0f0f0f0f%s", r, r, map[string]string{"x": "0f0f0f0f", "w": ""}[r]),
		fmt.Sprintf("    mov %s1, #0x01010101%s", r, map[string]string{"x": "01010101", "w": ""}[r]),
		fmt.Sprintf("    mul %s0, %s0, %s1", r, r, r),
		fmt.Sprintf("    lsr %s0, %s0, #%d", r, r, map[string]int{"x": 56, "w": 24}[r]))
	if !inst.SkipFlags {
		lines = append(lines, fmt.Sprintf("    tst %s0, %s0", r, r))
		l.cpol = carryClear
	}
	wc, _ := writeRegFrom(dstReg, "x0", "w0")
	lines = append(lines, wc...)
	return lines, nil
}

// liftBitScan handles TZCNT, LZCNT, BSF, BSR.
//   - TZCNT/LZCNT: CF = (src == 0), ZF = (result == 0); the count of a zero source is the
//     operand size. CMP src, #0 then CCMP result, #0 gives Z = (result == 0) and C = !CF.
//   - BSF/BSR: ZF = (src == 0); a zero source leaves the destination unchanged.
//
// The carry sense afterwards is carrySub.
func (l *Arm64Lifter) liftBitScan(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	d, okD := arm64RegMap[dstReg]
	if !okDst || !okD {
		lines = append(lines, l.emitUnsupported(inst, "unsupported bit scan dst")...)
		return lines, nil
	}
	sz := d.Size
	bits := 8 * sz
	ld, ok := bitSource(inst.Inst.Args[1], sz, nextPC)
	if !ok {
		lines = append(lines, l.emitUnsupported(inst, "unsupported bit scan src")...)
		return lines, nil
	}
	lines = append(lines, ld...)
	r := "w"
	if sz == 8 {
		r = "x"
	}
	// x1 = result (x0 = zero-extended source).
	switch op {
	case x86asm.TZCNT, x86asm.BSF:
		if sz == 2 {
			lines = append(lines, "    orr w1, w0, #0x10000", "    rbit w1, w1", "    clz w1, w1")
		} else {
			lines = append(lines, fmt.Sprintf("    rbit %s1, %s0", r, r), fmt.Sprintf("    clz %s1, %s1", r, r))
		}
	case x86asm.LZCNT:
		if sz == 2 {
			lines = append(lines, "    lsl w1, w0, #16", "    orr w1, w1, #0x8000", "    clz w1, w1")
		} else {
			lines = append(lines, fmt.Sprintf("    clz %s1, %s0", r, r))
		}
	case x86asm.BSR:
		lines = append(lines, fmt.Sprintf("    clz %s1, %s0", r, r))
		top := bits - 1
		if sz == 2 {
			top = 31 // clz of the zero-extended 32-bit value
		}
		lines = append(lines, fmt.Sprintf("    mov %s2, #%d", r, top), fmt.Sprintf("    sub %s1, %s2, %s1", r, r, r))
	}
	flags := !inst.SkipFlags
	if op == x86asm.TZCNT || op == x86asm.LZCNT {
		if flags {
			// src != 0: flags of CMP result, #0 (Z = result == 0, C = 1 = !CF); src == 0:
			// NZCV = 0000 (CF = 1, ZF = 0 as the result is the operand size).
			lines = append(lines, fmt.Sprintf("    cmp %s0, #0", r), fmt.Sprintf("    ccmp %s1, #0, #0, ne", r))
			l.cpol = carrySub
		}
		wc, _ := writeRegFrom(dstReg, "x1", "w1")
		lines = append(lines, wc...)
		return lines, nil
	}
	// BSF/BSR: keep the destination when the source is zero.
	lines = append(lines, fmt.Sprintf("    cmp %s0, #0", r))
	if sz == 2 {
		lines = append(lines, fmt.Sprintf("    and w2, %s, #0xffff", d.W), "    csel w1, w2, w1, eq")
		wc, _ := writeRegFrom(dstReg, "x1", "w1")
		lines = append(lines, wc...)
	} else {
		lines = append(lines, fmt.Sprintf("    csel %s, %s, x1, eq", d.X, d.X))
	}
	l.cpol = carrySub // the CMP always runs
	return lines, nil
}

// liftBextr handles BMI1 BEXTR: dst = (src >> start) & ((1 << len) - 1), with start = ctrl[7:0]
// and len = ctrl[15:8]; a start >= the operand size gives 0 and a len >= it keeps all bits.
// ZF = (dst == 0), CF = OF = 0 (TST: carryClear).
func (l *Arm64Lifter) liftBextr(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	descDst, okDD := arm64RegMap[dstReg]
	ctrlReg, okCtrl := inst.Inst.Args[2].(x86asm.Reg)
	descCtrl, okDC := arm64RegMap[ctrlReg]
	if !okDst || !okDD || !okCtrl || !okDC {
		lines = append(lines, l.emitUnsupported(inst, "unsupported bextr operands")...)
		return lines, nil
	}
	sz := descDst.Size
	bits := 8 * sz
	// Read the control register first: dst may alias it.
	lines = append(lines, fmt.Sprintf("    ubfx w1, %s, #0, #8", descCtrl.W), fmt.Sprintf("    ubfx w2, %s, #8, #8", descCtrl.W))
	ld, ok := bitSource(inst.Inst.Args[1], sz, nextPC)
	if !ok {
		lines = append(lines, l.emitUnsupported(inst, "unsupported bextr src")...)
		return lines, nil
	}
	lines = append(lines, ld...)
	r := "w"
	if sz == 8 {
		r = "x"
	}
	lines = append(lines,
		fmt.Sprintf("    lsr %s0, %s0, %s1", r, r, r),
		fmt.Sprintf("    cmp w1, #%d", bits),
		fmt.Sprintf("    csel %s0, %szr, %s0, hs", r, r, r),
		fmt.Sprintf("    mov %s3, #1", r),
		fmt.Sprintf("    lsl %s3, %s3, %s2", r, r, r),
		fmt.Sprintf("    sub %s3, %s3, #1", r, r),
		fmt.Sprintf("    cmp w2, #%d", bits),
		fmt.Sprintf("    csinv %s3, %s3, %szr, lo", r, r, r),
		fmt.Sprintf("    and %s0, %s0, %s3", r, r, r))
	if !inst.SkipFlags {
		lines = append(lines, fmt.Sprintf("    tst %s0, %s0", r, r))
		l.cpol = carryClear
	}
	wc, _ := writeRegFrom(dstReg, "x0", "w0")
	lines = append(lines, wc...)
	return lines, nil
}

// liftCpuid handles CPUID instruction.
func (l *Arm64Lifter) liftCpuid(lines []string) []string {
	l.labelCounter++
	id := l.labelCounter
	lblLeaf0 := fmt.Sprintf(".Lcpuid_leaf0_%d", id)
	lblLeaf1 := fmt.Sprintf(".Lcpuid_leaf1_%d", id)
	lblLeaf7 := fmt.Sprintf(".Lcpuid_leaf7_%d", id)
	lblLeaf80 := fmt.Sprintf(".Lcpuid_leaf80_%d", id)
	lblLeaf81 := fmt.Sprintf(".Lcpuid_leaf81_%d", id)
	lblEnd := fmt.Sprintf(".Lcpuid_end_%d", id)

	lines = append(lines,
		"    // cpuid",
		"    mrs x16, nzcv",
		"    str x16, [sp, #-16]!",
		"    mov w16, w19 // save input leaf (eax)",
		"    mov w17, w21 // save input subleaf (ecx)",
		"    mov w19, #0  // eax = 0",
		"    mov w20, #0  // ebx = 0",
		"    mov w21, #0  // ecx = 0",
		"    mov w22, #0  // edx = 0",
		fmt.Sprintf("    cbz w16, %s", lblLeaf0),
		"    cmp w16, #1",
		fmt.Sprintf("    b.eq %s", lblLeaf1),
		"    cmp w16, #7",
		fmt.Sprintf("    b.eq %s", lblLeaf7),
		"    mov w20, #0x80000000",
		"    cmp w16, w20",
		fmt.Sprintf("    b.eq %s", lblLeaf80),
		"    add w20, w20, #1",
		"    cmp w16, w20",
		fmt.Sprintf("    b.eq %s", lblLeaf81),
		fmt.Sprintf("    b %s", lblEnd),
		fmt.Sprintf("%s:", lblLeaf0),
	)
	// Leaf 0: "GenuineIntel"
	lines = append(lines, loadImm32("w19", 0xd)...)
	lines = append(lines, loadImm32("w20", 0x756e6547)...)
	lines = append(lines, loadImm32("w22", 0x49656e69)...)
	lines = append(lines, loadImm32("w21", 0x6c65746e)...)
	lines = append(lines,
		fmt.Sprintf("    b %s", lblEnd),
		fmt.Sprintf("%s:", lblLeaf1),
	)
	// Leaf 1: Family/Model + Features
	lines = append(lines, loadImm32("w19", 0x000306c3)...)
	lines = append(lines, loadImm32("w20", 0x00100800)...)
	lines = append(lines, loadImm32("w21", 0x7ffafbff)...)
	lines = append(lines, loadImm32("w22", 0xbfebfbff)...)
	lines = append(lines,
		fmt.Sprintf("    b %s", lblEnd),
		fmt.Sprintf("%s:", lblLeaf7),
		fmt.Sprintf("    cbnz w17, %s", lblEnd),
	)
	// Leaf 7: AVX2, BMI1, BMI2
	lines = append(lines, loadImm32("w20", 0x00000219)...)
	lines = append(lines,
		fmt.Sprintf("    b %s", lblEnd),
		fmt.Sprintf("%s:", lblLeaf80),
	)
	// Leaf 0x80000000
	lines = append(lines, loadImm32("w19", 0x80000008)...)
	lines = append(lines,
		fmt.Sprintf("    b %s", lblEnd),
		fmt.Sprintf("%s:", lblLeaf81),
	)
	// Leaf 0x80000001
	lines = append(lines, loadImm32("w21", 0x00000021)...)
	lines = append(lines, loadImm32("w22", 0x2c100800)...)
	lines = append(lines,
		fmt.Sprintf("%s:", lblEnd),
		"    ldr x16, [sp], #16",
		"    msr nzcv, x16",
	)
	return lines
}

// liftXgetbv handles XGETBV instruction.
func (l *Arm64Lifter) liftXgetbv(lines []string) []string {
	l.labelCounter++
	id := l.labelCounter
	lblNonZero := fmt.Sprintf(".Lxgetbv_nonzero_%d", id)
	lblEnd := fmt.Sprintf(".Lxgetbv_end_%d", id)

	lines = append(lines,
		"    // xgetbv",
		fmt.Sprintf("    cbnz w21, %s", lblNonZero),
		"    mov x19, #7  // rax = 7 (x87 + SSE + AVX)",
		"    mov x22, #0  // rdx = 0",
		fmt.Sprintf("    b %s", lblEnd),
		fmt.Sprintf("%s:", lblNonZero),
		"    mov x19, #0",
		"    mov x22, #0",
		fmt.Sprintf("%s:", lblEnd),
	)
	return lines
}

// liftAndn handles BMI1 ANDN: dst = (~src1) & src2. src2 is loaded first: a memory src2 computes
// its address in x16, which must not hold src1. SF/ZF from the result, CF = OF = 0 (BICS clears
// C and V: carryClear).
func (l *Arm64Lifter) liftAndn(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okDst := inst.Inst.Args[0].(x86asm.Reg)
	s1Reg, okS1 := inst.Inst.Args[1].(x86asm.Reg)
	src2 := inst.Inst.Args[2]

	descDst, okDD := arm64RegMap[dstReg]
	descS1, okDS1 := arm64RegMap[s1Reg]
	if !okDst || !okDD || !okS1 || !okDS1 {
		lines = append(lines, l.emitUnsupported(inst, "unsupported andn operands")...)
		return lines, nil
	}
	sz := descDst.Size
	r := "w"
	s1 := descS1.W
	if sz == 8 {
		r, s1 = "x", descS1.X
	}
	s2 := ""
	if s2Reg, okS2 := src2.(x86asm.Reg); okS2 {
		d2, ok := arm64RegMap[s2Reg]
		if !ok {
			lines = append(lines, l.emitUnsupported(inst, "unmapped andn src2")...)
			return lines, nil
		}
		s2 = d2.W
		if sz == 8 {
			s2 = d2.X
		}
	} else if mem, okMem := src2.(x86asm.Mem); okMem {
		lines = append(lines, readMemTo(mem, nextPC, sz, "x17", "w17")...)
		s2 = r + "17"
	} else {
		lines = append(lines, l.emitUnsupported(inst, "unsupported andn src2")...)
		return lines, nil
	}
	dst := descDst.W
	if sz == 8 {
		dst = descDst.X
	}
	if inst.SkipFlags {
		lines = append(lines, fmt.Sprintf("    bic %s, %s, %s", dst, s2, s1))
	} else {
		lines = append(lines, fmt.Sprintf("    bics %s, %s, %s", dst, s2, s1))
		l.cpol = carryClear
	}
	return lines, nil
}

// liftFlagsOp handles CLC, STC, CMC, LAHF, SAHF in terms of the tracked carry sense.
// LAHF stores AF = PF = 0 (not modeled); SAHF keeps OF (V), as x86 does.
func (l *Arm64Lifter) liftFlagsOp(inst disasm.Instruction, lines []string) ([]string, error) {
	switch inst.Inst.Op {
	case x86asm.CLC:
		lines = append(lines, "    rmif xzr, #0, #2") // C = 0: CF = 0 in the carryClear sense
		l.cpol = carryClear
	case x86asm.STC:
		lines = append(lines, "    rmif xzr, #0, #2") // C = 0: CF = 1 in the carrySub sense
		l.cpol = carrySub
	case x86asm.CMC:
		lines = append(lines, "    cfinv") // inverting C inverts CF in either sense
		if l.cpol == carryClear {
			l.cpol = carryAdd
		}
	case x86asm.LAHF:
		// AH = SF:ZF:0:AF:0:PF:1:CF. AF and PF are not modelled (0 here; PF exists only after FP
		// compares, in V). Evidence from the lifted TLOU and U4 listings: every PF reader
		// (JP/JNP/SETP/CMOVP) follows a (V)(U)COMIS or FUCOMI(P); the AF/PF consumers are clang's
		// LAHF ... SAHF pairs around an overflow check (2 TLOU / 5 U4 sites, AH only restored)
		// and one PUSHFQ in a context save. None reads AF or PF of an integer operation.
		lines = append(lines,
			"    cset w0, mi",
			"    cset w1, eq",
			fmt.Sprintf("    cset w2, %s", l.carryCond(true)),
			"    orr w0, w2, w0, lsl #7",
			"    orr w0, w0, w1, lsl #6",
			"    orr w0, w0, #2",
			"    bfi x19, x0, #8, #8")
	case x86asm.SAHF:
		lines = append(lines,
			"    ubfx w0, w19, #8, #8", // AH
			"    rmif x0, #4, #12",     // N = SF (bit 7), Z = ZF (bit 6)
			"    rmif x0, #63, #2")     // C = CF (bit 0)
		l.cpol = carryAdd
	}
	return lines, nil
}

// liftLeave handles LEAVE (equivalent to mov rsp, rbp; pop rbp).
func (l *Arm64Lifter) liftLeave(lines []string) []string {
	// RSP is x26, RBP is x25
	lines = append(lines,
		"    mov x26, x25",        // mov rsp, rbp
		"    ldr x25, [x28, x26]", // pop rbp
		"    add x26, x26, #8",
	)
	return lines
}

// liftShldShrd handles SHLD and SHRD (16/32/64-bit) with x86 flags: CF = last bit shifted out of
// the destination, OF (count 1) = sign change, SF/ZF from the result. A masked count of 0
// changes nothing. 16/32-bit operands are shifted as one 2*size-bit value in a 64-bit register.
func (l *Arm64Lifter) liftShldShrd(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	srcReg, okSrcReg := inst.Inst.Args[1].(x86asm.Reg)
	if _, ok := arm64RegMap[srcReg]; !okSrcReg || !ok {
		lines = append(lines, l.emitUnsupported(inst, "shld/shrd src must be a register")...)
		return lines, nil
	}
	mask := 31
	if arm64RegMap[srcReg].Size == 8 {
		mask = 63
	}
	immCount := -1
	switch c := inst.Inst.Args[2].(type) {
	case x86asm.Imm:
		immCount = int(c) & mask
		if immCount == 0 {
			return append(lines, zeroCountZext(inst)...), nil
		}
		lines = append(lines, fmt.Sprintf("    mov w1, #%d", immCount))
	case x86asm.Reg:
		if c != x86asm.CL {
			lines = append(lines, l.emitUnsupported(inst, "shld/shrd count register")...)
			return lines, nil
		}
		lines = append(lines, fmt.Sprintf("    and w1, w21, #%d", mask)) // before CL can be written
	default:
		lines = append(lines, l.emitUnsupported(inst, "shld/shrd count")...)
		return lines, nil
	}
	load, val, size, store, ok := unaryOperand(inst, nextPC)
	if !ok {
		lines = append(lines, l.emitUnsupported(inst, "unsupported shld/shrd dst")...)
		return lines, nil
	}
	bits := 8 * size
	lines = append(lines, load...)
	rs, _ := readRegTo(srcReg, "x0", "w0")
	lines = append(lines, rs...)
	skip := ""
	if immCount < 0 {
		skip = fmt.Sprintf("L_shxd_0x%x_%d", inst.Address, l.labelCounter)
		l.labelCounter++
		lines = append(lines, fmt.Sprintf("    cbz w1, %s", skip))
	}
	// x3 = destination value (zero-extended), x5 = result, x2 = CF.
	if bits == 64 {
		lines = append(lines, fmt.Sprintf("    mov x3, %s", val), "    neg x6, x1")
		if op == x86asm.SHLD {
			lines = append(lines, "    lsl x5, x3, x1", "    lsr x7, x0, x6", "    orr x5, x5, x7", "    lsr x2, x3, x6")
		} else {
			lines = append(lines, "    lsr x5, x3, x1", "    lsl x7, x0, x6", "    orr x5, x5, x7", "    sub x6, x1, #1", "    lsr x2, x3, x6")
		}
	} else {
		lines = append(lines, fmt.Sprintf("    mov w3, %s", val))
		if bits == 16 {
			lines = append(lines, "    and w3, w3, #0xffff")
		}
		if op == x86asm.SHLD {
			lines = append(lines, fmt.Sprintf("    orr x4, x0, x3, lsl #%d", bits), "    lsl x5, x4, x1",
				fmt.Sprintf("    lsr x5, x5, #%d", bits), fmt.Sprintf("    mov w6, #%d", 2*bits), "    sub w6, w6, w1", "    lsr x2, x4, x6")
		} else {
			lines = append(lines, fmt.Sprintf("    orr x4, x3, x0, lsl #%d", bits), "    lsr x5, x4, x1",
				"    sub w6, w1, #1", "    lsr x2, x4, x6")
		}
	}
	if !inst.SkipFlags {
		lines = append(lines, "    and x2, x2, #1",
			"    eor x7, x5, x3", fmt.Sprintf("    ubfx x7, x7, #%d, #1", bits-1)) // OF = sign change
		outPol := carryAdd
		if immCount < 0 && l.cpol == carrySub {
			outPol = carrySub
		}
		if outPol == carrySub {
			lines = append(lines, "    eor x2, x2, #1")
		}
		lines = append(lines, "    orr x2, x7, x2, lsl #1")
		if bits == 64 {
			lines = append(lines, "    cmn xzr, x5")
		} else {
			lines = append(lines, fmt.Sprintf("    cmn wzr, w5, lsl #%d", 32-bits))
		}
		lines = append(lines, "    rmif x2, #0, #3")
		l.cpol = outPol
	}
	res := "w5"
	if bits == 64 {
		res = "x5"
	}
	lines = append(lines, store(res)...)
	if skip != "" {
		lines = append(lines, skip+":")
	}
	return lines, nil
}

// liftEnter handles ENTER alloc, level.
func (l *Arm64Lifter) liftEnter(inst disasm.Instruction, lines []string) []string {
	alloc := uint64(0)
	if imm, ok := inst.Inst.Args[0].(x86asm.Imm); ok {
		alloc = uint64(uint16(imm))
	}
	level := 0
	if imm, ok := inst.Inst.Args[1].(x86asm.Imm); ok {
		level = int(imm) & 0x1f
	}
	// push rbp:
	lines = append(lines,
		"    sub  x26, x26, #8",
		"    str  x25, [x28, x26]",
	)
	if level == 0 {
		lines = append(lines, "    mov  x25, x26")
	} else {
		lines = append(lines, "    mov  x16, x26") // frame_ptr = rsp
		for i := 1; i < level; i++ {
			lines = append(lines,
				fmt.Sprintf("    sub  x17, x25, #%d", i*8),
				"    ldr  x17, [x28, x17]",
				"    sub  x26, x26, #8",
				"    str  x17, [x28, x26]",
			)
		}
		lines = append(lines,
			"    sub  x26, x26, #8",
			"    str  x16, [x28, x26]",
			"    mov  x25, x16",
		)
	}
	if alloc > 0 {
		if alloc <= 4095 {
			lines = append(lines, fmt.Sprintf("    sub  x26, x26, #%d", alloc))
		} else {
			lines = append(lines,
				fmt.Sprintf("    movz x16, #0x%x", alloc&0xffff),
				fmt.Sprintf("    movk x16, #0x%x, lsl #16", (alloc>>16)&0xffff),
				"    sub  x26, x26, x16",
			)
		}
	}
	return lines
}

// liftRclRcr handles RCL and RCR: a rotate of the (size+1)-bit value CF:dst. The count is masked
// to 5 bits (6 for 64-bit) and taken modulo size+1; a zero count changes nothing. Only CF and OF
// change (RCL: OF = MSB(result) ^ CF after; RCR: OF = MSB(dst) ^ CF before), inserted with RMIF.
func (l *Arm64Lifter) liftRclRcr(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	op := inst.Inst.Op
	load, val, size, store, ok := unaryOperand(inst, nextPC)
	if !ok {
		lines = append(lines, l.emitUnsupported(inst, "unsupported rcl/rcr operand")...)
		return lines, nil
	}
	bits := 8 * size
	mask := 31
	if bits == 64 {
		mask = 63
	}
	immCount := -1
	switch c := inst.Inst.Args[1].(type) {
	case x86asm.Imm:
		immCount = int(c) & mask
		if bits < 32 {
			immCount %= bits + 1
		}
		if immCount == 0 {
			return append(lines, zeroCountZext(inst)...), nil
		}
		lines = append(lines, fmt.Sprintf("    mov w1, #%d", immCount))
	case x86asm.Reg:
		if c != x86asm.CL {
			lines = append(lines, l.emitUnsupported(inst, "rcl/rcr count register")...)
			return lines, nil
		}
		lines = append(lines, fmt.Sprintf("    and w1, w21, #%d", mask))
		if bits < 32 {
			lines = append(lines, fmt.Sprintf("    mov w5, #%d", bits+1), "    udiv w6, w1, w5", "    msub w1, w6, w5, w1")
		}
	default:
		lines = append(lines, l.emitUnsupported(inst, "rcl/rcr count")...)
		return lines, nil
	}
	lines = append(lines, fmt.Sprintf("    cset w2, %s", l.carryCond(true))) // CF in, before NZCV changes
	lines = append(lines, load...)
	skip := ""
	if immCount < 0 {
		skip = fmt.Sprintf("L_rcx_0x%x_%d", inst.Address, l.labelCounter)
		l.labelCounter++
		lines = append(lines, fmt.Sprintf("    cbz w1, %s", skip))
	}
	// x3 = value (zero-extended); x5 = result; x4 = CF out; x7 = OF.
	if bits == 64 {
		lines = append(lines, fmt.Sprintf("    mov x3, %s", val), "    neg x6, x1") // x6 = 64 - c
		if op == x86asm.RCL {
			lines = append(lines, "    lsl x5, x3, x1", "    sub x7, x1, #1", "    lsl x7, x2, x7", "    orr x5, x5, x7",
				"    lsr x7, x3, #1", "    lsr x7, x7, x6", "    orr x5, x5, x7", "    lsr x4, x3, x6")
		} else {
			lines = append(lines, "    lsr x5, x3, x1", "    lsl x7, x2, x6", "    orr x5, x5, x7",
				"    lsl x7, x3, #1", "    lsl x7, x7, x6", "    orr x5, x5, x7", "    sub x7, x1, #1", "    lsr x4, x3, x7")
		}
		lines = append(lines, "    and x4, x4, #1")
	} else {
		lines = append(lines, fmt.Sprintf("    mov w3, %s", val))
		if bits < 32 {
			lines = append(lines, fmt.Sprintf("    and w3, w3, #0x%x", (1<<bits)-1))
		}
		lines = append(lines, fmt.Sprintf("    orr x4, x3, x2, lsl #%d", bits), fmt.Sprintf("    mov w6, #%d", bits+1), "    sub w6, w6, w1")
		if op == x86asm.RCL {
			lines = append(lines, "    lsl x5, x4, x1", "    lsr x7, x4, x6", "    orr x5, x5, x7")
		} else {
			lines = append(lines, "    lsr x5, x4, x1", "    lsl x7, x4, x6", "    orr x5, x5, x7")
		}
		lines = append(lines, fmt.Sprintf("    ubfx x4, x5, #%d, #1", bits))
	}
	if !inst.CarryDead || !inst.OverflowDead {
		if op == x86asm.RCL {
			lines = append(lines, fmt.Sprintf("    ubfx x7, x5, #%d, #1", bits-1), "    eor x7, x7, x4")
		} else {
			lines = append(lines, fmt.Sprintf("    ubfx x7, x3, #%d, #1", bits-1), "    eor x7, x7, x2")
		}
		outPol := carryAdd
		if immCount < 0 && l.cpol == carrySub {
			outPol = carrySub
		}
		if outPol == carrySub {
			lines = append(lines, "    eor x4, x4, #1")
		}
		lines = append(lines, "    orr x4, x7, x4, lsl #1", "    rmif x4, #0, #3")
		l.cpol = outPol
	}
	res := "w5"
	if bits == 64 {
		res = "x5"
	}
	lines = append(lines, store(res)...)
	if skip != "" {
		lines = append(lines, skip+":")
		lines = append(lines, zeroCountZext(inst)...)
	}
	return lines, nil
}

// zeroCountZext is what a shift or rotate of a 32-bit register by a masked count of 0 still
// does: it writes the register, which zero-extends it to 64 bits (flags and value unchanged).
func zeroCountZext(inst disasm.Instruction) []string {
	if r, ok := inst.Inst.Args[0].(x86asm.Reg); ok {
		if d, okD := arm64RegMap[r]; okD && d.Size == 4 {
			return []string{fmt.Sprintf("    mov %s, %s", d.W, d.W)}
		}
	}
	return nil
}

// liftCrc32 handles CRC32.
func (l *Arm64Lifter) liftCrc32(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okD := inst.Inst.Args[0].(x86asm.Reg)
	if !okD {
		lines = append(lines, l.emitUnsupported(inst, "crc32 invalid dst")...)
		return lines, nil
	}
	dDesc, okDesc := arm64RegMap[dstReg]
	if !okDesc {
		lines = append(lines, l.emitUnsupported(inst, "crc32 unknown dst")...)
		return lines, nil
	}

	src := inst.Inst.Args[1]
	srcSize := 4
	srcRegStr := "w16"
	if sReg, okS := src.(x86asm.Reg); okS {
		if sDesc, okSDesc := arm64RegMap[sReg]; okSDesc {
			srcSize = sDesc.Size
			rc, _ := readRegTo(sReg, "x16", "w16")
			lines = append(lines, rc...)
		}
	} else if mem, okMem := src.(x86asm.Mem); okMem {
		srcSize = defMemSz(inst)
		lines = append(lines, computeEffAddr(mem, nextPC, "x17")...)
		switch srcSize {
		case 1:
			lines = append(lines, "    ldrb w16, [x28, x17]")
		case 2:
			lines = append(lines, "    ldrh w16, [x28, x17]")
		case 4:
			lines = append(lines, "    ldr  w16, [x28, x17]")
		case 8:
			lines = append(lines, "    ldr  x16, [x28, x17]")
		}
	}

	if srcSize == 8 {
		srcRegStr = "x16"
	}

	rc, _ := readRegTo(dstReg, "x17", "w17")
	lines = append(lines, rc...)

	switch srcSize {
	case 1:
		lines = append(lines, fmt.Sprintf("    crc32cb %s, w17, %s", dDesc.W, srcRegStr))
	case 2:
		lines = append(lines, fmt.Sprintf("    crc32ch %s, w17, %s", dDesc.W, srcRegStr))
	case 4:
		lines = append(lines, fmt.Sprintf("    crc32cw %s, w17, %s", dDesc.W, srcRegStr))
	case 8:
		lines = append(lines, fmt.Sprintf("    crc32cx %s, w17, %s", dDesc.W, srcRegStr))
	}
	return lines, nil
}

// liftLar handles LAR (Load Access Rights Byte).
func (l *Arm64Lifter) liftLar(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dstReg, okD := inst.Inst.Args[0].(x86asm.Reg)
	if !okD {
		return lines, nil
	}
	dDesc, okDesc := arm64RegMap[dstReg]
	if !okDesc {
		return lines, nil
	}
	lines = append(lines,
		fmt.Sprintf("    mov  %s, #0x00f30000", dDesc.W),
		"    cmp  wzr, wzr", // sets ZF=1
	)
	return lines, nil
}

// liftStr handles STR (Store Task Register).
func (l *Arm64Lifter) liftStr(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	dst := inst.Inst.Args[0]
	if reg, okR := dst.(x86asm.Reg); okR {
		if dDesc, okDesc := arm64RegMap[reg]; okDesc {
			lines = append(lines, fmt.Sprintf("    mov  %s, #0x40", dDesc.W))
			return lines, nil
		}
	} else if mem, okMem := dst.(x86asm.Mem); okMem {
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		lines = append(lines,
			"    mov  w17, #0x40",
			"    strh w17, [x28, x16]",
		)
		return lines, nil
	}
	return lines, nil
}
