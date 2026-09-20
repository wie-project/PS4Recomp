package lifter

import (
	"fmt"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

func (l *Lifter) liftBMI(inst disasm.Instruction, nextPC uint64) ([]string, error) {
	op := inst.Inst.Op
	args := inst.Inst.Args

	dstReg, ok := args[0].(x86asm.Reg)
	if !ok {
		return nil, fmt.Errorf("%v destination must be a register", op)
	}

	info, ok := regMap[dstReg]
	if !ok || (info.Size != 4 && info.Size != 8) {
		return nil, fmt.Errorf("%v unsupported destination register: %v", op, dstReg)
	}
	sz := info.Size
	bits := sz * 8
	cType := uintType(sz)

	var lines []string

	switch op {
	case x86asm.BEXTR:
		// BEXTR dst, src, ctrl
		srcRead, _, err := l.getOperandRead(args[1], sz, nextPC)
		if err != nil {
			return nil, err
		}
		ctrlRead, _, err := l.getOperandRead(args[2], sz, nextPC)
		if err != nil {
			return nil, err
		}

		lines = append(lines,
			"    {",
			fmt.Sprintf("      uint32_t ctrl = (uint32_t)(%s);", ctrlRead),
			"      uint32_t start = ctrl & 0xff;",
			"      uint32_t len = (ctrl >> 8) & 0xff;",
			fmt.Sprintf("      %s src = (%s)(%s);", cType, cType, srcRead),
			fmt.Sprintf("      %s res = 0;", cType),
			fmt.Sprintf("      if (start < %d) {", bits),
			fmt.Sprintf("        %s shifted = src >> start;", cType),
			fmt.Sprintf("        %s mask = (len >= %d) ? (%s)(~(%s)0) : (((%s)1 << len) - 1);", cType, bits, cType, cType, cType),
			"        res = shifted & mask;",
			"      }",
			"      ctx->zf = (res == 0);",
			"      ctx->cf = 0;",
			"      ctx->of = 0;",
		)
		w, err := l.getOperandWrite(args[0], sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, s := range w {
			lines = append(lines, "      "+s)
		}
		lines = append(lines, "    }")
		return lines, nil

	case x86asm.BZHI:
		// BZHI dst, src, indexReg
		srcRead, _, err := l.getOperandRead(args[1], sz, nextPC)
		if err != nil {
			return nil, err
		}
		idxRead, _, err := l.getOperandRead(args[2], sz, nextPC)
		if err != nil {
			return nil, err
		}

		lines = append(lines,
			"    {",
			fmt.Sprintf("      uint32_t idx = ((uint32_t)(%s)) & 0xff;", idxRead),
			fmt.Sprintf("      %s src = (%s)(%s);", cType, cType, srcRead),
			fmt.Sprintf("      %s res;", cType),
			fmt.Sprintf("      if (idx < %d) {", bits),
			fmt.Sprintf("        %s mask = ((%s)1 << idx) - 1;", cType, cType),
			"        res = src & mask;",
			"        ctx->cf = 0;",
			"      } else {",
			"        res = src;",
			fmt.Sprintf("        ctx->cf = (idx >= %d);", bits),
			"      }",
			"      ctx->zf = (res == 0);",
			"      ctx->of = 0;",
		)
		w, err := l.getOperandWrite(args[0], sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, s := range w {
			lines = append(lines, "      "+s)
		}
		lines = append(lines, "    }")
		return lines, nil

	case x86asm.ANDN:
		// ANDN dst, src1, src2
		src1Read, _, err := l.getOperandRead(args[1], sz, nextPC)
		if err != nil {
			return nil, err
		}
		src2Read, _, err := l.getOperandRead(args[2], sz, nextPC)
		if err != nil {
			return nil, err
		}

		lines = append(lines,
			"    {",
			fmt.Sprintf("      %s a = (%s)(%s);", cType, cType, src1Read),
			fmt.Sprintf("      %s b = (%s)(%s);", cType, cType, src2Read),
			fmt.Sprintf("      %s res = (~a) & b;", cType),
			"      ctx->zf = (res == 0);",
			fmt.Sprintf("      ctx->sf = (res >> %d) & 1;", bits-1),
			"      ctx->cf = 0;",
			"      ctx->of = 0;",
		)
		w, err := l.getOperandWrite(args[0], sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, s := range w {
			lines = append(lines, "      "+s)
		}
		lines = append(lines, "    }")
		return lines, nil

	case x86asm.BLSR:
		// BLSR dst, src
		srcRead, _, err := l.getOperandRead(args[1], sz, nextPC)
		if err != nil {
			return nil, err
		}

		lines = append(lines,
			"    {",
			fmt.Sprintf("      %s src = (%s)(%s);", cType, cType, srcRead),
			fmt.Sprintf("      %s res = (src - 1) & src;", cType),
			"      ctx->cf = (src == 0);",
			"      ctx->zf = (res == 0);",
			fmt.Sprintf("      ctx->sf = (res >> %d) & 1;", bits-1),
			"      ctx->of = 0;",
		)
		w, err := l.getOperandWrite(args[0], sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, s := range w {
			lines = append(lines, "      "+s)
		}
		lines = append(lines, "    }")
		return lines, nil

	case x86asm.BLSMSK:
		// BLSMSK dst, src
		srcRead, _, err := l.getOperandRead(args[1], sz, nextPC)
		if err != nil {
			return nil, err
		}

		lines = append(lines,
			"    {",
			fmt.Sprintf("      %s src = (%s)(%s);", cType, cType, srcRead),
			fmt.Sprintf("      %s res = (src - 1) ^ src;", cType),
			"      ctx->cf = (src == 0);",
			"      ctx->zf = 0;",
			fmt.Sprintf("      ctx->sf = (res >> %d) & 1;", bits-1),
			"      ctx->of = 0;",
		)
		w, err := l.getOperandWrite(args[0], sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, s := range w {
			lines = append(lines, "      "+s)
		}
		lines = append(lines, "    }")
		return lines, nil

	case x86asm.BLSI:
		// BLSI dst, src
		srcRead, _, err := l.getOperandRead(args[1], sz, nextPC)
		if err != nil {
			return nil, err
		}

		lines = append(lines,
			"    {",
			fmt.Sprintf("      %s src = (%s)(%s);", cType, cType, srcRead),
			fmt.Sprintf("      %s res = (-src) & src;", cType),
			"      ctx->cf = (src != 0);",
			"      ctx->zf = (res == 0);",
			fmt.Sprintf("      ctx->sf = (res >> %d) & 1;", bits-1),
			"      ctx->of = 0;",
		)
		w, err := l.getOperandWrite(args[0], sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, s := range w {
			lines = append(lines, "      "+s)
		}
		lines = append(lines, "    }")
		return lines, nil

	case x86asm.SHLX, x86asm.SHRX, x86asm.SARX:
		// SHLX/SHRX/SARX dst, src, cntReg
		srcRead, _, err := l.getOperandRead(args[1], sz, nextPC)
		if err != nil {
			return nil, err
		}
		cntRead, _, err := l.getOperandRead(args[2], sz, nextPC)
		if err != nil {
			return nil, err
		}

		var shiftExpr string
		switch op {
		case x86asm.SHLX:
			shiftExpr = fmt.Sprintf("((%s)(%s)) << shift", cType, srcRead)
		case x86asm.SHRX:
			shiftExpr = fmt.Sprintf("((%s)(%s)) >> shift", cType, srcRead)
		case x86asm.SARX:
			signedType := fmt.Sprintf("int%d_t", bits)
			shiftExpr = fmt.Sprintf("(%s)(((%s)(%s)) >> shift)", cType, signedType, srcRead)
		}

		lines = append(lines,
			"    {",
			fmt.Sprintf("      uint32_t shift = ((uint32_t)(%s)) & %d;", cntRead, bits-1),
			fmt.Sprintf("      %s res = %s;", cType, shiftExpr),
		)
		w, err := l.getOperandWrite(args[0], sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, s := range w {
			lines = append(lines, "      "+s)
		}
		lines = append(lines, "    }")
		return lines, nil

	case x86asm.RORX:
		// RORX dst, src, imm8
		srcRead, _, err := l.getOperandRead(args[1], sz, nextPC)
		if err != nil {
			return nil, err
		}
		imm, ok := args[2].(x86asm.Imm)
		if !ok {
			return nil, fmt.Errorf("RORX third argument must be immediate")
		}

		lines = append(lines,
			"    {",
			fmt.Sprintf("      uint32_t count = %d & %d;", uint32(imm)&0xff, bits-1),
			fmt.Sprintf("      %s src = (%s)(%s);", cType, cType, srcRead),
			fmt.Sprintf("      %s res = count == 0 ? src : (src >> count) | (src << (%d - count));", cType, bits),
		)
		w, err := l.getOperandWrite(args[0], sz, "res", nextPC)
		if err != nil {
			return nil, err
		}
		for _, s := range w {
			lines = append(lines, "      "+s)
		}
		lines = append(lines, "    }")
		return lines, nil

	case x86asm.MULX:
		// MULX dst_hi, dst_lo, src
		srcRead, _, err := l.getOperandRead(args[2], sz, nextPC)
		if err != nil {
			return nil, err
		}

		if sz == 4 {
			lines = append(lines,
				"    {",
				fmt.Sprintf("      uint64_t prod = (uint64_t)(uint32_t)(ctx->rdx) * (uint64_t)(uint32_t)(%s);", srcRead),
				"      uint32_t lo = (uint32_t)prod;",
				"      uint32_t hi = (uint32_t)(prod >> 32);",
			)
			wLo, err := l.getOperandWrite(args[1], 4, "lo", nextPC)
			if err != nil {
				return nil, err
			}
			for _, s := range wLo {
				lines = append(lines, "      "+s)
			}
			wHi, err := l.getOperandWrite(args[0], 4, "hi", nextPC)
			if err != nil {
				return nil, err
			}
			for _, s := range wHi {
				lines = append(lines, "      "+s)
			}
			lines = append(lines, "    }")
		} else {
			lines = append(lines,
				"    {",
				fmt.Sprintf("      unsigned __int128 prod = (unsigned __int128)(ctx->rdx) * (unsigned __int128)(%s);", srcRead),
				"      uint64_t lo = (uint64_t)prod;",
				"      uint64_t hi = (uint64_t)(prod >> 64);",
			)
			wLo, err := l.getOperandWrite(args[1], 8, "lo", nextPC)
			if err != nil {
				return nil, err
			}
			for _, s := range wLo {
				lines = append(lines, "      "+s)
			}
			wHi, err := l.getOperandWrite(args[0], 8, "hi", nextPC)
			if err != nil {
				return nil, err
			}
			for _, s := range wHi {
				lines = append(lines, "      "+s)
			}
			lines = append(lines, "    }")
		}
		return lines, nil
	}

	return nil, fmt.Errorf("unsupported BMI opcode: %v", op)
}
