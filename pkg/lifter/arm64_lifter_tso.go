package lifter

import (
	"fmt"
	"regexp"
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

var (
	// A scalar GPR load or store to guest memory: op, data register, address register.
	guestScalarAccessRe = regexp.MustCompile(`^(\s+)(ldr|ldrb|ldrh|ldrsb|ldrsh|ldrsw|str|strb|strh)\s+([wx](?:\d+|zr)),\s*\[x28,\s*(x\d+)\]\s*(//.*)?$`)
	scratchUseRe        = regexp.MustCompile(`\b[wx]([0-7])\b`)
)

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
		if m := guestScalarAccessRe.FindStringSubmatch(ln); m != nil && m[4] != "x26" {
			hit = true
			break
		}
	}
	if !hit {
		return lines
	}
	used := map[string]bool{}
	for _, ln := range body {
		code := ln
		if i := strings.Index(code, "//"); i >= 0 {
			code = code[:i]
		}
		for _, m := range scratchUseRe.FindAllStringSubmatch(code, -1) {
			used[m[1]] = true
		}
	}
	// The scratch register is written and consumed inside each rewritten access only, so it
	// must merely not hold a value of the lowering at that point. A lowering that calls a
	// helper (bl/blr) passes and receives values in registers it names in its own text (the
	// helper's other caller-saved registers are dead after the call), so a register absent
	// from the text is free on both sides of the call as well.
	scratch := ""
	for _, n := range []string{"7", "6", "5", "4", "3", "2", "1", "0"} {
		if !used[n] {
			scratch = "x" + n
			break
		}
	}
	out := append([]string(nil), lines[:start]...)
	for _, ln := range body {
		m := guestScalarAccessRe.FindStringSubmatch(ln)
		if m == nil || m[4] == "x26" {
			out = append(out, ln)
			continue
		}
		out = append(out, l.orderedAccess(m[1], m[2], m[3], m[4], scratch, ln)...)
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
		ordered = []string{fmt.Sprintf("%sldapr %s, [%s]", ind, data, a)}
	case "ldrb", "ldrh":
		ordered = []string{fmt.Sprintf("%sldapr%s %s, [%s]", ind, op[3:], data, a)}
	case "ldrsb", "ldrsh":
		ordered = []string{fmt.Sprintf("%sldapr%s %s, [%s]", ind, op[4:], w, a),
			fmt.Sprintf("%ssxt%s %s, %s", ind, op[4:], data, w)}
	case "ldrsw":
		ordered = []string{fmt.Sprintf("%sldapr %s, [%s]", ind, w, a), fmt.Sprintf("%ssxtw %s, %s", ind, data, w)}
	case "str":
		ordered = []string{fmt.Sprintf("%sstlr %s, [%s]", ind, data, a)}
	default: // strb, strh
		ordered = []string{fmt.Sprintf("%sstlr%s %s, [%s]", ind, op[3:], data, a)}
	}
	size := map[string]int{"ldrb": 1, "ldrsb": 1, "strb": 1, "ldrh": 2, "ldrsh": 2, "strh": 2, "ldrsw": 4}[op]
	if size == 0 {
		size = 4
		if data[0] == 'x' {
			size = 8
		}
	}
	host := fmt.Sprintf("%sadd %s, x28, %s", ind, a, addr)
	if size == 1 {
		return append([]string{host}, ordered...)
	}
	l.labelCounter++
	fast := fmt.Sprintf("L_tso_ok_%d", l.labelCounter)
	done := fmt.Sprintf("L_tso_done_%d", l.labelCounter)
	out := []string{
		fmt.Sprintf("%sadd %s, %s, #%d", ind, a, addr, size-1),
		fmt.Sprintf("%seor %s, %s, %s", ind, a, a, addr),
		fmt.Sprintf("%stbz %s, #4, %s", ind, a, fast), // no 16-byte boundary crossed
	}
	out = append(out, fence()...)
	out = append(out, fmt.Sprintf("%sb %s", ind, done), fast+":", host)
	out = append(out, ordered...)
	return append(out, done+":")
}
