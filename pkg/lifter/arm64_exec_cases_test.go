package lifter

import (
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"math"
	"testing"

	"golang.org/x/arch/x86/x86asm"
)

// Shifts and rotates by CL and by an immediate: the count is masked to 5 bits (6 for 64-bit
// operands), a masked count of 0 leaves every flag unchanged (and still zero-extends a 32-bit
// register), CF is the last bit shifted out, OF is defined for a count of 1. Flags x86 leaves
// undefined are not compared: OF for counts above 1, and CF of 8/16-bit operations whose count
// exceeds the operand width.
func TestExecShifts(t *testing.T) {
	type form struct {
		name  string
		width int
		cl    string // encoding of OP reg, cl
		imm   string // encoding of OP reg, imm8 (count appended)
	}
	var forms []form
	for _, op := range []struct {
		name  string
		modrm string
	}{{"shl", "e0"}, {"shr", "e8"}, {"sar", "f8"}, {"rol", "c0"}, {"ror", "c8"}, {"rcl", "d0"}, {"rcr", "d8"}} {
		forms = append(forms,
			form{op.name + " al", 8, "d2" + op.modrm, "c0" + op.modrm},
			form{op.name + " ax", 16, "66d3" + op.modrm, "66c1" + op.modrm},
			form{op.name + " eax", 32, "d3" + op.modrm, "c1" + op.modrm},
			form{op.name + " rax", 64, "48d3" + op.modrm, "48c1" + op.modrm},
		)
	}
	values := []uint64{0x8000000180000001, 0x4000000040004041, 0xfffffffffffffffe, 1}
	var cases []execCase
	for _, f := range forms {
		for _, count := range []uint64{0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 0xff} {
			mask := uint64(0x1f)
			if f.width == 64 {
				mask = 0x3f
			}
			mc := count & mask
			cmp := uint64(readMask)
			if mc != 0 {
				if mc != 1 {
					cmp &^= fOF
				}
				if mc > uint64(f.width) {
					cmp &^= fCF
				}
			}
			for vi, v := range values {
				for _, fl := range []uint64{0, fCF | fZF | fSF | fOF} {
					for _, enc := range []string{f.cl, f.imm + fmt.Sprintf("%02x", count)} {
						kind := "cl"
						if enc != f.cl {
							kind = "imm"
						}
						cases = append(cases, execCase{
							name:     fmt.Sprintf("%s, %s=%d v%d flags=%#x", f.name, kind, count, vi, fl),
							code:     []string{enc},
							regs:     map[x86asm.Reg]uint64{x86asm.RAX: v, x86asm.RCX: 0xabcd00 | count},
							flags:    fl,
							cmpFlags: cmp,
						})
					}
				}
			}
		}
	}
	runExecCases(t, cases)
}

// SHLD/SHRD by CL and by an immediate: the count is masked to 5 bits (6 for 64-bit operands),
// a masked count of 0 leaves the value and every flag unchanged and still zero-extends a 32-bit
// register, CF is the last bit shifted out of the destination, OF is defined for a count of 1.
// 16-bit counts above 16 leave the result and flags undefined and are not run.
func TestExecShiftDouble(t *testing.T) {
	type form struct {
		name  string
		width int
		cl    string // encoding of OP reg, reg, cl
		imm   string // encoding of OP reg, reg, imm8 (count appended)
	}
	var forms []form
	for _, op := range []struct {
		name string
		cl   string
		imm  string
	}{{"shld", "0fa5d0", "0fa4d0"}, {"shrd", "0fadd0", "0facd0"}} {
		forms = append(forms,
			form{op.name + " ax, dx", 16, "66" + op.cl, "66" + op.imm},
			form{op.name + " eax, edx", 32, op.cl, op.imm},
			form{op.name + " rax, rdx", 64, "48" + op.cl, "48" + op.imm},
		)
	}
	values := []uint64{0x8000000180000001, 0x4000000040004041, 0xfffffffffffffffe, 1}
	const src = 0xc3a50f1e8d7b2469
	var cases []execCase
	for _, f := range forms {
		for _, count := range []uint64{0, 1, 2, 7, 15, 16, 31, 32, 33, 63, 64, 0xff} {
			mask := uint64(0x1f)
			if f.width == 64 {
				mask = 0x3f
			}
			mc := count & mask
			if mc > uint64(f.width) {
				continue
			}
			cmp := uint64(readMask)
			if mc > 1 {
				cmp &^= fOF
			}
			for vi, v := range values {
				for _, fl := range []uint64{0, fCF | fZF | fSF | fOF} {
					for _, enc := range []string{f.cl, f.imm + fmt.Sprintf("%02x", count)} {
						kind := "cl"
						if enc != f.cl {
							kind = "imm"
						}
						cases = append(cases, execCase{
							name:     fmt.Sprintf("%s, %s=%d v%d flags=%#x", f.name, kind, count, vi, fl),
							code:     []string{enc},
							regs:     map[x86asm.Reg]uint64{x86asm.RAX: v, x86asm.RCX: 0xabcd00 | count, x86asm.RDX: src},
							flags:    fl,
							cmpFlags: cmp,
						})
					}
				}
			}
		}
	}
	runExecCases(t, cases)
}

