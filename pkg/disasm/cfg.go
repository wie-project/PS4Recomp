package disasm

import (
	"encoding/binary"
	"fmt"
	"sort"

	"ps4-recomp/pkg/elfloader"

	"golang.org/x/arch/x86/x86asm"
)

// Instruction wraps x86asm.Inst with its guest address.
type Instruction struct {
	Address   uint64
	Inst      x86asm.Inst
	SkipFlags bool
}

// BasicBlock represents a straight-line sequence of instructions.
type BasicBlock struct {
	StartAddr uint64
	EndAddr   uint64 // Address of the next instruction after the last one
	Insts     []Instruction
	Succs     []uint64 // Successor block start addresses
}

// Function represents a discovered guest function composed of basic blocks.
type Function struct {
	Name       string
	EntryAddr  uint64
	Blocks     map[uint64]*BasicBlock
	BlockOrder []uint64 // Topologically or address-sorted block start addresses
}

// Disassembler performs CFG recovery and reachability analysis.
type Disassembler struct {
	elf       *elfloader.LoadedELF
	textStart uint64
	textEnd   uint64

	Functions map[uint64]*Function
	Blocks    map[uint64]*BasicBlock
}

// NewDisassembler creates a new disassembler for the given loaded ELF.
func NewDisassembler(loaded *elfloader.LoadedELF) (*Disassembler, error) {
	textSec, ok := loaded.Sections[".text"]
	if !ok {
		return nil, fmt.Errorf("no .text section found in ELF")
	}

	return &Disassembler{
		elf:       loaded,
		textStart: textSec.Addr,
		textEnd:   textSec.Addr + textSec.Size,
		Functions: make(map[uint64]*Function, 1024),
		Blocks:    make(map[uint64]*BasicBlock, 4096),
	}, nil
}

// AnalyzeReachable traverses and discovers all functions reachable from the given entry addresses.
func (d *Disassembler) AnalyzeReachable(entryAddrs []uint64) error {
	queue := make([]uint64, 0, len(entryAddrs)*2)
	visited := make(map[uint64]bool, len(entryAddrs)*2)

	for _, addr := range entryAddrs {
		if !visited[addr] && addr >= d.textStart && addr < d.textEnd {
			queue = append(queue, addr)
			visited[addr] = true
		}
	}

	for head := 0; head < len(queue); head++ {
		curr := queue[head]

		fn, newCalls, err := d.disasmFunction(curr)
		if err != nil {
			continue
		}
		d.Functions[curr] = fn

		for _, target := range newCalls {
			if !visited[target] && target >= d.textStart && target < d.textEnd {
				visited[target] = true
				queue = append(queue, target)
			}
		}
	}

	return nil
}

// DisasmFunction disassembles a single function starting at entryAddr (exported for testing).
func (d *Disassembler) DisasmFunction(entryAddr uint64) (*Function, []uint64, error) {
	return d.disasmFunction(entryAddr)
}

// disasmFunction disassembles a single function starting at entryAddr.
func (d *Disassembler) disasmFunction(entryAddr uint64) (*Function, []uint64, error) {
	if sym, ok := d.elf.SymbolByAddr[entryAddr]; ok && sym.Size > 0 {
		return d.disasmLinearFunction(entryAddr, sym.Size)
	}
	return d.disasmBranchFollowing(entryAddr)
}

