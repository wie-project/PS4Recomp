package analyzer

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/emitter"
	"ps4-recomp/pkg/lifter"

	"golang.org/x/arch/x86/x86asm"
)

// OpcodeFrequency tracks an opcode and its usage count.
type OpcodeFrequency struct {
	Op    x86asm.Op
	Name  string
	Count int
}

// AnalysisReport contains statistics about instruction support for a binary.
type AnalysisReport struct {
	BinaryPath          string
	BinarySize          int64
	SeedCount           int
	FunctionCount       int
	TotalInstructions   int
	SupportedCount      int
	UnsupportedCount    int
	CoveragePercent     float64
	UniqueOpcodes       int
	SupportedUniqueOps  int
	MissingUniqueOps    int
	MissingOpcodes      []OpcodeFrequency
	TopSupportedOpcodes []OpcodeFrequency
	HLE                 *emitter.HLEReport
}

// AnalyzeBinary recovers the reachable CFG (the same seeding the recompiler uses)
// and checks each lifted instruction against the opcode registry.
func AnalyzeBinary(path string) (*AnalysisReport, error) {
	return AnalyzeBinarySeeded(path, false)
}

// AnalyzeBinarySeeded is AnalyzeBinary with an explicit all-symbols seed.
func AnalyzeBinarySeeded(path string, allSymbols bool) (*AnalysisReport, error) {
	fi, err := os.Stat(path)
	if err != nil {
		return nil, fmt.Errorf("failed to stat binary: %w", err)
	}

	loaded, err := elfloader.LoadELF(path)
	if err != nil {
		return nil, fmt.Errorf("failed to load ELF: %w", err)
	}

	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		return nil, fmt.Errorf("failed to initialize disassembler: %w", err)
	}
	d.SkipFlagLiveness = true
	entries := disasm.SeedEntryPoints(loaded, allSymbols)

	opCounts := make(map[x86asm.Op]int)
	missingCounts := make(map[x86asm.Op]int)
	totalInsts := 0
	supportedInsts := 0
	unsupportedInsts := 0
	functionCount := 0

	visited := make(map[uint64]bool, len(entries)*2)
	queue := make([]uint64, 0, len(entries)*2)
	for _, addr := range entries {
		if addr == 0 || !loaded.InExecutable(addr) || visited[addr] {
			continue
		}
		visited[addr] = true
		queue = append(queue, addr)
	}
	seedCount := len(queue)

	hle := emitter.NewHLEReport(loaded, nil)
	syscallShims := emitter.ImportShimMap(loaded, "shim_syscall")

	for head := 0; head < len(queue); head++ {
		if head == 0 || head%10000 == 0 || head == len(queue)-1 {
			fmt.Printf("[*] Progress: %d / %d functions analyzed (Queue capacity: %d)\n",
				head, functionCount, len(queue))
		}
		fn, calls, err := d.DisasmFunction(queue[head])
		if err != nil || fn == nil {
			continue
		}
		functionCount++
		for _, b := range fn.Blocks {
			hle.ObserveBlock(b.Insts, syscallShims)
			for _, inst := range b.Insts {
				op := inst.Inst.Op
				opCounts[op]++
				totalInsts++
				if lifter.IsOpcodeSupported(op) {
					supportedInsts++
				} else {
					unsupportedInsts++
					missingCounts[op]++
				}
			}
		}
		for _, target := range calls {
			if target == 0 || !loaded.InExecutable(target) || visited[target] {
				continue
			}
			visited[target] = true
			queue = append(queue, target)
		}
	}

	coverage := 0.0
	if totalInsts > 0 {
		coverage = (float64(supportedInsts) / float64(totalInsts)) * 100.0
	}

	var missingFreqs []OpcodeFrequency
	for op, count := range missingCounts {
		missingFreqs = append(missingFreqs, OpcodeFrequency{
			Op:    op,
			Name:  op.String(),
			Count: count,
		})
	}
	sort.Slice(missingFreqs, func(i, j int) bool {
		if missingFreqs[i].Count == missingFreqs[j].Count {
			return missingFreqs[i].Name < missingFreqs[j].Name
		}
		return missingFreqs[i].Count > missingFreqs[j].Count
	})

	var supportedFreqs []OpcodeFrequency
	for op, count := range opCounts {
		if lifter.IsOpcodeSupported(op) {
			supportedFreqs = append(supportedFreqs, OpcodeFrequency{
				Op:    op,
				Name:  op.String(),
				Count: count,
			})
		}
	}
	sort.Slice(supportedFreqs, func(i, j int) bool {
		return supportedFreqs[i].Count > supportedFreqs[j].Count
	})

	return &AnalysisReport{
		BinaryPath:          path,
		BinarySize:          fi.Size(),
		SeedCount:           seedCount,
		FunctionCount:       functionCount,
		TotalInstructions:   totalInsts,
		SupportedCount:      supportedInsts,
		UnsupportedCount:    unsupportedInsts,
		CoveragePercent:     coverage,
		UniqueOpcodes:       len(opCounts),
		SupportedUniqueOps:  len(supportedFreqs),
		MissingUniqueOps:    len(missingFreqs),
		MissingOpcodes:      missingFreqs,
		TopSupportedOpcodes: supportedFreqs,
		HLE:                 hle,
	}, nil
}

