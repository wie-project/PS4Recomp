package lifter_test

import (
	"fmt"
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/lifter"
)

func TestLiftMain(t *testing.T) {
	elfPath := "../../hello_world.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		t.Fatalf("failed to load elf: %v", err)
	}

	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		t.Fatalf("failed to create disasm: %v", err)
	}

	mainSym := loaded.SymbolByName["main"]
	if err := d.AnalyzeReachable([]uint64{mainSym.Address}); err != nil {
		t.Fatalf("analyze main: %v", err)
	}

	mainFn, ok := d.Functions[mainSym.Address]
	if !ok {
		t.Fatalf("main not found in disasm")
	}

	l := lifter.NewLifter(map[uint64]bool{mainSym.Address: true})

	var cLines []string
	cLines = append(cLines, "void fn_main(GuestContext *ctx) {")

	for _, blockAddr := range mainFn.BlockOrder {
		block := mainFn.Blocks[blockAddr]
		cLines = append(cLines, "")
		cLines = append(cLines, fmt.Sprintf("loc_0x%x:", blockAddr))
		for _, inst := range block.Insts {
			nextPC := inst.Address + uint64(inst.Inst.Len)
			lines, err := l.LiftInstruction(inst, nextPC, mainFn)
			if err != nil {
				t.Fatalf("error lifting inst at 0x%x: %v", inst.Address, err)
			}
			cLines = append(cLines, lines...)
		}
	}
	cLines = append(cLines, "}")

	t.Logf("Generated C code for main:\n%s", strings.Join(cLines[:40], "\n"))
}
