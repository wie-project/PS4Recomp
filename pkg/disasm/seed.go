package disasm

import (
	"debug/elf"
	"encoding/binary"

	"ps4-recomp/pkg/elfloader"

	"golang.org/x/arch/x86/x86asm"
)

// SeedEntryPoints collects CFG roots from the ELF entry point, .init_array,
// and every STT_FUNC symbol. When the image is stripped (few function symbols),
// it also recovers pointers from relocations and read-only data.
func SeedEntryPoints(loaded *elfloader.LoadedELF, allSymbols bool) []uint64 {
	if loaded == nil {
		return nil
	}
	seen := make(map[uint64]struct{}, 256)
	var entries []uint64
	add := func(addr uint64) {
		if addr == 0 || !loaded.InExecutable(addr) || loaded.InData(addr) {
			return
		}
		if _, ok := seen[addr]; ok {
			return
		}
		seen[addr] = struct{}{}
		entries = append(entries, addr)
	}
	// Pointer relocations name function entries. When the target module carries unwind
	// extents, the entry has to be one of those starts. A PC-relative reloc
	// addend is not a pointer and is ignored.
	addPtr := func(addr uint64) {
		if loaded.InUnwindScope(addr) {
			if loaded.IsFuncEntry(addr) {
				add(addr)
			}
			return
		}
		if !looksLikeFuncStart(loaded.MemoryImage, addr) {
			return
		}
		add(addr)
	}

	add(loaded.EntryPoint)
	for _, addr := range loaded.InitArray {
		add(addr)
	}
	if mainSym, ok := loaded.SymbolByName["main"]; ok {
		add(mainSym.Address)
	}

	funcSyms := 0
	for _, sym := range loaded.Symbols {
		if sym.Type != elf.STT_FUNC || sym.Address == 0 {
			continue
		}
		if allSymbols || loaded.InExecutable(sym.Address) {
			add(sym.Address)
			funcSyms++
		}
	}

	// Relocations name function pointers (vtables, init, callbacks) even when
	// the symbol table is complete: static functions have no STT_FUNC dynsym.
	// The applied slot is the pointer. After a module bias the reloc addend
	// itself is stale, and a PC32 addend was never a virtual address.
	for _, rel := range loaded.Relocations {
		switch rel.Type {
		case elfloader.R_X86_64_RELATIVE, elfloader.R_X86_64_64:
		default:
			continue
		}
		if rel.Offset+8 > uint64(len(loaded.MemoryImage)) {
			continue
		}
		val := binary.LittleEndian.Uint64(loaded.MemoryImage[rel.Offset : rel.Offset+8])
		addPtr(val)
	}

	// Dense RO-segment scan recovers function pointer tables (vtables, relro)
	// that are not in the dynamic symbol table or relocation table.

	for _, seg := range loaded.Segments {
		if seg.Flags&elf.PF_X != 0 {
			continue
		}
		// Function-pointer tables live in read-only data (vtables, relro).
		// Writable segments are too noisy: integers collide with code addresses.
		if seg.Flags&elf.PF_W != 0 {
			continue
		}
		if seg.Memsz < 8 || seg.Vaddr+8 > uint64(len(loaded.MemoryImage)) {
			continue
		}
		end := seg.Vaddr + seg.Memsz
		if end > uint64(len(loaded.MemoryImage)) {
			end = uint64(len(loaded.MemoryImage))
		}
		for addr := seg.Vaddr; addr+8 <= end; addr += 8 {
			val := binary.LittleEndian.Uint64(loaded.MemoryImage[addr : addr+8])
			if val&3 != 0 {
				continue
			}
			addPtr(val)
		}
	}

	return entries
}

func looksLikeFuncStart(img []byte, addr uint64) bool {
	if addr >= uint64(len(img)) || !prologueByte(img[addr]) {
		return false
	}
	inst, err := x86asm.Decode(img[addr:], 64)
	if err != nil || inst.Len == 0 || isPrivileged(inst.Op) {
		return false
	}
	return true
}

func prologueByte(b byte) bool {
	switch b {
	case 0x40, 0x41, 0x43, 0x44, 0x45, 0x48, 0x49, 0x4C, 0x4D, // REX prefixes
		0x53, 0x54, 0x55, 0x56, 0x57, // push
		0x89, 0x8B, // mov
		0xC3,             // ret
		0xE8, 0xE9, 0xEB, // call/jmp
		0xF3: // endbr64 / rep
		return true
	default:
		return false
	}
}
