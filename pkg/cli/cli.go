package cli

import (
	"context"
	"debug/elf"
	"encoding/binary"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"time"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/emitter"
	"ps4-recomp/pkg/lifter"
)

// Config contains runtime configuration parameters parsed from CLI arguments.
type Config struct {
	ElfPath    string
	OutDir     string
	Compile    bool
	Run        bool
	TimeoutSec int
	Jobs       int
	OptLevel   string
	AllSymbols bool
	ChunkSize  int
	Verbose    bool
}

const banner = `
===================================================================
  PS4Recomp: PlayStation 4 x86-64 to Native ARM64 AOT Recompiler
===================================================================`

const usageText = `
Usage:
  ps4-recomp [flags] <input.elf>
  ps4-recomp [flags] -elf <input.elf>

Options:
  -o, -out <dir>          Output directory for generated C code and binaries (default: "build")
  -e, -elf <file>         Path to input PS4 ELF binary
  -c, -compile            Compile generated C code to native ARM64 binary with clang (default: true)
  -j, -jobs <num>         Number of parallel clang workers (default: NumCPU)
  -O, -opt <level>        Clang optimization level (0, 1, 2, 3, s, z) (default: "2")
  -r, -run                Run the compiled binary after successful recompilation (default: false)
  -t, -timeout <sec>      Watchdog timeout in seconds when running binary (0 = no timeout) (default: 0)
  -a, -all-symbols        Recompile all symbols in symbol table even if unreachable (default: false)
  -k, -chunk-size <num>   Number of functions per emitted C chunk file (default: 250)
  -v, -verbose            Enable verbose output and detailed timing breakdown (default: false)
  -h, -help               Show this help message
`

func normalizeArgs(args []string) ([]string, []string) {
	var flags []string
	var positional []string
	valueFlags := map[string]bool{
		"-o": true, "-out": true, "--out": true,
		"-e": true, "-elf": true, "--elf": true,
		"-t": true, "-timeout": true, "--timeout": true,
		"-j": true, "-jobs": true, "--jobs": true,
		"-O": true, "-opt": true, "--opt": true,
		"-k": true, "-chunk-size": true, "--chunk-size": true,
	}

	for i := 0; i < len(args); i++ {
		arg := args[i]
		if strings.HasPrefix(arg, "-") {
			flags = append(flags, arg)
			base, _, _ := strings.Cut(arg, "=")
			if valueFlags[base] && !strings.Contains(arg, "=") && i+1 < len(args) && !strings.HasPrefix(args[i+1], "-") {
				i++
				flags = append(flags, args[i])
			}
		} else {
			positional = append(positional, arg)
		}
	}
	return flags, positional
}

