# PS4Recomp

A static ahead-of-time (AOT) recompiler that translates PlayStation 4 (x86-64 ELF) binaries into native Apple Silicon (ARM64 macOS) executables without JIT compilation, virtual machines, or MoltenVK overhead.

![PS4Recomp Native Metal Output](images/graphics_test_02.png)

```text
===================================================================
  PS4Recomp: PlayStation 4 x86-64 to Native ARM64 AOT Recompiler
===================================================================
[ps4-recomp] [1/4] Loading ELF: tools/OpenOrbis/PS4Toolchain/samples/graphics/graphics/x64/Debug/graphics.elf
             Entry point: 0xcb238 | Segments: 4 | Relocations: 3057 | Time: 6ms
[ps4-recomp] [2/4] Analyzing CFG and discovering reachable code...
             Discovered 5264 functions, 29181 basic blocks, 207565 instructions | Time: 140ms
[ps4-recomp] [3/4] Emitting C source files to 'output_graphics/'...
             Emitted 32 C source files | Time: 332ms
[ps4-recomp] [4/4] Compiling native ARM64 binary with clang (-O2, 8 workers)...
             Compiled binary: output_graphics/ps4_app | Time: 33.72s
[ps4-recomp] All tasks completed successfully in 34.198s
===================================================================
[ps4-recomp] Running output_graphics/ps4_app (watchdog timeout: 4s)...
[ps4-recomp] Initializing runtime...
[ps4-recomp] Loading guest memory image (1277952 bytes), allocated dynamic address space (1090.0 MB)
[ps4-recomp] Executing _start (0xcb238)...
main: Creating a scene
main: Entering draw loop...
[videoout] Presenting frame 0 (buffer 0, 1920x1080)
[videoout] Presenting frame 1 (buffer 1, 1920x1080)
[videoout] Presenting frame 2 (buffer 0, 1920x1080)
[ps4-recomp] Watchdog timeout of 4s reached. Terminated cleanly.
```

---

## Overview

The recompiler statically parses guest PlayStation 4 x86-64 ELF binaries, recovers complete control flow graphs (CFGs) and function boundaries, lifts AMD64/SIMD/AVX instructions into clean C code, and compiles the result into a native Mach-O executable using Apple Clang.

The graphics and kernel subsystems are architected natively from first principles: direct Apple Silicon Metal rendering with zero Vulkan/MoltenVK translation layers, unified memory architecture (UMA) emulation for PS4 direct video memory, and hardware VSync synchronization via native event queues.

### Key Architectural Components

- **Native macOS `.app` Bundling & Clean GUI Lifecycle (`pkg/cli/cli.go` & `pkg/runtime/ps4_metal_screen.m`)**:
  - Automatically packages compiled binaries into self-contained macOS Application bundles (`<AppName>.app`).
  - Embeds full resources (`Contents/Resources/assets`, `guest_image.bin`, `sce_sys`), auto-generates `AppIcon.icns`, and produces `Info.plist`.
  - Seamless standalone execution via Finder or `open <app>.app`. Clean, immediate termination via <kbd>Cmd</kbd>+<kbd>Q</kbd>, <kbd>Cmd</kbd>+<kbd>W</kbd>, <kbd>Esc</kbd>, or the window close button with zero lingering background processes.
- **Direct Apple Silicon Metal Presentation (`pkg/runtime/ps4_metal_screen.m`)**:
  - High-performance native macOS windowing (`NSWindow`) and display layer (`CAMetalLayer`).
  - Native `MTLPixelFormatBGRA8Unorm` pipeline matching PlayStation 4 32-bit framebuffer memory layout with zero color distortion.
  - Zero-copy pixel presentation pipeline using `MTLCommandQueue` and blit encoding.
- **Guest VFS Path Virtualization Layer (`pkg/runtime/ps4_vfs.c`)**:
  - Transparently maps PS4 guest file paths (`/app0/...`, `/data/...`) to bundled application resources.
  - Supports `--app-dir` CLI parameter, `PS4_APP_DIR` environment variable, or automatic executable-relative resolution.
- **Subsystem Lifecycle Teardowns & Zero-Leak Memory Management**:
  - Strict resource tracking and teardowns for Direct Memory, Event Queues, VideoOut handles, Synchronization Mutex maps, and Metal display layers.
  - Guest thread stack extent reclamation (`recomp_free_thread_context`) on thread termination and join.
  - Full AddressSanitizer and LeakSanitizer (`--asan`) verification.
- **Direct Video Memory Manager (`pkg/runtime/ps4_direct_mem.c`)**:
  - Simulates the PS4 Unified Memory Architecture (UMA) direct physical memory space.
  - Implements physical block allocation (`sceKernelAllocateDirectMemory`, `sceKernelGetDirectMemorySize`, `sceKernelReleaseDirectMemory`).
  - Page-aligned guest virtual mapping (`sceKernelMapDirectMemory`) backed by the recompiler's unified address space.
