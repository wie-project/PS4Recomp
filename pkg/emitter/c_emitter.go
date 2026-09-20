package emitter

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strings"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/lifter"

	"golang.org/x/arch/x86/x86asm"
)

// CanonicalShims maps library/kernel symbol names to host runtime shims.
var CanonicalShims = map[string]string{
	"sceKernelUsleep":           "shim_sceKernelUsleep",
	"sysconf":                   "shim_sysconf",
	"open":                      "shim_open",
	"fcntl":                     "shim_fcntl",
	"__error":                   "shim_error",
	"mmap":                      "shim_mmap",
	"munmap":                    "shim_munmap",
	"madvise":                   "shim_madvise",
	"sigprocmask":               "shim_sigprocmask",
	"sigaction":                 "shim_sigaction",
	"fstat":                     "shim_fstat",
	"close":                     "shim_close",
	"read":                      "shim_read",
	"readv":                     "shim_readv",
	"write":                     "shim_write",
	"writev":                    "shim_writev",
	"ioctl":                     "shim_ioctl",
	"nanosleep":                 "shim_nanosleep",
	"lseek":                     "shim_lseek",
	"exit":                      "shim_exit",
	"poll":                      "shim_poll",
	"raise":                     "shim_raise",
	"sched_yield":               "shim_sched_yield",
	"pthread_create":            "shim_pthread_create",
	"pthread_join":              "shim_pthread_join",
	"pthread_detach":            "shim_pthread_detach",
	"pthread_self":              "shim_pthread_self",
	"pthread_equal":             "shim_pthread_equal",
	"pthread_once":              "shim_pthread_once",
	"pthread_key_create":        "shim_pthread_key_create",
	"pthread_setspecific":       "shim_pthread_setspecific",
	"pthread_getspecific":       "shim_pthread_getspecific",
	"pthread_mutex_init":        "shim_pthread_mutex_init",
	"pthread_mutex_lock":        "shim_pthread_mutex_lock",
	"pthread_mutex_trylock":     "shim_pthread_mutex_trylock",
	"pthread_mutex_unlock":      "shim_pthread_mutex_unlock",
	"pthread_mutex_destroy":     "shim_pthread_mutex_destroy",
	"pthread_mutexattr_init":    "shim_pthread_mutexattr_init",
	"pthread_mutexattr_settype": "shim_pthread_mutexattr_settype",
	"pthread_mutexattr_destroy": "shim_pthread_mutexattr_destroy",
	"pthread_cond_init":         "shim_pthread_cond_init",
	"pthread_cond_wait":         "shim_pthread_cond_wait",
	"pthread_cond_timedwait":    "shim_pthread_cond_timedwait",
	"pthread_cond_signal":       "shim_pthread_cond_signal",
	"pthread_cond_broadcast":    "shim_pthread_cond_broadcast",
	"pthread_cond_destroy":      "shim_pthread_cond_destroy",
	"pthread_rwlock_rdlock":     "shim_pthread_rwlock_rdlock",
	"pthread_rwlock_wrlock":     "shim_pthread_rwlock_wrlock",
	"pthread_rwlock_unlock":     "shim_pthread_rwlock_unlock",
	"syscall":                   "shim_syscall",
	// Direct Memory
	"sceKernelAllocateDirectMemory": "shim_sceKernelAllocateDirectMemory",
	"sceKernelGetDirectMemorySize":  "shim_sceKernelGetDirectMemorySize",
	"sceKernelMapDirectMemory":      "shim_sceKernelMapDirectMemory",
	"sceKernelReleaseDirectMemory":  "shim_sceKernelReleaseDirectMemory",
	// Event Queue
	"sceKernelCreateEqueue":         "shim_sceKernelCreateEqueue",
	"sceKernelDeleteEqueue":         "shim_sceKernelDeleteEqueue",
	"sceKernelWaitEqueue":           "shim_sceKernelWaitEqueue",
	// VideoOut display
	"sceVideoOutOpen":               "shim_sceVideoOutOpen",
	"sceVideoOutClose":              "shim_sceVideoOutClose",
	"sceVideoOutSetBufferAttribute": "shim_sceVideoOutSetBufferAttribute",
	"sceVideoOutRegisterBuffers":    "shim_sceVideoOutRegisterBuffers",
	"sceVideoOutSetFlipRate":        "shim_sceVideoOutSetFlipRate",
	"sceVideoOutAddFlipEvent":       "shim_sceVideoOutAddFlipEvent",
	"sceVideoOutSubmitFlip":         "shim_sceVideoOutSubmitFlip",
	"sceVideoOutGetFlipStatus":      "shim_sceVideoOutGetFlipStatus",
}

