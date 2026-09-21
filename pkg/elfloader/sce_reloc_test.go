package elfloader

import (
	"os"
	"testing"
)

func TestPRXJumpSlotMatchesELF(t *testing.T) {
	prxPath := "../../tools/OpenOrbis/PS4Toolchain/samples/using_library/sce_module/libExample.prx"
	elfPath := "../../tools/OpenOrbis/PS4Toolchain/samples/library_example/library_example/x64/Debug/library_example.elf"
	if _, err := os.Stat(prxPath); err != nil {
		t.Skip("libExample.prx not present")
	}
	if _, err := os.Stat(elfPath); err != nil {
		t.Skip("library_example.elf not present")
	}

	prx, err := LoadELF(prxPath)
	if err != nil {
		t.Fatal(err)
	}
	plain, err := LoadELF(elfPath)
	if err != nil {
		t.Fatal(err)
	}

	elfByOff := map[uint64]string{}
	for _, rel := range plain.Relocations {
		if rel.Type == R_X86_64_JUMP_SLOT {
			elfByOff[rel.Offset] = rel.SymName
		}
	}
	prxByOff := map[uint64]string{}
	for _, rel := range prx.Relocations {
		if rel.Type == R_X86_64_JUMP_SLOT {
			prxByOff[rel.Offset] = NIDPrefix(rel.SymName)
		}
	}

	lockOff := uint64(0x1100b0)
	if elfByOff[lockOff] != "pthread_mutex_lock" {
		t.Fatalf("ELF GOT 0x%x = %q", lockOff, elfByOff[lockOff])
	}
	wantNID := CalculateNID("pthread_mutex_lock")
	if prxByOff[lockOff] != wantNID {
		t.Fatalf("PRX GOT 0x%x = %q want mutex_lock NID %s", lockOff, prxByOff[lockOff], wantNID)
	}
}
