package disasm

import (
	"testing"

	"golang.org/x/arch/x86/x86asm"
)

// VEX 0F 77 (VZEROUPPER, VZEROALL with VEX.L = 1) is the one VEX opcode without a ModRM byte.
// The decoder used to read the next byte as ModRM: `vzeroupper; ret` came out as one 4-byte
// instruction, the ret was lost and the lifted function ran on past its end (inFAMOUS Second
// Son, eboot 0x894882), and a following byte with mod != 3 pulled in a displacement as well.
func TestDecodeVzeroWithoutModRM(t *testing.T) {
	cases := []struct {
		name string
		b    []byte
		op   x86asm.Op
	}{
		{"vzeroupper; ret", []byte{0xc5, 0xf8, 0x77, 0xc3}, x86asm.VZEROUPPER},
		{"vzeroupper; nop x5", []byte{0xc5, 0xf8, 0x77, 0x90, 0x90, 0x90, 0x90, 0x90}, x86asm.VZEROUPPER},
		{"vzeroall; ret", []byte{0xc5, 0xfc, 0x77, 0xc3}, x86asm.VZEROALL},
		{"vzeroupper (3-byte VEX); ret", []byte{0xc4, 0xe1, 0x78, 0x77, 0xc3}, x86asm.VZEROUPPER},
		{"vzeroupper at the end", []byte{0xc5, 0xf8, 0x77}, x86asm.VZEROUPPER},
	}
	for _, c := range cases {
		inst, err := Decode(c.b)
		if err != nil {
			t.Errorf("%s: %v", c.name, err)
			continue
		}
		want := 3
		if c.b[0] == 0xc4 {
			want = 4
		}
		if inst.Op != c.op || inst.Len != want {
			t.Errorf("%s: %v len %d, want %v len %d", c.name, inst.Op, inst.Len, c.op, want)
		}
		if len(c.b) > want && c.b[want] == 0xc3 {
			next, err := Decode(c.b[want:])
			if err != nil || next.Op != x86asm.RET {
				t.Errorf("%s: next instruction %v (%v), want RET", c.name, next.Op, err)
			}
		}
	}
}
