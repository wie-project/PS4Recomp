package elfloader

import (
	"debug/elf"
	"slices"
)

// DWARF pointer encodings used by .eh_frame and .eh_frame_hdr.
const (
	dwEHPEOmit     = 0xff
	dwEHPEAbsptr   = 0x00
	dwEHPEUleb     = 0x01
	dwEHPEUdata2   = 0x02
	dwEHPEUdata4   = 0x03
	dwEHPEUdata8   = 0x04
	dwEHPESleb     = 0x09
	dwEHPESdata2   = 0x0a
	dwEHPESdata4   = 0x0b
	dwEHPESdata8   = 0x0c
	dwEHPEpcrel    = 0x10
	dwEHPEdatarel  = 0x30
	dwEHPEIndirect = 0x80
)

// attachUnwind fills FuncBounds and DataRanges from PT_GNU_EH_FRAME.
// A missing or unreadable table leaves the image without bounds; disassembly
// then keeps its previous caps. Load itself does not fail.
func attachUnwind(l *LoadedELF, file *elf.File) {
	if l == nil || file == nil || len(l.MemoryImage) == 0 {
		return
	}
	for _, prog := range file.Progs {
		if prog.Type != elf.PT_GNU_EH_FRAME || prog.Filesz < 4 {
			continue
		}
		if parseEHFrameWindow(l, prog.Vaddr, prog.Filesz) {
			l.UnwindRanges = mergeRanges(append(l.UnwindRanges, l.ExecRanges...))
			return
		}
		l.FuncBounds = nil
		l.UnwindRanges = nil
		l.DataRanges = nil
	}
	if sec, ok := l.Sections[".eh_frame_hdr"]; ok && sec != nil && sec.Size >= 4 {
		if parseEHFrameHdr(l, sec.Addr, sec.Size, 0) {
			l.UnwindRanges = mergeRanges(append(l.UnwindRanges, l.ExecRanges...))
			return
		}
		l.FuncBounds = nil
		l.UnwindRanges = nil
		l.DataRanges = nil
	}
	if sec, ok := l.Sections[".eh_frame"]; ok && sec != nil && sec.Size >= 8 && sec.Addr != 0 {
		limit := sec.Addr + sec.Size
		if end, bounds, lsdaBounds := walkEHFrame(l.MemoryImage, sec.Addr, limit); len(bounds) > 0 {
			l.FuncBounds = normalizeBounds(bounds)
			l.LSDABounds = normalizeBounds(lsdaBounds)
			l.UnwindRanges = mergeRanges(append(l.UnwindRanges, l.ExecRanges...))
			l.DataRanges = mergeRanges([]AddrRange{{Start: sec.Addr, End: end}})
		}
	}
}

// parseEHFrameWindow reads unwind info from a PT_GNU_EH_FRAME mapping.
// GNU ld points the program header at .eh_frame_hdr. Orbis images point it at
// a larger window that starts before the version byte; the header is inside.
func parseEHFrameWindow(l *LoadedELF, winVA, winSize uint64) bool {
	img := l.MemoryImage
	if winVA >= uint64(len(img)) {
		return false
	}
	end := winVA + winSize
	if end > uint64(len(img)) {
		end = uint64(len(img))
	}
	try := func(hdr uint64) bool {
		if hdr < winVA || hdr+4 > end {
			return false
		}
		// When the version byte is not at p_vaddr, the header's pc-relative
		// fields were encoded against the program header address.
		if !parseEHFrameHdr(l, hdr, end-hdr, int64(hdr-winVA)) {
			l.FuncBounds = nil
			l.DataRanges = nil
			return false
		}
		l.DataRanges = mergeRanges(append(l.DataRanges, AddrRange{Start: winVA, End: end}))
		return len(l.FuncBounds) > 0
	}
	if img[winVA] == 1 && try(winVA) {
		return true
	}
	// Clang's .eh_frame_hdr starts with version 1, pcrel|sdata4, udata4.
	for i := winVA; i+4 <= end; i++ {
		if img[i] == 1 && img[i+1] == 0x1b && img[i+2] == 0x03 && img[i+3] == 0x3b {
			if try(i) {
				return true
			}
		}
	}
	return false
}

