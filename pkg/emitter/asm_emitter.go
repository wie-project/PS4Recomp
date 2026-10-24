package emitter

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"slices"
	"strings"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/lifter"
)

type GuestModule struct {
	FileName       string
	Aliases        []string
	Exports        []elfloader.Symbol
	Init           []uint64
	StartAddr      uint64
	EndAddr        uint64
	Seg0Addr       uint64
	Seg0Size       uint64
	EHFrameHdrAddr uint64
	EHFrameHdrSize uint64
	EHFrameAddr    uint64
	EHFrameSize    uint64
	Segments       []*elfloader.Segment
}

func writeFileIfChanged(path string, data []byte) error {
	existing, err := os.ReadFile(path)
	if err == nil && bytes.Equal(existing, data) {
		return nil
	}
	return os.WriteFile(path, data, 0o644)
}

func uniqueModuleNames(fileName string, aliases []string) []string {
	seen := make(map[string]struct{})
	var out []string
	add := func(s string) {
		s = strings.TrimSpace(s)
		if s == "" {
			return
		}
		key := strings.ToLower(filepath.Base(s))
		if _, ok := seen[key]; ok {
			return
		}
		seen[key] = struct{}{}
		out = append(out, filepath.Base(s))
	}
	add(fileName)
	for _, a := range aliases {
		add(a)
	}
	return out
}

// AsmEmitter emits Apple Silicon ARM64 assembly (.s) files from disassembled functions.
type AsmEmitter struct {
	elf          *elfloader.LoadedELF
	disasm       *disasm.Disassembler
	lifter       *lifter.Arm64Lifter
	shimMap      map[uint64]string
	AppDir       string
	Modules      []GuestModule
	ChunkSize    int
	Funcs        []disasm.ReachableFunc
	HLE          *HLEReport
	linesBuf     []string
	knownFuncSet map[uint64]bool
	// unresolved describes every import slot (PLT stub and GOT slot address) that no host
	// shim and no loaded module satisfies. Each one is bound to shim_unresolved_stub.
	unresolved map[uint64]unresolvedImport
	// UnresolvedFuncImports and UnresolvedDataImports count the unresolved import relocations:
	// JUMP_SLOT ones are calls, GLOB_DAT/R_X86_64_64 ones are address loads (a data import is
	// bound to the same stub, so reading it as data yields the stub's address).
	UnresolvedFuncImports int
	UnresolvedDataImports int
	// UnresolvedDataNames lists the unresolved data imports, sorted, each as its name (the NID
	// when the NID database does not know it) and importing library.
	UnresolvedDataNames []string
	// Warnings collects generation-time problems that do not stop the generation (the caller
	// prints them in its summary).
	Warnings []string
}

// maxModuleSegments is the number of segments a module registers with the runtime
// (RecompModuleUnwindInfo.segments, the four of SceKernelModuleInfo).
const maxModuleSegments = 4

// registeredSegments returns the segments of module name that fit the runtime's table, with a
// warning in e.Warnings when some do not.
func (e *AsmEmitter) registeredSegments(name string, segs []*elfloader.Segment) []*elfloader.Segment {
	if len(segs) <= maxModuleSegments {
		return segs
	}
	e.Warnings = append(e.Warnings, fmt.Sprintf("module %s has %d segments; module info reports only the first %d",
		name, len(segs), maxModuleSegments))
	return segs[:maxModuleSegments]
}

// unresolvedImport is what shim_unresolved_stub reports when the guest calls a missing import.
type unresolvedImport struct {
	nid  string // NID portion of the dynsym name
	name string // plaintext symbol name when the NID database knows it
	lib  string // importing library as named by DT_SCE_IMPORT_LIB
}

// unresolvedStubShim is the host function every unresolved import reports through.
const unresolvedStubShim = "shim_unresolved_stub"