// disasmLinearFunction linearly decodes instructions within [entryAddr, entryAddr+size)
// and partitions them into basic blocks based on control flow leaders and jump tables.
func (d *Disassembler) disasmLinearFunction(entryAddr uint64, size uint64) (*Function, []uint64, error) {
	symName := fmt.Sprintf("fn_%x", entryAddr)
	if sym, ok := d.elf.SymbolByAddr[entryAddr]; ok && sym.Name != "" {
		symName = sym.Name
	}

	estInstCount := int(size/4) + 4
	fn := &Function{
		Name:       symName,
		EntryAddr:  entryAddr,
		Blocks:     make(map[uint64]*BasicBlock, 8),
		BlockOrder: make([]uint64, 0, 8),
	}

	fnEnd := entryAddr + size
	insts := make([]Instruction, 0, estInstCount)
	discoveredCalls := make([]uint64, 0, 8)

	pc := entryAddr
	for pc < fnEnd {
		if pc < d.textStart || pc >= d.textEnd || pc >= uint64(len(d.elf.MemoryImage)) {
			break
		}

		inst, err := x86asm.Decode(d.elf.MemoryImage[pc:], 64)
		if err != nil || inst.Len == 0 {
			break
		}

		wrapped := Instruction{
			Address: pc,
			Inst:    inst,
		}
		insts = append(insts, wrapped)

		nextPC := pc + uint64(inst.Len)

		// Record calls
		if inst.Op == x86asm.CALL {
			if rel, ok := inst.Args[0].(x86asm.Rel); ok {
				target := uint64(int64(nextPC) + int64(rel))
				discoveredCalls = append(discoveredCalls, target)
			}
		}

		// Record function pointers loaded via LEA RIP+disp
		if inst.Op == x86asm.LEA {
			if mem, ok := inst.Args[1].(x86asm.Mem); ok && mem.Base == x86asm.RIP {
				disp := int64(int32(mem.Disp))
				target := uint64(int64(nextPC) + disp)
				if target >= d.textStart && target < d.textEnd && (target < entryAddr || target >= fnEnd) {
					discoveredCalls = append(discoveredCalls, target)
				}
			}
		}

		pc = nextPC
	}

	if len(insts) == 0 {
		return nil, nil, fmt.Errorf("no instructions decoded for function at 0x%x", entryAddr)
	}

	// Identify basic block leaders
	leaders := make(map[uint64]bool)
	leaders[entryAddr] = true

	for _, inst := range insts {
		nextPC := inst.Address + uint64(inst.Inst.Len)

		switch {
		case inst.Inst.Op == x86asm.JMP:
			if rel, ok := inst.Inst.Args[0].(x86asm.Rel); ok {
				target := uint64(int64(nextPC) + int64(rel))
				if target >= entryAddr && target < fnEnd {
					leaders[target] = true
				} else {
					// Tail call to another function
					discoveredCalls = append(discoveredCalls, target)
				}
			}
			if nextPC < fnEnd {
				leaders[nextPC] = true
			}

		case isJcc(inst.Inst.Op):
			if rel, ok := inst.Inst.Args[0].(x86asm.Rel); ok {
				target := uint64(int64(nextPC) + int64(rel))
				if target >= entryAddr && target < fnEnd {
					leaders[target] = true
				}
			}
			if nextPC < fnEnd {
				leaders[nextPC] = true
			}

		case inst.Inst.Op == x86asm.RET || inst.Inst.Op == x86asm.UD2:
			if nextPC < fnEnd {
				leaders[nextPC] = true
			}

		case inst.Inst.Op == x86asm.LEA:
			// Check if LEA references a jump table in .rodata
			if mem, ok := inst.Inst.Args[1].(x86asm.Mem); ok && mem.Base == x86asm.RIP {
				disp := int64(int32(mem.Disp))
				tableAddr := uint64(int64(nextPC) + disp)
				for _, target := range d.findJumpTableTargets(tableAddr, entryAddr, fnEnd) {
					leaders[target] = true
				}
			}
		}
	}

	// Also check if any known relocation target is inside this function
	for _, rel := range d.elf.Relocations {
		if rel.Addend > 0 {
			target := uint64(rel.Addend)
			if target >= entryAddr && target < fnEnd {
				leaders[target] = true
			}
		}
	}

	// Build basic blocks
	var currentBlock *BasicBlock
	for _, inst := range insts {
		if leaders[inst.Address] || currentBlock == nil {
			if currentBlock != nil && len(currentBlock.Insts) > 0 {
				currentBlock.EndAddr = inst.Address
				fn.Blocks[currentBlock.StartAddr] = currentBlock
			}
			currentBlock = &BasicBlock{
				StartAddr: inst.Address,
				Insts:     make([]Instruction, 0, 8),
			}
			currentBlock.Insts = append(currentBlock.Insts, inst)
		} else {
			currentBlock.Insts = append(currentBlock.Insts, inst)
		}
	}
	if currentBlock != nil && len(currentBlock.Insts) > 0 {
		lastInst := currentBlock.Insts[len(currentBlock.Insts)-1]
		currentBlock.EndAddr = lastInst.Address + uint64(lastInst.Inst.Len)
		fn.Blocks[currentBlock.StartAddr] = currentBlock
	}

	for addr := range fn.Blocks {
		fn.BlockOrder = append(fn.BlockOrder, addr)
	}
	sort.Slice(fn.BlockOrder, func(i, j int) bool {
		return fn.BlockOrder[i] < fn.BlockOrder[j]
	})

	d.analyzeBlockFlagLiveness(fn)
	return fn, discoveredCalls, nil
}