// LookupShim looks up a shim name for a symbol name, stripping leading underscores if needed.
func LookupShim(name string) (string, bool) {
	if shim, ok := CanonicalShims[name]; ok {
		return shim, true
	}
	stripped := strings.TrimPrefix(name, "_")
	if shim, ok := CanonicalShims[stripped]; ok {
		return shim, true
	}
	return "", false
}

// CEmitter emits C source files from disassembled functions.
type CEmitter struct {
	elf     *elfloader.LoadedELF
	disasm  *disasm.Disassembler
	lifter  *lifter.Lifter
	shimMap map[uint64]string
	AppDir  string
}

// NewCEmitter creates a new C emitter.
func NewCEmitter(loaded *elfloader.LoadedELF, d *disasm.Disassembler, l *lifter.Lifter) *CEmitter {
	shimMap := make(map[uint64]string)
	for _, rel := range loaded.Relocations {
		if rel.SymName != "" {
			if shim, ok := LookupShim(rel.SymName); ok {
				if rel.PltAddr != 0 {
					shimMap[rel.PltAddr] = shim
				}
				shimMap[rel.Offset] = shim
			}
		}
	}
	for _, sym := range loaded.Symbols {
		if sym.Name != "" && sym.Address != 0 {
			if shim, ok := LookupShim(sym.Name); ok {
				shimMap[sym.Address] = shim
			}
		}
	}

	return &CEmitter{
		elf:     loaded,
		disasm:  d,
		lifter:  l,
		shimMap: shimMap,
	}
}

// ResolveEntryAddress dynamically resolves the primary guest entry point.
// It prioritizes the ELF EntryPoint (_start), falls back to the "main" symbol,
// and defaults to 0x60 if no metadata is present.
func (e *CEmitter) ResolveEntryAddress() (uint64, string) {
	if e.elf.EntryPoint != 0 {
		return e.elf.EntryPoint, "_start"
	}
	if mainSym, ok := e.elf.SymbolByName["main"]; ok && mainSym.Address != 0 {
		return mainSym.Address, "main"
	}
	return 0x60, "entry"
}

// EmitAll generates all C files and guest image in the target directory,
// returning the list of generated C source file paths.
func (e *CEmitter) EmitAll(outDir string) ([]string, error) {
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return nil, err
	}

	var cFiles []string

	mainPath := filepath.Join(outDir, "main.c")
	if err := e.EmitMainRunner(mainPath); err != nil {
		return nil, fmt.Errorf("failed to emit main.c: %w", err)
	}
	cFiles = append(cFiles, mainPath)

	binPath := filepath.Join(outDir, "guest_image.bin")
	if err := e.EmitGuestImage(binPath); err != nil {
		return nil, fmt.Errorf("failed to emit guest_image.bin: %w", err)
	}

	headerPath := filepath.Join(outDir, "guest_functions.h")
	if err := e.EmitFunctionsHeader(headerPath); err != nil {
		return nil, fmt.Errorf("failed to emit guest_functions.h: %w", err)
	}

	codeFiles, err := e.EmitChunkedCode(outDir, 250)
	if err != nil {
		return nil, fmt.Errorf("failed to emit chunked code: %w", err)
	}
	cFiles = append(cFiles, codeFiles...)

	return cFiles, nil
}

// EmitFunctionsHeader generates forward declarations for all recompiled functions.
func (e *CEmitter) EmitFunctionsHeader(path string) (err error) {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer func() {
		if cerr := f.Close(); err == nil {
			err = cerr
		}
	}()

	w := bufio.NewWriter(f)
	defer func() {
		if ferr := w.Flush(); err == nil {
			err = ferr
		}
	}()

	if _, err := w.WriteString("#ifndef GUEST_FUNCTIONS_H\n#define GUEST_FUNCTIONS_H\n\n#include \"recomp_runtime.h\"\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"); err != nil {
		return err
	}

	var fnAddrs []uint64
	for addr := range e.disasm.Functions {
		fnAddrs = append(fnAddrs, addr)
	}
	slices.Sort(fnAddrs)

	for _, addr := range fnAddrs {
		if _, err := fmt.Fprintf(w, "void fn_0x%x(GuestContext *__restrict__ ctx);\n", addr); err != nil {
			return err
		}
	}

	if _, err := w.WriteString("\n#ifdef __cplusplus\n}\n#endif\n\n#endif // GUEST_FUNCTIONS_H\n"); err != nil {
		return err
	}
	return nil
}

