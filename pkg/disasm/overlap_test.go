package disasm

import "testing"

// overlapImage is a function whose branch target lies inside another instruction, so the
// decoder sees two overlapping instruction streams that join again at 0x49:
//
//	0x40: test edi, edi
//	0x42: je 0x47
//	0x44: mov eax, 0xc0ff2211   (5 bytes; its immediate holds ff c0 at 0x47)
//	0x47: inc eax               (ff c0, reached only by the je)
//	0x49: ret
//
// In address order 0x47 follows 0x44, but the mov falls through to 0x49, not to the inc.
func overlapImage() []byte {
	img := make([]byte, 0x100)
	copy(img[0x40:], []byte{
		0x85, 0xff, // test edi, edi
		0x74, 0x03, // je 0x47
		0xb8, 0x11, 0x22, 0xff, 0xc0, // mov eax, 0xc0ff2211
		0xc3, // ret
	})
	return img
}

// A block must end where its last instruction ends, and its fall-through address must start a
// block, also when an overlapping instruction stream puts another instruction in between in
// address order.
func TestOverlappingInstructionsSplitBlocks(t *testing.T) {
	d, err := NewDisassembler(imageELF(overlapImage(), nil))
	if err != nil {
		t.Fatal(err)
	}
	fn, _, err := d.DisasmFunction(0x40)
	if err != nil {
		t.Fatal(err)
	}
	var movBlock *BasicBlock
	for _, b := range fn.Blocks {
		for _, in := range b.Insts {
			if in.Address == 0x44 {
				movBlock = b
			}
		}
	}
	if movBlock == nil {
		t.Fatal("mov at 0x44 not decoded")
	}
	last := movBlock.Insts[len(movBlock.Insts)-1]
	if last.Address != 0x44 {
		t.Fatalf("block of the mov runs on into 0x%x (%s)", last.Address, last.Inst)
	}
	if movBlock.EndAddr != 0x49 {
		t.Fatalf("block of the mov ends at 0x%x, want 0x49", movBlock.EndAddr)
	}
	if fn.Blocks[0x49] == nil {
		t.Fatal("fall-through target 0x49 is not a block start")
	}
	if b := fn.Blocks[0x47]; b == nil || b.EndAddr != 0x49 || len(b.Insts) != 1 {
		t.Fatalf("inc block at 0x47 = %+v, want one instruction ending at 0x49", b)
	}
	succs, unknown := blockSuccessors(fn, movBlock, nil)
	if unknown || len(succs) != 1 || succs[0] != 0x49 {
		t.Fatalf("successors of the mov block = %x (unknown %v), want [49]", succs, unknown)
	}
}