// String instructions (DF = 0): REP MOVS/STOS run element by element forward, so an overlapping
// forward copy replicates the leading elements; REP MOVS/STOS/LODS keep every flag; REPE/REPNE
// CMPS/SCAS compare at the operand width (SF/OF of a byte compare) and with RCX = 0 leave the
// flags of the previous instruction, in whatever carry sense the lifter tracked them.
func TestExecStringOps(t *testing.T) {
	pattern := make([]byte, 0x100)
	for i := range pattern {
		pattern[i] = byte(i*37 + 0x81)
	}
	const (
		addRaxRbx = "4801d8" // add rax, rbx: CF in the add sense
		cmpRaxRbx = "4839d8" // cmp rax, rbx: CF in the sub sense
		testRax   = "4885c0" // test rax, rax: CF = 0
	)
	src, dst := uint64(execMemBase+0x100), uint64(execMemBase+0x400)
	var cases []execCase
	type sop struct {
		name string
		enc  string
		elem int
	}
	movs := []sop{{"rep movsb", "f3a4", 1}, {"rep movsw", "66f3a5", 2}, {"rep movsd", "f3a5", 4}, {"rep movsq", "f348a5", 8}}
	stos := []sop{{"rep stosb", "f3aa", 1}, {"rep stosw", "66f3ab", 2}, {"rep stosd", "f3ab", 4}, {"rep stosq", "f348ab", 8}}
	preludes := []struct {
		name string
		code []string
	}{{"", nil}, {"add;", []string{addRaxRbx}}, {"cmp;", []string{cmpRaxRbx}}, {"test;", []string{testRax}}}
	for _, p := range preludes {
		for _, fl := range []uint64{0, fCF | fZF | fSF | fOF} {
			// RAX/RBX for the prelude: add carries, cmp borrows.
			regsFor := func(extra map[x86asm.Reg]uint64) map[x86asm.Reg]uint64 {
				r := map[x86asm.Reg]uint64{x86asm.RAX: 0x8000000000000001, x86asm.RBX: 0x8000000000000003}
				for k, v := range extra {
					r[k] = v
				}
				return r
			}
			for _, m := range movs {
				for _, n := range []uint64{0, 1, 7, 33} {
					for _, overlap := range []int{0, 1, m.elem, 3 * m.elem} {
						s, d := src, dst
						if overlap > 0 {
							d = src + uint64(overlap) // forward copy into its own source
						}
						cases = append(cases, execCase{
							name:  fmt.Sprintf("%s %s rcx=%d overlap=%d flags=%#x", p.name, m.name, n, overlap, fl),
							code:  append(append([]string{}, p.code...), m.enc),
							regs:  regsFor(map[x86asm.Reg]uint64{x86asm.RSI: s, x86asm.RDI: d, x86asm.RCX: n}),
							flags: fl,
							mem:   map[int][]byte{0x100: pattern},
						})
					}
				}
			}
			for _, m := range stos {
				for _, n := range []uint64{0, 5} {
					cases = append(cases, execCase{
						name:  fmt.Sprintf("%s %s rcx=%d flags=%#x", p.name, m.name, n, fl),
						code:  append(append([]string{}, p.code...), m.enc),
						regs:  regsFor(map[x86asm.Reg]uint64{x86asm.RDI: dst, x86asm.RCX: n, x86asm.RAX: 0x1122334455667788}),
						flags: fl,
					})
				}
			}
			// CMPS/SCAS. Strings: equal for 5 bytes, then 0x80 vs 0x01 (byte compare: SF/OF).
			a := []byte{1, 2, 3, 4, 5, 0x80, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}
			b := []byte{1, 2, 3, 4, 5, 0x01, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}
			for _, c := range []struct {
				name string
				enc  string
			}{{"repe cmpsb", "f3a6"}, {"repne cmpsb", "f2a6"}, {"repe cmpsq", "f348a7"}, {"repe cmpsw", "66f3a7"},
				{"repe cmpsd", "f3a7"}, {"cmpsb", "a6"}} {
				for _, n := range []uint64{0, 1, 3, 6, 16} {
					for _, swap := range []bool{false, true} {
						x, y := a, b
						if swap {
							x, y = b, a
						}
						cases = append(cases, execCase{
							name:  fmt.Sprintf("%s %s rcx=%d swap=%v flags=%#x", p.name, c.name, n, swap, fl),
							code:  append(append([]string{}, p.code...), c.enc),
							regs:  regsFor(map[x86asm.Reg]uint64{x86asm.RSI: src, x86asm.RDI: dst, x86asm.RCX: n}),
							flags: fl,
							mem:   map[int][]byte{0x100: x, 0x400: y},
						})
					}
				}
			}
			for _, c := range []struct {
				name string
				enc  string
			}{{"repne scasb", "f2ae"}, {"repe scasb", "f3ae"}, {"repne scasq", "f248af"}, {"scasb", "ae"}} {
				for _, n := range []uint64{0, 1, 6, 16} {
					for _, al := range []uint64{0x1234567800000005, 0xffffff80, 0x01} {
						cases = append(cases, execCase{
							name:  fmt.Sprintf("%s %s rcx=%d rax=%#x flags=%#x", p.name, c.name, n, al, fl),
							code:  append(append([]string{}, p.code...), c.enc),
							regs:  map[x86asm.Reg]uint64{x86asm.RDI: dst, x86asm.RCX: n, x86asm.RAX: al, x86asm.RBX: 3},
							flags: fl,
							mem:   map[int][]byte{0x400: a},
						})
					}
				}
			}
			for _, c := range []struct {
				name string
				enc  string
			}{{"lodsb", "ac"}, {"lodsd", "ad"}, {"lodsq", "48ad"}, {"rep lodsb", "f3ac"}} {
				for _, n := range []uint64{0, 3} {
					cases = append(cases, execCase{
						name:  fmt.Sprintf("%s %s rcx=%d flags=%#x", p.name, c.name, n, fl),
						code:  append(append([]string{}, p.code...), c.enc),
						regs:  regsFor(map[x86asm.Reg]uint64{x86asm.RSI: src, x86asm.RCX: n}),
						flags: fl,
						mem:   map[int][]byte{0x100: pattern},
					})
				}
			}
		}
	}
	runExecCases(t, cases)
}

