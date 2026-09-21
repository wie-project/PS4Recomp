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

func stringElemCType(width int) string {
	switch width {
	case 2:
		return "uint16_t"
	case 4:
		return "uint32_t"
	case 8:
		return "uint64_t"
	default:
		return "uint8_t"
	}
}

func stringMemLoad(addr string, width int) string {
	switch width {
	case 2:
		return "MEM_U16(" + addr + ")"
	case 4:
		return "MEM_U32(" + addr + ")"
	case 8:
		return "MEM_U64(" + addr + ")"
	default:
		return "MEM_U8(" + addr + ")"
	}
}

func stringMemStore(addr, val string, width int) string {
	switch width {
	case 2:
		return "MEM_U16(" + addr + ") = (uint16_t)(" + val + ");"
	case 4:
		return "MEM_U32(" + addr + ") = (uint32_t)(" + val + ");"
	case 8:
		return "MEM_U64(" + addr + ") = (uint64_t)(" + val + ");"
	default:
		return "MEM_U8(" + addr + ") = (uint8_t)(" + val + ");"
	}
}

func (l *Lifter) liftMovs(inst x86asm.Inst, width int) []string {
	if instHasRep(inst) {
		return []string{
			fmt.Sprintf("    { size_t n = (size_t)ctx->rcx * %dULL;", width),
			"      memmove(ctx->mem_base + ctx->rdi, ctx->mem_base + ctx->rsi, n);",
			"      ctx->rdi += n; ctx->rsi += n; ctx->rcx = 0;",
			"    }",
		}
	}
	return []string{
		"    " + stringMemStore("ctx->rdi", stringMemLoad("ctx->rsi", width), width),
		fmt.Sprintf("    ctx->rsi += %d; ctx->rdi += %d;", width, width),
	}
}

func (l *Lifter) liftStos(inst x86asm.Inst, width int) []string {
	if width == 1 && instHasRep(inst) {
		return []string{
			"    memset(ctx->mem_base + ctx->rdi, (int)(uint8_t)ctx->rax, (size_t)ctx->rcx);",
			"    ctx->rdi += ctx->rcx;",
			"    ctx->rcx = 0;",
		}
	}
	ct := stringElemCType(width)
	if instHasRep(inst) {
		return []string{
			fmt.Sprintf("    { %s v = (%s)ctx->rax; uint64_t n = ctx->rcx;", ct, ct),
			fmt.Sprintf("      %s *p = (%s *)(ctx->mem_base + ctx->rdi);", ct, ct),
			"      for (uint64_t i = 0; i < n; i++) p[i] = v;",
			fmt.Sprintf("      ctx->rdi += n * %dULL; ctx->rcx = 0;", width),
			"    }",
		}
	}
	return []string{
		"    " + stringMemStore("ctx->rdi", "ctx->rax", width),
		fmt.Sprintf("    ctx->rdi += %d;", width),
	}
}

func (l *Lifter) liftLods(inst x86asm.Inst, width int) []string {
	ct := stringElemCType(width)
	load := stringMemLoad("ctx->rsi", width)
	var (
		write string
		err   error
	)
	switch width {
	case 1:
		write, err = GetRegWriteStmt(x86asm.AL, load)
	case 2:
		write, err = GetRegWriteStmt(x86asm.AX, load)
	case 4:
		write, err = GetRegWriteStmt(x86asm.EAX, load)
	default:
		write, err = GetRegWriteStmt(x86asm.RAX, load)
	}
	if err != nil {
		write = fmt.Sprintf("ctx->rax = (%s)(%s);", ct, load)
	}
	if instHasRep(inst) {
		return []string{
			fmt.Sprintf("    { uint64_t n = ctx->rcx; if (n) { ctx->rsi += (n - 1) * %dULL; %s ctx->rsi += %d; } ctx->rcx = 0; }", width, write, width),
		}
	}
	return []string{
		"    " + write,
		fmt.Sprintf("    ctx->rsi += %d;", width),
	}
}

