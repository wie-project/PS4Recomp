package cli

import (
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"

	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/sfo"
)

func TestParseArgsPatches(t *testing.T) {
	cfg, err := ParseArgs([]string{"game.elf", "--patches", "p.xml", "--patch", "Shadow Quality - 4096", "-patch=Bug Fix"})
	if err != nil {
		t.Fatal(err)
	}
	if cfg.PatchFile != "p.xml" || !slices.Equal(cfg.Patches, []string{"Shadow Quality - 4096", "Bug Fix"}) {
		t.Fatalf("patches %q %q", cfg.PatchFile, cfg.Patches)
	}
	if _, err := ParseArgs([]string{"game.elf", "--patch", "A"}); err == nil {
		t.Error("-patch without -patches accepted")
	}
	if _, err := ParseArgs([]string{"game.elf", "--patches", "p.xml"}); err == nil {
		t.Error("-patches without -patch accepted")
	}
}

// patchGame lays out an app dir with hello_world.elf, a param.sfo and a patch file holding one
// set per line.
func patchGame(t *testing.T, lines map[string]string) *Config {
	t.Helper()
	dir := t.TempDir()
	elfData, err := os.ReadFile("../../hello_world.elf")
	if err != nil {
		t.Fatal(err)
	}
	elfPath := filepath.Join(dir, "hello.elf")
	if err := os.WriteFile(elfPath, elfData, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(dir, "sce_sys"), 0o755); err != nil {
		t.Fatal(err)
	}
	param := sfo.Build([]sfo.Entry{{Key: "APP_VER", Str: "01.00"}, {Key: "TITLE_ID", Str: "CUSA00001"}})
	if err := os.WriteFile(filepath.Join(dir, "sce_sys", "param.sfo"), param, 0o644); err != nil {
		t.Fatal(err)
	}
	var b strings.Builder
	b.WriteString(`<Patch><TitleID><ID>CUSA00001</ID></TitleID>`)
	for name, line := range lines {
		fmt.Fprintf(&b, `<Metadata Name=%q AppVer="01.00" AppElf="hello.elf"><PatchList><Line %s/></PatchList></Metadata>`, name, line)
	}
	b.WriteString(`</Patch>`)
	xmlPath := filepath.Join(dir, "patches.xml")
	if err := os.WriteFile(xmlPath, []byte(b.String()), 0o644); err != nil {
		t.Fatal(err)
	}
	return &Config{ElfPath: elfPath, AppDir: dir, PatchFile: xmlPath}
}

func TestLoadPatchedELF(t *testing.T) {
	plain, err := elfloader.LoadELF("../../hello_world.elf")
	if err != nil {
		t.Fatal(err)
	}
	reloc := plain.Relocations[0].Offset
	cfg := patchGame(t, map[string]string{
		"Main":  `Type="bytes" Address="0x00400060" Value="c3"`,
		"Reloc": fmt.Sprintf(`Type="bytes32" Address="0x%x" Value="1"`, reloc+0x400000),
	})

	cfg.Patches = []string{"Main"}
	loaded, res, err := loadPatchedELF(cfg)
	if err != nil {
		t.Fatal(err)
	}
	if loaded.MemoryImage[0x60] != 0xc3 || plain.MemoryImage[0x60] == 0xc3 {
		t.Fatalf("main byte 0x%x (unpatched 0x%x)", loaded.MemoryImage[0x60], plain.MemoryImage[0x60])
	}
	if !slices.Equal(res.Names(), []string{"Main"}) || res.Target.TitleID != "CUSA00001" || res.Target.ElfName != "hello.elf" {
		t.Fatalf("result %+v", res)
	}

	cfg.Patches = []string{"Reloc"}
	if _, _, err := loadPatchedELF(cfg); err == nil || !strings.Contains(err.Error(), "overlaps the target of relocation") {
		t.Fatalf("patch over a relocation: %v", err)
	}

	cfg.Patches = []string{"Main"}
	if err := os.Remove(filepath.Join(cfg.AppDir, "sce_sys", "param.sfo")); err != nil {
		t.Fatal(err)
	}
	if _, _, err := loadPatchedELF(cfg); err == nil || !strings.Contains(err.Error(), "param.sfo") {
		t.Fatalf("missing param.sfo: %v", err)
	}

	cfg.PatchFile, cfg.Patches = "", nil
	_, res, err = loadPatchedELF(cfg)
	if err != nil || res != nil || !strings.Contains(res.Report(), "No patches applied.") {
		t.Fatalf("no patches: %v %+v", err, res)
	}
}
