package lifter

import (
	"fmt"

	"golang.org/x/arch/x86/x86asm"
)

func (l *Lifter) liftFld(arg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	if reg, ok := arg.(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
		idx := int(reg - x86asm.F0)
		return []string{fmt.Sprintf("    fpu_push(ctx, FPU_ST(%d));", idx)}, nil
	}
	if mem, ok := arg.(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		if defMemSz == 8 {
			return []string{
				fmt.Sprintf("    { double d; uint64_t u = MEM_U64(%s); memcpy(&d, &u, 8); fpu_push(ctx, d); }", addrExpr),
			}, nil
		}
		return []string{
			fmt.Sprintf("    { float f; uint32_t u = MEM_U32(%s); memcpy(&f, &u, 4); fpu_push(ctx, (double)f); }", addrExpr),
		}, nil
	}
	return nil, fmt.Errorf("unsupported FLD operand")
}

func (l *Lifter) liftFild(arg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	if mem, ok := arg.(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		if defMemSz == 8 {
			return []string{fmt.Sprintf("    fpu_push(ctx, (double)(int64_t)MEM_U64(%s));", addrExpr)}, nil
		}
		if defMemSz == 2 {
			return []string{fmt.Sprintf("    fpu_push(ctx, (double)(int16_t)MEM_U16(%s));", addrExpr)}, nil
		}
		return []string{fmt.Sprintf("    fpu_push(ctx, (double)(int32_t)MEM_U32(%s));", addrExpr)}, nil
	}
	return nil, fmt.Errorf("unsupported FILD operand")
}

func (l *Lifter) liftFstp(arg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	if reg, ok := arg.(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
		idx := int(reg - x86asm.F0)
		return []string{fmt.Sprintf("    FPU_ST(%d) = fpu_pop(ctx);", idx)}, nil
	}
	if mem, ok := arg.(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		if defMemSz == 8 {
			return []string{
				fmt.Sprintf("    { double d = fpu_pop(ctx); uint64_t u; memcpy(&u, &d, 8); MEM_U64(%s) = u; }", addrExpr),
			}, nil
		}
		return []string{
			fmt.Sprintf("    { float f = (float)fpu_pop(ctx); uint32_t u; memcpy(&u, &f, 4); MEM_U32(%s) = u; }", addrExpr),
		}, nil
	}
	return nil, fmt.Errorf("unsupported FSTP operand")
}

func (l *Lifter) liftFist(op x86asm.Op, arg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	if mem, ok := arg.(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		valExpr := "FPU_ST(0)"
		if op == x86asm.FISTP {
			valExpr = "fpu_pop(ctx)"
		}
		if defMemSz == 8 {
			return []string{fmt.Sprintf("    MEM_U64(%s) = (uint64_t)(int64_t)round(%s);", addrExpr, valExpr)}, nil
		}
		if defMemSz == 2 {
			return []string{fmt.Sprintf("    MEM_U16(%s) = (uint16_t)(int16_t)round(%s);", addrExpr, valExpr)}, nil
		}
		return []string{fmt.Sprintf("    MEM_U32(%s) = (uint32_t)(int32_t)round(%s);", addrExpr, valExpr)}, nil
	}
	return nil, fmt.Errorf("unsupported FIST operand")
}

func (l *Lifter) liftFxch(arg x86asm.Arg) ([]string, error) {
	idx := 1
	if reg, ok := arg.(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
		idx = int(reg - x86asm.F0)
	}
	return []string{
		fmt.Sprintf("    { double tmp = FPU_ST(0); FPU_ST(0) = FPU_ST(%d); FPU_ST(%d) = tmp; }", idx, idx),
	}, nil
}

func (l *Lifter) liftFadd(op x86asm.Op, args x86asm.Args, defMemSz int, nextPC uint64) ([]string, error) {
	var lines []string
	if reg, ok := args[0].(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
		dstIdx := int(reg - x86asm.F0)
		lines = append(lines, fmt.Sprintf("    FPU_ST(%d) += FPU_ST(0);", dstIdx))
	} else if mem, ok := args[0].(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		if defMemSz == 8 {
			lines = append(lines, fmt.Sprintf("    { double d; uint64_t u = MEM_U64(%s); memcpy(&d, &u, 8); FPU_ST(0) += d; }", addrExpr))
		} else {
			lines = append(lines, fmt.Sprintf("    { float f; uint32_t u = MEM_U32(%s); memcpy(&f, &u, 4); FPU_ST(0) += (double)f; }", addrExpr))
		}
	} else {
		lines = append(lines, "    FPU_ST(1) += FPU_ST(0);")
	}
	if op == x86asm.FADDP {
		lines = append(lines, "    fpu_pop(ctx);")
	}
	return lines, nil
}

