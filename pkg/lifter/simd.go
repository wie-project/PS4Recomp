package lifter

import (
	"fmt"

	"golang.org/x/arch/x86/x86asm"
)

func isXmm(reg x86asm.Reg) bool {
	info, ok := regMap[reg]
	return ok && info.Size == 16
}

func isYmm(reg x86asm.Reg) bool {
	return reg >= x86asm.Y0 && reg <= x86asm.Y15
}

func ymmIdx(reg x86asm.Reg) int {
	return int(reg - x86asm.Y0)
}

func xmmOrYmmIdx(reg x86asm.Reg) int {
	if isYmm(reg) {
		return int(reg - x86asm.Y0)
	}
	return int(reg - x86asm.X0)
}

func (l *Lifter) liftVectorMove(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	// 256-bit YMM register moves
	if dstReg, ok := dst.(x86asm.Reg); ok && isYmm(dstReg) {
		dIdx := ymmIdx(dstReg)
		if srcReg, ok := src.(x86asm.Reg); ok && isYmm(srcReg) {
			sIdx := ymmIdx(srcReg)
			return []string{
				fmt.Sprintf("    ctx->xmm[%d] = ctx->xmm[%d];", dIdx, sIdx),
				fmt.Sprintf("    ctx->ymmh[%d] = ctx->ymmh[%d];", dIdx, sIdx),
			}, nil
		}
		if srcMem, ok := src.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(srcMem, nextPC)
			if err != nil {
				return nil, err
			}
			return []string{
				fmt.Sprintf("    memcpy(&ctx->xmm[%d], ctx->mem_base + (%s), 16);", dIdx, addr),
				fmt.Sprintf("    memcpy(&ctx->ymmh[%d], ctx->mem_base + (%s) + 16, 16);", dIdx, addr),
			}, nil
		}
	}
	if dstMem, ok := dst.(x86asm.Mem); ok {
		if srcReg, ok := src.(x86asm.Reg); ok && isYmm(srcReg) {
			addr, err := MemAddrExpr(dstMem, nextPC)
			if err != nil {
				return nil, err
			}
			sIdx := ymmIdx(srcReg)
			return []string{
				fmt.Sprintf("    memcpy(ctx->mem_base + (%s), &ctx->xmm[%d], 16);", addr, sIdx),
				fmt.Sprintf("    memcpy(ctx->mem_base + (%s) + 16, &ctx->ymmh[%d], 16);", addr, sIdx),
			}, nil
		}
	}

	// 128-bit XMM register moves
	if dstReg, ok := dst.(x86asm.Reg); ok && isXmm(dstReg) {
		infoDst := regMap[dstReg]
		dIdx := int(dstReg - x86asm.X0)
		if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
			infoSrc := regMap[srcReg]
			return []string{
				fmt.Sprintf("    ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc.BaseReg),
				fmt.Sprintf("    memset(&ctx->ymmh[%d], 0, 16);", dIdx),
			}, nil
		}
		if srcMem, ok := src.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(srcMem, nextPC)
			if err != nil {
				return nil, err
			}
			return []string{
				fmt.Sprintf("    memcpy(&ctx->%s, ctx->mem_base + (%s), 16);", infoDst.BaseReg, addr),
				fmt.Sprintf("    memset(&ctx->ymmh[%d], 0, 16);", dIdx),
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
			return []string{fmt.Sprintf("    memcpy(ctx->mem_base + (%s), &ctx->%s, 16);", addr, infoSrc.BaseReg)}, nil
		}
	}
	return nil, fmt.Errorf("unsupported vector move operands")
}

func (l *Lifter) liftVectorXor(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	srcReg, ok2 := src.(x86asm.Reg)
	if ok1 && ok2 && dstReg == srcReg {
		if isXmm(dstReg) {
			info := regMap[dstReg]
			dIdx := int(dstReg - x86asm.X0)
			return []string{
				fmt.Sprintf("    memset(&ctx->%s, 0, 16);", info.BaseReg),
				fmt.Sprintf("    memset(&ctx->ymmh[%d], 0, 16);", dIdx),
			}, nil
		}
		if isYmm(dstReg) {
			dIdx := ymmIdx(dstReg)
			return []string{
				fmt.Sprintf("    memset(&ctx->xmm[%d], 0, 16);", dIdx),
				fmt.Sprintf("    memset(&ctx->ymmh[%d], 0, 16);", dIdx),
			}, nil
		}
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

func (l *Lifter) liftPcmpgtq(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("pcmpgtq dst must be XMM register")
	}
	infoDst := regMap[dstReg]

	var srcExprs [2]string
	if srcReg, ok2 := src.(x86asm.Reg); ok2 && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		for i := range 2 {
			srcExprs[i] = fmt.Sprintf("ctx->%s.u64[%d]", infoSrc.BaseReg, i)
		}
	} else if srcMem, ok2 := src.(x86asm.Mem); ok2 {
		addrExpr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		for i := range 2 {
			srcExprs[i] = fmt.Sprintf("MEM_U64((%s) + %d)", addrExpr, i*8)
		}
	} else {
		return nil, fmt.Errorf("unsupported pcmpgtq operands")
	}

	var lines []string
	for i := range 2 {
		lines = append(lines, fmt.Sprintf("    ctx->%s.u64[%d] = ((int64_t)ctx->%s.u64[%d] > (int64_t)(%s)) ? 0xFFFFFFFFFFFFFFFFULL : 0;",
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
		if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
			infoSrc := regMap[srcReg]
			return []string{
				fmt.Sprintf("    { uint32_t val = ctx->%s.u32[0]; memset(&ctx->%s, 0, 16); ctx->%s.u32[0] = val; }", infoSrc.BaseReg, infoDst.BaseReg, infoDst.BaseReg),
			}, nil
		}
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
		if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
			infoSrc := regMap[srcReg]
			return []string{
				fmt.Sprintf("    { uint64_t val = ctx->%s.u64[0]; memset(&ctx->%s, 0, 16); ctx->%s.u64[0] = val; }", infoSrc.BaseReg, infoDst.BaseReg, infoDst.BaseReg),
			}, nil
		}
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
				fmt.Sprintf("    memset(&ctx->%s, 0, 16);", infoDst.BaseReg),
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
				fmt.Sprintf("    memset(&ctx->%s, 0, 16);", infoDst.BaseReg),
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

func (l *Lifter) liftScalar3OpF32(opStr string, dstReg, src1Reg x86asm.Reg, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	if !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("scalar 3-op f32 dst and src1 must be XMM registers")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	var lines []string
	lines = append(lines, "    {")
	lines = append(lines, fmt.Sprintf("      float s1 = ctx->%s.f32[0];", infoSrc1.BaseReg))
	if s2Reg, ok := src2.(x86asm.Reg); ok && isXmm(s2Reg) {
		infoSrc2 := regMap[s2Reg]
		lines = append(lines, fmt.Sprintf("      float s2 = ctx->%s.f32[0];", infoSrc2.BaseReg))
	} else if s2Mem, ok := src2.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(s2Mem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(lines,
			"      float s2;",
			fmt.Sprintf("      uint32_t u = MEM_U32(%s);", addr),
			"      memcpy(&s2, &u, 4);",
		)
	} else {
		return nil, fmt.Errorf("unsupported scalar 3-op f32 src2: %v", src2)
	}
	lines = append(lines,
		fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
		fmt.Sprintf("      ctx->%s.f32[0] = s1 %s s2;", infoDst.BaseReg, opStr),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", int(dstReg-x86asm.X0)),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftScalar3OpF64(opStr string, dstReg, src1Reg x86asm.Reg, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	if !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("scalar 3-op f64 dst and src1 must be XMM registers")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	var lines []string
	lines = append(lines, "    {")
	lines = append(lines, fmt.Sprintf("      double s1 = ctx->%s.f64[0];", infoSrc1.BaseReg))
	if s2Reg, ok := src2.(x86asm.Reg); ok && isXmm(s2Reg) {
		infoSrc2 := regMap[s2Reg]
		lines = append(lines, fmt.Sprintf("      double s2 = ctx->%s.f64[0];", infoSrc2.BaseReg))
	} else if s2Mem, ok := src2.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(s2Mem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(lines,
			"      double s2;",
			fmt.Sprintf("      uint64_t u = MEM_U64(%s);", addr),
			"      memcpy(&s2, &u, 8);",
		)
	} else {
		return nil, fmt.Errorf("unsupported scalar 3-op f64 src2: %v", src2)
	}
	lines = append(lines,
		fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
		fmt.Sprintf("      ctx->%s.f64[0] = s1 %s s2;", infoDst.BaseReg, opStr),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", int(dstReg-x86asm.X0)),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftMinMax3Op(isMin bool, isDouble bool, dstReg, src1Reg x86asm.Reg, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	if !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("min/max 3-op dst and src1 must be XMM registers")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	cmpOp := "<"
	if !isMin {
		cmpOp = ">"
	}
	var lines []string
	lines = append(lines, "    {")
	if isDouble {
		lines = append(lines, fmt.Sprintf("      double s1 = ctx->%s.f64[0];", infoSrc1.BaseReg))
		if s2Reg, ok := src2.(x86asm.Reg); ok && isXmm(s2Reg) {
			infoSrc2 := regMap[s2Reg]
			lines = append(lines, fmt.Sprintf("      double s2 = ctx->%s.f64[0];", infoSrc2.BaseReg))
		} else if s2Mem, ok := src2.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(s2Mem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(lines,
				"      double s2;",
				fmt.Sprintf("      uint64_t u = MEM_U64(%s);", addr),
				"      memcpy(&s2, &u, 8);",
			)
		} else {
			return nil, fmt.Errorf("unsupported min/max 3-op src2: %v", src2)
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("      if (s2 %s s1) ctx->%s.f64[0] = s2;", cmpOp, infoDst.BaseReg),
			fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", int(dstReg-x86asm.X0)),
			"    }",
		)
	} else {
		lines = append(lines, fmt.Sprintf("      float s1 = ctx->%s.f32[0];", infoSrc1.BaseReg))
		if s2Reg, ok := src2.(x86asm.Reg); ok && isXmm(s2Reg) {
			infoSrc2 := regMap[s2Reg]
			lines = append(lines, fmt.Sprintf("      float s2 = ctx->%s.f32[0];", infoSrc2.BaseReg))
		} else if s2Mem, ok := src2.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(s2Mem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(lines,
				"      float s2;",
				fmt.Sprintf("      uint32_t u = MEM_U32(%s);", addr),
				"      memcpy(&s2, &u, 4);",
			)
		} else {
			return nil, fmt.Errorf("unsupported min/max 3-op src2: %v", src2)
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("      if (s2 %s s1) ctx->%s.f32[0] = s2;", cmpOp, infoDst.BaseReg),
			fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", int(dstReg-x86asm.X0)),
			"    }",
		)
	}
	return lines, nil
}

func (l *Lifter) liftSqrt3Op(isDouble bool, dstReg, src1Reg x86asm.Reg, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	if !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("sqrt 3-op dst and src1 must be XMM registers")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	var lines []string
	lines = append(lines, "    {")
	if isDouble {
		if s2Reg, ok := src2.(x86asm.Reg); ok && isXmm(s2Reg) {
			infoSrc2 := regMap[s2Reg]
			lines = append(lines, fmt.Sprintf("      double s2 = ctx->%s.f64[0];", infoSrc2.BaseReg))
		} else if s2Mem, ok := src2.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(s2Mem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(lines,
				"      double s2;",
				fmt.Sprintf("      uint64_t u = MEM_U64(%s);", addr),
				"      memcpy(&s2, &u, 8);",
			)
		} else {
			return nil, fmt.Errorf("unsupported sqrt 3-op src2: %v", src2)
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("      ctx->%s.f64[0] = sqrt(s2);", infoDst.BaseReg),
			fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", int(dstReg-x86asm.X0)),
			"    }",
		)
	} else {
		if s2Reg, ok := src2.(x86asm.Reg); ok && isXmm(s2Reg) {
			infoSrc2 := regMap[s2Reg]
			lines = append(lines, fmt.Sprintf("      float s2 = ctx->%s.f32[0];", infoSrc2.BaseReg))
		} else if s2Mem, ok := src2.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(s2Mem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(lines,
				"      float s2;",
				fmt.Sprintf("      uint32_t u = MEM_U32(%s);", addr),
				"      memcpy(&s2, &u, 4);",
			)
		} else {
			return nil, fmt.Errorf("unsupported sqrt 3-op src2: %v", src2)
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("      ctx->%s.f32[0] = sqrtf(s2);", infoDst.BaseReg),
			fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", int(dstReg-x86asm.X0)),
			"    }",
		)
	}
	return lines, nil
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
	if !ok || (!isXmm(dstReg) && !isYmm(dstReg)) {
		return nil, fmt.Errorf("cvtdq2ps dst must be XMM or YMM register")
	}
	var lines []string
	lines = append(lines, "    {")
	if isYmm(dstReg) {
		dIdx := ymmIdx(dstReg)
		if srcReg, ok := src.(x86asm.Reg); ok && isYmm(srcReg) {
			sIdx := ymmIdx(srcReg)
			lines = append(lines,
				fmt.Sprintf("      xmm_reg_t src_lo = ctx->xmm[%d];", sIdx),
				fmt.Sprintf("      xmm_reg_t src_hi = ctx->ymmh[%d];", sIdx),
			)
		} else if srcMem, ok := src.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(srcMem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(lines,
				"      xmm_reg_t src_lo, src_hi;",
				fmt.Sprintf("      memcpy(&src_lo, ctx->mem_base + (%s), 16);", addr),
				fmt.Sprintf("      memcpy(&src_hi, ctx->mem_base + (%s) + 16, 16);", addr),
			)
		} else {
			return nil, fmt.Errorf("cvtdq2ps invalid src")
		}
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      ctx->xmm[%d].f32[%d] = (float)src_lo.s32[%d];", dIdx, i, i),
				fmt.Sprintf("      ctx->ymmh[%d].f32[%d] = (float)src_hi.s32[%d];", dIdx, i, i),
			)
		}
		lines = append(lines, "    }")
		return lines, nil
	}

	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)
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
	lines = append(lines, fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx), "    }")
	return lines, nil
}

