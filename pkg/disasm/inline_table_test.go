package disasm

import (
	"debug/elf"
	"encoding/binary"
	"testing"

	"ps4-recomp/pkg/elfloader"
)

// Switch tables placed inside the function that uses them, as the PS4 toolchain emits them:
// after the code and inside both the symbol size and the .eh_frame extent of the function
// (Gravity Rush Remastered, eboot 0x452a30 with tables at 0x4534b0..0x453530). Their entries
// must not be decoded as instructions (`d9 f6 ff ff` is FDECSTP; UD2; UD2).

// asm is a small assembler buffer for the tests below.
type asm struct {
	img []byte
	pc  uint64
}

func newAsm(size int, at uint64) *asm { return &asm{img: make([]byte, size), pc: at} }

func (a *asm) put(b ...byte) uint64 {
	at := a.pc
	copy(a.img[at:], b)
	a.pc += uint64(len(b))
	return at
}

func (a *asm) align(n uint64) {
	for a.pc%n != 0 {
		a.put(0xcc)
	}
}

// rel8 patches the 8-bit displacement of the 2-byte branch at at to reach target.
func (a *asm) rel8(at, target uint64) { a.img[at+1] = byte(int8(int64(target) - int64(at+2))) }

// ripDisp patches the disp32 of the RIP-relative instruction at at (length n, disp32 at
// offset off) to reach target.
func (a *asm) ripDisp(at uint64, off, n int, target uint64) {
	binary.LittleEndian.PutUint32(a.img[at+uint64(off):], uint32(int32(int64(target)-int64(at+uint64(n)))))
}

// rel32Table writes a Clang PIC table (entries relative to the table) at the current pc.
func (a *asm) rel32Table(targets ...uint64) uint64 {
	t := a.pc
	for _, c := range targets {
		var b [4]byte
		binary.LittleEndian.PutUint32(b[:], uint32(int32(int64(c)-int64(t))))
		a.put(b[:]...)
	}
	return t
}

// sizedFunc is a LoadedELF whose function [entry, end) has an STT_FUNC symbol of that size and
// an unwind extent of the same range, so it is decoded by the linear decoder.
func sizedFunc(img []byte, entry, end uint64) *elfloader.LoadedELF {
	l := unwoundFunc(img, entry, end)
	l.SymbolByAddr[entry] = elfloader.Symbol{Name: "sw", Address: entry, Size: end - entry, Type: elf.STT_FUNC}
	l.Symbols = []elfloader.Symbol{l.SymbolByAddr[entry]}
	return l
}

// unwoundFunc is a LoadedELF with an unwind extent [entry, end) and no symbol: the
// branch-following decoder handles the function.
func unwoundFunc(img []byte, entry, end uint64) *elfloader.LoadedELF {
	l := switchTestELF(img)
	l.FuncBounds = []elfloader.AddrRange{{Start: entry, End: end}}
	l.UnwindRanges = []elfloader.AddrRange{{Start: 0, End: uint64(len(img))}}
	return l
}

func decodeOne(t *testing.T, loaded *elfloader.LoadedELF, entry uint64) *Function {
	t.Helper()
	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}
	fn, _, err := d.DisasmFunction(entry)
	if err != nil {
		t.Fatal(err)
	}
	return fn
}

// noInstIn fails for every decoded instruction of fn that overlaps [start, end).
func noInstIn(t *testing.T, fn *Function, start, end uint64) {
	t.Helper()
	for _, b := range fn.Blocks {
		for _, in := range b.Insts {
			if in.Address < end && start < in.Address+uint64(in.Inst.Len) {
				t.Errorf("instruction %v at 0x%x decoded from table bytes [0x%x, 0x%x)", in.Inst, in.Address, start, end)
			}
		}
	}
}

func wantTargets(t *testing.T, fn *Function, jmp uint64, want []uint64) {
	t.Helper()
	got := fn.JumpTables[jmp]
	if len(got) != len(want) {
		t.Fatalf("table of the jmp at 0x%x: targets %#x, want %#x", jmp, got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("table of the jmp at 0x%x: targets %#x, want %#x", jmp, got, want)
		}
	}
}

func wantBlocks(t *testing.T, fn *Function, addrs ...uint64) {
	t.Helper()
	for _, a := range addrs {
		if fn.Blocks[a] == nil {
			t.Errorf("no block at 0x%x (blocks %#x)", a, fn.BlockOrder)
		}
	}
}

// A rel32 table in the middle of a sized function: the table is skipped, and the code after it
// (a case and the default) is decoded from its own start.
func TestInlineRel32TableIsNotCode(t *testing.T) {
	a := newAsm(0x400, 0x100)
	entry := a.put(0x83, 0xfe, 0x03) // cmp esi, 3
	ja := a.put(0x77, 0)             // ja def
	lea := a.put(0x48, 0x8d, 0x05, 0, 0, 0, 0)
	a.put(0x48, 0x63, 0x0c, 0xb0) // movsxd rcx, [rax+rsi*4]
	a.put(0x48, 0x01, 0xc1)       // add rcx, rax
	jmp := a.put(0xff, 0xe1)      // jmp rcx
	c0 := a.put(0xb8, 1, 0, 0, 0, 0xc3)
	c1 := a.put(0xb8, 2, 0, 0, 0, 0xc3)
	c2 := a.put(0xb8, 3, 0, 0, 0, 0xc3)
	a.align(4)
	table := a.pc
	a.pc += 16
	c3 := a.put(0xb8, 4, 0, 0, 0, 0xc3)
	def := a.put(0x31, 0xc0, 0xc3) // xor eax, eax; ret
	end := a.pc
	a.pc = table
	a.rel32Table(c0, c1, c2, c3)
	a.rel8(ja, def)
	a.ripDisp(lea, 3, 7, table)

	fn := decodeOne(t, sizedFunc(a.img, entry, end), entry)
	wantTargets(t, fn, jmp, []uint64{c0, c1, c2, c3})
	noInstIn(t, fn, table, table+16)
	wantBlocks(t, fn, c0, c1, c2, c3, def)
}

