package lifter_test

import (
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/lifter"
)

// benchInstructions decodes hello_world.elf once and returns the functions the
// lifters translate in the benchmark.
func benchInstructions(b *testing.B) []*disasm.Function {
	b.Helper()
	loaded, err := elfloader.LoadELF("../../hello_world.elf")
	if err != nil {
		b.Fatalf("failed to load elf: %v", err)
	}
	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		b.Fatalf("failed to create disassembler: %v", err)
	}
	if err := d.AnalyzeReachable(disasm.SeedEntryPoints(loaded, false)); err != nil {
		b.Fatalf("AnalyzeReachable failed: %v", err)
	}
	out := make([]*disasm.Function, 0, len(d.Functions))
	for _, fn := range d.Functions {
		out = append(out, fn)
	}
	if len(out) == 0 {
		b.Fatal("no functions decoded")
	}
	return out
}

// BenchmarkLiftAll measures the full x86-to-ARM64 lift of a small binary. The
// output line count is accumulated so the compiler cannot drop the work.
func BenchmarkLiftAll(b *testing.B) {
	funcs := benchInstructions(b)
	known := make(map[uint64]bool, len(funcs))
	for _, fn := range funcs {
		known[fn.EntryAddr] = true
	}

	b.ReportAllocs()
	b.ResetTimer()
	lines := 0
	for i := 0; i < b.N; i++ {
		l := lifter.NewArm64Lifter(known)
		l.SetTSO(false)
		var buf []string
		for _, fn := range funcs {
			for _, blockAddr := range fn.BlockOrder {
				block := fn.Blocks[blockAddr]
				if block == nil {
					continue
				}
				l.ResetFlags()
				for _, inst := range block.Insts {
					nextPC := inst.Address + uint64(inst.Inst.Len)
					out, err := l.LiftInstructionToBuf(inst, nextPC, fn, buf[:0])
					if err != nil {
						b.Fatalf("lift 0x%x: %v", inst.Address, err)
					}
					buf = out
					lines += len(out)
				}
			}
		}
	}
	if lines == 0 {
		b.Fatal("no lines emitted")
	}
}
