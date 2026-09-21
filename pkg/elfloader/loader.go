package elfloader

import (
	"bytes"
	"debug/elf"
	"encoding/binary"
	"fmt"
	"os"
)

const (
	PT_SCE_DYNLIBDATA elf.ProgType = 0x61000000
	PT_SCE_RELRO      elf.ProgType = 0x61000010

	R_X86_64_JUMP_SLOT = 7
	R_X86_64_RELATIVE  = 8
	R_X86_64_GLOB_DAT  = 6
	R_X86_64_64        = 1

	DT_SCE_JMPREL   int64 = 0x61000029
	DT_SCE_PLTRELSZ int64 = 0x6100002d
	DT_SCE_RELA     int64 = 0x6100002f
	DT_SCE_RELASZ   int64 = 0x61000031
	DT_SCE_STRTAB   int64 = 0x61000035
	DT_SCE_STRSZ    int64 = 0x61000037
	DT_SCE_SYMTAB   int64 = 0x61000039
	DT_SCE_SYMENT   int64 = 0x6100003b
	DT_SCE_SYMTABSZ int64 = 0x6100003f

	DT_INIT = 12
	DT_FINI = 13
)

// AddrRange is a half-open [Start, End) guest virtual address interval.
type AddrRange struct {
	Start uint64
	End   uint64
}

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
	Offset  uint64
	Type    uint32
	SymIdx  uint32
	SymName string
	Addend  int64
	PltAddr uint64
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
	Path         string
	FileName     string
	EntryPoint   uint64
	Segments     []*Segment
	Sections     map[string]*elf.Section
	Symbols      []Symbol
	SymbolByAddr map[uint64]Symbol
	SymbolByName map[string]Symbol
	DynSymbols   []Symbol
	Relocations  []Relocation
	InitArray    []uint64
	ExecRanges   []AddrRange
	DynlibData   []byte

	MinVAddr uint64
	MaxVAddr uint64

	// MemoryImage holds the pre-mapped guest memory image with relative relocs applied.
	MemoryImage []byte
}

// LoadELF reads and parses a 64-bit ELF binary. PS4 SELF/FSELF containers are unwrapped first.
func LoadELF(path string) (*LoadedELF, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read '%s': %w", path, err)
	}
	if IsSELF(data) {
		data, err = ExtractELFFromSELF(data)
		if err != nil {
			return nil, fmt.Errorf("failed to extract ELF from SELF '%s': %w", path, err)
		}
	}
	loaded, err := LoadELFBytes(data)
	if err != nil {
		return nil, err
	}
	loaded.Path = path
	loaded.FileName = basename(path)
	return loaded, nil
}

