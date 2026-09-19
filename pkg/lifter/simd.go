package lifter

import (
	"fmt"

	"golang.org/x/arch/x86/x86asm"
)

func isXmm(reg x86asm.Reg) bool {
	info, ok := regMap[reg]
	return ok && info.Size == 16
}

func (l *Lifter) liftVectorMove(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	if dstReg, ok := dst.(x86asm.Reg); ok {
		if srcReg, ok := src.(x86asm.Reg); ok {
			infoDst := regMap[dstReg]
			infoSrc := regMap[srcReg]
			return []string{fmt.Sprintf("    ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc.BaseReg)}, nil
		}
		if srcMem, ok := src.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(srcMem, nextPC)
			if err != nil {
				return nil, err
			}
			infoDst := regMap[dstReg]
			return []string{fmt.Sprintf("    memcpy(&ctx->%s, ctx->mem_base + (%s), 16);", infoDst.BaseReg, addr)}, nil
		}
	}
	if dstMem, ok := dst.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(dstMem, nextPC)
		if err != nil {
			return nil, err
		}
		if srcReg, ok := src.(x86asm.Reg); ok {
			infoSrc := regMap[srcReg]
			return []string{fmt.Sprintf("    memcpy(ctx->mem_base + (%s), &ctx->%s, 16);", addr, infoSrc.BaseReg)}, nil
		}
	}
	return nil, fmt.Errorf("unsupported vector move operands")
}

func (l *Lifter) liftVectorXor(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	srcReg, ok2 := src.(x86asm.Reg)
	if ok1 && ok2 && dstReg == srcReg {
		info := regMap[dstReg]
		return []string{fmt.Sprintf("    memset(&ctx->%s, 0, 16);", info.BaseReg)}, nil
	}
	return l.liftVectorBitwise(" ^ ", dst, src, nextPC)
}

func (l *Lifter) liftVectorBitwise(opStr string, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("vector bitwise dst must be XMM register")
	}
	infoDst := regMap[dstReg]

	if srcReg, ok2 := src.(x86asm.Reg); ok2 && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		return []string{
			fmt.Sprintf("    ctx->%s.u64[0] = ctx->%s.u64[0] %s ctx->%s.u64[0];", infoDst.BaseReg, infoDst.BaseReg, opStr, infoSrc.BaseReg),
			fmt.Sprintf("    ctx->%s.u64[1] = ctx->%s.u64[1] %s ctx->%s.u64[1];", infoDst.BaseReg, infoDst.BaseReg, opStr, infoSrc.BaseReg),
		}, nil
	}
	if srcMem, ok2 := src.(x86asm.Mem); ok2 {
		addrExpr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{
			fmt.Sprintf("    ctx->%s.u64[0] = ctx->%s.u64[0] %s MEM_U64(%s);", infoDst.BaseReg, infoDst.BaseReg, opStr, addrExpr),
			fmt.Sprintf("    ctx->%s.u64[1] = ctx->%s.u64[1] %s MEM_U64((%s) + 8);", infoDst.BaseReg, infoDst.BaseReg, opStr, addrExpr),
		}, nil
	}
	return nil, fmt.Errorf("unsupported vector bitwise operands")
}

func (l *Lifter) liftPandn(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("pandn dst must be XMM register")
	}
	infoDst := regMap[dstReg]

	if srcReg, ok2 := src.(x86asm.Reg); ok2 && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		return []string{
			fmt.Sprintf("    ctx->%s.u64[0] = (~ctx->%s.u64[0]) & ctx->%s.u64[0];", infoDst.BaseReg, infoDst.BaseReg, infoSrc.BaseReg),
			fmt.Sprintf("    ctx->%s.u64[1] = (~ctx->%s.u64[1]) & ctx->%s.u64[1];", infoDst.BaseReg, infoDst.BaseReg, infoSrc.BaseReg),
		}, nil
	}
	if srcMem, ok2 := src.(x86asm.Mem); ok2 {
		addrExpr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{
			fmt.Sprintf("    ctx->%s.u64[0] = (~ctx->%s.u64[0]) & MEM_U64(%s);", infoDst.BaseReg, infoDst.BaseReg, addrExpr),
			fmt.Sprintf("    ctx->%s.u64[1] = (~ctx->%s.u64[1]) & MEM_U64((%s) + 8);", infoDst.BaseReg, infoDst.BaseReg, addrExpr),
		}, nil
	}
	return nil, fmt.Errorf("unsupported pandn operands")
}

