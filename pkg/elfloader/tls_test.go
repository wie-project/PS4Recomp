package elfloader

import (
	"debug/elf"
	"encoding/binary"
	"strings"
	"testing"
)

// tlsImage is a module with a PT_TLS template and GOT slots for three tls_index pairs.
func tlsImage(name string, vaddr, filesz, memsz, align uint64, rels []Relocation) *LoadedELF {
	return &LoadedELF{
		FileName:    name,
		TLSVaddr:    vaddr,
		TLSFilesz:   filesz,
		TLSMemsz:    memsz,
		TLSAlign:    align,
		MemoryImage: make([]byte, 0x1000),
		Relocations: rels,
		DynSymbols: []Symbol{
			{},
			{Type: elf.STT_SECTION},
			{Name: "foreign_tls#A#B", Type: elf.STT_TLS, Section: elf.SHN_UNDEF},
			{Name: "own_tls", Type: elf.STT_TLS, Section: 5, Address: 0x10},
		},
	}
}

func word(l *LoadedELF, off uint64) uint64 {
	return binary.LittleEndian.Uint64(l.MemoryImage[off : off+8])
}

func TestAddTLSModuleNumbersModulesInLoadOrder(t *testing.T) {
	main := tlsImage("eboot.bin", 0xda0060, 4, 0x30, 8, []Relocation{{Offset: 0x100, Type: R_X86_64_DTPMOD64}})
	noTLS := tlsImage("libSceNoTLS.prx", 0, 0, 0, 0, []Relocation{{Offset: 0x100, Type: R_X86_64_RELATIVE, Addend: 0x40}})
	fios := tlsImage("libSceFios2.prx", 0x8010020, 0, 0x40, 8, []Relocation{
		{Offset: 0x200, Type: R_X86_64_DTPMOD64, SymIdx: 1},
		{Offset: 0x210, Type: R_X86_64_DTPMOD64, SymIdx: 3},
		{Offset: 0x220, Type: R_X86_64_RELATIVE, Addend: 0x1234},
	})
	libc := tlsImage("libc.prx", 0x81dc020, 0x1a0, 0x490, 0x10, []Relocation{{Offset: 0x300, Type: R_X86_64_DTPMOD64, SymIdx: 1}})

	for _, m := range []*LoadedELF{main, noTLS, fios, libc} {
		if p := AddTLSModule(main, m); len(p) != 0 {
			t.Fatalf("%s: unexpected problems %v", m.FileName, p)
		}
	}
	LayoutStaticTLS(main.TLSModules)

	want := []TLSModule{
		{ID: 1, Name: "eboot.bin", Vaddr: 0xda0060, Filesz: 4, Memsz: 0x30, Align: 8, Offset: 0x30},
		{ID: 2, Name: "libSceFios2.prx", Vaddr: 0x8010020, Memsz: 0x40, Align: 8, Offset: 0x70},
		{ID: 3, Name: "libc.prx", Vaddr: 0x81dc020, Filesz: 0x1a0, Memsz: 0x490, Align: 0x10, Offset: 0x500},
	}
	if len(main.TLSModules) != len(want) {
		t.Fatalf("modules %+v", main.TLSModules)
	}
	for i := range want {
		if main.TLSModules[i] != want[i] {
			t.Errorf("module %d: got %+v want %+v", i, main.TLSModules[i], want[i])
		}
	}
	if got := word(main, 0x100); got != 1 {
		t.Errorf("eboot DTPMOD64 = %d, want 1", got)
	}
	if got := word(fios, 0x200); got != 2 {
		t.Errorf("Fios DTPMOD64 (section symbol) = %d, want 2", got)
	}
	if got := word(fios, 0x210); got != 2 {
		t.Errorf("Fios DTPMOD64 (own defined symbol) = %d, want 2", got)
	}
	if got := word(fios, 0x220); got != 0 {
		t.Errorf("a non-TLS relocation was written: 0x%x", got)
	}
	if got := word(libc, 0x300); got != 3 {
		t.Errorf("libc DTPMOD64 = %d, want 3", got)
	}
}