// EmitChunkedCode partitions recompiled functions across multiple C source files
// and emits a dispatch.c driver that ties all chunk registrations together.
func (e *CEmitter) EmitChunkedCode(outDir string, chunkSize int) ([]string, error) {
	var fnAddrs []uint64
	for addr := range e.disasm.Functions {
		fnAddrs = append(fnAddrs, addr)
	}
	slices.Sort(fnAddrs)

	if chunkSize <= 0 {
		chunkSize = 250
	}

	numChunks := (len(fnAddrs) + chunkSize - 1) / chunkSize
	if numChunks == 0 {
		numChunks = 1
	}

	var generatedFiles []string

	for chunkIdx := 0; chunkIdx < numChunks; chunkIdx++ {
		start := chunkIdx * chunkSize
		end := min(start+chunkSize, len(fnAddrs))
		chunkAddrs := fnAddrs[start:end]

		chunkFileName := fmt.Sprintf("code_%03d.c", chunkIdx)
		chunkPath := filepath.Join(outDir, chunkFileName)
		if err := e.emitSingleChunk(chunkPath, chunkIdx, chunkAddrs); err != nil {
			return nil, fmt.Errorf("failed to emit %s: %w", chunkFileName, err)
		}
		generatedFiles = append(generatedFiles, chunkPath)
	}

	dispatchPath := filepath.Join(outDir, "dispatch.c")
	if err := e.emitDispatch(dispatchPath, numChunks); err != nil {
		return nil, fmt.Errorf("failed to emit dispatch.c: %w", err)
	}
	generatedFiles = append(generatedFiles, dispatchPath)

	return generatedFiles, nil
}

func (e *CEmitter) emitSingleChunk(path string, chunkIdx int, chunkAddrs []uint64) (err error) {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer func() {
		if cerr := f.Close(); err == nil {
			err = cerr
		}
	}()

	w := bufio.NewWriter(f)
	defer func() {
		if ferr := w.Flush(); err == nil {
			err = ferr
		}
	}()

	if _, err := fmt.Fprintf(w, "#include \"recomp_runtime.h\"\n#include \"guest_functions.h\"\n\n// Chunk %d (%d functions)\n\n", chunkIdx, len(chunkAddrs)); err != nil {
		return err
	}

	for _, addr := range chunkAddrs {
		fn := e.disasm.Functions[addr]
		if err := e.emitFunction(w, fn); err != nil {
			return err
		}
	}

	// Emit chunk dispatch registration function
	if _, err := fmt.Fprintf(w, "// Registration for chunk %d\nvoid recomp_init_dispatch_chunk_%d(void) {\n", chunkIdx, chunkIdx); err != nil {
		return err
	}
	for _, addr := range chunkAddrs {
		fn := e.disasm.Functions[addr]
		if shim, isShimmed := e.shimMap[addr]; isShimmed {
			if _, err := fmt.Fprintf(w, "    recomp_register_fn(0x%xULL, %s);\n", addr, shim); err != nil {
				return err
			}
			continue
		}
		for _, blockAddr := range fn.BlockOrder {
			if _, err := fmt.Fprintf(w, "    recomp_register_fn(0x%xULL, fn_0x%x);\n", blockAddr, addr); err != nil {
				return err
			}
		}
	}
	if _, err := w.WriteString("}\n"); err != nil {
		return err
	}

	return nil
}

