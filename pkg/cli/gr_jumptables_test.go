package cli

import (
	"io"
	"os"
	"path/filepath"
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"

	"golang.org/x/arch/x86/x86asm"
)

// TestGravityRushJumpTablesAreNotCode recovers the CFG of Gravity Rush Remastered (CUSA01113)
// as the build does (eboot.bin with its sce_module PRX merged into one guest image) and
// requires that switch tables inside functions are not decoded as instructions. Before, 191
// table entries were lifted as code (FDECSTP, STD, CLI, IN, LJMP ...). The ranges below are one
// table of each kind of recovery; the addresses are those of the merged image (eboot below
// 0x23c0000, libSceFios2.prx at 0x2410000, libc.prx at 0x2940000). PS4_GR_EBOOT is the path of
// the decrypted eboot.bin; the modules are read from sce_module next to it.
func TestGravityRushJumpTablesAreNotCode(t *testing.T) {
	eboot := os.Getenv("PS4_GR_EBOOT")
	if eboot == "" {
		t.Skip("PS4_GR_EBOOT is not set (path of Gravity Rush Remastered CUSA01113 eboot.bin); " +
			"pkg/disasm inline_table_test.go covers the table kinds without the game")
	}
	oldLog := logw
	logw = io.Discard
	defer func() { logw = oldLog }()

	cfg := &Config{ElfPath: eboot, AppDir: filepath.Dir(eboot)}
	loaded, err := elfloader.LoadELF(eboot)
	if err != nil {
		t.Fatal(err)
	}
	// The same steps as the CFG stage of the build.
	elfloader.AddTLSModule(loaded, loaded)
	modules, err := loadCompanionModules(cfg, loaded)
	if err != nil {
		t.Fatal(err)
	}
	elfloader.LayoutStaticTLS(loaded.TLSModules)
	elfloader.ApplyTLSOffsets(loaded)
	var exports []elfloader.Symbol
	for _, m := range modules {
		exports = append(exports, m.Exports...)
	}
	if err := elfloader.ResolveModuleRelocations(loaded, exports); err != nil {
		t.Fatal(err)
	}
	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}
	d.SkipFlagLiveness = true

	tables := []struct {
		start, end uint64
		what       string
	}{
		{0x4534b0, 0x453530, "eboot 0x452a30: four tables after the code of a sized function"},
		{0x235d70, 0x235d84, "eboot 0x230d00: table after a call that does not return (branch-following)"},
		{0x24182c4, 0x241835c, "libSceFios2 0x2417960: bounds check in a predecessor block"},
		{0x297774c, 0x2977760, "libc 0x2976fd0: vmovsd between the bounds check and its ja"},
		{0x298317c, 0x2983190, "libc 0x2983160: five entries to one target"},
		{0x2980280, 0x2980290, "libc 0x297ff60: entries that decoded as mov al, moffs64"},
	}
	var nFn, nInst, bad int
	err = d.WalkReachable(disasm.SeedEntryPoints(loaded, false), func(fn *disasm.Function) error {
		nFn++
		for _, b := range fn.Blocks {
			for _, in := range b.Insts {
				nInst++
				end := in.Address + uint64(in.Inst.Len)
				for _, tb := range tables {
					if in.Address < tb.end && tb.start < end {
						t.Errorf("fn 0x%x: %v at 0x%x decoded from table bytes (%s)", fn.EntryAddr, in.Inst, in.Address, tb.what)
					}
				}
				// Table entries decoded as code showed up as these; none is user-mode game code (HLT is:
				// libc 0x29572f0 is `hlt; ret`).
				switch in.Inst.Op {
				case x86asm.LJMP, x86asm.LCALL, x86asm.IN, x86asm.OUT, x86asm.CLI, x86asm.STI:
					if bad++; bad <= 20 {
						t.Errorf("fn 0x%x: %v at 0x%x is privileged or a far transfer", fn.EntryAddr, in.Inst, in.Address)
					}
				}
			}
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	if bad > 20 {
		t.Errorf("%d such instructions in total", bad)
	}
	t.Logf("%d functions, %d instructions", nFn, nInst)
}
