package lifter

import (
	"golang.org/x/arch/x86/x86asm"
)

// IsJcc returns true if op is a conditional jump.
func IsJcc(op x86asm.Op) bool {
	switch op {
	case x86asm.JA, x86asm.JAE, x86asm.JB, x86asm.JBE,
		x86asm.JCXZ, x86asm.JECXZ, x86asm.JRCXZ,
		x86asm.JE, x86asm.JG, x86asm.JGE,
		x86asm.JL, x86asm.JLE, x86asm.JNE,
		x86asm.JNO, x86asm.JNP, x86asm.JNS,
		x86asm.JO, x86asm.JP, x86asm.JS:
		return true
	default:
		return false
	}
}

func isSetcc(op x86asm.Op) bool {
	switch op {
	case x86asm.SETA, x86asm.SETAE, x86asm.SETB, x86asm.SETBE,
		x86asm.SETE, x86asm.SETG, x86asm.SETGE, x86asm.SETL,
		x86asm.SETLE, x86asm.SETNE, x86asm.SETNO, x86asm.SETNP,
		x86asm.SETNS, x86asm.SETO, x86asm.SETP, x86asm.SETS:
		return true
	default:
		return false
	}
}

func isCmovcc(op x86asm.Op) bool {
	switch op {
	case x86asm.CMOVA, x86asm.CMOVAE, x86asm.CMOVB, x86asm.CMOVBE,
		x86asm.CMOVE, x86asm.CMOVG, x86asm.CMOVGE, x86asm.CMOVL,
		x86asm.CMOVLE, x86asm.CMOVNE, x86asm.CMOVNO, x86asm.CMOVNP,
		x86asm.CMOVNS, x86asm.CMOVO, x86asm.CMOVP, x86asm.CMOVS:
		return true
	default:
		return false
	}
}
