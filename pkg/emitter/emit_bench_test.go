package emitter_test

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/emitter"
	"ps4-recomp/pkg/lifter"
)

// emitHarness loads hello_world.elf the way the CLI does: reachable functions are indexed
// without their IR, and the emitter decodes each function on demand while lifting.
func emitHarness(tb testing.TB) (*elfloader.LoadedELF, *disasm.Disassembler, []disasm.ReachableFunc) {
	tb.Helper()
	loaded, err := elfloader.LoadELF("../../hello_world.elf")
	if err != nil {
		tb.Fatalf("failed to load elf: %v", err)
	}
	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		tb.Fatalf("failed to create disassembler: %v", err)
	}
	funcs, err := d.DiscoverReachable(disasm.SeedEntryPoints(loaded, false), nil)
	if err != nil {
		tb.Fatalf("DiscoverReachable failed: %v", err)
	}
	if len(funcs) == 0 {
		tb.Fatal("no reachable functions")
	}
	nInst := 0
	for _, f := range funcs {
		nInst += f.Insts
	}
	tb.Logf("%d functions, %d instructions", len(funcs), nInst)
	return loaded, d, funcs
}

func newEmitter(loaded *elfloader.LoadedELF, d *disasm.Disassembler, funcs []disasm.ReachableFunc) *emitter.AsmEmitter {
	known := make(map[uint64]bool, len(funcs))
	for _, f := range funcs {
		known[f.Addr] = true
	}
	em := emitter.NewAsmEmitter(loaded, d, lifter.NewArm64Lifter(known))
	em.Funcs = funcs
	return em
}

func BenchmarkEmitChunkedAsm(b *testing.B) {
	loaded, d, funcs := emitHarness(b)
	em := newEmitter(loaded, d, funcs)
	dir := b.TempDir()

	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if _, err := em.EmitChunkedAsm(dir, 15000); err != nil {
			b.Fatalf("EmitChunkedAsm failed: %v", err)
		}
	}
}

// TestEmitChunkedAsmDeterministic pins the chunk files to the same bytes across two runs:
// chunk workers lift in parallel, so ordering and per-chunk lifter state must not leak into
// the output.
func TestEmitChunkedAsmDeterministic(t *testing.T) {
	loaded, d, funcs := emitHarness(t)
	dirA, dirB := t.TempDir(), t.TempDir()

	if _, err := newEmitter(loaded, d, funcs).EmitChunkedAsm(dirA, 15000); err != nil {
		t.Fatalf("first EmitChunkedAsm failed: %v", err)
	}
	if _, err := newEmitter(loaded, d, funcs).EmitChunkedAsm(dirB, 15000); err != nil {
		t.Fatalf("second EmitChunkedAsm failed: %v", err)
	}

	entries, err := os.ReadDir(dirA)
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) == 0 {
		t.Fatal("no files emitted")
	}
	for _, ent := range entries {
		if ent.IsDir() || !strings.HasSuffix(ent.Name(), ".s") {
			continue
		}
		a, err := os.ReadFile(filepath.Join(dirA, ent.Name()))
		if err != nil {
			t.Fatal(err)
		}
		b, err := os.ReadFile(filepath.Join(dirB, ent.Name()))
		if err != nil {
			t.Fatal(err)
		}
		if string(a) != string(b) {
			t.Fatalf("%s differs between two emissions", ent.Name())
		}
	}
}
