package lifter

import (
	"fmt"

	"golang.org/x/arch/x86/x86asm"
)

// liftVmovlpd handles 2-operand or 3-operand VMOVLPD.
func (l *Lifter) liftVmovlpd(dst, src1, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	if src2 == nil {
		// 2-operand store: VMOVLPD m64, xmm1
		return l.liftMovlpd(dst, src1, nextPC)
	}
	// 3-operand load: VMOVLPD dst_xmm, src1_xmm, m64
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	mem, ok3 := src2.(x86asm.Mem)
	if !ok1 || !ok2 || !ok3 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("vmovlpd invalid operands")
	}
	addr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)
	return []string{
		"    {",
		fmt.Sprintf("      uint64_t val = MEM_U64(%s);", addr),
		fmt.Sprintf("      xmm_reg_t tmp = ctx->%s;", infoSrc1.BaseReg),
		"      tmp.u64[0] = val;",
		fmt.Sprintf("      ctx->%s = tmp;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	}, nil
}

// liftPblendw handles VPBLENDW xmm1, xmm2, xmm3/m128, imm8
func (l *Lifter) liftPblendw(dst, src1, src2, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	imm, ok3 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !ok3 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("vpblendw invalid operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)
	imm8 := uint8(imm)

	lines := []string{"    {"}
	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)
	lines = append(lines, fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg))
	lines = append(lines, "      xmm_reg_t res;")
	for i := 0; i < 8; i++ {
		if (imm8 & (1 << i)) != 0 {
			lines = append(lines, fmt.Sprintf("      res.u16[%d] = s2.u16[%d];", i, i))
		} else {
			lines = append(lines, fmt.Sprintf("      res.u16[%d] = s1.u16[%d];", i, i))
		}
	}
	lines = append(lines,
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftBlendpd handles VBLENDPD xmm1/ymm1, xmm2/ymm2, xmm3/m128/m256, imm8
func (l *Lifter) liftBlendpd(dst, src1, src2, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	imm, ok3 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !ok3 || (!isXmm(dstReg) && !isYmm(dstReg)) {
		return nil, fmt.Errorf("vblendpd invalid operands")
	}
	imm8 := uint8(imm)
	lines := []string{"    {"}

	if isYmm(dstReg) && isYmm(src1Reg) {
		dIdx := ymmIdx(dstReg)
		s1Idx := ymmIdx(src1Reg)
		s2Code, err := l.loadYmmArg(src2, nextPC, "s2")
		if err != nil {
			return nil, err
		}
		lines = append(lines, s2Code...)
		lines = append(lines,
			fmt.Sprintf("      xmm_reg_t s1_lo = ctx->xmm[%d];", s1Idx),
			fmt.Sprintf("      xmm_reg_t s1_hi = ctx->ymmh[%d];", s1Idx),
			"      xmm_reg_t res_lo, res_hi;",
		)
		for i := 0; i < 2; i++ {
			if (imm8 & (1 << i)) != 0 {
				lines = append(lines, fmt.Sprintf("      res_lo.f64[%d] = s2_lo.f64[%d];", i, i))
			} else {
				lines = append(lines, fmt.Sprintf("      res_lo.f64[%d] = s1_lo.f64[%d];", i, i))
			}
			if (imm8 & (1 << (i + 2))) != 0 {
				lines = append(lines, fmt.Sprintf("      res_hi.f64[%d] = s2_hi.f64[%d];", i, i))
			} else {
				lines = append(lines, fmt.Sprintf("      res_hi.f64[%d] = s1_hi.f64[%d];", i, i))
			}
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->xmm[%d] = res_lo;", dIdx),
			fmt.Sprintf("      ctx->ymmh[%d] = res_hi;", dIdx),
			"    }",
		)
		return lines, nil
	}

	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)

	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)
	lines = append(lines, fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg))
	lines = append(lines, "      xmm_reg_t res;")
	for i := 0; i < 2; i++ {
		if (imm8 & (1 << i)) != 0 {
			lines = append(lines, fmt.Sprintf("      res.f64[%d] = s2.f64[%d];", i, i))
		} else {
			lines = append(lines, fmt.Sprintf("      res.f64[%d] = s1.f64[%d];", i, i))
		}
	}
	lines = append(lines,
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftBlendvpd handles VBLENDVPD dst, src1, src2, mask
func (l *Lifter) liftBlendvpd(dst, src1, src2, maskArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	if !ok1 || !ok2 || (!isXmm(dstReg) && !isYmm(dstReg)) {
		return nil, fmt.Errorf("vblendvpd invalid operands")
	}

	lines := []string{"    {"}
	if isYmm(dstReg) && isYmm(src1Reg) {
		dIdx := ymmIdx(dstReg)
		s1Idx := ymmIdx(src1Reg)
		s2Code, err := l.loadYmmArg(src2, nextPC, "s2")
		if err != nil {
			return nil, err
		}
		maskCode, err := l.loadYmmArg(maskArg, nextPC, "mask")
		if err != nil {
			return nil, err
		}
		lines = append(lines, s2Code...)
		lines = append(lines, maskCode...)
		lines = append(lines,
			fmt.Sprintf("      xmm_reg_t s1_lo = ctx->xmm[%d];", s1Idx),
			fmt.Sprintf("      xmm_reg_t s1_hi = ctx->ymmh[%d];", s1Idx),
			"      xmm_reg_t res_lo, res_hi;",
		)
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.f64[%d] = (mask_lo.s64[%d] < 0) ? s2_lo.f64[%d] : s1_lo.f64[%d];", i, i, i, i),
				fmt.Sprintf("      res_hi.f64[%d] = (mask_hi.s64[%d] < 0) ? s2_hi.f64[%d] : s1_hi.f64[%d];", i, i, i, i),
			)
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->xmm[%d] = res_lo;", dIdx),
			fmt.Sprintf("      ctx->ymmh[%d] = res_hi;", dIdx),
			"    }",
		)
		return lines, nil
	}

	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)

	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)

	maskRegStr := "ctx->xmm[0]"
	if mReg, ok := maskArg.(x86asm.Reg); ok && isXmm(mReg) {
		maskRegStr = fmt.Sprintf("ctx->%s", regMap[mReg].BaseReg)
	}

	lines = append(lines,
		fmt.Sprintf("      xmm_reg_t mask = %s;", maskRegStr),
		fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg),
		"      xmm_reg_t res;",
		"      res.f64[0] = (mask.s64[0] < 0) ? s2.f64[0] : s1.f64[0];",
		"      res.f64[1] = (mask.s64[1] < 0) ? s2.f64[1] : s1.f64[1];",
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftMaskmovps handles VMASKMOVPS load or store
func (l *Lifter) liftMaskmovps(dst, maskArg, memOrSrc x86asm.Arg, nextPC uint64) ([]string, error) {
	if memDst, ok := dst.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(memDst, nextPC)
		if err != nil {
			return nil, err
		}
		lines := []string{"    {"}
		mReg, okM := maskArg.(x86asm.Reg)
		if okM && isYmm(mReg) {
			mCode, err := l.loadYmmArg(maskArg, nextPC, "mask")
			if err != nil {
				return nil, err
			}
			sCode, err := l.loadYmmArg(memOrSrc, nextPC, "src")
			if err != nil {
				return nil, err
			}
			lines = append(lines, mCode...)
			lines = append(lines, sCode...)
			lines = append(lines, fmt.Sprintf("      uint8_t *base = ctx->mem_base + (%s);", addr))
			for i := 0; i < 4; i++ {
				lines = append(lines,
					fmt.Sprintf("      if (mask_lo.s32[%d] < 0) { memcpy(base + %d, &src_lo.f32[%d], 4); }", i, i*4, i),
					fmt.Sprintf("      if (mask_hi.s32[%d] < 0) { memcpy(base + %d, &src_hi.f32[%d], 4); }", i, (i+4)*4, i),
				)
			}
			lines = append(lines, "    }")
			return lines, nil
		}
		mCode, err := l.loadXmmArg(maskArg, nextPC, "mask")
		if err != nil {
			return nil, err
		}
		sCode, err := l.loadXmmArg(memOrSrc, nextPC, "src")
		if err != nil {
			return nil, err
		}
		lines = append(lines, mCode...)
		lines = append(lines, sCode...)
		lines = append(lines, fmt.Sprintf("      uint8_t *base = ctx->mem_base + (%s);", addr))
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      if (mask.s32[%d] < 0) { memcpy(base + %d, &src.f32[%d], 4); }", i, i*4, i),
			)
		}
		lines = append(lines, "    }")
		return lines, nil
	}

	dstReg, ok1 := dst.(x86asm.Reg)
	memSrc, ok2 := memOrSrc.(x86asm.Mem)
	if !ok1 || !ok2 || (!isXmm(dstReg) && !isYmm(dstReg)) {
		return nil, fmt.Errorf("vmaskmovps load invalid operands")
	}
	addr, err := MemAddrExpr(memSrc, nextPC)
	if err != nil {
		return nil, err
	}
	lines := []string{"    {"}

	if isYmm(dstReg) {
		dIdx := ymmIdx(dstReg)
		mCode, err := l.loadYmmArg(maskArg, nextPC, "mask")
		if err != nil {
			return nil, err
		}
		lines = append(lines, mCode...)
		lines = append(lines,
			fmt.Sprintf("      uint8_t *base = ctx->mem_base + (%s);", addr),
			"      xmm_reg_t res_lo = {0}, res_hi = {0};",
		)
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      if (mask_lo.s32[%d] < 0) { memcpy(&res_lo.f32[%d], base + %d, 4); }", i, i, i*4),
				fmt.Sprintf("      if (mask_hi.s32[%d] < 0) { memcpy(&res_hi.f32[%d], base + %d, 4); }", i, i, (i+4)*4),
			)
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->xmm[%d] = res_lo;", dIdx),
			fmt.Sprintf("      ctx->ymmh[%d] = res_hi;", dIdx),
			"    }",
		)
		return lines, nil
	}

	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)
	mCode, err := l.loadXmmArg(maskArg, nextPC, "mask")
	if err != nil {
		return nil, err
	}
	lines = append(lines, mCode...)
	lines = append(lines,
		fmt.Sprintf("      uint8_t *base = ctx->mem_base + (%s);", addr),
		"      xmm_reg_t res = {0};",
	)
	for i := 0; i < 4; i++ {
		lines = append(lines,
			fmt.Sprintf("      if (mask.s32[%d] < 0) { memcpy(&res.f32[%d], base + %d, 4); }", i, i, i*4),
		)
	}
	lines = append(lines,
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftMaskmovpd handles VMASKMOVPD load or store
func (l *Lifter) liftMaskmovpd(dst, maskArg, memOrSrc x86asm.Arg, nextPC uint64) ([]string, error) {
	if memDst, ok := dst.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(memDst, nextPC)
		if err != nil {
			return nil, err
		}
		lines := []string{"    {"}
		mReg, okM := maskArg.(x86asm.Reg)
		if okM && isYmm(mReg) {
			mCode, err := l.loadYmmArg(maskArg, nextPC, "mask")
			if err != nil {
				return nil, err
			}
			sCode, err := l.loadYmmArg(memOrSrc, nextPC, "src")
			if err != nil {
				return nil, err
			}
			lines = append(lines, mCode...)
			lines = append(lines, sCode...)
			lines = append(lines, fmt.Sprintf("      uint8_t *base = ctx->mem_base + (%s);", addr))
			for i := 0; i < 2; i++ {
				lines = append(lines,
					fmt.Sprintf("      if (mask_lo.s64[%d] < 0) { memcpy(base + %d, &src_lo.f64[%d], 8); }", i, i*8, i),
					fmt.Sprintf("      if (mask_hi.s64[%d] < 0) { memcpy(base + %d, &src_hi.f64[%d], 8); }", i, (i+2)*8, i),
				)
			}
			lines = append(lines, "    }")
			return lines, nil
		}
		mCode, err := l.loadXmmArg(maskArg, nextPC, "mask")
		if err != nil {
			return nil, err
		}
		sCode, err := l.loadXmmArg(memOrSrc, nextPC, "src")
		if err != nil {
			return nil, err
		}
		lines = append(lines, mCode...)
		lines = append(lines, sCode...)
		lines = append(lines, fmt.Sprintf("      uint8_t *base = ctx->mem_base + (%s);", addr))
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      if (mask.s64[%d] < 0) { memcpy(base + %d, &src.f64[%d], 8); }", i, i*8, i),
			)
		}
		lines = append(lines, "    }")
		return lines, nil
	}

	dstReg, ok1 := dst.(x86asm.Reg)
	memSrc, ok2 := memOrSrc.(x86asm.Mem)
	if !ok1 || !ok2 || (!isXmm(dstReg) && !isYmm(dstReg)) {
		return nil, fmt.Errorf("vmaskmovpd load invalid operands")
	}
	addr, err := MemAddrExpr(memSrc, nextPC)
	if err != nil {
		return nil, err
	}
	lines := []string{"    {"}

	if isYmm(dstReg) {
		dIdx := ymmIdx(dstReg)
		mCode, err := l.loadYmmArg(maskArg, nextPC, "mask")
		if err != nil {
			return nil, err
		}
		lines = append(lines, mCode...)
		lines = append(lines,
			fmt.Sprintf("      uint8_t *base = ctx->mem_base + (%s);", addr),
			"      xmm_reg_t res_lo = {0}, res_hi = {0};",
		)
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      if (mask_lo.s64[%d] < 0) { memcpy(&res_lo.f64[%d], base + %d, 8); }", i, i, i*8),
				fmt.Sprintf("      if (mask_hi.s64[%d] < 0) { memcpy(&res_hi.f64[%d], base + %d, 8); }", i, i, (i+2)*8),
			)
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->xmm[%d] = res_lo;", dIdx),
			fmt.Sprintf("      ctx->ymmh[%d] = res_hi;", dIdx),
			"    }",
		)
		return lines, nil
	}

	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)
	mCode, err := l.loadXmmArg(maskArg, nextPC, "mask")
	if err != nil {
		return nil, err
	}
	lines = append(lines, mCode...)
	lines = append(lines,
		fmt.Sprintf("      uint8_t *base = ctx->mem_base + (%s);", addr),
		"      xmm_reg_t res = {0};",
	)
	for i := 0; i < 2; i++ {
		lines = append(lines,
			fmt.Sprintf("      if (mask.s64[%d] < 0) { memcpy(&res.f64[%d], base + %d, 8); }", i, i, i*8),
		)
	}
	lines = append(lines,
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVbroadcastsd handles VBROADCASTSD dst_xmm/ymm, m64
func (l *Lifter) liftVbroadcastsd(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	mem, ok := src.(x86asm.Mem)
	if !ok {
		return nil, fmt.Errorf("vbroadcastsd source must be memory")
	}
	addr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	dstReg, ok2 := dst.(x86asm.Reg)
	if !ok2 {
		return nil, fmt.Errorf("vbroadcastsd destination must be register")
	}
	if isYmm(dstReg) {
		idx := ymmIdx(dstReg)
		return []string{
			"    {",
			fmt.Sprintf("      double d; memcpy(&d, ctx->mem_base + (%s), 8);", addr),
			fmt.Sprintf("      ctx->xmm[%d].f64[0] = d; ctx->xmm[%d].f64[1] = d;", idx, idx),
			fmt.Sprintf("      ctx->ymmh[%d].f64[0] = d; ctx->ymmh[%d].f64[1] = d;", idx, idx),
			"    }",
		}, nil
	}
	if isXmm(dstReg) {
		idx := int(dstReg - x86asm.X0)
		return []string{
			"    {",
			fmt.Sprintf("      double d; memcpy(&d, ctx->mem_base + (%s), 8);", addr),
			fmt.Sprintf("      ctx->xmm[%d].f64[0] = d; ctx->xmm[%d].f64[1] = d;", idx, idx),
			fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", idx),
			"    }",
		}, nil
	}
	return nil, fmt.Errorf("vbroadcastsd destination must be XMM or YMM")
}

// liftVbroadcastf128 handles VBROADCASTF128 ymm, m128
func (l *Lifter) liftVbroadcastf128(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	mem, ok1 := src.(x86asm.Mem)
	dstReg, ok2 := dst.(x86asm.Reg)
	if !ok1 || !ok2 || !isYmm(dstReg) {
		return nil, fmt.Errorf("vbroadcastf128 requires YMM destination and m128 source")
	}
	addr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	idx := ymmIdx(dstReg)
	return []string{
		"    {",
		"      xmm_reg_t tmp;",
		fmt.Sprintf("      memcpy(&tmp, ctx->mem_base + (%s), 16);", addr),
		fmt.Sprintf("      ctx->xmm[%d] = tmp;", idx),
		fmt.Sprintf("      ctx->ymmh[%d] = tmp;", idx),
		"    }",
	}, nil
}

// liftVmovmskpd handles VMOVMSKPD r32/r64, src_xmm/ymm
func (l *Lifter) liftVmovmskpd(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 {
		return nil, fmt.Errorf("vmovmskpd destination must be general purpose register")
	}
	infoDst := regMap[dstReg]

	if srcReg, ok := src.(x86asm.Reg); ok && isYmm(srcReg) {
		idx := ymmIdx(srcReg)
		return []string{
			"    {",
			fmt.Sprintf("      uint32_t mask = ((ctx->xmm[%d].u64[0] >> 63) & 1) |", idx),
			fmt.Sprintf("                      (((ctx->xmm[%d].u64[1] >> 63) & 1) << 1) |", idx),
			fmt.Sprintf("                      (((ctx->ymmh[%d].u64[0] >> 63) & 1) << 2) |", idx),
			fmt.Sprintf("                      (((ctx->ymmh[%d].u64[1] >> 63) & 1) << 3);", idx),
			fmt.Sprintf("      ctx->%s = mask;", infoDst.BaseReg),
			"    }",
		}, nil
	}

	lines := []string{"    {"}
	sCode, err := l.loadXmmArg(src, nextPC, "src")
	if err != nil {
		return nil, err
	}
	lines = append(lines, sCode...)
	lines = append(lines,
		"      uint32_t mask = ((src.u64[0] >> 63) & 1) | (((src.u64[1] >> 63) & 1) << 1);",
		fmt.Sprintf("      ctx->%s = mask;", infoDst.BaseReg),
		"    }",
	)
	return lines, nil
}

// liftVperm2f128 handles VPERM2F128 ymm1, ymm2, ymm3/m256, imm8
func (l *Lifter) liftVperm2f128(dst, src1, src2, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	imm, ok3 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !ok3 || !isYmm(dstReg) || !isYmm(src1Reg) {
		return nil, fmt.Errorf("vperm2f128 invalid operands")
	}
	dIdx := ymmIdx(dstReg)
	s1Idx := ymmIdx(src1Reg)
	imm8 := uint8(imm)

	lines := []string{"    {"}
	lines = append(lines,
		fmt.Sprintf("      xmm_reg_t s1_lo = ctx->xmm[%d];", s1Idx),
		fmt.Sprintf("      xmm_reg_t s1_hi = ctx->ymmh[%d];", s1Idx),
		"      xmm_reg_t s2_lo, s2_hi;",
	)
	if src2Reg, ok := src2.(x86asm.Reg); ok && isYmm(src2Reg) {
		s2Idx := ymmIdx(src2Reg)
		lines = append(lines,
			fmt.Sprintf("      s2_lo = ctx->xmm[%d]; s2_hi = ctx->ymmh[%d];", s2Idx, s2Idx),
		)
	} else if src2Mem, ok := src2.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(src2Mem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(lines,
			fmt.Sprintf("      memcpy(&s2_lo, ctx->mem_base + (%s), 16);", addr),
			fmt.Sprintf("      memcpy(&s2_hi, ctx->mem_base + (%s) + 16, 16);", addr),
		)
	} else {
		return nil, fmt.Errorf("vperm2f128 invalid src2")
	}

	lines = append(lines,
		"      xmm_reg_t src_table[4] = { s1_lo, s1_hi, s2_lo, s2_hi };",
		"      xmm_reg_t zero_lane = {0};",
		fmt.Sprintf("      xmm_reg_t res_lo = (0x%02x & 8) ? zero_lane : src_table[0x%02x & 3];", imm8, imm8),
		fmt.Sprintf("      xmm_reg_t res_hi = (0x%02x & 0x80) ? zero_lane : src_table[(0x%02x >> 4) & 3];", imm8, imm8),
		fmt.Sprintf("      ctx->xmm[%d] = res_lo;", dIdx),
		fmt.Sprintf("      ctx->ymmh[%d] = res_hi;", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVroundpd handles VROUNDPD dst_xmm, src_xmm/m128, imm8
func (l *Lifter) liftVroundpd(dst, src, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(dstReg) {
		return nil, fmt.Errorf("vroundpd invalid operands")
	}
	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)
	mode := uint8(imm) & 3
	roundFunc := "round"
	switch mode {
	case 1:
		roundFunc = "floor"
	case 2:
		roundFunc = "ceil"
	case 3:
		roundFunc = "trunc"
	}

	lines := []string{"    {"}
	sCode, err := l.loadXmmArg(src, nextPC, "src")
	if err != nil {
		return nil, err
	}
	lines = append(lines, sCode...)
	lines = append(lines,
		fmt.Sprintf("      ctx->%s.f64[0] = %s(src.f64[0]);", infoDst.BaseReg, roundFunc),
		fmt.Sprintf("      ctx->%s.f64[1] = %s(src.f64[1]);", infoDst.BaseReg, roundFunc),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVdpps handles VDPPS dst_xmm, src1_xmm, src2_xmm/m128, imm8
func (l *Lifter) liftVdpps(dst, src1, src2, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	imm, ok3 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !ok3 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("vdpps invalid operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)
	imm8 := uint8(imm)

	lines := []string{"    {"}
	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)
	lines = append(lines,
		fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg),
		fmt.Sprintf("      float p0 = (0x%02x & 0x10) ? (s1.f32[0] * s2.f32[0]) : 0.0f;", imm8),
		fmt.Sprintf("      float p1 = (0x%02x & 0x20) ? (s1.f32[1] * s2.f32[1]) : 0.0f;", imm8),
		fmt.Sprintf("      float p2 = (0x%02x & 0x40) ? (s1.f32[2] * s2.f32[2]) : 0.0f;", imm8),
		fmt.Sprintf("      float p3 = (0x%02x & 0x80) ? (s1.f32[3] * s2.f32[3]) : 0.0f;", imm8),
		"      float sum = p0 + p1 + p2 + p3;",
		"      xmm_reg_t res = {0};",
		fmt.Sprintf("      if (0x%02x & 0x01) res.f32[0] = sum;", imm8),
		fmt.Sprintf("      if (0x%02x & 0x02) res.f32[1] = sum;", imm8),
		fmt.Sprintf("      if (0x%02x & 0x04) res.f32[2] = sum;", imm8),
		fmt.Sprintf("      if (0x%02x & 0x08) res.f32[3] = sum;", imm8),
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVrcp handles VRCPPS and VRCPSS
func (l *Lifter) liftVrcp(isScalar bool, dst, src1, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("vrcp destination must be XMM")
	}
	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)

	if isScalar {
		src1Reg, ok2 := src1.(x86asm.Reg)
		if !ok2 || !isXmm(src1Reg) {
			return nil, fmt.Errorf("vrcpss src1 must be XMM")
		}
		infoSrc1 := regMap[src1Reg]
		lines := []string{"    {"}
		s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
		if err != nil {
			return nil, err
		}
		lines = append(lines, s2Code...)
		lines = append(lines,
			fmt.Sprintf("      xmm_reg_t res = ctx->%s;", infoSrc1.BaseReg),
			"      res.f32[0] = 1.0f / s2.f32[0];",
			fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
			fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
			"    }",
		)
		return lines, nil
	}

	// Packed VRCPPS
	src := src1
	if src2 != nil {
		src = src2
	}
	lines := []string{"    {"}
	sCode, err := l.loadXmmArg(src, nextPC, "s")
	if err != nil {
		return nil, err
	}
	lines = append(lines, sCode...)
	for i := 0; i < 4; i++ {
		lines = append(lines, fmt.Sprintf("      ctx->%s.f32[%d] = 1.0f / s.f32[%d];", infoDst.BaseReg, i, i))
	}
	lines = append(lines, fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx), "    }")
	return lines, nil
}

// liftHaddHsub handles VPHADDD, VPHADDW, VHSUBPS, VHSUBPD, VPHSUBD
func (l *Lifter) liftHaddHsub(op x86asm.Op, dst, src1, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("horizontal SIMD op requires XMM operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)
	lines = append(lines, fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg))
	lines = append(lines, "      xmm_reg_t res;")

	switch op {
	case x86asm.VPHADDD:
		lines = append(lines,
			"      res.s32[0] = s1.s32[0] + s1.s32[1];",
			"      res.s32[1] = s1.s32[2] + s1.s32[3];",
			"      res.s32[2] = s2.s32[0] + s2.s32[1];",
			"      res.s32[3] = s2.s32[2] + s2.s32[3];",
		)
	case x86asm.VPHADDW:
		for i := 0; i < 4; i++ {
			lines = append(lines, fmt.Sprintf("      res.s16[%d] = s1.s16[%d] + s1.s16[%d];", i, 2*i, 2*i+1))
		}
		for i := 0; i < 4; i++ {
			lines = append(lines, fmt.Sprintf("      res.s16[%d] = s2.s16[%d] + s2.s16[%d];", i+4, 2*i, 2*i+1))
		}
	case x86asm.VHSUBPS:
		lines = append(lines,
			"      res.f32[0] = s1.f32[0] - s1.f32[1];",
			"      res.f32[1] = s1.f32[2] - s1.f32[3];",
			"      res.f32[2] = s2.f32[0] - s2.f32[1];",
			"      res.f32[3] = s2.f32[2] - s2.f32[3];",
		)
	case x86asm.VHSUBPD:
		lines = append(lines,
			"      res.f64[0] = s1.f64[0] - s1.f64[1];",
			"      res.f64[1] = s2.f64[0] - s2.f64[1];",
		)
	case x86asm.VPHSUBD:
		lines = append(lines,
			"      res.s32[0] = s1.s32[0] - s1.s32[1];",
			"      res.s32[1] = s1.s32[2] - s1.s32[3];",
			"      res.s32[2] = s2.s32[0] - s2.s32[1];",
			"      res.s32[3] = s2.s32[2] - s2.s32[3];",
		)
	}

	lines = append(lines,
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftPminmaxExtra handles VPMAXUW, VPMAXSW, VPMINSW, VPMINUB, VPMAXUB, VPMINUW
func (l *Lifter) liftPminmaxExtra(op x86asm.Op, dst, src1, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("min/max SIMD op requires XMM operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)
	lines = append(lines, fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg))
	lines = append(lines, "      xmm_reg_t res;")

	switch op {
	case x86asm.VPMAXUW:
		for i := 0; i < 8; i++ {
			lines = append(lines, fmt.Sprintf("      res.u16[%d] = (s1.u16[%d] > s2.u16[%d]) ? s1.u16[%d] : s2.u16[%d];", i, i, i, i, i))
		}
	case x86asm.VPMINUW:
		for i := 0; i < 8; i++ {
			lines = append(lines, fmt.Sprintf("      res.u16[%d] = (s1.u16[%d] < s2.u16[%d]) ? s1.u16[%d] : s2.u16[%d];", i, i, i, i, i))
		}
	case x86asm.VPMAXSW:
		for i := 0; i < 8; i++ {
			lines = append(lines, fmt.Sprintf("      res.s16[%d] = (s1.s16[%d] > s2.s16[%d]) ? s1.s16[%d] : s2.s16[%d];", i, i, i, i, i))
		}
	case x86asm.VPMINSW:
		for i := 0; i < 8; i++ {
			lines = append(lines, fmt.Sprintf("      res.s16[%d] = (s1.s16[%d] < s2.s16[%d]) ? s1.s16[%d] : s2.s16[%d];", i, i, i, i, i))
		}
	case x86asm.VPMAXUB:
		for i := 0; i < 16; i++ {
			lines = append(lines, fmt.Sprintf("      res.u8[%d] = (s1.u8[%d] > s2.u8[%d]) ? s1.u8[%d] : s2.u8[%d];", i, i, i, i, i))
		}
	case x86asm.VPMINUB:
		for i := 0; i < 16; i++ {
			lines = append(lines, fmt.Sprintf("      res.u8[%d] = (s1.u8[%d] < s2.u8[%d]) ? s1.u8[%d] : s2.u8[%d];", i, i, i, i, i))
		}
	}

	lines = append(lines,
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftPmaddExtra handles VPMADDWD and VPMADDUBSW
func (l *Lifter) liftPmaddExtra(op x86asm.Op, dst, src1, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("pmadd SIMD op requires XMM operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)
	lines = append(lines, fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg))
	lines = append(lines, "      xmm_reg_t res;")

	if op == x86asm.VPMADDWD {
		for i := 0; i < 4; i++ {
			lines = append(lines,
				fmt.Sprintf("      res.s32[%d] = (int32_t)s1.s16[%d] * (int32_t)s2.s16[%d] + (int32_t)s1.s16[%d] * (int32_t)s2.s16[%d];",
					i, 2*i, 2*i, 2*i+1, 2*i+1),
			)
		}
	} else if op == x86asm.VPMADDUBSW {
		for i := 0; i < 8; i++ {
			lines = append(lines,
				fmt.Sprintf("      { int32_t prod = (int32_t)s1.u8[%d] * (int32_t)s2.s8[%d] + (int32_t)s1.u8[%d] * (int32_t)s2.s8[%d];",
					2*i, 2*i, 2*i+1, 2*i+1),
				fmt.Sprintf("        res.s16[%d] = (prod > 32767) ? 32767 : ((prod < -32768) ? -32768 : (int16_t)prod); }", i),
			)
		}
	}

	lines = append(lines,
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVpmovmskb handles VPMOVMSKB r32/r64, src_xmm
func (l *Lifter) liftVpmovmskb(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 {
		return nil, fmt.Errorf("vpmovmskb destination must be general purpose register")
	}
	infoDst := regMap[dstReg]

	lines := []string{"    {"}
	sCode, err := l.loadXmmArg(src, nextPC, "src")
	if err != nil {
		return nil, err
	}
	lines = append(lines, sCode...)
	lines = append(lines,
		"      uint32_t mask = 0;",
		"      for (int i = 0; i < 16; i++) { if (src.u8[i] & 0x80) mask |= (1 << i); }",
		fmt.Sprintf("      ctx->%s = mask;", infoDst.BaseReg),
		"    }",
	)
	return lines, nil
}

// liftPcmpExtra handles VPCMPEQQ, VPCMPGTB, VPCMPGTW
func (l *Lifter) liftPcmpExtra(op x86asm.Op, dst, src1, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("pcmp SIMD op requires XMM operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)
	lines = append(lines, fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg))
	lines = append(lines, "      xmm_reg_t res;")

	switch op {
	case x86asm.VPCMPEQQ:
		lines = append(lines,
			"      res.u64[0] = (s1.u64[0] == s2.u64[0]) ? 0xFFFFFFFFFFFFFFFFULL : 0;",
			"      res.u64[1] = (s1.u64[1] == s2.u64[1]) ? 0xFFFFFFFFFFFFFFFFULL : 0;",
		)
	case x86asm.VPCMPGTB:
		for i := 0; i < 16; i++ {
			lines = append(lines, fmt.Sprintf("      res.u8[%d] = (s1.s8[%d] > s2.s8[%d]) ? 0xFF : 0x00;", i, i, i))
		}
	case x86asm.VPCMPGTW:
		for i := 0; i < 8; i++ {
			lines = append(lines, fmt.Sprintf("      res.u16[%d] = (s1.s16[%d] > s2.s16[%d]) ? 0xFFFF : 0x0000;", i, i, i))
		}
	}

	lines = append(lines,
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftPmulExtra handles VPMULUDQ, VPMULDQ, VPMULHW, VPMULHUW
func (l *Lifter) liftPmulExtra(op x86asm.Op, dst, src1, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("pmul SIMD op requires XMM operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)
	lines = append(lines, fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg))
	lines = append(lines, "      xmm_reg_t res;")

	switch op {
	case x86asm.VPMULUDQ:
		lines = append(lines,
			"      res.u64[0] = (uint64_t)s1.u32[0] * (uint64_t)s2.u32[0];",
			"      res.u64[1] = (uint64_t)s1.u32[2] * (uint64_t)s2.u32[2];",
		)
	case x86asm.VPMULDQ:
		lines = append(lines,
			"      res.s64[0] = (int64_t)s1.s32[0] * (int64_t)s2.s32[0];",
			"      res.s64[1] = (int64_t)s1.s32[2] * (int64_t)s2.s32[2];",
		)
	case x86asm.VPMULHW:
		for i := 0; i < 8; i++ {
			lines = append(lines, fmt.Sprintf("      res.s16[%d] = (int16_t)(((int32_t)s1.s16[%d] * (int32_t)s2.s16[%d]) >> 16);", i, i, i))
		}
	case x86asm.VPMULHUW:
		for i := 0; i < 8; i++ {
			lines = append(lines, fmt.Sprintf("      res.u16[%d] = (uint16_t)(((uint32_t)s1.u16[%d] * (uint32_t)s2.u16[%d]) >> 16);", i, i, i))
		}
	}

	lines = append(lines,
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVpackusdw handles VPACKUSDW dst_xmm, src1_xmm, src2_xmm/m128
func (l *Lifter) liftVpackusdw(dst, src1, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("vpackusdw requires XMM operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)
	lines = append(lines,
		fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg),
		"      xmm_reg_t res;",
	)
	for i := 0; i < 4; i++ {
		lines = append(lines,
			fmt.Sprintf("      { int32_t v = s1.s32[%d]; res.u16[%d] = (v < 0) ? 0 : ((v > 65535) ? 65535 : (uint16_t)v); }", i, i),
		)
	}
	for i := 0; i < 4; i++ {
		lines = append(lines,
			fmt.Sprintf("      { int32_t v = s2.s32[%d]; res.u16[%d] = (v < 0) ? 0 : ((v > 65535) ? 65535 : (uint16_t)v); }", i, i+4),
		)
	}
	lines = append(lines,
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVpsadbw handles VPSADBW dst_xmm, src1_xmm, src2_xmm/m128
func (l *Lifter) liftVpsadbw(dst, src1, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("vpsadbw requires XMM operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)
	lines = append(lines,
		fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg),
		"      uint64_t sum0 = 0, sum1 = 0;",
		"      for (int i = 0; i < 8; i++) { int diff = (int)s1.u8[i] - (int)s2.u8[i]; sum0 += (diff < 0) ? -diff : diff; }",
		"      for (int i = 8; i < 16; i++) { int diff = (int)s1.u8[i] - (int)s2.u8[i]; sum1 += (diff < 0) ? -diff : diff; }",
		"      xmm_reg_t res = {0};",
		"      res.u64[0] = sum0; res.u64[1] = sum1;",
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVpabs handles VPABSD and VPABSW
func (l *Lifter) liftVpabs(op x86asm.Op, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("vpabs requires XMM destination")
	}
	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	sCode, err := l.loadXmmArg(src, nextPC, "s")
	if err != nil {
		return nil, err
	}
	lines = append(lines, sCode...)
	lines = append(lines, "      xmm_reg_t res = {0};")

	if op == x86asm.VPABSD {
		for i := 0; i < 4; i++ {
			lines = append(lines, fmt.Sprintf("      res.u32[%d] = (s.s32[%d] < 0) ? (uint32_t)(-s.s32[%d]) : (uint32_t)s.s32[%d];", i, i, i, i))
		}
	} else if op == x86asm.VPABSW {
		for i := 0; i < 8; i++ {
			lines = append(lines, fmt.Sprintf("      res.u16[%d] = (s.s16[%d] < 0) ? (uint16_t)(-s.s16[%d]) : (uint16_t)s.s16[%d];", i, i, i, i))
		}
	}

	lines = append(lines,
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVphminposuw handles VPHMINPOSUW dst_xmm, src_xmm/m128
func (l *Lifter) liftVphminposuw(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("vphminposuw requires XMM destination")
	}
	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	sCode, err := l.loadXmmArg(src, nextPC, "s")
	if err != nil {
		return nil, err
	}
	lines = append(lines, sCode...)
	lines = append(lines,
		"      uint16_t minVal = s.u16[0];",
		"      uint16_t minIdx = 0;",
		"      for (int i = 1; i < 8; i++) { if (s.u16[i] < minVal) { minVal = s.u16[i]; minIdx = i; } }",
		"      xmm_reg_t res = {0};",
		"      res.u16[0] = minVal;",
		"      res.u16[1] = minIdx;",
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftPaddsubSatExtra handles VPADDUSB, VPADDSW, VPSUBSW, VPSUBUSB, VPSUBUSW
func (l *Lifter) liftPaddsubSatExtra(op x86asm.Op, dst, src1, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("saturating SIMD add/sub requires XMM operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)
	lines = append(lines, fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg))
	lines = append(lines, "      xmm_reg_t res = {0};")

	switch op {
	case x86asm.VPADDUSB:
		for i := 0; i < 16; i++ {
			lines = append(lines, fmt.Sprintf("      { uint32_t sum = (uint32_t)s1.u8[%d] + (uint32_t)s2.u8[%d]; res.u8[%d] = (sum > 255) ? 255 : (uint8_t)sum; }", i, i, i))
		}
	case x86asm.VPADDSW:
		for i := 0; i < 8; i++ {
			lines = append(lines, fmt.Sprintf("      { int32_t sum = (int32_t)s1.s16[%d] + (int32_t)s2.s16[%d]; res.s16[%d] = (sum > 32767) ? 32767 : ((sum < -32768) ? -32768 : (int16_t)sum); }", i, i, i))
		}
	case x86asm.VPSUBSW:
		for i := 0; i < 8; i++ {
			lines = append(lines, fmt.Sprintf("      { int32_t diff = (int32_t)s1.s16[%d] - (int32_t)s2.s16[%d]; res.s16[%d] = (diff > 32767) ? 32767 : ((diff < -32768) ? -32768 : (int16_t)diff); }", i, i, i))
		}
	case x86asm.VPSUBUSB:
		for i := 0; i < 16; i++ {
			lines = append(lines, fmt.Sprintf("      { int32_t diff = (int32_t)s1.u8[%d] - (int32_t)s2.u8[%d]; res.u8[%d] = (diff < 0) ? 0 : (uint8_t)diff; }", i, i, i))
		}
	case x86asm.VPSUBUSW:
		for i := 0; i < 8; i++ {
			lines = append(lines, fmt.Sprintf("      { int32_t diff = (int32_t)s1.u16[%d] - (int32_t)s2.u16[%d]; res.u16[%d] = (diff < 0) ? 0 : (uint16_t)diff; }", i, i, i))
		}
	}

	lines = append(lines,
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVaddsub handles VADDSUBPS and VADDSUBPD
func (l *Lifter) liftVaddsub(op x86asm.Op, dst, src1, src2 x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	if !ok1 || !ok2 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("vaddsub requires XMM operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)
	lines = append(lines, fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg))
	lines = append(lines, "      xmm_reg_t res = {0};")

	if op == x86asm.VADDSUBPS {
		lines = append(lines,
			"      res.f32[0] = s1.f32[0] - s2.f32[0];",
			"      res.f32[1] = s1.f32[1] + s2.f32[1];",
			"      res.f32[2] = s1.f32[2] - s2.f32[2];",
			"      res.f32[3] = s1.f32[3] + s2.f32[3];",
		)
	} else if op == x86asm.VADDSUBPD {
		lines = append(lines,
			"      res.f64[0] = s1.f64[0] - s2.f64[0];",
			"      res.f64[1] = s1.f64[1] + s2.f64[1];",
		)
	}

	lines = append(lines,
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVcmppd handles VCMPPD dst_xmm/ymm, src1_xmm/ymm, src2_xmm/ymm/m128/m256, imm8
func (l *Lifter) liftVcmppd(dst, src1, src2, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	imm, ok3 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !ok3 || (!isXmm(dstReg) && !isYmm(dstReg)) {
		return nil, fmt.Errorf("vcmppd requires XMM or YMM operands")
	}
	imm8 := uint8(imm) & 0x1F

	predicateExpr := func(a, b string) string {
		switch imm8 {
		case 0:
			return fmt.Sprintf("(!isnan(%s) && !isnan(%s) && (%s == %s))", a, b, a, b)
		case 1:
			return fmt.Sprintf("(!isnan(%s) && !isnan(%s) && (%s < %s))", a, b, a, b)
		case 2:
			return fmt.Sprintf("(!isnan(%s) && !isnan(%s) && (%s <= %s))", a, b, a, b)
		case 3:
			return fmt.Sprintf("(isnan(%s) || isnan(%s))", a, b)
		case 4:
			return fmt.Sprintf("(isnan(%s) || isnan(%s) || (%s != %s))", a, b, a, b)
		case 5:
			return fmt.Sprintf("(isnan(%s) || isnan(%s) || !(%s < %s))", a, b, a, b)
		case 6:
			return fmt.Sprintf("(isnan(%s) || isnan(%s) || !(%s <= %s))", a, b, a, b)
		case 7:
			return fmt.Sprintf("(!isnan(%s) && !isnan(%s))", a, b)
		default:
			return fmt.Sprintf("(%s == %s)", a, b)
		}
	}

	lines := []string{"    {"}
	if isYmm(dstReg) && isYmm(src1Reg) {
		dIdx := ymmIdx(dstReg)
		s1Idx := ymmIdx(src1Reg)
		s2Code, err := l.loadYmmArg(src2, nextPC, "s2")
		if err != nil {
			return nil, err
		}
		lines = append(lines, s2Code...)
		lines = append(lines,
			fmt.Sprintf("      xmm_reg_t s1_lo = ctx->xmm[%d];", s1Idx),
			fmt.Sprintf("      xmm_reg_t s1_hi = ctx->ymmh[%d];", s1Idx),
			"      xmm_reg_t res_lo = {0}, res_hi = {0};",
		)
		for i := 0; i < 2; i++ {
			lines = append(lines,
				fmt.Sprintf("      res_lo.u64[%d] = %s ? 0xFFFFFFFFFFFFFFFFULL : 0;", i, predicateExpr(fmt.Sprintf("s1_lo.f64[%d]", i), fmt.Sprintf("s2_lo.f64[%d]", i))),
				fmt.Sprintf("      res_hi.u64[%d] = %s ? 0xFFFFFFFFFFFFFFFFULL : 0;", i, predicateExpr(fmt.Sprintf("s1_hi.f64[%d]", i), fmt.Sprintf("s2_hi.f64[%d]", i))),
			)
		}
		lines = append(lines,
			fmt.Sprintf("      ctx->xmm[%d] = res_lo;", dIdx),
			fmt.Sprintf("      ctx->ymmh[%d] = res_hi;", dIdx),
			"    }",
		)
		return lines, nil
	}

	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	dIdx := int(dstReg - x86asm.X0)

	s2Code, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2Code...)
	lines = append(lines, fmt.Sprintf("      xmm_reg_t s1 = ctx->%s;", infoSrc1.BaseReg))
	lines = append(lines, "      xmm_reg_t res = {0};")

	lines = append(lines,
		fmt.Sprintf("      res.u64[0] = %s ? 0xFFFFFFFFFFFFFFFFULL : 0;", predicateExpr("s1.f64[0]", "s2.f64[0]")),
		fmt.Sprintf("      res.u64[1] = %s ? 0xFFFFFFFFFFFFFFFFULL : 0;", predicateExpr("s1.f64[1]", "s2.f64[1]")),
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVcvttpd2dq handles VCVTTPD2DQ dst_xmm, src_xmm/m128
func (l *Lifter) liftVcvttpd2dq(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("vcvttpd2dq requires XMM destination")
	}
	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	sCode, err := l.loadXmmArg(src, nextPC, "s")
	if err != nil {
		return nil, err
	}
	lines = append(lines, sCode...)
	lines = append(lines,
		"      xmm_reg_t res = {0};",
		"      res.s32[0] = (int32_t)s.f64[0];",
		"      res.s32[1] = (int32_t)s.f64[1];",
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVcvtdq2pd handles VCVTDQ2PD dst_xmm, src_xmm/m64
func (l *Lifter) liftVcvtdq2pd(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("vcvtdq2pd requires XMM destination")
	}
	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	sCode, err := l.loadXmmArg(src, nextPC, "s")
	if err != nil {
		return nil, err
	}
	lines = append(lines, sCode...)
	lines = append(lines,
		"      xmm_reg_t res = {0};",
		"      res.f64[0] = (double)s.s32[0];",
		"      res.f64[1] = (double)s.s32[1];",
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVcvtph2ps handles VCVTPH2PS dst_xmm, src_xmm/m64 (F16C half to float)
func (l *Lifter) liftVcvtph2ps(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("vcvtph2ps requires XMM destination")
	}
	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)

	lines := []string{"    {"}
	sCode, err := l.loadXmmArg(src, nextPC, "s")
	if err != nil {
		return nil, err
	}
	lines = append(lines, sCode...)
	lines = append(lines,
		"      xmm_reg_t res = {0};",
		"      for (int i = 0; i < 4; i++) {",
		"        uint16_t h = s.u16[i];",
		"        uint32_t sign = ((uint32_t)(h & 0x8000)) << 16;",
		"        int32_t exp = (h >> 10) & 0x1F;",
		"        uint32_t mant = h & 0x3FF;",
		"        uint32_t f_bits = 0;",
		"        if (exp == 0) {",
		"          if (mant != 0) {",
		"            exp = 1;",
		"            while ((mant & 0x400) == 0) { mant <<= 1; exp--; }",
		"            mant &= 0x3FF;",
		"            f_bits = sign | ((uint32_t)(exp + 112) << 23) | (mant << 13);",
		"          } else { f_bits = sign; }",
		"        } else if (exp == 31) {",
		"          f_bits = sign | 0x7F800000 | (mant << 13);",
		"        } else {",
		"          f_bits = sign | ((uint32_t)(exp + 112) << 23) | (mant << 13);",
		"        }",
		"        memcpy(&res.f32[i], &f_bits, 4);",
		"      }",
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVcvtps2ph handles VCVTPS2PH dst_xmm/m64, src_xmm, imm8 (F16C float to half)
func (l *Lifter) liftVcvtps2ph(dst, src, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	lines := []string{"    {"}
	sCode, err := l.loadXmmArg(src, nextPC, "s")
	if err != nil {
		return nil, err
	}
	lines = append(lines, sCode...)
	lines = append(lines,
		"      uint16_t h_arr[4];",
		"      for (int i = 0; i < 4; i++) {",
		"        uint32_t f_bits; memcpy(&f_bits, &s.f32[i], 4);",
		"        uint16_t sign = (f_bits >> 16) & 0x8000;",
		"        int32_t exp = ((f_bits >> 23) & 0xFF) - 112;",
		"        uint32_t mant = f_bits & 0x7FFFFF;",
		"        uint16_t h = 0;",
		"        if (exp <= 0) { h = sign; }",
		"        else if (exp >= 31) { h = sign | 0x7C00; }",
		"        else { h = sign | ((uint16_t)exp << 10) | (uint16_t)(mant >> 13); }",
		"        h_arr[i] = h;",
		"      }",
	)

	if dstMem, ok := dst.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(dstMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(lines,
			fmt.Sprintf("      memcpy(ctx->mem_base + (%s), h_arr, 8);", addr),
			"    }",
		)
		return lines, nil
	}

	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("vcvtps2ph destination must be XMM or m64")
	}
	infoDst := regMap[dstReg]
	dIdx := int(dstReg - x86asm.X0)
	lines = append(lines,
		"      xmm_reg_t res = {0};",
		"      memcpy(res.u16, h_arr, 8);",
		fmt.Sprintf("      ctx->%s = res;", infoDst.BaseReg),
		fmt.Sprintf("      memset(&ctx->ymmh[%d], 0, 16);", dIdx),
		"    }",
	)
	return lines, nil
}

// liftVmxcsr handles VSTMXCSR and VLDMXCSR
func (l *Lifter) liftVmxcsr(isStore bool, arg x86asm.Arg, nextPC uint64) ([]string, error) {
	mem, ok := arg.(x86asm.Mem)
	if !ok {
		return nil, fmt.Errorf("mxcsr requires memory operand")
	}
	addr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	if isStore {
		return []string{
			fmt.Sprintf("    MEM_U32(%s) = 0x1F80;", addr),
		}, nil
	}
	return []string{
		fmt.Sprintf("    { (void)MEM_U32(%s); }", addr),
	}, nil
}
