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

	put(0x83, 0xfe, 0x05) // cmp esi, 5
	put(0x77, 0x20)       // ja +0x20
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

// Bounds checks that size a switch table: a CMP on the register the index was copied from
// (mov ebx, edx; cmp edx, 0xe; ja), CMP + JBE/JB, and no bound when the copy source changes
// after the copy.
func TestSwitchTableCountBounds(t *testing.T) {
	dec := func(hexes ...[]byte) []Instruction {
		var w []Instruction
		pc := uint64(0x1000)
		for _, b := range hexes {
			inst, err := x86asm.Decode(b, 64)
			if err != nil {
				t.Fatal(err)
			}
			w = append(w, Instruction{Address: pc, Inst: inst})
			pc += uint64(inst.Len)
		}
		return w
	}
	movEbxEdx := []byte{0x89, 0xd3}
	for _, tc := range []struct {
		name   string
		window []Instruction
		idx    x86asm.Reg
		want   int
	}{
		{"mov ebx, edx; cmp edx, 0xe; ja", dec(movEbxEdx, []byte{0x83, 0xfa, 0x0e}, []byte{0x77, 0x00}), x86asm.RBX, 15},
		{"cmp ecx, 7; jbe", dec([]byte{0x83, 0xf9, 0x07}, []byte{0x76, 0x00}), x86asm.RCX, 8},
		{"cmp ecx, 7; jb", dec([]byte{0x83, 0xf9, 0x07}, []byte{0x72, 0x00}), x86asm.RCX, 7},
		{"cmp ecx, 7; ja", dec([]byte{0x83, 0xf9, 0x07}, []byte{0x77, 0x00}), x86asm.RCX, 8},
		{"mov ebx, edx; inc edx; cmp edx, 0xe; ja", dec(movEbxEdx, []byte{0xff, 0xc2}, []byte{0x83, 0xfa, 0x0e}, []byte{0x77, 0x00}), x86asm.RBX, 0},
	} {
		if got := switchTableCount(tc.window, tc.idx); got != tc.want {
			t.Errorf("%s: count %d, want %d", tc.name, got, tc.want)
		}
	}
}

// A switch without a provable bound is scanned until the entries stop looking like code; the
// scan is cut at the first entry that lands inside an already decoded instruction, so neither
// that entry nor the ones after it become blocks.
func TestUnboundedSwitchStopsInsideInstruction(t *testing.T) {
	img := make([]byte, 0x400)
	pc := uint64(0x100)
	put := func(b ...byte) uint64 {
		at := pc
		copy(img[pc:], b)
		pc += uint64(len(b))
		return at
	}
	movabs := put(0x48, 0xba, 1, 2, 3, 4, 5, 6, 7, 8) // movabs rdx, imm64 (decoded before the table)
	lea := put(0x48, 0x8d, 0x05, 0, 0, 0, 0)          // lea rax, [rip+table]
	put(0x48, 0x63, 0x0c, 0xb0)                       // movsxd rcx, [rax+rsi*4]
	put(0x48, 0x01, 0xc1)                             // add rcx, rax
	put(0xff, 0xe1)                                   // jmp rcx
	case0 := put(0xc3)
	case1 := put(0xc3)
	after := put(0xc3) // only an entry past the cut points here
	const table = 0x200
	binary.LittleEndian.PutUint32(img[lea+3:], uint32(int32(table-int64(lea+7))))
	inside := movabs + 3
	for i, target := range []uint64{case0, case1, inside, after} {
		binary.LittleEndian.PutUint32(img[table+4*i:], uint32(int32(int64(target)-table)))
	}
	d, err := NewDisassembler(switchTestELF(img))
	if err != nil {
		t.Fatal(err)
	}
	if err := d.AnalyzeReachable([]uint64{0x100}); err != nil {
		t.Fatal(err)
	}
	fn := d.Functions[0x100]
	if fn == nil {
		t.Fatal("function not recovered")
	}
	for _, a := range []uint64{case0, case1} {
		if fn.Blocks[a] == nil {
			t.Errorf("case 0x%x is not a block (blocks %#x)", a, fn.BlockOrder)
		}
	}
	for _, a := range []uint64{inside, after} {
		if fn.Blocks[a] != nil {
			t.Errorf("0x%x became a block although the table scan should stop at 0x%x (blocks %#x)", a, inside, fn.BlockOrder)
		}
	}
}

