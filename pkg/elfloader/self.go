package elfloader

import (
	"bytes"
	"debug/elf"
	"encoding/binary"
	"fmt"
)

// SELFMagic is the 4-byte little-endian magic 0x1D3D154F identifying a PS4 SELF/FSELF.
var SELFMagic = [4]byte{0x4F, 0x15, 0x3D, 0x1D}

const (
	selfHeaderSize = 0x20
	selfEntrySize  = 0x20

	selfPropHasBlocks     = 11
	selfPropSegmentShift  = 20
	selfPropSegmentMask   = 0xFFFF
)

// IsSELF reports whether data starts with a PS4 SELF header.
func IsSELF(data []byte) bool {
	return len(data) >= 4 && bytes.Equal(data[:4], SELFMagic[:])
}

type selfEntry struct {
	Props      uint64
	Offset     uint64
	FileSize   uint64
	MemorySize uint64
}

func (e selfEntry) hasBlocks() bool {
	return (e.Props>>selfPropHasBlocks)&1 == 1
}

func (e selfEntry) segmentIndex() uint64 {
	return (e.Props >> selfPropSegmentShift) & selfPropSegmentMask
}

// ExtractELFFromSELF reconstructs a loadable ELF image from a PS4 SELF/FSELF
// container using the SELF entry table and embedded ELF program headers.
func ExtractELFFromSELF(data []byte) ([]byte, error) {
	if !IsSELF(data) {
		return data, nil
	}
	if len(data) < selfHeaderSize {
		return nil, fmt.Errorf("SELF header too short")
	}

	numEntries := int(binary.LittleEndian.Uint16(data[0x18:]))
	if numEntries < 0 || numEntries > 4096 {
		return nil, fmt.Errorf("invalid SELF entry count %d", numEntries)
	}
	entryOff := selfHeaderSize
	if entryOff+numEntries*selfEntrySize > len(data) {
		return nil, fmt.Errorf("SELF entry table is truncated")
	}

	entries := make([]selfEntry, numEntries)
	for i := range entries {
		off := entryOff + i*selfEntrySize
		entries[i] = selfEntry{
			Props:      binary.LittleEndian.Uint64(data[off:]),
			Offset:     binary.LittleEndian.Uint64(data[off+8:]),
			FileSize:   binary.LittleEndian.Uint64(data[off+16:]),
			MemorySize: binary.LittleEndian.Uint64(data[off+24:]),
		}
	}

	elfOff := entryOff + numEntries*selfEntrySize
	elfOff = int((uint64(elfOff) + 0xF) &^ 0xF)
	if elfOff+64 > len(data) || data[elfOff] != 0x7f || string(data[elfOff+1:elfOff+4]) != "ELF" {
		idx := bytes.Index(data[elfOff:], []byte{0x7f, 'E', 'L', 'F'})
		if idx < 0 {
			idx = bytes.Index(data, []byte{0x7f, 'E', 'L', 'F'})
			if idx <= 0 {
				return nil, fmt.Errorf("SELF container does not contain an embedded ELF")
			}
			elfOff = idx
		} else {
			elfOff += idx
		}
	}
	if elfOff+64 > len(data) {
		return nil, fmt.Errorf("embedded ELF header is truncated")
	}

	ehdr := data[elfOff : elfOff+64]
	if ehdr[4] != 2 { // ELFCLASS64
		return nil, fmt.Errorf("embedded ELF is not 64-bit")
	}
	phoff := binary.LittleEndian.Uint64(ehdr[32:])
	phentsize := binary.LittleEndian.Uint16(ehdr[54:])
	phnum := binary.LittleEndian.Uint16(ehdr[56:])
	if phentsize < 0x38 || phnum == 0 {
		return nil, fmt.Errorf("embedded ELF has no program headers")
	}
	phdrStart := elfOff + int(phoff)
	phdrEnd := phdrStart + int(phnum)*int(phentsize)
	if phoff > uint64(len(data)) || phdrEnd > len(data) {
		return nil, fmt.Errorf("embedded ELF program headers are truncated")
	}

	outSize := uint64(phdrEnd - elfOff)
	type phdrView struct {
		index  int
		typ    uint32
		offset uint64
		filesz uint64
	}
	phdrs := make([]phdrView, 0, phnum)
	for i := 0; i < int(phnum); i++ {
		p := data[phdrStart+i*int(phentsize):]
		pv := phdrView{
			index:  i,
			typ:    binary.LittleEndian.Uint32(p[0:4]),
			offset: binary.LittleEndian.Uint64(p[8:16]),
			filesz: binary.LittleEndian.Uint64(p[32:40]),
		}
		phdrs = append(phdrs, pv)
		if end := pv.offset + pv.filesz; end > outSize {
			outSize = end
		}
	}
	if outSize < 64 {
		outSize = 64
	}

	out := make([]byte, outSize)
	copy(out[0:64], ehdr)
	if phoff+uint64(int(phnum)*int(phentsize)) <= outSize {
		copy(out[phoff:], data[phdrStart:phdrEnd])
	}

	// Drop section headers: FSELF does not store a complete section table.
	binary.LittleEndian.PutUint64(out[40:], 0)
	binary.LittleEndian.PutUint16(out[60:], 0)
	binary.LittleEndian.PutUint16(out[62:], 0)

	copied := 0
	for _, ph := range phdrs {
		if ph.filesz == 0 || ph.offset >= outSize {
			continue
		}
		n := ph.filesz
		if ph.offset+n > outSize {
			n = outSize - ph.offset
		}
		src := findSELFSegment(data, entries, ph.index, ph.filesz)
		if src == nil {
			srcOff := elfOff + int(ph.offset)
			if srcOff >= 0 && srcOff < len(data) {
				avail := uint64(len(data) - srcOff)
				if n > avail {
					n = avail
				}
				src = data[srcOff : srcOff+int(n)]
			}
		}
		if src == nil {
			continue
		}
		if uint64(len(src)) < n {
			n = uint64(len(src))
		}
		copy(out[ph.offset:ph.offset+n], src[:n])
		copied++
	}
	if copied == 0 {
		return nil, fmt.Errorf("SELF contained no loadable segment data")
	}

	if !bytes.HasPrefix(out, []byte{0x7f, 'E', 'L', 'F'}) {
		return nil, fmt.Errorf("reconstructed SELF image is not an ELF")
	}
	if elf.Machine(binary.LittleEndian.Uint16(out[18:])) != elf.EM_X86_64 {
		return nil, fmt.Errorf("reconstructed SELF image is not x86-64")
	}
	return out, nil
}

func findSELFSegment(data []byte, entries []selfEntry, phdrIndex int, filesz uint64) []byte {
	for _, e := range entries {
		if !e.hasBlocks() || int(e.segmentIndex()) != phdrIndex {
			continue
		}
		if e.Offset >= uint64(len(data)) || e.FileSize == 0 {
			continue
		}
		end := e.Offset + e.FileSize
		if end > uint64(len(data)) {
			end = uint64(len(data))
		}
		seg := data[e.Offset:end]
		if filesz > 0 && uint64(len(seg)) > filesz {
			seg = seg[:filesz]
		}
		return seg
	}
	return nil
}