// collectUnresolvedImports finds the import slots that remain self-bound after module
// relocation: elfloader.ResolveModuleRelocations leaves a slot pointing at its own PLT stub
// (or at itself when there is no PLT) when neither a module export nor the canary applies, and
// the emitter binds a host shim to the slots it can serve. What is left would jump to itself
// forever, so it is routed to shim_unresolved_stub with its name instead.
func collectUnresolvedImports(loaded *elfloader.LoadedELF, shimMap map[uint64]string) (out map[uint64]unresolvedImport, funcs, data int, dataNames []string) {
	out = make(map[uint64]unresolvedImport)
	seenData := make(map[string]bool)
	for _, rel := range loaded.Relocations {
		switch rel.Type {
		case elfloader.R_X86_64_JUMP_SLOT, elfloader.R_X86_64_GLOB_DAT, elfloader.R_X86_64_64:
		default:
			continue
		}
		if rel.SymName == "" || rel.Offset+8 > uint64(len(loaded.MemoryImage)) {
			continue
		}
		if _, ok := LookupShim(rel.SymName); ok {
			continue
		}
		slot := binary.LittleEndian.Uint64(loaded.MemoryImage[rel.Offset : rel.Offset+8])
		if slot != rel.Offset && (rel.PltAddr == 0 || slot != rel.PltAddr) {
			continue
		}
		imp := unresolvedImport{nid: elfloader.NIDPrefix(rel.SymName), lib: loaded.LibraryForNID(rel.SymName)}
		if canon, ok := elfloader.ResolveNID(rel.SymName); ok {
			imp.name = canon
		}
		if rel.Type == elfloader.R_X86_64_JUMP_SLOT {
			funcs++
		} else {
			data++
			label := imp.name
			if label == "" {
				label = imp.nid
			}
			if imp.lib != "" {
				label += " (" + imp.lib + ")"
			}
			if !seenData[label] {
				seenData[label] = true
				dataNames = append(dataNames, label)
			}
		}
		for _, addr := range []uint64{rel.PltAddr, rel.Offset} {
			if addr == 0 {
				continue
			}
			if _, bound := shimMap[addr]; bound {
				continue
			}
			if _, dup := out[addr]; !dup {
				out[addr] = imp
			}
		}
	}
	slices.Sort(dataNames)
	return out, funcs, data, dataNames
}

// NewAsmEmitter creates a new ARM64 assembly emitter.
func NewAsmEmitter(loaded *elfloader.LoadedELF, d *disasm.Disassembler, l *lifter.Arm64Lifter) *AsmEmitter {
	shimMap := make(map[uint64]string)
	bindAddr := func(name string, addr uint64) {
		if name == "" || addr == 0 {
			return
		}
		if shim, ok := LookupShim(name); ok {
			shimMap[addr] = shim
		}
	}
	for _, rel := range loaded.Relocations {
		if rel.Type == elfloader.R_X86_64_JUMP_SLOT && rel.Offset+8 <= uint64(len(loaded.MemoryImage)) {
			if rel.SymName != "" {
				if shim, ok := LookupShim(rel.SymName); ok {
					if rel.PltAddr != 0 {
						shimMap[rel.PltAddr] = shim
					}
					shimMap[rel.Offset] = shim
					binary.LittleEndian.PutUint64(loaded.MemoryImage[rel.Offset:rel.Offset+8], rel.Offset)
					continue
				}
			}
			continue
		}
		if rel.SymName == "" {
			continue
		}
		shim, ok := LookupShim(rel.SymName)
		if !ok {
			continue
		}
		if rel.PltAddr != 0 {
			shimMap[rel.PltAddr] = shim
		}
		shimMap[rel.Offset] = shim
	}
	for _, sym := range loaded.Symbols {
		bindAddr(sym.Name, sym.Address)
	}
	for _, sym := range loaded.DynSymbols {
		bindAddr(sym.Name, sym.Address)
	}

	unresolved, unresFuncs, unresData, unresDataNames := collectUnresolvedImports(loaded, shimMap)
	for addr := range unresolved {
		shimMap[addr] = unresolvedStubShim
	}

	return &AsmEmitter{
		elf:        loaded,
		disasm:     d,
		lifter:     l,
		shimMap:    shimMap,
		unresolved: unresolved,

		UnresolvedFuncImports: unresFuncs,
		UnresolvedDataImports: unresData,
		UnresolvedDataNames:   unresDataNames,
	}
}

// ResolveEntryAddress dynamically resolves the primary guest entry point.
func (e *AsmEmitter) ResolveEntryAddress() (uint64, string, error) {
	if e.elf.EntryPoint != 0 {
		return e.elf.EntryPoint, "_start", nil
	}
	if mainSym, ok := e.elf.SymbolByName["main"]; ok && mainSym.Address != 0 {
		return mainSym.Address, "main", nil
	}
	return 0, "", fmt.Errorf("ELF has no entry point and no main symbol")
}

