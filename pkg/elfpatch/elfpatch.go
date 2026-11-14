// Package elfpatch applies game patches in the shadPS4 patch XML format to the bytes of an
// executable's ELF image before it is loaded and analyzed. The patched code is then discovered
// and lifted like any other code, and patched data lands in the guest image: nothing is patched
// at run time.
//
// Format (shadPS4 MemoryPatcher::ApplyPatchesFromXML):
//
//	<Patch>
//	  <TitleID><ID>CUSA00000</ID>...</TitleID>
//	  <Metadata Name="..." AppVer="01.00" AppElf="eboot.bin" ...>
//	    <PatchList><Line Type="bytes32" Address="0x004004aa" Value="0xf00"/>...</PatchList>
//	  </Metadata>
//	</Patch>
//
// shadPS4 maps each segment of the main executable at load base + p_vaddr and writes a line at
// load base + (Address - 0x400000), so a line's Address names p_vaddr Address-0x400000 whatever
// the load base is. The recompiler's guest addresses are the p_vaddr values themselves.
package elfpatch

import (
	"bytes"
	"cmp"
	"debug/elf"
	"encoding/binary"
	"encoding/hex"
	"encoding/xml"
	"fmt"
	"math"
	"os"
	"slices"
	"strconv"
	"strings"

	"ps4-recomp/pkg/elfloader"
)

// ExecutableLoadBase is the image base patch addresses are written against: shadPS4 writes a
// line at the main executable's load base + (Address - ExecutableLoadBase).
const ExecutableLoadBase = 0x400000

// File is a parsed patch XML file.
type File struct {
	TitleIDs []string
	Sets     []Set
}

// Set is one <Metadata> element: a named patch the user can enable.
type Set struct {
	Name   string
	AppVer string
	AppElf string
	Lines  []Line
}

// Line is one <Line> element as written in the file. Its value is decoded only when the set is
// selected, so an unsupported type in a set nobody enables does not block a build.
type Line struct {
	Type    string
	Address string
	Value   string
}

type xmlPatch struct {
	XMLName  xml.Name `xml:"Patch"`
	TitleIDs []string `xml:"TitleID>ID"`
	Sets     []struct {
		Name   string `xml:"Name,attr"`
		AppVer string `xml:"AppVer,attr"`
		AppElf string `xml:"AppElf,attr"`
		Lines  []struct {
			Type    string `xml:"Type,attr"`
			Address string `xml:"Address,attr"`
			Value   string `xml:"Value,attr"`
		} `xml:"PatchList>Line"`
	} `xml:"Metadata"`
}

// Load reads and parses a patch XML file.
func Load(path string) (*File, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read patch file: %w", err)
	}
	f, err := Parse(data)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	return f, nil
}

// Parse parses patch XML.
func Parse(data []byte) (*File, error) {
	var x xmlPatch
	if err := xml.Unmarshal(data, &x); err != nil {
		return nil, fmt.Errorf("invalid patch XML: %w", err)
	}
	f := &File{Sets: make([]Set, 0, len(x.Sets))}
	for _, id := range x.TitleIDs {
		f.TitleIDs = append(f.TitleIDs, strings.TrimSpace(id))
	}
	if len(f.TitleIDs) == 0 {
		return nil, fmt.Errorf("patch XML has no <TitleID><ID>")
	}
	for _, s := range x.Sets {
		set := Set{Name: s.Name, AppVer: s.AppVer, AppElf: s.AppElf, Lines: make([]Line, 0, len(s.Lines))}
		if set.Name == "" {
			return nil, fmt.Errorf("patch XML has a <Metadata> without Name")
		}
		for _, l := range s.Lines {
			set.Lines = append(set.Lines, Line{Type: l.Type, Address: l.Address, Value: l.Value})
		}
		f.Sets = append(f.Sets, set)
	}
	return f, nil
}

