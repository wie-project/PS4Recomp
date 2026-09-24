package emitter

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strings"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/lifter"

	"golang.org/x/arch/x86/x86asm"
)

// GuestModule is a recompiled PRX/SPRX whose exports are published to sceKernelDlsym.
type GuestModule struct {
	FileName string
	Aliases  []string
	Exports  []elfloader.Symbol
	Init     []uint64
}

// CanonicalShims maps library/kernel symbol names to host runtime shims.
var CanonicalShims = map[string]string{
	"sceKernelUsleep":           "shim_sceKernelUsleep",
	"sysconf":                   "shim_sysconf",
	"open":                      "shim_open",
	"fcntl":                     "shim_fcntl",
	"__error":                   "shim_error",
	"mmap":                      "shim_mmap",
	"munmap":                    "shim_munmap",
	"madvise":                   "shim_madvise",
	"sigprocmask":               "shim_sigprocmask",
	"sigaction":                 "shim_sigaction",
	"fstat":                     "shim_fstat",
	"close":                     "shim_close",
	"read":                      "shim_read",
	"readv":                     "shim_readv",
	"write":                     "shim_write",
	"writev":                    "shim_writev",
	"ioctl":                     "shim_ioctl",
	"nanosleep":                 "shim_nanosleep",
	"lseek":                     "shim_lseek",
	"exit":                      "shim_exit",
	"poll":                      "shim_poll",
	"raise":                     "shim_raise",
	"pthread_sigmask":           "shim_pthread_sigmask",
	"cpuset_getaffinity":        "shim_cpuset_getaffinity",
	"getrlimit":                 "shim_getrlimit",
	"sched_yield":               "shim_sched_yield",
	"pthread_create":            "shim_pthread_create",
	"pthread_join":              "shim_pthread_join",
	"pthread_detach":            "shim_pthread_detach",
	"pthread_self":              "shim_pthread_self",
	"pthread_equal":             "shim_pthread_equal",
	"pthread_once":              "shim_pthread_once",
	"pthread_key_create":        "shim_pthread_key_create",
	"pthread_setspecific":       "shim_pthread_setspecific",
	"pthread_getspecific":       "shim_pthread_getspecific",
	"pthread_mutex_init":        "shim_pthread_mutex_init",
	"pthread_mutex_lock":        "shim_pthread_mutex_lock",
	"pthread_mutex_trylock":     "shim_pthread_mutex_trylock",
	"pthread_mutex_unlock":      "shim_pthread_mutex_unlock",
	"pthread_mutex_destroy":     "shim_pthread_mutex_destroy",
	"pthread_mutexattr_init":    "shim_pthread_mutexattr_init",
	"pthread_mutexattr_settype": "shim_pthread_mutexattr_settype",
	"pthread_mutexattr_destroy": "shim_pthread_mutexattr_destroy",
	"pthread_cond_init":         "shim_pthread_cond_init",
	"pthread_cond_wait":         "shim_pthread_cond_wait",
	"pthread_cond_timedwait":    "shim_pthread_cond_timedwait",
	"pthread_cond_signal":       "shim_pthread_cond_signal",
	"pthread_cond_broadcast":    "shim_pthread_cond_broadcast",
	"pthread_cond_destroy":      "shim_pthread_cond_destroy",
	"pthread_rwlock_rdlock":     "shim_pthread_rwlock_rdlock",
	"pthread_rwlock_wrlock":     "shim_pthread_rwlock_wrlock",
	"pthread_rwlock_unlock":     "shim_pthread_rwlock_unlock",
	"syscall":                   "shim_syscall",
	// Guest libc memory primitives. SELF/PRX images statically link musl
	// copies and export them by NID; bind those addresses to host shims so
	// every module uses one implementation that copies through mem_base.
	"memcpy":  "shim_memcpy",
	"memmove": "shim_memmove",
	"memset":  "shim_memset",
	"strlen":  "shim_strlen",
	"strcpy":  "shim_strcpy",
	"strncpy": "shim_strncpy",
	"strcmp":  "shim_strcmp",
	// Direct Memory
	"sceKernelAllocateDirectMemory": "shim_sceKernelAllocateDirectMemory",
	"sceKernelGetDirectMemorySize":  "shim_sceKernelGetDirectMemorySize",
	"sceKernelMapDirectMemory":      "shim_sceKernelMapDirectMemory",
	"sceKernelReleaseDirectMemory":  "shim_sceKernelReleaseDirectMemory",
	// Event Queue
	"sceKernelCreateEqueue": "shim_sceKernelCreateEqueue",
	"sceKernelDeleteEqueue": "shim_sceKernelDeleteEqueue",
	"sceKernelWaitEqueue":   "shim_sceKernelWaitEqueue",
	// VideoOut display
	"sceVideoOutOpen":                "shim_sceVideoOutOpen",
	"sceVideoOutClose":               "shim_sceVideoOutClose",
	"sceVideoOutSetBufferAttribute":  "shim_sceVideoOutSetBufferAttribute",
	"sceVideoOutRegisterBuffers":     "shim_sceVideoOutRegisterBuffers",
	"sceVideoOutSetFlipRate":         "shim_sceVideoOutSetFlipRate",
	"sceVideoOutAddFlipEvent":        "shim_sceVideoOutAddFlipEvent",
	"sceVideoOutSubmitFlip":          "shim_sceVideoOutSubmitFlip",
	"sceVideoOutGetFlipStatus":       "shim_sceVideoOutGetFlipStatus",
	"sceVideoOutGetResolutionStatus": "shim_sceVideoOutGetResolutionStatus",
	"sceVideoOutIsFlipPending":       "shim_sceVideoOutIsFlipPending",
	"sceVideoOutUnregisterBuffers":   "shim_sceVideoOutUnregisterBuffers",
	// Stack Protector
	"__stack_chk_fail": "shim___stack_chk_fail",
	// Libc aliases with leading underscore
	"_exit":           "shim_exit",
	"_fcntl":          "shim_fcntl",
	"_ioctl":          "shim_ioctl",
	"_open":           "shim_open",
	"_read":           "shim_read",
	"_readv":          "shim_readv",
	"_write":          "shim_write",
	"_writev":         "shim_writev",
	"sceKernelMunmap": "shim_munmap",
	// Time & Resource
	"clock_gettime": "shim_clock_gettime",
	"gettimeofday":  "shim_gettimeofday",
	"getrusage":     "shim_getrusage",
	// Pthread attributes & sched
	"pthread_attr_init":           "shim_pthread_attr_init",
	"pthread_attr_destroy":        "shim_pthread_attr_destroy",
	"pthread_attr_setdetachstate": "shim_pthread_attr_setdetachstate",
	"pthread_attr_setstacksize":   "shim_pthread_attr_setstacksize",
	"pthread_getschedparam":       "shim_pthread_getschedparam",
	"pthread_setschedparam":       "shim_pthread_setschedparam",
	"pthread_setcanceltype":       "shim_pthread_setcanceltype",
	"sched_get_priority_max":      "shim_sched_get_priority_max",
	"sched_get_priority_min":      "shim_sched_get_priority_min",
	// POSIX Semaphores
	"sem_init":     "shim_sem_init",
	"sem_destroy":  "shim_sem_destroy",
	"sem_wait":     "shim_sem_wait",
	"sem_trywait":  "shim_sem_trywait",
	"sem_post":     "shim_sem_post",
	"sem_getvalue": "shim_sem_getvalue",
	// AudioOut
	"sceAudioOutInit":   "shim_sceAudioOutInit",
	"sceAudioOutOpen":   "shim_sceAudioOutOpen",
	"sceAudioOutOutput": "shim_sceAudioOutOutput",
	"sceAudioOutClose":  "shim_sceAudioOutClose",
	// User Service
	"sceUserServiceInitialize":         "shim_sceUserServiceInitialize",
	"sceUserServiceGetInitialUser":     "shim_sceUserServiceGetInitialUser",
	"sceUserServiceGetLoginUserIdList": "shim_sceUserServiceGetLoginUserIdList",
	"sceUserServiceGetUserName":        "shim_sceUserServiceGetUserName",
	"sceUserServiceTerminate":          "shim_sceUserServiceTerminate",
	// Pad Subsystem
	"scePadInit":      "shim_scePadInit",
	"scePadOpen":      "shim_scePadOpen",
	"scePadClose":     "shim_scePadClose",
	"scePadReadState": "shim_scePadReadState",
	"scePadRead":      "shim_scePadRead",
	"scePadGetHandle": "shim_scePadGetHandle",
	// Keyboard Subsystem
	"sceKeyboardInit":        "shim_sceKeyboardInit",
	"sceKeyboardOpen":        "shim_sceKeyboardOpen",
	"sceKeyboardClose":       "shim_sceKeyboardClose",
	"sceKeyboardReadState":   "shim_sceKeyboardReadState",
	"sceKeyboardGetKey2Char": "shim_sceKeyboardGetKey2Char",
	"sceKeyboardGetHandle":   "shim_sceKeyboardGetHandle",
	// Sysmodule
	"sceSysmoduleLoadModule":           "shim_sceSysmoduleLoadModule",
	"sceSysmoduleIsLoaded":             "shim_sceSysmoduleIsLoaded",
	"sceSysmoduleUnloadModule":         "shim_sceSysmoduleUnloadModule",
	"sceSysmoduleLoadModuleInternal":   "shim_sceSysmoduleLoadModuleInternal",
	"sceSysmoduleUnloadModuleInternal": "shim_sceSysmoduleUnloadModuleInternal",
	// FreeType
	"FT_Init_FreeType":   "shim_FT_Init_FreeType",
	"FT_New_Face":        "shim_FT_New_Face",
	"FT_Set_Pixel_Sizes": "shim_FT_Set_Pixel_Sizes",
	"FT_Get_Char_Index":  "shim_FT_Get_Char_Index",
	"FT_Load_Glyph":      "shim_FT_Load_Glyph",
	"FT_Render_Glyph":    "shim_FT_Render_Glyph",
	// CommonDialog & MsgDialog
	"sceCommonDialogInitialize":       "shim_sceCommonDialogInitialize",
	"sceCommonDialogIsUsed":           "shim_sceCommonDialogIsUsed",
	"sceMsgDialogInitialize":          "shim_sceMsgDialogInitialize",
	"sceMsgDialogOpen":                "shim_sceMsgDialogOpen",
	"sceMsgDialogGetResult":           "shim_sceMsgDialogGetResult",
	"sceMsgDialogGetStatus":           "shim_sceMsgDialogGetStatus",
	"sceMsgDialogUpdateStatus":        "shim_sceMsgDialogUpdateStatus",
	"sceMsgDialogClose":               "shim_sceMsgDialogClose",
	"sceMsgDialogTerminate":           "shim_sceMsgDialogTerminate",
	"sceMsgDialogProgressBarInc":      "shim_sceMsgDialogProgressBarInc",
	"sceMsgDialogProgressBarSetMsg":   "shim_sceMsgDialogProgressBarSetMsg",
	"sceMsgDialogProgressBarSetValue": "shim_sceMsgDialogProgressBarSetValue",
	// NpTrophy
	"sceNpTrophyInit":            "shim_sceNpTrophyInit",
	"sceNpTrophyTerm":            "shim_sceNpTrophyTerm",
	"sceNpTrophyCreateContext":   "shim_sceNpTrophyCreateContext",
	"sceNpTrophyDestroyContext":  "shim_sceNpTrophyDestroyContext",
	"sceNpTrophyCreateHandle":    "shim_sceNpTrophyCreateHandle",
	"sceNpTrophyDestroyHandle":   "shim_sceNpTrophyDestroyHandle",
	"sceNpTrophyRegisterContext": "shim_sceNpTrophyRegisterContext",
	"sceNpTrophyUnlockTrophy":    "shim_sceNpTrophyUnlockTrophy",
	"sceNpTrophyShowTrophyList":  "shim_sceNpTrophyShowTrophyList",
	// Dynamic Module Loader
	"sceKernelLoadStartModule": "shim_sceKernelLoadStartModule",
	"sceKernelDlsym":           "shim_sceKernelDlsym",
}