func (l *Lifter) liftPmuludq(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("pmuludq dst must be XMM register")
	}
	infoDst := regMap[dstReg]

	var src0, src1 string
	if srcReg, ok2 := src.(x86asm.Reg); ok2 && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		src0 = fmt.Sprintf("ctx->%s.u32[0]", infoSrc.BaseReg)
		src1 = fmt.Sprintf("ctx->%s.u32[2]", infoSrc.BaseReg)
	} else if srcMem, ok2 := src.(x86asm.Mem); ok2 {
		addrExpr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		src0 = fmt.Sprintf("MEM_U32(%s)", addrExpr)
		src1 = fmt.Sprintf("MEM_U32((%s) + 8)", addrExpr)
	} else {
		return nil, fmt.Errorf("unsupported pmuludq operands")
	}

	return []string{
		fmt.Sprintf("    ctx->%s.u64[0] = (uint64_t)ctx->%s.u32[0] * (uint64_t)(%s);", infoDst.BaseReg, infoDst.BaseReg, src0),
		fmt.Sprintf("    ctx->%s.u64[1] = (uint64_t)ctx->%s.u32[2] * (uint64_t)(%s);", infoDst.BaseReg, infoDst.BaseReg, src1),
	}, nil
}

func (l *Lifter) liftPcmpgtd(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("pcmpgtd dst must be XMM register")
	}
	infoDst := regMap[dstReg]

	var srcExprs [4]string
	if srcReg, ok2 := src.(x86asm.Reg); ok2 && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		for i := 0; i < 4; i++ {
			srcExprs[i] = fmt.Sprintf("ctx->%s.u32[%d]", infoSrc.BaseReg, i)
		}
	} else if srcMem, ok2 := src.(x86asm.Mem); ok2 {
		addrExpr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		for i := 0; i < 4; i++ {
			srcExprs[i] = fmt.Sprintf("MEM_U32((%s) + %d)", addrExpr, i*4)
		}
	} else {
		return nil, fmt.Errorf("unsupported pcmpgtd operands")
	}

	var lines []string
	for i := 0; i < 4; i++ {
		lines = append(lines, fmt.Sprintf("    ctx->%s.u32[%d] = ((int32_t)ctx->%s.u32[%d] > (int32_t)(%s)) ? 0xFFFFFFFFU : 0;",
			infoDst.BaseReg, i, infoDst.BaseReg, i, srcExprs[i]))
	}
	return lines, nil
}

func (l *Lifter) liftPunpckldq(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("punpckldq dst must be XMM register")
	}
	infoDst := regMap[dstReg]

	var src0, src1 string
	if srcReg, ok2 := src.(x86asm.Reg); ok2 && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		src0 = fmt.Sprintf("ctx->%s.u32[0]", infoSrc.BaseReg)
		src1 = fmt.Sprintf("ctx->%s.u32[1]", infoSrc.BaseReg)
	} else if srcMem, ok2 := src.(x86asm.Mem); ok2 {
		addrExpr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		src0 = fmt.Sprintf("MEM_U32(%s)", addrExpr)
		src1 = fmt.Sprintf("MEM_U32((%s) + 4)", addrExpr)
	} else {
		return nil, fmt.Errorf("unsupported punpckldq operands")
	}

	return []string{
		fmt.Sprintf("    { uint32_t d0 = ctx->%s.u32[0]; uint32_t d1 = ctx->%s.u32[1];", infoDst.BaseReg, infoDst.BaseReg),
		fmt.Sprintf("      uint32_t s0 = %s; uint32_t s1 = %s;", src0, src1),
		fmt.Sprintf("      ctx->%s.u32[0] = d0; ctx->%s.u32[1] = s0;", infoDst.BaseReg, infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.u32[2] = d1; ctx->%s.u32[3] = s1; }", infoDst.BaseReg, infoDst.BaseReg),
	}, nil
}

