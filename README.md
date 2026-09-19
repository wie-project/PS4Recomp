# PS4Recomp

A static ahead-of-time (AOT) recompiler that translates PlayStation 4 (x86-64 ELF) binaries into native Apple Silicon (ARM64 macOS) executables without JIT compilation or emulation.

## Overview

The recompiler statically parses the guest x86-64 ELF binary, recovers functions and control flow graphs (CFGs), lifts x86-64 machine instructions into clean, modular C code, and compiles the result into a native Mach-O executable using host Clang.

### Key Architectural Components

- **ELF Loader (`pkg/elfloader`)**: Loads ELF segments into a contiguous guest memory image, extracts static and dynamic symbols, parses `.init_array`, and processes ELF64 relocations (`.rela.dyn` and `.rela.plt`).
- **Disassembler & CFG (`pkg/disasm`)**: Recovers functions, basic blocks, indirect jump tables from `.rodata`, and performs backward flag liveness analysis for dead flag elimination (DFE).
- **Instruction Lifter (`pkg/lifter`)**: Modular instruction lifter translating x86-64 instructions into clean C:
  - `alu.go`: Integer arithmetic, logic, shifts, bit tests (`BT`, `BSF`, `BSR`), multiply, divide (`IMUL`, `MUL`, `DIV`, `IDIV`), and atomic hardware exchange (`XADD` via `__sync_fetch_and_add`, `CMPXCHG` via `__sync_val_compare_and_swap`).
  - `control_flow.go`: Condition evaluation (`setcc`, `cmovcc`) and exception unwinding frame detection (`isContextRestoreFunction`).
  - `fpu.go`: x87 FPU stack emulation (`fld`, `fild`, `fstp`, `fist`, `fxch`, `fadd`, `fsub`, `fmul`, `fdiv`, `fucomi`, `fldcw`, `fnstcw`).
  - `simd.go`: SSE/AVX vector moves, bitwise operations, packed integer math, scalar/packed floats (`ADDSD`, `MULSD`, `SUBSD`, `DIVSD`, `UCOMISD`), and conversions (`CVTSI2SD`, `CVTTSD2SI`, `CVTSS2SD`, `CVTSD2SS`).
  - `lifter.go`: Core instruction dispatch loop, opcode support registry, and operand decode/read/write primitives.
- **Exception Handling & Unwinding (`pkg/runtime` & `pkg/emitter`)**:
  - Full C++ exception handling (`try`, `catch`, `throw`) support without JIT.
  - Guest `libunwind` and `libcxxabi` execute natively to parse DWARF `.eh_frame` unwind tables and locate landing pads.
  - Functions that can throw or catch register lightweight `_setjmp`/`_longjmp` unwind frames (`UnwindFrame`).
  - Low-level resume points (`Registers_x86_64::jumpto` / `unw_resume`) transfer control to the enclosing landing pad with proper stack pointer restoration (`RSP += 8`), adhering to System V AMD64 ABI conventions.
- **Multi-Threading & Synchronization (`pkg/runtime/ps4_threading.c` & `pkg/runtime/ps4_sync.c`)**:
  - Full POSIX and Orbis multi-threading runtime support with dedicated 4 MB stacks per guest thread.
  - Thread lifecycle management (`pthread_create`, `pthread_join`, `pthread_detach`, `pthread_self`, `pthread_equal`).
  - Dynamic TLS key support (`pthread_key_create`, `pthread_setspecific`, `pthread_getspecific`).
  - Synchronization primitives: hash-table mutex map with recursive mutex support, condition variables (`wait`, `timedwait`, `signal`, `broadcast`), reader-writer locks, and thread-safe one-time initialization (`pthread_once`).
- **Virtual Memory Extent Allocator (`pkg/runtime/recomp_runtime.c`)**:
  - Extent-based virtual memory manager for guest `mmap`, `munmap`, and `madvise`.
  - Page-aligned allocations, best-fit search, extent splitting, bidirectional coalescing on free, and physical host page purging via `madvise(MADV_DONTNEED)`.
- **System Call & Kernel ABI (`pkg/runtime/ps4_syscalls.c`)**:
  - Genuine implementations replacing stubs:
    - `fstat`: Full 128-byte FreeBSD 64-bit `struct ps4_stat` translation (`st_dev`, `st_ino`, `st_mode`, `st_nlink`, `st_uid`, `st_gid`, `st_rdev`, timestamps, `st_size`, `st_blocks`, `st_blksize`).
    - `poll`: Direct translation of `struct pollfd` arrays forwarded to host `poll()`.
    - `ioctl`: Kernel request forwarding with proper error propagation.
    - `raise` & `sigprocmask`: Direct POSIX signal raising and 128-bit `sigset_t` translation.
    - Strict `errno` synchronization: All failing syscalls and shims sync `errno` directly to guest TLS (`ctx->fs_base + 0x100`).
    - Unknown syscalls strictly return `-1` with `errno = ENOSYS`.
- **Optimized C Emitter (`pkg/emitter`)**:
  - Emits partitioned C source files for multi-threaded Clang compilation.
  - **Fast-path function entry**: Direct fallthrough on normal function entry (`ctx->rip == addr`), bypassing basic block switches.
  - **Selective unwinding**: Leaf functions skip `UnwindFrame` and `_setjmp` overhead completely.
  - **Optimized flag computation**: Hot integer ALU operations prune auxiliary (`af`) and parity (`pf`) calculations.

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
Basic runtime initializers, stdout writing, and microsecond sleep:

```bash
go run main.go -elf hello_world.elf -out output_hello -compile
perl -e 'alarm 4; exec "./output_hello/ps4_app"'
```

