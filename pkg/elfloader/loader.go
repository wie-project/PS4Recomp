package elfloader

import (
	"debug/elf"
	"encoding/binary"
	"fmt"
	"io"
	"os"
)

// Segment represents a loaded memory segment.
type Segment struct {
	Vaddr  uint64
	Memsz  uint64
	Filesz uint64
	Flags  elf.ProgFlag
	Data   []byte
}

// Relocation represents a dynamic or static relocation record.
type Relocation struct {
	Offset  uint64 // Virtual address of the location to patch
	Type    uint32 // Relocation type (e.g. R_X86_64_RELATIVE)
	SymIdx  uint32 // Symbol index in dynsym/symtab
	SymName string // Symbol name, if resolved
	Addend  int64  // Addend value
	PltAddr uint64 // Address in .plt section if R_X86_64_JUMP_SLOT
}

// Symbol represents an exported or local symbol.
type Symbol struct {
	Name    string
	Address uint64
	Size    uint64
	Type    elf.SymType
	Section elf.SectionIndex
}

// LoadedELF holds the parsed ELF metadata and initial memory layout.
type LoadedELF struct {
	EntryPoint   uint64
	Segments     []*Segment
	Sections     map[string]*elf.Section
	Symbols      []Symbol
	SymbolByAddr map[uint64]Symbol
	SymbolByName map[string]Symbol
	DynSymbols   []Symbol
	Relocations  []Relocation
	InitArray    []uint64

	// MinVAddr and MaxVAddr define the guest virtual address range.
	MinVAddr uint64
	MaxVAddr uint64

	// MemoryImage holds the pre-mapped guest memory image with R_X86_64_RELATIVE applied.
	MemoryImage []byte
}

