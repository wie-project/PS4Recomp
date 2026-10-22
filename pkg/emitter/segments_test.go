package emitter

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"ps4-recomp/pkg/elfloader"
)

// A merged companion module's segments must not appear in the main image's segment list.
func TestOwnSegmentsExcludesMergedModules(t *testing.T) {
	main := &elfloader.LoadedELF{
		OrigMinVAddr: 0,
		OrigMaxVAddr: 0x2400000,
		MaxVAddr:     0x2400000,
		Segments: []*elfloader.Segment{
			{Vaddr: 0x0, Memsz: 0x1e00000, Flags: 5},
			{Vaddr: 0x2000000, Memsz: 0x70000, Flags: 4},
			{Vaddr: 0x2200000, Memsz: 0x1e0000, Flags: 6},
		},
	}
	fios := &elfloader.LoadedELF{
		MinVAddr: 0x2800000,
		MaxVAddr: 0x2900000,
		Segments: []*elfloader.Segment{{Vaddr: 0x2800000, Memsz: 0x80000, Flags: 5}},
	}
	if err := elfloader.MergeImages(main, fios); err != nil {
		t.Fatal(err)
	}
	if len(main.Segments) != 4 {
		t.Fatalf("merge should append the module segment: got %d segments", len(main.Segments))
	}
	got := ownSegments(main)
	if len(got) != 3 {
		t.Fatalf("ownSegments: got %d segments, want 3", len(got))
	}
	for _, s := range got {
		if s.Vaddr >= 0x2400000 {
			t.Fatalf("ownSegments kept merged module segment at 0x%x", s.Vaddr)
		}
	}
}

// A module with more segments than the runtime's table holds registers the first four and
// leaves a generation-time warning naming it.
func TestModuleSegmentCapWarns(t *testing.T) {
	em := NewAsmEmitter(unresolvedTestELF(), nil, nil)
	var segs []*elfloader.Segment
	for i := uint64(0); i < 5; i++ {
		segs = append(segs, &elfloader.Segment{Vaddr: 0x800000 + i*0x10000, Memsz: 0x10000, Flags: 4})
	}
	em.Modules = []GuestModule{{FileName: "libBig.prx", StartAddr: 0x800000, EndAddr: 0x850000, Segments: segs}}
	path := filepath.Join(t.TempDir(), "guest_modules.c")
	if err := em.EmitGuestModules(path); err != nil {
		t.Fatal(err)
	}
	if len(em.Warnings) != 1 || !strings.Contains(em.Warnings[0], "libBig.prx has 5 segments") {
		t.Fatalf("warnings %q, want one for libBig.prx's 5 segments", em.Warnings)
	}
	src, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(string(src), "0x840000ULL") || !strings.Contains(string(src), ".num_segments = 4,") {
		t.Errorf("guest_modules.c does not register exactly the first four segments of libBig.prx")
	}
}