func (l *Lifter) liftMovd(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	if dstReg, ok := dst.(x86asm.Reg); ok && isXmm(dstReg) {
		infoDst := regMap[dstReg]
		srcExpr, _, err := l.getOperandRead(src, 4, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{
			fmt.Sprintf("    memset(&ctx->%s, 0, 16);", infoDst.BaseReg),
			fmt.Sprintf("    ctx->%s.u32[0] = (uint32_t)(%s);", infoDst.BaseReg, srcExpr),
		}, nil
	}
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		return l.getOperandWrite(dst, 4, fmt.Sprintf("ctx->%s.u32[0]", infoSrc.BaseReg), nextPC)
	}
	return nil, fmt.Errorf("unsupported MOVD operands")
}

func (l *Lifter) liftMovq(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	if dstReg, ok := dst.(x86asm.Reg); ok && isXmm(dstReg) {
		infoDst := regMap[dstReg]
		srcExpr, _, err := l.getOperandRead(src, 8, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{
			fmt.Sprintf("    memset(&ctx->%s, 0, 16);", infoDst.BaseReg),
			fmt.Sprintf("    ctx->%s.u64[0] = (uint64_t)(%s);", infoDst.BaseReg, srcExpr),
		}, nil
	}
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		return l.getOperandWrite(dst, 8, fmt.Sprintf("ctx->%s.u64[0]", infoSrc.BaseReg), nextPC)
	}
	return nil, fmt.Errorf("unsupported MOVQ operands")
}

func (l *Lifter) liftMovss(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	if dstReg, ok := dst.(x86asm.Reg); ok && isXmm(dstReg) {
		infoDst := regMap[dstReg]
		if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
			infoSrc := regMap[srcReg]
			return []string{
				fmt.Sprintf("    ctx->%s.u32[0] = ctx->%s.u32[0];", infoDst.BaseReg, infoSrc.BaseReg),
			}, nil
		}
		if srcMem, ok := src.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(srcMem, nextPC)
			if err != nil {
				return nil, err
			}
			return []string{
				fmt.Sprintf("    ctx->%s.u32[0] = MEM_U32(%s);", infoDst.BaseReg, addr),
			}, nil
		}
	}
	if dstMem, ok := dst.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(dstMem, nextPC)
		if err != nil {
			return nil, err
		}
		if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
			infoSrc := regMap[srcReg]
			return []string{
				fmt.Sprintf("    MEM_U32(%s) = ctx->%s.u32[0];", addr, infoSrc.BaseReg),
			}, nil
		}
	}
	return nil, fmt.Errorf("unsupported MOVSS operands")
}

func (l *Lifter) liftMovsd(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	if dstReg, ok := dst.(x86asm.Reg); ok && isXmm(dstReg) {
		infoDst := regMap[dstReg]
		if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
			infoSrc := regMap[srcReg]
			return []string{
				fmt.Sprintf("    ctx->%s.u64[0] = ctx->%s.u64[0];", infoDst.BaseReg, infoSrc.BaseReg),
			}, nil
		}
		if srcMem, ok := src.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(srcMem, nextPC)
			if err != nil {
				return nil, err
			}
			return []string{
				fmt.Sprintf("    ctx->%s.u64[0] = MEM_U64(%s);", infoDst.BaseReg, addr),
			}, nil
		}
	}
	if dstMem, ok := dst.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(dstMem, nextPC)
		if err != nil {
			return nil, err
		}
		if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
			infoSrc := regMap[srcReg]
			return []string{
				fmt.Sprintf("    MEM_U64(%s) = ctx->%s.u64[0];", addr, infoSrc.BaseReg),
			}, nil
		}
	}
	return nil, fmt.Errorf("unsupported MOVSD operands")
}