func (e *CEmitter) emitFunction(w *bufio.Writer, fn *disasm.Function) error {
	addr := fn.EntryAddr
	if shim, isShimmed := e.shimMap[addr]; isShimmed {
		if _, err := fmt.Fprintf(w, "// Function %s at 0x%x (Forwarded directly to host shim %s)\nvoid fn_0x%x(GuestContext *__restrict__ ctx) {\n    UnwindFrame *__cur_unwind_frame = NULL;\n    (void)__cur_unwind_frame;\n    %s(ctx);\n    return;\n}\n\n", fn.Name, addr, shim, addr, shim); err != nil {
			return err
		}
		return nil
	}

	if _, err := fmt.Fprintf(w, "// Function %s at 0x%x\nvoid fn_0x%x(GuestContext *__restrict__ ctx) {\n", fn.Name, addr, addr); err != nil {
		return err
	}

	maxEnd := addr
	for _, b := range fn.Blocks {
		if b.EndAddr > maxEnd {
			maxEnd = b.EndAddr
		}
	}
	if sym, ok := e.elf.SymbolByAddr[addr]; ok && sym.Size > 0 && addr+sym.Size > maxEnd {
		maxEnd = addr + sym.Size
	}

	needsUnwind := false
	if len(fn.BlockOrder) > 1 {
		for _, b := range fn.Blocks {
			for _, inst := range b.Insts {
				if inst.Inst.Op == x86asm.CALL {
					needsUnwind = true
					break
				}
			}
			if needsUnwind {
				break
			}
		}
	}

	if needsUnwind {
		if _, err := fmt.Fprintf(w, "    UnwindFrame __unwind_frame;\n    __unwind_frame.fn_start = 0x%xULL;\n    __unwind_frame.fn_end = 0x%xULL;\n    __unwind_frame.prev = ctx->unwind_frame;\n    ctx->unwind_frame = &__unwind_frame;\n    UnwindFrame *__cur_unwind_frame = &__unwind_frame;\n", addr, maxEnd); err != nil {
			return err
		}
		if _, err := w.WriteString("    if (_setjmp(__unwind_frame.buf) != 0) {\n        switch (ctx->rip) {\n"); err != nil {
			return err
		}
		for _, blockAddr := range fn.BlockOrder {
			if blockAddr != addr {
				if _, err := fmt.Fprintf(w, "            case 0x%xULL: goto loc_0x%x;\n", blockAddr, blockAddr); err != nil {
					return err
				}
			}
		}
		if _, err := fmt.Fprintf(w, "            default: goto loc_0x%x;\n        }\n    }\n", addr); err != nil {
			return err
		}
	} else {
		if _, err := w.WriteString("    UnwindFrame *__cur_unwind_frame = NULL;\n"); err != nil {
			return err
		}
	}

	if len(fn.BlockOrder) > 1 {
		if _, err := fmt.Fprintf(w, "    if (ctx->rip != 0x%xULL) {\n        switch (ctx->rip) {\n", addr); err != nil {
			return err
		}
		for _, blockAddr := range fn.BlockOrder {
			if blockAddr != addr {
				if _, err := fmt.Fprintf(w, "            case 0x%xULL: goto loc_0x%x;\n", blockAddr, blockAddr); err != nil {
					return err
				}
			}
		}
		if _, err := fmt.Fprintf(w, "            default: goto loc_0x%x;\n        }\n    }\n", addr); err != nil {
			return err
		}
	}

	for _, blockAddr := range fn.BlockOrder {
		block := fn.Blocks[blockAddr]
		if _, err := fmt.Fprintf(w, "\nloc_0x%x:\n", blockAddr); err != nil {
			return err
		}
		if _, err := fmt.Fprintf(w, "    ctx->rip = 0x%xULL;\n", blockAddr); err != nil {
			return err
		}

		for _, inst := range block.Insts {
			nextPC := inst.Address + uint64(inst.Inst.Len)
			lines, err := e.lifter.LiftInstruction(inst, nextPC, fn)
			if err != nil {
				if _, err := fmt.Fprintf(w, "    /* 0x%x: %s [UNSUPPORTED: %v] */\n", inst.Address, inst.Inst.String(), err); err != nil {
					return err
				}
				if _, err := fmt.Fprintf(w, "    fprintf(stderr, \"FATAL: Unsupported instruction at 0x%x: %s\\n\"); abort();\n", inst.Address, inst.Inst.String()); err != nil {
					return err
				}
			} else {
				for _, line := range lines {
					if _, err := w.WriteString(line); err != nil {
						return err
					}
					if err := w.WriteByte('\n'); err != nil {
						return err
					}
				}
			}
		}
	}

	if _, err := w.WriteString("    RECOMP_POP_UNWIND();\n}\n\n"); err != nil {
		return err
	}
	return nil
}

func (e *CEmitter) emitDispatch(path string, numChunks int) (err error) {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer func() {
		if cerr := f.Close(); err == nil {
			err = cerr
		}
	}()

	w := bufio.NewWriter(f)
	defer func() {
		if ferr := w.Flush(); err == nil {
			err = ferr
		}
	}()

	if _, err := w.WriteString("#include \"recomp_runtime.h\"\n\n// Forward declarations of chunk registration functions\n"); err != nil {
		return err
	}
	for i := range numChunks {
		if _, err := fmt.Fprintf(w, "void recomp_init_dispatch_chunk_%d(void);\n", i); err != nil {
			return err
		}
	}

	if _, err := w.WriteString("\n// Initialize dispatch table with all recompiled chunks and runtime shims\nvoid recomp_init_dispatch_table(void) {\n"); err != nil {
		return err
	}

	for i := range numChunks {
		if _, err := fmt.Fprintf(w, "    recomp_init_dispatch_chunk_%d();\n", i); err != nil {
			return err
		}
	}

	if err := e.emitPLTRegistrations(w); err != nil {
		return err
	}

	if _, err := w.WriteString("}\n"); err != nil {
		return err
	}

	return nil
}

