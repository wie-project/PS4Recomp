package emitter

import (
	"fmt"
	"io"
	"os"
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

// EmitAll generates all C files in the target directory.
func (e *CEmitter) EmitAll(outDir string) error {
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return err
	}

	if err := e.EmitRecompiledCode(outDir + "/recompiled_code.c"); err != nil {
		return fmt.Errorf("failed to emit recompiled_code.c: %w", err)
	}

	if err := e.EmitGuestImage(outDir + "/guest_image.c"); err != nil {
		return fmt.Errorf("failed to emit guest_image.c: %w", err)
	}

	if err := e.EmitMainRunner(outDir + "/main.c"); err != nil {
		return fmt.Errorf("failed to emit main.c: %w", err)
	}

	return nil
}

// EmitRecompiledCode writes all lifted functions and dispatch table to path.
func (e *CEmitter) EmitRecompiledCode(path string) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()

	// Header includes
	io.WriteString(f, "#include \"recomp_runtime.h\"\n\n")

	// Sort function entry addresses
	var fnAddrs []uint64
	for addr := range e.disasm.Functions {
		fnAddrs = append(fnAddrs, addr)
	}
	sort.Slice(fnAddrs, func(i, j int) bool { return fnAddrs[i] < fnAddrs[j] })

	// Forward declarations
	io.WriteString(f, "// Forward declarations of recompiled functions\n")
	for _, addr := range fnAddrs {
		fmt.Fprintf(f, "void fn_0x%x(GuestContext *ctx);\n", addr)
	}
	io.WriteString(f, "\n")

	// Emit each function
	for _, addr := range fnAddrs {
		fn := e.disasm.Functions[addr]
		fmt.Fprintf(f, "// Function %s at 0x%x\n", fn.Name, addr)
		fmt.Fprintf(f, "void fn_0x%x(GuestContext *ctx) {\n", addr)

		// If function has multiple blocks, emit a switch on ctx->rip to allow jumping to internal blocks
		if len(fn.BlockOrder) > 1 {
			io.WriteString(f, "    switch (ctx->rip) {\n")
			for _, blockAddr := range fn.BlockOrder {
				if blockAddr != addr {
					fmt.Fprintf(f, "        case 0x%xULL: goto loc_0x%x;\n", blockAddr, blockAddr)
				}
			}
			fmt.Fprintf(f, "        default: goto loc_0x%x;\n", addr)
			io.WriteString(f, "    }\n")
		}

		for _, blockAddr := range fn.BlockOrder {
			block := fn.Blocks[blockAddr]
			fmt.Fprintf(f, "\nloc_0x%x:\n", blockAddr)

			for _, inst := range block.Insts {
				nextPC := inst.Address + uint64(inst.Inst.Len)
				lines, err := e.lifter.LiftInstruction(inst, nextPC, fn)
				if err != nil {
					// Unsupported instruction fallback
					fmt.Fprintf(f, "    /* 0x%x: %s [UNSUPPORTED: %v] */\n", inst.Address, inst.Inst.String(), err)
					fmt.Fprintf(f, "    fprintf(stderr, \"FATAL: Unsupported instruction at 0x%x: %s\\n\"); abort();\n", inst.Address, inst.Inst.String())
				} else {
					for _, line := range lines {
						fmt.Fprintf(f, "%s\n", line)
					}
				}
			}
		}

		io.WriteString(f, "}\n\n")
	}

	// Emit dispatch table registration
	io.WriteString(f, "// Initialize dispatch table with recompiled functions and runtime shims\n")
	io.WriteString(f, "void recomp_init_dispatch_table(void) {\n")

	for _, addr := range fnAddrs {
		fn := e.disasm.Functions[addr]
		for _, blockAddr := range fn.BlockOrder {
			fmt.Fprintf(f, "    recomp_register_fn(0x%xULL, fn_0x%x);\n", blockAddr, addr)
		}
	}

	// Register PLT entries to their respective shims
	e.emitPLTRegistrations(f)

	io.WriteString(f, "}\n")

	return nil
}

func (e *CEmitter) emitPLTRegistrations(w io.Writer) {
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
					fmt.Fprintf(w, "    recomp_register_fn(0x%xULL, %s); // PLT %s\n", rel.PltAddr, shim, rel.SymName)
				}
				fmt.Fprintf(w, "    recomp_register_fn(0x%xULL, %s); // GOT %s\n", rel.Offset, shim, rel.SymName)
			}
		}
	}
}

// EmitGuestImage writes the ELF memory image as a C file.
func (e *CEmitter) EmitGuestImage(path string) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()

	io.WriteString(f, "#include <stdint.h>\n#include <stddef.h>\n\n")
	fmt.Fprintf(f, "const size_t g_elf_image_size = %d;\n", len(e.elf.MemoryImage))
	io.WriteString(f, "const uint8_t g_elf_image[] = {\n")

	// Emit in chunks of 16 bytes per line
	image := e.elf.MemoryImage
	for i := 0; i < len(image); i += 16 {
		end := i + 16
		if end > len(image) {
			end = len(image)
		}
		var parts []string
		for _, b := range image[i:end] {
			parts = append(parts, fmt.Sprintf("0x%02x", b))
		}
		fmt.Fprintf(f, "    %s,\n", strings.Join(parts, ", "))
	}

	io.WriteString(f, "};\n")
	return nil
}

// EmitMainRunner writes main.c driver.
func (e *CEmitter) EmitMainRunner(path string) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()

	content := fmt.Sprintf(`#include "recomp_runtime.h"

extern const uint8_t g_elf_image[];
extern const size_t g_elf_image_size;
extern void recomp_init_dispatch_table(void);

int main(int argc, char **argv) {
    printf("[ps4-recomp] Initializing runtime...\n");
    recomp_init_dispatch_table();

    GuestContext *ctx = recomp_init_runtime(g_elf_image, g_elf_image_size);
    if (!ctx) {
        fprintf(stderr, "[ps4-recomp] Failed to allocate guest memory\n");
        return 1;
    }

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

    printf("[ps4-recomp] Executing main (0x60)...\n");
    recomp_dispatch(ctx, 0x60ULL);

    printf("[ps4-recomp] Execution complete.\n");
    recomp_free_runtime(ctx);
    return 0;
}
`, e.formatInitArray())

	_, err = io.WriteString(f, content)
	return err
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
