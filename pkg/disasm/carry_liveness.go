package disasm

import (
	"strings"

	"golang.org/x/arch/x86/x86asm"
)

// CF/OF liveness across the blocks of a function.
//
// The ARM64 assembly lifter keeps x86 CF in ARM C with an inverted sense after subtractions
// (C = !CF, ARM's borrow convention) and the direct sense after additions and logical
// operations. At a block boundary the lifter assumes the subtract sense, so a block that ends in
// the other sense has to invert C on the way out, but only when a successor can read CF before
// redefining it. Some lowerings also preserve CF (INC/DEC) or compute CF/OF (rotates) only when
// the flag is live. This pass computes, per instruction, whether CF and OF are dead after it.
// It is conservative: unknown successors and unknown instructions keep the flags live.

const (
	flagCF uint8 = 1 << iota
	flagOF
)

// carryOverflowEffect returns the CF/OF bits an instruction reads (before writing) and the bits
// it defines (writes or leaves undefined, so an earlier value cannot be observed through it).
// An instruction it does not know is treated as reading CF and OF and defining nothing.
func carryOverflowEffect(inst x86asm.Inst) (reads, kills uint8) {
	reads, kills, known := knownCarryOverflowEffect(inst)
	if !known {
		return flagCF | flagOF, 0
	}
	return reads, kills
}