func (l *Lifter) liftFiadd(arg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	if mem, ok := arg.(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		if defMemSz == 2 {
			return []string{fmt.Sprintf("    FPU_ST(0) += (double)(int16_t)MEM_U16(%s);", addrExpr)}, nil
		}
		return []string{fmt.Sprintf("    FPU_ST(0) += (double)(int32_t)MEM_U32(%s);", addrExpr)}, nil
	}
	return nil, fmt.Errorf("unsupported FIADD operand")
}

func (l *Lifter) liftFsub(op x86asm.Op, args x86asm.Args, defMemSz int, nextPC uint64) ([]string, error) {
	var lines []string
	if reg0, ok0 := args[0].(x86asm.Reg); ok0 && reg0 >= x86asm.F0 && reg0 <= x86asm.F7 {
		dstIdx := int(reg0 - x86asm.F0)
		srcIdx := 0
		if reg1, ok1 := args[1].(x86asm.Reg); ok1 && reg1 >= x86asm.F0 && reg1 <= x86asm.F7 {
			srcIdx = int(reg1 - x86asm.F0)
		}
		lines = append(lines, fmt.Sprintf("    FPU_ST(%d) -= FPU_ST(%d);", dstIdx, srcIdx))
	} else if mem, ok := args[0].(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		if defMemSz == 8 {
			lines = append(lines, fmt.Sprintf("    { double d; uint64_t u = MEM_U64(%s); memcpy(&d, &u, 8); FPU_ST(0) -= d; }", addrExpr))
		} else {
			lines = append(lines, fmt.Sprintf("    { float f; uint32_t u = MEM_U32(%s); memcpy(&f, &u, 4); FPU_ST(0) -= (double)f; }", addrExpr))
		}
	} else {
		lines = append(lines, "    FPU_ST(1) -= FPU_ST(0);")
	}
	if op == x86asm.FSUBP {
		lines = append(lines, "    fpu_pop(ctx);")
	}
	return lines, nil
}

func (l *Lifter) liftFisub(arg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	if mem, ok := arg.(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		if defMemSz == 2 {
			return []string{fmt.Sprintf("    FPU_ST(0) -= (double)(int16_t)MEM_U16(%s);", addrExpr)}, nil
		}
		return []string{fmt.Sprintf("    FPU_ST(0) -= (double)(int32_t)MEM_U32(%s);", addrExpr)}, nil
	}
	return nil, fmt.Errorf("unsupported FISUB operand")
}

func (l *Lifter) liftFmul(op x86asm.Op, args x86asm.Args, defMemSz int, nextPC uint64) ([]string, error) {
	var lines []string
	if reg, ok := args[0].(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
		dstIdx := int(reg - x86asm.F0)
		lines = append(lines, fmt.Sprintf("    FPU_ST(%d) *= FPU_ST(0);", dstIdx))
	} else if mem, ok := args[0].(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		if defMemSz == 8 {
			lines = append(lines, fmt.Sprintf("    { double d; uint64_t u = MEM_U64(%s); memcpy(&d, &u, 8); FPU_ST(0) *= d; }", addrExpr))
		} else {
			lines = append(lines, fmt.Sprintf("    { float f; uint32_t u = MEM_U32(%s); memcpy(&f, &u, 4); FPU_ST(0) *= (double)f; }", addrExpr))
		}
	} else {
		lines = append(lines, "    FPU_ST(1) *= FPU_ST(0);")
	}
	if op == x86asm.FMULP {
		lines = append(lines, "    fpu_pop(ctx);")
	}
	return lines, nil
}

func (l *Lifter) liftFimul(arg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	if mem, ok := arg.(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		if defMemSz == 2 {
			return []string{fmt.Sprintf("    FPU_ST(0) *= (double)(int16_t)MEM_U16(%s);", addrExpr)}, nil
		}
		return []string{fmt.Sprintf("    FPU_ST(0) *= (double)(int32_t)MEM_U32(%s);", addrExpr)}, nil
	}
	return nil, fmt.Errorf("unsupported FIMUL operand")
}

