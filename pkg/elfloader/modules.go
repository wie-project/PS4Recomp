package elfloader

import (
	"debug/elf"
	"encoding/binary"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strings"
)

// System stub modules shipped with the OpenOrbis SDK. Their APIs are provided
// by host shims; they are not AOT-recompiled.
var systemStubModules = map[string]struct{}{
	"libc.prx":         {},
	"libscelibc.prx":   {},
	"libscefios2.prx":  {},
	"libscelibc.sprx":  {},
	"libscefios2.sprx": {},
	"right.sprx":       {},
	"right.prx":        {},
}

// IsSystemStubModule reports whether name/path is an SDK import stub PRX.
// OpenOrbis SDK stub modules are tiny symbol placeholders (< 64 KB).
// Real retail modules provided by games (such as retail libc.prx or libSceFios2.prx)
// contain actual implementations and are much larger.
func IsSystemStubModule(name string) bool {
	if _, ok := systemStubModules[strings.ToLower(filepath.Base(name))]; !ok {
		return false
	}
	if fi, err := os.Stat(name); err == nil && fi.Size() > 64*1024 {
		return false
	}
	return true
}

// ModuleRef is a companion PRX/SPRX/ELF to AOT-link into the guest image.
type ModuleRef struct {
	Path    string
	Aliases []string
}

// ReferencedModuleNames extracts .prx/.sprx paths stored as C strings in guest memory.
func ReferencedModuleNames(image []byte) []string {
	seen := make(map[string]struct{})
	var out []string
	for i := 0; i < len(image); {
		if image[i] < 0x20 || image[i] > 0x7e {
			i++
			continue
		}
		j := i
		for j < len(image) && image[j] >= 0x20 && image[j] <= 0x7e {
			j++
		}
		if j-i >= 8 && j < len(image) && image[j] == 0 {
			s := string(image[i:j])
			if isGuestModulePath(s) {
				if _, ok := seen[s]; !ok {
					seen[s] = struct{}{}
					out = append(out, s)
				}
			}
		}
		i = j + 1
	}
	return out
}

func isGuestModulePath(s string) bool {
	lower := strings.ToLower(s)
	if !strings.Contains(lower, ".prx") && !strings.Contains(lower, ".sprx") {
		return false
	}
	return !IsSystemStubModule(filepath.Base(s))
}

// MapGuestPath translates a PS4 guest path onto the host app directory.
// /app0/sce_module/foo.prx → <appDir>/sce_module/foo.prx
func MapGuestPath(guestPath, appDir string) string {
	if appDir == "" || guestPath == "" {
		return ""
	}
	p := strings.ReplaceAll(guestPath, "\\", "/")
	switch {
	case p == "/app0":
		return appDir
	case strings.HasPrefix(p, "/app0/"):
		return filepath.Join(appDir, p[len("/app0/"):])
	case strings.HasPrefix(p, "/"):
		return ""
	default:
		return filepath.Join(appDir, p)
	}
}

func fileExists(path string) bool {
	if path == "" {
		return false
	}
	fi, err := os.Stat(path)
	return err == nil && !fi.IsDir()
}

func addModuleRef(out *[]ModuleRef, seen map[string]struct{}, path string, aliases ...string) {
	if !fileExists(path) || IsSystemStubModule(path) {
		return
	}
	if abs, err := filepath.Abs(path); err == nil {
		path = abs
	}
	if _, ok := seen[path]; ok {
		for i := range *out {
			if (*out)[i].Path == path {
				(*out)[i].Aliases = uniqueNonEmpty(append((*out)[i].Aliases, aliases...))
			}
		}
		return
	}
	seen[path] = struct{}{}
	*out = append(*out, ModuleRef{Path: path, Aliases: uniqueNonEmpty(aliases)})
}

func uniqueNonEmpty(in []string) []string {
	seen := make(map[string]struct{})
	var out []string
	for _, s := range in {
		s = strings.TrimSpace(s)
		if s == "" {
			continue
		}
		key := strings.ToLower(filepath.Base(s))
		if _, ok := seen[key]; ok {
			continue
		}
		seen[key] = struct{}{}
		out = append(out, filepath.Base(s))
	}
	return out
}

