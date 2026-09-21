package disasm

import (
	"os"
	"testing"

	"ps4-recomp/pkg/elfloader"
)

func TestSeedEntryPointsHelloWorld(t *testing.T) {
	loaded, err := elfloader.LoadELF("../../hello_world.elf")
	if err != nil {
		t.Fatal(err)
	}
	entries := SeedEntryPoints(loaded, false)
	if len(entries) == 0 {
		t.Fatal("no entry points")
	}
	foundMain := false
	foundEntry := false
	for _, e := range entries {
		if e == loaded.EntryPoint {
			foundEntry = true
		}
		if main, ok := loaded.SymbolByName["main"]; ok && e == main.Address {
			foundMain = true
		}
	}
	if !foundEntry {
		t.Fatal("entry point missing from seeds")
	}
	if !foundMain {
		t.Fatal("main missing from seeds")
	}

	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}
	if err := d.AnalyzeReachable(entries); err != nil {
		t.Fatal(err)
	}
	if len(d.Functions) < 100 {
		t.Fatalf("too few functions: %d", len(d.Functions))
	}
}

func TestNewDisassemblerWithoutTextSection(t *testing.T) {
	path := "../../tools/OpenOrbis/PS4Toolchain/samples/using_library/eboot.bin"
	if _, err := os.Stat(path); err != nil {
		t.Skip("using_library eboot.bin not present")
	}
	loaded, err := elfloader.LoadELF(path)
	if err != nil {
		t.Fatal(err)
	}
	delete(loaded.Sections, ".text")
	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatalf("NewDisassembler on SELF without .text: %v", err)
	}
	entries := SeedEntryPoints(loaded, true)
	if err := d.AnalyzeReachable(entries); err != nil {
		t.Fatal(err)
	}
	if len(d.Functions) == 0 {
		t.Fatal("expected recovered functions from executable segments")
	}
}