// knownCarryOverflowEffect is carryOverflowEffect for the instructions it classifies; known is
// false for the others.
func knownCarryOverflowEffect(inst x86asm.Inst) (reads, kills uint8, known bool) {
	both := flagCF | flagOF
	switch inst.Op {
	case x86asm.JB, x86asm.JAE, x86asm.JBE, x86asm.JA,
		x86asm.SETB, x86asm.SETAE, x86asm.SETBE, x86asm.SETA,
		x86asm.CMOVB, x86asm.CMOVAE, x86asm.CMOVBE, x86asm.CMOVA,
		x86asm.FCMOVB, x86asm.FCMOVNB, x86asm.FCMOVBE, x86asm.FCMOVNBE, x86asm.LAHF:
		return flagCF, 0, true
	case x86asm.JO, x86asm.JNO, x86asm.JL, x86asm.JGE, x86asm.JLE, x86asm.JG,
		x86asm.SETO, x86asm.SETNO, x86asm.SETL, x86asm.SETGE, x86asm.SETLE, x86asm.SETG,
		x86asm.CMOVO, x86asm.CMOVNO, x86asm.CMOVL, x86asm.CMOVGE, x86asm.CMOVLE, x86asm.CMOVG:
		return flagOF, 0, true
	case x86asm.JE, x86asm.JNE, x86asm.JS, x86asm.JNS, x86asm.JP, x86asm.JNP,
		x86asm.JCXZ, x86asm.JECXZ, x86asm.JRCXZ, x86asm.LOOP, x86asm.LOOPE, x86asm.LOOPNE,
		x86asm.SETE, x86asm.SETNE, x86asm.SETS, x86asm.SETNS, x86asm.SETP, x86asm.SETNP,
		x86asm.CMOVE, x86asm.CMOVNE, x86asm.CMOVS, x86asm.CMOVNS, x86asm.CMOVP, x86asm.CMOVNP,
		x86asm.FCMOVE, x86asm.FCMOVNE, x86asm.FCMOVU, x86asm.FCMOVNU:
		return 0, 0, true // ZF/SF/PF only
	case x86asm.PUSHF, x86asm.PUSHFD, x86asm.PUSHFQ:
		return both, 0, true
	case x86asm.ADC, x86asm.SBB:
		return flagCF, both, true
	case x86asm.CMC:
		return flagCF, flagCF, true
	case x86asm.CLC, x86asm.STC:
		return 0, flagCF, true
	case x86asm.INC, x86asm.DEC:
		return 0, flagOF, true // CF is preserved
	case x86asm.SAHF:
		return 0, flagCF, true // OF is preserved
	case x86asm.ADD, x86asm.SUB, x86asm.CMP, x86asm.TEST, x86asm.AND, x86asm.OR, x86asm.XOR, x86asm.NEG,
		x86asm.BSF, x86asm.BSR, x86asm.TZCNT, x86asm.LZCNT, x86asm.POPCNT,
		x86asm.MUL, x86asm.IMUL, x86asm.DIV, x86asm.IDIV,
		x86asm.ANDN, x86asm.BEXTR, x86asm.BLSI, x86asm.BLSR, x86asm.BLSMSK, x86asm.BZHI,
		x86asm.COMISS, x86asm.UCOMISS, x86asm.COMISD, x86asm.UCOMISD,
		x86asm.VCOMISS, x86asm.VUCOMISS, x86asm.VCOMISD, x86asm.VUCOMISD,
		x86asm.PTEST, x86asm.VPTEST, x86asm.VTESTPS, x86asm.VTESTPD,
		x86asm.BT, x86asm.BTS, x86asm.BTR, x86asm.BTC,
		x86asm.POPF, x86asm.POPFD, x86asm.POPFQ, x86asm.CMPXCHG, x86asm.XADD,
		x86asm.FCOMI, x86asm.FCOMIP, x86asm.FUCOMI, x86asm.FUCOMIP,
		x86asm.PCMPISTRI, x86asm.PCMPESTRI, x86asm.PCMPISTRM, x86asm.PCMPESTRM,
		x86asm.VPCMPISTRI, x86asm.VPCMPESTRI, x86asm.VPCMPISTRM, x86asm.VPCMPESTRM,
		// Flags are not preserved across calls (System V ABI).
		x86asm.CALL:
		return 0, both, true
	case x86asm.CMPSB, x86asm.CMPSW, x86asm.CMPSD, x86asm.CMPSQ, x86asm.SCASB, x86asm.SCASW, x86asm.SCASD, x86asm.SCASQ:
		// With REPE/REPNE and RCX = 0 nothing is compared and the flags pass through.
		if hasRepPrefix(inst) {
			return 0, 0, true
		}
		return 0, both, true
	case x86asm.SHL, x86asm.SHR, x86asm.SAR, x86asm.ROL, x86asm.ROR, x86asm.SHLD, x86asm.SHRD:
		// A count of 0 leaves the flags unchanged, so only a non-zero immediate count defines them.
		if c, ok := shiftImmCount(inst); ok && c != 0 {
			return 0, both, true
		}
		return 0, 0, true
	case x86asm.RCL, x86asm.RCR:
		if c, ok := shiftImmCount(inst); ok && c != 0 {
			return flagCF, both, true
		}
		return flagCF, 0, true
	}
	if flagNeutral(inst) {
		return 0, 0, true
	}
	return 0, 0, false
}

