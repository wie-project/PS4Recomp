package lifter_test

import (
	"sort"
	"testing"

	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/lifter"
)

func TestLifterCoverage(t *testing.T) {
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

	l := lifter.NewLifter(map[uint64]bool{})

	supported := 0
	unsupportedOp := make(map[x86asm.Op]int)
	var firstErrors []string

	for _, fn := range d.Functions {
		for _, b := range fn.Blocks {
			for _, inst := range b.Insts {
				nextPC := inst.Address + uint64(inst.Inst.Len)
				_, err := l.LiftInstruction(inst, nextPC, fn)
				if err == nil {
					supported++
				} else {
					unsupportedOp[inst.Inst.Op]++
					if len(firstErrors) < 10 {
						firstErrors = append(firstErrors, err.Error())
					}
				}
			}
		}
	}

	total := supported
	for _, count := range unsupportedOp {
		total += count
	}

	t.Logf("Total instructions tested: %d", total)
	t.Logf("Supported: %d (%.1f%%)", supported, float64(supported)/float64(total)*100.0)

	type opStat struct {
		op    x86asm.Op
		count int
	}
	var unsuppList []opStat
	for op, count := range unsupportedOp {
		unsuppList = append(unsuppList, opStat{op, count})
	}
	sort.Slice(unsuppList, func(i, j int) bool {
		return unsuppList[i].count > unsuppList[j].count
	})

	t.Logf("Unsupported opcodes count: %d", len(unsuppList))
	for i := 0; i < len(unsuppList) && i < 20; i++ {
		t.Logf("  Missing #%2d: %-12s (used %d times)", i+1, unsuppList[i].op.String(), unsuppList[i].count)
	}

	if len(firstErrors) > 0 {
		t.Logf("Sample errors:")
		for _, e := range firstErrors {
			t.Logf("  %s", e)
		}
	}
}