func thisAppModuleDirs(elfPath, appDir string) []string {
	seen := make(map[string]struct{})
	var dirs []string
	add := func(dir string) {
		if dir == "" {
			return
		}
		if abs, err := filepath.Abs(dir); err == nil {
			dir = abs
		}
		if _, err := os.Stat(dir); err != nil {
			return
		}
		if _, ok := seen[dir]; ok {
			return
		}
		seen[dir] = struct{}{}
		dirs = append(dirs, dir)
	}
	if appDir != "" {
		add(appDir)
		add(filepath.Join(appDir, "sce_module"))
		add(filepath.Join(appDir, "prx"))
	}
	if elfPath != "" {
		dir := filepath.Dir(elfPath)
		for range 5 {
			add(filepath.Join(dir, "sce_module"))
			add(filepath.Join(dir, "prx"))
			parent := filepath.Dir(dir)
			if parent == dir {
				break
			}
			dir = parent
		}
	}
	return dirs
}

// DiscoverCompanionModules finds PRX/SPRX files that belong to this app:
// files in <appDir>/sce_module, and guest paths such as /app0/sce_module/foo.prx
// resolved through MapGuestPath. It does not search sibling projects.
func DiscoverCompanionModules(elfPath, appDir string, image []byte) []ModuleRef {
	seen := make(map[string]struct{})
	var out []ModuleRef

	for _, dir := range thisAppModuleDirs(elfPath, appDir) {
		entries, err := os.ReadDir(dir)
		if err != nil {
			continue
		}
		for _, e := range entries {
			if e.IsDir() {
				continue
			}
			ext := strings.ToLower(filepath.Ext(e.Name()))
			if ext != ".prx" && ext != ".sprx" {
				continue
			}
			addModuleRef(&out, seen, filepath.Join(dir, e.Name()), e.Name())
		}
	}

	for _, ref := range ReferencedModuleNames(image) {
		mapped := MapGuestPath(ref, appDir)
		if mapped != "" {
			addModuleRef(&out, seen, mapped, filepath.Base(ref))
		}
		for _, dir := range thisAppModuleDirs(elfPath, appDir) {
			addModuleRef(&out, seen, filepath.Join(dir, filepath.Base(ref)), filepath.Base(ref))
		}
	}
	return out
}

// Overlaps reports whether two loaded images share any virtual address bytes.
func Overlaps(a, b *LoadedELF) bool {
	if a == nil || b == nil {
		return false
	}
	return a.MinVAddr < b.MaxVAddr && b.MinVAddr < a.MaxVAddr
}

func alignUp(v, a uint64) uint64 {
	if a == 0 {
		return v
	}
	return (v + a - 1) &^ (a - 1)
}

// ApplyBias shifts the entire loaded image by delta bytes so it can sit next
// to another module without overlapping. RIP-relative code stays valid because
// the module is moved as a unit.
func (l *LoadedELF) ApplyBias(delta uint64) error {
	if l == nil || delta == 0 {
		return nil
	}
	newMax := l.MaxVAddr + delta
	newImg := make([]byte, alignUp(newMax, 4096))
	if l.MaxVAddr > l.MinVAddr && l.MaxVAddr <= uint64(len(l.MemoryImage)) {
		copy(newImg[l.MinVAddr+delta:l.MaxVAddr+delta], l.MemoryImage[l.MinVAddr:l.MaxVAddr])
	}

	shift := func(addr uint64) uint64 {
		if addr == 0 {
			return 0
		}
		return addr + delta
	}

	l.EntryPoint = shift(l.EntryPoint)
	l.MinVAddr += delta
	l.MaxVAddr += delta
	l.MemoryImage = newImg

	for _, seg := range l.Segments {
		seg.Vaddr += delta
		end := seg.Vaddr + seg.Memsz
		if end > uint64(len(l.MemoryImage)) {
			end = uint64(len(l.MemoryImage))
		}
		if seg.Vaddr < end {
			seg.Data = l.MemoryImage[seg.Vaddr:end]
		} else {
			seg.Data = nil
		}
	}
	for i := range l.ExecRanges {
		l.ExecRanges[i].Start += delta
		l.ExecRanges[i].End += delta
	}
	for i := range l.FuncBounds {
		l.FuncBounds[i].Start += delta
		l.FuncBounds[i].End += delta
	}
	for i := range l.UnwindRanges {
		l.UnwindRanges[i].Start += delta
		l.UnwindRanges[i].End += delta
	}
	for i := range l.DataRanges {
		l.DataRanges[i].Start += delta
		l.DataRanges[i].End += delta
	}

	for i := range l.Symbols {
		if l.Symbols[i].Address != 0 {
			l.Symbols[i].Address += delta
		}
	}
	l.SymbolByAddr = make(map[uint64]Symbol, len(l.Symbols))
	l.SymbolByName = make(map[string]Symbol, len(l.Symbols))
	for _, sym := range l.Symbols {
		if sym.Name != "" {
			l.SymbolByName[sym.Name] = sym
		}
		if sym.Address != 0 {
			l.SymbolByAddr[sym.Address] = sym
		}
	}
	for i := range l.DynSymbols {
		if l.DynSymbols[i].Address != 0 {
			l.DynSymbols[i].Address += delta
		}
	}
	for i := range l.InitArray {
		if l.InitArray[i] != 0 {
			l.InitArray[i] += delta
		}
	}

	for i := range l.Relocations {
		rel := &l.Relocations[i]
		rel.Offset += delta
		if rel.PltAddr != 0 {
			rel.PltAddr += delta
		}
		switch rel.Type {
		case R_X86_64_RELATIVE:
			if rel.Offset+8 <= uint64(len(l.MemoryImage)) {
				binary.LittleEndian.PutUint64(l.MemoryImage[rel.Offset:rel.Offset+8], uint64(rel.Addend)+delta)
			}
		case R_X86_64_JUMP_SLOT:
			if rel.PltAddr != 0 && rel.Offset+8 <= uint64(len(l.MemoryImage)) {
				binary.LittleEndian.PutUint64(l.MemoryImage[rel.Offset:rel.Offset+8], rel.PltAddr)
			}
		case R_X86_64_64, R_X86_64_GLOB_DAT:
			if int(rel.SymIdx) < len(l.DynSymbols) {
				sym := l.DynSymbols[rel.SymIdx]
				if sym.Address != 0 && rel.Offset+8 <= uint64(len(l.MemoryImage)) {
					binary.LittleEndian.PutUint64(l.MemoryImage[rel.Offset:rel.Offset+8], sym.Address+uint64(rel.Addend))
				}
			}
		}
	}
	return nil
}

