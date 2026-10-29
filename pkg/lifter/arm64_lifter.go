package lifter

import (
	"fmt"
	"regexp"
	"strings"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

// RegDesc holds the ARM64 register mapping details for an x86 register.
type Arm64RegDesc struct {
	X       string // 64-bit ARM64 register name (e.g. "x19")
	W       string // 32-bit ARM64 register name (e.g. "w19")
	Size    int    // Size in bytes: 1, 2, 4, 8
	IsHigh8 bool   // true for AH, BH, CH, DH (bits 8-15)
}

// XmmDesc holds the ARM64 vector register mapping for an x86 XMM register.
type XmmDesc struct {
	Q string // 128-bit (e.g. "q0")
	D string // 64-bit float/double (e.g. "d0")
	S string // 32-bit float (e.g. "s0")
	V string // Vector register (e.g. "v0")
}

func isSegmentReg(reg x86asm.Reg) bool {
	return reg == x86asm.CS || reg == x86asm.DS || reg == x86asm.ES || reg == x86asm.SS || reg == x86asm.FS || reg == x86asm.GS
}

func isMmx(reg x86asm.Reg) bool {
	return reg >= x86asm.M0 && reg <= x86asm.M7
}

func mmxIdx(reg x86asm.Reg) int {
	return int(reg - x86asm.M0)
}

// Register map:
// x86 GPRs map directly to dedicated ARM64 registers.
// X19-X25: callee-saved registers in Apple Darwin ABI (preserved across C calls)
// X26: Guest RSP
// X27: Guest FS_BASE (TLS)
// X28: Guest MEM_BASE (Base address of mapped guest virtual memory)
// X8-X15: Guest R8-R15
var arm64RegMap = map[x86asm.Reg]Arm64RegDesc{
	// RAX
	x86asm.RAX: {X: "x19", W: "w19", Size: 8},
	x86asm.EAX: {X: "x19", W: "w19", Size: 4},
	x86asm.AX:  {X: "x19", W: "w19", Size: 2},
	x86asm.AL:  {X: "x19", W: "w19", Size: 1, IsHigh8: false},
	x86asm.AH:  {X: "x19", W: "w19", Size: 1, IsHigh8: true},

	// RBX
	x86asm.RBX: {X: "x20", W: "w20", Size: 8},
	x86asm.EBX: {X: "x20", W: "w20", Size: 4},
	x86asm.BX:  {X: "x20", W: "w20", Size: 2},
	x86asm.BL:  {X: "x20", W: "w20", Size: 1, IsHigh8: false},
	x86asm.BH:  {X: "x20", W: "w20", Size: 1, IsHigh8: true},

	// RCX
	x86asm.RCX: {X: "x21", W: "w21", Size: 8},
	x86asm.ECX: {X: "x21", W: "w21", Size: 4},
	x86asm.CX:  {X: "x21", W: "w21", Size: 2},
	x86asm.CL:  {X: "x21", W: "w21", Size: 1, IsHigh8: false},
	x86asm.CH:  {X: "x21", W: "w21", Size: 1, IsHigh8: true},

	// RDX
	x86asm.RDX: {X: "x22", W: "w22", Size: 8},
	x86asm.EDX: {X: "x22", W: "w22", Size: 4},
	x86asm.DX:  {X: "x22", W: "w22", Size: 2},
	x86asm.DL:  {X: "x22", W: "w22", Size: 1, IsHigh8: false},
	x86asm.DH:  {X: "x22", W: "w22", Size: 1, IsHigh8: true},

	// RSI
	x86asm.RSI: {X: "x23", W: "w23", Size: 8},
	x86asm.ESI: {X: "x23", W: "w23", Size: 4},
	x86asm.SI:  {X: "x23", W: "w23", Size: 2},
	x86asm.SIB: {X: "x23", W: "w23", Size: 1},

	// RDI
	x86asm.RDI: {X: "x24", W: "w24", Size: 8},
	x86asm.EDI: {X: "x24", W: "w24", Size: 4},
	x86asm.DI:  {X: "x24", W: "w24", Size: 2},
	x86asm.DIB: {X: "x24", W: "w24", Size: 1},

	// RBP
	x86asm.RBP: {X: "x25", W: "w25", Size: 8},
	x86asm.EBP: {X: "x25", W: "w25", Size: 4},
	x86asm.BP:  {X: "x25", W: "w25", Size: 2},
	x86asm.BPB: {X: "x25", W: "w25", Size: 1},

	// RSP
	x86asm.RSP: {X: "x26", W: "w26", Size: 8},
	x86asm.ESP: {X: "x26", W: "w26", Size: 4},
	x86asm.SP:  {X: "x26", W: "w26", Size: 2},
	x86asm.SPB: {X: "x26", W: "w26", Size: 1},

	// R8 - R15
	x86asm.R8:  {X: "x8", W: "w8", Size: 8},
	x86asm.R8L: {X: "x8", W: "w8", Size: 4},
	x86asm.R8W: {X: "x8", W: "w8", Size: 2},
	x86asm.R8B: {X: "x8", W: "w8", Size: 1},

	x86asm.R9:  {X: "x9", W: "w9", Size: 8},
	x86asm.R9L: {X: "x9", W: "w9", Size: 4},
	x86asm.R9W: {X: "x9", W: "w9", Size: 2},
	x86asm.R9B: {X: "x9", W: "w9", Size: 1},

	x86asm.R10:  {X: "x10", W: "w10", Size: 8},
	x86asm.R10L: {X: "x10", W: "w10", Size: 4},
	x86asm.R10W: {X: "x10", W: "w10", Size: 2},
	x86asm.R10B: {X: "x10", W: "w10", Size: 1},

	x86asm.R11:  {X: "x11", W: "w11", Size: 8},
	x86asm.R11L: {X: "x11", W: "w11", Size: 4},
	x86asm.R11W: {X: "x11", W: "w11", Size: 2},
	x86asm.R11B: {X: "x11", W: "w11", Size: 1},

	x86asm.R12:  {X: "x12", W: "w12", Size: 8},
	x86asm.R12L: {X: "x12", W: "w12", Size: 4},
	x86asm.R12W: {X: "x12", W: "w12", Size: 2},
	x86asm.R12B: {X: "x12", W: "w12", Size: 1},

	x86asm.R13:  {X: "x13", W: "w13", Size: 8},
	x86asm.R13L: {X: "x13", W: "w13", Size: 4},
	x86asm.R13W: {X: "x13", W: "w13", Size: 2},
	x86asm.R13B: {X: "x13", W: "w13", Size: 1},

	x86asm.R14:  {X: "x14", W: "w14", Size: 8},
	x86asm.R14L: {X: "x14", W: "w14", Size: 4},
	x86asm.R14W: {X: "x14", W: "w14", Size: 2},
	x86asm.R14B: {X: "x14", W: "w14", Size: 1},

	x86asm.R15:  {X: "x15", W: "w15", Size: 8},
	x86asm.R15L: {X: "x15", W: "w15", Size: 4},
	x86asm.R15W: {X: "x15", W: "w15", Size: 2},
	x86asm.R15B: {X: "x15", W: "w15", Size: 1},
}

// XMM registers map to ARM64 NEON V0-V15
var arm64XmmMap = map[x86asm.Reg]XmmDesc{
	x86asm.X0:  {Q: "q0", D: "d0", S: "s0", V: "v0"},
	x86asm.X1:  {Q: "q1", D: "d1", S: "s1", V: "v1"},
	x86asm.X2:  {Q: "q2", D: "d2", S: "s2", V: "v2"},
	x86asm.X3:  {Q: "q3", D: "d3", S: "s3", V: "v3"},
	x86asm.X4:  {Q: "q4", D: "d4", S: "s4", V: "v4"},
	x86asm.X5:  {Q: "q5", D: "d5", S: "s5", V: "v5"},
	x86asm.X6:  {Q: "q6", D: "d6", S: "s6", V: "v6"},
	x86asm.X7:  {Q: "q7", D: "d7", S: "s7", V: "v7"},
	x86asm.X8:  {Q: "q8", D: "d8", S: "s8", V: "v8"},
	x86asm.X9:  {Q: "q9", D: "d9", S: "s9", V: "v9"},
	x86asm.X10: {Q: "q10", D: "d10", S: "s10", V: "v10"},
	x86asm.X11: {Q: "q11", D: "d11", S: "s11", V: "v11"},
	x86asm.X12: {Q: "q12", D: "d12", S: "s12", V: "v12"},
	x86asm.X13: {Q: "q13", D: "d13", S: "s13", V: "v13"},
	x86asm.X14: {Q: "q14", D: "d14", S: "s14", V: "v14"},
	x86asm.X15: {Q: "q15", D: "d15", S: "s15", V: "v15"},
}

// getVectorDesc resolves XMM or YMM registers to ARM64 NEON registers: the low 128 bits live in
// v0-v15, the YMM upper halves in v16-v31. The upper-half names are returned as placeholders
// ("vU<n>", "qU<n>"): lowerings also use v16-v31 as scratch registers, and LiftInstructionToBuf
// moves a scratch register that collides with an operand's upper half elsewhere before it
// replaces the placeholders with the real names (see fixUpperHalves).
func getVectorDesc(reg x86asm.Reg) (isYmm bool, lowQ, highQ, lowV, highV, lowS, lowD string, ok bool) {
	if reg >= x86asm.Y0 && reg <= x86asm.Y15 {
		idx := int(reg - x86asm.Y0)
		return true, fmt.Sprintf("q%d", idx), fmt.Sprintf("qU%d", idx+16),
			fmt.Sprintf("v%d", idx), fmt.Sprintf("vU%d", idx+16),
			fmt.Sprintf("s%d", idx), fmt.Sprintf("d%d", idx), true
	}
	if reg >= x86asm.X0 && reg <= x86asm.X15 {
		idx := int(reg - x86asm.X0)
		return false, fmt.Sprintf("q%d", idx), fmt.Sprintf("qU%d", idx+16),
			fmt.Sprintf("v%d", idx), fmt.Sprintf("vU%d", idx+16),
			fmt.Sprintf("s%d", idx), fmt.Sprintf("d%d", idx), true
	}
	return false, "", "", "", "", "", "", false
}

// Arm64Lifter translates x86-64 instructions directly into Apple Silicon ARM64 assembly (.s).
type Arm64Lifter struct {
	knownFuncs    map[uint64]bool
	currentFn     *disasm.Function
	unsuppCounter uint64
	labelCounter  uint64
	// cpol is how ARM C holds x86 CF at the current point of the block being lifted.
	cpol carryPol
	// noTSO turns off acquire/release for non-stack guest memory (see arm64_lifter_tso.go).
	noTSO bool
}

// x86 flags live in NZCV: N = SF, Z = ZF, V = OF (PF after an FP compare, the only place PF
// is modeled), and C = CF in one of the senses below. The sense is tracked statically while a
// block is lifted; at every block boundary it is carrySub.
type carryPol uint8

const (
	// carrySub: C = !CF, ARM's borrow convention after SUBS/CMP.
	carrySub carryPol = iota
	// carryAdd: C = CF, after ADDS/ADCS and the lowerings that insert CF into C directly.
	carryAdd
	// carryClear: C = 0 and CF = 0, after ANDS/TST and the other logical operations.
	carryClear
)

// ResetFlags starts a block: C holds CF in the block-boundary sense (carrySub).
func (l *Arm64Lifter) ResetFlags() {
	if l != nil {
		l.cpol = carrySub
	}
}

// normalizeCarry returns the instructions that bring C into the block-boundary sense
// (C = !CF) and records that sense.
func (l *Arm64Lifter) normalizeCarry() []string {
	if l.cpol == carrySub {
		return nil
	}
	l.cpol = carrySub
	return []string{"    cfinv"}
}

// EndBlock returns the code that ends a block falling through from its last instruction:
// the next block starts in the carrySub sense, so C is inverted when CF is live there.
func (l *Arm64Lifter) EndBlock(last disasm.Instruction) []string {
	if l == nil || last.CarryDead {
		return nil
	}
	return l.normalizeCarry()
}

// localLabel returns the label of the block of fn that starts at target, or "" when target
// is no block of fn.
func localLabel(fn *disasm.Function, target uint64) string {
	if fn == nil || fn.Blocks[target] == nil {
		return ""
	}
	return fmt.Sprintf("L_fn_0x%x_loc_0x%x", fn.EntryAddr, target)
}

// branchTo returns the unconditional transfer to the direct branch target target: a branch to
// its block when it is one of fn, a tail call when it is a known function entry, otherwise the
// dispatcher (recomp_indirect_jump with x0 = target). The caller brings C into the
// block-boundary sense first when CF is live at target.
func (l *Arm64Lifter) branchTo(fn *disasm.Function, target uint64) []string {
	if lbl := localLabel(fn, target); lbl != "" {
		return []string{"    b " + lbl}
	}
	if l.IsKnownFunc(target) {
		return []string{"    ldr x30, [sp], #16", fmt.Sprintf("    b _fn_0x%x", target)}
	}
	lines := loadImm64("x0", target)
	return append(lines, "    ldr x30, [sp], #16", "    b _recomp_indirect_jump")
}

// fallsThrough reports whether control can continue after inst at the next instruction. The
// set matches disasm.blockSuccessors: RET, LRET, UD2, HLT, JMP and INT end control flow, except
// INT 0x41 (the Orbis debugger break), which the INT lowering below turns into a no-op.
func fallsThrough(inst x86asm.Inst) bool {
	switch inst.Op {
	case x86asm.JMP, x86asm.RET, x86asm.LRET, x86asm.UD2, x86asm.HLT:
		return false
	case x86asm.INT:
		imm, ok := inst.Args[0].(x86asm.Imm)
		return ok && imm == 0x41
	}
	return true
}

// FallThrough returns the branch that continues a block at the fall-through address of its
// last instruction when the code emitted next does not start there (next is the start of the
// block emitted after this one, hasNext false at the end of the function). Blocks are emitted
// in address order, and a branch into the middle of an instruction gives overlapping
// instruction streams (see disasm.disasmBranchFollowing), so the block after this one in
// address order can belong to the other stream. At the end of a function whose last decoded
// instruction falls through (decoding stopped at a cap or a decode error), the continuation is
// a tail jump to that address instead of a return. Returns nil when the next emitted block is
// the successor, or when the last instruction never falls through. Call it after EndBlock:
// the target starts in the block-boundary carry sense.
func (l *Arm64Lifter) FallThrough(fn *disasm.Function, last disasm.Instruction, next uint64, hasNext bool) []string {
	if !fallsThrough(last.Inst) {
		return nil
	}
	target := last.Address + uint64(last.Inst.Len)
	if hasNext && next == target {
		return nil
	}
	return l.branchTo(fn, target)
}

// carryCond returns the ARM condition that is true when x86 CF == want, in the current sense.
func (l *Arm64Lifter) carryCond(want bool) string {
	if (l.cpol == carrySub) == want {
		return "cc"
	}
	return "cs"
}

// x86CondIndex maps Jcc/SETcc/CMOVcc to the x86 condition encoding (O=0 ... G=15).
func x86CondIndex(op x86asm.Op) (int, bool) {
	switch op {
	case x86asm.JO, x86asm.SETO, x86asm.CMOVO:
		return 0, true
	case x86asm.JNO, x86asm.SETNO, x86asm.CMOVNO:
		return 1, true
	case x86asm.JB, x86asm.SETB, x86asm.CMOVB:
		return 2, true
	case x86asm.JAE, x86asm.SETAE, x86asm.CMOVAE:
		return 3, true
	case x86asm.JE, x86asm.SETE, x86asm.CMOVE:
		return 4, true
	case x86asm.JNE, x86asm.SETNE, x86asm.CMOVNE:
		return 5, true
	case x86asm.JBE, x86asm.SETBE, x86asm.CMOVBE:
		return 6, true
	case x86asm.JA, x86asm.SETA, x86asm.CMOVA:
		return 7, true
	case x86asm.JS, x86asm.SETS, x86asm.CMOVS:
		return 8, true
	case x86asm.JNS, x86asm.SETNS, x86asm.CMOVNS:
		return 9, true
	case x86asm.JP, x86asm.SETP, x86asm.CMOVP:
		return 10, true
	case x86asm.JNP, x86asm.SETNP, x86asm.CMOVNP:
		return 11, true
	case x86asm.JL, x86asm.SETL, x86asm.CMOVL:
		return 12, true
	case x86asm.JGE, x86asm.SETGE, x86asm.CMOVGE:
		return 13, true
	case x86asm.JLE, x86asm.SETLE, x86asm.CMOVLE:
		return 14, true
	case x86asm.JG, x86asm.SETG, x86asm.CMOVG:
		return 15, true
	}
	return 0, false
}

// condFor returns the ARM condition for x86 condition cc in the current carry sense, and the
// instructions that must run before it is used (A/BE after an addition first invert C, which
// changes the tracked sense).
func (l *Arm64Lifter) condFor(cc int) ([]string, string) {
	switch cc {
	case 0, 10: // O; P (V holds PF after FP compares)
		return nil, "vs"
	case 1, 11:
		return nil, "vc"
	case 2: // B: CF = 1
		return nil, l.carryCond(true)
	case 3: // AE: CF = 0
		return nil, l.carryCond(false)
	case 4:
		return nil, "eq"
	case 5:
		return nil, "ne"
	case 6, 7: // BE: CF = 1 or ZF = 1; A: CF = 0 and ZF = 0
		var pre []string
		switch l.cpol {
		case carryClear:
			if cc == 6 {
				return nil, "eq"
			}
			return nil, "ne"
		case carryAdd:
			pre = l.normalizeCarry()
		}
		if cc == 6 {
			return pre, "ls"
		}
		return pre, "hi"
	case 8:
		return nil, "mi"
	case 9:
		return nil, "pl"
	case 12:
		return nil, "lt"
	case 13:
		return nil, "ge"
	case 14:
		return nil, "le"
	}
	return nil, "gt"
}

// NewArm64Lifter creates a new ARM64 assembly lifter.
func NewArm64Lifter(knownFuncs map[uint64]bool) *Arm64Lifter {
	return &Arm64Lifter{
		knownFuncs: knownFuncs,
	}
}

// emitUnsupported generates a diagnostic stub that calls recomp_unsupported_instruction.
func (l *Arm64Lifter) emitUnsupported(inst disasm.Instruction, reason string) []string {
	var fnEntry uint64
	if l != nil && l.currentFn != nil {
		fnEntry = l.currentFn.EntryAddr
	}
	msg := reason
	if msg == "" {
		msg = disasm.InstString(inst.Inst)
	}
	l.unsuppCounter++
	strLabel := fmt.Sprintf("L_unsupp_str_0x%x_%d", inst.Address, l.unsuppCounter)
	skipLabel := fmt.Sprintf("L_unsupp_skip_0x%x_%d", inst.Address, l.unsuppCounter)

	var lines []string
	lines = append(lines, fmt.Sprintf("    // UNSUPPORTED: %s", msg))
	lines = append(lines, loadImm64("x0", inst.Address)...)
	lines = append(lines, loadImm64("x1", fnEntry)...)
	lines = append(lines, fmt.Sprintf("    b %s", skipLabel))
	lines = append(lines, fmt.Sprintf("%s:", strLabel))
	escaped := strings.ReplaceAll(msg, "\"", "\\\"")
	lines = append(lines, fmt.Sprintf("    .asciz \"%s\"", escaped))
	lines = append(lines, "    .balign 4")
	lines = append(lines, fmt.Sprintf("%s:", skipLabel))
	lines = append(lines, fmt.Sprintf("    adr x2, %s", strLabel))
	lines = append(lines, "    b _recomp_unsupported_instruction")
	return lines
}

// IsKnownFunc reports whether addr is the entry point of a known function.
func (l *Arm64Lifter) IsKnownFunc(addr uint64) bool {
	return l != nil && l.knownFuncs != nil && l.knownFuncs[addr]
}

// loadImm32 generates instructions to load a 32-bit immediate into a 32-bit register.
func loadImm32(reg string, val uint32) []string {
	if reg == "" {
		return nil
	}
	if val <= 0xffff {
		return []string{fmt.Sprintf("    mov %s, #0x%x", reg, val)}
	}
	low := val & 0xffff
	hi := (val >> 16) & 0xffff
	res := []string{fmt.Sprintf("    movz %s, #0x%x", reg, low)}
	if hi != 0 {
		res = append(res, fmt.Sprintf("    movk %s, #0x%x, lsl #16", reg, hi))
	}
	return res
}

// loadImm64 generates instructions to load an arbitrary 64-bit immediate into a register.
func loadImm64(reg string, val uint64) []string {
	if reg == "" {
		return nil
	}
	if val == 0 {
		return []string{fmt.Sprintf("    mov %s, #0", reg)}
	}
	if val <= 0xffff {
		return []string{fmt.Sprintf("    movz %s, #0x%x", reg, val)}
	}
	var lines []string
	first := true
	for shift := 0; shift < 64; shift += 16 {
		chunk := (val >> shift) & 0xffff
		if chunk != 0 {
			if first {
				lines = append(lines, fmt.Sprintf("    movz %s, #0x%x, lsl #%d", reg, chunk, shift))
				first = false
			} else {
				lines = append(lines, fmt.Sprintf("    movk %s, #0x%x, lsl #%d", reg, chunk, shift))
			}
		}
	}
	if len(lines) == 0 {
		lines = append(lines, fmt.Sprintf("    mov %s, #0", reg))
	}
	return lines
}

// defMemSz determines the effective memory/operand size from an instruction.
func defMemSz(inst disasm.Instruction) int {
	sz := inst.Inst.MemBytes
	if sz == 0 && inst.Inst.DataSize > 0 {
		sz = inst.Inst.DataSize / 8
	}
	if sz == 0 {
		sz = 8
	}
	return sz
}

// unsupportedAddressing reports memory operand forms that computeEffAddr cannot express, which
// therefore trap instead of accessing a wrong address. None occurs in TLOU or U4 (CUSA00557,
// CUSA04529):
//   - the address-size prefix (addr32): the address would have to wrap at 4 GiB;
//   - MOV with a 64-bit absolute address (moffs, opcodes A0-A3): x86asm reports it like a
//     RIP-relative operand and the displacement does not fit the 32-bit path;
//   - GS: the guest GS base is not kept anywhere.
//
// NOP and the prefetches do not access memory and are exempt.
func unsupportedAddressing(inst x86asm.Inst) string {
	switch inst.Op {
	case x86asm.NOP, x86asm.PREFETCHT0, x86asm.PREFETCHNTA, x86asm.PREFETCHT1, x86asm.PREFETCHT2, x86asm.PREFETCHW:
		return ""
	}
	for _, a := range inst.Args {
		if a == nil {
			break
		}
		m, ok := a.(x86asm.Mem)
		if !ok {
			continue
		}
		switch {
		case inst.AddrSize == 32:
			return "addr32 memory operand (32-bit address wrap not lowered)"
		case inst.Op == x86asm.MOV && inst.Opcode>>24 >= 0xa0 && inst.Opcode>>24 <= 0xa3:
			return "mov with a 64-bit absolute address (moffs) not lowered"
		case m.Segment == x86asm.GS:
			return "GS segment override not lowered"
		}
	}
	return ""
}

// computeEffAddr generates instructions to compute a guest memory effective address into targetReg (usually x16).
func computeEffAddr(mem x86asm.Mem, nextPC uint64, targetReg string) []string {
	var lines []string
	disp := int64(int32(mem.Disp))

	// 1. RIP-relative addressing: [rip + disp]. x86asm's VEX decoder leaves Base unset for
	// RIP-relative operands; it can be told from the absolute [disp32] form (a SIB byte with no
	// base and no index) by Scale, which only a SIB byte sets.
	if mem.Base == x86asm.RIP || (mem.Segment == 0 && mem.Base == 0 && mem.Index == 0 && mem.Scale == 0) {
		effAddr := uint64(int64(nextPC) + disp)
		return loadImm64(targetReg, effAddr)
	}

	// 2. FS: [fs_base + disp]; with a base or index register the address is computed as usual
	// and fs_base added at the end.
	if mem.Segment == x86asm.FS && (mem.Base != 0 || mem.Index != 0) {
		plain := mem
		plain.Segment = 0
		if plain.Base == x86asm.RIP {
			lines = append(lines, loadImm64(targetReg, uint64(int64(nextPC)+disp))...)
		} else {
			lines = append(lines, computeEffAddr(plain, nextPC, targetReg)...)
		}
		return append(lines, fmt.Sprintf("    add %s, %s, x27", targetReg, targetReg))
	}
	if mem.Segment == x86asm.FS {
		if disp != 0 {
			if disp >= 0 && disp <= 4095 {
				lines = append(lines, fmt.Sprintf("    add %s, x27, #%d", targetReg, disp))
			} else if disp < 0 && -disp <= 4095 {
				lines = append(lines, fmt.Sprintf("    sub %s, x27, #%d", targetReg, -disp))
			} else {
				lines = append(lines, loadImm64(targetReg, uint64(disp))...)
				lines = append(lines, fmt.Sprintf("    add %s, x27, %s", targetReg, targetReg))
			}
		} else {
			lines = append(lines, fmt.Sprintf("    mov %s, x27", targetReg))
		}
		return lines
	}

	// 3. Base register
	hasBase := false
	if mem.Base != 0 {
		if armReg, ok := arm64RegMap[mem.Base]; ok {
			lines = append(lines, fmt.Sprintf("    mov %s, %s", targetReg, armReg.X))
			hasBase = true
		}
	}

	// 4. Index * Scale
	if mem.Index != 0 {
		if armIdx, ok := arm64RegMap[mem.Index]; ok {
			shift := 0
			switch mem.Scale {
			case 2:
				shift = 1
			case 4:
				shift = 2
			case 8:
				shift = 3
			}
			if hasBase {
				if shift > 0 {
					lines = append(lines, fmt.Sprintf("    add %s, %s, %s, lsl #%d", targetReg, targetReg, armIdx.X, shift))
				} else {
					lines = append(lines, fmt.Sprintf("    add %s, %s, %s", targetReg, targetReg, armIdx.X))
				}
			} else {
				if shift > 0 {
					lines = append(lines, fmt.Sprintf("    lsl %s, %s, #%d", targetReg, armIdx.X, shift))
				} else {
					lines = append(lines, fmt.Sprintf("    mov %s, %s", targetReg, armIdx.X))
				}
				hasBase = true
			}
		}
	}

	if !hasBase {
		lines = append(lines, fmt.Sprintf("    mov %s, #0", targetReg))
	}

	// 5. Displacement
	if disp != 0 {
		if disp > 0 && disp <= 4095 {
			lines = append(lines, fmt.Sprintf("    add %s, %s, #%d", targetReg, targetReg, disp))
		} else if disp < 0 && -disp <= 4095 {
			lines = append(lines, fmt.Sprintf("    sub %s, %s, #%d", targetReg, targetReg, -disp))
		} else {
			scratch := "x7"
			if targetReg == "x7" {
				scratch = "x6"
			}
			lines = append(lines, loadImm64(scratch, uint64(disp))...)
			lines = append(lines, fmt.Sprintf("    add %s, %s, %s", targetReg, targetReg, scratch))
		}
	}

	return lines
}

// readRegTo generates code to read the value of an x86 register into targetArmReg (e.g. x16 / w16).
func readRegTo(reg x86asm.Reg, targetX string, targetW string) ([]string, bool) {
	desc, ok := arm64RegMap[reg]
	if !ok {
		return nil, false
	}
	var lines []string
	if desc.IsHigh8 {
		lines = append(lines, fmt.Sprintf("    ubfx %s, %s, #8, #8", targetW, desc.W))
	} else if desc.Size == 1 {
		lines = append(lines, fmt.Sprintf("    and %s, %s, #0xff", targetW, desc.W))
	} else if desc.Size == 2 {
		lines = append(lines, fmt.Sprintf("    and %s, %s, #0xffff", targetW, desc.W))
	} else if desc.Size == 4 {
		lines = append(lines, fmt.Sprintf("    mov %s, %s", targetW, desc.W))
	} else {
		lines = append(lines, fmt.Sprintf("    mov %s, %s", targetX, desc.X))
	}
	return lines, true
}

// writeRegFrom generates code to write the value in srcArmReg (e.g. x16 / w16) into an x86 register.
func writeRegFrom(reg x86asm.Reg, srcX string, srcW string) ([]string, bool) {
	desc, ok := arm64RegMap[reg]
	if !ok {
		return nil, false
	}
	var lines []string
	if desc.IsHigh8 {
		lines = append(lines, fmt.Sprintf("    bfi %s, %s, #8, #8", desc.X, srcX))
	} else if desc.Size == 1 {
		lines = append(lines, fmt.Sprintf("    bfi %s, %s, #0, #8", desc.X, srcX))
	} else if desc.Size == 2 {
		lines = append(lines, fmt.Sprintf("    bfi %s, %s, #0, #16", desc.X, srcX))
	} else if desc.Size == 4 {
		lines = append(lines, fmt.Sprintf("    mov %s, %s", desc.W, srcW)) // zeroes upper 32 bits
	} else {
		lines = append(lines, fmt.Sprintf("    mov %s, %s", desc.X, srcX))
	}
	return lines, true
}

// loadImmToReg writes an immediate value into an x86 register respecting x86 register sizing.
func loadImmToReg(reg x86asm.Reg, val uint64) ([]string, bool) {
	desc, ok := arm64RegMap[reg]
	if !ok {
		return nil, false
	}
	var lines []string
	if desc.IsHigh8 {
		lines = append(lines, fmt.Sprintf("    mov w16, #0x%x", val&0xff))
		lines = append(lines, fmt.Sprintf("    bfi %s, x16, #8, #8", desc.X))
	} else if desc.Size == 1 {
		lines = append(lines, fmt.Sprintf("    mov w16, #0x%x", val&0xff))
		lines = append(lines, fmt.Sprintf("    bfi %s, x16, #0, #8", desc.X))
	} else if desc.Size == 2 {
		lines = append(lines, fmt.Sprintf("    mov w16, #0x%x", val&0xffff))
		lines = append(lines, fmt.Sprintf("    bfi %s, x16, #0, #16", desc.X))
	} else if desc.Size == 4 {
		lines = append(lines, loadImm32(desc.W, uint32(val))...)
	} else {
		lines = append(lines, loadImm64(desc.X, val)...)
	}
	return lines, true
}

// readMemTo generates instructions to read memory into target ARM register.
func readMemTo(mem x86asm.Mem, nextPC uint64, size int, targetX string, targetW string) []string {
	var lines []string
	lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
	switch size {
	case 1:
		lines = append(lines, fmt.Sprintf("    ldrb %s, [x28, x16]", targetW))
	case 2:
		lines = append(lines, fmt.Sprintf("    ldrh %s, [x28, x16]", targetW))
	case 4:
		lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", targetW))
	default:
		lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x16]", targetX))
	}
	return lines
}

