// SPDX-License-Identifier: GPL-2.0-or-later

package elfloader

import (
	"bytes"
	"encoding/binary"
	"testing"
)

// TestHostDataPageNetExports: libSceNet's in6addr exports (shadPS4 LIB_OBJ) resolve to their
// own copies in the host data page, by NID or by name, through the module merge and the import
// resolution, and R_X86_64_64 keeps its addend.
func TestHostDataPageNetExports(t *testing.T) {
	const mainGot = 0x2000
	main := &LoadedELF{
		FileName:    "main",
		MinVAddr:    0x1000,
		MaxVAddr:    0x3000,
		MemoryImage: make([]byte, 0x3000),
		Relocations: []Relocation{
			{Offset: mainGot, Type: R_X86_64_GLOB_DAT, SymName: "XCuA-GqjA-k#C#C"}, // in6addr_loopback
			{Offset: mainGot + 8, Type: R_X86_64_64, SymName: "PgNI+j4zxzM#C#C", Addend: 15},
			{Offset: mainGot + 16, Type: R_X86_64_GLOB_DAT, SymName: "f7uOxY9mM1U#D#D"},
		},
	}
	page := ensureHostDataPage(main)
	if page != 0x3000 || uint64(len(main.MemoryImage)) != page+hostDataPageSize {
		t.Fatalf("page at 0x%x, image 0x%x", page, len(main.MemoryImage))
	}
	if err := ResolveModuleRelocations(main, nil); err != nil {
		t.Fatal(err)
	}

	const srcGot = 0x12000
	src := &LoadedELF{
		FileName:    "companion.prx",
		MinVAddr:    0x10000,
		MaxVAddr:    0x13000,
		MemoryImage: make([]byte, 0x13000),
		Relocations: []Relocation{
			{Offset: srcGot, Type: R_X86_64_GLOB_DAT, SymName: "in6addr_any"},
			{Offset: srcGot + 8, Type: R_X86_64_GLOB_DAT, SymName: "84MgU4MMTLQ#C#C"},
		},
	}
	if err := MergeImages(main, src); err != nil {
		t.Fatal(err)
	}
	if main.HostDataAddr != page {
		t.Fatalf("HostDataAddr 0x%x, want 0x%x", main.HostDataAddr, page)
	}

	slot := func(off uint64) uint64 { return binary.LittleEndian.Uint64(main.MemoryImage[off : off+8]) }
	at := func(addr uint64, n int) []byte { return main.MemoryImage[addr : addr+uint64(n)] }
	loopback := []byte{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}
	if a := slot(mainGot); !bytes.Equal(at(a, 16), loopback) {
		t.Errorf("in6addr_loopback slot 0x%x holds %x", a, at(a, 16))
	}
	if a := slot(mainGot + 8); at(a, 1)[0] != 1 || a != page+0x80+15 {
		t.Errorf("nodelocal_allnodes + 15: slot 0x%x", a)
	}
	if a := slot(mainGot + 16); a != page || binary.LittleEndian.Uint64(at(a, 8)) != stackCanaryValue {
		t.Errorf("canary slot 0x%x", a)
	}
	if a := slot(srcGot); a != page+0x10 || !bytes.Equal(at(a, 16), make([]byte, 16)) {
		t.Errorf("in6addr_any slot 0x%x", a)
	}
	allNodes := []byte{0xff, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}
	if a := slot(srcGot + 8); !bytes.Equal(at(a, 16), allNodes) {
		t.Errorf("linklocal_allnodes slot 0x%x holds %x", a, at(a, 16))
	}
	// Each export is its own copy.
	end := uint64(0)
	for i := range hostDataSymbols {
		s := &hostDataSymbols[i]
		if s.offset < end || s.offset+uint64(len(s.value)) > hostDataPageSize {
			t.Errorf("%s: offset 0x%x overlaps the previous export or leaves the page", s.name, s.offset)
		}
		end = s.offset + uint64(len(s.value))
		if CalculateNID(s.name) != s.nid {
			t.Errorf("%s: NID %s, want %s", s.name, s.nid, CalculateNID(s.name))
		}
	}
}
