package disasm

import (
	"debug/elf"
	"fmt"
	"slices"

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

	// SkipFlagLiveness omits dead-flag analysis. The opcode coverage analyzer
	// uses this; the recompiler leaves it false so SkipFlags stays accurate.
	SkipFlagLiveness bool

	// funcSymAddrs is the sorted unique set of STT_FUNC addresses, used to
	// bound jump-table recovery when a function has no symbol size.
	funcSymAddrs []uint64
}

// NewDisassembler creates a new disassembler for the given loaded ELF.
func NewDisassembler(loaded *elfloader.LoadedELF) (*Disassembler, error) {
	start, end := executableBounds(loaded)
	if start >= end {
		return nil, fmt.Errorf("ELF has no executable mapped segments")
	}

	return &Disassembler{
		elf:          loaded,
		textStart:    start,
		textEnd:      end,
		Functions:    make(map[uint64]*Function, 1024),
		Blocks:       make(map[uint64]*BasicBlock, 4096),
		funcSymAddrs: collectFuncSymAddrs(loaded),
	}, nil
}

func collectFuncSymAddrs(loaded *elfloader.LoadedELF) []uint64 {
	if loaded == nil || len(loaded.Symbols) == 0 {
		return nil
	}
	seen := make(map[uint64]struct{}, 256)
	addrs := make([]uint64, 0, 256)
	for _, s := range loaded.Symbols {
		if s.Type != elf.STT_FUNC || s.Address == 0 {
			continue
		}
		if _, ok := seen[s.Address]; ok {
			continue
		}
		seen[s.Address] = struct{}{}
		addrs = append(addrs, s.Address)
	}
	slices.Sort(addrs)
	return addrs
}

// functionEnd is the exclusive upper bound for intra-function jump-table
// targets: symbol size if present, otherwise the next STT_FUNC, otherwise
// the end of executable memory.
func (d *Disassembler) functionEnd(entry, size uint64) uint64 {
	if size > 0 {
		return entry + size
	}
	n := len(d.funcSymAddrs)
	i, found := slices.BinarySearch(d.funcSymAddrs, entry)
	if found {
		i++
	}
	if i < n && d.funcSymAddrs[i] > entry {
		return d.funcSymAddrs[i]
	}
	return d.textEnd
}

func executableBounds(loaded *elfloader.LoadedELF) (uint64, uint64) {
	if loaded == nil {
		return 0, 0
	}
	if len(loaded.ExecRanges) > 0 {
		start := loaded.ExecRanges[0].Start
		end := loaded.ExecRanges[0].End
		for _, r := range loaded.ExecRanges[1:] {
			if r.Start < start {
				start = r.Start
			}
			if r.End > end {
				end = r.End
			}
		}
		return start, end
	}
	if textSec, ok := loaded.Sections[".text"]; ok && textSec.Size > 0 {
		return textSec.Addr, textSec.Addr + textSec.Size
	}
	return 0, 0
}

func (d *Disassembler) inCode(addr uint64) bool {
	if d == nil || d.elf == nil {
		return false
	}
	if addr >= uint64(len(d.elf.MemoryImage)) {
		return false
	}
	if len(d.elf.ExecRanges) > 0 {
		return d.elf.InExecutable(addr)
	}
	return addr >= d.textStart && addr < d.textEnd
}

