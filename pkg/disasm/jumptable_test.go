package disasm

import (
	"debug/elf"
	"encoding/binary"
	"os"
	"testing"

	"ps4-recomp/pkg/elfloader"

	"golang.org/x/arch/x86/x86asm"
)

func assembleClangSwitch(img []byte) (entry, case0, case1, case2, def, table uint64) {
	entry = 0x100
	pc := entry
	put := func(b ...byte) {
		copy(img[pc:], b)
		pc += uint64(len(b))
	}

	put(0x83, 0xc6, 0xf7) // add esi, -9
	put(0x83, 0xfe, 0x02) // cmp esi, 2
	jaPC := pc
	put(0x77, 0x00) // ja default (disp patched)

	leaPC := pc
	put(0x48, 0x8d, 0x05, 0, 0, 0, 0) // lea rax, [rip+table]
	put(0x48, 0x63, 0x0c, 0xb0)       // movsxd rcx, [rax+rsi*4]
	put(0x48, 0x01, 0xc1)             // add rcx, rax
	put(0xff, 0xe1)                   // jmp rcx

	case0 = pc
	put(0xc3)
	case1 = pc
	put(0xc3)
	case2 = pc
	put(0xc3)
	def = pc
	put(0xc3)

	img[jaPC+1] = byte(int8(int64(def) - int64(jaPC+2)))

	table = 0x200
	leaNext := leaPC + 7
	disp := int32(int64(table) - int64(leaNext))
	binary.LittleEndian.PutUint32(img[leaPC+3:], uint32(disp))
	binary.LittleEndian.PutUint32(img[table+0:], uint32(int32(int64(case0)-int64(table))))
	binary.LittleEndian.PutUint32(img[table+4:], uint32(int32(int64(case1)-int64(table))))
	binary.LittleEndian.PutUint32(img[table+8:], uint32(int32(int64(case2)-int64(table))))
	binary.LittleEndian.PutUint32(img[table+12:], 0)
	return entry, case0, case1, case2, def, table
}

func switchTestELF(img []byte) *elfloader.LoadedELF {
	return &elfloader.LoadedELF{
		MemoryImage:  img,
		ExecRanges:   []elfloader.AddrRange{{Start: 0, End: uint64(len(img))}},
		SymbolByAddr: make(map[uint64]elfloader.Symbol),
		Sections:     make(map[string]*elf.Section),
	}
}

func TestMatchPICSwitchFromBytes(t *testing.T) {
	img := make([]byte, 0x400)
	entry, _, _, _, _, table := assembleClangSwitch(img)

	var window []Instruction
	pc := entry
	for i := 0; i < 16; i++ {
		inst, err := x86asm.Decode(img[pc:], 64)
		if err != nil {
			t.Fatalf("decode 0x%x: %v", pc, err)
		}
		window = append(window, Instruction{Address: pc, Inst: inst})
		pc += uint64(inst.Len)
		if inst.Op == x86asm.JMP {
			break
		}
	}
	sw, ok := matchPICSwitch(window)
	if !ok {
		t.Fatal("expected PIC switch match")
	}
	if sw.tableAddr != table {
		t.Fatalf("table=0x%x want 0x%x", sw.tableAddr, table)
	}
	if sw.count != 3 {
		t.Fatalf("count=%d want 3", sw.count)
	}
}

func TestJumpTableLeadersWithoutRodata(t *testing.T) {
	img := make([]byte, 0x400)
	entry, case0, case1, case2, def, _ := assembleClangSwitch(img)
	loaded := switchTestELF(img)

	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}
	if err := d.AnalyzeReachable([]uint64{entry}); err != nil {
		t.Fatal(err)
	}
	fn := d.Functions[entry]
	if fn == nil {
		t.Fatal("function not recovered")
	}
	for _, addr := range []uint64{entry, case0, case1, case2, def} {
		if _, ok := fn.Blocks[addr]; !ok {
			t.Errorf("missing block leader 0x%x (blocks=%v)", addr, fn.BlockOrder)
		}
	}
}

