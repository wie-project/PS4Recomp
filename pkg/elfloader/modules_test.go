package elfloader

import (
	"encoding/binary"
	"os"
	"path/filepath"
	"testing"
)

func TestMapGuestPath(t *testing.T) {
	app := "/tmp/game"
	if got := MapGuestPath("/app0/sce_module/libExample.prx", app); got != filepath.Join(app, "sce_module", "libExample.prx") {
		t.Fatalf("got %q", got)
	}
	if got := MapGuestPath("/app0", app); got != app {
		t.Fatalf("got %q", got)
	}
	if got := MapGuestPath("/data/save", app); got != "" {
		t.Fatalf("non-app0 absolute path must not map onto appDir, got %q", got)
	}
	if got := MapGuestPath("sce_module/libExample.prx", app); got != filepath.Join(app, "sce_module", "libExample.prx") {
		t.Fatalf("got %q", got)
	}
}

func TestReferencedModuleNames(t *testing.T) {
	img := make([]byte, 64)
	copy(img[8:], []byte("/app0/sce_module/libExample.prx\x00"))
	copy(img[40:], []byte("libc.prx\x00"))
	names := ReferencedModuleNames(img)
	if len(names) != 1 || names[0] != "/app0/sce_module/libExample.prx" {
		t.Fatalf("names=%v", names)
	}
}

func TestDiscoverResolvesApp0(t *testing.T) {
	dir := t.TempDir()
	modDir := filepath.Join(dir, "sce_module")
	if err := os.Mkdir(modDir, 0o755); err != nil {
		t.Fatal(err)
	}
	prx := filepath.Join(modDir, "libExample.prx")
	if err := os.WriteFile(prx, []byte("not-an-elf"), 0o644); err != nil {
		t.Fatal(err)
	}
	img := append([]byte{0, 0, 0, 0}, []byte("/app0/sce_module/libExample.prx\x00")...)
	refs := DiscoverCompanionModules("", dir, img)
	if len(refs) != 1 {
		t.Fatalf("expected 1 module, got %+v", refs)
	}
	if filepath.Base(refs[0].Path) != "libExample.prx" {
		t.Fatalf("path=%s", refs[0].Path)
	}
}

func TestRetailModuleNotStub(t *testing.T) {
	dir := t.TempDir()
	libcStub := filepath.Join(dir, "libc.prx")
	if err := os.WriteFile(libcStub, make([]byte, 1024), 0o644); err != nil {
		t.Fatal(err)
	}
	if !IsSystemStubModule(libcStub) {
		t.Fatalf("expected %s (< 64KB) to be a system stub", libcStub)
	}

	libcPath := filepath.Join(dir, "retail", "libc.prx")
	if err := os.MkdirAll(filepath.Dir(libcPath), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(libcPath, make([]byte, 128*1024), 0o644); err != nil {
		t.Fatal(err)
	}
	if IsSystemStubModule(libcPath) {
		t.Fatalf("expected %s (> 64KB) to NOT be treated as a system stub", libcPath)
	}
}

func TestDiscoverPrxDir(t *testing.T) {
	dir := t.TempDir()
	prxDir := filepath.Join(dir, "prx")
	if err := os.Mkdir(prxDir, 0o755); err != nil {
		t.Fatal(err)
	}
	plugin := filepath.Join(prxDir, "akplugin.prx")
	if err := os.WriteFile(plugin, []byte("stub"), 0o644); err != nil {
		t.Fatal(err)
	}
	refs := DiscoverCompanionModules("", dir, nil)
	found := false
	for _, ref := range refs {
		if filepath.Base(ref.Path) == "akplugin.prx" {
			found = true
			break
		}
	}
	if !found {
		t.Fatalf("expected akplugin.prx from prx directory to be discovered, got %+v", refs)
	}
}

func TestApplyBiasCanary(t *testing.T) {
	img := make([]byte, 0x10000)
	const origCanaryAddr = 0x5000
	const gotOffset = 0x2000
	const canaryVal = uint64(0x595e9fbd94fda766)
	binary.LittleEndian.PutUint64(img[origCanaryAddr:origCanaryAddr+8], canaryVal)
	binary.LittleEndian.PutUint64(img[gotOffset:gotOffset+8], origCanaryAddr)

	loaded := &LoadedELF{
		MinVAddr:    0x1000,
		MaxVAddr:    0x6000,
		CanaryAddr:  origCanaryAddr,
		MemoryImage: img,
		Relocations: []Relocation{
			{Offset: gotOffset, Type: R_X86_64_GLOB_DAT, SymName: "f7uOxY9mM1U#D#D"},
		},
	}

	const delta = uint64(0x20000)
	if err := loaded.ApplyBias(delta); err != nil {
		t.Fatalf("ApplyBias failed: %v", err)
	}

	if loaded.CanaryAddr != origCanaryAddr+delta {
		t.Fatalf("expected CanaryAddr 0x%x, got 0x%x", origCanaryAddr+delta, loaded.CanaryAddr)
	}

	newGotOffset := gotOffset + delta
	gotPtr := binary.LittleEndian.Uint64(loaded.MemoryImage[newGotOffset : newGotOffset+8])
	if gotPtr != loaded.CanaryAddr {
		t.Fatalf("expected GOT entry to point to shifted canary 0x%x, got 0x%x", loaded.CanaryAddr, gotPtr)
	}
}

func TestMergeImagesCanary(t *testing.T) {
	const dstCanaryAddr = 0x8000
	const canaryVal = uint64(0x595e9fbd94fda766)

	dstImg := make([]byte, 0x9000)
	binary.LittleEndian.PutUint64(dstImg[dstCanaryAddr:dstCanaryAddr+8], canaryVal)
	dst := &LoadedELF{
		FileName:    "main",
		MinVAddr:    0x1000,
		MaxVAddr:    0x9000,
		CanaryAddr:  dstCanaryAddr,
		MemoryImage: dstImg,
	}

	const srcCanaryAddr = 0x15000
	const srcGotOffset = 0x12000
	srcImg := make([]byte, 0x16000)
	binary.LittleEndian.PutUint64(srcImg[srcCanaryAddr:srcCanaryAddr+8], canaryVal)
	binary.LittleEndian.PutUint64(srcImg[srcGotOffset:srcGotOffset+8], srcCanaryAddr)
	src := &LoadedELF{
		FileName:    "companion.prx",
		MinVAddr:    0x10000,
		MaxVAddr:    0x16000,
		CanaryAddr:  srcCanaryAddr,
		MemoryImage: srcImg,
		Relocations: []Relocation{
			{Offset: srcGotOffset, Type: R_X86_64_GLOB_DAT, SymName: "__stack_chk_guard"},
		},
	}

	if err := MergeImages(dst, src); err != nil {
		t.Fatalf("MergeImages failed: %v", err)
	}

	if dst.CanaryAddr != dstCanaryAddr {
		t.Fatalf("expected dst CanaryAddr 0x%x, got 0x%x", dstCanaryAddr, dst.CanaryAddr)
	}

	gotPtr := binary.LittleEndian.Uint64(dst.MemoryImage[srcGotOffset : srcGotOffset+8])
	if gotPtr != dstCanaryAddr {
		t.Fatalf("expected merged GOT entry to point to process canary 0x%x, got 0x%x", dstCanaryAddr, gotPtr)
	}
}

