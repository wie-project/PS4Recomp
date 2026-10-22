package lifter

import (
	"fmt"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

// Locked read-modify-write instructions.
//
// x86 executes a LOCK-prefixed read-modify-write on memory (and XCHG with a memory operand, which
// is implicitly locked) atomically and as a full memory barrier. The lowerings here use ARMv8.1
// LSE atomics with acquire and release semantics (LDADDAL, LDCLRAL, LDEORAL, LDSETAL, SWPAL,
// CASAL, CASPAL) where the operation has an LSE form, and an LDAXR/STLXR loop followed by
// DMB ISH where it does not (NEG, ADC, SBB). An AL atomic that writes memory is ordered after
// every earlier and before every later access of the thread, so it needs no extra barrier. A
// compare-and-swap that fails does not write and has only acquire semantics; the failure path
// of CMPXCHG/CMPXCHG8B/CMPXCHG16B therefore runs a DMB ISH.
//
// Alignment: an LSE atomic or exclusive access must not cross a 16-byte boundary (FEAT_LSE2
// allows any alignment inside a 16-byte granule). A misaligned guest LOCK operand that crosses
// one raises SIGBUS; there is no split fallback, because two halves could not be updated
// atomically anyway. x86 handles it (as a bus lock); compilers never emit it for C11/C++
// atomics, which are naturally aligned. Ordinary loads and stores under TSO have a fenced
// fallback for this case (orderedAccess).
//
// The destination register of an LSE atomic is never WZR/XZR: with a zero destination the
// instruction loses its acquire semantics (LDADDAL WZR is not an acquire). The flags are
// recomputed from the old value the atomic returns, with the same rules (operand top-alignment
// for 8/16-bit, carry sense) as the non-atomic lowerings.

// HasLockPrefix reports whether the instruction carries an F0 LOCK prefix. x86asm records the
// second byte of a VEX prefix in the prefix list as well, so VEX instructions never count.
func HasLockPrefix(inst x86asm.Inst) bool {
	for _, p := range inst.Prefix {
		if p == 0 {
			break
		}
		if p.IsVEX() || p.IsEVEX() {
			return false
		}
		if p&0xFF == x86asm.PrefixLOCK {
			return true
		}
	}
	return false
}

// atomicSuffix is the size suffix of the exclusive and LSE instructions.
func atomicSuffix(size int) string {
	switch size {
	case 1:
		return "b"
	case 2:
		return "h"
	}
	return ""
}

// sizedReg names scratch register n in the width that holds an operand of size bytes.
func sizedReg(size, n int) string {
	if size == 8 {
		return fmt.Sprintf("x%d", n)
	}
	return fmt.Sprintf("w%d", n)
}

// lockAddr puts the host address of a locked memory operand in x16.
func lockAddr(mem x86asm.Mem, nextPC uint64) []string {
	return append(computeEffAddr(mem, nextPC, "x16"), "    add x16, x28, x16")
}

// lockSource puts the source operand of a locked instruction in a register: the pinned register
// for 32/64-bit registers, else w2/x2 (8/16-bit registers zero-extended, immediates masked to
// the operand size).
func lockSource(arg x86asm.Arg, size int) ([]string, string, bool) {
	switch v := arg.(type) {
	case x86asm.Reg:
		d, ok := arm64RegMap[v]
		if !ok {
			return nil, "", false
		}
		if size == 8 {
			return nil, d.X, true
		}
		if size == 4 && !d.IsHigh8 && d.Size == 4 {
			return nil, d.W, true
		}
		rc, _ := readRegTo(v, "x2", "w2")
		return rc, "w2", true
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
		if size == 8 {
			return loadImm64("x2", val), "x2", true
		}
		return loadImm32("w2", uint32(val)), "w2", true
	}
	return nil, "", false
}

// addFlags sets the flags of old + b (carryAdd).
func (l *Arm64Lifter) addFlags(size int, old, b string) []string {
	l.cpol = carryAdd
	if size >= 4 {
		return []string{fmt.Sprintf("    cmn %s, %s", old, b)}
	}
	sh := 32 - 8*size
	return []string{fmt.Sprintf("    lsl w3, %s, #%d", old, sh), fmt.Sprintf("    cmn w3, %s, lsl #%d", b, sh)}
}

// subFlags sets the flags of old - b (carrySub).
func (l *Arm64Lifter) subFlags(size int, old, b string) []string {
	l.cpol = carrySub
	if size >= 4 {
		return []string{fmt.Sprintf("    cmp %s, %s", old, b)}
	}
	sh := 32 - 8*size
	return []string{fmt.Sprintf("    lsl w3, %s, #%d", old, sh), fmt.Sprintf("    cmp w3, %s, lsl #%d", b, sh)}
}

// logicFlags sets the flags of the logical operation armOp (and/orr/eor) on old and b
// (carryClear: ANDS/TST clear C and V).
func (l *Arm64Lifter) logicFlags(size int, armOp, old, b string) []string {
	l.cpol = carryClear
	r3 := sizedReg(size, 3)
	if size >= 4 {
		if armOp == "and" {
			return []string{fmt.Sprintf("    tst %s, %s", old, b)}
		}
		return []string{fmt.Sprintf("    %s %s, %s, %s", armOp, r3, old, b), fmt.Sprintf("    tst %s, %s", r3, r3)}
	}
	sh := 32 - 8*size
	if armOp == "and" {
		return []string{fmt.Sprintf("    lsl w3, %s, #%d", old, sh), fmt.Sprintf("    tst w3, %s, lsl #%d", b, sh)}
	}
	return []string{fmt.Sprintf("    %s w3, %s, %s", armOp, old, b), fmt.Sprintf("    lsl w3, w3, #%d", sh), "    tst w3, w3"}
}

// exclusiveLoop wraps body (which computes the new value in w1/x1 from the old value in
// w17/x17) in an LDAXR/STLXR retry loop on [x16], followed by DMB ISH: the exclusive pair alone
// is acquire/release, not the full barrier of x86 LOCK. body must not read or write NZCV, as it
// may run more than once.
func (l *Arm64Lifter) exclusiveLoop(size int, body []string) []string {
	l.labelCounter++
	lbl := fmt.Sprintf("L_lock_loop_%d", l.labelCounter)
	s := atomicSuffix(size)
	lines := []string{fmt.Sprintf("%s:", lbl), fmt.Sprintf("    ldaxr%s %s, [x16]", s, sizedReg(size, 17))}
	lines = append(lines, body...)
	return append(lines,
		fmt.Sprintf("    stlxr%s w5, %s, [x16]", s, sizedReg(size, 1)),
		fmt.Sprintf("    cbnz w5, %s", lbl),
		"    dmb ish")
}

// failBarrier runs DMB ISH when the compare of a compare-and-swap failed (Z = 0): a failed CAS
// does not write and has only acquire semantics, while a failed x86 LOCK CMPXCHG is still a
// full barrier.
func (l *Arm64Lifter) failBarrier() []string {
	l.labelCounter++
	lbl := fmt.Sprintf("L_lock_cas_ok_%d", l.labelCounter)
	return []string{fmt.Sprintf("    b.eq %s", lbl), "    dmb ish", fmt.Sprintf("%s:", lbl)}
}

// liftLocked lowers a LOCK-prefixed ADD/SUB/AND/OR/XOR/INC/DEC/NEG/NOT/ADC/SBB/BTS/BTR/BTC on
// memory. ok is false for the instructions it does not handle (XADD, CMPXCHG, CMPXCHG8B/16B and
// XCHG are atomic in their own lowerings; LOCK on a register operand is #UD).
func (l *Arm64Lifter) liftLocked(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, bool) {
	op := inst.Inst.Op
	mem, isMem := inst.Inst.Args[0].(x86asm.Mem)
	if !isMem {
		return lines, false
	}
	size := defMemSz(inst)
	if size != 1 && size != 2 && size != 4 && size != 8 {
		return lines, false
	}
	s := atomicSuffix(size)
	old := sizedReg(size, 17)
	r0 := sizedReg(size, 0)
	flags := !inst.SkipFlags

	switch op {
	case x86asm.ADD, x86asm.SUB, x86asm.AND, x86asm.OR, x86asm.XOR:
		src, b, ok := lockSource(inst.Inst.Args[1], size)
		if !ok {
			return append(lines, l.emitUnsupported(inst, "unsupported lock alu source")...), true
		}
		lines = append(lines, src...)
		lines = append(lines, lockAddr(mem, nextPC)...)
		switch op {
		case x86asm.ADD:
			lines = append(lines, fmt.Sprintf("    ldaddal%s %s, %s, [x16]", s, b, old))
			if flags {
				lines = append(lines, l.addFlags(size, old, b)...)
			}
		case x86asm.SUB:
			lines = append(lines, fmt.Sprintf("    neg %s, %s", r0, b),
				fmt.Sprintf("    ldaddal%s %s, %s, [x16]", s, r0, old))
			if flags {
				lines = append(lines, l.subFlags(size, old, b)...)
			}
		case x86asm.AND:
			// LDCLR clears the bits set in its operand: old & b = clear(old, ~b).
			lines = append(lines, fmt.Sprintf("    mvn %s, %s", r0, b),
				fmt.Sprintf("    ldclral%s %s, %s, [x16]", s, r0, old))
			if flags {
				lines = append(lines, l.logicFlags(size, "and", old, b)...)
			}
		case x86asm.OR:
			lines = append(lines, fmt.Sprintf("    ldsetal%s %s, %s, [x16]", s, b, old))
			if flags {
				lines = append(lines, l.logicFlags(size, "orr", old, b)...)
			}
		default:
			lines = append(lines, fmt.Sprintf("    ldeoral%s %s, %s, [x16]", s, b, old))
			if flags {
				lines = append(lines, l.logicFlags(size, "eor", old, b)...)
			}
		}
		return lines, true

	case x86asm.INC, x86asm.DEC:
		// INC/DEC keep CF: 8/16-bit flags come from SETF8/SETF16 (C kept), 32/64-bit ones from
		// CMN/CMP with C restored afterwards when CF is live.
		lines = append(lines, lockAddr(mem, nextPC)...)
		arith := "add"
		if op == x86asm.INC {
			lines = append(lines, fmt.Sprintf("    mov %s, #1", r0))
		} else {
			arith = "sub"
			lines = append(lines, fmt.Sprintf("    mov %s, #-1", r0))
		}
		lines = append(lines, fmt.Sprintf("    ldaddal%s %s, %s, [x16]", s, r0, old))
		switch {
		case !flags:
		case size < 4:
			ext, setf := "sxtb", "setf8"
			if size == 2 {
				ext, setf = "sxth", "setf16"
			}
			lines = append(lines,
				fmt.Sprintf("    %s w3, %s", ext, old),
				fmt.Sprintf("    %s w3, w3, #1", arith),
				fmt.Sprintf("    %s w3", setf))
		default:
			keepC := !inst.CarryDead
			if keepC {
				lines = append(lines, "    cset w2, cs")
			}
			cmp := "cmn"
			if op == x86asm.DEC {
				cmp = "cmp"
			}
			lines = append(lines, fmt.Sprintf("    %s %s, #1", cmp, old))
			if keepC {
				lines = append(lines, "    rmif x2, #63, #2") // C = bit 0 of x2
			}
		}
		return lines, true

	case x86asm.NOT:
		lines = append(lines, lockAddr(mem, nextPC)...)
		lines = append(lines, fmt.Sprintf("    mov %s, #-1", r0),
			fmt.Sprintf("    ldeoral%s %s, %s, [x16]", s, r0, old))
		return lines, true

	case x86asm.NEG:
		lines = append(lines, lockAddr(mem, nextPC)...)
		lines = append(lines, l.exclusiveLoop(size, []string{fmt.Sprintf("    neg %s, %s", sizedReg(size, 1), old)})...)
		if flags {
			// NEGS leaves C = (x == 0) = !CF (carrySub).
			if size < 4 {
				lines = append(lines, fmt.Sprintf("    lsl w3, %s, #%d", old, 32-8*size), "    negs w3, w3")
			} else {
				lines = append(lines, fmt.Sprintf("    negs %s, %s", sizedReg(size, 3), old))
			}
			l.cpol = carrySub
		}
		return lines, true

	case x86asm.ADC, x86asm.SBB:
		return l.liftLockedAdcSbb(inst, mem, nextPC, size, lines), true

	case x86asm.BTS, x86asm.BTR, x86asm.BTC:
		return l.liftLockedBt(inst, mem, nextPC, size, lines), true
	}
	return lines, false
}

// liftLockedAdcSbb lowers LOCK ADC/SBB on memory with an exclusive loop. CF is read into w4
// before the loop (the loop body must not depend on NZCV); the flags are recomputed from the
// old value afterwards: 32/64-bit with ADCS/SBCS after C is set from w4, 8/16-bit from the
// sign-extended sum (SETF8/SETF16) and the carry out of the zero-extended one, as liftAdcSbb.
func (l *Arm64Lifter) liftLockedAdcSbb(inst disasm.Instruction, mem x86asm.Mem, nextPC uint64, size int, lines []string) []string {
	isSbb := inst.Inst.Op == x86asm.SBB
	src, b, ok := lockSource(inst.Inst.Args[1], size)
	if !ok {
		return append(lines, l.emitUnsupported(inst, "unsupported lock adc/sbb source")...)
	}
	lines = append(lines, src...)
	r4 := sizedReg(size, 4)
	lines = append(lines, fmt.Sprintf("    cset %s, %s", r4, l.carryCond(true)))
	lines = append(lines, lockAddr(mem, nextPC)...)
	arith := "add"
	if isSbb {
		arith = "sub"
	}
	old, r1 := sizedReg(size, 17), sizedReg(size, 1)
	lines = append(lines, l.exclusiveLoop(size, []string{
		fmt.Sprintf("    %s %s, %s, %s", arith, r1, old, b),
		fmt.Sprintf("    %s %s, %s, %s", arith, r1, r1, r4),
	})...)
	if inst.SkipFlags {
		return lines
	}
	if size >= 4 {
		lines = append(lines, fmt.Sprintf("    cmp %s, #1", r4)) // C = CF
		r3 := sizedReg(size, 3)
		if isSbb {
			lines = append(lines, "    cfinv", fmt.Sprintf("    sbcs %s, %s, %s", r3, old, b)) // SBCS needs C = !CF
			l.cpol = carrySub
		} else {
			lines = append(lines, fmt.Sprintf("    adcs %s, %s, %s", r3, old, b))
			l.cpol = carryAdd
		}
		return lines
	}
	ext, setf := "sxtb", "setf8"
	if size == 2 {
		ext, setf = "sxth", "setf16"
	}
	lines = append(lines,
		fmt.Sprintf("    %s w3, w17", ext),
		fmt.Sprintf("    %s w6, %s", ext, b),
		fmt.Sprintf("    %s w3, w3, w6", arith),
		fmt.Sprintf("    %s w3, w3, w4", arith),
		fmt.Sprintf("    %s w3", setf), // N, Z, V of the narrow result; C kept
		fmt.Sprintf("    %s w6, w17, %s", arith, b),
		fmt.Sprintf("    %s w6, w6, w4", arith),
	)
	if isSbb {
		// borrow = sign of the zero-extended difference; C = !borrow
		lines = append(lines, "    lsr w6, w6, #31", "    eor w6, w6, #1")
		l.cpol = carrySub
	} else {
		lines = append(lines, fmt.Sprintf("    ubfx w6, w6, #%d, #1", 8*size))
		l.cpol = carryAdd
	}
	return append(lines, "    rmif x6, #63, #2") // C = bit 0 of x6
}

// liftLockedBt lowers LOCK BTS/BTR/BTC with LDSETAL/LDCLRAL/LDEORAL of the bit mask. CF is the
// tested bit of the old value (carryAdd); ZF is kept and the other flags are undefined, as in
// liftBt. A register bit offset is signed and selects the operand at
// addr + (offset >> log2(bits)) * size.
func (l *Arm64Lifter) liftLockedBt(inst disasm.Instruction, mem x86asm.Mem, nextPC uint64, size int, lines []string) []string {
	if size == 1 {
		return append(lines, l.emitUnsupported(inst, "lock bt on a byte")...)
	}
	bits := 8 * size
	log2 := map[int]int{16: 4, 32: 5, 64: 6}[bits]
	lines = append(lines, computeEffAddr(mem, nextPC, "x16")...)
	r0 := sizedReg(size, 0)
	k := 0
	imm, isImm := inst.Inst.Args[1].(x86asm.Imm)
	if isImm {
		k = int(uint64(imm) % uint64(bits))
		lines = append(lines, loadImm64("x0", uint64(1)<<k)...)
	} else {
		r, ok := inst.Inst.Args[1].(x86asm.Reg)
		desc, okD := arm64RegMap[r]
		if !ok || !okD {
			return append(lines, l.emitUnsupported(inst, "unsupported lock bt bit offset")...)
		}
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
			fmt.Sprintf("    and x1, x4, #%d", bits-1),
			"    mov x0, #1", "    lsl x0, x0, x1")
	}
	lines = append(lines, "    add x16, x28, x16")
	s := atomicSuffix(size)
	old := sizedReg(size, 17)
	at := map[x86asm.Op]string{x86asm.BTS: "ldsetal", x86asm.BTR: "ldclral", x86asm.BTC: "ldeoral"}[inst.Inst.Op]
	lines = append(lines, fmt.Sprintf("    %s%s %s, %s, [x16]", at, s, r0, old))
	if !inst.CarryDead {
		if isImm {
			lines = append(lines, fmt.Sprintf("    ubfx x2, x17, #%d, #1", k))
		} else {
			lines = append(lines, "    lsr x2, x17, x1")
		}
		lines = append(lines, "    rmif x2, #63, #2") // C = tested bit (bit 0 of x2)
		l.cpol = carryAdd
	}
	return lines
}

