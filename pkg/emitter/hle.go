package emitter

import (
	"fmt"
	"os"
	"sort"
	"strings"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"

	"golang.org/x/arch/x86/x86asm"
)

// ImplementedSyscalls is the FreeBSD/Orbis syscall number table handled by
// shim_syscall in pkg/runtime/ps4_syscalls.c. Keep both lists identical.
var ImplementedSyscalls = map[int]string{
	1:   "exit",
	3:   "read",
	4:   "write",
	5:   "open",
	6:   "close",
	20:  "getpid",
	24:  "getuid",
	25:  "geteuid",
	47:  "getgid",
	73:  "munmap",
	116: "gettimeofday",
	118: "getrusage",
	232: "clock_gettime",
	432: "thr_self",
	477: "mmap",
}

// ImportClass is how an imported dynamic symbol is satisfied at AOT time.
type ImportClass int

const (
	ImportHostShim ImportClass = iota
	ImportGuestExport
	ImportDefined
	ImportUnresolved
)

func (c ImportClass) String() string {
	switch c {
	case ImportHostShim:
		return "host-shim"
	case ImportGuestExport:
		return "guest-export"
	case ImportDefined:
		return "defined"
	default:
		return "unresolved"
	}
}

// ImportSymbol is one unique imported name from JUMP_SLOT/GLOB_DAT/64 relocs.
type ImportSymbol struct {
	Name    string
	Library string
	Class   ImportClass
	Shim    string
	Relocs  int
}

// SyscallSite tallies one recovered syscall number (or dynamic).
type SyscallSite struct {
	Number       int // -1 = non-immediate
	Count        int
	Implemented  bool
	Name         string
	ExampleAddrs []uint64
}

// HLEReport is a static inventory of host-shim gaps: imported symbols and
// syscall numbers recovered from immediates in reachable code.
type HLEReport struct {
	NeededLibs []string
	Imports    []ImportSymbol

	HostShimCount    int
	GuestExportCount int
	DefinedCount     int
	UnresolvedCount  int
	UnresolvedRelocs int

	Syscalls        []SyscallSite
	DynamicSyscalls int
	RawSyscallInsts int

	syscallAcc map[int]*SyscallSite
}

const maxSyscallExamples = 8

// NewHLEReport classifies dynamic imports against host shims and linked PRX
// exports. Syscall numbers are filled later by ObserveBlock on reachable code.
func NewHLEReport(loaded *elfloader.LoadedELF, modules []GuestModule) *HLEReport {
	r := &HLEReport{
		syscallAcc: make(map[int]*SyscallSite),
	}
	if loaded == nil {
		return r
	}
	r.NeededLibs = append([]string(nil), loaded.NeededLibs...)

	exportNID := make(map[string]struct{})
	for _, mod := range modules {
		for _, exp := range mod.Exports {
			if exp.Name == "" {
				continue
			}
			exportNID[exp.Name] = struct{}{}
			if nid := elfloader.NIDPrefix(exp.Name); nid != "" {
				exportNID[nid] = struct{}{}
			}
		}
	}
	defined := make(map[string]struct{})
	for _, sym := range loaded.DynSymbols {
		if sym.Name != "" && sym.Address != 0 {
			defined[sym.Name] = struct{}{}
		}
	}

	byName := make(map[string]*ImportSymbol)
	for _, rel := range loaded.Relocations {
		if !isImportReloc(rel) {
			continue
		}
		ent, ok := byName[rel.SymName]
		if !ok {
			ent = &ImportSymbol{
				Name:    rel.SymName,
				Library: loaded.LibraryForNID(rel.SymName),
				Class:   ImportUnresolved,
			}
			if shim, hit := LookupShim(rel.SymName); hit {
				ent.Class = ImportHostShim
				ent.Shim = shim
			} else if _, hit := exportNID[rel.SymName]; hit {
				ent.Class = ImportGuestExport
			} else if nid := elfloader.NIDPrefix(rel.SymName); nid != "" {
				if _, hit := exportNID[nid]; hit {
					ent.Class = ImportGuestExport
				}
			}
			if ent.Class == ImportUnresolved {
				if _, hit := defined[rel.SymName]; hit {
					ent.Class = ImportDefined
				}
			}
			byName[rel.SymName] = ent
		}
		ent.Relocs++
	}

	r.Imports = make([]ImportSymbol, 0, len(byName))
	for _, ent := range byName {
		r.Imports = append(r.Imports, *ent)
		switch ent.Class {
		case ImportHostShim:
			r.HostShimCount++
		case ImportGuestExport:
			r.GuestExportCount++
		case ImportDefined:
			r.DefinedCount++
		default:
			r.UnresolvedCount++
			r.UnresolvedRelocs += ent.Relocs
		}
	}
	sort.Slice(r.Imports, func(i, j int) bool {
		if r.Imports[i].Relocs != r.Imports[j].Relocs {
			return r.Imports[i].Relocs > r.Imports[j].Relocs
		}
		return r.Imports[i].Name < r.Imports[j].Name
	})
	return r
}

