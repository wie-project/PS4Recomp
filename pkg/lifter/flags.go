package lifter

import (
	"fmt"

	"golang.org/x/arch/x86/x86asm"
)

// ConditionExpr returns the C boolean expression testing the flags for a Jcc opcode.
func ConditionExpr(op x86asm.Op) (string, error) {
	switch op {
	case x86asm.JE:
		return "ctx->zf", nil
	case x86asm.JNE:
		return "!ctx->zf", nil
	case x86asm.JB:
		return "ctx->cf", nil
	case x86asm.JAE:
		return "!ctx->cf", nil
	case x86asm.JBE:
		return "(ctx->cf || ctx->zf)", nil
	case x86asm.JA:
		return "(!ctx->cf && !ctx->zf)", nil
	case x86asm.JS:
		return "ctx->sf", nil
	case x86asm.JNS:
		return "!ctx->sf", nil
	case x86asm.JO:
		return "ctx->of", nil
	case x86asm.JNO:
		return "!ctx->of", nil
	case x86asm.JL:
		return "ctx->sf != ctx->of", nil
	case x86asm.JGE:
		return "ctx->sf == ctx->of", nil
	case x86asm.JLE:
		return "ctx->zf || ctx->sf != ctx->of", nil
	case x86asm.JG:
		return "!ctx->zf && ctx->sf == ctx->of", nil
	case x86asm.JP:
		return "ctx->pf", nil
	case x86asm.JNP:
		return "!ctx->pf", nil
	case x86asm.JCXZ:
		return "((uint16_t)ctx->rcx == 0)", nil
	case x86asm.JECXZ:
		return "((uint32_t)ctx->rcx == 0)", nil
	case x86asm.JRCXZ:
		return "(ctx->rcx == 0)", nil
	default:
		return "", fmt.Errorf("unsupported condition op: %v", op)
	}
}

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

