package lifter

import (
	"fmt"
	"strconv"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

// Lifter translates disasm.Instruction into C code.
type Lifter struct {
	knownFuncs map[uint64]bool
}

// NewLifter creates a new instruction lifter.
func NewLifter(knownFuncs map[uint64]bool) *Lifter {
	return &Lifter{
		knownFuncs: knownFuncs,
	}
}

// IsKnownFunc reports whether addr is the entry point of a known function.
func (l *Lifter) IsKnownFunc(addr uint64) bool {
	return l != nil && l.knownFuncs != nil && l.knownFuncs[addr]
}

// IsOpcodeSupported returns true if the lifter implements translation for the given x86 opcode.
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
		x86asm.VUCOMISS, x86asm.VUCOMISD, x86asm.VPSHUFHW, x86asm.VPSHUFLW, x86asm.VROUNDSD, x86asm.VROUNDSS,
		x86asm.VSQRTSS, x86asm.VSQRTSD, x86asm.VRSQRTSS, x86asm.VRSQRTPS, x86asm.VMOVDDUP, x86asm.VCVTSS2SI, x86asm.VCVTSD2SI,
		x86asm.VCVTPS2PD, x86asm.VCVTPD2PS, x86asm.VEXTRACTF128, x86asm.VEXTRACTI128, x86asm.VINSERTF128, x86asm.VINSERTI128,
		x86asm.VPERMILPS, x86asm.VPERMILPD, x86asm.VCMPPS, x86asm.VSHUFPS,
		x86asm.VINSERTPS, x86asm.VMOVSHDUP, x86asm.VMOVLPS, x86asm.VMOVMSKPS,
		x86asm.VBLENDPS, x86asm.VBLENDVPS, x86asm.VPBLENDVB, x86asm.SHUFPS, x86asm.SHUFPD,
		x86asm.VEXTRACTPS, x86asm.VCMPSS, x86asm.VCMPSD, x86asm.VSHUFPD,
		x86asm.VADDPD, x86asm.VSUBPD, x86asm.VMULPD, x86asm.VDIVPD, x86asm.VMINPD, x86asm.VMAXPD,
		x86asm.VMINSD, x86asm.VMAXSD, x86asm.VANDPD, x86asm.VXORPD, x86asm.VORPD, x86asm.VANDNPS, x86asm.VANDNPD,
		x86asm.VPADDB, x86asm.VPADDD, x86asm.VPADDQ, x86asm.VPSUBB, x86asm.VPSUBD, x86asm.VPSUBQ,
		x86asm.VPCMPEQB, x86asm.VPCMPEQW, x86asm.VPCMPEQD, x86asm.VPCMPGTD, x86asm.VPCMPGTQ,
		x86asm.VPUNPCKLQDQ, x86asm.VPUNPCKHQDQ, x86asm.VUNPCKLPD, x86asm.VUNPCKHPD,
		x86asm.VPSHUFD, x86asm.VPSHUFB, x86asm.VPALIGNR, x86asm.VPSLLQ, x86asm.VPSRLQ, x86asm.VPSLLDQ, x86asm.VPSRLDQ,
		x86asm.VPINSRQ, x86asm.VPEXTRB, x86asm.VPEXTRW, x86asm.VPEXTRD, x86asm.VPEXTRQ,
		x86asm.VPMINSD, x86asm.VPMAXSD, x86asm.VPMINUD, x86asm.VPMAXUD, x86asm.VPMINSB, x86asm.VPMAXSB,
		x86asm.VSQRTPS, x86asm.VSQRTPD, x86asm.VMOVLHPS, x86asm.VMOVHLPS, x86asm.VMOVHPD, x86asm.VMOVHPS,
		x86asm.VROUNDPS, x86asm.VPTEST,
		x86asm.PSHUFB, x86asm.PALIGNR, x86asm.PEXTRB, x86asm.PEXTRW, x86asm.PEXTRD, x86asm.PEXTRQ,
		x86asm.PMINSD, x86asm.PMAXSD, x86asm.PMINUD, x86asm.PMAXUD, x86asm.PMINSB, x86asm.PMAXSB,
		x86asm.MOVLHPS, x86asm.MOVHLPS, x86asm.ANDNPS, x86asm.ANDNPD, x86asm.ROUNDPS, x86asm.PTEST,
		x86asm.MOVNTI, x86asm.SQRTPS, x86asm.SQRTPD, x86asm.MOVSLDUP, x86asm.CWD,
		x86asm.LFENCE, x86asm.MFENCE, x86asm.SFENCE,
		x86asm.CLC, x86asm.STC, x86asm.CLD, x86asm.STD,
		x86asm.MOVSB, x86asm.MOVSW, x86asm.MOVSQ,
		x86asm.STOSB, x86asm.STOSW, x86asm.STOSD, x86asm.STOSQ,
		x86asm.SCASB, x86asm.SCASW, x86asm.SCASD, x86asm.SCASQ,
		x86asm.LODSB, x86asm.LODSW, x86asm.LODSD, x86asm.LODSQ,
		x86asm.CMPSB, x86asm.CMPSW, x86asm.CMPSD, x86asm.CMPSQ,
		x86asm.LEAVE, x86asm.ENTER, x86asm.SAHF, x86asm.LAHF,
		x86asm.CVTSI2SD, x86asm.CVTSI2SS, x86asm.CVTSS2SD, x86asm.CVTSD2SS,
		x86asm.CVTTSD2SI, x86asm.CVTTSS2SI, x86asm.CVTSD2SI, x86asm.CVTSS2SI,
		x86asm.VERW,
		x86asm.FLDZ, x86asm.FLD1, x86asm.FABS, x86asm.FLD, x86asm.FILD, x86asm.FST, x86asm.FSTP,
		x86asm.FIST, x86asm.FISTP, x86asm.FXCH, x86asm.FCHS,
		x86asm.FADD, x86asm.FADDP, x86asm.FIADD, x86asm.FSUB, x86asm.FSUBP, x86asm.FSUBRP,
		x86asm.FISUB, x86asm.FMUL, x86asm.FMULP, x86asm.FIMUL,
		x86asm.FDIV, x86asm.FDIVP, x86asm.FDIVR, x86asm.FIDIV,
		x86asm.FUCOMI, x86asm.FUCOMIP, x86asm.FCOMI, x86asm.FCOMIP,
		x86asm.FLDCW, x86asm.FNSTCW,
		x86asm.FISTTP, x86asm.FCOMP, x86asm.FCOM, x86asm.FICOM, x86asm.FICOMP,
		x86asm.FCMOVB, x86asm.FCMOVE, x86asm.FCMOVNB, x86asm.FCMOVU,
		x86asm.FIDIVR, x86asm.FDIVRP, x86asm.FSUBR, x86asm.FLDL2T, x86asm.FBSTP,
		x86asm.FNOP, x86asm.FNSTENV, x86asm.FNSTSW, x86asm.FYL2XP1,
		// Control flow, ALU, system
		x86asm.LOOP, x86asm.LOOPE, x86asm.LOOPNE, x86asm.LRET, x86asm.LCALL, x86asm.LJMP,
		x86asm.RCL, x86asm.RCR, x86asm.CMC, x86asm.XLATB, x86asm.RDTSC,
		x86asm.UD0, x86asm.ICEBP, x86asm.XABORT, x86asm.SLDT, x86asm.LAR,
		// AVX / SIMD additions
		x86asm.VMOVLPD, x86asm.VPBLENDW, x86asm.VBLENDPD, x86asm.VBLENDVPD,
		x86asm.VMASKMOVPS, x86asm.VMASKMOVPD, x86asm.VBROADCASTSD, x86asm.VBROADCASTF128,
		x86asm.VMOVMSKPD, x86asm.VPERM2F128, x86asm.VROUNDPD, x86asm.VDPPS,
		x86asm.VRCPPS, x86asm.VRCPSS, x86asm.VPHADDD, x86asm.VPHADDW,
		x86asm.VHSUBPS, x86asm.VHSUBPD, x86asm.VPHSUBD,
		x86asm.VPMAXUW, x86asm.VPMAXSW, x86asm.VPMINSW, x86asm.VPMINUB, x86asm.VPMAXUB, x86asm.VPMINUW,
		x86asm.VPMADDWD, x86asm.VPMADDUBSW, x86asm.VPMOVMSKB,
		x86asm.VPCMPEQQ, x86asm.VPCMPGTB, x86asm.VPCMPGTW,
		x86asm.VPMULUDQ, x86asm.VPMULDQ, x86asm.VPMULHW, x86asm.VPMULHUW,
		x86asm.VPACKUSDW, x86asm.VPSADBW, x86asm.VPABSD, x86asm.VPABSW,
		x86asm.VPHMINPOSUW, x86asm.VPADDUSB, x86asm.VPADDSW,
		x86asm.VPSUBSW, x86asm.VPSUBUSB, x86asm.VPSUBUSW,
		x86asm.VADDSUBPS, x86asm.VADDSUBPD, x86asm.VCMPPD,
		x86asm.VCVTTPD2DQ, x86asm.VCVTDQ2PD, x86asm.VCVTPH2PS, x86asm.VCVTPS2PH,
		x86asm.VSTMXCSR, x86asm.VLDMXCSR,
		x86asm.VPMOVSXDQ, x86asm.VPMOVZXDQ, x86asm.VPMOVSXBD, x86asm.VPMOVZXBD,
		x86asm.VPMOVSXBW, x86asm.VPMOVZXBW, x86asm.VPMOVSXWQ, x86asm.VPMOVZXWQ,
		x86asm.VPMOVSXBQ, x86asm.VPMOVZXBQ,
		// BMI1 / BMI2 opcodes
		x86asm.ANDN, x86asm.BEXTR, x86asm.BLSI, x86asm.BLSMSK, x86asm.BLSR, x86asm.BZHI,
		x86asm.MULX, x86asm.RORX, x86asm.SARX, x86asm.SHLX, x86asm.SHRX,
		x86asm.VPCMPISTRI, x86asm.PCMPISTRI:
		return true
	default:
		return isSetcc(op) || isCmovcc(op) || IsJcc(op)
	}
}

