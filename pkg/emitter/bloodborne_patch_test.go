package emitter

import (
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"

	"golang.org/x/arch/x86/x86asm"
)

func TestBloodborne109PatchDetectionAndLowering(t *testing.T) {
	if isBloodborne109(nil) {
		t.Fatal("nil ELF matched Bloodborne v1.09")
	}

	img := make([]byte, 0x2035000)
	for _, p := range bloodborne109Patches {
		copy(img[p.addr:], p.orig)
	}
	loaded := &elfloader.LoadedELF{MemoryImage: img}
	if !isBloodborne109(loaded) {
		t.Fatal("expected synthetic Bloodborne v1.09 image to match")
	}

	// Mutating one byte must disable the patch so unrelated games are never touched.
	img[0x2034608] ^= 0xff
	if isBloodborne109(loaded) {
		t.Fatal("corrupted image should not match Bloodborne v1.09")
	}
	img[0x2034608] ^= 0xff

	// Verify byte-patched instructions decode cleanly with preserved length.
	for _, p := range bloodborne109Patches {
		origDec, err := x86asm.Decode(p.orig, 64)
		if err != nil {
			t.Fatalf("decode orig at 0x%x: %v", p.addr, err)
		}
		in := disasm.Instruction{Address: p.addr, Inst: origDec}
		if p.addr == 0x2018e36 {
			lines, ok := bloodborneDynamicDeltaLines(in.Address)
			if !ok || len(lines) == 0 {
				t.Fatalf("expected dynamic frame-delta lines at 0x%x", p.addr)
			}
			continue
		}
		out := bloodbornePatchInst(in)
		if out.Inst.Len != origDec.Len {
			t.Fatalf("0x%x: patched instruction length %d != original %d", p.addr, out.Inst.Len, origDec.Len)
		}
	}
}
