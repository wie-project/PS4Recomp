package disasm

import (
	"encoding/binary"

	"golang.org/x/arch/x86/x86asm"
)

// Max number of switch cases accepted from a single table. Larger values are
// treated as a failed match rather than scanning arbitrary data as code.
const maxJumpTableEntries = 4096

// JumpTableFormat represents the encoding format of jump table entries.
type JumpTableFormat int

const (
	TableFormatRel32 JumpTableFormat = iota // 32-bit signed offset added to BaseAddr (Clang PIC standard)
	TableFormatAbs64                        // 64-bit absolute target address
	TableFormatRel8                         // 8-bit offset added to BaseAddr
	TableFormatRel16                        // 16-bit offset added to BaseAddr
)

// ResolvedJumpTable holds recovered jump table metadata and destination targets.
type ResolvedJumpTable struct {
	Format    JumpTableFormat
	TableAddr uint64
	BaseAddr  uint64
	Count     int
	Targets   []uint64
}

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
func modifiesReg(inst x86asm.Inst, reg x86asm.Reg) bool {
	if reg == 0 {
		return false
	}
	switch inst.Op {
	case x86asm.CMP, x86asm.TEST, x86asm.BT, x86asm.NOP, x86asm.UD2:
		return false
	case x86asm.PUSH:
		return sameGPR(reg, x86asm.RSP)
	case x86asm.XCHG:
		for _, arg := range inst.Args {
			if r, ok := arg.(x86asm.Reg); ok && sameGPR(r, reg) {
				return true
			}
		}
		return false
	case x86asm.CALL:
		return true
	default:
		if len(inst.Args) > 0 {
			if dst, ok := inst.Args[0].(x86asm.Reg); ok && sameGPR(dst, reg) {
				return true
			}
		}
		return false
	}
}

