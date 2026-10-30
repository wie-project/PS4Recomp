package cli

import (
	"bufio"
	"bytes"
	"context"
	"encoding/xml"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"slices"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"ps4-recomp/pkg/analyzer"
	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/emitter"
	"ps4-recomp/pkg/events"
	"ps4-recomp/pkg/lifter"
	"ps4-recomp/pkg/sfo"
)

// Config contains runtime configuration parameters parsed from CLI arguments.
type Config struct {
	ElfPath       string
	OutDir        string
	Compile       bool
	Run           bool
	TimeoutSec    int
	Jobs          int
	OptLevel      string
	AllSymbols    bool
	ChunkSize     int
	Verbose       bool
	AppDir        string
	CopyResources bool
	Asan          bool
	// TSO: acquire loads and release stores for non-stack guest memory (x86 memory ordering).
	TSO bool
	// JSON emits progress as JSON lines on stdout; the human log goes to stderr.
	JSON bool
}

const banner = `
===================================================================
  PS4Recomp: PlayStation 4 x86-64 to Native ARM64 AOT Recompiler
===================================================================`

const usageText = `
Usage:
  ps4-recomp [flags] <input.elf>
  ps4-recomp build [flags] <eboot.bin | input.elf>
  ps4-recomp analyze <eboot.bin | input.elf | module.prx>
  ps4-recomp version [--json]

Options:
  -o, -out <dir>          Output directory for generated code and binaries (default: "build")
  -e, -elf <file>         Path to input PS4 ELF binary
  -c, -compile            Compile generated code to native ARM64 binary with clang (default: true)
  -j, -jobs <num>         Number of parallel clang workers (default: NumCPU)
  -O, -opt <level>        Clang optimization level (0, 1, 2, 3, s, z) (default: "2")
  -r, -run                Run the compiled binary after successful recompilation (default: false)
  -t, -timeout <sec>      Watchdog timeout in seconds when running binary (0 = no timeout) (default: 0)
  -a, -all-symbols        Recompile all symbols in symbol table even if unreachable (default: false)
  -k, -chunk-size <num>   Number of functions per emitted assembly chunk file (default: 250)
  -v, -verbose            Enable verbose output and detailed timing breakdown (default: false)
      -app-dir <dir>      Host directory mapping to /app0 for game assets (auto-detected if omitted)
      -copy-resources     Copy all game assets/resources into macOS .app bundle Resources/ (default: false)
      -asan               Compile with AddressSanitizer and LeakSanitizer (default: false)
      -json               Emit progress as JSON lines on stdout (the log goes to stderr)
      -tso                x86 memory ordering: acquire/release for non-stack guest memory (default: true)
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
		TSO:       true,
	}

	var help bool

	fs.StringVar(&cfg.ElfPath, "elf", "", "Path to input PS4 ELF binary")
	fs.StringVar(&cfg.ElfPath, "e", "", "Path to input PS4 ELF binary (short)")
	fs.StringVar(&cfg.OutDir, "out", cfg.OutDir, "Output directory for recompiled assembly code and binaries")
	fs.StringVar(&cfg.OutDir, "o", cfg.OutDir, "Output directory (short)")
	fs.BoolVar(&cfg.Compile, "compile", cfg.Compile, "Compile generated assembly code to native ARM64 binary with clang")
	fs.BoolVar(&cfg.Compile, "c", cfg.Compile, "Compile generated code (short)")
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
	fs.BoolVar(&cfg.CopyResources, "copy-resources", false, "Copy all game assets/resources into macOS .app bundle Resources/")
	fs.BoolVar(&cfg.CopyResources, "bundle-resources", false, "Copy all game assets/resources into macOS .app bundle Resources/ (alias)")
	fs.BoolVar(&cfg.Asan, "asan", false, "Compile with AddressSanitizer and LeakSanitizer")
	fs.BoolVar(&cfg.TSO, "tso", cfg.TSO, "x86 memory ordering: acquire/release for non-stack guest memory")
	fs.BoolVar(&cfg.JSON, "json", false, "Emit progress as JSON lines on stdout")
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

	// "build" is an alias of the default command.
	if len(posArgs) > 0 && posArgs[0] == "build" {
		posArgs = posArgs[1:]
	}

	// Positional arguments override if -elf was not explicitly passed
	if len(posArgs) > 0 {
		subcmd := posArgs[0]
		if subcmd != "analyze" && !strings.HasSuffix(subcmd, ".elf") && !strings.HasSuffix(subcmd, ".bin") && !strings.HasSuffix(subcmd, ".prx") && !strings.HasSuffix(subcmd, ".sprx") {
			return nil, fmt.Errorf("unknown command or invalid input file '%s'. Run 'ps4-recomp --help' for usage", subcmd)
		}

		if cfg.ElfPath == "" {
			cfg.ElfPath = subcmd
		}
	}

	if cfg.ElfPath == "" {
		return nil, fmt.Errorf("no input PS4 ELF binary specified\n%s", usageText)
	}

	// Auto-detect AppDir (directory containing game files/assets if omitted)
	if cfg.AppDir == "" && cfg.ElfPath != "" {
		elfDir := filepath.Dir(cfg.ElfPath)
		markers := []string{"sce_sys", "assets", "contentid.txt", "sce_module"}
		checkMarkers := func(d string) bool {
			for _, m := range markers {
				if _, err := os.Stat(filepath.Join(d, m)); err == nil {
					return true
				}
			}
			return false
		}

		if checkMarkers(elfDir) {
			if absDir, err := filepath.Abs(elfDir); err == nil {
				cfg.AppDir = absDir
			}
		} else {
			dir := elfDir
			for range 5 {
				if checkMarkers(dir) {
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
	}

	if cfg.Jobs < 1 {
		cfg.Jobs = 1
	}

	return cfg, nil
}

// Version is the ps4-recomp release string, reported as "built_with" by build --json.
const Version = "0.4.0"

// stdout is where machine-readable output (JSON events) goes; tests replace it.
var stdout io.Writer = os.Stdout

// logw receives the human-readable build log. With --json it is stderr, so stdout stays pure
// JSON lines.
var logw io.Writer = os.Stdout

// Execute runs the complete recompilation and optional execution pipeline.
func Execute(args []string) (err error) {
	if len(args) > 0 && (args[0] == "version" || args[0] == "-version" || args[0] == "--version") {
		if slices.Contains(args, "-json") || slices.Contains(args, "--json") {
			ev := events.New(stdout, "version", true)
			ev.Done(map[string]string{"version": Version, "go": runtime.Version()})
			return nil
		}
		fmt.Fprintf(stdout, "ps4-recomp %s\n", Version)
		return nil
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
		if slices.Contains(args, "-json") || slices.Contains(args, "--json") {
			return events.New(stdout, "build", true).Error("bad_input", err.Error())
		}
		return err
	}

	logw = os.Stdout
	var ev *events.Writer
	if cfg.JSON {
		logw = os.Stderr
		ev = events.New(stdout, "build", true)
	} else {
		ev = events.New(io.Discard, "build", false)
	}
	defer func() { logw = os.Stdout }()
	// Every failure of the build path is reported as an error event before it is returned.
	defer func() {
		if err != nil && cfg.JSON {
			code := "build_failed"
			switch {
			case errors.Is(err, errBuildCancelled):
				code = "cancelled"
			case errors.Is(err, fs.ErrNotExist):
				code = "bad_input"
			}
			ev.Error(code, err.Error())
		}
	}()
	// SIGINT or SIGTERM cancels the build: the ninja or clang process groups are stopped,
	// and the build ends at the next stage boundary with errBuildCancelled.
	ctx, stopSignals := buildSignalContext()
	defer stopSignals()

	totalStart := time.Now()
	fmt.Fprintln(logw, banner)

	// Step 1: Load ELF
	stepStart := time.Now()
	ev.Progress("load", 1, 4, "stages", "loading the ELF")
	fmt.Fprintf(logw, "[ps4-recomp] [1/4] Loading ELF: %s\n", cfg.ElfPath)
	loaded, err := elfloader.LoadELF(cfg.ElfPath)
	if err != nil {
		return fmt.Errorf("failed to load ELF '%s': %w", cfg.ElfPath, err)
	}
	fmt.Fprintf(logw, "             Entry point: 0x%x | Segments: %d | Relocations: %d | Unwind functions: %d | Time: %v\n",
		loaded.EntryPoint, len(loaded.Segments), len(loaded.Relocations), len(loaded.FuncBounds), time.Since(stepStart).Round(time.Millisecond))

	if ctx.Err() != nil {
		return errBuildCancelled
	}

	// Step 2: Disassembly and CFG Analysis
	stepStart = time.Now()
	ev.Progress("cfg", 2, 4, "stages", "analyzing the control flow")
	fmt.Fprintf(logw, "[ps4-recomp] [2/4] Analyzing CFG and discovering reachable code...\n")
	for _, p := range elfloader.AddTLSModule(loaded, loaded) {
		fmt.Fprintf(os.Stderr, "             Warning: %s\n", p)
	}
	moduleInfos, err := loadCompanionModules(cfg, loaded)
	if err != nil {
		return err
	}
	elfloader.LayoutStaticTLS(loaded.TLSModules)
	for _, p := range elfloader.ApplyTLSOffsets(loaded) {
		fmt.Fprintf(os.Stderr, "             Warning: %s\n", p)
	}
	if n := len(loaded.TLSModules); n > 0 {
		fmt.Fprintf(logw, "             Static TLS: %d modules, 0x%x bytes\n", n, loaded.TLSModules[n-1].Offset)
	}
	var allCompanionExports []elfloader.Symbol
	for _, m := range moduleInfos {
		allCompanionExports = append(allCompanionExports, m.Exports...)
	}
	if err := elfloader.ResolveModuleRelocations(loaded, allCompanionExports); err != nil {
		return fmt.Errorf("failed resolving module relocations: %w", err)
	}
	hle := emitter.NewHLEReport(loaded, moduleInfos)
	fmt.Fprint(logw, hle.ImportSummary())

	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		return fmt.Errorf("failed to initialize disassembler: %w", err)
	}

	entries := disasm.SeedEntryPoints(loaded, cfg.AllSymbols)
	lastReport := time.Now()
	funcs, err := d.DiscoverReachable(entries, func(nFn, nInst int) {
		if time.Since(lastReport) >= 2*time.Second {
			fmt.Fprintf(logw, "             ... %d functions, %d instructions\n", nFn, nInst)
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
	fmt.Fprintf(logw, "             Discovered %d functions, %d instructions | cap hits: %d | privileged stops: %d | Time: %v\n",
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

	if ctx.Err() != nil {
		return errBuildCancelled
	}

	// Step 3: C Source Code Generation
	stepStart = time.Now()
	ev.Progress("emit", 3, 4, "stages", "emitting C sources")
	fmt.Fprintf(logw, "[ps4-recomp] [3/4] Emitting C source files to '%s/'...\n", cfg.OutDir)
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
		if isRuntimeSource(info.Name()) {
			runtimeCFiles = append(runtimeCFiles, path)
		}
		return nil
	})
	if err != nil {
		return fmt.Errorf("failed scanning runtime files: %w", err)
	}
	gcnInc, gcnLib := findGcnRecompiler()
	if gcnLib != "" {
		// the runtime is compiled against these headers: the library must be their build
		if err := checkGcnLibrary(gcnLib); err != nil {
			return err
		}
	}
	if gcnInc != "" && !seenInc[gcnInc] {
		seenInc[gcnInc] = true
		runtimeIncludeDirs = append(runtimeIncludeDirs, gcnInc)
	}
	spirvInc, _ := findSpirvCross()
	if spirvInc != "" && !seenInc[spirvInc] {
		seenInc[spirvInc] = true
		runtimeIncludeDirs = append(runtimeIncludeDirs, spirvInc)
	}
	atracInc, atracLib, atracSrcs := findLibAtrac9()
	if atracInc != "" {
		if !seenInc[atracInc] {
			seenInc[atracInc] = true
			runtimeIncludeDirs = append(runtimeIncludeDirs, atracInc)
		}
		if atracLib == "" {
			runtimeCFiles = append(runtimeCFiles, atracSrcs...)
		}
	}
	minimp3Inc := findMinimp3()
	if minimp3Inc != "" && !seenInc[minimp3Inc] {
		seenInc[minimp3Inc] = true
		runtimeIncludeDirs = append(runtimeIncludeDirs, minimp3Inc)
	}
	slices.Sort(runtimeCFiles)
	slices.Sort(runtimeIncludeDirs)

	knownFuncs := make(map[uint64]bool, len(funcs))
	for _, f := range funcs {
		knownFuncs[f.Addr] = true
	}

	l := lifter.NewArm64Lifter(knownFuncs)
	l.SetTSO(cfg.TSO)
	em := emitter.NewAsmEmitter(loaded, d, l)
	em.AppDir = cfg.AppDir
	em.Modules = moduleInfos
	em.ChunkSize = cfg.ChunkSize
	em.Funcs = funcs
	em.HLE = hle
	cFiles, err := em.EmitAll(cfg.OutDir)
	if err != nil {
		return fmt.Errorf("failed to emit ARM64 assembly: %w", err)
	}
	hlePath := filepath.Join(cfg.OutDir, "hle_report.txt")
	if err := hle.WriteFile(hlePath); err != nil {
		return fmt.Errorf("failed to write HLE report: %w", err)
	}
	fmt.Fprint(logw, hle.SyscallSummary())
	fmt.Fprintf(logw, "             Unresolved imports bound to the abort stub: %d calls, %d data (GLOB_DAT/R_64)\n",
		em.UnresolvedFuncImports, em.UnresolvedDataImports)
	if len(em.UnresolvedDataNames) > 0 {
		fmt.Fprintf(logw, "             Unresolved data imports: %s\n", strings.Join(em.UnresolvedDataNames, ", "))
	}
	for _, w := range em.Warnings {
		fmt.Fprintf(logw, "             Warning: %s\n", w)
	}
	fmt.Fprintf(logw, "             Wrote %s\n", hlePath)

	if cfg.AppDir != "" {
		srcAssets := filepath.Join(cfg.AppDir, "assets")
		dstAssets := filepath.Join(cfg.OutDir, "assets")
		if fi, err := os.Stat(srcAssets); err == nil && fi.IsDir() {
			_ = os.RemoveAll(dstAssets)
			_ = copyDir(srcAssets, dstAssets)
		}
	}

	cFiles = append(cFiles, runtimeCFiles...)
	tso := "on (acquire/release for non-stack guest memory)"
	if !cfg.TSO {
		tso = "off"
	}
	nAsm := 0
	for _, f := range cFiles {
		if strings.HasSuffix(f, ".s") {
			nAsm++
		}
	}
	fmt.Fprintf(logw, "             Emitted %d source files (%d assembly, %d C) | TSO: %s | Time: %v\n",
		len(cFiles), nAsm, len(cFiles)-nAsm, tso, time.Since(stepStart).Round(time.Millisecond))

	if ctx.Err() != nil {
		return errBuildCancelled
	}

	// Step 4: Compile with Clang
	targetBin := filepath.Join(cfg.OutDir, "ps4_app")
	appBundle := ""
	if cfg.Compile {
		stepStart = time.Now()
		ev.Progress("compile", 0, int64(len(cFiles)), "files", "compiling")
		fmt.Fprintf(logw, "[ps4-recomp] [4/4] Compiling native ARM64 binary with clang (-O%s, %d workers)...\n",
			cfg.OptLevel, cfg.Jobs)
		if err := compileParallel(ctx, cFiles, cfg.OutDir, runtimeIncludeDirs, targetBin, cfg.Jobs, cfg.OptLevel, cfg.Asan, func(done, total int64) {
			ev.Progress("compile", done, total, "files", "compiling")
		}); err != nil {
			return fmt.Errorf("compilation failed: %w", err)
		}
		fmt.Fprintf(logw, "             Compiled binary: %s | Time: %v\n",
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
			fmt.Fprintf(logw, "             Packaged macOS App Bundle: %s\n", bundleDir)
			appBundle = bundleDir
		}
	} else {
		fmt.Fprintln(logw, "[ps4-recomp] [4/4] Clang compilation skipped (--compile=false). Generating build.ninja...")
		if err := generateNinjaBuild(cFiles, cfg.OutDir, runtimeIncludeDirs, targetBin, cfg.OptLevel, cfg.Asan); err != nil {
			fmt.Fprintf(logw, "             Warning: failed to generate build.ninja: %v\n", err)
		} else {
			fmt.Fprintf(logw, "             Generated %s\n", filepath.Join(cfg.OutDir, "build.ninja"))
		}
	}

	fmt.Fprintf(logw, "[ps4-recomp] All tasks completed successfully in %v\n", time.Since(totalStart).Round(time.Millisecond))

	if ctx.Err() != nil {
		return errBuildCancelled
	}
	return finishBuild(ctx, ev, cfg, targetBin, appBundle)
}

// finishBuild runs the optional run step and then reports the build as done, so that with
// --json the last event of the stream is "done" only if the whole command succeeded.
func finishBuild(ctx context.Context, ev *events.Writer, cfg *Config, targetBin, appBundle string) error {
	if cfg.Run && cfg.Compile {
		fmt.Fprintln(logw, "===================================================================")
		if err := runBinary(ctx, targetBin, cfg.TimeoutSec, cfg.AppDir); err != nil {
			return fmt.Errorf("execution failed: %w", cancelledOr(ctx, err))
		}
	}
	if appBundle != "" {
		ev.Done(buildResult{App: appBundle, TitleID: titleIDOf(cfg.AppDir), BuiltWith: Version})
	} else {
		ev.Done(buildResult{BuiltWith: Version})
	}
	return nil
}

func generateNinjaBuild(cFiles []string, outDir string, includeDirs []string, targetBin string, optLevel string, asan bool) error {
	ftCflags, ftLibs := findFreeType()
	ffCflags, ffLibs := findFFmpeg()

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
	incFlags = append(incFlags, ffCflags...)

	hasAsm := false
	for _, f := range cFiles {
		if filepath.Ext(f) == ".s" {
			hasAsm = true
			break
		}
	}

	// -ffp-contract=off: keep clang from fusing a*b+c into an arm64 FMA in C runtime code
	// (x86 SSE/AVX without FMA never fuses, and runtime helpers emulate guest float math).
	cflags := []string{"-O" + optLevel, "-ffp-contract=off", "-fvisibility=hidden", "-Wno-parentheses-equality"}
	if asan {
		cflags = append(cflags, "-fsanitize=address,undefined", "-fno-omit-frame-pointer")
	}
	if runtime.GOOS == "darwin" && runtime.GOARCH == "arm64" {
		darwinFlags := []string{"-target", "arm64-apple-darwin", "-mcpu=apple-m1"}
		if hasAsm {
			darwinFlags = append(darwinFlags, "-DRECOMP_ARM64_ASM=1")
		}
		cflags = append(darwinFlags, cflags...)
	}

	fmt.Fprintf(&sb, "cflags = %s\n\n", ninjaShellArgs(append(cflags, incFlags...)))

	sb.WriteString("rule compile_c\n")
	sb.WriteString("  command = clang -MD -MF $out.d $cflags -c $in -o $out\n")
	sb.WriteString("  depfile = $out.d\n")
	sb.WriteString("  deps = gcc\n")
	sb.WriteString("  description = CC $in\n\n")

	sb.WriteString("rule compile_s\n")
	sb.WriteString("  command = clang -MD -MF $out.d $cflags -c $in -o $out\n")
	sb.WriteString("  depfile = $out.d\n")
	sb.WriteString("  deps = gcc\n")
	sb.WriteString("  description = AS $in\n\n")

	sb.WriteString("rule compile_m\n")
	sb.WriteString("  command = clang -MD -MF $out.d $cflags -fobjc-arc -c $in -o $out\n")
	sb.WriteString("  depfile = $out.d\n")
	sb.WriteString("  deps = gcc\n")
	sb.WriteString("  description = OBJC $in\n\n")

	sb.WriteString("rule compile_mm\n")
	sb.WriteString("  command = clang++ -MD -MF $out.d $cflags -fobjc-arc -std=c++17 -c $in -o $out\n")
	sb.WriteString("  depfile = $out.d\n")
	sb.WriteString("  deps = gcc\n")
	sb.WriteString("  description = OBJCXX $in\n\n")

	sb.WriteString("rule compile_cpp\n")
	sb.WriteString("  command = clang++ -MD -MF $out.d $cflags -std=c++17 -c $in -o $out\n")
	sb.WriteString("  depfile = $out.d\n")
	sb.WriteString("  deps = gcc\n")
	sb.WriteString("  description = CXX $in\n\n")

	var ldflags []string
	if asan {
		ldflags = append(ldflags, "-fsanitize=address,undefined")
	}
	if runtime.GOOS == "darwin" {
		ldflags = append(ldflags, "-target", "arm64-apple-darwin", "-Wl,-dead_strip", "-Wl,-x",
			"-framework", "Metal", "-framework", "Cocoa", "-framework", "QuartzCore", "-framework", "GameController", "-framework", "AudioToolbox", "-framework", "ImageIO", "-framework", "MetalFX", "-framework", "VideoToolbox", "-framework", "CoreMedia", "-framework", "CoreVideo", "-lz")
	} else {
		ldflags = append(ldflags, "-Wl,--gc-sections", "-Wl,-s", "-lpthread", "-lm")
	}
	var staticLibs []string // the link's implicit inputs: a rebuilt library relinks the game
	if _, gcnLib := findGcnRecompiler(); gcnLib != "" {
		ldflags = append(ldflags, gcnLib)
		staticLibs = append(staticLibs, gcnLib)
	}
	_, spirvLib := findSpirvCross()
	if spirvLib != "" {
		ldflags = append(ldflags, spirvLib, "-lc++")
		staticLibs = append(staticLibs, spirvLib)
	}
	_, atracLib, _ := findLibAtrac9()
	if atracLib != "" {
		ldflags = append(ldflags, atracLib)
		staticLibs = append(staticLibs, atracLib)
	}
	ldflags = append(ldflags, ftLibs...)
	ldflags = append(ldflags, ffLibs...)

	fmt.Fprintf(&sb, "ldflags = %s\n\n", ninjaShellArgs(ldflags))
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
		if ext == ".s" {
			rule = "compile_s"
		} else if ext == ".m" {
			rule = "compile_m"
		} else if ext == ".mm" {
			rule = "compile_mm"
		} else if ext == ".cpp" {
			rule = "compile_cpp"
		}
		fmt.Fprintf(&sb, "build %s: %s %s\n", ninjaEscape(objFile), rule, ninjaEscape(relCFile))
	}

	relTargetBin, err := filepath.Rel(outDir, targetBin)
	if err != nil {
		relTargetBin = filepath.Base(targetBin)
	}

	sb.WriteString(ninjaLinkBuild(relTargetBin, objFiles, staticLibs))
	fmt.Fprintf(&sb, "default %s\n", ninjaEscape(relTargetBin))

	ninjaPath := filepath.Join(outDir, "build.ninja")
	existing, err := os.ReadFile(ninjaPath)
	content := []byte(sb.String())
	if err == nil && bytes.Equal(existing, content) {
		return nil
	}
	return os.WriteFile(ninjaPath, content, 0o644)
}

// buildResult is the result object of the final "done" event of build --json.
type buildResult struct {
	App       string `json:"app,omitempty"`
	TitleID   string `json:"title_id,omitempty"`
	BuiltWith string `json:"built_with"`
}

// ninjaStatus matches the "[done/total]" prefix of a ninja status line.
var ninjaStatus = regexp.MustCompile(`^\[(\d+)/(\d+)\]`)

// runNinja runs ninja in outDir, forwarding its output to the build log and reporting each
// "[done/total]" status line to progress. When ctx ends, ninja's process group is stopped
// (groupCommand) and errBuildCancelled is returned.
func runNinja(ctx context.Context, ninjaPath, outDir string, numWorkers int, progress func(done, total int64)) error {
	cmd := groupCommand(ctx, ninjaPath, "-C", outDir, fmt.Sprintf("-j%d", numWorkers))
	cmd.Stderr = os.Stderr
	pipe, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	if err := cmd.Start(); err != nil {
		return err
	}
	sc := bufio.NewScanner(pipe)
	for sc.Scan() {
		line := sc.Bytes()
		fmt.Fprintf(logw, "%s\n", line)
		if m := ninjaStatus.FindSubmatch(line); m != nil && progress != nil {
			done, _ := strconv.ParseInt(string(m[1]), 10, 64)
			total, _ := strconv.ParseInt(string(m[2]), 10, 64)
			progress(done, total)
		}
	}
	io.Copy(io.Discard, pipe) // drain, so a very long line cannot block ninja
	return cancelledOr(ctx, cmd.Wait())
}

// shellQuote quotes arg for /bin/sh, which runs ninja's commands: an argument with any
// character outside a safe set is put in single quotes, an embedded quote being closed, escaped
// with a backslash and reopened. Paths such as
// ".../PS4Recomp.app/Contents/Resources/3rdparty/..." may contain a space.
func shellQuote(arg string) string {
	if arg != "" && strings.IndexFunc(arg, func(r rune) bool {
		return !(r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' || r >= '0' && r <= '9' || strings.ContainsRune("-_./=,+:@%", r))
	}) < 0 {
		return arg
	}
	return "'" + strings.ReplaceAll(arg, "'", `'\''`) + "'"
}

// ninjaShellArgs is the value of a ninja variable expanded into a command line: each argument
// shell-quoted, then ninja's "$" escaped.
func ninjaShellArgs(args []string) string {
	var b strings.Builder
	for i, a := range args {
		if i > 0 {
			b.WriteByte(' ')
		}
		b.WriteString(strings.ReplaceAll(shellQuote(a), "$", "$$"))
	}
	return b.String()
}

// ninjaEscape escapes a path in a ninja build line.
func ninjaEscape(p string) string {
	return strings.NewReplacer("$", "$$", " ", "$ ", ":", "$:").Replace(p)
}

// ninjaLinkBuild is the build line of the game binary: the objects are its inputs, the static
// libraries it links (the shader translator, SPIRV-Cross, LibAtrac9) implicit inputs, so a
// library copied in after a rebuild relinks the game.
func ninjaLinkBuild(target string, objs, libs []string) string {
	var b strings.Builder
	b.WriteString("\nbuild ")
	b.WriteString(ninjaEscape(target))
	b.WriteString(": link_bin")
	for _, o := range objs {
		b.WriteByte(' ')
		b.WriteString(ninjaEscape(o))
	}
	if len(libs) > 0 {
		b.WriteString(" |")
		for _, l := range libs {
			b.WriteByte(' ')
			b.WriteString(ninjaEscape(l))
		}
	}
	b.WriteByte('\n')
	return b.String()
}

// compileParallel compiles cFiles and links targetBin. progress, when non-nil, is called with
// the number of files compiled so far and the total, as files complete. When ctx ends, the
// running ninja or clang process groups are stopped and errBuildCancelled is returned.
func compileParallel(ctx context.Context, cFiles []string, outDir string, includeDirs []string, targetBin string, numWorkers int, optLevel string, asan bool, progress func(done, total int64)) error {
	ftCflags, ftLibs := findFreeType()
	if len(ftCflags)+len(ftLibs) == 0 {
		fmt.Fprintln(logw, "             FreeType: not found")
	} else {
		fmt.Fprintf(logw, "             FreeType: %s\n", strings.Join(append(slices.Clone(ftCflags), ftLibs...), " "))
	}
	ffCflags, ffLibs := findFFmpeg()
	if len(ffCflags)+len(ffLibs) == 0 {
		fmt.Fprintln(logw, "             FFmpeg: not found")
	} else {
		fmt.Fprintf(logw, "             FFmpeg: %s\n", strings.Join(append(slices.Clone(ffCflags), ffLibs...), " "))
	}
	// Try building with Ninja first if available
	if ninjaPath := findNinja(); ninjaPath != "" {
		fmt.Fprintf(logw, "             ninja: %s\n", ninjaPath)
		if err := generateNinjaBuild(cFiles, outDir, includeDirs, targetBin, optLevel, asan); err != nil {
			fmt.Fprintf(logw, "             ninja: cannot write build.ninja (%v); compiling with clang directly\n", err)
		} else {
			err := runNinja(ctx, ninjaPath, outDir, numWorkers, progress)
			if err == nil || errors.Is(err, errBuildCancelled) {
				return err
			}
			fmt.Fprintf(logw, "             ninja: build failed (%v); compiling with clang directly\n", err)
		}
	} else {
		fmt.Fprintln(logw, "             ninja: not found; compiling with clang directly")
	}
	total := int64(len(cFiles))
	var finished atomic.Int64
	fileDone := func() {
		if progress != nil {
			progress(finished.Add(1), total)
		}
	}

	// Build a flags fingerprint: any flag change (opt level, asan, includes)
	// must invalidate all cached object files.
	var flagsKey strings.Builder
	flagsKey.WriteString("-O")
	flagsKey.WriteString(optLevel)
	if asan {
		flagsKey.WriteString(" -fsanitize=address,undefined")
	}
	for _, inc := range includeDirs {
		flagsKey.WriteByte(' ')
		flagsKey.WriteString(inc)
	}
	for _, f := range ftCflags {
		flagsKey.WriteByte(' ')
		flagsKey.WriteString(f)
	}
	flagsFingerprint := flagsKey.String()

	objFiles := make([]string, len(cFiles))
	errChan := make(chan error, len(cFiles))
	jobs := make(chan int, len(cFiles))

	var wg sync.WaitGroup
	for range numWorkers {
		wg.Go(func() {
			for idx := range jobs {
				if ctx.Err() != nil {
					return
				}
				cFile := cFiles[idx]
				ext := filepath.Ext(cFile)
				objFile := strings.TrimSuffix(cFile, ext) + ".o"
				flagsFile := objFile + ".flags"
				objFiles[idx] = objFile

				// Check if object is up-to-date:
				// 1. .o must exist and be newer than .c
				// 2. .flags sidecar must match current compiler flags
				upToDate := false
				cStat, errC := os.Stat(cFile)
				oStat, errO := os.Stat(objFile)
				if errC == nil && errO == nil && oStat.ModTime().After(cStat.ModTime()) {
					if prevFlags, err := os.ReadFile(flagsFile); err == nil {
						upToDate = string(prevFlags) == flagsFingerprint
					}
				}
				if upToDate {
					fileDone()
					continue
				}

				clangArgs := []string{
					"-O" + optLevel,
					"-ffp-contract=off",
					"-fvisibility=hidden",
					"-Wno-parentheses-equality",
					"-I" + outDir,
				}
				for _, inc := range includeDirs {
					clangArgs = append(clangArgs, "-I"+inc)
				}
				clangArgs = append(clangArgs, ftCflags...)
				clangArgs = append(clangArgs, ffCflags...)
				if asan {
					clangArgs = append(clangArgs, "-fsanitize=address,undefined", "-fno-omit-frame-pointer")
				}
				compilerBin := "clang"
				if ext == ".m" {
					clangArgs = append(clangArgs, "-fobjc-arc")
				} else if ext == ".mm" {
					clangArgs = append(clangArgs, "-fobjc-arc", "-std=c++17")
					compilerBin = "clang++"
				} else if ext == ".cpp" {
					clangArgs = append(clangArgs, "-std=c++17")
					compilerBin = "clang++"
				}
				clangArgs = append(clangArgs, "-c", cFile, "-o", objFile)
				if runtime.GOOS == "darwin" && runtime.GOARCH == "arm64" {
					clangArgs = append([]string{"-target", "arm64-apple-darwin", "-mcpu=apple-m1", "-DRECOMP_ARM64_ASM=1"}, clangArgs...)
				}

				cmd := groupCommand(ctx, compilerBin, clangArgs...)
				if out, err := cmd.CombinedOutput(); err != nil {
					errChan <- cancelledOr(ctx, fmt.Errorf("error compiling %s: %w\n%s", filepath.Base(cFile), err, string(out)))
					return
				}

				// Write flags sidecar so the next run can detect flag changes.
				_ = os.WriteFile(flagsFile, []byte(flagsFingerprint), 0o644)
				fileDone()
			}
		})
	}

	for i := range cFiles {
		jobs <- i
	}
	close(jobs)

	wg.Wait()
	close(errChan)

	if ctx.Err() != nil {
		return errBuildCancelled
	}
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
			"-framework", "Metal", "-framework", "Cocoa", "-framework", "QuartzCore", "-framework", "GameController", "-framework", "AudioToolbox", "-framework", "ImageIO", "-framework", "MetalFX", "-framework", "VideoToolbox", "-framework", "CoreMedia", "-framework", "CoreVideo", "-lz")
	} else {
		linkArgs = append(linkArgs, "-Wl,--gc-sections", "-Wl,-s", "-lpthread", "-lm")
	}
	if _, gcnLib := findGcnRecompiler(); gcnLib != "" {
		linkArgs = append(linkArgs, gcnLib)
	}
	_, spirvLib := findSpirvCross()
	if spirvLib != "" {
		linkArgs = append(linkArgs, spirvLib, "-lc++")
	}
	_, atracLib, _ := findLibAtrac9()
	if atracLib != "" {
		linkArgs = append(linkArgs, atracLib)
	}
	linkArgs = append(linkArgs, ftLibs...)
	linkArgs = append(linkArgs, ffLibs...)
	linkArgs = append(linkArgs, objFiles...)

	cmd := groupCommand(ctx, "clang", linkArgs...)
	if out, err := cmd.CombinedOutput(); err != nil {
		return cancelledOr(ctx, fmt.Errorf("link error: %w\n%s", err, string(out)))
	}
	return nil
}

// runBinary runs the built game for --run; ending ctx kills it.
func runBinary(ctx context.Context, targetBin string, timeoutSec int, appDir string) error {
	var cancel context.CancelFunc
	if timeoutSec > 0 {
		ctx, cancel = context.WithTimeout(ctx, time.Duration(timeoutSec)*time.Second)
		defer cancel()
	}

	cmd := exec.CommandContext(ctx, targetBin)
	cmd.Stdout = logw // stderr in JSON mode: the game must not write into the event stream
	cmd.Stderr = os.Stderr
	cmd.Env = os.Environ()
	if appDir != "" {
		cmd.Env = append(cmd.Env, "PS4_APP_DIR="+appDir)
	}

	if timeoutSec > 0 {
		fmt.Fprintf(logw, "[ps4-recomp] Running %s (watchdog timeout: %ds)...\n", targetBin, timeoutSec)
	} else {
		fmt.Fprintf(logw, "[ps4-recomp] Running %s...\n", targetBin)
	}

	err := cmd.Run()
	if ctx.Err() == context.DeadlineExceeded {
		fmt.Fprintf(logw, "[ps4-recomp] Watchdog timeout of %ds reached. Terminated cleanly.\n", timeoutSec)
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
		cleanName := strings.TrimPrefix(strings.ReplaceAll(name, "\\", "/"), "%s/")
		if _, ok := found[strings.ToLower(filepath.Base(name))]; ok {
			continue
		}
		if _, ok := found[strings.ToLower(filepath.Base(cleanName))]; ok {
			continue
		}
		fmt.Fprintf(logw, "             Referenced module %s was not found for AOT linking\n", name)
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
			fmt.Fprintf(logw, "             Skipping module %s: %v\n", filepath.Base(ref.Path), err)
			continue
		}
		if elfloader.Overlaps(main, mod) {
			if err := mod.ApplyBias(nextBase - mod.MinVAddr); err != nil {
				return nil, fmt.Errorf("failed to relocate module %s: %w", filepath.Base(ref.Path), err)
			}
		}
		for _, p := range elfloader.AddTLSModule(main, mod) {
			fmt.Fprintf(os.Stderr, "             Warning: %s\n", p)
		}
		if err := elfloader.MergeImages(main, mod); err != nil {
			return nil, fmt.Errorf("failed to merge module %s: %w", filepath.Base(ref.Path), err)
		}
		nextBase = (main.MaxVAddr + 0xFFFF) &^ 0xFFFF
		exports := mod.ExportedFunctions()
		modules = append(modules, emitter.GuestModule{
			FileName:       mod.FileName,
			Aliases:        ref.Aliases,
			Exports:        exports,
			Init:           append([]uint64(nil), mod.InitArray...),
			StartAddr:      mod.MinVAddr,
			EndAddr:        mod.MaxVAddr,
			Seg0Addr:       mod.Seg0Addr,
			Seg0Size:       mod.Seg0Size,
			EHFrameHdrAddr: mod.EHFrameHdrAddr,
			EHFrameHdrSize: mod.EHFrameHdrSize,
			EHFrameAddr:    mod.EHFrameAddr,
			EHFrameSize:    mod.EHFrameSize,
			Segments:       mod.Segments,
		})
		fmt.Fprintf(logw, "             Linked module %s (%d exports, load 0x%x-0x%x)\n",
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
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer func() { _ = in.Close() }()

	srcInfo, err := in.Stat()
	if err != nil {
		return err
	}

	// Skip copy when destination already exists and is up-to-date.
	// This is critical for Ninja incremental builds: if we truncate+rewrite
	// headers even with identical content, Ninja sees a newer mtime on every
	// header file and recompiles all generated C chunks unconditionally.
	if dstInfo, err := os.Stat(dst); err == nil {
		if dstInfo.Size() == srcInfo.Size() && !srcInfo.ModTime().After(dstInfo.ModTime()) {
			return nil
		}
	}

	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}

	mode := srcInfo.Mode()
	if mode&0o111 != 0 {
		mode = 0o755
	} else {
		mode = 0o644
	}

	out, err := os.OpenFile(dst, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, mode)
	if err != nil {
		return err
	}

	if _, err := io.Copy(out, in); err != nil {
		_ = out.Close()
		return err
	}
	if err := out.Close(); err != nil {
		return err
	}
	_ = os.Chmod(dst, mode)
	return nil
}

// isRuntimeSource reports whether a file of the copied runtime is compiled. AppleDouble
// sidecars ("._<name>", which macOS writes next to each file with extended attributes on a
// volume that has none, such as exFAT) carry a source extension but are not sources.
func isRuntimeSource(name string) bool {
	if strings.HasPrefix(name, "._") {
		return false
	}
	switch filepath.Ext(name) {
	case ".c", ".m", ".mm", ".cpp":
		return true
	}
	return false
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

func copyDirFiltered(src, dst string) error {
	return filepath.Walk(src, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		name := info.Name()
		if strings.HasPrefix(name, ".") {
			if info.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}
		ext := strings.ToLower(filepath.Ext(name))
		if ext == ".prx" || ext == ".elf" || strings.EqualFold(name, "eboot.bin") {
			if info.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}
		rel, err := filepath.Rel(src, path)
		if err != nil {
			return err
		}
		target := filepath.Join(dst, rel)
		if info.IsDir() {
			return os.MkdirAll(target, 0o755)
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

	// Keep cfg.OutDir/ps4_app updated
	compatBin := filepath.Join(cfg.OutDir, "ps4_app")
	if compatBin != compiledBin {
		_ = os.Remove(compatBin)
		_ = copyFile(compiledBin, compatBin)
		_ = os.Chmod(compatBin, 0o755)
	}

	// 2. Copy guest_image.bin into Contents/Resources/
	guestImgSrc := filepath.Join(cfg.OutDir, "guest_image.bin")
	guestImgDst := filepath.Join(resourcesDir, "guest_image.bin")
	if fi, err := os.Stat(guestImgSrc); err == nil && !fi.IsDir() {
		_ = copyFile(guestImgSrc, guestImgDst)
	}

	// 3. Copy resources into Contents/Resources/
	if cfg.AppDir != "" {
		if cfg.CopyResources {
			entries, err := os.ReadDir(cfg.AppDir)
			if err == nil {
				absOut, _ := filepath.Abs(cfg.OutDir)
				absElf, _ := filepath.Abs(cfg.ElfPath)
				for _, entry := range entries {
					name := entry.Name()
					if strings.HasPrefix(name, ".") || strings.EqualFold(name, "prx") {
						continue
					}
					srcPath := filepath.Join(cfg.AppDir, name)
					absSrc, _ := filepath.Abs(srcPath)
					if absSrc == absOut || absSrc == absElf {
						continue
					}
					ext := strings.ToLower(filepath.Ext(name))
					if ext == ".prx" || ext == ".elf" || strings.EqualFold(name, "eboot.bin") {
						continue
					}
					dstPath := filepath.Join(resourcesDir, name)
					if entry.IsDir() {
						_ = os.RemoveAll(dstPath)
						if err := copyDirFiltered(srcPath, dstPath); err != nil {
							return "", fmt.Errorf("failed to copy %s into app bundle: %w", name, err)
						}
					} else {
						if err := copyFile(srcPath, dstPath); err != nil {
							return "", fmt.Errorf("failed to copy %s into app bundle: %w", name, err)
						}
					}
				}
			}
		} else {
			// Default selective copy of assets/ and sce_sys/ if present
			srcAssets := filepath.Join(cfg.AppDir, "assets")
			dstAssets := filepath.Join(resourcesDir, "assets")
			if fi, err := os.Stat(srcAssets); err == nil && fi.IsDir() {
				_ = os.RemoveAll(dstAssets)
				if err := copyDir(srcAssets, dstAssets); err != nil {
					return "", fmt.Errorf("failed to copy assets into app bundle: %w", err)
				}
			}

			srcSceSys := filepath.Join(cfg.AppDir, "sce_sys")
			dstSceSys := filepath.Join(resourcesDir, "sce_sys")
			if fi, err := os.Stat(srcSceSys); err == nil && fi.IsDir() {
				_ = os.RemoveAll(dstSceSys)
				if err := copyDir(srcSceSys, dstSceSys); err != nil {
					return "", fmt.Errorf("failed to copy sce_sys into app bundle: %w", err)
				}
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
	if err := os.WriteFile(filepath.Join(contentsDir, "Info.plist"), []byte(appInfoPlist(appName, hasIcon, bundleMetaFor(cfg.AppDir, appName))), 0o644); err != nil {
		return "", fmt.Errorf("failed to write Info.plist: %w", err)
	}

	// 6. Sign the bundle ad hoc: binds Info.plist and seals the resources, so the system (Game
	// Mode, Metal shader cache, Gatekeeper on a local build) sees one consistent app identity.
	// A signing or verification failure fails the build (build_failed with --json).
	if err := signAppBundle(bundleDir); err != nil {
		return "", fmt.Errorf("ad-hoc signing failed: %w", err)
	}

	return bundleDir, nil
}

// bundleMeta is the identity a game bundle presents to macOS.
type bundleMeta struct{ BundleID, DisplayName, Version string }

// readSFO loads <appDir>/sce_sys/param.sfo, or returns nil when there is none.
func readSFO(appDir string) *sfo.File {
	if appDir == "" {
		return nil
	}
	f, err := sfo.Load(filepath.Join(appDir, "sce_sys", "param.sfo"))
	if err != nil {
		return nil
	}
	return f
}

// titleIDOf returns TITLE_ID from the app dir's param.sfo, or "".
func titleIDOf(appDir string) string {
	if f := readSFO(appDir); f != nil {
		id, _ := f.String("TITLE_ID")
		return id
	}
	return ""
}

// bundleMetaFor derives the bundle identity from <appDir>/sce_sys/param.sfo: bundle id
// org.ps4recomp.<TITLE_ID>, TITLE as the display name and APP_VER as the version (real dumps
// often carry only VERSION, which is the fallback). Missing values keep the generic defaults.
func bundleMetaFor(appDir, appName string) bundleMeta {
	m := bundleMeta{BundleID: "org.ps4recomp." + appName, DisplayName: appName, Version: "1.0"}
	f := readSFO(appDir)
	if f == nil {
		return m
	}
	if id, ok := f.String("TITLE_ID"); ok {
		if id = sanitizeBundleID(id); id != "" {
			m.BundleID = "org.ps4recomp." + id
		}
	}
	if t, ok := f.String("TITLE"); ok && strings.TrimSpace(t) != "" {
		m.DisplayName = t
	}
	for _, key := range []string{"APP_VER", "VERSION"} {
		if v, ok := f.String(key); ok && strings.TrimSpace(v) != "" {
			m.Version = v
			break
		}
	}
	return m
}

// sanitizeBundleID keeps only the characters a bundle identifier allows.
func sanitizeBundleID(s string) string {
	return strings.Map(func(r rune) rune {
		if r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' || r >= '0' && r <= '9' || r == '.' || r == '-' {
			return r
		}
		return -1
	}, s)
}

// xmlEscape escapes s for use as plist string content.
func xmlEscape(s string) string {
	var b strings.Builder
	if err := xml.EscapeText(&b, []byte(s)); err != nil {
		return ""
	}
	return b.String()
}

// appInfoPlist is the Contents/Info.plist of a generated game bundle. LSSupportsGameMode and the
// games category let macOS run it in Game Mode (CPU and GPU priority, lower Bluetooth latency for
// controllers) when it is full screen.
func appInfoPlist(appName string, hasIcon bool, meta bundleMeta) string {
	iconXML := ""
	if hasIcon {
		iconXML = "    <key>CFBundleIconFile</key>\n    <string>AppIcon</string>\n"
	}
	return fmt.Sprintf(`<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>%s</string>
    <key>CFBundleIdentifier</key>
    <string>%s</string>
    <key>CFBundleName</key>
    <string>%s</string>
    <key>CFBundleDisplayName</key>
    <string>%s</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>%s</string>
    <key>CFBundleVersion</key>
    <string>%s</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.games</string>
    <key>LSSupportsGameMode</key>
    <true/>
    <key>NSHighResolutionCapable</key>
    <true/>