Output:
```text
[ps4-recomp] Initializing runtime...
[ps4-recomp] Loading guest memory image (1146880 bytes), allocated dynamic address space (1090.0 MB)
[ps4-recomp] Executing _start (0xb0b88)...
main: Hello world! Waiting 2 seconds!
main: Done. Infinitely looping...
```

#### B. C++ Exception Handling (`exceptions.elf`)
C++ `try`/`catch`/`throw`, DWARF `.eh_frame` table evaluation, and landing pad dispatch:

```bash
go run main.go -elf exceptions.elf -out output_exc -compile
perl -e 'alarm 4; exec "./output_exc/ps4_app"'
```

Output:
```text
[ps4-recomp] Initializing runtime...
[ps4-recomp] Loading guest memory image (1277952 bytes), allocated dynamic address space (1090.0 MB)
[ps4-recomp] Executing _start (0xcb1c8)...
main: Before testcase...
testcase: Caught! The exception says: Catch me if you can. :p
main: .what() = Another one.
main: Testcase PASS
main: Infinite looping...
```

#### C. Multi-Threading & Synchronization (`tests/threading_test/threading_test.elf`)
Concurrent `std::thread` workers, `std::mutex`, atomic `LOCK XADD` (`fetch_add`), and thread joining:

```bash
go run main.go -elf tests/threading_test/threading_test.elf -out output_thread -compile
perl -e 'alarm 4; exec "./output_thread/ps4_app"'
```

Output:
```text
[ps4-recomp] Initializing runtime...
[ps4-recomp] Loading guest memory image (630784 bytes), allocated dynamic address space (1090.0 MB)
[ps4-recomp] Executing _start (0x5a0f8)...
main: Starting multi-threading test...
main: Spawned worker threads, waiting for join...
Thread 1 started!
Thread 2 started!
Thread 2 finished its 50 iterations.
Thread 1 finished its 50 iterations.
main: Both threads joined. Final counter = 100 (expected 100)
main: Multi-threading test PASS
main: Done. Infinitely looping...
```

#### D. Memory & Computational Stress Test (`tests/memory_stress_test/memory_stress_test.elf`)
1,000,000 prime sieve, 128x128 matrix multiplication (2M ops), QuickSort on 50,000 integers, 5,000 container iterations, and 400MB extent churn & address reuse:

```bash
go run main.go -elf tests/memory_stress_test/memory_stress_test.elf -out output_mem -compile
perl -e 'alarm 5; exec "./output_mem/ps4_app"'
```

Output:
```text
[ps4-recomp] Initializing runtime...
[ps4-recomp] Loading guest memory image (565248 bytes), allocated dynamic address space (1090.0 MB)
[ps4-recomp] Executing _start (0x50848)...
main: Starting Aggressive Memory & Loop Stress Test...
main: Running Sieve of Eratosthenes to 1,000,000...
main: Primes found = 78498 (expected 78498) -> OK
main: Running 128x128 Matrix Multiplication (2M operations)...
main: Matrix total sum = 4194304 (0.0, expected 4194304) -> OK
main: Running QuickSort on 50,000 64-bit integers...
main: QuickSort verification -> OK
main: Running 5,000 dynamic container allocation cycles...
main: Container churn verification -> OK
main: Running 400MB mmap/munmap virtual memory extent churn...
test_mmap_churn: Completed 100 cycles of 4MB (400 MB churn). Address reused 99 times!
main: VM extent reuse verification -> OK

main: All memory and loop stress tests PASSED!
main: Done. Infinitely looping...
```

---

## Environment Configuration

You can customize the guest virtual address space size by setting `PS4_RECOMP_MEM`:

```bash
# Allocate 2 GB of guest virtual address space
PS4_RECOMP_MEM=2G perl -e 'alarm 4; exec "./output_hello/ps4_app"'
```

---

## Project Structure

```text
├── cmd/
│   └── ps4-recomp/        # CLI tool entry point
├── pkg/
│   ├── disasm/            # Disassembly, CFG recovery, jump tables, and flag liveness analysis
│   ├── elfloader/         # ELF segment loader, symbol table parser, and relocation engine
│   ├── emitter/           # Partitioned C emission and parallel Clang build driver
│   ├── lifter/            # Modular x86-64 machine instruction lifter
│   │   ├── alu.go         # Integer ALU, shifts, rotates, mul/div, atomic XADD/CMPXCHG
│   │   ├── control_flow.go# Branching, setcc, cmovcc, exception unwinding detection
│   │   ├── fpu.go         # x87 FPU stack emulation
│   │   ├── simd.go        # SSE/AVX vector moves, packed math, scalar/packed floats
│   │   └── lifter.go      # Core instruction dispatch loop and operand primitives
│   └── runtime/           # Native host runtime and PS4 kernel ABI
│       ├── recomp_runtime.h # Guest context, SIMD unions, extent structs, and prototypes
│       ├── recomp_runtime.c # Virtual memory extent manager, flat address space, runtime init
│       ├── ps4_syscalls.c   # POSIX syscalls, files, memory, and signals
│       ├── ps4_threading.c  # Guest thread lifecycle, stacks, TLS keys
│       └── ps4_sync.c       # Mutexes, condition variables, rwlocks, and pthread_once
├── tests/
│   ├── threading_test/    # Multi-threaded C++ testcase
│   └── memory_stress_test/# Compute and virtual memory stress testcase
├── hello_world.elf        # Testcase 1: Standard I/O, usleep, global initializers
└── exceptions.elf         # Testcase 2: C++ throw/catch, DWARF unwinding, stringstream
```
