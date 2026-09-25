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

func TestNoReturnTerminatesBlock(t *testing.T) {
	img := make([]byte, 0x200)
	// Entry at 0x10: call abort (at 0x80)
	// Call instruction: E8 <rel32>
	// nextPC = 0x15, rel32 = 0x80 - 0x15 = 0x6b
	img[0x10] = 0xe8
	img[0x11] = 0x6b
	img[0x12] = 0x00
	img[0x13] = 0x00
	img[0x14] = 0x00
	// Bytes following the call: garbage/padding that would otherwise be decoded
	img[0x15] = 0x90 // nop
	img[0x16] = 0x90 // nop

	// Target 0x80: abort function
	img[0x80] = 0x0f // ud2
	img[0x81] = 0x0b

	loaded := imageELF(img, []elfloader.AddrRange{{Start: 0x10, End: 0x50}})
	loaded.Symbols = []elfloader.Symbol{
		{Name: "abort", Address: 0x80, Size: 2, Type: 2},
	}
	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}
	fn, _, err := d.DisasmFunction(0x10)
	if err != nil {
		t.Fatal(err)
	}
	// The function starting at 0x10 should only contain the CALL instruction (len 5, ending at 0x15).
	// It must NOT decode the trailing NOPs at 0x15.
	totalInsts := 0
	for _, b := range fn.Blocks {
		for _, inst := range b.Insts {
			totalInsts++
			if inst.Address >= 0x15 {
				t.Fatalf("decoded instruction 0x%x past noreturn call", inst.Address)
			}
		}
	}
	if totalInsts != 1 {
		t.Fatalf("expected 1 instruction, got %d", totalInsts)
	}
}

func TestBlockSplitsOnMaxBytes(t *testing.T) {
	const codeSize = 9000
	img := make([]byte, codeSize+0x100)
	for i := 0; i < codeSize-1; i++ {
		img[0x10+i] = 0x90 // nop
	}
	img[0x10+codeSize-1] = 0xc3 // ret

	loaded := imageELF(img, []elfloader.AddrRange{{Start: 0x10, End: uint64(0x10 + codeSize)}})
	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}
	fn, _, err := d.DisasmFunction(0x10)
	if err != nil {
		t.Fatal(err)
	}
	totalInsts := 0
	for _, b := range fn.Blocks {
		totalInsts += len(b.Insts)
	}
	if totalInsts != codeSize {
		t.Fatalf("expected %d instructions across split blocks, got %d", codeSize, totalInsts)
	}
	if len(fn.Blocks) <= 1 {
		t.Fatalf("expected function to split into multiple blocks (> 8192 bytes), got %d blocks", len(fn.Blocks))
	}
	if d.CapHits != 0 {
		t.Fatalf("block splitting should not record a cap hit, got %d cap hits", d.CapHits)
	}
}