// farSliceSwitch assembles a switch whose table load and add are scheduled far from the jmp:
// the compiler moved 38 unrelated SIMD instructions between `add rax, rdx` and `jmp rax`
// (inFAMOUS Second Son, eboot 0x23dab7..0x23db8e). It returns the image, the entry, the jmp,
// the five case targets (the first is the instruction after the jmp), the default and the end.
func farSliceSwitch() (img []byte, entry, jmpPC uint64, cases []uint64, def, end uint64) {
	img = make([]byte, 0x400)
	entry = 0x100
	pc := entry
	put := func(b ...byte) {
		copy(img[pc:], b)
		pc += uint64(len(b))
	}
	put(0x89, 0xc8)       // mov eax, ecx
	put(0x83, 0xf9, 0x04) // cmp ecx, 4
	jaPC := pc
	put(0x0f, 0x87, 0, 0, 0, 0) // ja default (rel32 patched)
	leaPC := pc
	put(0x48, 0x8d, 0x15, 0, 0, 0, 0) // lea rdx, [rip+table]
	put(0x48, 0x63, 0x04, 0x82)       // movsxd rax, [rdx+rax*4]
	put(0x48, 0x01, 0xd0)             // add rax, rdx
	for i := 0; i < 38; i++ {
		put(0xc5, 0xf8, 0x59, 0xc0) // vmulps xmm0, xmm0, xmm0
	}
	jmpPC = pc
	put(0xff, 0xe0) // jmp rax
	for i := 0; i < 5; i++ {
		cases = append(cases, pc)
		put(0xc3)
	}
	def = pc
	put(0xc3)
	end = pc
	binary.LittleEndian.PutUint32(img[jaPC+2:], uint32(int32(int64(def)-int64(jaPC+6))))
	table := uint64(0x300)
	binary.LittleEndian.PutUint32(img[leaPC+3:], uint32(int32(int64(table)-int64(leaPC+7))))
	for i, c := range cases {
		binary.LittleEndian.PutUint32(img[table+uint64(4*i):], uint32(int32(int64(c)-int64(table))))
	}
	return img, entry, jmpPC, cases, def, end
}

// checkFarSliceSwitch recovers the function at entry and checks its table and leaders.
func checkFarSliceSwitch(t *testing.T, loaded *elfloader.LoadedELF, entry, jmpPC uint64, cases []uint64, def uint64) {
	t.Helper()
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
	if got := fn.JumpTables[jmpPC]; len(got) != len(cases) {
		t.Fatalf("jump table at 0x%x: targets %#x, want %#x", jmpPC, got, cases)
	}
	for _, c := range append(cases, def) {
		if _, ok := fn.Blocks[c]; !ok {
			t.Errorf("missing block leader 0x%x (blocks=%v)", c, fn.BlockOrder)
		}
	}
}

// The table is found however far back in the straight-line run before the jmp it is: through
// the branch-following decoder (no symbol) ...
func TestJumpTableSliceFarFromJmp(t *testing.T) {
	img, entry, jmpPC, cases, def, _ := farSliceSwitch()
	checkFarSliceSwitch(t, switchTestELF(img), entry, jmpPC, cases, def)
}

// ... and through the linear decoder of a function with a symbol size (disasmLinearFunction).
func TestJumpTableSliceFarFromJmpLinear(t *testing.T) {
	img, entry, jmpPC, cases, def, end := farSliceSwitch()
	loaded := switchTestELF(img)
	loaded.SymbolByAddr[entry] = elfloader.Symbol{Name: "far_switch", Address: entry, Size: end - entry, Type: elf.STT_FUNC}
	checkFarSliceSwitch(t, loaded, entry, jmpPC, cases, def)
}