// String instructions with DF = 1: every element moves to a decreasing address, one at a time,
// so a backward copy into an overlapping higher destination is exact (memmove) and one into a
// lower destination replicates the trailing elements. DF comes from STD (and CLD clears it
// again, inside the case), from the incoming RFLAGS (the state a previous block left), or from
// POPFQ; PUSHFQ stores it in bit 10. DF itself is compared after every case.
func TestExecStringOpsBackward(t *testing.T) {
	pattern := make([]byte, 0x100)
	for i := range pattern {
		pattern[i] = byte(i*37 + 0x81)
	}
	const std, cld = "fd", "fc"
	// Element 0 of each operand is at +0x180 of the window, the copies run below it.
	src, dst := uint64(execMemBase+0x180), uint64(execMemBase+0x480)
	type sop struct {
		name string
		enc  string
		elem int
	}
	var cases []execCase
	// dfSeqs: how DF = 1 reaches the instruction and whether the case clears it afterwards.
	dfSeqs := []struct {
		name      string
		pre, post []string
		flags     uint64
	}{{"std;", []string{std}, []string{cld}, 0}, {"df=1 in;", nil, nil, fDF}, {"std; (left set)", []string{std}, nil, 0}}
	seq := func(pre []string, op string, post []string) []string {
		return append(append(append([]string{}, pre...), op), post...)
	}
	movs := []sop{{"rep movsb", "f3a4", 1}, {"rep movsw", "66f3a5", 2}, {"rep movsd", "f3a5", 4}, {"rep movsq", "f348a5", 8}}
	for _, d := range dfSeqs {
		for _, m := range movs {
			for _, n := range []uint64{0, 1, 7, 33} {
				// overlap > 0: destination above the source (backward memmove);
				// overlap < 0: destination below it (the copy overwrites its own unread source).
				for _, overlap := range []int{0, 1, m.elem, 3 * m.elem, -1, -m.elem, -3 * m.elem} {
					s, dd := src, dst
					if overlap != 0 {
						dd = uint64(int64(src) + int64(overlap))
					}
					cases = append(cases, execCase{
						name:  fmt.Sprintf("%s %s rcx=%d overlap=%d", d.name, m.name, n, overlap),
						code:  seq(d.pre, m.enc, d.post),
						regs:  map[x86asm.Reg]uint64{x86asm.RSI: s, x86asm.RDI: dd, x86asm.RCX: n},
						flags: d.flags | fCF | fZF,
						mem:   map[int][]byte{0x100: pattern},
					})
				}
			}
		}
		for _, m := range []sop{{"movsb", "a4", 1}, {"movsw", "66a5", 2}, {"movsd", "a5", 4}, {"movsq", "48a5", 8}} {
			cases = append(cases, execCase{
				name:  fmt.Sprintf("%s %s", d.name, m.name),
				code:  seq(d.pre, m.enc, d.post),
				regs:  map[x86asm.Reg]uint64{x86asm.RSI: src, x86asm.RDI: dst, x86asm.RCX: 5},
				flags: d.flags,
				mem:   map[int][]byte{0x100: pattern},
			})
		}
		for _, m := range []sop{{"rep stosb", "f3aa", 1}, {"rep stosw", "66f3ab", 2}, {"rep stosd", "f3ab", 4},
			{"rep stosq", "f348ab", 8}, {"stosb", "aa", 1}, {"stosq", "48ab", 8}} {
			for _, n := range []uint64{0, 1, 5} {
				cases = append(cases, execCase{
					name:  fmt.Sprintf("%s %s rcx=%d", d.name, m.name, n),
					code:  seq(d.pre, m.enc, d.post),
					regs:  map[x86asm.Reg]uint64{x86asm.RDI: dst, x86asm.RCX: n, x86asm.RAX: 0x1122334455667788},
					flags: d.flags | fSF | fOF,
					mem:   map[int][]byte{0x400: pattern},
				})
			}
		}
		for _, m := range []sop{{"lodsb", "ac", 1}, {"lodsw", "66ad", 2}, {"lodsd", "ad", 4}, {"lodsq", "48ad", 8},
			{"rep lodsb", "f3ac", 1}, {"rep lodsq", "f348ad", 8}} {
			for _, n := range []uint64{0, 1, 3} {
				cases = append(cases, execCase{
					name:  fmt.Sprintf("%s %s rcx=%d", d.name, m.name, n),
					code:  seq(d.pre, m.enc, d.post),
					regs:  map[x86asm.Reg]uint64{x86asm.RSI: src, x86asm.RCX: n, x86asm.RAX: 0x8877665544332211},
					flags: d.flags,
					mem:   map[int][]byte{0x100: pattern},
				})
			}
		}
		// CMPS/SCAS backward: the operands end at +0x180 / +0x480 and are compared from there
		// down. Equal for the last 5 elements, then 0x80 vs 0x01 (byte compare: SF/OF).
		a := make([]byte, 0x90)
		b := make([]byte, 0x90)
		for i := range a {
			a[i] = byte(i + 1)
			b[i] = byte(i + 1)
		}
		a[0x80-5], b[0x80-5] = 0x80, 0x01
		for _, c := range []sop{{"repe cmpsb", "f3a6", 1}, {"repne cmpsb", "f2a6", 1}, {"repe cmpsw", "66f3a7", 2},
			{"repe cmpsd", "f3a7", 4}, {"repe cmpsq", "f348a7", 8}, {"cmpsb", "a6", 1}} {
			for _, n := range []uint64{0, 1, 3, 6, 16} {
				for _, swap := range []bool{false, true} {
					x, y := a, b
					if swap {
						x, y = b, a
					}
					cases = append(cases, execCase{
						name:  fmt.Sprintf("%s %s rcx=%d swap=%v", d.name, c.name, n, swap),
						code:  seq(d.pre, c.enc, d.post),
						regs:  map[x86asm.Reg]uint64{x86asm.RSI: src, x86asm.RDI: dst, x86asm.RCX: n},
						flags: d.flags,
						mem:   map[int][]byte{0x100: x, 0x400: y},
					})
				}
			}
		}
		for _, c := range []sop{{"repne scasb", "f2ae", 1}, {"repe scasb", "f3ae", 1}, {"repne scasq", "f248af", 8},
			{"repe scasw", "66f3af", 2}, {"scasb", "ae", 1}} {
			for _, n := range []uint64{0, 1, 6, 16} {
				for _, al := range []uint64{0x7a7a7a7a7a7a7a7b, 0xffffff80, 0x79} {
					cases = append(cases, execCase{
						name:  fmt.Sprintf("%s %s rcx=%d rax=%#x", d.name, c.name, n, al),
						code:  seq(d.pre, c.enc, d.post),
						regs:  map[x86asm.Reg]uint64{x86asm.RDI: dst, x86asm.RCX: n, x86asm.RAX: al},
						flags: d.flags,
						mem:   map[int][]byte{0x400: a},
					})
				}
			}
		}
	}
	// The eboot's backward memmove tail (dst > src, overlapping): RDI/RSI advanced past the end,
	// the RCX & 7 trailing bytes copied by REP MOVSB, then RDX >> 3 qwords by REP MOVSQ, all
	// with DF = 1 between STD and CLD.
	memmoveBack := []string{"4801cf", "4801ce", std, "4883e107", "48ffcf", "48ffce", "f3a4", "4889d1",
		"48c1e903", "4883ee07", "4883ef07", "f348a5", cld}
	for _, n := range []uint64{0, 1, 7, 8, 9, 40, 77} {
		for _, gap := range []uint64{1, 3, 8, 13, 0x100} {
			cases = append(cases, execCase{
				name: fmt.Sprintf("memmove backward n=%d dst=src+%d", n, gap),
				code: memmoveBack,
				regs: map[x86asm.Reg]uint64{x86asm.RSI: execMemBase + 0x100, x86asm.RDI: execMemBase + 0x100 + gap,
					x86asm.RCX: n, x86asm.RDX: n},
				noFlags: true, // ADD/AND/DEC/SHR/SUB flags of the bookkeeping: not the point here
				mem:     map[int][]byte{0x100: pattern},
			})
		}
	}
	// PUSHFQ stores DF in bit 10; POPFQ loads it, and the next string instruction follows it.
	for _, fl := range []uint64{0, fDF} {
		cases = append(cases, execCase{
			name: fmt.Sprintf("pushfq df=%d", fl>>10), code: []string{"9c"}, flags: fl | fCF, cmpFlags: readMask,
		})
		cases = append(cases, execCase{
			name: fmt.Sprintf("std; pushfq; cld df=%d", fl>>10), code: []string{std, "9c", cld}, flags: fl,
		})
		for _, img := range []uint64{0x202, 0x202 | fDF, 0x202 | fDF | fCF | fZF} {
			cases = append(cases, execCase{
				name:  fmt.Sprintf("popfq %#x; rep stosb df=%d", img, fl>>10),
				code:  []string{"9d", "f3aa"},
				regs:  map[x86asm.Reg]uint64{x86asm.RDI: dst, x86asm.RCX: 4, x86asm.RAX: 0x5a},
				flags: fl,
				mem:   map[int][]byte{int(execRSP - execMemBase): le64(img)},
			})
		}
	}
	runExecCases(t, cases)
}

