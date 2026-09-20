package lifter_test

import (
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/lifter"

	"golang.org/x/arch/x86/x86asm"
)

// TestGlobalX86OpcodeCoverage measures the global coverage of x86-64 instructions
// supported by pkg/lifter across all opcodes defined in the x86 instruction set.
func TestGlobalX86OpcodeCoverage(t *testing.T) {
	// Discover all valid opcodes defined in x86asm
	var allOps []x86asm.Op
	var supportedOps []x86asm.Op
	var unsupportedOps []x86asm.Op

	for i := 1; i < 5000; i++ {
		op := x86asm.Op(i)
		name := op.String()
		if strings.HasPrefix(name, "Op(") || name == "" {
			continue
		}
		allOps = append(allOps, op)
		if lifter.IsOpcodeSupported(op) {
			supportedOps = append(supportedOps, op)
		} else {
			unsupportedOps = append(unsupportedOps, op)
		}
	}

	total := len(allOps)
	suppCount := len(supportedOps)
	coveragePct := float64(suppCount) / float64(total) * 100.0

	t.Logf("=== Global x86-64 Instruction Set Coverage ===")
	t.Logf("Total defined x86 opcodes: %d", total)
	t.Logf("Supported by lifter:       %d (%.2f%%)", suppCount, coveragePct)
	t.Logf("Unsupported opcodes:      %d (%.2f%%)", len(unsupportedOps), 100.0-coveragePct)

	// Categorize critical instruction families
	categories := map[string][]x86asm.Op{
		"Core ALU & Logic": {
			x86asm.ADD, x86asm.ADC, x86asm.SUB, x86asm.SBB, x86asm.AND, x86asm.OR,
			x86asm.XOR, x86asm.CMP, x86asm.TEST, x86asm.INC, x86asm.DEC, x86asm.NEG,
			x86asm.NOT, x86asm.ROL, x86asm.ROR, x86asm.SHL, x86asm.SHR, x86asm.SAR,
		},
		"Control Flow": {
			x86asm.JMP, x86asm.CALL, x86asm.RET, x86asm.JE, x86asm.JNE, x86asm.JA,
			x86asm.JAE, x86asm.JB, x86asm.JBE, x86asm.JG, x86asm.JGE, x86asm.JL,
			x86asm.JLE, x86asm.JS, x86asm.JNS, x86asm.JO, x86asm.JNO, x86asm.JP,
			x86asm.JNP,
		},
		"Data Movement & Stack": {
			x86asm.MOV, x86asm.MOVZX, x86asm.MOVSX, x86asm.MOVSXD, x86asm.LEA,
			x86asm.PUSH, x86asm.POP, x86asm.XCHG, x86asm.XADD, x86asm.CMPXCHG,
		},
		"Condition & Flag Setting": {
			x86asm.SETE, x86asm.SETNE, x86asm.SETA, x86asm.SETAE, x86asm.SETB,
			x86asm.SETBE, x86asm.SETG, x86asm.SETGE, x86asm.SETL, x86asm.SETLE,
			x86asm.CMOVE, x86asm.CMOVNE, x86asm.CMOVA, x86asm.CMOVAE, x86asm.CMOVB,
			x86asm.CMOVBE, x86asm.CMOVG, x86asm.CMOVGE, x86asm.CMOVL, x86asm.CMOVLE,
		},
		"Multiply & Divide": {
			x86asm.MUL, x86asm.IMUL, x86asm.DIV, x86asm.IDIV,
			x86asm.CDQ, x86asm.CQO, x86asm.CWD, x86asm.CBW,
		},
		"String & Block Operations": {
			x86asm.MOVSB, x86asm.MOVSW, x86asm.MOVSD, x86asm.MOVSQ,
			x86asm.STOSB, x86asm.STOSW, x86asm.STOSD, x86asm.STOSQ,
			x86asm.CMPSB, x86asm.LODSB, x86asm.SCASB,
		},
		"Bit Manipulation": {
			x86asm.BSF, x86asm.BSR, x86asm.BT, x86asm.BTS, x86asm.BTR, x86asm.BTC,
			x86asm.POPCNT, x86asm.TZCNT, x86asm.LZCNT,
		},
		"Scalar SSE Math": {
			x86asm.MOVSS, x86asm.MOVSD_XMM, x86asm.ADDSS, x86asm.ADDSD,
			x86asm.SUBSS, x86asm.SUBSD, x86asm.MULSS, x86asm.MULSD,
			x86asm.DIVSS, x86asm.DIVSD, x86asm.UCOMISS, x86asm.UCOMISD,
		},
		"x87 FPU": {
			x86asm.FLD, x86asm.FLDZ, x86asm.FILD, x86asm.FST, x86asm.FSTP,
			x86asm.FIST, x86asm.FISTP, x86asm.FADD, x86asm.FADDP, x86asm.FSUB,
			x86asm.FSUBP, x86asm.FMUL, x86asm.FMULP, x86asm.FDIV, x86asm.FDIVP,
			x86asm.FUCOMI, x86asm.FLDCW, x86asm.FNSTCW,
		},
	}

	type catStat struct {
		name      string
		total     int
		supported int
		pct       float64
		missing   []string
	}

	var stats []catStat
	for name, opList := range categories {
		supp := 0
		var missing []string
		for _, op := range opList {
			if lifter.IsOpcodeSupported(op) {
				supp++
			} else {
				missing = append(missing, op.String())
			}
		}
		stats = append(stats, catStat{
			name:      name,
			total:     len(opList),
			supported: supp,
			pct:       float64(supp) / float64(len(opList)) * 100.0,
			missing:   missing,
		})
	}

	sort.Slice(stats, func(i, j int) bool {
		return stats[i].name < stats[j].name
	})

	t.Logf("--- Core Architecture Category Breakdown ---")
	for _, cs := range stats {
		t.Logf("%-26s %2d / %2d (%5.1f%%)", cs.name+":", cs.supported, cs.total, cs.pct)
		if len(cs.missing) > 0 {
			t.Logf("  Missing: %s", strings.Join(cs.missing, ", "))
		}
	}
}