// liftAtomicXadd lowers XADD with a memory destination: LDADDAL returns the old value, which
// goes to the source register; the flags are those of old + src (carryAdd).
func (l *Arm64Lifter) liftAtomicXadd(inst disasm.Instruction, mem x86asm.Mem, srcReg x86asm.Reg, nextPC uint64, lines []string) []string {
	size := arm64RegMap[srcReg].Size
	src, b, _ := lockSource(srcReg, size)
	lines = append(lines, src...)
	lines = append(lines, lockAddr(mem, nextPC)...)
	old := sizedReg(size, 17)
	lines = append(lines, fmt.Sprintf("    ldaddal%s %s, %s, [x16]", atomicSuffix(size), b, old))
	if !inst.SkipFlags {
		lines = append(lines, l.addFlags(size, old, b)...)
	}
	wc, _ := writeRegFrom(srcReg, "x17", "w17")
	return append(lines, wc...)
}

// liftAtomicXchg lowers XCHG with a memory operand (implicitly locked) with SWPAL.
func (l *Arm64Lifter) liftAtomicXchg(mem x86asm.Mem, reg x86asm.Reg, nextPC uint64, lines []string) []string {
	size := arm64RegMap[reg].Size
	src, b, _ := lockSource(reg, size)
	lines = append(lines, src...)
	lines = append(lines, lockAddr(mem, nextPC)...)
	lines = append(lines, fmt.Sprintf("    swpal%s %s, %s, [x16]", atomicSuffix(size), b, sizedReg(size, 17)))
	wc, _ := writeRegFrom(reg, "x17", "w17")
	return append(lines, wc...)
}

