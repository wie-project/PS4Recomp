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

// CapHitInfo records diagnostic information about a function decode that hit safety caps.
type CapHitInfo struct {
	EntryAddr uint64
	Name      string
	Reason    string
	PC        uint64
	InstCount int
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

	// CapHits counts functions whose decode stopped on the safety cap
	// (maxBlockBytes / maxFnInsts) rather than on control flow or an unwind bound.
	CapHits int
	// CapHitDetails records diagnostic records for every function that hit a safety cap.
	CapHitDetails []CapHitInfo
	// PrivilegedStops counts user-mode-illegal opcodes that ended a block.
	// Those bytes are data decoded on the wrong boundary.
	PrivilegedStops int

	// pendingData holds jump-table bytes for the function currently being
	// decoded. The ranges are dropped with the function: keeping them would
	// hide a later entry on the second disassembly pass.
	pendingData []elfloader.AddrRange
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

// functionEnd is the exclusive upper bound for bytes that belong to the
// function at entry. A symbol size, an .eh_frame extent, and the next known
// function start each tighten it. The fallback is the end of executable memory.
func (d *Disassembler) functionEnd(entry, size uint64) uint64 {
	end := d.textEnd
	if size > 0 && entry+size < end {
		end = entry + size
	}
	if d.elf != nil {
		if ceil := d.elf.CodeCeiling(entry); ceil > entry && ceil < end {
			end = ceil
		}
	}
	n := len(d.funcSymAddrs)
	i, found := slices.BinarySearch(d.funcSymAddrs, entry)
	if found {
		i++
	}
	if i < n && d.funcSymAddrs[i] > entry && d.funcSymAddrs[i] < end {
		end = d.funcSymAddrs[i]
	}
	return end
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

// InCode reports whether addr is a guest instruction boundary the CFG may enter.
func (d *Disassembler) InCode(addr uint64) bool {
	return d.inCode(addr)
}

func (d *Disassembler) inCode(addr uint64) bool {
	if d == nil || d.elf == nil {
		return false
	}
	if addr >= uint64(len(d.elf.MemoryImage)) {
		return false
	}
	exec := addr >= d.textStart && addr < d.textEnd
	if len(d.elf.ExecRanges) > 0 {
		exec = d.elf.InExecutable(addr)
	}
	if !exec || d.elf.InData(addr) || pendingRangeHas(d.pendingData, addr) {
		return false
	}
	return true
}

func pendingRangeHas(rs []elfloader.AddrRange, addr uint64) bool {
	for _, r := range rs {
		if addr >= r.Start && addr < r.End {
			return true
		}
	}
	return false
}

// noteData records [start, end) as bytes the decoder must not enter.
// Extents that overlap an unwind function are left alone: those bytes are code.
func (d *Disassembler) noteData(start, end uint64) {
	if d == nil || end <= start || d.overlapsFunc(start, end) {
		return
	}
	d.pendingData = append(d.pendingData, elfloader.AddrRange{Start: start, End: end})
}

func (d *Disassembler) overlapsFunc(start, end uint64) bool {
	if d.elf == nil || len(d.elf.FuncBounds) == 0 {
		return false
	}
	b := d.elf.FuncBounds
	i, ok := slices.BinarySearchFunc(b, start, func(r elfloader.AddrRange, addr uint64) int {
		if addr >= r.End {
			return -1
		}
		if addr < r.Start {
			return 1
		}
		return 0
	})
	if ok {
		return true
	}
	return i < len(b) && b[i].Start < end
}

func (d *Disassembler) flushData() {
	if d == nil || len(d.pendingData) == 0 {
		return
	}
	d.pendingData = d.pendingData[:0]
}

// ReachableFunc is a compact index entry for a discovered function.
type ReachableFunc struct {
	Addr  uint64
	Insts int
}

// WalkReachable disassembles each reachable function and invokes visit, then
// drops the IR unless visit retains it. Call targets discovered in the
// function are enqueued. Decode errors skip that entry.
func (d *Disassembler) WalkReachable(entryAddrs []uint64, visit func(fn *Function) error) error {
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
		if visit != nil {
			if err := visit(fn); err != nil {
				return err
			}
		}

		for _, target := range newCalls {
			if !visited[target] && d.inCode(target) {
				visited[target] = true
				queue = append(queue, target)
			}
		}
	}
	d.flushData()
	return nil
}

// seedableFunc reports whether a RIP-relative address is worth queuing as a
// new function. With unwind extents, only real entries qualify. Otherwise the
// prologue heuristic is the filter.
func (d *Disassembler) seedableFunc(addr uint64) bool {
	if d == nil || d.elf == nil || !d.inCode(addr) {
		return false
	}
	if d.elf.InUnwindScope(addr) {
		return d.elf.IsFuncEntry(addr)
	}
	return looksLikeFuncStart(d.elf.MemoryImage, addr)
}

// AnalyzeReachable traverses and discovers all functions reachable from the given entry addresses.
func (d *Disassembler) AnalyzeReachable(entryAddrs []uint64) error {
	return d.WalkReachable(entryAddrs, func(fn *Function) error {
		d.Functions[fn.EntryAddr] = fn
		return nil
	})
}

// DiscoverReachable returns a compact address index of reachable functions
// without retaining their IR. Flag liveness is skipped during the walk.
func (d *Disassembler) DiscoverReachable(entryAddrs []uint64, progress func(nFn, nInst int)) ([]ReachableFunc, error) {
	prev := d.SkipFlagLiveness
	d.SkipFlagLiveness = true
	defer func() { d.SkipFlagLiveness = prev }()

	out := make([]ReachableFunc, 0, 1024)
	nInst := 0
	err := d.WalkReachable(entryAddrs, func(fn *Function) error {
		n := 0
		for _, b := range fn.Blocks {
			n += len(b.Insts)
		}
		out = append(out, ReachableFunc{Addr: fn.EntryAddr, Insts: n})
		nInst += n
		if progress != nil {
			progress(len(out), nInst)
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	slices.SortFunc(out, func(a, b ReachableFunc) int {
		if a.Addr < b.Addr {
			return -1
		}
		if a.Addr > b.Addr {
			return 1
		}
		return 0
	})
	return out, nil
}

// DisasmFunction disassembles a single function starting at entryAddr (exported for testing).
func (d *Disassembler) DisasmFunction(entryAddr uint64) (*Function, []uint64, error) {
	return d.disasmFunction(entryAddr)
}

// disasmFunction disassembles a single function starting at entryAddr.
func (d *Disassembler) disasmFunction(entryAddr uint64) (*Function, []uint64, error) {
	d.flushData()
	var size uint64
	if d.elf != nil {
		if sym, ok := d.elf.SymbolByAddr[entryAddr]; ok && sym.Size > 0 {
			size = sym.Size
		}
	}
	if size > 0 {
		end := d.functionEnd(entryAddr, size)
		if end > entryAddr {
			return d.disasmLinearFunction(entryAddr, end-entryAddr)
		}
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
				if (target < entryAddr || target >= fnEnd) && d.seedableFunc(target) {
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
				} else if d.inCode(target) {
					discoveredCalls = append(discoveredCalls, target)
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

	// Without a symbol size or an unwind extent, functionEnd is the whole RX
	// range. The caps stop a fallthrough into data from becoming one
	// multi-megabyte function. Real compiler blocks are far smaller.
	const maxBlockBytes = 8192
	const maxFnInsts = 32768
	fnEnd := d.functionEnd(entryAddr, 0)
	inside := func(addr uint64) bool {
		return addr >= entryAddr && addr < fnEnd && d.inCode(addr)
	}
	// A direct transfer past the function is a tail call, not another block.
	edge := func(target uint64) {
		if inside(target) {
			enqueueBlock(target)
			return
		}
		if target != 0 && d.inCode(target) {
			discoveredCalls = append(discoveredCalls, target)
		}
	}

	capped := false
	var capReason string
	var capPC uint64
	for head := 0; head < len(blockQueue); head++ {
		if len(instAtAddr) >= maxFnInsts {
			capped = true
			capReason = fmt.Sprintf("function instruction count (%d) >= cap (%d)", len(instAtAddr), maxFnInsts)
			capPC = blockQueue[head]
			break
		}
		blockStart := blockQueue[head]
		pc := blockStart

		for pc < fnEnd && d.inCode(pc) {
			if pc >= uint64(len(d.elf.MemoryImage)) {
				break
			}

			inst, err := x86asm.Decode(d.elf.MemoryImage[pc:], 64)
			if err != nil || inst.Len == 0 {
				break
			}
			if isPrivileged(inst.Op) {
				d.PrivilegedStops++
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

			isBranch := false
			isTerminal := false

			switch inst.Op {
			case x86asm.JMP:
				isBranch = true
				isTerminal = true
				if rel, ok := inst.Args[0].(x86asm.Rel); ok {
					edge(uint64(int64(nextPC) + int64(rel)))
				} else if sw, ok := matchPICSwitch(recent); ok {
					for _, target := range d.jumpTableTargets(sw, entryAddr, fnEnd) {
						edge(target)
					}
				}
			case x86asm.RET:
				isTerminal = true
			case x86asm.UD2:
				isTerminal = true
			case x86asm.CALL:
				// Call does not terminate the basic block; execution continues at nextPC.
				if rel, ok := inst.Args[0].(x86asm.Rel); ok {
					target := uint64(int64(nextPC) + int64(rel))
					discoveredCalls = append(discoveredCalls, target)
				}
			case x86asm.LEA:
				if mem, ok := inst.Args[1].(x86asm.Mem); ok && mem.Base == x86asm.RIP {
					disp := int64(int32(mem.Disp))
					target := uint64(int64(nextPC) + disp)
					if d.seedableFunc(target) {
						discoveredCalls = append(discoveredCalls, target)
					}
				}
			default:
				if isJcc(inst.Op) {
					isBranch = true
					if rel, ok := inst.Args[0].(x86asm.Rel); ok {
						edge(uint64(int64(nextPC) + int64(rel)))
					}
					if inside(nextPC) {
						enqueueBlock(nextPC)
					}
				}
			}

			pc = nextPC
			if isTerminal || isBranch {
				break
			}
			if pc-blockStart >= maxBlockBytes {
				capped = true
				capReason = fmt.Sprintf("basic block bytes (%d) >= cap (%d)", pc-blockStart, maxBlockBytes)
				capPC = pc
				break
			}
			if len(instAtAddr) >= maxFnInsts {
				capped = true
				capReason = fmt.Sprintf("function instruction count (%d) >= cap (%d)", len(instAtAddr), maxFnInsts)
				capPC = pc
				break
			}
		}
	}
	if capped {
		d.CapHits++
		d.CapHitDetails = append(d.CapHitDetails, CapHitInfo{
			EntryAddr: entryAddr,
			Name:      symName,
			Reason:    capReason,
			PC:        capPC,
			InstCount: len(instAtAddr),
		})
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

// isPrivileged reports opcodes that do not appear in Jaguar user-mode code.
// Decoding one means the PC is not an instruction boundary.
func isPrivileged(op x86asm.Op) bool {
	switch op {
	case x86asm.IN, x86asm.INSB, x86asm.INSW, x86asm.INSD,
		x86asm.OUT, x86asm.OUTSB, x86asm.OUTSW, x86asm.OUTSD,
		x86asm.CLI, x86asm.STI, x86asm.HLT,
		x86asm.IRET, x86asm.IRETD, x86asm.IRETQ,
		x86asm.LGDT, x86asm.CLTS, x86asm.WBINVD, x86asm.INVD, x86asm.INVLPG,
		x86asm.RDMSR, x86asm.WRMSR, x86asm.SWAPGS, x86asm.SYSRET, x86asm.SYSEXIT:
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
