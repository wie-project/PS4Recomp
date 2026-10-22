package disasm

import (
	"encoding/binary"
	"testing"

	"golang.org/x/arch/x86/x86asm"
)

// Byte-indexed dispatch of an interpreter (the shape of Uncharted 4 fn_0xaa500 at 0xaa5c7):
//
//	0x100: movzx eax, byte [r10]
//	0x104: lea rcx, [rip+table]
//	0x10b: movsxd rax, [rcx+4*rax]
//	0x10f: add rax, rcx
//	0x112: jmp rax
//
// The index has no bounds check. Cases start at 0x120: case 0 is "mov eax, imm32; ret" (so it
// has addresses inside an instruction), cases 1-3 are "ret".
const (
	bdJmp   = 0x112
	bdCase0 = 0x120
	bdTable = 0x1000
)

func byteDispatchImage() []byte {
	img := make([]byte, 0x2000)
	copy(img[0x100:], []byte{
		0x41, 0x0f, 0xb6, 0x02, // movzx eax, byte [r10]
		0x48, 0x8d, 0x0d, 0, 0, 0, 0, // lea rcx, [rip+table]
		0x48, 0x63, 0x04, 0x81, // movsxd rax, [rcx+4*rax]
		0x48, 0x01, 0xc8, // add rax, rcx
		0xff, 0xe0, // jmp rax
	})
	binary.LittleEndian.PutUint32(img[0x107:], uint32(int32(bdTable-0x10b)))
	copy(img[bdCase0:], []byte{0xb8, 0x11, 0x22, 0x33, 0x44, 0xc3, 0xc3, 0xc3, 0xc3})
	return img
}

// byteDispatchSplitImage is the dispatch exactly as Uncharted 4 has it: a conditional branch
// between the byte load and the table jump, so the load is in a predecessor block.
//
//	0x100: movzx eax, byte [r10]
//	0x104: test cl, cl
//	0x106: jne case 3
//	0x108: lea rcx, [rip+table] ; 0x10f: movsxd rax, [rcx+4*rax] ; 0x113: add rax, rcx
//	0x116: jmp rax
func byteDispatchSplitImage() []byte {
	img := make([]byte, 0x2000)
	copy(img[0x100:], []byte{
		0x41, 0x0f, 0xb6, 0x02, // movzx eax, byte [r10]
		0x84, 0xc9, // test cl, cl
		0x75, 0x00, // jne case 3 (patched)
		0x48, 0x8d, 0x0d, 0, 0, 0, 0, // lea rcx, [rip+table]
		0x48, 0x63, 0x04, 0x81, // movsxd rax, [rcx+4*rax]
		0x48, 0x01, 0xc8, // add rax, rcx
		0xff, 0xe0, // jmp rax
	})
	img[0x107] = byte(bdCase(3) - 0x108)
	binary.LittleEndian.PutUint32(img[0x10b:], uint32(int32(bdTable-0x10f)))
	copy(img[bdCase0:], []byte{0xb8, 0x11, 0x22, 0x33, 0x44, 0xc3, 0xc3, 0xc3, 0xc3})
	return img
}

// The byte load in a predecessor block caps the scan too (found through the predecessors).
func TestByteDispatchCapFromPredecessor(t *testing.T) {
	img := byteDispatchSplitImage()
	for i := 0; i < 256; i++ {
		putRel(img, i, bdCase(i%3))
	}
	putRel(img, 256, bdCase0+1) // the next table
	putRel(img, 257, bdCase0+2)
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
	if got := fn.JumpTables[0x116]; len(got) != 3 {
		t.Fatalf("table targets %#x, want the 3 cases", got)
	}
	for _, a := range []uint64{bdCase0 + 1, bdCase0 + 2} {
		if fn.Blocks[a] != nil {
			t.Errorf("0x%x inside case 0's mov became a block", a)
		}
	}
}

// bdCase returns the address of case i (0-3).
func bdCase(i int) uint64 {
	if i == 0 {
		return bdCase0
	}
	return bdCase0 + 5 + uint64(i)
}

func putRel(img []byte, slot int, target uint64) {
	binary.LittleEndian.PutUint32(img[bdTable+4*slot:], uint32(int32(int64(target)-bdTable)))
}

func decodeWindow(t *testing.T, img []byte, from, to uint64) []Instruction {
	t.Helper()
	var w []Instruction
	for pc := from; pc < to; {
		in, err := x86asm.Decode(img[pc:], 64)
		if err != nil {
			t.Fatal(err)
		}
		w = append(w, Instruction{Address: pc, Inst: in})
		pc += uint64(in.Len)
	}
	return w
}

