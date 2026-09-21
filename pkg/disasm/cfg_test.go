package disasm_test

import (
	"testing"

	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
)

func TestCFGAnalysis(t *testing.T) {
	elfPath := "../../hello_world.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		t.Fatalf("failed to load elf: %v", err)
	}

	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		t.Fatalf("failed to create disasm: %v", err)
	}

	entries := []uint64{loaded.EntryPoint}
	entries = append(entries, loaded.InitArray...)
	if mainSym, ok := loaded.SymbolByName["main"]; ok {
		entries = append(entries, mainSym.Address)
	}

	err = d.AnalyzeReachable(entries)
	if err != nil {
		t.Fatalf("AnalyzeReachable failed: %v", err)
	}

	t.Logf("Discovered %d reachable functions", len(d.Functions))
	totalBlocks := 0
	totalInsts := 0
	for _, fn := range d.Functions {
		totalBlocks += len(fn.Blocks)
		for _, b := range fn.Blocks {
			totalInsts += len(b.Insts)
		}
	}
	t.Logf("Total blocks: %d, Total instructions: %d", totalBlocks, totalInsts)
}

func TestReachableWithRelocs(t *testing.T) {
	elfPath := "../../hello_world.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		t.Fatalf("failed to load elf: %v", err)
	}

	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		t.Fatalf("failed to create disasm: %v", err)
	}

	textSec := loaded.Sections[".text"]
	entries := []uint64{loaded.EntryPoint}
	entries = append(entries, loaded.InitArray...)
	if mainSym, ok := loaded.SymbolByName["main"]; ok {
		entries = append(entries, mainSym.Address)
	}

	for _, rel := range loaded.Relocations {
		if rel.Addend > 0 {
			target := uint64(rel.Addend)
			if target >= textSec.Addr && target < textSec.Addr+textSec.Size {
				entries = append(entries, target)
			}
		}
	}

	err = d.AnalyzeReachable(entries)
	if err != nil {
		t.Fatalf("AnalyzeReachable failed: %v", err)
	}

	t.Logf("Discovered %d reachable functions with relocs", len(d.Functions))
	totalBlocks := 0
	totalInsts := 0
	for _, fn := range d.Functions {
		totalBlocks += len(fn.Blocks)
		for _, b := range fn.Blocks {
			totalInsts += len(b.Insts)
		}
	}
	t.Logf("Total blocks: %d, Total instructions: %d", totalBlocks, totalInsts)
}

func TestDisasmPixelFormat(t *testing.T) {
	elfPath := "../../tools/OpenOrbis/PS4Toolchain/samples/SDL2/SDL2/x64/Debug/SDL2.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		t.Fatalf("failed to load elf: %v", err)
	}

	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		t.Fatalf("failed to create disasm: %v", err)
	}

	sym := loaded.SymbolByAddr[0x1317d0]
	t.Logf("Sym at 0x1317d0: name=%s size=0x%x", sym.Name, sym.Size)

	_, _, err = d.DisasmFunction(0x1317d0)
	if err != nil {
		t.Fatalf("DisasmFunction failed: %v", err)
	}

	raw := loaded.MemoryImage[0x131b74 : 0x131b74+32]
	t.Logf("Bytes at 0x131b74: %x", raw)

	// Scan all text section for decode failures
	textSec := loaded.Sections[".text"]
	textStart := textSec.Addr
	textEnd := textStart + textSec.Size

	errorCounts := make(map[string]int)
	totalFailed := 0
	totalInsts := 0

	pc := textStart
	for pc < textEnd {
		inst, err := x86asm.Decode(loaded.MemoryImage[pc:], 64)
		if err != nil {
			totalFailed++
			msg := err.Error()
			errorCounts[msg]++
			if totalFailed <= 20 {
				t.Logf("Decode fail at 0x%x: %v (bytes: %02x %02x %02x %02x %02x %02x)",
					pc, err,
					loaded.MemoryImage[pc], loaded.MemoryImage[pc+1], loaded.MemoryImage[pc+2],
					loaded.MemoryImage[pc+3], loaded.MemoryImage[pc+4], loaded.MemoryImage[pc+5])
			}
			pc++
			continue
		}
		totalInsts++
		pc += uint64(inst.Len)
	}

	t.Logf("Total successfully decoded: %d, total failures: %d", totalInsts, totalFailed)
	for msg, count := range errorCounts {
		t.Logf("  Error '%s': %d times", msg, count)
	}
}