// findJumpTableTargets scans .rodata at tableAddr for 32-bit relative offsets pointing within [fnStart, fnEnd).
func (d *Disassembler) findJumpTableTargets(tableAddr uint64, fnStart, fnEnd uint64) []uint64 {
	rodataSec, ok := d.elf.Sections[".rodata"]
	if !ok {
		return nil
	}
	rodataStart := rodataSec.Addr
	rodataEnd := rodataSec.Addr + rodataSec.Size

	if tableAddr < rodataStart || tableAddr >= rodataEnd {
		return nil
	}

	var targets []uint64
	curr := tableAddr
	for curr+4 <= rodataEnd && curr+4 <= uint64(len(d.elf.MemoryImage)) {
		offset := int32(binary.LittleEndian.Uint32(d.elf.MemoryImage[curr : curr+4]))
		target := tableAddr + uint64(int64(offset))
		if target >= fnStart && target < fnEnd {
			targets = append(targets, target)
			curr += 4
		} else {
			break
		}
	}
	return targets
}

// disasmBranchFollowing disassembles a single function starting at entryAddr (fallback).
func (d *Disassembler) disasmBranchFollowing(entryAddr uint64) (*Function, []uint64, error) {
	symName := fmt.Sprintf("fn_%x", entryAddr)
	if sym, ok := d.elf.SymbolByAddr[entryAddr]; ok && sym.Name != "" {
		symName = sym.Name
	}

	fn := &Function{
		Name:      symName,
		EntryAddr: entryAddr,
		Blocks:    make(map[uint64]*BasicBlock),
	}

	var discoveredCalls []uint64
	blockQueue := []uint64{entryAddr}
	blockVisited := make(map[uint64]bool)
	blockVisited[entryAddr] = true

	// Also track instructions to detect block splits
	instAtAddr := make(map[uint64]Instruction)
	jumpTargets := make(map[uint64]bool)
	jumpTargets[entryAddr] = true

	// Step 1: Linear sweep along branches within the function
	for head := 0; head < len(blockQueue); head++ {
		blockStart := blockQueue[head]

		pc := blockStart

		for pc >= d.textStart && pc < d.textEnd {

			offset := pc
			if offset >= uint64(len(d.elf.MemoryImage)) {
				break
			}

			inst, err := x86asm.Decode(d.elf.MemoryImage[offset:], 64)
			if err != nil || inst.Len == 0 {
				break
			}

			wrapped := Instruction{
				Address: pc,
				Inst:    inst,
			}
			instAtAddr[pc] = wrapped

			nextPC := pc + uint64(inst.Len)

			// Check control flow changes
			isBranch := false
			isTerminal := false

			switch inst.Op {
			case x86asm.JMP:
				isBranch = true
				isTerminal = true
				if rel, ok := inst.Args[0].(x86asm.Rel); ok {
					target := uint64(int64(nextPC) + int64(rel))
					// Check if target is inside this function or a tail call
					// For now, if within reasonable function boundaries or before next symbol
					jumpTargets[target] = true
					if !blockVisited[target] && target >= d.textStart && target < d.textEnd {
						blockVisited[target] = true
						blockQueue = append(blockQueue, target)
					}
				}
			case x86asm.RET:
				isTerminal = true
			case x86asm.UD2:
				isTerminal = true
			case x86asm.CALL:
				// Call does not terminate the basic block; execution continues at nextPC.
				// Record call target as potential new function.
				if rel, ok := inst.Args[0].(x86asm.Rel); ok {
					target := uint64(int64(nextPC) + int64(rel))
					discoveredCalls = append(discoveredCalls, target)
				}
			case x86asm.LEA:
				if mem, ok := inst.Args[1].(x86asm.Mem); ok && mem.Base == x86asm.RIP {
					disp := int64(int32(mem.Disp))
					target := uint64(int64(nextPC) + disp)
					if target >= d.textStart && target < d.textEnd {
						discoveredCalls = append(discoveredCalls, target)
					}
				}
			default:
				// Conditional jumps (Jcc)
				if isJcc(inst.Op) {
					isBranch = true
					if rel, ok := inst.Args[0].(x86asm.Rel); ok {
						target := uint64(int64(nextPC) + int64(rel))
						jumpTargets[target] = true
						if !blockVisited[target] && target >= d.textStart && target < d.textEnd {
							blockVisited[target] = true
							blockQueue = append(blockQueue, target)
						}
					}
					// Fallthrough block
					jumpTargets[nextPC] = true
					if !blockVisited[nextPC] && nextPC >= d.textStart && nextPC < d.textEnd {
						blockVisited[nextPC] = true
						blockQueue = append(blockQueue, nextPC)
					}
				}
			}

			pc = nextPC
			if isTerminal || isBranch {
				break
			}
		}
	}

	if len(instAtAddr) == 0 {
		return nil, nil, fmt.Errorf("no instructions decoded for function at 0x%x", entryAddr)
	}

	// Identify basic block leaders
	leaders := make(map[uint64]bool)
	leaders[entryAddr] = true
	for target := range jumpTargets {
		if _, ok := instAtAddr[target]; ok {
			leaders[target] = true
		}
	}

	// Sort instruction addresses
	sortedAddrs := make([]uint64, 0, len(instAtAddr))
	for addr := range instAtAddr {
		sortedAddrs = append(sortedAddrs, addr)
	}
	sort.Slice(sortedAddrs, func(i, j int) bool {
		return sortedAddrs[i] < sortedAddrs[j]
	})

	// Build basic blocks
	var currentBlock *BasicBlock
	for _, addr := range sortedAddrs {
		inst := instAtAddr[addr]
		if leaders[addr] || currentBlock == nil {
			if currentBlock != nil && len(currentBlock.Insts) > 0 {
				currentBlock.EndAddr = addr
				fn.Blocks[currentBlock.StartAddr] = currentBlock
			}
			currentBlock = &BasicBlock{
				StartAddr: addr,
				Insts:     []Instruction{inst},
			}
		} else {
			currentBlock.Insts = append(currentBlock.Insts, inst)
		}
	}
	if currentBlock != nil && len(currentBlock.Insts) > 0 {
		lastInst := currentBlock.Insts[len(currentBlock.Insts)-1]
		currentBlock.EndAddr = lastInst.Address + uint64(lastInst.Inst.Len)
		fn.Blocks[currentBlock.StartAddr] = currentBlock
	}

	// Sort block addresses
	for addr := range fn.Blocks {
		fn.BlockOrder = append(fn.BlockOrder, addr)
	}
	sort.Slice(fn.BlockOrder, func(i, j int) bool {
		return fn.BlockOrder[i] < fn.BlockOrder[j]
	})

	d.analyzeBlockFlagLiveness(fn)
	return fn, discoveredCalls, nil
}

