package lifter

// Regression tests for the instruction classes that the lifter-difftest-asm harness
// (cmd/lifter-difftest-asm, which runs the lifted code against x86-64 under Rosetta 2) found
// lowered wrongly. Each test checks the property of the lowering that its fix introduced; the
// harness checks the behavior end to end.

import (
	"encoding/hex"
	"strconv"
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

// liftBlock lifts the hex-encoded x86 instructions as one block of a function (flags are
// treated as live everywhere, the conservative default) and returns the lowering of each.
func liftBlock(t *testing.T, insts ...string) [][]string {
	t.Helper()
	return liftBlockIn(t, false, insts...)
}

// liftBlockIn is liftBlock in a function with (usesYmm) or without YMM instructions.
func liftBlockIn(t *testing.T, usesYmm bool, insts ...string) [][]string {
	t.Helper()
	l := NewArm64Lifter(nil)
	fn := &disasm.Function{
		EntryAddr: 0x1000,
		EndAddr:   0x9000,
		Blocks:    map[uint64]*disasm.BasicBlock{0x1000: {StartAddr: 0x1000}},
		UsesYmm:   usesYmm,
	}
	pc := uint64(0x1000)
	var out [][]string
	for _, h := range insts {
		b, err := hex.DecodeString(h)
		if err != nil {
			t.Fatalf("bad hex %q: %v", h, err)
		}
		inst, err := x86asm.Decode(b, 64)
		if err != nil {
			t.Fatalf("decode %s: %v", h, err)
		}
		next := pc + uint64(inst.Len)
		lines, err := l.LiftInstructionToBuf(disasm.Instruction{Address: pc, Inst: inst}, next, fn, nil)
		if err != nil {
			t.Fatalf("lift %s: %v", inst, err)
		}
		out = append(out, lines)
		pc = next
	}
	return out
}

// code returns the instruction text of a lowering line: no indentation, no trailing comment.
func code(line string) string {
	if i := strings.Index(line, "//"); i >= 0 {
		line = line[:i]
	}
	return strings.Join(strings.Fields(line), " ")
}

// lineMatches compares a lowering line with want; a want ending in "..." is a prefix.
func lineMatches(line, want string) bool {
	c := code(line)
	if p, ok := strings.CutSuffix(want, "..."); ok {
		return strings.HasPrefix(c, p)
	}
	return c == want
}

// requireInOrder fails unless lines contain each of want, in this order.
func requireInOrder(t *testing.T, what string, lines []string, want ...string) {
	t.Helper()
	i := 0
	for _, ln := range lines {
		if i < len(want) && lineMatches(ln, want[i]) {
			i++
		}
	}
	if i < len(want) {
		t.Errorf("%s: expected %q (in order %q), got:\n%s", what, want[i], want, strings.Join(lines, "\n"))
	}
}

// forbid fails if a line of lines matches one of bad.
func forbid(t *testing.T, what string, lines []string, bad ...string) {
	t.Helper()
	for _, ln := range lines {
		for _, b := range bad {
			if lineMatches(ln, b) {
				t.Errorf("%s: unexpected %q, got:\n%s", what, code(ln), strings.Join(lines, "\n"))
			}
		}
	}
}

// writesVecReg reports whether a line of lines has vector register n (v/q/d/s/h/b) as its
// destination, i.e. its first operand; stores and compares have no register destination.
func writesVecReg(lines []string, n int) bool {
	for _, ln := range lines {
		f := strings.Fields(strings.ReplaceAll(code(ln), ",", " "))
		if len(f) < 2 {
			continue
		}
		switch f[0] {
		case "str", "stp", "stur", "st1", "cmp", "fcmp", "tst", "cmn":
			continue
		}
		d := f[1]
		if i := strings.IndexAny(d, ".["); i >= 0 {
			d = d[:i]
		}
		if len(d) > 1 && strings.ContainsRune("vqdshb", rune(d[0])) && d[1:] == strconv.Itoa(n) {
			return true
		}
	}
	return false
}

// Carry sense: x86 CF is held in ARM C either inverted (after SUBS/CMP, C = !CF) or directly
// (after ADDS, and C = 0 = CF after logical operations). The lifter used one fixed sense per
// condition, so CF-reading conditions after logical operations, A/BE after additions and the
// flags of 8/16-bit arithmetic were wrong, and INC/DEC clobbered CF.
func TestRegressCarrySense(t *testing.T) {
	// and eax, ebx; jb: after AND, C = 0 = CF; the branch brings C into the block-boundary
	// sense (C = !CF) before testing it.
	l := liftBlock(t, "21d8", "720e")
	requireInOrder(t, "jb after and", l[1], "cfinv", "b.cs ...")

	// add rax, rbx; ja: ARM HI needs C = !CF, so C is inverted first.
	l = liftBlock(t, "4801d8", "770e")
	requireInOrder(t, "ja after add", l[1], "cfinv", "b.ls ...")

	// add al, bl: 8-bit flags come from the operands shifted to the top of a W register.
	l = liftBlock(t, "00d8")
	requireInOrder(t, "add al, bl", l[0], "adds w17, w17, w16, lsl #24", "bfi x19, x17, #0, #8")

	// cmp rax, rbx; inc ecx: INC keeps CF.
	l = liftBlock(t, "4839d8", "ffc1")
	requireInOrder(t, "inc after cmp", l[1], "cset w2, cs", "adds w21, w21, #1", "rmif x2, #63, #2")

	// adc r12d, 0 at a block start: C = !CF there, ADCS needs C = CF.
	l = liftBlock(t, "4183d400")
	requireInOrder(t, "adc", l[0], "cfinv", "adcs w12, w12, wzr")
}

// Shifts and rotates: a count of 0 leaves the flags unchanged, CF is the last bit shifted out,
// OF is defined for counts of 1, and rotates set CF/OF. The lifter set only SF/ZF (TST).
func TestRegressShiftFlags(t *testing.T) {
	// cmp rax, rbx; shl eax, cl: flags only change for a non-zero count.
	l := liftBlock(t, "4839d8", "d3e0")
	requireInOrder(t, "shl eax, cl", l[1], "and w1, w21, #31", "cbz w1, ...", "rmif x2, #0, #3", "L_shift_...", "mov w19, w0")

	// rol eax, 1: CF = bit 0 of the result, OF = CF ^ MSB.
	l = liftBlock(t, "d1c0")
	requireInOrder(t, "rol eax, 1", l[0], "ror w0, w4, #31", "and x2, x0, #1", "rmif x2, #0, #3")

	// shr rdx, 0x20: CF = bit 31, OF = the original MSB.
	l = liftBlock(t, "48c1ea20")
	requireInOrder(t, "shr rdx, 0x20", l[0], "ubfx x2, x22, #31, #1", "ubfx x3, x22, #63, #1", "rmif x2, #0, #3")

	// rcl eax, 1 at a block start: CF is read in the block-boundary sense (CF = !C).
	l = liftBlock(t, "d1d0")
	requireInOrder(t, "rcl eax, 1", l[0], "cset w2, cc", "rmif x4, #0, #3")
}

// MUL/IMUL set CF = OF = (the result does not fit the destination); the lifter left them.
func TestRegressMulFlags(t *testing.T) {
	l := liftBlock(t, "410fafc6") // imul eax, r14d
	requireInOrder(t, "imul eax, r14d", l[0], "smull x0, w19, w14", "cmp x0, w0, sxtw", "cset w2, ne", "mov w19, w0", "rmif x2, #0, #3")

	l = liftBlock(t, "48f7e1") // mul rcx
	requireInOrder(t, "mul rcx", l[0], "umulh x22, x19, x21", "cmp x2, #0", "rmif x2, #0, #3")
}

// Bit test, bit counts and BMI: BT changes only CF (it set ZF), TZCNT/LZCNT take CF from the
// source and ZF from the result, BEXTR with a start >= 32 gives 0, ANDN read its first source
// from a register its address computation had overwritten, BLSI/BLSR/BLSMSK set CF in the
// wrong sense.
func TestRegressBitOps(t *testing.T) {
	// cmp rax, rbx; bt qword [rax+0x88], 0x20
	l := liftBlock(t, "4839d8", "480fbaa08800000020")
	requireInOrder(t, "bt", l[1], "ubfx x2, x17, #32, #1", "rmif x2, #63, #2")
	forbid(t, "bt", l[1], "cmp ...", "tst ...", "msr nzcv...")

	l = liftBlock(t, "f30fbcc1") // tzcnt eax, ecx
	requireInOrder(t, "tzcnt", l[0], "rbit w1, w0", "clz w1, w1", "cmp w0, #0", "ccmp w1, #0, #0, ne", "mov w19, w1")

	l = liftBlock(t, "c4e270f780c0000000") // bextr eax, [rax+0xc0], ecx
	requireInOrder(t, "bextr", l[0], "lsr w0, w0, w1", "cmp w1, #32", "csel w0, wzr, w0, hs")

	l = liftBlock(t, "c4e208f208") // andn ecx, r14d, [rax]
	requireInOrder(t, "andn", l[0], "add x7, x28, x16", "ldapr w17, [x7]", "bics w21, w17, w14")

	l = liftBlock(t, "c4e278f3c9") // blsr eax, ecx: CF = (src == 0)
	requireInOrder(t, "blsr", l[0], "cmp w0, #0", "cset w2, eq", "tst w1, w1", "rmif x2, #63, #2", "mov w19, w1")
}

// Narrow operands: 16-bit CMOVcc, CDQ (a 32-bit write clears RDX[63:32]), and XCHG/XADD/CMPXCHG
// on bytes and words (XCHG accessed 8 bytes, XADD/CMPXCHG flags came from 32-bit arithmetic).
func TestRegressNarrowOperands(t *testing.T) {
	l := liftBlock(t, "660f42c1") // cmovb ax, cx
	requireInOrder(t, "cmovb ax, cx", l[0], "csel w16, w21, w19, cc", "bfi x19, x16, #0, #16")

	l = liftBlock(t, "99") // cdq
	requireInOrder(t, "cdq", l[0], "asr w22, w19, #31")

	l = liftBlock(t, "8610") // xchg byte [rax], dl
	requireInOrder(t, "xchg byte", l[0], "and w2, w22, #0xff", "swpalb w2, w17, [x16]", "bfi x22, x17, #0, #8")

	l = liftBlock(t, "0fc008") // xadd byte [rax], cl
	requireInOrder(t, "xadd byte", l[0], "and w2, w21, #0xff", "ldaddalb w2, w17, [x16]", "lsl w3, w17, #24", "cmn w3, w2, lsl #24")

	l = liftBlock(t, "660fb108") // cmpxchg word [rax], cx
	requireInOrder(t, "cmpxchg word", l[0], "and w0, w19, #0xffff", "casalh w0, w2, [x16]", "lsl w1, w19, #16", "cmp w1, w0, lsl #16")
}

// Compare and test flags: (U)COMIS* and FCOMI give ZF = PF = CF = 1 for unordered and SF = 0
// (the lifter used the raw FCMP NZCV), and PTEST leaves C in the subtract sense.
func TestRegressCompareFlags(t *testing.T) {
	l := liftBlock(t, "c5f82ec0") // vucomiss xmm0, xmm0
	requireInOrder(t, "vucomiss", l[0], "fcmp s0, s0", "cset w2, vs", "axflag", "rmif x2, #0, #1")

	l = liftBlock(t, "dbf1") // fcomi st, st(1)
	requireInOrder(t, "fcomi", l[0], "fcmp d28, d29", "axflag")

	// add rax, rbx; vptest xmm0, xmm1; jb: JB reads the C that PTEST wrote (C = !CF).
	l = liftBlock(t, "4801d8", "c4e27917c1", "720e")
	requireInOrder(t, "jb after vptest", l[2], "b.cs ...")
	forbid(t, "jb after vptest", l[2], "cfinv")
}

// YMM upper halves live in v16-v31, which lowerings also use as scratch registers: a scratch
// register must not be the upper half of a source operand.
func TestRegressYmmUpperHalves(t *testing.T) {
	l := liftBlock(t, "c4e37506c220") // vperm2f128 ymm0, ymm1, ymm2, 0x20
	for _, n := range []int{17, 18} { // ymm1, ymm2 upper halves
		if writesVecReg(l[0], n) {
			t.Errorf("vperm2f128 writes v%d (a source's upper half):\n%s", n, strings.Join(l[0], "\n"))
		}
	}
	requireInOrder(t, "vperm2f128", l[0], "mov v16.16b, v19.16b")

	l = liftBlock(t, "c4e27d17c1") // vptest ymm0, ymm1
	for _, n := range []int{16, 17} {
		if writesVecReg(l[0], n) {
			t.Errorf("vptest ymm writes v%d (a source's upper half):\n%s", n, strings.Join(l[0], "\n"))
		}
	}

	// In a function with YMM code the other upper halves a lowering uses as scratch are saved
	// and restored: v30 (YMM14's upper half) for the memory operand of VADDPS, and v16 for legacy
	// SQRTSS, which must keep XMM0's upper half.
	l = liftBlockIn(t, true, "c5f05883c0000000") // vaddps xmm0, xmm1, [rbx+0xc0]
	requireInOrder(t, "vaddps in a YMM function", l[0], "str q30, [sp, #-16]!", "ldr q30, [x28, x16]",
		"fadd v0.4s, v1.4s, v30.4s", "movi v16.16b, #0", "ldr q30, [sp], #16")
	l = liftBlockIn(t, true, "f30f51c0") // sqrtss xmm0, xmm0
	requireInOrder(t, "sqrtss in a YMM function", l[0], "str q16, [sp, #-16]!", "fsqrt s16, s16", "ldr q16, [sp], #16")
	// Without YMM code no upper half is live: nothing is saved.
	l = liftBlock(t, "c5f05883c0000000")
	forbid(t, "vaddps outside a YMM function", l[0], "str q30, [sp, #-16]!")
}

// SSE/AVX semantics: MAX/MIN return the second operand for NaN and signed zeros (ARM FMAX/FMIN
// do not), float->int conversions return 0x80000000 for NaN and out-of-range values, RCPPS needs
// a refinement step for x86 precision, and legacy scalar operations keep the rest of the
// destination register (writing an ARM S/D register clears it).
func TestRegressVectorSemantics(t *testing.T) {
	l := liftBlock(t, "c4c1505ff1") // vmaxps xmm6, xmm5, xmm9
	requireInOrder(t, "vmaxps", l[0], "fcmgt v29.4s, v5.4s, v9.4s", "bsl v29.16b, v5.16b, v9.16b", "mov v6.16b, v29.16b")
	forbid(t, "vmaxps", l[0], "fmax ...")

	l = liftBlock(t, "c5fa2cd0") // vcvttss2si edx, xmm0
	requireInOrder(t, "vcvttss2si", l[0], "fcvtzs w22, s0", "fcmeq s30, s0, s0", "and w2, w2, #0x80000000", "orr w22, w22, w2")

	l = liftBlock(t, "c5f8538de0feffff") // vrcpps xmm1, [rbp-0x120]
	requireInOrder(t, "vrcpps", l[0], "frecpe v27.4s, v24.4s", "frecps v26.4s, v24.4s, v27.4s", "fmul v27.4s, v27.4s, v26.4s")

	l = liftBlock(t, "f30f58c1") // addss xmm0, xmm1
	requireInOrder(t, "addss", l[0], "fadd s31, s0, s1", "mov v0.s[0], v31.s[0]")

	l = liftBlock(t, "f30f5ac1") // cvtss2sd xmm0, xmm1
	requireInOrder(t, "cvtss2sd", l[0], "fcvt d30, s1", "mov v0.d[0], v30.d[0]")
}

// MXCSR lives at GuestContext offset 0x2e8 (0x2ec, which the lifter used, is padding before
// mmx[]), and LDMXCSR/FXRSTOR set the FPCR rounding mode and flush-to-zero bits from it.
func TestRegressMxcsr(t *testing.T) {
	l := liftBlock(t, "0fae55f4") // ldmxcsr [rbp-0xc]
	requireInOrder(t, "ldmxcsr", l[0], "ldr w0, [x28, x16]", "str w0, [x17, #0x2e8]", "mrs x1, fpcr", "ubfx w2, w0, #14, #1", "bfi x1, x2, #22, #1", "msr fpcr, x1")

	l = liftBlock(t, "0fae5df4") // stmxcsr [rbp-0xc]
	requireInOrder(t, "stmxcsr", l[0], "ldr w0, [x17, #0x2e8]", "str w0, [x28, x16]")

	l = liftBlock(t, "0fae08") // fxrstor [rax]
	requireInOrder(t, "fxrstor", l[0], "str w5, [x17, #0x2e8]", "msr fpcr, x1")
}

// PMULHRSW, PSIGNB/W/D and LDDQU (legacy and VEX) were unsupported.
func TestRegressSsse3Ops(t *testing.T) {
	for _, tc := range []struct {
		name, hex string
		want      []string
	}{
		{"vpmulhrsw xmm1, xmm2, xmm3", "c4e2690bcb", []string{"smull v29.4s, v2.4h, v3.4h", "smull2 v30.4s, v2.8h, v3.8h", "rshrn v1.4h, v29.4s, #15", "rshrn2 v1.8h, v30.4s, #15"}},
		{"pmulhrsw xmm1, xmm2", "660f380bca", []string{"smull v29.4s, v1.4h, v2.4h", "rshrn v1.4h, v29.4s, #15"}},
		{"vpsignw xmm1, xmm2, xmm3", "c4e26909cb", []string{"cmlt v29.8h, v3.8h, #0", "cmeq v30.8h, v3.8h, #0", "neg v31.8h, v2.8h", "bsl v29.16b, v31.16b, v2.16b", "bic v1.16b, v29.16b, v30.16b"}},
		{"psignw xmm1, xmm2", "660f3809ca", []string{"cmlt v29.8h, v2.8h, #0", "bic v1.16b, v29.16b, v30.16b"}},
		{"vlddqu xmm1, [rax]", "c5fbf008", []string{"ldr q1, [x28, x16]"}},
		{"lddqu xmm1, [rax]", "f20ff008", []string{"ldr q1, [x28, x16]"}},
	} {
		l := liftBlock(t, tc.hex)
		forbid(t, tc.name, l[0], "b _recomp_unsupported_instruction")
		requireInOrder(t, tc.name, l[0], tc.want...)
	}
}

// x18 is the platform register on Darwin (the kernel may change it at any time); lowerings
// used it as a scratch register.
func TestRegressNoPlatformRegister(t *testing.T) {
	for _, h := range []string{
		"0ffdc1",       // paddw mm0, mm1
		"d900",         // fld dword [rax]
		"dfe0",         // fnstsw ax
		"c4e27917c1",   // vptest xmm0, xmm1
		"c4e2712c00",   // vmaskmovps xmm0, xmm1, [rax]
		"c4e2712e00",   // vmaskmovps [rax], xmm1, xmm0
		"660f3a63c100", // pcmpistri xmm0, xmm1, 0
		"0fae00",       // fxsave [rax]
	} {
		l := liftBlock(t, h)
	scan:
		for _, ln := range l[0] {
			for _, f := range strings.FieldsFunc(code(ln), func(r rune) bool { return strings.ContainsRune(" ,[]!#", r) }) {
				if f == "x18" || f == "w18" {
					t.Errorf("%s uses %s:\n%s", h, f, strings.Join(l[0], "\n"))
					break scan
				}
			}
		}
	}
}

// lastWriteBefore returns the code of the last line before index end whose destination (first
// operand) is register reg, or "".
func lastWriteBefore(lines []string, end int, reg string) string {
	for i := end - 1; i >= 0; i-- {
		f := strings.Fields(strings.ReplaceAll(code(lines[i]), ",", " "))
		if len(f) >= 2 && f[1] == reg && !strings.HasPrefix(f[0], "st") {
			return code(lines[i])
		}
	}
	return ""
}

// x87 stores to memory computed the address into x16 and then read the x87 stack, whose helpers
// reload x16 with the GuestContext pointer: the value went to [x28 + ctx pointer].
func TestRegressX87Stores(t *testing.T) {
	for _, tc := range []struct{ name, hex, store string }{
		{"fstp dword [rax]", "d918", "str s28, [x28, x16]"},
		{"fstp qword [rax]", "dd18", "str d28, [x28, x16]"},
		{"fst dword [rax]", "d910", "str s28, [x28, x16]"},
		{"fistp dword [rax]", "db18", "str w17, [x28, x16]"},
		{"fisttp qword [rax]", "dd08", "str x17, [x28, x16]"},
		{"fist word [rax]", "df10", "strh w17, [x28, x16]"},
		{"fistp word [rax]", "df18", "strh w17, [x28, x16]"},
	} {
		l := liftBlock(t, tc.hex)[0]
		forbid(t, tc.name, l, "b _recomp_unsupported_instruction")
		at := -1
		for i, ln := range l {
			if code(ln) == tc.store {
				at = i
			}
		}
		if at < 0 {
			t.Errorf("%s: no %q in:\n%s", tc.name, tc.store, strings.Join(l, "\n"))
			continue
		}
		if w := lastWriteBefore(l, at, "x16"); w != "mov x16, x19" {
			t.Errorf("%s: the store address x16 was last written by %q, not the address computation:\n%s",
				tc.name, w, strings.Join(l, "\n"))
		}
	}
	// FIST/FISTP give the integer indefinite for NaN and out-of-range values (after rounding in
	// the control word's mode, TestExecX87Rounding).
	requireInOrder(t, "fistp word", liftBlock(t, "df18")[0], "frintx d28, d28", "fcvtzs x17, d28", "and w1, w1, #0x8000")
	requireInOrder(t, "fistp dword", liftBlock(t, "db18")[0], "frintx d28, d28", "fcvtzs x17, d28", "and w1, w1, #0x80000000")
}

// FCMOVcc ST(0), ST(i) took i from the first operand (always ST(0), a no-op) and used fixed ARM
// conditions instead of the tracked carry sense.
func TestRegressFcmov(t *testing.T) {
	l := liftBlock(t, "dbd2")[0] // fcmovnbe st0, st2
	requireInOrder(t, "fcmovnbe st0, st2", l, "add w17, w17, #2", "ldr d29, [x5, w17, uxtw #3]", "fcsel d28, d29, d28, hi")

	l = liftBlock(t, "4801d8", "dbd2")[1] // add rax, rbx; fcmovnbe st0, st2: C = CF after ADD
	requireInOrder(t, "fcmovnbe after add", l, "cfinv", "fcsel d28, d29, d28, hi")

	l = liftBlock(t, "4801d8", "dac1")[1] // add rax, rbx; fcmovb st0, st1: CF = 1 is C = 1 after ADD
	requireInOrder(t, "fcmovb after add", l, "add w17, w17, #1", "fcsel d28, d29, d28, cs")
}

// LOOP/LOOPE/LOOPNE decremented RCX with SUBS, which overwrote the guest flags, LOOPE/LOOPNE
// then tested the Z of that SUBS instead of the guest ZF, and nothing brought C into the
// block-boundary sense before the branch.
func TestRegressLoop(t *testing.T) {
	for _, tc := range []struct {
		name, hex string
		want      []string
	}{
		{"loop", "e20e", []string{"sub x21, x21, #1", "cbz x21, L_loop_skip_...", "b _recomp_indirect_jump"}},
		{"loope", "e10e", []string{"sub x21, x21, #1", "cbz x21, L_loope_skip_...", "b.ne L_loope_skip_..."}},
		{"loopne", "e00e", []string{"sub x21, x21, #1", "cbz x21, L_loopne_skip_...", "b.eq L_loopne_skip_..."}},
	} {
		l := liftBlock(t, "4839d8", tc.hex)[1] // cmp rax, rbx; loop*
		requireInOrder(t, tc.name, l, tc.want...)
		forbid(t, tc.name, l, "subs ...", "cfinv")
		// After an ADD (C = CF) the branch targets need C = !CF.
		requireInOrder(t, tc.name+" after add", liftBlock(t, "4801d8", tc.hex)[1], "cfinv", "sub x21, x21, #1")
	}
}

// The x87 lowerings worked in d0-d2, the low halves of XMM0-XMM2: any x87 instruction
// overwrote XMM0 (and XMM1/XMM2 for two-operand forms).
func TestRegressX87KeepsXmm(t *testing.T) {
	for _, tc := range []struct{ name, hex string }{
		{"fld dword [rax]", "d900"},
		{"fld tbyte [rax]", "db28"},
		{"fstp tbyte [rax]", "db38"},
		{"fstp qword [rax]", "dd18"},
		{"fadd st0, st2", "d8c2"},
		{"fmul dword [rax]", "d808"},
		{"fxch st1", "d9c9"},
		{"fucomip st0, st1", "dfe9"},
		{"fcmovnbe st0, st2", "dbd2"},
		{"fistp dword [rax]", "db18"},
		{"fscale", "d9fd"},
		{"fprem1", "d9f5"},
		{"fxtract", "d9f4"},
	} {
		l := liftBlock(t, tc.hex)[0]
		for n := 0; n < 16; n++ {
			if writesVecReg(l, n) {
				t.Errorf("%s writes v%d (XMM%d):\n%s", tc.name, n, n, strings.Join(l, "\n"))
			}
		}
	}
}

// FSCALE built 2^n from the exponent bits without range checks (garbage beyond 2^+-1023),
// FPREM/FPREM1 used a rounded quotient (wrong remainders, FPREM rounded instead of truncating,
// no quotient bits), and FXTRACT left the exponent and the significand swapped.
func TestRegressX87Math(t *testing.T) {
	l := liftBlock(t, "d9fd")[0] // fscale
	requireInOrder(t, "fscale", l, "frintz d31, d29", "fcvtzs x2, d31", "mov x6, #2200", "csel x2, x6, x2, gt",
		"movz x3, #0x7fe0, lsl #48", "movz x3, #0x0360, lsl #48", "fmul d28, d28, d31", "msr nzcv, x7")

	l = liftBlock(t, "d9f5")[0] // fprem1: long division, round to nearest (ties to even)
	requireInOrder(t, "fprem1", l, "subs x1, x2, x3", "sub x6, x6, x5", "lsl x6, x6, #1", "tbz x17, #0, 1f",
		"scvtf d28, x6", "msr nzcv, x7", "orr w6, w6, w2, lsl #14", "strh w6, [x16, #0x2e6]")
	l = liftBlock(t, "d9f8")[0] // fprem: truncated quotient, no rounding step
	requireInOrder(t, "fprem", l, "subs x1, x2, x3", "scvtf d28, x6")
	forbid(t, "fprem", l, "tbz x17, #0, 1f", "frintn ...")

	l = liftBlock(t, "d9f4")[0] // fxtract: ST(0) = exponent, then push the significand
	requireInOrder(t, "fxtract", l, "scvtf d29, x2", "orr x0, x0, #0x3ff0000000000000",
		"str d29, [x5, w17, uxtw #3]", "sub w17, w17, #1", "str d28, [x5, w17, uxtw #3]")
}

// An absolute [disp32] operand (SIB byte, no base, no index) was treated like the RIP-relative
// operands that x86asm's VEX decoder reports without a base: mov eax, [0xc] read the guest code
// after the instruction instead of address 0xc (the NULL page).
func TestRegressAbsoluteAddress(t *testing.T) {
	l := liftBlock(t, "8b04250c000000")[0] // mov eax, [0xc]
	requireInOrder(t, "mov eax, [0xc]", l, "mov x16, #0", "add x16, x16, #12", "add x7, x28, x16", "ldapr w19, [x7]")
	// VEX RIP-relative: vmovss xmm0, [rip+0x10] at 0x1000 (next instruction at 0x1008).
	l = liftBlock(t, "c5fa100510000000")[0]
	requireInOrder(t, "vmovss xmm0, [rip+0x10]", l, "movz x16, #0x1018...")
}

// INT 0x41 (the Orbis debugger break after Naughty Dog's HALT messages) trapped with BRK and
// killed TLOU after "A frame did not complete in 2.00 seconds"; the console, without a debugger,
// continues. Other interrupts still trap.
func TestRegressInt41Continues(t *testing.T) {
	forbid(t, "int 0x41", liftBlock(t, "cd41")[0], "brk ...")
	requireInOrder(t, "int3", liftBlock(t, "cc")[0], "brk #0")
	requireInOrder(t, "int 0x80", liftBlock(t, "cd80")[0], "brk #0")
}

// CALL [RSP+X] reads its target before the return address is pushed (TLOU 20, U4 13 sites);
// TestExecStackRsp checks PUSH/POP with RSP operands by execution.
func TestRegressCallRspOperand(t *testing.T) {
	lines := liftBlock(t, "ff542438")[0] // call qword ptr [rsp+0x38]
	requireInOrder(t, "call [rsp+0x38]", lines, "mov x16, x26", "add x16, x16, #56", "ldr x0, [x28, x16]",
		"sub x26, x26, #8", "bl _recomp_indirect_call")
}

// x87 instructions with no site in TLOU or U4 (CUSA00557, CUSA04529) are not lowered: they trap
// with a report instead of running a guessed lowering. FCOS had one that called libm's cos and
// clobbered XMM0-XMM7, the YMM upper halves and the flags.
func TestRegressX87UnusedTrap(t *testing.T) {
	for _, tc := range []struct{ name, hex string }{
		{"fcos", "d9ff"}, {"fsin", "d9fe"}, {"fsincos", "d9fb"}, {"fptan", "d9f2"}, {"fpatan", "d9f3"},
		{"f2xm1", "d9f0"}, {"fyl2x", "d9f1"}, {"fyl2xp1", "d9f9"},
		{"fbld tbyte [rax]", "df20"}, {"fbstp tbyte [rax]", "df30"},
		{"fnsave [rax]", "dd30"}, {"frstor [rax]", "dd20"},
		{"fxam", "d9e5"}, {"ftst", "d9e4"}, {"fucom st1", "dde1"}, {"fucomp st1", "dde9"}, {"fucompp", "dae9"},
		{"fincstp", "d9f7"}, {"fdecstp", "d9f6"},
	} {
		requireInOrder(t, tc.name, liftBlock(t, tc.hex)[0], "b _recomp_unsupported_instruction")
	}
}

// FS:[base+index*scale+disp] ignored base and index (0 sites); the address is now computed as
// usual and the FS base (x27) added. Forms computeEffAddr cannot express trap (0 sites each):
// addr32, MOV moffs64 (A0-A3), GS.
func TestRegressAddressingForms(t *testing.T) {
	requireInOrder(t, "mov rax, fs:[rax+8]", liftBlock(t, "64488b4008")[0],
		"mov x16, x19", "add x16, x16, #8", "add x16, x16, x27", "ldr x19, [x28, x16]")
	requireInOrder(t, "mov rax, fs:[rax+8*rcx]", liftBlock(t, "64488b04c8")[0],
		"mov x16, x19", "add x16, x16, x21, lsl #3", "add x16, x16, x27", "ldr x19, [x28, x16]")
	requireInOrder(t, "mov rax, fs:[0x28]", liftBlock(t, "64488b042528000000")[0], "add x16, x27, #40", "ldr x19, [x28, x16]")
	for _, tc := range []struct{ name, hex string }{
		{"mov eax, [moffs64]", "a10000001000000000"},
		{"mov [moffs64], rax", "48a30000001000000000"},
		{"mov rax, gs:[0]", "65488b042500000000"},
		{"mov eax, [edi]", "678b07"},
		{"lea rax, [edi+eax]", "67488d0407"},
	} {
		requireInOrder(t, tc.name, liftBlock(t, tc.hex)[0], "b _recomp_unsupported_instruction")
	}
	forbid(t, "nop [rax+rax]", liftBlock(t, "0f1f440000")[0], "b _recomp_unsupported_instruction")
}

// The local jump-table dispatch of an indirect JMP compared with CMP/B.EQ and so clobbered the
// flags on the way into the table target, and did not bring CF into the block-boundary sense.
// A direct JMP out of the function did not either (CF is live there only when the target is no
// function entry, disasm TestCarryOverflowLiveness).
func TestRegressJumpDispatchKeepsFlags(t *testing.T) {
	lift := func(insts ...string) []string {
		l := NewArm64Lifter(map[uint64]bool{0x5000: true})
		fn := &disasm.Function{EntryAddr: 0x1000, EndAddr: 0x1030,
			Blocks:     map[uint64]*disasm.BasicBlock{0x1000: {StartAddr: 0x1000}, 0x1010: {StartAddr: 0x1010}, 0x1020: {StartAddr: 0x1020}},
			BlockOrder: []uint64{0x1000, 0x1010, 0x1020}}
		pc := uint64(0x1000)
		var out []string
		for _, h := range insts {
			b, _ := hex.DecodeString(h)
			inst, err := x86asm.Decode(b, 64)
			if err != nil {
				t.Fatal(err)
			}
			out, err = l.LiftInstructionToBuf(disasm.Instruction{Address: pc, Inst: inst}, pc+uint64(inst.Len), fn, out)
			if err != nil {
				t.Fatal(err)
			}
			pc += uint64(inst.Len)
		}
		return out
	}
	lines := lift("4801d8", "ffe0") // add rax, rbx; jmp rax
	requireInOrder(t, "add; jmp rax", lines, "adds x19, x19, x20", "mov x0, x19", "cfinv", "mrs x17, nzcv",
		"cmp x0, x16", "b.lo L_fn_0x1000_outside_0x1003", "cmp x0, x16", "b.hs L_fn_0x1000_outside_0x1003",
		"msr nzcv, x17", "eor x16, x16, x0", "cbz x16, L_fn_0x1000_loc_0x1000", "cbz x16, L_fn_0x1000_loc_0x1010",
		"cbz x16, L_fn_0x1000_loc_0x1020", "b L_fn_0x1000_not_local_0x1003", "L_fn_0x1000_outside_0x1003:",
		"msr nzcv, x17", "L_fn_0x1000_not_local_0x1003:", "b _recomp_indirect_jump")
	forbid(t, "jmp rax", lines, "b.eq ...")
	// jmp 0x6000 (not a known function): CF into the block-boundary sense first.
	requireInOrder(t, "add; jmp 0x6000", lift("4801d8", "e9f84f0000"), "adds x19, x19, x20", "cfinv", "b _recomp_indirect_jump")
}

// STD was lifted as a NOP, so a following string instruction ran forward instead of backward.
// DF is not modelled (0 sites in TLOU and U4): STD traps, CLD stays a NOP.
func TestRegressStdTraps(t *testing.T) {
	requireInOrder(t, "std", liftBlock(t, "fd")[0], "b _recomp_unsupported_instruction")
	forbid(t, "cld", liftBlock(t, "fc")[0], "b _recomp_unsupported_instruction")
}
