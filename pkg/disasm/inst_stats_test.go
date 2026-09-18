package disasm_test

import (
	"sort"
	"testing"

	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
)

func TestOpcodeStats(t *testing.T) {
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

	if err := d.AnalyzeReachable(entries); err != nil {
		t.Fatalf("AnalyzeReachable failed: %v", err)
	}

	opCounts := make(map[x86asm.Op]int)
	for _, fn := range d.Functions {
		for _, b := range fn.Blocks {
			for _, inst := range b.Insts {
				opCounts[inst.Inst.Op]++
			}
		}
	}

	type opCount struct {
		op    x86asm.Op
		count int
	}
	var list []opCount
	for op, count := range opCounts {
		list = append(list, opCount{op, count})
	}
	sort.Slice(list, func(i, j int) bool {
		return list[i].count > list[j].count
	})

	t.Logf("Total unique opcodes: %d", len(list))
	for i := 0; i < len(list) && i < 40; i++ {
		t.Logf("#%2d: %-12s %d", i+1, list[i].op.String(), list[i].count)
	}
}
