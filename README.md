# PS4Recomp

A static ahead-of-time (AOT) recompiler that translates PlayStation 4 (x86-64 ELF) binaries into native Apple Silicon (ARM64 macOS) executables without JIT compilation.

## Overview

The recompiler statically parses the guest x86-64 ELF binary, recovers functions and basic blocks, lifts x86-64 machine instructions into equivalent C code, and compiles the result into a native Mach-O executable using host Clang.

### Key Components

- **ELF Loader (`pkg/elfloader`)**: Loads ELF segments into a contiguous guest memory image, extracts static and dynamic symbols, parses `.init_array`, and handles ELF64 relocations (`.rela.dyn` and `.rela.plt`).
- **Disassembler & CFG (`pkg/disasm`)**: Recovers functions, basic blocks, and indirect jump tables from `.rodata`.
- **Instruction Lifter (`pkg/lifter`)**: Translates x86-64 instructions into portable C statements, handling register state, sign-extended displacement addressing, arithmetic/logic operations, and CPU flags.
- **Runtime Environment (`pkg/runtime`)**: 
  - Manages a 1 GB flat guest address space.
  - Sets up guest stack, TLS (Thread Local Storage), and arguments.
  - Provides shims for PS4/FreeBSD kernel calls and runtime APIs (`sceKernelUsleep`, `mmap`, `munmap`, `writev`, `open`, `close`, `fstat`, etc.).
- **Driver CLI (`cmd/ps4-recomp`)**: Orchestrates the pipeline: loading ELF, lifting code, emitting C files, and compiling the final binary via Clang.

## Prerequisites

- macOS running on Apple Silicon (ARM64)
- Go 1.21 or newer
- Xcode Command Line Tools (`clang`)

## Building and Running

### 1. Run Unit Tests and Static Analysis

```bash
go test -v ./...
go vet ./...
```

### 2. Recompile an ELF Binary

To recompile `hello_world.elf` and produce a native executable:

```bash
go run ./cmd/ps4-recomp -elf hello_world.elf -out build
```

This generates:
- `build/recompiled_code.c`: Lifted guest code functions and dispatch registrations.
- `build/guest_image.c`: Embedded ELF image and initial memory data.
- `build/ps4_app`: Native Apple Silicon ARM64 Mach-O binary.

### 3. Run Recompiled Binary

```bash
./build/ps4_app
```

Output:
```text
[ps4-recomp] Initializing runtime...
[ps4-recomp] Calling global constructors (.init_array)...
[ps4-recomp] Running init constructor at 0x1c0...
[ps4-recomp] Executing main (0x60)...
main: Hello world! Waiting 2 seconds!
main: Done. Infinitely looping...
```

## Project Structure

```text
├── cmd/
│   └── ps4-recomp/        # Main recompiler CLI
├── pkg/
│   ├── disasm/            # Disassembler and control flow recovery
│   ├── elfloader/         # ELF parsing and relocation handling
│   ├── emitter/           # C code and build artifact generation
│   ├── lifter/            # x86-64 to C instruction lifting
│   └── runtime/           # Host runtime and PS4 syscall/library shims
└── hello_world.elf        # Target PS4 ELF binary
```