// parseEHFrameHdr reads a GNU .eh_frame_hdr at hdrVA and the .eh_frame it points at.
// The search table has no sizes; function extents come from the FDEs.
// pcrelBias is subtracted from pc-relative values encoded in the header when
// the version byte does not sit at the address the linker used.
func parseEHFrameHdr(l *LoadedELF, hdrVA, hdrSize uint64, pcrelBias int64) bool {
	img := l.MemoryImage
	if hdrSize < 4 || hdrVA >= uint64(len(img)) || hdrVA+4 > uint64(len(img)) {
		return false
	}
	if img[hdrVA] != 1 {
		return false
	}
	dec := ehDec{img: img, hdr: hdrVA}
	off := int(hdrVA) + 4
	frameVA, off, ok := dec.read(off, img[hdrVA+1])
	if !ok {
		return false
	}
	if pcrelBias != 0 && img[hdrVA+1]&0x70 == dwEHPEpcrel {
		adj := int64(frameVA) - pcrelBias
		if adj <= 0 {
			return false
		}
		frameVA = uint64(adj)
	}
	if frameVA == 0 || frameVA >= uint64(len(img)) {
		return false
	}
	if _, off, ok = dec.read(off, img[hdrVA+2]); !ok {
		return false
	}
	_ = off

	limit := uint64(len(img))
	if hdrVA > frameVA && hdrVA < limit {
		limit = hdrVA
	}
	end, bounds, lsdaBounds := walkEHFrame(img, frameVA, limit)
	if len(bounds) == 0 || end <= frameVA {
		return false
	}
	l.FuncBounds = normalizeBounds(bounds)
	l.LSDABounds = normalizeBounds(lsdaBounds)
	hdrEnd := hdrVA + hdrSize
	if hdrEnd > uint64(len(img)) {
		hdrEnd = uint64(len(img))
	}
	l.DataRanges = mergeRanges([]AddrRange{
		{Start: frameVA, End: end},
		{Start: hdrVA, End: hdrEnd},
	})
	return true
}

type cieInfo struct {
	enc     byte
	lsdaEnc byte
	hasLSDA bool
	hasAugZ bool
	ok      bool
}

// walkEHFrame reads CIE/FDE records in [frameVA, limit). end is the address
// just past the terminator, or the end of the last valid record.
func walkEHFrame(img []byte, frameVA, limit uint64) (end uint64, bounds []AddrRange, lsdaBounds []AddrRange) {
	if frameVA >= uint64(len(img)) || limit > uint64(len(img)) || frameVA >= limit {
		return frameVA, nil, nil
	}
	cies := make(map[uint64]cieInfo)
	pos := frameVA
	const maxRecords = 1 << 22
	for n := 0; n < maxRecords && pos+4 <= limit; n++ {
		length32 := readU32(img, int(pos))
		body := pos + 4
		if length32 == 0 {
			return body, bounds, lsdaBounds
		}
		if length32 == 0xffffffff {
			if pos+12 > limit {
				return pos, bounds, lsdaBounds
			}
			longLen := readU64(img, int(pos+4))
			recEnd := pos + 12 + longLen
			if longLen == 0 || recEnd < pos || recEnd > limit {
				return pos, bounds, lsdaBounds
			}
			pos = recEnd
			continue
		}
		recEnd := body + uint64(length32)
		if recEnd < body || recEnd > limit {
			return pos, bounds, lsdaBounds
		}
		if body+4 > recEnd {
			return pos, bounds, lsdaBounds
		}
		id := uint64(readU32(img, int(body)))
		if id == 0 {
			cies[pos] = parseCIE(img, pos, recEnd)
			pos = recEnd
			continue
		}
		if id > body {
			pos = recEnd
			continue
		}
		cieVA := body - id
		info, seen := cies[cieVA]
		if !seen {
			info = parseCIE(img, cieVA, cieRecordEnd(img, cieVA, limit))
			cies[cieVA] = info
		}
		if info.ok {
			if start, size, fdeOff, ok := readFDERange(img, int(body+4), info.enc); ok {
				bounds = append(bounds, AddrRange{Start: start, End: start + size})
				if info.hasAugZ && info.hasLSDA && info.lsdaEnc != dwEHPEOmit && fdeOff < int(recEnd) {
					if augLen, augDataOff, ok := readULEB(img, fdeOff, int(recEnd)); ok && augLen > 0 && augDataOff < int(recEnd) {
						lsdaVal, _, ok := (ehDec{img: img, hdr: frameVA}).read(augDataOff, info.lsdaEnc)
						if ok && lsdaVal != 0 {
							lsdaBounds = append(lsdaBounds, AddrRange{Start: start, End: start + size})
						}
					}
				}
			}
		}
		pos = recEnd
	}
	return pos, bounds, lsdaBounds
}

