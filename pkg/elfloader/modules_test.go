package elfloader

import (
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
