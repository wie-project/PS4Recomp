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
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityShaderCompilerExtEvent' not found
[ps4-recomp] WARN: Called unimplemented function NID 'tlpXm5KLgqk' (Lib: libSceVrTracker) at RIP=0x8ad02e8 (caller RIP=0x8ae39eb, RSP=0x7ffff9ac8)
[ps4-recomp] WARN: Called unimplemented function NID 'wkgY4lDEV9k' (Lib: libSceVrTracker) at RIP=0x8ad16e8 (caller RIP=0x8ae39f7, RSP=0x7ffff9ac8)
[ps4-recomp] WARN: Called unimplemented function NID 'buMmdpEwRuM' (Lib: libSceVrTracker) at RIP=0x8ad16f8 (caller RIP=0x8ae3a0d, RSP=0x7ffff9ac8)
[ps4-recomp] WARN: Called unimplemented function NID 'El4tFb9f8dk' (Lib: libSceVrTracker) at RIP=0x8ad0128 (caller RIP=0x8ad36c4, RSP=0x7ffff9a68)
[ps4-recomp] WARN: Called unimplemented function NID 'El4tFb9f8dk' (Lib: libSceVrTracker) at RIP=0x8ad0128 (caller RIP=0x8ad37ac, RSP=0x7ffff9a68)
[ps4-recomp] WARN: Called unimplemented function NID 'El4tFb9f8dk' (Lib: libSceVrTracker) at RIP=0x8ad0128 (caller RIP=0x8ad37c9, RSP=0x7ffff9a68)
[ps4-recomp] WARN: Called unimplemented function NID 'ItZXngg8tfo' (Lib: libSceVrTracker) at RIP=0x8ad1708 (caller RIP=0x8ae3a6e, RSP=0x7ffff9ac8)
[ps4-recomp] sceKernelLoadStartModule: /app0/Media/Plugins/UnityNpToolkit2.prx
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnitySetGraphicsDevice' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityRenderEvent' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityGetAudioEffectDefinitions' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityPluginLoad' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityPluginUnload' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityRenderingExtEvent' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityRenderingExtQuery' not found
[ps4-recomp] sceKernelDlsym: handle 5 symbol 'UnityShaderCompilerExtEvent' not found
[ps4-unwind] GetModuleInfoForUnwind: addr=0x9556ed1 -> mod=libc.prx, hdr=0x95ae9ec, frame=0x9583b30 (size=0x2aebc)
...
FATAL: guest C++ exception (type 22Il2CppExceptionWrapper) thrown from 0x290cfac: exception unwinding is not supported by the ARM64 assembly backend
Registers:
  RAX=0x000000001ae0d280 RBX=0x000000001a128dc0 RCX=0x00000000021047f8 RDX=0x0000000000000000
  RSI=0x0000000006bf0000 RDI=0x000000001ae0d280 RBP=0x00000007ffff98c0 RSP=0x00000007ffff98a8
  R8 =0x00000007ffff97d0 R9 =0x0000000019c00098 R10=0x0000000000000000 R11=0x000000000000001c
  R12=0x0000000000000061 R13=0x000000001addaaf0 R14=0x0000000000000061 R15=0x0000000060000000
