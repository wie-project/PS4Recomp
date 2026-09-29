package lifter_test

import (
	"fmt"
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/lifter"

	"golang.org/x/arch/x86/x86asm"
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

func TestLiftIndirectJmpWithJumpTableEmitsLocalSwitch(t *testing.T) {
	fn := &disasm.Function{
		Name:      "fn_test_switch",
		EntryAddr: 0x1000,
		EndAddr:   0x1200,
		Blocks: map[uint64]*disasm.BasicBlock{
			0x1000: {StartAddr: 0x1000, EndAddr: 0x1008},
			0x1020: {StartAddr: 0x1020, EndAddr: 0x1030},
			0x1040: {StartAddr: 0x1040, EndAddr: 0x1050},
		},
		BlockOrder: []uint64{0x1000, 0x1020, 0x1040},
		JumpTables: map[uint64][]uint64{
			0x1006: {0x1020, 0x1040},
		},
	}

	inst := disasm.Instruction{
		Address: 0x1006,
		Inst: x86asm.Inst{
			Op:   x86asm.JMP,
			Args: x86asm.Args{x86asm.RAX},
			Len:  2,
		},
	}

	l := lifter.NewLifter(nil)
	lines, err := l.LiftInstruction(inst, 0x1008, fn)
	if err != nil {
		t.Fatalf("LiftInstruction failed: %v", err)
	}

	code := strings.Join(lines, "\n")
	if !strings.Contains(code, "switch") {
		t.Fatalf("expected switch statement in generated code, got:\n%s", code)
	}
	if !strings.Contains(code, "case 0x1020ULL: goto loc_0x1020;") {
		t.Errorf("missing case 0x1020 in code:\n%s", code)
	}
	if !strings.Contains(code, "case 0x1040ULL: goto loc_0x1040;") {
		t.Errorf("missing case 0x1040 in code:\n%s", code)
	}
	if !strings.Contains(code, "recomp_dispatch") {
		t.Errorf("missing fallback recomp_dispatch in default case:\n%s", code)
	}
}

func TestLiftIndirectJmpFallbackWithinFunctionExtent(t *testing.T) {
	fn := &disasm.Function{
		Name:      "fn_test_fallback",
		EntryAddr: 0x2000,
		EndAddr:   0x2200,
		Blocks: map[uint64]*disasm.BasicBlock{
			0x2000: {StartAddr: 0x2000, EndAddr: 0x2010},
			0x2050: {StartAddr: 0x2050, EndAddr: 0x2060},
		},
		BlockOrder: []uint64{0x2000, 0x2050},
		JumpTables: make(map[uint64][]uint64),
	}

	inst := disasm.Instruction{
		Address: 0x2008,
		Inst: x86asm.Inst{
			Op:   x86asm.JMP,
			Args: x86asm.Args{x86asm.RDX},
			Len:  2,
		},
	}

	l := lifter.NewLifter(nil)
	lines, err := l.LiftInstruction(inst, 0x200a, fn)
	if err != nil {
		t.Fatalf("LiftInstruction failed: %v", err)
	}

	code := strings.Join(lines, "\n")
	if !strings.Contains(code, ">= 0x2000ULL") || !strings.Contains(code, "< 0x2200ULL") {
		t.Fatalf("expected function extent bounds guard in code, got:\n%s", code)
	}
	if !strings.Contains(code, "case 0x2050ULL: goto loc_0x2050;") {
		t.Errorf("missing fallback case 0x2050 in code:\n%s", code)
	}
	if !strings.Contains(code, "recomp_dispatch") {
		t.Errorf("missing fallback recomp_dispatch:\n%s", code)
	}
}