- **Display Output & VideoOut Subsystem (`pkg/runtime/ps4_videoout.c`)**:
  - Full `libSceVideoOut.so` implementation (`sceVideoOutOpen`, `sceVideoOutSetBufferAttribute`, `sceVideoOutRegisterBuffers`, `sceVideoOutSetFlipRate`, `sceVideoOutAddFlipEvent`, `sceVideoOutSubmitFlip`, `sceVideoOutGetFlipStatus`).
  - Double and multi-buffering with hardware-synchronized flip events.
- **Event Queue Subsystem (`pkg/runtime/ps4_equeue.c`)**:
  - Native kernel event queue mechanism (`sceKernelCreateEqueue`, `sceKernelDeleteEqueue`, `sceKernelWaitEqueue`).
  - Condition-variable event signaling with support for `ORBIS_KERNEL_EVFILT_VIDEO_OUT` and timed waits.
- **ELF Loader (`pkg/elfloader`)**: Loads ELF segments into a contiguous guest memory image, extracts static and dynamic symbols, parses `.init_array`, and processes ELF64 relocations (`.rela.dyn` and `.rela.plt`).
- **Disassembler & CFG (`pkg/disasm`)**: Recovers functions, basic blocks, indirect jump tables from `.rodata`, and performs backward flag liveness analysis for dead flag elimination (DFE).
- **Instruction Lifter (`pkg/lifter`)**: Modular instruction lifter translating x86-64 instructions into clean C:
  - **100% instruction coverage verified across all 17 OpenOrbis SDK samples** (>500,000 instructions):
  - `alu.go`: Integer arithmetic, logic, bit manipulation (`BSWAP`, `BT`, `BTS`, `BTR`, `BTC`, `POPCNT`, `LZCNT`, `TZCNT`, `SHLD`, `SHRD`), CPU feature discovery (`CPUID`, `XGETBV`), and atomic hardware synchronization (`LOCK XADD`, `CMPXCHG`).
  - `simd.go`: Packed SIMD arithmetic (`PSUB*`, `PMULLW`, `PMULHW`, `PMADDWD`), packing/unpacking (`PACK*`, `PUNPCK*`), element insertion/shuffling (`PINSR*`, `PSHUF*`), vector shifts (`PSLL*`, `PSRL*`, `PSRA*`), scalar/packed floats (`ADDSD`, `MULSD`, `COMISD`, `COMISS`, `MINSD`, `MAXSD`, `SQRTSS`, `SQRTSD`), and comprehensive 2/3/4-operand **AVX/VEX** instruction support.
  - `fpu.go`: IEEE 754 80-bit x87 FPU stack emulation with hardware status/control words.
  - `control_flow.go`: Branch prediction hints, condition code evaluation, and DWARF exception recovery.
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
  - Emits partitioned C source files for multi-threaded Clang compilation with instruction budget balancing (~15,000 instructions per chunk) to eliminate single-core compilation bottlenecks.
  - **Fast-path function entry**: Direct fallthrough on normal function entry (`ctx->rip == addr`), bypassing basic block switches.
  - **Selective unwinding**: Leaf functions skip `UnwindFrame` and `_setjmp` overhead completely.
  - **Optimized flag computation**: Hot integer ALU operations prune auxiliary (`af`) and parity (`pf`) calculations.
- **Audio Subsystem (`libSceAudioOut` in `pkg/runtime/ps4_audioout.c`)**:
  - Native macOS `AudioToolbox` / `AudioQueue` streaming architecture for Linear PCM audio output (`sceAudioOutOpen`, `sceAudioOutOutput`, `sceAudioOutClose`).
  - Sample-accurate timing and pacing synchronized with hardware audio frames via `mach_absolute_time()`, eliminating buffer underrun/overrun.
- **Gamepad & Input Subsystem (`libScePad` in `pkg/runtime/ps4_pad.m`)**:
  - Native `GameController.framework` support for physical controllers (DualShock 4, DualSense, Xbox, Switch Pro, MFi).
  - Comprehensive ergonomic keyboard mapping for all controller components: Left/Right analog sticks, digital D-pad, face buttons (Cross, Circle, Square, Triangle), analog triggers (L2/R2), bumpers (L1/R1), stick clicks (L3/R3), touchpad click, and Options/Restart button (<kbd>R</kbd>).
- **Keyboard Subsystem (`libSceKeyboard` in `pkg/runtime/ps4_keyboard.m`)**:
  - Full `libSceKeyboard` implementation (`sceKeyboardInit`, `sceKeyboardOpen`, `sceKeyboardReadState`, `sceKeyboardGetKey2Char`, `sceKeyboardGetHandle`).
  - Hardware USB HID keycode translation and modifier state handling for PS4 guest applications.