// Target identifies the binary a patch file is applied to: TITLE_ID and APP_VER of its
// param.sfo and the file name of the executable.
type Target struct {
	TitleID string
	AppVer  string
	ElfName string
}

// Write is one decoded patch line placed in the ELF.
type Write struct {
	Type    string
	Address uint64 // as written in the patch file
	Vaddr   uint64 // guest address (p_vaddr)
	Offset  uint64 // ELF file offset
	Data    []byte
	Orig    []byte // the bytes the write replaced
}

// AppliedSet is a selected set with its writes, in file order.
type AppliedSet struct {
	Name   string
	Writes []Write
}

// Result describes the patches applied to one ELF.
type Result struct {
	Path   string
	Target Target
	Sets   []AppliedSet
}

// Select validates names against the file and the target and decodes the selected sets, in the
// order requested. Every name must exist exactly once for the target's app version.
func (f *File) Select(names []string, t Target) ([]AppliedSet, error) {
	if !slices.Contains(f.TitleIDs, t.TitleID) {
		return nil, fmt.Errorf("patch file is for title %s, the game is %s", strings.Join(f.TitleIDs, ", "), t.TitleID)
	}
	out := make([]AppliedSet, 0, len(names))
	for i, name := range names {
		if slices.Contains(names[:i], name) {
			return nil, fmt.Errorf("patch %q is requested twice", name)
		}
		set, err := f.find(name, t.AppVer)
		if err != nil {
			return nil, err
		}
		if set.AppElf == "" {
			return nil, fmt.Errorf("patch %q has no AppElf", name)
		}
		if !strings.EqualFold(set.AppElf, t.ElfName) {
			return nil, fmt.Errorf("patch %q is for %s, the binary is %s", name, set.AppElf, t.ElfName)
		}
		if len(set.Lines) == 0 {
			return nil, fmt.Errorf("patch %q has no lines", name)
		}
		as := AppliedSet{Name: name, Writes: make([]Write, 0, len(set.Lines))}
		for _, l := range set.Lines {
			w, err := decodeLine(l)
			if err != nil {
				return nil, fmt.Errorf("patch %q: line Address=%q: %w", name, l.Address, err)
			}
			as.Writes = append(as.Writes, w)
		}
		out = append(out, as)
	}
	return out, nil
}

func (f *File) find(name, appVer string) (*Set, error) {
	var found *Set
	var versions []string
	for i := range f.Sets {
		s := &f.Sets[i]
		if s.Name != name {
			continue
		}
		versions = append(versions, s.AppVer)
		if s.AppVer != appVer {
			continue
		}
		if found != nil {
			return nil, fmt.Errorf("patch %q is defined twice for app version %s", name, appVer)
		}
		found = s
	}
	if found == nil {
		if len(versions) == 0 {
			return nil, fmt.Errorf("patch %q is not in the patch file", name)
		}
		return nil, fmt.Errorf("patch %q is for app version %s, the game is %s", name, strings.Join(versions, ", "), appVer)
	}
	return found, nil
}

