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
		for i := range 4 {
			srcExprs[i] = fmt.Sprintf("ctx->%s.u32[%d]", infoSrc.BaseReg, i)
		}
	} else if srcMem, ok2 := src.(x86asm.Mem); ok2 {
		addrExpr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		for i := range 4 {
			srcExprs[i] = fmt.Sprintf("MEM_U32((%s) + %d)", addrExpr, i*4)
		}
	} else {
		return nil, fmt.Errorf("unsupported pcmpgtd operands")
	}

	var lines []string
	for i := range 4 {
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
	var cntExpr string
	if reg, ok := countArg.(x86asm.Reg); ok && isXmm(reg) {
		infoC := regMap[reg]
		cntExpr = fmt.Sprintf("ctx->%s.u64[0]", infoC.BaseReg)
	} else if imm, ok := countArg.(x86asm.Imm); ok {
		cntExpr = fmt.Sprintf("%d", imm)
	} else {
		cRead, _, err := l.getOperandRead(countArg, 1, nextPC)
		if err != nil {
			return nil, err
		}
		cntExpr = cRead
	}
	infoDst := regMap[dstReg]
	return []string{
		fmt.Sprintf("    { uint32_t shift = (uint32_t)(%s);", cntExpr),
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

func (l *Lifter) liftPackedF32(op string, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("packed f32 dst must be XMM register")
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
		return nil, fmt.Errorf("unsupported packed f32 operands")
	}
	switch op {
	case "+", "-", "*", "/":
		for i := 0; i < 4; i++ {
			lines = append(lines, fmt.Sprintf("      ctx->%s.f32[%d] %s= src.f32[%d];", infoDst.BaseReg, i, op, i))
		}
	case "min":
		for i := 0; i < 4; i++ {
			lines = append(lines, fmt.Sprintf("      if (src.f32[%d] < ctx->%s.f32[%d]) ctx->%s.f32[%d] = src.f32[%d];", i, infoDst.BaseReg, i, infoDst.BaseReg, i, i))
		}
	case "max":
		for i := 0; i < 4; i++ {
			lines = append(lines, fmt.Sprintf("      if (src.f32[%d] > ctx->%s.f32[%d]) ctx->%s.f32[%d] = src.f32[%d];", i, infoDst.BaseReg, i, infoDst.BaseReg, i, i))
		}
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftCvtdq2ps(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("cvtdq2ps dst must be XMM register")
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
		return nil, fmt.Errorf("cvtdq2ps invalid src")
	}
	for i := 0; i < 4; i++ {
		lines = append(lines, fmt.Sprintf("      ctx->%s.f32[%d] = (float)src.s32[%d];", infoDst.BaseReg, i, i))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftCvtps2dq(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("cvtps2dq dst must be XMM register")
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
		return nil, fmt.Errorf("cvtps2dq invalid src")
	}
	for i := 0; i < 4; i++ {
		lines = append(lines, fmt.Sprintf("      ctx->%s.s32[%d] = (int32_t)roundf(src.f32[%d]);", infoDst.BaseReg, i, i))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftPavgb(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("pavgb dst must be XMM register")
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
		return nil, fmt.Errorf("pavgb invalid src")
	}
	lines = append(
		lines,
		fmt.Sprintf("      for (int i = 0; i < 16; i++) ctx->%s.u8[i] = (uint8_t)(((uint32_t)ctx->%s.u8[i] + (uint32_t)src.u8[i] + 1) >> 1);", infoDst.BaseReg, infoDst.BaseReg),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftPavgw(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("pavgw dst must be XMM register")
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
		return nil, fmt.Errorf("pavgw invalid src")
	}
	lines = append(
		lines,
		fmt.Sprintf("      for (int i = 0; i < 8; i++) ctx->%s.u16[i] = (uint16_t)(((uint32_t)ctx->%s.u16[i] + (uint32_t)src.u16[i] + 1) >> 1);", infoDst.BaseReg, infoDst.BaseReg),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftVbroadcastss(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("vbroadcastss dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	var lines []string
	lines = append(lines, "    {", "      float val;")
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		lines = append(lines, fmt.Sprintf("      val = ctx->%s.f32[0];", infoSrc.BaseReg))
	} else if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(
			lines,
			fmt.Sprintf("      uint32_t u = MEM_U32(%s);", addr),
			"      memcpy(&val, &u, 4);",
		)
	} else {
		return nil, fmt.Errorf("vbroadcastss unsupported src operand")
	}
	for i := 0; i < 4; i++ {
		lines = append(lines, fmt.Sprintf("      ctx->%s.f32[%d] = val;", infoDst.BaseReg, i))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftHaddps(dst, src1, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("haddps dst must be XMM register")
	}
	infoDst := regMap[dstReg]

	var lines []string
	lines = append(lines, "    {", "      xmm_reg_t s1, s2;")

	if s1Reg, ok := src1.(x86asm.Reg); ok && isXmm(s1Reg) {
		infoS1 := regMap[s1Reg]
		lines = append(lines, fmt.Sprintf("      s1 = ctx->%s;", infoS1.BaseReg))
	} else if s1Mem, ok := src1.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(s1Mem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(lines, fmt.Sprintf("      memcpy(&s1, ctx->mem_base + (%s), 16);", addr))
	} else {
		return nil, fmt.Errorf("haddps invalid src1")
	}

	if s2Reg, ok := src2.(x86asm.Reg); ok && isXmm(s2Reg) {
		infoS2 := regMap[s2Reg]
		lines = append(lines, fmt.Sprintf("      s2 = ctx->%s;", infoS2.BaseReg))
	} else if s2Mem, ok := src2.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(s2Mem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(lines, fmt.Sprintf("      memcpy(&s2, ctx->mem_base + (%s), 16);", addr))
	} else {
		return nil, fmt.Errorf("haddps invalid src2")
	}

	lines = append(
		lines,
		fmt.Sprintf("      ctx->%s.f32[0] = s1.f32[0] + s1.f32[1];", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.f32[1] = s1.f32[2] + s1.f32[3];", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.f32[2] = s2.f32[0] + s2.f32[1];", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.f32[3] = s2.f32[2] + s2.f32[3];", infoDst.BaseReg),
		"    }",
	)
	return lines, nil
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
	case 4:
		lines = append(
			lines,
			"      uint32_t d[2], s[2];",
			fmt.Sprintf("      memcpy(d, ctx->%s.u32, 8); memcpy(s, src.u32, 8);", infoDst.BaseReg),
			fmt.Sprintf("      ctx->%s.u32[0] = d[0]; ctx->%s.u32[1] = s[0];", infoDst.BaseReg, infoDst.BaseReg),
			fmt.Sprintf("      ctx->%s.u32[2] = d[1]; ctx->%s.u32[3] = s[1];", infoDst.BaseReg, infoDst.BaseReg),
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

func (l *Lifter) liftPsub(elemBytes int, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("psub dst must be XMM register")
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
		return nil, fmt.Errorf("psub invalid src")
	}
	switch elemBytes {
	case 1:
		lines = append(
			lines,
			fmt.Sprintf("      for (int i = 0; i < 16; i++) ctx->%s.u8[i] -= src.u8[i];", infoDst.BaseReg),
		)
	case 2:
		lines = append(
			lines,
			fmt.Sprintf("      for (int i = 0; i < 8; i++) ctx->%s.u16[i] -= src.u16[i];", infoDst.BaseReg),
		)
	case 4:
		lines = append(
			lines,
			fmt.Sprintf("      for (int i = 0; i < 4; i++) ctx->%s.u32[i] -= src.u32[i];", infoDst.BaseReg),
		)
	case 8:
		lines = append(
			lines,
			fmt.Sprintf("      for (int i = 0; i < 2; i++) ctx->%s.u64[i] -= src.u64[i];", infoDst.BaseReg),
		)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftPmullw(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("pmullw dst must be XMM register")
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
		lines = append(lines, "      xmm_reg_t src;", fmt.Sprintf("      memcpy(&src, ctx->mem_base + (%s), 16);", addr))
	} else {
		return nil, fmt.Errorf("pmullw invalid src")
	}
	lines = append(
		lines,
		fmt.Sprintf("      for (int i = 0; i < 8; i++) ctx->%s.s16[i] = (int16_t)(((int32_t)ctx->%s.s16[i] * (int32_t)src.s16[i]) & 0xFFFF);", infoDst.BaseReg, infoDst.BaseReg),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftPmulhw(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("pmulhw dst must be XMM register")
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
		lines = append(lines, "      xmm_reg_t src;", fmt.Sprintf("      memcpy(&src, ctx->mem_base + (%s), 16);", addr))
	} else {
		return nil, fmt.Errorf("pmulhw invalid src")
	}
	lines = append(
		lines,
		fmt.Sprintf("      for (int i = 0; i < 8; i++) ctx->%s.s16[i] = (int16_t)(((int32_t)ctx->%s.s16[i] * (int32_t)src.s16[i]) >> 16);", infoDst.BaseReg, infoDst.BaseReg),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftPmaddwd(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("pmaddwd dst must be XMM register")
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
		lines = append(lines, "      xmm_reg_t src;", fmt.Sprintf("      memcpy(&src, ctx->mem_base + (%s), 16);", addr))
	} else {
		return nil, fmt.Errorf("pmaddwd invalid src")
	}
	lines = append(
		lines,
		"      for (int i = 0; i < 4; i++) {",
		fmt.Sprintf("        int32_t p0 = (int32_t)ctx->%s.s16[2*i] * (int32_t)src.s16[2*i];", infoDst.BaseReg),
		fmt.Sprintf("        int32_t p1 = (int32_t)ctx->%s.s16[2*i+1] * (int32_t)src.s16[2*i+1];", infoDst.BaseReg),
		fmt.Sprintf("        ctx->%s.s32[i] = p0 + p1;", infoDst.BaseReg),
		"      }",
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftPack(op x86asm.Op, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("pack dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	var lines []string
	lines = append(lines, "    {")
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		lines = append(lines, fmt.Sprintf("      xmm_reg_t d = ctx->%s; xmm_reg_t s = ctx->%s;", infoDst.BaseReg, infoSrc.BaseReg))
	} else if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(
			lines,
			fmt.Sprintf("      xmm_reg_t d = ctx->%s; xmm_reg_t s;", infoDst.BaseReg),
			fmt.Sprintf("      memcpy(&s, ctx->mem_base + (%s), 16);", addr),
		)
	} else {
		return nil, fmt.Errorf("pack invalid src")
	}

	switch op {
	case x86asm.PACKSSDW:
		lines = append(
			lines,
			"      for (int i = 0; i < 4; i++) {",
			"        int32_t v = d.s32[i];",
			"        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;",
			fmt.Sprintf("        ctx->%s.s16[i] = (int16_t)v;", infoDst.BaseReg),
			"      }",
			"      for (int i = 0; i < 4; i++) {",
			"        int32_t v = s.s32[i];",
			"        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;",
			fmt.Sprintf("        ctx->%s.s16[i+4] = (int16_t)v;", infoDst.BaseReg),
			"      }",
		)
	case x86asm.PACKUSWB:
		lines = append(
			lines,
			"      for (int i = 0; i < 8; i++) {",
			"        int16_t v = d.s16[i];",
			"        if (v < 0) v = 0; else if (v > 255) v = 255;",
			fmt.Sprintf("        ctx->%s.u8[i] = (uint8_t)v;", infoDst.BaseReg),
			"      }",
			"      for (int i = 0; i < 8; i++) {",
			"        int16_t v = s.s16[i];",
			"        if (v < 0) v = 0; else if (v > 255) v = 255;",
			fmt.Sprintf("        ctx->%s.u8[i+8] = (uint8_t)v;", infoDst.BaseReg),
			"      }",
		)
	case x86asm.PACKSSWB:
		lines = append(
			lines,
			"      for (int i = 0; i < 8; i++) {",
			"        int16_t v = d.s16[i];",
			"        if (v > 127) v = 127; else if (v < -128) v = -128;",
			fmt.Sprintf("        ctx->%s.s8[i] = (int8_t)v;", infoDst.BaseReg),
			"      }",
			"      for (int i = 0; i < 8; i++) {",
			"        int16_t v = s.s16[i];",
			"        if (v > 127) v = 127; else if (v < -128) v = -128;",
			fmt.Sprintf("        ctx->%s.s8[i+8] = (int8_t)v;", infoDst.BaseReg),
			"      }",
		)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftPinsr(elemBytes int, dst, src, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(dstReg) {
		return nil, fmt.Errorf("pinsr invalid operands")
	}
	infoDst := regMap[dstReg]
	sRead, _, err := l.getOperandRead(src, elemBytes, nextPC)
	if err != nil {
		return nil, err
	}

	var lines []string
	switch elemBytes {
	case 1:
		lines = append(lines, fmt.Sprintf("    ctx->%s.u8[%d] = (uint8_t)(%s);", infoDst.BaseReg, imm&0xF, sRead))
	case 2:
		lines = append(lines, fmt.Sprintf("    ctx->%s.u16[%d] = (uint16_t)(%s);", infoDst.BaseReg, imm&0x7, sRead))
	case 4:
		lines = append(lines, fmt.Sprintf("    ctx->%s.u32[%d] = (uint32_t)(%s);", infoDst.BaseReg, imm&0x3, sRead))
	case 8:
		lines = append(lines, fmt.Sprintf("    ctx->%s.u64[%d] = (uint64_t)(%s);", infoDst.BaseReg, imm&0x1, sRead))
	}
	return lines, nil
}

func (l *Lifter) liftPshiftW(op x86asm.Op, dst, countArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("vector word shift destination must be XMM")
	}
	infoDst := regMap[dstReg]

	var cntExpr string
	if reg, ok := countArg.(x86asm.Reg); ok && isXmm(reg) {
		infoC := regMap[reg]
		cntExpr = fmt.Sprintf("ctx->%s.u64[0]", infoC.BaseReg)
	} else if imm, ok := countArg.(x86asm.Imm); ok {
		cntExpr = fmt.Sprintf("%d", imm)
	} else {
		cRead, _, err := l.getOperandRead(countArg, 1, nextPC)
		if err != nil {
			return nil, err
		}
		cntExpr = cRead
	}

	lines := []string{
		"    {",
		fmt.Sprintf("      uint32_t shift = (uint32_t)(%s);", cntExpr),
	}
	switch op {
	case x86asm.PSLLW:
		lines = append(
			lines,
			"      if (shift < 16) {",
			fmt.Sprintf("        for (int i = 0; i < 8; i++) ctx->%s.u16[i] <<= shift;", infoDst.BaseReg),
			"      } else {",
			fmt.Sprintf("        memset(&ctx->%s, 0, 16);", infoDst.BaseReg),
			"      }",
		)
	case x86asm.PSRLW:
		lines = append(
			lines,
			"      if (shift < 16) {",
			fmt.Sprintf("        for (int i = 0; i < 8; i++) ctx->%s.u16[i] >>= shift;", infoDst.BaseReg),
			"      } else {",
			fmt.Sprintf("        memset(&ctx->%s, 0, 16);", infoDst.BaseReg),
			"      }",
		)
	case x86asm.PSRAW:
		lines = append(
			lines,
			"      if (shift >= 16) shift = 15;",
			fmt.Sprintf("      for (int i = 0; i < 8; i++) ctx->%s.s16[i] >>= shift;", infoDst.BaseReg),
		)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftPsrad(dst, countArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("vector psrad destination must be XMM")
	}
	infoDst := regMap[dstReg]

	var cntExpr string
	if reg, ok := countArg.(x86asm.Reg); ok && isXmm(reg) {
		infoC := regMap[reg]
		cntExpr = fmt.Sprintf("ctx->%s.u64[0]", infoC.BaseReg)
	} else if imm, ok := countArg.(x86asm.Imm); ok {
		cntExpr = fmt.Sprintf("%d", imm)
	} else {
		cRead, _, err := l.getOperandRead(countArg, 1, nextPC)
		if err != nil {
			return nil, err
		}
		cntExpr = cRead
	}

	return []string{
		"    {",
		fmt.Sprintf("      uint32_t shift = (uint32_t)(%s);", cntExpr),
		"      if (shift >= 32) shift = 31;",
		fmt.Sprintf("      for (int i = 0; i < 4; i++) ctx->%s.s32[i] >>= shift;", infoDst.BaseReg),
		"    }",
	}, nil
}

func (l *Lifter) liftPshiftQ(shiftOp string, dst, countArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("vector qword shift destination must be XMM")
	}
	infoDst := regMap[dstReg]
	var cntExpr string
	if reg, ok := countArg.(x86asm.Reg); ok && isXmm(reg) {
		infoC := regMap[reg]
		cntExpr = fmt.Sprintf("ctx->%s.u64[0]", infoC.BaseReg)
	} else if imm, ok := countArg.(x86asm.Imm); ok {
		cntExpr = fmt.Sprintf("%d", imm)
	} else {
		cRead, _, err := l.getOperandRead(countArg, 1, nextPC)
		if err != nil {
			return nil, err
		}
		cntExpr = cRead
	}

	return []string{
		"    {",
		fmt.Sprintf("      uint32_t shift = (uint32_t)(%s);", cntExpr),
		"      if (shift < 64) {",
		fmt.Sprintf("        ctx->%s.u64[0] %s= shift; ctx->%s.u64[1] %s= shift;", infoDst.BaseReg, shiftOp, infoDst.BaseReg, shiftOp),
		"      } else {",
		fmt.Sprintf("        memset(&ctx->%s, 0, 16);", infoDst.BaseReg),
		"      }",
		"    }",
	}, nil
}

func (l *Lifter) liftPshiftBytes(isLeft bool, dst, immArg x86asm.Arg) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok || !ok2 || !isXmm(dstReg) {
		return nil, fmt.Errorf("pslldq/psrldq invalid operands")
	}
	infoDst := regMap[dstReg]
	count := int(imm)

	var lines []string
	lines = append(lines, "    {", fmt.Sprintf("      xmm_reg_t tmp = ctx->%s;", infoDst.BaseReg))
	if count >= 16 {
		lines = append(lines, fmt.Sprintf("      memset(&ctx->%s, 0, 16);", infoDst.BaseReg))
	} else if count == 0 {
		// No-op
	} else if isLeft { // PSLLDQ: shift towards higher byte addresses
		lines = append(
			lines,
			fmt.Sprintf("      memset(&ctx->%s.u8[0], 0, %d);", infoDst.BaseReg, count),
			fmt.Sprintf("      memcpy(&ctx->%s.u8[%d], &tmp.u8[0], %d);", infoDst.BaseReg, count, 16-count),
		)
	} else { // PSRLDQ: shift towards lower byte addresses
		lines = append(
			lines,
			fmt.Sprintf("      memcpy(&ctx->%s.u8[0], &tmp.u8[%d], %d);", infoDst.BaseReg, count, 16-count),
			fmt.Sprintf("      memset(&ctx->%s.u8[%d], 0, %d);", infoDst.BaseReg, 16-count, count),
		)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftPunpckh(elemBytes int, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("punpckh dst must be XMM register")
	}
	infoDst := regMap[dstReg]

	var lines []string
	lines = append(lines, "    {")
	if srcReg, ok2 := src.(x86asm.Reg); ok2 && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		lines = append(lines, fmt.Sprintf("      xmm_reg_t src = ctx->%s;", infoSrc.BaseReg))
	} else if srcMem, ok2 := src.(x86asm.Mem); ok2 {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(lines, "      xmm_reg_t src;", fmt.Sprintf("      memcpy(&src, ctx->mem_base + (%s), 16);", addr))
	} else {
		return nil, fmt.Errorf("punpckh invalid src")
	}

	switch elemBytes {
	case 1:
		lines = append(
			lines,
			"      uint8_t d[8], s[8];",
			fmt.Sprintf("      memcpy(d, &ctx->%s.u8[8], 8); memcpy(s, &src.u8[8], 8);", infoDst.BaseReg),
			fmt.Sprintf("      for (int i = 0; i < 8; i++) { ctx->%s.u8[2*i] = d[i]; ctx->%s.u8[2*i+1] = s[i]; }", infoDst.BaseReg, infoDst.BaseReg),
		)
	case 2:
		lines = append(
			lines,
			"      uint16_t d[4], s[4];",
			fmt.Sprintf("      memcpy(d, &ctx->%s.u16[4], 8); memcpy(s, &src.u16[4], 8);", infoDst.BaseReg),
			fmt.Sprintf("      for (int i = 0; i < 4; i++) { ctx->%s.u16[2*i] = d[i]; ctx->%s.u16[2*i+1] = s[i]; }", infoDst.BaseReg, infoDst.BaseReg),
		)
	case 4:
		lines = append(
			lines,
			"      uint32_t d[2], s[2];",
			fmt.Sprintf("      memcpy(d, &ctx->%s.u32[2], 8); memcpy(s, &src.u32[2], 8);", infoDst.BaseReg),
			fmt.Sprintf("      ctx->%s.u32[0] = d[0]; ctx->%s.u32[1] = s[0];", infoDst.BaseReg, infoDst.BaseReg),
			fmt.Sprintf("      ctx->%s.u32[2] = d[1]; ctx->%s.u32[3] = s[1];", infoDst.BaseReg, infoDst.BaseReg),
		)
	case 8: // PUNPCKHQDQ
		lines = append(
			lines,
			fmt.Sprintf("      ctx->%s.u64[0] = ctx->%s.u64[1];", infoDst.BaseReg, infoDst.BaseReg),
			fmt.Sprintf("      ctx->%s.u64[1] = src.u64[1];", infoDst.BaseReg),
		)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftPunpcklqdq(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("punpcklqdq dst must be XMM register")
	}
	infoDst := regMap[dstReg]

	var lines []string
	lines = append(lines, "    {")
	if srcReg, ok2 := src.(x86asm.Reg); ok2 && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		lines = append(lines, fmt.Sprintf("      ctx->%s.u64[1] = ctx->%s.u64[0];", infoDst.BaseReg, infoSrc.BaseReg))
	} else if srcMem, ok2 := src.(x86asm.Mem); ok2 {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(lines, fmt.Sprintf("      ctx->%s.u64[1] = MEM_U64(%s);", infoDst.BaseReg, addr))
	} else {
		return nil, fmt.Errorf("punpcklqdq invalid src")
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftComis(isDouble bool, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	return l.liftUcomis(isDouble, dst, src, nextPC)
}

func (l *Lifter) liftMinMax(isMin bool, isDouble bool, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("min/max dst must be XMM")
	}
	infoDst := regMap[dstReg]
	var sExpr string
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		if isDouble {
			sExpr = fmt.Sprintf("ctx->%s.f64[0]", infoSrc.BaseReg)
		} else {
			sExpr = fmt.Sprintf("ctx->%s.f32[0]", infoSrc.BaseReg)
		}
	} else if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		if isDouble {
			sExpr = fmt.Sprintf("({ double s; uint64_t u = MEM_U64(%s); memcpy(&s, &u, 8); s; })", addr)
		} else {
			sExpr = fmt.Sprintf("({ float s; uint32_t u = MEM_U32(%s); memcpy(&s, &u, 4); s; })", addr)
		}
	} else {
		return nil, fmt.Errorf("min/max invalid src")
	}

	cmpOp := "<"
	if !isMin {
		cmpOp = ">"
	}

	if isDouble {
		return []string{
			fmt.Sprintf("    { double s = %s; if (s %s ctx->%s.f64[0]) ctx->%s.f64[0] = s; }", sExpr, cmpOp, infoDst.BaseReg, infoDst.BaseReg),
		}, nil
	}
	return []string{
		fmt.Sprintf("    { float s = %s; if (s %s ctx->%s.f32[0]) ctx->%s.f32[0] = s; }", sExpr, cmpOp, infoDst.BaseReg, infoDst.BaseReg),
	}, nil
}

func (l *Lifter) liftSqrt(isDouble bool, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("sqrt dst must be XMM")
	}
	infoDst := regMap[dstReg]
	var sExpr string
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		if isDouble {
			sExpr = fmt.Sprintf("ctx->%s.f64[0]", infoSrc.BaseReg)
		} else {
			sExpr = fmt.Sprintf("ctx->%s.f32[0]", infoSrc.BaseReg)
		}
	} else if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		if isDouble {
			sExpr = fmt.Sprintf("({ double s; uint64_t u = MEM_U64(%s); memcpy(&s, &u, 8); s; })", addr)
		} else {
			sExpr = fmt.Sprintf("({ float s; uint32_t u = MEM_U32(%s); memcpy(&s, &u, 4); s; })", addr)
		}
	} else {
		return nil, fmt.Errorf("sqrt invalid src")
	}

	if isDouble {
		return []string{
			fmt.Sprintf("    ctx->%s.f64[0] = sqrt(%s);", infoDst.BaseReg, sExpr),
		}, nil
	}
	return []string{
		fmt.Sprintf("    ctx->%s.f32[0] = sqrtf(%s);", infoDst.BaseReg, sExpr),
	}, nil
}

func (l *Lifter) liftMovhpd(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	if dstReg, ok := dst.(x86asm.Reg); ok && isXmm(dstReg) {
		infoDst := regMap[dstReg]
		if srcMem, ok := src.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(srcMem, nextPC)
			if err != nil {
				return nil, err
			}
			return []string{
				fmt.Sprintf("    ctx->%s.u64[1] = MEM_U64(%s);", infoDst.BaseReg, addr),
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
				fmt.Sprintf("    MEM_U64(%s) = ctx->%s.u64[1];", addr, infoSrc.BaseReg),
			}, nil
		}
	}
	return nil, fmt.Errorf("unsupported MOVHPD operands")
}

func (l *Lifter) liftMovlpd(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	if dstReg, ok := dst.(x86asm.Reg); ok && isXmm(dstReg) {
		infoDst := regMap[dstReg]
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
	return nil, fmt.Errorf("unsupported MOVLPD operands")
}

func (l *Lifter) liftPshufhw(dst, src, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(dstReg) {
		return nil, fmt.Errorf("unsupported PSHUFHW operands")
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
		return nil, fmt.Errorf("invalid PSHUFHW source")
	}
	order := []int{
		int(imm & 3),
		int((imm >> 2) & 3),
		int((imm >> 4) & 3),
		int((imm >> 6) & 3),
	}
	lines = append(
		lines,
		fmt.Sprintf("      uint16_t w4 = src.u16[4 + %d];", order[0]),
		fmt.Sprintf("      uint16_t w5 = src.u16[4 + %d];", order[1]),
		fmt.Sprintf("      uint16_t w6 = src.u16[4 + %d];", order[2]),
		fmt.Sprintf("      uint16_t w7 = src.u16[4 + %d];", order[3]),
		fmt.Sprintf("      ctx->%s.u16[4] = w4;", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.u16[5] = w5;", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.u16[6] = w6;", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.u16[7] = w7;", infoDst.BaseReg),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftVexOp(op x86asm.Op, args x86asm.Args, defMemSz int, nextPC uint64) ([]string, error) {
	// 2-operand moves
	switch op {
	case x86asm.VMOVAPS, x86asm.VMOVDQA, x86asm.VMOVDQU, x86asm.VMOVNTPS, x86asm.VMOVNTDQ:
		return l.liftVectorMove(args[0], args[1], nextPC)
	case x86asm.VMOVD:
		return l.liftMovd(args[0], args[1], nextPC)
	case x86asm.VMOVQ:
		return l.liftMovq(args[0], args[1], nextPC)
	case x86asm.VUCOMISS:
		return l.liftUcomis(false, args[0], args[1], nextPC)
	case x86asm.VUCOMISD:
		return l.liftUcomis(true, args[0], args[1], nextPC)
	case x86asm.VCVTTSS2SI:
		return l.liftCvttsd2si(false, args[0], args[1], defMemSz, nextPC)
	case x86asm.VCVTTSD2SI:
		return l.liftCvttsd2si(true, args[0], args[1], defMemSz, nextPC)
	case x86asm.VCVTDQ2PS:
		src := args[1]
		if args[2] != nil {
			src = args[2]
		}
		return l.liftCvtdq2ps(args[0], src, nextPC)
	case x86asm.VCVTPS2DQ:
		src := args[1]
		if args[2] != nil {
			src = args[2]
		}
		return l.liftCvtps2dq(args[0], src, nextPC)
	case x86asm.VBROADCASTSS:
		return l.liftVbroadcastss(args[0], args[1], nextPC)
	case x86asm.VPSHUFHW:
		return l.liftPshufhw(args[0], args[1], args[2], nextPC)
	case x86asm.VPSHUFLW:
		return l.liftPshuflw(args[0], args[1], args[2], nextPC)
	}

	// 2 or 3 operand scalar moves
	if op == x86asm.VMOVSS {
		if args[2] == nil {
			return l.liftMovss(args[0], args[1], nextPC)
		}
		// 3-operand: dst = src1; dst.f32[0] = src2.f32[0];
		dstReg, ok1 := args[0].(x86asm.Reg)
		src1Reg, ok2 := args[1].(x86asm.Reg)
		if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
			return nil, fmt.Errorf("vmovss invalid registers")
		}
		infoDst := regMap[dstReg]
		infoSrc1 := regMap[src1Reg]
		lines := []string{
			fmt.Sprintf("    ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
		}
		code, err := l.liftMovss(args[0], args[2], nextPC)
		if err != nil {
			return nil, err
		}
		return append(lines, code...), nil
	}

	if op == x86asm.VMOVSD {
		if args[2] == nil {
			return l.liftMovsd(args[0], args[1], nextPC)
		}
		dstReg, ok1 := args[0].(x86asm.Reg)
		src1Reg, ok2 := args[1].(x86asm.Reg)
		if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
			return nil, fmt.Errorf("vmovsd invalid registers")
		}
		infoDst := regMap[dstReg]
		infoSrc1 := regMap[src1Reg]
		lines := []string{
			fmt.Sprintf("    ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
		}
		code, err := l.liftMovsd(args[0], args[2], nextPC)
		if err != nil {
			return nil, err
		}
		return append(lines, code...), nil
	}

	// 4-operand VPINSR
	if op == x86asm.VPINSRB || op == x86asm.VPINSRW || op == x86asm.VPINSRD {
		dstReg, ok1 := args[0].(x86asm.Reg)
		src1Reg, ok2 := args[1].(x86asm.Reg)
		if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
			return nil, fmt.Errorf("vpinsr invalid registers")
		}
		infoDst := regMap[dstReg]
		infoSrc1 := regMap[src1Reg]
		var elemBytes int
		switch op {
		case x86asm.VPINSRB:
			elemBytes = 1
		case x86asm.VPINSRD:
			elemBytes = 4
		default:
			elemBytes = 2
		}
		lines := []string{
			fmt.Sprintf("    ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
		}
		code, err := l.liftPinsr(elemBytes, args[0], args[2], args[3], nextPC)
		if err != nil {
			return nil, err
		}
		return append(lines, code...), nil
	}

	// VROUNDSD dst, src1, src2, imm
	if op == x86asm.VROUNDSD {
		dstReg, ok1 := args[0].(x86asm.Reg)
		src1Reg, ok2 := args[1].(x86asm.Reg)
		imm, ok3 := args[3].(x86asm.Imm)
		if !ok1 || !ok2 || !ok3 || !isXmm(dstReg) || !isXmm(src1Reg) {
			return nil, fmt.Errorf("vroundsd invalid operands")
		}
		infoDst := regMap[dstReg]
		infoSrc1 := regMap[src1Reg]
		var sExpr string
		if src2Reg, ok := args[2].(x86asm.Reg); ok && isXmm(src2Reg) {
			infoSrc2 := regMap[src2Reg]
			sExpr = fmt.Sprintf("ctx->%s.f64[0]", infoSrc2.BaseReg)
		} else if src2Mem, ok := args[2].(x86asm.Mem); ok {
			addr, err := MemAddrExpr(src2Mem, nextPC)
			if err != nil {
				return nil, err
			}
			sExpr = fmt.Sprintf("({ double s; uint64_t u = MEM_U64(%s); memcpy(&s, &u, 8); s; })", addr)
		} else {
			return nil, fmt.Errorf("vroundsd invalid src2")
		}
		roundMode := imm & 3
		var roundFunc string
		switch roundMode {
		case 1:
			roundFunc = "floor"
		case 2:
			roundFunc = "ceil"
		case 3:
			roundFunc = "trunc"
		default:
			roundFunc = "round"
		}
		return []string{
			fmt.Sprintf("    ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("    ctx->%s.f64[0] = %s(%s);", infoDst.BaseReg, roundFunc, sExpr),
		}, nil
	}

	// VROUNDSS dst, src1, src2, imm
	if op == x86asm.VROUNDSS {
		dstReg, ok1 := args[0].(x86asm.Reg)
		src1Reg, ok2 := args[1].(x86asm.Reg)
		imm, ok3 := args[3].(x86asm.Imm)
		if !ok1 || !ok2 || !ok3 || !isXmm(dstReg) || !isXmm(src1Reg) {
			return nil, fmt.Errorf("vroundss invalid operands")
		}
		infoDst := regMap[dstReg]
		infoSrc1 := regMap[src1Reg]
		var sExpr string
		if src2Reg, ok := args[2].(x86asm.Reg); ok && isXmm(src2Reg) {
			infoSrc2 := regMap[src2Reg]
			sExpr = fmt.Sprintf("ctx->%s.f32[0]", infoSrc2.BaseReg)
		} else if src2Mem, ok := args[2].(x86asm.Mem); ok {
			addr, err := MemAddrExpr(src2Mem, nextPC)
			if err != nil {
				return nil, err
			}
			sExpr = fmt.Sprintf("({ float s; uint32_t u = MEM_U32(%s); memcpy(&s, &u, 4); s; })", addr)
		} else {
			return nil, fmt.Errorf("vroundss invalid src2")
		}
		roundMode := imm & 3
		var roundFunc string
		switch roundMode {
		case 1:
			roundFunc = "floorf"
		case 2:
			roundFunc = "ceilf"
		case 3:
			roundFunc = "truncf"
		default:
			roundFunc = "roundf"
		}
		return []string{
			fmt.Sprintf("    ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("    ctx->%s.f32[0] = %s(%s);", infoDst.BaseReg, roundFunc, sExpr),
		}, nil
	}

	// 3-operand VEX operations: dst = src1; op(dst, src2)
	dstReg, ok1 := args[0].(x86asm.Reg)
	src1Reg, ok2 := args[1].(x86asm.Reg)
	if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("3-operand VEX requires XMM dst and src1")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]

	lines := []string{
		fmt.Sprintf("    ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
	}

	var code []string
	var err error

	switch op {
	case x86asm.VADDSS:
		code, err = l.liftScalarF32("+", args[0], args[2], nextPC)
	case x86asm.VADDSD:
		code, err = l.liftScalarF64("+", args[0], args[2], nextPC)
	case x86asm.VSUBSS:
		code, err = l.liftScalarF32("-", args[0], args[2], nextPC)
	case x86asm.VSUBSD:
		code, err = l.liftScalarF64("-", args[0], args[2], nextPC)
	case x86asm.VMULSS:
		code, err = l.liftScalarF32("*", args[0], args[2], nextPC)
	case x86asm.VMULSD:
		code, err = l.liftScalarF64("*", args[0], args[2], nextPC)
	case x86asm.VDIVSS:
		code, err = l.liftScalarF32("/", args[0], args[2], nextPC)
	case x86asm.VDIVSD:
		code, err = l.liftScalarF64("/", args[0], args[2], nextPC)
	case x86asm.VADDPS:
		code, err = l.liftPackedF32("+", args[0], args[2], nextPC)
	case x86asm.VSUBPS:
		code, err = l.liftPackedF32("-", args[0], args[2], nextPC)
	case x86asm.VMULPS:
		code, err = l.liftPackedF32("*", args[0], args[2], nextPC)
	case x86asm.VDIVPS:
		code, err = l.liftPackedF32("/", args[0], args[2], nextPC)
	case x86asm.VMAXPS:
		code, err = l.liftPackedF32("max", args[0], args[2], nextPC)
	case x86asm.VMINPS:
		code, err = l.liftPackedF32("min", args[0], args[2], nextPC)
	case x86asm.VHADDPS:
		code, err = l.liftHaddps(args[0], args[1], args[2], nextPC)
	case x86asm.VXORPS, x86asm.VPXOR:
		code, err = l.liftVectorBitwise(" ^ ", args[0], args[2], nextPC)
	case x86asm.VPOR:
		code, err = l.liftVectorBitwise(" | ", args[0], args[2], nextPC)
	case x86asm.VPAND:
		code, err = l.liftVectorBitwise(" & ", args[0], args[2], nextPC)
	case x86asm.VPADDW:
		code, err = l.liftPadd(2, args[0], args[2], nextPC)
	case x86asm.VPSUBW:
		code, err = l.liftPsub(2, args[0], args[2], nextPC)
	case x86asm.VPMULLW:
		code, err = l.liftPmullw(args[0], args[2], nextPC)
	case x86asm.VPAVGB:
		code, err = l.liftPavgb(args[0], args[2], nextPC)
	case x86asm.VPAVGW:
		code, err = l.liftPavgw(args[0], args[2], nextPC)
	case x86asm.VPACKUSWB:
		code, err = l.liftPack(x86asm.PACKUSWB, args[0], args[2], nextPC)
	case x86asm.VPACKSSDW:
		code, err = l.liftPack(x86asm.PACKSSDW, args[0], args[2], nextPC)
	case x86asm.VPACKSSWB:
		code, err = l.liftPack(x86asm.PACKSSWB, args[0], args[2], nextPC)
	case x86asm.VPUNPCKLBW:
		code, err = l.liftPunpckl(1, args[0], args[2], nextPC)
	case x86asm.VPUNPCKHBW:
		code, err = l.liftPunpckh(1, args[0], args[2], nextPC)
	case x86asm.VPUNPCKLWD:
		code, err = l.liftPunpckl(2, args[0], args[2], nextPC)
	case x86asm.VPUNPCKHWD:
		code, err = l.liftPunpckh(2, args[0], args[2], nextPC)
	case x86asm.VPUNPCKLDQ, x86asm.VUNPCKLPS:
		code, err = l.liftPunpckl(4, args[0], args[2], nextPC)
	case x86asm.VPUNPCKHDQ, x86asm.VUNPCKHPS:
		code, err = l.liftPunpckh(4, args[0], args[2], nextPC)
	case x86asm.VPSLLW:
		code, err = l.liftPshiftW(x86asm.PSLLW, args[0], args[2], nextPC)
	case x86asm.VPSRLW:
		code, err = l.liftPshiftW(x86asm.PSRLW, args[0], args[2], nextPC)
	case x86asm.VPSRAW:
		code, err = l.liftPshiftW(x86asm.PSRAW, args[0], args[2], nextPC)
	case x86asm.VPSLLD:
		code, err = l.liftPshift("<<", args[0], args[2], nextPC)
	case x86asm.VPSRLD:
		code, err = l.liftPshift(">>", args[0], args[2], nextPC)
	case x86asm.VPSRAD:
		code, err = l.liftPsrad(args[0], args[2], nextPC)
	case x86asm.VCVTSI2SS:
		code, err = l.liftCvtsi2s(false, args[0], args[2], defMemSz, nextPC)
	case x86asm.VCVTSI2SD:
		code, err = l.liftCvtsi2s(true, args[0], args[2], defMemSz, nextPC)
	case x86asm.VCVTSS2SD:
		code, err = l.liftCvtss2sd(args[0], args[2], nextPC)
	case x86asm.VCVTSD2SS:
		code, err = l.liftCvtsd2ss(args[0], args[2], nextPC)
	case x86asm.VSQRTSS:
		code, err = l.liftSqrt(false, args[0], args[2], nextPC)
	case x86asm.VSQRTSD:
		code, err = l.liftSqrt(true, args[0], args[2], nextPC)
	default:
		return nil, fmt.Errorf("unsupported VEX op: %v", op)
	}

	if err != nil {
		return nil, err
	}
	return append(lines, code...), nil
}