func nidPrefix(name string) string {
	if i := strings.IndexByte(name, '#'); i >= 0 {
		return name[:i]
	}
	return ""
}

func shimByNID(nid string) (string, bool) {
	if nid == "" {
		return "", false
	}
	for plain, shim := range CanonicalShims {
		if elfloader.CalculateNID(plain) == nid {
			return shim, true
		}
		if !strings.HasPrefix(plain, "_") && elfloader.CalculateNID("_"+plain) == nid {
			return shim, true
		}
	}
	return "", false
}

// LookupShim looks up a host shim for a symbol name or Sony NID (hash#lib#mod).
func LookupShim(name string) (string, bool) {
	if shim, ok := CanonicalShims[name]; ok {
		return shim, true
	}
	stripped := strings.TrimPrefix(name, "_")
	if shim, ok := CanonicalShims[stripped]; ok {
		return shim, true
	}
	if nid := nidPrefix(name); nid != "" {
		if shim, ok := shimByNID(nid); ok {
			return shim, true
		}
	}
	return "", false
}

// CEmitter emits C source files from disassembled functions.
type CEmitter struct {
	elf       *elfloader.LoadedELF
	disasm    *disasm.Disassembler
	lifter    *lifter.Lifter
	shimMap   map[uint64]string
	AppDir    string
	Modules   []GuestModule
	ChunkSize int
	// Funcs is an optional compact index of reachable functions. When set,
	// emission re-disassembles each address instead of requiring d.Functions.
	Funcs    []disasm.ReachableFunc
	HLE      *HLEReport
	linesBuf []string
}

