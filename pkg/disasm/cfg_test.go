package disasm_test

import (
	"testing"

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