func (l *Lifter) liftPshufd(dst, src, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 {
		return nil, fmt.Errorf("unsupported PSHUFD operands")
	}
	infoDst := regMap[dstReg]
	srcReadExpr := func(idx int) string {
		return fmt.Sprintf("src.u32[%d]", idx)
	}

	var lines []string
	lines = append(lines, "    {")
	if srcReg, ok := src.(x86asm.Reg); ok {
		infoSrc := regMap[srcReg]
		lines = append(lines, fmt.Sprintf("      xmm_reg_t src = ctx->%s;", infoSrc.BaseReg))
	} else if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(
			lines,
			"      xmm_reg_t src;",
			fmt.Sprintf("      memcpy(&src, ctx->mem_base + (%s), 16);", addr),
		)
	} else {
		return nil, fmt.Errorf("invalid PSHUFD source")
	}

	order := []int{
		int(imm & 3),
		int((imm >> 2) & 3),
		int((imm >> 4) & 3),
		int((imm >> 6) & 3),
	}

	lines = append(
		lines,
		fmt.Sprintf("      ctx->%s.u32[0] = %s;", infoDst.BaseReg, srcReadExpr(order[0])),
		fmt.Sprintf("      ctx->%s.u32[1] = %s;", infoDst.BaseReg, srcReadExpr(order[1])),
		fmt.Sprintf("      ctx->%s.u32[2] = %s;", infoDst.BaseReg, srcReadExpr(order[2])),
		fmt.Sprintf("      ctx->%s.u32[3] = %s;", infoDst.BaseReg, srcReadExpr(order[3])),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftPshift(shiftOp string, dst, countArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok {
		return nil, fmt.Errorf("vector shift destination must be register")
	}
	countVal, _, err := l.getOperandRead(countArg, 1, nextPC)
	if err != nil {
		return nil, err
	}
	infoDst := regMap[dstReg]
	return []string{
		fmt.Sprintf("    { uint32_t shift = (uint32_t)(%s);", countVal),
		"      if (shift < 32) {",
		fmt.Sprintf("        ctx->%s.u32[0] %s= shift; ctx->%s.u32[1] %s= shift;", infoDst.BaseReg, shiftOp, infoDst.BaseReg, shiftOp),
		fmt.Sprintf("        ctx->%s.u32[2] %s= shift; ctx->%s.u32[3] %s= shift;", infoDst.BaseReg, shiftOp, infoDst.BaseReg, shiftOp),
		"      } else {",
		fmt.Sprintf("        memset(&ctx->%s, 0, 16);", infoDst.BaseReg),
		"      }",
		"    }",
	}, nil
}

func (l *Lifter) liftScalarF64(opStr string, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("scalar f64 dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		return []string{
			fmt.Sprintf("    ctx->%s.f64[0] %s= ctx->%s.f64[0];", infoDst.BaseReg, opStr, infoSrc.BaseReg),
		}, nil
	}
	if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{
			fmt.Sprintf("    { double s; uint64_t u = MEM_U64(%s); memcpy(&s, &u, 8); ctx->%s.f64[0] %s= s; }", addr, infoDst.BaseReg, opStr),
		}, nil
	}
	return nil, fmt.Errorf("unsupported scalar f64 operands")
}

func (l *Lifter) liftScalarF32(opStr string, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("scalar f32 dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		return []string{
			fmt.Sprintf("    ctx->%s.f32[0] %s= ctx->%s.f32[0];", infoDst.BaseReg, opStr, infoSrc.BaseReg),
		}, nil
	}
	if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{
			fmt.Sprintf("    { float s; uint32_t u = MEM_U32(%s); memcpy(&s, &u, 4); ctx->%s.f32[0] %s= s; }", addr, infoDst.BaseReg, opStr),
		}, nil
	}
	return nil, fmt.Errorf("unsupported scalar f32 operands")
}