// writeMemFrom generates instructions to write from ARM register to guest memory.
func writeMemFrom(mem x86asm.Mem, nextPC uint64, size int, srcX string, srcW string) []string {
	var lines []string
	addrReg := "x16"
	if srcX == "x16" || srcW == "w16" {
		addrReg = "x17"
	}
	lines = append(lines, computeEffAddr(mem, nextPC, addrReg)...)
	switch size {
	case 1:
		lines = append(lines, fmt.Sprintf("    strb %s, [x28, %s]", srcW, addrReg))
	case 2:
		lines = append(lines, fmt.Sprintf("    strh %s, [x28, %s]", srcW, addrReg))
	case 4:
		lines = append(lines, fmt.Sprintf("    str %s, [x28, %s]", srcW, addrReg))
	default:
		lines = append(lines, fmt.Sprintf("    str %s, [x28, %s]", srcX, addrReg))
	}
	return lines
}

func invertArm64Cond(cond string) string {
	switch cond {
	case "eq":
		return "ne"
	case "ne":
		return "eq"
	case "cc":
		return "cs"
	case "cs":
		return "cc"
	case "hi":
		return "ls"
	case "ls":
		return "hi"
	case "lt":
		return "ge"
	case "ge":
		return "lt"
	case "le":
		return "gt"
	case "gt":
		return "le"
	case "mi":
		return "pl"
	case "pl":
		return "mi"
	case "vs":
		return "vc"
	case "vc":
		return "vs"
	default:
		return "ne"
	}
}

