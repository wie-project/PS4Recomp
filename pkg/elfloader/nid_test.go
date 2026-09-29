package elfloader

import (
	"encoding/binary"
	"os"
	"testing"
)

func TestCalculateNID(t *testing.T) {
	got := CalculateNID("_Z19testLibraryFunctionPcmi")
	if got != "66ZEpOB7184" {
		t.Fatalf("NID=%q", got)
	}
	if NIDPrefix("66ZEpOB7184#A#A") != "66ZEpOB7184" {
		t.Fatal(NIDPrefix("66ZEpOB7184#A#A"))
	}
}

func TestDecodeSCEID(t *testing.T) {
	id, ok := decodeSCEID("A")
	if !ok || id != 0 {
		t.Fatalf("A -> %d %v want 0", id, ok)
	}
	id, ok = decodeSCEID("B")
	if !ok || id != 1 {
		t.Fatalf("B -> %d %v want 1", id, ok)
	}
	id, ok = decodeSCEID("d")
	if !ok || id != 29 {
		t.Fatalf("d -> %d %v want 29", id, ok)
	}
}

func TestHogwartsImportLibNames(t *testing.T) {
	path := "/Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition/extracted/eboot.bin"
	loaded, err := LoadELF(path)
	if err != nil {
		t.Skip(err)
	}
	if len(loaded.ImportLibs) == 0 {
		t.Fatal("expected DT_SCE_IMPORT_LIB names")
	}
	name := "WfAiBW8Wcek#d#c"
	lib := loaded.LibraryForNID(name)
	if lib == "d" || lib == "plaintext" {
		t.Fatalf("LibraryForNID(%s)=%q ImportLibs=%v", name, lib, loaded.ImportLibs)
	}
	if len(lib) < 4 {
		t.Fatalf("LibraryForNID(%s)=%q looks like an encoded id", name, lib)
	}
	t.Logf("%s -> %s (libs=%d)", name, lib, len(loaded.ImportLibs))
	if loaded.ProcParamAddr != 0x9800000 {
		t.Errorf("expected ProcParamAddr 0x9800000, got 0x%x", loaded.ProcParamAddr)
	}
	if loaded.CanaryAddr == 0 {
		t.Errorf("expected CanaryAddr to be allocated")
	}
}

func TestHogwartsCanaryResolution(t *testing.T) {
	ebootPath := "/Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition/extracted/eboot.bin"
	fiosPath := "/Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition/extracted/sce_module/libSceFios2.prx"
	main, err := LoadELF(ebootPath)
	if err != nil {
		t.Skip(err)
	}
	fios, err := LoadELF(fiosPath)
	if err != nil {
		t.Fatal(err)
	}
	nextBase := (main.MaxVAddr + 0xFFFF) &^ 0xFFFF
	if Overlaps(main, fios) {
		if err := fios.ApplyBias(nextBase - fios.MinVAddr); err != nil {
			t.Fatal(err)
		}
	}
	if err := MergeImages(main, fios); err != nil {
		t.Fatal(err)
	}
	var canaryRelocOffset uint64
	for _, rel := range fios.Relocations {
		canon, _ := ResolveNID(rel.SymName)
		if rel.SymName == "__stack_chk_guard" || canon == "__stack_chk_guard" {
			canaryRelocOffset = rel.Offset
			break
		}
	}
	if canaryRelocOffset == 0 {
		t.Fatal("no __stack_chk_guard relocation found in fios")
	}
	t.Logf("fios canaryRelocOffset = 0x%x", canaryRelocOffset)
	val := binary.LittleEndian.Uint64(main.MemoryImage[canaryRelocOffset : canaryRelocOffset+8])
	t.Logf("main.CanaryAddr = 0x%x", main.CanaryAddr)
	t.Logf("GOT entry at 0x%x = 0x%x", canaryRelocOffset, val)
	if val != main.CanaryAddr {
		t.Fatalf("expected 0x%x to point to main.CanaryAddr (0x%x), got 0x%x", canaryRelocOffset, main.CanaryAddr, val)
	}
	canaryInMem := binary.LittleEndian.Uint64(main.MemoryImage[val : val+8])
	t.Logf("Canary value at 0x%x = 0x%x", val, canaryInMem)
	if canaryInMem != 0x595e9fbd94fda766 {
		t.Fatalf("expected canary value 0x595e9fbd94fda766, got 0x%x", canaryInMem)
	}
}

func TestHogwartsFullCanaryInGuestImage(t *testing.T) {
	ebootPath := "/Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition/extracted/eboot.bin"
	appDir := "/Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition/extracted"
	main, err := LoadELF(ebootPath)
	if err != nil {
		t.Skip(err)
	}
	refs := DiscoverCompanionModules(ebootPath, appDir, main.MemoryImage)
	var allCompanionExports []Symbol
	nextBase := (main.MaxVAddr + 0xFFFF) &^ 0xFFFF
	for _, ref := range refs {
		prxELF, err := LoadELF(ref.Path)
		if err != nil {
			continue
		}
		if Overlaps(main, prxELF) {
			if err := prxELF.ApplyBias(nextBase - prxELF.MinVAddr); err != nil {
				t.Fatal(err)
			}
		}
		if err := MergeImages(main, prxELF); err != nil {
			t.Fatal(err)
		}
		nextBase = (main.MaxVAddr + 0xFFFF) &^ 0xFFFF
		allCompanionExports = append(allCompanionExports, prxELF.ExportedFunctions()...)
	}
	if err := ResolveModuleRelocations(main, allCompanionExports); err != nil {
		t.Fatalf("ResolveModuleRelocations failed: %v", err)
	}
	for _, rel := range main.Relocations {
		if rel.Offset >= 0xa320500 && rel.Offset <= 0xa320520 {
			canon, _ := ResolveNID(rel.SymName)
			t.Logf("Reloc at 0x%x: SymName=%s, Canon=%s, Type=%d", rel.Offset, rel.SymName, canon, rel.Type)
		}
	}
	val := binary.LittleEndian.Uint64(main.MemoryImage[0xa3203a8:0xa3203b0])
	t.Logf("Full merge: len=%d, main.CanaryAddr = 0x%x, [0xa3203a8] = 0x%x", len(main.MemoryImage), main.CanaryAddr, val)
	if val != main.CanaryAddr {
		t.Fatalf("expected 0xa3203a8 to point to 0x%x, got 0x%x", main.CanaryAddr, val)
	}
	if os.Getenv("UPDATE_GUEST_IMAGE") == "1" {
		target := "/Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition/build/guest_image.bin"
		if err := os.WriteFile(target, main.MemoryImage, 0o644); err != nil {
			t.Fatalf("failed to update guest_image.bin: %v", err)
		}
		t.Logf("Updated %s (%d bytes)", target, len(main.MemoryImage))
	}
}

func TestResolveNID(t *testing.T) {
	// Test known Sony system NIDs
	tests := []struct {
		sym  string
		want string
	}{
		{"zr094EQ39Ww#BA#+", "__cxa_pure_virtual"},
		{"9BcDykPmo1I#-#K", "__error"},
		{"E6ao34wPw+U", "stat"},
	}
	for _, tc := range tests {
		got, ok := ResolveNID(tc.sym)
		if !ok || got != tc.want {
			t.Errorf("ResolveNID(%q) = %q, %v; want %q, true", tc.sym, got, ok, tc.want)
		}
	}
}