func (l *Lifter) liftFdiv(arg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	if mem, ok := arg.(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		if defMemSz == 8 {
			return []string{
				fmt.Sprintf("    { double d; uint64_t u = MEM_U64(%s); memcpy(&d, &u, 8); FPU_ST(0) /= d; }", addrExpr),
			}, nil
		}
		return []string{
			fmt.Sprintf("    { float f; uint32_t u = MEM_U32(%s); memcpy(&f, &u, 4); FPU_ST(0) /= (double)f; }", addrExpr),
		}, nil
	}
	return []string{"    FPU_ST(1) /= FPU_ST(0); fpu_pop(ctx);"}, nil
}

func (l *Lifter) liftFucomi(op x86asm.Op, args x86asm.Args) ([]string, error) {
	idx := 1
	if reg, ok := args[1].(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
		idx = int(reg - x86asm.F0)
	}
	lines := []string{
		fmt.Sprintf("    { double a = FPU_ST(0); double b = FPU_ST(%d);", idx),
		"      if (isnan(a) || isnan(b)) { ctx->zf = 1; ctx->pf = 1; ctx->cf = 1; }",
		"      else if (a > b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 0; }",
		"      else if (a < b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 1; }",
		"      else { ctx->zf = 1; ctx->pf = 0; ctx->cf = 0; }",
		"      ctx->of = 0; ctx->sf = 0; ctx->af = 0; }",
	}
	if op == x86asm.FUCOMIP {
		lines = append(lines, "    fpu_pop(ctx);")
	}
	return lines, nil
}

func (l *Lifter) liftFldcw(arg x86asm.Arg, nextPC uint64) ([]string, error) {
	if mem, ok := arg.(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{fmt.Sprintf("    ctx->fpu_cw = MEM_U16(%s);", addrExpr)}, nil
	}
	return nil, fmt.Errorf("unsupported FLDCW operand")
}

func (l *Lifter) liftFnstcw(arg x86asm.Arg, nextPC uint64) ([]string, error) {
	if mem, ok := arg.(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{fmt.Sprintf("    MEM_U16(%s) = ctx->fpu_cw;", addrExpr)}, nil
	}
	return nil, fmt.Errorf("unsupported FNSTCW operand")
}

func (l *Lifter) liftFisttp(arg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	if mem, ok := arg.(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		valExpr := "fpu_pop(ctx)"
		if defMemSz == 8 {
			return []string{fmt.Sprintf("    MEM_U64(%s) = (uint64_t)(int64_t)trunc(%s);", addrExpr, valExpr)}, nil
		}
		if defMemSz == 2 {
			return []string{fmt.Sprintf("    MEM_U16(%s) = (uint16_t)(int16_t)trunc(%s);", addrExpr, valExpr)}, nil
		}
		return []string{fmt.Sprintf("    MEM_U32(%s) = (uint32_t)(int32_t)trunc(%s);", addrExpr, valExpr)}, nil
	}
	return nil, fmt.Errorf("unsupported FISTTP operand")
}