// decodeLine converts a line to the bytes shadPS4 writes for it. Types whose meaning shadPS4
// defines independently of the host are supported; pattern-scan types (mask, mask_jump32) and
// utf16 (converted through the host locale) are rejected.
func decodeLine(l Line) (Write, error) {
	w := Write{Type: l.Type}
	var err error
	switch l.Type {
	case "bytes":
		if l.Value == "" || len(l.Value)%2 != 0 {
			return Write{}, fmt.Errorf("bytes value %q is not an even number of hex digits", l.Value)
		}
		w.Data, err = hex.DecodeString(l.Value)
		if err != nil {
			return Write{}, fmt.Errorf("bytes value %q is not hex", l.Value)
		}
	case "byte", "bytes16", "bytes32", "bytes64":
		size := map[string]int{"byte": 1, "bytes16": 2, "bytes32": 4, "bytes64": 8}[l.Type]
		v, err := parseInt(l.Value, size*8)
		if err != nil {
			return Write{}, fmt.Errorf("%s value %q: %w", l.Type, l.Value, err)
		}
		w.Data = binary.LittleEndian.AppendUint64(nil, v)[:size]
	case "float32":
		v, err := strconv.ParseFloat(l.Value, 32)
		if err != nil {
			return Write{}, fmt.Errorf("float32 value %q: %w", l.Value, err)
		}
		w.Data = binary.LittleEndian.AppendUint32(nil, math.Float32bits(float32(v)))
	case "float64":
		v, err := strconv.ParseFloat(l.Value, 64)
		if err != nil {
			return Write{}, fmt.Errorf("float64 value %q: %w", l.Value, err)
		}
		w.Data = binary.LittleEndian.AppendUint64(nil, math.Float64bits(v))
	case "utf8":
		w.Data = append([]byte(l.Value), 0)
	default:
		return Write{}, fmt.Errorf("unsupported patch type %q (supported: bytes, byte, bytes16, bytes32, bytes64, float32, float64, utf8)", l.Type)
	}
	w.Address, err = strconv.ParseUint(strings.TrimPrefix(strings.TrimPrefix(l.Address, "0x"), "0X"), 16, 64)
	if err != nil {
		return Write{}, fmt.Errorf("invalid address")
	}
	return w, nil
}

// parseInt parses an unsigned integer the way shadPS4 does: hex with a 0x prefix, decimal
// otherwise. The value must fit in bits.
func parseInt(s string, bits int) (uint64, error) {
	base := 10
	digits := s
	if len(s) >= 3 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') {
		base, digits = 16, s[2:]
	}
	v, err := strconv.ParseUint(digits, base, bits)
	if err != nil {
		return 0, fmt.Errorf("not an unsigned %d-bit integer", bits)
	}
	return v, nil
}

// Apply writes the selected sets into data, an unwrapped ELF image. Every write must lie inside
// the file-backed part of one loadable segment: patching .bss or unmapped space has no meaning
// for an ahead-of-time image. Writes may not overlap each other.
func Apply(data []byte, sets []AppliedSet) error {
	f, err := elf.NewFile(bytes.NewReader(data))
	if err != nil {
		return fmt.Errorf("failed to parse ELF: %w", err)
	}
	defer func() { _ = f.Close() }()
	for si := range sets {
		for wi := range sets[si].Writes {
			w := &sets[si].Writes[wi]
			if err := place(f.Progs, uint64(len(data)), w); err != nil {
				return fmt.Errorf("patch %q: %w", sets[si].Name, err)
			}
		}
	}
	if err := checkOverlaps(sets); err != nil {
		return err
	}
	for si := range sets {
		for wi := range sets[si].Writes {
			w := &sets[si].Writes[wi]
			dst := data[w.Offset : w.Offset+uint64(len(w.Data))]
			w.Orig = slices.Clone(dst)
			copy(dst, w.Data)
		}
	}
	return nil
}

// place resolves the guest address and file offset of w.
func place(progs []*elf.Prog, fileSize uint64, w *Write) error {
	if w.Address < ExecutableLoadBase {
		return fmt.Errorf("address 0x%x is below the executable base 0x%x", w.Address, ExecutableLoadBase)
	}
	w.Vaddr = w.Address - ExecutableLoadBase
	n := uint64(len(w.Data))
	var hit *elf.Prog
	for _, p := range progs {
		if p.Type != elf.PT_LOAD && p.Type != elfloader.PT_SCE_RELRO {
			continue
		}
		fsz := min(p.Filesz, fileSize-min(p.Off, fileSize))
		if w.Vaddr < p.Vaddr || w.Vaddr-p.Vaddr >= fsz || n > fsz-(w.Vaddr-p.Vaddr) {
			continue
		}
		if hit != nil {
			return fmt.Errorf("address 0x%x is in two segments (p_vaddr 0x%x and 0x%x)", w.Address, hit.Vaddr, p.Vaddr)
		}
		hit = p
	}
	if hit == nil {
		return fmt.Errorf("address 0x%x (guest 0x%x, %d bytes) is not inside the file-backed part of a loadable segment",
			w.Address, w.Vaddr, n)
	}
	w.Offset = hit.Off + (w.Vaddr - hit.Vaddr)
	return nil
}

