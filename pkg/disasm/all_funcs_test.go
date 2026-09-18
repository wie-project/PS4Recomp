package disasm_test

import (
	"debug/elf"
	"testing"
	"time"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
)

func TestDisasmAllSymbolsAndRelocs(t *testing.T) {
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

	var entries []uint64
	entries = append(entries, loaded.EntryPoint)
	entries = append(entries, loaded.InitArray...)

	for _, sym := range loaded.Symbols {
		if sym.Type == elf.STT_FUNC && sym.Address != 0 {
			entries = append(entries, sym.Address)
		}
	}

	for _, rel := range loaded.Relocations {
		if rel.Addend > 0 {
			target := uint64(rel.Addend)
			if target >= textSec.Addr && target < textSec.Addr+textSec.Size {
				entries = append(entries, target)
			}
		}
	}

	start := time.Now()
	if err := d.AnalyzeReachable(entries); err != nil {
		t.Fatalf("AnalyzeReachable failed: %v", err)
	}
	elapsed := time.Since(start)

	t.Logf("Disassembled %d functions in %v", len(d.Functions), elapsed)
}