// An absolute (8-byte) table read by `jmp [rdx+rsi*8]`, inside a sized function.
func TestInlineAbs64TableIsNotCode(t *testing.T) {
	a := newAsm(0x400, 0x100)
	entry := a.put(0x83, 0xfe, 0x02) // cmp esi, 2
	ja := a.put(0x77, 0)             // ja def
	lea := a.put(0x48, 0x8d, 0x15, 0, 0, 0, 0)
	jmp := a.put(0xff, 0x24, 0xf2) // jmp [rdx+rsi*8]
	c0 := a.put(0xb8, 1, 0, 0, 0, 0xc3)
	c1 := a.put(0xb8, 2, 0, 0, 0, 0xc3)
	c2 := a.put(0xb8, 3, 0, 0, 0, 0xc3)
	a.align(8)
	table := a.pc
	for _, c := range []uint64{c0, c1, c2} {
		var b [8]byte
		binary.LittleEndian.PutUint64(b[:], c)
		a.put(b[:]...)
	}
	def := a.put(0x31, 0xc0, 0xc3)
	end := a.pc
	a.rel8(ja, def)
	a.ripDisp(lea, 3, 7, table)

	fn := decodeOne(t, sizedFunc(a.img, entry, end), entry)
	wantTargets(t, fn, jmp, []uint64{c0, c1, c2})
	noInstIn(t, fn, table, table+24)
	wantBlocks(t, fn, def)
}

// The bounds check is in a predecessor block (`cmp ecx, 2; jbe switch`), as in libSceFios2
// 0x2417960, and the code between them in address order writes the index, so only a search
// along the predecessors finds the bound. The linear decoder proves the table length through
// its block graph. The dword
// after the table reads as an entry pointing at the jbe: a scan of an unproven table would take
// it as a case and split the check from its jump, so unproven targets are left out of the graph.
func TestInlineTableBoundInPredecessor(t *testing.T) {
	a := newAsm(0x400, 0x100)
	entry := a.put(0x83, 0xf9, 0x02) // cmp ecx, 2
	jbe := a.put(0x76, 0)            // jbe sw
	a.put(0x31, 0xc9, 0x89, 0xc8)    // xor ecx, ecx; mov eax, ecx (the index is written off the switch path)
	a.put(0xc3)                      // ret
	sw := a.put(0x48, 0x8d, 0x05, 0, 0, 0, 0)
	a.put(0x48, 0x63, 0x0c, 0x88) // movsxd rcx, [rax+rcx*4]
	a.put(0x48, 0x01, 0xc1)       // add rcx, rax
	jmp := a.put(0xff, 0xe1)      // jmp rcx
	c0 := a.put(0xb8, 1, 0, 0, 0, 0xc3)
	c1 := a.put(0xb8, 2, 0, 0, 0, 0xc3)
	c2 := a.put(0xb8, 3, 0, 0, 0, 0xc3)
	a.align(4)
	table := a.rel32Table(c0, c1, c2)
	var b [4]byte
	binary.LittleEndian.PutUint32(b[:], uint32(int32(int64(jbe)-int64(table))))
	a.put(b[:]...)
	end := a.pc
	a.rel8(jbe, sw)
	a.ripDisp(sw, 3, 7, table)

	fn := decodeOne(t, sizedFunc(a.img, entry, end), entry)
	wantTargets(t, fn, jmp, []uint64{c0, c1, c2})
	noInstIn(t, fn, table, table+12)
	if b := fn.Blocks[entry]; b == nil || len(b.Insts) != 2 {
		t.Errorf("the check and its jbe are not one block: %#x", fn.BlockOrder)
	}
}

// Branch-following: a call that does not return (not known as such) is followed by the table.
// The fall-through is decoded before the switch is resolved; the function is decoded again
// with the table known (eboot 0x230d00, table 0x235d70 after a call to an import stub).
func TestTableAfterCallIsNotCode(t *testing.T) {
	a := newAsm(0x400, 0x100)
	entry := a.put(0x83, 0xfe, 0x02) // cmp esi, 2
	ja := a.put(0x77, 0)             // ja fail
	lea := a.put(0x48, 0x8d, 0x05, 0, 0, 0, 0)
	a.put(0x48, 0x63, 0x0c, 0xb0) // movsxd rcx, [rax+rsi*4]
	a.put(0x48, 0x01, 0xc1)       // add rcx, rax
	jmp := a.put(0xff, 0xe1)      // jmp rcx
	c0 := a.put(0xb8, 1, 0, 0, 0, 0xc3)
	c1 := a.put(0xb8, 2, 0, 0, 0, 0xc3)
	c2 := a.put(0xb8, 3, 0, 0, 0, 0xc3)
	fail := a.put(0xe8, 0, 0, 0, 0) // call 0x300
	a.ripDisp(fail, 1, 5, 0x300)
	table := a.rel32Table(c0, c1, c2)
	end := a.pc
	a.img[0x300] = 0xc3
	a.rel8(ja, fail)
	a.ripDisp(lea, 3, 7, table)

	fn := decodeOne(t, unwoundFunc(a.img, entry, end), entry)
	wantTargets(t, fn, jmp, []uint64{c0, c1, c2})
	noInstIn(t, fn, table, table+12)
	wantBlocks(t, fn, fail)
}