func isImportReloc(rel elfloader.Relocation) bool {
	if rel.SymName == "" {
		return false
	}
	switch rel.Type {
	case elfloader.R_X86_64_JUMP_SLOT, elfloader.R_X86_64_GLOB_DAT, elfloader.R_X86_64_64:
		return true
	default:
		return false
	}
}

// ImportShimMap returns PLT/GOT addresses bound to a host shim without writing
// the guest image (safe for analyze).
func ImportShimMap(loaded *elfloader.LoadedELF, shim string) map[uint64]string {
	out := make(map[uint64]string)
	if loaded == nil || shim == "" {
		return out
	}
	for _, rel := range loaded.Relocations {
		if rel.SymName == "" {
			continue
		}
		got, ok := LookupShim(rel.SymName)
		if !ok || got != shim {
			continue
		}
		if rel.PltAddr != 0 {
			out[rel.PltAddr] = shim
		}
		out[rel.Offset] = shim
	}
	return out
}

// ObserveBlock recovers syscall() / SYSCALL immediates from one basic block.
func (r *HLEReport) ObserveBlock(insts []disasm.Instruction, shimMap map[uint64]string) {
	if r == nil {
		return
	}
	for i, inst := range insts {
		nextPC := inst.Address + uint64(inst.Inst.Len)
		op := inst.Inst.Op
		switch op {
		case x86asm.SYSCALL, x86asm.SYSENTER:
			r.RawSyscallInsts++
			if n, ok := immediateInFamily(insts, i, x86asm.RAX); ok {
				r.addSyscall(int(n), inst.Address)
			} else {
				r.DynamicSyscalls++
			}
		case x86asm.CALL, x86asm.JMP:
			target, ok := callOrMemTarget(inst.Inst, nextPC)
			if !ok || shimMap == nil || shimMap[target] != "shim_syscall" {
				continue
			}
			if n, ok := immediateInFamily(insts, i, x86asm.RDI); ok {
				r.addSyscall(int(n), inst.Address)
			} else {
				r.DynamicSyscalls++
			}
		}
	}
}

func (r *HLEReport) addSyscall(num int, addr uint64) {
	if r.syscallAcc == nil {
		r.syscallAcc = make(map[int]*SyscallSite)
	}
	site := r.syscallAcc[num]
	if site == nil {
		name := ImplementedSyscalls[num]
		site = &SyscallSite{
			Number:      num,
			Implemented: name != "",
			Name:        name,
		}
		r.syscallAcc[num] = site
	}
	site.Count++
	if len(site.ExampleAddrs) < maxSyscallExamples {
		site.ExampleAddrs = append(site.ExampleAddrs, addr)
	}
}

// FinalizeSyscalls snapshots the accumulator into the exported Syscalls slice.
func (r *HLEReport) FinalizeSyscalls() {
	if r == nil {
		return
	}
	r.Syscalls = r.Syscalls[:0]
	for _, site := range r.syscallAcc {
		r.Syscalls = append(r.Syscalls, *site)
	}
	sort.Slice(r.Syscalls, func(i, j int) bool {
		if r.Syscalls[i].Count != r.Syscalls[j].Count {
			return r.Syscalls[i].Count > r.Syscalls[j].Count
		}
		return r.Syscalls[i].Number < r.Syscalls[j].Number
	})
}

func callOrMemTarget(inst x86asm.Inst, nextPC uint64) (uint64, bool) {
	if len(inst.Args) == 0 {
		return 0, false
	}
	switch arg := inst.Args[0].(type) {
	case x86asm.Rel:
		return uint64(int64(nextPC) + int64(arg)), true
	case x86asm.Mem:
		if arg.Base == x86asm.RIP {
			return nextPC + uint64(arg.Disp), true
		}
	}
	return 0, false
}

func gpFamily(r x86asm.Reg) x86asm.Reg {
	switch r {
	case x86asm.AL, x86asm.AX, x86asm.EAX, x86asm.RAX:
		return x86asm.RAX
	case x86asm.CL, x86asm.CX, x86asm.ECX, x86asm.RCX:
		return x86asm.RCX
	case x86asm.DL, x86asm.DX, x86asm.EDX, x86asm.RDX:
		return x86asm.RDX
	case x86asm.BL, x86asm.BX, x86asm.EBX, x86asm.RBX:
		return x86asm.RBX
	case x86asm.SPB, x86asm.SP, x86asm.ESP, x86asm.RSP:
		return x86asm.RSP
	case x86asm.BPB, x86asm.BP, x86asm.EBP, x86asm.RBP:
		return x86asm.RBP
	case x86asm.SIB, x86asm.SI, x86asm.ESI, x86asm.RSI:
		return x86asm.RSI
	case x86asm.DIB, x86asm.DI, x86asm.EDI, x86asm.RDI:
		return x86asm.RDI
	default:
		return r
	}
}

