package lifter

import (
	"fmt"
	"testing"

	"golang.org/x/arch/x86/x86asm"
)

// AMD SSE4A EXTRQ and INSERTQ, both forms, against hand-computed values (Rosetta 2 does not
// execute SSE4A, so there is no x86 reference run). The destination is loaded from +0x100, the
// source from +0x110, and the destination is stored at +0x120. Bits 127:64 of the destination
// are undefined in the AMD manual; the lowering leaves them as they were, and the expectations
// say so. Every field here has index + length <= 64 (larger sums are undefined).
func TestExecSSE4A(t *testing.T) {
	const (
		dLo = 0x0123456789abcdef
		dHi = 0xfedcba9876543210
	)
	type tc struct {
		name           string
		op             string // the SSE4A instruction, hex
		load           []string
		store          string
		dstLo, dstHi   uint64
		srcLo, srcHi   uint64
		wantLo, wantHi uint64
	}
	xmm01 := []string{"f30f6f07", "f30f6f4f10"} // movdqu xmm0, [rdi]; movdqu xmm1, [rdi+0x10]
	st0 := "f30f7f4720"                         // movdqu [rdi+0x20], xmm0
	xmm910 := []string{"f3440f6f0f", "f3440f6f5710"}
	st9 := "f3440f7f4f20"
	imm := func(l, i byte) string { return fmt.Sprintf("%02x%02x", l, i) }

	var cs []tc
	// EXTRQ xmm0, xmm1: length xmm1[5:0], index xmm1[13:8]; the other control bits are ignored.
	for _, v := range []struct{ ctl, want uint64 }{
		{0x0408, 0xde},               // length 8, index 4
		{0x3010, 0x0123},             // length 16, index 48
		{0, dLo},                     // length 0 = 64, index 0
		{0x0034, 0x0003456789abcdef}, // length 52, index 0
		{0xabcd00000000e1c4, 0x3},    // length 4, index 33 (0xe1 & 63)
		{0xffffffffffffc000, dLo},    // length 0, index 0 (0xc0 & 63)
		{0x3f01, 0},                  // length 1, index 63: bit 63 of dLo is 0
		{0x3f01 | 1<<62, 0},          // high control bits ignored
		{0x0001, 1},                  // length 1, index 0
	} {
		cs = append(cs, tc{fmt.Sprintf("extrq ctl=%#x", v.ctl), "660f79c1", xmm01, st0,
			dLo, dHi, v.ctl, 0x5555555555555555, v.want, dHi})
	}
	cs = append(cs, tc{"extrq xmm9, xmm10 (REX)", "66450f79ca", xmm910, st9,
		dLo, dHi, 0x0408, 0, 0xde, dHi})

	// EXTRQ xmm0, imm8 length, imm8 index (bits 5:0 of each byte).
	for _, v := range []struct {
		l, i byte
		want uint64
	}{
		{8, 4, 0xde}, {0, 0, dLo}, {12, 52, 0x012}, {0xc8, 0x44, 0xde}, {1, 63, 0}, {32, 32, 0x01234567},
	} {
		cs = append(cs, tc{fmt.Sprintf("extrq imm %d,%d", v.l, v.i), "660f78c0" + imm(v.l, v.i), xmm01, st0,
			dLo, dHi, 0, 0, v.want, dHi})
	}
	cs = append(cs, tc{"extrq xmm9, 8, 4 (REX)", "66410f78c1" + imm(8, 4), xmm910, st9,
		dLo, dHi, 0, 0, 0xde, dHi})

	// INSERTQ xmm0, xmm1: field xmm1[length-1:0], length xmm1[69:64], index xmm1[77:72].
	for _, v := range []struct{ field, ctl, want uint64 }{
		{0x77777777777777a5, 0x1008, 0x0123456789a5cdef},             // length 8, index 16
		{0x1122334455667788, 0, 0x1122334455667788},                  // length 64, index 0
		{0xfffffffffffffffa, 0x3c04, 0xa123456789abcdef},             // length 4, index 60
		{0x77777777777777a5, 0xffffffffffffd0c8, 0x0123456789a5cdef}, // high control bits ignored
		{0, 0x0120, 0x0123456600000001},                              // length 32, index 1: bits 32:1 cleared
	} {
		cs = append(cs, tc{fmt.Sprintf("insertq ctl=%#x", v.ctl), "f20f79c1", xmm01, st0,
			dLo, dHi, v.field, v.ctl, v.want, dHi})
	}
	// Destination = source: the field and the control are read before the destination is written.
	cs = append(cs, tc{"insertq xmm0, xmm0", "f20f79c0", xmm01, st0,
		dLo, 0x1008, 0, 0, 0x0123456789efcdef, 0x1008})
	cs = append(cs, tc{"insertq xmm9, xmm10 (REX)", "f2450f79ca", xmm910, st9,
		dLo, dHi, 0x77777777777777a5, 0x1008, 0x0123456789a5cdef, dHi})

	// INSERTQ xmm0, xmm1, imm8 length, imm8 index.
	for _, v := range []struct {
		l, i        byte
		field, want uint64
	}{
		{8, 16, 0x77777777777777a5, 0x0123456789a5cdef},
		{0, 0, 0x1122334455667788, 0x1122334455667788},
		{4, 60, 0xfffffffffffffffa, 0xa123456789abcdef},
		{0x48, 0xd0, 0x77777777777777a5, 0x0123456789a5cdef},
		{32, 32, 0xffffffff, 0xffffffff89abcdef},
	} {
		cs = append(cs, tc{fmt.Sprintf("insertq imm %d,%d", v.l, v.i), "f20f78c1" + imm(v.l, v.i), xmm01, st0,
			dLo, dHi, v.field, 0xdeadbeef, v.want, dHi})
	}

	var cases []execCase
	var want []map[int][]byte
	for _, c := range cs {
		code := append(append([]string{}, c.load...), c.op, c.store)
		cases = append(cases, execCase{
			name:    c.name,
			code:    code,
			regs:    map[x86asm.Reg]uint64{x86asm.RDI: execMemBase + 0x100},
			mem:     map[int][]byte{0x100: append(le64(c.dstLo), le64(c.dstHi)...), 0x110: append(le64(c.srcLo), le64(c.srcHi)...)},
			noFlags: true,
		})
		want = append(want, map[int][]byte{0x120: append(le64(c.wantLo), le64(c.wantHi)...)})
	}
	runExecExpect(t, cases, want)
}