func TestAddTLSModuleAppliesDTPOFFAndTPOFF(t *testing.T) {
	// TPOFF64 in the executable: no symbol, A = 8 -> 8 - 0x30.
	main := tlsImage("eboot.bin", 0x1000, 0, 0x30, 8, []Relocation{{Offset: 0x100, Type: R_X86_64_TPOFF64, Addend: 8}})
	mod := tlsImage("libX.prx", 0x2000, 0, 0x40, 0x10, []Relocation{
		{Offset: 0x200, Type: R_X86_64_DTPOFF64, SymIdx: 3, Addend: 4},    // own_tls (0x10) + 4
		{Offset: 0x210, Type: R_X86_64_DTPOFF64, SymIdx: 1, Addend: 0x20}, // section symbol + 0x20
		{Offset: 0x400, Type: R_X86_64_TPOFF64, SymIdx: 3, Addend: 8},     // 0x18 - offset 0x70
	})
	for _, m := range []*LoadedELF{main, mod} {
		if p := AddTLSModule(main, m); len(p) != 0 {
			t.Fatalf("%s: problems %v", m.FileName, p)
		}
	}
	if got := word(mod, 0x200); got != 0x14 {
		t.Errorf("DTPOFF64 own symbol = 0x%x, want 0x14", got)
	}
	if got := word(mod, 0x210); got != 0x20 {
		t.Errorf("DTPOFF64 section symbol = 0x%x, want 0x20", got)
	}
	if got := word(main, 0x100); got != 0 {
		t.Errorf("TPOFF64 written before the layout: 0x%x", got)
	}
	LayoutStaticTLS(main.TLSModules)
	if main.TLSModules[1].Offset != 0x70 {
		t.Fatalf("module 2 offset 0x%x, want 0x70", main.TLSModules[1].Offset)
	}
	// The module is merged into main by now: its TPOFF64 slot is written in main's image.
	if p := ApplyTLSOffsets(main); len(p) != 0 {
		t.Fatalf("ApplyTLSOffsets problems %v", p)
	}
	if got := int64(word(main, 0x100)); got != 8-0x30 {
		t.Errorf("eboot TPOFF64 = %d, want %d", got, 8-0x30)
	}
	if got := int64(word(main, 0x400)); got != 0x18-0x70 {
		t.Errorf("module TPOFF64 = %d, want %d", got, 0x18-0x70)
	}
	if p := ApplyTLSOffsets(main); len(p) != 0 {
		t.Errorf("the queue was not emptied: %v", p)
	}
}

func TestAddTLSModuleReportsWhatItCannotResolve(t *testing.T) {
	main := tlsImage("eboot.bin", 0x1000, 0, 0x10, 8, nil)
	mod := tlsImage("libX.prx", 0x2000, 0, 0x10, 8, []Relocation{
		{Offset: 0x100, Type: R_X86_64_DTPMOD64, SymIdx: 2, SymName: "foreign_tls#A#B"},
		{Offset: 0x110, Type: R_X86_64_DTPOFF64, SymIdx: 2, SymName: "foreign_tls#A#B"},
		{Offset: 0x120, Type: R_X86_64_TPOFF64, SymIdx: 2, SymName: "foreign_tls#A#B"},
		{Offset: 0x2000, Type: R_X86_64_DTPMOD64, SymIdx: 1},
	})
	noTLS := tlsImage("libY.prx", 0, 0, 0, 0, []Relocation{{Offset: 0x100, Type: R_X86_64_DTPMOD64, SymIdx: 1}})
	AddTLSModule(main, main)
	p := AddTLSModule(main, mod)
	if len(p) != 4 || !strings.Contains(p[0], "type 16") || !strings.Contains(p[1], "type 17") ||
		!strings.Contains(p[2], "type 18") || !strings.Contains(p[0], "foreign_tls") || !strings.Contains(p[3], "outside the image") {
		t.Fatalf("problems %q", p)
	}
	for _, off := range []uint64{0x100, 0x110, 0x120} {
		if got := word(mod, off); got != 0 {
			t.Errorf("a relocation naming another module's symbol was written at 0x%x: 0x%x", off, got)
		}
	}
	if p := AddTLSModule(main, noTLS); len(p) != 1 || !strings.Contains(p[0], "without PT_TLS") {
		t.Errorf("TLS relocation in a module without PT_TLS: %q", p)
	}
	if len(main.TLSModules) != 2 {
		t.Errorf("%d TLS modules, want 2", len(main.TLSModules))
	}
}

