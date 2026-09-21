package lifter

import (
	"fmt"

	"golang.org/x/arch/x86/x86asm"
)

func (l *Lifter) loadXmmArg(src x86asm.Arg, nextPC uint64, varName string) ([]string, error) {
	if srcReg, ok := src.(x86asm.Reg); ok && isXmm(srcReg) {
		info := regMap[srcReg]
		return []string{fmt.Sprintf("      xmm_reg_t %s = ctx->%s;", varName, info.BaseReg)}, nil
	}
	if srcMem, ok := src.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{
			fmt.Sprintf("      xmm_reg_t %s;", varName),
			fmt.Sprintf("      memcpy(&%s, ctx->mem_base + (%s), 16);", varName, addr),
		}, nil
	}
	return nil, fmt.Errorf("expected XMM or memory operand")
}

func (l *Lifter) liftShufps(dst, src1, src2, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(dstReg) {
		return nil, fmt.Errorf("shufps requires XMM destination and immediate")
	}
	infoDst := regMap[dstReg]
	lines := []string{"    {"}
	s1, err := l.loadXmmArg(src1, nextPC, "s1")
	if err != nil {
		return nil, err
	}
	s2, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s1...)
	lines = append(lines, s2...)
	imm8 := uint8(imm)
	for i := 0; i < 4; i++ {
		sel := (imm8 >> (i * 2)) & 3
		src := "s1"
		if i >= 2 {
			src = "s2"
		}
		lines = append(lines, fmt.Sprintf("      ctx->%s.f32[%d] = %s.f32[%d];", infoDst.BaseReg, i, src, sel))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftPermilpsImm(dst, src, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(dstReg) {
		return nil, fmt.Errorf("vpermilps requires XMM destination and immediate")
	}
	infoDst := regMap[dstReg]
	lines := []string{"    {"}
	s, err := l.loadXmmArg(src, nextPC, "src")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s...)
	imm8 := uint8(imm)
	for i := 0; i < 4; i++ {
		sel := (imm8 >> (i * 2)) & 3
		lines = append(lines, fmt.Sprintf("      ctx->%s.f32[%d] = src.f32[%d];", infoDst.BaseReg, i, sel))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftPermilpdImm(dst, src, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(dstReg) {
		return nil, fmt.Errorf("vpermilpd requires XMM destination and immediate")
	}
	infoDst := regMap[dstReg]
	lines := []string{"    {"}
	s, err := l.loadXmmArg(src, nextPC, "src")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s...)
	imm8 := uint8(imm)
	for i := 0; i < 2; i++ {
		sel := (imm8 >> i) & 1
		lines = append(lines, fmt.Sprintf("      ctx->%s.f64[%d] = src.f64[%d];", infoDst.BaseReg, i, sel))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftMovshdup(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("vmovshdup destination must be XMM")
	}
	infoDst := regMap[dstReg]
	lines := []string{"    {"}
	s, err := l.loadXmmArg(src, nextPC, "src")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s...)
	lines = append(
		lines,
		fmt.Sprintf("      ctx->%s.f32[0] = src.f32[1];", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.f32[1] = src.f32[1];", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.f32[2] = src.f32[3];", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.f32[3] = src.f32[3];", infoDst.BaseReg),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftInsertps(dst, src1, src2, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	imm, ok3 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !ok3 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("vinsertps invalid operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	imm8 := uint8(imm)
	countS := (imm8 >> 6) & 3
	countD := (imm8 >> 4) & 3
	zmask := imm8 & 0xF
	lines := []string{
		"    {",
		fmt.Sprintf("      xmm_reg_t tmp = ctx->%s;", infoSrc1.BaseReg),
	}
	if src2Reg, ok := src2.(x86asm.Reg); ok && isXmm(src2Reg) {
		infoSrc2 := regMap[src2Reg]
		lines = append(lines, fmt.Sprintf("      tmp.f32[%d] = ctx->%s.f32[%d];", countD, infoSrc2.BaseReg, countS))
	} else if src2Mem, ok := src2.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(src2Mem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(lines, fmt.Sprintf("      { uint32_t u = MEM_U32(%s); memcpy(&tmp.f32[%d], &u, 4); }", addr, countD))
	} else {
		return nil, fmt.Errorf("vinsertps invalid src2")
	}
	for i := 0; i < 4; i++ {
		if zmask&(1<<i) != 0 {
			lines = append(lines, fmt.Sprintf("      tmp.f32[%d] = 0.0f;", i))
		}
	}
	lines = append(lines, fmt.Sprintf("      ctx->%s = tmp;", infoDst.BaseReg), "    }")
	return lines, nil
}

func (l *Lifter) liftBlendps(dst, src1, src2, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(dstReg) {
		return nil, fmt.Errorf("vblendps invalid operands")
	}
	infoDst := regMap[dstReg]
	lines := []string{"    {"}
	s1, err := l.loadXmmArg(src1, nextPC, "s1")
	if err != nil {
		return nil, err
	}
	s2, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s1...)
	lines = append(lines, s2...)
	imm8 := uint8(imm)
	for i := 0; i < 4; i++ {
		src := "s1"
		if imm8&(1<<i) != 0 {
			src = "s2"
		}
		lines = append(lines, fmt.Sprintf("      ctx->%s.f32[%d] = %s.f32[%d];", infoDst.BaseReg, i, src, i))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftBlendvps(dst, src1, src2, maskArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	if !ok1 || !isXmm(dstReg) {
		return nil, fmt.Errorf("vblendvps destination must be XMM")
	}
	infoDst := regMap[dstReg]
	lines := []string{"    {"}
	s1, err := l.loadXmmArg(src1, nextPC, "s1")
	if err != nil {
		return nil, err
	}
	s2, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	mask, err := l.loadXmmArg(maskArg, nextPC, "mask")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s1...)
	lines = append(lines, s2...)
	lines = append(lines, mask...)
	for i := 0; i < 4; i++ {
		lines = append(lines, fmt.Sprintf("      ctx->%s.f32[%d] = (mask.u32[%d] & 0x80000000U) ? s2.f32[%d] : s1.f32[%d];",
			infoDst.BaseReg, i, i, i, i))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftMovmskps(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok {
		return nil, fmt.Errorf("vmovmskps destination must be a GPR")
	}
	lines := []string{"    {"}
	s, err := l.loadXmmArg(src, nextPC, "src")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s...)
	lines = append(lines, "      uint32_t m = ((src.u32[0] >> 31) & 1) | ((src.u32[1] >> 30) & 2) | ((src.u32[2] >> 29) & 4) | ((src.u32[3] >> 28) & 8);")
	stmt, err := GetRegWriteStmt(dstReg, "m")
	if err != nil {
		return nil, err
	}
	lines = append(lines, "      "+stmt, "    }")
	return lines, nil
}

func cmppsPredicate(pred uint8, a, b string) string {
	switch pred & 7 {
	case 0: // EQ
		return fmt.Sprintf("((%s) == (%s))", a, b)
	case 1: // LT
		return fmt.Sprintf("((%s) < (%s))", a, b)
	case 2: // LE
		return fmt.Sprintf("((%s) <= (%s))", a, b)
	case 3: // UNORD
		return fmt.Sprintf("(isnan(%s) || isnan(%s))", a, b)
	case 4: // NEQ
		return fmt.Sprintf("((%s) != (%s))", a, b)
	case 5: // NLT
		return fmt.Sprintf("(!((%s) < (%s)))", a, b)
	case 6: // NLE
		return fmt.Sprintf("(!((%s) <= (%s)))", a, b)
	default: // ORD
		return fmt.Sprintf("(!isnan(%s) && !isnan(%s))", a, b)
	}
}

func (l *Lifter) liftCmpps(dst, src1, src2, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(dstReg) {
		return nil, fmt.Errorf("vcmpps invalid operands")
	}
	infoDst := regMap[dstReg]
	pred := uint8(imm)
	lines := []string{"    {"}
	s1, err := l.loadXmmArg(src1, nextPC, "s1")
	if err != nil {
		return nil, err
	}
	s2, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s1...)
	lines = append(lines, s2...)
	for i := 0; i < 4; i++ {
		cond := cmppsPredicate(pred, fmt.Sprintf("s1.f32[%d]", i), fmt.Sprintf("s2.f32[%d]", i))
		lines = append(lines, fmt.Sprintf("      ctx->%s.u32[%d] = (%s) ? 0xFFFFFFFFU : 0;", infoDst.BaseReg, i, cond))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func instHasRep(inst x86asm.Inst) bool {
	return instRepPrefix(inst) == 0xF3
}

func instHasRepn(inst x86asm.Inst) bool {
	return instRepPrefix(inst) == 0xF2
}

func instRepPrefix(inst x86asm.Inst) byte {
	for _, p := range inst.Prefix {
		if p == 0 {
			continue
		}
		switch p & 0xFF {
		case 0xF3, 0xF2:
			return byte(p & 0xFF)
		}
	}
	return 0
}

func (l *Lifter) liftMovsByte(inst x86asm.Inst) []string {
	if instHasRep(inst) {
		return []string{
			"    memmove(ctx->mem_base + ctx->rdi, ctx->mem_base + ctx->rsi, (size_t)ctx->rcx);",
			"    ctx->rdi += ctx->rcx;",
			"    ctx->rsi += ctx->rcx;",
			"    ctx->rcx = 0;",
		}
	}
	return []string{
		"    MEM_U8(ctx->rdi) = MEM_U8(ctx->rsi);",
		"    ctx->rsi += 1;",
		"    ctx->rdi += 1;",
	}
}

func (l *Lifter) liftStosByte(inst x86asm.Inst) []string {
	if instHasRep(inst) {
		return []string{
			"    memset(ctx->mem_base + ctx->rdi, (int)(uint8_t)ctx->rax, (size_t)ctx->rcx);",
			"    ctx->rdi += ctx->rcx;",
			"    ctx->rcx = 0;",
		}
	}
	return []string{
		"    MEM_U8(ctx->rdi) = (uint8_t)ctx->rax;",
		"    ctx->rdi += 1;",
	}
}

func (l *Lifter) liftScasByte(inst x86asm.Inst) []string {
	// Direction is always incrementing (CLD). F2 REPNE stops on match;
	// F3 REPE stops on mismatch. Both consume RCX as the scan limit.
	stopOnMatch := instHasRepn(inst)
	counted := stopOnMatch || instHasRep(inst)
	if counted {
		cond := "if (ctx->zf)"
		if !stopOnMatch {
			cond = "if (!ctx->zf)"
		}
		return []string{
			"    { uint8_t al = (uint8_t)ctx->rax; uint64_t n = ctx->rcx;",
			"      uint8_t *p = ctx->mem_base + ctx->rdi;",
			"      uint64_t i = 0;",
			"      ctx->zf = 0;",
			"      for (; i < n; i++) {",
			"        uint8_t v = p[i];",
			"        uint8_t res = (uint8_t)(v - al);",
			"        set_flags_sub_u8(ctx, v, al, res);",
			"        " + cond + " { i++; break; }",
			"      }",
			"      ctx->rdi += i; ctx->rcx = n - i;",
			"    }",
		}
	}
	return []string{
		"    { uint8_t v = MEM_U8(ctx->rdi); uint8_t al = (uint8_t)ctx->rax;",
		"      uint8_t res = (uint8_t)(v - al);",
		"      set_flags_sub_u8(ctx, v, al, res);",
		"      ctx->rdi += 1;",
		"    }",
	}
}
