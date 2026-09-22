package disasm

import (
	"testing"

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

	d, err := NewDisassembler(loaded)
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

func TestCheckEntryPoint(t *testing.T) {
	elfPath := "../../hello_world.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		t.Fatalf("failed to load elf: %v", err)
	}

	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatalf("NewDisassembler error: %v", err)
	}

	err = d.AnalyzeReachable([]uint64{loaded.EntryPoint})
	t.Logf("AnalyzeReachable error: %v, total funcs: %d", err, len(d.Functions))
	for fAddr, fn := range d.Functions {
		t.Logf("  Func 0x%x: %s (blocks: %d)", fAddr, fn.Name, len(fn.Blocks))
	}
}

func TestBMIDecoding(t *testing.T) {
	// bextr %ecx, %eax, %eax: c4 e2 70 f7 c0
	bextrBytes := []byte{0xc4, 0xe2, 0x70, 0xf7, 0xc0}
	inst, err := x86asm.Decode(bextrBytes, 64)
	if err != nil {
		t.Fatalf("decode bextr failed: %v", err)
	}
	if inst.Op != x86asm.BEXTR {
		t.Fatalf("expected BEXTR op, got %v", inst.Op)
	}
	if inst.Len != 5 {
		t.Fatalf("expected len 5, got %d", inst.Len)
	}
	t.Logf("decoded successfully: %s", inst.String())

	testCases := []struct {
		name string
		raw  []byte
		op   x86asm.Op
	}{
		{"ANDN", []byte{0xc4, 0xe2, 0x70, 0xf2, 0xc2}, x86asm.ANDN},
		{"BZHI", []byte{0xc4, 0xe2, 0x70, 0xf5, 0xc2}, x86asm.BZHI},
		{"BLSR", []byte{0xc4, 0xe2, 0x78, 0xf3, 0xcb}, x86asm.BLSR},
		{"BLSMSK", []byte{0xc4, 0xe2, 0x78, 0xf3, 0xd3}, x86asm.BLSMSK},
		{"BLSI", []byte{0xc4, 0xe2, 0x78, 0xf3, 0xdb}, x86asm.BLSI},
		{"SHLX", []byte{0xc4, 0xe2, 0x71, 0xf7, 0xc6}, x86asm.SHLX},
		{"SHRX", []byte{0xc4, 0xe2, 0x73, 0xf7, 0xc6}, x86asm.SHRX},
		{"SARX", []byte{0xc4, 0xe2, 0x72, 0xf7, 0xc6}, x86asm.SARX},
		{"RORX", []byte{0xc4, 0xe3, 0x7b, 0xf0, 0xc6, 0x04}, x86asm.RORX},
		{"MULX", []byte{0xc4, 0xe2, 0x73, 0xf6, 0xc6}, x86asm.MULX},
	}

	for _, tc := range testCases {
		decoded, err := x86asm.Decode(tc.raw, 64)
		if err != nil {
			t.Errorf("%s decode failed: %v", tc.name, err)
			continue
		}
		if decoded.Op != tc.op {
			t.Errorf("%s: expected op %v, got %v", tc.name, tc.op, decoded.Op)
		}
		t.Logf("%s decoded: %s", tc.name, decoded.String())
	}
}

func TestDiscoverReachableWithMergedPRX(t *testing.T) {
	prxPath := "../../tools/OpenOrbis/PS4Toolchain/samples/using_library/sce_module/libExample.prx"
	mainPath := "../../tools/OpenOrbis/PS4Toolchain/samples/using_library/using_library/x64/Debug/using_library.elf"

	mainLoaded, err := elfloader.LoadELF(mainPath)
	if err != nil {
		t.Skipf("sample elf not found: %v", err)
	}
	prxLoaded, err := elfloader.LoadELF(prxPath)
	if err != nil {
		t.Skipf("sample prx not found: %v", err)
	}
	if err := prxLoaded.ApplyBias(0x20000); err != nil {
		t.Fatal(err)
	}
	if err := elfloader.MergeImages(mainLoaded, prxLoaded); err != nil {
		t.Fatal(err)
	}

	d, err := NewDisassembler(mainLoaded)
	if err != nil {
		t.Fatal(err)
	}

	entries := SeedEntryPoints(mainLoaded, false)
	// Add PRX exported functions to entries as done in CLI
	for _, sym := range prxLoaded.ExportedFunctions() {
		entries = append(entries, sym.Address)
	}

	funcs, err := d.DiscoverReachable(entries, nil)
	if err != nil {
		t.Fatal(err)
	}

	hasTarget := false
	for _, f := range funcs {
		if f.Addr == 0x8c380 {
			hasTarget = true
			break
		}
	}
	if !hasTarget {
		t.Errorf("expected 0x8c380 to be discovered via reachable CFG analysis from merged PRX")
	}
}