// EmitAll generates all .s assembly files and runtime C files in outDir.
func (e *AsmEmitter) EmitAll(outDir string) ([]string, error) {
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return nil, err
	}

	var buildFiles []string

	mainPath := filepath.Join(outDir, "main.c")
	if err := e.EmitMainRunner(mainPath); err != nil {
		return nil, fmt.Errorf("failed to emit main.c: %w", err)
	}
	buildFiles = append(buildFiles, mainPath)

	binPath := filepath.Join(outDir, "guest_image.bin")
	if err := e.EmitGuestImage(binPath); err != nil {
		return nil, fmt.Errorf("failed to emit guest_image.bin: %w", err)
	}

	trampPath := filepath.Join(outDir, "trampoline_arm64.s")
	if err := e.EmitTrampoline(trampPath); err != nil {
		return nil, fmt.Errorf("failed to emit trampoline_arm64.s: %w", err)
	}
	buildFiles = append(buildFiles, trampPath)

	codeFiles, err := e.EmitChunkedAsm(outDir, 15000)
	if err != nil {
		return nil, fmt.Errorf("failed to emit chunked assembly: %w", err)
	}
	buildFiles = append(buildFiles, codeFiles...)

	modPath := filepath.Join(outDir, "guest_modules.c")
	if err := e.EmitGuestModules(modPath); err != nil {
		return nil, fmt.Errorf("failed to emit guest_modules.c: %w", err)
	}
	buildFiles = append(buildFiles, modPath)
	synthPath := strings.TrimSuffix(modPath, ".c") + "_synth.s"
	if _, err := os.Stat(synthPath); err == nil {
		buildFiles = append(buildFiles, synthPath)
	}

	return buildFiles, nil
}

// EmitGuestImage writes the raw loaded guest memory image.
func (e *AsmEmitter) EmitGuestImage(path string) error {
	return writeFileIfChanged(path, e.elf.MemoryImage)
}

func (e *AsmEmitter) functionAddrs() []uint64 {
	if len(e.Funcs) > 0 {
		addrs := make([]uint64, len(e.Funcs))
		for i, f := range e.Funcs {
			addrs[i] = f.Addr
		}
		return addrs
	}
	if e.disasm == nil {
		return nil
	}
	addrs := make([]uint64, 0, len(e.disasm.Functions))
	for addr := range e.disasm.Functions {
		addrs = append(addrs, addr)
	}
	slices.Sort(addrs)
	return addrs
}

func (e *AsmEmitter) instCountByAddr() map[uint64]int {
	if len(e.Funcs) > 0 {
		m := make(map[uint64]int, len(e.Funcs))
		for _, f := range e.Funcs {
			m[f.Addr] = f.Insts
		}
		return m
	}
	if e.disasm == nil {
		return nil
	}
	m := make(map[uint64]int, len(e.disasm.Functions))
	for addr, fn := range e.disasm.Functions {
		count := 0
		if fn != nil {
			for _, b := range fn.Blocks {
				count += len(b.Insts)
			}
		}
		m[addr] = count
	}
	return m
}

func (e *AsmEmitter) loadFunction(addr uint64) (*disasm.Function, error) {
	if e.disasm.Functions != nil {
		if fn := e.disasm.Functions[addr]; fn != nil {
			return fn, nil
		}
	}
	fn, _, err := e.disasm.DisasmFunction(addr)
	return fn, err
}

// EmitChunkedAsm partitions recompiled functions across .s assembly files.
func (e *AsmEmitter) EmitChunkedAsm(outDir string, targetBudget int) ([]string, error) {
	fnAddrs := e.functionAddrs()

	if targetBudget <= 0 {
		targetBudget = 15000
	}
	maxFuncsPerChunk := e.ChunkSize
	if maxFuncsPerChunk <= 0 {
		maxFuncsPerChunk = 250
	}

	var chunks [][]uint64
	var currentChunk []uint64
	currentInsts := 0

	insts := e.instCountByAddr()
	for _, addr := range fnAddrs {
		nInsts := insts[addr]
		if len(currentChunk) > 0 && (currentInsts+nInsts > targetBudget || len(currentChunk) >= maxFuncsPerChunk) {
			chunks = append(chunks, currentChunk)
			currentChunk = nil
			currentInsts = 0
		}
		currentChunk = append(currentChunk, addr)
		currentInsts += nInsts
	}
	if len(currentChunk) > 0 {
		chunks = append(chunks, currentChunk)
	}
	if len(chunks) == 0 {
		chunks = append(chunks, []uint64{})
	}

	var generatedFiles []string

	for chunkIdx, chunkAddrs := range chunks {
		chunkFileName := fmt.Sprintf("code_%03d.s", chunkIdx)
		chunkPath := filepath.Join(outDir, chunkFileName)
		regPath := filepath.Join(outDir, fmt.Sprintf("code_%03d_reg.c", chunkIdx))
		if err := e.emitSingleAsmChunk(chunkPath, chunkIdx, chunkAddrs); err != nil {
			return nil, fmt.Errorf("failed to emit %s: %w", chunkFileName, err)
		}
		generatedFiles = append(generatedFiles, chunkPath, regPath)
	}

	dispatchPath := filepath.Join(outDir, "dispatch.c")
	if err := e.EmitDispatch(dispatchPath, len(chunks)); err != nil {
		return nil, fmt.Errorf("failed to emit dispatch.c: %w", err)
	}
	generatedFiles = append(generatedFiles, dispatchPath)

	return generatedFiles, nil
}