// NewCEmitter creates a new C emitter.
func NewCEmitter(loaded *elfloader.LoadedELF, d *disasm.Disassembler, l *lifter.Lifter) *CEmitter {
	shimMap := make(map[uint64]string)
	bindAddr := func(name string, addr uint64) {
		if name == "" || addr == 0 {
			return
		}
		if shim, ok := LookupShim(name); ok {
			shimMap[addr] = shim
		}
	}
	for _, rel := range loaded.Relocations {
		if rel.Type == elfloader.R_X86_64_JUMP_SLOT && rel.Offset+8 <= uint64(len(loaded.MemoryImage)) {
			if rel.SymName != "" {
				if shim, ok := LookupShim(rel.SymName); ok {
					if rel.PltAddr != 0 {
						shimMap[rel.PltAddr] = shim
					}
					shimMap[rel.Offset] = shim
					// call *[GOT] loads the slot; store the GOT VA so dispatch hits the shim.
					binary.LittleEndian.PutUint64(loaded.MemoryImage[rel.Offset:rel.Offset+8], rel.Offset)
					continue
				}
			}
			binary.LittleEndian.PutUint64(loaded.MemoryImage[rel.Offset:rel.Offset+8], 0)
			continue
		}
		if rel.SymName == "" {
			continue
		}
		shim, ok := LookupShim(rel.SymName)
		if !ok {
			continue
		}
		if rel.PltAddr != 0 {
			shimMap[rel.PltAddr] = shim
		}
		shimMap[rel.Offset] = shim
	}
	for _, sym := range loaded.Symbols {
		bindAddr(sym.Name, sym.Address)
	}
	// SELF/PRX files often have empty section symbols; the Sony dynsym
	// table still names statically linked libc routines by NID.
	for _, sym := range loaded.DynSymbols {
		bindAddr(sym.Name, sym.Address)
	}

	return &CEmitter{
		elf:     loaded,
		disasm:  d,
		lifter:  l,
		shimMap: shimMap,
	}
}

