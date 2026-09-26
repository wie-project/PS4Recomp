package lifter

import (
	"fmt"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

func (l *Lifter) liftMov(dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	} else if reg, ok := src.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}

	srcExpr, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}
	return l.getOperandWrite(dst, sz, srcExpr, nextPC)
}

func (l *Lifter) liftExtend(op x86asm.Op, dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok {
		return nil, fmt.Errorf("extend destination must be register")
	}
	srcExpr, srcSz, err := l.getOperandRead(src, defMemSz, nextPC)
	if err != nil {
		return nil, err
	}

	var castExpr string
	if op == x86asm.MOVZX {
		switch srcSz {
		case 1:
			castExpr = fmt.Sprintf("(uint64_t)(uint8_t)(%s)", srcExpr)
		case 2:
			castExpr = fmt.Sprintf("(uint64_t)(uint16_t)(%s)", srcExpr)
		case 4:
			castExpr = fmt.Sprintf("(uint64_t)(uint32_t)(%s)", srcExpr)
		default:
			castExpr = fmt.Sprintf("(uint64_t)(%s)", srcExpr)
		}
	} else {
		switch srcSz {
		case 1:
			castExpr = fmt.Sprintf("(uint64_t)(int64_t)(int8_t)(%s)", srcExpr)
		case 2:
			castExpr = fmt.Sprintf("(uint64_t)(int64_t)(int16_t)(%s)", srcExpr)
		case 4:
			castExpr = fmt.Sprintf("(uint64_t)(int64_t)(int32_t)(%s)", srcExpr)
		default:
			castExpr = fmt.Sprintf("(uint64_t)(int64_t)(%s)", srcExpr)
		}
	}

	stmt, err := GetRegWriteStmt(dstReg, castExpr)
	if err != nil {
		return nil, err
	}
	return []string{"    " + stmt}, nil
}

