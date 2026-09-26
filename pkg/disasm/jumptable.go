package disasm

import (
	"encoding/binary"

	"golang.org/x/arch/x86/x86asm"
)

// Max number of switch cases accepted from a single table. Larger values are
// treated as a failed match rather than scanning arbitrary data as code.
const maxJumpTableEntries = 4096

// picSwitch describes a recovered x86-64 PIC switch jump table.
type picSwitch struct {
	tableAddr uint64 // address of the 32-bit offset array
	baseAddr  uint64 // value added to each offset (table itself for Clang)
	count     int    // exact length if taken from cmp/ja; 0 = scan with bounds
}

func isIndirectJump(inst x86asm.Inst) bool {
	if inst.Op != x86asm.JMP {
		return false
	}
	_, isRel := inst.Args[0].(x86asm.Rel)
	return !isRel
}

func sameGPR(a, b x86asm.Reg) bool {
	fa, fb := gprFamily(a), gprFamily(b)
	return fa != 0 && fa == fb
}

func gprFamily(r x86asm.Reg) byte {
	switch r {
	case x86asm.RAX, x86asm.EAX, x86asm.AX, x86asm.AL, x86asm.AH:
		return 1
	case x86asm.RCX, x86asm.ECX, x86asm.CX, x86asm.CL, x86asm.CH:
		return 2
	case x86asm.RDX, x86asm.EDX, x86asm.DX, x86asm.DL, x86asm.DH:
		return 3
	case x86asm.RBX, x86asm.EBX, x86asm.BX, x86asm.BL, x86asm.BH:
		return 4
	case x86asm.RSP, x86asm.ESP, x86asm.SP, x86asm.SPB:
		return 5
	case x86asm.RBP, x86asm.EBP, x86asm.BP, x86asm.BPB:
		return 6
	case x86asm.RSI, x86asm.ESI, x86asm.SI, x86asm.SIB:
		return 7
	case x86asm.RDI, x86asm.EDI, x86asm.DI, x86asm.DIB:
		return 8
	case x86asm.R8, x86asm.R8L, x86asm.R8W, x86asm.R8B:
		return 9
	case x86asm.R9, x86asm.R9L, x86asm.R9W, x86asm.R9B:
		return 10
	case x86asm.R10, x86asm.R10L, x86asm.R10W, x86asm.R10B:
		return 11
	case x86asm.R11, x86asm.R11L, x86asm.R11W, x86asm.R11B:
		return 12
	case x86asm.R12, x86asm.R12L, x86asm.R12W, x86asm.R12B:
		return 13
	case x86asm.R13, x86asm.R13L, x86asm.R13W, x86asm.R13B:
		return 14
	case x86asm.R14, x86asm.R14L, x86asm.R14W, x86asm.R14B:
		return 15
	case x86asm.R15, x86asm.R15L, x86asm.R15W, x86asm.R15B:
		return 16
	default:
		return 0
	}
}

func instImm(arg x86asm.Arg) (int64, bool) {
	imm, ok := arg.(x86asm.Imm)
	return int64(imm), ok
}

func leaRIPTarget(inst Instruction) (x86asm.Reg, uint64, bool) {
	if inst.Inst.Op != x86asm.LEA {
		return 0, 0, false
	}
	dst, ok := inst.Inst.Args[0].(x86asm.Reg)
	if !ok {
		return 0, 0, false
	}
	mem, ok := inst.Inst.Args[1].(x86asm.Mem)
	if !ok || mem.Base != x86asm.RIP {
		return 0, 0, false
	}
	next := inst.Address + uint64(inst.Inst.Len)
	target := uint64(int64(next) + int64(int32(mem.Disp)))
	return dst, target, true
}