- **Bit Manipulation Instruction Sets (BMI1 / BMI2 in `pkg/lifter/bmi.go`)**:
  - Full support for x86-64 BMI1 and BMI2 extensions: `BEXTR`, `BZHI`, `ANDN`, `BLSR`, `BLSMSK`, `BLSI`, `SHLX`, `SHRX`, `SARX`, `RORX`, `MULX`.
  - Vendored and extended disassembler (`vendor/golang.org/x/arch/x86/x86asm/bmi.go`) ensuring out-of-the-box decoding of modern compiler output.
- **System Dialog Subsystem (`libSceCommonDialog` & `libSceMsgDialog` in `pkg/runtime/ps4_dialog.m`)**:
  - Native Cocoa `NSAlert` modal presentation for PS4 system and user dialogs (`OK`, `Yes/No`, `OK/Cancel`, and custom dual-button choices).
  - Synchronous and asynchronous status polling (`sceMsgDialogUpdateStatus`, `sceMsgDialogGetResult`), with automatic headless fallback for headless/CI environments.

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

### 2. Recompilation and Execution

The CLI compiles PlayStation 4 ELF binaries into native macOS `.app` bundles, optionally executing them with a watchdog timer (`-r -t <seconds>`):

#### A. Interactive 2D Metal Graphics (`graphics.elf`)
Renders a high-resolution Mandelbrot fractal directly onto a native Metal window using PS4 direct video memory and flip event synchronization:

```bash
go run . tools/OpenOrbis/PS4Toolchain/samples/graphics/graphics/x64/Debug/graphics.elf -o output_graphics -r -t 4
```

#### B. PNG Texture Decoding & VFS Asset Loading (`pngdec.elf`)
Loads `/app0/assets/images/logo.png` from the `.app` bundle via VFS, decodes compressed PNG textures, and presents frames to Metal:

```bash
go run . tools/OpenOrbis/PS4Toolchain/samples/pngdec/pngdec/x64/Debug/pngdec.elf -o output_pngdec -r -t 4
```

#### C. Interactive Controller & Gamepad Input (`input.elf`)
Renders the DualShock 4 controller interface on Metal, receiving live button, stick, and trigger inputs from physical controllers via `GameController.framework` (DualShock 4, DualSense, Xbox, Switch Pro, MFi) or keyboard fallback:

```bash
go run . tools/OpenOrbis/PS4Toolchain/samples/input/input/x64/Debug/input.elf --app-dir tools/OpenOrbis/PS4Toolchain/samples/input -o output_input -r -t 4
```

#### D. Vector Font Rasterization & Text Layout (`font.elf`)
Loads `/app0/assets/fonts/Gontserrat-Regular.ttf` via VFS, rasterizes TrueType vector fonts at multiple sizes with FreeType 2, and renders multi-line antialiased text to Metal:

```bash
go run . tools/OpenOrbis/PS4Toolchain/samples/font/font/x64/Debug/font.elf --app-dir tools/OpenOrbis/PS4Toolchain/samples/font -o output_font -r -t 4
```

#### E. Standalone App Launching & Window Controls
Recompiled applications are packaged as standalone `.app` bundles that can be launched directly:

```bash
# Launch the native macOS app bundle directly
open output_pngdec/pngdec.app

# Or execute the binary directly from a terminal
./output_pngdec/pngdec.app/Contents/MacOS/pngdec
```

- **Quit Application**: Press <kbd>Cmd</kbd> + <kbd>Q</kbd> or select **Quit** from the application menu.
- **Close Window**: Press <kbd>Cmd</kbd> + <kbd>W</kbd>, <kbd>Esc</kbd>, or click the red close button <kbd>⨉</kbd>. The app terminates cleanly with zero lingering background processes.

#### D. Core Verification Tests
- **Multi-Threading & Atomicity (`threading_test.elf`)**:
  ```bash
  go run . tests/threading_test/threading_test.elf -o output_thread --asan -r -t 3
  # Result: Multi-worker std::thread join, atomic LOCK XADD, 0 ASan/LSan leaks.
  ```
- **C++ Exceptions & DWARF Unwinding (`exceptions.elf`)**:
  ```bash
  go run . exceptions.elf -o output_exc -r -t 3
  # Result: try/catch/throw, .eh_frame table evaluation, and landing pad dispatch.
  ```
- **Memory & Compute Stress Test (`memory_stress_test.elf`)**:
  ```bash
  go run . tests/memory_stress_test/memory_stress_test.elf -o output_mem -r -t 5
  # Result: 1M prime sieve, 128x128 matrix mult, 400MB mmap extent churn.
  ```