// FDECSTP: TOP = (TOP - 1) & 7, wrapping from 0 to 7, the registers keep their values (ST(i)
// becomes ST(i+1)), and C1 is cleared. The values are read back through FLD ST(i) of registers
// that hold one (reading an empty register is a stack fault on x86; the lifter has no tags).
func TestExecX87Fdecstp(t *testing.T) {
	const (
		fldA      = "dd06"   // fld qword [rsi]
		fldB      = "dd4608" // fld qword [rsi+8]
		fdecstp   = "d9f6"
		fnstswAX  = "dfe0"
		fnstswM16 = "dd7f10" // fnstsw [rdi+16]
		fstpM0    = "dd1f"   // fstp qword [rdi]
		fstpM8    = "dd5f08" // fstp qword [rdi+8]
		fldenv    = "d966"   // fldenv [rsi+disp8]
	)
	src, dst := uint64(execMemBase+0x100), uint64(execMemBase+0x400)
	regs := map[x86asm.Reg]uint64{x86asm.RSI: src, x86asm.RDI: dst}
	vals := append(le64(math.Float64bits(1.25)), le64(math.Float64bits(-3.5))...)
	rep := func(n int, enc string) []string {
		var c []string
		for range n {
			c = append(c, enc)
		}
		return c
	}
	cases := []execCase{
		{name: "fdecstp from TOP 0 wraps to 7", code: []string{fdecstp, fnstswAX}, regs: regs},
		{name: "fdecstp x9 from TOP 0", code: append(rep(9, fdecstp), fnstswAX), regs: regs},
		{name: "fdecstp x8 is the identity", code: append(rep(8, fdecstp), fnstswAX), regs: regs},
		{
			// TOP 6 after two loads; FDECSTP: TOP 5, b = ST(1), a = ST(2).
			name: "fld a; fld b; fdecstp; fld st(1); fstp; fld st(2); fstp",
			code: []string{fldA, fldB, fdecstp, fnstswM16, "d9c1", fstpM0, "d9c2", fstpM8, fnstswAX},
			regs: regs, mem: map[int][]byte{0x100: vals},
		},
		{
			// FDECSTP from TOP 6 down past 0 to 7: ST(0) = a (register 7), ST(7) = b (register 6).
			// FST and FXCH read them without a push (register 6 is full).
			name: "fld a; fld b; fdecstp x7 (TOP 7); fst; fxch st(7); fst",
			code: append(append([]string{fldA, fldB}, rep(7, fdecstp)...), fnstswM16, "dd17", "d9cf", "dd5708", fnstswAX),
			regs: regs, mem: map[int][]byte{0x100: vals},
		},
	}
	// FLDENV with C1 (and C3/C2/C0) set and TOP = 2, all tags empty: FDECSTP clears C1 and keeps
	// the other condition codes, TOP = 1.
	for _, sw := range []uint16{0x1200, 0x5700, 0x1000} {
		env := make([]byte, 28)
		binary.LittleEndian.PutUint16(env[0:], 0x37f)
		binary.LittleEndian.PutUint16(env[4:], sw)
		binary.LittleEndian.PutUint16(env[8:], 0xffff)
		cases = append(cases, execCase{
			name: fmt.Sprintf("fldenv fsw=%#x; fdecstp", sw),
			code: []string{fldenv + "20", fdecstp, fnstswAX},
			regs: regs, mem: map[int][]byte{0x120: env},
		})
	}
	runExecCases(t, cases)
}

