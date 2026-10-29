package disasm

import (
	"testing"

	"golang.org/x/arch/x86/x86asm"
)

// x86asm does not know AMD's SSE4A: for 66/F2 0F 78/79 it returns the mandatory prefix alone as
// a one-byte instruction of op 0, which the CFG pass used to turn into a UD2 that ended the block.
func TestDecodeSSE4A(t *testing.T) {
	cases := []struct {
		name string
		b    []byte
		op   x86asm.Op
		args []x86asm.Arg
		text string
	}{
		{"extrq xmm1, xmm7", []byte{0x66, 0x0f, 0x79, 0xcf}, OpEXTRQ,
			[]x86asm.Arg{x86asm.X1, x86asm.X7}, "EXTRQ X1, X7"},
		{"extrq xmm5, xmm15 (REX.B)", []byte{0x66, 0x41, 0x0f, 0x79, 0xef}, OpEXTRQ,
			[]x86asm.Arg{x86asm.X5, x86asm.X15}, "EXTRQ X5, X15"},
		{"extrq xmm10, xmm0 (REX.R)", []byte{0x66, 0x44, 0x0f, 0x79, 0xd0}, OpEXTRQ,
			[]x86asm.Arg{x86asm.X10, x86asm.X0}, "EXTRQ X10, X0"},
		{"extrq xmm9, 8, 4", []byte{0x66, 0x41, 0x0f, 0x78, 0xc1, 0x08, 0x04}, OpEXTRQ,
			[]x86asm.Arg{x86asm.X9, x86asm.Imm(8), x86asm.Imm(4)}, "EXTRQ X9, 0x8, 0x4"},
		{"insertq xmm1, xmm2", []byte{0xf2, 0x0f, 0x79, 0xca}, OpINSERTQ,
			[]x86asm.Arg{x86asm.X1, x86asm.X2}, "INSERTQ X1, X2"},
		{"insertq xmm1, xmm2, 8, 16", []byte{0xf2, 0x0f, 0x78, 0xca, 0x08, 0x10}, OpINSERTQ,
			[]x86asm.Arg{x86asm.X1, x86asm.X2, x86asm.Imm(8), x86asm.Imm(16)}, "INSERTQ X1, X2, 0x8, 0x10"},
		{"insertq xmm12, xmm3 (REX.R)", []byte{0xf2, 0x44, 0x0f, 0x79, 0xe3}, OpINSERTQ,
			[]x86asm.Arg{x86asm.X12, x86asm.X3}, "INSERTQ X12, X3"},
	}
	for _, c := range cases {
		// A trailing ret checks that the length is the instruction's own.
		b := append(append([]byte{}, c.b...), 0xc3)
		inst, err := Decode(b)
		if err != nil {
			t.Errorf("%s: %v", c.name, err)
			continue
		}
		if inst.Op != c.op || inst.Len != len(c.b) {
			t.Errorf("%s: op %v len %d, want %v len %d", c.name, inst.Op, inst.Len, c.op, len(c.b))
		}
		for i, a := range c.args {
			if inst.Args[i] != a {
				t.Errorf("%s: arg %d = %v, want %v", c.name, i, inst.Args[i], a)
			}
		}
		if inst.Args[len(c.args)] != nil {
			t.Errorf("%s: extra argument %v", c.name, inst.Args[len(c.args)])
		}
		if got := InstString(inst); got != c.text {
			t.Errorf("%s: text %q, want %q", c.name, got, c.text)
		}
	}
}

// Encodings that are not SSE4A keep x86asm's decoding: a memory operand (SSE4A has register
// forms only), EXTRQ-immediate with ModRM.reg != 0, VMREAD/VMWRITE without the prefix, and the
// MOVNTSS/MOVNTSD that x86asm already decodes.
func TestDecodeSSE4ARejects(t *testing.T) {
	for _, b := range [][]byte{
		{0x66, 0x0f, 0x79, 0x07},             // mod != 3
		{0xf2, 0x0f, 0x78, 0x07, 0x08, 0x10}, // mod != 3
		{0x66, 0x0f, 0x78, 0xc9, 0x08, 0x04}, // /1
		{0x0f, 0x78, 0xc8},                   // VMREAD
		{0x0f, 0x79, 0xc8},                   // VMWRITE
		{0xf2, 0x0f, 0x2b, 0x07},             // MOVNTSD
		{0xf3, 0x0f, 0x2b, 0x07},             // MOVNTSS
		{0x66, 0x0f},                         // truncated
		{0x66, 0x0f, 0x78, 0xc1, 0x08},       // truncated immediate
	} {
		got, gerr := Decode(b)
		want, werr := x86asm.Decode(b, 64)
		if got.Op == OpEXTRQ || got.Op == OpINSERTQ || got.Op != want.Op || got.Len != want.Len || (gerr == nil) != (werr == nil) {
			t.Errorf("% x: %v len %d err %v, x86asm %v len %d err %v", b, got.Op, got.Len, gerr, want.Op, want.Len, werr)
		}
	}
}

// A function with EXTRQ in the middle is decoded to its end: the SSE4A instruction is lifted, not
// a block-ending UD2 that drops the rest (inFAMOUS Second Son fn 0x88fd70 at 0x890a77).
func TestCFGContinuesPastSSE4A(t *testing.T) {
	img := make([]byte, 0x200)
	copy(img[0x100:], []byte{
		0x66, 0x0f, 0x79, 0xcf, // extrq xmm1, xmm7
		0xf2, 0x0f, 0x78, 0xca, 0x08, 0x10, // insertq xmm1, xmm2, 8, 16
		0x31, 0xc0, // xor eax, eax
		0xc3, // ret
	})
	d, err := NewDisassembler(switchTestELF(img))
	if err != nil {
		t.Fatal(err)
	}
	if err := d.AnalyzeReachable([]uint64{0x100}); err != nil {
		t.Fatal(err)
	}
	fn := d.Functions[0x100]
	if fn == nil {
		t.Fatal("function not recovered")
	}
	var ops []x86asm.Op
	for _, a := range fn.BlockOrder {
		for _, in := range fn.Blocks[a].Insts {
			ops = append(ops, in.Inst.Op)
		}
	}
	want := []x86asm.Op{OpEXTRQ, OpINSERTQ, x86asm.XOR, x86asm.RET}
	if len(ops) != len(want) {
		t.Fatalf("ops %v, want %v", ops, want)
	}
	for i := range want {
		if ops[i] != want[i] {
			t.Fatalf("ops %v, want %v", ops, want)
		}
	}
}