// ResolveEntryAddress dynamically resolves the primary guest entry point.
// It prioritizes the ELF EntryPoint (_start) and falls back to the "main" symbol.
func (e *CEmitter) ResolveEntryAddress() (uint64, string, error) {
	if e.elf.EntryPoint != 0 {
		return e.elf.EntryPoint, "_start", nil
	}
	if mainSym, ok := e.elf.SymbolByName["main"]; ok && mainSym.Address != 0 {
		return mainSym.Address, "main", nil
	}
	return 0, "", fmt.Errorf("ELF has no entry point and no main symbol")
}

// EmitAll generates all C files and guest image in the target directory,
// returning the list of generated C source file paths.
func (e *CEmitter) EmitAll(outDir string) ([]string, error) {
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return nil, err
	}

	var cFiles []string

	mainPath := filepath.Join(outDir, "main.c")
	if err := e.EmitMainRunner(mainPath); err != nil {
		return nil, fmt.Errorf("failed to emit main.c: %w", err)
	}
	cFiles = append(cFiles, mainPath)

	binPath := filepath.Join(outDir, "guest_image.bin")
	if err := e.EmitGuestImage(binPath); err != nil {
		return nil, fmt.Errorf("failed to emit guest_image.bin: %w", err)
	}

	headerPath := filepath.Join(outDir, "guest_functions.h")
	if err := e.EmitFunctionsHeader(headerPath); err != nil {
		return nil, fmt.Errorf("failed to emit guest_functions.h: %w", err)
	}

	codeFiles, err := e.EmitChunkedCode(outDir, 15000)
	if err != nil {
		return nil, fmt.Errorf("failed to emit chunked code: %w", err)
	}
	cFiles = append(cFiles, codeFiles...)

	modPath := filepath.Join(outDir, "guest_modules.c")
	if err := e.EmitGuestModules(modPath); err != nil {
		return nil, fmt.Errorf("failed to emit guest_modules.c: %w", err)
	}
	cFiles = append(cFiles, modPath)

	return cFiles, nil
}

func writeFileIfChanged(path string, data []byte) error {
	existing, err := os.ReadFile(path)
	if err == nil && bytes.Equal(existing, data) {
		return nil
	}
	return os.WriteFile(path, data, 0o644)
}

// EmitFunctionsHeader generates forward declarations for all recompiled functions.
func (e *CEmitter) EmitFunctionsHeader(path string) (err error) {
	var buf bytes.Buffer
	w := bufio.NewWriter(&buf)

	if _, err := w.WriteString("#ifndef GUEST_FUNCTIONS_H\n#define GUEST_FUNCTIONS_H\n\n#include \"recomp_runtime.h\"\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"); err != nil {
		return err
	}

	fnAddrs := e.functionAddrs()

	for _, addr := range fnAddrs {
		if _, err := fmt.Fprintf(w, "void fn_0x%x(GuestContext *__restrict__ ctx);\n", addr); err != nil {
			return err
		}
	}

	if _, err := w.WriteString("\n#ifdef __cplusplus\n}\n#endif\n\n#endif // GUEST_FUNCTIONS_H\n"); err != nil {
		return err
	}
	if err := w.Flush(); err != nil {
		return err
	}
	return writeFileIfChanged(path, buf.Bytes())
}

// EmitChunkedCode partitions recompiled functions across multiple C source files
// based on an instruction budget to ensure balanced compile times and file sizes,
// and emits a dispatch.c driver that ties all chunk registrations together.
func (e *CEmitter) functionAddrs() []uint64 {
	if len(e.Funcs) > 0 {
		addrs := make([]uint64, len(e.Funcs))
		for i, f := range e.Funcs {
			addrs[i] = f.Addr
		}
		return addrs
	}
	addrs := make([]uint64, 0, len(e.disasm.Functions))
	for addr := range e.disasm.Functions {
		addrs = append(addrs, addr)
	}
	slices.Sort(addrs)
	return addrs
}

func (e *CEmitter) instCountByAddr() map[uint64]int {
	if len(e.Funcs) > 0 {
		m := make(map[uint64]int, len(e.Funcs))
		for _, f := range e.Funcs {
			m[f.Addr] = f.Insts
		}
		return m
	}
	m := make(map[uint64]int, len(e.disasm.Functions))
	for addr, fn := range e.disasm.Functions {
		m[addr] = e.functionInstCount(fn)
	}
	return m
}

func (e *CEmitter) loadFunction(addr uint64) (*disasm.Function, error) {
	if e.disasm.Functions != nil {
		if fn := e.disasm.Functions[addr]; fn != nil {
			return fn, nil
		}
	}
	fn, _, err := e.disasm.DisasmFunction(addr)
	if err != nil {
		return nil, err
	}
	return fn, nil
}

