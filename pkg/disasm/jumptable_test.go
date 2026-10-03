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
	sw, ok := matchPICSwitch(window, nil, nil)
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

func TestMatchPICSwitchInterleaved(t *testing.T) {
	img := make([]byte, 0x400)
	entry := uint64(0x100)
	pc := entry
	put := func(b ...byte) {
		copy(img[pc:], b)
		pc += uint64(len(b))
	}

	put(0x83, 0xfe, 0x05)             // cmp esi, 5
	put(0x77, 0x20)                   // ja +0x20
	leaPC := pc
	put(0x48, 0x8d, 0x05, 0, 0, 0, 0) // lea rax, [rip+table]
	put(0x48, 0x63, 0x0c, 0xb0)       // movsxd rcx, [rax+rsi*4]
	put(0xbf, 0x0a, 0x00, 0x00, 0x00) // mov edi, 0xa  (interleaved)
	put(0xba, 0x10, 0x00, 0x00, 0x00) // mov edx, 0x10 (interleaved)
	put(0x48, 0x01, 0xc1)             // add rcx, rax
	put(0xff, 0xe1)                   // jmp rcx

	table := uint64(0x200)
	leaNext := leaPC + 7
	disp := int32(int64(table) - int64(leaNext))
	binary.LittleEndian.PutUint32(img[leaPC+3:], uint32(disp))

	var window []Instruction
	pc = entry
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
	sw, ok := matchPICSwitch(window, nil, nil)
	if !ok {
		t.Fatal("expected PIC switch match with interleaved instructions")
	}
	if sw.tableAddr != table {
		t.Fatalf("table=0x%x want 0x%x", sw.tableAddr, table)
	}
	if sw.count != 6 {
		t.Fatalf("count=%d want 6", sw.count)
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
	sw, ok := matchPICSwitch(window, nil, nil)
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

func TestMatchPICSwitchSplitAcrossBlocks(t *testing.T) {
	// Simulate a switch where LEA and CMP/JA were in predecessor blocks:
	// Block 3:
	//   movzx ecx, cl
	//   movsxd rcx, [rax + 4*rcx]
	//   add rcx, rax
	//   jmp rcx
	block3Bytes := []byte{
		0x0f, 0xb6, 0xc9, // movzx ecx, cl
		0x48, 0x63, 0x0c, 0x88, // movsxd rcx, [rax+4*rcx]
		0x48, 0x01, 0xc1, // add rcx, rax
		0xff, 0xe1, // jmp rcx
	}
	var window []Instruction
	pc := uint64(0x1000)
	for len(block3Bytes[pc-0x1000:]) > 0 {
		inst, err := x86asm.Decode(block3Bytes[pc-0x1000:], 64)
		if err != nil {
			t.Fatal(err)
		}
		window = append(window, Instruction{Address: pc, Inst: inst})
		pc += uint64(inst.Len)
	}

	findTable := func(reg x86asm.Reg) (uint64, bool) {
		if sameGPR(reg, x86asm.RAX) {
			return 0x5000, true
		}
		return 0, false
	}
	findCount := func(idxReg x86asm.Reg) (int, bool) {
		if sameGPR(idxReg, x86asm.RCX) {
			return 17, true
		}
		return 0, false
	}

	sw, ok := matchPICSwitch(window, findTable, findCount)
	if !ok {
		t.Fatal("expected matchPICSwitch to succeed via predecessor callbacks")
	}
	if sw.tableAddr != 0x5000 {
		t.Errorf("tableAddr=%x, want 0x5000", sw.tableAddr)
	}
	if sw.count != 17 {
		t.Errorf("count=%d, want 17", sw.count)
	}
}

func TestMatchAbs64JumpTable(t *testing.T) {
	img := make([]byte, 0x1000)
	entry := uint64(0x100)
	pc := entry
	put := func(b ...byte) {
		copy(img[pc:], b)
		pc += uint64(len(b))
	}

	put(0x48, 0x83, 0xff, 0x03) // cmp rdi, 3
	jaPC := pc
	put(0x77, 0x00) // ja default (disp patched)

	leaPC := pc
	put(0x48, 0x8d, 0x05, 0, 0, 0, 0) // lea rax, [rip+table]
	put(0x48, 0x8b, 0x14, 0xf8)       // mov rdx, [rax+rdi*8]
	put(0xff, 0xe2)                   // jmp rdx

	case0 := pc
	put(0x90, 0xc3)
	case1 := pc
	put(0x90, 0xc3)
	case2 := pc
	put(0x90, 0xc3)
	case3 := pc
	put(0x90, 0xc3)
	def := pc
	put(0x90, 0xc3)

	img[jaPC+1] = byte(int8(int64(def) - int64(jaPC+2)))

	table := uint64(0x500)
	leaNext := leaPC + 7
	disp := int32(int64(table) - int64(leaNext))
	binary.LittleEndian.PutUint32(img[leaPC+3:], uint32(disp))
	binary.LittleEndian.PutUint64(img[table+0:], case0)
	binary.LittleEndian.PutUint64(img[table+8:], case1)
	binary.LittleEndian.PutUint64(img[table+16:], case2)
	binary.LittleEndian.PutUint64(img[table+24:], case3)

	loaded := switchTestELF(img)
	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}

	var window []Instruction
	p := entry
	for p < case0 {
		inst, err := x86asm.Decode(img[p:], 64)
		if err != nil {
			t.Fatalf("decode at 0x%x: %v", p, err)
		}
		window = append(window, Instruction{Address: p, Inst: inst})
		p += uint64(inst.Len)
	}

	res, ok := d.ResolveJumpTable(window, nil, nil, entry, 0x600)
	if !ok {
		t.Fatal("expected 64-bit jump table resolution")
	}
	if res.Format != TableFormatAbs64 {
		t.Errorf("got format %v, want TableFormatAbs64", res.Format)
	}
	if res.Count != 4 {
		t.Errorf("got count %d, want 4", res.Count)
	}
	if len(res.Targets) != 4 {
		t.Fatalf("got %d targets, want 4", len(res.Targets))
	}
	expected := []uint64{case0, case1, case2, case3}
	for i, exp := range expected {
		if res.Targets[i] != exp {
			t.Errorf("target[%d] = 0x%x, want 0x%x", i, res.Targets[i], exp)
		}
	}
}

func TestMatchAbs64DirectMemJump(t *testing.T) {
	img := make([]byte, 0x1000)
	entry := uint64(0x100)
	pc := entry
	put := func(b ...byte) {
		copy(img[pc:], b)
		pc += uint64(len(b))
	}

	put(0x48, 0x83, 0xff, 0x02) // cmp rdi, 2
	jaPC := pc
	put(0x77, 0x00) // ja default

	leaPC := pc
	put(0x48, 0x8d, 0x05, 0, 0, 0, 0) // lea rax, [rip+table]
	put(0xff, 0x24, 0xf8)             // jmp [rax+rdi*8]

	case0 := pc
	put(0x90, 0xc3)
	case1 := pc
	put(0x90, 0xc3)
	case2 := pc
	put(0x90, 0xc3)
	def := pc
	put(0x90, 0xc3)

	img[jaPC+1] = byte(int8(int64(def) - int64(jaPC+2)))

	table := uint64(0x600)
	leaNext := leaPC + 7
	disp := int32(int64(table) - int64(leaNext))
	binary.LittleEndian.PutUint32(img[leaPC+3:], uint32(disp))
	binary.LittleEndian.PutUint64(img[table+0:], case0)
	binary.LittleEndian.PutUint64(img[table+8:], case1)
	binary.LittleEndian.PutUint64(img[table+16:], case2)

	loaded := switchTestELF(img)
	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}

	var window []Instruction
	p := entry
	for p < case0 {
		inst, err := x86asm.Decode(img[p:], 64)
		if err != nil {
			t.Fatalf("decode at 0x%x: %v", p, err)
		}
		window = append(window, Instruction{Address: p, Inst: inst})
		p += uint64(inst.Len)
	}

	res, ok := d.ResolveJumpTable(window, nil, nil, entry, 0x700)
	if !ok {
		t.Fatal("expected direct memory 64-bit jump table resolution")
	}
	if res.Format != TableFormatAbs64 {
		t.Errorf("got format %v, want TableFormatAbs64", res.Format)
	}
	if res.Count != 3 {
		t.Errorf("got count %d, want 3", res.Count)
	}
	if len(res.Targets) != 3 {
		t.Fatalf("got %d targets, want 3", len(res.Targets))
	}
}