// TestSwitchBoundOnTakenEdgeDoesNotCountTable: two switches on one index (inFAMOUS Second Son,
// eboot 0x202130 and 0x202251). The first `cmp eax, 7; ja second` bounds the index only on its
// fall-through edge; its taken edge leads to the second switch, whose 13-entry table is indexed
// with values above 7 (the first switch's cases also flow into it, where the index is at most
// 7: a bound found on one path is not a bound on all of them). Taking 8 from the first check
// dropped the second table's case 9, and the game stopped on "Unresolved indirect jump/call to
// 0x20233d".
func TestSwitchBoundOnTakenEdgeDoesNotCountTable(t *testing.T) {
	img := make([]byte, 0x400)
	pc := uint64(0x100)
	entry := pc
	put := func(b ...byte) {
		copy(img[pc:], b)
		pc += uint64(len(b))
	}
	lea := func(table uint64) {
		at := pc
		put(0x48, 0x8d, 0x05, 0, 0, 0, 0) // lea rax, [rip+table]
		binary.LittleEndian.PutUint32(img[at+3:], uint32(int32(int64(table)-int64(at+7))))
	}
	const table1, table2 = 0x300, 0x340

	put(0x41, 0x89, 0xc5) // mov r13d, eax
	put(0x83, 0xf8, 0x07) // cmp eax, 7
	ja1 := pc
	put(0x77, 0x00) // ja second (patched)
	lea(table1)
	put(0x4a, 0x63, 0x0c, 0xa8) // movsxd rcx, [rax+r13*4]
	put(0x48, 0x01, 0xc1)       // add rcx, rax
	put(0xff, 0xe1)             // jmp rcx
	low := pc
	put(0x90) // nop: the first switch's cases fall into the second
	second := pc
	img[ja1+1] = byte(int8(int64(second) - int64(ja1+2)))
	put(0x8b, 0x85, 0xe0, 0xfe, 0xff, 0xff) // mov eax, [rbp-0x120] (a spilled copy of the index)
	put(0x83, 0xf8, 0x0c)                   // cmp eax, 12
	ja2 := pc
	put(0x77, 0x00) // ja def (patched)
	lea(table2)
	put(0x4a, 0x63, 0x0c, 0xa8) // movsxd rcx, [rax+r13*4]
	put(0x48, 0x01, 0xc1)       // add rcx, rax
	put(0xff, 0xe1)             // jmp rcx
	common := pc
	put(0xc3)
	case9 := pc
	put(0xc3)
	def := pc
	put(0xc3)
	img[ja2+1] = byte(int8(int64(def) - int64(ja2+2)))

	for i := 0; i < 8; i++ {
		binary.LittleEndian.PutUint32(img[table1+4*uint64(i):], uint32(int32(int64(low)-int64(table1))))
	}
	for i := 0; i < 13; i++ {
		target := common
		if i == 9 {
			target = case9
		}
		binary.LittleEndian.PutUint32(img[table2+4*uint64(i):], uint32(int32(int64(target)-int64(table2))))
	}

	d, err := NewDisassembler(switchTestELF(img))
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
	for _, addr := range []uint64{low, second, common, case9, def} {
		if _, ok := fn.Blocks[addr]; !ok {
			t.Errorf("missing block 0x%x (blocks %v)", addr, fn.BlockOrder)
		}
	}
}

// switchAsm assembles small x86-64 functions for the predecessor-count tests.
type switchAsm struct {
	img []byte
	pc  uint64
}

func (a *switchAsm) put(b ...byte) uint64 {
	at := a.pc
	copy(a.img[a.pc:], b)
	a.pc += uint64(len(b))
	return at
}

// jcc8 emits a short conditional or unconditional jump (opcode op) and returns where to patch it.
func (a *switchAsm) jcc8(op byte) uint64 { return a.put(op, 0) }

func (a *switchAsm) patch8(at, target uint64) {
	a.img[at+1] = byte(int8(int64(target) - int64(at+2)))
}

