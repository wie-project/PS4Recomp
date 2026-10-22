# PS4Recomp

A macOS Apple Silicon ahead-of-time (AOT) recompiler that translates PlayStation 4 (x86-64 ELF) binaries into native executables. No JIT, virtual machines, or MoltenVK required.

![First Silksong screen](images/silk_test_01.png)
---

## Features

- **Native ARM64 Assembly AOT Compiler**: Recovers functions, CFG, and jump tables, translating AMD64 instructions directly into native ARM64 assembly (`.s`) chunks. Compiles via Apple Clang assembler in parallel without intermediate C translation overhead.
- **x86 Memory Ordering (TSO)**: Supports x86 Total Store Order via acquire/release semantics (`-tso`) on non-stack guest memory, preventing subtle multi-threaded race conditions in game engines and lock-free data structures.
- **Darwin ABI & Register Safety**: Pinned guest register architecture (`x28` memory base, `x27` TLS/FS base) while strictly respecting Darwin ARM64 calling conventions: `x18` (Apple platform-reserved) is untouched, `d8`-`d15` callee-saved registers are preserved, and stack alignment is verified.
- **Universal Multi-Module & Plugin Linking**: Recursively discovers and binds all companion `.prx` modules and game plugins throughout the application directory structure. Automatically relocates virtual memory spaces with zero collisions and merges symbols into a unified guest address space.
- **FreeBSD / Orbis OS Conforming Synchronization**: True guest-memory allocated synchronization primitives (`pthread_mutex_t`, `pthread_cond_t`, `pthread_rwlock_t`) supporting internal libc field accesses (such as Unity's `m_flags`), adaptive spinlocks, and fiber-aware thread ownership.
- **Native Metal Graphics & VideoOut**: Direct Apple Metal presentation with frame pacing (`ps4_metal_screen`, `ps4_present_queue`), hardware VSync, gamma correction, and zero-copy UMA physical direct memory aliasing (`mach_vm_remap`).
- **Orbis Kernel & POSIX Subsystems**: High-level emulation (HLE) of direct physical memory, event queues (`libkernel` equeues with condition broadcasting), asynchronous I/O (`aio`), and virtual file system virtualization (`/app0`).
- **Rich Media & Hardware Integration**: Multi-channel 48 kHz PCM audio mixer paced by device clock (`libSceAudioOut`), hardware AAC decoding via AudioToolbox (`libSceAudiodec`), native GameController gamepad input with hotkeys (`libScePad`), hardware ImageIO decoding (`libScePngDec`, `libSceJpegDec`), directory-based save data (`libSceSaveData`), and RFC-compliant datetime (`libSceRtc`).
- **Tooling & Standalone Bundling**: Automatic generation of self-contained macOS `.app` application bundles, resource staging, and ad-hoc code-signing.

---

## Quick Start

### Prerequisites

- macOS (Apple Silicon: M1 / M2 / M3 / M4)
- Go 1.22+
- Xcode Command Line Tools (`clang`, `clang++`)
- FreeType 2 (`brew install freetype`)
- Ninja (`brew install ninja`, recommended for fast parallel builds)

### Prerequisites for Commercial Games

PS4Recomp operates **exclusively on already decrypted and extracted game folders** containing the game's executable (`eboot.bin`) and assets. It does not parse or decrypt proprietary encrypted package containers directly.

To dump and extract your legally purchased game backups on macOS, we officially recommend using the native, open-source community tool **[ps4-pkg-tools](https://github.com/xXJSONDeruloXx/ps4-pkg-tools)** (supports both CLI and GUI interfaces).

### Cloning & 3rd-Party Dependencies

PS4Recomp relies on several 3rd-party submodules located in `3rdparty/`:

| Submodule                                | Purpose                                                                            | Integration / Output                                  |
| ---------------------------------------- | ---------------------------------------------------------------------------------- | ----------------------------------------------------- |
| **SPIRV-Cross** (`3rdparty/spirv-cross`) | Translates SPIR-V GPU shaders to Apple Metal Shading Language (MSL)                | Pre-built static library (`libspirv-cross.a`)         |
| **LibAtrac9** (`3rdparty/libatrac9`)     | ATRAC9 hardware/software audio decoder for PS4 AJM (`libSceAjm`)                   | Pre-built static library (`libatrac9.a`) or C sources |
| **minimp3** (`3rdparty/minimp3`)         | Lightweight MP3 audio decoding header for sound playback                           | Header-only (`minimp3.h`), auto-included              |
| **ffmpeg-core** (`3rdparty/ffmpeg-core`) | Multimedia decoding backend (H.264/AVC & HEVC) for video playback (`libSceAvcdec`) | Headers & libraries                                   |

```bash
# Clone repository with all submodules:
git clone --recursive https://github.com/wie-project/PS4Recomp.git
cd PS4Recomp

# Or if already cloned without --recursive:
git submodule update --init --recursive
```

### Building 3rd-Party Static Libraries & PS4Recomp

Before compiling games, build the static libraries for the 3rd-party dependencies:

#### 1. Build SPIRV-Cross

```bash
cd 3rdparty/spirv-cross
clang++ -O3 -std=c++17 -c spirv_cross.cpp spirv_cross_parsed_ir.cpp spirv_parser.cpp spirv_glsl.cpp spirv_msl.cpp spirv_cfg.cpp spirv_cross_c.cpp
ar rcs libspirv-cross.a *.o && rm -f *.o
cd ../..
```

#### 2. Build LibAtrac9

```bash
cd 3rdparty/libatrac9
clang -O3 -c C/src/*.c
ar rcs libatrac9.a *.o && rm -f *.o
cd ../..
```

#### 3. Build ps4-recomp binary

```bash
go build -o ps4-recomp ./cmd/ps4-recomp
```

---

## Usage Guide

### 1. Analyze a Binary

Scan CFGs, discover companion PRXs recursively, and report instruction compatibility for the decrypted executable:

```bash
./ps4-recomp analyze path/to/extracted_game/eboot.bin
```

### 2. Recompile to macOS `.app` Bundle

Recompile the extracted Orbis binary and package it into a native standalone application bundle:

```bash
# Basic compilation to native ARM64 binary with full TSO memory ordering
./ps4-recomp path/to/extracted_game/eboot.bin -o output_dir -c

# Full standalone package with VFS assets and resources included into macOS app bundle
./ps4-recomp path/to/extracted_game/eboot.bin --app-dir /path/to/extracted_game --copy-resources -o output_dir -c
```

### 3. CLI Options

| Flag                    | Description                                                          | Default       |
| ----------------------- | -------------------------------------------------------------------- | ------------- |
| `-o`, `-out <dir>`      | Output directory for generated code and binaries                     | `"build"`     |
| `-e`, `-elf <file>`     | Path to input PS4 ELF binary                                         | —             |
| `-c`, `-compile`        | Compile emitted assembly to native ARM64 binary with Clang           | `true`        |
| `-j`, `-jobs <num>`     | Number of parallel Clang assembler workers                           | `NumCPU`      |
| `-O`, `-opt <level>`    | Clang optimization level (`0`, `1`, `2`, `3`, `s`, `z`)              | `"2"`         |
| `-r`, `-run`            | Run the compiled binary after successful recompilation               | `false`       |
| `-t`, `-timeout <sec>`  | Watchdog timeout in seconds when running binary (`0` = no timeout)   | `0`           |
| `-a`, `-all-symbols`    | Recompile all symbols in symbol table even if unreachable            | `false`       |
| `-k`, `-chunk-size <n>` | Number of functions per emitted ARM64 assembly chunk file            | `250`         |
| `-v`, `-verbose`        | Enable verbose output and detailed timing breakdown                  | `false`       |
| `--tso`                 | x86 Total Store Order memory model (acquire/release on memory)       | `true`        |
| `--app-dir <dir>`       | Host directory mapping to `/app0` for game assets                    | Auto-detected |
| `--copy-resources`      | Copy all game assets/resources into macOS `.app` bundle `Resources/` | `false`       |
| `--asan`                | Compile runtime with AddressSanitizer and LeakSanitizer              | `false`       |

---

## Commercial Game Milestones

### Hollow Knight: Silksong (Unity 6 / Il2Cpp)

Silksong boots into the native Unity 6 engine on Apple Silicon. Over **341,000 reachable functions** and **19.3 million instructions** across `eboot.bin` and 11 companion PRXs and plugins (`Il2cppUserAssemblies.prx`, `HollowKnightPS4Plugin.prx`, `UnityNpToolkit2.prx`, `SaveData.prx`, `CommonDialog.prx`, `PSN.prx`, `lib_burst_generated.prx`, `PS4Util.prx`, `libSceFios2.prx`, `libSceJobManager.prx`, `libc.prx`) are recompiled directly into native ARM64 assembly.

The game successfully maps 2.5 GB direct physical memory, initializes FMOD audio, mounts VFS assets, and runs Unity engine subsystems including `PlayerPrefs` and concurrent workers:

```text
[ps4-thread] Thread created: id=1051, name='PlayerPrefs', entry=0xb2d420, arg=0x2a236108
[ps4-thread] Thread started: id=1051, name='PlayerPrefs', entry=0xb2d420, arg=0x2a236108
[ps4-mem] sceKernelReserveVirtualRange: in=0xd7240000 len=65536 (0.06 MB) flags=0x10 align=0
[ps4-vm] recomp_vm_alloc_fixed (reserved): desired=0xd7240000 size=65536 (0.06 MB)
[ps4-mem] sceKernelReserveVirtualRange SUCCESS: out=0xd7240000
[ps4-mem] sceKernelCheckedReleaseDirectMemory: phys=0x0 len=65536
[ps4-thread] Thread exited: id=1051, name='PlayerPrefs', ret=0x0
[ps4-recomp] sceKernelLoadStartModule: /app0/Media/Plugins/UnityNpToolkit2.prx
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnitySetGraphicsDevice' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityRenderEvent' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityGetAudioEffectDefinitions' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityPluginLoad' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityPluginUnload' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityRenderingExtEvent' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityRenderingExtQuery' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityShaderCompilerExtEvent' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnitySetEventQueue' not found
[ps4-recomp] sceKernelLoadStartModule: /app0/Media/Plugins/UnityNpToolkit2.prx
[ps4-recomp] WARN: Called unimplemented function NID 'vbh-MWtjMh4' (Lib: libSceVrTracker) at RIP=0x8ad0a68 (caller RIP=0x8ada789, RSP=0x7ffff9308)
Registers:
  RAX=0x0000000010000000 RBX=0x00000007ffff9b88 RCX=0x00000007ffff9b90 RDX=0x0000000008adaed0
  RSI=0x00000007ffff9b88 RDI=0x00000007ffff9320 RBP=0x00000007ffff93f0 RSP=0x00000007ffff9308
  R8 =0x00000000000001f1 R9 =0x0000000000000040 R10=0x0000000019c00000 R11=0x0000000000000000
  R12=0x0000000008adaed0 R13=0x00000000023e7000 R14=0x00000007ffff9b90 R15=0x00000007ffff9420
...
```

### Hogwarts Legacy (Unreal Engine 4)

Unreal Engine 4 boots past early engine initialization, expands virtual memory pool blocks, configures flexible memory sizes, and parses game packages:

```text
[ps4-recomp] Initializing runtime...
[ps4-recomp] Loading guest memory image (312811520 bytes), allocated dynamic address space (524288.0 MB)
[ps4-recomp] Initializing custom memory allocator via SceLibcParam at 0x4188230...
[ps4-mem] sceKernelAllocateMainDirectMemory: len=67108864 (64.00 MB) align=65536 type=3
[ps4-mem] sceKernelAllocateDirectMemory: start=0x0 end=0xffffffffffffffff len=67108864 (64.00 MB) align=65536 type=3
[ps4-mem] sceKernelMapDirectMemory: in=0x4000000000 len=67108864 (64.00 MB) prot=0x32 flags=0x90 phys=0x0 align=0
[ps4-vm] recomp_vm_alloc_fixed (direct_mem): desired=0x4000000000 size=67108864 (64.00 MB)
[ps4-mem] sceKernelMapDirectMemory SUCCESS: out=0x4000000000
[ps4-mem] sceKernelMemoryPoolExpand: start=0x4000000 end=0x800000000 len=34292563968 (32703.94 MB) align=0
[ps4-mem] sceKernelAllocateDirectMemory: start=0x4000000 end=0x800000000 len=34292563968 (32703.94 MB) align=65536 type=3
[ps4-mem] sceKernelMemoryPoolReserve: in=0x1000000000 len=8589934592 (8192.00 MB) align=0 flags=0x90
[ps4-vm] recomp_vm_alloc_fixed (pool_reserved): desired=0x1000000000 size=8589934592 (8192.00 MB)
[ps4-mem] sceKernelMemoryPoolReserve SUCCESS: out=0x1000000000
[ps4-recomp] Executing _start (0xf9a50)...
GoodPGO
File root is /app0/!
[ps4-mem] sceKernelConfiguredFlexibleMemorySize: 4294967296 (4096.00 MB)
FIOS2: built with SDK version 0x09508001
...
```

---

## Project Structure

```text
├── cmd/
│   ├── ps4-recomp/              # Main CLI entry point
│   └── lifter-difftest-asm/     # AMD64 vs ARM64 assembly differential testing harness
└── pkg/
    ├── analyzer/                # Reachable CFG & instruction coverage analyzer
    ├── disasm/                  # Instruction decoder, jump-table & carry-liveness recovery
    ├── elfloader/               # ELF64/SELF/PRX parser, recursive companion module discovery
    ├── lifter/                  # AMD64 to ARM64 assembly lifter (TSO, locked RMW, FPU, SIMD)
    ├── emitter/                 # Assembly partitioner, bridge trampoline, shims & Ninja driver
    └── runtime/                 # macOS host runtime & Orbis/POSIX kernel ABI
        ├── core/                # Guest context, memory manager & Mach-O crash handlers
        ├── kernel/              # Direct memory aliasing, sync pools, threading, equeue, VFS
        └── modules/             # HLE modules (VideoOut, AudioOut, Audiodec, Pad, SaveData, RTC, etc.)
```

---

## Contributing Guidelines

- **English only**: All code comments, documentation, and commit messages must be written in English.
- **Strict verification**: All modifications must pass `go test -count=1 ./...` and `go vet ./...` cleanly with zero regressions. No artificial stubs returning 0 where honest implementation is required.
- **Multi-platform support**: macOS Apple Silicon remains the primary target architecture. Contributions for other platforms are welcome as long as they don't break or slow down macOS support.

---

## Credits & Acknowledgements

Special thanks to the open-source PlayStation emulation and research communities:

- **[ps4-native-mac](https://github.com/manjarqo/ps4-native-mac)** — The first fork of the project that improved the work of the original
- **[OpenOrbis](https://github.com/OpenOrbis)** — Open-source PS4 toolchain and reverse engineering.
- **[shadPS4](https://github.com/shadps4-emu/shadPS4)** — HLE module architecture and system behavior documentation.
- **[ps4libdoc](https://github.com/idc/ps4libdoc)** — Symbol dictionaries and NID-to-name mappings.

---

## Legal Notice

PS4Recomp is an independent educational and research project into ahead-of-time binary translation. It does not distribute, bundle, or contain any copyrighted PlayStation 4 system libraries, firmware files, or proprietary encryption keys. "PlayStation", "PlayStation 4", "PS4", and "Orbis OS" are registered trademarks of Sony Interactive Entertainment Inc. All commercial game titles and assets belong to their respective owners.

---

## License

[GPL-2.0](LICENSE)