// ParseArgs parses CLI arguments into a Config structure.
func ParseArgs(args []string) (*Config, error) {
	flagArgs, posArgs := normalizeArgs(args)

	fs := flag.NewFlagSet("ps4-recomp", flag.ContinueOnError)
	fs.SetOutput(io.Discard)

	cfg := &Config{
		OutDir:    "build",
		Compile:   true,
		Jobs:      runtime.NumCPU(),
		OptLevel:  "2",
		ChunkSize: 250,
	}

	var help bool

	fs.StringVar(&cfg.ElfPath, "elf", "", "Path to input PS4 ELF binary")
	fs.StringVar(&cfg.ElfPath, "e", "", "Path to input PS4 ELF binary (short)")
	fs.StringVar(&cfg.OutDir, "out", cfg.OutDir, "Output directory for recompiled C code and binaries")
	fs.StringVar(&cfg.OutDir, "o", cfg.OutDir, "Output directory (short)")
	fs.BoolVar(&cfg.Compile, "compile", cfg.Compile, "Compile generated C code to native ARM64 binary with clang")
	fs.BoolVar(&cfg.Compile, "c", cfg.Compile, "Compile generated C code (short)")
	fs.BoolVar(&cfg.Run, "run", cfg.Run, "Run the compiled binary after successful compilation")
	fs.BoolVar(&cfg.Run, "r", cfg.Run, "Run binary (short)")
	fs.IntVar(&cfg.TimeoutSec, "timeout", cfg.TimeoutSec, "Execution timeout in seconds")
	fs.IntVar(&cfg.TimeoutSec, "t", cfg.TimeoutSec, "Execution timeout in seconds (short)")
	fs.IntVar(&cfg.Jobs, "jobs", cfg.Jobs, "Parallel compiler jobs")
	fs.IntVar(&cfg.Jobs, "j", cfg.Jobs, "Parallel compiler jobs (short)")
	fs.StringVar(&cfg.OptLevel, "opt", cfg.OptLevel, "Clang optimization level")
	fs.StringVar(&cfg.OptLevel, "O", cfg.OptLevel, "Clang optimization level (short)")
	fs.BoolVar(&cfg.AllSymbols, "all-symbols", cfg.AllSymbols, "Recompile all symbols in symbol table")
	fs.BoolVar(&cfg.AllSymbols, "a", cfg.AllSymbols, "Recompile all symbols (short)")
	fs.IntVar(&cfg.ChunkSize, "chunk-size", cfg.ChunkSize, "Number of functions per C chunk")
	fs.IntVar(&cfg.ChunkSize, "k", cfg.ChunkSize, "Chunk size (short)")
	fs.BoolVar(&cfg.Verbose, "verbose", cfg.Verbose, "Verbose logging")
	fs.BoolVar(&cfg.Verbose, "v", cfg.Verbose, "Verbose logging (short)")
	fs.BoolVar(&help, "help", false, "Show help message")
	fs.BoolVar(&help, "h", false, "Show help message (short)")

	if err := fs.Parse(flagArgs); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			fmt.Print(banner + usageText)
			return nil, flag.ErrHelp
		}
		return nil, fmt.Errorf("invalid arguments: %w\nRun 'ps4-recomp --help' for usage", err)
	}

	if help {
		fmt.Print(banner + usageText)
		return nil, flag.ErrHelp
	}

	// Positional arguments override if -elf was not explicitly passed
	if cfg.ElfPath == "" && len(posArgs) > 0 {
		cfg.ElfPath = posArgs[0]
	}

	if cfg.ElfPath == "" {
		return nil, fmt.Errorf("no input PS4 ELF binary specified\n%s", usageText)
	}

	if cfg.Jobs < 1 {
		cfg.Jobs = 1
	}

	return cfg, nil
}

