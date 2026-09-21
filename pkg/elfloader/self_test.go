package elfloader_test

import (
	"os"
	"testing"

	"ps4-recomp/pkg/elfloader"
)

func TestLoadPlainELF(t *testing.T) {
	loaded, err := elfloader.LoadELF("../../hello_world.elf")
	if err != nil {
		t.Fatalf("LoadELF: %v", err)
	}
	if loaded.EntryPoint == 0 {
		t.Fatal("missing entry point")
	}
	if len(loaded.ExecRanges) == 0 {
		t.Fatal("expected executable ranges")
	}
	if !loaded.InExecutable(loaded.EntryPoint) {
		t.Fatalf("entry 0x%x is not in executable range", loaded.EntryPoint)
	}
}

func TestLoadOpenOrbisEbootSELF(t *testing.T) {
	path := "../../tools/OpenOrbis/PS4Toolchain/samples/using_library/eboot.bin"
	if _, err := os.Stat(path); err != nil {
		t.Skip("using_library eboot.bin not present")
	}
	loaded, err := elfloader.LoadELF(path)
	if err != nil {
		t.Fatalf("LoadELF eboot.bin: %v", err)
	}
	if loaded.EntryPoint != 0x3320 {
		t.Fatalf("entry point 0x%x, want 0x3320", loaded.EntryPoint)
	}
	if len(loaded.ExecRanges) == 0 {
		t.Fatal("SELF eboot has no executable ranges")
	}
	if !loaded.InExecutable(loaded.EntryPoint) {
		t.Fatalf("entry 0x%x not executable", loaded.EntryPoint)
	}
}

func TestExtractELFFromPlainBytes(t *testing.T) {
	data, err := os.ReadFile("../../hello_world.elf")
	if err != nil {
		t.Fatal(err)
	}
	out, err := elfloader.ExtractELFFromSELF(data)
	if err != nil {
		t.Fatal(err)
	}
	if len(out) != len(data) {
		t.Fatalf("plain ELF should pass through unchanged")
	}
}

func TestTitleIDNotInLoader(t *testing.T) {
	if elfloader.IsSELF([]byte{0x7f, 'E', 'L', 'F'}) {
		t.Fatal("ELF magic must not be classified as SELF")
	}
}