// dispatch emits `lea rcx, [rip+table]; movsxd rax, [rcx+4*rax]; add rax, rcx; jmp rax` and
// returns the jmp's address.
func (a *switchAsm) dispatch(table uint64) uint64 {
	at := a.put(0x48, 0x8d, 0x0d, 0, 0, 0, 0)
	binary.LittleEndian.PutUint32(a.img[at+3:], uint32(int32(int64(table)-int64(at+7))))
	a.put(0x48, 0x63, 0x04, 0x81) // movsxd rax, [rcx+rax*4]
	a.put(0x48, 0x01, 0xc8)       // add rax, rcx
	return a.put(0xff, 0xe0)      // jmp rax
}

func (a *switchAsm) table(at uint64, targets ...uint64) {
	for i, t := range targets {
		binary.LittleEndian.PutUint32(a.img[at+4*uint64(i):], uint32(int32(int64(t)-int64(at))))
	}
}

func analyzeSwitch(t *testing.T, img []byte, entry, jmp uint64) map[uint64]bool {
	t.Helper()
	d, err := NewDisassembler(switchTestELF(img))
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
	got := map[uint64]bool{}
	for _, tg := range fn.JumpTables[jmp] {
		got[tg] = true
	}
	return got
}

// TestSwitchCountFollowsCopyBeforeJcc: inFAMOUS Second Son fn 0x23cef0 checks the index in
// another register and copies it between the check and the jump (`cmp ebx, 7; mov eax, [..];
// mov [..], eax; mov eax, ebx; ja`). The check bounds eax: the count is 8. A write of the
// checked register that is not a copy ends the search.
func TestSwitchCountFollowsCopyBeforeJcc(t *testing.T) {
	dec := func(b ...byte) []Instruction {
		var w []Instruction
		for pc := 0; pc < len(b); {
			inst, err := x86asm.Decode(b[pc:], 64)
			if err != nil {
				t.Fatal(err)
			}
			w = append(w, Instruction{Address: uint64(0x1000 + pc), Inst: inst})
			pc += inst.Len
		}
		return w
	}
	w := dec(0x83, 0xfb, 0x07, // cmp ebx, 7
		0x8b, 0x07, // mov eax, [rdi]
		0x89, 0x07, // mov [rdi], eax
		0x89, 0xd8, // mov eax, ebx
		0x77, 0x00) // ja
	if n, killed, _ := switchTableCountKill(w, x86asm.RAX); n != 8 || killed {
		t.Errorf("count %d killed %v, want 8 from the check on ebx", n, killed)
	}
	w = dec(0x83, 0xfb, 0x07, // cmp ebx, 7
		0x8b, 0x1f, // mov ebx, [rdi]  (not the value checked)
		0x89, 0xd8, // mov eax, ebx
		0x77, 0x00) // ja
	if n, _, _ := switchTableCountKill(w, x86asm.RAX); n != 0 {
		t.Errorf("count %d after ebx was reloaded, want none", n)
	}
}

// TestSwitchCountSearchesPredecessorsForTheCopiedRegister: the index is copied (`mov eax, ebx`)
// after the block that checks ebx; the predecessor search must look for ebx there. The table
// has 8 entries; the word after it is not one.
func TestSwitchCountSearchesPredecessorsForTheCopiedRegister(t *testing.T) {
	a := &switchAsm{img: make([]byte, 0x400), pc: 0x100}
	entry := a.put(0x83, 0xfb, 0x07) // cmp ebx, 7
	jaDef := a.jcc8(0x77)            // ja def
	a.put(0x89, 0xd8)                // mov eax, ebx
	const table = 0x300
	jmp := a.dispatch(table)
	c0 := a.put(0xc3)
	c7 := a.put(0xc3)
	extra := a.put(0xc3)
	def := a.put(0xc3)
	a.patch8(jaDef, def)
	a.table(table, c0, c0, c0, c0, c0, c0, c0, c7, extra)

	got := analyzeSwitch(t, a.img, entry, jmp)
	if !got[c0] || !got[c7] || got[extra] || len(got) != 2 {
		t.Errorf("targets %v, want 0x%x and 0x%x only (count 8 from the check on ebx)", got, c0, c7)
	}
}

