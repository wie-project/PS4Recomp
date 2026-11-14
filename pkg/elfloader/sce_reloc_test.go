package elfloader

import (
	"debug/elf"
	"encoding/binary"
	"os"
	"testing"
)

func TestPRXJumpSlotMatchesELF(t *testing.T) {
	prxPath := "../../tools/OpenOrbis/PS4Toolchain/samples/using_library/sce_module/libExample.prx"
	elfPath := "../../tools/OpenOrbis/PS4Toolchain/samples/library_example/library_example/x64/Debug/library_example.elf"
	if _, err := os.Stat(prxPath); err != nil {
		t.Skip("libExample.prx not present")
	}
	if _, err := os.Stat(elfPath); err != nil {
		t.Skip("library_example.elf not present")
	}

	prx, err := LoadELF(prxPath)
	if err != nil {
		t.Fatal(err)
	}
	plain, err := LoadELF(elfPath)
	if err != nil {
		t.Fatal(err)
	}

	elfByOff := map[uint64]string{}
	for _, rel := range plain.Relocations {
		if rel.Type == R_X86_64_JUMP_SLOT {
			elfByOff[rel.Offset] = rel.SymName
		}
	}
	prxByOff := map[uint64]string{}
	for _, rel := range prx.Relocations {
		if rel.Type == R_X86_64_JUMP_SLOT {
			prxByOff[rel.Offset] = NIDPrefix(rel.SymName)
		}
	}

	lockOff := uint64(0x1100b0)
	if elfByOff[lockOff] != "pthread_mutex_lock" {
		t.Fatalf("ELF GOT 0x%x = %q", lockOff, elfByOff[lockOff])
	}
	wantNID := CalculateNID("pthread_mutex_lock")
	if prxByOff[lockOff] != wantNID {
		t.Fatalf("PRX GOT 0x%x = %q want mutex_lock NID %s", lockOff, prxByOff[lockOff], wantNID)
	}
}

func TestResolveModuleRelocations(t *testing.T) {
	main := &LoadedELF{
		MemoryImage: make([]byte, 0x2000),
		Relocations: []Relocation{
			{Offset: 0x1000, Type: R_X86_64_JUMP_SLOT, SymName: "sample_fn"},
			{Offset: 0x1010, Type: R_X86_64_GLOB_DAT, SymName: "unresolved_fn"},
		},
	}
	exports := []Symbol{
		{Name: "sample_fn", Address: 0x1500},
	}
	if err := ResolveModuleRelocations(main, exports); err != nil {
		t.Fatalf("ResolveModuleRelocations failed: %v", err)
	}

	gotAddr := uint64(main.MemoryImage[0x1000]) | (uint64(main.MemoryImage[0x1001]) << 8) | (uint64(main.MemoryImage[0x1002]) << 16)
	if gotAddr != 0x1500 {
		t.Fatalf("expected resolved address 0x1500 at 0x1000, got 0x%x", gotAddr)
	}

	gotUnresolved := uint64(main.MemoryImage[0x1010]) | (uint64(main.MemoryImage[0x1011]) << 8) | (uint64(main.MemoryImage[0x1012]) << 16)
	if gotUnresolved != 0x1010 {
		t.Fatalf("expected fallback target 0x1010 at 0x1010, got 0x%x", gotUnresolved)
	}
}

// A PRX references its optional module_start/module_stop as weak undefined plaintext symbols.
// With no definition they resolve to 0, while a weak NID import keeps its self-bound slot so the
// emitter can bind a host shim to it or report it as unresolved.
func TestResolveWeakUndefinedPlaintextToZero(t *testing.T) {
	l := &LoadedELF{
		MemoryImage: make([]byte, 0x2000),
		DynSymbols: []Symbol{
			{},
			{Name: "module_start", Bind: elf.STB_WEAK, Section: elf.SHN_UNDEF},
			{Name: "DFmMT80xcNI#A#B", Bind: elf.STB_WEAK, Section: elf.SHN_UNDEF},
			{Name: "module_stop", Bind: elf.STB_GLOBAL, Section: elf.SHN_UNDEF},
		},
	}
	rela := make([]byte, 0, 3*24)
	for i, off := range []uint64{0x1000, 0x1008, 0x1010} {
		var e [24]byte
		binary.LittleEndian.PutUint64(e[0:8], off)
		binary.LittleEndian.PutUint64(e[8:16], uint64(i+1)<<32|R_X86_64_JUMP_SLOT)
		rela = append(rela, e[:]...)
	}
	applyRelaTable(l, rela, false)
	if len(l.Relocations) != 3 {
		t.Fatalf("got %d relocations, want 3", len(l.Relocations))
	}
	for i, want := range []bool{true, true, false} {
		if l.Relocations[i].WeakUndef != want {
			t.Fatalf("relocation %d (%s): WeakUndef=%v, want %v", i, l.Relocations[i].SymName, l.Relocations[i].WeakUndef, want)
		}
	}
	for _, off := range []uint64{0x1000, 0x1008, 0x1010} {
		binary.LittleEndian.PutUint64(l.MemoryImage[off:off+8], 0xdead)
	}
	if err := ResolveModuleRelocations(l, nil); err != nil {
		t.Fatal(err)
	}
	for _, c := range []struct {
		off  uint64
		want uint64
	}{{0x1000, 0}, {0x1008, 0x1008}, {0x1010, 0x1010}} {
		if got := binary.LittleEndian.Uint64(l.MemoryImage[c.off : c.off+8]); got != c.want {
			t.Fatalf("GOT 0x%x = 0x%x, want 0x%x", c.off, got, c.want)
		}
	}
}