func checkOverlaps(sets []AppliedSet) error {
	type span struct {
		start, end uint64
		set        string
	}
	var spans []span
	for _, s := range sets {
		for _, w := range s.Writes {
			spans = append(spans, span{w.Vaddr, w.Vaddr + uint64(len(w.Data)), s.Name})
		}
	}
	slices.SortFunc(spans, func(a, b span) int { return cmp.Compare(a.start, b.start) })
	for i := 1; i < len(spans); i++ {
		if spans[i].start < spans[i-1].end {
			return fmt.Errorf("patch %q writes guest 0x%x, which patch %q also writes",
				spans[i].set, spans[i].start, spans[i-1].set)
		}
	}
	return nil
}

// CheckRelocations rejects writes that overlap a relocation target. The loader, the module
// linker and the startup binder all rewrite relocation slots after the ELF is patched, so a
// patched value there would be replaced or would contradict the relocation the analysis uses.
func CheckRelocations(sets []AppliedSet, relocs []elfloader.Relocation) error {
	for _, s := range sets {
		for _, w := range s.Writes {
			end := w.Vaddr + uint64(len(w.Data))
			for _, r := range relocs {
				if r.Offset < end && w.Vaddr < r.Offset+relocSize(r.Type) {
					return fmt.Errorf("patch %q writes guest 0x%x..0x%x, which overlaps the target of relocation type %d at 0x%x",
						s.Name, w.Vaddr, end, r.Type, r.Offset)
				}
			}
		}
	}
	return nil
}

// relocSize is the width of the slot an x86-64 relocation writes.
func relocSize(t uint32) uint64 {
	switch elf.R_X86_64(t) {
	case elf.R_X86_64_PC32, elf.R_X86_64_32, elf.R_X86_64_32S, elf.R_X86_64_PLT32, elf.R_X86_64_GOTPCREL:
		return 4
	}
	return 8
}

// Report is the human-readable record of the applied patches.
func (r *Result) Report() string {
	var b strings.Builder
	b.WriteString("PS4Recomp patch report\n")
	b.WriteString("======================\n\n")
	if r == nil || len(r.Sets) == 0 {
		b.WriteString("No patches applied.\n")
		return b.String()
	}
	fmt.Fprintf(&b, "Patch file: %s\n", r.Path)
	fmt.Fprintf(&b, "Title: %s  App version: %s  ELF: %s\n", r.Target.TitleID, r.Target.AppVer, r.Target.ElfName)
	b.WriteString("Addresses: patch address, guest address (p_vaddr = patch - 0x400000), ELF file offset.\n")
	for _, s := range r.Sets {
		fmt.Fprintf(&b, "\n## %s (%d lines, %d bytes)\n", s.Name, len(s.Writes), s.Bytes())
		for _, w := range s.Writes {
			fmt.Fprintf(&b, "  %-8s 0x%08x guest 0x%x..0x%x file 0x%x: %x -> %x\n",
				w.Type, w.Address, w.Vaddr, w.Vaddr+uint64(len(w.Data)), w.Offset, w.Orig, w.Data)
		}
	}
	return b.String()
}

// Bytes is the number of bytes the set writes.
func (s *AppliedSet) Bytes() int {
	n := 0
	for _, w := range s.Writes {
		n += len(w.Data)
	}
	return n
}

// Names lists the applied set names, or nil when none were applied.
func (r *Result) Names() []string {
	if r == nil {
		return nil
	}
	names := make([]string, len(r.Sets))
	for i, s := range r.Sets {
		names[i] = s.Name
	}
	return names
}
