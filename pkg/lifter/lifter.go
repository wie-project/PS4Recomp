package lifter

import (
	"fmt"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

// Lifter translates disasm.Instruction into C code.
type Lifter struct {
	knownFuncs map[uint64]bool
}

// NewLifter creates a new instruction lifter.
func NewLifter(knownFuncs map[uint64]bool) *Lifter {
	return &Lifter{
		knownFuncs: knownFuncs,
	}
}

// LiftInstruction lifts a single instruction into C statements.
func (l *Lifter) LiftInstruction(inst disasm.Instruction, nextPC uint64, fn *disasm.Function) ([]string, error) {
	pc := inst.Address
	op := inst.Inst.Op
	args := inst.Inst.Args

	var lines []string
	lines = append(lines, fmt.Sprintf("    /* 0x%x: %s */", pc, inst.Inst.String()))
	lines = append(lines, fmt.Sprintf("    ctx->rip = 0x%xULL;", pc))

	// Determine effective memory/operand size from instruction
	defMemSz := inst.Inst.MemBytes
	if defMemSz == 0 && inst.Inst.DataSize > 0 {
		defMemSz = inst.Inst.DataSize / 8
	}
	if defMemSz == 0 {
		defMemSz = 8
	}

	switch op {
	case x86asm.NOP:
		// No-op

	case x86asm.MOV:
		code, err := l.liftMov(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CDQE:
		lines = append(lines, "    ctx->rax = (uint64_t)(int64_t)(int32_t)ctx->rax;")

	case x86asm.ROL, x86asm.ROR:
		code, err := l.liftRotate(op, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MOVZX, x86asm.MOVSX, x86asm.MOVSXD:
		code, err := l.liftExtend(op, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.LEA:
		dstReg, ok := args[0].(x86asm.Reg)
		if !ok {
			return nil, fmt.Errorf("0x%x: LEA destination must be a register", pc)
		}
		srcMem, ok := args[1].(x86asm.Mem)
		if !ok {
			return nil, fmt.Errorf("0x%x: LEA source must be a memory operand", pc)
		}
		addrExpr, err := MemAddrExpr(srcMem, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		stmt, err := GetRegWriteStmt(dstReg, addrExpr)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, "    "+stmt)

	case x86asm.PUSH:
		valExpr, sz, err := l.getOperandRead(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(
			lines,
			"    ctx->rsp -= 8;",
			fmt.Sprintf("    MEM_U64(ctx->rsp) = (uint64_t)(%s);", valExpr),
		)
		_ = sz

	case x86asm.POP:
		dstReg, ok := args[0].(x86asm.Reg)
		if !ok {
			return nil, fmt.Errorf("0x%x: POP destination must be register", pc)
		}
		stmt, err := GetRegWriteStmt(dstReg, "MEM_U64(ctx->rsp)")
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(
			lines,
			"    "+stmt,
			"    ctx->rsp += 8;",
		)

	case x86asm.ADD, x86asm.SUB, x86asm.AND, x86asm.OR, x86asm.XOR, x86asm.CMP, x86asm.TEST, x86asm.ADC:
		code, err := l.liftAlu(op, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.BSF:
		code, err := l.liftBsf(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.INC, x86asm.DEC, x86asm.NEG, x86asm.NOT:
		code, err := l.liftUnary(op, args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.SHL, x86asm.SHR, x86asm.SAR:
		code, err := l.liftShift(op, args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.XCHG:
		code, err := l.liftXchg(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.XADD:
		code, err := l.liftXadd(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CMPXCHG:
		code, err := l.liftCmpxchg(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.IMUL:
		code, err := l.liftImul(args, defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MUL:
		code, err := l.liftMul(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.CALL:
		if rel, ok := args[0].(x86asm.Rel); ok {
			target := uint64(int64(nextPC) + int64(rel))
			lines = append(
				lines,
				"    ctx->rsp -= 8;",
				fmt.Sprintf("    MEM_U64(ctx->rsp) = 0x%xULL;", nextPC),
				fmt.Sprintf("    recomp_dispatch(ctx, 0x%xULL);", target),
			)
		} else {
			targetExpr, _, err := l.getOperandRead(args[0], defMemSz, nextPC)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			lines = append(
				lines,
				"    ctx->rsp -= 8;",
				fmt.Sprintf("    MEM_U64(ctx->rsp) = 0x%xULL;", nextPC),
				fmt.Sprintf("    recomp_dispatch(ctx, %s);", targetExpr),
			)
		}

	case x86asm.RET:
		lines = append(
			lines,
			"    ctx->rsp += 8;",
			"    return;",
		)

	case x86asm.JMP:
		if rel, ok := args[0].(x86asm.Rel); ok {
			target := uint64(int64(nextPC) + int64(rel))
			if _, ok := fn.Blocks[target]; ok {
				lines = append(lines, fmt.Sprintf("    goto loc_0x%x;", target))
			} else {
				lines = append(
					lines,
					fmt.Sprintf("    recomp_dispatch(ctx, 0x%xULL);", target),
					"    return;",
				)
			}
		} else {
			targetExpr, _, err := l.getOperandRead(args[0], defMemSz, nextPC)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			lines = append(
				lines,
				fmt.Sprintf("    recomp_dispatch(ctx, %s);", targetExpr),
				"    return;",
			)
		}

	case x86asm.UD2:
		lines = append(
			lines,
			fmt.Sprintf("    fprintf(stderr, \"UD2 instruction at 0x%x\\n\");", pc),
			"    abort();",
		)

	case x86asm.INT:
		lines = append(
			lines,
			fmt.Sprintf("    /* Interrupt at 0x%x */", pc),
		)

	case x86asm.FWAIT:
		lines = append(lines, "    /* fwait no-op */")

	// Vector/SSE operations
	case x86asm.MOVUPS, x86asm.MOVAPS, x86asm.MOVDQU, x86asm.MOVDQA:
		code, err := l.liftVectorMove(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PXOR, x86asm.XORPS:
		code, err := l.liftVectorXor(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.POR:
		code, err := l.liftVectorBitwise(" | ", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PAND:
		code, err := l.liftVectorBitwise(" & ", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MOVD:
		code, err := l.liftMovd(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MOVQ:
		code, err := l.liftMovq(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.MOVSS:
		code, err := l.liftMovss(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSHUFD:
		code, err := l.liftPshufd(args[0], args[1], args[2], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSLLD:
		code, err := l.liftPshift("<<", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.PSRLD:
		code, err := l.liftPshift(">>", args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.SBB:
		code, err := l.liftSbb(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.BT:
		code, err := l.liftBt(args[0], args[1], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.DIV:
		code, err := l.liftDiv(args[0], defMemSz, nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	case x86asm.HLT:
		lines = append(lines, "    exit(0);")

	case x86asm.MOVSD_XMM, x86asm.MOVSD:
		code, err := l.liftMovsd(args[0], args[1], nextPC)
		if err != nil {
			return nil, fmt.Errorf("0x%x: %w", pc, err)
		}
		lines = append(lines, code...)

	default:
		// Check SETcc
		if isSetcc(op) {
			cond, err := setccCondition(op)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			setExpr := fmt.Sprintf("(%s ? 1 : 0)", cond)
			writeStmts, err := l.getOperandWrite(args[0], 1, setExpr, nextPC)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			for _, ws := range writeStmts {
				lines = append(lines, "    "+ws)
			}
			return lines, nil
		}

		// Check CMOVcc
		if isCmovcc(op) {
			cond, err := cmovCondition(op)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			srcExpr, sz, err := l.getOperandRead(args[1], defMemSz, nextPC)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			writeStmts, err := l.getOperandWrite(args[0], sz, srcExpr, nextPC)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			lines = append(lines, fmt.Sprintf("    if (%s) {", cond))
			for _, ws := range writeStmts {
				lines = append(lines, "    "+ws)
			}
			lines = append(lines, "    }")
			return lines, nil
		}

		// Check Jcc
		if IsJcc(op) {
			cond, err := ConditionExpr(op)
			if err != nil {
				return nil, fmt.Errorf("0x%x: %w", pc, err)
			}
			rel, ok := args[0].(x86asm.Rel)
			if !ok {
				return nil, fmt.Errorf("0x%x: Jcc target must be Rel", pc)
			}
			target := uint64(int64(nextPC) + int64(rel))
			if _, ok := fn.Blocks[target]; ok {
				lines = append(lines, fmt.Sprintf("    if (%s) goto loc_0x%x;", cond, target))
			} else {
				lines = append(
					lines,
					fmt.Sprintf("    if (%s) { recomp_dispatch(ctx, 0x%xULL); return; }", cond, target),
				)
			}
		} else {
			return nil, fmt.Errorf("unsupported opcode: %v", op)
		}
	}

	return lines, nil
}

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

func (l *Lifter) liftAlu(op x86asm.Op, dst, src x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
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
		lines = append(
			lines,
			"      res = a + b;",
			fmt.Sprintf("      set_flags_add_u%d(ctx, a, b, res);", sz*8),
		)
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
		lines = append(
			lines,
			"      res = a - b;",
			fmt.Sprintf("      set_flags_sub_u%d(ctx, a, b, res);", sz*8),
		)
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
		lines = append(
			lines,
			"      res = a & b;",
			fmt.Sprintf("      set_flags_logic_u%d(ctx, res);", sz*8),
		)
		writeStmts, err := l.getOperandWrite(dst, sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, ws := range writeStmts {
			lines = append(lines, "    "+ws)
		}

	case x86asm.OR:
		lines = append(
			lines,
			"      res = a | b;",
			fmt.Sprintf("      set_flags_logic_u%d(ctx, res);", sz*8),
		)
		writeStmts, err := l.getOperandWrite(dst, sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, ws := range writeStmts {
			lines = append(lines, "    "+ws)
		}

	case x86asm.XOR:
		lines = append(
			lines,
			"      res = a ^ b;",
			fmt.Sprintf("      set_flags_logic_u%d(ctx, res);", sz*8),
		)
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

func (l *Lifter) liftUnary(op x86asm.Op, dst x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
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
		lines = append(
			lines,
			"      res = a + 1;",
			fmt.Sprintf("      set_flags_inc_u%d(ctx, a, res);", sz*8),
		)
	case x86asm.DEC:
		lines = append(
			lines,
			"      res = a - 1;",
			fmt.Sprintf("      set_flags_dec_u%d(ctx, a, res);", sz*8),
		)
	case x86asm.NEG:
		lines = append(
			lines,
			"      res = -a;",
			fmt.Sprintf("      set_flags_sub_u%d(ctx, 0, a, res);", sz*8),
		)
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

func (l *Lifter) liftShift(op x86asm.Op, dst, countArg x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
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
		lines = append(
			lines,
			"      res = a << count;",
			fmt.Sprintf("      set_flags_logic_u%d(ctx, res);", sz*8),
		)
	case x86asm.SHR:
		lines = append(
			lines,
			"      res = a >> count;",
			fmt.Sprintf("      set_flags_logic_u%d(ctx, res);", sz*8),
		)
	case x86asm.SAR:
		signedType := intType(sz)
		lines = append(
			lines,
			fmt.Sprintf("      res = (%s)(((%s)a) >> count);", cType, signedType),
			fmt.Sprintf("      set_flags_logic_u%d(ctx, res);", sz*8),
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
	sz := sz1
	if sz2 > sz {
		sz = sz2
	}
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
	dRead, _, err := l.getOperandRead(dst, sz, nextPC)
	if err != nil {
		return nil, err
	}
	sRead, _, err := l.getOperandRead(src, sz, nextPC)
	if err != nil {
		return nil, err
	}
	var lines []string
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
	dRead, _, err := l.getOperandRead(dst, sz, nextPC)
	if err != nil {
		return nil, err
	}
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
	srcReg, ok2 := src.(x86asm.Reg)
	if ok1 && ok2 {
		infoDst := regMap[dstReg]
		infoSrc := regMap[srcReg]
		return []string{
			fmt.Sprintf("    ctx->%s.u64[0] = ctx->%s.u64[0] %s ctx->%s.u64[0];", infoDst.BaseReg, infoDst.BaseReg, opStr, infoSrc.BaseReg),
			fmt.Sprintf("    ctx->%s.u64[1] = ctx->%s.u64[1] %s ctx->%s.u64[1];", infoDst.BaseReg, infoDst.BaseReg, opStr, infoSrc.BaseReg),
		}, nil
	}
	return nil, fmt.Errorf("unsupported vector bitwise operands")
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
		fmt.Sprintf("      if (shift < 32) {"),
		fmt.Sprintf("        ctx->%s.u32[0] %s= shift; ctx->%s.u32[1] %s= shift;", infoDst.BaseReg, shiftOp, infoDst.BaseReg, shiftOp),
		fmt.Sprintf("        ctx->%s.u32[2] %s= shift; ctx->%s.u32[3] %s= shift;", infoDst.BaseReg, shiftOp, infoDst.BaseReg, shiftOp),
		"      } else {",
		fmt.Sprintf("        memset(&ctx->%s, 0, 16);", infoDst.BaseReg),
		"      }",
		"    }",
	}, nil
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

func (l *Lifter) liftBt(base, bit x86asm.Arg, defMemSz int, nextPC uint64) ([]string, error) {
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
	return []string{
		fmt.Sprintf("    ctx->cf = (((uint64_t)(%s)) >> (((uint64_t)(%s)) & %d)) & 1;", bRead, bitRead, sz*8-1),
	}, nil
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
	lines = append(lines,
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
			fmt.Sprintf("    { unsigned __int128 dividend = ((unsigned __int128)ctx->rdx << 64) | ctx->rax;"),
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
			fmt.Sprintf("    { uint64_t dividend = ((uint64_t)(uint32_t)ctx->rdx << 32) | (uint32_t)ctx->rax;"),
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

func isXmm(reg x86asm.Reg) bool {
	info, ok := regMap[reg]
	return ok && info.Size == 16
}

func (l *Lifter) getOperandRead(arg x86asm.Arg, defMemSz int, nextPC uint64) (string, int, error) {
	switch a := arg.(type) {
	case x86asm.Reg:
		return GetRegReadExpr(a)
	case x86asm.Mem:
		addr, err := MemAddrExpr(a, nextPC)
		if err != nil {
			return "", 0, err
		}
		sz := defMemSz
		if sz == 0 {
			sz = 8
		}
		expr, err := MemReadExpr(addr, sz)
		return expr, sz, err
	case x86asm.Imm:
		return fmt.Sprintf("0x%xULL", uint64(a)), 8, nil
	default:
		return "", 0, fmt.Errorf("unknown operand type: %T", arg)
	}
}

func (l *Lifter) getOperandWrite(arg x86asm.Arg, sz int, valExpr string, nextPC uint64) ([]string, error) {
	switch a := arg.(type) {
	case x86asm.Reg:
		stmt, err := GetRegWriteStmt(a, valExpr)
		if err != nil {
			return nil, err
		}
		return []string{stmt}, nil
	case x86asm.Mem:
		addr, err := MemAddrExpr(a, nextPC)
		if err != nil {
			return nil, err
		}
		stmt, err := MemWriteStmt(addr, sz, valExpr)
		if err != nil {
			return nil, err
		}
		return []string{stmt}, nil
	default:
		return nil, fmt.Errorf("cannot write to operand of type %T", arg)
	}
}

func isSetcc(op x86asm.Op) bool {
	switch op {
	case x86asm.SETA, x86asm.SETAE, x86asm.SETB, x86asm.SETBE,
		x86asm.SETE, x86asm.SETG, x86asm.SETGE, x86asm.SETL,
		x86asm.SETLE, x86asm.SETNE, x86asm.SETNO, x86asm.SETNP,
		x86asm.SETNS, x86asm.SETO, x86asm.SETP, x86asm.SETS:
		return true
	default:
		return false
	}
}

func setccCondition(op x86asm.Op) (string, error) {
	switch op {
	case x86asm.SETE:
		return "ctx->zf", nil
	case x86asm.SETNE:
		return "!ctx->zf", nil
	case x86asm.SETB:
		return "ctx->cf", nil
	case x86asm.SETAE:
		return "!ctx->cf", nil
	case x86asm.SETBE:
		return "(ctx->cf || ctx->zf)", nil
	case x86asm.SETA:
		return "(!ctx->cf && !ctx->zf)", nil
	case x86asm.SETS:
		return "ctx->sf", nil
	case x86asm.SETNS:
		return "!ctx->sf", nil
	case x86asm.SETO:
		return "ctx->of", nil
	case x86asm.SETNO:
		return "!ctx->of", nil
	case x86asm.SETL:
		return "(ctx->sf != ctx->of)", nil
	case x86asm.SETGE:
		return "(ctx->sf == ctx->of)", nil
	case x86asm.SETLE:
		return "(ctx->zf || (ctx->sf != ctx->of))", nil
	case x86asm.SETG:
		return "(!ctx->zf && (ctx->sf == ctx->of))", nil
	case x86asm.SETP:
		return "ctx->pf", nil
	case x86asm.SETNP:
		return "!ctx->pf", nil
	default:
		return "", fmt.Errorf("unknown setcc op: %v", op)
	}
}

func isCmovcc(op x86asm.Op) bool {
	switch op {
	case x86asm.CMOVA, x86asm.CMOVAE, x86asm.CMOVB, x86asm.CMOVBE,
		x86asm.CMOVE, x86asm.CMOVG, x86asm.CMOVGE, x86asm.CMOVL,
		x86asm.CMOVLE, x86asm.CMOVNE, x86asm.CMOVNO, x86asm.CMOVNP,
		x86asm.CMOVNS, x86asm.CMOVO, x86asm.CMOVP, x86asm.CMOVS:
		return true
	default:
		return false
	}
}

func cmovCondition(op x86asm.Op) (string, error) {
	switch op {
	case x86asm.CMOVE:
		return "ctx->zf", nil
	case x86asm.CMOVNE:
		return "!ctx->zf", nil
	case x86asm.CMOVB:
		return "ctx->cf", nil
	case x86asm.CMOVAE:
		return "!ctx->cf", nil
	case x86asm.CMOVBE:
		return "(ctx->cf || ctx->zf)", nil
	case x86asm.CMOVA:
		return "(!ctx->cf && !ctx->zf)", nil
	case x86asm.CMOVS:
		return "ctx->sf", nil
	case x86asm.CMOVNS:
		return "!ctx->sf", nil
	case x86asm.CMOVO:
		return "ctx->of", nil
	case x86asm.CMOVNO:
		return "!ctx->of", nil
	case x86asm.CMOVL:
		return "ctx->sf != ctx->of", nil
	case x86asm.CMOVGE:
		return "ctx->sf == ctx->of", nil
	case x86asm.CMOVLE:
		return "ctx->zf || ctx->sf != ctx->of", nil
	case x86asm.CMOVG:
		return "!ctx->zf && ctx->sf == ctx->of", nil
	case x86asm.CMOVP:
		return "ctx->pf", nil
	case x86asm.CMOVNP:
		return "!ctx->pf", nil
	default:
		return "", fmt.Errorf("unknown cmovcc op: %v", op)
	}
}

func uintType(size int) string {
	switch size {
	case 1:
		return "uint8_t"
	case 2:
		return "uint16_t"
	case 4:
		return "uint32_t"
	default:
		return "uint64_t"
	}
}

func intType(size int) string {
	switch size {
	case 1:
		return "int8_t"
	case 2:
		return "int16_t"
	case 4:
		return "int32_t"
	default:
		return "int64_t"
	}
}