// MergeImages copies src's mapped bytes and metadata into dst. The caller must
// relocate src first if the virtual address ranges overlap.
func MergeImages(dst, src *LoadedELF) error {
	if dst == nil || src == nil {
		return fmt.Errorf("merge requires two loaded images")
	}
	if Overlaps(dst, src) {
		return fmt.Errorf("cannot merge overlapping modules %s (0x%x-0x%x) and %s (0x%x-0x%x)",
			dst.FileName, dst.MinVAddr, dst.MaxVAddr, src.FileName, src.MinVAddr, src.MaxVAddr)
	}
	if src.MaxVAddr > uint64(len(dst.MemoryImage)) {
		n := make([]byte, alignUp(src.MaxVAddr, 4096))
		copy(n, dst.MemoryImage)
		dst.MemoryImage = n
	}
	if src.MaxVAddr > src.MinVAddr && src.MaxVAddr <= uint64(len(src.MemoryImage)) {
		copy(dst.MemoryImage[src.MinVAddr:src.MaxVAddr], src.MemoryImage[src.MinVAddr:src.MaxVAddr])
	}
	if src.MinVAddr < dst.MinVAddr {
		dst.MinVAddr = src.MinVAddr
	}
	if src.MaxVAddr > dst.MaxVAddr {
		dst.MaxVAddr = src.MaxVAddr
	}
	dst.Segments = append(dst.Segments, src.Segments...)
	dst.ExecRanges = append(dst.ExecRanges, src.ExecRanges...)
	dst.FuncBounds = append(dst.FuncBounds, src.FuncBounds...)
	slices.SortFunc(dst.FuncBounds, func(a, b AddrRange) int {
		if a.Start < b.Start {
			return -1
		}
		if a.Start > b.Start {
			return 1
		}
		return 0
	})
	dst.UnwindRanges = mergeRanges(append(dst.UnwindRanges, src.UnwindRanges...))
	dst.DataRanges = mergeRanges(append(dst.DataRanges, src.DataRanges...))
	dst.Relocations = append(dst.Relocations, src.Relocations...)
	dst.InitArray = append(dst.InitArray, src.InitArray...)
	for _, sym := range src.Symbols {
		addSymbol(dst, sym)
	}
	if len(src.DynSymbols) > 1 {
		if len(dst.DynSymbols) == 0 {
			dst.DynSymbols = append(dst.DynSymbols, Symbol{})
		}
		dst.DynSymbols = append(dst.DynSymbols, src.DynSymbols[1:]...)
	}
	for _, seg := range dst.Segments {
		end := seg.Vaddr + seg.Memsz
		if end > uint64(len(dst.MemoryImage)) {
			end = uint64(len(dst.MemoryImage))
		}
		if seg.Vaddr < end {
			seg.Data = dst.MemoryImage[seg.Vaddr:end]
		}
	}
	return nil
}