// LoadELF reads and parses a 64-bit ELF binary.
func LoadELF(path string) (*LoadedELF, error) {
	file, err := elf.Open(path)
	if err != nil {
		return nil, fmt.Errorf("failed to open elf: %w", err)
	}
	defer func() { _ = file.Close() }()

	if file.Class != elf.ELFCLASS64 || file.Machine != elf.EM_X86_64 {
		return nil, fmt.Errorf("unsupported ELF: must be 64-bit x86-64")
	}

	rawFile, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("failed to open raw elf file: %w", err)
	}
	defer func() { _ = rawFile.Close() }()

	loaded := &LoadedELF{
		EntryPoint:   file.Entry,
		Sections:     make(map[string]*elf.Section),
		SymbolByAddr: make(map[uint64]Symbol),
		SymbolByName: make(map[string]Symbol),
		MinVAddr:     ^uint64(0),
		MaxVAddr:     0,
	}

	// 1. Process Program Headers (LOAD segments)
	for _, prog := range file.Progs {
		if prog.Type != elf.PT_LOAD || prog.Memsz == 0 {
			continue
		}

		segData := make([]byte, prog.Memsz)
		if prog.Filesz > 0 {
			if _, err := rawFile.Seek(int64(prog.Off), io.SeekStart); err != nil {
				return nil, fmt.Errorf("failed to seek segment at offset 0x%x: %w", prog.Off, err)
			}
			if _, err := io.ReadFull(rawFile, segData[:prog.Filesz]); err != nil {
				return nil, fmt.Errorf("failed to read segment data at 0x%x: %w", prog.Vaddr, err)
			}
		}

		seg := &Segment{
			Vaddr:  prog.Vaddr,
			Memsz:  prog.Memsz,
			Filesz: prog.Filesz,
			Flags:  prog.Flags,
			Data:   segData,
		}
		loaded.Segments = append(loaded.Segments, seg)

		if prog.Vaddr < loaded.MinVAddr {
			loaded.MinVAddr = prog.Vaddr
		}
		end := prog.Vaddr + prog.Memsz
		if end > loaded.MaxVAddr {
			loaded.MaxVAddr = end
		}
	}

	if len(loaded.Segments) == 0 {
		return nil, fmt.Errorf("no PT_LOAD segments found in ELF")
	}

	// Round memory image size up to 4096-byte page boundary
	pageSize := uint64(4096)
	totalMemSize := (loaded.MaxVAddr + pageSize - 1) &^ (pageSize - 1)
	loaded.MemoryImage = make([]byte, totalMemSize)

	// Copy segments into MemoryImage
	for _, seg := range loaded.Segments {
		copy(loaded.MemoryImage[seg.Vaddr:seg.Vaddr+seg.Filesz], seg.Data[:seg.Filesz])
	}

	// 2. Process Sections
	for _, sec := range file.Sections {
		loaded.Sections[sec.Name] = sec
	}

	// 3. Process Symbols (.symtab and .dynsym)
	syms, err := file.Symbols()
	if err == nil {
		for _, s := range syms {
			sym := Symbol{
				Name:    s.Name,
				Address: s.Value,
				Size:    s.Size,
				Type:    elf.ST_TYPE(s.Info),
				Section: s.Section,
			}
			loaded.Symbols = append(loaded.Symbols, sym)
			if sym.Name != "" {
				loaded.SymbolByName[sym.Name] = sym
			}
			if sym.Address != 0 {
				loaded.SymbolByAddr[sym.Address] = sym
			}
		}
	}

	dynSyms, err := file.DynamicSymbols()
	if err == nil {
		// debug/elf drops the STN_UNDEF (entry 0) symbol from DynamicSymbols().
		// Since ELF relocation entries store 1-based symbol indices (info >> 32),
		// we insert an empty Symbol{} at index 0 so that loaded.DynSymbols[symIdx] is 1-to-1.
		loaded.DynSymbols = append(loaded.DynSymbols, Symbol{})
		for _, s := range dynSyms {
			sym := Symbol{
				Name:    s.Name,
				Address: s.Value,
				Size:    s.Size,
				Type:    elf.ST_TYPE(s.Info),
				Section: s.Section,
			}
			loaded.DynSymbols = append(loaded.DynSymbols, sym)
		}
	}

	// 4. Process Relocations (.rela.dyn and .rela.plt)
	relSecNames := []string{".rela.dyn", ".rela.plt"}
	for _, name := range relSecNames {
		sec, ok := loaded.Sections[name]
		if !ok || sec.Size == 0 {
			continue
		}
		relData, err := sec.Data()
		if err != nil {
			return nil, fmt.Errorf("failed to read %s: %w", name, err)
		}

		// ELF64 Rela is 24 bytes:
		// Elf64_Addr   r_offset (8 bytes)
		// Elf64_Xword  r_info   (8 bytes: sym = info >> 32, type = info & 0xffffffff)
		// Elf64_Sxword r_addend (8 bytes)
		entrySize := 24
		for i := 0; i+entrySize <= len(relData); i += entrySize {
			offset := binary.LittleEndian.Uint64(relData[i : i+8])
			info := binary.LittleEndian.Uint64(relData[i+8 : i+16])
			addend := int64(binary.LittleEndian.Uint64(relData[i+16 : i+24]))

			relType := uint32(info & 0xffffffff)
			symIdx := uint32(info >> 32)

			var symName string
			if int(symIdx) < len(loaded.DynSymbols) {
				symName = loaded.DynSymbols[symIdx].Name
			}

			rel := Relocation{
				Offset:  offset,
				Type:    relType,
				SymIdx:  symIdx,
				SymName: symName,
				Addend:  addend,
			}

			// If this is in .rela.plt (R_X86_64_JUMP_SLOT), calculate corresponding PLT entry
			const R_X86_64_JUMP_SLOT = 7
			if relType == R_X86_64_JUMP_SLOT && name == ".rela.plt" {
				if pltSec, ok := loaded.Sections[".plt"]; ok {
					entryIdx := i / entrySize
					pltAddr := pltSec.Addr + 16*uint64(entryIdx+1)
					rel.PltAddr = pltAddr
					if offset+8 <= uint64(len(loaded.MemoryImage)) {
						binary.LittleEndian.PutUint64(loaded.MemoryImage[offset:offset+8], pltAddr)
					}
				}
			}

			// Apply R_X86_64_RELATIVE statically into memory image
			// R_X86_64_RELATIVE: *offset = B + A (where B is base address = 0 for default guest mapping)
			const R_X86_64_RELATIVE = 8
			if relType == R_X86_64_RELATIVE {
				if offset+8 <= uint64(len(loaded.MemoryImage)) {
					binary.LittleEndian.PutUint64(loaded.MemoryImage[offset:offset+8], uint64(addend))
				}
			}
			loaded.Relocations = append(loaded.Relocations, rel)
		}
	}

	// 5. Parse .init_array
	if initSec, ok := loaded.Sections[".init_array"]; ok && initSec.Size > 0 {
		initData, err := initSec.Data()
		if err == nil {
			for i := 0; i+8 <= len(initData); i += 8 {
				addr := binary.LittleEndian.Uint64(initData[i : i+8])
				// If relocation was applied, fetch from MemoryImage at initSec.Addr + i
				if initSec.Addr+uint64(i)+8 <= uint64(len(loaded.MemoryImage)) {
					addr = binary.LittleEndian.Uint64(loaded.MemoryImage[initSec.Addr+uint64(i) : initSec.Addr+uint64(i)+8])
				}
				if addr != 0 {
					loaded.InitArray = append(loaded.InitArray, addr)
				}
			}
		}
	}

	return loaded, nil
}