// LiftInstructionToBuf translates a single x86 instruction into ARM64 assembly instructions.
func (l *Arm64Lifter) LiftInstructionToBuf(inst disasm.Instruction, nextPC uint64, fn *disasm.Function, dst []string) ([]string, error) {
	start := len(dst)
	lines, err := l.liftInstructionInternal(inst, nextPC, fn, dst)
	if err == nil && inst.Inst.Op == x86asm.CALL {
		// Flags are dead after a call (System V ABI); any sense is consistent.
		l.cpol = carrySub
	}
	if err == nil {
		lines = fixUpperHalves(inst.Inst, lines, start, fn != nil && fn.UsesYmm)
		if !l.noTSO {
			lines = l.applyTSO(inst, lines, start)
		}
	}
	return lines, err
}

var (
	highVecRe  = regexp.MustCompile(`\b[bhsdqv](1[6-9]|2[0-9]|3[01])\b`)
	upperTokRe = regexp.MustCompile(`\b([bhsdqv])U(1[6-9]|2[0-9]|3[01])\b`)
)

// vecArg0IsSource reports VEX instructions whose first operand is a source, not a destination.
func vecArg0IsSource(op x86asm.Op) bool {
	switch op {
	case x86asm.VCOMISS, x86asm.VUCOMISS, x86asm.VCOMISD, x86asm.VUCOMISD,
		x86asm.VPTEST, x86asm.VTESTPS, x86asm.VTESTPD:
		return true
	}
	return false
}

