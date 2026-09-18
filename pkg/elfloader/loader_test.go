package elfloader_test

import (
	"testing"

	"ps4-recomp/pkg/elfloader"
)

func TestLoadELF(t *testing.T) {
	elfPath := "../../hello_world.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		t.Fatalf("failed to load elf: %v", err)
	}

	if loaded.EntryPoint != 0xb0b88 {
		t.Errorf("expected entry point 0xb0b88, got 0x%x", loaded.EntryPoint)
	}

	if len(loaded.Segments) == 0 {
		t.Errorf("expected LOAD segments, got 0")
	}

	if len(loaded.Relocations) == 0 {
		t.Errorf("expected relocations, got 0")
	}

	if len(loaded.InitArray) == 0 {
		t.Errorf("expected init_array entries, got 0")
	} else if loaded.InitArray[0] != 0x1c0 {
		t.Errorf("expected init_array[0] == 0x1c0, got 0x%x", loaded.InitArray[0])
	}

	mainSym, ok := loaded.SymbolByName["main"]
	if !ok {
		t.Errorf("expected main symbol in symtab")
	} else if mainSym.Address != 0x60 {
		t.Errorf("expected main address 0x60, got 0x%x", mainSym.Address)
	}
}

func TestPLTRelocations(t *testing.T) {
	elfPath := "../../hello_world.elf"
	loaded, err := elfloader.LoadELF(elfPath)
	if err != nil {
		t.Fatalf("failed to load elf: %v", err)
	}

	var jumpSlots []elfloader.Relocation
	for _, rel := range loaded.Relocations {
		if rel.Type == 7 { // R_X86_64_JUMP_SLOT
			jumpSlots = append(jumpSlots, rel)
		}
	}

	expected := []struct {
		sym     string
		pltAddr uint64
	}{
		{"sceKernelUsleep", 0xb0e40},
		{"sysconf", 0xb0e50},
		{"open", 0xb0e60},
		{"__error", 0xb0e70},
		{"mmap", 0xb0e80},
		{"sigprocmask", 0xb0e90},
	}

	for i, exp := range expected {
		if i >= len(jumpSlots) {
			t.Fatalf("fewer jump slots than expected: %d", len(jumpSlots))
		}
		if jumpSlots[i].SymName != exp.sym {
			t.Errorf("jumpSlot[%d] SymName: expected %q, got %q", i, exp.sym, jumpSlots[i].SymName)
		}
		if jumpSlots[i].PltAddr != exp.pltAddr {
			t.Errorf("jumpSlot[%d] PltAddr: expected 0x%x, got 0x%x", i, exp.pltAddr, jumpSlots[i].PltAddr)
		}
	}
}