func immediateInFamily(insts []disasm.Instruction, idx int, fam x86asm.Reg) (int64, bool) {
	for i := idx - 1; i >= 0; i-- {
		inst := insts[i].Inst
		if v, ok := immWriteFamily(inst, fam); ok {
			return v, true
		}
		if writesFamily(inst, fam) {
			return 0, false
		}
	}
	return 0, false
}

func immWriteFamily(inst x86asm.Inst, fam x86asm.Reg) (int64, bool) {
	switch inst.Op {
	case x86asm.MOV:
		dst, ok := inst.Args[0].(x86asm.Reg)
		if !ok || gpFamily(dst) != fam {
			return 0, false
		}
		if imm, ok := inst.Args[1].(x86asm.Imm); ok {
			return int64(imm), true
		}
	case x86asm.XOR, x86asm.SUB:
		dst, ok0 := inst.Args[0].(x86asm.Reg)
		src, ok1 := inst.Args[1].(x86asm.Reg)
		if ok0 && ok1 && dst == src && gpFamily(dst) == fam {
			return 0, true
		}
	}
	return 0, false
}

func writesFamily(inst x86asm.Inst, fam x86asm.Reg) bool {
	switch inst.Op {
	case x86asm.CALL, x86asm.SYSCALL, x86asm.SYSENTER, x86asm.INT:
		switch fam {
		case x86asm.RAX, x86asm.RCX, x86asm.RDX, x86asm.RSI, x86asm.RDI, x86asm.R8, x86asm.R9:
			return true
		}
		return false
	}
	if r, ok := inst.Args[0].(x86asm.Reg); ok && gpFamily(r) == fam {
		return true
	}
	if inst.Op == x86asm.XCHG {
		if r, ok := inst.Args[1].(x86asm.Reg); ok && gpFamily(r) == fam {
			return true
		}
	}
	return false
}

// ImportSummary is a short stdout block (no per-symbol dump).
func (r *HLEReport) ImportSummary() string {
	if r == nil {
		return ""
	}
	var b strings.Builder
	fmt.Fprintf(&b, "             HLE imports: %d unique (%d host shims, %d linked PRX, %d defined, %d unresolved)\n",
		len(r.Imports), r.HostShimCount, r.GuestExportCount, r.DefinedCount, r.UnresolvedCount)
	if r.UnresolvedCount == 0 {
		return b.String()
	}
	type libCount struct {
		lib    string
		n      int
		relocs int
	}
	byLib := make(map[string]*libCount)
	for i := range r.Imports {
		imp := &r.Imports[i]
		if imp.Class != ImportUnresolved {
			continue
		}
		ent := byLib[imp.Library]
		if ent == nil {
			ent = &libCount{lib: imp.Library}
			byLib[imp.Library] = ent
		}
		ent.n++
		ent.relocs += imp.Relocs
	}
	libs := make([]libCount, 0, len(byLib))
	for _, ent := range byLib {
		libs = append(libs, *ent)
	}
	sort.Slice(libs, func(i, j int) bool {
		if libs[i].n != libs[j].n {
			return libs[i].n > libs[j].n
		}
		return libs[i].lib < libs[j].lib
	})
	limit := 12
	if len(libs) < limit {
		limit = len(libs)
	}
	fmt.Fprintf(&b, "             Unresolved libraries (top %d of %d):\n", limit, len(libs))
	for i := 0; i < limit; i++ {
		fmt.Fprintf(&b, "               %-28s %5d symbols (%d relocs)\n", libs[i].lib, libs[i].n, libs[i].relocs)
	}
	resolvedNames := 0
	for i := range r.Imports {
		if r.Imports[i].Class == ImportUnresolved {
			if _, ok := elfloader.ResolveNID(r.Imports[i].Name); ok {
				resolvedNames++
			}
		}
	}
	if resolvedNames > 0 {
		fmt.Fprintf(&b, "             NID name database: %d / %d unresolved symbols mapped to known functions\n",
			resolvedNames, r.UnresolvedCount)
	}
	return b.String()
}