func (l *Lifter) liftScas(inst x86asm.Inst, width int) []string {
	ct := stringElemCType(width)
	bits := width * 8
	acc := fmt.Sprintf("(%s)ctx->rax", ct)
	stopOnMatch := instHasRepn(inst)
	counted := stopOnMatch || instHasRep(inst)
	if counted {
		cond := "if (ctx->zf)"
		if !stopOnMatch {
			cond = "if (!ctx->zf)"
		}
		return []string{
			fmt.Sprintf("    { %s al = %s; uint64_t n = ctx->rcx; uint64_t i = 0;", ct, acc),
			"      if (n) {",
			fmt.Sprintf("        %s *p = (%s *)(ctx->mem_base + ctx->rdi);", ct, ct),
			"        for (; i < n; i++) {",
			fmt.Sprintf("          %s v = p[i]; %s res = (%s)(v - al);", ct, ct, ct),
			fmt.Sprintf("          set_flags_sub_u%d(ctx, v, al, res);", bits),
			"          " + cond + " { i++; break; }",
			"        }",
			fmt.Sprintf("        ctx->rdi += i * %dULL; ctx->rcx = n - i;", width),
			"      }",
			"    }",
		}
	}
	return []string{
		fmt.Sprintf("    { %s v = (%s)%s; %s al = %s;", ct, ct, stringMemLoad("ctx->rdi", width), ct, acc),
		fmt.Sprintf("      %s res = (%s)(v - al);", ct, ct),
		fmt.Sprintf("      set_flags_sub_u%d(ctx, v, al, res);", bits),
		fmt.Sprintf("      ctx->rdi += %d;", width),
		"    }",
	}
}

func (l *Lifter) liftCmps(inst x86asm.Inst, width int) []string {
	ct := stringElemCType(width)
	bits := width * 8
	stopOnMatch := instHasRepn(inst)
	counted := stopOnMatch || instHasRep(inst)
	if counted {
		cond := "if (ctx->zf)"
		if !stopOnMatch {
			cond = "if (!ctx->zf)"
		}
		return []string{
			"    { uint64_t n = ctx->rcx; uint64_t i = 0;",
			"      if (n) {",
			fmt.Sprintf("        %s *a = (%s *)(ctx->mem_base + ctx->rsi);", ct, ct),
			fmt.Sprintf("        %s *b = (%s *)(ctx->mem_base + ctx->rdi);", ct, ct),
			"        for (; i < n; i++) {",
			fmt.Sprintf("          %s va = a[i]; %s vb = b[i]; %s res = (%s)(va - vb);", ct, ct, ct, ct),
			fmt.Sprintf("          set_flags_sub_u%d(ctx, va, vb, res);", bits),
			"          " + cond + " { i++; break; }",
			"        }",
			fmt.Sprintf("        ctx->rsi += i * %dULL; ctx->rdi += i * %dULL; ctx->rcx = n - i;", width, width),
			"      }",
			"    }",
		}
	}
	return []string{
		fmt.Sprintf("    { %s va = (%s)%s; %s vb = (%s)%s;", ct, ct, stringMemLoad("ctx->rsi", width), ct, ct, stringMemLoad("ctx->rdi", width)),
		fmt.Sprintf("      %s res = (%s)(va - vb);", ct, ct),
		fmt.Sprintf("      set_flags_sub_u%d(ctx, va, vb, res);", bits),
		fmt.Sprintf("      ctx->rsi += %d; ctx->rdi += %d;", width, width),
		"    }",
	}
}

func (l *Lifter) liftLeave() []string {
	return []string{
		"    ctx->rsp = ctx->rbp;",
		"    ctx->rbp = MEM_U64(ctx->rsp);",
		"    ctx->rsp += 8;",
	}
}

func (l *Lifter) liftEnter(args x86asm.Args) []string {
	alloc := int64(0)
	if imm, ok := args[0].(x86asm.Imm); ok {
		alloc = int64(uint16(imm))
	}
	level := 0
	if imm, ok := args[1].(x86asm.Imm); ok {
		level = int(imm) & 0x1f
	}
	lines := []string{
		"    ctx->rsp -= 8;",
		"    MEM_U64(ctx->rsp) = ctx->rbp;",
		"    { uint64_t frame = ctx->rsp;",
	}
	if level > 0 {
		lines = append(
			lines,
			fmt.Sprintf("      for (int i = 1; i < %d; i++) { ctx->rbp -= 8; ctx->rsp -= 8; MEM_U64(ctx->rsp) = MEM_U64(ctx->rbp); }", level),
			"      ctx->rsp -= 8; MEM_U64(ctx->rsp) = frame;",
		)
	}
	lines = append(
		lines,
		"      ctx->rbp = frame;",
		fmt.Sprintf("      ctx->rsp -= %dULL;", uint64(alloc)),
		"    }",
	)
	return lines
}

func (l *Lifter) liftSahf() []string {
	return []string{
		"    { uint8_t ah = (uint8_t)(ctx->rax >> 8);",
		"      ctx->sf = (ah >> 7) & 1; ctx->zf = (ah >> 6) & 1;",
		"      ctx->af = (ah >> 4) & 1; ctx->pf = (ah >> 2) & 1; ctx->cf = ah & 1;",
		"    }",
	}
}