func (l *Lifter) liftPackedF64(opStr string, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("packed f64 dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	var lines []string
	lines = append(lines, "    {")
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		lines = append(lines, fmt.Sprintf("      xmm_reg_t src = ctx->%s;", infoSrc.BaseReg))
	} else if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(
			lines,
			"      xmm_reg_t src;",
			fmt.Sprintf("      memcpy(&src, ctx->mem_base + (%s), 16);", addr),
		)
	} else {
		return nil, fmt.Errorf("unsupported packed f64 operands")
	}
	lines = append(
		lines,
		fmt.Sprintf("      ctx->%s.f64[0] %s= src.f64[0];", infoDst.BaseReg, opStr),
		fmt.Sprintf("      ctx->%s.f64[1] %s= src.f64[1];", infoDst.BaseReg, opStr),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftPcmpeq(elemBytes int, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("pcmpeq dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	var lines []string
	lines = append(lines, "    {")
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		lines = append(lines, fmt.Sprintf("      xmm_reg_t src = ctx->%s;", infoSrc.BaseReg))
	} else if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(
			lines,
			"      xmm_reg_t src;",
			fmt.Sprintf("      memcpy(&src, ctx->mem_base + (%s), 16);", addr),
		)
	} else {
		return nil, fmt.Errorf("pcmpeq invalid src")
	}
	switch elemBytes {
	case 1:
		lines = append(
			lines,
			fmt.Sprintf("      for (int i = 0; i < 16; i++) ctx->%s.u8[i] = (ctx->%s.u8[i] == src.u8[i]) ? 0xFF : 0;", infoDst.BaseReg, infoDst.BaseReg),
		)
	case 2:
		lines = append(
			lines,
			fmt.Sprintf("      for (int i = 0; i < 8; i++) ctx->%s.u16[i] = (ctx->%s.u16[i] == src.u16[i]) ? 0xFFFF : 0;", infoDst.BaseReg, infoDst.BaseReg),
		)
	case 4:
		lines = append(
			lines,
			fmt.Sprintf("      for (int i = 0; i < 4; i++) ctx->%s.u32[i] = (ctx->%s.u32[i] == src.u32[i]) ? 0xFFFFFFFFU : 0;", infoDst.BaseReg, infoDst.BaseReg),
		)
	case 8:
		lines = append(
			lines,
			fmt.Sprintf("      for (int i = 0; i < 2; i++) ctx->%s.u64[i] = (ctx->%s.u64[i] == src.u64[i]) ? 0xFFFFFFFFFFFFFFFFULL : 0;", infoDst.BaseReg, infoDst.BaseReg),
		)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftPadd(elemBytes int, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("padd dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	var lines []string
	lines = append(lines, "    {")
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		lines = append(lines, fmt.Sprintf("      xmm_reg_t src = ctx->%s;", infoSrc.BaseReg))
	} else if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(
			lines,
			"      xmm_reg_t src;",
			fmt.Sprintf("      memcpy(&src, ctx->mem_base + (%s), 16);", addr),
		)
	} else {
		return nil, fmt.Errorf("padd invalid src")
	}
	switch elemBytes {
	case 1:
		lines = append(
			lines,
			fmt.Sprintf("      for (int i = 0; i < 16; i++) ctx->%s.u8[i] += src.u8[i];", infoDst.BaseReg),
		)
	case 2:
		lines = append(
			lines,
			fmt.Sprintf("      for (int i = 0; i < 8; i++) ctx->%s.u16[i] += src.u16[i];", infoDst.BaseReg),
		)
	case 4:
		lines = append(
			lines,
			fmt.Sprintf("      for (int i = 0; i < 4; i++) ctx->%s.u32[i] += src.u32[i];", infoDst.BaseReg),
		)
	case 8:
		lines = append(
			lines,
			fmt.Sprintf("      for (int i = 0; i < 2; i++) ctx->%s.u64[i] += src.u64[i];", infoDst.BaseReg),
		)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftPunpckl(elemBytes int, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("punpckl dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	var lines []string
	lines = append(lines, "    {")
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		lines = append(lines, fmt.Sprintf("      xmm_reg_t src = ctx->%s;", infoSrc.BaseReg))
	} else if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(
			lines,
			"      xmm_reg_t src;",
			fmt.Sprintf("      memcpy(&src, ctx->mem_base + (%s), 16);", addr),
		)
	} else {
		return nil, fmt.Errorf("punpckl invalid src")
	}
	switch elemBytes {
	case 1:
		lines = append(
			lines,
			"      uint8_t d[8], s[8];",
			fmt.Sprintf("      memcpy(d, ctx->%s.u8, 8); memcpy(s, src.u8, 8);", infoDst.BaseReg),
			fmt.Sprintf("      for (int i = 0; i < 8; i++) { ctx->%s.u8[2*i] = d[i]; ctx->%s.u8[2*i+1] = s[i]; }", infoDst.BaseReg, infoDst.BaseReg),
		)
	case 2:
		lines = append(
			lines,
			"      uint16_t d[4], s[4];",
			fmt.Sprintf("      memcpy(d, ctx->%s.u16, 8); memcpy(s, src.u16, 8);", infoDst.BaseReg),
			fmt.Sprintf("      for (int i = 0; i < 4; i++) { ctx->%s.u16[2*i] = d[i]; ctx->%s.u16[2*i+1] = s[i]; }", infoDst.BaseReg, infoDst.BaseReg),
		)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftUnpcklpd(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("unpcklpd dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		return []string{
			fmt.Sprintf("    ctx->%s.f64[1] = ctx->%s.f64[0];", infoDst.BaseReg, infoSrc.BaseReg),
		}, nil
	}
	if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{
			fmt.Sprintf("    { double s; uint64_t u = MEM_U64(%s); memcpy(&s, &u, 8); ctx->%s.f64[1] = s; }", addr, infoDst.BaseReg),
		}, nil
	}
	return nil, fmt.Errorf("unpcklpd invalid src")
}

func (l *Lifter) liftUcomis(isDouble bool, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("ucomis dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	var lines []string
	lines = append(lines, "    {")
	if isDouble {
		lines = append(lines, fmt.Sprintf("      double a = ctx->%s.f64[0]; double b;", infoDst.BaseReg))
		if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
			infoSrc := regMap[srcReg]
			lines = append(lines, fmt.Sprintf("      b = ctx->%s.f64[0];", infoSrc.BaseReg))
		} else if srcMem, ok := src.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(srcMem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(lines, fmt.Sprintf("      uint64_t u = MEM_U64(%s); memcpy(&b, &u, 8);", addr))
		} else {
			return nil, fmt.Errorf("ucomisd invalid src")
		}
	} else {
		lines = append(lines, fmt.Sprintf("      float a = ctx->%s.f32[0]; float b;", infoDst.BaseReg))
		if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
			infoSrc := regMap[srcReg]
			lines = append(lines, fmt.Sprintf("      b = ctx->%s.f32[0];", infoSrc.BaseReg))
		} else if srcMem, ok := src.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(srcMem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(lines, fmt.Sprintf("      uint32_t u = MEM_U32(%s); memcpy(&b, &u, 4);", addr))
		} else {
			return nil, fmt.Errorf("ucomiss invalid src")
		}
	}
	lines = append(
		lines,
		"      if (isnan(a) || isnan(b)) { ctx->zf = 1; ctx->pf = 1; ctx->cf = 1; }",
		"      else if (a > b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 0; }",
		"      else if (a < b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 1; }",
		"      else { ctx->zf = 1; ctx->pf = 0; ctx->cf = 0; }",
		"      ctx->of = 0; ctx->sf = 0; ctx->af = 0;",
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftCvtsi2s(isDouble bool, dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("cvtsi2s dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	sz := defMemSz
	if reg, ok := src.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	sRead, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}
	var castType string
	if sz == 8 {
		castType = "int64_t"
	} else {
		castType = "int32_t"
	}
	if isDouble {
		return []string{
			fmt.Sprintf("    ctx->%s.f64[0] = (double)(%s)(%s);", infoDst.BaseReg, castType, sRead),
		}, nil
	}
	return []string{
		fmt.Sprintf("    ctx->%s.f32[0] = (float)(%s)(%s);", infoDst.BaseReg, castType, sRead),
	}, nil
}

func (l *Lifter) liftCvtss2sd(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("cvtss2sd dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		return []string{
			fmt.Sprintf("    ctx->%s.f64[0] = (double)ctx->%s.f32[0];", infoDst.BaseReg, infoSrc.BaseReg),
		}, nil
	}
	if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{
			fmt.Sprintf("    { float s; uint32_t u = MEM_U32(%s); memcpy(&s, &u, 4); ctx->%s.f64[0] = (double)s; }", addr, infoDst.BaseReg),
		}, nil
	}
	return nil, fmt.Errorf("unsupported cvtss2sd src")
}

func (l *Lifter) liftCvtsd2ss(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("cvtsd2ss dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		return []string{
			fmt.Sprintf("    ctx->%s.f32[0] = (float)ctx->%s.f64[0];", infoDst.BaseReg, infoSrc.BaseReg),
		}, nil
	}
	if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{
			fmt.Sprintf("    { double s; uint64_t u = MEM_U64(%s); memcpy(&s, &u, 8); ctx->%s.f32[0] = (float)s; }", addr, infoDst.BaseReg),
		}, nil
	}
	return nil, fmt.Errorf("unsupported cvtsd2ss src")
}