// SyscallSummary is a short stdout block after reachable emission.
func (r *HLEReport) SyscallSummary() string {
	if r == nil {
		return ""
	}
	r.FinalizeSyscalls()
	var b strings.Builder
	fmt.Fprintf(&b, "             Syscalls (immediate in reachable CFG): %d numbers, %d dynamic sites, %d SYSCALL/SYSENTER ops\n",
		len(r.Syscalls), r.DynamicSyscalls, r.RawSyscallInsts)
	missing := 0
	for _, s := range r.Syscalls {
		if !s.Implemented {
			missing++
		}
	}
	fmt.Fprintf(&b, "             Implemented numbers: %d | ENOSYS numbers: %d\n", len(r.Syscalls)-missing, missing)
	shown := 0
	for _, s := range r.Syscalls {
		if s.Implemented {
			continue
		}
		if shown == 0 {
			b.WriteString("             Unimplemented syscall numbers:\n")
		}
		name := s.Name
		if name == "" {
			name = "-"
		}
		fmt.Fprintf(&b, "               %4d %-16s x%d\n", s.Number, name, s.Count)
		shown++
		if shown >= 20 {
			rest := missing - shown
			if rest > 0 {
				fmt.Fprintf(&b, "               ... %d more in hle_report.txt\n", rest)
			}
			break
		}
	}
	return b.String()
}

// FullString is the on-disk report.
func (r *HLEReport) FullString() string {
	if r == nil {
		return ""
	}
	r.FinalizeSyscalls()
	var b strings.Builder
	b.WriteString("PS4Recomp HLE report\n")
	b.WriteString("====================\n\n")
	b.WriteString("Static inventory of imported symbols (dynamic relocations) and syscall\n")
	b.WriteString("numbers recovered from immediates in reachable code. Runtime dlsym names\n")
	b.WriteString("and syscall numbers computed in registers are not listed.\n\n")

	if len(r.NeededLibs) > 0 {
		b.WriteString("DT_NEEDED:\n")
		for _, lib := range r.NeededLibs {
			fmt.Fprintf(&b, "  %s\n", lib)
		}
		b.WriteByte('\n')
	}

	fmt.Fprintf(&b, "Imports: %d unique names\n", len(r.Imports))
	fmt.Fprintf(&b, "  host shim:     %d\n", r.HostShimCount)
	fmt.Fprintf(&b, "  linked PRX:    %d\n", r.GuestExportCount)
	fmt.Fprintf(&b, "  defined:       %d\n", r.DefinedCount)
	fmt.Fprintf(&b, "  unresolved:    %d (%d relocs)\n\n", r.UnresolvedCount, r.UnresolvedRelocs)

	type libGroup struct {
		lib  string
		syms []ImportSymbol
	}
	byLib := make(map[string]*libGroup)
	var order []string
	for _, imp := range r.Imports {
		if imp.Class != ImportUnresolved {
			continue
		}
		g := byLib[imp.Library]
		if g == nil {
			g = &libGroup{lib: imp.Library}
			byLib[imp.Library] = g
			order = append(order, imp.Library)
		}
		g.syms = append(g.syms, imp)
	}
	sort.Slice(order, func(i, j int) bool {
		if len(byLib[order[i]].syms) != len(byLib[order[j]].syms) {
			return len(byLib[order[i]].syms) > len(byLib[order[j]].syms)
		}
		return order[i] < order[j]
	})
	if len(order) > 0 {
		b.WriteString("Unresolved symbols by library\n")
		for _, lib := range order {
			g := byLib[lib]
			fmt.Fprintf(&b, "\n## %s (%d)\n", lib, len(g.syms))
			for _, s := range g.syms {
				nameDisplay := s.Name
				if resolved, ok := elfloader.ResolveNID(s.Name); ok {
					nameDisplay = fmt.Sprintf("%-28s -> %s", s.Name, resolved)
				}
				fmt.Fprintf(&b, "  %s\trelocs=%d\n", nameDisplay, s.Relocs)
			}
		}
		b.WriteByte('\n')
	}

	b.WriteString("Syscalls\n")
	fmt.Fprintf(&b, "  immediate numbers: %d\n", len(r.Syscalls))
	fmt.Fprintf(&b, "  dynamic sites:     %d\n", r.DynamicSyscalls)
	fmt.Fprintf(&b, "  SYSCALL/SYSENTER:  %d\n\n", r.RawSyscallInsts)
	if len(r.Syscalls) > 0 {
		b.WriteString("  num   name             count  status\n")
		for _, s := range r.Syscalls {
			status := "ENOSYS"
			if s.Implemented {
				status = "implemented"
			}
			name := s.Name
			if name == "" {
				name = "-"
			}
			fmt.Fprintf(&b, "  %-5d %-16s %-6d %s", s.Number, name, s.Count, status)
			if len(s.ExampleAddrs) > 0 {
				b.WriteString("  examples")
				for _, a := range s.ExampleAddrs {
					fmt.Fprintf(&b, " 0x%x", a)
				}
			}
			b.WriteByte('\n')
		}
	}
	return b.String()
}

// WriteFile writes FullString to path.
func (r *HLEReport) WriteFile(path string) error {
	if r == nil {
		return nil
	}
	return os.WriteFile(path, []byte(r.FullString()), 0o644)
}