func (e *CEmitter) emitPLTRegistrations(w *bufio.Writer) error {
	for _, rel := range e.elf.Relocations {
		if rel.SymName != "" {
			if shim, ok := LookupShim(rel.SymName); ok {
				if rel.PltAddr != 0 {
					if _, err := fmt.Fprintf(w, "    recomp_register_fn(0x%xULL, %s); // PLT %s\n", rel.PltAddr, shim, rel.SymName); err != nil {
						return err
					}
				}
				if _, err := fmt.Fprintf(w, "    recomp_register_fn(0x%xULL, %s); // GOT %s\n", rel.Offset, shim, rel.SymName); err != nil {
					return err
				}
			}
		}
	}

	// Register shims for matching static symbols (e.g. libc stubs like syscall)
	for _, sym := range e.elf.Symbols {
		if sym.Name != "" && sym.Address != 0 {
			if shim, ok := LookupShim(sym.Name); ok {
				if _, err := fmt.Fprintf(w, "    recomp_register_fn(0x%xULL, %s); // Symbol %s\n", sym.Address, shim, sym.Name); err != nil {
					return err
				}
			}
		}
	}
	return nil
}

// EmitGuestImage writes the raw binary ELF memory image directly to disk.
func (e *CEmitter) EmitGuestImage(path string) error {
	return os.WriteFile(path, e.elf.MemoryImage, 0o644)
}

// EmitMainRunner writes main.c driver.
func (e *CEmitter) EmitMainRunner(path string) (err error) {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer func() {
		if cerr := f.Close(); err == nil {
			err = cerr
		}
	}()

	w := bufio.NewWriter(f)
	defer func() {
		if ferr := w.Flush(); err == nil {
			err = ferr
		}
	}()

	entryAddr, entryName := e.ResolveEntryAddress()

	vfsInitArg := "NULL"
	if e.AppDir != "" {
		vfsInitArg = fmt.Sprintf("%q", e.AppDir)
	}

	content := fmt.Sprintf(`#include "recomp_runtime.h"
#include "ps4_vfs.h"

extern void recomp_init_dispatch_table(void);

int main(int argc, char **argv) {
    printf("[ps4-recomp] Initializing runtime...\n");
    ps4_vfs_init(%s);
    recomp_init_dispatch_table();

    const char *prog_name = (argc > 0 && argv[0]) ? argv[0] : "ps4_app";
    GuestContext *ctx = recomp_init_runtime_file("guest_image.bin", 0, prog_name);
    if (!ctx) {
        fprintf(stderr, "[ps4-recomp] Failed to allocate guest memory or load guest_image.bin\n");
        return 1;
    }

    if (strcmp("%s", "_start") != 0) {
        printf("[ps4-recomp] Calling global constructors (.init_array)...\n");
        // Run .init_array
        uint64_t init_arr[] = {
%s
        };
        size_t init_count = sizeof(init_arr) / sizeof(init_arr[0]);
        for (size_t i = 0; i < init_count; i++) {
            if (init_arr[i] != 0) {
                printf("[ps4-recomp] Running init constructor at 0x%%llx...\n", (unsigned long long)init_arr[i]);
                recomp_dispatch(ctx, init_arr[i]);
            }
        }
    }

    printf("[ps4-recomp] Executing %s (0x%x)...\n");
    recomp_dispatch(ctx, 0x%xULL);

    printf("[ps4-recomp] Execution complete.\n");
    recomp_free_runtime(ctx);
    return 0;
}
`, vfsInitArg, entryName, e.formatInitArray(), entryName, entryAddr, entryAddr)

	if _, err := w.WriteString(content); err != nil {
		return err
	}
	return nil
}

func (e *CEmitter) formatInitArray() string {
	var lines []string
	for _, addr := range e.elf.InitArray {
		lines = append(lines, fmt.Sprintf("        0x%xULL,", addr))
	}
	if len(lines) == 0 {
		return "        0"
	}
	return strings.Join(lines, "\n")
}