func (l *Lifter) liftCvtps2dq(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || (!isXmm(dstReg) && !isYmm(dstReg)) {
		return nil, fmt.Errorf("cvtps2dq dst must be XMM or YMM register")
	}
	var lines []string
	lines = append(lines, "    {")
	if isYmm(dstReg) {
		dIdx := ymmIdx(dstReg)
		if srcReg, ok := src.(x86asm.Reg); ok && isYmm(srcReg) {
			sIdx := ymmIdx(srcReg)
			lines = append(lines,
				fmt.Sprintf("      xmm_reg_t src_lo = ctx->xmm[%d];", sIdx),
				fmt.Sprintf("      xmm_reg_t src_hi = ctx->ymmh[%d];", sIdx),
			)
		} else if srcMem, ok := src.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(srcMem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(lines,
				"      xmm_reg_t src_lo, src_hi;",
				fmt.Sprintf("      memcpy(&src_lo, ctx->mem_base + (%s), 16);", addr),
				fmt.Sprintf("      memcpy(&src_hi, ctx->mem_base + (%s) + 16, 16);", addr),
			)
		} else {
			return nil, fmt.Errorf("cvtps2dq invalid src")
		}
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      ctx->xmm[%d].s32[%d] = (int32_t)roundf(src_lo.f32[%d]);", dIdx, i, i),
				fmt.Sprintf("      ctx->ymmh[%d].s32[%d] = (int32_t)roundf(src_hi.f32[%d]);", dIdx, i, i),
			)
		}
		lines = append(lines, "    }")
		return lines, nil
	}

	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)
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
	lines = append(lines, fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx), "    }")
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
	if !ok || (!isXmm(dstReg) && !isYmm(dstReg)) {
		return nil, fmt.Errorf("vbroadcastss dst must be XMM or YMM register")
	}
	var lines []string
	lines = append(lines, "    {", "      float val;")
	if srcReg, ok := src.(x86asm.Reg); ok && (isXmm(srcReg) || isYmm(srcReg)) {
		if isXmm(srcReg) {
			infoSrc := regMap[srcReg]
			lines = append(lines, fmt.Sprintf("      val = ctx->%s.f32[0];", infoSrc.BaseReg))
		} else {
			sIdx := ymmIdx(srcReg)
			lines = append(lines, fmt.Sprintf("      val = ctx->xmm[%d].f32[0];", sIdx))
		}
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
	if isYmm(dstReg) {
		dIdx := ymmIdx(dstReg)
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      ctx->xmm[%d].f32[%d] = val;", dIdx, i),
				fmt.Sprintf("      ctx->ymmh[%d].f32[%d] = val;", dIdx, i),
			)
		}
	} else {
		infoDst := regMap[dstReg]
		dIdx := int(dstReg - x86asm.X0)
		for i := 0; i < 4; i++ {
			lines = append(lines, fmt.Sprintf("      ctx->%s.f32[%d] = val;", infoDst.BaseReg, i))
		}
		lines = append(lines, fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx))
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