// flagNeutral reports instructions that neither read nor write any of the arithmetic flags:
// data movement, address arithmetic, BMI2 shifts, fences and hints, and vector, MMX and x87
// instructions other than the compares and conditional moves classified above.
func flagNeutral(inst x86asm.Inst) bool {
	switch inst.Op {
	case x86asm.MOV, x86asm.MOVZX, x86asm.MOVSX, x86asm.MOVSXD, x86asm.MOVBE, x86asm.MOVNTI, x86asm.LEA,
		x86asm.NOP, x86asm.PUSH, x86asm.POP, x86asm.XCHG, x86asm.BSWAP, x86asm.NOT,
		x86asm.CBW, x86asm.CWDE, x86asm.CDQE, x86asm.CWD, x86asm.CDQ, x86asm.CQO,
		x86asm.SHLX, x86asm.SHRX, x86asm.SARX, x86asm.RORX, x86asm.MULX,
		x86asm.CRC32, x86asm.CMPXCHG8B, x86asm.CMPXCHG16B, // CMPXCHG8B/16B write ZF only
		x86asm.LFENCE, x86asm.SFENCE, x86asm.MFENCE, x86asm.PAUSE, x86asm.CLFLUSH,
		x86asm.PREFETCHNTA, x86asm.PREFETCHT0, x86asm.PREFETCHT1, x86asm.PREFETCHT2, x86asm.PREFETCHW,
		x86asm.CLD, x86asm.STD, x86asm.MOVSB, x86asm.MOVSW, x86asm.MOVSD, x86asm.MOVSQ,
		x86asm.STOSB, x86asm.STOSW, x86asm.STOSD, x86asm.STOSQ, x86asm.LODSB, x86asm.LODSW, x86asm.LODSD, x86asm.LODSQ,
		x86asm.CPUID, x86asm.RDTSC, x86asm.RDTSCP, x86asm.XGETBV, x86asm.LEAVE, x86asm.ENTER,
		x86asm.LDMXCSR, x86asm.STMXCSR, x86asm.VLDMXCSR, x86asm.VSTMXCSR,
		x86asm.FXSAVE, x86asm.FXRSTOR, x86asm.XSAVE, x86asm.XRSTOR, x86asm.EMMS,
		x86asm.RET, x86asm.JMP, x86asm.INT, x86asm.UD2, x86asm.HLT:
		return true
	}
	name := inst.Op.String()
	if strings.HasPrefix(name, "V") || strings.HasPrefix(name, "F") {
		return true // AVX and x87 (their flag-writing compares are classified before)
	}
	for _, a := range inst.Args {
		if a == nil {
			break
		}
		if r, ok := a.(x86asm.Reg); ok && (r >= x86asm.X0 && r <= x86asm.X15 || r >= x86asm.M0 && r <= x86asm.M7) {
			return true // SSE/MMX
		}
	}
	return false
}

// readsAnyFlag reports whether an instruction can read one of the arithmetic flags (CF, PF, AF,
// ZF, SF, OF) or pass it on unchanged to a later reader in a way the per-block SkipFlags pass
// must respect. Unknown instructions count as readers.
func readsAnyFlag(inst x86asm.Inst) bool {
	op := inst.Op
	if isJcc(op) || isSetcc(op) || isCmovcc(op) {
		return true
	}
	switch op {
	case x86asm.ADC, x86asm.SBB, x86asm.RCL, x86asm.RCR, x86asm.CMC, x86asm.LAHF,
		x86asm.PUSHF, x86asm.PUSHFD, x86asm.PUSHFQ,
		x86asm.FCMOVB, x86asm.FCMOVNB, x86asm.FCMOVE, x86asm.FCMOVNE,
		x86asm.FCMOVBE, x86asm.FCMOVNBE, x86asm.FCMOVU, x86asm.FCMOVNU:
		return true
	}
	_, _, known := knownCarryOverflowEffect(inst)
	return !known
}

// hasRepPrefix reports a REP/REPE (F3) or REPNE (F2) prefix.
func hasRepPrefix(inst x86asm.Inst) bool {
	for _, p := range inst.Prefix {
		if p == 0 {
			break
		}
		if b := p & 0xff; b == 0xf3 || b == 0xf2 {
			return true
		}
	}
	return false
}

// shiftImmCount returns the masked immediate count of a shift or rotate.
func shiftImmCount(inst x86asm.Inst) (uint64, bool) {
	for i := len(inst.Args) - 1; i >= 1; i-- {
		if inst.Args[i] == nil {
			continue
		}
		imm, ok := inst.Args[i].(x86asm.Imm)
		if !ok {
			return 0, false
		}
		mask := uint64(0x1f)
		if inst.DataSize == 64 || inst.MemBytes == 8 {
			mask = 0x3f
		}
		if r, isReg := inst.Args[0].(x86asm.Reg); isReg && r >= x86asm.RAX && r <= x86asm.R15 {
			mask = 0x3f
		}
		return uint64(imm) & mask, true
	}
	return 0, false
}

// isOrbisDebugBreak reports whether inst is INT 0x41, which the lifter lowers to a no-op (see
// the INT case in pkg/lifter/arm64_lifter.go): control continues at the next instruction.
func isOrbisDebugBreak(inst x86asm.Inst) bool {
	imm, ok := inst.Args[0].(x86asm.Imm)
	return inst.Op == x86asm.INT && ok && imm == 0x41
}