func (l *Lifter) liftAlu(inst disasm.Instruction, dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	op := inst.Inst.Op
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	} else if reg, ok := src.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}

	dstRead, _, err := l.getOperandRead(dst, sz, nextPC)
	if err != nil {
		return nil, err
	}
	srcRead, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}

	cType := uintType(sz)
	var lines []string

	lines = append(
		lines,
		fmt.Sprintf("    { %s a = (%s)(%s); %s b = (%s)(%s); %s res;", cType, cType, dstRead, cType, cType, srcRead, cType),
	)

	switch op {
	case x86asm.ADD:
		lines = append(lines, "      res = a + b;")
		if !inst.SkipFlags {
			lines = append(lines, fmt.Sprintf("      set_flags_add_u%d(ctx, a, b, res);", sz*8))
		}
		writeStmts, err := l.getOperandWrite(dst, sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, ws := range writeStmts {
			lines = append(lines, "    "+ws)
		}

	case x86asm.ADC:
		lines = append(
			lines,
			fmt.Sprintf("      res = a + b + (%s)ctx->cf;", cType),
			fmt.Sprintf("      set_flags_add_u%d(ctx, a, b, res);", sz*8),
		)
		writeStmts, err := l.getOperandWrite(dst, sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, ws := range writeStmts {
			lines = append(lines, "    "+ws)
		}

	case x86asm.SUB:
		lines = append(lines, "      res = a - b;")
		if !inst.SkipFlags {
			lines = append(lines, fmt.Sprintf("      set_flags_sub_u%d(ctx, a, b, res);", sz*8))
		}
		writeStmts, err := l.getOperandWrite(dst, sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, ws := range writeStmts {
			lines = append(lines, "    "+ws)
		}

	case x86asm.CMP:
		lines = append(
			lines,
			"      res = a - b;",
			fmt.Sprintf("      set_flags_sub_u%d(ctx, a, b, res);", sz*8),
		)

	case x86asm.TEST:
		lines = append(
			lines,
			"      res = a & b;",
			fmt.Sprintf("      set_flags_logic_u%d(ctx, res);", sz*8),
		)

	case x86asm.AND:
		lines = append(lines, "      res = a & b;")
		if !inst.SkipFlags {
			lines = append(lines, fmt.Sprintf("      set_flags_logic_u%d(ctx, res);", sz*8))
		}
		writeStmts, err := l.getOperandWrite(dst, sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, ws := range writeStmts {
			lines = append(lines, "    "+ws)
		}

	case x86asm.OR:
		lines = append(lines, "      res = a | b;")
		if !inst.SkipFlags {
			lines = append(lines, fmt.Sprintf("      set_flags_logic_u%d(ctx, res);", sz*8))
		}
		writeStmts, err := l.getOperandWrite(dst, sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, ws := range writeStmts {
			lines = append(lines, "    "+ws)
		}

	case x86asm.XOR:
		lines = append(lines, "      res = a ^ b;")
		if !inst.SkipFlags {
			lines = append(lines, fmt.Sprintf("      set_flags_logic_u%d(ctx, res);", sz*8))
		}
		writeStmts, err := l.getOperandWrite(dst, sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, ws := range writeStmts {
			lines = append(lines, "    "+ws)
		}
	}

	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftUnary(inst disasm.Instruction, dst x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	op := inst.Inst.Op
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}

	dstRead, _, err := l.getOperandRead(dst, sz, nextPC)
	if err != nil {
		return nil, err
	}
	cType := uintType(sz)
	var lines []string
	lines = append(
		lines,
		fmt.Sprintf("    { %s a = (%s)(%s); %s res;", cType, cType, dstRead, cType),
	)

	switch op {
	case x86asm.INC:
		lines = append(lines, "      res = a + 1;")
		if !inst.SkipFlags {
			lines = append(lines, fmt.Sprintf("      set_flags_inc_u%d(ctx, a, res);", sz*8))
		}
	case x86asm.DEC:
		lines = append(lines, "      res = a - 1;")
		if !inst.SkipFlags {
			lines = append(lines, fmt.Sprintf("      set_flags_dec_u%d(ctx, a, res);", sz*8))
		}
	case x86asm.NEG:
		lines = append(lines, "      res = -a;")
		if !inst.SkipFlags {
			lines = append(lines, fmt.Sprintf("      set_flags_sub_u%d(ctx, 0, a, res);", sz*8))
		}
	case x86asm.NOT:
		lines = append(
			lines,
			"      res = ~a;",
		)
	}

	writeStmts, err := l.getOperandWrite(dst, sz, "res", nextPC)
	if err != nil {
		return nil, err
	}
	for _, ws := range writeStmts {
		lines = append(lines, "    "+ws)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftShift(inst disasm.Instruction, dst, countArg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	op := inst.Inst.Op
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}

	dstRead, _, err := l.getOperandRead(dst, sz, nextPC)
	if err != nil {
		return nil, err
	}
	countRead, _, err := l.getOperandRead(countArg, 1, nextPC)
	if err != nil {
		return nil, err
	}
	cType := uintType(sz)
	var lines []string
	lines = append(
		lines,
		fmt.Sprintf("    { %s a = (%s)(%s); uint8_t count = ((uint8_t)(%s)) & 0x3f; %s res;", cType, cType, dstRead, countRead, cType),
	)

	switch op {
	case x86asm.SHL:
		lines = append(lines, "      res = a << count;")
		if !inst.SkipFlags {
			lines = append(lines, fmt.Sprintf("      set_flags_logic_u%d(ctx, res);", sz*8))
		}
	case x86asm.SHR:
		lines = append(lines, "      res = a >> count;")
		if !inst.SkipFlags {
			lines = append(lines, fmt.Sprintf("      set_flags_logic_u%d(ctx, res);", sz*8))
		}
	case x86asm.SAR:
		signedType := intType(sz)
		lines = append(
			lines,
			fmt.Sprintf("      res = (%s)(((%s)a) >> count);", cType, signedType),
		)
		if !inst.SkipFlags {
			lines = append(lines, fmt.Sprintf("      set_flags_logic_u%d(ctx, res);", sz*8))
		}
	}

	writeStmts, err := l.getOperandWrite(dst, sz, "res", nextPC)
	if err != nil {
		return nil, err
	}
	for _, ws := range writeStmts {
		lines = append(lines, "    "+ws)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftRotate(op x86asm.Op, dst, countArg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	dstRead, _, err := l.getOperandRead(dst, sz, nextPC)
	if err != nil {
		return nil, err
	}
	countRead, _, err := l.getOperandRead(countArg, 1, nextPC)
	if err != nil {
		return nil, err
	}
	bits := sz * 8
	cType := uintType(sz)
	var lines []string
	lines = append(
		lines,
		fmt.Sprintf("    { %s a = (%s)(%s); uint8_t count = ((uint8_t)(%s)) & %d; %s res;", cType, cType, dstRead, countRead, bits-1, cType),
	)
	if op == x86asm.ROL {
		lines = append(
			lines,
			fmt.Sprintf("      res = (a << count) | (a >> ((%d - count) & %d));", bits, bits-1),
			"      ctx->cf = res & 1;",
		)
	} else {
		lines = append(
			lines,
			fmt.Sprintf("      res = (a >> count) | (a << ((%d - count) & %d));", bits, bits-1),
			fmt.Sprintf("      ctx->cf = (res >> %d) & 1;", bits-1),
		)
	}
	writeStmts, err := l.getOperandWrite(dst, sz, "res", nextPC)
	if err != nil {
		return nil, err
	}
	for _, ws := range writeStmts {
		lines = append(lines, "    "+ws)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftXchg(op1, op2 x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	v1, sz1, err := l.getOperandRead(op1, defMemSz, nextPC)
	if err != nil {
		return nil, err
	}
	v2, sz2, err := l.getOperandRead(op2, defMemSz, nextPC)
	if err != nil {
		return nil, err
	}
	sz := max(sz2, sz1)
	cType := uintType(sz)
	var lines []string
	lines = append(
		lines,
		fmt.Sprintf("    { %s tmp1 = (%s)(%s); %s tmp2 = (%s)(%s);", cType, cType, v1, cType, cType, v2),
	)
	w1, err := l.getOperandWrite(op1, sz, "tmp2", nextPC)
	if err != nil {
		return nil, err
	}
	w2, err := l.getOperandWrite(op2, sz, "tmp1", nextPC)
	if err != nil {
		return nil, err
	}
	for _, s := range w1 {
		lines = append(lines, "    "+s)
	}
	for _, s := range w2 {
		lines = append(lines, "    "+s)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftXadd(dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	} else if reg, ok := src.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	cType := uintType(sz)
	sRead, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}

	var lines []string
	if dstMem, ok := dst.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(dstMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(
			lines,
			fmt.Sprintf("    { %s orig_src = (%s)(%s);", cType, cType, sRead),
			fmt.Sprintf("      %s orig_dst = __sync_fetch_and_add((%s *)(ctx->mem_base + (%s)), orig_src);", cType, cType, addr),
			fmt.Sprintf("      %s res = orig_dst + orig_src;", cType),
			fmt.Sprintf("      set_flags_add_u%d(ctx, orig_dst, orig_src, res);", sz*8),
		)
		wSrc, err := l.getOperandWrite(src, sz, "orig_dst", nextPC)
		if err != nil {
			return nil, err
		}
		for _, s := range wSrc {
			lines = append(lines, "    "+s)
		}
		lines = append(lines, "    }")
		return lines, nil
	}

	dRead, _, err := l.getOperandRead(dst, sz, nextPC)
	if err != nil {
		return nil, err
	}
	lines = append(
		lines,
		fmt.Sprintf("    { %s orig_dst = (%s)(%s); %s orig_src = (%s)(%s); %s res = orig_dst + orig_src;", cType, cType, dRead, cType, cType, sRead, cType),
		fmt.Sprintf("      set_flags_add_u%d(ctx, orig_dst, orig_src, res);", sz*8),
	)
	wDst, err := l.getOperandWrite(dst, sz, "res", nextPC)
	if err != nil {
		return nil, err
	}
	wSrc, err := l.getOperandWrite(src, sz, "orig_dst", nextPC)
	if err != nil {
		return nil, err
	}
	for _, s := range wDst {
		lines = append(lines, "    "+s)
	}
	for _, s := range wSrc {
		lines = append(lines, "    "+s)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftCmpxchg(dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	} else if reg, ok := src.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	cType := uintType(sz)
	sRead, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}
	var accReg x86asm.Reg
	switch sz {
	case 1:
		accReg = x86asm.AL
	case 2:
		accReg = x86asm.AX
	case 4:
		accReg = x86asm.EAX
	default:
		accReg = x86asm.RAX
	}
	accRead, _, _ := GetRegReadExpr(accReg)

	var lines []string
	if dstMem, ok := dst.(x86asm.Mem); ok {
		addr, err := MemAddrExpr(dstMem, nextPC)
		if err != nil {
			return nil, err
		}
		lines = append(
			lines,
			fmt.Sprintf("    { %s src = (%s)(%s); %s acc = (%s)(%s);", cType, cType, sRead, cType, cType, accRead),
			fmt.Sprintf("      %s prev = __sync_val_compare_and_swap((%s *)(ctx->mem_base + (%s)), acc, src);", cType, cType, addr),
			fmt.Sprintf("      set_flags_sub_u%d(ctx, acc, prev, acc - prev);", sz*8),
			"      if (acc != prev) {",
		)
		wAcc, err := GetRegWriteStmt(accReg, "prev")
		if err != nil {
			return nil, err
		}
		lines = append(lines, "      "+wAcc, "      }", "    }")
		return lines, nil
	}

	dRead, _, err := l.getOperandRead(dst, sz, nextPC)
	if err != nil {
		return nil, err
	}
	lines = append(
		lines,
		fmt.Sprintf("    { %s dest = (%s)(%s); %s src = (%s)(%s); %s acc = (%s)(%s);", cType, cType, dRead, cType, cType, sRead, cType, cType, accRead),
		fmt.Sprintf("      set_flags_sub_u%d(ctx, acc, dest, acc - dest);", sz*8),
		"      if (acc == dest) {",
	)
	wDst, err := l.getOperandWrite(dst, sz, "src", nextPC)
	if err != nil {
		return nil, err
	}
	for _, s := range wDst {
		lines = append(lines, "      "+s)
	}
	lines = append(lines, "      } else {")
	wAcc, err := GetRegWriteStmt(accReg, "dest")
	if err != nil {
		return nil, err
	}
	lines = append(lines, "      "+wAcc, "      }", "    }")
	return lines, nil
}

func (l *Lifter) liftImul(args x86asm.Args, defMemSz int, nextPC uint64) ([]string, error) {
	if args[1] == nil {
		// 1-arg: IMUL src -> RDX:RAX = RAX * src
		sz := defMemSz
		if reg, ok := args[0].(x86asm.Reg); ok {
			if info, ok := regMap[reg]; ok && info.Size > 0 {
				sz = info.Size
			}
		}
		sRead, _, err := l.getOperandRead(args[0], sz, nextPC)
		if err != nil {
			return nil, err
		}
		if sz == 8 {
			return []string{
				fmt.Sprintf("    { __int128 res = (__int128)(int64_t)ctx->rax * (int64_t)(%s);", sRead),
				"      ctx->rax = (uint64_t)res;",
				"      ctx->rdx = (uint64_t)(res >> 64);",
				"      ctx->cf = ctx->of = (res != (int64_t)res);",
				"    }",
			}, nil
		}
		if sz == 4 {
			return []string{
				fmt.Sprintf("    { int64_t res = (int64_t)(int32_t)ctx->rax * (int32_t)(%s);", sRead),
				"      ctx->rax = (uint64_t)(uint32_t)res;",
				"      ctx->rdx = (uint64_t)(uint32_t)(res >> 32);",
				"      ctx->cf = ctx->of = (res != (int32_t)res);",
				"    }",
			}, nil
		}
		if sz == 2 {
			return []string{
				fmt.Sprintf("    { int32_t res = (int32_t)(int16_t)ctx->rax * (int16_t)(%s);", sRead),
				"      ctx->rax = (ctx->rax & ~0xffffULL) | ((uint64_t)(uint16_t)res);",
				"      ctx->rdx = (ctx->rdx & ~0xffffULL) | ((uint64_t)(uint16_t)(res >> 16));",
				"      ctx->cf = ctx->of = (res != (int16_t)res);",
				"    }",
			}, nil
		}
		if sz == 1 {
			return []string{
				fmt.Sprintf("    { int16_t res = (int16_t)(int8_t)ctx->rax * (int8_t)(%s);", sRead),
				"      ctx->rax = (ctx->rax & ~0xffffULL) | ((uint64_t)(uint16_t)res);",
				"      ctx->cf = ctx->of = (res != (int8_t)res);",
				"    }",
			}, nil
		}
		return nil, fmt.Errorf("unsupported 1-arg IMUL size: %d", sz)
	}

	if args[2] == nil {
		// 2-arg: IMUL dst, src -> dst = dst * src
		sz := defMemSz
		if reg, ok := args[0].(x86asm.Reg); ok {
			if info, ok := regMap[reg]; ok && info.Size > 0 {
				sz = info.Size
			}
		}
		dRead, _, err := l.getOperandRead(args[0], sz, nextPC)
		if err != nil {
			return nil, err
		}
		sRead, _, err := l.getOperandRead(args[1], sz, nextPC)
		if err != nil {
			return nil, err
		}
		signedType := intType(sz)
		var lines []string
		lines = append(
			lines,
			fmt.Sprintf("    { %s a = (%s)(%s); %s b = (%s)(%s); %s res = a * b;", signedType, signedType, dRead, signedType, signedType, sRead, signedType),
		)
		wDst, err := l.getOperandWrite(args[0], sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, s := range wDst {
			lines = append(lines, "    "+s)
		}
		lines = append(lines, "    }")
		return lines, nil
	}

	// 3-arg: IMUL dst, src, imm -> dst = src * imm
	sz := defMemSz
	if reg, ok := args[0].(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	sRead, _, err := l.getOperandRead(args[1], sz, nextPC)
	if err != nil {
		return nil, err
	}
	immRead, _, err := l.getOperandRead(args[2], sz, nextPC)
	if err != nil {
		return nil, err
	}
	signedType := intType(sz)
	var lines []string
	lines = append(
		lines,
		fmt.Sprintf("    { %s a = (%s)(%s); %s b = (%s)(%s); %s res = a * b;", signedType, signedType, sRead, signedType, signedType, immRead, signedType),
	)
	wDst, err := l.getOperandWrite(args[0], sz, "res", nextPC)
	if err != nil {
		return nil, err
	}
	for _, s := range wDst {
		lines = append(lines, "    "+s)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftMul(src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
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
	if sz == 8 {
		return []string{
			fmt.Sprintf("    { unsigned __int128 res = (unsigned __int128)ctx->rax * (uint64_t)(%s);", sRead),
			"      ctx->rax = (uint64_t)res;",
			"      ctx->rdx = (uint64_t)(res >> 64);",
			"      ctx->cf = ctx->of = (ctx->rdx != 0);",
			"    }",
		}, nil
	}
	if sz == 4 {
		return []string{
			fmt.Sprintf("    { uint64_t res = (uint64_t)(uint32_t)ctx->rax * (uint32_t)(%s);", sRead),
			"      ctx->rax = (uint64_t)(uint32_t)res;",
			"      ctx->rdx = (uint64_t)(uint32_t)(res >> 32);",
			"      ctx->cf = ctx->of = (ctx->rdx != 0);",
			"    }",
		}, nil
	}
	if sz == 2 {
		return []string{
			fmt.Sprintf("    { uint32_t res = (uint32_t)(uint16_t)ctx->rax * (uint16_t)(%s);", sRead),
			"      ctx->rax = (ctx->rax & ~0xffffULL) | ((uint64_t)(uint16_t)res);",
			"      ctx->rdx = (ctx->rdx & ~0xffffULL) | ((uint64_t)(uint16_t)(res >> 16));",
			"      ctx->cf = ctx->of = (ctx->rdx != 0);",
			"    }",
		}, nil
	}
	if sz == 1 {
		return []string{
			fmt.Sprintf("    { uint16_t res = (uint16_t)(uint8_t)ctx->rax * (uint8_t)(%s);", sRead),
			"      ctx->rax = (ctx->rax & ~0xffffULL) | ((uint64_t)res);",
			"      ctx->cf = ctx->of = ((res & 0xff00) != 0);",
			"    }",
		}, nil
	}
	return nil, fmt.Errorf("unsupported MUL size: %d", sz)
}

func (l *Lifter) liftSbb(dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	} else if reg, ok := src.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	dRead, _, err := l.getOperandRead(dst, sz, nextPC)
	if err != nil {
		return nil, err
	}
	sRead, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}
	cType := uintType(sz)
	var lines []string
	lines = append(
		lines,
		fmt.Sprintf("    { %s a = (%s)(%s); %s b = (%s)(%s) + (%s)ctx->cf; %s res = a - b;", cType, cType, dRead, cType, cType, sRead, cType, cType),
		fmt.Sprintf("      set_flags_sub_u%d(ctx, a, b, res);", sz*8),
	)
	wDst, err := l.getOperandWrite(dst, sz, "res", nextPC)
	if err != nil {
		return nil, err
	}
	for _, s := range wDst {
		lines = append(lines, "    "+s)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftBswap(dst x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	dRead, _, err := l.getOperandRead(dst, sz, nextPC)
	if err != nil {
		return nil, err
	}
	var swapExpr string
	switch sz {
	case 2:
		swapExpr = fmt.Sprintf("__builtin_bswap16((uint16_t)(%s))", dRead)
	case 4:
		swapExpr = fmt.Sprintf("__builtin_bswap32((uint32_t)(%s))", dRead)
	case 8:
		swapExpr = fmt.Sprintf("__builtin_bswap64((uint64_t)(%s))", dRead)
	default:
		return nil, fmt.Errorf("unsupported BSWAP size: %d", sz)
	}
	return l.getOperandWrite(dst, sz, swapExpr, nextPC)
}

func (l *Lifter) liftMovbe(dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	} else if reg, ok := src.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	sRead, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}
	var swapExpr string
	switch sz {
	case 2:
		swapExpr = fmt.Sprintf("__builtin_bswap16((uint16_t)(%s))", sRead)
	case 4:
		swapExpr = fmt.Sprintf("__builtin_bswap32((uint32_t)(%s))", sRead)
	case 8:
		swapExpr = fmt.Sprintf("__builtin_bswap64((uint64_t)(%s))", sRead)
	default:
		return nil, fmt.Errorf("unsupported MOVBE size: %d", sz)
	}
	writes, err := l.getOperandWrite(dst, sz, swapExpr, nextPC)
	if err != nil {
		return nil, err
	}
	lines := make([]string, 0, len(writes))
	for _, w := range writes {
		lines = append(lines, "    "+w)
	}
	return lines, nil
}

func (l *Lifter) liftBitTest(op x86asm.Op, base, bit x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := base.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	bRead, _, err := l.getOperandRead(base, sz, nextPC)
	if err != nil {
		return nil, err
	}
	bitRead, _, err := l.getOperandRead(bit, 1, nextPC)
	if err != nil {
		return nil, err
	}
	mask := sz*8 - 1
	cType := uintType(sz)

	lines := []string{
		"    {",
		fmt.Sprintf("      %s bit_idx = ((%s)(%s)) & %d;", cType, cType, bitRead, mask),
		fmt.Sprintf("      %s val = (%s)(%s);", cType, cType, bRead),
		"      ctx->cf = (val >> bit_idx) & 1;",
	}

	var resExpr string
	switch op {
	case x86asm.BT:
		// No modification
	case x86asm.BTR:
		resExpr = fmt.Sprintf("val & ~(((%s)1) << bit_idx)", cType)
	case x86asm.BTS:
		resExpr = fmt.Sprintf("val | (((%s)1) << bit_idx)", cType)
	case x86asm.BTC:
		resExpr = fmt.Sprintf("val ^ (((%s)1) << bit_idx)", cType)
	default:
		return nil, fmt.Errorf("unsupported bit test op: %v", op)
	}

	if resExpr != "" {
		wLines, err := l.getOperandWrite(base, sz, resExpr, nextPC)
		if err != nil {
			return nil, err
		}
		for _, w := range wLines {
			lines = append(lines, "    "+w)
		}
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftBsf(dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	sRead, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}
	cType := uintType(sz)
	var lines []string
	lines = append(
		lines,
		fmt.Sprintf("    { %s val = (%s)(%s);", cType, cType, sRead),
		"      ctx->zf = (val == 0);",
		"      if (val != 0) {",
	)
	var ctzExpr string
	if sz == 8 {
		ctzExpr = "(__builtin_ctzll(val))"
	} else {
		ctzExpr = "(__builtin_ctz((uint32_t)val))"
	}
	wDst, err := l.getOperandWrite(dst, sz, ctzExpr, nextPC)
	if err != nil {
		return nil, err
	}
	for _, s := range wDst {
		lines = append(lines, "        "+s)
	}
	lines = append(lines, "      }", "    }")
	return lines, nil
}

func (l *Lifter) liftBsr(dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	sRead, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}
	cType := uintType(sz)
	var lines []string
	lines = append(
		lines,
		fmt.Sprintf("    { %s val = (%s)(%s);", cType, cType, sRead),
		"      ctx->zf = (val == 0);",
		"      if (val != 0) {",
	)
	var clzExpr string
	switch sz {
	case 8:
		clzExpr = "(63 - __builtin_clzll(val))"
	case 4:
		clzExpr = "(31 - __builtin_clz((uint32_t)val))"
	default:
		clzExpr = "(15 - (__builtin_clz((uint32_t)(uint16_t)val) - 16))"
	}
	wDst, err := l.getOperandWrite(dst, sz, clzExpr, nextPC)
	if err != nil {
		return nil, err
	}
	for _, s := range wDst {
		lines = append(lines, "        "+s)
	}
	lines = append(lines, "      }", "    }")
	return lines, nil
}

func (l *Lifter) liftDiv(src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
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
	if sz == 8 {
		return []string{
			"    { unsigned __int128 dividend = ((unsigned __int128)ctx->rdx << 64) | ctx->rax;",
			fmt.Sprintf("      uint64_t divisor = (uint64_t)(%s);", sRead),
			"      if (divisor != 0) {",
			"        ctx->rax = (uint64_t)(dividend / divisor);",
			"        ctx->rdx = (uint64_t)(dividend % divisor);",
			"      }",
			"    }",
		}, nil
	}
	if sz == 4 {
		return []string{
			"    { uint64_t dividend = ((uint64_t)(uint32_t)ctx->rdx << 32) | (uint32_t)ctx->rax;",
			fmt.Sprintf("      uint32_t divisor = (uint32_t)(%s);", sRead),
			"      if (divisor != 0) {",
			"        ctx->rax = (uint64_t)(uint32_t)(dividend / divisor);",
			"        ctx->rdx = (uint64_t)(uint32_t)(dividend % divisor);",
			"      }",
			"    }",
		}, nil
	}
	if sz == 2 {
		return []string{
			"    { uint32_t dividend = ((uint32_t)(uint16_t)ctx->rdx << 16) | (uint16_t)ctx->rax;",
			fmt.Sprintf("      uint16_t divisor = (uint16_t)(%s);", sRead),
			"      if (divisor != 0) {",
			"        ctx->rax = (ctx->rax & ~0xffffULL) | ((uint64_t)(uint16_t)(dividend / divisor));",
			"        ctx->rdx = (ctx->rdx & ~0xffffULL) | ((uint64_t)(uint16_t)(dividend % divisor));",
			"      }",
			"    }",
		}, nil
	}
	if sz == 1 {
		return []string{
			"    { uint16_t dividend = (uint16_t)ctx->rax;",
			fmt.Sprintf("      uint8_t divisor = (uint8_t)(%s);", sRead),
			"      if (divisor != 0) {",
			"        uint8_t quot = (uint8_t)(dividend / divisor);",
			"        uint8_t rem = (uint8_t)(dividend % divisor);",
			"        ctx->rax = (ctx->rax & ~0xffffULL) | ((uint64_t)rem << 8) | quot;",
			"      }",
			"    }",
		}, nil
	}
	return nil, fmt.Errorf("unsupported DIV size: %d", sz)
}

func (l *Lifter) liftIdiv(src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
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
	if sz == 8 {
		return []string{
			"    { __int128 dividend = ((__int128)(int64_t)ctx->rdx << 64) | ctx->rax;",
			fmt.Sprintf("      int64_t divisor = (int64_t)(%s);", sRead),
			"      if (divisor != 0) {",
			"        ctx->rax = (uint64_t)(int64_t)(dividend / divisor);",
			"        ctx->rdx = (uint64_t)(int64_t)(dividend % divisor);",
			"      }",
			"    }",
		}, nil
	}
	if sz == 4 {
		return []string{
			"    { int64_t dividend = ((int64_t)(int32_t)ctx->rdx << 32) | (uint32_t)ctx->rax;",
			fmt.Sprintf("      int32_t divisor = (int32_t)(%s);", sRead),
			"      if (divisor != 0) {",
			"        ctx->rax = (uint64_t)(uint32_t)(int32_t)(dividend / divisor);",
			"        ctx->rdx = (uint64_t)(uint32_t)(int32_t)(dividend % divisor);",
			"      }",
			"    }",
		}, nil
	}
	if sz == 2 {
		return []string{
			"    { int32_t dividend = ((int32_t)(int16_t)ctx->rdx << 16) | (uint16_t)ctx->rax;",
			fmt.Sprintf("      int16_t divisor = (int16_t)(%s);", sRead),
			"      if (divisor != 0) {",
			"        ctx->rax = (ctx->rax & ~0xffffULL) | ((uint64_t)(uint16_t)(dividend / divisor));",
			"        ctx->rdx = (ctx->rdx & ~0xffffULL) | ((uint64_t)(uint16_t)(dividend % divisor));",
			"      }",
			"    }",
		}, nil
	}
	if sz == 1 {
		return []string{
			"    { int16_t dividend = (int16_t)(int8_t)(ctx->rax & 0xff); /* or full AX */ dividend = (int16_t)ctx->rax;",
			fmt.Sprintf("      int8_t divisor = (int8_t)(%s);", sRead),
			"      if (divisor != 0) {",
			"        int8_t quot = (int8_t)(dividend / divisor);",
			"        int8_t rem = (int8_t)(dividend % divisor);",
			"        ctx->rax = (ctx->rax & ~0xffffULL) | (((uint64_t)(uint8_t)rem) << 8) | ((uint8_t)quot);",
			"      }",
			"    }",
		}, nil
	}
	return nil, fmt.Errorf("unsupported IDIV size: %d", sz)
}

func (l *Lifter) liftPopcnt(dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	sRead, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}
	cType := uintType(sz)
	var cntExpr string
	if sz == 8 {
		cntExpr = fmt.Sprintf("__builtin_popcountll((uint64_t)(%s))", sRead)
	} else {
		cntExpr = fmt.Sprintf("__builtin_popcount((uint32_t)(%s))", sRead)
	}

	lines := []string{
		"    {",
		fmt.Sprintf("      %s val = (%s)(%s);", cType, cType, sRead),
		"      ctx->zf = (val == 0);",
		"      ctx->cf = 0; ctx->of = 0; ctx->sf = 0; ctx->af = 0; ctx->pf = 0;",
		fmt.Sprintf("      %s res = (%s)(%s);", cType, cType, cntExpr),
	}
	wLines, err := l.getOperandWrite(dst, sz, "res", nextPC)
	if err != nil {
		return nil, err
	}
	for _, w := range wLines {
		lines = append(lines, "    "+w)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftLzcnt(dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	sRead, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}
	cType := uintType(sz)
	totalBits := sz * 8

	var clzExpr string
	switch sz {
	case 8:
		clzExpr = "__builtin_clzll(val)"
	case 4:
		clzExpr = "__builtin_clz((uint32_t)val)"
	default:
		clzExpr = "(__builtin_clz((uint32_t)(uint16_t)val) - 16)"
	}

	lines := []string{
		"    {",
		fmt.Sprintf("      %s val = (%s)(%s);", cType, cType, sRead),
		fmt.Sprintf("      %s res;", cType),
		"      if (val == 0) {",
		fmt.Sprintf("        res = %d;", totalBits),
		"        ctx->cf = 1; ctx->zf = 0;",
		"      } else {",
		fmt.Sprintf("        res = (%s)(%s);", cType, clzExpr),
		"        ctx->cf = 0; ctx->zf = (res == 0);",
		"      }",
		"      ctx->of = 0; ctx->sf = 0; ctx->af = 0; ctx->pf = 0;",
	}
	wLines, err := l.getOperandWrite(dst, sz, "res", nextPC)
	if err != nil {
		return nil, err
	}
	for _, w := range wLines {
		lines = append(lines, "    "+w)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftTzcnt(dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	sRead, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}
	cType := uintType(sz)
	totalBits := sz * 8

	var ctzExpr string
	if sz == 8 {
		ctzExpr = "__builtin_ctzll(val)"
	} else {
		ctzExpr = "__builtin_ctz((uint32_t)val)"
	}

	lines := []string{
		"    {",
		fmt.Sprintf("      %s val = (%s)(%s);", cType, cType, sRead),
		fmt.Sprintf("      %s res;", cType),
		"      if (val == 0) {",
		fmt.Sprintf("        res = %d;", totalBits),
		"        ctx->cf = 1; ctx->zf = 0;",
		"      } else {",
		fmt.Sprintf("        res = (%s)(%s);", cType, ctzExpr),
		"        ctx->cf = 0; ctx->zf = (res == 0);",
		"      }",
		"      ctx->of = 0; ctx->sf = 0; ctx->af = 0; ctx->pf = 0;",
	}
	wLines, err := l.getOperandWrite(dst, sz, "res", nextPC)
	if err != nil {
		return nil, err
	}
	for _, w := range wLines {
		lines = append(lines, "    "+w)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftDoubleShift(op x86asm.Op, dst, src, cnt x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	sz := defMemSz
	if reg, ok := dst.(x86asm.Reg); ok {
		if info, ok := regMap[reg]; ok && info.Size > 0 {
			sz = info.Size
		}
	}
	dRead, _, err := l.getOperandRead(dst, sz, nextPC)
	if err != nil {
		return nil, err
	}
	sRead, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}
	cRead, _, err := l.getOperandRead(cnt, 1, nextPC)
	if err != nil {
		return nil, err
	}

	cType := uintType(sz)
	mask := sz*8 - 1
	totalBits := sz * 8

	lines := []string{
		"    {",
		fmt.Sprintf("      %s d = (%s)(%s);", cType, cType, dRead),
		fmt.Sprintf("      %s s = (%s)(%s);", cType, cType, sRead),
		fmt.Sprintf("      uint32_t count = ((uint32_t)(%s)) & %d;", cRead, mask),
		"      if (count > 0) {",
	}

	if op == x86asm.SHLD {
		lines = append(lines,
			fmt.Sprintf("        ctx->cf = (d >> (%d - count)) & 1;", totalBits),
			fmt.Sprintf("        %s res = (d << count) | (s >> (%d - count));", cType, totalBits),
			fmt.Sprintf("        if (count == 1) ctx->of = ((res >> %d) ^ ctx->cf) & 1;", totalBits-1),
		)
		wLines, err := l.getOperandWrite(dst, sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, w := range wLines {
			lines = append(lines, "      "+w)
		}
		lines = append(lines, fmt.Sprintf("        set_flags_logic_u%d(ctx, res);", totalBits))
	} else { // SHRD
		lines = append(lines,
			"        ctx->cf = (d >> (count - 1)) & 1;",
			fmt.Sprintf("        %s res = (d >> count) | (s << (%d - count));", cType, totalBits),
			fmt.Sprintf("        if (count == 1) ctx->of = (((res ^ d) >> %d) & 1);", totalBits-1),
		)
		wLines, err := l.getOperandWrite(dst, sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, w := range wLines {
			lines = append(lines, "      "+w)
		}
		lines = append(lines, fmt.Sprintf("        set_flags_logic_u%d(ctx, res);", totalBits))
	}

	lines = append(lines, "      }", "    }")
	return lines, nil
}

func (l *Lifter) liftCpuid() []string {
	return []string{
		"    {",
		"      uint32_t leaf = (uint32_t)ctx->rax;",
		"      uint32_t subleaf = (uint32_t)ctx->rcx;",
		"      uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;",
		"      switch (leaf) {",
		"      case 0:",
		"        eax = 0xd; ebx = 0x756e6547; edx = 0x49656e69; ecx = 0x6c65746e;",
		"        break;",
		"      case 1:",
		"        eax = 0x000306c3; ebx = 0x00100800; ecx = 0x7ffafbff; edx = 0xbfebfbff;",
		"        break;",
		"      case 7:",
		"        if (subleaf == 0) { ebx = 0x00000219; }",
		"        break;",
		"      case 0x80000000:",
		"        eax = 0x80000008;",
		"        break;",
		"      case 0x80000001:",
		"        ecx = 0x00000021; edx = 0x2c100800;",
		"        break;",
		"      default:",
		"        break;",
		"      }",
		"      ctx->rax = (uint64_t)eax; ctx->rbx = (uint64_t)ebx;",
		"      ctx->rcx = (uint64_t)ecx; ctx->rdx = (uint64_t)edx;",
		"    }",
	}
}

func (l *Lifter) liftXgetbv() []string {
	return []string{
		"    {",
		"      if ((uint32_t)ctx->rcx == 0) {",
		"        ctx->rax = 7ULL;",
		"        ctx->rdx = 0ULL;",
		"      } else {",
		"        ctx->rax = 0; ctx->rdx = 0;",
		"      }",
		"    }",
	}
}

func (l *Lifter) liftPushfq() []string {
	return []string{
		"    {",
		"      uint64_t rflags = 0x2;",
		"      if (ctx->cf) rflags |= (1ULL << 0);",
		"      if (ctx->pf) rflags |= (1ULL << 2);",
		"      if (ctx->af) rflags |= (1ULL << 4);",
		"      if (ctx->zf) rflags |= (1ULL << 6);",
		"      if (ctx->sf) rflags |= (1ULL << 7);",
		"      if (ctx->of) rflags |= (1ULL << 11);",
		"      ctx->rsp -= 8;",
		"      MEM_U64(ctx->rsp) = rflags;",
		"    }",
	}
}

func (l *Lifter) liftPopfq() []string {
	return []string{
		"    {",
		"      uint64_t rflags = MEM_U64(ctx->rsp);",
		"      ctx->rsp += 8;",
		"      ctx->cf = (rflags >> 0) & 1;",
		"      ctx->pf = (rflags >> 2) & 1;",
		"      ctx->af = (rflags >> 4) & 1;",
		"      ctx->zf = (rflags >> 6) & 1;",
		"      ctx->sf = (rflags >> 7) & 1;",
		"      ctx->of = (rflags >> 11) & 1;",
		"    }",
	}
}

func (l *Lifter) liftRclRcr(op x86asm.Op, dst, countArg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	dstRead, sz, err := l.getOperandRead(dst, defMemSz, nextPC)
	if err != nil {
		return nil, err
	}
	countRead, _, err := l.getOperandRead(countArg, 1, nextPC)
	if err != nil {
		return nil, err
	}
	bits := sz * 8
	cType := uintType(sz)
	var lines []string
	lines = append(
		lines,
		fmt.Sprintf("    { %s a = (%s)(%s); uint8_t count = ((uint8_t)(%s)) %% %d;", cType, cType, dstRead, countRead, bits+1),
		"      uint8_t old_cf = ctx->cf;",
	)
	if op == x86asm.RCL {
		lines = append(
			lines,
			"      for (int i = 0; i < count; i++) {",
			fmt.Sprintf("        uint8_t bit = (a >> %d) & 1;", bits-1),
			"        a = (a << 1) | old_cf;",
			"        old_cf = bit;",
			"      }",
		)
	} else {
		lines = append(
			lines,
			"      for (int i = 0; i < count; i++) {",
			"        uint8_t bit = a & 1;",
			fmt.Sprintf("        a = (a >> 1) | (((%s)old_cf) << %d);", cType, bits-1),
			"        old_cf = bit;",
			"      }",
		)
	}
	lines = append(lines, "      ctx->cf = old_cf;")
	writeStmts, err := l.getOperandWrite(dst, sz, "a", nextPC)
	if err != nil {
		return nil, err
	}
	for _, ws := range writeStmts {
		lines = append(lines, "    "+ws)
	}
	lines = append(lines, "    }")
	return lines, nil
}

func (l *Lifter) liftCmc() []string {
	return []string{"    ctx->cf = !ctx->cf;"}
}

func (l *Lifter) liftXlatb() []string {
	return []string{
		"    ctx->rax = (ctx->rax & ~0xFFULL) | MEM_U8(ctx->rbx + (ctx->rax & 0xFF));",
	}
}

func (l *Lifter) liftRdtsc() []string {
	return []string{
		"    {",
		"      uint64_t tsc = recomp_rdtsc();",
		"      ctx->rax = (ctx->rax & ~0xFFFFFFFFULL) | (uint32_t)tsc;",
		"      ctx->rdx = (ctx->rdx & ~0xFFFFFFFFULL) | (uint32_t)(tsc >> 32);",
		"    }",
	}
}

func (l *Lifter) liftSldt(dst x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	writes, err := l.getOperandWrite(dst, defMemSz, "0", nextPC)
	if err != nil {
		return nil, err
	}
	lines := make([]string, 0, len(writes))
	for _, w := range writes {
		lines = append(lines, "    "+w)
	}
	return lines, nil
}

func (l *Lifter) liftLar(dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	writes, err := l.getOperandWrite(dst, defMemSz, "0x00F20000", nextPC)
	if err != nil {
		return nil, err
	}
	lines := []string{"    ctx->zf = 1;"}
	for _, w := range writes {
		lines = append(lines, "    "+w)
	}
	return lines, nil
}

func (l *Lifter) liftCmpxchg16b(memArg x86asm.Arg, nextPC uint64) ([]string, error) {
	mem, ok := memArg.(x86asm.Mem)
	if !ok {
		return nil, fmt.Errorf("cmpxchg16b operand must be memory")
	}
	addr, err := MemAddrExpr(mem, nextPC)
	if err != nil {
		return nil, err
	}
	return []string{
		"    {",
		fmt.Sprintf("      unsigned __int128 *p = (unsigned __int128 *)(ctx->mem_base + (%s));", addr),
		"      unsigned __int128 expected = ((unsigned __int128)ctx->rdx << 64) | ctx->rax;",
		"      unsigned __int128 desired = ((unsigned __int128)ctx->rcx << 64) | ctx->rbx;",
		"      unsigned __int128 actual = __sync_val_compare_and_swap(p, expected, desired);",
		"      if (actual == expected) {",
		"        ctx->zf = 1;",
		"      } else {",
		"        ctx->zf = 0;",
		"        ctx->rax = (uint64_t)actual;",
		"        ctx->rdx = (uint64_t)(actual >> 64);",
		"      }",
		"    }",
	}, nil
}

func (l *Lifter) liftCrc32(dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
	dstReg, ok := dst.(x86asm.Reg)
	if !ok {
		return nil, fmt.Errorf("crc32 destination must be register")
	}
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
	dRead, _, err := l.getOperandRead(dst, 8, nextPC)
	if err != nil {
		return nil, err
	}

	var callExpr string
	switch sz {
	case 1:
		callExpr = fmt.Sprintf("recomp_crc32_u8((uint32_t)(%s), (uint8_t)(%s))", dRead, sRead)
	case 2:
		callExpr = fmt.Sprintf("recomp_crc32_u16((uint32_t)(%s), (uint16_t)(%s))", dRead, sRead)
	case 4:
		callExpr = fmt.Sprintf("recomp_crc32_u32((uint32_t)(%s), (uint32_t)(%s))", dRead, sRead)
	case 8:
		callExpr = fmt.Sprintf("recomp_crc32_u64((uint64_t)(%s), (uint64_t)(%s))", dRead, sRead)
	default:
		return nil, fmt.Errorf("unsupported CRC32 size: %d", sz)
	}

	wDst, err := GetRegWriteStmt(dstReg, callExpr)
	if err != nil {
		return nil, err
	}
	return []string{"    " + wDst}, nil
}

func (l *Lifter) liftPortIO(op x86asm.Op) []string {
	// User-space guest binaries may contain legacy/unused port string instructions (e.g. INSD, OUTSD, OUTSB).
	// On x86, INSD increments/decrements RDI by 4 depending on DF; OUTSD / OUTSB updates RSI by 4 or 1.
	switch op {
	case x86asm.INSD:
		return []string{
			"    if (ctx->df) { ctx->rdi -= 4; } else { ctx->rdi += 4; }",
		}
	case x86asm.OUTSD:
		return []string{
			"    if (ctx->df) { ctx->rsi -= 4; } else { ctx->rsi += 4; }",
		}
	case x86asm.OUTSB:
		return []string{
			"    if (ctx->df) { ctx->rsi -= 1; } else { ctx->rsi += 1; }",
		}
	default:
		return nil
	}
}