// liftAtomicCmpxchg lowers CMPXCHG with a memory destination with CASAL. The flags are those of
// CMP accumulator, old (carrySub). The accumulator receives the old value on failure only: a
// 32-bit accumulator is zero-extended then and keeps RAX[63:32] on success.
func (l *Arm64Lifter) liftAtomicCmpxchg(mem x86asm.Mem, srcReg x86asm.Reg, nextPC uint64, lines []string) []string {
	size := arm64RegMap[srcReg].Size
	src, b, _ := lockSource(srcReg, size)
	lines = append(lines, src...)
	switch size {
	case 8:
		lines = append(lines, "    mov x0, x19")
	case 4:
		lines = append(lines, "    mov w0, w19")
	case 2:
		lines = append(lines, "    and w0, w19, #0xffff")
	default:
		lines = append(lines, "    and w0, w19, #0xff")
	}
	lines = append(lines, lockAddr(mem, nextPC)...)
	r0 := sizedReg(size, 0)
	lines = append(lines, fmt.Sprintf("    casal%s %s, %s, [x16]", atomicSuffix(size), r0, b))
	switch size {
	case 8:
		lines = append(lines, "    cmp x19, x0", "    mov x19, x0")
	case 4:
		lines = append(lines, "    cmp w19, w0", "    csel x19, x19, x0, eq")
	default:
		sh := 32 - 8*size
		lines = append(lines, fmt.Sprintf("    lsl w1, w19, #%d", sh), fmt.Sprintf("    cmp w1, w0, lsl #%d", sh),
			fmt.Sprintf("    bfi x19, x0, #0, #%d", 8*size))
	}
	l.cpol = carrySub
	return append(lines, l.failBarrier()...)
}

