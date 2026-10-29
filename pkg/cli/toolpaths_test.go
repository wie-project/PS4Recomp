// SPDX-License-Identifier: GPL-2.0-or-later

package cli

import (
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

func touch(t *testing.T, path string, mode os.FileMode) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte("x"), mode); err != nil {
		t.Fatal(err)
	}
}

func fakeLook(path string, err error) func(string) (string, error) {
	return func(string) (string, error) { return path, err }
}

func TestNinjaBundledWinsOverPath(t *testing.T) {
	kit := t.TempDir()
	touch(t, filepath.Join(kit, "bin", "ninja"), 0o755)
	if got := ninjaIn([]string{kit}, fakeLook("/usr/bin/ninja", nil)); got != filepath.Join(kit, "bin", "ninja") {
		t.Fatalf("got %q", got)
	}
}

func TestNinjaSiblingBinDir(t *testing.T) {
	kit := t.TempDir()
	touch(t, filepath.Join(kit, "bin", "ninja"), 0o755)
	exeDir := filepath.Join(kit, "libexec")
	if err := os.MkdirAll(exeDir, 0o755); err != nil {
		t.Fatal(err)
	}
	if got := ninjaIn([]string{exeDir}, fakeLook("", errors.New("no"))); got != filepath.Join(exeDir, "..", "bin", "ninja") {
		t.Fatalf("got %q", got)
	}
}

func TestNinjaFallsBackToPath(t *testing.T) {
	if got := ninjaIn([]string{t.TempDir()}, fakeLook("/usr/bin/ninja", nil)); got != "/usr/bin/ninja" {
		t.Fatalf("got %q", got)
	}
	if got := ninjaIn([]string{t.TempDir()}, fakeLook("", errors.New("no"))); got != "" {
		t.Fatalf("got %q, want empty", got)
	}
}

func TestNinjaIgnoresNonExecutable(t *testing.T) {
	kit := t.TempDir()
	touch(t, filepath.Join(kit, "bin", "ninja"), 0o644)
	if got := ninjaIn([]string{kit}, fakeLook("/usr/bin/ninja", nil)); got != "/usr/bin/ninja" {
		t.Fatalf("got %q", got)
	}
}

func noPkgConfig(...string) ([]byte, error) { return nil, errors.New("no pkg-config") }

func TestFreeTypeBundled(t *testing.T) {
	kit := t.TempDir()
	ft := filepath.Join(kit, "3rdparty", "freetype")
	touch(t, filepath.Join(ft, "include", "freetype2", "ft2build.h"), 0o644)
	touch(t, filepath.Join(ft, "lib", "libfreetype.a"), 0o644)
	touch(t, filepath.Join(ft, "lib", "libpng16.a"), 0o644)
	cflags, libs := freeTypeIn([]string{kit}, noPkgConfig, "/nonexistent")
	if want := []string{"-I" + ft + "/include/freetype2"}; !slices.Equal(cflags, want) {
		t.Fatalf("cflags %v, want %v", cflags, want)
	}
	want := []string{ft + "/lib/libfreetype.a", ft + "/lib/libpng16.a", "-lz", "-lbz2"}
	if !slices.Equal(libs, want) {
		t.Fatalf("libs %v, want %v", libs, want)
	}
}

func TestFreeTypeBundledWithoutPNG(t *testing.T) {
	kit := t.TempDir()
	ft := filepath.Join(kit, "3rdparty", "freetype")
	touch(t, filepath.Join(ft, "include", "freetype2", "ft2build.h"), 0o644)
	touch(t, filepath.Join(ft, "lib", "libfreetype.a"), 0o644)
	_, libs := freeTypeIn([]string{kit}, noPkgConfig, "/nonexistent")
	if want := []string{ft + "/lib/libfreetype.a", "-lz", "-lbz2"}; !slices.Equal(libs, want) {
		t.Fatalf("libs %v, want %v", libs, want)
	}
}