func (l *Lifter) liftFcom(op x86asm.Op, arg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	lines := []string{"    {"}
	rhs := "FPU_ST(1)"
	if reg, ok := arg.(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
		rhs = fmt.Sprintf("FPU_ST(%d)", int(reg-x86asm.F0))
	} else if mem, ok := arg.(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		if defMemSz == 8 {
			lines = append(lines, fmt.Sprintf("      double d; uint64_t u = MEM_U64(%s); memcpy(&d, &u, 8);", addrExpr))
		} else {
			lines = append(lines, fmt.Sprintf("      float f; uint32_t u = MEM_U32(%s); memcpy(&f, &u, 4); double d = (double)f;", addrExpr))
		}
		rhs = "d"
	}
	lines = append(lines,
		fmt.Sprintf("      double a = FPU_ST(0); double b = %s;", rhs),
		"      uint16_t sw = ctx->fpu_sw & ~0x4500;",
		"      if (isnan(a) || isnan(b)) { sw |= 0x4500; }",
		"      else if (a < b) { sw |= 0x0100; }",
		"      else if (a == b) { sw |= 0x4000; }",
		"      ctx->fpu_sw = sw;",
	)
	if op == x86asm.FCOMP {
		lines = append(lines, "      fpu_pop(ctx);")
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftFicom(op x86asm.Op, arg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	mem, ok := arg.(x86asm.Mem)
	if !ok {
		return nil, fmt.Errorf("ficom requires memory operand")
	}
	addrExpr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	lines := []string{"    {"}
	if defMemSz == 2 {
		lines = append(lines, fmt.Sprintf("      double b = (double)(int16_t)MEM_U16(%s);", addrExpr))
	} else {
		lines = append(lines, fmt.Sprintf("      double b = (double)(int32_t)MEM_U32(%s);", addrExpr))
	}
	lines = append(lines,
		"      double a = FPU_ST(0);",
		"      uint16_t sw = ctx->fpu_sw & ~0x4500;",
		"      if (isnan(a) || isnan(b)) { sw |= 0x4500; }",
		"      else if (a < b) { sw |= 0x0100; }",
		"      else if (a == b) { sw |= 0x4000; }",
		"      ctx->fpu_sw = sw;",
	)
	if op == x86asm.FICOMP {
		lines = append(lines, "      fpu_pop(ctx);")
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftFcmov(op x86asm.Op, arg x86asm.Arg) ([]string, error) {
	reg, ok := arg.(x86asm.Reg)
	if !ok || reg < x86asm.F0 || reg > x86asm.F7 {
		return nil, fmt.Errorf("fcmov requires FPU register")
	}
	idx := int(reg - x86asm.F0)
	cond := "0"
	switch op {
	case x86asm.FCMOVB:
		cond = "ctx->cf"
	case x86asm.FCMOVE:
		cond = "ctx->zf"
	case x86asm.FCMOVNB:
		cond = "!ctx->cf"
	case x86asm.FCMOVNE:
		cond = "!ctx->zf"
	case x86asm.FCMOVBE:
		cond = "ctx->cf || ctx->zf"
	case x86asm.FCMOVNBE:
		cond = "!ctx->cf && !ctx->zf"
	case x86asm.FCMOVU:
		cond = "ctx->pf"
	case x86asm.FCMOVNU:
		cond = "!ctx->pf"
	}
	return []string{
		fmt.Sprintf("    if (%s) { FPU_ST(0) = FPU_ST(%d); }", cond, idx),
	}, nil
}

func (l *Lifter) liftFidivr(arg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	mem, ok := arg.(x86asm.Mem)
	if !ok {
		return nil, fmt.Errorf("fidivr requires memory operand")
	}
	addrExpr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	if defMemSz == 2 {
		return []string{fmt.Sprintf("    FPU_ST(0) = ((double)(int16_t)MEM_U16(%s)) / FPU_ST(0);", addrExpr)}, nil
	}
	return []string{fmt.Sprintf("    FPU_ST(0) = ((double)(int32_t)MEM_U32(%s)) / FPU_ST(0);", addrExpr)}, nil
}

func (l *Lifter) liftFdivrp() []string {
	return []string{"    FPU_ST(1) = FPU_ST(0) / FPU_ST(1); fpu_pop(ctx);"}
}

func (l *Lifter) liftFsubr(args x86asm.Args, defMemSz int, nextPC uint64) ([]string, error) {
	if reg0, ok0 := args[0].(x86asm.Reg); ok0 && reg0 >= x86asm.F0 && reg0 <= x86asm.F7 {
		dstIdx := int(reg0 - x86asm.F0)
		srcIdx := 0
		if reg1, ok1 := args[1].(x86asm.Reg); ok1 && reg1 >= x86asm.F0 && reg1 <= x86asm.F7 {
			srcIdx = int(reg1 - x86asm.F0)
		}
		return []string{fmt.Sprintf("    FPU_ST(%d) = FPU_ST(%d) - FPU_ST(%d);", dstIdx, srcIdx, dstIdx)}, nil
	}
	if mem, ok := args[0].(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		if defMemSz == 8 {
			return []string{
				fmt.Sprintf("    { double d; uint64_t u = MEM_U64(%s); memcpy(&d, &u, 8); FPU_ST(0) = d - FPU_ST(0); }", addrExpr),
			}, nil
		}
		return []string{
			fmt.Sprintf("    { float f; uint32_t u = MEM_U32(%s); memcpy(&f, &u, 4); FPU_ST(0) = (double)f - FPU_ST(0); }", addrExpr),
		}, nil
	}
	return []string{"    FPU_ST(0) = FPU_ST(1) - FPU_ST(0);"}, nil
}

func (l *Lifter) liftFldl2t() []string {
	return []string{"    fpu_push(ctx, 3.32192809488736234787);"}
}

func (l *Lifter) liftFbstp(arg x86asm.Arg, nextPC uint64) ([]string, error) {
	mem, ok := arg.(x86asm.Mem)
	if !ok {
		return nil, fmt.Errorf("fbstp requires memory operand")
	}
	addrExpr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	return []string{
		"    {",
		"      int64_t val = (int64_t)round(fpu_pop(ctx));",
		"      uint8_t sign = (val < 0) ? 0x80 : 0x00;",
		"      uint64_t pos = (val < 0) ? (uint64_t)(-val) : (uint64_t)val;",
		"      uint8_t bcd[10] = {0};",
		"      for (int i = 0; i < 9; i++) { uint8_t d0 = pos % 10; pos /= 10; uint8_t d1 = pos % 10; pos /= 10; bcd[i] = d0 | (d1 << 4); }",
		"      bcd[9] = sign;",
		fmt.Sprintf("      memcpy(ctx->mem_base + (%s), bcd, 10);", addrExpr),
		"    }",
	}, nil
}

func (l *Lifter) liftFnstenv(arg x86asm.Arg, nextPC uint64) ([]string, error) {
	mem, ok := arg.(x86asm.Mem)
	if !ok {
		return nil, fmt.Errorf("fnstenv requires memory operand")
	}
	addrExpr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	return []string{
		"    {",
		fmt.Sprintf("      uint8_t *ptr = ctx->mem_base + (%s);", addrExpr),
		"      *(uint16_t*)(ptr) = ctx->fpu_cw;",
		"      *(uint16_t*)(ptr + 4) = (ctx->fpu_sw & ~0x3800) | ((ctx->fpu_top & 7) << 11);",
		"      *(uint16_t*)(ptr + 8) = 0xFFFF;",
		"    }",
	}, nil
}

func (l *Lifter) liftFnstsw(arg x86asm.Arg, nextPC uint64) ([]string, error) {
	swExpr := "((ctx->fpu_sw & ~0x3800) | ((ctx->fpu_top & 7) << 11))"
	if reg, ok := arg.(x86asm.Reg); ok && (reg == x86asm.AX || reg == x86asm.EAX || reg == x86asm.RAX) {
		return []string{
			fmt.Sprintf("    ctx->rax = (ctx->rax & ~0xFFFFULL) | (uint16_t)%s;", swExpr),
		}, nil
	}
	if mem, ok := arg.(x86asm.Mem); ok {
		addrExpr, err := MemAddrExpr(mem, nextPC)
		if err != nil {
			return nil, err
		}
		return []string{
			fmt.Sprintf("    MEM_U16(%s) = (uint16_t)%s;", addrExpr, swExpr),
		}, nil
	}
	return nil, fmt.Errorf("unsupported FNSTSW operand")
}

func (l *Lifter) liftFyl2xp1() []string {
	return []string{"    FPU_ST(1) *= log2(FPU_ST(0) + 1.0); fpu_pop(ctx);"}
}

func (l *Lifter) liftFldenv(arg x86asm.Arg, nextPC uint64) ([]string, error) {
	mem, ok := arg.(x86asm.Mem)
	if !ok {
		return nil, fmt.Errorf("fldenv requires memory operand")
	}
	addrExpr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	return []string{
		"    {",
		fmt.Sprintf("      const uint8_t *ptr = ctx->mem_base + (%s);", addrExpr),
		"      ctx->fpu_cw = *(const uint16_t*)(ptr);",
		"      ctx->fpu_sw = *(const uint16_t*)(ptr + 4);",
		"      ctx->fpu_top = (ctx->fpu_sw >> 11) & 7;",
		"    }",
	}, nil
}

func (l *Lifter) liftFnclex() []string {
	return []string{"    ctx->fpu_sw &= ~0x7F;"}
}

func (l *Lifter) liftFfreep() []string {
	return []string{"    fpu_pop(ctx);"}
}

func (l *Lifter) liftFsqrt() []string {
	return []string{"    FPU_ST(0) = sqrt(FPU_ST(0));"}
}

func (l *Lifter) liftFrndint() []string {
	return []string{"    FPU_ST(0) = round(FPU_ST(0));"}
}

func (l *Lifter) liftFxtract() []string {
	return []string{
		"    {",
		"      double v = FPU_ST(0);",
		"      int exp = 0;",
		"      double sig = frexp(v, &exp);",
		"      FPU_ST(0) = (double)exp;",
		"      fpu_push(ctx, sig * 2.0);",
		"    }",
	}
}

func (l *Lifter) liftFscale() []string {
	return []string{"    FPU_ST(0) = ldexp(FPU_ST(0), (int)trunc(FPU_ST(1)));"}
}

func (l *Lifter) liftFprem1() []string {
	return []string{"    FPU_ST(0) = remainder(FPU_ST(0), FPU_ST(1));"}
}

func (l *Lifter) liftFucom(popCount int, args []x86asm.Arg) []string {
	srcIdx := 1
	if len(args) > 0 {
		if reg, ok := args[0].(x86asm.Reg); ok && reg >= x86asm.F0 && reg <= x86asm.F7 {
			srcIdx = int(reg - x86asm.F0)
		}
	}
	lines := []string{
		"    {",
		fmt.Sprintf("      double a = FPU_ST(0); double b = FPU_ST(%d);", srcIdx),
		"      ctx->fpu_sw &= ~0x4500; // clear C0, C2, C3",
		"      if (isnan(a) || isnan(b)) { ctx->fpu_sw |= 0x4500; }",
		"      else if (a < b) { ctx->fpu_sw |= 0x0100; } // C0 = 1",
		"      else if (a == b) { ctx->fpu_sw |= 0x4000; } // C3 = 1",
		"    }",
	}
	for i := 0; i < popCount; i++ {
		lines = append(lines, "    fpu_pop(ctx);")
	}
	return lines
}

func (l *Lifter) liftFisubr(arg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	mem, ok := arg.(x86asm.Mem)
	if !ok {
		return nil, fmt.Errorf("fisubr requires memory operand")
	}
	addrExpr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	if defMemSz == 2 {
		return []string{fmt.Sprintf("    FPU_ST(0) = (double)(int16_t)MEM_U16(%s) - FPU_ST(0);", addrExpr)}, nil
	}
	return []string{fmt.Sprintf("    FPU_ST(0) = (double)(int32_t)MEM_U32(%s) - FPU_ST(0);", addrExpr)}, nil
}

func (l *Lifter) liftFrstor(arg x86asm.Arg, nextPC uint64) ([]string, error) {
	mem, ok := arg.(x86asm.Mem)
	if !ok {
		return nil, fmt.Errorf("frstor requires memory operand")
	}
	addrExpr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	return []string{
		"    {",
		fmt.Sprintf("      const uint8_t *ptr = ctx->mem_base + (%s);", addrExpr),
		"      ctx->fpu_cw = *(const uint16_t*)(ptr);",
		"      ctx->fpu_sw = *(const uint16_t*)(ptr + 4);",
		"      ctx->fpu_top = (ctx->fpu_sw >> 11) & 7;",
		"    }",
	}, nil
}

func (l *Lifter) liftFxsave(arg x86asm.Arg, nextPC uint64) ([]string, error) {
	mem, ok := arg.(x86asm.Mem)
	if !ok {
		return nil, fmt.Errorf("fxsave requires memory operand")
	}
	addrExpr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	return []string{
		"    {",
		fmt.Sprintf("      uint8_t *ptr = ctx->mem_base + (%s);", addrExpr),
		"      memset(ptr, 0, 512);",
		"      *(uint16_t*)(ptr) = ctx->fpu_cw;",
		"      *(uint16_t*)(ptr + 2) = (ctx->fpu_sw & ~0x3800) | ((ctx->fpu_top & 7) << 11);",
		"      *(uint16_t*)(ptr + 4) = 0xFFFF;",
		"      *(uint32_t*)(ptr + 24) = ctx->mxcsr ? ctx->mxcsr : 0x1F80;",
		"      *(uint32_t*)(ptr + 28) = 0xFFFF; // mxcsr_mask",
		"      memcpy(ptr + 160, ctx->xmm, 256); // 16 xmm registers",
		"    }",
	}, nil
}

func (l *Lifter) liftFxrstor(arg x86asm.Arg, nextPC uint64) ([]string, error) {
	mem, ok := arg.(x86asm.Mem)
	if !ok {
		return nil, fmt.Errorf("fxrstor requires memory operand")
	}
	addrExpr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	return []string{
		"    {",
		fmt.Sprintf("      const uint8_t *ptr = ctx->mem_base + (%s);", addrExpr),
		"      ctx->fpu_cw = *(const uint16_t*)(ptr);",
		"      ctx->fpu_sw = *(const uint16_t*)(ptr + 2);",
		"      ctx->fpu_top = (ctx->fpu_sw >> 11) & 7;",
		"      ctx->mxcsr = *(const uint32_t*)(ptr + 24);",
		"      memcpy(ctx->xmm, ptr + 160, 256);",
		"    }",
	}, nil
}