// ExportedFunctions returns named function symbols suitable for sceKernelDlsym.
func (l *LoadedELF) ExportedFunctions() []Symbol {
	if l == nil {
		return nil
	}
	seen := make(map[string]struct{})
	var out []Symbol
	consider := func(sym Symbol) {
		if sym.Name == "" || sym.Address == 0 {
			return
		}
		if _, ok := seen[sym.Name]; ok {
			return
		}
		validAddr := sym.Address < uint64(len(l.MemoryImage))
		isExport := sym.Type == elf.STT_FUNC || sym.Type == elf.STT_OBJECT || (sym.Type == elf.STT_NOTYPE && validAddr)
		if !isExport {
			return
		}
		seen[sym.Name] = struct{}{}
		out = append(out, sym)
	}
	for _, sym := range l.DynSymbols {
		consider(sym)
	}
	for _, sym := range l.Symbols {
		consider(sym)
	}
	return out
}

// ResolveModuleRelocations patches all GOT/PLT and data relocations across the main image
// and linked companion modules into main.MemoryImage.
func ResolveModuleRelocations(main *LoadedELF, companionExports []Symbol) error {
	if main == nil {
		return nil
	}

	// 1. Build lookup table from companion module exports and main defined symbols
	exportMap := make(map[string]uint64)
	for _, sym := range companionExports {
		if sym.Name != "" && sym.Address != 0 {
			exportMap[sym.Name] = sym.Address
			if nid := NIDPrefix(sym.Name); nid != "" {
				exportMap[nid] = sym.Address
			}
			if canon, ok := ResolveNID(sym.Name); ok && canon != "" {
				exportMap[canon] = sym.Address
			}
		}
	}
	for _, sym := range main.DynSymbols {
		if sym.Name != "" && sym.Address != 0 {
			if _, ok := exportMap[sym.Name]; !ok {
				exportMap[sym.Name] = sym.Address
			}
			if nid := NIDPrefix(sym.Name); nid != "" {
				if _, ok := exportMap[nid]; !ok {
					exportMap[nid] = sym.Address
				}
			}
			if canon, ok := ResolveNID(sym.Name); ok && canon != "" {
				if _, ok := exportMap[canon]; !ok {
					exportMap[canon] = sym.Address
				}
			}
		}
	}
	for _, sym := range main.Symbols {
		if sym.Name != "" && sym.Address != 0 {
			if _, ok := exportMap[sym.Name]; !ok {
				exportMap[sym.Name] = sym.Address
			}
		}
	}

	// 2. Iterate through all relocations in main
	for i := range main.Relocations {
		rel := &main.Relocations[i]
		if rel.Offset+8 > uint64(len(main.MemoryImage)) {
			continue
		}

		switch rel.Type {
		case R_X86_64_JUMP_SLOT, R_X86_64_GLOB_DAT, R_X86_64_64:
			if rel.SymName == "" {
				continue
			}

			// Check if symbol exists in exported/defined symbols
			targetAddr, ok := exportMap[rel.SymName]
			if !ok {
				if nid := NIDPrefix(rel.SymName); nid != "" {
					targetAddr, ok = exportMap[nid]
				}
			}
			if !ok {
				if canon, hit := ResolveNID(rel.SymName); hit && canon != "" {
					targetAddr, ok = exportMap[canon]
				}
			}

			if ok && targetAddr != 0 {
				finalAddr := targetAddr + uint64(rel.Addend)
				binary.LittleEndian.PutUint64(main.MemoryImage[rel.Offset:rel.Offset+8], finalAddr)
			} else {
				// For host shims or unresolved symbols: point the GOT slot to rel.Offset (or rel.PltAddr)
				// so indirect call recomp_dispatch(ctx, MEM_U64(rel.Offset)) dispatches to the registered shim/stub!
				target := rel.Offset
				if rel.PltAddr != 0 {
					target = rel.PltAddr
				}
				binary.LittleEndian.PutUint64(main.MemoryImage[rel.Offset:rel.Offset+8], target)
			}
		}
	}

	return nil
}