func (e *AsmEmitter) emitSingleAsmChunk(path string, chunkIdx int, chunkAddrs []uint64) error {
	var buf bytes.Buffer
	w := bufio.NewWriter(&buf)

	if _, err := fmt.Fprintf(w, "// Chunk %d (%d functions)\n%s\n.p2align 2\n\n", chunkIdx, len(chunkAddrs), guestTextSection); err != nil {
		return err
	}

	for _, addr := range chunkAddrs {
		if shim, isShimmed := e.shimMap[addr]; isShimmed {
			// Forward directly to host shim via bridge
			writeShimStub(w, fmt.Sprintf("_fn_0x%x", addr), shim, addr)
			continue
		}

		fn, err := e.loadFunction(addr)
		if err != nil {
			return fmt.Errorf("0x%x: %w", addr, err)
		}
		if fn == nil {
			continue
		}

		fmt.Fprintf(w, ".globl _fn_0x%x\n_fn_0x%x:\n", addr, addr)
		fmt.Fprintf(w, "    str x30, [sp, #-16]!\n")

		for bi, blockAddr := range fn.BlockOrder {
			block := fn.Blocks[blockAddr]
			if block == nil {
				continue
			}

			fmt.Fprintf(w, "\nL_fn_0x%x_loc_0x%x:\n", addr, blockAddr)
			e.lifter.ResetFlags()

			for _, inst := range block.Insts {
				nextPC := inst.Address + uint64(inst.Inst.Len)
				lines, err := e.lifter.LiftInstructionToBuf(inst, nextPC, fn, e.linesBuf[:0])
				if err != nil {
					fmt.Fprintf(w, "    // 0x%x: %s [UNSUPPORTED: %v]\n    brk #0\n", inst.Address, inst.Inst.String(), err)
				} else {
					e.linesBuf = lines
					for _, line := range lines {
						w.WriteString(line)
						w.WriteByte('\n')
					}
				}
			}
			// A block that falls through hands the next one CF in the block-boundary sense.
			if n := len(block.Insts); n > 0 {
				for _, line := range e.lifter.EndBlock(block.Insts[n-1]) {
					w.WriteString(line)
					w.WriteByte('\n')
				}
				// The next emitted block is not always the fall-through successor
				// (overlapping instruction streams): branch to it then.
				var next uint64
				hasNext := false
				for _, a := range fn.BlockOrder[bi+1:] {
					if fn.Blocks[a] != nil {
						next, hasNext = a, true
						break
					}
				}
				for _, line := range e.lifter.FallThrough(fn, block.Insts[n-1], next, hasNext) {
					w.WriteString(line)
					w.WriteByte('\n')
				}
			}
		}
		w.WriteString("    ldr x30, [sp], #16\n    ret\n\n")
	}

	// Chunk registration function in C (for dispatch table)
	regFileName := strings.TrimSuffix(path, ".s") + "_reg.c"
	if err := e.emitChunkRegistration(regFileName, chunkIdx, chunkAddrs); err != nil {
		return err
	}

	if err := w.Flush(); err != nil {
		return err
	}
	return writeFileIfChanged(path, buf.Bytes())
}

// guestTextSection holds all code that runs with the guest registers pinned in host registers:
// recompiled functions, shim stubs and the indirect-branch entry points of the trampoline. The
// runtime tells guest code from host code by this section (recomp_pc_in_guest_code).
const guestTextSection = ".section __TEXT,__recomp_guest,regular,pure_instructions"