// A full 256-entry table followed by another table: the byte index caps the scan at 256
// entries, so the next table's entries (read relative to this table's base they point into
// the mov of case 0) are not taken. The count stays unproven (weak path).
func TestByteDispatchCapsTableScan(t *testing.T) {
	img := byteDispatchImage()
	for i := 0; i < 256; i++ {
		putRel(img, i, bdCase(i%4))
	}
	putRel(img, 256, bdCase0+1) // the next table
	putRel(img, 257, bdCase0+2)

	d, err := NewDisassembler(switchTestELF(img))
	if err != nil {
		t.Fatal(err)
	}
	sw, ok := d.ResolveJumpTable(decodeWindow(t, img, 0x100, bdJmp+2), nil, nil, 0x100, uint64(len(img)))
	if !ok {
		t.Fatal("dispatch not recognized")
	}
	if sw.Count != 0 || sw.Cap != 256 {
		t.Fatalf("count %d cap %d, want an unproven count capped at 256", sw.Count, sw.Cap)
	}
	if len(sw.Targets) != 4 {
		t.Fatalf("targets %#x, want the 4 cases", sw.Targets)
	}
	for _, tg := range sw.Targets {
		if tg == bdCase0+1 || tg == bdCase0+2 {
			t.Fatalf("entry past the 256th taken: %#x", sw.Targets)
		}
	}

	if err := d.AnalyzeReachable([]uint64{0x100}); err != nil {
		t.Fatal(err)
	}
	fn := d.Functions[0x100]
	if fn == nil {
		t.Fatal("function not recovered")
	}
	for i := 0; i < 4; i++ {
		if fn.Blocks[bdCase(i)] == nil {
			t.Errorf("case %d at 0x%x is not a block", i, bdCase(i))
		}
	}
	for _, a := range []uint64{bdCase0 + 1, bdCase0 + 2} {
		if fn.Blocks[a] != nil {
			t.Errorf("0x%x inside case 0's mov became a block", a)
		}
	}
}

// A short byte-indexed table (3 entries) followed by other data: the scan still stops at the
// first entry that is no code address of the function, as for any unproven table, instead of
// reading 256 entries.
func TestShortByteDispatchTableStopsAtData(t *testing.T) {
	img := byteDispatchImage()
	for i := 0; i < 3; i++ {
		putRel(img, i, bdCase(i))
	}
	binary.LittleEndian.PutUint32(img[bdTable+12:], 0x7fff0000) // outside the image
	putRel(img, 4, bdCase0+1)                                    // another table after it
	putRel(img, 5, bdCase(3))

	d, err := NewDisassembler(switchTestELF(img))
	if err != nil {
		t.Fatal(err)
	}
	sw, ok := d.ResolveJumpTable(decodeWindow(t, img, 0x100, bdJmp+2), nil, nil, 0x100, uint64(len(img)))
	if !ok {
		t.Fatal("dispatch not recognized")
	}
	want := map[uint64]bool{bdCase(0): true, bdCase(1): true, bdCase(2): true}
	if len(sw.Targets) != 3 {
		t.Fatalf("targets %#x, want the 3 real cases", sw.Targets)
	}
	for _, tg := range sw.Targets {
		if !want[tg] {
			t.Fatalf("targets %#x, want the 3 real cases", sw.Targets)
		}
	}
}

// byteIndexCap follows register copies to the byte load and ignores other index sources.
func TestByteIndexCap(t *testing.T) {
	dec := func(code ...[]byte) []Instruction {
		var b []byte
		for _, c := range code {
			b = append(b, c...)
		}
		return decodeWindow(t, append(b, 0x90), 0, uint64(len(b)))
	}
	for _, c := range []struct {
		name string
		w    []Instruction
		want int
	}{
		{"movzx eax, byte [r10]", dec([]byte{0x41, 0x0f, 0xb6, 0x02}), 256},
		{"movzx ecx, byte [r10]; mov eax, ecx", dec([]byte{0x41, 0x0f, 0xb6, 0x0a}, []byte{0x89, 0xc8}), 256},
		{"movzx eax, byte [r10]; test cl, cl", dec([]byte{0x41, 0x0f, 0xb6, 0x02}, []byte{0x84, 0xc9}), 256},
		{"movzx eax, word [r10]", dec([]byte{0x41, 0x0f, 0xb7, 0x02}), 0},
		{"movzx eax, cl", dec([]byte{0x0f, 0xb6, 0xc1}), 0},
		{"movzx eax, byte [r10]; inc eax", dec([]byte{0x41, 0x0f, 0xb6, 0x02}, []byte{0xff, 0xc0}), 0},
	} {
		if got := byteIndexCap(c.w, x86asm.RAX); got != c.want {
			t.Errorf("%s: cap %d, want %d", c.name, got, c.want)
		}
	}
}