// SummaryString formats the analysis report into a human-readable text document.
func (r *AnalysisReport) SummaryString() string {
	var sb strings.Builder
	sb.WriteString("===================================================================\n")
	sb.WriteString("  PS4 Binary Reachable CFG Instruction Coverage Analysis\n")
	sb.WriteString("===================================================================\n")
	sb.WriteString(fmt.Sprintf("Binary File:            %s\n", filepath.Base(r.BinaryPath)))
	sb.WriteString(fmt.Sprintf("Binary Size:            %.2f MB (%d bytes)\n", float64(r.BinarySize)/(1024*1024), r.BinarySize))
	sb.WriteString(fmt.Sprintf("CFG Seeds:              %d\n", r.SeedCount))
	sb.WriteString(fmt.Sprintf("Reachable Functions:    %d\n", r.FunctionCount))
	sb.WriteString(fmt.Sprintf("Total Disassembled:     %d instructions\n", r.TotalInstructions))
	sb.WriteString(fmt.Sprintf("Unique Opcodes:         %d\n", r.UniqueOpcodes))
	sb.WriteString(fmt.Sprintf("Supported by Lifter:    %d instructions (%.2f%%)\n", r.SupportedCount, r.CoveragePercent))
	sb.WriteString(fmt.Sprintf("Missing / Unsupported:  %d instructions (%.2f%%)\n", r.UnsupportedCount, 100.0-r.CoveragePercent))
	sb.WriteString(fmt.Sprintf("Missing Unique Opcodes: %d\n", r.MissingUniqueOps))

	if len(r.MissingOpcodes) > 0 {
		sb.WriteString("\n-------------------------------------------------------------------\n")
		sb.WriteString("  Missing Instructions Requiring Lifter Implementation\n")
		sb.WriteString("-------------------------------------------------------------------\n")
		sb.WriteString("  Rank  Opcode                  Occurrences   Percentage\n")
		sb.WriteString("  -----------------------------------------------------------------\n")
		limit := 50
		for i, m := range r.MissingOpcodes {
			pct := 0.0
			if r.TotalInstructions > 0 {
				pct = (float64(m.Count) / float64(r.TotalInstructions)) * 100.0
			}
			sb.WriteString(fmt.Sprintf("  [%-2d]  %-22s  %-12d  (%.3f%%)\n", i+1, m.Name, m.Count, pct))
			if i+1 >= limit {
				rest := len(r.MissingOpcodes) - limit
				if rest > 0 {
					sb.WriteString(fmt.Sprintf("  ... and %d more missing opcodes\n", rest))
				}
				break
			}
		}
	} else {
		sb.WriteString("\nAll reachable instructions in this binary are supported by the lifter.\n")
	}

	if r.HLE != nil {
		sb.WriteString("\n-------------------------------------------------------------------\n")
		sb.WriteString("  Host shims, imported symbols, and recovered syscalls\n")
		sb.WriteString("-------------------------------------------------------------------\n")
		sb.WriteString(r.HLE.ImportSummary())
		sb.WriteString(r.HLE.SyscallSummary())
	}

	return sb.String()
}