func cieRecordEnd(img []byte, cieVA, limit uint64) uint64 {
	if cieVA+4 > uint64(len(img)) || cieVA+4 > limit {
		return cieVA
	}
	length := uint64(readU32(img, int(cieVA)))
	if length == 0 || length == 0xffffffff {
		return cieVA
	}
	end := cieVA + 4 + length
	if end > uint64(len(img)) || end > limit {
		return cieVA
	}
	return end
}

func parseCIE(img []byte, start, end uint64) cieInfo {
	// x86-64 absolute pointer when the augmentation has no 'R'.
	info := cieInfo{enc: dwEHPEUdata8, lsdaEnc: dwEHPEOmit}
	if start+8 > end || end > uint64(len(img)) {
		return info
	}
	i := int(start) + 8
	if i >= int(end) {
		return info
	}
	ver := img[i]
	i++
	augStart := i
	for i < int(end) && img[i] != 0 {
		i++
	}
	if i >= int(end) {
		return info
	}
	aug := img[augStart:i]
	i++
	var ok bool
	if _, i, ok = readULEB(img, i, int(end)); !ok {
		return info
	}
	if _, i, ok = readSLEB(img, i, int(end)); !ok {
		return info
	}
	if ver == 1 {
		if i >= int(end) {
			return info
		}
		i++
	} else if _, i, ok = readULEB(img, i, int(end)); !ok {
		return info
	}
	if len(aug) == 0 || aug[0] != 'z' {
		info.ok = true
		return info
	}
	info.hasAugZ = true
	augLen, i, ok := readULEB(img, i, int(end))
	if !ok || uint64(i)+augLen > end {
		return info
	}
	dataEnd := i + int(augLen)
	for _, ch := range aug[1:] {
		if i >= dataEnd {
			return info
		}
		switch ch {
		case 'L':
			info.hasLSDA = true
			info.lsdaEnc = img[i]
			i++
		case 'R':
			info.enc = img[i]
			i++
		case 'P':
			if i+1 > dataEnd {
				return info
			}
			penc := img[i]
			i++
			_, i, ok = (ehDec{img: img}).read(i, penc)
			if !ok || i > dataEnd {
				return info
			}
		default:
			return info
		}
	}
	info.ok = true
	return info
}

func readFDERange(img []byte, off int, enc byte) (start, size uint64, nextOff int, ok bool) {
	if enc == dwEHPEOmit {
		return 0, 0, off, false
	}
	dec := ehDec{img: img}
	start, off, ok = dec.read(off, enc)
	if !ok {
		return 0, 0, off, false
	}
	// Address range uses the same format and an absolute application.
	size, off, ok = dec.read(off, enc&0x0f)
	if !ok || size == 0 || start+size < start {
		return 0, 0, off, false
	}
	return start, size, off, true
}

type ehDec struct {
	img []byte
	hdr uint64
}