func (l *Lifter) liftHaddpd(dst, src1, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("haddpd dst must be XMM register")
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
		return nil, fmt.Errorf("haddpd invalid src1")
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
		return nil, fmt.Errorf("haddpd invalid src2")
	}

	lines = append(
		lines,
		fmt.Sprintf("      ctx->%s.f64[0] = s1.f64[0] + s1.f64[1];", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.f64[1] = s2.f64[0] + s2.f64[1];", infoDst.BaseReg),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftCvtps2pd(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || (!isXmm(dstReg) && !isYmm(dstReg)) {
		return nil, fmt.Errorf("cvtps2pd dst must be XMM or YMM register")
	}

	lines := []string{"    {"}
	if isYmm(dstReg) {
		dIdx := ymmIdx(dstReg)
		if srcReg, ok := src.(x86asm.Reg); ok && (isXmm(srcReg) || isYmm(srcReg)) {
			sIdx := int(srcReg - x86asm.X0)
			if isYmm(srcReg) {
				sIdx = ymmIdx(srcReg)
			}
			lines = append(
				lines,
				fmt.Sprintf("      float f0 = ctx->xmm[%d].f32[0];", sIdx),
				fmt.Sprintf("      float f1 = ctx->xmm[%d].f32[1];", sIdx),
				fmt.Sprintf("      float f2 = ctx->xmm[%d].f32[2];", sIdx),
				fmt.Sprintf("      float f3 = ctx->xmm[%d].f32[3];", sIdx),
			)
		} else if srcMem, ok := src.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(srcMem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(
				lines,
				fmt.Sprintf("      float f0 = *(float *)(ctx->mem_base + (%s));", addr),
				fmt.Sprintf("      float f1 = *(float *)(ctx->mem_base + (%s) + 4);", addr),
				fmt.Sprintf("      float f2 = *(float *)(ctx->mem_base + (%s) + 8);", addr),
				fmt.Sprintf("      float f3 = *(float *)(ctx->mem_base + (%s) + 12);", addr),
			)
		} else {
			return nil, fmt.Errorf("cvtps2pd invalid src")
		}
		lines = append(
			lines,
			fmt.Sprintf("      ctx->xmm[%d].f64[0] = (double)f0;", dIdx),
			fmt.Sprintf("      ctx->xmm[%d].f64[1] = (double)f1;", dIdx),
			fmt.Sprintf("      ctx->ymmh[%d].f64[0] = (double)f2;", dIdx),
			fmt.Sprintf("      ctx->ymmh[%d].f64[1] = (double)f3;", dIdx),
			"    }",
		)
		return lines, nil
	}

	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)
	if srcReg, ok := src.(x86asm.Reg); ok && (isXmm(srcReg) || isYmm(srcReg)) {
		sIdx := int(srcReg - x86asm.X0)
		if isYmm(srcReg) {
			sIdx = ymmIdx(srcReg)
		}
		lines = append(
			lines,
			fmt.Sprintf("      float f0 = ctx->xmm[%d].f32[0];", sIdx),
			fmt.Sprintf("      float f1 = ctx->xmm[%d].f32[1];", sIdx),
		)
	} else if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(
			lines,
			fmt.Sprintf("      float f0 = *(float *)(ctx->mem_base + (%s));", addr),
			fmt.Sprintf("      float f1 = *(float *)(ctx->mem_base + (%s) + 4);", addr),
		)
	} else {
		return nil, fmt.Errorf("cvtps2pd invalid src")
	}
	lines = append(
		lines,
		fmt.Sprintf("      ctx->%s.f64[0] = (double)f0;", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.f64[1] = (double)f1;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftCvtpd2ps(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("cvtpd2ps dst must be XMM register")
	}
	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)

	// Check if source is 256-bit (YMM register)
	if srcReg, ok := src.(x86asm.Reg); ok && isYmm(srcReg) {
		sIdx := ymmIdx(srcReg)
		return []string{
			"    {",
			fmt.Sprintf("      ctx->%s.f32[0] = (float)ctx->xmm[%d].f64[0];", infoDst.BaseReg, sIdx),
			fmt.Sprintf("      ctx->%s.f32[1] = (float)ctx->xmm[%d].f64[1];", infoDst.BaseReg, sIdx),
			fmt.Sprintf("      ctx->%s.f32[2] = (float)ctx->ymmh[%d].f64[0];", infoDst.BaseReg, sIdx),
			fmt.Sprintf("      ctx->%s.f32[3] = (float)ctx->ymmh[%d].f64[1];", infoDst.BaseReg, sIdx),
			fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
			"    }",
		}, nil
	}

	lines := []string{"    {"}
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		infoSrc := regMap[srcReg]
		lines = append(
			lines,
			fmt.Sprintf("      double d0 = ctx->%s.f64[0];", infoSrc.BaseReg),
			fmt.Sprintf("      double d1 = ctx->%s.f64[1];", infoSrc.BaseReg),
		)
	} else if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(
			lines,
			fmt.Sprintf("      double d0 = *(double *)(ctx->mem_base + (%s));", addr),
			fmt.Sprintf("      double d1 = *(double *)(ctx->mem_base + (%s) + 8);", addr),
		)
	} else {
		return nil, fmt.Errorf("cvtpd2ps invalid src")
	}
	lines = append(
		lines,
		fmt.Sprintf("      ctx->%s.f32[0] = (float)d0;", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.f32[1] = (float)d1;", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.u64[1] = 0;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
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
	switch opStr {
	case "+", "-", "*", "/":
		lines = append(
			lines,
			fmt.Sprintf("      ctx->%s.f64[0] %s= src.f64[0];", infoDst.BaseReg, opStr),
			fmt.Sprintf("      ctx->%s.f64[1] %s= src.f64[1];", infoDst.BaseReg, opStr),
		)
	case "min":
		lines = append(
			lines,
			fmt.Sprintf("      if (src.f64[0] < ctx->%s.f64[0]) ctx->%s.f64[0] = src.f64[0];", infoDst.BaseReg, infoDst.BaseReg),
			fmt.Sprintf("      if (src.f64[1] < ctx->%s.f64[1]) ctx->%s.f64[1] = src.f64[1];", infoDst.BaseReg, infoDst.BaseReg),
		)
	case "max":
		lines = append(
			lines,
			fmt.Sprintf("      if (src.f64[0] > ctx->%s.f64[0]) ctx->%s.f64[0] = src.f64[0];", infoDst.BaseReg, infoDst.BaseReg),
			fmt.Sprintf("      if (src.f64[1] > ctx->%s.f64[1]) ctx->%s.f64[1] = src.f64[1];", infoDst.BaseReg, infoDst.BaseReg),
		)
	}
	lines = append(lines, "    }")
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
	if !ok || (!isXmm(dstReg) && !isMmx(dstReg)) {
		return nil, fmt.Errorf("padd dst must be XMM or MMX register")
	}

	if isMmx(dstReg) {
		mIdx := mmxIdx(dstReg)
		sRead, _, err := l.getOperandRead(src, 8, nextPC)
		if err != nil {
			return nil, err
		}
		var lines []string
		lines = append(lines,
			"    {",
			fmt.Sprintf("      uint64_t s = (uint64_t)(%s);", sRead),
			fmt.Sprintf("      uint64_t d = ctx->mmx[%d];", mIdx),
		)
		switch elemBytes {
		case 1:
			lines = append(lines,
				"      uint8_t *pd = (uint8_t *)&d; uint8_t *ps = (uint8_t *)&s;",
				"      for (int i = 0; i < 8; i++) pd[i] += ps[i];",
			)
		case 2:
			lines = append(lines,
				"      uint16_t *pd = (uint16_t *)&d; uint16_t *ps = (uint16_t *)&s;",
				"      for (int i = 0; i < 4; i++) pd[i] += ps[i];",
			)
		case 4:
			lines = append(lines,
				"      uint32_t *pd = (uint32_t *)&d; uint32_t *ps = (uint32_t *)&s;",
				"      for (int i = 0; i < 2; i++) pd[i] += ps[i];",
			)
		case 8:
			lines = append(lines, "      d += s;")
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->mmx[%d] = d;", mIdx),
			"    }",
		)
		return lines, nil
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

func (l *Lifter) liftCvtFloatToInt(trunc, isDouble bool, dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
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

	conv := srcExpr
	if !trunc {
		if isDouble {
			conv = fmt.Sprintf("llrint(%s)", srcExpr)
		} else {
			conv = fmt.Sprintf("llrintf(%s)", srcExpr)
		}
	}
	writeStmts, err := l.getOperandWrite(dst, sz, fmt.Sprintf("(%s)(%s)", castType, conv), nextPC)
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
	if !ok || (!isXmm(dstReg) && !isMmx(dstReg)) {
		return nil, fmt.Errorf("psub dst must be XMM or MMX register")
	}

	if isMmx(dstReg) {
		mIdx := mmxIdx(dstReg)
		sRead, _, err := l.getOperandRead(src, 8, nextPC)
		if err != nil {
			return nil, err
		}
		var lines []string
		lines = append(lines,
			"    {",
			fmt.Sprintf("      uint64_t s = (uint64_t)(%s);", sRead),
			fmt.Sprintf("      uint64_t d = ctx->mmx[%d];", mIdx),
		)
		switch elemBytes {
		case 1:
			lines = append(lines,
				"      uint8_t *pd = (uint8_t *)&d; uint8_t *ps = (uint8_t *)&s;",
				"      for (int i = 0; i < 8; i++) pd[i] -= ps[i];",
			)
		case 2:
			lines = append(lines,
				"      uint16_t *pd = (uint16_t *)&d; uint16_t *ps = (uint16_t *)&s;",
				"      for (int i = 0; i < 4; i++) pd[i] -= ps[i];",
			)
		case 4:
			lines = append(lines,
				"      uint32_t *pd = (uint32_t *)&d; uint32_t *ps = (uint32_t *)&s;",
				"      for (int i = 0; i < 2; i++) pd[i] -= ps[i];",
			)
		case 8:
			lines = append(lines, "      d -= s;")
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->mmx[%d] = d;", mIdx),
			"    }",
		)
		return lines, nil
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
	if !ok1 || !ok2 || (!isXmm(dstReg) && !isMmx(dstReg)) {
		return nil, fmt.Errorf("pinsr invalid operands")
	}

	sRead, _, err := l.getOperandRead(src, elemBytes, nextPC)
	if err != nil {
		return nil, err
	}

	if isMmx(dstReg) {
		mIdx := mmxIdx(dstReg)
		switch elemBytes {
		case 2:
			lane := imm & 0x3
			mask := ^(uint64(0xFFFF) << (lane * 16))
			return []string{
				fmt.Sprintf("    ctx->mmx[%d] = (ctx->mmx[%d] & 0x%xULL) | (((uint64_t)(uint16_t)(%s)) << %d);",
					mIdx, mIdx, mask, sRead, lane*16),
			}, nil
		case 1:
			lane := imm & 0x7
			mask := ^(uint64(0xFF) << (lane * 8))
			return []string{
				fmt.Sprintf("    ctx->mmx[%d] = (ctx->mmx[%d] & 0x%xULL) | (((uint64_t)(uint8_t)(%s)) << %d);",
					mIdx, mIdx, mask, sRead, lane*8),
			}, nil
		}
	}

	infoDst := regMap[dstReg]
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
	if !ok || (!isXmm(dstReg) && !isMmx(dstReg)) {
		return nil, fmt.Errorf("vector word shift destination must be XMM or MMX")
	}

	var cntExpr string
	if reg, ok := countArg.(x86asm.Reg); ok && isXmm(reg) {
		infoC := regMap[reg]
		cntExpr = fmt.Sprintf("ctx->%s.u64[0]", infoC.BaseReg)
	} else if reg, ok := countArg.(x86asm.Reg); ok && isMmx(reg) {
		cntExpr = fmt.Sprintf("ctx->mmx[%d]", mmxIdx(reg))
	} else if imm, ok := countArg.(x86asm.Imm); ok {
		cntExpr = fmt.Sprintf("%d", imm)
	} else {
		cRead, _, err := l.getOperandRead(countArg, 1, nextPC)
		if err != nil {
			return nil, err
		}
		cntExpr = cRead
	}

	if isMmx(dstReg) {
		mIdx := mmxIdx(dstReg)
		lines := []string{
			"    {",
			fmt.Sprintf("      uint32_t shift = (uint32_t)(%s);", cntExpr),
		}
		switch op {
		case x86asm.PSLLW:
			lines = append(lines,
				"      if (shift < 16) {",
				fmt.Sprintf("        uint16_t *p = (uint16_t *)&ctx->mmx[%d];", mIdx),
				"        for (int i = 0; i < 4; i++) p[i] <<= shift;",
				"      } else {",
				fmt.Sprintf("        ctx->mmx[%d] = 0;", mIdx),
				"      }",
			)
		case x86asm.PSRLW:
			lines = append(lines,
				"      if (shift < 16) {",
				fmt.Sprintf("        uint16_t *p = (uint16_t *)&ctx->mmx[%d];", mIdx),
				"        for (int i = 0; i < 4; i++) p[i] >>= shift;",
				"      } else {",
				fmt.Sprintf("        ctx->mmx[%d] = 0;", mIdx),
				"      }",
			)
		case x86asm.PSRAW:
			lines = append(lines,
				"      if (shift >= 16) shift = 15;",
				fmt.Sprintf("      int16_t *p = (int16_t *)&ctx->mmx[%d];", mIdx),
				"      for (int i = 0; i < 4; i++) p[i] >>= shift;",
			)
		}
		lines = append(lines, "    }")
		return lines, nil
	}

	infoDst := regMap[dstReg]

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
	case x86asm.VMOVAPS, x86asm.VMOVUPS, x86asm.VMOVUPD, x86asm.VMOVAPD, x86asm.VMOVDQA, x86asm.VMOVDQU, x86asm.VMOVNTPS, x86asm.VMOVNTDQ, x86asm.VMOVNTDQA, x86asm.VMOVNTPD:
		return l.liftVectorMove(args[0], args[1], nextPC)
	case x86asm.VEXTRACTF128, x86asm.VEXTRACTI128:
		return l.liftVextract128(args[0], args[1], args[2], nextPC)
	case x86asm.VINSERTF128, x86asm.VINSERTI128:
		return l.liftVinsert128(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VRSQRTPS:
		return l.liftRsqrtPacked(args[0], args[1], nextPC)
	case x86asm.VMOVDDUP:
		return l.liftMovddup(args[0], args[1], nextPC)
	case x86asm.VCVTSS2SI:
		src := args[1]
		if args[2] != nil {
			src = args[2]
		}
		return l.liftCvtFloatToInt(false, false, args[0], src, defMemSz, nextPC)
	case x86asm.VCVTSD2SI:
		src := args[1]
		if args[2] != nil {
			src = args[2]
		}
		return l.liftCvtFloatToInt(false, true, args[0], src, defMemSz, nextPC)
	case x86asm.VCVTPS2PD:
		src := args[1]
		if args[2] != nil {
			src = args[2]
		}
		return l.liftCvtps2pd(args[0], src, nextPC)
	case x86asm.VCVTPD2PS:
		src := args[1]
		if args[2] != nil {
			src = args[2]
		}
		return l.liftCvtpd2ps(args[0], src, nextPC)
	case x86asm.VMOVLPS:
		if args[2] == nil {
			return l.liftMovlpd(args[0], args[1], nextPC)
		}
		dstReg, ok1 := args[0].(x86asm.Reg)
		src1Reg, ok2 := args[1].(x86asm.Reg)
		if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
			return nil, fmt.Errorf("vmovlps invalid registers")
		}
		infoDst := regMap[dstReg]
		infoSrc1 := regMap[src1Reg]
		lines := []string{fmt.Sprintf("    ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg)}
		code, err := l.liftMovlpd(args[0], args[2], nextPC)
		if err != nil {
			return nil, err
		}
		return append(lines, code...), nil
	case x86asm.VMOVSHDUP:
		return l.liftMovshdup(args[0], args[1], nextPC)
	case x86asm.VMOVSLDUP:
		return l.liftMovsldup(args[0], args[1], nextPC)
	case x86asm.VEXTRACTPS:
		return l.liftExtractps(args[0], args[1], args[2], nextPC)
	case x86asm.VCMPSS:
		return l.liftVcmpss(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VCMPSD:
		return l.liftVcmpsd(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VPBLENDVB:
		return l.liftVpblendvb(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VSHUFPD:
		return l.liftShufpd(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VPSHUFD:
		return l.liftPshufd(args[0], args[1], args[2], nextPC)
	case x86asm.VPSHUFB:
		return l.liftPshufb(args[0], args[1], args[2], nextPC)
	case x86asm.VPALIGNR:
		return l.liftPalignr(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VMOVLHPS:
		return l.liftMovlhps(args[0], args[1], args[2])
	case x86asm.VMOVHLPS:
		return l.liftMovhlps(args[0], args[1], args[2])
	case x86asm.VPEXTRB:
		return l.liftPextr(1, args[0], args[1], args[2], nextPC)
	case x86asm.VPEXTRW:
		return l.liftPextr(2, args[0], args[1], args[2], nextPC)
	case x86asm.VPEXTRD:
		return l.liftPextr(4, args[0], args[1], args[2], nextPC)
	case x86asm.VPEXTRQ:
		return l.liftPextr(8, args[0], args[1], args[2], nextPC)
	case x86asm.VSQRTPS:
		return l.liftPackedSqrt(false, args[0], args[1], nextPC)
	case x86asm.VSQRTPD:
		return l.liftPackedSqrt(true, args[0], args[1], nextPC)
	case x86asm.VROUNDPS:
		return l.liftRoundps(args[0], args[1], args[2], nextPC)
	case x86asm.VPTEST:
		return l.liftPtest(args[0], args[1], nextPC)
	case x86asm.VTESTPS:
		return l.liftVtestp(false, args[0], args[1], nextPC)
	case x86asm.VTESTPD:
		return l.liftVtestp(true, args[0], args[1], nextPC)
	case x86asm.VMOVHPD, x86asm.VMOVHPS:
		if args[2] == nil {
			return l.liftMovhpd(args[0], args[1], nextPC)
		}
		dstReg, ok1 := args[0].(x86asm.Reg)
		src1Reg, ok2 := args[1].(x86asm.Reg)
		if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
			return nil, fmt.Errorf("vmovhpd invalid registers")
		}
		infoDst := regMap[dstReg]
		infoSrc1 := regMap[src1Reg]
		lines := []string{fmt.Sprintf("    ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg)}
		code, err := l.liftMovhpd(args[0], args[2], nextPC)
		if err != nil {
			return nil, err
		}
		return append(lines, code...), nil
	case x86asm.VMOVMSKPS:
		return l.liftMovmskps(args[0], args[1], nextPC)
	case x86asm.VPERMILPS:
		if _, ok := args[2].(x86asm.Imm); ok {
			return l.liftPermilpsImm(args[0], args[1], args[2], nextPC)
		}
		dstReg, ok1 := args[0].(x86asm.Reg)
		if !ok1 || (!isXmm(dstReg) && !isYmm(dstReg)) {
			return nil, fmt.Errorf("vpermilps destination must be XMM or YMM")
		}
		lines := []string{"    {"}
		if isYmm(dstReg) {
			dIdx := ymmIdx(dstReg)
			s1, err := l.loadYmmArg(args[1], nextPC, "src")
			if err != nil {
				return nil, err
			}
			s2, err := l.loadYmmArg(args[2], nextPC, "sel")
			if err != nil {
				return nil, err
			}
			lines = append(lines, s1...)
			lines = append(lines, s2...)
			for i := 0; i < 4; i++ {
				lines = append(lines,
					fmt.Sprintf("      ctx->xmm[%d].f32[%d] = src_lo.f32[sel_lo.u32[%d] & 3];", dIdx, i, i),
					fmt.Sprintf("      ctx->ymmh[%d].f32[%d] = src_hi.f32[sel_hi.u32[%d] & 3];", dIdx, i, i),
				)
			}
			lines = append(lines, "    }")
			return lines, nil
		}
		infoDst := regMap[dstReg]
		s1, err := l.loadXmmArg(args[1], nextPC, "src")
		if err != nil {
			return nil, err
		}
		s2, err := l.loadXmmArg(args[2], nextPC, "sel")
		if err != nil {
			return nil, err
		}
		lines = append(lines, s1...)
		lines = append(lines, s2...)
		for i := 0; i < 4; i++ {
			lines = append(lines, fmt.Sprintf("      ctx->%s.f32[%d] = src.f32[sel.u32[%d] & 3];", infoDst.BaseReg, i, i))
		}
		dIdx := int(dstReg - x86asm.X0)
		lines = append(lines, fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx), "    }")
		return lines, nil
	case x86asm.VPERMILPD:
		if _, ok := args[2].(x86asm.Imm); ok {
			return l.liftPermilpdImm(args[0], args[1], args[2], nextPC)
		}
		dstReg, ok1 := args[0].(x86asm.Reg)
		if !ok1 || (!isXmm(dstReg) && !isYmm(dstReg)) {
			return nil, fmt.Errorf("vpermilpd destination must be XMM or YMM")
		}
		lines := []string{"    {"}
		if isYmm(dstReg) {
			dIdx := ymmIdx(dstReg)
			s1, err := l.loadYmmArg(args[1], nextPC, "src")
			if err != nil {
				return nil, err
			}
			s2, err := l.loadYmmArg(args[2], nextPC, "sel")
			if err != nil {
				return nil, err
			}
			lines = append(lines, s1...)
			lines = append(lines, s2...)
			for i := 0; i < 2; i++ {
				lines = append(lines,
					fmt.Sprintf("      ctx->xmm[%d].f64[%d] = src_lo.f64[(sel_lo.u64[%d] >> 1) & 1];", dIdx, i, i),
					fmt.Sprintf("      ctx->ymmh[%d].f64[%d] = src_hi.f64[(sel_hi.u64[%d] >> 1) & 1];", dIdx, i, i),
				)
			}
			lines = append(lines, "    }")
			return lines, nil
		}
		infoDst := regMap[dstReg]
		s1, err := l.loadXmmArg(args[1], nextPC, "src")
		if err != nil {
			return nil, err
		}
		s2, err := l.loadXmmArg(args[2], nextPC, "sel")
		if err != nil {
			return nil, err
		}
		lines = append(lines, s1...)
		lines = append(lines, s2...)
		for i := 0; i < 2; i++ {
			lines = append(lines, fmt.Sprintf("      ctx->%s.f64[%d] = src.f64[(sel.u64[%d] >> 1) & 1];", infoDst.BaseReg, i, i))
		}
		dIdx := int(dstReg - x86asm.X0)
		lines = append(lines, fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx), "    }")
		return lines, nil
	case x86asm.VSHUFPS:
		return l.liftShufps(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VINSERTPS:
		return l.liftInsertps(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VBLENDPS:
		return l.liftBlendps(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VBLENDVPS:
		return l.liftBlendvps(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VCMPPS:
		return l.liftCmpps(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VMOVD:
		return l.liftMovd(args[0], args[1], nextPC)
	case x86asm.VMOVQ:
		return l.liftMovq(args[0], args[1], nextPC)
	case x86asm.VUCOMISS:
		return l.liftUcomis(false, args[0], args[1], nextPC)
	case x86asm.VUCOMISD:
		return l.liftUcomis(true, args[0], args[1], nextPC)
	case x86asm.VCOMISS:
		return l.liftComis(false, args[0], args[1], nextPC)
	case x86asm.VCOMISD:
		return l.liftComis(true, args[0], args[1], nextPC)
	case x86asm.VCVTTSS2SI:
		return l.liftCvtFloatToInt(true, false, args[0], args[1], defMemSz, nextPC)
	case x86asm.VCVTTSD2SI:
		return l.liftCvtFloatToInt(true, true, args[0], args[1], defMemSz, nextPC)
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
	case x86asm.VCVTTPS2DQ:
		src := args[1]
		if args[2] != nil {
			src = args[2]
		}
		return l.liftCvttps2dq(args[0], src, nextPC)
	case x86asm.VPMOVZXWD:
		return l.liftPmovsxzx(false, 2, 4, args[0], args[1], nextPC)
	case x86asm.VPMOVSXWD:
		return l.liftPmovsxzx(true, 2, 4, args[0], args[1], nextPC)
	case x86asm.VPMOVSXDQ:
		return l.liftPmovsxzx(true, 4, 8, args[0], args[1], nextPC)
	case x86asm.VPMOVZXDQ:
		return l.liftPmovsxzx(false, 4, 8, args[0], args[1], nextPC)
	case x86asm.VPMOVSXBD:
		return l.liftPmovsxzx(true, 1, 4, args[0], args[1], nextPC)
	case x86asm.VPMOVZXBD:
		return l.liftPmovsxzx(false, 1, 4, args[0], args[1], nextPC)
	case x86asm.VPMOVSXBW:
		return l.liftPmovsxzx(true, 1, 2, args[0], args[1], nextPC)
	case x86asm.VPMOVZXBW:
		return l.liftPmovsxzx(false, 1, 2, args[0], args[1], nextPC)
	case x86asm.VPMOVSXWQ:
		return l.liftPmovsxzx(true, 2, 8, args[0], args[1], nextPC)
	case x86asm.VPMOVZXWQ:
		return l.liftPmovsxzx(false, 2, 8, args[0], args[1], nextPC)
	case x86asm.VPMOVSXBQ:
		return l.liftPmovsxzx(true, 1, 8, args[0], args[1], nextPC)
	case x86asm.VPMOVZXBQ:
		return l.liftPmovsxzx(false, 1, 8, args[0], args[1], nextPC)
	case x86asm.VMOVLPD:
		return l.liftVmovlpd(args[0], args[1], args[2], nextPC)
	case x86asm.VPBLENDW:
		return l.liftPblendw(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VBLENDPD:
		return l.liftBlendpd(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VBLENDVPD:
		return l.liftBlendvpd(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VMASKMOVPS:
		return l.liftMaskmovps(args[0], args[1], args[2], nextPC)
	case x86asm.VMASKMOVPD:
		return l.liftMaskmovpd(args[0], args[1], args[2], nextPC)
	case x86asm.VBROADCASTSD:
		return l.liftVbroadcastsd(args[0], args[1], nextPC)
	case x86asm.VBROADCASTF128:
		return l.liftVbroadcastf128(args[0], args[1], nextPC)
	case x86asm.VMOVMSKPD:
		return l.liftVmovmskpd(args[0], args[1], nextPC)
	case x86asm.VPERM2F128:
		return l.liftVperm2f128(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VROUNDPD:
		return l.liftVroundpd(args[0], args[1], args[2], nextPC)
	case x86asm.VDPPS:
		return l.liftVdpps(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VRCPPS:
		return l.liftVrcp(false, args[0], args[1], args[2], nextPC)
	case x86asm.VRCPSS:
		return l.liftVrcp(true, args[0], args[1], args[2], nextPC)
	case x86asm.VPHADDD, x86asm.VPHADDW, x86asm.VHSUBPS, x86asm.VHSUBPD, x86asm.VPHSUBD:
		return l.liftHaddHsub(op, args[0], args[1], args[2], nextPC)
	case x86asm.VPMAXUW, x86asm.VPMINUW, x86asm.VPMAXSW, x86asm.VPMINSW, x86asm.VPMAXUB, x86asm.VPMINUB:
		return l.liftPminmaxExtra(op, args[0], args[1], args[2], nextPC)
	case x86asm.VPMADDWD, x86asm.VPMADDUBSW:
		return l.liftPmaddExtra(op, args[0], args[1], args[2], nextPC)
	case x86asm.VPMOVMSKB:
		return l.liftVpmovmskb(args[0], args[1], nextPC)
	case x86asm.VPCMPEQQ, x86asm.VPCMPGTB, x86asm.VPCMPGTW:
		return l.liftPcmpExtra(op, args[0], args[1], args[2], nextPC)
	case x86asm.VPMULUDQ, x86asm.VPMULDQ, x86asm.VPMULHW, x86asm.VPMULHUW:
		return l.liftPmulExtra(op, args[0], args[1], args[2], nextPC)
	case x86asm.VPACKUSDW:
		return l.liftVpackusdw(args[0], args[1], args[2], nextPC)
	case x86asm.VPSADBW:
		return l.liftVpsadbw(args[0], args[1], args[2], nextPC)
	case x86asm.VPABSD, x86asm.VPABSW, x86asm.VPABSB:
		return l.liftVpabs(op, args[0], args[1], nextPC)
	case x86asm.VPHMINPOSUW:
		return l.liftVphminposuw(args[0], args[1], nextPC)
	case x86asm.VPADDUSB, x86asm.VPADDSW, x86asm.VPSUBSW, x86asm.VPSUBUSB, x86asm.VPSUBUSW:
		return l.liftPaddsubSatExtra(op, args[0], args[1], args[2], nextPC)
	case x86asm.VADDSUBPS, x86asm.VADDSUBPD:
		return l.liftVaddsub(op, args[0], args[1], args[2], nextPC)
	case x86asm.VCMPPD:
		return l.liftVcmppd(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VCVTTPD2DQ:
		return l.liftVcvttpd2dq(args[0], args[1], nextPC)
	case x86asm.VCVTDQ2PD:
		return l.liftVcvtdq2pd(args[0], args[1], nextPC)
	case x86asm.VCVTPH2PS:
		return l.liftVcvtph2ps(args[0], args[1], nextPC)
	case x86asm.VCVTPS2PH:
		return l.liftVcvtps2ph(args[0], args[1], args[2], nextPC)
	case x86asm.VAESENC:
		return l.liftVaesRound("recomp_vaesenc", args[0], args[1], args[2], nextPC)
	case x86asm.VAESENCLAST:
		return l.liftVaesRound("recomp_vaesenclast", args[0], args[1], args[2], nextPC)
	case x86asm.VAESDEC:
		return l.liftVaesRound("recomp_vaesdec", args[0], args[1], args[2], nextPC)
	case x86asm.VAESDECLAST:
		return l.liftVaesRound("recomp_vaesdeclast", args[0], args[1], args[2], nextPC)
	case x86asm.VAESIMC:
		return l.liftVaesImc(args[0], args[1], nextPC)
	case x86asm.VAESKEYGENASSIST:
		return l.liftVaesKeyGenAssist(args[0], args[1], args[2], nextPC)
	case x86asm.VPCLMULQDQ:
		return l.liftVpclmulqdq(args[0], args[1], args[2], args[3], nextPC)
	case x86asm.VSTMXCSR, x86asm.STMXCSR:
		return l.liftVmxcsr(true, args[0], nextPC)
	case x86asm.VLDMXCSR, x86asm.LDMXCSR:
		return l.liftVmxcsr(false, args[0], nextPC)
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
		dIdx := int(dstReg - x86asm.X0)
		var lines []string
		lines = append(lines, "    {")
		if src2Reg, ok := args[2].(x86asm.Reg); ok && isXmm(src2Reg) {
			infoSrc2 := regMap[src2Reg]
			lines = append(lines, fmt.Sprintf("      float s2 = ctx->%s.f32[0];", infoSrc2.BaseReg))
		} else if src2Mem, ok := args[2].(x86asm.Mem); ok {
			addr, err := MemAddrExpr(src2Mem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(lines,
				"      float s2;",
				fmt.Sprintf("      uint32_t u = MEM_U32(%s);", addr),
				"      memcpy(&s2, &u, 4);",
			)
		} else {
			return nil, fmt.Errorf("vmovss invalid src2: %v", args[2])
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("      ctx->%s.f32[0] = s2;", infoDst.BaseReg),
			fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
			"    }",
		)
		return lines, nil
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
		dIdx := int(dstReg - x86asm.X0)
		var lines []string
		lines = append(lines, "    {")
		if src2Reg, ok := args[2].(x86asm.Reg); ok && isXmm(src2Reg) {
			infoSrc2 := regMap[src2Reg]
			lines = append(lines, fmt.Sprintf("      double s2 = ctx->%s.f64[0];", infoSrc2.BaseReg))
		} else if src2Mem, ok := args[2].(x86asm.Mem); ok {
			addr, err := MemAddrExpr(src2Mem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(lines,
				"      double s2;",
				fmt.Sprintf("      uint64_t u = MEM_U64(%s);", addr),
				"      memcpy(&s2, &u, 8);",
			)
		} else {
			return nil, fmt.Errorf("vmovsd invalid src2: %v", args[2])
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("      ctx->%s.f64[0] = s2;", infoDst.BaseReg),
			fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
			"    }",
		)
		return lines, nil
	}

	// 4-operand VPINSR
	if op == x86asm.VPINSRB || op == x86asm.VPINSRW || op == x86asm.VPINSRD || op == x86asm.VPINSRQ {
		dstReg, ok1 := args[0].(x86asm.Reg)
		src1Reg, ok2 := args[1].(x86asm.Reg)
		if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
			return nil, fmt.Errorf("vpinsr invalid registers")
		}
		infoDst := regMap[dstReg]
		infoSrc1 := regMap[src1Reg]
		dIdx := int(dstReg - x86asm.X0)
		var elemBytes int
		switch op {
		case x86asm.VPINSRB:
			elemBytes = 1
		case x86asm.VPINSRD:
			elemBytes = 4
		case x86asm.VPINSRQ:
			elemBytes = 8
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
		lines = append(lines, code...)
		lines = append(lines, fmt.Sprintf("    memset(&ctx->ymmh[%d], 0, 16);", dIdx))
		return lines, nil
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
		dIdx := int(dstReg - x86asm.X0)
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
			"    {",
			fmt.Sprintf("      double s = %s;", sExpr),
			fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("      ctx->%s.f64[0] = %s(s);", infoDst.BaseReg, roundFunc),
			fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
			"    }",
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
		dIdx := int(dstReg - x86asm.X0)
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
			"    {",
			fmt.Sprintf("      float s = %s;", sExpr),
			fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("      ctx->%s.f32[0] = %s(s);", infoDst.BaseReg, roundFunc),
			fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
			"    }",
		}, nil
	}

	// 3-operand VEX operations
	dstReg, ok1 := args[0].(x86asm.Reg)
	src1Reg, ok2 := args[1].(x86asm.Reg)
	if !ok1 || !ok2 {
		return nil, fmt.Errorf("3-operand VEX requires register dst and src1")
	}
	is256 := isYmm(dstReg) && isYmm(src1Reg)
	if !is256 && (!isXmm(dstReg) || !isXmm(src1Reg)) {
		return nil, fmt.Errorf("3-operand VEX requires XMM or YMM dst and src1")
	}

	switch op {
	case x86asm.VADDSS:
		return l.liftScalar3OpF32("+", dstReg, src1Reg, args[2], nextPC)
	case x86asm.VSUBSS:
		return l.liftScalar3OpF32("-", dstReg, src1Reg, args[2], nextPC)
	case x86asm.VMULSS:
		return l.liftScalar3OpF32("*", dstReg, src1Reg, args[2], nextPC)
	case x86asm.VDIVSS:
		return l.liftScalar3OpF32("/", dstReg, src1Reg, args[2], nextPC)
	case x86asm.VADDSD:
		return l.liftScalar3OpF64("+", dstReg, src1Reg, args[2], nextPC)
	case x86asm.VSUBSD:
		return l.liftScalar3OpF64("-", dstReg, src1Reg, args[2], nextPC)
	case x86asm.VMULSD:
		return l.liftScalar3OpF64("*", dstReg, src1Reg, args[2], nextPC)
	case x86asm.VDIVSD:
		return l.liftScalar3OpF64("/", dstReg, src1Reg, args[2], nextPC)
	case x86asm.VMAXSS:
		return l.liftMinMax3Op(false, false, dstReg, src1Reg, args[2], nextPC)
	case x86asm.VMINSS:
		return l.liftMinMax3Op(true, false, dstReg, src1Reg, args[2], nextPC)
	case x86asm.VMAXSD:
		return l.liftMinMax3Op(false, true, dstReg, src1Reg, args[2], nextPC)
	case x86asm.VMINSD:
		return l.liftMinMax3Op(true, true, dstReg, src1Reg, args[2], nextPC)
	case x86asm.VSQRTSS:
		return l.liftSqrt3Op(false, dstReg, src1Reg, args[2], nextPC)
	case x86asm.VSQRTSD:
		return l.liftSqrt3Op(true, dstReg, src1Reg, args[2], nextPC)
	case x86asm.VCVTSI2SS:
		return l.liftVexCvtsi2s(false, dstReg, src1Reg, args[2], defMemSz, nextPC)
	case x86asm.VCVTSI2SD:
		return l.liftVexCvtsi2s(true, dstReg, src1Reg, args[2], defMemSz, nextPC)
	case x86asm.VCVTSS2SD:
		return l.liftVexCvtScalar(true, dstReg, src1Reg, args[2], nextPC)
	case x86asm.VCVTSD2SS:
		return l.liftVexCvtScalar(false, dstReg, src1Reg, args[2], nextPC)
	case x86asm.VRSQRTSS:
		src := args[2]
		if src == nil {
			src = args[1]
		}
		return l.liftVexRsqrtss(dstReg, src1Reg, src, nextPC)
	case x86asm.VPSLLW, x86asm.VPSRLW, x86asm.VPSRAW,
		x86asm.VPSLLD, x86asm.VPSRLD, x86asm.VPSRAD,
		x86asm.VPSLLQ, x86asm.VPSRLQ:
		return l.liftVexShift(op, is256, dstReg, src1Reg, args[2], nextPC)
	case x86asm.VPSLLDQ:
		return l.liftVexPshiftBytes(true, is256, dstReg, src1Reg, args[2])
	case x86asm.VPSRLDQ:
		return l.liftVexPshiftBytes(false, is256, dstReg, src1Reg, args[2])
	default:
		// All packed vector operations
		return l.liftVexVector3Op(op, is256, dstReg, src1Reg, args[2], nextPC)
	}
}

func (l *Lifter) liftVexShift(op x86asm.Op, is256 bool, dstReg, src1Reg x86asm.Reg, countArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dIdx := xmmOrYmmIdx(dstReg)
	s1Idx := xmmOrYmmIdx(src1Reg)

	var lines []string
	lines = append(lines, "    {")

	var cntExpr string
	if reg, ok := countArg.(x86asm.Reg); ok && isXmm(reg) {
		s2Idx := int(reg - x86asm.X0)
		cntExpr = fmt.Sprintf("ctx->xmm[%d].u64[0]", s2Idx)
	} else if imm, ok := countArg.(x86asm.Imm); ok {
		cntExpr = fmt.Sprintf("%d", imm)
	} else if mem, ok := countArg.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		cntExpr = fmt.Sprintf("MEM_U64(%s)", addr)
	} else {
		return nil, fmt.Errorf("unsupported vex shift count: %v", countArg)
	}

	lines = append(lines, fmt.Sprintf("      uint32_t shift = (uint32_t)(%s);", cntExpr))
	lines = append(lines, fmt.Sprintf("      xmm_reg_t s1_lo = ctx->xmm[%d];", s1Idx))
	if is256 {
		lines = append(lines, fmt.Sprintf("      xmm_reg_t s1_hi = ctx->ymmh[%d];", s1Idx))
	} else {
		lines = append(lines, "      xmm_reg_t s1_hi = {0}; (void)s1_hi;")
	}
	lines = append(lines, "      xmm_reg_t res_lo = {0}, res_hi = {0}; (void)res_hi;")

	switch op {
	case x86asm.VPSLLW:
		lines = append(lines,
			"      if (shift < 16) {",
			"        for (int i = 0; i < 8; i++) { res_lo.u16[i] = s1_lo.u16[i] << shift; res_hi.u16[i] = s1_hi.u16[i] << shift; }",
			"      } else {",
			"        memset(&res_lo, 0, 16); memset(&res_hi, 0, 16);",
			"      }",
		)
	case x86asm.VPSRLW:
		lines = append(lines,
			"      if (shift < 16) {",
			"        for (int i = 0; i < 8; i++) { res_lo.u16[i] = s1_lo.u16[i] >> shift; res_hi.u16[i] = s1_hi.u16[i] >> shift; }",
			"      } else {",
			"        memset(&res_lo, 0, 16); memset(&res_hi, 0, 16);",
			"      }",
		)
	case x86asm.VPSRAW:
		lines = append(lines,
			"      if (shift >= 16) shift = 15;",
			"      for (int i = 0; i < 8; i++) { res_lo.s16[i] = s1_lo.s16[i] >> shift; res_hi.s16[i] = s1_hi.s16[i] >> shift; }",
		)
	case x86asm.VPSLLD:
		lines = append(lines,
			"      if (shift < 32) {",
			"        for (int i = 0; i < 4; i++) { res_lo.u32[i] = s1_lo.u32[i] << shift; res_hi.u32[i] = s1_hi.u32[i] << shift; }",
			"      } else {",
			"        memset(&res_lo, 0, 16); memset(&res_hi, 0, 16);",
			"      }",
		)
	case x86asm.VPSRLD:
		lines = append(lines,
			"      if (shift < 32) {",
			"        for (int i = 0; i < 4; i++) { res_lo.u32[i] = s1_lo.u32[i] >> shift; res_hi.u32[i] = s1_hi.u32[i] >> shift; }",
			"      } else {",
			"        memset(&res_lo, 0, 16); memset(&res_hi, 0, 16);",
			"      }",
		)
	case x86asm.VPSRAD:
		lines = append(lines,
			"      if (shift >= 32) shift = 31;",
			"      for (int i = 0; i < 4; i++) { res_lo.s32[i] = s1_lo.s32[i] >> shift; res_hi.s32[i] = s1_hi.s32[i] >> shift; }",
		)
	case x86asm.VPSLLQ:
		lines = append(lines,
			"      if (shift < 64) {",
			"        for (int i = 0; i < 2; i++) { res_lo.u64[i] = s1_lo.u64[i] << shift; res_hi.u64[i] = s1_hi.u64[i] << shift; }",
			"      } else {",
			"        memset(&res_lo, 0, 16); memset(&res_hi, 0, 16);",
			"      }",
		)
	case x86asm.VPSRLQ:
		lines = append(lines,
			"      if (shift < 64) {",
			"        for (int i = 0; i < 2; i++) { res_lo.u64[i] = s1_lo.u64[i] >> shift; res_hi.u64[i] = s1_hi.u64[i] >> shift; }",
			"      } else {",
			"        memset(&res_lo, 0, 16); memset(&res_hi, 0, 16);",
			"      }",
		)
	default:
		return nil, fmt.Errorf("unsupported vex shift op: %v", op)
	}

	lines = append(lines, fmt.Sprintf("      ctx->xmm[%d] = res_lo;", dIdx))
	if is256 {
		lines = append(lines, fmt.Sprintf("      ctx->ymmh[%d] = res_hi;", dIdx))
	} else {
		lines = append(lines, fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftVexPshiftBytes(isLeft bool, is256 bool, dstReg, src1Reg x86asm.Reg, immArg x86asm.Arg) ([]string, error) {
	imm, ok := immArg.(x86asm.Imm)
	if !ok {
		return nil, fmt.Errorf("vpslldq/vpsrldq invalid immediate operand")
	}
	dIdx := xmmOrYmmIdx(dstReg)
	s1Idx := xmmOrYmmIdx(src1Reg)
	count := int(imm)

	var lines []string
	lines = append(lines, "    {")
	lines = append(lines, fmt.Sprintf("      xmm_reg_t s1_lo = ctx->xmm[%d];", s1Idx))
	if is256 {
		lines = append(lines, fmt.Sprintf("      xmm_reg_t s1_hi = ctx->ymmh[%d];", s1Idx))
	} else {
		lines = append(lines, "      xmm_reg_t s1_hi = {0}; (void)s1_hi;")
	}
	lines = append(lines, "      xmm_reg_t res_lo = {0}, res_hi = {0}; (void)res_hi;")
	if count >= 16 {
		// already 0
	} else if count == 0 {
		lines = append(lines, "      res_lo = s1_lo;")
		if is256 {
			lines = append(lines, "      res_hi = s1_hi;")
		}
	} else if isLeft { // PSLLDQ: shift towards higher byte addresses
		lines = append(lines,
			fmt.Sprintf("      memcpy(&res_lo.u8[%d], &s1_lo.u8[0], %d);", count, 16-count),
		)
		if is256 {
			lines = append(lines,
				fmt.Sprintf("      memcpy(&res_hi.u8[%d], &s1_hi.u8[0], %d);", count, 16-count),
			)
		}
	} else { // PSRLDQ: shift towards lower byte addresses
		lines = append(lines,
			fmt.Sprintf("      memcpy(&res_lo.u8[0], &s1_lo.u8[%d], %d);", count, 16-count),
		)
		if is256 {
			lines = append(lines,
				fmt.Sprintf("      memcpy(&res_hi.u8[0], &s1_hi.u8[%d], %d);", count, 16-count),
			)
		}
	}
	lines = append(lines, fmt.Sprintf("      ctx->xmm[%d] = res_lo;", dIdx))
	if is256 {
		lines = append(lines, fmt.Sprintf("      ctx->ymmh[%d] = res_hi;", dIdx))
	} else {
		lines = append(lines, fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftVexCvtsi2s(isDouble bool, dstReg, src1Reg x86asm.Reg, src2 x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	if !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("vcvtsi2s dst and src1 must be XMM registers")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)
	sz := defMemSz
	if reg, ok := src2.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	sRead, _, err := l.getOperandRead(src2, sz, nextPC)
	if err != nil {
		return nil, err
	}
	castType := "int32_t"
	if sz == 8 {
		castType = "int64_t"
	}
	var lines []string
	lines = append(lines, "    {")
	if isDouble {
		lines = append(lines,
			fmt.Sprintf("      double d = (double)(%s)(%s);", castType, sRead),
			fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("      ctx->%s.f64[0] = d;", infoDst.BaseReg),
		)
	} else {
		lines = append(lines,
			fmt.Sprintf("      float f = (float)(%s)(%s);", castType, sRead),
			fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("      ctx->%s.f32[0] = f;", infoDst.BaseReg),
		)
	}
	lines = append(lines,
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftVexCvtScalar(toSD bool, dstReg, src1Reg x86asm.Reg, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	if !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("vcvtss2sd/vcvtsd2ss dst and src1 must be XMM registers")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)
	var lines []string
	lines = append(lines, "    {")
	if toSD { // VCVTSS2SD: float to double
		if src2Reg, ok := src2.(x86asm.Reg); ok && isXmm(src2Reg) {
			infoSrc2 := regMap[src2Reg]
			lines = append(lines, fmt.Sprintf("      double d = (double)ctx->%s.f32[0];", infoSrc2.BaseReg))
		} else if src2Mem, ok := src2.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(src2Mem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(lines,
				"      float s;",
				fmt.Sprintf("      uint32_t u = MEM_U32(%s);", addr),
				"      memcpy(&s, &u, 4);",
				"      double d = (double)s;",
			)
		} else {
			return nil, fmt.Errorf("vcvtss2sd invalid src2: %v", src2)
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("      ctx->%s.f64[0] = d;", infoDst.BaseReg),
		)
	} else { // VCVTSD2SS: double to float
		if src2Reg, ok := src2.(x86asm.Reg); ok && isXmm(src2Reg) {
			infoSrc2 := regMap[src2Reg]
			lines = append(lines, fmt.Sprintf("      float f = (float)ctx->%s.f64[0];", infoSrc2.BaseReg))
		} else if src2Mem, ok := src2.(x86asm.Mem); ok {
			addr, err := MemAddrExpr(src2Mem, nextPC)
			if err != nil {
				return nil, err
			}
			lines = append(lines,
				"      double s;",
				fmt.Sprintf("      uint64_t u = MEM_U64(%s);", addr),
				"      memcpy(&s, &u, 8);",
				"      float f = (float)s;",
			)
		} else {
			return nil, fmt.Errorf("vcvtsd2ss invalid src2: %v", src2)
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
			fmt.Sprintf("      ctx->%s.f32[0] = f;", infoDst.BaseReg),
		)
	}
	lines = append(lines,
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftVexRsqrtss(dstReg, src1Reg x86asm.Reg, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	if !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("vrsqrtss dst and src1 must be XMM registers")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)
	var lines []string
	lines = append(lines, "    {")
	if src2Reg, ok := src2.(x86asm.Reg); ok && isXmm(src2Reg) {
		infoSrc2 := regMap[src2Reg]
		lines = append(lines, fmt.Sprintf("      float s2 = ctx->%s.f32[0];", infoSrc2.BaseReg))
	} else if src2Mem, ok := src2.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(src2Mem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(lines,
			"      float s2;",
			fmt.Sprintf("      uint32_t u = MEM_U32(%s);", addr),
			"      memcpy(&s2, &u, 4);",
		)
	} else {
		return nil, fmt.Errorf("vrsqrtss invalid src2: %v", src2)
	}
	lines = append(lines,
		fmt.Sprintf("      ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
		fmt.Sprintf("      ctx->%s.f32[0] = 1.0f / sqrtf(s2);", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftVexVector3Op(op x86asm.Op, is256 bool, dstReg, src1Reg x86asm.Reg, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	dIdx := xmmOrYmmIdx(dstReg)
	s1Idx := xmmOrYmmIdx(src1Reg)

	var lines []string
	lines = append(lines, "    {")

	if s2Reg, ok := src2.(x86asm.Reg); ok && ((is256 && isYmm(s2Reg)) || (!is256 && isXmm(s2Reg))) {
		s2Idx := xmmOrYmmIdx(s2Reg)
		lines = append(lines, fmt.Sprintf("      xmm_reg_t s2_lo = ctx->xmm[%d];", s2Idx))
		if is256 {
			lines = append(lines, fmt.Sprintf("      xmm_reg_t s2_hi = ctx->ymmh[%d];", s2Idx))
		} else {
			lines = append(lines, "      xmm_reg_t s2_hi = {0}; (void)s2_hi;")
		}
	} else if s2Mem, ok := src2.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(s2Mem, nextPC)
		if err != nil {
			return nil, err
		}
		if is256 {
			lines = append(
				lines,
				"      xmm_reg_t s2_lo, s2_hi;",
				fmt.Sprintf("      memcpy(&s2_lo, ctx->mem_base + (%s), 16);", addr),
				fmt.Sprintf("      memcpy(&s2_hi, ctx->mem_base + (%s) + 16, 16);", addr),
			)
		} else {
			lines = append(
				lines,
				"      xmm_reg_t s2_lo;",
				fmt.Sprintf("      memcpy(&s2_lo, ctx->mem_base + (%s), 16);", addr),
				"      xmm_reg_t s2_hi = {0}; (void)s2_hi;",
			)
		}
	} else {
		return nil, fmt.Errorf("liftVexVector3Op unsupported src2: %v", src2)
	}

	lines = append(lines, fmt.Sprintf("      xmm_reg_t s1_lo = ctx->xmm[%d];", s1Idx))
	if is256 {
		lines = append(lines, fmt.Sprintf("      xmm_reg_t s1_hi = ctx->ymmh[%d];", s1Idx))
	} else {
		lines = append(lines, "      xmm_reg_t s1_hi = {0}; (void)s1_hi;")
	}
	lines = append(lines, "      xmm_reg_t res_lo = {0}, res_hi = {0}; (void)res_hi;")

	switch op {
	case x86asm.VADDPS:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.f32[%d] = s1_lo.f32[%d] + s2_lo.f32[%d];", i, i, i),
				fmt.Sprintf("      res_hi.f32[%d] = s1_hi.f32[%d] + s2_hi.f32[%d];", i, i, i),
			)
		}
	case x86asm.VSUBPS:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.f32[%d] = s1_lo.f32[%d] - s2_lo.f32[%d];", i, i, i),
				fmt.Sprintf("      res_hi.f32[%d] = s1_hi.f32[%d] - s2_hi.f32[%d];", i, i, i),
			)
		}
	case x86asm.VMULPS:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.f32[%d] = s1_lo.f32[%d] * s2_lo.f32[%d];", i, i, i),
				fmt.Sprintf("      res_hi.f32[%d] = s1_hi.f32[%d] * s2_hi.f32[%d];", i, i, i),
			)
		}
	case x86asm.VDIVPS:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.f32[%d] = s1_lo.f32[%d] / s2_lo.f32[%d];", i, i, i),
				fmt.Sprintf("      res_hi.f32[%d] = s1_hi.f32[%d] / s2_hi.f32[%d];", i, i, i),
			)
		}
	case x86asm.VMAXPS:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.f32[%d] = (s1_lo.f32[%d] > s2_lo.f32[%d]) ? s1_lo.f32[%d] : s2_lo.f32[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.f32[%d] = (s1_hi.f32[%d] > s2_hi.f32[%d]) ? s1_hi.f32[%d] : s2_hi.f32[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VMINPS:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.f32[%d] = (s1_lo.f32[%d] < s2_lo.f32[%d]) ? s1_lo.f32[%d] : s2_lo.f32[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.f32[%d] = (s1_hi.f32[%d] < s2_hi.f32[%d]) ? s1_hi.f32[%d] : s2_hi.f32[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VADDPD:
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.f64[%d] = s1_lo.f64[%d] + s2_lo.f64[%d];", i, i, i),
				fmt.Sprintf("      res_hi.f64[%d] = s1_hi.f64[%d] + s2_hi.f64[%d];", i, i, i),
			)
		}
	case x86asm.VSUBPD:
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.f64[%d] = s1_lo.f64[%d] - s2_lo.f64[%d];", i, i, i),
				fmt.Sprintf("      res_hi.f64[%d] = s1_hi.f64[%d] - s2_hi.f64[%d];", i, i, i),
			)
		}
	case x86asm.VMULPD:
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.f64[%d] = s1_lo.f64[%d] * s2_lo.f64[%d];", i, i, i),
				fmt.Sprintf("      res_hi.f64[%d] = s1_hi.f64[%d] * s2_hi.f64[%d];", i, i, i),
			)
		}
	case x86asm.VDIVPD:
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.f64[%d] = s1_lo.f64[%d] / s2_lo.f64[%d];", i, i, i),
				fmt.Sprintf("      res_hi.f64[%d] = s1_hi.f64[%d] / s2_hi.f64[%d];", i, i, i),
			)
		}
	case x86asm.VMAXPD:
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.f64[%d] = (s1_lo.f64[%d] > s2_lo.f64[%d]) ? s1_lo.f64[%d] : s2_lo.f64[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.f64[%d] = (s1_hi.f64[%d] > s2_hi.f64[%d]) ? s1_hi.f64[%d] : s2_hi.f64[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VMINPD:
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.f64[%d] = (s1_lo.f64[%d] < s2_lo.f64[%d]) ? s1_lo.f64[%d] : s2_lo.f64[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.f64[%d] = (s1_hi.f64[%d] < s2_hi.f64[%d]) ? s1_hi.f64[%d] : s2_hi.f64[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VXORPS, x86asm.VXORPD, x86asm.VPXOR:
		lines = append(lines,
			"      res_lo.u64[0] = s1_lo.u64[0] ^ s2_lo.u64[0];",
			"      res_lo.u64[1] = s1_lo.u64[1] ^ s2_lo.u64[1];",
			"      res_hi.u64[0] = s1_hi.u64[0] ^ s2_hi.u64[0];",
			"      res_hi.u64[1] = s1_hi.u64[1] ^ s2_hi.u64[1];",
		)
	case x86asm.VORPS, x86asm.VORPD, x86asm.VPOR:
		lines = append(lines,
			"      res_lo.u64[0] = s1_lo.u64[0] | s2_lo.u64[0];",
			"      res_lo.u64[1] = s1_lo.u64[1] | s2_lo.u64[1];",
			"      res_hi.u64[0] = s1_hi.u64[0] | s2_hi.u64[0];",
			"      res_hi.u64[1] = s1_hi.u64[1] | s2_hi.u64[1];",
		)
	case x86asm.VANDPS, x86asm.VANDPD, x86asm.VPAND:
		lines = append(lines,
			"      res_lo.u64[0] = s1_lo.u64[0] & s2_lo.u64[0];",
			"      res_lo.u64[1] = s1_lo.u64[1] & s2_lo.u64[1];",
			"      res_hi.u64[0] = s1_hi.u64[0] & s2_hi.u64[0];",
			"      res_hi.u64[1] = s1_hi.u64[1] & s2_hi.u64[1];",
		)
	case x86asm.VANDNPS, x86asm.VANDNPD, x86asm.VPANDN:
		lines = append(lines,
			"      res_lo.u64[0] = (~s1_lo.u64[0]) & s2_lo.u64[0];",
			"      res_lo.u64[1] = (~s1_lo.u64[1]) & s2_lo.u64[1];",
			"      res_hi.u64[0] = (~s1_hi.u64[0]) & s2_hi.u64[0];",
			"      res_hi.u64[1] = (~s1_hi.u64[1]) & s2_hi.u64[1];",
		)
	case x86asm.VPADDB:
		for i := 0; i < 16; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u8[%d] = s1_lo.u8[%d] + s2_lo.u8[%d];", i, i, i),
				fmt.Sprintf("      res_hi.u8[%d] = s1_hi.u8[%d] + s2_hi.u8[%d];", i, i, i),
			)
		}
	case x86asm.VPADDW:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u16[%d] = s1_lo.u16[%d] + s2_lo.u16[%d];", i, i, i),
				fmt.Sprintf("      res_hi.u16[%d] = s1_hi.u16[%d] + s2_hi.u16[%d];", i, i, i),
			)
		}
	case x86asm.VPADDD:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u32[%d] = s1_lo.u32[%d] + s2_lo.u32[%d];", i, i, i),
				fmt.Sprintf("      res_hi.u32[%d] = s1_hi.u32[%d] + s2_hi.u32[%d];", i, i, i),
			)
		}
	case x86asm.VPADDQ:
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u64[%d] = s1_lo.u64[%d] + s2_lo.u64[%d];", i, i, i),
				fmt.Sprintf("      res_hi.u64[%d] = s1_hi.u64[%d] + s2_hi.u64[%d];", i, i, i),
			)
		}
	case x86asm.VPSUBB:
		for i := 0; i < 16; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u8[%d] = s1_lo.u8[%d] - s2_lo.u8[%d];", i, i, i),
				fmt.Sprintf("      res_hi.u8[%d] = s1_hi.u8[%d] - s2_hi.u8[%d];", i, i, i),
			)
		}
	case x86asm.VPSUBW:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u16[%d] = s1_lo.u16[%d] - s2_lo.u16[%d];", i, i, i),
				fmt.Sprintf("      res_hi.u16[%d] = s1_hi.u16[%d] - s2_hi.u16[%d];", i, i, i),
			)
		}
	case x86asm.VPSUBD:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u32[%d] = s1_lo.u32[%d] - s2_lo.u32[%d];", i, i, i),
				fmt.Sprintf("      res_hi.u32[%d] = s1_hi.u32[%d] - s2_hi.u32[%d];", i, i, i),
			)
		}
	case x86asm.VPSUBQ:
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u64[%d] = s1_lo.u64[%d] - s2_lo.u64[%d];", i, i, i),
				fmt.Sprintf("      res_hi.u64[%d] = s1_hi.u64[%d] - s2_hi.u64[%d];", i, i, i),
			)
		}
	case x86asm.VPCMPEQB:
		for i := 0; i < 16; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u8[%d] = (s1_lo.u8[%d] == s2_lo.u8[%d]) ? 0xFF : 0;", i, i, i),
				fmt.Sprintf("      res_hi.u8[%d] = (s1_hi.u8[%d] == s2_hi.u8[%d]) ? 0xFF : 0;", i, i, i),
			)
		}
	case x86asm.VPCMPEQW:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u16[%d] = (s1_lo.u16[%d] == s2_lo.u16[%d]) ? 0xFFFF : 0;", i, i, i),
				fmt.Sprintf("      res_hi.u16[%d] = (s1_hi.u16[%d] == s2_hi.u16[%d]) ? 0xFFFF : 0;", i, i, i),
			)
		}
	case x86asm.VPCMPEQD:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u32[%d] = (s1_lo.u32[%d] == s2_lo.u32[%d]) ? 0xFFFFFFFFU : 0;", i, i, i),
				fmt.Sprintf("      res_hi.u32[%d] = (s1_hi.u32[%d] == s2_hi.u32[%d]) ? 0xFFFFFFFFU : 0;", i, i, i),
			)
		}
	case x86asm.VPCMPEQQ:
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u64[%d] = (s1_lo.u64[%d] == s2_lo.u64[%d]) ? 0xFFFFFFFFFFFFFFFFULL : 0;", i, i, i),
				fmt.Sprintf("      res_hi.u64[%d] = (s1_hi.u64[%d] == s2_hi.u64[%d]) ? 0xFFFFFFFFFFFFFFFFULL : 0;", i, i, i),
			)
		}
	case x86asm.VPCMPGTB:
		for i := 0; i < 16; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u8[%d] = (s1_lo.s8[%d] > s2_lo.s8[%d]) ? 0xFF : 0;", i, i, i),
				fmt.Sprintf("      res_hi.u8[%d] = (s1_hi.s8[%d] > s2_hi.s8[%d]) ? 0xFF : 0;", i, i, i),
			)
		}
	case x86asm.VPCMPGTW:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u16[%d] = (s1_lo.s16[%d] > s2_lo.s16[%d]) ? 0xFFFF : 0;", i, i, i),
				fmt.Sprintf("      res_hi.u16[%d] = (s1_hi.s16[%d] > s2_hi.s16[%d]) ? 0xFFFF : 0;", i, i, i),
			)
		}
	case x86asm.VPCMPGTD:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u32[%d] = (s1_lo.s32[%d] > s2_lo.s32[%d]) ? 0xFFFFFFFFU : 0;", i, i, i),
				fmt.Sprintf("      res_hi.u32[%d] = (s1_hi.s32[%d] > s2_hi.s32[%d]) ? 0xFFFFFFFFU : 0;", i, i, i),
			)
		}
	case x86asm.VPCMPGTQ:
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u64[%d] = (s1_lo.s64[%d] > s2_lo.s64[%d]) ? 0xFFFFFFFFFFFFFFFFULL : 0;", i, i, i),
				fmt.Sprintf("      res_hi.u64[%d] = (s1_hi.s64[%d] > s2_hi.s64[%d]) ? 0xFFFFFFFFFFFFFFFFULL : 0;", i, i, i),
			)
		}
	case x86asm.VPMINSD:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.s32[%d] = (s1_lo.s32[%d] < s2_lo.s32[%d]) ? s1_lo.s32[%d] : s2_lo.s32[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.s32[%d] = (s1_hi.s32[%d] < s2_hi.s32[%d]) ? s1_hi.s32[%d] : s2_hi.s32[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VPMAXSD:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.s32[%d] = (s1_lo.s32[%d] > s2_lo.s32[%d]) ? s1_lo.s32[%d] : s2_lo.s32[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.s32[%d] = (s1_hi.s32[%d] > s2_hi.s32[%d]) ? s1_hi.s32[%d] : s2_hi.s32[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VPMINUD:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u32[%d] = (s1_lo.u32[%d] < s2_lo.u32[%d]) ? s1_lo.u32[%d] : s2_lo.u32[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.u32[%d] = (s1_hi.u32[%d] < s2_hi.u32[%d]) ? s1_hi.u32[%d] : s2_hi.u32[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VPMAXUD:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u32[%d] = (s1_lo.u32[%d] > s2_lo.u32[%d]) ? s1_lo.u32[%d] : s2_lo.u32[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.u32[%d] = (s1_hi.u32[%d] > s2_hi.u32[%d]) ? s1_hi.u32[%d] : s2_hi.u32[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VPMINSB:
		for i := 0; i < 16; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.s8[%d] = (s1_lo.s8[%d] < s2_lo.s8[%d]) ? s1_lo.s8[%d] : s2_lo.s8[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.s8[%d] = (s1_hi.s8[%d] < s2_hi.s8[%d]) ? s1_hi.s8[%d] : s2_hi.s8[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VPMAXSB:
		for i := 0; i < 16; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.s8[%d] = (s1_lo.s8[%d] > s2_lo.s8[%d]) ? s1_lo.s8[%d] : s2_lo.s8[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.s8[%d] = (s1_hi.s8[%d] > s2_hi.s8[%d]) ? s1_hi.s8[%d] : s2_hi.s8[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VPMINUB:
		for i := 0; i < 16; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u8[%d] = (s1_lo.u8[%d] < s2_lo.u8[%d]) ? s1_lo.u8[%d] : s2_lo.u8[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.u8[%d] = (s1_hi.u8[%d] < s2_hi.u8[%d]) ? s1_hi.u8[%d] : s2_hi.u8[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VPMAXUB:
		for i := 0; i < 16; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u8[%d] = (s1_lo.u8[%d] > s2_lo.u8[%d]) ? s1_lo.u8[%d] : s2_lo.u8[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.u8[%d] = (s1_hi.u8[%d] > s2_hi.u8[%d]) ? s1_hi.u8[%d] : s2_hi.u8[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VPMINUW:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u16[%d] = (s1_lo.u16[%d] < s2_lo.u16[%d]) ? s1_lo.u16[%d] : s2_lo.u16[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.u16[%d] = (s1_hi.u16[%d] < s2_hi.u16[%d]) ? s1_hi.u16[%d] : s2_hi.u16[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VPMAXUW:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u16[%d] = (s1_lo.u16[%d] > s2_lo.u16[%d]) ? s1_lo.u16[%d] : s2_lo.u16[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.u16[%d] = (s1_hi.u16[%d] > s2_hi.u16[%d]) ? s1_hi.u16[%d] : s2_hi.u16[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VPMINSW:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.s16[%d] = (s1_lo.s16[%d] < s2_lo.s16[%d]) ? s1_lo.s16[%d] : s2_lo.s16[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.s16[%d] = (s1_hi.s16[%d] < s2_hi.s16[%d]) ? s1_hi.s16[%d] : s2_hi.s16[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VPMAXSW:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.s16[%d] = (s1_lo.s16[%d] > s2_lo.s16[%d]) ? s1_lo.s16[%d] : s2_lo.s16[%d];", i, i, i, i, i),
				fmt.Sprintf("      res_hi.s16[%d] = (s1_hi.s16[%d] > s2_hi.s16[%d]) ? s1_hi.s16[%d] : s2_hi.s16[%d];", i, i, i, i, i),
			)
		}
	case x86asm.VPAVGB:
		for i := 0; i < 16; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u8[%d] = (uint8_t)(((uint32_t)s1_lo.u8[%d] + (uint32_t)s2_lo.u8[%d] + 1) >> 1);", i, i, i),
				fmt.Sprintf("      res_hi.u8[%d] = (uint8_t)(((uint32_t)s1_hi.u8[%d] + (uint32_t)s2_hi.u8[%d] + 1) >> 1);", i, i, i),
			)
		}
	case x86asm.VPAVGW:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u16[%d] = (uint16_t)(((uint32_t)s1_lo.u16[%d] + (uint32_t)s2_lo.u16[%d] + 1) >> 1);", i, i, i),
				fmt.Sprintf("      res_hi.u16[%d] = (uint16_t)(((uint32_t)s1_hi.u16[%d] + (uint32_t)s2_hi.u16[%d] + 1) >> 1);", i, i, i),
			)
		}
	case x86asm.VPACKSSWB:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      { int16_t v = s1_lo.s16[%d]; if (v > 127) v = 127; else if (v < -128) v = -128; res_lo.s8[%d] = (int8_t)v; }", i, i),
				fmt.Sprintf("      { int16_t v = s2_lo.s16[%d]; if (v > 127) v = 127; else if (v < -128) v = -128; res_lo.s8[%d] = (int8_t)v; }", i, i+8),
				fmt.Sprintf("      { int16_t v = s1_hi.s16[%d]; if (v > 127) v = 127; else if (v < -128) v = -128; res_hi.s8[%d] = (int8_t)v; }", i, i),
				fmt.Sprintf("      { int16_t v = s2_hi.s16[%d]; if (v > 127) v = 127; else if (v < -128) v = -128; res_hi.s8[%d] = (int8_t)v; }", i, i+8),
			)
		}
	case x86asm.VPACKSSDW:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      { int32_t v = s1_lo.s32[%d]; if (v > 32767) v = 32767; else if (v < -32768) v = -32768; res_lo.s16[%d] = (int16_t)v; }", i, i),
				fmt.Sprintf("      { int32_t v = s2_lo.s32[%d]; if (v > 32767) v = 32767; else if (v < -32768) v = -32768; res_lo.s16[%d] = (int16_t)v; }", i, i+4),
				fmt.Sprintf("      { int32_t v = s1_hi.s32[%d]; if (v > 32767) v = 32767; else if (v < -32768) v = -32768; res_hi.s16[%d] = (int16_t)v; }", i, i),
				fmt.Sprintf("      { int32_t v = s2_hi.s32[%d]; if (v > 32767) v = 32767; else if (v < -32768) v = -32768; res_hi.s16[%d] = (int16_t)v; }", i, i+4),
			)
		}
	case x86asm.VPACKUSWB:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      { int16_t v = s1_lo.s16[%d]; if (v < 0) v = 0; else if (v > 255) v = 255; res_lo.u8[%d] = (uint8_t)v; }", i, i),
				fmt.Sprintf("      { int16_t v = s2_lo.s16[%d]; if (v < 0) v = 0; else if (v > 255) v = 255; res_lo.u8[%d] = (uint8_t)v; }", i, i+8),
				fmt.Sprintf("      { int16_t v = s1_hi.s16[%d]; if (v < 0) v = 0; else if (v > 255) v = 255; res_hi.u8[%d] = (uint8_t)v; }", i, i),
				fmt.Sprintf("      { int16_t v = s2_hi.s16[%d]; if (v < 0) v = 0; else if (v > 255) v = 255; res_hi.u8[%d] = (uint8_t)v; }", i, i+8),
			)
		}
	case x86asm.VPMULLD:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u32[%d] = (uint32_t)((int32_t)s1_lo.u32[%d] * (int32_t)s2_lo.u32[%d]);", i, i, i),
				fmt.Sprintf("      res_hi.u32[%d] = (uint32_t)((int32_t)s1_hi.u32[%d] * (int32_t)s2_hi.u32[%d]);", i, i, i),
			)
		}
	case x86asm.VPMULLW:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u16[%d] = (uint16_t)((int16_t)s1_lo.u16[%d] * (int16_t)s2_lo.u16[%d]);", i, i, i),
				fmt.Sprintf("      res_hi.u16[%d] = (uint16_t)((int16_t)s1_hi.u16[%d] * (int16_t)s2_hi.u16[%d]);", i, i, i),
			)
		}
	case x86asm.VPMULUDQ:
		lines = append(lines,
			"      res_lo.u64[0] = (uint64_t)s1_lo.u32[0] * (uint64_t)s2_lo.u32[0];",
			"      res_lo.u64[1] = (uint64_t)s1_lo.u32[2] * (uint64_t)s2_lo.u32[2];",
			"      res_hi.u64[0] = (uint64_t)s1_hi.u32[0] * (uint64_t)s2_hi.u32[0];",
			"      res_hi.u64[1] = (uint64_t)s1_hi.u32[2] * (uint64_t)s2_hi.u32[2];",
		)
	case x86asm.VPMULDQ:
		lines = append(lines,
			"      res_lo.u64[0] = (uint64_t)((int64_t)s1_lo.s32[0] * (int64_t)s2_lo.s32[0]);",
			"      res_lo.u64[1] = (uint64_t)((int64_t)s1_lo.s32[2] * (int64_t)s2_lo.s32[2]);",
			"      res_hi.u64[0] = (uint64_t)((int64_t)s1_hi.s32[0] * (int64_t)s2_hi.s32[0]);",
			"      res_hi.u64[1] = (uint64_t)((int64_t)s1_hi.s32[2] * (int64_t)s2_hi.s32[2]);",
		)
	case x86asm.VPUNPCKLBW:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u8[%d] = s1_lo.u8[%d]; res_lo.u8[%d] = s2_lo.u8[%d];", i*2, i, i*2+1, i),
				fmt.Sprintf("      res_hi.u8[%d] = s1_hi.u8[%d]; res_hi.u8[%d] = s2_hi.u8[%d];", i*2, i, i*2+1, i),
			)
		}
	case x86asm.VPUNPCKHBW:
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u8[%d] = s1_lo.u8[%d]; res_lo.u8[%d] = s2_lo.u8[%d];", i*2, i+8, i*2+1, i+8),
				fmt.Sprintf("      res_hi.u8[%d] = s1_hi.u8[%d]; res_hi.u8[%d] = s2_hi.u8[%d];", i*2, i+8, i*2+1, i+8),
			)
		}
	case x86asm.VPUNPCKLWD:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u16[%d] = s1_lo.u16[%d]; res_lo.u16[%d] = s2_lo.u16[%d];", i*2, i, i*2+1, i),
				fmt.Sprintf("      res_hi.u16[%d] = s1_hi.u16[%d]; res_hi.u16[%d] = s2_hi.u16[%d];", i*2, i, i*2+1, i),
			)
		}
	case x86asm.VPUNPCKHWD:
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u16[%d] = s1_lo.u16[%d]; res_lo.u16[%d] = s2_lo.u16[%d];", i*2, i+4, i*2+1, i+4),
				fmt.Sprintf("      res_hi.u16[%d] = s1_hi.u16[%d]; res_hi.u16[%d] = s2_hi.u16[%d];", i*2, i+4, i*2+1, i+4),
			)
		}
	case x86asm.VPUNPCKLDQ, x86asm.VUNPCKLPS:
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u32[%d] = s1_lo.u32[%d]; res_lo.u32[%d] = s2_lo.u32[%d];", i*2, i, i*2+1, i),
				fmt.Sprintf("      res_hi.u32[%d] = s1_hi.u32[%d]; res_hi.u32[%d] = s2_hi.u32[%d];", i*2, i, i*2+1, i),
			)
		}
	case x86asm.VPUNPCKHDQ, x86asm.VUNPCKHPS:
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u32[%d] = s1_lo.u32[%d]; res_lo.u32[%d] = s2_lo.u32[%d];", i*2, i+2, i*2+1, i+2),
				fmt.Sprintf("      res_hi.u32[%d] = s1_hi.u32[%d]; res_hi.u32[%d] = s2_hi.u32[%d];", i*2, i+2, i*2+1, i+2),
			)
		}
	case x86asm.VPUNPCKLQDQ, x86asm.VUNPCKLPD:
		lines = append(lines,
			"      res_lo.u64[0] = s1_lo.u64[0]; res_lo.u64[1] = s2_lo.u64[0];",
			"      res_hi.u64[0] = s1_hi.u64[0]; res_hi.u64[1] = s2_hi.u64[0];",
		)
	case x86asm.VPUNPCKHQDQ, x86asm.VUNPCKHPD:
		lines = append(lines,
			"      res_lo.u64[0] = s1_lo.u64[1]; res_lo.u64[1] = s2_lo.u64[1];",
			"      res_hi.u64[0] = s1_hi.u64[1]; res_hi.u64[1] = s2_hi.u64[1];",
		)
	case x86asm.VHADDPS:
		lines = append(lines,
			"      res_lo.f32[0] = s1_lo.f32[0] + s1_lo.f32[1];",
			"      res_lo.f32[1] = s1_lo.f32[2] + s1_lo.f32[3];",
			"      res_lo.f32[2] = s2_lo.f32[0] + s2_lo.f32[1];",
			"      res_lo.f32[3] = s2_lo.f32[2] + s2_lo.f32[3];",
			"      res_hi.f32[0] = s1_hi.f32[0] + s1_hi.f32[1];",
			"      res_hi.f32[1] = s1_hi.f32[2] + s1_hi.f32[3];",
			"      res_hi.f32[2] = s2_hi.f32[0] + s2_hi.f32[1];",
			"      res_hi.f32[3] = s2_hi.f32[2] + s2_hi.f32[3];",
		)
	case x86asm.VHADDPD:
		lines = append(lines,
			"      res_lo.f64[0] = s1_lo.f64[0] + s1_lo.f64[1];",
			"      res_lo.f64[1] = s2_lo.f64[0] + s2_lo.f64[1];",
			"      res_hi.f64[0] = s1_hi.f64[0] + s1_hi.f64[1];",
			"      res_hi.f64[1] = s2_hi.f64[0] + s2_hi.f64[1];",
		)
	default:
		return nil, fmt.Errorf("unsupported VEX vector op: %v", op)
	}

	lines = append(lines, fmt.Sprintf("      ctx->xmm[%d] = res_lo;", dIdx))
	if is256 {
		lines = append(lines, fmt.Sprintf("      ctx->ymmh[%d] = res_hi;", dIdx))
	} else {
		lines = append(lines, fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftPcmpistri(op x86asm.Op, args x86asm.Args, nextPC uint64) ([]string, error) {
	src2Reg, ok1 := args[0].(x86asm.Reg)
	if !ok1 || !isXmm(src2Reg) {
		return nil, fmt.Errorf("pcmpistri requires XMM src2")
	}
	s2Idx := int(src2Reg - x86asm.X0)

	immArg, ok3 := args[2].(x86asm.Imm)
	if !ok3 {
		return nil, fmt.Errorf("pcmpistri requires immediate argument")
	}
	imm8 := uint8(immArg)

	if src1Reg, ok2 := args[1].(x86asm.Reg); ok2 && isXmm(src1Reg) {
		s1Idx := int(src1Reg - x86asm.X0)
		return []string{
			fmt.Sprintf("    recomp_vpcmpistri(ctx, &ctx->xmm[%d], &ctx->xmm[%d], 0x%02x);", s2Idx, s1Idx, imm8),
		}, nil
	} else if src1Mem, ok2 := args[1].(x86asm.Mem); ok2 {
		addr, err := MemAddrExpr(src1Mem, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{
			fmt.Sprintf("    recomp_vpcmpistri(ctx, &ctx->xmm[%d], (const void *)(ctx->mem_base + (%s)), 0x%02x);", s2Idx, addr, imm8),
		}, nil
	}
	return nil, fmt.Errorf("pcmpistri unsupported src1 operand: %v", args[1])
}

func (l *Lifter) liftPcmpestri(op x86asm.Op, args x86asm.Args, nextPC uint64) ([]string, error) {
	// PCMPESTRI / VPCMPESTRI: src2, src1, imm8
	// Uses explicit string lengths in RAX and RDX, returning index in RCX and updating flags.
	var src2Arg, src1Arg, immArg x86asm.Arg
	if args[3] != nil {
		// 4-operand VEX form: VPCMPESTRI xmm1, xmm2, xmm3/m128, imm8 (xmm1 is ignored/fixed return in rcx)
		src2Arg = args[1]
		src1Arg = args[2]
		immArg = args[3]
	} else {
		// 3-operand legacy or standard form: PCMPESTRI xmm1, xmm2/m128, imm8
		src2Arg = args[0]
		src1Arg = args[1]
		immArg = args[2]
	}

	src2Reg, ok1 := src2Arg.(x86asm.Reg)
	if !ok1 || !isXmm(src2Reg) {
		return nil, fmt.Errorf("pcmpestri requires XMM src2")
	}
	s2Idx := int(src2Reg - x86asm.X0)

	immVal, ok3 := immArg.(x86asm.Imm)
	if !ok3 {
		return nil, fmt.Errorf("pcmpestri requires immediate argument")
	}
	imm8 := uint8(immVal)

	if src1Reg, ok2 := src1Arg.(x86asm.Reg); ok2 && isXmm(src1Reg) {
		s1Idx := int(src1Reg - x86asm.X0)
		return []string{
			fmt.Sprintf("    recomp_vpcmpe_stri(ctx, &ctx->xmm[%d], &ctx->xmm[%d], 0x%02x);", s1Idx, s2Idx, imm8),
		}, nil
	} else if src1Mem, ok2 := src1Arg.(x86asm.Mem); ok2 {
		addr, err := MemAddrExpr(src1Mem, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{
			fmt.Sprintf("    recomp_vpcmpe_stri(ctx, (const void *)(ctx->mem_base + (%s)), &ctx->xmm[%d], 0x%02x);", addr, s2Idx, imm8),
		}, nil
	}
	return nil, fmt.Errorf("pcmpestri unsupported src1 operand: %v", src1Arg)
}
