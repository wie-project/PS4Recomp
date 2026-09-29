# PS4Recomp

A static ahead-of-time (AOT) recompiler translating PlayStation 4 (x86-64 ELF) binaries into native Apple Silicon (ARM64 macOS) executables without JIT compilation, virtual machines, or MoltenVK overhead.

![PS4Recomp Native Metal Output](images/graphics_test_02.png)

---

## Features

- **Ahead-of-Time C Emitter & Compiler**: Recovers functions, CFG basic blocks, jump tables, and DWARF unwinding tables (`.eh_frame`), translating AMD64 instructions into partitioned C for fast, parallel Apple Clang compilation.
- **Direct Apple Metal Presentation**: Native `MTLPixelFormatBGRA8Unorm` rendering pipeline with hardware VSync via `CAMetalLayer` and PS4 direct video memory (UMA).
- **macOS App Bundling**: Automatically packages recompiled executables into standalone `.app` bundles with resources, icons, and clean window lifecycles (<kbd>Cmd</kbd>+<kbd>Q</kbd> / <kbd>Cmd</kbd>+<kbd>W</kbd>).
- **Orbis Kernel & POSIX Subsystems**:
  - **Memory & UMA**: Direct video memory allocation (`sceKernelAllocateDirectMemory`, `sceKernelMapDirectMemory`), virtual extents (`mmap`, `munmap`, `madvise`).
  - **Threading & Synchronization**: Full Orbis & POSIX threads (`scePthread*`), recursive mutexes, condition variables, reader-writer locks (`scePthreadRwlock*`), semaphores, and event flags.
  - **Event Queues (`sceKernelCreateEqueue`)**: Hardware flip and vblank event integration.
  - **Networking & BSD Sockets**: Sony `libSceNet` (`sceNet*`) and standard BSD socket APIs (`socket`, `bind`, `connect`, `listen`, `accept`, `send`, `recv`, `setsockopt`, `epoll`).
  - **I/O & Syscalls**: VFS path virtualization (`/app0`), 64-bit FreeBSD syscall shims (`fstat`, `unlink`, `poll`, `ioctl`, `getdirentries64`).
- **Media & Hardware Inputs**:
  - `libSceAudioOut`: Low-latency PCM streaming via macOS `AudioToolbox`.
  - `libScePad` & `libSceKeyboard`: Native DualSense/DualShock 4 via `GameController.framework` and keyboard fallback.
  - `libSceMsgDialog`: Native Cocoa modal dialogs with headless fallback.
  - `libSceNpTrophy`: Local trophy lifecycle tracking and notifications.
- **Companion PRX Linking**: Automatically discovers and links companion PRX modules in `sce_module/` and `prx/`, resolving thousands of guest dynamic library imports statically.
- **PS4 PKG Tooling**: Integrated Sony `\x7fCNT` PKG inspector and extractor (`eboot.bin`, PRX modules, PFS files).

---

## Quick Start

### Prerequisites

- macOS on Apple Silicon (ARM64)
- Go 1.22+
- Xcode Command Line Tools (`clang`)
- FreeType 2 (`brew install freetype`)

### Building the Tool

```bash
git clone https://github.com/vladislavkalinkin/PS4Recomp.git
cd PS4Recomp
go build -o ps4-recomp ./cmd/ps4-recomp
```

---

## Usage Guide

### 1. Analyze a Binary

Inspect instruction coverage, reachable functions, unwind data, and HLE imports:

```bash
./ps4-recomp analyze path/to/eboot.bin
```

This scans reachable CFGs, discovers companion PRX modules in adjacent `sce_module/` or `prx/` folders, and reports instruction compatibility alongside host shims, linked PRXs, and unresolved Sony NIDs.

### 2. Recompile an Application

Recompile an ELF/PRX to a native macOS `.app` bundle:

```bash
# Recompile and generate source files only
./ps4-recomp game.elf -o output_dir

# Recompile and build native ARM64 binary (-c)
./ps4-recomp game.elf -o output_dir -c

# Recompile, compile, and execute with a 5-second watchdog (-r -t 5)
./ps4-recomp game.elf -o output_dir -c -r -t 5

# Specify application root directory for /app0 VFS assets and PRXs
./ps4-recomp game.elf --app-dir /path/to/extracted/game -o output_dir -c

# Package standalone macOS .app bundle with all extracted game assets inside Resources/
./ps4-recomp game.elf --app-dir /path/to/extracted/game --copy-resources -o output_dir -c
```

### 3. Inspect and Extract PS4 PKG Packages

Manage retail and homebrew `.pkg` files directly:

```bash
# View package metadata (Title ID, version, category)
./ps4-recomp pkg info game.pkg

# List files inside a PKG container
./ps4-recomp pkg list game.pkg

# Extract eboot.bin, PRXs, and system metadata
./ps4-recomp pkg extract game.pkg -o extracted/

# Extract all game assets and filesystem resources
./ps4-recomp pkg extract game.pkg -o extracted/ --all
```

---

## Commercial Game Milestones

### Hogwarts Legacy (Unreal Engine 4)

