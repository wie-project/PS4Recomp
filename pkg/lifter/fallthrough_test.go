package lifter

import (
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

// FallThrough continues a block whose next emitted block is not its successor, with the same
// no-fall-through set as disasm.blockSuccessors (INT 0x41 is a no-op and falls through).
func TestFallThrough(t *testing.T) {
	fn := &disasm.Function{EntryAddr: 0x1000, Blocks: map[uint64]*disasm.BasicBlock{0x1010: {StartAddr: 0x1010}}}
	l := NewArm64Lifter(map[uint64]bool{0x2000: true})
	inst := func(addr uint64, code ...byte) disasm.Instruction {
		in, err := x86asm.Decode(code, 64)
		if err != nil {
			t.Fatal(err)
		}
		return disasm.Instruction{Address: addr, Inst: in}
	}
	for _, c := range []struct {
		name    string
		last    disasm.Instruction
		next    uint64
		hasNext bool
		want    string // substring of the joined output, "" for no code
	}{
		{"next block is the successor", inst(0x100f, 0x90), 0x1010, true, ""},
		{"local successor behind another block", inst(0x100f, 0x90), 0x100c, true, "b L_fn_0x1000_loc_0x1010"},
		{"successor is a known function", inst(0x1fff, 0x90), 0, false, "b _fn_0x2000"},
		{"successor unknown", inst(0x2fff, 0x90), 0, false, "b _recomp_indirect_jump"},
		{"INT 0x41 falls through", inst(0x100e, 0xcd, 0x41), 0x100c, true, "b L_fn_0x1000_loc_0x1010"},
		{"INT 3 does not", inst(0x100e, 0xcd, 0x03), 0x100c, true, ""},
		{"HLT does not", inst(0x100f, 0xf4), 0x100c, true, ""},
		{"RET does not", inst(0x100f, 0xc3), 0x100c, true, ""},
		{"UD2 does not", inst(0x100e, 0x0f, 0x0b), 0x100c, true, ""},
		{"JMP does not", inst(0x100e, 0xeb, 0x00), 0x100c, true, ""},
	} {
		got := strings.Join(l.FallThrough(fn, c.last, c.next, c.hasNext), "\n")
		if c.want == "" && got != "" || c.want != "" && !strings.Contains(got, c.want) {
			t.Errorf("%s: got %q, want %q", c.name, got, c.want)
		}
	}
}