// read decodes one EH pointer at off. Guest addresses match image indexes.
func (e ehDec) read(off int, enc byte) (uint64, int, bool) {
	if enc == dwEHPEOmit {
		return 0, off, true
	}
	format := enc & 0x0f
	fieldVA := uint64(off)
	var raw int64
	var ok bool
	switch format {
	case dwEHPEAbsptr, dwEHPEUdata8:
		var u uint64
		u, off, ok = readU64At(e.img, off)
		raw = int64(u)
	case dwEHPEUleb:
		var u uint64
		u, off, ok = readULEB(e.img, off, len(e.img))
		raw = int64(u)
	case dwEHPEUdata2:
		if off+2 > len(e.img) {
			return 0, off, false
		}
		raw = int64(uint16(e.img[off]) | uint16(e.img[off+1])<<8)
		off += 2
		ok = true
	case dwEHPEUdata4:
		if off+4 > len(e.img) {
			return 0, off, false
		}
		raw = int64(readU32(e.img, off))
		off += 4
		ok = true
	case dwEHPESleb:
		raw, off, ok = readSLEB(e.img, off, len(e.img))
	case dwEHPESdata2:
		if off+2 > len(e.img) {
			return 0, off, false
		}
		raw = int64(int16(uint16(e.img[off]) | uint16(e.img[off+1])<<8))
		off += 2
		ok = true
	case dwEHPESdata4:
		if off+4 > len(e.img) {
			return 0, off, false
		}
		raw = int64(int32(readU32(e.img, off)))
		off += 4
		ok = true
	case dwEHPESdata8:
		var u uint64
		u, off, ok = readU64At(e.img, off)
		raw = int64(u)
	default:
		return 0, off, false
	}
	if !ok {
		return 0, off, false
	}
	val, ok := applyEH(enc, raw, fieldVA, e.hdr, e.img)
	if !ok {
		return 0, off, false
	}
	return val, off, true
}

func applyEH(enc byte, raw int64, fieldVA, hdr uint64, img []byte) (uint64, bool) {
	var addr int64
	switch enc & 0x70 {
	case 0:
		addr = raw
	case dwEHPEpcrel:
		addr = int64(fieldVA) + raw
	case dwEHPEdatarel:
		addr = int64(hdr) + raw
	default:
		return 0, false
	}
	if addr < 0 {
		return 0, false
	}
	val := uint64(addr)
	if enc&dwEHPEIndirect != 0 {
		if val+8 > uint64(len(img)) {
			return 0, false
		}
		val = readU64(img, int(val))
	}
	return val, true
}

func readULEB(img []byte, i, limit int) (uint64, int, bool) {
	var n uint64
	var s uint
	for k := 0; k < 10; k++ {
		if i >= limit || i >= len(img) {
			return 0, i, false
		}
		c := img[i]
		i++
		n |= uint64(c&0x7f) << s
		if c&0x80 == 0 {
			return n, i, true
		}
		s += 7
	}
	return 0, i, false
}

func readSLEB(img []byte, i, limit int) (int64, int, bool) {
	var n uint64
	var s uint
	var c byte
	for k := 0; k < 10; k++ {
		if i >= limit || i >= len(img) {
			return 0, i, false
		}
		c = img[i]
		i++
		n |= uint64(c&0x7f) << s
		s += 7
		if c&0x80 == 0 {
			break
		}
	}
	if c&0x80 != 0 {
		return 0, i, false
	}
	if s < 64 && c&0x40 != 0 {
		n |= ^uint64(0) << s
	}
	return int64(n), i, true
}

func readU32(img []byte, i int) uint32 {
	return uint32(img[i]) | uint32(img[i+1])<<8 | uint32(img[i+2])<<16 | uint32(img[i+3])<<24
}

func readU64At(img []byte, i int) (uint64, int, bool) {
	if i < 0 || i+8 > len(img) {
		return 0, i, false
	}
	return readU64(img, i), i + 8, true
}

func readU64(img []byte, i int) uint64 {
	return uint64(img[i]) | uint64(img[i+1])<<8 | uint64(img[i+2])<<16 | uint64(img[i+3])<<24 |
		uint64(img[i+4])<<32 | uint64(img[i+5])<<40 | uint64(img[i+6])<<48 | uint64(img[i+7])<<56
}