// TestBinaryCoverage specifically measures instruction coverage within hello_world.elf.
func TestBinaryCoverage(t *testing.T) {
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

	supported := 0
	unsupportedOp := make(map[x86asm.Op]int)

	for _, fn := range d.Functions {
		for _, b := range fn.Blocks {
			for _, inst := range b.Insts {
				if lifter.IsOpcodeSupported(inst.Inst.Op) {
					supported++
				} else {
					unsupportedOp[inst.Inst.Op]++
				}
			}
		}
	}

	total := supported
	for _, count := range unsupportedOp {
		total += count
	}

	t.Logf("Binary: %s", elfPath)
	t.Logf("Total reachable instructions: %d", total)
	t.Logf("Supported in binary:        %d (%.1f%%)", supported, float64(supported)/float64(total)*100.0)

	if len(unsupportedOp) > 0 {
		t.Errorf("Binary contains %d unsupported instructions out of %d", len(unsupportedOp), total)
	}
}

// TestOpenOrbisSamplesCoverage verifies that all 17 OpenOrbis sample ELFs achieve 100% instruction coverage.
func TestOpenOrbisSamplesCoverage(t *testing.T) {
	samplesRoot := "../../tools/OpenOrbis/PS4Toolchain/samples"
	if _, err := os.Stat(samplesRoot); os.IsNotExist(err) {
		t.Skip("OpenOrbis samples directory not found")
	}

	var elfPaths []string
	_ = filepath.Walk(samplesRoot, func(p string, info os.FileInfo, err error) error {
		if err == nil && !info.IsDir() && strings.HasSuffix(p, ".elf") {
			elfPaths = append(elfPaths, p)
		}
		return nil
	})

	if len(elfPaths) == 0 {
		t.Skip("No OpenOrbis sample ELFs found")
	}

	sort.Strings(elfPaths)

	for _, elfPath := range elfPaths {
		sampleName := filepath.Base(elfPath)
		loaded, err := elfloader.LoadELF(elfPath)
		if err != nil {
			t.Errorf("sample %s: LoadELF failed: %v", sampleName, err)
			continue
		}

		d, err := disasm.NewDisassembler(loaded)
		if err != nil {
			t.Errorf("sample %s: NewDisassembler failed: %v", sampleName, err)
			continue
		}

		entries := []uint64{loaded.EntryPoint}
		entries = append(entries, loaded.InitArray...)
		if mainSym, ok := loaded.SymbolByName["main"]; ok {
			entries = append(entries, mainSym.Address)
		}

		if err := d.AnalyzeReachable(entries); err != nil {
			t.Errorf("sample %s: AnalyzeReachable failed: %v", sampleName, err)
			continue
		}

		unsupported := make(map[x86asm.Op]int)
		total := 0
		for _, fn := range d.Functions {
			for _, b := range fn.Blocks {
				for _, inst := range b.Insts {
					total++
					if !lifter.IsOpcodeSupported(inst.Inst.Op) {
						unsupported[inst.Inst.Op]++
					}
				}
			}
		}

		if len(unsupported) > 0 {
			t.Errorf("Sample %s has %d unsupported opcodes: %v", sampleName, len(unsupported), unsupported)
		} else {
			t.Logf("Sample %-16s: 100.0%% coverage (%d instructions across %d functions)", sampleName, total, len(d.Functions))
		}
	}
}