// writeShimStub emits the guest-callable symbol sym of a host shim for guest address addr:
// guest code calls it like any recompiled function, and it hands the shim and addr (stored
// to ctx->rip, so diagnostics name the shim a thread is in) to _recomp_call_shim.
func writeShimStub(w io.Writer, sym, shim string, addr uint64) {
	fmt.Fprintf(w, ".globl %s\n%s:\n", sym, sym)
	fmt.Fprintf(w, "    adrp x0, _%s@PAGE\n", shim)
	fmt.Fprintf(w, "    add  x0, x0, _%s@PAGEOFF\n", shim)
	fmt.Fprintf(w, "    movz x1, #0x%x\n", addr&0xffff)
	for shift := 16; shift < 64; shift += 16 {
		if chunk := (addr >> shift) & 0xffff; chunk != 0 {
			fmt.Fprintf(w, "    movk x1, #0x%x, lsl #%d\n", chunk, shift)
		}
	}
	fmt.Fprintf(w, "    b    _recomp_call_shim\n\n")
}

func (e *AsmEmitter) emitChunkRegistration(path string, chunkIdx int, chunkAddrs []uint64) error {
	var buf bytes.Buffer
	w := bufio.NewWriter(&buf)

	fmt.Fprintf(w, "#include \"recomp_runtime.h\"\n\n")

	for _, addr := range chunkAddrs {
		fmt.Fprintf(w, "extern void fn_0x%x(GuestContext *ctx);\n", addr)
	}

	fmt.Fprintf(w, "\nvoid recomp_init_dispatch_chunk_%d(void) {\n", chunkIdx)
	for _, addr := range chunkAddrs {
		fmt.Fprintf(w, "    recomp_register_fn(0x%xULL, fn_0x%x);\n", addr, addr)
	}
	fmt.Fprintf(w, "}\n")

	if err := w.Flush(); err != nil {
		return err
	}
	return writeFileIfChanged(path, buf.Bytes())
}

// EmitTrampoline writes the ARM64 assembly bridge between the host C runtime and guest
// assembly (trampolineAsm).
func (e *AsmEmitter) EmitTrampoline(path string) error {
	return writeFileIfChanged(path, []byte(trampolineAsm))
}

// EmitDispatch ties all chunk registrations together.
func (e *AsmEmitter) EmitDispatch(path string, numChunks int) error {
	var buf bytes.Buffer
	w := bufio.NewWriter(&buf)

	fmt.Fprintf(w, "#include \"recomp_runtime.h\"\n\nvoid recomp_register_guest_modules(void);\n\n")

	for i := range numChunks {
		fmt.Fprintf(w, "void recomp_init_dispatch_chunk_%d(void);\n", i)
	}

	fmt.Fprintf(w, "\nvoid recomp_init_dispatch_table(void) {\n")
	for i := range numChunks {
		fmt.Fprintf(w, "    recomp_init_dispatch_chunk_%d();\n", i)
	}
	fmt.Fprintf(w, "    recomp_register_guest_modules();\n}\n")

	if err := w.Flush(); err != nil {
		return err
	}
	return writeFileIfChanged(path, buf.Bytes())
}