// MOV with a 64-bit absolute address (moffs, A0-A3): loads into AL/AX (the rest of RAX kept),
// EAX (zero-extended) and RAX, stores from each, at an unaligned address.
func TestExecMovMoffs(t *testing.T) {
	pattern := make([]byte, 0x40)
	for i := range pattern {
		pattern[i] = byte(i*29 + 0x93)
	}
	addr := hex.EncodeToString(le64(execMemBase + 0x203))
	var cases []execCase
	for _, f := range []struct{ name, op string }{
		{"mov al, [moffs]", "a0"}, {"mov ax, [moffs]", "66a1"}, {"mov eax, [moffs]", "a1"}, {"mov rax, [moffs]", "48a1"},
		{"mov [moffs], al", "a2"}, {"mov [moffs], ax", "66a3"}, {"mov [moffs], eax", "a3"}, {"mov [moffs], rax", "48a3"},
	} {
		cases = append(cases, execCase{
			name:  f.name,
			code:  []string{f.op + addr},
			regs:  map[x86asm.Reg]uint64{x86asm.RAX: 0x1122334455667788},
			flags: fCF | fSF,
			mem:   map[int][]byte{0x200: pattern},
		})
	}
	runExecCases(t, cases)
}

// Stack operations that address through RSP: PUSH RSP pushes the value before the decrement,
// POP RSP loads RSP from the old top (no increment after), PUSH [RSP+X] reads its operand
// before the decrement, POP [RSP+X] computes the address after the increment.
func TestExecStackRsp(t *testing.T) {
	stack := make([]byte, 0x40)
	for i := range stack {
		stack[i] = byte(0xa0 + i)
	}
	top := uint64(execRSP - 0x20)
	var cases []execCase
	for _, c := range []struct {
		name string
		code []string
	}{
		{"push rsp", []string{"54"}},
		{"push rsp; pop rax", []string{"54", "58"}},
		{"pop rsp", []string{"5c"}},
		{"push rsp; pop rsp", []string{"54", "5c"}},
		{"push qword [rsp+8]", []string{"ff742408"}},
		{"push qword [rsp]", []string{"ff3424"}},
		{"pop qword [rsp+8]", []string{"8f442408"}},
		{"pop qword [rsp]", []string{"8f0424"}},
		{"push qword [rsp+8]; pop rbx", []string{"ff742408", "5b"}},
	} {
		cases = append(cases, execCase{
			name: c.name,
			code: c.code,
			regs: map[x86asm.Reg]uint64{x86asm.RSP: top},
			// pop rsp loads this value: a valid address in the window
			mem: map[int][]byte{int(top-execMemBase) - 0x20: stack, int(top - execMemBase): le64(execMemBase + 0x1000)},
		})
	}
	runExecCases(t, cases)
}

