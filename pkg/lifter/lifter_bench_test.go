package lifter_test

import (
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/lifter"
)

func BenchmarkLiftFunction(b *testing.B) {
	elfPath := "../../hello_world.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		b.Fatalf("failed to load elf: %v", err)
	}

	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		b.Fatalf("failed to create disasm: %v", err)
	}

	mainSym := loaded.SymbolByName["main"]
	if err := d.AnalyzeReachable([]uint64{mainSym.Address}); err != nil {
		b.Fatalf("analyze main: %v", err)
	}

	mainFn, ok := d.Functions[mainSym.Address]
	if !ok {
		b.Fatalf("main not found")
	}

	l := lifter.NewLifter(map[uint64]bool{mainSym.Address: true})

	b.ResetTimer()
	b.ReportAllocs()

	for i := 0; i < b.N; i++ {
		for _, blockAddr := range mainFn.BlockOrder {
			block := mainFn.Blocks[blockAddr]
			for _, inst := range block.Insts {
				nextPC := inst.Address + uint64(inst.Inst.Len)
				_, err := l.LiftInstruction(inst, nextPC, mainFn)
				if err != nil {
					b.Fatalf("error lifting: %v", err)
				}
			}
		}
	}
}

func BenchmarkLiftFunctionToBuf(b *testing.B) {
	elfPath := "../../hello_world.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		b.Fatalf("failed to load elf: %v", err)
	}

	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		b.Fatalf("failed to create disasm: %v", err)
	}

	mainSym := loaded.SymbolByName["main"]
	if err := d.AnalyzeReachable([]uint64{mainSym.Address}); err != nil {
		b.Fatalf("analyze main: %v", err)
	}

	mainFn, ok := d.Functions[mainSym.Address]
	if !ok {
		b.Fatalf("main not found")
	}

	l := lifter.NewLifter(map[uint64]bool{mainSym.Address: true})

	b.ResetTimer()
	b.ReportAllocs()

	var buf []string
	for i := 0; i < b.N; i++ {
		for _, blockAddr := range mainFn.BlockOrder {
			block := mainFn.Blocks[blockAddr]
			for _, inst := range block.Insts {
				nextPC := inst.Address + uint64(inst.Inst.Len)
				lines, err := l.LiftInstructionToBuf(inst, nextPC, mainFn, buf[:0])
				if err != nil {
					b.Fatalf("error lifting: %v", err)
				}
				buf = lines
			}
		}
	}
}