// TestSwitchCountFromJbTakenEdge: `cmp ecx, 5; jb switch; jmp def` bounds the index on the jb's
// taken edge. The table has 5 entries; the word after it is not one.
func TestSwitchCountFromJbTakenEdge(t *testing.T) {
	a := &switchAsm{img: make([]byte, 0x400), pc: 0x100}
	entry := a.put(0x89, 0xc8) // mov eax, ecx
	a.put(0x83, 0xf9, 0x05)    // cmp ecx, 5
	jb := a.jcc8(0x72)         // jb switch
	jmpDef := a.jcc8(0xeb)     // jmp def
	const table = 0x300
	sw := a.pc
	a.patch8(jb, sw)
	jmp := a.dispatch(table)
	c0 := a.put(0xc3)
	c1 := a.put(0xc3)
	extra := a.put(0xc3)
	def := a.put(0xc3)
	a.patch8(jmpDef, def)
	a.table(table, c0, c1, c0, c1, c0, extra)

	got := analyzeSwitch(t, a.img, entry, jmp)
	if !got[c0] || !got[c1] || got[extra] || len(got) != 2 {
		t.Errorf("targets %v, want 0x%x and 0x%x only (count 5 from jb)", got, c0, c1)
	}
}

// TestSwitchCountLargestOverPaths: two paths reach the switch, bounded at 4 and at 7; the
// table has 7 entries.
func TestSwitchCountLargestOverPaths(t *testing.T) {
	a := &switchAsm{img: make([]byte, 0x400), pc: 0x100}
	entry := a.put(0x85, 0xd2) // test edx, edx
	je := a.jcc8(0x74)         // je other
	a.put(0x83, 0xf8, 0x03)    // cmp eax, 3
	ja1 := a.jcc8(0x77)        // ja def
	jmpSw := a.jcc8(0xeb)      // jmp switch
	other := a.put(0x83, 0xf8, 0x06)
	a.patch8(je, other)
	ja2 := a.jcc8(0x77) // ja def (falls into switch)
	sw := a.pc
	a.patch8(jmpSw, sw)
	const table = 0x300
	jmp := a.dispatch(table)
	c0 := a.put(0xc3)
	c6 := a.put(0xc3)
	extra := a.put(0xc3)
	def := a.put(0xc3)
	a.patch8(ja1, def)
	a.patch8(ja2, def)
	a.table(table, c0, c0, c0, c0, c0, c0, c6, extra)

	got := analyzeSwitch(t, a.img, entry, jmp)
	if !got[c0] || !got[c6] || got[extra] || len(got) != 2 {
		t.Errorf("targets %v, want 0x%x and 0x%x only (count 7, the larger bound)", got, c0, c6)
	}
}

// TestSwitchCountUnprovenWhenIndexWrittenOnOnePath: one path checks the index, the other loads
// it from memory without a check. The length is unproven, so the table is scanned (bounded by
// the scan's own rules) instead of being cut at the checked path's 4.
func TestSwitchCountUnprovenWhenIndexWrittenOnOnePath(t *testing.T) {
	a := &switchAsm{img: make([]byte, 0x400), pc: 0x100}
	entry := a.put(0x85, 0xd2) // test edx, edx
	je := a.jcc8(0x74)         // je other
	a.put(0x83, 0xf8, 0x03)    // cmp eax, 3
	ja1 := a.jcc8(0x77)        // ja def
	jmpSw := a.jcc8(0xeb)      // jmp switch
	other := a.put(0x8b, 0x06) // mov eax, [rsi]
	a.patch8(je, other)
	sw := a.pc
	a.patch8(jmpSw, sw)
	const table = 0x300
	jmp := a.dispatch(table)
	c0 := a.put(0xc3)
	c4 := a.put(0xc3)
	def := a.put(0xc3)
	a.patch8(ja1, def)
	a.table(table, c0, c0, c0, c0, c4)

	got := analyzeSwitch(t, a.img, entry, jmp)
	if !got[c0] || !got[c4] {
		t.Errorf("targets %v, want 0x%x and 0x%x (the count of one path is not proven)", got, c0, c4)
	}
}