func (e *CEmitter) EmitChunkedCode(outDir string, targetBudget int) ([]string, error) {
	fnAddrs := e.functionAddrs()

	if targetBudget <= 0 {
		targetBudget = 15000
	}
	maxFuncsPerChunk := e.ChunkSize
	if maxFuncsPerChunk <= 0 {
		maxFuncsPerChunk = 150
	}

	var chunks [][]uint64
	var currentChunk []uint64
	currentInsts := 0

	insts := e.instCountByAddr()
	for _, addr := range fnAddrs {
		nInsts := insts[addr]

		if len(currentChunk) > 0 && (currentInsts+nInsts > targetBudget || len(currentChunk) >= maxFuncsPerChunk) {
			chunks = append(chunks, currentChunk)
			currentChunk = nil
			currentInsts = 0
		}

		currentChunk = append(currentChunk, addr)
		currentInsts += nInsts
	}

	if len(currentChunk) > 0 {
		chunks = append(chunks, currentChunk)
	}

	if len(chunks) == 0 {
		chunks = append(chunks, []uint64{})
	}

	var generatedFiles []string

	for chunkIdx, chunkAddrs := range chunks {
		chunkFileName := fmt.Sprintf("code_%03d.c", chunkIdx)
		chunkPath := filepath.Join(outDir, chunkFileName)
		if err := e.emitSingleChunk(chunkPath, chunkIdx, chunkAddrs); err != nil {
			return nil, fmt.Errorf("failed to emit %s: %w", chunkFileName, err)
		}
		generatedFiles = append(generatedFiles, chunkPath)
	}

	dispatchPath := filepath.Join(outDir, "dispatch.c")
	if err := e.emitDispatch(dispatchPath, len(chunks)); err != nil {
		return nil, fmt.Errorf("failed to emit dispatch.c: %w", err)
	}
	generatedFiles = append(generatedFiles, dispatchPath)

	return generatedFiles, nil
}

func (e *CEmitter) functionInstCount(fn *disasm.Function) int {
	if fn == nil {
		return 0
	}
	count := 0
	for _, b := range fn.Blocks {
		count += len(b.Insts)
	}
	return count
}

func (e *CEmitter) collectExternalCallees(loaded []*disasm.Function, chunkSet map[uint64]bool) []uint64 {
	extMap := make(map[uint64]bool)
	for _, fn := range loaded {
		if fn == nil {
			continue
		}
		for _, b := range fn.Blocks {
			for _, inst := range b.Insts {
				for _, arg := range inst.Inst.Args {
					if arg == nil {
						break
					}
					if rel, ok := arg.(x86asm.Rel); ok {
						nextPC := inst.Address + uint64(inst.Inst.Len)
						target := uint64(int64(nextPC) + int64(rel))
						if !chunkSet[target] && e.lifter.IsKnownFunc(target) {
							extMap[target] = true
						}
					}
				}
			}
		}
	}
	var res []uint64
	for addr := range extMap {
		res = append(res, addr)
	}
	slices.Sort(res)
	return res
}

func (e *CEmitter) emitSingleChunk(path string, chunkIdx int, chunkAddrs []uint64) (err error) {
	var buf bytes.Buffer
	w := bufio.NewWriter(&buf)

	if _, err := fmt.Fprintf(w, "#include \"recomp_runtime.h\"\n\n// Chunk %d (%d functions)\n\n", chunkIdx, len(chunkAddrs)); err != nil {
		return err
	}

	loaded := make([]*disasm.Function, len(chunkAddrs))
	for i, addr := range chunkAddrs {
		fn, err := e.loadFunction(addr)
		if err != nil {
			return fmt.Errorf("0x%x: %w", addr, err)
		}
		loaded[i] = fn
	}

	// Forward declarations for functions defined in this chunk
	if _, err := w.WriteString("// Forward declarations for functions defined in this chunk\n"); err != nil {
		return err
	}
	for _, addr := range chunkAddrs {
		if _, err := fmt.Fprintf(w, "void fn_0x%x(GuestContext *__restrict__ ctx);\n", addr); err != nil {
			return err
		}
	}

	// Forward declarations for external callees referenced by this chunk
	chunkSet := make(map[uint64]bool, len(chunkAddrs))
	for _, addr := range chunkAddrs {
		chunkSet[addr] = true
	}
	extCalls := e.collectExternalCallees(loaded, chunkSet)
	if len(extCalls) > 0 {
		if _, err := w.WriteString("\n// Forward declarations for external functions called by this chunk\n"); err != nil {
			return err
		}
		for _, addr := range extCalls {
			if _, err := fmt.Fprintf(w, "void fn_0x%x(GuestContext *__restrict__ ctx);\n", addr); err != nil {
				return err
			}
		}
	}
	if _, err := w.WriteString("\n"); err != nil {
		return err
	}

	for _, fn := range loaded {
		if err := e.emitFunction(w, fn); err != nil {
			return err
		}
	}

	// Emit chunk dispatch registration function
	if _, err := fmt.Fprintf(w, "// Registration for chunk %d\nvoid recomp_init_dispatch_chunk_%d(void) {\n", chunkIdx, chunkIdx); err != nil {
		return err
	}
	for i, addr := range chunkAddrs {
		fn := loaded[i]
		if shim, isShimmed := e.shimMap[addr]; isShimmed {
			if _, err := fmt.Fprintf(w, "    recomp_register_fn(0x%xULL, %s);\n", addr, shim); err != nil {
				return err
			}
			continue
		}
		for _, blockAddr := range fn.BlockOrder {
			if _, err := fmt.Fprintf(w, "    recomp_register_fn(0x%xULL, fn_0x%x);\n", blockAddr, addr); err != nil {
				return err
			}
		}
	}
	if _, err := w.WriteString("}\n"); err != nil {
		return err
	}
	if err := w.Flush(); err != nil {
		return err
	}

	return writeFileIfChanged(path, buf.Bytes())
}