// LoadELFBytes parses a 64-bit x86-64 ELF image from memory.
func LoadELFBytes(data []byte) (*LoadedELF, error) {
	file, err := elf.NewFile(bytes.NewReader(data))
	if err != nil {
		return nil, fmt.Errorf("failed to parse elf: %w", err)
	}
	defer func() { _ = file.Close() }()

	if file.Class != elf.ELFCLASS64 || file.Machine != elf.EM_X86_64 {
		return nil, fmt.Errorf("unsupported ELF: must be 64-bit x86-64")
	}

	loaded := &LoadedELF{
		EntryPoint:   file.Entry,
		Sections:     make(map[string]*elf.Section),
		SymbolByAddr: make(map[uint64]Symbol),
		SymbolByName: make(map[string]Symbol),
		MinVAddr:     ^uint64(0),
		MaxVAddr:     0,
	}

	type pendingSeg struct {
		prog *elf.Prog
		off  uint64
		fsz  uint64
	}
	var pending []pendingSeg

	for _, prog := range file.Progs {
		if prog.Type == PT_SCE_DYNLIBDATA && prog.Filesz > 0 && prog.Off+prog.Filesz <= uint64(len(data)) {
			loaded.DynlibData = data[prog.Off : prog.Off+prog.Filesz]
		}
		if !isMappedProg(prog.Type) || prog.Memsz == 0 {
			continue
		}
		fsz := prog.Filesz
		if prog.Off+fsz > uint64(len(data)) {
			if prog.Off >= uint64(len(data)) {
				fsz = 0
			} else {
				fsz = uint64(len(data)) - prog.Off
			}
		}
		pending = append(pending, pendingSeg{prog: prog, off: prog.Off, fsz: fsz})
		if prog.Vaddr < loaded.MinVAddr {
			loaded.MinVAddr = prog.Vaddr
		}
		end := prog.Vaddr + prog.Memsz
		if end > loaded.MaxVAddr {
			loaded.MaxVAddr = end
		}
		if prog.Flags&elf.PF_X != 0 {
			loaded.ExecRanges = append(loaded.ExecRanges, AddrRange{Start: prog.Vaddr, End: end})
		}
	}

	if len(pending) == 0 {
		return nil, fmt.Errorf("no PT_LOAD segments found in ELF")
	}

	pageSize := uint64(4096)
	totalMemSize := (loaded.MaxVAddr + pageSize - 1) &^ (pageSize - 1)
	loaded.MemoryImage = make([]byte, totalMemSize)

	for _, p := range pending {
		if p.fsz > 0 {
			end := p.prog.Vaddr + p.fsz
			if end > uint64(len(loaded.MemoryImage)) {
				return nil, fmt.Errorf("segment at 0x%x exceeds memory image", p.prog.Vaddr)
			}
			copy(loaded.MemoryImage[p.prog.Vaddr:end], data[p.off:p.off+p.fsz])
		}
		seg := &Segment{
			Vaddr:  p.prog.Vaddr,
			Memsz:  p.prog.Memsz,
			Filesz: p.fsz,
			Flags:  p.prog.Flags,
		}
		memEnd := p.prog.Vaddr + p.prog.Memsz
		if memEnd > uint64(len(loaded.MemoryImage)) {
			memEnd = uint64(len(loaded.MemoryImage))
		}
		seg.Data = loaded.MemoryImage[p.prog.Vaddr:memEnd]
		loaded.Segments = append(loaded.Segments, seg)
	}

	for _, sec := range file.Sections {
		loaded.Sections[sec.Name] = sec
	}

	if syms, err := file.Symbols(); err == nil {
		for _, s := range syms {
			addSymbol(loaded, Symbol{
				Name:    s.Name,
				Address: s.Value,
				Size:    s.Size,
				Type:    elf.ST_TYPE(s.Info),
				Section: s.Section,
			})
		}
	}

	if dynSyms, err := file.DynamicSymbols(); err == nil {
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
			if sym.Address != 0 {
				addSymbol(loaded, sym)
			}
		}
	}

	parseSCEDynamic(loaded, data, file)
	parseRelocations(loaded, file)
	parseInitArray(loaded)

	if len(loaded.ExecRanges) == 0 {
		for _, seg := range loaded.Segments {
			if seg.Flags&elf.PF_X != 0 {
				loaded.ExecRanges = append(loaded.ExecRanges, AddrRange{Start: seg.Vaddr, End: seg.Vaddr + seg.Memsz})
			}
		}
	}
	return loaded, nil
}

func isMappedProg(t elf.ProgType) bool {
	return t == elf.PT_LOAD || t == PT_SCE_RELRO
}

func addSymbol(loaded *LoadedELF, sym Symbol) {
	loaded.Symbols = append(loaded.Symbols, sym)
	if sym.Name != "" {
		if existing, ok := loaded.SymbolByName[sym.Name]; !ok || existing.Address == 0 {
			loaded.SymbolByName[sym.Name] = sym
		}
	}
	if sym.Address != 0 {
		if existing, ok := loaded.SymbolByAddr[sym.Address]; !ok || existing.Name == "" {
			loaded.SymbolByAddr[sym.Address] = sym
		}
	}
}

func parseRelocations(loaded *LoadedELF, file *elf.File) {
	relSecNames := []string{".rela.dyn", ".rela.plt"}
	for _, name := range relSecNames {
		sec, ok := loaded.Sections[name]
		if !ok || sec.Size == 0 {
			continue
		}
		relData, err := sec.Data()
		if err != nil {
			continue
		}
		applyRelaTable(loaded, relData, name == ".rela.plt")
	}

	if pltSec, ok := loaded.Sections[".plt"]; ok {
		entryIdx := 0
		for i := range loaded.Relocations {
			rel := &loaded.Relocations[i]
			if rel.Type == R_X86_64_JUMP_SLOT && rel.PltAddr == 0 {
				rel.PltAddr = pltSec.Addr + 16*uint64(entryIdx+1)
				entryIdx++
				if rel.Offset+8 <= uint64(len(loaded.MemoryImage)) {
					binary.LittleEndian.PutUint64(loaded.MemoryImage[rel.Offset:rel.Offset+8], rel.PltAddr)
				}
			}
		}
	}
}

