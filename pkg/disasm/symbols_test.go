package disasm_test

import (
	"testing"

	"debug/elf"
	"golang.org/x/arch/x86/x86asm"
	"ps4-recomp/pkg/elfloader"
)

func TestFunctionSymbols(t *testing.T) {
	elfPath := "../../hello_world.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		t.Fatalf("failed to load elf: %v", err)
	}

	funcCount := 0
	withSize := 0
	for _, sym := range loaded.Symbols {
		if sym.Type == elf.STT_FUNC && sym.Address != 0 {
			funcCount++
			if sym.Size > 0 {
				withSize++
			}
		}
	}
	t.Logf("Total STT_FUNC symbols: %d, with Size > 0: %d", funcCount, withSize)
	t.Logf("Entry point: 0x%x", loaded.EntryPoint)
	t.Logf("InitArray count: %d (first: 0x%x)", len(loaded.InitArray), loaded.InitArray[0])
}

func TestLinearSweepAllFunctions(t *testing.T) {
	elfPath := "../../hello_world.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		t.Fatalf("failed to load elf: %v", err)
	}

	success := 0
	decodeErrors := 0
	for _, sym := range loaded.Symbols {
		if sym.Type == elf.STT_FUNC && sym.Address != 0 && sym.Size > 0 {
			pc := sym.Address
			limit := sym.Address + sym.Size
			hasErr := false
			for pc < limit {
				if pc >= uint64(len(loaded.MemoryImage)) {
					hasErr = true
					break
				}
				inst, err := x86asm.Decode(loaded.MemoryImage[pc:], 64)
				if err != nil || inst.Len == 0 {
					hasErr = true
					break
				}
				pc += uint64(inst.Len)
			}
			if hasErr {
				decodeErrors++
			} else {
				success++
			}
		}
	}
	t.Logf("Linear sweep results: %d succeeded, %d had decode errors", success, decodeErrors)
}
