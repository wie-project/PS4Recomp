package analyzer

import (
	"os"
	"testing"
)

func TestAnalyzeBinary(t *testing.T) {
	elfPath := "../../tools/OpenOrbis/PS4Toolchain/samples/graphics/graphics/x64/Debug/graphics.elf"
	if _, err := os.Stat(elfPath); err != nil {
		t.Skipf("Sample ELF %s not found", elfPath)
	}

	report, err := AnalyzeBinary(elfPath)
	if err != nil {
		t.Fatalf("AnalyzeBinary failed: %v", err)
	}

	if report.TotalInstructions == 0 {
		t.Errorf("Expected instructions to be found, got 0")
	}

	if report.CoveragePercent < 90.0 {
		t.Errorf("Expected high instruction coverage, got %.2f%%", report.CoveragePercent)
	}

	t.Logf("\n%s", report.SummaryString())
}