// setZeroFlagFromEq copies the Z result of the compare just made into the guest flags saved in
// x5 (MRS NZCV before the compare) and restores them: CMPXCHG8B/16B change only ZF. The
// compare's Z is kept for the following B.EQ.
func setZeroFlagFromEq() []string {
	return []string{"    cset x6, eq", "    bfi x5, x6, #30, #1", "    msr nzcv, x5"}
}

// liftCmpxchg8b lowers CMPXCHG8B m64 with CASAL: EDX:EAX is compared with m64; on success
// ECX:EBX is stored, on failure m64 is loaded into EDX:EAX (both zero-extended). Only ZF changes.
func (l *Arm64Lifter) liftCmpxchg8b(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	mem, ok := inst.Inst.Args[0].(x86asm.Mem)
	if !ok {
		return append(lines, l.emitUnsupported(inst, "cmpxchg8b operand must be memory")...), nil
	}
	lines = append(lines,
		"    mov w0, w19", "    bfi x0, x22, #32, #32", // expected EDX:EAX
		"    mov x4, x0",
		"    mov w2, w20", "    bfi x2, x21, #32, #32", // new ECX:EBX
	)
	lines = append(lines, lockAddr(mem, nextPC)...)
	lines = append(lines, "    casal x0, x2, [x16]")
	flags := !inst.SkipFlags
	if flags {
		lines = append(lines, "    mrs x5, nzcv")
	}
	lines = append(lines, "    cmp x0, x4",
		"    mov w6, w0", "    lsr x7, x0, #32",
		"    csel x19, x19, x6, eq", "    csel x22, x22, x7, eq")
	if flags {
		lines = append(lines, setZeroFlagFromEq()...)
	} else {
		l.cpol = carrySub
	}
	return append(lines, l.failBarrier()...), nil
}