func (e *CEmitter) functionNeedsUnwind(fn *disasm.Function, maxEnd uint64) bool {
	if fn == nil || len(fn.BlockOrder) <= 1 {
		return false
	}

	// 1. If ELF has .eh_frame unwind tables, only functions with an LSDA
	// (exception landing pads for C++ catch/cleanup) or calling setjmp need UnwindFrame.
	if len(e.elf.FuncBounds) > 0 {
		if e.elf.FunctionHasLSDA(fn.EntryAddr, maxEnd) {
			return true
		}
		for _, b := range fn.Blocks {
			for _, inst := range b.Insts {
				if inst.Inst.Op == x86asm.CALL {
					for _, arg := range inst.Inst.Args {
						if arg == nil {
							break
						}
						if rel, ok := arg.(x86asm.Rel); ok {
							target := uint64(int64(inst.Address+uint64(inst.Inst.Len)) + int64(rel))
							if sym, ok := e.elf.SymbolByAddr[target]; ok {
								if strings.Contains(sym.Name, "setjmp") || strings.Contains(sym.Name, "sigsetjmp") {
									return true
								}
							}
						}
					}
				}
			}
		}
		return false
	}

	// 2. Fallback for binaries without .eh_frame (e.g. synthetic test ELFs):
	for _, b := range fn.Blocks {
		for _, inst := range b.Insts {
			if inst.Inst.Op == x86asm.CALL {
				return true
			}
		}
	}
	return false
}

func (e *CEmitter) emitFunction(w *bufio.Writer, fn *disasm.Function) error {
	addr := fn.EntryAddr
	if shim, isShimmed := e.shimMap[addr]; isShimmed {
		if _, err := fmt.Fprintf(w, "// Function %s at 0x%x (Forwarded directly to host shim %s)\nvoid fn_0x%x(GuestContext *__restrict__ ctx) {\n    UnwindFrame *__cur_unwind_frame = NULL;\n    (void)__cur_unwind_frame;\n    %s(ctx);\n    return;\n}\n\n", fn.Name, addr, shim, addr, shim); err != nil {
			return err
		}
		return nil
	}

	if _, err := fmt.Fprintf(w, "// Function %s at 0x%x\nvoid fn_0x%x(GuestContext *__restrict__ ctx) {\n", fn.Name, addr, addr); err != nil {
		return err
	}

	maxEnd := addr
	for _, b := range fn.Blocks {
		if b.EndAddr > maxEnd {
			maxEnd = b.EndAddr
		}
	}
	if sym, ok := e.elf.SymbolByAddr[addr]; ok && sym.Size > 0 && addr+sym.Size > maxEnd {
		maxEnd = addr + sym.Size
	}

	needsUnwind := e.functionNeedsUnwind(fn, maxEnd)

	if needsUnwind {
		if _, err := fmt.Fprintf(w, "    UnwindFrame __unwind_frame;\n    __unwind_frame.fn_start = 0x%xULL;\n    __unwind_frame.fn_end = 0x%xULL;\n    __unwind_frame.prev = ctx->unwind_frame;\n    ctx->unwind_frame = &__unwind_frame;\n    UnwindFrame *__cur_unwind_frame = &__unwind_frame;\n", addr, maxEnd); err != nil {
			return err
		}
		if _, err := w.WriteString("    if (_setjmp(__unwind_frame.buf) != 0) {\n        switch (ctx->rip) {\n"); err != nil {
			return err
		}
		for _, blockAddr := range fn.BlockOrder {
			if blockAddr != addr {
				if _, err := fmt.Fprintf(w, "            case 0x%xULL: goto loc_0x%x;\n", blockAddr, blockAddr); err != nil {
					return err
				}
			}
		}
		if _, err := fmt.Fprintf(w, "            default: goto loc_0x%x;\n        }\n    }\n", addr); err != nil {
			return err
		}
	} else {
		if _, err := w.WriteString("    UnwindFrame *__cur_unwind_frame = NULL;\n"); err != nil {
			return err
		}
	}

	if len(fn.BlockOrder) > 1 {
		if _, err := fmt.Fprintf(w, "    if (ctx->rip != 0x%xULL) {\n        switch (ctx->rip) {\n", addr); err != nil {
			return err
		}
		for _, blockAddr := range fn.BlockOrder {
			if blockAddr != addr {
				if _, err := fmt.Fprintf(w, "            case 0x%xULL: goto loc_0x%x;\n", blockAddr, blockAddr); err != nil {
					return err
				}
			}
		}
		if _, err := fmt.Fprintf(w, "            default: goto loc_0x%x;\n        }\n    }\n", addr); err != nil {
			return err
		}
	}

	for _, blockAddr := range fn.BlockOrder {
		block := fn.Blocks[blockAddr]
		if _, err := fmt.Fprintf(w, "\nloc_0x%x:\n", blockAddr); err != nil {
			return err
		}
		if _, err := fmt.Fprintf(w, "    ctx->rip = 0x%xULL;\n", blockAddr); err != nil {
			return err
		}

		if e.HLE != nil {
			e.HLE.ObserveBlock(block.Insts, e.shimMap)
		}
		for _, inst := range block.Insts {
			nextPC := inst.Address + uint64(inst.Inst.Len)
			lines, err := e.lifter.LiftInstructionToBuf(inst, nextPC, fn, e.linesBuf[:0])
			if err != nil {
				if _, err := fmt.Fprintf(w, "    /* 0x%x: %s [UNSUPPORTED: %v] */\n", inst.Address, inst.Inst.String(), err); err != nil {
					return err
				}
				if _, err := fmt.Fprintf(w, "    fprintf(stderr, \"FATAL: Unsupported instruction at 0x%x: %s\\n\"); abort();\n", inst.Address, inst.Inst.String()); err != nil {
					return err
				}
			} else {
				e.linesBuf = lines
				for _, line := range lines {
					if _, err := w.WriteString(line); err != nil {
						return err
					}
					if err := w.WriteByte('\n'); err != nil {
						return err
					}
				}
			}
		}
	}

	if _, err := w.WriteString("    RECOMP_POP_UNWIND();\n}\n\n"); err != nil {
		return err
	}
	return nil
}

