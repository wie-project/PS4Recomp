package disasm

import (
	"encoding/binary"
	"testing"

	"ps4-recomp/pkg/elfloader"

	"golang.org/x/arch/x86/x86asm"
)

func imageELF(img []byte, bounds []elfloader.AddrRange) *elfloader.LoadedELF {
	return &elfloader.LoadedELF{
		MemoryImage:  img,
		ExecRanges:   []elfloader.AddrRange{{Start: 0, End: uint64(len(img))}},
		FuncBounds:   bounds,
		SymbolByAddr: map[uint64]elfloader.Symbol{},
		SymbolByName: map[string]elfloader.Symbol{},
	}
}

func TestUnwindCeilingStopsBeforeFollowingCode(t *testing.T) {
	img := make([]byte, 0x100)
	img[0x40] = 0x90 // nop
	// Looks like a real instruction, but it belongs to the next extent.
	img[0x41] = 0x48
	img[0x42] = 0x89
	img[0x43] = 0xc0 // mov rax, rax
	loaded := imageELF(img, []elfloader.AddrRange{{Start: 0x40, End: 0x41}})
	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}
	fn, _, err := d.DisasmFunction(0x40)
	if err != nil {
		t.Fatal(err)
	}
	n := 0
	for _, b := range fn.Blocks {
		n += len(b.Insts)
		for _, inst := range b.Insts {
			if inst.Address != 0x40 {
				t.Fatalf("decoded 0x%x %s past unwind end", inst.Address, inst.Inst)
			}
		}
	}
	if n != 1 {
		t.Fatalf("instructions = %d", n)
	}
}

func TestPrivilegedOpcodeEndsBlock(t *testing.T) {
	img := make([]byte, 0x100)
	img[0x40] = 0x90 // nop
	img[0x41] = 0x6e // outsb
	img[0x42] = 0x6e
	loaded := imageELF(img, nil)
	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}
	fn, _, err := d.DisasmFunction(0x40)
	if err != nil {
		t.Fatal(err)
	}
	for _, b := range fn.Blocks {
		for _, inst := range b.Insts {
			if inst.Inst.Op == x86asm.OUTSB || inst.Inst.Op == x86asm.OUTSD || inst.Inst.Op == x86asm.INSD {
				t.Fatalf("privileged opcode kept at 0x%x", inst.Address)
			}
		}
	}
	if d.PrivilegedStops != 1 {
		t.Fatalf("privileged stops = %d", d.PrivilegedStops)
	}
}

func TestSeedIgnoresNonPointerAddend(t *testing.T) {
	img := make([]byte, 0x100)
	img[0x40] = 0x55 // push rbp
	binary.LittleEndian.PutUint64(img[0x80:], 0x40)
	loaded := imageELF(img, []elfloader.AddrRange{{Start: 0x40, End: 0x41}})
	loaded.Relocations = []elfloader.Relocation{
		{Type: 2, Offset: 0x10, Addend: 0x40}, // R_X86_64_PC32, not a pointer
		{Type: elfloader.R_X86_64_RELATIVE, Offset: 0x80, Addend: 0x40},
	}
	entries := SeedEntryPoints(loaded, false)
	if len(entries) != 1 || entries[0] != 0x40 {
		t.Fatalf("seeds = %v", entries)
	}

	// Same pointer, but not an unwind entry: do not invent a function.
	loaded.FuncBounds = []elfloader.AddrRange{{Start: 0x50, End: 0x60}}
	entries = SeedEntryPoints(loaded, false)
	if len(entries) != 0 {
		t.Fatalf("non-entry pointer seeded: %v", entries)
	}
}
