package main

import (
	"debug/elf"
	"encoding/binary"
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

func main() {
	elfPath := flag.String("elf", "hello_world.elf", "Path to input PS4 ELF binary")
	outDir := flag.String("out", "build", "Output directory for recompiled C code and binaries")
	compile := flag.Bool("compile", true, "Compile generated C code to native ARM64 binary with clang")
	allSymbols := flag.Bool("all-symbols", false, "Recompile all symbols in symbol table even if not reachable")
	flag.Parse()

	fmt.Printf("[ps4-recomp] Loading ELF: %s\n", *elfPath)
	loaded, err := elfloader.LoadELF(*elfPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error loading ELF: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("[ps4-recomp] Entry point: 0x%x, Segments: %d, Relocations: %d\n",
		loaded.EntryPoint, len(loaded.Segments), len(loaded.Relocations))

	fmt.Printf("[ps4-recomp] Analyzing CFG and discovering code...\n")
	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error creating disassembler: %v\n", err)
		os.Exit(1)
	}

	textSec, ok := loaded.Sections[".text"]
	if !ok {
		fmt.Fprintf(os.Stderr, "No .text section found\n")
		os.Exit(1)
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
			if *allSymbols || (sym.Address >= textSec.Addr && sym.Address < textSec.Addr+textSec.Size) {
				entries = append(entries, sym.Address)
			}
		}
	}

	if err := d.AnalyzeReachable(entries); err != nil {
		fmt.Fprintf(os.Stderr, "Error analyzing reachable code: %v\n", err)
		os.Exit(1)
	}

	totalBlocks := 0
	totalInsts := 0
	for _, fn := range d.Functions {
		totalBlocks += len(fn.Blocks)
		for _, b := range fn.Blocks {
			totalInsts += len(b.Insts)
		}
	}
	fmt.Printf("[ps4-recomp] Discovered %d functions, %d basic blocks, %d instructions\n",
		len(d.Functions), totalBlocks, totalInsts)

	knownFuncs := make(map[uint64]bool)
	for addr := range d.Functions {
		knownFuncs[addr] = true
	}

	l := lifter.NewLifter(knownFuncs)
	em := emitter.NewCEmitter(loaded, d, l)

	if err := os.MkdirAll(*outDir, 0o755); err != nil {
		fmt.Fprintf(os.Stderr, "Error creating output dir: %v\n", err)
		os.Exit(1)
	}

	// Copy runtime files to outDir
	runtimeFiles := []string{"recomp_runtime.h", "recomp_runtime.c", "ps4_syscalls.c", "ps4_threading.c", "ps4_sync.c"}
	for _, rf := range runtimeFiles {
		src := filepath.Join("pkg", "runtime", rf)
		dst := filepath.Join(*outDir, rf)
		if err := copyFile(src, dst); err != nil {
			fmt.Fprintf(os.Stderr, "Error copying runtime file %s: %v\n", rf, err)
			os.Exit(1)
		}
	}

	fmt.Printf("[ps4-recomp] Emitting modular C source files to %s/...\n", *outDir)
	cFiles, err := em.EmitAll(*outDir)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error emitting C code: %v\n", err)
		os.Exit(1)
	}

	cFiles = append(cFiles, filepath.Join(*outDir, "recomp_runtime.c"))
	cFiles = append(cFiles, filepath.Join(*outDir, "ps4_syscalls.c"))
	cFiles = append(cFiles, filepath.Join(*outDir, "ps4_threading.c"))
	cFiles = append(cFiles, filepath.Join(*outDir, "ps4_sync.c"))
	fmt.Printf("[ps4-recomp] C source emission complete (%d source files).\n", len(cFiles))

	if *compile {
		targetBin := filepath.Join(*outDir, "ps4_app")
		start := time.Now()
		if err := compileParallel(cFiles, *outDir, targetBin); err != nil {
			fmt.Fprintf(os.Stderr, "Compilation failed: %v\n", err)
			os.Exit(1)
		}
		fmt.Printf("[ps4-recomp] Successfully compiled native ARM64 binary in %v: %s\n",
			time.Since(start).Round(time.Millisecond), targetBin)
	}
}

func compileParallel(cFiles []string, outDir, targetBin string) error {
	numWorkers := runtime.NumCPU()
	if numWorkers < 1 {
		numWorkers = 1
	}
	fmt.Printf("[ps4-recomp] Compiling %d C files using %d parallel clang workers...\n", len(cFiles), numWorkers)

	objFiles := make([]string, len(cFiles))
	errChan := make(chan error, len(cFiles))
	jobs := make(chan int, len(cFiles))

	var wg sync.WaitGroup
	for w := 0; w < numWorkers; w++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for idx := range jobs {
				cFile := cFiles[idx]
				objFile := strings.TrimSuffix(cFile, ".c") + ".o"
				objFiles[idx] = objFile

				args := []string{"-O2", "-mcpu=apple-m1", "-fvisibility=hidden", "-target", "arm64-apple-darwin", "-I" + outDir, "-c", cFile, "-o", objFile}
				cmd := exec.Command("clang", args...)
				if out, err := cmd.CombinedOutput(); err != nil {
					errChan <- fmt.Errorf("error compiling %s: %w\n%s", filepath.Base(cFile), err, string(out))
					return
				}
			}
		}()
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

	fmt.Printf("[ps4-recomp] Linking native ARM64 binary: %s\n", targetBin)
	linkArgs := []string{"-target", "arm64-apple-darwin", "-Wl,-dead_strip", "-Wl,-x", "-o", targetBin}
	linkArgs = append(linkArgs, objFiles...)
	cmd := exec.Command("clang", linkArgs...)
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("link error: %w\n%s", err, string(out))
	}
	return nil
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
