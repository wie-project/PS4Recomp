package elfloader_test

import (
	"encoding/binary"
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

// A SELF's program headers without their own entry (PT_SCE_PROCPARAM among them) lie inside a
// PT_LOAD that is copied from its entry; they must not be refilled from SELF-layout bytes. The
// process parameters of the eboot in PS4RECOMP_EBOOT must therefore read back intact: size,
// then the "ORBI" magic.
func TestSELFProcParamIntactFromEnv(t *testing.T) {
	path := os.Getenv("PS4RECOMP_EBOOT")
	if path == "" {
		t.Skip("PS4RECOMP_EBOOT not set")
	}
	loaded, err := elfloader.LoadELF(path)
	if err != nil {
		t.Fatal(err)
	}
	a := loaded.ProcParamAddr
	if a == 0 || loaded.ProcParamSize < 16 || a+16 > uint64(len(loaded.MemoryImage)) {
		t.Fatalf("PT_SCE_PROCPARAM at %#x size %#x (image %#x)", a, loaded.ProcParamSize, len(loaded.MemoryImage))
	}
	m := loaded.MemoryImage[a:]
	size := binary.LittleEndian.Uint64(m)
	if string(m[8:12]) != "ORBI" || size < 16 || size > loaded.ProcParamSize {
		t.Fatalf("process parameters at %#x: size %#x magic %q, want size <= %#x and \"ORBI\"", a, size, m[8:12], loaded.ProcParamSize)
	}
	t.Logf("process parameters at %#x: size %#x, SDK version %#x", a, size, binary.LittleEndian.Uint32(m[16:]))
}
