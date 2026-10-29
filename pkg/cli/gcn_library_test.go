// SPDX-License-Identifier: GPL-2.0-or-later

package cli

import (
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

// gcnTree writes a small gcn-recompiler source tree (with the files the build ID globs take and
// some they do not) and a spirv-cross directory next to it; it returns the gcn-recompiler dir.
func gcnTree(t *testing.T) string {
	t.Helper()
	root := t.TempDir()
	dir := filepath.Join(root, "gcn-recompiler")
	files := map[string]string{
		"gcn-recompiler/src/api/a.cpp":                   "int a;",
		"gcn-recompiler/src/b.h":                         "#pragma once",
		"gcn-recompiler/src/notes.txt":                   "not an input",
		"gcn-recompiler/include/gcn_recompiler.h":        "/* api */",
		"gcn-recompiler/externals/sirit/src/s.cpp":       "int s;",
		"gcn-recompiler/externals/sirit/include/sirit.h": "/* sirit */",
		"gcn-recompiler/externals/fmt/src/format.cc":     "not an input",
		"spirv-cross/spirv_cross.cpp":                    "int c;",
		"spirv-cross/spirv_cross.hpp":                    "/* c */",
		"spirv-cross/libspirv-cross.a":                   "!<arch>\n",
		"spirv-cross/main.cpp":                           "not an input",
	}
	for p, c := range files {
		f := filepath.Join(root, p)
		if err := os.MkdirAll(filepath.Dir(f), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(f, []byte(c), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	return dir
}

func TestGcnBuildIDInputs(t *testing.T) {
	dir := gcnTree(t)
	got, err := gcnBuildIDInputs(dir)
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"../spirv-cross/libspirv-cross.a", "../spirv-cross/spirv_cross.cpp", "../spirv-cross/spirv_cross.hpp",
		"externals/sirit/include/sirit.h", "externals/sirit/src/s.cpp", "include/gcn_recompiler.h", "src/api/a.cpp",
		"src/b.h"}
	if strings.Join(got, ",") != strings.Join(want, ",") {
		t.Fatalf("inputs %v, want %v", got, want)
	}
}

// The Go build ID equals the one cmake/gcn_build_id.cmake writes for the same input list (CMake
// gets the absolute paths, unsorted, as CMakeLists.txt passes them).
func TestGcnBuildIDMatchesCMake(t *testing.T) {
	cmake, err := exec.LookPath("cmake")
	if err != nil {
		t.Skip("cmake not installed")
	}
	script, err := filepath.Abs("../../3rdparty/gcn-recompiler/cmake/gcn_build_id.cmake")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(script); err != nil {
		t.Skip("gcn-recompiler sources not present")
	}
	dir := gcnTree(t)
	files, err := gcnBuildIDInputs(dir)
	if err != nil {
		t.Fatal(err)
	}
	id, err := gcnBuildID(dir, files)
	if err != nil {
		t.Fatal(err)
	}
	var list strings.Builder
	for i := len(files) - 1; i >= 0; i-- { // reversed: the script sorts
		list.WriteString(dir + "/" + files[i] + "\n")
	}
	tmp := t.TempDir()
	listPath, outPath := filepath.Join(tmp, "inputs.txt"), filepath.Join(tmp, "gcn_build_id.h")
	if err := os.WriteFile(listPath, []byte(list.String()), 0o644); err != nil {
		t.Fatal(err)
	}
	if out, err := exec.Command(cmake, "-DLIST="+listPath, "-DOUT="+outPath, "-P", script).CombinedOutput(); err != nil {
		t.Fatalf("cmake: %v: %s", err, out)
	}
	h, err := os.ReadFile(outPath)
	if err != nil {
		t.Fatal(err)
	}
	m := regexp.MustCompile(`GCN_BUILD_ID "([0-9a-f]{32})"`).FindSubmatch(h)
	if m == nil {
		t.Fatalf("no GCN_BUILD_ID in %q", h)
	}
	if string(m[1]) != id {
		t.Fatalf("build ID %s, cmake/gcn_build_id.cmake writes %s", id, m[1])
	}
}

func TestCheckGcnLibrary(t *testing.T) {
	dir := gcnTree(t)
	files, err := gcnBuildIDInputs(dir)
	if err != nil {
		t.Fatal(err)
	}
	id, err := gcnBuildID(dir, files)
	if err != nil {
		t.Fatal(err)
	}
	lib := filepath.Join(dir, "libgcn_recompiler.a")
	write := func(s string) {
		t.Helper()
		if err := os.WriteFile(lib, []byte(s), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	// the shader cache ID string among other strings (a longer hex run before "-f" is not one)
	write("!<arch>\x00abc0123456789abcdef0123456789abcdef0123-f9\x00" + id + "-f3\x00tail")
	if err := checkGcnLibrary(lib); err != nil {
		t.Fatalf("library of these sources refused: %v", err)
	}
	other := strings.Repeat("0", 32)
	write("!<arch>\x00" + other + "-f3\x00")
	if err := checkGcnLibrary(lib); err == nil || !strings.Contains(err.Error(), "built from other sources") ||
		!strings.Contains(err.Error(), other) || !strings.Contains(err.Error(), id) {
		t.Fatalf("library of other sources: %v", err)
	}
	write("!<arch>\x00no build id here\x00")
	if err := checkGcnLibrary(lib); err == nil || !strings.Contains(err.Error(), "has no build ID") {
		t.Fatalf("library without a build ID: %v", err)
	}
	// a change of one input changes the ID: the library is refused
	write("!<arch>\x00" + id + "-f3\x00")
	if err := os.WriteFile(filepath.Join(dir, "src", "b.h"), []byte("#pragma once\n// changed"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := checkGcnLibrary(lib); err == nil {
		t.Fatal("library older than a changed source accepted")
	}
	// a packaged library (no sources next to it) is not checked
	if err := os.RemoveAll(filepath.Join(dir, "src")); err != nil {
		t.Fatal(err)
	}
	if err := checkGcnLibrary(lib); err != nil {
		t.Fatalf("packaged library: %v", err)
	}
}

// The game's link lists the static libraries as implicit inputs (a library copied in after a
// rebuild relinks the game; before, the game kept the library it was first linked with), with
// ninja's escapes.
func TestNinjaLinkBuildLibraries(t *testing.T) {
	got := ninjaLinkBuild("ps4_app", []string{"a.o", "dir b/c.o"},
		[]string{"/w/3rdparty/gcn-recompiler/libgcn_recompiler.a", "/v/x:y/libspirv-cross.a"})
	want := "\nbuild ps4_app: link_bin a.o dir$ b/c.o | /w/3rdparty/gcn-recompiler/libgcn_recompiler.a " +
		"/v/x$:y/libspirv-cross.a\n"
	if got != want {
		t.Fatalf("link build line %q, want %q", got, want)
	}
	if got := ninjaLinkBuild("ps4_app", []string{"a.o"}, nil); got != "\nbuild ps4_app: link_bin a.o\n" {
		t.Fatalf("without libraries: %q", got)
	}
}

// A gcn-recompiler dir reached through a symlink: "../spirv-cross" is the one next to the real
// dir (the OS resolves ".." through the link; CMake globs it so), not the one next to the link.
func TestGcnBuildIDThroughSymlink(t *testing.T) {
	real := gcnTree(t)
	link := filepath.Join(t.TempDir(), "w")
	if err := os.MkdirAll(filepath.Join(link, "spirv-cross"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(link, "spirv-cross", "spirv_cross.cpp"), []byte("decoy"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(real, filepath.Join(link, "gcn-recompiler")); err != nil {
		t.Fatal(err)
	}
	id := func(dir string) string {
		t.Helper()
		files, err := gcnBuildIDInputs(dir)
		if err != nil {
			t.Fatal(err)
		}
		s, err := gcnBuildID(dir, files)
		if err != nil {
			t.Fatal(err)
		}
		return s
	}
	if a, b := id(real), id(filepath.Join(link, "gcn-recompiler")); a != b {
		t.Fatalf("build ID through the symlink %s, of the real dir %s", b, a)
	}
}

// A missing include/ (or src/) dir is an empty glob for CMake, not an error.
func TestGcnBuildIDInputsMissingDirs(t *testing.T) {
	dir := gcnTree(t)
	if err := os.RemoveAll(filepath.Join(dir, "include")); err != nil {
		t.Fatal(err)
	}
	got, err := gcnBuildIDInputs(dir)
	if err != nil {
		t.Fatalf("without include/: %v", err)
	}
	for _, f := range got {
		if strings.HasPrefix(f, "include/") {
			t.Fatalf("include/ input %s listed", f)
		}
	}
	if err := os.RemoveAll(filepath.Join(dir, "src")); err != nil {
		t.Fatal(err)
	}
	if _, err := gcnBuildIDInputs(dir); err != nil {
		t.Fatalf("without src/: %v", err)
	}
}

// The refusal says how to rebuild and copy the library, and that a GCN_SPIRV_CROSS_DIR override
// makes the check refuse.
func TestCheckGcnLibraryMessage(t *testing.T) {
	dir := gcnTree(t)
	lib := filepath.Join(dir, "libgcn_recompiler.a")
	if err := os.WriteFile(lib, []byte(strings.Repeat("0", 32)+"-f3\x00"), 0o644); err != nil {
		t.Fatal(err)
	}
	err := checkGcnLibrary(lib)
	if err == nil {
		t.Fatal("library of other sources accepted")
	}
	for _, want := range []string{"ninja -C <gcn-build>", "cmake --build <gcn-build>",
		"cp <gcn-build>/libgcn_recompiler.a " + dir + "/", "GCN_SPIRV_CROSS_DIR"} {
		if !strings.Contains(err.Error(), want) {
			t.Errorf("message %q does not contain %q", err, want)
		}
	}
}