// Execute runs the complete recompilation and optional execution pipeline.
func Execute(args []string) error {
	cfg, err := ParseArgs(args)
	if err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return nil
		}
		return err
	}

	totalStart := time.Now()
	fmt.Println(banner)

	// Step 1: Load ELF
	stepStart := time.Now()
	fmt.Printf("[ps4-recomp] [1/4] Loading ELF: %s\n", cfg.ElfPath)
	loaded, err := elfloader.LoadELF(cfg.ElfPath)
	if err != nil {
		return fmt.Errorf("failed to load ELF '%s': %w", cfg.ElfPath, err)
	}
	fmt.Printf("             Entry point: 0x%x | Segments: %d | Relocations: %d | Time: %v\n",
		loaded.EntryPoint, len(loaded.Segments), len(loaded.Relocations), time.Since(stepStart).Round(time.Millisecond))

	// Step 2: Disassembly and CFG Analysis
	stepStart = time.Now()
	fmt.Printf("[ps4-recomp] [2/4] Analyzing CFG and discovering reachable code...\n")
	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		return fmt.Errorf("failed to initialize disassembler: %w", err)
	}

	textSec, ok := loaded.Sections[".text"]
	if !ok {
		return fmt.Errorf("ELF '%s' is missing .text section", cfg.ElfPath)
	}

	var entries []uint64
	entries = append(entries, loaded.EntryPoint)
	entries = append(entries, loaded.InitArray...)
	if mainSym, ok := loaded.SymbolByName["main"]; ok {
		entries = append(entries, mainSym.Address)
	}

	for _, rel := range loaded.Relocations {
		if rel.Addend > 0 {
			target := uint64(rel.Addend)
			if target >= textSec.Addr && target < textSec.Addr+textSec.Size {
				entries = append(entries, target)
			}
		}
	}

	// Scan data sections for function pointers (vtables, callback tables)
	dataSecNames := []string{".rodata", ".data.rel.ro", ".data"}
	for _, secName := range dataSecNames {
		sec, ok := loaded.Sections[secName]
		if !ok || sec.Size < 8 {
			continue
		}
		for off := uint64(0); off+8 <= sec.Size; off += 8 {
			addr := sec.Addr + off
			if addr+8 <= uint64(len(loaded.MemoryImage)) {
				val := binary.LittleEndian.Uint64(loaded.MemoryImage[addr : addr+8])
				if val >= textSec.Addr && val < textSec.Addr+textSec.Size {
					entries = append(entries, val)
				}
			}
		}
	}

	// Seed defined function symbols as entry points
	for _, sym := range loaded.Symbols {
		if sym.Type == elf.STT_FUNC && sym.Address != 0 {
			if cfg.AllSymbols || (sym.Address >= textSec.Addr && sym.Address < textSec.Addr+textSec.Size) {
				entries = append(entries, sym.Address)
			}
		}
	}

	if err := d.AnalyzeReachable(entries); err != nil {
		return fmt.Errorf("CFG analysis error: %w", err)
	}

	totalBlocks := 0
	totalInsts := 0
	for _, fn := range d.Functions {
		totalBlocks += len(fn.Blocks)
		for _, b := range fn.Blocks {
			totalInsts += len(b.Insts)
		}
	}
	fmt.Printf("             Discovered %d functions, %d basic blocks, %d instructions | Time: %v\n",
		len(d.Functions), totalBlocks, totalInsts, time.Since(stepStart).Round(time.Millisecond))

	// Step 3: C Source Code Generation
	stepStart = time.Now()
	fmt.Printf("[ps4-recomp] [3/4] Emitting C source files to '%s/'...\n", cfg.OutDir)
	if err := os.MkdirAll(cfg.OutDir, 0o755); err != nil {
		return fmt.Errorf("failed to create output directory '%s': %w", cfg.OutDir, err)
	}

	runtimeDir, err := findRuntimeDir()
	if err != nil {
		return fmt.Errorf("runtime directory error: %w", err)
	}

	runtimeFiles := []string{
		"recomp_runtime.h", "recomp_runtime.c",
		"ps4_syscalls.c", "ps4_threading.c", "ps4_sync.c",
		"ps4_direct_mem.h", "ps4_direct_mem.c",
		"ps4_equeue.h", "ps4_equeue.c",
		"ps4_metal_screen.h", "ps4_metal_screen.m",
		"ps4_videoout.h", "ps4_videoout.c",
	}
	for _, rf := range runtimeFiles {
		src := filepath.Join(runtimeDir, rf)
		dst := filepath.Join(cfg.OutDir, rf)
		if err := copyFile(src, dst); err != nil {
			return fmt.Errorf("failed to copy runtime file %s: %w", rf, err)
		}
	}

	knownFuncs := make(map[uint64]bool, len(d.Functions))
	for addr := range d.Functions {
		knownFuncs[addr] = true
	}

	l := lifter.NewLifter(knownFuncs)
	em := emitter.NewCEmitter(loaded, d, l)

	cFiles, err := em.EmitAll(cfg.OutDir)
	if err != nil {
		return fmt.Errorf("failed to emit C code: %w", err)
	}

	cFiles = append(cFiles,
		filepath.Join(cfg.OutDir, "recomp_runtime.c"),
		filepath.Join(cfg.OutDir, "ps4_syscalls.c"),
		filepath.Join(cfg.OutDir, "ps4_threading.c"),
		filepath.Join(cfg.OutDir, "ps4_sync.c"),
		filepath.Join(cfg.OutDir, "ps4_direct_mem.c"),
		filepath.Join(cfg.OutDir, "ps4_equeue.c"),
		filepath.Join(cfg.OutDir, "ps4_videoout.c"),
		filepath.Join(cfg.OutDir, "ps4_metal_screen.m"),
	)
	fmt.Printf("             Emitted %d C source files | Time: %v\n",
		len(cFiles), time.Since(stepStart).Round(time.Millisecond))

	// Step 4: Compile with Clang
	targetBin := filepath.Join(cfg.OutDir, "ps4_app")
	if cfg.Compile {
		stepStart = time.Now()
		fmt.Printf("[ps4-recomp] [4/4] Compiling native ARM64 binary with clang (-O%s, %d workers)...\n",
			cfg.OptLevel, cfg.Jobs)
		if err := compileParallel(cFiles, cfg.OutDir, targetBin, cfg.Jobs, cfg.OptLevel); err != nil {
			return fmt.Errorf("compilation failed: %w", err)
		}
		fmt.Printf("             Compiled binary: %s | Time: %v\n",
			targetBin, time.Since(stepStart).Round(time.Millisecond))
	} else {
		fmt.Println("[ps4-recomp] [4/4] Clang compilation skipped (--compile=false).")
	}

	fmt.Printf("[ps4-recomp] All tasks completed successfully in %v\n", time.Since(totalStart).Round(time.Millisecond))

	// Step 5: Optional Run
	if cfg.Run && cfg.Compile {
		fmt.Println("===================================================================")
		if err := runBinary(targetBin, cfg.TimeoutSec); err != nil {
			return fmt.Errorf("execution failed: %w", err)
		}
	}

	return nil
}