func TestFreeTypePkgConfig(t *testing.T) {
	pc := func(args ...string) ([]byte, error) {
		if slices.Contains(args, "--cflags") {
			return []byte("-I/x/include/freetype2 -I/x/include\n"), nil
		}
		return []byte("-L/x/lib -lfreetype\n"), nil
	}
	cflags, libs := freeTypeIn([]string{t.TempDir()}, pc, "/nonexistent")
	if !slices.Equal(cflags, []string{"-I/x/include/freetype2", "-I/x/include"}) || !slices.Equal(libs, []string{"-L/x/lib", "-lfreetype"}) {
		t.Fatalf("got %v %v", cflags, libs)
	}
}

func TestFreeTypeHomebrewAndNone(t *testing.T) {
	brew := t.TempDir()
	if err := os.MkdirAll(filepath.Join(brew, "include", "freetype2"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(brew, "lib"), 0o755); err != nil {
		t.Fatal(err)
	}
	cflags, libs := freeTypeIn(nil, noPkgConfig, brew)
	if !slices.Equal(cflags, []string{"-I" + brew + "/include/freetype2"}) || !slices.Equal(libs, []string{"-L" + brew + "/lib", "-lfreetype"}) {
		t.Fatalf("got %v %v", cflags, libs)
	}
	cflags, libs = freeTypeIn(nil, noPkgConfig, "/nonexistent")
	if cflags != nil || libs != nil {
		t.Fatalf("got %v %v, want nil", cflags, libs)
	}
}

func TestShellQuote(t *testing.T) {
	for in, want := range map[string]string{
		"-O3":                     "-O3",
		"-I/w/3rdparty/ft":        "-I/w/3rdparty/ft",
		"-I/A/PS4 Recomp.app/x":   "'-I/A/PS4 Recomp.app/x'",
		"it's":                    `'it'\''s'`,
		"":                        "''",
		"$HOME":                   "'$HOME'",
	} {
		if got := shellQuote(in); got != want {
			t.Errorf("shellQuote(%q) = %q, want %q", in, got, want)
		}
	}
}

// Paths may contain a space ("PS4 Recomp.app"): build.ninja must hand each of them to the
// shell as one argument, and escape them in build lines.
func TestNinjaBuildPathsWithSpace(t *testing.T) {
	root := filepath.Join(t.TempDir(), "PS4 Recomp.app")
	outDir := filepath.Join(root, "out dir")
	inc := filepath.Join(root, "engine", "3rdparty", "it's lib")
	src := filepath.Join(outDir, "src a", "x.c")
	touch(t, src, 0o644)
	if err := generateNinjaBuild([]string{src}, outDir, []string{inc}, filepath.Join(outDir, "ps4 app"), "2", false); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(filepath.Join(outDir, "build.ninja"))
	if err != nil {
		t.Fatal(err)
	}
	text := string(data)
	var cflags string
	for _, line := range strings.Split(text, "\n") {
		if v, ok := strings.CutPrefix(line, "cflags = "); ok {
			cflags = v
		}
	}
	if cflags == "" {
		t.Fatalf("no cflags in build.ninja:\n%s", text)
	}
	// What /bin/sh makes of the expanded variable, one argument per line.
	out, err := exec.Command("/bin/sh", "-c", "printf '%s\\n' "+strings.ReplaceAll(cflags, "$$", "$")).Output()
	if err != nil {
		t.Fatal(err)
	}
	if args := strings.Split(strings.TrimSuffix(string(out), "\n"), "\n"); !slices.Contains(args, "-I"+inc) {
		t.Fatalf("shell arguments %q lack %q", args, "-I"+inc)
	}
	for _, want := range []string{"build src$ a/x.o: compile_c src$ a/x.c\n", "build ps4$ app: link_bin src$ a/x.o", "default ps4$ app\n"} {
		if !strings.Contains(text, want) {
			t.Errorf("build.ninja lacks %q:\n%s", want, text)
		}
	}
}
