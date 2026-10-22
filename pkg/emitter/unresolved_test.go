package emitter

import (
	"debug/elf"
	"encoding/binary"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"ps4-recomp/pkg/elfloader"
)

// Guest addresses of the fake imports of unresolvedTestELF.
const (
	unresPlt     = 0x540000 // PLT stub of the unresolved import
	unresGot     = 0x560000 // its GOT slot
	unresGlobDat = 0x560010 // unresolved GLOB_DAT slot (no PLT)
	shimmedPlt   = 0x540010
	shimmedGot   = 0x560020
	linkedPlt    = 0x540020
	linkedGot    = 0x560030
	linkedTarget = 0x300000 // export of a companion module the linked import resolves to
	namedPlt     = 0x540030 // unresolved import whose NID the NID database names
	namedGot     = 0x560040
	ebootMin     = 0x400000
	ebootMax     = 0x600000
)

// unresolvedTestELF builds the image that elfloader.ResolveModuleRelocations leaves for three
// imports: one nothing provides (its GOT slot points back at the PLT stub), one a host shim
// serves, and one a companion module exports (its GOT slot holds the export address).
func unresolvedTestELF() *elfloader.LoadedELF {
	img := make([]byte, 0x600000)
	put := func(addr, v uint64) { binary.LittleEndian.PutUint64(img[addr:], v) }
	put(unresGot, unresPlt)
	put(unresGlobDat, unresGlobDat)
	put(shimmedGot, shimmedPlt)
	put(linkedGot, linkedTarget)
	put(namedGot, namedPlt)
	return &elfloader.LoadedELF{
		FileName:    "eboot.bin",
		MemoryImage: img,
		MaxVAddr:    0x600000,
		// The eboot's own segments (ELF program header flags) and one a merged companion module
		// (libSceFios2) appended after OrigMaxVAddr, which module info must not report.
		OrigMinVAddr: ebootMin,
		OrigMaxVAddr: ebootMax,
		Segments: []*elfloader.Segment{
			{Vaddr: ebootMin, Memsz: 0x180000, Flags: elf.PF_R | elf.PF_X},
			{Vaddr: 0x580000, Memsz: 0x40000, Flags: elf.PF_R},
			{Vaddr: 0x5c0000, Memsz: 0x40000, Flags: elf.PF_R | elf.PF_W},
			{Vaddr: 0x800000, Memsz: 0x10000, Flags: elf.PF_R | elf.PF_X},
		},
		ImportLibs: map[uint16]string{0: "libSceFoo"},
		Relocations: []elfloader.Relocation{
			{Type: elfloader.R_X86_64_JUMP_SLOT, Offset: unresGot, PltAddr: unresPlt, SymName: "AbCdEfGhIjK#A#A"},
			{Type: elfloader.R_X86_64_GLOB_DAT, Offset: unresGlobDat, SymName: "LmNoPqRsTuV#A#A"},
			{Type: elfloader.R_X86_64_JUMP_SLOT, Offset: shimmedGot, PltAddr: shimmedPlt, SymName: "memcpy"},
			{Type: elfloader.R_X86_64_JUMP_SLOT, Offset: namedGot, PltAddr: namedPlt, SymName: "kgOk-kzW+8M#A#A"}, // sceRazorCpuInit
			{Type: elfloader.R_X86_64_JUMP_SLOT, Offset: linkedGot, PltAddr: linkedPlt, SymName: "WxYzAbCdEfG#A#A"},
		},
	}
}

// An import that neither a host shim nor a module satisfies must be bound to
// shim_unresolved_stub (PLT and GOT) and registered with its NID and library, so the guest
// reports it by name instead of jumping to its own PLT stub forever.
func TestUnresolvedImportsAreBound(t *testing.T) {
	loaded := unresolvedTestELF()
	em := NewAsmEmitter(loaded, nil, nil)

	for _, addr := range []uint64{unresPlt, unresGot, unresGlobDat} {
		if got := em.shimMap[addr]; got != "shim_unresolved_stub" {
			t.Errorf("0x%x bound to %q, want shim_unresolved_stub", addr, got)
		}
	}
	if got := em.shimMap[shimmedPlt]; got != "shim_memcpy" {
		t.Errorf("shimmed PLT bound to %q, want shim_memcpy", got)
	}
	for _, addr := range []uint64{linkedPlt, linkedGot} {
		if got, ok := em.shimMap[addr]; ok {
			t.Errorf("module-resolved import 0x%x bound to %q, want it left alone", addr, got)
		}
	}

	if em.UnresolvedFuncImports != 2 || em.UnresolvedDataImports != 1 {
		t.Errorf("unresolved counts: %d calls, %d data, want 2 and 1", em.UnresolvedFuncImports, em.UnresolvedDataImports)
	}
	if got := strings.Join(em.UnresolvedDataNames, "|"); got != "LmNoPqRsTuV (libSceFoo)" {
		t.Errorf("unresolved data import names %q, want LmNoPqRsTuV (libSceFoo)", got)
	}

	path := filepath.Join(t.TempDir(), "guest_modules.c")
	if err := em.EmitGuestModules(path); err != nil {
		t.Fatal(err)
	}
	src, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	code := string(src)
	for _, want := range []string{
		`recomp_register_unresolved(0x540000ULL, "AbCdEfGhIjK", "", "libSceFoo");`,
		`recomp_register_unresolved(0x560000ULL, "AbCdEfGhIjK", "", "libSceFoo");`,
		`recomp_register_unresolved(0x560010ULL, "LmNoPqRsTuV", "", "libSceFoo");`,
		`recomp_register_unresolved(0x540030ULL, "kgOk-kzW+8M", "sceRazorCpuInit", "libSceFoo");`,
		`recomp_register_unresolved(0x560040ULL, "kgOk-kzW+8M", "sceRazorCpuInit", "libSceFoo");`,
		"recomp_register_fn(0x540000ULL, fn_0x540000);",
		"recomp_register_fn(0x560000ULL, fn_0x560000);",
	} {
		if !strings.Contains(code, want) {
			t.Errorf("guest_modules.c lacks %q", want)
		}
	}
	for _, bad := range []string{"0x540010ULL, \"", "0x540020ULL, \"", "0x560030ULL, \""} {
		if strings.Contains(code, "recomp_register_unresolved("+bad) {
			t.Errorf("guest_modules.c registers a resolved import: %q", bad)
		}
	}

	synth, err := os.ReadFile(strings.TrimSuffix(path, ".c") + "_synth.s")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(synth), "_fn_0x560000:\n    adrp x0, _shim_unresolved_stub@PAGE") {
		t.Errorf("no stub for the unresolved GOT slot in the synth file:\n%s", synth)
	}
}
