package disasm_test

import (
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"

	"golang.org/x/arch/x86/x86asm"
)

func TestDisasmSample(t *testing.T) {
	elfPath := "../../hello_world.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		t.Fatalf("failed to load elf: %v", err)
	}

	pc := uint64(0xb0e70)
	limit := uint64(0xb0e85)
	namePtr := uint64(loaded.MemoryImage[0x1065b8]) |
		(uint64(loaded.MemoryImage[0x1065b9]) << 8) |
		(uint64(loaded.MemoryImage[0x1065ba]) << 16) |
		(uint64(loaded.MemoryImage[0x1065bb]) << 24) |
		(uint64(loaded.MemoryImage[0x1065bc]) << 32) |
		(uint64(loaded.MemoryImage[0x1065bd]) << 40) |
		(uint64(loaded.MemoryImage[0x1065be]) << 48) |
		(uint64(loaded.MemoryImage[0x1065bf]) << 56)
	t.Logf("namePtr: 0x%x", namePtr)
	if namePtr < uint64(len(loaded.MemoryImage)) {
		t.Logf("Type name: %s", string(loaded.MemoryImage[namePtr:namePtr+40]))
	}
	for pc < limit {
		inst, err := x86asm.Decode(loaded.MemoryImage[pc:], 64)
		if err != nil {
			t.Fatalf("decode error at 0x%x: %v", pc, err)
		}
		t.Logf("0x%x: %s (op=%v, len=%d, args=%v)", pc, inst.String(), inst.Op, inst.Len, inst.Args)
		pc += uint64(inst.Len)
	}
}

func TestDisasmAtomicAdd(t *testing.T) {
	elfPath := "../../hello_world.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		t.Fatalf("failed to load elf: %v", err)
	}

	addr := uint64(0x58e10)
	sym := loaded.SymbolByAddr[addr]
	t.Logf("Sym: %s, size: 0x%x", sym.Name, sym.Size)

	pc := addr
	limit := addr + sym.Size
	for pc < limit {
		inst, err := x86asm.Decode(loaded.MemoryImage[pc:], 64)
		if err != nil {
			t.Fatalf("decode error at 0x%x: %v", pc, err)
		}
		pc += uint64(inst.Len)
	}

	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		t.Fatalf("NewDisassembler error: %v", err)
	}
	if err := d.AnalyzeReachable([]uint64{0x58e10}); err != nil {
		t.Fatalf("AnalyzeReachable error: %v", err)
	}
	fn := d.Functions[0x58e10]
	t.Logf("Function %s has %d blocks:", fn.Name, len(fn.Blocks))
	for _, bAddr := range fn.BlockOrder {
		t.Logf("  Block at 0x%x (insts: %d)", bAddr, len(fn.Blocks[bAddr].Insts))
	}
	if _, ok := fn.Blocks[0x58eb1]; !ok {
		t.Errorf("Expected block at 0x58eb1 to be discovered, but it was not!")
	}
}