// fixUpperHalves post-processes the lowering of one instruction (lines[start:]):
//  1. A scratch register in v16-v31 that is also an operand's upper half (a "vU<n>"
//     placeholder from getVectorDesc) is renamed to a free one, then the placeholders become
//     the real register names.
//  2. In a function with YMM code (ymmFn), every v16-v31 register the lowering touches other
//     than the upper halves it legitimately reads or writes is saved and restored around it.
//     The legitimate ones are those of its YMM operands and, for a VEX instruction, the upper
//     half of its XMM destination (VEX.128 zeroes it). In functions without YMM instructions no
//     upper half is live (YMM registers are caller-saved), so the scratch use is harmless.
//     Restoring also makes legacy SSE instructions keep their destination's upper half.
func fixUpperHalves(inst x86asm.Inst, lines []string, start int, ymmFn bool) []string {
	body := lines[start:]
	var opnd, scratch uint32
	for _, ln := range body {
		t := strings.TrimSpace(ln)
		if t == "" || t[0] == '.' || strings.HasPrefix(t, "//") {
			continue
		}
		for _, m := range upperTokRe.FindAllStringSubmatch(t, -1) {
			n := 0
			fmt.Sscanf(m[2], "%d", &n)
			opnd |= 1 << uint(n)
		}
		if i := strings.Index(t, "//"); i >= 0 {
			t = t[:i]
		}
		for _, m := range highVecRe.FindAllStringSubmatch(t, -1) {
			n := 0
			fmt.Sscanf(m[1], "%d", &n)
			scratch |= 1 << uint(n)
		}
	}
	if opnd == 0 && (scratch == 0 || !ymmFn) {
		return lines
	}
	// The upper halves the instruction legitimately reads or writes: those of its YMM operands
	// and, for a VEX instruction, that of its XMM destination (VEX.128 zeroes it).
	var legit uint32
	vex := strings.HasPrefix(inst.Op.String(), "V")
	for i, a := range inst.Args {
		if a == nil {
			break
		}
		r, ok := a.(x86asm.Reg)
		if !ok {
			continue
		}
		if r >= x86asm.Y0 && r <= x86asm.Y15 {
			legit |= 1 << (16 + uint(r-x86asm.Y0))
		} else if i == 0 && vex && !vecArg0IsSource(inst.Op) && r >= x86asm.X0 && r <= x86asm.X15 {
			legit |= 1 << (16 + uint(r-x86asm.X0))
		}
	}
	// 1. Rename scratch registers that collide with an operand's upper half (whether or not
	// the lowering refers to that upper half), then resolve the placeholders.
	reserved := opnd | legit
	rename := map[int]int{}
	free := ^(reserved | scratch) & 0xffff0000
	for n := 16; n < 32; n++ {
		if scratch&reserved&(1<<uint(n)) == 0 {
			continue
		}
		for m := 31; m >= 16; m-- {
			if free&(1<<uint(m)) != 0 {
				free &^= 1 << uint(m)
				rename[n] = m
				scratch = scratch&^(1<<uint(n)) | 1<<uint(m)
				break
			}
		}
	}
	for i := start; i < len(lines); i++ {
		ln := lines[i]
		t := strings.TrimSpace(ln)
		if t == "" || t[0] == '.' || strings.HasPrefix(t, "//") {
			continue
		}
		if len(rename) > 0 {
			ln = highVecRe.ReplaceAllStringFunc(ln, func(r string) string {
				n := 0
				fmt.Sscanf(r[1:], "%d", &n)
				if m, ok := rename[n]; ok {
					return fmt.Sprintf("%c%d", r[0], m)
				}
				return r
			})
		}
		lines[i] = upperTokRe.ReplaceAllString(ln, "$1$2")
	}
	if !ymmFn || inst.Op == x86asm.VZEROUPPER || inst.Op == x86asm.VZEROALL {
		return lines
	}

	// 2. Save and restore the upper halves the lowering is not meant to change.
	for _, ln := range lines[start:] {
		f := strings.Fields(strings.TrimSpace(ln))
		if len(f) == 0 || strings.HasPrefix(f[0], "//") {
			continue
		}
		if f[0] == "ret" || f[0] == "br" || ((f[0] == "b" || strings.HasPrefix(f[0], "b.")) && len(f) > 1 && strings.HasPrefix(f[1], "L_fn_")) {
			return lines // control leaves the instruction: no single restore point
		}
	}
	spill := (scratch | opnd) &^ legit
	if spill == 0 {
		return lines
	}
	var regs []int
	for n := 16; n < 32; n++ {
		if spill&(1<<uint(n)) != 0 {
			regs = append(regs, n)
		}
	}
	var save, restore []string
	for i := 0; i < len(regs); i += 2 {
		if i+1 < len(regs) {
			save = append(save, fmt.Sprintf("    stp q%d, q%d, [sp, #-32]!", regs[i], regs[i+1]))
			restore = append([]string{fmt.Sprintf("    ldp q%d, q%d, [sp], #32", regs[i], regs[i+1])}, restore...)
		} else {
			save = append(save, fmt.Sprintf("    str q%d, [sp, #-16]!", regs[i]))
			restore = append([]string{fmt.Sprintf("    ldr q%d, [sp], #16", regs[i])}, restore...)
		}
	}
	// Keep the "// 0x...: inst" comment first.
	body = append([]string(nil), lines[start:]...)
	out := lines[:start]
	if len(body) > 0 && strings.HasPrefix(strings.TrimSpace(body[0]), "//") {
		out = append(out, body[0])
		body = body[1:]
	}
	out = append(out, save...)
	out = append(out, body...)
	out = append(out, restore...)
	return out
}