func (l *Lifter) liftPshuflw(dst, src, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(dstReg) {
		return nil, fmt.Errorf("unsupported PSHUFLW operands")
	}
	infoDst := regMap[dstReg]
	var lines []string
	lines = append(lines, "    {")
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		lines = append(lines, fmt.Sprintf("      xmm_reg_t src = ctx->%s;", infoSrc.BaseReg))
	} else if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(
			lines,
			"      xmm_reg_t src;",
			fmt.Sprintf("      memcpy(&src, ctx->mem_base + (%s), 16);", addr),
		)
	} else {
		return nil, fmt.Errorf("invalid PSHUFLW source")
	}
	order := []int{
		int(imm & 3),
		int((imm >> 2) & 3),
		int((imm >> 4) & 3),
		int((imm >> 6) & 3),
	}
	lines = append(
		lines,
		fmt.Sprintf("      uint16_t w0 = src.u16[%d];", order[0]),
		fmt.Sprintf("      uint16_t w1 = src.u16[%d];", order[1]),
		fmt.Sprintf("      uint16_t w2 = src.u16[%d];", order[2]),
		fmt.Sprintf("      uint16_t w3 = src.u16[%d];", order[3]),
		fmt.Sprintf("      ctx->%s.u16[0] = w0;", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.u16[1] = w1;", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.u16[2] = w2;", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.u16[3] = w3;", infoDst.BaseReg),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftCvttsd2si(isDouble bool, dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok {
		return nil, fmt.Errorf("cvttsd2si dst must be GP register")
	}
	sz := defMemSz
	if info, ok := regMap[dstReg]; ok && info.Size > 0 {
		sz = info.Size
	}
	var castType string
	if sz == 8 {
		castType = "int64_t"
	} else {
		castType = "int32_t"
	}
	var srcExpr string
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		if isDouble {
			srcExpr = fmt.Sprintf("ctx->%s.f64[0]", infoSrc.BaseReg)
		} else {
			srcExpr = fmt.Sprintf("ctx->%s.f32[0]", infoSrc.BaseReg)
		}
	} else if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		if isDouble {
			srcExpr = fmt.Sprintf("({ double s; uint64_t u = MEM_U64(%s); memcpy(&s, &u, 8); s; })", addr)
		} else {
			srcExpr = fmt.Sprintf("({ float s; uint32_t u = MEM_U32(%s); memcpy(&s, &u, 4); s; })", addr)
		}
	} else {
		return nil, fmt.Errorf("cvttsd2si invalid src")
	}

	writeStmts, err := l.getOperandWrite(dst, sz, fmt.Sprintf("(%s)(%s)", castType, srcExpr), nextPC)
	if err != nil {
		return nil, err
	}
	var lines []string
	for _, ws := range writeStmts {
		lines = append(lines, "    "+ws)
	}
	return lines, nil
}