func (e *CEmitter) emitDispatch(path string, numChunks int) (err error) {
	var buf bytes.Buffer
	w := bufio.NewWriter(&buf)

	if _, err := w.WriteString("#include \"recomp_runtime.h\"\n\nvoid recomp_register_guest_modules(void);\n\n// Forward declarations of chunk registration functions\n"); err != nil {
		return err
	}
	for i := range numChunks {
		if _, err := fmt.Fprintf(w, "void recomp_init_dispatch_chunk_%d(void);\n", i); err != nil {
			return err
		}
	}

	if _, err := w.WriteString("\n// Initialize dispatch table with all recompiled chunks and runtime shims\nvoid recomp_init_dispatch_table(void) {\n"); err != nil {
		return err
	}

	for i := range numChunks {
		if _, err := fmt.Fprintf(w, "    recomp_init_dispatch_chunk_%d();\n", i); err != nil {
			return err
		}
	}

	if err := e.emitPLTRegistrations(w); err != nil {
		return err
	}

	if _, err := w.WriteString("    recomp_register_guest_modules();\n"); err != nil {
		return err
	}

	if _, err := w.WriteString("}\n"); err != nil {
		return err
	}
	if err := w.Flush(); err != nil {
		return err
	}

	return writeFileIfChanged(path, buf.Bytes())
}

func (e *CEmitter) emitPLTRegistrations(w *bufio.Writer) error {
	for _, rel := range e.elf.Relocations {
		if rel.SymName != "" {
			if shim, ok := LookupShim(rel.SymName); ok {
				if rel.PltAddr != 0 {
					if _, err := fmt.Fprintf(w, "    recomp_register_fn(0x%xULL, %s); // PLT %s\n", rel.PltAddr, shim, rel.SymName); err != nil {
						return err
					}
				}
				if _, err := fmt.Fprintf(w, "    recomp_register_fn(0x%xULL, %s); // GOT %s\n", rel.Offset, shim, rel.SymName); err != nil {
					return err
				}
			}
		}
	}

	// Register shims for matching static and dynamic symbols (e.g. libc memcpy).
	seen := make(map[uint64]struct{})
	registerSym := func(sym elfloader.Symbol) error {
		if sym.Name == "" || sym.Address == 0 {
			return nil
		}
		if _, ok := seen[sym.Address]; ok {
			return nil
		}
		shim, ok := LookupShim(sym.Name)
		if !ok {
			return nil
		}
		seen[sym.Address] = struct{}{}
		_, err := fmt.Fprintf(w, "    recomp_register_fn(0x%xULL, %s); // Symbol %s\n", sym.Address, shim, sym.Name)
		return err
	}
	for _, sym := range e.elf.Symbols {
		if err := registerSym(sym); err != nil {
			return err
		}
	}
	for _, sym := range e.elf.DynSymbols {
		if err := registerSym(sym); err != nil {
			return err
		}
	}
	return nil
}

// EmitGuestImage writes the raw binary ELF memory image directly to disk.
func (e *CEmitter) EmitGuestImage(path string) error {
	return writeFileIfChanged(path, e.elf.MemoryImage)
}

// EmitMainRunner writes main.c driver.
func (e *CEmitter) EmitMainRunner(path string) (err error) {
	entryAddr, entryName, err := e.ResolveEntryAddress()
	if err != nil {
		return err
	}

	vfsInitArg := "NULL"
	if e.AppDir != "" {
		vfsInitArg = fmt.Sprintf("%q", e.AppDir)
	}

	content := fmt.Sprintf(`#include "recomp_runtime.h"
#include "ps4_vfs.h"

extern void recomp_init_dispatch_table(void);

int main(int argc, char **argv) {
    printf("[ps4-recomp] Initializing runtime...\n");
    ps4_vfs_init(%s);
    recomp_init_dispatch_table();

    const char *prog_name = (argc > 0 && argv[0]) ? argv[0] : "ps4_app";
    GuestContext *ctx = recomp_init_runtime_file("guest_image.bin", 0, prog_name);
    if (!ctx) {
        fprintf(stderr, "[ps4-recomp] Failed to allocate guest memory or load guest_image.bin\n");
        return 1;
    }

    if (strcmp("%s", "_start") != 0) {
        printf("[ps4-recomp] Calling global constructors (.init_array)...\n");
        // Run .init_array
        uint64_t init_arr[] = {
%s
        };
        size_t init_count = sizeof(init_arr) / sizeof(init_arr[0]);
        for (size_t i = 0; i < init_count; i++) {
            if (init_arr[i] != 0) {
                printf("[ps4-recomp] Running init constructor at 0x%%llx...\n", (unsigned long long)init_arr[i]);
                recomp_call_guest(ctx, init_arr[i]);
            }
        }
    }

    printf("[ps4-recomp] Executing %s (0x%x)...\n");
    recomp_dispatch(ctx, 0x%xULL);

    printf("[ps4-recomp] Execution complete.\n");
    recomp_free_runtime(ctx);
    return 0;
}
`, vfsInitArg, entryName, e.formatInitArray(), entryName, entryAddr, entryAddr)

	return writeFileIfChanged(path, []byte(content))
}