func TestApplyTLSOffsetsReportsWhatItCannotWrite(t *testing.T) {
	main := tlsImage("eboot.bin", 0x1000, 0, 0x10, 8, []Relocation{{Offset: 0x100, Type: R_X86_64_TPOFF64}})
	AddTLSModule(main, main)
	if p := ApplyTLSOffsets(main); len(p) != 1 || !strings.Contains(p[0], "not laid out") {
		t.Errorf("before the layout: %q", p)
	}
	AddTLSModule(main, tlsImage("libX.prx", 0x2000, 0, 0x10, 8, []Relocation{{Offset: 0x900, Type: R_X86_64_TPOFF64}}))
	main.MemoryImage = main.MemoryImage[:0x800] // the slot lies past the merged image
	LayoutStaticTLS(main.TLSModules)
	if p := ApplyTLSOffsets(main); len(p) != 1 || !strings.Contains(p[0], "outside the merged image") {
		t.Errorf("outside the image: %q", p)
	}
}

func TestLayoutStaticTLSRoundsEachBlockToItsAlignment(t *testing.T) {
	mods := []TLSModule{{Memsz: 0x31, Align: 0x10}, {Memsz: 0x8, Align: 0x8}, {Memsz: 0x1, Align: 0x40}, {Memsz: 0x4}}
	LayoutStaticTLS(mods)
	for i, want := range []uint64{0x40, 0x48, 0x80, 0x84} {
		if mods[i].Offset != want {
			t.Errorf("module %d offset 0x%x, want 0x%x", i, mods[i].Offset, want)
		}
	}
}

func TestApplyBiasMovesTheTLSTemplate(t *testing.T) {
	l := &LoadedELF{TLSVaddr: 0x60020, TLSMemsz: 0x40, MinVAddr: 0, MaxVAddr: 0x1000, MemoryImage: make([]byte, 0x1000)}
	if err := l.ApplyBias(0x7fb0000); err != nil {
		t.Fatal(err)
	}
	if l.TLSVaddr != 0x8010020 {
		t.Errorf("TLSVaddr 0x%x, want 0x8010020", l.TLSVaddr)
	}
}

func TestApplyBiasKeepsTLSSymbolOffsets(t *testing.T) {
	l := &LoadedELF{MinVAddr: 0, MaxVAddr: 0x1000, MemoryImage: make([]byte, 0x1000),
		DynSymbols: []Symbol{{}, {Name: "tls_var", Type: elf.STT_TLS, Section: 5, Address: 0x18}, {Name: "fn", Type: elf.STT_FUNC, Address: 0x100}},
		Symbols:    []Symbol{{Name: "tls_var", Type: elf.STT_TLS, Section: 5, Address: 0x18}}}
	if err := l.ApplyBias(0x10000); err != nil {
		t.Fatal(err)
	}
	if l.DynSymbols[1].Address != 0x18 || l.Symbols[0].Address != 0x18 {
		t.Errorf("TLS symbol offset moved: dyn 0x%x, sym 0x%x", l.DynSymbols[1].Address, l.Symbols[0].Address)
	}
	if l.DynSymbols[2].Address != 0x10100 {
		t.Errorf("function symbol 0x%x, want 0x10100", l.DynSymbols[2].Address)
	}
}

func TestMergeImagesKeepsTheExecutablesTLS(t *testing.T) {
	dst := &LoadedELF{MinVAddr: 0, MaxVAddr: 0x1000, MemoryImage: make([]byte, 0x1000)}
	src := &LoadedELF{MinVAddr: 0x10000, MaxVAddr: 0x11000, MemoryImage: make([]byte, 0x11000),
		TLSVaddr: 0x10020, TLSFilesz: 8, TLSMemsz: 0x40, TLSAlign: 8}
	if err := MergeImages(dst, src); err != nil {
		t.Fatal(err)
	}
	if dst.TLSVaddr != 0 || dst.TLSMemsz != 0 {
		t.Errorf("a module's PT_TLS became the executable's: vaddr 0x%x memsz 0x%x", dst.TLSVaddr, dst.TLSMemsz)
	}
}