func compileParallel(cFiles []string, outDir, targetBin string, numWorkers int, optLevel string) error {
	objFiles := make([]string, len(cFiles))
	errChan := make(chan error, len(cFiles))
	jobs := make(chan int, len(cFiles))

	var wg sync.WaitGroup
	for range numWorkers {
		wg.Go(func() {
			for idx := range jobs {
				cFile := cFiles[idx]
				ext := filepath.Ext(cFile)
				objFile := strings.TrimSuffix(cFile, ext) + ".o"
				objFiles[idx] = objFile

				clangArgs := []string{
					"-O" + optLevel,
					"-fvisibility=hidden",
					"-I" + outDir,
				}
				if ext == ".m" {
					clangArgs = append(clangArgs, "-fobjc-arc")
				}
				clangArgs = append(clangArgs, "-c", cFile, "-o", objFile)
				if runtime.GOOS == "darwin" && runtime.GOARCH == "arm64" {
					clangArgs = append([]string{"-target", "arm64-apple-darwin", "-mcpu=apple-m1"}, clangArgs...)
				}

				cmd := exec.Command("clang", clangArgs...)
				if out, err := cmd.CombinedOutput(); err != nil {
					errChan <- fmt.Errorf("error compiling %s: %w\n%s", filepath.Base(cFile), err, string(out))
					return
				}
			}
		})
	}

	for i := range cFiles {
		jobs <- i
	}
	close(jobs)

	wg.Wait()
	close(errChan)

	for err := range errChan {
		if err != nil {
			return err
		}
	}

	linkArgs := []string{"-o", targetBin}
	if runtime.GOOS == "darwin" {
		linkArgs = append(linkArgs, "-target", "arm64-apple-darwin", "-Wl,-dead_strip", "-Wl,-x",
			"-framework", "Metal", "-framework", "Cocoa", "-framework", "QuartzCore")
	} else {
		linkArgs = append(linkArgs, "-Wl,--gc-sections", "-Wl,-s", "-lpthread", "-lm")
	}
	linkArgs = append(linkArgs, objFiles...)

	cmd := exec.Command("clang", linkArgs...)
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("link error: %w\n%s", err, string(out))
	}
	return nil
}

func runBinary(targetBin string, timeoutSec int) error {
	ctx := context.Background()
	var cancel context.CancelFunc
	if timeoutSec > 0 {
		ctx, cancel = context.WithTimeout(ctx, time.Duration(timeoutSec)*time.Second)
		defer cancel()
	}

	cmd := exec.CommandContext(ctx, targetBin)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	if timeoutSec > 0 {
		fmt.Printf("[ps4-recomp] Running %s (watchdog timeout: %ds)...\n", targetBin, timeoutSec)
	} else {
		fmt.Printf("[ps4-recomp] Running %s...\n", targetBin)
	}

	err := cmd.Run()
	if ctx.Err() == context.DeadlineExceeded {
		fmt.Printf("[ps4-recomp] Watchdog timeout of %ds reached. Terminated cleanly.\n", timeoutSec)
		return nil
	}
	return err
}

func findRuntimeDir() (string, error) {
	candidates := []string{"pkg/runtime"}

	if exe, err := os.Executable(); err == nil {
		exeDir := filepath.Dir(exe)
		candidates = append(candidates,
			filepath.Join(exeDir, "pkg", "runtime"),
			filepath.Join(exeDir, "..", "pkg", "runtime"),
		)
	}

	if wd, err := os.Getwd(); err == nil {
		dir := wd
		for range 5 {
			candidates = append(candidates, filepath.Join(dir, "pkg", "runtime"))
			parent := filepath.Dir(dir)
			if parent == dir {
				break
			}
			dir = parent
		}
	}

	for _, c := range candidates {
		if fi, err := os.Stat(c); err == nil && fi.IsDir() {
			if _, err := os.Stat(filepath.Join(c, "recomp_runtime.h")); err == nil {
				return c, nil
			}
		}
	}

	return "", fmt.Errorf("unable to locate runtime directory (checked %v)", candidates)
}

func copyFile(src, dst string) (err error) {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer func() {
		if cerr := in.Close(); err == nil {
			err = cerr
		}
	}()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer func() {
		if cerr := out.Close(); err == nil {
			err = cerr
		}
	}()

	if _, err = io.Copy(out, in); err != nil {
		return err
	}
	return nil
}
