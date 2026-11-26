// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import (
	"os/exec"
	"path/filepath"
	"testing"
)

// TestZlib runs testdata/zlib/main.c against libSceZlib/ps4_zlib.cpp: the order of the state
// and argument checks, request numbering, and inflate results (complete, short output buffer,
// corrupt stream) through the worker thread.
func TestZlib(t *testing.T) {
	for _, tool := range []string{"clang", "clang++"} {
		if _, err := exec.LookPath(tool); err != nil {
			t.Skipf("%s not found", tool)
		}
	}
	dir := t.TempDir()
	obj := filepath.Join(dir, "ps4_zlib.o")
	bin := filepath.Join(dir, "zlib")
	common := []string{"-O2", "-Wall", "-Wextra", "-Werror", "-I../runtime/core"}
	build := [][]string{
		append([]string{"clang++", "-std=c++20", "-c", "../runtime/modules/libSceZlib/ps4_zlib.cpp",
			"-o", obj}, common...),
		append([]string{"clang", "-std=gnu11", "-I../runtime/modules/libSceZlib",
			filepath.Join("testdata", "zlib", "main.c"), obj, "-lz", "-lc++", "-o", bin}, common...),
	}
	for _, args := range build {
		if out, err := exec.Command(args[0], args[1:]...).CombinedOutput(); err != nil {
			t.Fatalf("%v: %v\n%s", args, err, out)
		}
	}
	runHostCases(t, bin, []string{"order", "inflate"})
}
