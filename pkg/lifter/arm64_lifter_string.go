package lifter

import (
	"fmt"
	"math/bits"

	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/disasm"
)

// The direction flag (EFLAGS.DF) is kept in GuestContext.df (0 or 1), not in NZCV: CLD and STD
// store it, PUSHFQ and POPFQ move it to and from bit 10 of the RFLAGS image, and every string
// instruction reads it, so the direction holds across blocks, calls and POPFQ exactly as the
// architectural flag does. A new context starts with DF = 0 (the System V ABI state at every
// call boundary).

// liftDirectionFlag handles CLD (DF = 0) and STD (DF = 1).
func (l *Arm64Lifter) liftDirectionFlag(set bool, lines []string) []string {
	lines = append(lines, l.emitLoadCtxPtr("x16")...)
	if set {
		return append(lines,
			"    mov  w17, #1",
			"    strb w17, [x16, #0x9e]", // df
		)
	}
	return append(lines, "    strb wzr, [x16, #0x9e]") // df
}

// strStep loads the negated element step of a string instruction into x7: -width with DF = 0
// and +width with DF = 1, so "sub ptr, ptr, x7" moves RSI/RDI to the next element in the
// direction DF selects (increasing addresses for DF = 0, decreasing for DF = 1). NZCV is not
// touched (REP MOVS/STOS/LODS keep every flag).
func (l *Arm64Lifter) strStep(width int) []string {
	lines := l.emitLoadCtxPtr("x7")
	return append(lines,
		"    ldrb w7, [x7, #0x9e]", // df
		fmt.Sprintf("    lsl  x7, x7, #%d", bits.TrailingZeros(uint(width))+1),
		fmt.Sprintf("    sub  x7, x7, #%d", width),
	)
}

func instHasRep(inst x86asm.Inst) bool {
	return instRepPrefix(inst) == 0xF3
}

func instHasRepn(inst x86asm.Inst) bool {
	return instRepPrefix(inst) == 0xF2
}

func instRepPrefix(inst x86asm.Inst) byte {
	for _, p := range inst.Prefix {
		if p == 0 {
			continue
		}
		switch p & 0xFF {
		case 0xF3, 0xF2:
			return byte(p & 0xFF)
		}
	}
	return 0
}

// liftStringOp handles string instructions: MOVS*, STOS*, LODS*, SCAS*, CMPS*.
func (l *Arm64Lifter) liftStringOp(inst disasm.Instruction, width int, lines []string) ([]string, error) {
	op := inst.Inst.Op
	switch op {
	case x86asm.MOVSB, x86asm.MOVSW, x86asm.MOVSD, x86asm.MOVSQ:
		return l.liftMovs(inst, width, lines)
	case x86asm.STOSB, x86asm.STOSW, x86asm.STOSD, x86asm.STOSQ:
		return l.liftStos(inst, width, lines)
	case x86asm.LODSB, x86asm.LODSW, x86asm.LODSD, x86asm.LODSQ:
		return l.liftLods(inst, width, lines)
	case x86asm.SCASB, x86asm.SCASW, x86asm.SCASD, x86asm.SCASQ:
		return l.liftScas(inst, width, lines)
	case x86asm.CMPSB, x86asm.CMPSW, x86asm.CMPSD, x86asm.CMPSQ:
		return l.liftCmps(inst, width, lines)
	default:
		lines = append(lines, l.emitUnsupported(inst, "unsupported string op")...)
		return lines, nil
	}
}

