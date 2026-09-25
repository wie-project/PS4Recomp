package cli

import (
	"bytes"
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"slices"
	"strings"
	"sync"
	"time"

	"ps4-recomp/pkg/analyzer"
	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/emitter"
	"ps4-recomp/pkg/lifter"
	"ps4-recomp/pkg/ps4pkg"
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
	AppDir     string
	Asan       bool
}

const banner = `
===================================================================
  PS4Recomp: PlayStation 4 x86-64 to Native ARM64 AOT Recompiler
===================================================================`

const usageText = `
Usage:
  ps4-recomp [flags] <input.elf>
  ps4-recomp analyze <eboot.bin | input.elf | module.prx>
  ps4-recomp pkg info <path.pkg | directory>
  ps4-recomp pkg extract <path.pkg> [-o out_dir] [--resources]

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
      -app-dir <dir>      Host directory mapping to /app0 for game assets (auto-detected if omitted)
      -asan               Compile with AddressSanitizer and LeakSanitizer (default: false)
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
		"-app-dir": true, "--app-dir": true,
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
	fs.StringVar(&cfg.AppDir, "app-dir", "", "Host directory mapping to /app0 for game assets")
	fs.BoolVar(&cfg.Asan, "asan", false, "Compile with AddressSanitizer and LeakSanitizer")
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

	// Auto-detect AppDir (parent directory containing assets/ if omitted)
	if cfg.AppDir == "" && cfg.ElfPath != "" {
		dir := filepath.Dir(cfg.ElfPath)
		for range 5 {
			if fi, err := os.Stat(filepath.Join(dir, "assets")); err == nil && fi.IsDir() {
				if absDir, err := filepath.Abs(dir); err == nil {
					cfg.AppDir = absDir
				}
				break
			}
			parent := filepath.Dir(dir)
			if parent == dir {
				break
			}
			dir = parent
		}
	}

	if cfg.Jobs < 1 {
		cfg.Jobs = 1
	}

	return cfg, nil
}

// Execute runs the complete recompilation and optional execution pipeline.
func Execute(args []string) error {
	if len(args) > 0 && args[0] == "pkg" {
		return handlePkgCommand(args[1:])
	}
	if len(args) > 0 && args[0] == "analyze" {
		if len(args) < 2 {
			return errors.New("usage: ps4-recomp analyze <binary.elf | eboot.bin | module.prx>")
		}
		report, err := analyzer.AnalyzeBinary(args[1])
		if err != nil {
			return fmt.Errorf("analysis failed: %w", err)
		}
		fmt.Println(report.SummaryString())
		return nil
	}

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
	fmt.Printf("             Entry point: 0x%x | Segments: %d | Relocations: %d | Unwind functions: %d | Time: %v\n",
		loaded.EntryPoint, len(loaded.Segments), len(loaded.Relocations), len(loaded.FuncBounds), time.Since(stepStart).Round(time.Millisecond))

	// Step 2: Disassembly and CFG Analysis
	stepStart = time.Now()
	fmt.Printf("[ps4-recomp] [2/4] Analyzing CFG and discovering reachable code...\n")
	moduleInfos, err := loadCompanionModules(cfg, loaded)
	if err != nil {
		return err
	}
	var allCompanionExports []elfloader.Symbol
	for _, m := range moduleInfos {
		allCompanionExports = append(allCompanionExports, m.Exports...)
	}
	if err := elfloader.ResolveModuleRelocations(loaded, allCompanionExports); err != nil {
		return fmt.Errorf("failed resolving module relocations: %w", err)
	}
	hle := emitter.NewHLEReport(loaded, moduleInfos)
	fmt.Print(hle.ImportSummary())

	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		return fmt.Errorf("failed to initialize disassembler: %w", err)
	}

	entries := disasm.SeedEntryPoints(loaded, cfg.AllSymbols)
	lastReport := time.Now()
	funcs, err := d.DiscoverReachable(entries, func(nFn, nInst int) {
		if time.Since(lastReport) >= 2*time.Second {
			fmt.Printf("             ... %d functions, %d instructions\n", nFn, nInst)
			lastReport = time.Now()
		}
	})
	if err != nil {
		return fmt.Errorf("CFG analysis error: %w", err)
	}

	totalInsts := 0
	for _, f := range funcs {
		totalInsts += f.Insts
	}
	fmt.Printf("             Discovered %d functions, %d instructions | cap hits: %d | privileged stops: %d | Time: %v\n",
		len(funcs), totalInsts, d.CapHits, d.PrivilegedStops, time.Since(stepStart).Round(time.Millisecond))
	if d.CapHits > 0 {
		fmt.Fprintf(os.Stderr, "             Warning: %d function(s) hit decode caps:\n", d.CapHits)
		for i, hit := range d.CapHitDetails {
			if i >= 10 {
				fmt.Fprintf(os.Stderr, "               ... and %d more\n", len(d.CapHitDetails)-10)
				break
			}
			fmt.Fprintf(os.Stderr, "               - %s (0x%x): %s at PC 0x%x (%d insts decoded)\n",
				hit.Name, hit.EntryAddr, hit.Reason, hit.PC, hit.InstCount)
		}
	}

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

	runtimeOutDir := filepath.Join(cfg.OutDir, "runtime")
	if err := copyDir(runtimeDir, runtimeOutDir); err != nil {
		return fmt.Errorf("failed to copy runtime files: %w", err)
	}

	var runtimeCFiles []string
	var runtimeIncludeDirs []string
	seenInc := make(map[string]bool)
	seenInc[cfg.OutDir] = true
	seenInc[runtimeOutDir] = true

	err = filepath.Walk(runtimeOutDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			if !seenInc[path] {
				seenInc[path] = true
				runtimeIncludeDirs = append(runtimeIncludeDirs, path)
			}
			return nil
		}
		ext := filepath.Ext(path)
		if ext == ".c" || ext == ".m" {
			runtimeCFiles = append(runtimeCFiles, path)
		}
		return nil
	})
	if err != nil {
		return fmt.Errorf("failed scanning runtime files: %w", err)
	}
	slices.Sort(runtimeCFiles)
	slices.Sort(runtimeIncludeDirs)

	knownFuncs := make(map[uint64]bool, len(funcs))
	for _, f := range funcs {
		knownFuncs[f.Addr] = true
	}

	l := lifter.NewLifter(knownFuncs)
	em := emitter.NewCEmitter(loaded, d, l)
	em.AppDir = cfg.AppDir
	em.Modules = moduleInfos
	em.ChunkSize = cfg.ChunkSize
	em.Funcs = funcs
	em.HLE = hle

	cFiles, err := em.EmitAll(cfg.OutDir)
	if err != nil {
		return fmt.Errorf("failed to emit C code: %w", err)
	}
	hlePath := filepath.Join(cfg.OutDir, "hle_report.txt")
	if err := hle.WriteFile(hlePath); err != nil {
		return fmt.Errorf("failed to write HLE report: %w", err)
	}
	fmt.Print(hle.SyscallSummary())
	fmt.Printf("             Wrote %s\n", hlePath)

	if cfg.AppDir != "" {
		srcAssets := filepath.Join(cfg.AppDir, "assets")
		dstAssets := filepath.Join(cfg.OutDir, "assets")
		if fi, err := os.Stat(srcAssets); err == nil && fi.IsDir() {
			_ = os.RemoveAll(dstAssets)
			_ = copyDir(srcAssets, dstAssets)
		}
	}

	cFiles = append(cFiles, runtimeCFiles...)
	fmt.Printf("             Emitted %d C source files | Time: %v\n",
		len(cFiles), time.Since(stepStart).Round(time.Millisecond))

	// Step 4: Compile with Clang
	targetBin := filepath.Join(cfg.OutDir, "ps4_app")
	if cfg.Compile {
		stepStart = time.Now()
		fmt.Printf("[ps4-recomp] [4/4] Compiling native ARM64 binary with clang (-O%s, %d workers)...\n",
			cfg.OptLevel, cfg.Jobs)
		if err := compileParallel(cFiles, cfg.OutDir, runtimeIncludeDirs, targetBin, cfg.Jobs, cfg.OptLevel, cfg.Asan); err != nil {
			return fmt.Errorf("compilation failed: %w", err)
		}
		fmt.Printf("             Compiled binary: %s | Time: %v\n",
			targetBin, time.Since(stepStart).Round(time.Millisecond))

		appName := strings.TrimSuffix(filepath.Base(cfg.ElfPath), filepath.Ext(cfg.ElfPath))
		if appName == "" || appName == "." {
			appName = "ps4_app"
		}
		if runtime.GOOS == "darwin" {
			bundleDir, err := packageAppBundle(cfg, appName, targetBin)
			if err != nil {
				return fmt.Errorf("failed to package macOS app bundle: %w", err)
			}
			targetBin = filepath.Join(bundleDir, "Contents", "MacOS", appName)
			fmt.Printf("             Packaged macOS App Bundle: %s\n", bundleDir)
		}
	} else {
		fmt.Println("[ps4-recomp] [4/4] Clang compilation skipped (--compile=false).")
	}

	fmt.Printf("[ps4-recomp] All tasks completed successfully in %v\n", time.Since(totalStart).Round(time.Millisecond))

	// Step 5: Optional Run
	if cfg.Run && cfg.Compile {
		fmt.Println("===================================================================")
		if err := runBinary(targetBin, cfg.TimeoutSec, cfg.AppDir); err != nil {
			return fmt.Errorf("execution failed: %w", err)
		}
	}

	return nil
}

func generateNinjaBuild(cFiles []string, outDir string, includeDirs []string, targetBin string, optLevel string, asan bool) error {
	var ftCflags []string
	var ftLibs []string
	if out, err := exec.Command("pkg-config", "--cflags", "freetype2").Output(); err == nil {
		ftCflags = strings.Fields(string(out))
	} else if _, err := os.Stat("/opt/homebrew/opt/freetype/include/freetype2"); err == nil {
		ftCflags = []string{"-I/opt/homebrew/opt/freetype/include/freetype2"}
	}
	if out, err := exec.Command("pkg-config", "--libs", "freetype2").Output(); err == nil {
		ftLibs = strings.Fields(string(out))
	} else if _, err := os.Stat("/opt/homebrew/opt/freetype/lib"); err == nil {
		ftLibs = []string{"-L/opt/homebrew/opt/freetype/lib", "-lfreetype"}
	}

	var sb strings.Builder
	sb.WriteString("ninja_required_version = 1.5\n\n")

	var incFlags []string
	incFlags = append(incFlags, "-I.")
	for _, inc := range includeDirs {
		relInc, err := filepath.Rel(outDir, inc)
		if err == nil && !strings.HasPrefix(relInc, "..") {
			incFlags = append(incFlags, "-I"+relInc)
		} else {
			incFlags = append(incFlags, "-I"+inc)
		}
	}
	incFlags = append(incFlags, ftCflags...)

	cflags := []string{"-O" + optLevel, "-fvisibility=hidden"}
	if asan {
		cflags = append(cflags, "-fsanitize=address,undefined", "-fno-omit-frame-pointer")
	}
	if runtime.GOOS == "darwin" && runtime.GOARCH == "arm64" {
		cflags = append([]string{"-target", "arm64-apple-darwin", "-mcpu=apple-m1"}, cflags...)
	}

	cflagsStr := strings.Join(append(cflags, incFlags...), " ")
	sb.WriteString(fmt.Sprintf("cflags = %s\n\n", cflagsStr))

	sb.WriteString("rule compile_c\n")
	sb.WriteString("  command = clang -MD -MF $out.d $cflags -c $in -o $out\n")
	sb.WriteString("  depfile = $out.d\n")
	sb.WriteString("  deps = gcc\n")
	sb.WriteString("  description = CC $in\n\n")

	sb.WriteString("rule compile_m\n")
	sb.WriteString("  command = clang -MD -MF $out.d $cflags -fobjc-arc -c $in -o $out\n")
	sb.WriteString("  depfile = $out.d\n")
	sb.WriteString("  deps = gcc\n")
	sb.WriteString("  description = OBJC $in\n\n")

	var ldflags []string
	if asan {
		ldflags = append(ldflags, "-fsanitize=address,undefined")
	}
	if runtime.GOOS == "darwin" {
		ldflags = append(ldflags, "-target", "arm64-apple-darwin", "-Wl,-dead_strip", "-Wl,-x",
			"-framework", "Metal", "-framework", "Cocoa", "-framework", "QuartzCore", "-framework", "GameController", "-framework", "AudioToolbox")
	} else {
		ldflags = append(ldflags, "-Wl,--gc-sections", "-Wl,-s", "-lpthread", "-lm")
	}
	ldflags = append(ldflags, ftLibs...)
	ldflagsStr := strings.Join(ldflags, " ")

	sb.WriteString(fmt.Sprintf("ldflags = %s\n\n", ldflagsStr))
	sb.WriteString("rule link_bin\n")
	sb.WriteString("  command = clang $in $ldflags -o $out\n")
	sb.WriteString("  description = LINK $out\n\n")

	var objFiles []string
	for _, cFile := range cFiles {
		relCFile, err := filepath.Rel(outDir, cFile)
		if err != nil {
			relCFile = cFile
		}
		ext := filepath.Ext(relCFile)
		objFile := strings.TrimSuffix(relCFile, ext) + ".o"
		objFiles = append(objFiles, objFile)
		rule := "compile_c"
		if ext == ".m" {
			rule = "compile_m"
		}
		sb.WriteString(fmt.Sprintf("build %s: %s %s\n", objFile, rule, relCFile))
	}

	relTargetBin, err := filepath.Rel(outDir, targetBin)
	if err != nil {
		relTargetBin = filepath.Base(targetBin)
	}

	sb.WriteString(fmt.Sprintf("\nbuild %s: link_bin %s\n", relTargetBin, strings.Join(objFiles, " ")))
	sb.WriteString(fmt.Sprintf("default %s\n", relTargetBin))

	ninjaPath := filepath.Join(outDir, "build.ninja")
	existing, err := os.ReadFile(ninjaPath)
	content := []byte(sb.String())
	if err == nil && bytes.Equal(existing, content) {
		return nil
	}
	return os.WriteFile(ninjaPath, content, 0o644)
}

func compileParallel(cFiles []string, outDir string, includeDirs []string, targetBin string, numWorkers int, optLevel string, asan bool) error {
	// Try building with Ninja first if available
	if ninjaPath, err := exec.LookPath("ninja"); err == nil {
		if err := generateNinjaBuild(cFiles, outDir, includeDirs, targetBin, optLevel, asan); err == nil {
			cmd := exec.Command(ninjaPath, "-C", outDir, fmt.Sprintf("-j%d", numWorkers))
			cmd.Stdout = os.Stdout
			cmd.Stderr = os.Stderr
			if err := cmd.Run(); err == nil {
				return nil
			}
		}
	}

	var ftCflags []string
	var ftLibs []string
	if out, err := exec.Command("pkg-config", "--cflags", "freetype2").Output(); err == nil {
		ftCflags = strings.Fields(string(out))
	} else if _, err := os.Stat("/opt/homebrew/opt/freetype/include/freetype2"); err == nil {
		ftCflags = []string{"-I/opt/homebrew/opt/freetype/include/freetype2"}
	}
	if out, err := exec.Command("pkg-config", "--libs", "freetype2").Output(); err == nil {
		ftLibs = strings.Fields(string(out))
	} else if _, err := os.Stat("/opt/homebrew/opt/freetype/lib"); err == nil {
		ftLibs = []string{"-L/opt/homebrew/opt/freetype/lib", "-lfreetype"}
	}

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

				cStat, errC := os.Stat(cFile)
				oStat, errO := os.Stat(objFile)
				if errC == nil && errO == nil && oStat.ModTime().After(cStat.ModTime()) {
					continue
				}

				clangArgs := []string{
					"-O" + optLevel,
					"-fvisibility=hidden",
					"-I" + outDir,
				}
				for _, inc := range includeDirs {
					clangArgs = append(clangArgs, "-I"+inc)
				}
				clangArgs = append(clangArgs, ftCflags...)
				if asan {
					clangArgs = append(clangArgs, "-fsanitize=address,undefined", "-fno-omit-frame-pointer")
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
	if asan {
		linkArgs = append(linkArgs, "-fsanitize=address,undefined")
	}
	if runtime.GOOS == "darwin" {
		linkArgs = append(linkArgs, "-target", "arm64-apple-darwin", "-Wl,-dead_strip", "-Wl,-x",
			"-framework", "Metal", "-framework", "Cocoa", "-framework", "QuartzCore", "-framework", "GameController", "-framework", "AudioToolbox")
	} else {
		linkArgs = append(linkArgs, "-Wl,--gc-sections", "-Wl,-s", "-lpthread", "-lm")
	}
	linkArgs = append(linkArgs, ftLibs...)
	linkArgs = append(linkArgs, objFiles...)

	cmd := exec.Command("clang", linkArgs...)
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("link error: %w\n%s", err, string(out))
	}
	return nil
}

func runBinary(targetBin string, timeoutSec int, appDir string) error {
	ctx := context.Background()
	var cancel context.CancelFunc
	if timeoutSec > 0 {
		ctx, cancel = context.WithTimeout(ctx, time.Duration(timeoutSec)*time.Second)
		defer cancel()
	}

	cmd := exec.CommandContext(ctx, targetBin)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Env = os.Environ()
	if appDir != "" {
		cmd.Env = append(cmd.Env, "PS4_APP_DIR="+appDir)
	}

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

func loadCompanionModules(cfg *Config, main *elfloader.LoadedELF) ([]emitter.GuestModule, error) {
	refs := elfloader.DiscoverCompanionModules(cfg.ElfPath, cfg.AppDir, main.MemoryImage)
	referenced := elfloader.ReferencedModuleNames(main.MemoryImage)
	found := make(map[string]struct{})
	for _, ref := range refs {
		found[strings.ToLower(filepath.Base(ref.Path))] = struct{}{}
		for _, alias := range ref.Aliases {
			found[strings.ToLower(filepath.Base(alias))] = struct{}{}
		}
	}
	for _, name := range referenced {
		if _, ok := found[strings.ToLower(filepath.Base(name))]; ok {
			continue
		}
		fmt.Printf("             Referenced module %s was not found for AOT linking\n", name)
	}
	if len(refs) == 0 {
		return nil, nil
	}

	nextBase := (main.MaxVAddr + 0xFFFF) &^ 0xFFFF
	if nextBase < 0x10000 {
		nextBase = 0x10000
	}
	var modules []emitter.GuestModule
	for _, ref := range refs {
		mod, err := elfloader.LoadELF(ref.Path)
		if err != nil {
			fmt.Printf("             Skipping module %s: %v\n", filepath.Base(ref.Path), err)
			continue
		}
		if elfloader.Overlaps(main, mod) {
			if err := mod.ApplyBias(nextBase - mod.MinVAddr); err != nil {
				return nil, fmt.Errorf("failed to relocate module %s: %w", filepath.Base(ref.Path), err)
			}
		}
		if err := elfloader.MergeImages(main, mod); err != nil {
			return nil, fmt.Errorf("failed to merge module %s: %w", filepath.Base(ref.Path), err)
		}
		nextBase = (main.MaxVAddr + 0xFFFF) &^ 0xFFFF
		exports := mod.ExportedFunctions()
		modules = append(modules, emitter.GuestModule{
			FileName: mod.FileName,
			Aliases:  ref.Aliases,
			Exports:  exports,
			Init:     append([]uint64(nil), mod.InitArray...),
		})
		fmt.Printf("             Linked module %s (%d exports, load 0x%x-0x%x)\n",
			mod.FileName, len(exports), mod.MinVAddr, mod.MaxVAddr)
	}
	return modules, nil
}

func findRuntimeDir() (string, error) {
	candidates := []string{"pkg/runtime"}

	if exe, err := os.Executable(); err == nil {
		exeDir := filepath.Dir(exe)
		candidates = append(
			candidates,
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
			if _, err := os.Stat(filepath.Join(c, "core", "recomp_runtime.h")); err == nil {
				return c, nil
			}
			if _, err := os.Stat(filepath.Join(c, "recomp_runtime.h")); err == nil {
				return c, nil
			}
		}
	}

	return "", fmt.Errorf("unable to locate runtime directory (checked %v)", candidates)
}

func copyFile(src, dst string) error {
	srcData, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	dstData, err := os.ReadFile(dst)
	if err == nil && bytes.Equal(srcData, dstData) {
		return nil
	}
	return os.WriteFile(dst, srcData, 0o644)
}

func copyDir(src, dst string) error {
	return filepath.Walk(src, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		rel, err := filepath.Rel(src, path)
		if err != nil {
			return err
		}
		target := filepath.Join(dst, rel)
		if info.IsDir() {
			return os.MkdirAll(target, info.Mode())
		}
		return copyFile(path, target)
	})
}

func packageAppBundle(cfg *Config, appName, compiledBin string) (string, error) {
	if runtime.GOOS != "darwin" {
		return compiledBin, nil
	}

	bundleDir := filepath.Join(cfg.OutDir, appName+".app")
	contentsDir := filepath.Join(bundleDir, "Contents")
	macosDir := filepath.Join(contentsDir, "MacOS")
	resourcesDir := filepath.Join(contentsDir, "Resources")

	if err := os.MkdirAll(macosDir, 0o755); err != nil {
		return "", err
	}
	if err := os.MkdirAll(resourcesDir, 0o755); err != nil {
		return "", err
	}

	// 1. Move/copy the compiled binary into Contents/MacOS/<appName>
	bundleBin := filepath.Join(macosDir, appName)
	if err := copyFile(compiledBin, bundleBin); err != nil {
		return "", fmt.Errorf("failed to copy binary to app bundle: %w", err)
	}
	_ = os.Chmod(bundleBin, 0o755)

	// Keep cfg.OutDir/ps4_app as a symlink for backward compatibility
	compatBin := filepath.Join(cfg.OutDir, "ps4_app")
	if compatBin != compiledBin {
		_ = os.Remove(compatBin)
		_ = os.Symlink(bundleBin, compatBin)
	}

	// 2. Copy guest_image.bin into Contents/Resources/
	guestImgSrc := filepath.Join(cfg.OutDir, "guest_image.bin")
	guestImgDst := filepath.Join(resourcesDir, "guest_image.bin")
	if fi, err := os.Stat(guestImgSrc); err == nil && !fi.IsDir() {
		_ = copyFile(guestImgSrc, guestImgDst)
	}

	// 3. Fully copy assets/ into Contents/Resources/assets/
	if cfg.AppDir != "" {
		srcAssets := filepath.Join(cfg.AppDir, "assets")
		dstAssets := filepath.Join(resourcesDir, "assets")
		if fi, err := os.Stat(srcAssets); err == nil && fi.IsDir() {
			_ = os.RemoveAll(dstAssets)
			if err := copyDir(srcAssets, dstAssets); err != nil {
				return "", fmt.Errorf("failed to copy assets into app bundle: %w", err)
			}
		}

		// Also fully copy sce_sys/ into Contents/Resources/sce_sys/
		srcSceSys := filepath.Join(cfg.AppDir, "sce_sys")
		dstSceSys := filepath.Join(resourcesDir, "sce_sys")
		if fi, err := os.Stat(srcSceSys); err == nil && fi.IsDir() {
			_ = os.RemoveAll(dstSceSys)
			if err := copyDir(srcSceSys, dstSceSys); err != nil {
				return "", fmt.Errorf("failed to copy sce_sys into app bundle: %w", err)
			}
		}
	}

	// 4. Generate AppIcon.icns from icon0.png if present
	hasIcon := false
	if cfg.AppDir != "" {
		iconPath := filepath.Join(cfg.AppDir, "sce_sys", "icon0.png")
		if _, err := os.Stat(iconPath); err == nil {
			icnsDst := filepath.Join(resourcesDir, "AppIcon.icns")
			cmd := exec.Command("sips", "-s", "format", "icns", iconPath, "--out", icnsDst)
			if err := cmd.Run(); err == nil {
				hasIcon = true
			}
		}
	}

	// 5. Generate Contents/Info.plist
	iconXml := ""
	if hasIcon {
		iconXml = "    <key>CFBundleIconFile</key>\n    <string>AppIcon</string>\n"
	}

	plistContent := fmt.Sprintf(`<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>%s</string>
    <key>CFBundleIdentifier</key>
    <string>org.ps4recomp.%s</string>
    <key>CFBundleName</key>
    <string>%s</string>
    <key>CFBundleDisplayName</key>
    <string>%s</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
