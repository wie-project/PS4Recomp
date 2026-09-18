package main

import (
	"debug/elf"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/emitter"
	"ps4-recomp/pkg/lifter"
)

func main() {
	elfPath := flag.String("elf", "hello_world.elf", "Path to input PS4 ELF binary")
	outDir := flag.String("out", "build", "Output directory for recompiled C code and binaries")
	compile := flag.Bool("compile", true, "Compile generated C code to native ARM64 binary with clang")
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

	for _, sym := range loaded.Symbols {
		if sym.Type == elf.STT_FUNC && sym.Address != 0 {
			entries = append(entries, sym.Address)
		}
	}

	for _, rel := range loaded.Relocations {
		if rel.Addend > 0 {
			target := uint64(rel.Addend)
			if target >= textSec.Addr && target < textSec.Addr+textSec.Size {
				entries = append(entries, target)
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

	if err := os.MkdirAll(*outDir, 0755); err != nil {
		fmt.Fprintf(os.Stderr, "Error creating output dir: %v\n", err)
		os.Exit(1)
	}

	// Copy runtime files to outDir
	runtimeFiles := []string{"recomp_runtime.h", "recomp_runtime.c", "ps4_syscalls.c"}
	for _, rf := range runtimeFiles {
		src := filepath.Join("pkg", "runtime", rf)
		dst := filepath.Join(*outDir, rf)
		if err := copyFile(src, dst); err != nil {
			fmt.Fprintf(os.Stderr, "Error copying runtime file %s: %v\n", rf, err)
			os.Exit(1)
		}
	}

	fmt.Printf("[ps4-recomp] Emitting C source files to %s/...\n", *outDir)
	if err := em.EmitAll(*outDir); err != nil {
		fmt.Fprintf(os.Stderr, "Error emitting C code: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("[ps4-recomp] C source emission complete.\n")

	if *compile {
		targetBin := filepath.Join(*outDir, "ps4_app")
		fmt.Printf("[ps4-recomp] Compiling to native Apple Silicon ARM64 Mach-O: %s\n", targetBin)

		cFiles := []string{
			filepath.Join(*outDir, "main.c"),
			filepath.Join(*outDir, "recompiled_code.c"),
			filepath.Join(*outDir, "guest_image.c"),
			filepath.Join(*outDir, "recomp_runtime.c"),
			filepath.Join(*outDir, "ps4_syscalls.c"),
		}

		args := []string{"-O2", "-target", "arm64-apple-darwin", "-I" + *outDir, "-o", targetBin}
		args = append(args, cFiles...)

		cmd := exec.Command("clang", args...)
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		if err := cmd.Run(); err != nil {
			fmt.Fprintf(os.Stderr, "Clang compilation failed: %v\n", err)
			os.Exit(1)
		}

		fmt.Printf("[ps4-recomp] Successfully compiled native ARM64 binary: %s\n", targetBin)
	}
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)
	return err
}
