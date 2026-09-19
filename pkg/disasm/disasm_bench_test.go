package disasm_test

import (
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
)

func BenchmarkAnalyzeReachable(b *testing.B) {
	elfPath := "../../hello_world.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		b.Fatalf("failed to load elf: %v", err)
	}

	entries := []uint64{loaded.EntryPoint}
	entries = append(entries, loaded.InitArray...)
	if mainSym, ok := loaded.SymbolByName["main"]; ok {
		entries = append(entries, mainSym.Address)
	}

	b.ResetTimer()
	b.ReportAllocs()

	for i := 0; i < b.N; i++ {
		d, err := disasm.NewDisassembler(loaded)
		if err != nil {
			b.Fatalf("failed to create disasm: %v", err)
		}
		if err := d.AnalyzeReachable(entries); err != nil {
			b.Fatalf("AnalyzeReachable failed: %v", err)
		}
	}
}
