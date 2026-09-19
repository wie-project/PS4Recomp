package lifter

import (
	"fmt"
	"strings"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

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

func setccCondition(op x86asm.Op) (string, error) {
	switch op {
	case x86asm.SETE:
		return "ctx->zf", nil
	case x86asm.SETNE:
		return "!ctx->zf", nil
	case x86asm.SETB:
		return "ctx->cf", nil
	case x86asm.SETAE:
		return "!ctx->cf", nil
	case x86asm.SETBE:
		return "(ctx->cf || ctx->zf)", nil
	case x86asm.SETA:
		return "(!ctx->cf && !ctx->zf)", nil
	case x86asm.SETS:
		return "ctx->sf", nil
	case x86asm.SETNS:
		return "!ctx->sf", nil
	case x86asm.SETO:
		return "ctx->of", nil
	case x86asm.SETNO:
		return "!ctx->of", nil
	case x86asm.SETL:
		return "(ctx->sf != ctx->of)", nil
	case x86asm.SETGE:
		return "(ctx->sf == ctx->of)", nil
	case x86asm.SETLE:
		return "(ctx->zf || (ctx->sf != ctx->of))", nil
	case x86asm.SETG:
		return "(!ctx->zf && (ctx->sf == ctx->of))", nil
	case x86asm.SETP:
		return "ctx->pf", nil
	case x86asm.SETNP:
		return "!ctx->pf", nil
	default:
		return "", fmt.Errorf("unknown setcc op: %v", op)
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

func cmovCondition(op x86asm.Op) (string, error) {
	switch op {
	case x86asm.CMOVE:
		return "ctx->zf", nil
	case x86asm.CMOVNE:
		return "!ctx->zf", nil
	case x86asm.CMOVB:
		return "ctx->cf", nil
	case x86asm.CMOVAE:
		return "!ctx->cf", nil
	case x86asm.CMOVBE:
		return "(ctx->cf || ctx->zf)", nil
	case x86asm.CMOVA:
		return "(!ctx->cf && !ctx->zf)", nil
	case x86asm.CMOVS:
		return "ctx->sf", nil
	case x86asm.CMOVNS:
		return "!ctx->sf", nil
	case x86asm.CMOVO:
		return "ctx->of", nil
	case x86asm.CMOVNO:
		return "!ctx->of", nil
	case x86asm.CMOVL:
		return "ctx->sf != ctx->of", nil
	case x86asm.CMOVGE:
		return "ctx->sf == ctx->of", nil
	case x86asm.CMOVLE:
		return "ctx->zf || ctx->sf != ctx->of", nil
	case x86asm.CMOVG:
		return "!ctx->zf && ctx->sf == ctx->of", nil
	case x86asm.CMOVP:
		return "ctx->pf", nil
	case x86asm.CMOVNP:
		return "!ctx->pf", nil
	default:
		return "", fmt.Errorf("unknown cmovcc op: %v", op)
	}
}

// isContextRestoreFunction returns true if the function performs low-level context restoration
// (such as libunwind's Registers_x86_64::jumpto, unw_resume, or longjmp) where control is
// transferred to an arbitrary landing pad or frame without returning through the normal caller chain.
func isContextRestoreFunction(fn *disasm.Function) bool {
	if fn == nil {
		return false
	}

	// 1. Symbol matching for standard ABI context-restore / unwinding entry points
	knownContextRestorers := []string{
		"Registers_x86_646jumptoEv",
		"unw_resume",
		"_Unwind_Resume",
		"longjmp",
		"siglongjmp",
		"setcontext",
	}
	for _, sym := range knownContextRestorers {
		if strings.Contains(fn.Name, sym) {
			return true
		}
	}

	// 2. Architectural pattern fallback for stripped or customized binaries:
	// Registers_x86_64::jumpto restores RSP from [RDI+0x38], pops RDI, and RETs to the target IP.
	hasMovRsp := false
	hasPopRdi := false
	for _, b := range fn.Blocks {
		for _, inst := range b.Insts {
			if inst.Inst.Op == x86asm.MOV {
				if reg, ok := inst.Inst.Args[0].(x86asm.Reg); ok && reg == x86asm.RSP {
					if mem, ok := inst.Inst.Args[1].(x86asm.Mem); ok && mem.Base == x86asm.RDI && mem.Disp == 0x38 {
						hasMovRsp = true
					}
				}
			}
			if hasMovRsp && inst.Inst.Op == x86asm.POP {
				if reg, ok := inst.Inst.Args[0].(x86asm.Reg); ok && reg == x86asm.RDI {
					hasPopRdi = true
				}
			}
			if hasPopRdi && inst.Inst.Op == x86asm.RET {
				return true
			}
		}
	}
	return false
}