func (l *Arm64Lifter) liftMovs(inst disasm.Instruction, width int, lines []string) ([]string, error) {
	loadInst := "ldr"
	storeInst := "str"
	reg := "x16"
	switch width {
	case 1:
		loadInst = "ldrb"
		storeInst = "strb"
		reg = "w16"
	case 2:
		loadInst = "ldrh"
		storeInst = "strh"
		reg = "w16"
	case 4:
		loadInst = "ldr"
		storeInst = "str"
		reg = "w16"
	case 8:
		loadInst = "ldr"
		storeInst = "str"
		reg = "x16"
	}

	// One element at a time, in the direction DF selects: an overlapping copy reads every
	// element after the previous store, as the x86 element-by-element definition requires.
	lines = append(lines, l.strStep(width)...)
	rep := instHasRep(inst.Inst)
	var lblLoop, lblDone string
	if rep {
		l.labelCounter++
		lblLoop = fmt.Sprintf("L_rep_movs_loop_%x_%d", inst.Address, l.labelCounter)
		lblDone = fmt.Sprintf("L_rep_movs_done_%x_%d", inst.Address, l.labelCounter)
		lines = append(lines, fmt.Sprintf("    cbz x21, %s", lblDone), lblLoop+":")
	}
	lines = append(lines,
		fmt.Sprintf("    %s %s, [x28, x23]", loadInst, reg),
		fmt.Sprintf("    %s %s, [x28, x24]", storeInst, reg),
		"    sub x23, x23, x7",
		"    sub x24, x24, x7",
	)
	if rep {
		lines = append(lines,
			"    sub x21, x21, #1", // REP MOVS keeps the flags: no SUBS
			fmt.Sprintf("    cbnz x21, %s", lblLoop),
			lblDone+":",
		)
	}
	return lines, nil
}

func (l *Arm64Lifter) liftStos(inst disasm.Instruction, width int, lines []string) ([]string, error) {
	storeInst := "str"
	reg := "x19"
	switch width {
	case 1:
		storeInst = "strb"
		reg = "w19"
	case 2:
		storeInst = "strh"
		reg = "w19"
	case 4:
		storeInst = "str"
		reg = "w19"
	case 8:
		storeInst = "str"
		reg = "x19"
	}

	lines = append(lines, l.strStep(width)...)
	rep := instHasRep(inst.Inst)
	var lblLoop, lblDone string
	if rep {
		l.labelCounter++
		lblLoop = fmt.Sprintf("L_rep_stos_loop_%x_%d", inst.Address, l.labelCounter)
		lblDone = fmt.Sprintf("L_rep_stos_done_%x_%d", inst.Address, l.labelCounter)
		lines = append(lines, fmt.Sprintf("    cbz x21, %s", lblDone), lblLoop+":")
	}
	lines = append(lines,
		fmt.Sprintf("    %s %s, [x28, x24]", storeInst, reg),
		"    sub x24, x24, x7",
	)
	if rep {
		lines = append(lines,
			"    sub x21, x21, #1", // REP STOS keeps the flags: no SUBS
			fmt.Sprintf("    cbnz x21, %s", lblLoop),
			lblDone+":",
		)
	}
	return lines, nil
}

func (l *Arm64Lifter) liftLods(inst disasm.Instruction, width int, lines []string) ([]string, error) {
	hasRep := instHasRep(inst.Inst)
	lines = append(lines, l.strStep(width)...)
	var lblDone string
	if hasRep {
		// REP LODS: only the last element reaches the accumulator. RSI moves over the RCX - 1
		// elements before it, the load and the final step complete the RCX steps.
		l.labelCounter++
		lblDone = fmt.Sprintf("L_rep_lods_done_%x_%d", inst.Address, l.labelCounter)
		lines = append(lines,
			fmt.Sprintf("    cbz x21, %s", lblDone),
			"    sub x16, x21, #1",
			"    mul x16, x16, x7",
			"    sub x23, x23, x16",
		)
	}

	switch width {
	case 1:
		lines = append(lines,
			"    ldrb w16, [x28, x23]",
			"    bfi x19, x16, #0, #8",
		)
	case 2:
		lines = append(lines,
			"    ldrh w16, [x28, x23]",
			"    bfi x19, x16, #0, #16",
		)
	case 4:
		lines = append(lines, "    ldr w19, [x28, x23]")
	case 8:
		lines = append(lines, "    ldr x19, [x28, x23]")
	}
	lines = append(lines, "    sub x23, x23, x7")

	if hasRep {
		lines = append(lines,
			"    mov x21, #0",
			fmt.Sprintf("%s:", lblDone),
		)
	}
	return lines, nil
}