// matchPICSwitch recognizes the Clang/GCC x86-64 PIC switch idiom ending at
// the last instruction of window (an indirect JMP):
//
//	cmp/sub index, N
//	ja/jae default
//	lea tableReg, table(%rip)
//	movsxd jmpReg, [tableReg + index*4]
//	add jmpReg, baseReg
//	jmp jmpReg
//
// matchPICSwitch recognizes the Clang/GCC x86-64 PIC switch idiom ending at
// window[len-1], which must be an indirect JMP.
//
// If tableAddr or switch count is not found in window (e.g. when the switch
// spans across multiple basic blocks), findTableAddr and findCount are queried
// as fallbacks (tracing predecessor basic blocks).
func matchPICSwitch(
	window []Instruction,
	findTableAddr func(reg x86asm.Reg) (uint64, bool),
	findCount func(idxReg x86asm.Reg) (int, bool),
) (picSwitch, bool) {
	var none picSwitch
	n := len(window)
	if n < 3 {
		return none, false
	}

	jmp := window[n-1]
	if jmp.Inst.Op != x86asm.JMP {
		return none, false
	}
	jmpReg, ok := jmp.Inst.Args[0].(x86asm.Reg)
	if !ok {
		return none, false
	}

	add := window[n-2]
	if add.Inst.Op != x86asm.ADD {
		return none, false
	}
	addDst, ok := add.Inst.Args[0].(x86asm.Reg)
	if !ok || !sameGPR(addDst, jmpReg) {
		return none, false
	}
	baseReg, ok := add.Inst.Args[1].(x86asm.Reg)
	if !ok {
		return none, false
	}

	movs := window[n-3]
	if movs.Inst.Op != x86asm.MOVSXD && movs.Inst.Op != x86asm.MOVSX {
		return none, false
	}
	movDst, ok := movs.Inst.Args[0].(x86asm.Reg)
	if !ok || !sameGPR(movDst, jmpReg) {
		return none, false
	}
	mem, ok := movs.Inst.Args[1].(x86asm.Mem)
	if !ok || mem.Scale != 4 || mem.Index == 0 {
		return none, false
	}
	tableReg := mem.Base
	if tableReg == 0 {
		return none, false
	}
	idxReg := mem.Index

	var tableAddr, codeBase uint64
	foundTable, foundBase := false, false
	for i := n - 4; i >= 0; i-- {
		dst, target, isLEA := leaRIPTarget(window[i])
		if !isLEA {
			continue
		}
		if !foundTable && sameGPR(dst, tableReg) {
			tableAddr = target
			foundTable = true
		}
		if !foundBase && sameGPR(dst, baseReg) {
			codeBase = target
			foundBase = true
		}
		if foundTable && (foundBase || sameGPR(baseReg, tableReg)) {
			break
		}
	}
	if !foundTable && findTableAddr != nil {
		tableAddr, foundTable = findTableAddr(tableReg)
	}
	if !foundBase && !sameGPR(baseReg, tableReg) && findTableAddr != nil {
		codeBase, foundBase = findTableAddr(baseReg)
	}
	if !foundTable {
		return none, false
	}
	var baseAddr uint64
	switch {
	case sameGPR(baseReg, tableReg):
		baseAddr = tableAddr
	case foundBase:
		baseAddr = codeBase
	default:
		return none, false
	}

	count := switchTableCount(window[:n-3], idxReg)
	if count <= 0 && findCount != nil {
		if c, ok := findCount(idxReg); ok {
			count = c
		}
	}
	return picSwitch{tableAddr: tableAddr, baseAddr: baseAddr, count: count}, true
}

// switchTableCount reads `cmp/sub reg, imm; ja/jae` immediately before the
// table load. JA means entries = imm+1; JAE means entries = imm.
func switchTableCount(window []Instruction, idxReg x86asm.Reg) int {
	for i := len(window) - 1; i >= 1; i-- {
		op := window[i].Inst.Op
		if op != x86asm.JA && op != x86asm.JAE {
			continue
		}
		for j := i - 1; j >= 0; j-- {
			prev := window[j].Inst
			if prev.Op != x86asm.CMP && prev.Op != x86asm.SUB {
				// MOV/LEA/MOVZX spills do not clobber flags; keep walking.
				if prev.Op == x86asm.MOV || prev.Op == x86asm.LEA || prev.Op == x86asm.NOP || prev.Op == x86asm.MOVZX {
					continue
				}
				break
			}
			if dst, ok := prev.Args[0].(x86asm.Reg); ok && sameGPR(dst, idxReg) {
				imm, ok := instImm(prev.Args[1])
				if !ok || imm < 0 || imm >= maxJumpTableEntries {
					break
				}
				n := int(imm)
				if op == x86asm.JA {
					n++
				}
				if n <= 0 || n > maxJumpTableEntries {
					break
				}
				return n
			}
		}
	}


	return 0
}

// readPIC32Targets walks a 32-bit PIC jump table.
// Each entry is a signed offset added to baseAddr.
// count==0 scans until an entry falls outside [fnStart, fnEnd).
func (d *Disassembler) readPIC32Targets(tableAddr, baseAddr uint64, count int, fnStart, fnEnd uint64) []uint64 {
	if d == nil || d.elf == nil {
		return nil
	}
	img := d.elf.MemoryImage
	bounded := fnEnd > fnStart
	if count <= 0 && !bounded {
		return nil
	}

	limit := count
	if limit <= 0 {
		limit = maxJumpTableEntries
	}
	if limit > maxJumpTableEntries {
		limit = maxJumpTableEntries
	}

	targets := make([]uint64, 0, 8)
	seen := make(map[uint64]struct{}, 8)
	for i := 0; i < limit; i++ {
		off := tableAddr + uint64(i*4)
		if off+4 > uint64(len(img)) {
			break
		}
		rel := int32(binary.LittleEndian.Uint32(img[off : off+4]))
		if count <= 0 && baseAddr == tableAddr && rel == 0 {
			break
		}
		target := uint64(int64(baseAddr) + int64(rel))
		if bounded && (target < fnStart || target >= fnEnd) {
			if count > 0 {
				continue
			}
			break
		}
		if !d.inCode(target) {
			if count > 0 {
				continue
			}
			break
		}
		if _, ok := seen[target]; ok {
			continue
		}
		seen[target] = struct{}{}
		targets = append(targets, target)
	}
	if count <= 0 && len(targets) < 2 {
		return nil
	}
	return targets
}

func (d *Disassembler) jumpTableTargets(sw picSwitch, fnStart, fnEnd uint64) []uint64 {
	targets := d.readPIC32Targets(sw.tableAddr, sw.baseAddr, sw.count, fnStart, fnEnd)
	if sw.count > 0 {
		d.noteData(sw.tableAddr, sw.tableAddr+uint64(sw.count)*4)
	}
	return targets
}