// blockSuccessors returns the start addresses of the blocks control can reach from the end of b
// inside fn; unknown is true when a successor cannot be determined (indirect jump without a
// recovered table, fallthrough into an address that is not a block of fn, direct jump to an
// address outside fn that isFunc does not accept as a function entry).
func blockSuccessors(fn *Function, b *BasicBlock, isFunc func(uint64) bool) (succs []uint64, unknown bool) {
	if len(b.Insts) == 0 {
		return nil, true
	}
	last := b.Insts[len(b.Insts)-1]
	nextPC := last.Address + uint64(last.Inst.Len)
	local := func(t uint64) {
		if fn.Blocks[t] != nil {
			succs = append(succs, t)
			return
		}
		// A transfer to another function is a tail call: flags are dead there (System V ABI).
		// Any other target continues somewhere unknown with the flags possibly live.
		if isFunc == nil || !isFunc(t) {
			unknown = true
		}
	}
	switch op := last.Inst.Op; {
	case op == x86asm.INT && isOrbisDebugBreak(last.Inst):
		// INT 0x41 (the Orbis debugger break) is lowered to a no-op and falls through to
		// nextPC; it is handled like any other instruction below.
	case op == x86asm.RET || op == x86asm.LRET || op == x86asm.UD2 || op == x86asm.HLT || op == x86asm.INT:
		return nil, false
	case op == x86asm.JMP:
		if rel, ok := last.Inst.Args[0].(x86asm.Rel); ok {
			local(uint64(int64(nextPC) + int64(rel)))
			return succs, unknown
		}
		if ts, ok := fn.JumpTables[last.Address]; ok && len(ts) > 0 {
			for _, t := range ts {
				local(t)
			}
			return succs, unknown
		}
		return nil, true
	case isJcc(op):
		if rel, ok := last.Inst.Args[0].(x86asm.Rel); ok {
			local(uint64(int64(nextPC) + int64(rel)))
		} else {
			unknown = true
		}
		if fn.Blocks[nextPC] != nil {
			succs = append(succs, nextPC)
		} else {
			unknown = true
		}
		return succs, unknown
	}
	if fn.Blocks[nextPC] != nil {
		return []uint64{nextPC}, false
	}
	return nil, true
}

// analyzeCarryOverflowLiveness sets Instruction.CarryDead / OverflowDead for every instruction
// of fn (see the comment at the top of the file). isFunc reports whether a direct branch target
// outside fn is a function entry (a tail call).
func analyzeCarryOverflowLiveness(fn *Function, isFunc func(uint64) bool) {
	if fn == nil || len(fn.Blocks) == 0 {
		return
	}
	type info struct {
		succs   []uint64
		unknown bool
		in      uint8
	}
	infos := make(map[uint64]*info, len(fn.Blocks))
	for addr, b := range fn.Blocks {
		s, u := blockSuccessors(fn, b, isFunc)
		infos[addr] = &info{succs: s, unknown: u}
	}
	all := flagCF | flagOF
	for changed := true; changed; {
		changed = false
		for i := len(fn.BlockOrder) - 1; i >= 0; i-- {
			addr := fn.BlockOrder[i]
			b, bi := fn.Blocks[addr], infos[addr]
			if b == nil || bi == nil {
				continue
			}
			var live uint8
			if bi.unknown {
				live = all
			}
			for _, s := range bi.succs {
				if si := infos[s]; si != nil {
					live |= si.in
				}
			}
			for k := len(b.Insts) - 1; k >= 0; k-- {
				in := &b.Insts[k]
				in.CarryDead = live&flagCF == 0
				in.OverflowDead = live&flagOF == 0
				reads, kills := carryOverflowEffect(in.Inst)
				live = (live &^ kills) | reads
			}
			if live != bi.in {
				bi.in = live
				changed = true
			}
		}
	}
}
