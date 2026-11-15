package lifter

import (
	"strconv"
	"strings"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

// x86 memory ordering (TSO) on ARM.
//
// x86 keeps loads ordered with loads, stores with stores and loads before later stores; ARM
// orders none of them. As in the C backend (MEM_LD*/MEM_ST* in recomp_context.h), guest memory
// accesses whose address is not RSP- or RBP-relative use load-acquire (LDAPR, RCpc) and
// store-release (STLR): lock-free guest code that publishes data with plain stores and reads it
// with plain loads keeps its ordering. RSP/RBP-relative accesses are thread-private stack and
// frame accesses and stay plain, as do vector, FP and x87 accesses (the C backend did not order
// them either, and TLOU was correct without it).
//
// The ordering is applied to the lowering of one instruction after it is generated: every scalar
// GPR load or store of the form [x28, xN] (guest memory base + guest address; xN != x26, the
// guest RSP) is rewritten to base-only addressing, which LDAPR/STLR require, with a scratch
// register from x0-x7 that the lowering does not use:
//
//	ldr  w17, [x28, x16]   ->  add x7, x28, x16; ldapr w17, [x7]
//	ldrsh x0, [x28, x16]   ->  add x7, x28, x16; ldaprh w0, [x7]; sxth x0, w0
//	str  w17, [x28, x16]   ->  add x7, x28, x16; stlr w17, [x7]
//
// 2-, 4- and 8-byte accesses first check for a 16-byte boundary crossing (orderedAccess).
//
// Why LDAPR (RCpc) and not LDAR (RCsc): the two differ in one case only. An LDAR may not be
// satisfied before an earlier STLR to another address has become visible; an LDAPR may. x86-TSO
// allows exactly that reordering (a store followed by a load from another address may be
// observed in the other order: the store-buffer relaxation). x86 code that needs it closed uses
// MFENCE, lowered to dmb ish, or a locked instruction, lowered to an acquire-release atomic
// (casal, ldaddal, swpal, ldaxr/stlxr) that no later load passes. The orderings TSO does
// guarantee, load->load, load->store and store->store, LDAPR + STLR guarantee as well, so LDAR
// would only add ordering x86 never promised. The spec left "LDAPR or LDAR" to measurement; it
// was settled by this argument, and Task 9's TSO on/off A/B measured the cost of the ordering.

// SetTSO turns x86 memory ordering for non-stack guest memory on (the default) or off.
func (l *Arm64Lifter) SetTSO(on bool) {
	l.noTSO = !on
}

// TSO reports whether x86 memory ordering is on.
func (l *Arm64Lifter) TSO() bool {
	return !l.noTSO
}

// scalarAccess is a plain guest scalar access line: "    ldr w17, [x28, x16]".
type scalarAccess struct {
	indent string // leading whitespace, kept by the rewritten instructions
	op     string // ldr/ldrb/ldrh/ldrsb/ldrsh/ldrsw/str/strb/strh
	data   string // data register, e.g. w17 or xzr
	addr   string // guest address register, e.g. x16
}

// parseScalarAccess parses the form
//
//	^\s+(ldr|ldrb|ldrh|ldrsb|ldrsh|ldrsw|str|strb|strh)\s+([wx](?:\d+|zr)),\s*\[x28,\s*(x\d+)\]\s*(//.*)?$
//
// by hand: this runs on every lowered line of an instruction whose access is ordered.
func parseScalarAccess(line string) (scalarAccess, bool) {
	var a scalarAccess
	i := 0
	for i < len(line) && (line[i] == ' ' || line[i] == '\t') {
		i++
	}
	if i == 0 {
		return a, false
	}
	a.indent = line[:i]
	rest := line[i:]

	mnemonic, rest, ok := cutAsmToken(rest)
	if !ok || !isScalarAccessOp(mnemonic) {
		return scalarAccess{}, false
	}
	a.op = mnemonic

	k := 0
	for k < len(rest) && (rest[k] == ' ' || rest[k] == '\t') {
		k++
	}
	if k == 0 {
		return scalarAccess{}, false
	}
	rest = rest[k:]

	end := 0
	for end < len(rest) && rest[end] != ',' {
		end++
	}
	if end == len(rest) || end == 0 {
		return scalarAccess{}, false
	}
	a.data = rest[:end]
	if !isScalarDataReg(a.data) {
		return scalarAccess{}, false
	}
	rest = rest[end+1:]

	k = 0
	for k < len(rest) && (rest[k] == ' ' || rest[k] == '\t') {
		k++
	}
	rest = rest[k:]
	if !strings.HasPrefix(rest, "[x28,") {
		return scalarAccess{}, false
	}
	rest = rest[len("[x28,"):]
	k = 0
	for k < len(rest) && (rest[k] == ' ' || rest[k] == '\t') {
		k++
	}
	rest = rest[k:]

	end = 0
	for end < len(rest) && rest[end] != ']' {
		end++
	}
	if end == len(rest) {
		return scalarAccess{}, false
	}
	a.addr = rest[:end]
	if !isXRegName(a.addr) {
		return scalarAccess{}, false
	}
	rest = rest[end+1:]

	// Only whitespace and an optional // comment may follow.
	k = 0
	for k < len(rest) && (rest[k] == ' ' || rest[k] == '\t') {
		k++
	}
	rest = rest[k:]
	if rest != "" && !strings.HasPrefix(rest, "//") {
		return scalarAccess{}, false
	}
	return a, true
}

// cutAsmToken returns the word at the start of s and the rest after its trailing whitespace.
func cutAsmToken(s string) (token, rest string, ok bool) {
	i := 0
	for i < len(s) && s[i] != ' ' && s[i] != '\t' {
		i++
	}
	if i == 0 || i == len(s) {
		return "", "", false
	}
	return s[:i], s[i:], true
}

func isScalarAccessOp(op string) bool {
	switch op {
	case "ldr", "ldrb", "ldrh", "ldrsb", "ldrsh", "ldrsw", "str", "strb", "strh":
		return true
	}
	return false
}

// isScalarDataReg matches [wx](?:\d+|zr).
func isScalarDataReg(reg string) bool {
	if len(reg) < 2 || (reg[0] != 'w' && reg[0] != 'x') {
		return false
	}
	if reg[1:] == "zr" {
		return true
	}
	for i := 1; i < len(reg); i++ {
		if reg[i] < '0' || reg[i] > '9' {
			return false
		}
	}
	return true
}

// isXRegName matches x\d+.
func isXRegName(reg string) bool {
	if len(reg) < 2 || reg[0] != 'x' {
		return false
	}
	for i := 1; i < len(reg); i++ {
		if reg[i] < '0' || reg[i] > '9' {
			return false
		}
	}
	return true
}

// markScratchUse records every x0-x7 / w0-w7 token in code, the registers a lowering may
// hold a value in (\b[wx]([0-7])\b).
func markScratchUse(code string, used *[8]bool) {
	for i := 0; i+1 < len(code); i++ {
		if code[i] != 'w' && code[i] != 'x' {
			continue
		}
		if i > 0 && isAsmWordByte(code[i-1]) {
			continue
		}
		c := code[i+1]
		if c < '0' || c > '7' {
			continue
		}
		if i+2 < len(code) && isAsmWordByte(code[i+2]) {
			continue
		}
		used[c-'0'] = true
	}
}

// tsoApplies reports whether the instruction's guest memory accesses are ordered: it has an
// explicit memory operand that is not RSP- or RBP-relative, no vector or MMX register operand,
// and it is neither an x87 nor a string instruction
// (its implicit RSI/RDI accesses are block copies, which x86 fast-string operations do not order
// either). The SSE forms of MOVSD/CMPSD share the opcode names; their accesses are vector
// accesses, which stay plain anyway.
func tsoApplies(inst x86asm.Inst) bool {
	if isStringOp(inst.Op) || strings.HasPrefix(inst.Op.String(), "F") {
		return false // string instructions; x87
	}
	ordered := false
	for _, a := range inst.Args {
		if a == nil {
			break
		}
		if r, ok := a.(x86asm.Reg); ok && (r >= x86asm.X0 && r <= x86asm.Y15 || r >= x86asm.M0 && r <= x86asm.M7) {
			return false // vector, SIMD and MMX instructions (also their scalar element accesses)
		}
		if m, ok := a.(x86asm.Mem); ok {
			switch m.Base {
			case x86asm.RSP, x86asm.ESP, x86asm.RBP, x86asm.EBP:
				return false
			}
			ordered = true
		}
	}
	return ordered
}

func isStringOp(op x86asm.Op) bool {
	switch op {
	case x86asm.MOVSB, x86asm.MOVSW, x86asm.MOVSD, x86asm.MOVSQ, x86asm.STOSB, x86asm.STOSW, x86asm.STOSD,
		x86asm.STOSQ, x86asm.LODSB, x86asm.LODSW, x86asm.LODSD, x86asm.LODSQ, x86asm.SCASB, x86asm.SCASW,
		x86asm.SCASD, x86asm.SCASQ, x86asm.CMPSB, x86asm.CMPSW, x86asm.CMPSD, x86asm.CMPSQ:
		return true
	}
	return false
}

// applyTSO rewrites the scalar guest memory accesses of lines[start:] (the lowering of inst)
// into acquire loads and release stores.
func (l *Arm64Lifter) applyTSO(inst disasm.Instruction, lines []string, start int) []string {
	if !tsoApplies(inst.Inst) {
		return lines
	}
	body := lines[start:]
	hit := false
	for _, ln := range body {
		if a, ok := parseScalarAccess(ln); ok && a.addr != "x26" {
			hit = true
			break
		}
	}
	if !hit {
		return lines
	}
	var used [8]bool
	for _, ln := range body {
		code := ln
		if i := strings.Index(code, "//"); i >= 0 {
			code = code[:i]
		}
		markScratchUse(code, &used)
	}
	// The scratch register is written and consumed inside each rewritten access only, so it
	// must merely not hold a value of the lowering at that point. A lowering that calls a
	// helper (bl/blr) passes and receives values in registers it names in its own text (the
	// helper's other caller-saved registers are dead after the call), so a register absent
	// from the text is free on both sides of the call as well.
	scratch := ""
	for n := 7; n >= 0; n-- {
		if !used[n] {
			scratch = "x" + strconv.Itoa(n)
			break
		}
	}
	out := make([]string, start, len(lines)+4)
	copy(out, lines[:start])
	for _, ln := range body {
		a, ok := parseScalarAccess(ln)
		if !ok || a.addr == "x26" {
			out = append(out, ln)
			continue
		}
		out = append(out, l.orderedAccess(a.indent, a.op, a.data, a.addr, scratch, ln)...)
	}
	return out
}

// orderedAccess returns the acquire/release form of the plain access line (op data, [x28, addr]).
//
// LDAPR/STLR fault on an access that crosses a 16-byte boundary, also with FEAT_LSE2. Such an
// access (the guest address with its last byte differs in bit 4; the memory base is page
// aligned) takes the plain access with a fence instead, as recomp_ld*/recomp_st* did in the C
// backend: DMB ISHLD after a load (acquire), DMB ISH before a store (release). The check needs
// no flags. Byte accesses cannot cross. Without a free scratch register every access takes the
// fenced form.
func (l *Arm64Lifter) orderedAccess(ind, op, data, addr, a, plain string) []string {
	isLoad := strings.HasPrefix(op, "ldr")
	fence := func() []string {
		if isLoad {
			return []string{plain, ind + "dmb ishld"}
		}
		return []string{ind + "dmb ish", plain}
	}
	if a == "" {
		return fence()
	}
	var ordered []string
	w := "w" + data[1:]
	switch op {
	case "ldr":
		ordered = []string{ind + "ldapr " + data + ", [" + a + "]"}
	case "ldrb", "ldrh":
		ordered = []string{ind + "ldapr" + op[3:] + " " + data + ", [" + a + "]"}
	case "ldrsb", "ldrsh":
		ordered = []string{ind + "ldapr" + op[4:] + " " + w + ", [" + a + "]",
			ind + "sxt" + op[4:] + " " + data + ", " + w}
	case "ldrsw":
		ordered = []string{ind + "ldapr " + w + ", [" + a + "]", ind + "sxtw " + data + ", " + w}
	case "str":
		ordered = []string{ind + "stlr " + data + ", [" + a + "]"}
	default: // strb, strh
		ordered = []string{ind + "stlr" + op[3:] + " " + data + ", [" + a + "]"}
	}
	size := 0
	switch op {
	case "ldrb", "ldrsb", "strb":
		size = 1
	case "ldrh", "ldrsh", "strh":
		size = 2
	case "ldrsw":
		size = 4
	}
	if size == 0 {
		size = 4
		if data[0] == 'x' {
			size = 8
		}
	}
	host := ind + "add " + a + ", x28, " + addr
	if size == 1 {
		return append([]string{host}, ordered...)
	}
	l.labelCounter++
	fast := "L_tso_ok_" + uitoa(l.labelCounter)
	done := "L_tso_done_" + uitoa(l.labelCounter)
	out := make([]string, 0, len(ordered)+7)
	out = append(out,
		ind+"add "+a+", "+addr+", #"+strconv.Itoa(size-1),
		ind+"eor "+a+", "+a+", "+addr,
		ind+"tbz "+a+", #4, "+fast, // no 16-byte boundary crossed
	)
	out = append(out, fence()...)
	out = append(out, ind+"b "+done, fast+":", host)
	out = append(out, ordered...)
	return append(out, done+":")
}