%s</dict>
</plist>
`, xmlEscape(appName), xmlEscape(meta.BundleID), xmlEscape(meta.DisplayName), xmlEscape(meta.DisplayName),
		xmlEscape(meta.Version), xmlEscape(meta.Version), iconXML)
}

// signAppBundle signs the bundle ad hoc and verifies the signature strictly.
func signAppBundle(bundleDir string) error {
	// Finder info and resource forks on files copied from the game folder are "detritus" that a
	// strict signature does not allow.
	if out, err := exec.Command("xattr", "-cr", bundleDir).CombinedOutput(); err != nil {
		return fmt.Errorf("xattr -cr %s: %w: %s", bundleDir, err, strings.TrimSpace(string(out)))
	}
	if out, err := exec.Command("codesign", "--force", "--sign", "-", bundleDir).CombinedOutput(); err != nil {
		return fmt.Errorf("codesign %s: %w: %s", bundleDir, err, strings.TrimSpace(string(out)))
	}
	if out, err := exec.Command("codesign", "--verify", "--deep", "--strict", bundleDir).CombinedOutput(); err != nil {
		return fmt.Errorf("codesign --verify %s: %w: %s", bundleDir, err, strings.TrimSpace(string(out)))
	}
	return nil
}

// findGcnRecompiler locates the GCN shader recompiler library (shadPS4's
// shader_recompiler packaged in 3rdparty/gcn-recompiler): include dir and the
// prebuilt static library.
func findGcnRecompiler() (string, string) {
	candidates := []string{
		"3rdparty/gcn-recompiler",
	}
	if exe, err := os.Executable(); err == nil {
		exeDir := filepath.Dir(exe)
		candidates = append(candidates,
			filepath.Join(exeDir, "3rdparty", "gcn-recompiler"),
			filepath.Join(exeDir, "..", "3rdparty", "gcn-recompiler"),
		)
	}
	for _, dir := range candidates {
		libPath := filepath.Join(dir, "libgcn_recompiler.a")
		if _, err := os.Stat(libPath); err == nil {
			absInc, _ := filepath.Abs(filepath.Join(dir, "include"))
			absLib, _ := filepath.Abs(libPath)
			return absInc, absLib
		}
	}
	return "", ""
}

func findSpirvCross() (string, string) {
	candidates := []string{
		"3rdparty/spirv-cross",
	}
	if exe, err := os.Executable(); err == nil {
		exeDir := filepath.Dir(exe)
		candidates = append(candidates,
			filepath.Join(exeDir, "3rdparty", "spirv-cross"),
			filepath.Join(exeDir, "..", "3rdparty", "spirv-cross"),
		)
	}
	for _, dir := range candidates {
		libPath := filepath.Join(dir, "libspirv-cross.a")
		if _, err := os.Stat(libPath); err == nil {
			absDir, _ := filepath.Abs(dir)
			absLib, _ := filepath.Abs(libPath)
			return absDir, absLib
		}
	}
	return "", ""
}

func findLibAtrac9() (string, string, []string) {
	candidates := []string{
		"3rdparty/libatrac9",
	}
	if exe, err := os.Executable(); err == nil {
		exeDir := filepath.Dir(exe)
		candidates = append(candidates,
			filepath.Join(exeDir, "3rdparty", "libatrac9"),
			filepath.Join(exeDir, "..", "3rdparty", "libatrac9"),
		)
	}
	for _, dir := range candidates {
		hdrDir := filepath.Join(dir, "C", "src")
		hdr := filepath.Join(hdrDir, "libatrac9.h")
		if _, err := os.Stat(hdr); err == nil {
			absInc, _ := filepath.Abs(hdrDir)
			// Check for pre-built static library
			for _, libCandidate := range []string{
				filepath.Join(dir, "libatrac9.a"),
				filepath.Join(dir, "C", "bin", "libatrac9.a"),
				filepath.Join(hdrDir, "libatrac9.a"),
			} {
				if _, err := os.Stat(libCandidate); err == nil {
					absLib, _ := filepath.Abs(libCandidate)
					return absInc, absLib, nil
				}
			}
			matches, _ := filepath.Glob(filepath.Join(absInc, "*.c"))
			return absInc, "", matches
		}
	}
	return "", "", nil
}

func findMinimp3() string {
	candidates := []string{
		"3rdparty/minimp3",
	}
	if exe, err := os.Executable(); err == nil {
		exeDir := filepath.Dir(exe)
		candidates = append(candidates,
			filepath.Join(exeDir, "3rdparty", "minimp3"),
			filepath.Join(exeDir, "..", "3rdparty", "minimp3"),
		)
	}
	for _, dir := range candidates {
		hdr := filepath.Join(dir, "minimp3.h")
		if _, err := os.Stat(hdr); err == nil {
			absDir, _ := filepath.Abs(dir)
			return absDir
		}
	}
	return ""
}
