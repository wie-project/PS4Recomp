package ps4pkg

import (
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"
)

func TestShouldExtractPath(t *testing.T) {
	paths := []struct {
		rel  string
		exec bool
	}{
		{"eboot.bin", true},
		{"prx/akdelay.prx", true},
		{"sce_module/libc.prx", true},
		{"sce_sys/param.sfo", true},
		{"sce_sys/about/right.sprx", true},
		{"phoenix/content/paks/phoenix-ps4.pak", false},
		{"phoenix/content/paks/phoenix-ps4.utoc", false},
		{"phoenix/content/paks/phoenix-ps4.ucas", false},
		{"phoenix/content/movies/fmv/cin_credits/cin_credits.bk2", false},
		{"ue4commandline.txt", false},
		{"inviteicon.jpg", false},
		{"contentid.txt", false},
		{"sce_discmap.plt", false},
	}

	for _, p := range paths {
		if got := isExecutablePath(p.rel); got != p.exec {
			t.Errorf("isExecutablePath(%q)=%v, want %v", p.rel, got, p.exec)
		}
		if got := ShouldExtractPath(p.rel, ExtractOptions{Executables: true}); got != p.exec {
			t.Errorf("executables filter %q=%v, want %v", p.rel, got, p.exec)
		}
		if !ShouldExtractPath(p.rel, ExtractOptions{Resources: true}) {
			t.Errorf("resources filter skipped %q", p.rel)
		}
		if !ShouldExtractPath(p.rel, ExtractOptions{}) {
			t.Errorf("unfiltered extract skipped %q", p.rel)
		}
	}
}

func TestLivePKGListsGameResources(t *testing.T) {
	testDir := livePKGDir(t)
	type pkgFile struct {
		path string
		size int64
	}
	var pkgs []pkgFile
	err := filepath.Walk(testDir, func(path string, info os.FileInfo, err error) error {
		if err != nil || info == nil || info.IsDir() {
			return err
		}
		if strings.HasSuffix(strings.ToLower(info.Name()), ".pkg") {
			pkgs = append(pkgs, pkgFile{path: path, size: info.Size()})
		}
		return nil
	})
	if err != nil {
		t.Fatalf("walk: %v", err)
	}
	if len(pkgs) == 0 {
		t.Skip("no .pkg files under PS4RECOMP_PKG_DIR")
	}
	sort.Slice(pkgs, func(i, j int) bool { return pkgs[i].size > pkgs[j].size })

	var (
		pkgPath string
		files   []*PfsNode
	)
	for _, cand := range pkgs {
		pkg, err := Open(cand.path)
		if err != nil {
			continue
		}
		if pkg.PfsImageSize == 0 {
			_ = pkg.Close()
			continue
		}
		fs, err := pkg.OpenGameFS("")
		if err != nil {
			_ = pkg.Close()
			continue
		}
		all := fs.AllFiles()
		_ = pkg.Close()
		hasGame := false
		for _, f := range all {
			rel := strings.ToLower(strings.TrimPrefix(f.Path, "uroot/"))
			if rel == "eboot.bin" || strings.HasSuffix(rel, ".pak") {
				hasGame = true
				break
			}
		}
		if hasGame {
			pkgPath = cand.path
			files = all
			break
		}
	}
	if pkgPath == "" || len(files) == 0 {
		t.Skip("no PKG under PS4RECOMP_PKG_DIR contains eboot.bin or .pak")
	}

	var paks, movies, execs int
	for _, f := range files {
		rel := strings.TrimPrefix(f.Path, "uroot/")
		base := strings.ToLower(rel)
		switch {
		case strings.HasSuffix(base, ".pak") || strings.HasSuffix(base, ".utoc") || strings.HasSuffix(base, ".ucas"):
			paks++
			if ShouldExtractPath(rel, ExtractOptions{Executables: true}) {
				t.Errorf("pak %s classified as executable", rel)
			}
			if !ShouldExtractPath(rel, ExtractOptions{Resources: true}) {
				t.Errorf("pak %s excluded from --resources", rel)
			}
		case strings.HasSuffix(base, ".bk2"):
			movies++
		case isExecutablePath(rel):
			execs++
		}
	}
	t.Logf("PFS files=%d executables=%d paks/utoc/ucas=%d movies=%d pkg=%s",
		len(files), execs, paks, movies, pkgPath)
	if execs == 0 {
		t.Error("expected at least one executable path in inner PFS")
	}
	if paks == 0 && movies == 0 {
		t.Log("this package has no packed assets; filter still applied to executables")
	}
}