// EmitMainRunner emits the main.c entry point.
func (e *AsmEmitter) EmitMainRunner(path string) error {
	entryAddr, entryName, err := e.ResolveEntryAddress()
	if err != nil {
		return err
	}

	vfsInitArg := "NULL"
	if e.AppDir != "" {
		vfsInitArg = fmt.Sprintf("%q", e.AppDir)
	}

	content := fmt.Sprintf(`#include "recomp_runtime.h"
#include "ps4_vfs.h"
#include "ps4_sysmodule.h"

extern void recomp_init_dispatch_table(void);
extern void recomp_enter_guest(GuestContext *ctx, uint64_t target_fn);

typedef struct {
    int argc;
    char **argv;
} GuestMainArgs;

// Guest code runs on a secondary host thread so the host main thread stays free
// for AppKit (window creation, event pumping, dialogs).
static void *guest_main(void *p) {
    GuestMainArgs *a = (GuestMainArgs *)p;
    int argc = a->argc;
    char **argv = a->argv;
    printf("[ps4-recomp] Initializing runtime (ARM64 Assembly Backend)...\n");
    ps4_vfs_init(%s);
    recomp_init_dispatch_table();
    recomp_register_guest_modules();

    const char *prog_name = (argc > 0 && argv[0]) ? argv[0] : "/app0/eboot.bin";
    GuestContext *ctx = recomp_init_runtime_file("guest_image.bin", 0, prog_name);
    if (!ctx) {
        fprintf(stderr, "[ps4-recomp] Failed to allocate guest memory or load guest_image.bin\n");
        exit(1);
    }

    recomp_init_tls(ctx, 0x%xULL, 0x%xULL, 0x%xULL, 0x%xULL);

    recomp_init_process_param(ctx, 0x%xULL);

    recomp_modules_start_preloaded(ctx);

    recomp_setup_entry_args(ctx, argc, argv, 0x%xULL);

    printf("[ps4-recomp] Executing %%s (0x%%x) via ARM64 assembly...\n", %q, (unsigned int)0x%xULL);
    extern void fn_0x%x(void);
    recomp_enter_guest(ctx, (uint64_t)fn_0x%x);

    printf("[ps4-recomp] Execution complete.\n");
    recomp_free_runtime(ctx);
    exit(0);
    return NULL;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    static GuestMainArgs args;
    args.argc = argc;
    args.argv = argv;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024 * 1024);
    // Startup settings (language, PS4 Pro mode, upscaling, FPS HUD) are resolved before the
    // guest starts; the dialog needs the application directory for the language list.
    ps4_vfs_init(%s);
    ps4_host_settings_init();
    pthread_t guest_thread;
    if (pthread_create(&guest_thread, &attr, guest_main, &args) != 0) {
        fprintf(stderr, "[ps4-recomp] Failed to start guest thread\n");
        return 1;
    }
    pthread_attr_destroy(&attr);
    ps4_host_run_main_loop();
    return 0;
}
`, vfsInitArg, e.elf.TLSVaddr, e.elf.TLSFilesz, e.elf.TLSMemsz, e.elf.TLSAlign, e.elf.ProcParamAddr, entryAddr, entryName, entryAddr, entryAddr, entryAddr, vfsInitArg)

	return writeFileIfChanged(path, []byte(content))
}

// ownSegments returns the segments of the main image itself. MergeImages appends the
// segments of merged companion modules (e.g. libSceFios2) to the main image, but the eboot's
// module info must list only its own mapped segments.
func ownSegments(elf *elfloader.LoadedELF) []*elfloader.Segment {
	end := elf.OrigMaxVAddr
	if end == 0 {
		end = elf.MaxVAddr
	}
	var segs []*elfloader.Segment
	for _, s := range elf.Segments {
		if s.Vaddr >= elf.OrigMinVAddr && s.Vaddr < end {
			segs = append(segs, s)
		}
	}
	return segs
}