// x87 rounding control: FIST/FISTP and FRNDINT round to an integer, FST m32 rounds to single,
// in the mode of the x87 control word (not MXCSR's). FISTTP truncates in every mode; NaN,
// infinities and out-of-range values store the integer indefinite.
func TestExecX87Rounding(t *testing.T) {
	values := []float64{0, 2.5, -2.5, 3.5, -3.5, 0.5, -0.5, 1.5, 2.4999999999999996, 1e10, -1e10, 4e9,
		32767.5, -32768.5, 65536.7, 9.3e18, -9.3e18, 1 + 0x1p-24, 1 + 0x1p-23 + 0x1p-24, -(1 + 0x1p-24),
		3.4028235677973366e38, 1e-46, -1e-46, math.Inf(1), math.Inf(-1), math.NaN(), 7.999999999999999e15}
	seq := []string{
		"d92e",   // fldcw [rsi]
		"dd07",   // fld qword [rdi]
		"df7f08", // fistp qword [rdi+8]
		"dd07",   // fld qword [rdi]
		"d9fc",   // frndint
		"dd5f10", // fstp qword [rdi+0x10]
		"dd07",   // fld qword [rdi]
		"d95718", // fst dword [rdi+0x18]
		"db5724", // fist dword [rdi+0x24]
		"db5f1c", // fistp dword [rdi+0x1c]
		"dd07",   // fld qword [rdi]
		"df5f20", // fistp word [rdi+0x20]
		"dd07",   // fld qword [rdi]
		"db4f28", // fisttp dword [rdi+0x28]
		"d97f2c", // fnstcw [rdi+0x2c]
	}
	var cases []execCase
	for rc := uint64(0); rc < 4; rc++ {
		for _, mx := range []uint32{0x1f80, 0x5f80} { // MXCSR nearest / up: must not matter
			for _, v := range values {
				code := seq
				if mx != 0x1f80 {
					code = append([]string{"0fae5604"}, seq...) // ldmxcsr [rsi+4]
				}
				ctl := make([]byte, 8)
				binary.LittleEndian.PutUint16(ctl, uint16(0x37f|rc<<10))
				binary.LittleEndian.PutUint32(ctl[4:], mx)
				cases = append(cases, execCase{
					name:  fmt.Sprintf("rc=%d mxcsr=%#x v=%g", rc, mx, v),
					code:  code,
					regs:  map[x86asm.Reg]uint64{x86asm.RSI: execMemBase + 0x80, x86asm.RDI: execMemBase + 0x100},
					mem:   map[int][]byte{0x80: ctl, 0x100: le64(math.Float64bits(v))},
					flags: fCF | fZF,
				})
			}
		}
	}
	runExecCases(t, cases)
}