// matchPICSwitch recognizes the Clang/GCC x86-64 PIC switch idiom ending at
// window[len-1], which must be an indirect JMP.
//
// If tableAddr or switch count is not found in window (e.g. when the switch
// spans across multiple basic blocks), findTableAddr and findCount are queried
// as fallbacks (tracing predecessor basic blocks).
// matchJumpTable recovers indirect jump tables using backward data-flow slicing.
func matchJumpTable(
	window []Instruction,
	findTableAddr func(reg x86asm.Reg) (uint64, bool),
	findCount func(idxReg x86asm.Reg) (int, bool),
) (ResolvedJumpTable, bool) {
	var none ResolvedJumpTable
	n := len(window)
	if n < 1 {
		return none, false
	}

	jmp := window[n-1]
	if jmp.Inst.Op != x86asm.JMP {
		return none, false
	}

	// Case 1: Direct Memory Jump: jmp [mem]
	if mem, ok := jmp.Inst.Args[0].(x86asm.Mem); ok {
		if mem.Scale == 8 && mem.Index != 0 {
			tableAddr, foundTable := resolveMemTableAddr(window[:n-1], mem, jmp.Address+uint64(jmp.Inst.Len), findTableAddr)
			if foundTable {
				count := switchTableCount(window[:n-1], mem.Index)
				if count <= 0 && findCount != nil {
					if c, ok := findCount(mem.Index); ok {
						count = c
					}
				}
				return ResolvedJumpTable{
					Format:    TableFormatAbs64,
					TableAddr: tableAddr,
					Count:     count,
				}, true
			}
		}
	}

	// Case 2: Register Jump: jmp jmpReg
	jmpReg, ok := jmp.Inst.Args[0].(x86asm.Reg)
	if !ok {
		return none, false
	}

	// Case 2A: Check for 64-bit absolute table load directly into jmpReg:
	// mov jmpReg, [tableReg + idxReg*8]
	for i := n - 2; i >= 0 && i >= n-32; i-- {
		inst := window[i].Inst
		if inst.Op == x86asm.MOV {
			if dst, ok := inst.Args[0].(x86asm.Reg); ok && sameGPR(dst, jmpReg) {
				if mem, ok := inst.Args[1].(x86asm.Mem); ok && mem.Scale == 8 && mem.Index != 0 {
					tableAddr, foundTable := resolveMemTableAddr(window[:i], mem, window[i].Address+uint64(inst.Len), findTableAddr)
					if foundTable {
						count := switchTableCount(window[:i], mem.Index)
						if count <= 0 && findCount != nil {
							if c, ok := findCount(mem.Index); ok {
								count = c
							}
						}
						return ResolvedJumpTable{
							Format:    TableFormatAbs64,
							TableAddr: tableAddr,
							Count:     count,
						}, true
					}
				}
			}
		}
		if modifiesReg(inst, jmpReg) {
			break
		}
	}

	// Case 2B: Check for relative switch with addition:
	// add jmpReg, baseReg  (or lea jmpReg, [jmpReg + baseReg])
	addIdx := -1
	var baseReg, offReg x86asm.Reg
	for i := n - 2; i >= 0 && i >= n-32; i-- {
		inst := window[i].Inst
		if inst.Op == x86asm.ADD {
			dst, ok0 := inst.Args[0].(x86asm.Reg)
			src, ok1 := inst.Args[1].(x86asm.Reg)
			if ok0 && ok1 {
				if sameGPR(dst, jmpReg) {
					addIdx = i
					offReg = dst
					baseReg = src
					break
				} else if sameGPR(src, jmpReg) {
					addIdx = i
					offReg = src
					baseReg = dst
					break
				}
			}
		} else if inst.Op == x86asm.LEA {
			if dst, ok := inst.Args[0].(x86asm.Reg); ok && sameGPR(dst, jmpReg) {
				if mem, ok := inst.Args[1].(x86asm.Mem); ok && mem.Disp == 0 {
					if sameGPR(mem.Base, jmpReg) && mem.Index != 0 && mem.Scale <= 1 {
						addIdx = i
						offReg = mem.Base
						baseReg = mem.Index
						break
					} else if sameGPR(mem.Index, jmpReg) && mem.Base != 0 && mem.Scale <= 1 {
						addIdx = i
						offReg = mem.Index
						baseReg = mem.Base
						break
					}
				}
			}
		}
		if modifiesReg(inst, jmpReg) {
			break
		}
	}
	if addIdx < 0 {
		return none, false
	}

	// Trace the offset load before addIdx
	loadIdx := -1
	var tableReg, idxReg x86asm.Reg
	var format JumpTableFormat
	for i := addIdx - 1; i >= 0 && i >= addIdx-32; i-- {
		inst := window[i].Inst
		if inst.Op == x86asm.MOVSXD || inst.Op == x86asm.MOVSX || inst.Op == x86asm.MOVZX || inst.Op == x86asm.MOV {
			if dst, ok := inst.Args[0].(x86asm.Reg); ok && sameGPR(dst, offReg) {
				if mem, ok := inst.Args[1].(x86asm.Mem); ok && mem.Index != 0 {
					loadIdx = i
					idxReg = mem.Index
					tableReg = mem.Base
					switch {
					case mem.Scale == 4 || inst.Op == x86asm.MOVSXD:
						format = TableFormatRel32
					case mem.Scale == 1 && (inst.Op == x86asm.MOVZX || inst.Op == x86asm.MOVSX):
						format = TableFormatRel8
					case mem.Scale == 2 && (inst.Op == x86asm.MOVZX || inst.Op == x86asm.MOVSX):
						format = TableFormatRel16
					default:
						format = TableFormatRel32
					}
					break
				}
			}
		}
		if modifiesReg(inst, offReg) || modifiesReg(inst, baseReg) {
			break
		}
	}
	if loadIdx < 0 {
		return none, false
	}

	var tableAddr, codeBase uint64
	foundTable, foundBase := false, false
	for i := addIdx - 1; i >= 0; i-- {
		if i == loadIdx {
			continue
		}
		dst, target, isLEA := leaRIPTarget(window[i])
		if !isLEA {
			continue
		}
		if !foundTable && tableReg != 0 && sameGPR(dst, tableReg) {
			tableAddr = target
			foundTable = true
		}
		if !foundBase && baseReg != 0 && sameGPR(dst, baseReg) {
			codeBase = target
			foundBase = true
		}
		if foundTable && (foundBase || sameGPR(baseReg, tableReg)) {
			break
		}
	}
	if !foundTable && tableReg != 0 && findTableAddr != nil {
		tableAddr, foundTable = findTableAddr(tableReg)
	}
	if !foundBase && baseReg != 0 && !sameGPR(baseReg, tableReg) && findTableAddr != nil {
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

	count := switchTableCount(window[:loadIdx], idxReg)
	if count <= 0 && findCount != nil {
		if c, ok := findCount(idxReg); ok {
			count = c
		}
	}

	return ResolvedJumpTable{
		Format:    format,
		TableAddr: tableAddr,
		BaseAddr:  baseAddr,
		Count:     count,
	}, true
}

func resolveMemTableAddr(
	window []Instruction,
	mem x86asm.Mem,
	nextPC uint64,
	findTableAddr func(reg x86asm.Reg) (uint64, bool),
) (uint64, bool) {
	if mem.Base == x86asm.RIP {
		return uint64(int64(nextPC) + int64(int32(mem.Disp))), true
	}
	if mem.Base == 0 && mem.Disp != 0 {
		return uint64(mem.Disp), true
	}
	if mem.Base != 0 {
		for i := len(window) - 1; i >= 0; i-- {
			dst, target, isLEA := leaRIPTarget(window[i])
			if isLEA && sameGPR(dst, mem.Base) {
				return uint64(int64(target) + mem.Disp), true
			}
			if modifiesReg(window[i].Inst, mem.Base) {
				break
			}
		}
		if findTableAddr != nil {
			if base, ok := findTableAddr(mem.Base); ok {
				return uint64(int64(base) + mem.Disp), true
			}
		}
	}
	return 0, false
}

// matchPICSwitch wraps matchJumpTable for backward compatibility with existing tests.
func matchPICSwitch(
	window []Instruction,
	findTableAddr func(reg x86asm.Reg) (uint64, bool),
	findCount func(idxReg x86asm.Reg) (int, bool),
) (picSwitch, bool) {
	var none picSwitch
	table, ok := matchJumpTable(window, findTableAddr, findCount)
	if !ok || table.Format != TableFormatRel32 {
		return none, false
	}
	return picSwitch{
		tableAddr: table.TableAddr,
		baseAddr:  table.BaseAddr,
		count:     table.Count,
	}, true
}

// switchTableCount discovers table length from bounds checks (cmp/sub/and).
func switchTableCount(window []Instruction, idxReg x86asm.Reg) int {
	for i := len(window) - 1; i >= 1; i-- {
		op := window[i].Inst.Op
		switch op {
		case x86asm.JA, x86asm.JAE:
			for j := i - 1; j >= 0; j-- {
				prev := window[j].Inst
				if prev.Op == x86asm.MOV || prev.Op == x86asm.LEA || prev.Op == x86asm.NOP ||
					prev.Op == x86asm.MOVZX || prev.Op == x86asm.MOVSX {
					continue
				}
				if prev.Op != x86asm.CMP && prev.Op != x86asm.SUB {
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
					if n > 0 && n <= maxJumpTableEntries {
						return n
					}
				}
			}
		case x86asm.JB, x86asm.JBE:
			// cmp imm, reg; jb/jbe
			for j := i - 1; j >= 0; j-- {
				prev := window[j].Inst
				if prev.Op == x86asm.MOV || prev.Op == x86asm.LEA || prev.Op == x86asm.NOP {
					continue
				}
				if prev.Op != x86asm.CMP {
					break
				}
				if src, ok := prev.Args[1].(x86asm.Reg); ok && sameGPR(src, idxReg) {
					imm, ok := instImm(prev.Args[0])
					if !ok || imm < 0 || imm >= maxJumpTableEntries {
						break
					}
					n := int(imm)
					if op == x86asm.JB {
						n++
					}
					if n > 0 && n <= maxJumpTableEntries {
						return n
					}
				}
			}
		}
	}

	// Also check for index bitmasks: e.g. and idxReg, 0x1f -> count = 32
	for i := len(window) - 1; i >= 0 && i >= len(window)-8; i-- {
		inst := window[i].Inst
		if inst.Op == x86asm.AND {
			if dst, ok := inst.Args[0].(x86asm.Reg); ok && sameGPR(dst, idxReg) {
				if imm, ok := instImm(inst.Args[1]); ok && imm > 0 && imm < maxJumpTableEntries {
					// Check if mask is of the form 2^k - 1
					if (imm & (imm + 1)) == 0 {
						return int(imm + 1)
					}
				}
			}
		}
	}

	return 0
}

// readResolvedTargets reads all target addresses from the recovered jump table.
func (d *Disassembler) readResolvedTargets(sw ResolvedJumpTable, fnStart, fnEnd uint64) []uint64 {
	if d == nil || d.elf == nil {
		return nil
	}
	img := d.elf.MemoryImage
	bounded := fnEnd > fnStart

	limit := sw.Count
	if limit <= 0 {
		limit = maxJumpTableEntries
	}
	if limit > maxJumpTableEntries {
		limit = maxJumpTableEntries
	}

	entrySize := uint64(4)
	switch sw.Format {
	case TableFormatAbs64:
		entrySize = 8
	case TableFormatRel8:
		entrySize = 1
	case TableFormatRel16:
		entrySize = 2
	}

	targets := make([]uint64, 0, 8)
	seen := make(map[uint64]struct{}, 8)

	for i := 0; i < limit; i++ {
		off := sw.TableAddr + uint64(i)*entrySize
		if off+entrySize > uint64(len(img)) {
			break
		}

		var target uint64
		switch sw.Format {
		case TableFormatRel32:
			rel := int32(binary.LittleEndian.Uint32(img[off : off+4]))
			if sw.Count <= 0 && sw.BaseAddr == sw.TableAddr && rel == 0 {
				break
			}
			target = uint64(int64(sw.BaseAddr) + int64(rel))
		case TableFormatAbs64:
			target = binary.LittleEndian.Uint64(img[off : off+8])
			if sw.Count <= 0 && target == 0 {
				break
			}
		case TableFormatRel8:
			rel := int8(img[off])
			target = uint64(int64(sw.BaseAddr) + int64(rel))
		case TableFormatRel16:
			rel := int16(binary.LittleEndian.Uint16(img[off : off+2]))
			target = uint64(int64(sw.BaseAddr) + int64(rel))
		}

		if target == 0 {
			if sw.Count > 0 {
				continue
			}
			break
		}

		if bounded && (target < fnStart || target >= fnEnd) {
			if sw.Count > 0 {
				if d.inCode(target) {
					if _, ok := seen[target]; !ok {
						seen[target] = struct{}{}
						targets = append(targets, target)
					}
				}
				continue
			}
			break
		}

		if !d.inCode(target) {
			if sw.Count > 0 {
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

	if sw.Count <= 0 && len(targets) < 2 {
		return nil
	}
	if sw.Count > 0 {
		d.noteData(sw.TableAddr, sw.TableAddr+uint64(sw.Count)*entrySize)
	}
	return targets
}

// ResolveJumpTable resolves jump table metadata and returns target addresses.
func (d *Disassembler) ResolveJumpTable(
	window []Instruction,
	findTableAddr func(reg x86asm.Reg) (uint64, bool),
	findCount func(idxReg x86asm.Reg) (int, bool),
	fnStart, fnEnd uint64,
) (ResolvedJumpTable, bool) {
	table, ok := matchJumpTable(window, findTableAddr, findCount)
	if !ok {
		return ResolvedJumpTable{}, false
	}
	table.Targets = d.readResolvedTargets(table, fnStart, fnEnd)
	if len(table.Targets) == 0 {
		return ResolvedJumpTable{}, false
	}
	return table, true
}

func (d *Disassembler) readPIC32Targets(tableAddr, baseAddr uint64, count int, fnStart, fnEnd uint64) []uint64 {
	sw := ResolvedJumpTable{
		Format:    TableFormatRel32,
		TableAddr: tableAddr,
		BaseAddr:  baseAddr,
		Count:     count,
	}
	return d.readResolvedTargets(sw, fnStart, fnEnd)
}

func (d *Disassembler) jumpTableTargets(sw picSwitch, fnStart, fnEnd uint64) []uint64 {
	targets := d.readPIC32Targets(sw.tableAddr, sw.baseAddr, sw.count, fnStart, fnEnd)
	if sw.count > 0 {
		d.noteData(sw.tableAddr, sw.tableAddr+uint64(sw.count)*4)
	}
	return targets
}