func (l *Lifter) liftLahf() []string {
	return []string{
		"    { uint8_t ah = (uint8_t)((ctx->sf << 7) | (ctx->zf << 6) | (ctx->af << 4) | (ctx->pf << 2) | 0x2 | ctx->cf);",
		"      ctx->rax = (ctx->rax & ~0xFF00ULL) | ((uint64_t)ah << 8);",
		"    }",
	}
}

func (l *Lifter) liftMovsldup(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("vmovsldup destination must be XMM")
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
		fmt.Sprintf("      ctx->%s.f32[0] = src.f32[0];", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.f32[1] = src.f32[0];", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.f32[2] = src.f32[2];", infoDst.BaseReg),
		fmt.Sprintf("      ctx->%s.f32[3] = src.f32[2];", infoDst.BaseReg),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftExtractps(dst, src, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	srcReg, ok1 := src.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(srcReg) {
		return nil, fmt.Errorf("vextractps requires XMM source and immediate")
	}
	infoSrc := regMap[srcReg]
	sel := uint8(imm) & 3
	val := fmt.Sprintf("ctx->%s.u32[%d]", infoSrc.BaseReg, sel)
	writes, err := l.getOperandWrite(dst, 4, val, nextPC)
	if err != nil {
		return nil, err
	}
	lines := make([]string, 0, len(writes))
	for _, w := range writes {
		lines = append(lines, "    "+w)
	}
	return lines, nil
}

func (l *Lifter) liftVcmpss(dst, src1, src2, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	imm, ok3 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !ok3 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("vcmpss invalid operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	pred := uint8(imm)
	lines := []string{
		fmt.Sprintf("    ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
		"    {",
	}
	s2, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2...)
	cond := cmppsPredicate(pred, fmt.Sprintf("ctx->%s.f32[0]", infoDst.BaseReg), "s2.f32[0]")
	lines = append(
		lines,
		fmt.Sprintf("      ctx->%s.u32[0] = (%s) ? 0xFFFFFFFFU : 0;", infoDst.BaseReg, cond),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftVpblendvb(dst, src1, src2, maskArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("vpblendvb destination must be XMM")
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
	lines = append(
		lines,
		"      for (int i = 0; i < 16; i++) {",
		fmt.Sprintf("        ctx->%s.u8[i] = (mask.u8[i] & 0x80U) ? s2.u8[i] : s1.u8[i];", infoDst.BaseReg),
		"      }",
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftVcmpsd(dst, src1, src2, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	src1Reg, ok2 := src1.(x86asm.Reg)
	imm, ok3 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !ok3 || !isXmm(dstReg) || !isXmm(src1Reg) {
		return nil, fmt.Errorf("vcmpsd invalid operands")
	}
	infoDst := regMap[dstReg]
	infoSrc1 := regMap[src1Reg]
	pred := uint8(imm)
	lines := []string{
		fmt.Sprintf("    ctx->%s = ctx->%s;", infoDst.BaseReg, infoSrc1.BaseReg),
		"    {",
	}
	s2, err := l.loadXmmArg(src2, nextPC, "s2")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s2...)
	cond := cmppsPredicate(pred, fmt.Sprintf("ctx->%s.f64[0]", infoDst.BaseReg), "s2.f64[0]")
	lines = append(
		lines,
		fmt.Sprintf("      ctx->%s.u64[0] = (%s) ? 0xFFFFFFFFFFFFFFFFULL : 0;", infoDst.BaseReg, cond),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftShufpd(dst, src1, src2, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(dstReg) {
		return nil, fmt.Errorf("shufpd requires XMM destination and immediate")
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
	sel0 := uint8(imm) & 1
	sel1 := (uint8(imm) >> 1) & 1
	lines = append(
		lines,
		fmt.Sprintf("      ctx->%s.f64[0] = s1.f64[%d];", infoDst.BaseReg, sel0),
		fmt.Sprintf("      ctx->%s.f64[1] = s2.f64[%d];", infoDst.BaseReg, sel1),
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftMovlhps(dst, src1, src2 x86asm.Arg) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	s1, ok2 := src1.(x86asm.Reg)
	s2, ok3 := src2.(x86asm.Reg)
	if !ok1 || !ok2 || !ok3 || !isXmm(dstReg) || !isXmm(s1) || !isXmm(s2) {
		return nil, fmt.Errorf("movlhps requires XMM registers")
	}
	infoDst := regMap[dstReg]
	info1 := regMap[s1]
	info2 := regMap[s2]
	return []string{
		fmt.Sprintf("    { uint64_t lo = ctx->%s.u64[0]; uint64_t hi = ctx->%s.u64[0];", info1.BaseReg, info2.BaseReg),
		fmt.Sprintf("      ctx->%s.u64[0] = lo; ctx->%s.u64[1] = hi;", infoDst.BaseReg, infoDst.BaseReg),
		"    }",
	}, nil
}

func (l *Lifter) liftMovhlps(dst, src1, src2 x86asm.Arg) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	s1, ok2 := src1.(x86asm.Reg)
	s2, ok3 := src2.(x86asm.Reg)
	if !ok1 || !ok2 || !ok3 || !isXmm(dstReg) || !isXmm(s1) || !isXmm(s2) {
		return nil, fmt.Errorf("movhlps requires XMM registers")
	}
	infoDst := regMap[dstReg]
	info1 := regMap[s1]
	info2 := regMap[s2]
	return []string{
		fmt.Sprintf("    { uint64_t lo = ctx->%s.u64[1]; uint64_t hi = ctx->%s.u64[1];", info1.BaseReg, info2.BaseReg),
		fmt.Sprintf("      ctx->%s.u64[0] = lo; ctx->%s.u64[1] = hi;", infoDst.BaseReg, infoDst.BaseReg),
		"    }",
	}, nil
}

func (l *Lifter) liftPshufb(dst, data, mask x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("pshufb destination must be XMM")
	}
	infoDst := regMap[dstReg]
	lines := []string{"    {"}
	d, err := l.loadXmmArg(data, nextPC, "data")
	if err != nil {
		return nil, err
	}
	m, err := l.loadXmmArg(mask, nextPC, "mask")
	if err != nil {
		return nil, err
	}
	lines = append(lines, d...)
	lines = append(lines, m...)
	lines = append(
		lines,
		"      for (int i = 0; i < 16; i++) {",
		"        uint8_t idx = mask.u8[i];",
		fmt.Sprintf("        ctx->%s.u8[i] = (idx & 0x80U) ? 0 : data.u8[idx & 15];", infoDst.BaseReg),
		"      }",
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftPalignr(dst, src1, src2, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(dstReg) {
		return nil, fmt.Errorf("palignr requires XMM destination and immediate")
	}
	infoDst := regMap[dstReg]
	shift := int(uint8(imm))
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
	lines = append(
		lines,
		"      uint8_t cat[32];",
		"      memcpy(cat, s2.u8, 16); memcpy(cat + 16, s1.u8, 16);",
	)
	if shift >= 32 {
		lines = append(lines, fmt.Sprintf("      memset(&ctx->%s, 0, 16);", infoDst.BaseReg))
	} else {
		lines = append(lines, fmt.Sprintf("      memcpy(ctx->%s.u8, cat + %d, 16);", infoDst.BaseReg, shift))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftPextr(elemBytes int, dst, src, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	srcReg, ok1 := src.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(srcReg) {
		return nil, fmt.Errorf("pextr requires XMM source and immediate")
	}
	infoSrc := regMap[srcReg]
	n := 16 / elemBytes
	sel := int(uint8(imm)) & (n - 1)
	var val string
	switch elemBytes {
	case 1:
		val = fmt.Sprintf("ctx->%s.u8[%d]", infoSrc.BaseReg, sel)
	case 2:
		val = fmt.Sprintf("ctx->%s.u16[%d]", infoSrc.BaseReg, sel)
	case 8:
		val = fmt.Sprintf("ctx->%s.u64[%d]", infoSrc.BaseReg, sel)
	default:
		val = fmt.Sprintf("ctx->%s.u32[%d]", infoSrc.BaseReg, sel)
	}
	writes, err := l.getOperandWrite(dst, elemBytes, val, nextPC)
	if err != nil {
		return nil, err
	}
	lines := make([]string, 0, len(writes))
	for _, w := range writes {
		lines = append(lines, "    "+w)
	}
	return lines, nil
}

func (l *Lifter) liftPminmax(isMin, signed bool, elemBytes int, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("pmin/pmax destination must be XMM")
	}
	infoDst := regMap[dstReg]
	n := 16 / elemBytes
	field := "u32"
	switch {
	case signed && elemBytes == 1:
		field = "s8"
	case signed && elemBytes == 2:
		field = "s16"
	case signed && elemBytes == 4:
		field = "s32"
	case signed && elemBytes == 8:
		field = "s64"
	case elemBytes == 1:
		field = "u8"
	case elemBytes == 2:
		field = "u16"
	case elemBytes == 8:
		field = "u64"
	}
	cmp := "<"
	if !isMin {
		cmp = ">"
	}
	lines := []string{"    {"}
	s, err := l.loadXmmArg(src, nextPC, "src")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s...)
	lines = append(lines, fmt.Sprintf("      for (int i = 0; i < %d; i++) {", n))
	lines = append(lines, fmt.Sprintf("        if (src.%s[i] %s ctx->%s.%s[i]) ctx->%s.%s[i] = src.%s[i];",
		field, cmp, infoDst.BaseReg, field, infoDst.BaseReg, field, field))
	lines = append(lines, "      }", "    }")
	return lines, nil
}

func (l *Lifter) liftPackedSqrt(isDouble bool, dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok || !isXmm(dstReg) {
		return nil, fmt.Errorf("sqrtps/sqrtpd destination must be XMM")
	}
	infoDst := regMap[dstReg]
	lines := []string{"    {"}
	s, err := l.loadXmmArg(src, nextPC, "src")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s...)
	if isDouble {
		lines = append(
			lines,
			fmt.Sprintf("      ctx->%s.f64[0] = sqrt(src.f64[0]);", infoDst.BaseReg),
			fmt.Sprintf("      ctx->%s.f64[1] = sqrt(src.f64[1]);", infoDst.BaseReg),
		)
	} else {
		lines = append(
			lines,
			fmt.Sprintf("      ctx->%s.f32[0] = sqrtf(src.f32[0]);", infoDst.BaseReg),
			fmt.Sprintf("      ctx->%s.f32[1] = sqrtf(src.f32[1]);", infoDst.BaseReg),
			fmt.Sprintf("      ctx->%s.f32[2] = sqrtf(src.f32[2]);", infoDst.BaseReg),
			fmt.Sprintf("      ctx->%s.f32[3] = sqrtf(src.f32[3]);", infoDst.BaseReg),
		)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftPtest(dst, src x86asm.Arg, nextPC uint64) ([]string, error) {
	lines := []string{"    {"}
	a, err := l.loadXmmArg(dst, nextPC, "a")
	if err != nil {
		return nil, err
	}
	b, err := l.loadXmmArg(src, nextPC, "b")
	if err != nil {
		return nil, err
	}
	lines = append(lines, a...)
	lines = append(lines, b...)
	lines = append(
		lines,
		"      ctx->zf = ((a.u64[0] & b.u64[0]) | (a.u64[1] & b.u64[1])) == 0;",
		"      ctx->cf = (((~a.u64[0]) & b.u64[0]) | ((~a.u64[1]) & b.u64[1])) == 0;",
		"      ctx->of = 0; ctx->sf = 0; ctx->af = 0; ctx->pf = 0;",
		"    }",
	)
	return lines, nil
}

func (l *Lifter) liftRoundps(dst, src, immArg x86asm.Arg, nextPC uint64) ([]string, error) {
	dstReg, ok1 := dst.(x86asm.Reg)
	imm, ok2 := immArg.(x86asm.Imm)
	if !ok1 || !ok2 || !isXmm(dstReg) {
		return nil, fmt.Errorf("roundps requires XMM destination and immediate")
	}
	infoDst := regMap[dstReg]
	var roundFunc string
	switch imm & 3 {
	case 1:
		roundFunc = "floorf"
	case 2:
		roundFunc = "ceilf"
	case 3:
		roundFunc = "truncf"
	default:
		roundFunc = "roundf"
	}
	lines := []string{"    {"}
	s, err := l.loadXmmArg(src, nextPC, "src")
	if err != nil {
		return nil, err
	}
	lines = append(lines, s...)
	for i := 0; i < 4; i++ {
		lines = append(lines, fmt.Sprintf("      ctx->%s.f32[%d] = %s(src.f32[%d]);", infoDst.BaseReg, i, roundFunc, i))
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftCwd() []string {
	return []string{
		"    if ((int16_t)ctx->rax < 0) ctx->rdx = (ctx->rdx & ~0xFFFFULL) | 0xFFFFULL;",
		"    else ctx->rdx &= ~0xFFFFULL;",
	}
}

func (l *Lifter) liftMovnti(dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	val, sz, err := l.getOperandRead(src, defMemSz, nextPC)
	if err != nil {
		return nil, err
	}
	writes, err := l.getOperandWrite(dst, sz, val, nextPC)
	if err != nil {
		return nil, err
	}
	lines := make([]string, 0, len(writes))
	for _, w := range writes {
		lines = append(lines, "    "+w)
	}
	return lines, nil
}