func (d *Disassembler) analyzeBlockFlagLiveness(fn *Function) {
	for _, block := range fn.Blocks {
		flagsLive := true // Conservatively assume flags are live at block boundary
		for i := len(block.Insts) - 1; i >= 0; i-- {
			inst := &block.Insts[i]
			op := inst.Inst.Op

			reads := usesFlags(op)
			definesAll := definesAllFlags(op)

			if definesAll {
				if !flagsLive {
					inst.SkipFlags = true
				}
				flagsLive = false
			}

			if reads {
				flagsLive = true
			}
		}
	}
}

func usesFlags(op x86asm.Op) bool {
	if isJcc(op) || isSetcc(op) || isCmovcc(op) {
		return true
	}
	switch op {
	case x86asm.ADC, x86asm.SBB:
		return true
	default:
		return false
	}
}

func definesAllFlags(op x86asm.Op) bool {
	switch op {
	case x86asm.ADD, x86asm.SUB, x86asm.CMP, x86asm.TEST,
		x86asm.AND, x86asm.OR, x86asm.XOR,
		x86asm.NEG, x86asm.SHL, x86asm.SHR, x86asm.SAR:
		return true
	default:
		return false
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

func isJcc(op x86asm.Op) bool {
	switch op {
	case x86asm.JA, x86asm.JAE, x86asm.JB, x86asm.JBE,
		x86asm.JCXZ, x86asm.JECXZ, x86asm.JRCXZ,
		x86asm.JE, x86asm.JG, x86asm.JGE,
		x86asm.JL, x86asm.JLE, x86asm.JNE,
		x86asm.JNO, x86asm.JNP, x86asm.JNS,
		x86asm.JO, x86asm.JP, x86asm.JS:
		return true
	default:
		return false
	}
}
