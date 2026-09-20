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