// liftCmpxchg16b lowers CMPXCHG16B m128 with CASPAL: RDX:RAX is compared with m128; on success
// RCX:RBX is stored, on failure m128 is loaded into RDX:RAX. Only ZF changes.
func (l *Arm64Lifter) liftCmpxchg16b(inst disasm.Instruction, nextPC uint64, lines []string) ([]string, error) {
	mem, okMem := inst.Inst.Args[0].(x86asm.Mem)
	if !okMem {
		lines = append(lines, l.emitUnsupported(inst, "cmpxchg16b operand must be memory")...)
		return lines, nil
	}
	// CASP takes even/odd register pairs: x0:x1 = expected (RAX, RDX), x2:x3 = new (RBX, RCX).
	lines = append(lines, "    mov x0, x19", "    mov x1, x22", "    mov x2, x20", "    mov x3, x21")
	lines = append(lines, lockAddr(mem, nextPC)...)
	lines = append(lines, "    caspal x0, x1, x2, x3, [x16]")
	flags := !inst.SkipFlags
	if flags {
		lines = append(lines, "    mrs x5, nzcv")
	}
	lines = append(lines, "    cmp x0, x19", "    ccmp x1, x22, #0, eq",
		"    mov x19, x0", "    mov x22, x1") // equal to RAX/RDX on success
	if flags {
		lines = append(lines, setZeroFlagFromEq()...)
	} else {
		l.cpol = carrySub
	}
	return append(lines, l.failBarrier()...), nil
}