func TestBEXTRJumpTable(t *testing.T) {
	img := make([]byte, 0x1000)
	entry := uint64(0x100)
	pc := entry
	put := func(b ...byte) {
		copy(img[pc:], b)
		pc += uint64(len(b))
	}

	// Earlier obsolete AND EAX, 1 that must NOT confuse BEXTR:
	put(0x83, 0xe0, 0x01) // and eax, 1

	// BEXTR sequence:
	// mov ecx, 0x201 (start=1, len=2 -> 4 cases)
	put(0xb9, 0x01, 0x02, 0x00, 0x00)
	// bextr eax, r13d, ecx: c4 c2 70 f7 c5
	put(0xc4, 0xc2, 0x70, 0xf7, 0xc5)

	leaPC := pc
	put(0x48, 0x8d, 0x0d, 0, 0, 0, 0) // lea rcx, [rip+table]
	put(0x48, 0x63, 0x04, 0x81)       // movsxd rax, [rcx+rax*4]
	put(0x48, 0x01, 0xc8)             // add rax, rcx
	put(0xff, 0xe0)                   // jmp rax

	cases := make([]uint64, 4)
	for i := range cases {
		cases[i] = pc
		put(0xc3)
	}

	table := uint64(0x300)
	leaNext := leaPC + 7
	disp := int32(int64(table) - int64(leaNext))
	binary.LittleEndian.PutUint32(img[leaPC+3:], uint32(disp))
	for i, c := range cases {
		binary.LittleEndian.PutUint32(img[table+uint64(i*4):], uint32(int32(int64(c)-int64(table))))
	}

	loaded := switchTestELF(img)
	d, err := NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}

	var window []Instruction
	p := entry
	for p < cases[0] {
		inst, err := x86asm.Decode(img[p:], 64)
		if err != nil {
			t.Fatalf("decode at 0x%x: %v", p, err)
		}
		window = append(window, Instruction{Address: p, Inst: inst})
		p += uint64(inst.Len)
	}

	res, ok := d.ResolveJumpTable(window, nil, nil, entry, 0x800)
	if !ok {
		t.Fatal("expected BEXTR jump table resolution")
	}
	if res.Count != 4 {
		t.Fatalf("got count %d, want 4", res.Count)
	}
	if len(res.Targets) != 4 {
		t.Fatalf("got %d targets, want 4", len(res.Targets))
	}
	for i, c := range cases {
		if res.Targets[i] != c {
			t.Errorf("target[%d] = 0x%x, want 0x%x", i, res.Targets[i], c)
		}
	}
}

