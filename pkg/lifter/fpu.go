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