// strCompare sets NZCV from lhs - rhs at the operand width (x86 CF in the sub sense). 8/16-bit
// operands are compared in the top bits of a W register, so N, C and V are those of the narrow
// subtraction. lhs is "x19" (AL/AX/EAX/RAX) or a loaded scratch value; scratch x4.
func strCompare(width int, lhs, rhs string) []string {
	switch width {
	case 1, 2:
		sh := 32 - 8*width
		return []string{
			fmt.Sprintf("    lsl w4, w%s, #%d", lhs[1:], sh),
			fmt.Sprintf("    cmp w4, w%s, lsl #%d", rhs[1:], sh),
		}
	case 4:
		return []string{fmt.Sprintf("    cmp w%s, w%s", lhs[1:], rhs[1:])}
	}
	return []string{fmt.Sprintf("    cmp x%s, x%s", lhs[1:], rhs[1:])}
}

// strLoad returns the zero-extending load of one element.
func strLoad(width int, dst, addr string) string {
	switch width {
	case 1:
		return fmt.Sprintf("    ldrb w%s, [x28, %s]", dst[1:], addr)
	case 2:
		return fmt.Sprintf("    ldrh w%s, [x28, %s]", dst[1:], addr)
	case 4:
		return fmt.Sprintf("    ldr w%s, [x28, %s]", dst[1:], addr)
	}
	return fmt.Sprintf("    ldr x%s, [x28, %s]", dst[1:], addr)
}

// liftScas handles SCAS: flags of AL/AX/EAX/RAX - [RDI], RDI advances. liftCmps handles CMPS:
// flags of [RSI] - [RDI], RSI and RDI advance. With REPE/REPNE the comparison repeats while RCX
// is not 0 and ZF is 1 (REPE) or 0 (REPNE); RCX = 0 on entry leaves the flags of the previous
// instruction, so the carry is brought into the sub sense first and both paths end in it.
func (l *Arm64Lifter) liftScas(inst disasm.Instruction, width int, lines []string) ([]string, error) {
	lines = append(lines, l.strStep(width)...)
	body := []string{strLoad(width, "x16", "x24"), "    sub x24, x24, x7"}
	return l.liftRepCompare(inst, "scas", body, strCompare(width, "x19", "x16"), lines), nil
}

func (l *Arm64Lifter) liftCmps(inst disasm.Instruction, width int, lines []string) ([]string, error) {
	lines = append(lines, l.strStep(width)...)
	body := []string{
		strLoad(width, "x16", "x23"),
		strLoad(width, "x17", "x24"),
		"    sub x23, x23, x7",
		"    sub x24, x24, x7",
	}
	return l.liftRepCompare(inst, "cmps", body, strCompare(width, "x16", "x17"), lines), nil
}

func (l *Arm64Lifter) liftRepCompare(inst disasm.Instruction, name string, body, cmp, lines []string) []string {
	stopOnMatch := instHasRepn(inst.Inst)
	if !stopOnMatch && !instHasRep(inst.Inst) {
		lines = append(lines, body...)
		lines = append(lines, cmp...)
		l.cpol = carrySub
		return lines
	}
	l.labelCounter++
	id := l.labelCounter
	lblLoop := fmt.Sprintf("L_rep_%s_loop_%x_%d", name, inst.Address, id)
	lblDone := fmt.Sprintf("L_rep_%s_done_%x_%d", name, inst.Address, id)
	stopBranch := "b.ne" // REPE stops when not equal
	if stopOnMatch {
		stopBranch = "b.eq" // REPNE stops when equal
	}
	lines = append(lines, l.normalizeCarry()...)
	lines = append(lines, fmt.Sprintf("    cbz x21, %s", lblDone), lblLoop+":")
	lines = append(lines, body...)
	lines = append(lines, "    sub x21, x21, #1")
	lines = append(lines, cmp...)
	lines = append(lines,
		fmt.Sprintf("    %s %s", stopBranch, lblDone),
		fmt.Sprintf("    cbnz x21, %s", lblLoop),
		lblDone+":",
	)
	l.cpol = carrySub
	return lines
}