func applyRelaTable(loaded *LoadedELF, relData []byte, isPlt bool) {
	const entrySize = 24
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

		if relType == R_X86_64_JUMP_SLOT && isPlt {
			if pltSec, ok := loaded.Sections[".plt"]; ok {
				entryIdx := i / entrySize
				rel.PltAddr = pltSec.Addr + 16*uint64(entryIdx+1)
				if offset+8 <= uint64(len(loaded.MemoryImage)) {
					binary.LittleEndian.PutUint64(loaded.MemoryImage[offset:offset+8], rel.PltAddr)
				}
			}
		}

		if relType == R_X86_64_RELATIVE {
			if offset+8 <= uint64(len(loaded.MemoryImage)) {
				binary.LittleEndian.PutUint64(loaded.MemoryImage[offset:offset+8], uint64(addend))
			}
		}

		if relType == R_X86_64_GLOB_DAT && symName == "__stack_chk_guard" {
			applyStackCanary(loaded, offset)
		}

		if relType == R_X86_64_64 && int(symIdx) < len(loaded.DynSymbols) {
			sym := loaded.DynSymbols[symIdx]
			if sym.Address != 0 && offset+8 <= uint64(len(loaded.MemoryImage)) {
				binary.LittleEndian.PutUint64(loaded.MemoryImage[offset:offset+8], sym.Address+uint64(addend))
			}
		}

		loaded.Relocations = append(loaded.Relocations, rel)
	}
}

func applyStackCanary(loaded *LoadedELF, gotOffset uint64) {
	guardAddr := (loaded.MaxVAddr + 4095) &^ 4095
	if uint64(len(loaded.MemoryImage)) < guardAddr+4096 {
		newImg := make([]byte, guardAddr+4096)
		copy(newImg, loaded.MemoryImage)
		loaded.MemoryImage = newImg
		loaded.MaxVAddr = guardAddr + 4096
		for _, seg := range loaded.Segments {
			end := seg.Vaddr + seg.Memsz
			if end > uint64(len(loaded.MemoryImage)) {
				end = uint64(len(loaded.MemoryImage))
			}
			if seg.Vaddr < end {
				seg.Data = loaded.MemoryImage[seg.Vaddr:end]
			}
		}
	}
	const canaryValue = uint64(0x595e9fbd94fda766)
	binary.LittleEndian.PutUint64(loaded.MemoryImage[guardAddr:guardAddr+8], canaryValue)
	if gotOffset+8 <= uint64(len(loaded.MemoryImage)) {
		binary.LittleEndian.PutUint64(loaded.MemoryImage[gotOffset:gotOffset+8], guardAddr)
	}
}

func parseInitArray(loaded *LoadedELF) {
	if initSec, ok := loaded.Sections[".init_array"]; ok && initSec.Size > 0 {
		for i := uint64(0); i+8 <= initSec.Size; i += 8 {
			addr := initSec.Addr + i
			if addr+8 <= uint64(len(loaded.MemoryImage)) {
				fn := binary.LittleEndian.Uint64(loaded.MemoryImage[addr : addr+8])
				if fn != 0 {
					loaded.InitArray = append(loaded.InitArray, fn)
				}
			}
		}
		return
	}
	// Fall back to scanning mapped pointers later in SeedEntryPoints.
}