---

## Verified OpenOrbis Sample Coverage

Automated test suite (`pkg/lifter/coverage_test.go`) validates **100.0% opcode coverage** across all built samples:

| Sample Binary | Instructions | Functions | Status |
| :--- | :--- | :--- | :--- |
| `graphics.elf` | 19,348 | 662 | **100.0%** (Verified Live on Metal) |
| `input.elf` | 42,373 | 862 | **100.0%** (Verified Live on Metal & GameController) |
| `SDL2.elf` | 242,993 | 1,728 | **100.0%** (Verified Live: Game, Sound, Keyboard/Pad) |
| `pngdec.elf` | 41,322 | 839 | **100.0%** (Verified Live on Metal) |
| `threading.elf` | 22,036 | 818 | **100.0%** (Verified Live: std::thread & mutex) |
| `keyboard.elf` | 21,103 | 686 | **100.0%** (Verified Live on Metal & libSceKeyboard) |
| `font.elf` | 19,727 | 665 | **100.0%** (Verified Live on Metal & FreeType 2) |
| `audio-wav.elf` | 10,590 | 118 | **100.0%** (Verified Live: AudioQueue PCM 48kHz) |
| `system.elf` | 20,011 | 674 | **100.0%** |
| `networking.elf` | 18,913 | 653 | **100.0%** |
| `exceptions.elf` | 18,699 | 650 | **100.0%** (Verified Live: C++ throw/catch & unwinding) |
| `hello_world.elf` | 18,423 | 644 | **100.0%** (Verified Live: stdout & libc) |
| `piglet.elf` | 18,537 | 644 | **100.0%** |
| `net_http.elf` | 5,396 | 69 | **100.0%** |
| `trophies.elf` | 3,516 | 41 | **100.0%** |
| `dialogs.elf` | 3,422 | 42 | **100.0%** (Verified Live: Cocoa NSAlert & libSceMsgDialog) |
| `using_library.elf` | 3,352 | 40 | **100.0%** |

---

## Environment Configuration

You can customize the guest virtual address space size by setting `PS4_RECOMP_MEM`:

```bash
# Allocate 2 GB of guest virtual address space
PS4_RECOMP_MEM=2G perl -e 'alarm 4; exec "./output_graphics/ps4_app"'
```

---

## Project Structure

```text
├── cmd/
│   └── ps4-recomp/          # CLI tool entry point
├── images/
│   ├── graphics_test_01.png # Initial graphics render
│   └── graphics_test_02.png # Authentic blue Metal render
├── pkg/
│   ├── cli/                 # CLI driver, macOS .app packager, watchdog orchestration
│   ├── disasm/              # Disassembly, CFG recovery, indirect jump tables, flag liveness
│   ├── elfloader/           # ELF64 segment loader, symbol table parser, relocation engine
│   ├── emitter/             # Partitioned C emission, shims, parallel Clang compiler driver
│   ├── lifter/              # Modular AMD64 instruction lifter
│   │   ├── alu.go           # Integer ALU, bit operations, CPUID, atomic synchronization
│   │   ├── control_flow.go  # Branching, setcc, cmovcc, exception unwinding frames
│   │   ├── fpu.go           # IEEE 754 80-bit x87 FPU stack emulation
│   │   ├── simd.go          # Packed SIMD, vector arithmetic, AVX/VEX instructions
│   │   └── lifter.go        # Instruction decoder and opcode registry
│   └── runtime/             # Native host runtime and PS4 kernel ABI
│       ├── recomp_runtime.h # Guest context, SIMD unions, extent tracking
│       ├── recomp_runtime.c # Virtual memory extent manager, flat address space
│       ├── ps4_metal_screen.h/m # Native Apple Metal CAMetalLayer & Cocoa window renderer
│       ├── ps4_direct_mem.h/c   # PS4 direct physical memory allocation & UMA mapping
│       ├── ps4_videoout.h/c     # libSceVideoOut buffer registration & flip events
│       ├── ps4_equeue.h/c       # Kernel event queue mechanism
│       ├── ps4_vfs.h/c          # Guest VFS path virtualization (/app0/ resolution)
│       ├── ps4_syscalls.c       # FreeBSD/PS4 syscall shims, signals, errno sync
│       ├── ps4_threading.c      # Guest thread lifecycle, stacks, TLS keys
│       └── ps4_sync.c           # Mutexes, condition variables, rwlocks, pthread_once
├── tests/
│   ├── threading_test/      # Multi-threaded C++ testcase
│   └── memory_stress_test/  # Compute and virtual memory stress testcase
├── hello_world.elf          # Testcase 1: Standard I/O, usleep, global initializers
└── exceptions.elf           # Testcase 2: C++ throw/catch, DWARF unwinding, stringstream
```