```text
[ps4-recomp] Initializing runtime...
[ps4-recomp] Loading guest memory image (312811520 bytes), allocated dynamic address space (524288.0 MB)
[ps4-recomp] Initializing custom memory allocator via SceLibcParam at 0x4188230...
[ps4-recomp] Executing _start (0xf9a50)...
GoodPGO
File root is /app0/!
Used memory before allocating anything was 0.00MB
FIOS2: built with SDK version 0x09508001

FATAL: Unresolved indirect jump/call to 0x96f3948 (from RIP=0x96f3948)
Registers:
  RAX=0x0000000000000000 RBX=0x00000007ffffe2e0 RCX=0x0000000009f4a000 RDX=0x000000000a30f22a
  RSI=0x00000007ffffe1c0 RDI=0x00000007ffffe2e0 RBP=0x00000007ffffe658 RSP=0x00000007ffffe298
  R8 =0x000000000a32d1f8 R9 =0x000000000000000b R10=0x000000000a30bdd0 R11=0x0000000009f4a000
  R12=0x0000001001074cf0 R13=0x0000001000520670 R14=0x0000000000000000 R15=0x0000000008652f10
Guest call stack heuristic (RSP=0x7ffffe298):
  frame #0: 0x00000000041816b5 (RSP+0x0)
  frame #1: 0x0000000009ca6e90 (RSP+0x10)
  frame #2: 0x00000007ffffe2f8 (RSP+0x20)
  frame #3: 0x0000000004187746 (RSP+0x28)
  frame #4: 0x00000007ffffe5f0 (RSP+0x30)
  frame #5: 0x00000007ffffe5e8 (RSP+0x38)
  frame #6: 0x0000000009ca6e90 (RSP+0x50)
  frame #7: 0x00000007ffffe338 (RSP+0x60)
  frame #8: 0x0000000004187746 (RSP+0x68)
  frame #9: 0x00000007ffffe5f0 (RSP+0x70)
  frame #10: 0x0000000009ca6e90 (RSP+0x90)
  frame #11: 0x00000007ffffe378 (RSP+0xa0)
  frame #12: 0x0000000004187797 (RSP+0xa8)
  frame #13: 0x0000000009c66df0 (RSP+0xb0)
  frame #14: 0x00000007ffffe5e8 (RSP+0xb8)
  frame #15: 0x0000000009ca6e90 (RSP+0xd0)
  frame #16: 0x00000007ffffe3b8 (RSP+0xe0)
  frame #17: 0x0000000004187746 (RSP+0xe8)
  frame #18: 0x00000007ffffe5f0 (RSP+0xf0)
  frame #19: 0x00000007ffffe5e8 (RSP+0xf8)
  frame #20: 0x0000000009ca6e90 (RSP+0x110)
  frame #21: 0x00000007ffffe3f8 (RSP+0x120)
  frame #22: 0x0000000004187746 (RSP+0x128)
  frame #23: 0x00000007ffffe5f0 (RSP+0x130)
  frame #24: 0x00000007ffffe5e8 (RSP+0x138)
  frame #25: 0x0000000009ca6e90 (RSP+0x150)
  frame #26: 0x00000007ffffe438 (RSP+0x160)
  frame #27: 0x0000000004187746 (RSP+0x168)
  frame #28: 0x00000007ffffe5f0 (RSP+0x170)
  frame #29: 0x00000007ffffe5e8 (RSP+0x178)
  frame #30: 0x0000000009ca6e90 (RSP+0x190)
  frame #31: 0x00000007ffffe478 (RSP+0x1a0)
  frame #32: 0x0000000004187746 (RSP+0x1a8)
  frame #33: 0x00000007ffffe5f0 (RSP+0x1b0)
  frame #34: 0x00000007ffffe5e8 (RSP+0x1b8)
  frame #35: 0x0000000009ca6e90 (RSP+0x1d0)
  frame #36: 0x00000007ffffe4b8 (RSP+0x1e0)
  frame #37: 0x0000000004187746 (RSP+0x1e8)
  frame #38: 0x00000007ffffe5f0 (RSP+0x1f0)
  frame #39: 0x00000007ffffe5e8 (RSP+0x1f8)
```

---

## Project Structure

```text
├── cmd/ps4-recomp/          # CLI command-line entry point
├── pkg/
│   ├── analyzer/            # Static instruction coverage & CFG analyzer
│   ├── cli/                 # CLI driver, workflow orchestration, .app packager
│   ├── disasm/              # Disassembly, jump tables, dead flag elimination (DFE)
│   ├── elfloader/           # ELF64 / PRX loader, relocations, companion discovery
│   ├── emitter/             # Partitioned C emitter, HLE report, Clang driver
│   ├── lifter/              # AMD64 to C lifter (ALU, SIMD, AVX/VEX, FPU)
│   ├── ps4pkg/              # Sony PKG container & SFO metadata parser
│   └── runtime/             # Native macOS host runtime & kernel ABI
│       ├── core/            # Dynamic dispatch, guest context, register state
│       ├── kernel/          # Syscalls, UMA direct memory, threads, equeues, VFS
│       └── modules/         # libSceVideoOut, AudioOut, Net, Pad, Dialog, Trophy
└── tests/                   # Regression and integration test suites
```

---

## Acknowledgements & Credits

We would like to express our deep gratitude and appreciation to the open-source PlayStation emulation and homebrew research communities whose groundbreaking work and documentation made this static recompiler possible:

- **[OpenOrbis](https://github.com/OpenOrbis)**: For pioneering the open-source PS4 toolchain, header definitions, and system service reverse engineering.
- **[shadPS4](https://github.com/shadps4-emu/shadPS4)**: For their exceptional open-source PS4 emulator codebase, precise structure layouts, HLE module architectures, and invaluable documentation of Orbis system behaviors.
- **The PlayStation NID Database Contributors**: For compiling and maintaining comprehensive symbol dictionaries and NID-to-name mappings across PlayStation 4 system libraries.

---

## License

GPL-2.0. See [LICENSE](LICENSE) for details.
