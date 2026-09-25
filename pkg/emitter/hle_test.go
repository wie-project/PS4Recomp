package emitter

import (
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"

	"golang.org/x/arch/x86/x86asm"
)

func TestImplementedSyscallsMatchC(t *testing.T) {
	path := "../runtime/kernel/syscalls/ps4_syscalls.c"
	data, err := os.ReadFile(path)
	if err != nil {
		path = "../runtime/ps4_syscalls.c"
		data, err = os.ReadFile(path)
	}
	if err != nil {
		t.Fatal(err)
	}
	src := string(data)
	start := strings.Index(src, "void shim_syscall(")
	if start < 0 {
		t.Fatal("shim_syscall not found")
	}
	rest := src[start:]
	end := strings.Index(rest, "\nvoid ")
	if end > 0 {
		rest = rest[:end]
	}
	re := regexp.MustCompile(`(?m)^\s*case\s+(\d+)\s*:`)
	got := make(map[int]struct{})
	for _, m := range re.FindAllStringSubmatch(rest, -1) {
		n, _ := strconv.Atoi(m[1])
		got[n] = struct{}{}
	}
	if len(got) == 0 {
		t.Fatal("no case labels in shim_syscall")
	}
	for n := range got {
		if _, ok := ImplementedSyscalls[n]; !ok {
			t.Errorf("C implements syscall %d, missing from ImplementedSyscalls", n)
		}
	}
	for n := range ImplementedSyscalls {
		if _, ok := got[n]; !ok {
			t.Errorf("ImplementedSyscalls has %d, missing from shim_syscall", n)
		}
	}
}

func TestImmediateSyscallNumber(t *testing.T) {
	insts := []disasm.Instruction{
		{Address: 0x1000, Inst: x86asm.Inst{Op: x86asm.MOV, Args: x86asm.Args{x86asm.EDI, x86asm.Imm(477)}}},
		{Address: 0x1005, Inst: x86asm.Inst{Op: x86asm.CALL, Args: x86asm.Args{x86asm.Rel(0)}}},
	}
	n, ok := immediateInFamily(insts, 1, x86asm.RDI)
	if !ok || n != 477 {
		t.Fatalf("immediateInFamily = %d, %v; want 477, true", n, ok)
	}
}

func TestCollectGraphicsHostShims(t *testing.T) {
	path := "../../tools/OpenOrbis/PS4Toolchain/samples/graphics/graphics/x64/Debug/graphics.elf"
	if _, err := os.Stat(path); err != nil {
		t.Skip(path)
	}
	loaded, err := elfloader.LoadELF(path)
	if err != nil {
		t.Fatal(err)
	}
	r := NewHLEReport(loaded, nil)
	if r.HostShimCount == 0 {
		t.Fatalf("graphics.elf: expected host-shim imports, got %+v unique", len(r.Imports))
	}
	foundVideo := false
	for _, imp := range r.Imports {
		if imp.Class == ImportHostShim && strings.Contains(strings.ToLower(imp.Name+imp.Shim), "videoout") {
			foundVideo = true
			break
		}
		if imp.Class == ImportHostShim && imp.Shim == "shim_sceVideoOutOpen" {
			foundVideo = true
			break
		}
	}
	if !foundVideo {
		// NID-encoded names still bind via LookupShim; accept any videoout shim.
		for _, imp := range r.Imports {
			if strings.HasPrefix(imp.Shim, "shim_sceVideoOut") {
				foundVideo = true
				break
			}
		}
	}
	if !foundVideo {
		t.Fatalf("graphics.elf: no VideoOut host shim among %d imports", len(r.Imports))
	}
}

func TestNetworkingResolvedHostShims(t *testing.T) {
	path := "../../tools/OpenOrbis/PS4Toolchain/samples/networking/networking/x64/Debug/networking.elf"
	if _, err := os.Stat(path); err != nil {
		t.Skip(path)
	}
	loaded, err := elfloader.LoadELF(path)
	if err != nil {
		t.Fatal(err)
	}
	r := NewHLEReport(loaded, nil)
	want := map[string]bool{"accept": false, "bind": false, "listen": false}
	for _, imp := range r.Imports {
		if imp.Class != ImportHostShim {
			continue
		}
		if _, ok := want[imp.Name]; ok {
			want[imp.Name] = true
		}
	}
	for name, found := range want {
		if !found {
			t.Errorf("networking.elf: expected host shim for %s", name)
		}
	}
}

func TestHogwartsUnresolvedLibraryNames(t *testing.T) {
	path := "/Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition/extracted/eboot.bin"
	loaded, err := elfloader.LoadELF(path)
	if err != nil {
		t.Skip(err)
	}
	r := NewHLEReport(loaded, nil)
	if r.UnresolvedCount == 0 {
		t.Fatal("expected unresolved Hogwarts imports")
	}
	sce := 0
	for _, imp := range r.Imports {
		if imp.Class == ImportUnresolved && strings.HasPrefix(imp.Library, "libSce") {
			sce++
		}
	}
	if sce == 0 {
		t.Fatalf("unresolved imports still grouped by encoded ids, sample=%q", r.Imports[0].Library)
	}
	t.Logf("unresolved=%d libSce-grouped=%d", r.UnresolvedCount, sce)
}

func TestHLEReportWrite(t *testing.T) {
	path := "../../tools/OpenOrbis/PS4Toolchain/samples/hello_world/hello_world/x64/Debug/hello_world.elf"
	if _, err := os.Stat(path); err != nil {
		t.Skip(path)
	}
	loaded, err := elfloader.LoadELF(path)
	if err != nil {
		t.Fatal(err)
	}
	r := NewHLEReport(loaded, nil)
	dir := t.TempDir()
	out := filepath.Join(dir, "hle_report.txt")
	if err := r.WriteFile(out); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(out)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(data), "PS4Recomp HLE report") {
		t.Fatal("report missing header")
	}
}
