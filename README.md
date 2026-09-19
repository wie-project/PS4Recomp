# PS4Recomp

A static ahead-of-time (AOT) recompiler that translates PlayStation 4 (x86-64 ELF) binaries into native Apple Silicon (ARM64 macOS) executables without JIT compilation or emulation.

## Overview

The recompiler statically parses the guest x86-64 ELF binary, recovers functions and control flow graphs (CFGs), lifts x86-64 machine instructions into clean, modular C code, and compiles the result into a native Mach-O executable using host Clang.

### Key Components

- **ELF Loader (`pkg/elfloader`)**: Loads ELF segments into a contiguous guest memory image, extracts static and dynamic symbols, parses `.init_array`, and processes ELF64 relocations (`.rela.dyn` and `.rela.plt`).
- **Disassembler & CFG (`pkg/disasm`)**: Recovers functions, basic blocks, indirect jump tables from `.rodata`, and performs backward flag liveness analysis for dead flag elimination (DFE).
- **Instruction Lifter (`pkg/lifter`)**: Translates x86-64 instructions into portable C statements, handling 64-bit integer registers, sign-extended displacement addressing, arithmetic/logic operations, and CPU flags.
- **Exception Handling & Unwinding (`pkg/runtime` & `pkg/emitter`)**:
  - Full C++ exception handling (`try`, `catch`, `throw`) support without JIT.
  - Guest `libunwind` and `libcxxabi` execute natively to parse DWARF `.eh_frame` unwind tables and locate landing pads.
  - Functions that can throw or catch register lightweight `_setjmp`/`_longjmp` unwind frames (`UnwindFrame`).
  - Low-level resume point (`Registers_x86_64::jumpto` / `unw_resume`) transfers control to the enclosing landing pad with proper stack pointer restoration (`RSP += 8`), adhering to System V AMD64 ABI conventions.
- **Runtime Environment (`pkg/runtime`)**: 
  - Manages a dynamic flat guest address space (image size + dynamic headroom, configurable via `PS4_RECOMP_MEM`).
  - Sets up the guest stack, process arguments (`argc`, `argv`, `envp`, `auxv`), and dynamic TLS (Thread Local Storage).
  - Starts execution from the official CRT `_start` entry point, allowing guest libc to initialize stack guards and invoke `.init_array` constructors natively.
  - Implements PS4/FreeBSD kernel calls and runtime APIs (`sceKernelUsleep`, `mmap`, `munmap`, `writev`, `open`, `close`, `fstat`, etc.).
- **Optimized C Emitter (`pkg/emitter`)**:
  - Emits partitioned C source files for multi-threaded Clang compilation.
  - **Fast-path function entry**: Direct fallthrough on normal function entry (`ctx->rip == addr`), bypassing multi-way basic block switches.
  - **Selective unwinding**: Leaf functions (without `CALL` instructions) skip `UnwindFrame` and `_setjmp` overhead completely.
  - **Optimized flag computation**: Hot integer ALU operations prune auxiliary (`af`) and parity (`pf`) calculations, reducing Clang compilation time by over 60%.

---

## Prerequisites

- macOS running on Apple Silicon (ARM64)
- Go 1.21 or newer
- Xcode Command Line Tools (`clang`)

---

## Building and Running

### 1. Run Unit Tests and Static Analysis

```bash
go test -v ./...
golangci-lint run --no-config ./...
```

### 2. Recompile and Run Sample Binaries

#### A. Hello World (`hello_world.elf`)

Recompile the binary:
```bash
go run ./cmd/ps4-recomp -elf hello_world.elf -out build
```

Run the recompiled binary (ends in an intentional loop):
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

#### B. C++ Exceptions (`exceptions.elf`)

Recompile the binary:
```bash
go run ./cmd/ps4-recomp -elf exceptions.elf -out build_exceptions
```

Run the recompiled binary:
```bash
perl -e 'alarm 4; exec "./build_exceptions/ps4_app"'
```

Output:
```text
[ps4-recomp] Initializing runtime...
[ps4-recomp] Loading guest memory image (1277952 bytes), allocated dynamic address space (82.0 MB)
[ps4-recomp] Executing _start (0xcb1c8)...
main: Before testcase...
testcase: Caught! The exception says: Catch me if you can. :p
main: .what() = Another one.
main: Testcase PASS
main: Infinite looping...
```

---

## Environment Configuration

You can customize the guest virtual memory space by setting `PS4_RECOMP_MEM`:

```bash
# Allocate 256 MB of guest virtual address space
PS4_RECOMP_MEM=256M perl -e 'alarm 4; exec "./build/ps4_app"'
```

---

## Current Status & Limitations

The project strictly follows static recompilation principles without JIT emulation or synthetic workarounds.

1. **Instruction Set Coverage**:
   - Supports 100% of instructions present in `hello_world.elf` and `exceptions.elf` (over 130,000 instructions).
   - Core integer ALU, control flow, condition flags, basic scalar SSE, stack management, x87 FPU basics.
   - Vector SIMD operations (AVX, AVX2, packed SSE) and 64-bit integer division instructions (`IDIV`, `CQO`) can be added on demand as new testcases require.

2. **C++ Exception Handling**:
   - Supported for single-threaded call stacks with full DWARF `.eh_frame` table evaluation and landing pad dispatch.

3. **Threading**:
   - Single guest thread execution. Multi-threading and kernel synchronization shims (`scePthreadCreate`, futexes) are planned for future milestones.

4. **Graphics & Audio**:
   - Headless / terminal I/O. PS4 low-level rendering APIs (Gnm, Gnmx, AGC) and audio out are currently unmapped.

---

## Project Structure

```text
├── cmd/
│   └── ps4-recomp/        # CLI driver orchestrating the pipeline
├── pkg/
│   ├── disasm/            # Disassembly, CFG recovery, jump tables, and dead flag analysis
│   ├── elfloader/         # ELF segment loader, symbol table parser, and relocation engine
│   ├── emitter/           # Multi-file partitioned C emission and Clang compiler driver
│   ├── lifter/            # x86-64 machine instruction lifter to clean C code
│   └── runtime/           # Host runtime, flat memory space, and PS4 syscall/library shims
├── hello_world.elf        # Testcase 1: Standard I/O, usleep, global initializers
└── exceptions.elf         # Testcase 2: C++ throw/catch, DWARF unwinding, stringstream
```