```

### Hogwarts Legacy (Unreal Engine 4)

```text
[ps4-vfs] Application root: /Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition/game
[ps4-settings] language 1, PS4 Pro mode on, MetalFX upscaling off, FPS HUD on
[ps4-recomp] Initializing runtime (ARM64 Assembly Backend)...
[ps4-vfs] Application root: /Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition/game
[ps4-recomp] guest memory at host 0x7000000000 (0x8000000000 bytes)
[ps4-recomp] Loading guest memory image (305774592 bytes), allocated dynamic address space (524288.0 MB)
[ps4-mem] SceLibcParam at 0x8600000: size=0xa8 ver=14.1 malloc_replace=0x86000f0 new_replace=0x8600168
[ps4-mem] SceLibcMallocReplace at 0x86000f0: size=0x78 init=0x4188230 malloc=0x4188260
[ps4-recomp] Initializing custom memory allocator via SceLibcParam at 0x4188230...
[ps4-mem] sceKernelConfiguredFlexibleMemorySize: 4294967296 (4096.00 MB)
[ps4-vm] recomp_vm_alloc_named_aligned (sync_pool): size=4194304 (4.00 MB) align=4096 flags=0x0
[ps4-vm] recomp_vm_alloc_named_aligned SUCCESS: addr=0x123d0000
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
[ps4-mem] sceKernelMemoryPoolReserve: in=0x2000000000 len=8589934592 (8192.00 MB) align=0 flags=0x90
[ps4-vm] recomp_vm_alloc_fixed (pool_reserved): desired=0x2000000000 size=8589934592 (8192.00 MB)
[ps4-mem] sceKernelMemoryPoolReserve SUCCESS: out=0x2000000000
[ps4-mem] sceKernelMemoryPoolReserve: in=0x3000000000 len=1073741824 (1024.00 MB) align=0 flags=0x90
[ps4-vm] recomp_vm_alloc_fixed (pool_reserved): desired=0x3000000000 size=1073741824 (1024.00 MB)
[ps4-mem] sceKernelMemoryPoolReserve SUCCESS: out=0x3000000000
[ps4-recomp] Initializing libc malloc subsystem via _malloc_init at 0x122b8ec0...
[ps4-kernel] _sceKernelRtldSetApplicationHeapAPI: table at 0x123941b8
[ps4-kernel] heap_malloc=0x4188260, heap_free=0x4188240
[ps4-vm] recomp_vm_alloc_named_aligned (heap_trace_info): size=4096 (0.00 MB) align=4096 flags=0x0
[ps4-vm] recomp_vm_alloc_named_aligned SUCCESS: addr=0x127d0000
[ps4-recomp] TLS initialized: vaddr=0x9800060 filesz=0x0 memsz=0x68 align=8 static_tls_size=0x100 (fs_base=0x123b0000)
[ps4-recomp] Executing _start (0xf9a50) via ARM64 assembly...
GoodPGO
[ps4-sys] open: '/app0/ue4commandline.txt' (resolved='/Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition/game/ue4commandline.txt', flags=0x0) -> fd=3
Using commandline args from /app0/ue4commandline.txt!
AdditionalArgs before fixup = ../../../Phoenix/Phoenix.uproject
AdditionalArgs after fixup = ../../../Phoenix/Phoenix.uproject
File root is /app0/!
[ps4-mem] sceKernelConfiguredFlexibleMemorySize: 4294967296 (4096.00 MB)
Used memory before allocating anything was 85.50MB
FIOS2: built with SDK version 0x09508001
[ps4-vm] recomp_vm_alloc_named_aligned (anon): size=86016 (0.08 MB) align=4096 flags=0x0
[ps4-vm] recomp_vm_alloc_named_aligned SUCCESS: addr=0x127d1000
[ps4-thread] Thread created: id=1001, name='SceFiosCallback', entry=0x11a7a710, arg=0x11aa4c98
[ps4-vm] recomp_vm_alloc_named_aligned (anon): size=151552 (0.14 MB) align=4096 flags=0x0
[ps4-vm] recomp_vm_alloc_named_aligned SUCCESS: addr=0x127e6000
[ps4-thread] Thread created: id=1002, name='SceFiosIO', entry=0x11a7a710, arg=0x11aa8f60
[ps4-thread] Thread started: id=1001, name='SceFiosCallback', entry=0x11a7a710, arg=0x11aa4c98
[ps4-vm] recomp_vm_alloc_named_aligned (anon): size=151552 (0.14 MB) align=4096 flags=0x0
[ps4-thread] Thread started: id=1002, name='SceFiosIO', entry=0x11a7a710, arg=0x11aa8f60
[ps4-vm] recomp_vm_alloc_named_aligned SUCCESS: addr=0x1280b000
[ps4-thread] Thread created: id=1003, name='SceFiosIO /', entry=0x11a7a710, arg=0x11aad1e0
[ps4-thread] Thread started: id=1003, name='SceFiosIO /', entry=0x11a7a710, arg=0x11aad1e0
[ps4-sys] open: '/app0/deepfiles' (resolved='/Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition/game/deepfiles', flags=0x20000) -> fd=-1
Project file not found: ../../../phoenix/phoenix.uproject
[ps4-sys] open: '/app0/..' (resolved='/Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition/game/..', flags=0x20000) -> fd=3
^C
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