%s</dict>
</plist>
`, appName, appName, appName, appName, iconXml)

	if err := os.WriteFile(filepath.Join(contentsDir, "Info.plist"), []byte(plistContent), 0o644); err != nil {
		return "", fmt.Errorf("failed to write Info.plist: %w", err)
	}

	return bundleDir, nil
}

func handlePkgCommand(args []string) error {
	if len(args) == 0 || args[0] == "-h" || args[0] == "--help" || args[0] == "help" {
		fmt.Println(`Usage: ps4-recomp pkg <subcommand> [args...]

Subcommands:
  info <file.pkg | directory>     Inspect single PKG or scan directory for multiple PKGs (Base, Patch, DLC)
  extract <file.pkg> [options]    Extract inner PFS files (executables by default)
  list <file.pkg>                 List files in the inner PFS without writing them

Extract options:
  -o, -out <dir>                  Output directory (default: <pkg>_extracted)
  --passcode <32-char>            PKG passcode (fake packages are decrypted automatically)
  --executables                   Extract eboot.bin, PRX/SPRX, sce_sys and sce_module (default)
  --resources                     Also extract remaining PFS files (paks, movies, assets)
  --all                           Extract every inner PFS file (same as --resources)
  --meta                          Also dump unencrypted PKG table entries into sce_sys
  --list                          List inner PFS files without extracting`)
		return nil
	}

	subcmd := args[0]
	switch subcmd {
	case "info":
		if len(args) < 2 {
			return errors.New("usage: ps4-recomp pkg info <file.pkg | directory>")
		}
		target := args[1]
		fi, err := os.Stat(target)
		if err != nil {
			return fmt.Errorf("failed to access '%s': %w", target, err)
		}

		if fi.IsDir() {
			fmt.Printf("[ps4-recomp] Scanning directory for PS4 packages: %s\n", target)
			mgr := ps4pkg.NewMultiPKGManager()
			defer mgr.Close()

			if err := mgr.ScanDirectory(target); err != nil {
				return err
			}

			if len(mgr.Packages) == 0 {
				fmt.Println("No PS4 PKG files found in directory.")
				return nil
			}

			fmt.Println("\n===================================================================")
			fmt.Println("  PS4 Multi-PKG Package Report")
			fmt.Println("===================================================================")
			fmt.Printf("Total PKG Files Scanned: %d | Games Detected: %d\n\n", len(mgr.Packages), len(mgr.GameSets))

			idx := 1
			for _, set := range mgr.GameSets {
				fmt.Printf("[%d] %s\n", idx, set.SummaryString())
				idx++
			}
			return nil
		}

		// Single PKG file
		pkg, err := ps4pkg.Open(target)
		if err != nil {
			return fmt.Errorf("failed to open PKG: %w", err)
		}
		defer pkg.Close()

		fmt.Println("\n===================================================================")
		fmt.Println("  PS4 PKG Container Information")
		fmt.Println("===================================================================")
		fmt.Printf("File Path:       %s\n", pkg.FilePath)
		fmt.Printf("File Size:       %s (%d bytes)\n", ps4pkg.FormatSize(pkg.FileSize), pkg.FileSize)
		fmt.Printf("Content ID:      %s\n", pkg.ContentID)
		fmt.Printf("Title ID:        %s\n", pkg.TitleID())
		fmt.Printf("Title:           %s\n", pkg.Title())
		fmt.Printf("App Version:     %s\n", pkg.AppVersion())
		fmt.Printf("Category:        %s\n", pkg.Category())
		fmt.Printf("DRM Type:        0x%x\n", pkg.DrmType)
		fmt.Printf("Content Type:    0x%x\n", pkg.ContentType)
		fmt.Printf("PFS Image:       offset 0x%x, size %s\n", pkg.PfsImageOffset, ps4pkg.FormatSize(int64(pkg.PfsImageSize)))
		fmt.Printf("PFS Flags:       0x%x\n", pkg.PfsFlags)
		fmt.Printf("Total Entries:   %d (Table offset: 0x%x, size: 0x%x)\n", len(pkg.Entries), pkg.RawHeader.TableOffset, pkg.RawHeader.TableSize)
		if ekpfs, err := pkg.GetEkpfs(); err == nil {
			fmt.Printf("EKPFS:           recovered from fake IMAGE_KEY (%d bytes)\n", len(ekpfs))
		} else {
			fmt.Printf("EKPFS:           %v\n", err)
		}

		if pkg.SFO != nil && len(pkg.SFO.Entries) > 0 {
			fmt.Println("\n--- PARAM.SFO Metadata ---")
			for _, k := range pkg.SFO.Keys {
				v := pkg.SFO.Entries[k]
				if v.Format == ps4pkg.SFOFormatString || v.Format == ps4pkg.SFOFormatStringAlt {
					fmt.Printf("  %-24s: %s\n", k, v.StringVal)
				} else if v.Format == ps4pkg.SFOFormatInteger {
					fmt.Printf("  %-24s: 0x%08x (%d)\n", k, v.IntegerVal, v.IntegerVal)
				} else {
					fmt.Printf("  %-24s: [Binary: %d bytes]\n", k, len(v.BinaryVal))
				}
			}
		}

		fmt.Println("\n--- PKG Entry Table ---")
		fmt.Printf("  %-10s %-30s %-12s %-10s %-10s\n", "Entry ID", "Name", "Offset", "Size", "Encrypted")
		fmt.Println("  -------------------------------------------------------------------------")
		for _, e := range pkg.Entries {
			name := e.Name
			if name == "" {
				name = "(unnamed)"
			}
			encStr := "No"
			if e.IsEncrypted {
				encStr = fmt.Sprintf("Yes (Key %d)", e.KeyIndex)
			}
			fmt.Printf("  0x%08x %-30s 0x%-10x %-10s %-10s\n",
				e.ID, name, e.DataOffset, ps4pkg.FormatSize(int64(e.DataSize)), encStr)
		}
		fmt.Println()
		return nil

	case "list":
		if len(args) < 2 {
			return errors.New("usage: ps4-recomp pkg list <file.pkg> [--passcode <code>]")
		}
		return runPkgExtract(args[1], args[2:], true)

	case "extract":
		if len(args) < 2 {
			return errors.New("usage: ps4-recomp pkg extract <file.pkg> [-o out_dir] [--executables] [--resources] [--passcode <code>]")
		}
		return runPkgExtract(args[1], args[2:], false)

	default:
		return fmt.Errorf("unknown pkg subcommand '%s'. Run 'ps4-recomp pkg help' for usage", subcmd)
	}
}

func runPkgExtract(target string, args []string, listOnly bool) error {
	outDir := ""
	passcode := ""
	wantExecutables := false
	wantResources := false
	meta := false
	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "-o", "-out", "--out":
			if i+1 >= len(args) {
				return fmt.Errorf("%s requires a directory argument", args[i])
			}
			outDir = args[i+1]
			i++
		case "--passcode":
			if i+1 >= len(args) {
				return errors.New("--passcode requires a 32-character passcode")
			}
			passcode = args[i+1]
			i++
		case "--executables":
			wantExecutables = true
		case "--resources", "--all":
			wantResources = true
		case "--meta":
			meta = true
		case "--list":
			listOnly = true
		default:
			return fmt.Errorf("unknown extract option %q", args[i])
		}
	}
	// Extract defaults to executables so a 50+ GiB asset dump is opt-in.
	// List defaults to the full inner PFS unless --executables is passed.
	executables := false
	resources := wantResources
	if wantResources {
		executables = false
	} else if wantExecutables || !listOnly {
		executables = true
	}
	if outDir == "" && !listOnly {
		base := strings.TrimSuffix(filepath.Base(target), filepath.Ext(target))
		outDir = base + "_extracted"
	}

	pkg, err := ps4pkg.Open(target)
	if err != nil {
		return fmt.Errorf("failed to open PKG: %w", err)
	}
	defer pkg.Close()

	if !listOnly {
		if err := os.MkdirAll(outDir, 0o755); err != nil {
			return fmt.Errorf("failed to create output directory %s: %w", outDir, err)
		}
		fmt.Printf("[ps4-recomp] Extracting %s -> %s\n", filepath.Base(target), outDir)
	} else {
		fmt.Printf("[ps4-recomp] Listing files in %s\n", filepath.Base(target))
	}

	n, err := pkg.Extract(ps4pkg.ExtractOptions{
		OutputDir:   outDir,
		Passcode:    passcode,
		ListOnly:    listOnly,
		Executables: executables && !resources,
		Resources:   resources,
		MetaEntries: meta && !listOnly,
		OnFile: func(path string, size int64) {
			fmt.Printf("  %s (%s)\n", path, ps4pkg.FormatSize(size))
		},
	})
	if err != nil {
		return err
	}
	if listOnly {
		fmt.Printf("[ps4-recomp] %d files in inner PFS\n", n)
	} else {
		fmt.Printf("[ps4-recomp] Extracted %d files.\n", n)
	}
	return nil
}