func (e *CEmitter) formatInitArray() string {
	var lines []string
	for _, addr := range e.elf.InitArray {
		lines = append(lines, fmt.Sprintf("        0x%xULL,", addr))
	}
	if len(lines) == 0 {
		return "        0"
	}
	return strings.Join(lines, "\n")
}

// EmitGuestModules writes the runtime export tables used by sceKernelDlsym.
func (e *CEmitter) EmitGuestModules(path string) (err error) {
	var buf bytes.Buffer
	w := bufio.NewWriter(&buf)

	if _, err := w.WriteString("#include \"recomp_runtime.h\"\n#include \"ps4_sysmodule.h\"\n\n"); err != nil {
		return err
	}

	shimAddr := make(map[string]uint64)
	for addr, shim := range e.shimMap {
		for name, target := range CanonicalShims {
			if target == shim {
				if _, exists := shimAddr[name]; !exists {
					shimAddr[name] = addr
				}
			}
		}
	}

	nextSynth := (e.elf.MaxVAddr + 0xFFF) &^ 0xFFF
	if nextSynth < 0x1000 {
		nextSynth = 0x1000
	}
	var synthRegs []string
	shimNames := make([]string, 0, len(CanonicalShims))
	for name := range CanonicalShims {
		shimNames = append(shimNames, name)
	}
	slices.Sort(shimNames)
	for _, name := range shimNames {
		if _, ok := shimAddr[name]; ok {
			continue
		}
		shimAddr[name] = nextSynth
		synthRegs = append(synthRegs, fmt.Sprintf("    recomp_register_fn(0x%xULL, %s);\n", nextSynth, CanonicalShims[name]))
		nextSynth += 16
	}

	if _, err := w.WriteString("static const RecompModuleExport recomp_host_shim_exports[] = {\n"); err != nil {
		return err
	}
	for _, name := range shimNames {
		if _, err := fmt.Fprintf(w, "    { %q, 0x%xULL },\n", name, shimAddr[name]); err != nil {
			return err
		}
	}
	if _, err := w.WriteString("    { NULL, 0 }\n};\n\n"); err != nil {
		return err
	}

	for i, mod := range e.Modules {
		if _, err := fmt.Fprintf(w, "static const RecompModuleExport recomp_module_exports_%d[] = {\n", i); err != nil {
			return err
		}
		for _, exp := range mod.Exports {
			if _, err := fmt.Fprintf(w, "    { %q, 0x%xULL },\n", exp.Name, exp.Address); err != nil {
				return err
			}
		}
		if _, err := w.WriteString("    { NULL, 0 }\n};\n\n"); err != nil {
			return err
		}
		if len(mod.Init) > 0 {
			if _, err := fmt.Fprintf(w, "static const uint64_t recomp_module_init_%d[] = {\n", i); err != nil {
				return err
			}
			for _, addr := range mod.Init {
				if _, err := fmt.Fprintf(w, "    0x%xULL,\n", addr); err != nil {
					return err
				}
			}
			if _, err := w.WriteString("};\n\n"); err != nil {
				return err
			}
		}
	}

	if _, err := w.WriteString("void recomp_register_guest_modules(void) {\n"); err != nil {
		return err
	}
	for _, line := range synthRegs {
		if _, err := w.WriteString(line); err != nil {
			return err
		}
	}
	hostNames := []string{"libc.prx", "libSceLibcInternal.prx", "libkernel.prx", "libSceFios2.prx"}
	for _, name := range hostNames {
		if _, err := fmt.Fprintf(w, "    recomp_module_register(%q, recomp_host_shim_exports);\n", name); err != nil {
			return err
		}
	}
	for i, mod := range e.Modules {
		names := uniqueModuleNames(mod.FileName, mod.Aliases)
		for _, name := range names {
			if _, err := fmt.Fprintf(w, "    recomp_module_register(%q, recomp_module_exports_%d);\n", name, i); err != nil {
				return err
			}
			if len(mod.Init) > 0 {
				if _, err := fmt.Fprintf(w, "    recomp_module_register_init(%q, recomp_module_init_%d, %d);\n", name, i, len(mod.Init)); err != nil {
					return err
				}
			}
		}
	}
	if _, err := w.WriteString("}\n"); err != nil {
		return err
	}
	if err := w.Flush(); err != nil {
		return err
	}

	return writeFileIfChanged(path, buf.Bytes())
}

func uniqueModuleNames(fileName string, aliases []string) []string {
	seen := make(map[string]struct{})
	var out []string
	add := func(s string) {
		s = strings.TrimSpace(s)
		if s == "" {
			return
		}
		key := strings.ToLower(filepath.Base(s))
		if _, ok := seen[key]; ok {
			return
		}
		seen[key] = struct{}{}
		out = append(out, filepath.Base(s))
	}
	add(fileName)
	for _, a := range aliases {
		add(a)
	}
	return out
}
