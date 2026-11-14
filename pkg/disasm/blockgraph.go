package disasm

import "golang.org/x/arch/x86/x86asm"

// blockGraph is the decoded part of a function as basic blocks (insts, by start address) and
// their predecessors, for the backward searches of jump-table recovery that leave the block
// of the indirect jmp (start): the table address, the bounds check and the index source.
type blockGraph struct {
	preds map[uint64][]uint64
	insts map[uint64][]Instruction
}

// tableAddr looks for the RIP-relative LEA that produced reg in the predecessor blocks,
// following plain 64-bit register copies.
func (g *blockGraph) tableAddr(start uint64, reg x86asm.Reg) (uint64, bool) {
	type item struct {
		block uint64
		reg   x86asm.Reg
	}
	visited := make(map[item]bool)
	var queue []item
	for _, p := range g.preds[start] {
		it := item{p, reg}
		if !visited[it] {
			visited[it] = true
			queue = append(queue, it)
		}
	}
	depth := 0
	for len(queue) > 0 && depth < 256 {
		depth++
		var nextQueue []item
		for _, it := range queue {
			insts := g.insts[it.block]
			cur := it.reg
			killed := false
			for i := len(insts) - 1; i >= 0; i-- {
				dst, target, isLEA := leaRIPTarget(insts[i])
				if isLEA && sameGPR(dst, cur) {
					return target, true
				}
				if insts[i].Inst.Op == x86asm.MOV {
					d, ok0 := insts[i].Inst.Args[0].(x86asm.Reg)
					s, ok1 := insts[i].Inst.Args[1].(x86asm.Reg)
					if ok0 && ok1 && sameGPR(d, cur) && d >= x86asm.RAX && d <= x86asm.R15 {
						cur = s
						continue
					}
				}
				if modifiesReg(insts[i].Inst, cur) {
					killed = true
					break
				}
			}
			if !killed {
				for _, p := range g.preds[it.block] {
					nit := item{p, cur}
					if !visited[nit] {
						visited[nit] = true
						nextQueue = append(nextQueue, nit)
					}
				}
			}
		}
		queue = nextQueue
	}
	return 0, false
}

// count looks for the bounds check of the index (idxReg: the register
// it is in at this block's start) in the predecessor blocks. It must bound
// the index on every path to this jmp: the count is the largest one found,
// and a path that reaches the function entry, a write of the index or the
// search depth without a check gives none. "Every path" means every
// predecessor decoded so far: a predecessor found after the table is
// resolved is not checked again. Register copies (`mov idx, src`) are
// followed into the predecessors with the register they came from. A
// check that ends a block bounds the index only on one of its edges (ja:
// the fall-through, jb: the taken edge); on the other edge the index is
// out of that range and may still reach this jmp (a second switch on the
// same index), so the search goes on past it there.
func (g *blockGraph) count(start uint64, idxReg x86asm.Reg) (int, bool) {
	type edgeTo struct {
		pred, child uint64
		reg         x86asm.Reg
	}
	visited := make(map[edgeTo]bool)
	var queue []edgeTo
	for _, p := range g.preds[start] {
		e := edgeTo{p, start, idxReg}
		if !visited[e] {
			visited[e] = true
			queue = append(queue, e)
		}
	}
	if len(queue) == 0 {
		return 0, false
	}
	found := 0
	for depth := 0; len(queue) > 0 && depth < 256; depth++ {
		var nextQueue []edgeTo
		for _, e := range queue {
			insts := g.insts[e.pred]
			if n := len(insts); n > 0 && !boundHoldsOnEdge(insts[n-1], e.child) {
				insts = insts[:n-1]
			}
			c, killed, from := switchTableCountKill(insts, e.reg)
			if c > 0 {
				found = max(found, c)
				continue
			}
			if killed {
				return 0, false
			}
			preds := g.preds[e.pred]
			if len(preds) == 0 {
				return 0, false
			}
			for _, p := range preds {
				ne := edgeTo{p, e.pred, from}
				if !visited[ne] {
					visited[ne] = true
					nextQueue = append(nextQueue, ne)
				}
			}
		}
		queue = nextQueue
	}
	if len(queue) > 0 || found == 0 {
		return 0, false
	}
	return found, true
}

// indexCap follows the index register back through the predecessors to its
// source; a cap needs every path to end in a byte load (byteIndexCap).
func (g *blockGraph) indexCap(start uint64, idxReg x86asm.Reg) int {
	type item struct {
		block uint64
		reg   x86asm.Reg
	}
	visited := make(map[uint64]bool)
	var queue []item
	for _, p := range g.preds[start] {
		if !visited[p] {
			visited[p] = true
			queue = append(queue, item{p, idxReg})
		}
	}
	found := 0
	for depth := 0; len(queue) > 0 && depth < 256; depth++ {
		var next []item
		for _, it := range queue {
			c, from, killed := byteIndexCapKill(g.insts[it.block], it.reg)
			if killed {
				return 0
			}
			if c > 0 {
				found = c
				continue
			}
			preds := g.preds[it.block]
			if len(preds) == 0 {
				return 0
			}
			for _, p := range preds {
				if !visited[p] {
					visited[p] = true
					next = append(next, item{p, from})
				}
			}
		}
		queue = next
	}
	if len(queue) > 0 {
		return 0
	}
	return found
}
