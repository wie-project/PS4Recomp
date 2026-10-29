package disasm

import (
	"fmt"

	"golang.org/x/arch/x86/x86asm"
)

// AMD SSE4A bit-field instructions. x86asm has no op for them: given 66/F2 0F 78/79 it returns
// the mandatory prefix alone as a one-byte instruction of op 0 (and without the prefix the bytes
// are VMREAD/VMWRITE, which it does not know either). The values lie past every x86asm op.
//
// Operands, in Intel order (the immediates are the raw bytes; bits 5:0 are the length, 0 meaning
// 64, and the bit index):
//
//	EXTRQ   xmm1, xmm2              66 0F 79 /r       (length xmm2[5:0], index xmm2[13:8])
//	EXTRQ   xmm1, imm8 len, imm8 idx 66 0F 78 /0 ib ib
//	INSERTQ xmm1, xmm2              F2 0F 79 /r       (length xmm2[69:64], index xmm2[77:72])
//	INSERTQ xmm1, xmm2, imm8 len, imm8 idx F2 0F 78 /r ib ib
//
// The register forms take xmm1 from ModRM.reg and xmm2 from ModRM.rm; EXTRQ-immediate takes its
// only register from ModRM.rm. There are no memory forms.
const (
	OpEXTRQ x86asm.Op = 1<<16 + iota
	OpINSERTQ
)

// Decode decodes one 64-bit instruction at the start of b: x86asm.Decode, plus the SSE4A
// instructions it does not know.
func Decode(b []byte) (x86asm.Inst, error) {
	if inst, ok := decodeSSE4A(b); ok {
		return inst, nil
	}
	return x86asm.Decode(b, 64)
}

// decodeSSE4A decodes EXTRQ and INSERTQ: the mandatory 66/F2 prefix, an optional REX, 0F 78/79
// and a register ModRM, then two immediate bytes for 0F 78.
func decodeSSE4A(b []byte) (x86asm.Inst, bool) {
	if len(b) < 4 || (b[0] != 0x66 && b[0] != 0xf2) {
		return x86asm.Inst{}, false
	}
	i := 1
	var rex byte
	if b[i]&0xf0 == 0x40 {
		rex = b[i]
		i++
	}
	if len(b) < i+3 || b[i] != 0x0f || (b[i+1] != 0x78 && b[i+1] != 0x79) {
		return x86asm.Inst{}, false
	}
	imm := b[i+1] == 0x78
	modrm := b[i+2]
	if modrm>>6 != 3 {
		return x86asm.Inst{}, false
	}
	n := i + 3
	if imm {
		n += 2
		if len(b) < n {
			return x86asm.Inst{}, false
		}
	}
	reg := x86asm.X0 + x86asm.Reg(modrm>>3&7)
	if rex&4 != 0 {
		reg += 8
	}
	rm := x86asm.X0 + x86asm.Reg(modrm&7)
	if rex&1 != 0 {
		rm += 8
	}
	inst := x86asm.Inst{Mode: 64, AddrSize: 64, DataSize: 64, Len: n}
	switch {
	case b[0] == 0x66 && imm:
		if modrm>>3&7 != 0 {
			return x86asm.Inst{}, false
		}
		inst.Op = OpEXTRQ
		inst.Args[0], inst.Args[1], inst.Args[2] = rm, x86asm.Imm(b[n-2]), x86asm.Imm(b[n-1])
	case b[0] == 0x66:
		inst.Op = OpEXTRQ
		inst.Args[0], inst.Args[1] = reg, rm
	case imm:
		inst.Op = OpINSERTQ
		inst.Args[0], inst.Args[1], inst.Args[2], inst.Args[3] = reg, rm, x86asm.Imm(b[n-2]), x86asm.Imm(b[n-1])
	default:
		inst.Op = OpINSERTQ
		inst.Args[0], inst.Args[1] = reg, rm
	}
	return inst, true
}

// InstString is inst.String() with the names of the ops x86asm does not know.
func InstString(inst x86asm.Inst) string {
	var name string
	switch inst.Op {
	case OpEXTRQ:
		name = "EXTRQ"
	case OpINSERTQ:
		name = "INSERTQ"
	default:
		return inst.String()
	}
	s := name
	for i, a := range inst.Args {
		if a == nil {
			break
		}
		if i == 0 {
			s += " "
		} else {
			s += ", "
		}
		if v, ok := a.(x86asm.Imm); ok {
			s += fmt.Sprintf("%#x", int64(v))
		} else {
			s += a.String()
		}
	}
	return s
}
