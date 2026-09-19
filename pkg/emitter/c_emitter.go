package emitter

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/lifter"
)

// CEmitter emits C source files from disassembled functions.
type CEmitter struct {
	elf    *elfloader.LoadedELF
	disasm *disasm.Disassembler
	lifter *lifter.Lifter
}

// NewCEmitter creates a new C emitter.
func NewCEmitter(loaded *elfloader.LoadedELF, d *disasm.Disassembler, l *lifter.Lifter) *CEmitter {
	return &CEmitter{
		elf:    loaded,
		disasm: d,
		lifter: l,
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
	sort.Slice(fnAddrs, func(i, j int) bool { return fnAddrs[i] < fnAddrs[j] })

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
	sort.Slice(fnAddrs, func(i, j int) bool { return fnAddrs[i] < fnAddrs[j] })

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
		end := start + chunkSize
		if end > len(fnAddrs) {
			end = len(fnAddrs)
		}
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
	if _, err := fmt.Fprintf(w, "// Function %s at 0x%x\nvoid fn_0x%x(GuestContext *__restrict__ ctx) {\n", fn.Name, addr, addr); err != nil {
		return err
	}

	if len(fn.BlockOrder) > 1 {
		if _, err := w.WriteString("    switch (ctx->rip) {\n"); err != nil {
			return err
		}
		for _, blockAddr := range fn.BlockOrder {
			if blockAddr != addr {
				if _, err := fmt.Fprintf(w, "        case 0x%xULL: goto loc_0x%x;\n", blockAddr, blockAddr); err != nil {
					return err
				}
			}
		}
		if _, err := fmt.Fprintf(w, "        default: goto loc_0x%x;\n    }\n", addr); err != nil {
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
					if _, err := fmt.Fprintf(w, "%s\n", line); err != nil {
						return err
					}
				}
			}
		}
	}

	if _, err := w.WriteString("}\n\n"); err != nil {
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
	for i := 0; i < numChunks; i++ {
		if _, err := fmt.Fprintf(w, "void recomp_init_dispatch_chunk_%d(void);\n", i); err != nil {
			return err
		}
	}

	if _, err := w.WriteString("\n// Initialize dispatch table with all recompiled chunks and runtime shims\nvoid recomp_init_dispatch_table(void) {\n"); err != nil {
		return err
	}

	for i := 0; i < numChunks; i++ {
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
	// Map known PLT names to shims
	pltMap := map[string]string{
		"sceKernelUsleep":           "shim_sceKernelUsleep",
		"sysconf":                   "shim_sysconf",
		"open":                      "shim_open",
		"__error":                   "shim_error",
		"mmap":                      "shim_mmap",
		"sigprocmask":               "shim_sigprocmask",
		"fstat":                     "shim_fstat",
		"close":                     "shim_close",
		"_writev":                   "shim_writev",
		"_write":                    "shim_write",
		"_ioctl":                    "shim_ioctl",
		"nanosleep":                 "shim_nanosleep",
		"lseek":                     "shim_lseek",
		"sigaction":                 "shim_sigaction",
		"_exit":                     "shim_exit",
		"pthread_cond_broadcast":    "shim_pthread_cond_broadcast",
		"pthread_rwlock_wrlock":     "shim_pthread_rwlock_wrlock",
		"pthread_cond_destroy":      "shim_pthread_cond_destroy",
		"pthread_rwlock_rdlock":     "shim_pthread_rwlock_rdlock",
		"pthread_setspecific":       "shim_pthread_setspecific",
		"pthread_equal":             "shim_pthread_equal",
		"pthread_mutex_unlock":      "shim_pthread_mutex_unlock",
		"pthread_cond_timedwait":    "shim_pthread_cond_timedwait",
		"poll":                      "shim_poll",
		"pthread_rwlock_unlock":     "shim_pthread_rwlock_unlock",
		"pthread_detach":            "shim_pthread_detach",
		"pthread_mutexattr_init":    "shim_pthread_mutexattr_init",
		"pthread_mutex_lock":        "shim_pthread_mutex_lock",
		"munmap":                    "shim_munmap",
		"pthread_mutex_destroy":     "shim_pthread_mutex_destroy",
		"pthread_mutex_trylock":     "shim_pthread_mutex_trylock",
		"sched_yield":               "shim_sched_yield",
		"pthread_join":              "shim_pthread_join",
		"pthread_getspecific":       "shim_pthread_getspecific",
		"pthread_mutexattr_destroy": "shim_pthread_mutexattr_destroy",
		"pthread_self":              "shim_pthread_self",
		"pthread_mutexattr_settype": "shim_pthread_mutexattr_settype",
		"pthread_key_create":        "shim_pthread_key_create",
		"pthread_mutex_init":        "shim_pthread_mutex_init",
		"madvise":                   "shim_madvise",
		"pthread_once":              "shim_pthread_once",
		"pthread_cond_wait":         "shim_pthread_cond_wait",
		"raise":                     "shim_raise",
		"pthread_cond_signal":       "shim_pthread_cond_signal",
	}

	for _, rel := range e.elf.Relocations {
		if rel.SymName != "" {
			if shim, ok := pltMap[rel.SymName]; ok {
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

	content := fmt.Sprintf(`#include "recomp_runtime.h"

extern void recomp_init_dispatch_table(void);

int main(int argc, char **argv) {
    printf("[ps4-recomp] Initializing runtime...\n");
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
`, entryName, e.formatInitArray(), entryName, entryAddr, entryAddr)

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
