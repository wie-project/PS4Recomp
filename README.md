# PS4Recomp

A static ahead-of-time (AOT) recompiler that translates PlayStation 4 (x86-64 ELF) binaries into native Apple Silicon (ARM64 macOS) executables without JIT compilation.

## Overview

The recompiler statically parses the guest x86-64 ELF binary, recovers functions and basic blocks, lifts x86-64 machine instructions into equivalent C code, and compiles the result into a native Mach-O executable using host Clang.

### Key Components

- **ELF Loader (`pkg/elfloader`)**: Loads ELF segments into a contiguous guest memory image, extracts static and dynamic symbols, parses `.init_array`, and handles ELF64 relocations (`.rela.dyn` and `.rela.plt`).
- **Disassembler & CFG (`pkg/disasm`)**: Recovers functions, basic blocks, indirect jump tables from `.rodata`, and performs backward flag liveness analysis for dead flag elimination.
- **Instruction Lifter (`pkg/lifter`)**: Translates x86-64 instructions into portable C statements, handling register state, sign-extended displacement addressing, arithmetic/logic operations, and CPU flags.
- **Runtime Environment (`pkg/runtime`)**: 
  - Manages a dynamic flat guest address space (image size + dynamic headroom, configurable via `PS4_RECOMP_MEM`).
  - Sets up the guest stack, process arguments (`argc`, `argv`, `envp`, `auxv`), and dynamic TLS (Thread Local Storage).
  - Starts execution from the official CRT `_start` entry point, allowing the guest libc to initialize stack guards and invoke `.init_array` constructors natively.
  - Provides shims for PS4/FreeBSD kernel calls and runtime APIs (`sceKernelUsleep`, `mmap`, `munmap`, `writev`, `open`, `close`, `fstat`, etc.).
- **Driver CLI (`cmd/ps4-recomp`)**: Orchestrates the pipeline: loading ELF, lifting code with DFE, emitting partitioned C files with `__restrict__` qualifiers, and compiling the final binary via Clang (`-O2 -mcpu=apple-m1`).

## Prerequisites

- macOS running on Apple Silicon (ARM64)
- Go 1.21 or newer
- Xcode Command Line Tools (`clang`)

## Building and Running

### 1. Run Unit Tests and Static Analysis

```bash
go test -v ./...
golangci-lint run --no-config ./...
```

### 2. Recompile an ELF Binary

To recompile `hello_world.elf` and produce a native executable:

```bash
go run ./cmd/ps4-recomp -elf hello_world.elf -out build
```

This generates:
- `build/code_*.c`: Partitioned lifted guest code functions (compiled concurrently with multi-worker Clang).
- `build/dispatch.c`: Unified jump dispatch table connecting basic blocks and runtime shims.
- `build/guest_image.bin`: Raw binary guest ELF memory image mapped into guest address space at runtime.
- `build/ps4_app`: Native Apple Silicon ARM64 Mach-O binary.

### 3. Run Recompiled Binary

Run with a timeout or alarm if the binary ends in an intentional infinite loop:

```bash
perl -e 'alarm 4; exec "./build/ps4_app"'
```

Output:
```text
[ps4-recomp] Initializing runtime...
[ps4-recomp] Loading guest memory image (1146880 bytes), allocated dynamic address space (82.0 MB)
[ps4-recomp] Executing _start (0xb0b88)...
main: Hello world! Waiting 2 seconds!
main: Done. Infinitely looping...
```

To configure custom guest memory allocation (e.g. 256MB or 2GB):
```bash
PS4_RECOMP_MEM=256M perl -e 'alarm 4; exec "./build/ps4_app"'
```

## Current Limitations / Known Restrictions

The project adheres to strict AOT compilation principles (no runtime JIT emulation, no artificial shortcuts). The current boundaries and restrictions are:

1. **Instruction Set Coverage (9.01% Global x86-64)**:
   - Supports 137 of 1,520 standard x86 opcodes (100% of instructions present in `hello_world.elf`).
   - Implemented: Core integer ALU, control flow, condition testing / flags, basic SSE scalar moves, stack operations, x87 FPU basics.
   - Not yet lifted:
     - Vector SIMD operations (AVX, AVX2, packed SSE2/3/4 arithmetic).
     - 64-bit integer division (`IDIV`, `CQO`, `CDQ`, `CWD`, `CBW`).
     - Bit manipulation instructions (`POPCNT`, `LZCNT`, `TZCNT`, `BSR`, bit tests `BTS`/`BTR`/`BTC`).
     - String operations (`MOVSB`/`MOVSW`/`MOVSQ`, `STOSB`/`STOSQ`, `CMPSB`, `LODSB`, `SCASB`).
     - Atomic multi-byte exchange/compare operations (`CMPXCHG8B`/`CMPXCHG16B`).

2. **Single-Threaded Execution**:
   - The runtime currently executes a single guest thread.
   - PS4 kernel thread creation (`scePthreadCreate`, `pthread_create`) and synchronization primitives (kernel futexes, condition variables) are not yet implemented.

3. **Graphics & Audio Subsystems**:
   - Currently headless and console/terminal I/O only.
   - PS4 low-level rendering APIs (Gnm, Gnmx, AGC) and audio out (`libSceAudioOut`) are not yet shimmed.

4. **Dynamic Linker & Relocation Scope**:
   - Handles static executables and standard PLT relocation types (`R_X86_64_JUMP_SLOT`, `R_X86_64_64`, `R_X86_64_RELATIVE`).
   - Data GOT relocations (`R_X86_64_GLOB_DAT`, copy relocations) are not yet implemented and will be added as required by multi-module binaries.

5. **Syscall & Kernel Shim Coverage**:
   - Implements only the subset of FreeBSD/PS4 syscalls required for standard CRT initialization and basic I/O (`sys_write`, `sys_writev`, `sys_mmap`, `sys_munmap`, `sys_nanosleep`, `sys_clock_gettime`, `sys_exit`, `sceKernelUsleep`).
   - Unimplemented kernel calls return `-ENOSYS` or abort execution.

## Project Structure

```text
├── cmd/
│   └── ps4-recomp/        # Main recompiler CLI driver
├── pkg/
│   ├── disasm/            # Disassembly, control flow recovery, and dead flag analysis
│   ├── elfloader/         # ELF parsing and relocation handling
│   ├── emitter/           # Partitioned C emission and Clang compilation orchestration
│   ├── lifter/            # x86-64 instruction lifting to portable C
│   └── runtime/           # Host runtime, memory manager, and PS4 syscall/library shims
└── hello_world.elf        # Target PS4 ELF binary
```