// FNSTENV stores FCW, FSW with TOP and the tag word; FLDENV loads FCW and FSW, TOP included.
// The instruction and operand pointers (not modelled) are cleared in memory before the
// comparison, and so is the tag word after pushes (tags are not modelled; with an empty stack,
// the state at every call, it is compared).
//
// FNSTENV then masks all exceptions (SDM; libc's fegetenv reloads the control word for that
// reason). Rosetta 2 leaves the control word unchanged, so that part is checked on the lowering.
func TestExecX87Environment(t *testing.T) {
	const (
		fldcwRsi   = "d92e"             // fldcw [rsi]
		fnstenv    = "d97740"           // fnstenv [rdi+0x40]
		clearPtrs1 = "48c7474c00000000" // mov qword [rdi+0x4c], 0
		clearPtrs2 = "48c7475400000000" // mov qword [rdi+0x54], 0
		clearTags  = "c74748ffff0000"   // mov dword [rdi+0x48], 0xffff
		fnstcw60   = "d97f60"           // fnstcw [rdi+0x60]
		fld1       = "d9e8"
		fldenv     = "d96770" // fldenv [rdi+0x70]
		fnstswAx   = "dfe0"   // fnstsw ax
		fldRdi     = "dd07"   // fld qword [rdi]
		fistp64    = "db5f64" // fistp dword [rdi+0x64]
	)
	env := make([]byte, 28)
	binary.LittleEndian.PutUint32(env[0:], 0x0f7f)             // RC toward zero
	binary.LittleEndian.PutUint32(env[4:], 0x4000|5<<11|0x100) // C3, C0, TOP = 5
	binary.LittleEndian.PutUint32(env[8:], 0xffff)
	var cases []execCase
	for _, cw := range []uint16{0x037f, 0x0360, 0x0b72} {
		ctl := make([]byte, 2)
		binary.LittleEndian.PutUint16(ctl, cw)
		mem := map[int][]byte{0x80: ctl, 0x100: le64(math.Float64bits(2.75)), 0x170: env}
		regs := map[x86asm.Reg]uint64{x86asm.RSI: execMemBase + 0x80, x86asm.RDI: execMemBase + 0x100}
		cases = append(cases,
			execCase{name: fmt.Sprintf("fnstenv empty cw=%#x", cw), code: []string{fldcwRsi, fnstenv, clearPtrs1, clearPtrs2},
				regs: regs, mem: mem},
			execCase{name: fmt.Sprintf("fnstenv two pushes cw=%#x", cw),
				code: []string{fldcwRsi, fld1, fld1, fnstenv, clearPtrs1, clearPtrs2, clearTags}, regs: regs, mem: mem},
			execCase{name: fmt.Sprintf("fldenv cw=%#x", cw), code: []string{fldcwRsi, fldenv, fnstswAx, fnstcw60, fldRdi, fistp64},
				regs: regs, mem: mem},
		)
	}
	runExecCases(t, cases)
	requireInOrder(t, "fnstenv", liftBlock(t, "d930")[0], "ldrh w17, [x5, #0x2e4]", "str w17, [x16, #0]",
		"orr w17, w17, #0x3f", "strh w17, [x5, #0x2e4]")
}