// LiftInstruction lifts a single instruction into C statements.
func (l *Lifter) LiftInstruction(inst disasm.Instruction, nextPC uint64, fn *disasm.Function) ([]string, error) {
	return l.LiftInstructionToBuf(inst, nextPC, fn, nil)
}

// LiftInstructionToBuf lifts a single instruction into C statements, appending to dst to reuse storage.
func (l *Lifter) LiftInstructionToBuf(inst disasm.Instruction, nextPC uint64, fn *disasm.Function, dst []string) ([]string, error) {
	pc := inst.Address
	op := inst.Inst.Op
	args := inst.Inst.Args

	lines := dst
	lines = append(lines, "    /* 0x"+strconv.FormatUint(pc, 16)+": "+inst.Inst.String()+" */")

	// Determine effective memory/operand size from instruction
	defMemSz := inst.Inst.MemBytes
	if defMemSz == 0 && inst.Inst.DataSize > 0 {
		defMemSz = inst.Inst.DataSize / 8
	}
	if defMemSz == 0 {
		defMemSz = 8
	}

	switch op {
	case x86asm.NOP:
		// No-op

	case x86asm.MOV:
		code, err := l.liftMov(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CBW:
		lines = append(lines, "    ctx->rax = (ctx->rax & ~0xFFFFULL) | (uint16_t)(int16_t)(int8_t)ctx->rax;")

	case x86asm.CWDE:
		lines = append(lines, "    ctx->rax = (uint32_t)(int32_t)(int16_t)ctx->rax;")

	case x86asm.CDQE:
		lines = append(lines, "    ctx->rax = (uint64_t)(int64_t)(int32_t)ctx->rax;")

	case x86asm.CWD:
		lines = append(lines, l.liftCwd()...)

	case x86asm.CDQ:
		lines = append(lines, "    ctx->rdx = (uint64_t)(uint32_t)((int32_t)ctx->rax < 0 ? -1 : 0);")

	case x86asm.CQO:
		lines = append(lines, "    ctx->rdx = ((int64_t)ctx->rax < 0) ? 0xFFFFFFFFFFFFFFFFULL : 0;")

	case x86asm.ROL, x86asm.ROR:
		code, err := l.liftRotate(op, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MOVZX, x86asm.MOVSX, x86asm.MOVSXD:
		code, err := l.liftExtend(op, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.LEA:
		dstReg, ok := args[0].(x86asm.Reg)
		if !ok {
			return nil, fmt.Errorf("0x%x: LEA destination must be a register", pc)
		}
		srcMem, ok := args[1].(x86asm.Mem)
		if !ok {
			return nil, fmt.Errorf("0x%x: LEA source must be a memory operand", pc)
		}
		addrExpr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		stmt, err := GetRegWriteStmt(dstReg, addrExpr)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, "    "+stmt)

	case x86asm.PUSH:
		valExpr, sz, err := l.getOperandRead(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(
			lines,
			"    ctx->rsp -= 8;",
			fmt.Sprintf("    MEM_U64(ctx->rsp) = (uint64_t)(%s);", valExpr),
		)
		_ = sz

	case x86asm.POP:
		dstReg, ok := args[0].(x86asm.Reg)
		if !ok {
			return nil, fmt.Errorf("0x%x: POP destination must be register", pc)
		}
		stmt, err := GetRegWriteStmt(dstReg, "MEM_U64(ctx->rsp)")
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(
			lines,
			"    "+stmt,
			"    ctx->rsp += 8;",
		)

	case x86asm.ADD, x86asm.SUB, x86asm.AND, x86asm.OR, x86asm.XOR, x86asm.CMP, x86asm.TEST, x86asm.ADC:
		code, err := l.liftAlu(inst, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.BSF:
		code, err := l.liftBsf(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.ANDN, x86asm.BEXTR, x86asm.BLSI, x86asm.BLSMSK, x86asm.BLSR, x86asm.BZHI,
		x86asm.MULX, x86asm.RORX, x86asm.SARX, x86asm.SHLX, x86asm.SHRX:
		code, err := l.liftBMI(inst, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.INC, x86asm.DEC, x86asm.NEG, x86asm.NOT:
		code, err := l.liftUnary(inst, args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.SHL, x86asm.SHR, x86asm.SAR:
		code, err := l.liftShift(inst, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.XCHG:
		code, err := l.liftXchg(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.XADD:
		code, err := l.liftXadd(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CMPXCHG:
		code, err := l.liftCmpxchg(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.IMUL:
		code, err := l.liftImul(args, defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MUL:
		code, err := l.liftMul(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CALL:
		if rel, ok := args[0].(x86asm.Rel); ok {
			target := uint64(int64(nextPC) + int64(rel))
			lines = append(
				lines,
				"    ctx->rsp -= 8;",
				fmt.Sprintf("    MEM_U64(ctx->rsp) = 0x%xULL;", nextPC),
				fmt.Sprintf("    ctx->rip = 0x%xULL;", target),
			)
			if l.knownFuncs[target] {
				lines = append(lines, fmt.Sprintf("    fn_0x%x(ctx);", target))
			} else {
				lines = append(lines, fmt.Sprintf("    recomp_dispatch(ctx, 0x%xULL);", target))
			}
		} else {
			targetExpr, _, err := l.getOperandRead(args[0], defMemSz, nextPC)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			lines = append(
				lines,
				"    ctx->rsp -= 8;",
				fmt.Sprintf("    MEM_U64(ctx->rsp) = 0x%xULL;", nextPC),
				fmt.Sprintf("    ctx->rip = %s;", targetExpr),
				fmt.Sprintf("    recomp_dispatch(ctx, %s);", targetExpr),
			)
		}

	case x86asm.RET:
		if isContextRestoreFunction(fn) {
			lines = append(
				lines,
				"    {",
				"        uint64_t target_ip = MEM_U64(ctx->rsp);",
				"        ctx->rsp += 8;",
				"        recomp_unwind_to(ctx, target_ip);",
				"    }",
				"    return;",
			)
		} else {
			lines = append(
				lines,
				"    RECOMP_POP_UNWIND();",
				"    ctx->rsp += 8;",
				"    return;",
			)
		}

	case x86asm.LRET:
		lines = append(
			lines,
			"    RECOMP_POP_UNWIND();",
			"    ctx->rsp += 16;",
			"    return;",
		)

	case x86asm.LCALL:
		targetExpr, _, err := l.getOperandRead(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(
			lines,
			"    ctx->rsp -= 8;",
			fmt.Sprintf("    MEM_U64(ctx->rsp) = 0x%xULL;", nextPC),
			fmt.Sprintf("    recomp_dispatch(ctx, %s);", targetExpr),
		)

	case x86asm.LJMP:
		targetExpr, _, err := l.getOperandRead(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(
			lines,
			"    RECOMP_POP_UNWIND();",
			fmt.Sprintf("    recomp_dispatch(ctx, %s);", targetExpr),
			"    return;",
		)

	case x86asm.JMP:
		if rel, ok := args[0].(x86asm.Rel); ok {
			target := uint64(int64(nextPC) + int64(rel))
			if _, ok := fn.Blocks[target]; ok {
				lines = append(lines, fmt.Sprintf("    goto loc_0x%x;", target))
			} else if l.knownFuncs[target] {
				lines = append(
					lines,
					"    RECOMP_POP_UNWIND();",
					fmt.Sprintf("    ctx->rip = 0x%xULL;", target),
					fmt.Sprintf("    fn_0x%x(ctx);", target),
					"    return;",
				)
			} else {
				lines = append(
					lines,
					"    RECOMP_POP_UNWIND();",
					fmt.Sprintf("    ctx->rip = 0x%xULL;", target),
					fmt.Sprintf("    recomp_dispatch(ctx, 0x%xULL);", target),
					"    return;",
				)
			}
		} else {
			targetExpr, _, err := l.getOperandRead(args[0], defMemSz, nextPC)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			lines = append(
				lines,
				"    RECOMP_POP_UNWIND();",
				fmt.Sprintf("    ctx->rip = %s;", targetExpr),
				fmt.Sprintf("    recomp_dispatch(ctx, %s);", targetExpr),
				"    return;",
			)
		}

	case x86asm.LOOP, x86asm.LOOPE, x86asm.LOOPNE:
		code, err := l.liftLoop(op, args[0], nextPC, fn)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.RCL, x86asm.RCR:
		code, err := l.liftRclRcr(op, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CMC:
		lines = append(lines, l.liftCmc()...)

	case x86asm.XLATB:
		lines = append(lines, l.liftXlatb()...)

	case x86asm.RDTSC:
		lines = append(lines, l.liftRdtsc()...)

	case x86asm.SLDT:
		code, err := l.liftSldt(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.LAR:
		code, err := l.liftLar(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.UD0, x86asm.UD2, x86asm.ICEBP, x86asm.XABORT:
		lines = append(
			lines,
			fmt.Sprintf("    fprintf(stderr, \"Trap instruction %v at 0x%x\\n\");", op, pc),
			"    abort();",
		)

	case x86asm.INT:
		lines = append(
			lines,
			fmt.Sprintf("    /* Interrupt at 0x%x */", pc),
		)

	case x86asm.FWAIT:
		lines = append(lines, "    /* fwait no-op */")

	// Vector/SSE operations
	case x86asm.MOVUPS, x86asm.MOVAPS, x86asm.MOVDQU, x86asm.MOVDQA, x86asm.MOVUPD, x86asm.MOVAPD:
		code, err := l.liftVectorMove(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PXOR, x86asm.XORPS, x86asm.XORPD:
		code, err := l.liftVectorXor(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.POR, x86asm.ORPD, x86asm.ORPS:
		code, err := l.liftVectorBitwise(" | ", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PAND, x86asm.ANDPD, x86asm.ANDPS:
		code, err := l.liftVectorBitwise(" & ", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PCMPEQD:
		code, err := l.liftPcmpeq(4, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PCMPEQB:
		code, err := l.liftPcmpeq(1, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PCMPEQW:
		code, err := l.liftPcmpeq(2, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PADDD:
		code, err := l.liftPadd(4, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PADDB:
		code, err := l.liftPadd(1, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PADDW:
		code, err := l.liftPadd(2, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PADDQ:
		code, err := l.liftPadd(8, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PUNPCKLBW:
		code, err := l.liftPunpckl(1, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PUNPCKLWD:
		code, err := l.liftPunpckl(2, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.UNPCKLPD:
		code, err := l.liftUnpcklpd(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.ADDPD:
		code, err := l.liftPackedF64("+", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MULPD:
		code, err := l.liftPackedF64("*", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.SUBPD:
		code, err := l.liftPackedF64("-", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.DIVPD:
		code, err := l.liftPackedF64("/", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.ADDSD:
		code, err := l.liftScalarF64("+", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MULSD:
		code, err := l.liftScalarF64("*", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.SUBSD:
		code, err := l.liftScalarF64("-", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.DIVSD:
		code, err := l.liftScalarF64("/", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.ADDSS:
		code, err := l.liftScalarF32("+", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MULSS:
		code, err := l.liftScalarF32("*", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.SUBSS:
		code, err := l.liftScalarF32("-", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.DIVSS:
		code, err := l.liftScalarF32("/", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.UCOMISD:
		code, err := l.liftUcomis(true, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.UCOMISS:
		code, err := l.liftUcomis(false, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CVTSI2SD:
		code, err := l.liftCvtsi2s(true, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CVTSI2SS:
		code, err := l.liftCvtsi2s(false, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CVTSS2SD:
		code, err := l.liftCvtss2sd(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CVTSD2SS:
		code, err := l.liftCvtsd2ss(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CVTTSD2SI:
		code, err := l.liftCvtFloatToInt(true, true, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CVTSD2SI:
		code, err := l.liftCvtFloatToInt(false, true, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CVTTSS2SI:
		code, err := l.liftCvtFloatToInt(true, false, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CVTSS2SI:
		code, err := l.liftCvtFloatToInt(false, false, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSHUFLW:
		code, err := l.liftPshuflw(args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.VERW:
		lines = append(lines, "    ctx->zf = 1; /* flat memory write allowed */")

	case x86asm.BSR:
		code, err := l.liftBsr(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.IDIV:
		code, err := l.liftIdiv(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FLD1:
		lines = append(lines, "    fpu_push(ctx, 1.0);")

	case x86asm.FABS:
		lines = append(lines, "    ctx->fpu_stack[ctx->fpu_top] = fabs(ctx->fpu_stack[ctx->fpu_top]);")

	case x86asm.FCOMI, x86asm.FCOMIP:
		code, err := l.liftFucomi(op, args)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FSUBRP:
		lines = append(lines, "    FPU_ST(1) = FPU_ST(0) - FPU_ST(1); fpu_pop(ctx);")

	case x86asm.FDIVR:
		if mem, ok := args[0].(x86asm.Mem); ok {
			addrExpr, err := MemAddrExpr(mem, nextPC)
			if err != nil {
				return nil, err
			}
			if defMemSz == 8 {
				lines = append(lines, fmt.Sprintf("    { double d; uint64_t u = MEM_U64(%s); memcpy(&d, &u, 8); FPU_ST(0) = d / FPU_ST(0); }", addrExpr))
			} else {
				lines = append(lines, fmt.Sprintf("    { float f; uint32_t u = MEM_U32(%s); memcpy(&f, &u, 4); FPU_ST(0) = (double)f / FPU_ST(0); }", addrExpr))
			}
		} else {
			lines = append(lines, "    FPU_ST(1) = FPU_ST(0) / FPU_ST(1); fpu_pop(ctx);")
		}

	case x86asm.FIDIV:
		if mem, ok := args[0].(x86asm.Mem); ok {
			addrExpr, err := MemAddrExpr(mem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(lines, fmt.Sprintf("    FPU_ST(0) /= (double)(int32_t)MEM_U32(%s);", addrExpr))
		}

	case x86asm.MOVD:
		code, err := l.liftMovd(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MOVQ:
		code, err := l.liftMovq(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MOVSS:
		code, err := l.liftMovss(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSHUFD:
		code, err := l.liftPshufd(args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSLLD:
		code, err := l.liftPshift("<<", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSRLD:
		code, err := l.liftPshift(">>", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.SBB:
		code, err := l.liftSbb(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.BT, x86asm.BTR, x86asm.BTS, x86asm.BTC:
		code, err := l.liftBitTest(op, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.BSWAP:
		code, err := l.liftBswap(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.POPCNT:
		code, err := l.liftPopcnt(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.LZCNT:
		code, err := l.liftLzcnt(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.TZCNT:
		code, err := l.liftTzcnt(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.SHLD, x86asm.SHRD:
		code, err := l.liftDoubleShift(op, args[0], args[1], args[2], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CPUID:
		lines = append(lines, l.liftCpuid()...)

	case x86asm.PAUSE, x86asm.PREFETCHT0, x86asm.PREFETCHNTA, x86asm.PREFETCHT1, x86asm.PREFETCHT2, x86asm.PREFETCHW:
		// No-op hint
		lines = append(lines, "    /* pause/prefetch */")

	case x86asm.INSB:
		lines = append(lines, "    /* insb */")

	case x86asm.EMMS:
		lines = append(lines, "    ctx->fpu_top = 0;")

	case x86asm.PUSHFQ:
		lines = append(lines, l.liftPushfq()...)

	case x86asm.POPFQ:
		lines = append(lines, l.liftPopfq()...)

	case x86asm.XGETBV:
		lines = append(lines, l.liftXgetbv()...)

	case x86asm.PSUBB:
		code, err := l.liftPsub(1, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSUBW:
		code, err := l.liftPsub(2, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSUBD:
		code, err := l.liftPsub(4, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSUBQ:
		code, err := l.liftPsub(8, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PMULLW:
		code, err := l.liftPmullw(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PMULHW:
		code, err := l.liftPmulhw(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PMADDWD:
		code, err := l.liftPmaddwd(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PACKSSDW, x86asm.PACKUSWB, x86asm.PACKSSWB:
		code, err := l.liftPack(op, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PINSRB:
		code, err := l.liftPinsr(1, args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PINSRW:
		code, err := l.liftPinsr(2, args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PINSRD:
		code, err := l.liftPinsr(4, args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PINSRQ:
		code, err := l.liftPinsr(8, args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSLLW, x86asm.PSRLW, x86asm.PSRAW:
		code, err := l.liftPshiftW(op, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSRAD:
		code, err := l.liftPsrad(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSLLQ:
		code, err := l.liftPshiftQ("<<", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSRLQ:
		code, err := l.liftPshiftQ(">>", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSLLDQ:
		code, err := l.liftPshiftBytes(true, args[0], args[1])
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSRLDQ:
		code, err := l.liftPshiftBytes(false, args[0], args[1])
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PUNPCKHBW:
		code, err := l.liftPunpckh(1, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PUNPCKHWD:
		code, err := l.liftPunpckh(2, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PUNPCKHDQ, x86asm.UNPCKHPS:
		code, err := l.liftPunpckh(4, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PUNPCKLQDQ:
		code, err := l.liftPunpcklqdq(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PUNPCKHQDQ, x86asm.UNPCKHPD:
		code, err := l.liftPunpckh(8, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSHUFHW:
		code, err := l.liftPshufhw(args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.COMISD:
		code, err := l.liftComis(true, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.COMISS:
		code, err := l.liftComis(false, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MINSD:
		code, err := l.liftMinMax(true, true, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MAXSD:
		code, err := l.liftMinMax(false, true, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MINSS:
		code, err := l.liftMinMax(true, false, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MAXSS:
		code, err := l.liftMinMax(false, false, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.SQRTSS:
		code, err := l.liftSqrt(false, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.SQRTSD:
		code, err := l.liftSqrt(true, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MOVHPD, x86asm.MOVHPS:
		code, err := l.liftMovhpd(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MOVLPD, x86asm.MOVLPS:
		code, err := l.liftMovlpd(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MOVNTDQ:
		code, err := l.liftVectorMove(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.ADDPS:
		code, err := l.liftPackedF32("+", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.SUBPS:
		code, err := l.liftPackedF32("-", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MULPS:
		code, err := l.liftPackedF32("*", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.DIVPS:
		code, err := l.liftPackedF32("/", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MAXPS:
		code, err := l.liftPackedF32("max", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MINPS:
		code, err := l.liftPackedF32("min", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.HADDPS:
		code, err := l.liftHaddps(args[0], args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.HADDPD:
		code, err := l.liftHaddpd(args[0], args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PCMPGTQ:
		code, err := l.liftPcmpgtq(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CVTPS2PD:
		code, err := l.liftCvtps2pd(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CVTPD2PS:
		code, err := l.liftCvtpd2ps(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MOVBE:
		code, err := l.liftMovbe(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CVTDQ2PS:
		code, err := l.liftCvtdq2ps(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CVTPS2DQ:
		code, err := l.liftCvtps2dq(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PAVGB:
		code, err := l.liftPavgb(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PAVGW:
		code, err := l.liftPavgw(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CLC:
		lines = append(lines, "    ctx->cf = 0;")
	case x86asm.STC:
		lines = append(lines, "    ctx->cf = 1;")
	case x86asm.CLD, x86asm.STD:
		lines = append(lines, "    /* direction flag is not modeled; string ops increment */")
	case x86asm.MOVSB:
		lines = append(lines, l.liftMovs(inst.Inst, 1)...)
	case x86asm.MOVSW:
		lines = append(lines, l.liftMovs(inst.Inst, 2)...)
	case x86asm.MOVSQ:
		lines = append(lines, l.liftMovs(inst.Inst, 8)...)
	case x86asm.STOSB:
		lines = append(lines, l.liftStos(inst.Inst, 1)...)
	case x86asm.STOSW:
		lines = append(lines, l.liftStos(inst.Inst, 2)...)
	case x86asm.STOSD:
		lines = append(lines, l.liftStos(inst.Inst, 4)...)
	case x86asm.STOSQ:
		lines = append(lines, l.liftStos(inst.Inst, 8)...)
	case x86asm.SCASB:
		lines = append(lines, l.liftScas(inst.Inst, 1)...)
	case x86asm.SCASW:
		lines = append(lines, l.liftScas(inst.Inst, 2)...)
	case x86asm.SCASD:
		lines = append(lines, l.liftScas(inst.Inst, 4)...)
	case x86asm.SCASQ:
		lines = append(lines, l.liftScas(inst.Inst, 8)...)
	case x86asm.LODSB:
		lines = append(lines, l.liftLods(inst.Inst, 1)...)
	case x86asm.LODSW:
		lines = append(lines, l.liftLods(inst.Inst, 2)...)
	case x86asm.LODSD:
		lines = append(lines, l.liftLods(inst.Inst, 4)...)
	case x86asm.LODSQ:
		lines = append(lines, l.liftLods(inst.Inst, 8)...)
	case x86asm.CMPSB:
		lines = append(lines, l.liftCmps(inst.Inst, 1)...)
	case x86asm.CMPSW:
		lines = append(lines, l.liftCmps(inst.Inst, 2)...)
	case x86asm.CMPSD:
		lines = append(lines, l.liftCmps(inst.Inst, 4)...)
	case x86asm.CMPSQ:
		lines = append(lines, l.liftCmps(inst.Inst, 8)...)
	case x86asm.LEAVE:
		lines = append(lines, l.liftLeave()...)
	case x86asm.ENTER:
		lines = append(lines, l.liftEnter(args)...)
	case x86asm.SAHF:
		lines = append(lines, l.liftSahf()...)
	case x86asm.LAHF:
		lines = append(lines, l.liftLahf()...)
	case x86asm.SHUFPS:
		code, err := l.liftShufps(args[0], args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.SHUFPD:
		code, err := l.liftShufpd(args[0], args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.PSHUFB:
		code, err := l.liftPshufb(args[0], args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.PALIGNR:
		code, err := l.liftPalignr(args[0], args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.PEXTRB:
		code, err := l.liftPextr(1, args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.PEXTRW:
		code, err := l.liftPextr(2, args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.PEXTRD:
		code, err := l.liftPextr(4, args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.PEXTRQ:
		code, err := l.liftPextr(8, args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.PMINSD:
		code, err := l.liftPminmax(true, true, 4, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.PMAXSD:
		code, err := l.liftPminmax(false, true, 4, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.PMINUD:
		code, err := l.liftPminmax(true, false, 4, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.PMAXUD:
		code, err := l.liftPminmax(false, false, 4, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.PMINSB:
		code, err := l.liftPminmax(true, true, 1, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.PMAXSB:
		code, err := l.liftPminmax(false, true, 1, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.MOVLHPS:
		code, err := l.liftMovlhps(args[0], args[0], args[1])
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.MOVHLPS:
		code, err := l.liftMovhlps(args[0], args[0], args[1])
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.ROUNDPS:
		code, err := l.liftRoundps(args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.PTEST:
		code, err := l.liftPtest(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.MOVNTI:
		code, err := l.liftMovnti(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.SQRTPS:
		code, err := l.liftPackedSqrt(false, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.SQRTPD:
		code, err := l.liftPackedSqrt(true, args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.MOVSLDUP:
		code, err := l.liftMovsldup(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)
	case x86asm.LFENCE, x86asm.MFENCE, x86asm.SFENCE:
		lines = append(lines, "    __sync_synchronize();")
	case x86asm.VMOVAPS, x86asm.VMOVUPS, x86asm.VMOVUPD, x86asm.VMOVAPD, x86asm.VMOVDQA, x86asm.VMOVDQU, x86asm.VMOVD, x86asm.VMOVQ,
		x86asm.VMOVSS, x86asm.VMOVSD, x86asm.VMOVNTPS, x86asm.VMOVNTDQ, x86asm.VBROADCASTSS, x86asm.VMOVSLDUP,
		x86asm.VADDSS, x86asm.VADDSD, x86asm.VSUBSS, x86asm.VSUBSD,
		x86asm.VMULSS, x86asm.VMULSD, x86asm.VDIVSS, x86asm.VDIVSD,
		x86asm.VADDPS, x86asm.VSUBPS, x86asm.VMULPS, x86asm.VDIVPS,
		x86asm.VMAXPS, x86asm.VMINPS, x86asm.VMAXSS, x86asm.VMINSS, x86asm.VHADDPS, x86asm.VHADDPD,
		x86asm.VXORPS, x86asm.VORPS, x86asm.VANDPS, x86asm.VPXOR, x86asm.VPOR, x86asm.VPAND, x86asm.VPADDW, x86asm.VPSUBW, x86asm.VPMULLW,
		x86asm.VPAVGB, x86asm.VPAVGW,
		x86asm.VPINSRW, x86asm.VPINSRB, x86asm.VPINSRD,
		x86asm.VPSLLW, x86asm.VPSRLW, x86asm.VPSRAW, x86asm.VPSLLD, x86asm.VPSRLD, x86asm.VPSRAD,
		x86asm.VPACKUSWB, x86asm.VPACKSSDW, x86asm.VPACKSSWB,
		x86asm.VPUNPCKLBW, x86asm.VPUNPCKHBW, x86asm.VPUNPCKLWD, x86asm.VPUNPCKHWD,
		x86asm.VPUNPCKLDQ, x86asm.VPUNPCKHDQ, x86asm.VUNPCKLPS, x86asm.VUNPCKHPS,
		x86asm.VCVTDQ2PS, x86asm.VCVTPS2DQ, x86asm.VCVTTPS2DQ,
		x86asm.VPANDN, x86asm.VPMULLD, x86asm.VPMOVZXWD, x86asm.VPMOVSXWD,
		x86asm.VCVTSI2SS, x86asm.VCVTSI2SD, x86asm.VCVTTSS2SI, x86asm.VCVTTSD2SI,
		x86asm.VCVTSS2SD, x86asm.VCVTSD2SS,
		x86asm.VUCOMISS, x86asm.VUCOMISD, x86asm.VPSHUFHW, x86asm.VPSHUFLW,
		x86asm.VROUNDSD, x86asm.VROUNDSS, x86asm.VSQRTSS, x86asm.VSQRTSD,
		x86asm.VRSQRTSS, x86asm.VRSQRTPS, x86asm.VMOVDDUP, x86asm.VCVTSS2SI, x86asm.VCVTSD2SI,
		x86asm.VCVTPS2PD, x86asm.VCVTPD2PS, x86asm.VEXTRACTF128, x86asm.VEXTRACTI128, x86asm.VINSERTF128, x86asm.VINSERTI128,
		x86asm.VPERMILPS, x86asm.VPERMILPD, x86asm.VCMPPS, x86asm.VSHUFPS,
		x86asm.VINSERTPS, x86asm.VMOVSHDUP, x86asm.VMOVLPS, x86asm.VMOVMSKPS,
		x86asm.VBLENDPS, x86asm.VBLENDVPS, x86asm.VPBLENDVB, x86asm.VEXTRACTPS, x86asm.VCMPSS, x86asm.VCMPSD,
		x86asm.VSHUFPD, x86asm.VADDPD, x86asm.VSUBPD, x86asm.VMULPD, x86asm.VDIVPD, x86asm.VMINPD, x86asm.VMAXPD,
		x86asm.VMINSD, x86asm.VMAXSD, x86asm.VANDPD, x86asm.VXORPD, x86asm.VORPD, x86asm.VANDNPS, x86asm.VANDNPD,
		x86asm.VPADDB, x86asm.VPADDD, x86asm.VPADDQ, x86asm.VPSUBB, x86asm.VPSUBD, x86asm.VPSUBQ,
		x86asm.VPCMPEQB, x86asm.VPCMPEQW, x86asm.VPCMPEQD, x86asm.VPCMPGTD, x86asm.VPCMPGTQ,
		x86asm.VPUNPCKLQDQ, x86asm.VPUNPCKHQDQ, x86asm.VUNPCKLPD, x86asm.VUNPCKHPD,
		x86asm.VPSHUFD, x86asm.VPSHUFB, x86asm.VPALIGNR, x86asm.VPSLLQ, x86asm.VPSRLQ, x86asm.VPSLLDQ, x86asm.VPSRLDQ,
		x86asm.VPINSRQ, x86asm.VPEXTRB, x86asm.VPEXTRW, x86asm.VPEXTRD, x86asm.VPEXTRQ,
		x86asm.VPMINSD, x86asm.VPMAXSD, x86asm.VPMINUD, x86asm.VPMAXUD, x86asm.VPMINSB, x86asm.VPMAXSB,
		x86asm.VSQRTPS, x86asm.VSQRTPD, x86asm.VMOVLHPS, x86asm.VMOVHLPS, x86asm.VMOVHPD, x86asm.VMOVHPS,
		x86asm.VROUNDPS, x86asm.VPTEST,
		x86asm.VMOVLPD, x86asm.VPBLENDW, x86asm.VBLENDPD, x86asm.VBLENDVPD,
		x86asm.VMASKMOVPS, x86asm.VMASKMOVPD, x86asm.VBROADCASTSD, x86asm.VBROADCASTF128,
		x86asm.VMOVMSKPD, x86asm.VPERM2F128, x86asm.VROUNDPD, x86asm.VDPPS,
		x86asm.VRCPPS, x86asm.VRCPSS, x86asm.VPHADDD, x86asm.VPHADDW,
		x86asm.VHSUBPS, x86asm.VHSUBPD, x86asm.VPHSUBD,
		x86asm.VPMAXUW, x86asm.VPMAXSW, x86asm.VPMINSW, x86asm.VPMINUB, x86asm.VPMAXUB, x86asm.VPMINUW,
		x86asm.VPMADDWD, x86asm.VPMADDUBSW, x86asm.VPMOVMSKB,
		x86asm.VPCMPEQQ, x86asm.VPCMPGTB, x86asm.VPCMPGTW,
		x86asm.VPMULUDQ, x86asm.VPMULDQ, x86asm.VPMULHW, x86asm.VPMULHUW,
		x86asm.VPACKUSDW, x86asm.VPSADBW, x86asm.VPABSD, x86asm.VPABSW,
		x86asm.VPHMINPOSUW, x86asm.VPADDUSB, x86asm.VPADDSW,
		x86asm.VPSUBSW, x86asm.VPSUBUSB, x86asm.VPSUBUSW,
		x86asm.VADDSUBPS, x86asm.VADDSUBPD, x86asm.VCMPPD,
		x86asm.VCVTTPD2DQ, x86asm.VCVTDQ2PD, x86asm.VCVTPH2PS, x86asm.VCVTPS2PH,
		x86asm.VSTMXCSR, x86asm.VLDMXCSR,
		x86asm.VPMOVSXDQ, x86asm.VPMOVZXDQ, x86asm.VPMOVSXBD, x86asm.VPMOVZXBD,
		x86asm.VPMOVSXBW, x86asm.VPMOVZXBW, x86asm.VPMOVSXWQ, x86asm.VPMOVZXWQ,
		x86asm.VPMOVSXBQ, x86asm.VPMOVZXBQ:
		code, err := l.liftVexOp(op, args, defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.VPCMPISTRI, x86asm.PCMPISTRI:
		code, err := l.liftPcmpistri(op, args, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.DIV:
		code, err := l.liftDiv(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.HLT:
		lines = append(lines, "    exit(0);")

	case x86asm.MOVSD_XMM:
		code, err := l.liftMovsd(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MOVSD:
		lines = append(lines, l.liftMovs(inst.Inst, 4)...)

	case x86asm.PANDN, x86asm.ANDNPS, x86asm.ANDNPD:
		code, err := l.liftPandn(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PMULUDQ:
		code, err := l.liftPmuludq(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PCMPGTD:
		code, err := l.liftPcmpgtd(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PUNPCKLDQ, x86asm.UNPCKLPS:
		code, err := l.liftPunpckldq(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FLDZ:
		lines = append(lines, "    fpu_push(ctx, 0.0);")

	case x86asm.FLD:
		code, err := l.liftFld(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FILD:
		code, err := l.liftFild(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FST, x86asm.FSTP:
		code, err := l.liftFstp(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FIST, x86asm.FISTP:
		code, err := l.liftFist(op, args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FXCH:
		code, err := l.liftFxch(args[0])
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FCHS:
		lines = append(lines, "    FPU_ST(0) = -FPU_ST(0);")

	case x86asm.FADD, x86asm.FADDP:
		code, err := l.liftFadd(op, args, defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FIADD:
		code, err := l.liftFiadd(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FSUB, x86asm.FSUBP:
		code, err := l.liftFsub(op, args, defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FISUB:
		code, err := l.liftFisub(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FMUL, x86asm.FMULP:
		code, err := l.liftFmul(op, args, defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FIMUL:
		code, err := l.liftFimul(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FDIV, x86asm.FDIVP:
		code, err := l.liftFdiv(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FUCOMI, x86asm.FUCOMIP:
		code, err := l.liftFucomi(op, args)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FLDCW:
		code, err := l.liftFldcw(args[0], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FNSTCW:
		code, err := l.liftFnstcw(args[0], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FISTTP:
		code, err := l.liftFisttp(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FCOM, x86asm.FCOMP:
		code, err := l.liftFcom(op, args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FICOM, x86asm.FICOMP:
		code, err := l.liftFicom(op, args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FCMOVB, x86asm.FCMOVE, x86asm.FCMOVNB, x86asm.FCMOVU:
		code, err := l.liftFcmov(op, args[0])
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FIDIVR:
		code, err := l.liftFidivr(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FDIVRP:
		lines = append(lines, l.liftFdivrp()...)

	case x86asm.FSUBR:
		code, err := l.liftFsubr(args, defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FLDL2T:
		lines = append(lines, l.liftFldl2t()...)

	case x86asm.FBSTP:
		code, err := l.liftFbstp(args[0], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FNOP:
		lines = append(lines, "    /* fnop */")

	case x86asm.FNSTENV:
		code, err := l.liftFnstenv(args[0], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FNSTSW:
		code, err := l.liftFnstsw(args[0], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.FYL2XP1:
		lines = append(lines, l.liftFyl2xp1()...)

	default:
		// Check SETcc
		if isSetcc(op) {
			cond, err := setccCondition(op)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			setExpr := fmt.Sprintf("(%s ? 1 : 0)", cond)
			writeStmts, err := l.getOperandWrite(args[0], 1, setExpr, nextPC)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			for _, ws := range writeStmts {
				lines = append(lines, "    "+ws)
			}
			return lines, nil
		}

		// Check CMOVcc
		if isCmovcc(op) {
			cond, err := cmovCondition(op)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			srcExpr, sz, err := l.getOperandRead(args[1], defMemSz, nextPC)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			writeStmts, err := l.getOperandWrite(args[0], sz, srcExpr, nextPC)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			lines = append(lines, fmt.Sprintf("    if (%s) {", cond))
			for _, ws := range writeStmts {
				lines = append(lines, "    "+ws)
			}
			lines = append(lines, "    }")
			return lines, nil
		}

		// Check Jcc
		if IsJcc(op) {
			cond, err := ConditionExpr(op)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			rel, ok := args[0].(x86asm.Rel)
			if !ok {
				return nil, fmt.Errorf("0x%x: Jcc target must be Rel", pc)
			}
			target := uint64(int64(nextPC) + int64(rel))
			if _, ok := fn.Blocks[target]; ok {
				lines = append(lines, fmt.Sprintf("    if (%s) goto loc_0x%x;", cond, target))
			} else if l.knownFuncs[target] {
				lines = append(
					lines,
					fmt.Sprintf("    if (%s) { RECOMP_POP_UNWIND(); ctx->rip = 0x%xULL; fn_0x%x(ctx); return; }", cond, target, target),
				)
			} else {
				lines = append(
					lines,
					fmt.Sprintf("    if (%s) { RECOMP_POP_UNWIND(); ctx->rip = 0x%xULL; recomp_dispatch(ctx, 0x%xULL); return; }", cond, target, target),
				)
			}
		} else {
			return nil, fmt.Errorf("unsupported opcode: %v", op)
		}
	}

	return lines, nil
}

func (l *Lifter) getOperandRead(arg x86asm.Arg, defMemSz int, nextPC uint64) (string, int, error) {
	switch a := arg.(type) {
	case x86asm.Reg:
		return GetRegReadExpr(a)
	case x86asm.Mem:
		addr, err := MemAddrExpr(a, nextPC)
		if err != nil {
			return "", 0, err
		}
		sz := defMemSz
		if sz == 0 {
			sz = 8
		}
		expr, err := MemReadExpr(addr, sz)
		return expr, sz, err
	case x86asm.Imm:
		return "0x" + strconv.FormatUint(uint64(a), 16) + "ULL", 8, nil
	default:
		return "", 0, fmt.Errorf("unknown operand type: %T", arg)
	}
}

func (l *Lifter) getOperandWrite(arg x86asm.Arg, sz int, valExpr string, nextPC uint64) ([]string, error) {
	switch a := arg.(type) {
	case x86asm.Reg:
		stmt, err := GetRegWriteStmt(a, valExpr)
		if err != nil {
			return nil, err
		}
		return []string{stmt}, nil
	case x86asm.Mem:
		addr, err := MemAddrExpr(a, nextPC)
		if err != nil {
			return nil, err
		}
		stmt, err := MemWriteStmt(addr, sz, valExpr)
		if err != nil {
			return nil, err
		}
		return []string{stmt}, nil
	default:
		return nil, fmt.Errorf("cannot write to operand of type %T", arg)
	}
}

func uintType(size int) string {
	switch size {
	case 1:
		return "uint8_t"
	case 2:
		return "uint16_t"
	case 4:
		return "uint32_t"
	default:
		return "uint64_t"
	}
}

func intType(size int) string {
	switch size {
	case 1:
		return "int8_t"
	case 2:
		return "int16_t"
	case 4:
		return "int32_t"
	default:
		return "int64_t"
	}
}