// EmitGuestModules writes the runtime export tables, synthetic shims, and unwind registrations.
func (e *AsmEmitter) EmitGuestModules(path string) error {
	var buf bytes.Buffer
	w := bufio.NewWriter(&buf)

	fmt.Fprintf(w, "#include \"recomp_runtime.h\"\n#include \"ps4_sysmodule.h\"\n\n")

	// 1. Sort shimMap keys
	var sortedShimAddrs []uint64
	for addr := range e.shimMap {
		sortedShimAddrs = append(sortedShimAddrs, addr)
	}
	slices.Sort(sortedShimAddrs)

	shimAddr := make(map[string]uint64)
	for _, addr := range sortedShimAddrs {
		shim := e.shimMap[addr]
		for name, target := range CanonicalShims {
			if target == shim {
				if _, exists := shimAddr[name]; !exists {
					shimAddr[name] = addr
				}
			}
		}
	}

	nextSynth := uint64(0x1000)
	if e.elf != nil {
		nextSynth = (e.elf.MaxVAddr + 0xFFF) &^ 0xFFF
		if nextSynth < 0x1000 {
			nextSynth = 0x1000
		}
	}

	type synthEntry struct {
		addr uint64
		shim string
	}
	var synthList []synthEntry
	var synthRegs []string

	shimNames := make([]string, 0, len(CanonicalShims))
	for name := range CanonicalShims {
		shimNames = append(shimNames, name)
	}
	slices.Sort(shimNames)

	for _, name := range shimNames {
		if _, ok := shimAddr[name]; ok {
			continue
		}
		shimAddr[name] = nextSynth
		shimFn := CanonicalShims[name]
		synthList = append(synthList, synthEntry{addr: nextSynth, shim: shimFn})
		synthRegs = append(synthRegs, fmt.Sprintf("    extern void fn_0x%x(GuestContext *ctx);\n    recomp_register_fn(0x%xULL, fn_0x%x);\n", nextSynth, nextSynth, nextSynth))
		nextSynth += 16
	}

	// Forward declarations and weak fallback implementations for host shims
	uniqueShims := make(map[string]bool)
	uniqueShims[unresolvedStubShim] = true
	for _, target := range CanonicalShims {
		uniqueShims[target] = true
	}
	sortedShims := make([]string, 0, len(uniqueShims))
	for s := range uniqueShims {
		sortedShims = append(sortedShims, s)
	}
	slices.Sort(sortedShims)

	w.WriteString("// Forward declarations and weak fallback implementations for host shims\n")
	for _, s := range sortedShims {
		fmt.Fprintf(w, "void %s(GuestContext *ctx);\n", s)
	}
	w.WriteString("\n// Weak fallback implementations\n")
	for _, s := range sortedShims {
		if s == unresolvedStubShim {
			continue
		}
		fmt.Fprintf(w, "__attribute__((weak)) void %s(GuestContext *ctx) { recomp_unimplemented_shim(ctx, %q); }\n", s, s)
	}
	w.WriteString("\n")

	w.WriteString("static const RecompModuleExport recomp_host_shim_exports[] = {\n")
	for _, name := range shimNames {
		fmt.Fprintf(w, "    { %q, 0x%xULL },\n", name, shimAddr[name])
	}
	w.WriteString("    { NULL, 0 }\n};\n\n")

	for i, mod := range e.Modules {
		fmt.Fprintf(w, "static const RecompModuleExport recomp_module_exports_%d[] = {\n", i)
		for _, exp := range mod.Exports {
			fmt.Fprintf(w, "    { %q, 0x%xULL },\n", exp.Name, exp.Address)
		}
		w.WriteString("    { NULL, 0 }\n};\n\n")

		if len(mod.Init) > 0 {
			fmt.Fprintf(w, "static const uint64_t recomp_module_init_%d[] = {\n", i)
			for _, addr := range mod.Init {
				fmt.Fprintf(w, "    0x%xULL,\n", addr)
			}
			w.WriteString("};\n\n")
		}
	}

	w.WriteString("void recomp_register_guest_modules(void) {\n")
	unresolvedAddrs := make([]uint64, 0, len(e.unresolved))
	for addr := range e.unresolved {
		unresolvedAddrs = append(unresolvedAddrs, addr)
	}
	slices.Sort(unresolvedAddrs)
	for _, addr := range unresolvedAddrs {
		imp := e.unresolved[addr]
		fmt.Fprintf(w, "    recomp_register_unresolved(0x%xULL, %q, %q, %q);\n", addr, imp.nid, imp.name, imp.lib)
	}
	// Slots that are not discovered functions (GOT entries) still need a registered target;
	// the discovered ones (PLT stubs) get their stub from the code chunks.
	isFunc := make(map[uint64]bool)
	for _, a := range e.functionAddrs() {
		isFunc[a] = true
	}
	var unresolvedSynth []uint64
	for _, addr := range unresolvedAddrs {
		if isFunc[addr] {
			continue
		}
		unresolvedSynth = append(unresolvedSynth, addr)
		fmt.Fprintf(w, "    extern void fn_0x%x(GuestContext *ctx);\n    recomp_register_fn(0x%xULL, fn_0x%x);\n", addr, addr, addr)
	}
	for _, line := range synthRegs {
		w.WriteString(line)
	}
	hostNames := []string{"libc.prx", "libSceLibcInternal.prx", "libkernel.prx", "libSceFios2.prx"}
	for _, name := range hostNames {
		fmt.Fprintf(w, "    recomp_module_register(%q, recomp_host_shim_exports);\n", name)
	}

	if e.elf != nil {
		mainName := e.elf.FileName
		if mainName == "" {
			mainName = "eboot.bin"
		}
		startAddr := e.elf.OrigMinVAddr
		endAddr := e.elf.OrigMaxVAddr
		if endAddr == 0 {
			endAddr = e.elf.MaxVAddr
		}
		mainSegs := e.registeredSegments(mainName, ownSegments(e.elf))
		numSegs := len(mainSegs)
		var segsInit strings.Builder
		for si := 0; si < numSegs; si++ {
			s := mainSegs[si]
			prot := 0
			if s.Flags&1 != 0 {
				prot |= 1
			}
			if s.Flags&2 != 0 {
				prot |= 2
			}
			if s.Flags&4 != 0 {
				prot |= 4
			}
			fmt.Fprintf(&segsInit, "            { .addr = 0x%xULL, .size = 0x%xULL, .prot = %d },\n", s.Vaddr, s.Memsz, prot)
		}
		fmt.Fprintf(w, "    static const RecompModuleUnwindInfo eboot_unwind = {\n"+
			"        .name = %q,\n"+
			"        .start_addr = 0x%xULL,\n"+
			"        .end_addr = 0x%xULL,\n"+
			"        .seg0_addr = 0x%xULL,\n"+
			"        .seg0_size = 0x%xULL,\n"+
			"        .eh_frame_hdr_addr = 0x%xULL,\n"+
			"        .eh_frame_hdr_size = 0x%xULL,\n"+
			"        .eh_frame_addr = 0x%xULL,\n"+
			"        .eh_frame_size = 0x%xULL,\n"+
			"        .num_segments = %d,\n"+
			"        .segments = {\n%s"+
			"        },\n"+
			"    };\n"+
			"    recomp_module_register_unwind_info(&eboot_unwind);\n",
			mainName, startAddr, endAddr, e.elf.Seg0Addr, e.elf.Seg0Size,
			e.elf.EHFrameHdrAddr, e.elf.EHFrameHdrSize, e.elf.EHFrameAddr, e.elf.EHFrameSize,
			numSegs, segsInit.String())
	}

	for i, mod := range e.Modules {
		names := uniqueModuleNames(mod.FileName, mod.Aliases)
		for _, name := range names {
			fmt.Fprintf(w, "    recomp_module_register(%q, recomp_module_exports_%d);\n", name, i)
			if len(mod.Init) > 0 {
				fmt.Fprintf(w, "    recomp_module_register_init(%q, recomp_module_init_%d, %d);\n", name, i, len(mod.Init))
			}
		}
		modSegs := e.registeredSegments(mod.FileName, mod.Segments)
		modNumSegs := len(modSegs)
		var modSegsInit strings.Builder
		for si := 0; si < modNumSegs; si++ {
			s := modSegs[si]
			prot := 0
			if s.Flags&1 != 0 {
				prot |= 1
			}
			if s.Flags&2 != 0 {
				prot |= 2
			}
			if s.Flags&4 != 0 {
				prot |= 4
			}
			fmt.Fprintf(&modSegsInit, "            { .addr = 0x%xULL, .size = 0x%xULL, .prot = %d },\n", s.Vaddr, s.Memsz, prot)
		}
		fmt.Fprintf(w, "    static const RecompModuleUnwindInfo mod_%d_unwind = {\n"+
			"        .name = %q,\n"+
			"        .start_addr = 0x%xULL,\n"+
			"        .end_addr = 0x%xULL,\n"+
			"        .seg0_addr = 0x%xULL,\n"+
			"        .seg0_size = 0x%xULL,\n"+
			"        .eh_frame_hdr_addr = 0x%xULL,\n"+
			"        .eh_frame_hdr_size = 0x%xULL,\n"+
			"        .eh_frame_addr = 0x%xULL,\n"+
			"        .eh_frame_size = 0x%xULL,\n"+
			"        .num_segments = %d,\n"+
			"        .segments = {\n%s"+
			"        },\n"+
			"    };\n"+
			"    recomp_module_register_unwind_info(&mod_%d_unwind);\n",
			i, mod.FileName, mod.StartAddr, mod.EndAddr, mod.Seg0Addr, mod.Seg0Size,
			mod.EHFrameHdrAddr, mod.EHFrameHdrSize, mod.EHFrameAddr, mod.EHFrameSize,
			modNumSegs, modSegsInit.String(), i)
	}

	w.WriteString("}\n")
	if err := w.Flush(); err != nil {
		return err
	}
	if err := writeFileIfChanged(path, buf.Bytes()); err != nil {
		return err
	}

	// 2. Write synth_shims.s
	synthPath := strings.TrimSuffix(path, ".c") + "_synth.s"
	var sBuf bytes.Buffer
	sw := bufio.NewWriter(&sBuf)
	sw.WriteString("// Synthetic shims ARM64 bridge\n" + guestTextSection + "\n.p2align 2\n\n")
	for _, entry := range synthList {
		writeShimStub(sw, fmt.Sprintf("_fn_0x%x", entry.addr), entry.shim, entry.addr)
	}
	for _, addr := range unresolvedSynth {
		writeShimStub(sw, fmt.Sprintf("_fn_0x%x", addr), unresolvedStubShim, addr)
	}
	if err := sw.Flush(); err != nil {
		return err
	}
	return writeFileIfChanged(synthPath, sBuf.Bytes())
}