func (l *Arm64Lifter) liftInstructionInternal(inst disasm.Instruction, nextPC uint64, fn *disasm.Function, dst []string) ([]string, error) {
	if fn != l.currentFn {
		l.currentFn = fn
		l.cpol = carrySub
	}
	op := inst.Inst.Op
	args := inst.Inst.Args
	lines := dst

	comment := fmt.Sprintf("    // 0x%x: %s", inst.Address, disasm.InstString(inst.Inst))
	lines = append(lines, comment)

	if reason := unsupportedAddressing(inst.Inst); reason != "" {
		lines = append(lines, l.emitUnsupported(inst, reason)...)
		return lines, nil
	}

	if HasLockPrefix(inst.Inst) {
		if out, ok := l.liftLocked(inst, nextPC, lines); ok {
			return out, nil
		}
	}

	switch op {
	case x86asm.NOP, x86asm.FNOP, x86asm.FWAIT,
		x86asm.PREFETCHT0, x86asm.PREFETCHNTA, x86asm.PREFETCHT1, x86asm.PREFETCHT2, x86asm.PREFETCHW:
		lines = append(lines, "    nop")
		return lines, nil

	case x86asm.CLD:
		// DF is not modelled: string instructions always run forward (DF = 0, as the System V
		// ABI requires at calls).
		lines = append(lines, "    nop")
		return lines, nil

	case x86asm.STD:
		// DF = 1 (backward string operations) is not modelled; STD has no site in TLOU or U4.
		lines = append(lines, l.emitUnsupported(inst, "std: backward string operations not lowered")...)
		return lines, nil

	case x86asm.CLC, x86asm.STC, x86asm.CMC, x86asm.LAHF, x86asm.SAHF:
		return l.liftFlagsOp(inst, lines)

	case x86asm.PAUSE:
		lines = append(lines, "    yield")
		return lines, nil

	case x86asm.MFENCE:
		lines = append(lines, "    dmb ish")
		return lines, nil

	case x86asm.LFENCE:
		lines = append(lines, "    dmb ishld")
		return lines, nil

	case x86asm.SFENCE:
		lines = append(lines, "    dmb ishst")
		return lines, nil

	case x86asm.UD2:
		lines = append(lines, "    brk #0")
		return lines, nil

	case x86asm.MOV:
		return l.liftMov(inst, nextPC, lines)

	case x86asm.MOVZX:
		return l.liftMovzx(inst, nextPC, lines)

	case x86asm.MOVSX, x86asm.MOVSXD:
		return l.liftMovsx(inst, nextPC, lines)

	case x86asm.LEA:
		mem, ok := args[1].(x86asm.Mem)
		if !ok {
			lines = append(lines, "    brk #0 // unsupported lea operand")
			return lines, nil
		}
		dstReg, ok := args[0].(x86asm.Reg)
		if !ok {
			lines = append(lines, "    brk #0 // unsupported lea dst")
			return lines, nil
		}
		desc, ok := arm64RegMap[dstReg]
		if !ok {
			lines = append(lines, "    brk #0 // unmapped lea dst")
			return lines, nil
		}
		lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
		if desc.Size == 4 {
			lines = append(lines, fmt.Sprintf("    mov %s, w16", desc.W))
		} else if desc.Size == 2 {
			lines = append(lines, fmt.Sprintf("    bfi %s, x16, #0, #16", desc.X))
		} else {
			lines = append(lines, fmt.Sprintf("    mov %s, x16", desc.X))
		}
		return lines, nil

	case x86asm.ADD, x86asm.SUB, x86asm.AND, x86asm.OR, x86asm.XOR:
		return l.liftAlu(inst, nextPC, lines)

	case x86asm.CMP, x86asm.TEST:
		return l.liftCmpTest(inst, nextPC, lines)

	case x86asm.INC, x86asm.DEC:
		return l.liftIncDec(inst, nextPC, lines)

	case x86asm.NEG, x86asm.NOT:
		return l.liftNegNot(inst, nextPC, lines)

	case x86asm.SHL, x86asm.SHR, x86asm.SAR, x86asm.ROL, x86asm.ROR:
		return l.liftShift(inst, nextPC, lines)

	case x86asm.RCL, x86asm.RCR:
		return l.liftRclRcr(inst, nextPC, lines)

	case x86asm.IMUL:
		return l.liftImul(inst, nextPC, lines)

	case x86asm.MUL:
		return l.liftMul(inst, nextPC, lines)

	case x86asm.IDIV, x86asm.DIV:
		return l.liftDiv(inst, nextPC, lines)

	case x86asm.XADD:
		return l.liftXadd(inst, nextPC, lines)

	case x86asm.CMPXCHG:
		return l.liftCmpxchg(inst, nextPC, lines)

	case x86asm.CMPXCHG16B:
		return l.liftCmpxchg16b(inst, nextPC, lines)

	case x86asm.CMPXCHG8B:
		return l.liftCmpxchg8b(inst, nextPC, lines)

	case x86asm.XCHG:
		return l.liftXchg(inst, nextPC, lines)

	case x86asm.CDQ:
		lines = append(lines, "    asr w22, w19, #31") // EDX = sign of EAX; RDX upper half cleared
		return lines, nil

	case x86asm.CQO:
		lines = append(lines, "    asr x22, x19, #63")
		return lines, nil

	case x86asm.CDQE:
		lines = append(lines, "    sxtw x19, w19")
		return lines, nil

	case x86asm.CWDE:
		lines = append(lines, "    sxth w19, w19")
		return lines, nil

	case x86asm.CWD:
		lines = append(
			lines,
			"    sxth w16, w19",
			"    asr  w16, w16, #16",
			"    bfi  x22, x16, #0, #16",
		)
		return lines, nil

	case x86asm.CBW:
		lines = append(lines, "    sxtb w16, w19")
		lines = append(lines, "    bfi x19, x16, #0, #16")
		return lines, nil

	case x86asm.BSWAP:
		if reg, ok := args[0].(x86asm.Reg); ok {
			if desc, okDesc := arm64RegMap[reg]; okDesc {
				if desc.Size == 4 {
					lines = append(lines, fmt.Sprintf("    rev %s, %s", desc.W, desc.W))
				} else {
					lines = append(lines, fmt.Sprintf("    rev %s, %s", desc.X, desc.X))
				}
				return lines, nil
			}
		}
		lines = append(lines, "    brk #0 // unsupported bswap")
		return lines, nil

	case x86asm.BSF, x86asm.TZCNT, x86asm.BSR, x86asm.LZCNT:
		return l.liftBitScan(inst, nextPC, lines)

	case x86asm.JMP:
		if rel, ok := args[0].(x86asm.Rel); ok {
			target := uint64(int64(nextPC) + int64(rel))
			// A target outside the function is a tail call to a function (flags dead, so the
			// liveness pass marks CF dead) or, for a target that is no function entry, an
			// unknown continuation that starts in the block-boundary sense.
			if !inst.CarryDead {
				lines = append(lines, l.normalizeCarry()...)
			}
			lines = append(lines, l.branchTo(fn, target)...)
			return lines, nil
		}
		// emitLocalJumpTable dispatches an indirect jump to a block of the function. The table
		// targets start in the block-boundary carry sense, and the flags (which may be live into
		// them) survive the dispatch: the range check saves and restores NZCV, the block compare
		// chain uses EOR/CBZ.
		emitLocalJumpTable := func(targetReg string) {
			if fn != nil && len(fn.Blocks) > 1 && fn.EndAddr > fn.EntryAddr {
				if !inst.CarryDead {
					lines = append(lines, l.normalizeCarry()...)
				}
				outside := fmt.Sprintf("L_fn_0x%x_outside_0x%x", fn.EntryAddr, inst.Address)
				skipLocal := fmt.Sprintf("L_fn_0x%x_not_local_0x%x", fn.EntryAddr, inst.Address)
				lines = append(lines, "    mrs x17, nzcv")
				lines = append(lines, loadImm64("x16", fn.EntryAddr)...)
				lines = append(lines, fmt.Sprintf("    cmp %s, x16", targetReg))
				lines = append(lines, fmt.Sprintf("    b.lo %s", outside))
				lines = append(lines, loadImm64("x16", fn.EndAddr)...)
				lines = append(lines, fmt.Sprintf("    cmp %s, x16", targetReg))
				lines = append(lines, fmt.Sprintf("    b.hs %s", outside))
				lines = append(lines, "    msr nzcv, x17")

				for _, blockAddr := range fn.BlockOrder {
					lines = append(lines, loadImm64("x16", blockAddr)...)
					lines = append(lines, fmt.Sprintf("    eor x16, x16, %s", targetReg))
					lines = append(lines, fmt.Sprintf("    cbz x16, L_fn_0x%x_loc_0x%x", fn.EntryAddr, blockAddr))
				}
				lines = append(lines, fmt.Sprintf("    b %s", skipLocal))
				lines = append(lines, fmt.Sprintf("%s:", outside))
				lines = append(lines, "    msr nzcv, x17")
				lines = append(lines, fmt.Sprintf("%s:", skipLocal))
			}
		}

		if reg, ok := args[0].(x86asm.Reg); ok {
			if desc, okDesc := arm64RegMap[reg]; okDesc {
				lines = append(lines, fmt.Sprintf("    mov x0, %s", desc.X))
				emitLocalJumpTable("x0")
				lines = append(lines, "    ldr x30, [sp], #16")
				lines = append(lines, "    b _recomp_indirect_jump")
				return lines, nil
			}
		}
		if mem, ok := args[0].(x86asm.Mem); ok {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, "    ldr x0, [x28, x16]")
			emitLocalJumpTable("x0")
			lines = append(lines, "    ldr x30, [sp], #16")
			lines = append(lines, "    b _recomp_indirect_jump")
			return lines, nil
		}
		lines = append(lines, "    brk #0 // unsupported indirect jmp")
		return lines, nil

	case x86asm.CALL:
		if rel, ok := args[0].(x86asm.Rel); ok {
			target := uint64(int64(nextPC) + int64(rel))
			lines = append(lines, "    sub x26, x26, #8")
			lines = append(lines, loadImm64("x16", nextPC)...)
			lines = append(lines, "    str x16, [x28, x26]")
			if l.IsKnownFunc(target) {
				lines = append(lines, fmt.Sprintf("    bl _fn_0x%x", target))
			} else {
				lines = append(lines, loadImm64("x0", target)...)
				lines = append(lines, "    bl _recomp_indirect_call")
			}
			return lines, nil
		}
		if reg, ok := args[0].(x86asm.Reg); ok {
			if desc, okDesc := arm64RegMap[reg]; okDesc {
				lines = append(lines, "    sub x26, x26, #8")
				lines = append(lines, loadImm64("x16", nextPC)...)
				lines = append(lines, "    str x16, [x28, x26]")
				lines = append(lines, fmt.Sprintf("    mov x0, %s", desc.X))
				lines = append(lines, "    bl _recomp_indirect_call")
				return lines, nil
			}
		}
		if mem, ok := args[0].(x86asm.Mem); ok {
			lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
			lines = append(lines, "    ldr x0, [x28, x16]")
			lines = append(lines, "    sub x26, x26, #8")
			lines = append(lines, loadImm64("x16", nextPC)...)
			lines = append(lines, "    str x16, [x28, x26]")
			lines = append(lines, "    bl _recomp_indirect_call")
			return lines, nil
		}
		lines = append(lines, "    brk #0 // unsupported indirect call")
		return lines, nil

	case x86asm.RET:
		lines = append(lines, "    ldr x16, [x28, x26]")
		lines = append(lines, "    add x26, x26, #8")
		lines = append(lines, "    ldr x30, [sp], #16")
		lines = append(lines, "    ret")
		return lines, nil

	case x86asm.PUSH:
		if reg, ok := args[0].(x86asm.Reg); ok {
			if reg == x86asm.RSP {
				// PUSH RSP stores the value RSP had before the instruction.
				lines = append(lines, "    sub x16, x26, #8", "    str x26, [x28, x16]", "    mov x26, x16")
				return lines, nil
			}
			if desc, okDesc := arm64RegMap[reg]; okDesc {
				lines = append(lines, "    sub x26, x26, #8")
				lines = append(lines, fmt.Sprintf("    str %s, [x28, x26]", desc.X))
				return lines, nil
			}
		} else if imm, ok := args[0].(x86asm.Imm); ok {
			lines = append(lines, "    sub x26, x26, #8")
			lines = append(lines, loadImm64("x16", uint64(imm))...)
			lines = append(lines, "    str x16, [x28, x26]")
			return lines, nil
		} else if mem, ok := args[0].(x86asm.Mem); ok {
			lines = append(lines, readMemTo(mem, nextPC, 8, "x16", "w16")...)
			lines = append(lines, "    sub x26, x26, #8")
			lines = append(lines, "    str x16, [x28, x26]")
			return lines, nil
		}
		lines = append(lines, "    brk #0 // unsupported push")
		return lines, nil

	case x86asm.POP:
		if reg, ok := args[0].(x86asm.Reg); ok {
			if reg == x86asm.RSP {
				// POP RSP: RSP is the value loaded from the old top; the increment is lost.
				lines = append(lines, "    ldr x26, [x28, x26]")
				return lines, nil
			}
			if desc, okDesc := arm64RegMap[reg]; okDesc {
				lines = append(lines, fmt.Sprintf("    ldr %s, [x28, x26]", desc.X))
				lines = append(lines, "    add x26, x26, #8")
				return lines, nil
			}
		} else if mem, ok := args[0].(x86asm.Mem); ok {
			lines = append(lines, "    ldr x16, [x28, x26]")
			lines = append(lines, "    add x26, x26, #8")
			lines = append(lines, writeMemFrom(mem, nextPC, 8, "x16", "w16")...)
			return lines, nil
		}
		lines = append(lines, l.emitUnsupported(inst, "unsupported pop")...)
		return lines, nil
	case x86asm.LEAVE:
		lines = l.liftLeave(lines)
		return lines, nil

	case x86asm.ENTER:
		lines = l.liftEnter(inst, lines)
		return lines, nil

	case x86asm.SHLD, x86asm.SHRD:
		return l.liftShldShrd(inst, nextPC, lines)

	// Vector & Floating Point Moves
	case x86asm.MOVAPS, x86asm.MOVUPS, x86asm.MOVDQA, x86asm.MOVDQU, x86asm.MOVAPD, x86asm.MOVUPD,
		x86asm.LDDQU: // LDDQU is an unaligned load (MOVDQU)
		return l.liftVectorMove128(inst, nextPC, lines)

	case x86asm.PMULHRSW, x86asm.VPMULHRSW:
		return l.liftPmulhrsw(inst, nextPC, lines)

	case x86asm.PSIGNB, x86asm.PSIGNW, x86asm.PSIGND, x86asm.VPSIGNB, x86asm.VPSIGNW, x86asm.VPSIGND:
		return l.liftPsign(inst, nextPC, lines)

	case x86asm.MOVSS:
		return l.liftVectorMove32(inst, nextPC, lines)

	case x86asm.MOVSD:
		if isStringMovsd(inst) {
			return l.liftStringOp(inst, 4, lines)
		}
		return l.liftVectorMove64(inst, nextPC, lines)

	case x86asm.MOVSD_XMM:
		return l.liftVectorMove64(inst, nextPC, lines)

	case x86asm.MOVD, x86asm.MOVQ, x86asm.VMOVD, x86asm.VMOVQ:
		return l.liftMovdMovq(inst, nextPC, lines)

	case x86asm.MOVDDUP, x86asm.VMOVDDUP:
		return l.liftMovddup(inst, nextPC, lines)

	case x86asm.MOVLPS, x86asm.VMOVLPS, x86asm.MOVHPS, x86asm.VMOVHPS,
		x86asm.MOVLPD, x86asm.VMOVLPD, x86asm.MOVHPD, x86asm.VMOVHPD,
		x86asm.MOVLHPS, x86asm.VMOVLHPS, x86asm.MOVHLPS, x86asm.VMOVHLPS:
		return l.liftMovlpsMovhps(inst, nextPC, lines)

	case x86asm.BEXTR:
		return l.liftBextr(inst, nextPC, lines)

	case x86asm.ANDN:
		return l.liftAndn(inst, nextPC, lines)

	case x86asm.BLSI, x86asm.BLSR, x86asm.BLSMSK, x86asm.BZHI,
		x86asm.SHLX, x86asm.SHRX, x86asm.SARX, x86asm.RORX, x86asm.MULX:
		return l.liftBmi(inst, nextPC, lines)

	case x86asm.ROUNDSD, x86asm.ROUNDSS, x86asm.VROUNDSD, x86asm.VROUNDSS,
		x86asm.ROUNDPS, x86asm.ROUNDPD, x86asm.VROUNDPS, x86asm.VROUNDPD:
		return l.liftRound(inst, nextPC, lines)

	case x86asm.XORPS, x86asm.XORPD, x86asm.PXOR:
		return l.liftVectorLogic(inst, "eor", lines)

	case x86asm.ANDPS, x86asm.ANDPD, x86asm.PAND:
		return l.liftVectorLogic(inst, "and", lines)

	case x86asm.ORPS, x86asm.ORPD, x86asm.POR:
		return l.liftVectorLogic(inst, "orr", lines)

	case x86asm.ADDSS:
		return l.liftFpAlu(inst, nextPC, "fadd", false, lines)
	case x86asm.SUBSS:
		return l.liftFpAlu(inst, nextPC, "fsub", false, lines)
	case x86asm.MULSS:
		return l.liftFpAlu(inst, nextPC, "fmul", false, lines)
	case x86asm.DIVSS:
		return l.liftFpAlu(inst, nextPC, "fdiv", false, lines)

	case x86asm.ADDSD:
		return l.liftFpAlu(inst, nextPC, "fadd", true, lines)
	case x86asm.SUBSD:
		return l.liftFpAlu(inst, nextPC, "fsub", true, lines)
	case x86asm.MULSD:
		return l.liftFpAlu(inst, nextPC, "fmul", true, lines)
	case x86asm.DIVSD:
		return l.liftFpAlu(inst, nextPC, "fdiv", true, lines)

	case x86asm.UCOMISS, x86asm.COMISS:
		return l.liftFpCmp(inst, nextPC, false, lines)

	case x86asm.UCOMISD, x86asm.COMISD:
		return l.liftFpCmp(inst, nextPC, true, lines)

	case x86asm.CVTSI2SS, x86asm.CVTSI2SD, x86asm.VCVTSI2SS, x86asm.VCVTSI2SD:
		return l.liftCvtsi2fp(inst, nextPC, lines)

	case x86asm.CVTSS2SI, x86asm.CVTSD2SI, x86asm.CVTTSS2SI, x86asm.CVTTSD2SI,
		x86asm.VCVTSS2SI, x86asm.VCVTSD2SI, x86asm.VCVTTSS2SI, x86asm.VCVTTSD2SI:
		return l.liftCvtfp2si(inst, nextPC, lines)

	case x86asm.CVTSS2SD, x86asm.CVTSD2SS, x86asm.VCVTSS2SD, x86asm.VCVTSD2SS:
		return l.liftFpConvert(inst, nextPC, lines)

	case x86asm.ADC:
		return l.liftAdcSbb(inst, nextPC, false, lines)

	case x86asm.SBB:
		return l.liftAdcSbb(inst, nextPC, true, lines)

	case x86asm.BT, x86asm.BTS, x86asm.BTR, x86asm.BTC:
		return l.liftBt(inst, nextPC, lines)

	case x86asm.PSHUFD, x86asm.VPSHUFD, x86asm.SHUFPS, x86asm.VSHUFPS, x86asm.SHUFPD, x86asm.VSHUFPD:
		return l.liftShuf(inst, nextPC, lines)

	case x86asm.PSHUFLW, x86asm.VPSHUFLW, x86asm.PSHUFHW, x86asm.VPSHUFHW:
		return l.liftPshufw(inst, nextPC, lines)

	case x86asm.PALIGNR, x86asm.VPALIGNR:
		return l.liftPalignr(inst, nextPC, lines)

	case x86asm.MOVBE:
		return l.liftMovbe(inst, nextPC, lines)

	case x86asm.RDTSC:
		lines = l.liftRdtsc(lines)
		return lines, nil

	case x86asm.CPUID:
		lines = l.liftCpuid(lines)
		return lines, nil

	case x86asm.XGETBV:
		lines = l.liftXgetbv(lines)
		return lines, nil

	case x86asm.POPCNT:
		return l.liftPopcnt(inst, nextPC, lines)

	case x86asm.PCMPGTD, x86asm.PMULUDQ,
		x86asm.PADDB, x86asm.PADDW, x86asm.PADDD, x86asm.PADDQ,
		x86asm.PSUBB, x86asm.PSUBW, x86asm.PSUBD, x86asm.PSUBQ,
		x86asm.PCMPEQB, x86asm.PCMPEQW, x86asm.PCMPEQD,
		x86asm.PMAXUB, x86asm.PMINUB, x86asm.PMAXSB, x86asm.PMINSB,
		x86asm.PMAXUW, x86asm.PMINUW, x86asm.PMAXSW, x86asm.PMINSW,
		x86asm.PMAXUD, x86asm.PMINUD, x86asm.PMAXSD, x86asm.PMINSD,
		x86asm.PMULLD, x86asm.PMULLW,
		x86asm.PADDSB, x86asm.PADDSW, x86asm.PADDUSB,
		x86asm.PSUBSB, x86asm.PSUBUSB,
		x86asm.PCMPGTB, x86asm.PCMPGTW, x86asm.PCMPGTQ:
		return l.liftVectorBinary(inst, nextPC, lines)

	case x86asm.BLENDPS, x86asm.BLENDPD, x86asm.PBLENDW,
		x86asm.BLENDVPS, x86asm.BLENDVPD, x86asm.PBLENDVB:
		return l.liftBlend(inst, nextPC, lines)

	case x86asm.PSRLDQ, x86asm.VPSRLDQ:
		return l.liftVectorByteShift(inst, false, lines)

	case x86asm.PSLLDQ, x86asm.VPSLLDQ:
		return l.liftVectorByteShift(inst, true, lines)

	case x86asm.PSLLW, x86asm.PSLLD, x86asm.PSLLQ,
		x86asm.PSRLW, x86asm.PSRLD, x86asm.PSRLQ,
		x86asm.PSRAW, x86asm.PSRAD:
		return l.liftVectorShift(inst, nextPC, lines)

	case x86asm.VMOVUPS, x86asm.VMOVUPD, x86asm.VMOVAPS, x86asm.VMOVAPD, x86asm.VMOVDQU, x86asm.VMOVDQA,
		x86asm.VLDDQU: // VLDDQU is an unaligned load (VMOVDQU)
		return l.liftAvxMove(inst, nextPC, lines)

	case x86asm.VMOVSS:
		return l.liftAvxScalarMove(inst, nextPC, false, lines)

	case x86asm.VMOVSD:
		return l.liftAvxScalarMove(inst, nextPC, true, lines)

	case x86asm.VZEROUPPER, x86asm.VZEROALL:
		lines = l.liftVzeroupper(lines)
		return lines, nil

	case x86asm.VBROADCASTSS:
		return l.liftVbroadcastss(inst, nextPC, lines)

	case x86asm.VBROADCASTSD:
		return l.liftVbroadcastsd(inst, nextPC, lines)

	case x86asm.VBROADCASTF128:
		return l.liftVbroadcastf128(inst, nextPC, lines)

	case x86asm.VEXTRACTF128, x86asm.VEXTRACTI128:
		return l.liftVextractf128(inst, nextPC, lines)

	case x86asm.VINSERTF128, x86asm.VINSERTI128:
		return l.liftVinsertf128(inst, nextPC, lines)

	case x86asm.EXTRACTPS, x86asm.VEXTRACTPS:
		return l.liftExtractps(inst, nextPC, lines)

	case x86asm.INSERTPS, x86asm.VINSERTPS:
		return l.liftInsertps(inst, nextPC, lines)

	case x86asm.VPERM2F128:
		return l.liftVperm2f128(inst, nextPC, lines)

	case x86asm.VPERMILPS:
		return l.liftVpermil(inst, nextPC, false, lines)

	case x86asm.VPERMILPD:
		return l.liftVpermil(inst, nextPC, true, lines)

	case x86asm.PINSRB, x86asm.VPINSRB:
		return l.liftPinsr(inst, 1, nextPC, lines)

	case x86asm.PINSRW, x86asm.VPINSRW:
		return l.liftPinsr(inst, 2, nextPC, lines)

	case x86asm.PINSRD, x86asm.VPINSRD:
		return l.liftPinsr(inst, 4, nextPC, lines)

	case x86asm.PINSRQ, x86asm.VPINSRQ:
		return l.liftPinsr(inst, 8, nextPC, lines)

	case x86asm.PEXTRB, x86asm.VPEXTRB:
		return l.liftPextr(inst, 1, nextPC, lines)

	case x86asm.PEXTRW, x86asm.VPEXTRW:
		return l.liftPextr(inst, 2, nextPC, lines)

	case x86asm.PEXTRD, x86asm.VPEXTRD:
		return l.liftPextr(inst, 4, nextPC, lines)

	case x86asm.PEXTRQ, x86asm.VPEXTRQ:
		return l.liftPextr(inst, 8, nextPC, lines)

	case x86asm.PMOVMSKB, x86asm.VPMOVMSKB:
		return l.liftPmovmskb(inst, nextPC, lines)

	case x86asm.MOVMSKPS, x86asm.VMOVMSKPS:
		return l.liftMovmskps(inst, nextPC, lines)

	case x86asm.MOVMSKPD, x86asm.VMOVMSKPD:
		return l.liftMovmskpd(inst, nextPC, lines)

	case x86asm.VXORPS, x86asm.VXORPD, x86asm.VPXOR:
		return l.liftAvx3Op(inst, nextPC, "eor", false, lines)

	case x86asm.VANDPS, x86asm.VANDPD, x86asm.VPAND:
		return l.liftAvx3Op(inst, nextPC, "and", false, lines)

	case x86asm.VORPS, x86asm.VORPD, x86asm.VPOR:
		return l.liftAvx3Op(inst, nextPC, "orr", false, lines)

	case x86asm.DPPS, x86asm.VDPPS:
		return l.liftDpps(inst, nextPC, lines)

	case x86asm.ADDPS, x86asm.VADDPS:
		return l.liftAvx3Op(inst, nextPC, "fadd", false, lines)

	case x86asm.VMULPS, x86asm.MULPS:
		return l.liftAvx3Op(inst, nextPC, "fmul", false, lines)

	case x86asm.VSUBPS, x86asm.SUBPS:
		return l.liftAvx3Op(inst, nextPC, "fsub", false, lines)

	case x86asm.VDIVPS, x86asm.DIVPS:
		return l.liftAvx3Op(inst, nextPC, "fdiv", false, lines)

	case x86asm.MAXPS, x86asm.VMAXPS:
		return l.liftAvx3Op(inst, nextPC, "fmax", false, lines)

	case x86asm.MINPS, x86asm.VMINPS:
		return l.liftAvx3Op(inst, nextPC, "fmin", false, lines)

	case x86asm.SQRTSS, x86asm.VSQRTSS:
		return l.liftSqrt(inst, false, true, false, nextPC, lines)

	case x86asm.SQRTSD, x86asm.VSQRTSD:
		return l.liftSqrt(inst, true, true, false, nextPC, lines)

	case x86asm.SQRTPS, x86asm.VSQRTPS:
		return l.liftSqrt(inst, false, false, false, nextPC, lines)

	case x86asm.SQRTPD, x86asm.VSQRTPD:
		return l.liftSqrt(inst, true, false, false, nextPC, lines)

	case x86asm.RSQRTSS, x86asm.VRSQRTSS:
		return l.liftSqrt(inst, false, true, true, nextPC, lines)

	case x86asm.RSQRTPS, x86asm.VRSQRTPS:
		return l.liftSqrt(inst, false, false, true, nextPC, lines)

	case x86asm.VADDSS:
		return l.liftAvxScalarAlu(inst, nextPC, "fadd", false, lines)

	case x86asm.VMULSS:
		return l.liftAvxScalarAlu(inst, nextPC, "fmul", false, lines)

	case x86asm.VSUBSS:
		return l.liftAvxScalarAlu(inst, nextPC, "fsub", false, lines)

	case x86asm.VDIVSS:
		return l.liftAvxScalarAlu(inst, nextPC, "fdiv", false, lines)

	case x86asm.VADDSD:
		return l.liftAvxScalarAlu(inst, nextPC, "fadd", true, lines)

	case x86asm.VMULSD:
		return l.liftAvxScalarAlu(inst, nextPC, "fmul", true, lines)

	case x86asm.VSUBSD:
		return l.liftAvxScalarAlu(inst, nextPC, "fsub", true, lines)

	case x86asm.VDIVSD:
		return l.liftAvxScalarAlu(inst, nextPC, "fdiv", true, lines)

	case x86asm.VUCOMISS, x86asm.VCOMISS:
		return l.liftFpCmp(inst, nextPC, false, lines)

	case x86asm.VUCOMISD, x86asm.VCOMISD:
		return l.liftFpCmp(inst, nextPC, true, lines)

	case x86asm.VMAXSD, x86asm.VMINSD, x86asm.VMAXSS, x86asm.VMINSS:
		return l.liftAvxMinMax(inst, nextPC, lines)

	case x86asm.VPADDQ, x86asm.VPADDD, x86asm.VPADDW, x86asm.VPADDB:
		return l.liftAvxIntAlu(inst, nextPC, "add", lines)

	case x86asm.VPSUBQ, x86asm.VPSUBD, x86asm.VPSUBW, x86asm.VPSUBB:
		return l.liftAvxIntAlu(inst, nextPC, "sub", lines)

	case x86asm.VPCMPEQQ, x86asm.VPCMPEQD, x86asm.VPCMPEQW, x86asm.VPCMPEQB:
		return l.liftAvxIntAlu(inst, nextPC, "cmeq", lines)

	case x86asm.VPMULLD, x86asm.VPMULLW:
		return l.liftAvxIntAlu(inst, nextPC, "mul", lines)

	case x86asm.VPMAXUD, x86asm.VPMAXUW, x86asm.VPMAXUB:
		return l.liftAvxIntAlu(inst, nextPC, "umax", lines)

	case x86asm.VPMINUD, x86asm.VPMINUW, x86asm.VPMINUB:
		return l.liftAvxIntAlu(inst, nextPC, "umin", lines)

	case x86asm.VPMAXSD, x86asm.VPMAXSW, x86asm.VPMAXSB:
		return l.liftAvxIntAlu(inst, nextPC, "smax", lines)

	case x86asm.VPMINSD, x86asm.VPMINSW, x86asm.VPMINSB:
		return l.liftAvxIntAlu(inst, nextPC, "smin", lines)

	case x86asm.VPCMPGTQ, x86asm.VPCMPGTD, x86asm.VPCMPGTW, x86asm.VPCMPGTB:
		return l.liftAvxIntAlu(inst, nextPC, "cmgt", lines)

	case x86asm.VPADDSB, x86asm.VPADDSW:
		return l.liftAvxIntAlu(inst, nextPC, "sqadd", lines)

	case x86asm.VPADDUSB, x86asm.VPADDUSW:
		return l.liftAvxIntAlu(inst, nextPC, "uqadd", lines)

	case x86asm.VPSUBSB, x86asm.VPSUBSW:
		return l.liftAvxIntAlu(inst, nextPC, "sqsub", lines)

	case x86asm.VPSUBUSB, x86asm.VPSUBUSW:
		return l.liftAvxIntAlu(inst, nextPC, "uqsub", lines)

	case x86asm.PANDN, x86asm.VPANDN:
		return l.liftAvxPandn(inst, nextPC, lines)

	case x86asm.PUNPCKLDQ, x86asm.VPUNPCKLDQ,
		x86asm.PUNPCKHDQ, x86asm.VPUNPCKHDQ,
		x86asm.UNPCKLPS, x86asm.VUNPCKLPS,
		x86asm.UNPCKHPS, x86asm.VUNPCKHPS,
		x86asm.UNPCKLPD, x86asm.VUNPCKLPD,
		x86asm.UNPCKHPD, x86asm.VUNPCKHPD,
		x86asm.PUNPCKLBW, x86asm.VPUNPCKLBW,
		x86asm.PUNPCKHBW, x86asm.VPUNPCKHBW,
		x86asm.PUNPCKLWD, x86asm.VPUNPCKLWD,
		x86asm.PUNPCKHWD, x86asm.VPUNPCKHWD,
		x86asm.PUNPCKLQDQ, x86asm.VPUNPCKLQDQ,
		x86asm.PUNPCKHQDQ, x86asm.VPUNPCKHQDQ:
		return l.liftAvxUnpck(inst, nextPC, lines)

	case x86asm.VPMULUDQ:
		return l.liftAvxPmuludq(inst, nextPC, lines)

	case x86asm.VBLENDPS, x86asm.VBLENDPD, x86asm.VPBLENDW, x86asm.VPBLENDD,
		x86asm.VBLENDVPS, x86asm.VBLENDVPD, x86asm.VPBLENDVB:
		return l.liftBlend(inst, nextPC, lines)

	case x86asm.PMOVZXBW, x86asm.VPMOVZXBW,
		x86asm.PMOVZXBD, x86asm.VPMOVZXBD,
		x86asm.PMOVZXBQ, x86asm.VPMOVZXBQ,
		x86asm.PMOVZXWD, x86asm.VPMOVZXWD,
		x86asm.PMOVZXWQ, x86asm.VPMOVZXWQ,
		x86asm.PMOVZXDQ, x86asm.VPMOVZXDQ,
		x86asm.PMOVSXBW, x86asm.VPMOVSXBW,
		x86asm.PMOVSXBD, x86asm.VPMOVSXBD,
		x86asm.PMOVSXBQ, x86asm.VPMOVSXBQ,
		x86asm.PMOVSXWD, x86asm.VPMOVSXWD,
		x86asm.PMOVSXWQ, x86asm.VPMOVSXWQ,
		x86asm.PMOVSXDQ, x86asm.VPMOVSXDQ:
		return l.liftVectorExtend(inst, nextPC, lines)

	case x86asm.ADDPD, x86asm.VADDPD, x86asm.SUBPD, x86asm.VSUBPD,
		x86asm.MULPD, x86asm.VMULPD, x86asm.DIVPD, x86asm.VDIVPD,
		x86asm.MAXPD, x86asm.VMAXPD, x86asm.MINPD, x86asm.VMINPD:
		return l.liftAvxDoubleAlu(inst, nextPC, lines)

	case x86asm.VHADDPD, x86asm.VHADDPS:
		return l.liftVhadd(inst, nextPC, lines)

	case x86asm.PTEST, x86asm.VPTEST, x86asm.VTESTPD, x86asm.VTESTPS:
		return l.liftPtestVtest(inst, nextPC, lines)

	case x86asm.PACKSSDW, x86asm.VPACKSSDW,
		x86asm.PACKSSWB, x86asm.VPACKSSWB,
		x86asm.PACKUSDW, x86asm.VPACKUSDW,
		x86asm.PACKUSWB, x86asm.VPACKUSWB:
		return l.liftPack(inst, nextPC, lines)

	case x86asm.RCPPS, x86asm.VRCPPS, x86asm.RCPSS, x86asm.VRCPSS:
		return l.liftRcp(inst, nextPC, lines)

	case x86asm.HSUBPS, x86asm.VHSUBPS, x86asm.HSUBPD, x86asm.VHSUBPD:
		return l.liftVhsub(inst, nextPC, lines)

	case x86asm.PHADDD, x86asm.VPHADDD, x86asm.PHADDW, x86asm.VPHADDW:
		return l.liftVphadd(inst, nextPC, lines)

	case x86asm.VPSLLQ, x86asm.VPSRLQ,
		x86asm.VPSLLD, x86asm.VPSRLD, x86asm.VPSRAD,
		x86asm.VPSLLW, x86asm.VPSRLW, x86asm.VPSRAW:
		return l.liftAvxShift(inst, nextPC, lines)

	case x86asm.MOVSHDUP, x86asm.VMOVSHDUP, x86asm.MOVSLDUP, x86asm.VMOVSLDUP:
		return l.liftMovsdup(inst, nextPC, lines)

	case x86asm.PSHUFB, x86asm.VPSHUFB:
		return l.liftPshufb(inst, nextPC, lines)

	case x86asm.VANDNPS, x86asm.VANDNPD:
		return l.liftVandn(inst, nextPC, lines)

	case x86asm.CMPPS, x86asm.VCMPPS, x86asm.CMPSS, x86asm.VCMPSS,
		x86asm.CMPPD, x86asm.VCMPPD, x86asm.VCMPSD:
		return l.liftFpVectorCmp(inst, nextPC, lines)

	case x86asm.CMPSD:
		if isStringCmpsd(inst) {
			return l.liftStringOp(inst, 4, lines)
		}
		return l.liftFpVectorCmp(inst, nextPC, lines)

	case x86asm.CVTDQ2PS, x86asm.VCVTDQ2PS,
		x86asm.CVTTPS2DQ, x86asm.VCVTTPS2DQ,
		x86asm.CVTPS2DQ, x86asm.VCVTPS2DQ,
		x86asm.CVTPD2PS, x86asm.VCVTPD2PS,
		x86asm.CVTPS2PD, x86asm.VCVTPS2PD,
		x86asm.CVTTPD2DQ, x86asm.VCVTTPD2DQ,
		x86asm.CVTDQ2PD, x86asm.VCVTDQ2PD,
		x86asm.VCVTPH2PS, x86asm.VCVTPS2PH:
		return l.liftCvtdq(inst, nextPC, lines)

	case disasm.OpEXTRQ, disasm.OpINSERTQ:
		return l.liftSSE4A(inst, lines)

	case x86asm.MOVNTPS, x86asm.VMOVNTPS,
		x86asm.MOVNTDQ, x86asm.VMOVNTDQ,
		x86asm.MOVNTDQA, x86asm.VMOVNTDQA:
		return l.liftMovnt(inst, nextPC, lines)

	case x86asm.ADDSUBPS, x86asm.VADDSUBPS,
		x86asm.ADDSUBPD, x86asm.VADDSUBPD:
		return l.liftAddsub(inst, nextPC, lines)

	case x86asm.PAVGB, x86asm.VPAVGB, x86asm.PAVGW, x86asm.VPAVGW:
		return l.liftPavg(inst, nextPC, lines)

	case x86asm.PABSB, x86asm.VPABSB, x86asm.PABSW, x86asm.VPABSW, x86asm.PABSD, x86asm.VPABSD:
		return l.liftPabs(inst, nextPC, lines)

	case x86asm.PSADBW, x86asm.VPSADBW:
		return l.liftPsadbw(inst, nextPC, lines)

	case x86asm.PMADDWD, x86asm.VPMADDWD:
		return l.liftPmaddwd(inst, nextPC, lines)

	case x86asm.FLD, x86asm.FILD, x86asm.FLDZ, x86asm.FLD1,
		x86asm.FST, x86asm.FSTP, x86asm.FIST, x86asm.FISTP, x86asm.FISTTP,
		x86asm.FXCH, x86asm.FCHS, x86asm.FABS, x86asm.FSQRT,
		x86asm.FADD, x86asm.FADDP, x86asm.FSUB, x86asm.FSUBP, x86asm.FSUBR, x86asm.FSUBRP,
		x86asm.FMUL, x86asm.FMULP, x86asm.FDIV, x86asm.FDIVP, x86asm.FDIVR, x86asm.FDIVRP,
		x86asm.FIADD, x86asm.FISUB, x86asm.FIMUL, x86asm.FIDIV,
		x86asm.FUCOMI, x86asm.FUCOMIP, x86asm.FCOMI,
		x86asm.FCMOVB, x86asm.FCMOVE, x86asm.FCMOVBE, x86asm.FCMOVU,
		x86asm.FCMOVNB, x86asm.FCMOVNE, x86asm.FCMOVNBE, x86asm.FCMOVNU,
		x86asm.FNSTSW, x86asm.FLDCW, x86asm.FNSTCW, x86asm.FNCLEX,
		x86asm.FSCALE, x86asm.FPREM, x86asm.FPREM1,
		x86asm.FFREE, x86asm.FFREEP,
		x86asm.FNSTENV, x86asm.FLDENV,
		x86asm.FCOS, x86asm.FLDL2T, x86asm.FRNDINT, x86asm.FXTRACT,
		x86asm.FCOM, x86asm.FCOMP, x86asm.FCOMPP, x86asm.FICOM, x86asm.FICOMP:
		return l.liftFpu(inst, nextPC, lines)

	case x86asm.HLT, x86asm.INT, x86asm.INTO, x86asm.ICEBP:
		// Software interrupts. Sites in the lifted TLOU / U4 code: INT 0x41 8233 / 13037,
		// INT3 (CC) 300 / 212, INT 0x44 151 / 151, INTO and ICEBP 0. The C backend made every
		// INT a no-op. Here only INT 0x41 is one: it is the Orbis debugger break that Naughty
		// Dog's HALT/ASSERT macros issue after printing the message; without a debugger the
		// process continues, and TLOU relies on it to recover from "A frame did not complete in
		// 2.00 seconds" (Task 8). INT3 (assert breaks in game code) and INT 0x44 (a trap with
		// a code in ECX in the system modules' failure paths) stop a PS4 process without a
		// debugger, and no asm run so far has reached one, so they keep trapping (BRK, reported
		// by the signal handler), as do HLT, INTO and ICEBP.
		if imm, ok := inst.Inst.Args[0].(x86asm.Imm); ok && inst.Inst.Op == x86asm.INT && imm == 0x41 {
			lines = append(lines, "    // int 0x41: debugger break, no debugger attached")
			return lines, nil
		}
		lines = append(lines, "    brk #0")
		return lines, nil

	case x86asm.EMMS:
		lines = append(lines, "    // emms")
		return lines, nil

	// String instructions
	case x86asm.MOVSB:
		return l.liftStringOp(inst, 1, lines)
	case x86asm.MOVSW:
		return l.liftStringOp(inst, 2, lines)
	case x86asm.MOVSQ:
		return l.liftStringOp(inst, 8, lines)
	case x86asm.STOSB:
		return l.liftStringOp(inst, 1, lines)
	case x86asm.STOSW:
		return l.liftStringOp(inst, 2, lines)
	case x86asm.STOSD:
		return l.liftStringOp(inst, 4, lines)
	case x86asm.STOSQ:
		return l.liftStringOp(inst, 8, lines)
	case x86asm.LODSB:
		return l.liftStringOp(inst, 1, lines)
	case x86asm.LODSW:
		return l.liftStringOp(inst, 2, lines)
	case x86asm.LODSD:
		return l.liftStringOp(inst, 4, lines)
	case x86asm.LODSQ:
		return l.liftStringOp(inst, 8, lines)
	case x86asm.SCASB:
		return l.liftStringOp(inst, 1, lines)
	case x86asm.SCASW:
		return l.liftStringOp(inst, 2, lines)
	case x86asm.SCASD:
		return l.liftStringOp(inst, 4, lines)
	case x86asm.SCASQ:
		return l.liftStringOp(inst, 8, lines)
	case x86asm.CMPSB:
		return l.liftStringOp(inst, 1, lines)
	case x86asm.CMPSW:
		return l.liftStringOp(inst, 2, lines)
	case x86asm.CMPSQ:
		return l.liftStringOp(inst, 8, lines)

	// MXCSR Operations
	case x86asm.LDMXCSR, x86asm.STMXCSR, x86asm.VLDMXCSR, x86asm.VSTMXCSR:
		return l.liftMxcsr(inst, nextPC, lines)

	// Masked Vector Moves
	case x86asm.VMASKMOVPS, x86asm.VMASKMOVPD:
		return l.liftMaskmov(inst, nextPC, lines)

	// Vector Arithmetic & Multiplies
	case x86asm.PMADDUBSW, x86asm.VPMADDUBSW:
		return l.liftPmaddupsw(inst, nextPC, lines)

	case x86asm.PMULDQ, x86asm.VPMULDQ:
		return l.liftPmuldq(inst, nextPC, lines)

	case x86asm.PMULHW, x86asm.VPMULHW:
		return l.liftPmulhw(inst, nextPC, lines)

	case x86asm.VPHMINPOSUW:
		return l.liftVphminposuw(inst, nextPC, lines)

	// AES Crypto Instructions
	case x86asm.AESENC, x86asm.VAESENC,
		x86asm.AESENCLAST, x86asm.VAESENCLAST,
		x86asm.AESDEC, x86asm.VAESDEC,
		x86asm.AESDECLAST, x86asm.VAESDECLAST,
		x86asm.AESIMC, x86asm.VAESIMC,
		x86asm.AESKEYGENASSIST, x86asm.VAESKEYGENASSIST:
		return l.liftAes(inst, nextPC, lines)

	// Stack & Flags
	case x86asm.PUSHFQ:
		return l.liftPushfq(inst, lines)

	case x86asm.POPFQ:
		return l.liftPopfq(inst, lines)

	// XLAT
	case x86asm.XLATB:
		return l.liftXlatb(lines)

	// Loop & Jump on CX zero
	case x86asm.LOOP, x86asm.LOOPE, x86asm.LOOPNE, x86asm.JRCXZ:
		return l.liftLoop(inst, nextPC, fn, lines)

	// Far return
	case x86asm.LRET:
		lines = append(lines, "    ldr x30, [sp], #16", "    ret")
		return lines, nil

	// FXSAVE & FXRSTOR
	case x86asm.FXSAVE:
		return l.liftFxsave(inst, nextPC, lines)

	case x86asm.FXRSTOR:
		return l.liftFxrstor(inst, nextPC, lines)

	// Horizontal SIMD
	case x86asm.PHSUBD, x86asm.VPHSUBD:
		return l.liftVphsubd(inst, nextPC, lines)

	// Carryless Multiply
	case x86asm.PCLMULQDQ, x86asm.VPCLMULQDQ:
		return l.liftPclmulqdq(inst, nextPC, lines)

	// String comparison
	case x86asm.PCMPISTRI, x86asm.VPCMPISTRI:
		return l.liftPcmpistri(inst, nextPC, lines)

	case x86asm.PCMPESTRI, x86asm.VPCMPESTRI:
		return l.liftPcmpestri(inst, nextPC, lines)

	// CRC32
	case x86asm.CRC32:
		return l.liftCrc32(inst, nextPC, lines)

	// System / segment descriptors
	case x86asm.LAR:
		return l.liftLar(inst, nextPC, lines)

	case x86asm.STR:
		return l.liftStr(inst, nextPC, lines)

	default:
		if cc, ok := x86CondIndex(op); ok {
			opName := op.String()
			if strings.HasPrefix(opName, "SET") {
				pre, cond := l.condFor(cc)
				lines = append(lines, pre...)
				return l.liftSetcc(inst, nextPC, cond, lines)
			}
			if strings.HasPrefix(opName, "CMOV") {
				pre, cond := l.condFor(cc)
				lines = append(lines, pre...)
				return l.liftCmovcc(inst, nextPC, cond, lines)
			}
		}

		// Check conditional branches
		if cc, ok := x86CondIndex(op); ok {
			// The targets start in the block-boundary carry sense; invert first if CF is live.
			if !inst.CarryDead {
				lines = append(lines, l.normalizeCarry()...)
			}
			pre, cond := l.condFor(cc)
			lines = append(lines, pre...)
			if rel, ok := args[0].(x86asm.Rel); ok {
				target := uint64(int64(nextPC) + int64(rel))
				if lbl := localLabel(fn, target); lbl != "" {
					lines = append(lines, fmt.Sprintf("    b.%s %s", cond, lbl))
				} else {
					inv := invertArm64Cond(cond)
					skipLabel := fmt.Sprintf("L_fn_0x%x_skip_%d", fn.EntryAddr, l.labelCounter)
					l.labelCounter++
					lines = append(lines, fmt.Sprintf("    b.%s %s", inv, skipLabel))
					lines = append(lines, l.branchTo(fn, target)...)
					lines = append(lines, fmt.Sprintf("%s:", skipLabel))
				}
				return lines, nil
			}
		}

		// Fallback diagnostic
		lines = append(lines, l.emitUnsupported(inst, "")...)
		return lines, nil
	}
}

func isStringMovsd(inst disasm.Instruction) bool {
	if len(inst.Inst.Args) >= 2 {
		_, ok0 := inst.Inst.Args[0].(x86asm.Mem)
		_, ok1 := inst.Inst.Args[1].(x86asm.Mem)
		if ok0 && ok1 {
			return true
		}
	}
	return false
}

func isStringCmpsd(inst disasm.Instruction) bool {
	if len(inst.Inst.Args) >= 2 {
		_, ok0 := inst.Inst.Args[0].(x86asm.Mem)
		_, ok1 := inst.Inst.Args[1].(x86asm.Mem)
		if ok0 && ok1 {
			return true
		}
	}
	return false
}