func parseSCEDynamic(loaded *LoadedELF, data []byte, file *elf.File) {
	var dynOff, dynSz uint64
	for _, prog := range file.Progs {
		if prog.Type == elf.PT_DYNAMIC {
			dynOff = prog.Off
			dynSz = prog.Filesz
			break
		}
	}
	if dynSz < 16 || dynOff+dynSz > uint64(len(data)) {
		return
	}
	dyn := data[dynOff : dynOff+dynSz]

	tags := make(map[int64]uint64, 32)
	for i := 0; i+16 <= len(dyn); i += 16 {
		tag := int64(binary.LittleEndian.Uint64(dyn[i : i+8]))
		val := binary.LittleEndian.Uint64(dyn[i+8 : i+16])
		if tag == 0 {
			break
		}
		tags[tag] = val
	}

	base := loaded.DynlibData
	if len(base) == 0 {
		return
	}

	strtabOff, strtabSz := tags[DT_SCE_STRTAB], tags[DT_SCE_STRSZ]
	symtabOff, symtabSz := tags[DT_SCE_SYMTAB], tags[DT_SCE_SYMTABSZ]
	syment := tags[DT_SCE_SYMENT]
	if syment == 0 {
		syment = 24
	}
	if strtabOff >= uint64(len(base)) || symtabOff >= uint64(len(base)) {
		return
	}

	strtab := base[strtabOff:]
	if strtabSz > 0 && strtabOff+strtabSz <= uint64(len(base)) {
		strtab = base[strtabOff : strtabOff+strtabSz]
	}
	symBytes := base[symtabOff:]
	if symtabSz > 0 && symtabOff+symtabSz <= uint64(len(base)) {
		symBytes = base[symtabOff : symtabOff+symtabSz]
	}

	// SCE SYMTAB already includes the STN_UNDEF entry at index 0. Prepending
	// another dummy shifts every JUMP_SLOT onto the previous import.
	if len(loaded.DynSymbols) == 0 {
		for i := 0; i+int(syment) <= len(symBytes); i += int(syment) {
			raw := symBytes[i : i+int(syment)]
			nameOff := binary.LittleEndian.Uint32(raw[0:4])
			info := raw[4]
			shndx := binary.LittleEndian.Uint16(raw[6:8])
			value := binary.LittleEndian.Uint64(raw[8:16])
			size := binary.LittleEndian.Uint64(raw[16:24])
			name := cstringAt(strtab, int(nameOff))
			sym := Symbol{
				Name:    name,
				Address: value,
				Size:    size,
				Type:    elf.ST_TYPE(info),
				Section: elf.SectionIndex(shndx),
			}
			loaded.DynSymbols = append(loaded.DynSymbols, sym)
			if value != 0 && name != "" {
				addSymbol(loaded, sym)
			}
		}
	}

	_, hasRelaDyn := loaded.Sections[".rela.dyn"]
	_, hasRelaPlt := loaded.Sections[".rela.plt"]
	if !hasRelaDyn {
		if relaOff, ok := tags[DT_SCE_RELA]; ok {
			relaSz := tags[DT_SCE_RELASZ]
			if relaOff < uint64(len(base)) {
				end := relaOff + relaSz
				if relaSz == 0 || end > uint64(len(base)) {
					end = uint64(len(base))
				}
				applyRelaTable(loaded, base[relaOff:end], false)
			}
		}
	}
	if initAddr, ok := tags[DT_INIT]; ok && initAddr != 0 {
		loaded.InitArray = append(loaded.InitArray, initAddr)
	}

	if !hasRelaPlt {
		if jmpOff, ok := tags[DT_SCE_JMPREL]; ok {
			jmpSz := tags[DT_SCE_PLTRELSZ]
			if jmpOff < uint64(len(base)) {
				end := jmpOff + jmpSz
				if jmpSz == 0 || end > uint64(len(base)) {
					end = uint64(len(base))
				}
				applyRelaTable(loaded, base[jmpOff:end], true)
			}
		}
	}
}

func cstringAt(buf []byte, off int) string {
	if off < 0 || off >= len(buf) {
		return ""
	}
	end := off
	for end < len(buf) && buf[end] != 0 {
		end++
	}
	return string(buf[off:end])
}

func basename(path string) string {
	for i := len(path) - 1; i >= 0; i-- {
		if path[i] == '/' || path[i] == '\\' {
			return path[i+1:]
		}
	}
	return path
}

// InExecutable reports whether addr falls in an executable mapped range.
func (l *LoadedELF) InExecutable(addr uint64) bool {
	if l == nil {
		return false
	}
	for _, r := range l.ExecRanges {
		if addr >= r.Start && addr < r.End {
			return true
		}
	}
	return false
}

// ContainsVA reports whether addr is inside the loaded memory image.
func (l *LoadedELF) ContainsVA(addr uint64) bool {
	return l != nil && addr < uint64(len(l.MemoryImage))
}
