package elfloader

import (
	"debug/elf"
	"encoding/binary"
	"fmt"
)

// TLS relocation types (System V x86-64 psABI).
const (
	R_X86_64_DTPMOD64 = 16
	R_X86_64_DTPOFF64 = 17
	R_X86_64_TPOFF64  = 18
)

// TLSModule is one module's PT_TLS template and its block in the process's static TLS area.
//
// FreeBSD's rtld (Orbis is FreeBSD 9) gives every object that has a PT_TLS segment the next TLS
// module index in load order, the executable first, and places the blocks of the objects loaded
// at startup in the static TLS area. x86-64 uses TLS variant II: a block starts at the thread
// pointer minus Offset. The executable's own accesses are fs-relative with the offset the static
// linker computed; the modules reach theirs through __tls_get_addr({ID, offset}), whose ID the
// R_X86_64_DTPMOD64 relocations supply.
type TLSModule struct {
	ID     uint64 // tls_index.module: 1, 2, ... in load order
	Name   string // file name, for diagnostics
	Vaddr  uint64 // .tdata template in the merged image
	Filesz uint64
	Memsz  uint64
	Align  uint64
	Offset uint64 // the block is at thread pointer - Offset
}

// AddTLSModule gives mod's PT_TLS template the next TLS module index of main's process and
// resolves mod's TLS relocations against its own TLS symbols (mod may be main itself):
// R_X86_64_DTPMOD64 gets the index, R_X86_64_DTPOFF64 gets S + A (the offset in the block),
// and R_X86_64_TPOFF64 (S + A - the block's offset below FS) is queued for ApplyTLSOffsets.
// Call it before MergeImages(main, mod), with mod at its final address; once all modules are
// added, call LayoutStaticTLS and then ApplyTLSOffsets. A module without PT_TLS gets no index.
// It returns one message per TLS relocation it cannot resolve: one naming another module's TLS
// symbol, any TLS relocation in a module without PT_TLS, or a slot outside the image.
func AddTLSModule(main, mod *LoadedELF) []string {
	if main == nil || mod == nil {
		return nil
	}
	var problems []string
	if mod.TLSMemsz == 0 {
		for _, rel := range mod.Relocations {
			if isTLSReloc(rel.Type) {
				problems = append(problems, fmt.Sprintf("%s: TLS relocation type %d at 0x%x in a module without PT_TLS (%q)",
					mod.FileName, rel.Type, rel.Offset, rel.SymName))
			}
		}
		return problems
	}
	id := uint64(len(main.TLSModules)) + 1
	main.TLSModules = append(main.TLSModules, TLSModule{
		ID:     id,
		Name:   mod.FileName,
		Vaddr:  mod.TLSVaddr,
		Filesz: mod.TLSFilesz,
		Memsz:  mod.TLSMemsz,
		Align:  mod.TLSAlign,
	})
	for _, rel := range mod.Relocations {
		if !isTLSReloc(rel.Type) {
			continue
		}
		if !ownTLSSymbol(mod, rel.SymIdx) {
			problems = append(problems, fmt.Sprintf("%s: TLS relocation type %d at 0x%x names %q, a TLS symbol of another module",
				mod.FileName, rel.Type, rel.Offset, rel.SymName))
			continue
		}
		if rel.Offset+8 > uint64(len(mod.MemoryImage)) {
			problems = append(problems, fmt.Sprintf("%s: TLS relocation type %d at 0x%x lies outside the image (0x%x bytes)",
				mod.FileName, rel.Type, rel.Offset, len(mod.MemoryImage)))
			continue
		}
		value := tlsSymbolValue(mod, rel.SymIdx) + uint64(rel.Addend)
		switch rel.Type {
		case R_X86_64_DTPMOD64:
			binary.LittleEndian.PutUint64(mod.MemoryImage[rel.Offset:rel.Offset+8], id)
		case R_X86_64_DTPOFF64:
			binary.LittleEndian.PutUint64(mod.MemoryImage[rel.Offset:rel.Offset+8], value)
		case R_X86_64_TPOFF64:
			main.tlsTPOFF = append(main.tlsTPOFF, pendingTPOFF{module: len(main.TLSModules) - 1, file: mod.FileName,
				offset: rel.Offset, value: value})
		}
	}
	return problems
}

// pendingTPOFF is an R_X86_64_TPOFF64 slot (in the merged image) waiting for its module's
// static TLS offset.
type pendingTPOFF struct {
	module int // index in TLSModules
	file   string
	offset uint64
	value  uint64 // S + A
}

// ApplyTLSOffsets writes the R_X86_64_TPOFF64 slots AddTLSModule queued, into main's merged
// image: S + A - Offset, the variable's address relative to FS (negative, variant II). Call it
// after LayoutStaticTLS(main.TLSModules) and after every MergeImages. It returns one message
// per slot it cannot write.
func ApplyTLSOffsets(main *LoadedELF) []string {
	if main == nil {
		return nil
	}
	var problems []string
	for _, p := range main.tlsTPOFF {
		m := main.TLSModules[p.module]
		switch {
		case m.Offset == 0:
			problems = append(problems, fmt.Sprintf("%s: R_X86_64_TPOFF64 at 0x%x: static TLS is not laid out", p.file, p.offset))
		case p.offset+8 > uint64(len(main.MemoryImage)):
			problems = append(problems, fmt.Sprintf("%s: R_X86_64_TPOFF64 at 0x%x lies outside the merged image (0x%x bytes)",
				p.file, p.offset, len(main.MemoryImage)))
		default:
			binary.LittleEndian.PutUint64(main.MemoryImage[p.offset:p.offset+8], p.value-m.Offset)
		}
	}
	main.tlsTPOFF = nil
	return problems
}

func isTLSReloc(t uint32) bool {
	return t == R_X86_64_DTPMOD64 || t == R_X86_64_DTPOFF64 || t == R_X86_64_TPOFF64
}

// tlsSymbolValue is S for a TLS relocation against the module's own symbol: 0 for no symbol or
// a section symbol, else the symbol's value (an offset in the module's TLS block).
func tlsSymbolValue(mod *LoadedELF, symIdx uint32) uint64 {
	if symIdx == 0 || int(symIdx) >= len(mod.DynSymbols) {
		return 0
	}
	sym := mod.DynSymbols[symIdx]
	if sym.Type == elf.STT_SECTION {
		return 0
	}
	return sym.Address
}

// ownTLSSymbol reports whether a TLS relocation's symbol lies in the relocated module
// itself: no symbol, a section symbol, or a defined symbol.
func ownTLSSymbol(mod *LoadedELF, symIdx uint32) bool {
	if symIdx == 0 || int(symIdx) >= len(mod.DynSymbols) {
		return symIdx == 0
	}
	sym := mod.DynSymbols[symIdx]
	return sym.Type == elf.STT_SECTION || sym.Section != elf.SHN_UNDEF || sym.Address != 0
}

// LayoutStaticTLS sets each module's Offset the way FreeBSD 9's rtld allocates static TLS for
// variant II: the first block at round(memsz, align), each next one at
// round(previous offset + memsz, align).
func LayoutStaticTLS(mods []TLSModule) {
	var offset uint64
	for i := range mods {
		align := mods[i].Align
		if align == 0 {
			align = 1
		}
		offset = (offset + mods[i].Memsz + align - 1) / align * align
		mods[i].Offset = offset
	}
}