// AnalyzeReachable traverses and discovers all functions reachable from the given entry addresses.
func (d *Disassembler) AnalyzeReachable(entryAddrs []uint64) error {
	queue := make([]uint64, 0, len(entryAddrs)*2)
	visited := make(map[uint64]bool, len(entryAddrs)*2)

	for _, addr := range entryAddrs {
		if !visited[addr] && d.inCode(addr) {
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
			if !visited[target] && d.inCode(target) {
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
		if !d.inCode(pc) {
			break
		}

		inst, err := x86asm.Decode(d.elf.MemoryImage[pc:], 64)
		if err != nil || inst.Len == 0 {
			// Do not truncate the rest of the function on an unexpected byte or decode failure.
			// Insert a 1-byte UD2 placeholder so execution traps if reached, and continue
			// decoding subsequent instructions so that leaders, subsequent blocks, and
			// function epilogues (e.g. pop rbp; ret) are preserved.
			wrapped := Instruction{
				Address: pc,
				Inst: x86asm.Inst{
					Op:  x86asm.UD2,
					Len: 1,
				},
			}
			insts = append(insts, wrapped)
			pc++
			continue
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

		// Record function pointers loaded via LEA RIP+disp.
		// Jump tables also use LEA; only seed a call if the target looks like a prologue.
		if inst.Op == x86asm.LEA {
			if mem, ok := inst.Args[1].(x86asm.Mem); ok && mem.Base == x86asm.RIP {
				disp := int64(int32(mem.Disp))
				target := uint64(int64(nextPC) + disp)
				if d.inCode(target) && (target < entryAddr || target >= fnEnd) &&
					looksLikeFuncStart(d.elf.MemoryImage, target) {
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

	for i, inst := range insts {
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
			if mem, ok := inst.Inst.Args[1].(x86asm.Mem); ok && mem.Base == x86asm.RIP {
				disp := int64(int32(mem.Disp))
				tableAddr := uint64(int64(nextPC) + disp)
				for _, target := range d.findJumpTableTargets(tableAddr, entryAddr, fnEnd) {
					leaders[target] = true
				}
			}

		case isIndirectJump(inst.Inst):
			if sw, ok := matchPICSwitch(insts[:i+1]); ok {
				for _, target := range d.jumpTableTargets(sw, entryAddr, fnEnd) {
					if target >= entryAddr && target < fnEnd {
						leaders[target] = true
					} else {
						discoveredCalls = append(discoveredCalls, target)
					}
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
	slices.Sort(fn.BlockOrder)

	d.analyzeBlockFlagLiveness(fn)
	return fn, discoveredCalls, nil
}

// findJumpTableTargets reads a 32-bit PIC jump table at tableAddr. Entries are
// signed offsets from the table itself; recovery does not depend on section names
// (SELF/PRX images often have no section headers).
func (d *Disassembler) findJumpTableTargets(tableAddr uint64, fnStart, fnEnd uint64) []uint64 {
	return d.readPIC32Targets(tableAddr, tableAddr, 0, fnStart, fnEnd)
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

	const recentCap = 24
	recent := make([]Instruction, 0, recentCap)
	enqueueBlock := func(target uint64) {
		if target == 0 || !d.inCode(target) {
			return
		}
		jumpTargets[target] = true
		if !blockVisited[target] {
			blockVisited[target] = true
			blockQueue = append(blockQueue, target)
		}
	}

	// Step 1: Linear sweep along branches within the function
	for head := 0; head < len(blockQueue); head++ {
		blockStart := blockQueue[head]

		pc := blockStart

		for d.inCode(pc) {

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
			if len(recent) == recentCap {
				copy(recent, recent[1:])
				recent[recentCap-1] = wrapped
			} else {
				recent = append(recent, wrapped)
			}

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
					enqueueBlock(target)
				} else if sw, ok := matchPICSwitch(recent); ok {
					fnEnd := d.functionEnd(entryAddr, 0)
					for _, target := range d.jumpTableTargets(sw, entryAddr, fnEnd) {
						if target >= entryAddr && (fnEnd == 0 || target < fnEnd) {
							enqueueBlock(target)
						} else {
							discoveredCalls = append(discoveredCalls, target)
						}
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
					if d.inCode(target) && looksLikeFuncStart(d.elf.MemoryImage, target) {
						discoveredCalls = append(discoveredCalls, target)
					}
				}
			default:
				// Conditional jumps (Jcc)
				if isJcc(inst.Op) {
					isBranch = true
					if rel, ok := inst.Args[0].(x86asm.Rel); ok {
						enqueueBlock(uint64(int64(nextPC) + int64(rel)))
					}
					enqueueBlock(nextPC)
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
	slices.Sort(sortedAddrs)

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
	slices.Sort(fn.BlockOrder)

	d.analyzeBlockFlagLiveness(fn)
	return fn, discoveredCalls, nil
}

func (d *Disassembler) analyzeBlockFlagLiveness(fn *Function) {
	if d == nil || d.SkipFlagLiveness || fn == nil {
		return
	}
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
