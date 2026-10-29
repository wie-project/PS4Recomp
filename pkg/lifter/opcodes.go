package lifter

import (
	"ps4-recomp/pkg/disasm"

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

// IsOpcodeSupported returns true if the x86 opcode is supported by the recompiler.
func IsOpcodeSupported(op x86asm.Op) bool {
	switch op {
	case x86asm.NOP, x86asm.MOV, x86asm.CBW, x86asm.CWDE, x86asm.CDQE, x86asm.CDQ, x86asm.CQO,
		x86asm.ROL, x86asm.ROR,
		x86asm.MOVZX, x86asm.MOVSX, x86asm.MOVSXD, x86asm.LEA,
		x86asm.PUSH, x86asm.POP, x86asm.ADD, x86asm.SUB, x86asm.AND,
		x86asm.OR, x86asm.XOR, x86asm.CMP, x86asm.TEST, x86asm.ADC,
		x86asm.BSF, x86asm.BSR, x86asm.INC, x86asm.DEC, x86asm.NEG, x86asm.NOT,
		x86asm.SHL, x86asm.SHR, x86asm.SAR, x86asm.XCHG, x86asm.XADD,
		x86asm.CMPXCHG, x86asm.IMUL, x86asm.MUL, x86asm.CALL, x86asm.RET,
		x86asm.JMP, x86asm.UD2, x86asm.INT, x86asm.FWAIT,
		disasm.OpEXTRQ, disasm.OpINSERTQ,
		x86asm.MOVUPS, x86asm.MOVAPS, x86asm.MOVDQU, x86asm.MOVDQA,
		x86asm.MOVUPD, x86asm.MOVAPD,
		x86asm.PXOR, x86asm.XORPS, x86asm.XORPD, x86asm.POR, x86asm.ORPD, x86asm.ORPS,
		x86asm.PAND, x86asm.ANDPD, x86asm.ANDPS,
		x86asm.MOVD, x86asm.MOVQ, x86asm.MOVSS, x86asm.PSHUFD, x86asm.PSHUFLW,
		x86asm.PSLLD, x86asm.PSRLD, x86asm.SBB,
		x86asm.BT, x86asm.BTR, x86asm.BTS, x86asm.BTC,
		x86asm.BSWAP, x86asm.POPCNT, x86asm.LZCNT, x86asm.TZCNT,
		x86asm.SHLD, x86asm.SHRD,
		x86asm.CPUID, x86asm.PAUSE, x86asm.EMMS, x86asm.PUSHFQ, x86asm.POPFQ, x86asm.XGETBV,
		x86asm.PREFETCHT0, x86asm.PREFETCHNTA, x86asm.PREFETCHT1, x86asm.PREFETCHT2, x86asm.PREFETCHW,
		x86asm.DIV, x86asm.IDIV,
		x86asm.HLT, x86asm.MOVSD_XMM, x86asm.MOVSD, x86asm.PANDN,
		x86asm.PMULUDQ, x86asm.PCMPGTD, x86asm.PUNPCKLDQ,
		x86asm.PCMPEQD, x86asm.PCMPEQB, x86asm.PCMPEQW,
		x86asm.PADDD, x86asm.PADDB, x86asm.PADDW, x86asm.PADDQ,
		x86asm.PSUBB, x86asm.PSUBW, x86asm.PSUBD, x86asm.PSUBQ,
		x86asm.PSUBUSB, x86asm.PADDSW, x86asm.PADDSB, x86asm.PMAXUB, x86asm.PSUBSB,
		x86asm.PMADDUBSW, x86asm.PADDUSB, x86asm.PCMPGTB, x86asm.VPABSB,
		x86asm.PMULLW, x86asm.PMULHW, x86asm.PMADDWD,
		x86asm.PACKSSDW, x86asm.PACKUSWB, x86asm.PACKSSWB,
		x86asm.PINSRB, x86asm.PINSRW, x86asm.PINSRD, x86asm.PINSRQ,
		x86asm.PSLLW, x86asm.PSRLW, x86asm.PSRAW, x86asm.PSRAD,
		x86asm.PSLLQ, x86asm.PSRLQ, x86asm.PSLLDQ, x86asm.PSRLDQ,
		x86asm.PUNPCKLBW, x86asm.PUNPCKLWD, x86asm.UNPCKLPD, x86asm.UNPCKHPD,
		x86asm.PUNPCKHBW, x86asm.PUNPCKHWD, x86asm.PUNPCKHDQ, x86asm.PUNPCKLQDQ, x86asm.PUNPCKHQDQ,
		x86asm.PSHUFHW,
		x86asm.ADDPD, x86asm.MULPD, x86asm.SUBPD, x86asm.DIVPD,
		x86asm.ADDSD, x86asm.MULSD, x86asm.SUBSD, x86asm.DIVSD,
		x86asm.ADDSS, x86asm.MULSS, x86asm.SUBSS, x86asm.DIVSS,
		x86asm.ADDPS, x86asm.MULPS, x86asm.SUBPS, x86asm.DIVPS,
		x86asm.MAXPS, x86asm.MINPS, x86asm.HADDPS, x86asm.HADDPD,
		x86asm.CVTDQ2PS, x86asm.CVTPS2DQ, x86asm.PAVGB, x86asm.PAVGW,
		x86asm.UCOMISD, x86asm.UCOMISS, x86asm.COMISD, x86asm.COMISS,
		x86asm.MINSD, x86asm.MINSS, x86asm.MAXSD, x86asm.MAXSS,
		x86asm.SQRTSS, x86asm.SQRTSD,
		x86asm.MOVHPD, x86asm.MOVLPD, x86asm.MOVLPS, x86asm.MOVHPS,
		x86asm.MOVNTDQ, x86asm.UNPCKLPS, x86asm.UNPCKHPS, x86asm.INSB,
		x86asm.PCMPGTQ, x86asm.CVTPS2PD, x86asm.CVTPD2PS, x86asm.MOVBE,
		// AVX / VEX opcodes
		x86asm.VMOVAPS, x86asm.VMOVUPS, x86asm.VMOVUPD, x86asm.VMOVAPD, x86asm.VMOVDQA, x86asm.VMOVDQU, x86asm.VMOVD, x86asm.VMOVQ, x86asm.VMOVSS, x86asm.VMOVSD,
		x86asm.VMOVNTPS, x86asm.VMOVNTDQ, x86asm.VBROADCASTSS, x86asm.VMOVSLDUP,
		x86asm.VADDSS, x86asm.VADDSD, x86asm.VSUBSS, x86asm.VSUBSD, x86asm.VMULSS, x86asm.VMULSD, x86asm.VDIVSS, x86asm.VDIVSD,
		x86asm.VADDPS, x86asm.VSUBPS, x86asm.VMULPS, x86asm.VDIVPS, x86asm.VMAXPS, x86asm.VMINPS, x86asm.VMAXSS, x86asm.VMINSS, x86asm.VHADDPS, x86asm.VHADDPD,
		x86asm.VXORPS, x86asm.VORPS, x86asm.VANDPS, x86asm.VPXOR, x86asm.VPOR, x86asm.VPAND, x86asm.VPADDW, x86asm.VPSUBW, x86asm.VPMULLW,
		x86asm.VPAVGB, x86asm.VPAVGW,
		x86asm.VPINSRW, x86asm.VPINSRB, x86asm.VPINSRD,
		x86asm.VPSLLW, x86asm.VPSRLW, x86asm.VPSRAW, x86asm.VPSLLD, x86asm.VPSRLD, x86asm.VPSRAD,
		x86asm.VPACKUSWB, x86asm.VPACKSSDW, x86asm.VPACKSSWB,
		x86asm.VPUNPCKLBW, x86asm.VPUNPCKHBW, x86asm.VPUNPCKLWD, x86asm.VPUNPCKHWD,
		x86asm.VPUNPCKLDQ, x86asm.VPUNPCKHDQ, x86asm.VUNPCKLPS, x86asm.VUNPCKHPS,
		x86asm.VCVTDQ2PS, x86asm.VCVTPS2DQ, x86asm.VCVTTPS2DQ,
		x86asm.VPANDN, x86asm.VPMULLD, x86asm.VPMOVZXWD, x86asm.VPMOVSXWD,
		x86asm.VCVTSI2SS, x86asm.VCVTSI2SD, x86asm.VCVTTSS2SI, x86asm.VCVTTSD2SI, x86asm.VCVTSS2SD, x86asm.VCVTSD2SS,
		x86asm.VUCOMISS, x86asm.VUCOMISD, x86asm.VCOMISS, x86asm.VCOMISD, x86asm.VPSHUFHW, x86asm.VPSHUFLW, x86asm.VROUNDSD, x86asm.VROUNDSS,
		x86asm.VSQRTSS, x86asm.VSQRTSD, x86asm.VRSQRTSS, x86asm.VRSQRTPS, x86asm.VMOVDDUP, x86asm.VCVTSS2SI, x86asm.VCVTSD2SI,
		x86asm.VCVTPS2PD, x86asm.VCVTPD2PS, x86asm.VEXTRACTF128, x86asm.VEXTRACTI128, x86asm.VINSERTF128, x86asm.VINSERTI128,
		x86asm.VPERMILPS, x86asm.VPERMILPD, x86asm.VCMPPS, x86asm.VSHUFPS,
		x86asm.VINSERTPS, x86asm.VMOVSHDUP, x86asm.VMOVLPS, x86asm.VMOVMSKPS,
		x86asm.VBLENDPS, x86asm.VBLENDVPS, x86asm.VPBLENDVB, x86asm.SHUFPS, x86asm.SHUFPD,
		x86asm.PCLMULQDQ, x86asm.AESKEYGENASSIST,
		// String operations
		x86asm.MOVSB, x86asm.MOVSW, x86asm.MOVSQ,
		x86asm.STOSB, x86asm.STOSW, x86asm.STOSD, x86asm.STOSQ,
		x86asm.LODSB, x86asm.LODSW, x86asm.LODSD, x86asm.LODSQ,
		x86asm.SCASB, x86asm.SCASW, x86asm.SCASD, x86asm.SCASQ,
		x86asm.CMPSB, x86asm.CMPSW, x86asm.CMPSD, x86asm.CMPSQ,
		// x87 FPU opcodes
		x86asm.FLD, x86asm.FST, x86asm.FSTP, x86asm.FXCH,
		x86asm.FADD, x86asm.FADDP, x86asm.FIADD, x86asm.FSUB, x86asm.FSUBP,
		x86asm.FISUB, x86asm.FMUL, x86asm.FMULP, x86asm.FIMUL,
		x86asm.FDIV, x86asm.FDIVP, x86asm.FUCOMI, x86asm.FUCOMIP,
		x86asm.FLDCW, x86asm.FNSTCW:
		return true
	default:
		return isSetcc(op) || isCmovcc(op) || IsJcc(op)
	}
}
