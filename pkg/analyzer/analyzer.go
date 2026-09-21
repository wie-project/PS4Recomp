package analyzer

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"ps4-recomp/pkg/elfloader"
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
	BinaryPath            string
	BinarySize            int64
	TotalInstructions     int
	SupportedCount        int
	UnsupportedCount      int
	CoveragePercent       float64
	UniqueOpcodes         int
	SupportedUniqueOps    int
	MissingUniqueOps      int
	MissingOpcodes        []OpcodeFrequency
	TopSupportedOpcodes   []OpcodeFrequency
}

// AnalyzeBinary scans all executable machine code in an ELF or FSELF binary
// and checks each instruction against the recompilation lifter.
func AnalyzeBinary(path string) (*AnalysisReport, error) {
	fi, err := os.Stat(path)
	if err != nil {
		return nil, fmt.Errorf("failed to stat binary: %w", err)
	}

	loaded, err := elfloader.LoadELF(path)
	if err != nil {
		return nil, fmt.Errorf("failed to load ELF: %w", err)
	}

	opCounts := make(map[x86asm.Op]int)
	missingCounts := make(map[x86asm.Op]int)

	totalInsts := 0
	supportedInsts := 0
	unsupportedInsts := 0

	// Scan through all executable segments/sections
	for _, seg := range loaded.Segments {
		// Only scan executable segments (PF_X = 1)
		if (seg.Flags & 1) == 0 {
			continue
		}

		data := seg.Data
		if len(data) > int(seg.Filesz) {
			data = data[:seg.Filesz]
		}

		offset := 0
		for offset < len(data) {
			inst, err := x86asm.Decode(data[offset:], 64)
			if err != nil || inst.Len == 0 {
				offset++
				continue
			}

			op := inst.Op
			opCounts[op]++
			totalInsts++

			if lifter.IsOpcodeSupported(op) {
				supportedInsts++
			} else {
				unsupportedInsts++
				missingCounts[op]++
			}

			offset += inst.Len
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

	report := &AnalysisReport{
		BinaryPath:          path,
		BinarySize:          fi.Size(),
		TotalInstructions:   totalInsts,
		SupportedCount:      supportedInsts,
		UnsupportedCount:    unsupportedInsts,
		CoveragePercent:     coverage,
		UniqueOpcodes:       len(opCounts),
		SupportedUniqueOps:  len(supportedFreqs),
		MissingUniqueOps:    len(missingFreqs),
		MissingOpcodes:      missingFreqs,
		TopSupportedOpcodes: supportedFreqs,
	}

	return report, nil
}

// SummaryString formats the analysis report into a human-readable text document.
func (r *AnalysisReport) SummaryString() string {
	var sb strings.Builder
	sb.WriteString("===================================================================\n")
	sb.WriteString("  PS4 Binary Machine Code Instruction Coverage Analysis\n")
	sb.WriteString("===================================================================\n")
	sb.WriteString(fmt.Sprintf("Binary File:            %s\n", filepath.Base(r.BinaryPath)))
	sb.WriteString(fmt.Sprintf("Binary Size:            %.2f MB (%d bytes)\n", float64(r.BinarySize)/(1024*1024), r.BinarySize))
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
		for i, m := range r.MissingOpcodes {
			pct := 0.0
			if r.TotalInstructions > 0 {
				pct = (float64(m.Count) / float64(r.TotalInstructions)) * 100.0
			}
			sb.WriteString(fmt.Sprintf("  [%-2d]  %-22s  %-12d  (%.3f%%)\n", i+1, m.Name, m.Count, pct))
			if i >= 30 {
				sb.WriteString(fmt.Sprintf("  ... and %d more missing opcodes\n", len(r.MissingOpcodes)-31))
				break
			}
		}
	} else {
		sb.WriteString("\n✓ All instructions in this binary are 100.0% supported by the lifter!\n")
	}

	return sb.String()
}