// FCOM/FCOMP/FCOMPP/FICOM set C0/C2/C3 in the status word and leave EFLAGS alone.
func TestExecX87ComparesKeepFlags(t *testing.T) {
	var cases []execCase
	for _, c := range []struct {
		name string
		code []string
	}{
		{"fcom m64", []string{"dd07", "dc5708", "dd7f30", "ddd8"}},
		{"fcomp m64", []string{"dd07", "dc5f08", "dd7f30"}},
		{"fcompp", []string{"dd4708", "dd07", "ded9", "dd7f30"}},
		{"fcom st1", []string{"dd4708", "dd07", "d8d1", "dd7f30", "ddd8", "ddd8"}},
		{"ficom m32", []string{"dd07", "da5710", "dd7f30", "ddd8"}},
	} {
		for _, p := range [][2]float64{{1, 2}, {2, 1}, {2, 2}} {
			for _, rr := range [][2]uint64{{1, 2}, {2, 1}, {5, 5}, {0x8000000000000000, 1}} {
				ints := make([]byte, 4)
				binary.LittleEndian.PutUint32(ints, uint32(int32(p[1])))
				cases = append(cases, execCase{
					name: fmt.Sprintf("cmp %d,%d; %s %g,%g", rr[0], rr[1], c.name, p[0], p[1]),
					code: append([]string{"4839d8"}, c.code...), // cmp rax, rbx
					regs: map[x86asm.Reg]uint64{x86asm.RAX: rr[0], x86asm.RBX: rr[1], x86asm.RDI: execMemBase + 0x100},
					mem: map[int][]byte{0x100: le64(math.Float64bits(p[0])), 0x108: le64(math.Float64bits(p[1])),
						0x110: ints},
				})
			}
		}
	}
	runExecCases(t, cases)
}

// VCVTPS2PH rounds by its imm8: bits 1:0 give the mode unless bit 2 selects MXCSR.RC.
func TestExecVcvtps2phRounding(t *testing.T) {
	vals := []float32{1 + 0x1p-11, 1 + 3*0x1p-12, -(1 + 0x1p-11), 65520, 1e-7, 2049, -2049, 65519}
	src := make([]byte, 32)
	for i, v := range vals {
		binary.LittleEndian.PutUint32(src[4*i:], math.Float32bits(v))
	}
	var cases []execCase
	for rc := uint32(0); rc < 4; rc++ {
		mx := make([]byte, 4)
		binary.LittleEndian.PutUint32(mx, 0x1f80|rc<<13)
		for imm := 0; imm < 8; imm++ {
			ib := fmt.Sprintf("%02x", imm)
			cases = append(cases, execCase{
				name: fmt.Sprintf("mxcsr.rc=%d imm=%d", rc, imm),
				code: []string{
					"0fae16",            // ldmxcsr [rsi]
					"c5f8100f",          // vmovups xmm1, [rdi]
					"c4e3791d4f20" + ib, // vcvtps2ph qword [rdi+0x20], xmm1, imm
					"c4e3791dca" + ib,   // vcvtps2ph xmm2, xmm1, imm
					"c5f9d65730",        // vmovq [rdi+0x30], xmm2
					"c5fc100f",          // vmovups ymm1, [rdi]
					"c4e37d1d4f40" + ib, // vcvtps2ph xmmword [rdi+0x40], ymm1, imm
					"c4e37d1dcb" + ib,   // vcvtps2ph xmm3, ymm1, imm
					"c5f8115f50",        // vmovups [rdi+0x50], xmm3
				},
				regs: map[x86asm.Reg]uint64{x86asm.RSI: execMemBase + 0x80, x86asm.RDI: execMemBase + 0x100},
				mem:  map[int][]byte{0x80: mx, 0x100: src},
			})
		}
	}
	runExecCases(t, cases)
}