func normalizeBounds(in []AddrRange) []AddrRange {
	if len(in) == 0 {
		return nil
	}
	slices.SortFunc(in, func(a, b AddrRange) int {
		if a.Start < b.Start {
			return -1
		}
		if a.Start > b.Start {
			return 1
		}
		if a.End > b.End {
			return -1
		}
		if a.End < b.End {
			return 1
		}
		return 0
	})
	out := in[:0]
	for _, r := range in {
		if r.End <= r.Start {
			continue
		}
		if n := len(out); n > 0 && out[n-1].Start == r.Start {
			if r.End > out[n-1].End {
				out[n-1].End = r.End
			}
			continue
		}
		out = append(out, r)
	}
	return slices.Clip(out)
}

func mergeRanges(in []AddrRange) []AddrRange {
	if len(in) == 0 {
		return nil
	}
	slices.SortFunc(in, func(a, b AddrRange) int {
		if a.Start < b.Start {
			return -1
		}
		if a.Start > b.Start {
			return 1
		}
		if a.End < b.End {
			return -1
		}
		if a.End > b.End {
			return 1
		}
		return 0
	})
	out := make([]AddrRange, 0, len(in))
	cur := in[0]
	started := cur.End > cur.Start
	for _, r := range in {
		if r.End <= r.Start {
			continue
		}
		if !started {
			cur = r
			started = true
			continue
		}
		if r.Start <= cur.End {
			if r.End > cur.End {
				cur.End = r.End
			}
			continue
		}
		out = append(out, cur)
		cur = r
	}
	if started {
		out = append(out, cur)
	}
	return out
}

func rangeContains(rs []AddrRange, addr uint64) bool {
	i, ok := slices.BinarySearchFunc(rs, addr, func(r AddrRange, addr uint64) int {
		if addr >= r.End {
			return -1
		}
		if addr < r.Start {
			return 1
		}
		return 0
	})
	return ok && i >= 0 && i < len(rs)
}

// InData reports whether addr falls in unwind data (.eh_frame, .eh_frame_hdr).
func (l *LoadedELF) InData(addr uint64) bool {
	if l == nil {
		return false
	}
	return rangeContains(l.DataRanges, addr)
}

// InUnwindScope reports whether addr falls within the executable range of a module
// that has unwind information.
func (l *LoadedELF) InUnwindScope(addr uint64) bool {
	if l == nil {
		return false
	}
	if len(l.UnwindRanges) > 0 {
		return rangeContains(l.UnwindRanges, addr)
	}
	return len(l.FuncBounds) > 0
}

// InFuncExtent reports whether addr falls inside any known unwind function extent.
func (l *LoadedELF) InFuncExtent(addr uint64) bool {
	if l == nil || len(l.FuncBounds) == 0 {
		return false
	}
	return rangeContains(l.FuncBounds, addr)
}

// IsFuncEntry reports whether addr is the start of an unwind function extent.
func (l *LoadedELF) IsFuncEntry(addr uint64) bool {
	if l == nil || len(l.FuncBounds) == 0 {
		return false
	}
	_, ok := slices.BinarySearchFunc(l.FuncBounds, addr, func(r AddrRange, addr uint64) int {
		if r.Start < addr {
			return -1
		}
		if r.Start > addr {
			return 1
		}
		return 0
	})
	return ok
}

// CodeCeiling is the exclusive address where a decode that reached addr must
// stop: the end of the unwind extent containing addr, otherwise the next
// extent's start. Zero means the image has no unwind bounds.
func (l *LoadedELF) CodeCeiling(addr uint64) uint64 {
	if l == nil || len(l.FuncBounds) == 0 {
		return 0
	}
	i, ok := slices.BinarySearchFunc(l.FuncBounds, addr, func(r AddrRange, addr uint64) int {
		if addr >= r.End {
			return -1
		}
		if addr < r.Start {
			return 1
		}
		return 0
	})
	if ok {
		return l.FuncBounds[i].End
	}
	if i < len(l.FuncBounds) && l.FuncBounds[i].Start > addr {
		return l.FuncBounds[i].Start
	}
	return 0
}
