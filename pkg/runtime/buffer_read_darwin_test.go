// SPDX-License-Identifier: GPL-2.0-or-later

package recompruntime

import (
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

// TestGcnBufferReadFlag: the renderer takes a compute shader that only writes CMASK/HTILE for a
// fast clear; one that also reads it (DRIVECLUB marks shadow map HTILE tiles expanded before
// sampling) must run instead, or the depth it just rendered is cleared.
func TestGcnBufferReadFlag(t *testing.T) {
	clang, err := exec.LookPath("clang")
	if err != nil {
		t.Skip("clang is required for the native regression")
	}
	source := metalTestSource(t, "buffer_read.c")
	root := filepath.Dir(filepath.Dir(filepath.Dir(source)))
	gcn := filepath.Join(root, "3rdparty", "gcn-recompiler")
	libs := []string{filepath.Join(gcn, "libgcn_recompiler.a"), filepath.Join(root, "3rdparty", "spirv-cross", "libspirv-cross.a")}
	for _, l := range libs {
		if _, err := os.Stat(l); err != nil {
			t.Skipf("run scripts/build.sh first: %v", err)
		}
	}
	bin := filepath.Join(t.TempDir(), "buffer-read")
	args := append([]string{"-O2", "-I" + filepath.Join(gcn, "include"), source}, libs...)
	cmd := exec.Command(clang, append(args, "-lc++", "-o", bin)...)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("build native regression: %v\n%s", err, out)
	}
	if out, err := exec.Command(bin).CombinedOutput(); err != nil {
		t.Fatalf("native regression: %v\n%s", err, out)
	}
}