func TestFindJumpTableTargetsNoSectionNames(t *testing.T) {
	img := make([]byte, 0x400)
	entry, case0, case1, case2, _, table := assembleClangSwitch(img)
	loaded := switchTestELF(img)
	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}
	fnEnd := entry + 0x40
	got := d.findJumpTableTargets(table, entry, fnEnd)
	want := map[uint64]bool{case0: true, case1: true, case2: true}
	if len(got) != 3 {
		t.Fatalf("got %d targets %v, want 3", len(got), got)
	}
	for _, taddr := range got {
		if !want[taddr] {
			t.Errorf("unexpected target 0x%x", taddr)
		}
	}
}

func TestLibExamplePrintfSwitchLeaders(t *testing.T) {
	path := "../../tools/OpenOrbis/PS4Toolchain/samples/using_library/sce_module/libExample.prx"
	if _, err := os.Stat(path); err != nil {
		t.Skip("libExample.prx not present")
	}
	loaded, err := elfloader.LoadELF(path)
	if err != nil {
		t.Fatal(err)
	}
	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}

	// Hand-decode the known Clang switch at 0xb5d8 (vfprintf-style).
	var window []Instruction
	pc := uint64(0xb5d8)
	for i := 0; i < 8; i++ {
		inst, err := x86asm.Decode(loaded.MemoryImage[pc:], 64)
		if err != nil {
			t.Fatal(err)
		}
		window = append(window, Instruction{Address: pc, Inst: inst})
		pc += uint64(inst.Len)
		if inst.Op == x86asm.JMP {
			break
		}
	}
	sw, ok := matchPICSwitch(window)
	if !ok {
		for _, in := range window {
			t.Logf("  0x%x %s", in.Address, in.Inst)
		}
		t.Fatal("PIC switch at 0xb5d8 not matched")
	}
	t.Logf("table=0x%x base=0x%x count=%d", sw.tableAddr, sw.baseAddr, sw.count)
	targets := d.jumpTableTargets(sw, 0xb0b8, 0)
	found := false
	for _, tgt := range targets {
		if tgt == 0xbbee {
			found = true
			break
		}
	}
	if !found {
		t.Fatalf("0xbbee missing from %d table targets (count=%d)", len(targets), sw.count)
	}

	fn, _, err := d.DisasmFunction(0xb0b8)
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := fn.Blocks[0xbbee]; !ok {
		t.Fatalf("0xbbee not a leader after DisasmFunction (blocks=%d)", len(fn.Blocks))
	}
}

func TestLibExampleHltDoesNotDropEpilogue(t *testing.T) {
	path := "../../tools/OpenOrbis/PS4Toolchain/samples/using_library/sce_module/libExample.prx"
	if _, err := os.Stat(path); err != nil {
		t.Skip("libExample.prx not present")
	}
	loaded, err := elfloader.LoadELF(path)
	if err != nil {
		t.Fatal(err)
	}
	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}
	// NID tIhsqj0qsFE is 44 bytes and contains HLT (0xf4) on a side path.
	// The following RET is the target of an earlier JE and must stay a block.
	fn, _, err := d.DisasmFunction(0xe8f0)
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := fn.Blocks[0xe91b]; !ok {
		t.Fatalf("RET at 0xe91b was dropped (blocks=%v)", fn.BlockOrder)
	}
}

func TestLibExampleSwitchCaseIsLeader(t *testing.T) {
	path := "../../tools/OpenOrbis/PS4Toolchain/samples/using_library/sce_module/libExample.prx"
	if _, err := os.Stat(path); err != nil {
		t.Skip("libExample.prx not present")
	}
	loaded, err := elfloader.LoadELF(path)
	if err != nil {
		t.Fatal(err)
	}
	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}
	entries := SeedEntryPoints(loaded, false)
	if err := d.AnalyzeReachable(entries); err != nil {
		t.Fatal(err)
	}

	if _, ok := d.Functions[0xad5c]; !ok {
		t.Error("static function 0xad5c (reloc function pointer) was not seeded")
	}
	for _, addr := range []uint64{0xce12, 0xbbee} {
		found := false
		for _, fn := range d.Functions {
			if _, ok := fn.Blocks[addr]; ok {
				found = true
				break
			}
		}
		if !found {
			t.Errorf("0x%x was not recovered as a jump-table leader", addr)
		}
	}
}
